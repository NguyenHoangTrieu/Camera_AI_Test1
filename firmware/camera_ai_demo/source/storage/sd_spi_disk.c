/*
 * sd_spi_disk.c - see sd_spi_disk.h.
 *
 * Two things live in this one file, same as the SDK's own (DSPI-based,
 * unusable on this chip - see sd_spi_disk.h) reference glue:
 *  1. An sdspi_host_t implementation (SDCARD_SPI_*) driving LPSPI1 in
 *     hardware - init/setFrequency/exchange/csActivePolarity, the 4
 *     callbacks middleware/sdmmc/sdspi/fsl_sdspi.c needs to talk SD-over-
 *     SPI without knowing which SPI peripheral is underneath.
 *  2. The 5 diskio.h functions (disk_initialize/status/read/write/ioctl)
 *     ff.c calls directly - single hardcoded physical drive 0 (this
 *     project only ever has the one card, see ffconf.h's FF_VOLUMES=1),
 *     no multi-backend dispatch needed.
 *
 * LPSPI1 is now a SHARED bus (see ../spi1_bus.h): the LCD
 * (source/display/lcd_spi_hw.c) and touch controller
 * (source/display/touch_xpt2046.c) ride the same SCK/MOSI/MISO pins with
 * their own manual GPIO chip-selects. The SD card is the only device that
 * still uses the peripheral's real hardware PCS0 (P0_27/D10) - SDSPI_Init()
 * needs to flip its active-polarity at runtime via
 * SDCARD_SPI_CsActivePolarity() (temporarily active-high) to emit the SD
 * card's power-up sequence with CS deasserted, then every real command
 * uses kLPSPI_MasterPcsContinuous so CS stays asserted across the whole
 * multi-exchange() command/response/data sequence, not just one exchange()
 * call - exactly the same pattern NXP's own DSPI reference glue uses; that
 * runtime-polarity trick only works through real PCS hardware, which is
 * why the SD card (unlike the LCD/touch) couldn't move to a plain GPIO CS.
 * Because the bus is shared, disk_read()/disk_write() below explicitly
 * reclaim the SD card's operating baud rate before every FatFs-triggered
 * transfer - the LCD or touch driver may have left the bus at a different
 * rate since the last SD access. SDSPI_Init() itself doesn't need this: it
 * already re-asserts the 400kHz identification speed unconditionally at
 * its own start (middleware/sdmmc/sdspi/fsl_sdspi.c), regardless of
 * whatever the bus was left at.
 *
 * CONFIRMED on real hardware (2026-08-25, live SWD register inspection -
 * see WORKLOG.md): if the card/wiring is bad in a way that makes every SPI
 * response byte come back wrong (not a genuine "card busy" response),
 * SDSPI_Init() can take a *practically* unbounded amount of time even
 * though every individual wait inside middleware/sdmmc/sdspi/fsl_sdspi.c
 * is technically bounded (SDSPI_TRANSFER_RETRY_TIMES=20000, hardcoded,
 * not overridable via macro) - those bounded waits are nested up to 2-3
 * levels deep in some SDSPI_Init() call paths, so worst case multiplies
 * out to minutes-to-hours, not seconds. SDCARD_SPI_Exchange() below
 * enforces its own short wall-clock deadline across the whole init
 * attempt (not per-transfer) so a bad card fails fast regardless of what
 * fsl_sdspi.c's own retry math would otherwise allow.
 */

#include "sd_spi_disk.h"
#include "ff.h" /* Must come before diskio.h - defines BYTE/UINT/LBA_t/... that diskio.h's prototypes need. */
#include "diskio.h"
#include "fsl_common.h" /* DWT, SystemCoreClock - see SDCARD_SPI_Exchange()'s deadline check below. */
#include "fsl_debug_console.h"
#include "fsl_lpspi.h"
#include "fsl_sdspi.h"
#include "spi1_bus.h"

/* Only needed directly in this file for SDCARD_SPI_CsActivePolarity()'s
 * LPSPI_SetAllPcsPolarity() call - everything else routes through
 * spi1_bus.h now (init/baud-rate/transfers), since those are shared with
 * the LCD/touch controller. Polarity is SD-only (real hardware PCS0), so
 * it isn't part of the shared-bus wrapper. */
#define SD_SPI_BASEADDR LPSPI1

/* Operating-speed cap for host->busBaudRate below, NOT the mandatory
 * 400kHz card-identification speed (SDMMC_CLOCK_400KHZ, set separately
 * inside SDSPI_Init() itself before this ever applies). fsl_sdspi.c's
 * SDSPI_Init() speeds up to min(SD_CLOCK_25MHZ, host->busBaudRate) right
 * after reading the card's CSD register - CONFIRMED on real hardware
 * (2026-08-25, see WORKLOG.md) that leaving busBaudRate at 400000 (a
 * copy-paste leftover from the identification-phase value) pins every
 * later transfer at 400kHz forever: a 150KB snapshot BMP measured
 * ~3.3 SECONDS to write. 8MHz is a conservative middle ground given this
 * exact card/shield/wiring combination needed a pull-up fix to work at
 * all (see BOARD_InitSdCardPins() in pin_mux.c) - not pushed to the
 * driver's 25MHz ceiling without first confirming that's stable here too. */
#define SD_SPI_OPERATING_BAUDRATE 8000000U

/* Real SD-over-SPI init normally completes in well under 1 second even on
 * slow cards - 2 seconds is generous headroom, not a tight budget. */
#define SD_SPI_INIT_TIMEOUT_MS 2000U

static sdspi_host_t s_host;
static sdspi_card_t s_card;
static bool s_cardReady = false;
static uint32_t s_initDeadlineCycle;
static bool s_initTimedOut;
/* True only while disk_initialize() -> SDSPI_Init() is actually running -
 * see SDCARD_SPI_Exchange()'s comment for why this matters. */
static bool s_initInProgress;

/* -------------------------------------------------------------------- */
/* sdspi_host_t callbacks - LPSPI1 hardware SPI, see the file comment.   */
/* -------------------------------------------------------------------- */

static void SDCARD_SPI_Init(void)
{
    /* Brings up LPSPI1 once (idempotent - a no-op if the LCD or touch
     * driver already did this, whichever runs first). Baseline config
     * (8 bits/frame, mode 0, MSB first) already matches what SD-over-SPI
     * needs; SDSPI_Init() immediately calls setFrequency() anyway, so the
     * shared module's own baseline baud rate doesn't matter here. */
    SPI1_BUS_Init();

    /* DWT is already enabled by AI_MODEL_Init() (see model_runner*.cpp),
     * which always runs before SNAPSHOT_Init() in main.c - but enable it
     * defensively here too so this file doesn't silently depend on that
     * ordering. Doesn't reset DWT->CYCCNT (that would disturb the AI
     * timing code's own measurements), just makes sure it's running. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    s_initDeadlineCycle = DWT->CYCCNT + (SystemCoreClock / 1000U) * SD_SPI_INIT_TIMEOUT_MS;
    s_initTimedOut       = false;
}

static status_t SDCARD_SPI_SetFrequency(uint32_t frequency)
{
    return (SPI1_BUS_SetBaudRate(frequency) == 0U) ? kStatus_Fail : kStatus_Success;
}

static status_t SDCARD_SPI_Exchange(uint8_t *in, uint8_t *out, uint32_t size)
{
    /* See the file-level comment: fsl_sdspi.c calls exchange() many times
     * per SDSPI_Init() attempt, and every one of its own retry loops
     * bails out immediately the moment exchange() itself reports failure
     * (never mind why) - so failing fast here, once, is what actually
     * bounds the whole init attempt to SD_SPI_INIT_TIMEOUT_MS, regardless
     * of how deep or how large fsl_sdspi.c's own internal retry counts
     * are. (int32_t) cast makes the comparison wrap-safe the same way the
     * DWT-based rate limit in snapshot.c is.
     *
     * BUG FOUND on real hardware (2026-08-25, see WORKLOG.md): this check
     * used to run unconditionally, not just gated by s_initInProgress -
     * exchange() is also called later by SDSPI_ReadBlocks()/WriteBlocks()
     * during normal file I/O, long after the boot-time init's deadline
     * had already passed, so the very first real read/write after boot
     * always failed instantly ("no valid response") even on a perfectly
     * healthy, already-mounted card. Gating on s_initInProgress confines
     * this deadline to disk_initialize()'s own SDSPI_Init() call only. */
    if (s_initInProgress &&
        (s_initTimedOut || ((int32_t)(DWT->CYCCNT - s_initDeadlineCycle) >= 0)))
    {
        if (!s_initTimedOut)
        {
            PRINTF("Snapshot: SD card init timed out after %ums (no valid response) - giving up.\r\n",
                   SD_SPI_INIT_TIMEOUT_MS);
        }
        s_initTimedOut = true;
        return kStatus_Fail;
    }

    return SPI1_BUS_TransferBlocking(in, out, size, kLPSPI_MasterPcs0 | kLPSPI_MasterPcsContinuous);
}

static void SDCARD_SPI_CsActivePolarity(sdspi_cs_active_polarity_t polarity)
{
    LPSPI_SetAllPcsPolarity(SD_SPI_BASEADDR, (polarity == kSDSPI_CsActivePolarityLow) ? kLPSPI_Pcs0ActiveLow : 0U);
}

/* -------------------------------------------------------------------- */
/* diskio.h - single physical drive 0, see the file comment.            */
/* -------------------------------------------------------------------- */

bool SDCARD_DISK_IsReady(void)
{
    return s_cardReady;
}

uint64_t SDCARD_DISK_GetCapacityBytes(void)
{
    return (uint64_t)s_card.blockCount * (uint64_t)s_card.blockSize;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0U)
    {
        return STA_NOINIT;
    }

    s_host.busBaudRate      = SD_SPI_OPERATING_BAUDRATE;
    s_host.setFrequency     = SDCARD_SPI_SetFrequency;
    s_host.exchange         = SDCARD_SPI_Exchange;
    s_host.init             = SDCARD_SPI_Init;
    s_host.csActivePolarity = SDCARD_SPI_CsActivePolarity;
    s_card.host             = &s_host;

    /* Dual-core RTOS build only - tight lock around just this one call,
     * NOT the whole SNAPSHOT_Init()/SNAPSHOT_OnFrame() sequence (see
     * spi1_bus.h's SPI1_BUS_Lock() comment, WORKLOG.md Stage 4 follow-up):
     * confirmed on real hardware that wrapping a wider scope (either from
     * the caller, or via a recursive mutex allowing nested locks at both
     * levels) makes SD card mount fail consistently and reproducibly at
     * the card's own ACMD41 handshake - survived a real power cycle, so
     * not a stuck-card issue, but root cause not otherwise pinned down.
     * MUST be the plain mutex, not SPI1_BUS_LockNoPreempt() - tried that
     * too in the Stage 5 follow-up (WORKLOG.md) on the theory that it
     * might also fix a separate SD write-corruption bug, and it broke
     * mount again (same "SD card init timed out" symptom, confirmed via a
     * real A/B test: reverting just this one call from LockNoPreempt()
     * back to Lock() restored mount immediately, nothing else changed).
     * Root cause of exactly why still not pinned down - same as the
     * original finding above - but confirmed and reproducible, so left
     * alone rather than guessed at further. */
    s_initInProgress = true;
#ifdef DUALCORE_RTOS
    SPI1_BUS_Lock();
#endif
    s_cardReady      = (SDSPI_Init(&s_card) == kStatus_Success);
#ifdef DUALCORE_RTOS
    SPI1_BUS_Unlock();
#endif
    s_initInProgress = false;
    return s_cardReady ? 0U : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0U)
    {
        return STA_NOINIT;
    }
    return s_cardReady ? 0U : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if ((pdrv != 0U) || !s_cardReady)
    {
        return RES_PARERR;
    }
    /* Reclaim the SD card's operating baud rate before this file-I/O
     * transfer - the LCD or touch driver may have used (and left at a
     * different rate) the shared bus since the last SD access. Unlike
     * disk_initialize()'s SDSPI_Init(), plain reads/writes never call
     * setFrequency() again on their own - see the file-header comment. */
#ifdef DUALCORE_RTOS
    SPI1_BUS_Lock(); /* See disk_initialize()'s comment above. */
#endif
    (void)SPI1_BUS_SetBaudRate(SD_SPI_OPERATING_BAUDRATE);
    DRESULT result = (SDSPI_ReadBlocks(&s_card, buff, sector, count) == kStatus_Success) ? RES_OK : RES_ERROR;
#ifdef DUALCORE_RTOS
    SPI1_BUS_Unlock();
#endif
    return result;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if ((pdrv != 0U) || !s_cardReady)
    {
        return RES_PARERR;
    }
#ifdef DUALCORE_RTOS
    SPI1_BUS_Lock(); /* See disk_initialize()'s comment above. */
#endif
    (void)SPI1_BUS_SetBaudRate(SD_SPI_OPERATING_BAUDRATE); /* See disk_read()'s comment above. */
    DRESULT result = (SDSPI_WriteBlocks(&s_card, (uint8_t *)buff, sector, count) == kStatus_Success) ? RES_OK : RES_ERROR;
#ifdef DUALCORE_RTOS
    SPI1_BUS_Unlock();
#endif
    return result;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if ((pdrv != 0U) || !s_cardReady)
    {
        return RES_PARERR;
    }

    switch (cmd)
    {
        case GET_SECTOR_COUNT:
            *(uint32_t *)buff = s_card.blockCount;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = (WORD)s_card.blockSize;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(uint32_t *)buff = s_card.csd.eraseSectorSize;
            return RES_OK;
        case CTRL_SYNC:
            return RES_OK;
        default:
            return RES_PARERR;
    }
}
