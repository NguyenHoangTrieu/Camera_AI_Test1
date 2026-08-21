/*
 * pin_mux.h - Camera_AI_Test1 (FRDM-MCXN947)
 *
 * Pin routing for:
 *  - Debug UART (P1_8 / P1_9)
 *  - OV7670 camera on J9 SmartDMA/Camera header
 *  - TFT LCD (ST7796S, e.g. NXP LCD-PAR-S035) on the J8 FlexIO/LCD header
 *
 * Both the camera and FlexIO/LCD pin setups are copied from the verified
 * NXP example `display_examples/smartdma_camera_flexio_mculcd` (board port
 * for frdmmcxn947), which drives exactly this hardware combination. See
 * ../../README.md for the full pin table and J8 wiring notes.
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

#if defined(__cplusplus)
}
#endif

#endif /* _PIN_MUX_H_ */
