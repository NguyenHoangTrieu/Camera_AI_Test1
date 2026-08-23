/*
 * app.h - Camera_AI_Test1 (FRDM-MCXN947)
 *
 * Board-level constants: camera/panel geometry and the GPIO pins used for
 * the LCD bus. pin_mux.c and the LCD driver both include this file.
 *
 * Current default: GPIO bit-bang on the Arduino header
 * (DEMO_LCD_ARDUINO_HEADER=1). The J8 FlexIO/LCD header path and a J8
 * bit-bang diagnostic variant are still selectable (see CMakeLists.txt)
 * but abandoned - see WORKLOG.md.
 */
#ifndef _APP_H_
#define _APP_H_

#include "fsl_gpio.h"
#include "fsl_port.h"
#include "fsl_smartdma.h"

/* Normally set by CMakeLists.txt's LCD_ARDUINO_HEADER_BITBANG /
 * LCD_BITBANG_DIAGNOSTIC options. Defaulted here too so this header is
 * self-contained. Arduino header has no FlexIO route, so it implies
 * bit-bang. */
#ifndef DEMO_LCD_ARDUINO_HEADER
#define DEMO_LCD_ARDUINO_HEADER 1
#endif
#ifndef DEMO_LCD_BITBANG
#define DEMO_LCD_BITBANG (DEMO_LCD_ARDUINO_HEADER || 0)
#endif

/*******************************************************************************
 * Camera / frame buffer
 ******************************************************************************/
#define DEMO_CAMERA_RESOLUTION kVIDEO_ResolutionQVGA /* 320*240 */
#define DEMO_BUFFER_WIDTH      320U
#define DEMO_BUFFER_HEIGHT     240U
#define DEMO_SMARTDMA_API      kSMARTDMA_CameraWholeFrameQVGA /* whole 320x240 RGB565 frame per interrupt */

/*******************************************************************************
 * TFT LCD panel geometry. Panel is ILI9341-family, 240x320 native GRAM
 * (portrait), driven here in landscape via MADCTL (MV bit, see
 * lcd_bitbang.c's LCD_InitPanel) so it matches the 320x240 camera buffer
 * exactly - no software rotation.
 ******************************************************************************/
#define DEMO_PANEL_WIDTH  320U
#define DEMO_PANEL_HEIGHT 240U

/*******************************************************************************
 * J8 FlexIO/LCD header - 8-bit parallel MIPI-DBI/8080 bus (panel only has
 * LCD_D0..D7), driven by the FlexIO peripheral. Bus width
 * (FLEXIO_MCULCD_DATA_BUS_WIDTH=8) is set in CMakeLists.txt.
 ******************************************************************************/
#define DEMO_FLEXIO              FLEXIO0
#define DEMO_FLEXIO_CLOCK_FREQ   CLOCK_GetFlexioClkFreq()
/* Total bus bit rate (driver divides by the 8-bit width internally, so
 * this is 8x the per-pin WR toggle rate). This panel's controller can't
 * keep up with NXP's reference 20 MHz/pin - dropped down here to a rate
 * that latches cleanly. See WORKLOG.md before changing. */
#define DEMO_FLEXIO_BAUDRATE_BPS 800000U /* = 100 kHz/pin @ 8-bit width, with the /4 FlexIO clock divider */

#define DEMO_FLEXIO_WR_PIN           1U  /* FLEXIO0_D1 = P0_9 */
#define DEMO_FLEXIO_RD_PIN           0U  /* FLEXIO0_D0 = P0_8 */
#define DEMO_FLEXIO_DATA_PIN_START   16U /* LCD_D0..D7 = FLEXIO0_D16..D23 (8-bit bus) */
#define DEMO_FLEXIO_TX_START_SHIFTER 0U
#define DEMO_FLEXIO_RX_START_SHIFTER 0U
#define DEMO_FLEXIO_TX_END_SHIFTER   7U
#define DEMO_FLEXIO_RX_END_SHIFTER   7U
#define DEMO_FLEXIO_TIMER            0U

#if DEMO_LCD_ARDUINO_HEADER
/*******************************************************************************
 * Arduino header (current default). Bit-bang only - no FlexIO route there.
 * See README.md for the full pinout table and physical wiring notes.
 ******************************************************************************/
#define DEMO_LCD_D0_GPIO GPIO0
#define DEMO_LCD_D0_PIN  28U /* Arduino D8 */
#define DEMO_LCD_D1_GPIO GPIO0
#define DEMO_LCD_D1_PIN  10U /* Arduino D9 */
#define DEMO_LCD_D2_GPIO GPIO0
#define DEMO_LCD_D2_PIN  29U /* Arduino D2 */
#define DEMO_LCD_D3_GPIO GPIO1
#define DEMO_LCD_D3_PIN  23U /* Arduino D3 */
#define DEMO_LCD_D4_GPIO GPIO0
#define DEMO_LCD_D4_PIN  30U /* Arduino D4 */
#define DEMO_LCD_D5_GPIO GPIO1
#define DEMO_LCD_D5_PIN  21U /* Arduino D5 */
#define DEMO_LCD_D6_GPIO GPIO1
#define DEMO_LCD_D6_PIN  2U /* Arduino D6 */
#define DEMO_LCD_D7_GPIO GPIO0
#define DEMO_LCD_D7_PIN  31U /* Arduino D7 */

#define DEMO_LCD_RS_GPIO GPIO0
#define DEMO_LCD_RS_PIN  14U /* Arduino A2 */
#define DEMO_LCD_CS_GPIO GPIO0
#define DEMO_LCD_CS_PIN  22U /* Arduino A3 */
#define DEMO_LCD_RST_GPIO GPIO0
#define DEMO_LCD_RST_PIN  15U /* Arduino A4 */

/* Jumper-wired: shield's A0 pad (LCD_RD) -> board Arduino D0; shield's A1
 * pad (LCD_WR) -> board Arduino D1. A0/A1 themselves have no GPIO. */
#define DEMO_LCD_RD_GPIO GPIO4
#define DEMO_LCD_RD_PIN  3U /* Arduino D0 */
#define DEMO_LCD_WR_GPIO GPIO4
#define DEMO_LCD_WR_PIN  2U /* Arduino D1 */

#define DEMO_LCD_BLK_GPIO GPIO0
#define DEMO_LCD_BLK_PIN  23U /* Arduino A5 - unused otherwise, safe default */

#else /* !DEMO_LCD_ARDUINO_HEADER: J8 pin set (FlexIO or J8 bit-bang) */

/* Plain-GPIO LCD control signals (not on the FlexIO data/WR/RD pins above). */
#define DEMO_LCD_RST_GPIO GPIO4
#define DEMO_LCD_RST_PIN  7U /* P4_7 */
#define DEMO_LCD_CS_GPIO  GPIO0
#define DEMO_LCD_CS_PIN   12U /* P0_12 */
#define DEMO_LCD_RS_GPIO  GPIO0
#define DEMO_LCD_RS_PIN   7U /* P0_7 */

/* Backlight enable (J8 pin labeled LCD_BLK). Driven high in LCD_Init(). */
#define DEMO_LCD_BLK_GPIO GPIO4
#define DEMO_LCD_BLK_PIN  6U /* P4_6 */

/* Same WR/RD pins whether the backend is FlexIO or J8 bit-bang, plus GPIO
 * port/pin pairs for the 8 data lines (FlexIO addresses these by
 * FLEXIO0_D16..D23 instead, when active). */
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

#endif /* DEMO_LCD_ARDUINO_HEADER */

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
void BOARD_InitHardware(void);

/* SmartDMA camera capture only works with DCDC at Mid; USB HS PHY needs
 * Overdrive - see hardware_init.c and WORKLOG.md. These just flip the
 * regulator level (no clock/camera re-init) - main() switches back and
 * forth on its periodic refresh cycle. */
void BOARD_SetRegulatorsMidVoltage(void);
void BOARD_SetRegulatorsOverdriveVoltage(void);

#endif /* _APP_H_ */
