/*
 * snapshot.h - on face detection, draw a bounding box into the camera
 * frame buffer and save it as a BMP to the TFT shield's onboard microSD
 * card (source/storage/sd_spi_disk.c), rate-limited to at most one
 * capture per second.
 */
#ifndef _SNAPSHOT_H_
#define _SNAPSHOT_H_

#include <stdbool.h>
#include <stdint.h>
#include "model_runner.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! @brief Mounts the SD card filesystem. Safe to call even if no card is
 *  present/it fails - SNAPSHOT_OnFrame() then just silently skips every
 *  frame afterward instead of retrying every call. */
void SNAPSHOT_Init(void);

/*!
 * @brief Call once per processed frame, after AI_MODEL_RunInference().
 *
 * If aiResult has a "face" box AND at least 1 second (measured via the
 * DWT cycle counter, already enabled by AI_MODEL_Init() - see
 * model_runner.cpp/model_runner_npu.cpp) has passed since the last
 * successful capture, draws a box for every detected face directly into
 * `frame` (in place - safe here because main.c only calls this while
 * SmartDMA capture is stopped, right before CAMERA_CAPTURE_Reinit()
 * overwrites the buffer with the next frame anyway) and saves it as a BMP.
 * Otherwise does nothing. Never captures a second time within the same
 * one-second window, no matter how many frames see a face in it.
 *
 * @param frame        RGB565 camera frame buffer, frameWidth*frameHeight
 *                      pixels - modified in place if a box is drawn.
 * @param frameWidth,frameHeight camera frame buffer geometry (DEMO_BUFFER_WIDTH/HEIGHT).
 * @param aiResult     this frame's AI_MODEL_RunInference() output.
 * @param aiInputWidth,aiInputHeight AI_MODEL_GetInputWidth/Height() - the
 *                      coordinate space aiResult's boxes are in, scaled up
 *                      to frameWidth/frameHeight before drawing.
 * @return true if a snapshot was captured this call.
 */
bool SNAPSHOT_OnFrame(uint16_t *frame, uint16_t frameWidth, uint16_t frameHeight, const ai_model_result_t *aiResult,
                      uint16_t aiInputWidth, uint16_t aiInputHeight);

/*!
 * @brief True for SNAPSHOT_NOTICE_DURATION_MS (snapshot.c) following the
 * most recent successful capture - main.c polls this every frame to
 * show/clear an on-screen "CAPTURE" notification line, independent of
 * whatever SNAPSHOT_OnFrame() returned this particular call.
 *
 * Deliberately a *longer* window than the internal capture rate-limit
 * (SNAPSHOT_RATE_LIMIT_MS, 1 second, unaffected by this) - CONFIRMED on
 * real hardware (2026-08-25) that 1 second isn't enough time for a person
 * to notice a capture happened and react before it's already gone. A new
 * capture can become possible again while this notice is still showing;
 * that's fine, this is a human-facing indicator, not a machine-readable
 * "capture in progress" flag.
 */
bool SNAPSHOT_IsNoticeActive(void);

#ifdef __cplusplus
}
#endif

#endif /* _SNAPSHOT_H_ */
