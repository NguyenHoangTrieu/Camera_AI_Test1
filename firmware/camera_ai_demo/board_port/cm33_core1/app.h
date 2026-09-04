/*
 * app.h - core1 (Camera_AI_Test1 dual-core RTOS migration - see
 * WORKLOG.md).
 *
 * Stage 3: camera capture + LCD preview move here. These camera/LCD
 * macros are copied from the legacy board_port/cm33_core0/app.h (Arduino-
 * header default only - J8/FlexIO and touch are not carried into the
 * dual-core build, see CMakeLists.txt's DUALCORE_RTOS branch), since
 * core1 is now the permanent home for this hardware, not core0.
 */
#ifndef _APP_CORE1_H_
#define _APP_CORE1_H_

#include "fsl_gpio.h"
#include "fsl_port.h"
#include "fsl_smartdma.h"

#define DEMO_LCD_ARDUINO_HEADER 1
#define DEMO_LCD_BITBANG        1

/*******************************************************************************
 * Camera / frame buffer - see board_port/cm33_core0/app.h's original
 * comment (WORKLOG.md) for why whole-frame QVGA RGB565 is the confirmed-
 * working mode (a 384x384 grayscale mode was tried and reverted).
 ******************************************************************************/
#define DEMO_CAMERA_RESOLUTION kVIDEO_ResolutionQVGA /* 320*240 */
#define DEMO_BUFFER_WIDTH      320U
#define DEMO_BUFFER_HEIGHT     240U
#define DEMO_SMARTDMA_API      kSMARTDMA_CameraWholeFrameQVGA /* whole 320x240 RGB565 frame per interrupt */

/*******************************************************************************
 * TFT LCD panel geometry - ILI9341-family, 240x320 native GRAM (portrait),
 * driven here in landscape via MADCTL (MV bit, see lcd_spi_hw.c's
 * LCD_InitPanel).
 ******************************************************************************/
#define DEMO_PANEL_WIDTH  320U
#define DEMO_PANEL_HEIGHT 240U

/*******************************************************************************
 * Arduino header (current default). Panel is a 2.4" SPI TFT module
 * (ILI9341-family) with an onboard microSD slot and XPT2046 touch
 * controller - see README.md. SCK/SDI/SDO ride hardware LPSPI1, SHARED
 * with the microSD slot (Stage 4) - see source/spi1_bus.h. Only LCD_DC/
 * CS/RST/BLK are plain GPIO, on the Arduino header (A2..A5).
 ******************************************************************************/
#define DEMO_LCD_DC_GPIO GPIO0
#define DEMO_LCD_DC_PIN  14U /* Arduino A2 */
#define DEMO_LCD_CS_GPIO GPIO0
#define DEMO_LCD_CS_PIN  22U /* Arduino A3 */
#define DEMO_LCD_RST_GPIO GPIO0
#define DEMO_LCD_RST_PIN  15U /* Arduino A4 */

#define DEMO_LCD_BLK_GPIO GPIO0
#define DEMO_LCD_BLK_PIN  23U /* Arduino A5 - panel's LED pin, safe default even if unconnected */

/*******************************************************************************
 * Touch controller (XPT2046) pins - needed because board_port/pin_mux.c
 * (SHARED between both cores) unconditionally defines
 * BOARD_InitTouchPins(), which references these macros regardless of
 * whether anything actually calls that function. Touch itself is not
 * wired into any core1 task yet (same "present but unused" status as the
 * legacy single-core build - see README.md).
 ******************************************************************************/
#define DEMO_TOUCH_CS_GPIO GPIO0
#define DEMO_TOUCH_CS_PIN  10U /* Arduino D9 */
#define DEMO_TOUCH_IRQ_GPIO GPIO0
#define DEMO_TOUCH_IRQ_PIN  28U /* Arduino D8 - input, active low when pressed */

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
void BOARD_InitHardware(void);

#endif /* _APP_CORE1_H_ */
