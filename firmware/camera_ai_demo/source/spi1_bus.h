/*
 * spi1_bus.h - shared hardware LPSPI1 bus, Arduino header D10..D13.
 *
 * SCK/MOSI/MISO (D13/D11/D12) are physically shared by three devices on
 * the 2.4" SPI TFT module: the microSD slot (source/storage/sd_spi_disk.c,
 * hardware PCS0 on D10), the LCD panel
 * (source/display/lcd_spi_hw.c), and the touch controller
 * (source/display/touch_xpt2046.c). Only the microSD slot uses the
 * peripheral's own hardware chip-select (PCS0/D10) - SDSPI_Init() needs to
 * flip its active polarity at runtime (LPSPI_SetAllPcsPolarity()), which
 * only works through real PCS hardware, not a plain GPIO. LCD and touch
 * instead each use a plain GPIO pin for CS, toggled manually around every
 * transfer, and pass kLPSPI_MasterPcs1 as their transfer's PCS flag - PCS1
 * is never muxed to a physical pin on this board (see pin_mux.c), so it's
 * a "don't care" value the transfer API requires but nothing external
 * ever observes.
 *
 * Because the bus is shared, baud rate is NOT a fixed one-time setting -
 * whichever device used the bus most recently may have left it at a
 * different rate. Every driver must reclaim its own rate immediately
 * before its own transfers via SPI1_BUS_SetBaudRate(), not just once at
 * init. (sd_spi_disk.c's disk_read()/disk_write() do this; SDSPI_Init()
 * itself already re-asserts 400kHz unconditionally at its own start, so
 * disk_initialize() doesn't need it.)
 *
 * SPI1_BUS_SetBaudRate() also recomputes the PCS-to-SCK/last-SCK-to-PCS/
 * between-transfer delay registers every time it's called, not just the
 * SCK divider - CONFIRMED on real hardware (2026-09-04, self-directed SWD
 * debug session, see WORKLOG.md) that LPSPI_MasterSetBaudRate() alone
 * leaves those delays at whatever SPI1_BUS_Init() baked in from its
 * throwaway 400kHz baseline, which cost ~1.25us of stale fixed delay on
 * EVERY byte transferred (not per call - proven by chunk-size experiments
 * that changed call count with zero effect on measured time),
 * overwhelming useful throughput at higher baud rates. Every device on
 * this bus benefits automatically since they all go through
 * SPI1_BUS_SetBaudRate().
 */
#ifndef _SPI1_BUS_H_
#define _SPI1_BUS_H_

#include <stdint.h>
#include "fsl_common.h"

/*! @brief One-time LPSPI1 peripheral bring-up (mode 0, MSB-first, 8-bit
 *  frames - the shared baseline every device on this bus uses; each
 *  device still sets its own baud rate before its own transfers). Safe to
 *  call from every driver's own Init(), regardless of which runs first -
 *  a no-op after the first call. */
void SPI1_BUS_Init(void);

/*! @brief Reclaim the bus at the given baud rate - call this right before
 *  every transfer/transaction, not just once at init. Returns the actual
 *  achieved baud rate (0 on failure), same convention as the underlying
 *  LPSPI_MasterSetBaudRate(). */
uint32_t SPI1_BUS_SetBaudRate(uint32_t baudRate_Bps);

/*! @brief LPSPI1's current source clock frequency (whatever
 *  hardware_init.c attached FLEXCOMM1 to - see its comment). Diagnostic:
 *  every achievable baud rate is srcClock/((1<<prescaler)*(scaler+2)), so
 *  this plus SPI1_BUS_SetBaudRate()'s return value tells you exactly what
 *  rate a transfer actually ran at, instead of trusting the requested
 *  value. */
uint32_t SPI1_BUS_GetSourceClockFreq(void);

/*! @brief Blocking transfer of `size` bytes. `rxData` may be NULL for a
 *  write-only transfer (LCD); `txData` may be NULL for a read-only one
 *  (touch's ADC reads, once the command byte has already gone out). `pcs`
 *  is one of the kLPSPI_MasterPcsN transfer-config flags (optionally
 *  OR'd with kLPSPI_MasterPcsContinuous) - kLPSPI_MasterPcs1 for LCD/
 *  touch (unrouted, see file header), kLPSPI_MasterPcs0 for the microSD
 *  slot (real hardware CS). */
status_t SPI1_BUS_TransferBlocking(const uint8_t *txData, uint8_t *rxData, uint32_t size, uint32_t pcs);

/*! @brief One-time-per-frame setup for the eDMA pixel-push path below -
 *  call this ONCE before a run of SPI1_BUS_TransferBytesDMA() calls (e.g.
 *  once per LCD_PushPixelsOpen() invocation, not once per chunk), same
 *  usage pattern as mcuxsdk's own tested lpspi/edma_b2b_transfer reference
 *  example (examples/_boards/frdmmcxn947/driver_examples/lpspi/edma_b2b_transfer,
 *  which calls its equivalent of this function once and then transfers
 *  repeatedly). `pcs` - same meaning as SPI1_BUS_TransferBlocking()'s.
 *  Unconditionally clears TCR.RXMSK/TXMSK after preparing - CONFIRMED via
 *  a live register trace (2026-09-04, see WORKLOG.md) to be the actual
 *  fix for a real eDMA hang: every write-only blocking transfer on this
 *  shared bus (rxData==NULL, e.g. every LCD command byte) leaves
 *  TCR.RXMSK=1 via LPSPI_MasterTransferBlocking()'s own logic, which the
 *  underlying LPSPI_MasterTransferPrepareEDMALite() never clears -
 *  without this, the RX FIFO can never receive data, so the RX eDMA
 *  channel's completion (which the SDK's LPSPI+eDMA driver waits on even
 *  for a TX-only transfer) would never fire. */
status_t SPI1_BUS_PrepareDMA(uint32_t pcs);

/*! @brief Write-only transfer of `size` bytes via eDMA instead of the
 *  CPU-polled path above - see the .c file's header comment for why
 *  (2026-09-04, see WORKLOG.md: per-byte APB register-access latency in
 *  the SDK's blocking transfer path was the dominant remaining cost after
 *  fixing two earlier bugs, confirmed via self-directed SWD debugging).
 *  Stays on the shared bus's normal 8-bit frames (same as
 *  SPI1_BUS_TransferBlocking()). SPI1_BUS_PrepareDMA() MUST have already
 *  been called (once, not per-chunk - see its own comment for why: moving
 *  the per-call setup cost out of the chunk loop was a real, measured fps
 *  win, see WORKLOG.md). Callers needing MSB-first RGB565 wire order from
 *  a little-endian pixel buffer must byte-swap into `data` themselves
 *  before calling (see lcd_spi_hw.c's LCD_PushPixelsOpen()). Blocking:
 *  waits for eDMA completion (bounded, see the .c file) before returning.
 *  Only ever call this for LCD pixel data - SD/touch still use the polled
 *  path above, unaffected. */
status_t SPI1_BUS_TransferBytesDMA(const uint8_t *data, uint32_t size);

#endif /* _SPI1_BUS_H_ */
