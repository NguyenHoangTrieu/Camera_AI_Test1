/*
 * lcd_spi_hw.c - see lcd_spi_hw.h
 *
 * Hardware LPSPI1 SPI (mode 0: CPOL=0/CPHA=0, MSB first) LCD driver for
 * the Arduino header's 2.4" SPI TFT module (ILI9341-family), sharing the
 * bus with the onboard microSD slot (source/storage/sd_spi_disk.c) and
 * touch controller (source/display/touch_xpt2046.c) - see spi1_bus.h for
 * the sharing contract. CS/DC/RST/BLK are plain GPIO; SCK/SDI/SDO ride the
 * LPSPI1 peripheral (muxed in pin_mux.c's BOARD_InitSdCardPins(), which
 * now brings up the whole shared bus, not just the SD card's own PCS0/D10
 * pin - see that function's comment).
 *
 * Same MIPI-DCS command set as the earlier bit-banged 8080-parallel and
 * SPI drivers - only the byte-transfer mechanism changes.
 *
 * CONFIRMED on real hardware (2026-09-04, see WORKLOG.md): boots, shows a
 * correctly oriented (no MV/rotation fix needed) camera-preview image with
 * no bus-sharing corruption from the microSD slot. BGR bit in MADCTL
 * needed flipping vs. the earlier parallel panel - see LCD_InitPanel()'s
 * comment below.
 *
 * fps history (full trail in WORKLOG.md's 2026-09-04 entries): measured
 * 2fps at 24MHz SPI against a ~19-20fps best-case math prediction -
 * fixed a missing kLPSPI_MasterPcsContinuous flag (see LCD_WriteByte()'s
 * comment) for 2->5fps, then a stale delay-register bug in
 * spi1_bus.c's SPI1_BUS_SetBaudRate() for 5->7fps - both real,
 * individually confirmed on real hardware (the second via a
 * self-directed SWD register read, not just serial timing). **7fps is
 * this file's current CONFIRMED-working state.** A chunk-size experiment
 * proved neither of those two fixes was the full remaining story: 153,600
 * individual byte-wise TX FIFO register pokes per frame (fsl_lpspi.c's
 * blocking-transfer design) is the remaining dominant cost, and an eDMA
 * rewrite (LCD_PushPixelsOpen() using spi1_bus.c's
 * SPI1_BUS_TransferBytesDMA() instead of SPI1_BUS_TransferBlocking()) was
 * ATTEMPTED to remove it, in two variants (16-bit SPI frames, then 8-bit
 * matching mcuxsdk's own tested lpspi/edma_b2b_transfer reference
 * example) - both hung on real hardware (CONFIRMED via live SWD register
 * reads: the TX eDMA channel completes, but the RX eDMA channel - whose
 * completion the SDK's LPSPI+eDMA driver waits on even for a TX-only
 * transfer - never signals done, root cause not found despite verifying
 * clock config/channel-mux IDs/NVIC enable were all correct). Reverted to
 * the CPU-polled path below rather than risk a non-functional display -
 * spi1_bus.c's SPI1_BUS_TransferBytesDMA() is left in place, unused, for
 * a future session to pick back up; see WORKLOG.md for the full
 * investigation and what's already been ruled out.
 */

#include "lcd_spi_hw.h"
#include "app.h"
#include "board.h"
#include "fsl_common.h" /* SystemCoreClock - used by SDK_DelayAtLeastUs() below. */
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_lpspi.h"
#include "spi1_bus.h"
#include <stdbool.h>

/* fps math (see WORKLOG.md's 2026-09-04 entries for the full derivation
 * and history): one 320x240 RGB565 frame is 153,600 bytes = 1,228,800
 * bits of raw pixel data - at ANY given SPI clock, the wire time alone is
 * 1,228,800 / clock_Hz seconds, a hard floor no amount of software
 * optimization can beat. A ~24fps target needs that under ~40ms, i.e.
 * >=~31Mbps just for the wire, which needs a >=~35-40MHz-class SPI clock
 * to leave any real headroom - not reachable at all from this bus's
 * original 12MHz source (6MHz max baud). Requesting 24MHz here
 * (hardware_init.c switched LPSPI1's source to FRO_HF/48MHz specifically
 * for this) raises the wire-time floor to a best-case ~51ms/frame
 * (~19-20fps) - but the CPU-polled path this file currently uses (see the
 * file-header comment's fps history - an eDMA rewrite meant to close that
 * gap was attempted and reverted after hanging on real hardware) still
 * has real per-byte software overhead on top of that floor, so actual
 * measured fps (7, confirmed) is well short of the 19-20fps best case.
 * Getting past ~24fps would need both that eDMA gap closed AND an even
 * higher SPI clock (e.g. routing LPSPI1 from PLL0/150MHz instead of
 * FRO_HF/48MHz) - deliberately NOT attempted here: this project's own
 * history has an abandoned LCD bus (J8 FlexIO, also PLL0-derived) that
 * hit unresolved signal-integrity noise on this same class of breadboard
 * wiring at a considerably LOWER effective rate than a 35-40MHz SPI clock
 * would need to be. 24MHz was judged the more defensible next step.
 * SPI1_BUS_SetBaudRate() doesn't fail if 24MHz isn't exactly achievable -
 * LPSPI_MasterSetBaudRate() just clamps to the nearest achievable
 * divisor and returns that. If the image comes back glitchy/noisy/torn
 * (this project's wiring is currently breadboard-based - see WORKLOG.md -
 * worse for signal integrity at higher SPI rates than a direct/soldered
 * connection), lower this back toward 2-6MHz first before suspecting
 * anything else. */
#ifndef LCD_SPI_BAUDRATE_HZ
#define LCD_SPI_BAUDRATE_HZ 24000000U
#endif

static void LCD_SetCSPin(bool set) {
  GPIO_PinWrite(DEMO_LCD_CS_GPIO, DEMO_LCD_CS_PIN, set ? 1U : 0U);
}

static void LCD_SetDCPin(bool set) {
  GPIO_PinWrite(DEMO_LCD_DC_GPIO, DEMO_LCD_DC_PIN, set ? 1U : 0U);
}

static void LCD_SetResetPin(bool set) {
  GPIO_PinWrite(DEMO_LCD_RST_GPIO, DEMO_LCD_RST_PIN, set ? 1U : 0U);
}

static void LCD_SetBacklight(bool on) {
  GPIO_PinWrite(DEMO_LCD_BLK_GPIO, DEMO_LCD_BLK_PIN, on ? 1U : 0U);
}

/* kLPSPI_MasterPcs1 is never muxed to a physical pin on this board (see
 * spi1_bus.h) - toggling it internally has no external effect, it's just
 * the "don't care" PCS value the transfer API requires. The real chip
 * select is DEMO_LCD_CS_GPIO/PIN, a plain GPIO bracketing every
 * transaction below.
 *
 * kLPSPI_MasterPcsContinuous matters here even though PCS1 is unrouted:
 * CONFIRMED on real hardware (2026-09-04, see WORKLOG.md) that without it,
 * fsl_lpspi.c's LPSPI_MasterTransferBlocking() treats every 8-bit frame as
 * its own PCS burst and pays the full PCS-to-SCK/SCK-to-PCS setup/hold
 * delay BETWEEN EVERY SINGLE BYTE, purely as part of the peripheral's
 * internal timing generator - independent of whether anything is
 * physically wired to the PCS pin. Measured: ~2fps for a 320x240 frame at
 * 24MHz SPI - the delay-per-byte overhead this flag removes (~2.5us/byte,
 * still calibrated to spi1_bus.c's throwaway 400kHz init baseline, since
 * SPI1_BUS_SetBaudRate() only updates the SCK divider, not those delay
 * fields) was completely swamping the actual bit-clock time
 * (~333ns/byte at 24MHz). sd_spi_disk.c's SDCARD_SPI_Exchange() already
 * passed this flag correctly for the real hardware PCS0 case - missed
 * here when this file was first written, since it seemed irrelevant for
 * an unrouted "don't care" PCS channel. It is NOT irrelevant: it still
 * controls the internal per-frame delay-insertion behavior. */
static void LCD_WriteByte(uint8_t value) {
  (void)SPI1_BUS_TransferBlocking(&value, NULL, 1U, kLPSPI_MasterPcs1 | kLPSPI_MasterPcsContinuous);
}

/* Reclaim the shared bus at the LCD's own rate, then assert CS - the bus
 * may have been left at a different rate (or PCS) by the microSD slot or
 * touch controller since this driver last used it. See spi1_bus.h. */
static void LCD_BeginTransaction(void) {
  (void)SPI1_BUS_SetBaudRate(LCD_SPI_BAUDRATE_HZ);
  LCD_SetCSPin(false);
}

/* Command/data helpers below assume CS is already asserted (low) by the
 * caller, so LCD_SetWindow() can keep CS asserted across multiple commands
 * and LCD_PushPixels() closes it. */
static void LCD_WriteCommandOpen(uint8_t command) {
  LCD_SetDCPin(false); /* DC low = command */
  LCD_WriteByte(command);
  LCD_SetDCPin(true);
}

static void LCD_WriteDataArrayOpen(const uint8_t *data, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    LCD_WriteByte(data[i]);
  }
}

/* Write one command byte with no data, in its own CS-bracketed transfer. */
static void LCD_WriteCommand(uint8_t command) {
  LCD_BeginTransaction();
  LCD_WriteCommandOpen(command);
  LCD_SetCSPin(true);
}

/* Write one command byte followed by 1-4 data bytes, in one CS-bracketed
 * transfer. */
static void LCD_WriteCommandData(uint8_t command, const uint8_t *data,
                                 uint32_t len) {
  LCD_BeginTransaction();
  LCD_WriteCommandOpen(command);
  LCD_WriteDataArrayOpen(data, len);
  LCD_SetCSPin(true);
}

static void LCD_InitGpioPins(void) {
  const gpio_pin_config_t outConfig = {.pinDirection = kGPIO_DigitalOutput,
                                       .outputLogic = 0};
  const gpio_pin_config_t idleHighConfig = {.pinDirection = kGPIO_DigitalOutput,
                                            .outputLogic = 1};

  GPIO_PinInit(DEMO_LCD_RST_GPIO, DEMO_LCD_RST_PIN, &idleHighConfig);
  GPIO_PinInit(DEMO_LCD_CS_GPIO, DEMO_LCD_CS_PIN, &idleHighConfig);
  GPIO_PinInit(DEMO_LCD_DC_GPIO, DEMO_LCD_DC_PIN, &outConfig);

  /* Backlight: init as output and turn on immediately. */
  GPIO_PinInit(DEMO_LCD_BLK_GPIO, DEMO_LCD_BLK_PIN, &outConfig);
  LCD_SetBacklight(true);
}

/* Generic MIPI-DCS init sequence, carried over unchanged from the earlier
 * bit-bang drivers - same command set works over hardware SPI. */
static void LCD_InitPanel(void) {
  LCD_SetResetPin(false);
  SDK_DelayAtLeastUs(20000, SystemCoreClock);
  LCD_SetResetPin(true);
  SDK_DelayAtLeastUs(150000, SystemCoreClock);

  LCD_WriteCommand(0x01U); /* Software reset */
  SDK_DelayAtLeastUs(20000, SystemCoreClock);

  LCD_WriteCommand(0x11U); /* Sleep out */
  SDK_DelayAtLeastUs(150000, SystemCoreClock);

  /* MADCTL: memory access control. MV=1 (row/column exchange) matches this
   * panel's native 240x320 GRAM to the camera's 320x240 landscape buffer.
   * BGR=1 (0x28) was confirmed correct on the earlier PARALLEL panel, but
   * CONFIRMED WRONG on this SPI panel on real hardware (2026-09-04): image
   * came out with a strong blue/cyan cast over the whole picture - the
   * classic symptom of the panel decoding incoming RGB565 pixel data in
   * the opposite channel order it's actually sent in. BGR=0 (0x20) fixes
   * it - this SPI panel's controller apparently wants RGB order, unlike
   * the old parallel one. See WORKLOG.md. */
  LCD_WriteCommandData(0x36U, (const uint8_t[]){0x20U}, 1U);

  LCD_WriteCommandData(0x3AU, (const uint8_t[]){0x55U},
                       1U); /* Pixel format: 16bpp RGB565 */

  LCD_WriteCommand(0x29U); /* Display ON */
  SDK_DelayAtLeastUs(50000, SystemCoreClock);
}

void LCD_Init(void) {
  PRINTF("LCD: hardware SPI (LPSPI1, shared bus) on the Arduino header\r\n");
  SPI1_BUS_Init();

  /* DIAGNOSTIC (2026-09-04, see WORKLOG.md): fps stayed far below the
   * ~19-20fps math predicted for 24MHz even after fixing the missing
   * kLPSPI_MasterPcsContinuous flag (2fps -> 5fps, not the expected jump).
   * Printing the actual achieved baud rate directly, instead of trusting
   * LCD_SPI_BAUDRATE_HZ was really reached - if this doesn't read close to
   * 24000000, the clock-source change in hardware_init.c isn't taking
   * effect the way its comment assumes. */
  uint32_t srcClockHz = SPI1_BUS_GetSourceClockFreq();
  uint32_t achievedHz = SPI1_BUS_SetBaudRate(LCD_SPI_BAUDRATE_HZ);
  PRINTF("LCD: SPI1 source clock = %u Hz, requested %u Hz, achieved %u Hz\r\n",
         srcClockHz, LCD_SPI_BAUDRATE_HZ, achievedHz);

  LCD_InitGpioPins();
#ifdef DUALCORE_RTOS
  /* Dual-core RTOS build only - see spi1_bus.h's SPI1_BUS_Lock() comment
   * (WORKLOG.md, Stage 4): a boot-time race against a concurrent
   * SNAPSHOT_Init()/disk_initialize() is possible too, just narrower and
   * one-shot rather than the repeated per-frame race LCD_DrawImage() has. */
  SPI1_BUS_Lock();
#endif
  LCD_InitPanel();
#ifdef DUALCORE_RTOS
  SPI1_BUS_Unlock();
#endif
}

void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  const uint8_t colBuf[4] = {(uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFFU),
                             (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFFU)};
  const uint8_t rowBuf[4] = {(uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFFU),
                             (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFFU)};

  LCD_BeginTransaction();
  LCD_WriteCommandOpen(0x2AU); /* Column address set */
  LCD_WriteDataArrayOpen(colBuf, sizeof(colBuf));
  LCD_WriteCommandOpen(0x2BU); /* Page (row) address set */
  LCD_WriteDataArrayOpen(rowBuf, sizeof(rowBuf));
  LCD_WriteCommandOpen(0x2CU); /* Memory write - following bytes are pixels */
  /* CS stays asserted - LCD_PushPixels() closes it. */
}

/* eDMA (SPI1_BUS_TransferBytesDMA(), spi1_bus.c) was ATTEMPTED here
 * (2026-09-04, see WORKLOG.md's "DMA" entries), hung on real hardware, and
 * was reverted to the CPU-polled path below as a stopgap - then, in a
 * later session the same day, ROOT-CAUSED AND FIXED: a live register trace
 * (SPI1_BUS_RunDmaDiagnostic(), spi1_bus.c) showed the RX eDMA channel
 * waiting forever because TCR.RXMSK was stuck at 1 (RX data masked/
 * discarded, never stored to the RX FIFO) - left over from THIS FILE's own
 * LCD_WriteByte()/command calls, which always pass rxData=NULL to
 * SPI1_BUS_TransferBlocking(), and fsl_lpspi.c's LPSPI_MasterTransferBlocking()
 * sets TCR.RXMSK=1 whenever rxData is NULL. LPSPI_MasterTransferPrepareEDMALite()
 * never clears that bit (only touches CONT/CONTC/BYSW/PCS), so the eDMA
 * pixel-push always inherited it, and the RX FIFO could never reach its
 * DMA watermark. Fixed in SPI1_BUS_TransferBytesDMA() (spi1_bus.c) -
 * confirmed via the same live register trace that the eDMA transfer now
 * completes normally. Back on eDMA here as a result. The panel wants
 * MSB-first bytes per pixel, but the RGB565 source buffer is native
 * (little-endian) uint16_t order, so the bytes are swapped into this
 * small static scratch buffer before each chunk's SPI call (unchanged from
 * the CPU-polled version - 8-bit frames still need this, matching
 * mcuxsdk's own tested lpspi/edma_b2b_transfer reference example).
 *
 * fps follow-up (2026-09-04, see WORKLOG.md): the first working version of
 * this function called SPI1_BUS_PrepareDMA()'s underlying setup on EVERY
 * chunk (19x/frame at this chunk size) and only measured 7fps -> 8fps -
 * far short of the ~19-20fps bit-clock ceiling. Measured per-chunk time
 * (~23.7ms) vs. the theoretical bit-clock time for one chunk (~2.7ms at
 * 24MHz) pointed at Prepare()'s own per-call cost (module disable/FIFO-
 * flush/re-enable) as the new dominant cost, the same *class* of problem
 * the CPU-polled path had earlier in this file. mcuxsdk's own reference
 * example (examples/_boards/frdmmcxn947/driver_examples/lpspi/edma_b2b_transfer)
 * calls its Prepare-equivalent ONCE and transfers repeatedly - moved to
 * that pattern here: SPI1_BUS_PrepareDMA() now runs once per
 * LCD_PushPixelsOpen() call, not once per chunk. */
#define LCD_SPI_CHUNK_PIXELS 4096U
static uint8_t s_pixelSwapBuf[LCD_SPI_CHUNK_PIXELS * 2U];

void LCD_PushPixelsOpen(const uint16_t *pixels, uint32_t count) {
  (void)SPI1_BUS_PrepareDMA(kLPSPI_MasterPcs1 | kLPSPI_MasterPcsContinuous);

  while (count > 0U) {
    uint32_t chunk = (count > LCD_SPI_CHUNK_PIXELS) ? LCD_SPI_CHUNK_PIXELS : count;

    for (uint32_t i = 0; i < chunk; i++) {
      s_pixelSwapBuf[2U * i]      = (uint8_t)(pixels[i] >> 8);
      s_pixelSwapBuf[2U * i + 1U] = (uint8_t)(pixels[i] & 0xFFU);
    }
    (void)SPI1_BUS_TransferBytesDMA(s_pixelSwapBuf, chunk * 2U);

    pixels += chunk;
    count -= chunk;
  }
}

void LCD_EndWindow(void) { LCD_SetCSPin(true); }

void LCD_PushPixels(const uint16_t *pixels, uint32_t count) {
  LCD_PushPixelsOpen(pixels, count);
  LCD_EndWindow();
}

/* Per-frame LCD push time was measured and confirmed stable at ~56.9ms
 * (near the ~51ms bit-clock floor for a 320x240 push at 24MHz) across
 * multiple earlier sessions - see WORKLOG.md. The DWT-based diagnostic
 * that used to live here (function-static counters, printed once/sec)
 * was REMOVED (2026-09-04, see WORKLOG.md's tearing-fix entry): once
 * main.c's camera-preview loop started calling
 * CAMERA_CAPTURE_Deinit()/Reinit() every frame (to fix a real tearing
 * bug), those specific static counters started reading back garbage
 * (billions of "frames", nonsense window durations) - narrowed down to
 * their memory landing directly adjacent to camera_capture.c's SmartDMA
 * parameter/stack statics (confirmed via `nm`), but the exact write that
 * corrupts them was NOT fully root-caused (doubling the SmartDMA stack
 * size didn't fix it either - see WORKLOG.md for what was ruled out).
 * Removed rather than ship a diagnostic that prints nonsense - the LCD
 * push mechanism itself is unchanged by the tearing fix, so the
 * previously-measured ~56.9ms/frame number is still the right one to
 * cite. main.c's own fps/wait-for-frame counters (plain stack locals, not
 * statics living in this danger zone) remained reliable throughout and
 * are the diagnostic to trust if this needs re-measuring. */
void LCD_DrawImage(uint16_t x0, uint16_t y0, uint16_t width, uint16_t height,
                   const uint16_t *pixels) {
#ifdef DUALCORE_RTOS
  /* Dual-core RTOS build only - see spi1_bus.h's SPI1_BUS_Lock() comment
   * (WORKLOG.md, Stage 4): this whole SetWindow+Push sequence must be
   * atomic against StorageTask's concurrent SD transactions on the same
   * physical bus. */
  SPI1_BUS_Lock();
#endif
  LCD_SetWindow(x0, y0, (uint16_t)(x0 + width - 1U),
                (uint16_t)(y0 + height - 1U));
  LCD_PushPixels(pixels, (uint32_t)width * height);
#ifdef DUALCORE_RTOS
  SPI1_BUS_Unlock();
#endif
}
