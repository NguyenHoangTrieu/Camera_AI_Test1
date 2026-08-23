/*
 * pin_mux.h - Camera_AI_Test1 (FRDM-MCXN947)
 *
 * Pin routing for: debug UART, OV7670 camera (J9), and the TFT LCD -
 * Arduino header by default (BOARD_InitArduinoLcdPins()), or J8
 * FlexIO/LCD header (BOARD_InitFlexioPins(), abandoned but still
 * selectable via CMakeLists.txt). hardware_init.c calls whichever one
 * DEMO_LCD_ARDUINO_HEADER (app.h) selects.
 *
 * Camera pin setup is copied from NXP's
 * `display_examples/smartdma_camera_flexio_mculcd` example. See
 * ../../README.md for the full pin tables and wiring notes.
 */
#ifndef _PIN_MUX_H_
#define _PIN_MUX_H_

#if defined(__cplusplus)
extern "C" {
#endif

void BOARD_InitBootPins(void);
void BOARD_InitDebugUartPins(void);
void BOARD_InitCameraPins(void);
void BOARD_InitFlexioPins(void);
void BOARD_InitArduinoLcdPins(void);

#if defined(__cplusplus)
}
#endif

#endif /* _PIN_MUX_H_ */
