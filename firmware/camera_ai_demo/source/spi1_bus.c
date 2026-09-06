/*
 * spi1_bus.c - see spi1_bus.h.
 *
 * SPI1_BUS_TransferBytesDMA() (2026-09-04, see WORKLOG.md) was added
 * because even after fixing two real bugs in the CPU-polled path
 * (SPI1_BUS_TransferBlocking() - missing kLPSPI_MasterPcsContinuous, then
 * stale delay-scaler registers), fps was still capped well short of
 * target: fsl_lpspi.c's LPSPI_MasterTransferBlocking() pushes data ONE
 * BYTE AT A TIME into the TX FIFO (this bus uses 8-bit frames), so a
 * 320x240 frame costs 153,600 individual register accesses, each crossing
 * from the CPU's AHB domain into LPSPI1's own peripheral clock domain - a
 * bus-bridge latency cost, not a misconfiguration, so no amount of
 * further tuning of the blocking path fixes it. eDMA moves that
 * FIFO-feeding work into hardware instead. Initially hung on real
 * hardware in two variants (TX eDMA channel completed, RX eDMA channel -
 * whose completion the SDK's LPSPI+eDMA driver waits on even for a
 * TX-only transfer - never signaled done) - ROOT-CAUSED AND FIXED in a
 * later session the same day: LPSPI_MasterTransferBlocking() sets
 * TCR.RXMSK=1 whenever called with rxData==NULL (every LCD command byte
 * this shared bus ever writes), which masks/discards incoming SPI data at
 * the hardware level instead of storing it to the RX FIFO.
 * LPSPI_MasterTransferPrepareEDMALite() never clears that bit (only
 * touches CONT/CONTC/BYSW/PCS), so it silently inherited whatever RXMSK
 * was left at by the last blocking transfer - meaning the RX FIFO could
 * never reach its DMA watermark, so the RX eDMA channel waited forever.
 * See SPI1_BUS_TransferBytesDMA()'s own comment below for the fix and how
 * it was confirmed via a live register trace.
 */

#include "spi1_bus.h"
#include "fsl_clock.h"
#include "fsl_common.h" /* DWT, SystemCoreClock - SPI1_BUS_TransferPixelsDMA()'s completion-wait timeout. */
#include "fsl_edma.h"
#include "fsl_edma_soc.h" /* kDma0RequestMuxLpFlexcomm1Tx/Rx */
#include "fsl_lpspi.h"
#include "fsl_lpspi_edma.h"
#include <stdbool.h>

#ifdef DUALCORE_RTOS
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

static SemaphoreHandle_t s_busMutex;

/* Plain mutex, NOT recursive - deliberately reverted (WORKLOG.md, Stage 4
 * follow-up): switching to xSemaphoreCreateRecursiveMutex() while chasing
 * a separate write-failure bug caused SD card mount itself to start
 * failing consistently (SDSPI_Init returning
 * kStatus_SDSPI_SendOperationConditionFailed - the card's own ACMD41
 * handshake) - confirmed NOT a stuck-card/needs-power-cycle issue (failed
 * identically after a real power cycle) and confirmed tied to this
 * specific change (reverting it alone, with everything else unchanged,
 * restored mount success). Root cause of exactly why recursive-vs-plain
 * matters here not fully understood - not worth shipping a change whose
 * failure mode isn't understood, per this project's own "verify, don't
 * guess" standard. Structured to not need recursion at all instead: only
 * ONE call site ever holds this lock across a nested disk_*() call
 * (StorageTask's SNAPSHOT_OnFrame() wrapper in main_core1.c) - removed
 * the redundant inner locks in sd_spi_disk.c's disk_read()/disk_write()
 * (see those functions) rather than reaching for recursion again. */
void SPI1_BUS_CreateLock(void)
{
    s_busMutex = xSemaphoreCreateMutex();
    configASSERT(s_busMutex != NULL);
}

void SPI1_BUS_Lock(void)
{
    (void)xSemaphoreTake(s_busMutex, portMAX_DELAY);
}

void SPI1_BUS_Unlock(void)
{
    (void)xSemaphoreGive(s_busMutex);
}

/* See spi1_bus.h's comment - the plain mutex above only stops another TASK
 * from touching the bus, not the scheduler OR an ISR from preempting the
 * lock HOLDER mid-transaction.
 *
 * CONFIRMED on real hardware (WORKLOG.md, dual-core Stage 5 second
 * follow-up) that vTaskSuspendAll()/xTaskResumeAll() alone - which only
 * block TASK switches, not interrupt servicing - were NOT sufficient once
 * Stage 5 added the core0<->core1 MCMGR IPC round trip: the LCD went back
 * to showing torn images (user did a direct A/B against the
 * `spi_tft_change` single-core branch on the exact same hardware, which
 * stayed clean, definitively ruling out a hardware/wiring cause and
 * pointing back at this dual-core build specifically). Root-caused by
 * checking MAILBOX_IRQn's actual configured priority
 * (mcmgr_internal_core_api_mcxnx4x.c: `NVIC_SetPriority(MAILBOX_IRQn, 2)`
 * on core1) against this project's own `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`
 * (also 2, FreeRTOSConfig.h) - MCMGR's own mailbox interrupt (which
 * delivers core0's "result ready" doorbell, a real new interrupt source
 * Stage 5 introduced) sits exactly at the FreeRTOS-maskable threshold, so
 * it can fire and interrupt an LCD SPI transaction mid-stream at any time
 * - something `vTaskSuspendAll()` was never able to prevent, since it only
 * ever addressed task-level preemption, not this. `taskENTER_CRITICAL()`/
 * `taskEXIT_CRITICAL()` raise BASEPRI to that same threshold, which DOES
 * mask MAILBOX_IRQn (and also blocks PendSV, so no task switch can happen
 * either - this fully subsumes what vTaskSuspendAll() was providing,
 * hence dropping it here rather than layering both). Takes the mutex
 * first, before entering the critical section - correct order, since
 * xSemaphoreTake() can block/yield and must never be called from inside a
 * critical section. */
void SPI1_BUS_LockNoPreempt(void)
{
    SPI1_BUS_Lock();
    taskENTER_CRITICAL();
}

void SPI1_BUS_UnlockNoPreempt(void)
{
    taskEXIT_CRITICAL();
    SPI1_BUS_Unlock();
}
#endif

#define SPI1_BUS_BASEADDR LPSPI1
#define SPI1_BUS_CLK_FREQ CLOCK_GetLPFlexCommClkFreq(1u) /* FRO_HF/1 = 48MHz, see hardware_init.c's kFRO_HF_DIV_to_FLEXCOMM1 comment for why (was FRO12M/12MHz until the fps fix in WORKLOG.md's 2026-09-04 entry). */

/* DMA0 channels 0/1 - arbitrary but fixed choice, confirmed unused
 * elsewhere in this project (the camera capture path uses the separate
 * SmartDMA peripheral, not this general-purpose eDMA controller). Matches
 * mcuxsdk's own frdmmcxn947 lpspi/edma_b2b_transfer master example
 * (examples/_boards/frdmmcxn947/driver_examples/lpspi/edma_b2b_transfer),
 * which targets this exact LPSPI1 instance and confirmed the
 * kDma0RequestMuxLpFlexcomm1Tx/Rx request-source names and "channel mux"
 * (not separate DMAMUX peripheral) setup style this chip's eDMA4 uses. */
#define SPI1_BUS_DMA_BASEADDR   DMA0
#define SPI1_BUS_DMA_RX_CHANNEL 0U
#define SPI1_BUS_DMA_TX_CHANNEL 1U

/* eDMA's own hard requirement (see fsl_lpspi_edma.c's
 * LPSPI_MasterTransferPrepareEDMALite() assert) - CONFIRMED on real
 * hardware via a live SWD register read (2026-09-04, PARAM=0x00040303,
 * RXFIFO field decodes to 1<<3=8) that this chip's LPSPI1 has exactly the
 * minimum required depth, not more - if that ever needs re-verifying,
 * read LPSPI1_BASE+0x4 (PARAM) live: RXFIFO size = 1 << ((PARAM>>8)&0xFF). */

static bool s_spi1BusInitialized = false;

static edma_handle_t s_dmaRxHandle;
static edma_handle_t s_dmaTxHandle;
static lpspi_master_edma_handle_t s_lpspiDmaHandle;
static volatile bool s_dmaTransferDone = false;

static void SPI1_BUS_DmaCallback(LPSPI_Type *base, lpspi_master_edma_handle_t *handle, status_t status, void *userData)
{
    (void)base;
    (void)handle;
    (void)status;
    (void)userData;
    s_dmaTransferDone = true;
}

void SPI1_BUS_Init(void)
{
    if (s_spi1BusInitialized)
    {
        return;
    }

    lpspi_master_config_t masterConfig;
    LPSPI_MasterGetDefaultConfig(&masterConfig);
    /* baudRate here is a throwaway baseline - every real transfer
     * reclaims its own rate first via SPI1_BUS_SetBaudRate(). Defaults
     * already match what every device on this bus needs: 8 bits/frame,
     * mode 0 (CPOL=0/CPHA=0), MSB first. */
    masterConfig.baudRate = 400000U;
    masterConfig.whichPcs = kLPSPI_Pcs0;
    LPSPI_MasterInit(SPI1_BUS_BASEADDR, &masterConfig, SPI1_BUS_CLK_FREQ);

    edma_config_t edmaConfig;
    EDMA_GetDefaultConfig(&edmaConfig);
    EDMA_Init(SPI1_BUS_DMA_BASEADDR, &edmaConfig);

    EDMA_CreateHandle(&s_dmaRxHandle, SPI1_BUS_DMA_BASEADDR, SPI1_BUS_DMA_RX_CHANNEL);
    EDMA_CreateHandle(&s_dmaTxHandle, SPI1_BUS_DMA_BASEADDR, SPI1_BUS_DMA_TX_CHANNEL);
    EDMA_SetChannelMux(SPI1_BUS_DMA_BASEADDR, SPI1_BUS_DMA_RX_CHANNEL, kDma0RequestMuxLpFlexcomm1Rx);
    EDMA_SetChannelMux(SPI1_BUS_DMA_BASEADDR, SPI1_BUS_DMA_TX_CHANNEL, kDma0RequestMuxLpFlexcomm1Tx);

    LPSPI_MasterTransferCreateHandleEDMA(SPI1_BUS_BASEADDR, &s_lpspiDmaHandle, SPI1_BUS_DmaCallback, NULL,
                                         &s_dmaRxHandle, &s_dmaTxHandle);

    s_spi1BusInitialized = true;
}

uint32_t SPI1_BUS_SetBaudRate(uint32_t baudRate_Bps)
{
    uint32_t prescaler; /* out-only, LPSPI_MasterSetBaudRate() asserts this is non-NULL. */
    uint32_t actualBaud;

    /* LPSPI_MasterSetBaudRate() silently returns 0 (failure) unless the
     * peripheral is disabled first. */
    LPSPI_Enable(SPI1_BUS_BASEADDR, false);
    actualBaud = LPSPI_MasterSetBaudRate(SPI1_BUS_BASEADDR, baudRate_Bps, SPI1_BUS_CLK_FREQ, &prescaler);

    /* ROOT CAUSE of the LCD fps investigation (2026-09-04, confirmed via
     * self-directed SWD debug session, see WORKLOG.md): LPSPI_MasterSetBaudRate()
     * ONLY updates the SCK divider - it does NOT touch the PCS-to-SCK/
     * last-SCK-to-PCS/between-transfer delay registers. Those were baked
     * in once by SPI1_BUS_Init()'s LPSPI_MasterInit() call, computed from
     * a throwaway 400kHz baseline, and stored as a RAW CYCLE COUNT
     * relative to the source clock (fsl_lpspi.c's
     * LPSPI_MasterSetDelayTimes() math) - not automatically rescaled when
     * the SCK divider changes later. Proven on real hardware: quartering
     * LCD_SPI_CHUNK_PIXELS (75 -> 19 SPI1_BUS_TransferBlocking() calls per
     * frame) produced ZERO change in measured transfer time, ruling out
     * per-call overhead and pointing at a fixed PER-BYTE cost instead -
     * consistent with a ~1.25us stale "between transfer" delay applied to
     * every one of a 320x240 frame's 153,600 bytes (~192ms, matching the
     * measured ~200ms/frame almost exactly), regardless of chunking.
     * Recompute all three delays here, every time ANY device on this
     * shared bus changes the operating rate - fixes SD/LCD/touch all at
     * once, not just the LCD. */
    if (actualBaud != 0U)
    {
        uint32_t delayNs = (1000000000U / actualBaud) / 2U;
        (void)LPSPI_MasterSetDelayTimes(SPI1_BUS_BASEADDR, delayNs, kLPSPI_PcsToSck, SPI1_BUS_CLK_FREQ);
        (void)LPSPI_MasterSetDelayTimes(SPI1_BUS_BASEADDR, delayNs, kLPSPI_LastSckToPcs, SPI1_BUS_CLK_FREQ);
        (void)LPSPI_MasterSetDelayTimes(SPI1_BUS_BASEADDR, delayNs, kLPSPI_BetweenTransfer, SPI1_BUS_CLK_FREQ);
    }

    LPSPI_Enable(SPI1_BUS_BASEADDR, true);

    return actualBaud;
}

uint32_t SPI1_BUS_GetSourceClockFreq(void)
{
    return SPI1_BUS_CLK_FREQ;
}

status_t SPI1_BUS_TransferBlocking(const uint8_t *txData, uint8_t *rxData, uint32_t size, uint32_t pcs)
{
    lpspi_transfer_t transfer = {
        .txData      = txData,
        .rxData      = rxData,
        .dataSize    = size,
        .configFlags = pcs,
    };

    return LPSPI_MasterTransferBlocking(SPI1_BUS_BASEADDR, &transfer);
}

/* Generous, not a tight budget - even a full 320x240 frame (76,800
 * pixels) completes in low tens of ms over DMA at any baud rate this bus
 * has ever run at. Bounds the wait below in case a transfer never
 * completes (e.g. a future bug leaves the DMA request line stuck), same
 * "fail loud instead of hang forever" philosophy as SD_SPI_INIT_TIMEOUT_MS
 * in sd_spi_disk.c. */
#define SPI1_BUS_DMA_TIMEOUT_MS 200U

status_t SPI1_BUS_PrepareDMA(uint32_t pcs)
{
    /* 8-bit frames (the shared bus default, unchanged) - see spi1_bus.h's
     * comment for why a 16-bit-frame variant was tried and abandoned. */
    status_t status = LPSPI_MasterTransferPrepareEDMALite(SPI1_BUS_BASEADDR, &s_lpspiDmaHandle, pcs);

    /* ROOT CAUSE of the eDMA RX-channel-never-completes hang (2026-09-04,
     * see WORKLOG.md's eDMA entry and SPI1_BUS_RunDmaDiagnostic()'s live
     * register trace): LPSPI_MasterTransferBlocking() (fsl_lpspi.c) sets
     * TCR.RXMSK=1 whenever it's called with rxData==NULL - which is EVERY
     * call this shared bus ever makes for a write-only transfer (every LCD
     * command byte via LCD_WriteByte()). RXMSK=1 tells the hardware to
     * discard incoming data instead of storing it to the RX FIFO - and
     * nothing ever clears it back to 0 afterward. LPSPI_MasterTransferPrepareEDMALite()
     * (fsl_lpspi_edma.c) only touches CONT/CONTC/BYSW/PCS in its own TCR
     * write, so it silently INHERITS whatever RXMSK was left at by the
     * last blocking transfer - meaning the eDMA pixel-push always ran with
     * RX data masked, so the RX FIFO count could never reach its DMA
     * watermark, so the RX eDMA channel waited forever for a hardware
     * request that structurally could never fire (CONFIRMED via a live
     * register trace: FSR read rx=0 for the entire 200ms timeout window,
     * TCR read back 0x01280007 - bit 19/RXMSK set - the one time this was
     * actually checked). Clear both mask bits explicitly here: this
     * transfer always wants real (if discarded-by-the-caller) RX data to
     * actually land in the FIFO so eDMA can see it and signal completion. */
    if (status == kStatus_Success)
    {
        SPI1_BUS_BASEADDR->TCR &= ~(LPSPI_TCR_RXMSK_MASK | LPSPI_TCR_TXMSK_MASK);
    }

    return status;
}

status_t SPI1_BUS_TransferBytesDMA(const uint8_t *data, uint32_t size)
{
    /* Caller must have already called SPI1_BUS_PrepareDMA() - not
     * re-verified here (fsl_lpspi_edma.c's own reference usage pattern,
     * examples/_boards/frdmmcxn947/driver_examples/lpspi/edma_b2b_transfer,
     * calls Prepare() once and Transfer() repeatedly, so this mirrors
     * that). CONFIRMED on real hardware (2026-09-04, see WORKLOG.md) that
     * the eDMA handle's own state goes back to idle once each transfer's
     * completion callback fires, so back-to-back Transfer() calls after
     * one Prepare() are safe - this used to call Prepare() again on every
     * single chunk (19x/frame at the current chunk size), which re-did a
     * real, measured, per-call cost (module disable/flush/re-enable) that
     * turned out to dominate eDMA's own per-chunk time once the RXMSK hang
     * was fixed - hoisting Prepare() out to once/frame removes that. */
    lpspi_transfer_t transfer = {
        .txData      = data,
        .rxData      = NULL,
        .dataSize    = size,
        .configFlags = 0U, /* Unused by the *Lite variant - config already applied by Prepare(). */
    };

    s_dmaTransferDone = false;
    status_t status   = LPSPI_MasterTransferEDMALite(SPI1_BUS_BASEADDR, &s_lpspiDmaHandle, &transfer);
    if (status == kStatus_Success)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        uint32_t deadlineCycle = DWT->CYCCNT + (SystemCoreClock / 1000U) * SPI1_BUS_DMA_TIMEOUT_MS;

        while (!s_dmaTransferDone)
        {
            if ((int32_t)(DWT->CYCCNT - deadlineCycle) >= 0)
            {
                /* Reset the handle back to idle - without this, a single
                 * timeout would permanently wedge every future DMA
                 * transfer (CONFIRMED on real hardware, 2026-09-04, see
                 * WORKLOG.md: LPSPI_MasterTransferPrepareEDMALite()
                 * bails out immediately with kStatus_LPSPI_Busy once
                 * handle->state is stuck, so nothing after the first
                 * failure was even attempting a real transfer). */
                LPSPI_MasterTransferAbortEDMA(SPI1_BUS_BASEADDR, &s_lpspiDmaHandle);
                status = kStatus_Timeout;
                break;
            }
        }
    }

    return status;
}
