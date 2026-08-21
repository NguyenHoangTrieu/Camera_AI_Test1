/*
 * camera_capture.h - OV7670 capture via SmartDMA, J9 SmartDMA/Camera header
 *
 * Thin wrapper around the same camera + SmartDMA driver combination used by
 * the verified NXP example `display_examples/smartdma_camera_flexio_mculcd`.
 * The display side of this project (see ../display/lcd_flexio_mculcd.h) is
 * now also that same example's J8 FlexIO + ST7796S combination, which is
 * fast enough to draw directly from the live buffer below without the
 * anti-tearing snapshot/downscale step an earlier (GPIO bit-bang) revision
 * of this project needed.
 */
#ifndef _CAMERA_CAPTURE_H_
#define _CAMERA_CAPTURE_H_

#include <stdint.h>
#include <stdbool.h>

/*! @brief Init camera (SCCB/I2C + OV7670 registers) and boot the SmartDMA capture firmware. */
void CAMERA_CAPTURE_Init(void);

/*! @brief True once a new frame has landed in the capture buffer since the last call. */
bool CAMERA_CAPTURE_IsFrameReady(void);

/*! @brief Clear the "frame ready" flag after consuming the buffer. */
void CAMERA_CAPTURE_ClearFrameReady(void);

/*! @brief RGB565 frame buffer, DEMO_BUFFER_WIDTH x DEMO_BUFFER_HEIGHT pixels (see app.h). */
uint16_t *CAMERA_CAPTURE_GetFrameBuffer(void);

/*! @brief Total frames captured since CAMERA_CAPTURE_Init(), for liveness logging. */
uint32_t CAMERA_CAPTURE_GetFrameCount(void);

#endif /* _CAMERA_CAPTURE_H_ */
