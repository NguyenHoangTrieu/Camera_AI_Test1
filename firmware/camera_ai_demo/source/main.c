/*
 * main.c - Camera_AI_Test1
 *
 * Default build: OV7670 capture (via SmartDMA, J9 header) -> AI model hook
 * (stub, see source/ai/model_runner.c) -> J8 LCD (source/display/, live
 * preview, whatever fps CAMERA_CAPTURE_IsFrameReady()/LCD_DrawImage() settle
 * at - the FlexIO push is the bottleneck, low fps is expected and fine),
 * no USB.
 *
 * USB Video Class (UVC) streaming over USB HS (source/usb/usb_video_camera.c)
 * is ABANDONED - see WORKLOG.md "USB streaming pipeline abandoned" entry for
 * the full reasoning (short version: SmartDMA camera capture and the USB HS
 * PHY need mutually exclusive DCDC voltage levels on this chip, and this
 * board's one USB connector is hard-wired to the HS controller only, so
 * there's no way around it in software). The code is still here and still
 * builds (CMakeLists.txt's USB_STREAM_DIAGNOSTIC_DISABLE=OFF) in case this
 * is revisited - if built, it runs time-multiplexed (periodic Mid-voltage
 * recapture / Overdrive-streaming switching, confirmed stable on hardware),
 * not truly live - see the #else branch below for that path's details.
 */

#include "app.h"
#include "board.h"
#include "camera_capture.h"
#include "fsl_common.h"
#include "fsl_debug_console.h"
#include "lcd_display.h"
#include "model_runner.h"
#include "usb_video_camera.h"

/*
 * Cheap "is the camera actually sending real image data" signature: min/max/
 * average over a strided sample of pixels (not the whole 76800-pixel frame,
 * to keep this fast). A dead/disconnected sensor tends to produce a flat
 * buffer (all-0x0000 or a fixed pattern), so min==max and avg is constant
 * frame to frame. A live camera pointed at anything but a perfectly uniform
 * surface produces min!=max, and the avg drifts as the scene changes.
 */
static void CAMERA_CAPTURE_LogFrameSignature(uint32_t frameNumber,
                                             const uint16_t *frame) {
  const uint32_t pixelCount = (uint32_t)DEMO_BUFFER_WIDTH * DEMO_BUFFER_HEIGHT;
  const uint32_t stride = 97U; /* prime, avoids lining up with row width */
  uint16_t minPixel = 0xFFFFU;
  uint16_t maxPixel = 0x0000U;
  uint32_t sum = 0U;
  uint32_t samples = 0U;

  for (uint32_t i = 0; i < pixelCount; i += stride) {
    uint16_t p = frame[i];
    if (p < minPixel) {
      minPixel = p;
    }
    if (p > maxPixel) {
      maxPixel = p;
    }
    sum += p;
    samples++;
  }

  PRINTF("Camera: frame #%u ready, %u samples, pixel range 0x%04X..0x%04X, "
         "avg=0x%04X%s\r\n",
         frameNumber, samples, minPixel, maxPixel, (uint16_t)(sum / samples),
         (minPixel == maxPixel) ? " (flat - lens cap on, or no real image data)"
                                : "");
}

#if !DEMO_USB_STREAM_DISABLE
/*
 * Everything below, down to DEMO_CaptureFramesAtMidVoltage(), only exists
 * for the abandoned USB-streaming path - see the file-level comment above.
 *
 * Camera runs at Mid voltage (see BOARD_InitHardware()) - capture this many
 * frames before stopping SmartDMA and switching to Overdrive/USB. More than
 * 1 so the OV7670's auto-exposure/auto-gain have a few frames to converge
 * first (frame #1 alone tends to come back flat/underexposed - see
 * WORKLOG.md) - the frame streamed until the next refresh is whichever one
 * is on the buffer when this count is reached.
 */
#define DEMO_MID_VOLTAGE_WARMUP_FRAMES 10U

/*
 * PERIODIC REFRESH (see WORKLOG.md "periodic refresh" entry): how long to
 * stay at Overdrive/streaming before dropping back to Mid to grab a fresh
 * frame. Confirmed stable on real hardware (see WORKLOG.md) at the 5000 ms
 * value below - a continuous v4l2 capture session survived multiple refresh
 * cycles with no disconnects. Don't set this too low: the host needs real
 * time to enumerate/negotiate over USB each time the PHY comes back up, and
 * DEMO_MID_VOLTAGE_WARMUP_FRAMES needs to actually complete (at ~10fps
 * effective) within a reasonable fraction of this window too - a WARMUP_FRAMES
 * that takes longer than HOLD_MS to capture means USB barely ever gets a
 * turn (this exact mistake happened once - see WORKLOG.md "why flashing
 * looked broken" entry).
 */
#define DEMO_OVERDRIVE_HOLD_MS 5000U

/* Capture (or re-capture) DEMO_MID_VOLTAGE_WARMUP_FRAMES frames at Mid
 * voltage, log the last one, then stop SmartDMA. Caller must already be at
 * DCDC Mid (BOARD_SetRegulatorsMidVoltage()) before calling this. */
static void DEMO_CaptureFramesAtMidVoltage(void) {
  uint16_t *frame = NULL;
  uint32_t frameNumber = 0U;
  uint32_t startCount = CAMERA_CAPTURE_GetFrameCount();

  while ((CAMERA_CAPTURE_GetFrameCount() - startCount) <
         DEMO_MID_VOLTAGE_WARMUP_FRAMES) {
    if (CAMERA_CAPTURE_IsFrameReady()) {
      CAMERA_CAPTURE_ClearFrameReady();
      frame = CAMERA_CAPTURE_GetFrameBuffer();
      frameNumber = CAMERA_CAPTURE_GetFrameCount();
    }
  }
  CAMERA_CAPTURE_LogFrameSignature(frameNumber, frame);
  PRINTF("Camera: capture stopped after frame #%u.\r\n", frameNumber);
  CAMERA_CAPTURE_Deinit();
}
#endif /* !DEMO_USB_STREAM_DISABLE */

int main(void) {
  BOARD_InitHardware();

  PRINTF("\r\nCamera_AI_Test1 - FRDM-MCXN947\r\n");
  PRINTF("Camera: OV7670 on J9 SmartDMA/Camera header\r\n");
#if DEMO_USB_STREAM_DISABLE
  PRINTF("Display: J8 LCD live preview (camera + AI hook)\r\n\r\n");
#else
  PRINTF("Display: USB Video Class (UVC) webcam over USB High-Speed "
         "(abandoned path, time-multiplexed - see WORKLOG.md)\r\n\r\n");
#endif

  CAMERA_CAPTURE_Init();
  AI_MODEL_Init();

#if DEMO_USB_STREAM_DISABLE
  /* Default build: camera + AI loop + LCD preview, continuous, DCDC stays
   * at Mid the whole time (see BOARD_InitHardware()) - proven to run 700+
   * clean frames. Pushing each frame out over the FlexIO/bit-bang LCD bus
   * is much slower than the camera's own ~30fps capture rate, so the
   * on-screen refresh rate ends up well below that - expected and fine
   * (this is a live low-fps preview, not meant to be smooth video). */
  LCD_Init();

  while (1) {
    if (CAMERA_CAPTURE_IsFrameReady()) {
      CAMERA_CAPTURE_ClearFrameReady();

      uint16_t *frame = CAMERA_CAPTURE_GetFrameBuffer();
      uint32_t frameNumber = CAMERA_CAPTURE_GetFrameCount();

      /* Every 15th frame (~2x/sec at 30fps) so the log stays readable
       * instead of flooding the 115200-baud UART every frame. */
      if ((frameNumber % 15U) == 1U) {
        CAMERA_CAPTURE_LogFrameSignature(frameNumber, frame);
      }

      LCD_DrawImage(0U, 0U, DEMO_BUFFER_WIDTH, DEMO_BUFFER_HEIGHT, frame);

      ai_model_result_t aiResult;
      AI_MODEL_RunInference(frame, DEMO_BUFFER_WIDTH, DEMO_BUFFER_HEIGHT,
                            &aiResult);
      if (aiResult.valid) {
        /* debug_console_lite may not support %f, so print score as a percentage
         * integer. */
        PRINTF("AI result: class=%d score=%d%%\r\n", aiResult.classId,
               (int)(aiResult.score * 100.0f));
      }
    }
  }
#else
  /* USB streaming build: time-multiplexed, see the file-level comment at
   * the top of this file. Mid-voltage capture phase first - wait for a
   * few settled frames, then stop SmartDMA (capture and USB HS can't run
   * at the same time on this chip). */
  DEMO_CaptureFramesAtMidVoltage();
  PRINTF("Camera: switching to Overdrive for USB.\r\n");

  ai_model_result_t aiResult;
  AI_MODEL_RunInference(CAMERA_CAPTURE_GetFrameBuffer(), DEMO_BUFFER_WIDTH,
                        DEMO_BUFFER_HEIGHT, &aiResult);
  if (aiResult.valid) {
    /* debug_console_lite may not support %f, so print score as a percentage
     * integer. */
    PRINTF("AI result: class=%d score=%d%%\r\n", aiResult.classId,
           (int)(aiResult.score * 100.0f));
  }

  /* USB_DeviceClockInit() (full PHY/PLL bring-up + enumeration) runs
   * exactly ONCE here. The periodic refresh loop below only ever calls
   * the lighter BOARD_SetRegulatorsMidVoltage()/
   * BOARD_SetRegulatorsOverdriveVoltage() pair afterwards - see
   * hardware_init.c's comments for why re-running the full PHY bring-up
   * on an already-enumerated session is deliberately avoided. */
  USB_DeviceClockInit();
  USB_VideoCamera_Init();

  while (1) {
    USB_VideoCamera_Task();

    /* PERIODIC REFRESH - see the DEMO_OVERDRIVE_HOLD_MS comment above
     * for the risk being tested here. */
    SDK_DelayAtLeastUs(DEMO_OVERDRIVE_HOLD_MS * 1000U,
                       SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);

    PRINTF("Camera: refreshing - dropping to Mid voltage to recapture.\r\n");
    BOARD_SetRegulatorsMidVoltage();
    CAMERA_CAPTURE_Reinit();
    DEMO_CaptureFramesAtMidVoltage();
    BOARD_SetRegulatorsOverdriveVoltage();
    PRINTF("Camera: back to Overdrive, streaming the new frame.\r\n");
  }
#endif
}
