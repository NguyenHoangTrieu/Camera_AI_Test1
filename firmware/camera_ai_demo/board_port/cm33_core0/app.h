/*
 * app.h - Camera_AI_Test1 (FRDM-MCXN947)
 *
 * Central place for board-level constants: camera/panel geometry and the
 * FlexIO/GPIO pins used for the J8 hardware-accelerated LCD bus. pin_mux.c
 * and the LCD driver both include this file so the pin list only exists
 * once. This mirrors NXP's own `display_examples/smartdma_camera_flexio_mculcd`
 * example almost exactly (J8 is the officially-supported way to drive a
 * parallel display on this board), except the data bus is 8-bit here
 * (FLEXIO_MCULCD_DATA_BUS_WIDTH=8, set in CMakeLists.txt) because the panel
 * actually in hand only has LCD_D0..D7, not the full 16-bit bus J8 supports.
 */
#ifndef _APP_H_
#define _APP_H_

#include "fsl_gpio.h"
#include "fsl_port.h"
#include "fsl_smartdma.h"

/* Normally set by CMakeLists.txt's LCD_BITBANG_DIAGNOSTIC option (as a
 * global -D). Defaulted here too so this header is self-contained if
 * included somewhere that option didn't run. */
#ifndef DEMO_LCD_BITBANG
#define DEMO_LCD_BITBANG 0
#endif

/*******************************************************************************
 * Camera / frame buffer
 ******************************************************************************/
#define DEMO_CAMERA_RESOLUTION kVIDEO_ResolutionQVGA /* 320*240 */
#define DEMO_BUFFER_WIDTH      320U
#define DEMO_BUFFER_HEIGHT     240U
#define DEMO_SMARTDMA_API      kSMARTDMA_CameraWholeFrameQVGA /* whole 320x240 RGB565 frame per interrupt */

/*******************************************************************************
 * TFT LCD panel geometry. NOT ST7796S/480x320 - that was the original
 * (wrong) assumption from before the panel was known to be 8-bit-only (see
 * WORKLOG.md). The panel in hand is very likely an ILI9341-family 240x320
 * panel (2.4" "TFT LCD shield for Arduino Uno" with onboard SD reader),
 * driven here in its landscape orientation via MADCTL (see
 * lcd_flexio_mculcd.c's LCD_InitPanel) - which is 320x240, matching the
 * camera buffer below exactly. This must stay in sync with the panel's real
 * addressable GRAM range: WORKLOG.md documents that main.c's boot-time
 * "blank the screen" loop used to call LCD_SetWindow()/LCD_DrawImage() with
 * the old 480x320 values, sending out-of-range column/page addresses to the
 * real (smaller) panel on every boot, before the first camera frame was
 * ever drawn - a plausible contributor to the "solid white, no image"
 * symptom independent of any wiring/transport issue.
 ******************************************************************************/
#define DEMO_PANEL_WIDTH  320U
#define DEMO_PANEL_HEIGHT 240U

/*******************************************************************************
 * J8 FlexIO/LCD header - 8-bit parallel MIPI-DBI/8080 bus (panel only has
 * LCD_D0..D7), driven by the FlexIO peripheral (hardware-accelerated,
 * unlike a GPIO bit-bang driver). The data bus width itself
 * (FLEXIO_MCULCD_DATA_BUS_WIDTH=8) is set as a global compile definition in
 * CMakeLists.txt, not here - it's read by the SDK's own fsl_flexio_mculcd.c.
 ******************************************************************************/
#define DEMO_FLEXIO              FLEXIO0
#define DEMO_FLEXIO_CLOCK_FREQ   CLOCK_GetFlexioClkFreq()
/*
 * Total bus bit rate; the driver divides by the 8-bit bus width internally,
 * so this is 8x the actual per-pin WR toggle rate. NXP's reference example
 * runs this at 160,000,000 (=20 MHz/pin @ 8-bit width) for a real ST7796S,
 * which has a sub-50ns minimum write cycle time. First J8 bring-up
 * (backlight on, generic init sequence proven working on the old bit-banged
 * path, still blank) suggests this panel's controller is much slower than
 * that and simply isn't latching data at 20 MHz/pin - dropped way down
 * here as a first test. If the screen starts responding, this can likely
 * be raised again in steps to find the actual safe ceiling.
 *
 * 2,000,000 (250 kHz/pin) turned out to be below the minimum the FlexIO
 * clock setup could divide down to when it ran undivided off the 150 MHz
 * PLL0 (FLEXIO_MCULCD_Init() rejected it outright, timer divider field
 * overflowed) - 400 kHz/pin was the next safely-valid step up at that
 * clock. That got the panel responding to commands (after the RD-pin fix
 * below), but pixel data came out corrupted (random black/white noise, not
 * a clean image) - a signal-integrity symptom, meaning 400 kHz/pin is
 * apparently still too fast for this specific cheap/slow panel to reliably
 * latch data on the FlexIO hardware bus (even though the exact same byte
 * values worked fine bit-banged much slower - see WORKLOG.md).
 *
 * hardware_init.c now divides the FlexIO source clock itself down to
 * 37.5 MHz (was undivided 150 MHz) specifically so this value can go lower
 * than 400 kHz/pin without hitting the same divider-overflow limit.
 *
 * A first attempt paired that with 80,000 (10 kHz/pin) here and a /50
 * clock divider in hardware_init.c (~40x slower than the 400 kHz/pin that
 * produced noise) - that combination made FLEXIO_MCULCD_WriteCommandBlocking()
 * hang forever (confirmed via serial log: boot banner prints, then nothing
 * else at all, not even the camera init line that immediately follows
 * LCD_Init() in main.c) waiting for a FlexIO timer completion flag that
 * never asserts. Backed off to a smaller, safer step here: 800,000
 * (100 kHz/pin) with hardware_init.c's /4 clock divider keeps
 * FLEXIO_MCULCD_SetBaudRate()'s resulting internal divider (~188) in the
 * same numeric range that's already proven to work (the un-divided
 * 400 kHz/pin case used essentially that same divider magnitude, just
 * against a 150 MHz source instead of 37.5 MHz) - this tests "is it really
 * about absolute bus speed" without also venturing into whatever lower
 * bound broke the /50 attempt. If the image comes out clean at this
 * speed, raise both this and hardware_init.c's kCLOCK_DivFlexioClk
 * together in smaller steps to find the actual safe ceiling for this
 * panel. If it's still noisy, try going slower again but in smaller
 * increments than the /50 jump that hung. See WORKLOG.md.
 */
#define DEMO_FLEXIO_BAUDRATE_BPS 800000U /* = 100 kHz/pin @ 8-bit width, with the /4 FlexIO clock divider */

#define DEMO_FLEXIO_WR_PIN           1U  /* FLEXIO0_D1 = P0_9 */
#define DEMO_FLEXIO_RD_PIN           0U  /* FLEXIO0_D0 = P0_8 */
#define DEMO_FLEXIO_DATA_PIN_START   16U /* LCD_D0..D7 = FLEXIO0_D16..D23 (8-bit bus) */
#define DEMO_FLEXIO_TX_START_SHIFTER 0U
#define DEMO_FLEXIO_RX_START_SHIFTER 0U
#define DEMO_FLEXIO_TX_END_SHIFTER   7U
#define DEMO_FLEXIO_RX_END_SHIFTER   7U
#define DEMO_FLEXIO_TIMER            0U

/* Plain-GPIO LCD control signals (not on the FlexIO data/WR/RD pins above). */
#define DEMO_LCD_RST_GPIO GPIO4
#define DEMO_LCD_RST_PIN  7U /* P4_7 */
#define DEMO_LCD_CS_GPIO  GPIO0
#define DEMO_LCD_CS_PIN   12U /* P0_12 */
#define DEMO_LCD_RS_GPIO  GPIO0
#define DEMO_LCD_RS_PIN   7U /* P0_7 */

/*
 * Backlight enable (J8 pin labeled LCD_BLK on the board's own silkscreen).
 * NXP's reference example doesn't drive this - the official LCD-PAR-S035
 * panel has its own always-on backlight - but a generic bare panel module
 * commonly needs this pin driven high or the screen stays completely dark
 * even though everything else (bus, commands, GRAM writes) is working.
 * Driven high in LCD_Init() in lcd_flexio_mculcd.c.
 */
#define DEMO_LCD_BLK_GPIO GPIO4
#define DEMO_LCD_BLK_PIN  6U /* P4_6 */

/*******************************************************************************
 * Diagnostic bit-bang backend (DEMO_LCD_BITBANG=1, set via CMakeLists.txt
 * LCD_BITBANG_DIAGNOSTIC option) - drives the exact same 14 J8 signals as
 * plain GPIO instead of through the FlexIO peripheral, to isolate whether a
 * "solid white, no image" result is specific to the FlexIO hardware bus or
 * would happen either way (see lcd_bitbang_j8.c and WORKLOG.md). Same WR/RD
 * pins as the FlexIO section above, plus GPIO port/pin pairs for the 8 data
 * lines (which FlexIO addresses by FLEXIO0_D16..D23 instead).
 ******************************************************************************/
#define DEMO_LCD_WR_GPIO GPIO0
#define DEMO_LCD_WR_PIN  9U /* P0_9 */
#define DEMO_LCD_RD_GPIO GPIO0
#define DEMO_LCD_RD_PIN  8U /* P0_8 */

#define DEMO_LCD_D0_GPIO GPIO2
#define DEMO_LCD_D0_PIN  8U /* P2_8 */
#define DEMO_LCD_D1_GPIO GPIO2
#define DEMO_LCD_D1_PIN  9U /* P2_9 */
#define DEMO_LCD_D2_GPIO GPIO2
#define DEMO_LCD_D2_PIN  10U /* P2_10 */
#define DEMO_LCD_D3_GPIO GPIO2
#define DEMO_LCD_D3_PIN  11U /* P2_11 */
#define DEMO_LCD_D4_GPIO GPIO4
#define DEMO_LCD_D4_PIN  12U /* P4_12 */
#define DEMO_LCD_D5_GPIO GPIO4
#define DEMO_LCD_D5_PIN  13U /* P4_13 */
#define DEMO_LCD_D6_GPIO GPIO4
#define DEMO_LCD_D6_PIN  14U /* P4_14 */
#define DEMO_LCD_D7_GPIO GPIO4
#define DEMO_LCD_D7_PIN  15U /* P4_15 */

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
void BOARD_InitHardware(void);

/* SmartDMA camera capture only works with DCDC at Mid; USB HS PHY needs
 * Overdrive - see hardware_init.c and WORKLOG.md "time-multiplex fallback".
 * These just flip the regulator level (no USB PHY/clock bring-up, no camera
 * re-init) - main() uses them to switch back and forth on its periodic
 * refresh cycle. */
void BOARD_SetRegulatorsMidVoltage(void);
void BOARD_SetRegulatorsOverdriveVoltage(void);

#endif /* _APP_H_ */
