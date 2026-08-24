/*
 * main.c - Camera_AI_Test1
 *
 * Default build: OV7670 capture (via SmartDMA, J9 header) -> Edge Impulse
 * FOMO inference (source/ai/model_runner.cpp, closed_eye/open_eye/yawning)
 * -> LCD shows a solid status color (see DEMO_ShowStatusColor()), no USB.
 * See WORKLOG.md "LCD history" for why Arduino-header bit-bang (not
 * J8/FlexIO) is the active default.
 *
 * Live camera image display on the LCD is DISABLED (see
 * DEMO_DrawAiBoxes()/bbox_overlay.h below, and the old
 * memcpy()+LCD_DrawImage(camera frame) call, both commented out) - RAM
 * optimization: pushing/drawing on top of a full 320x240 frame needed a
 * second 150KB framebuffer (s_lcdSnapshot) just for tearing-free display.
 * That memory was first repurposed as a 150KB AI overflow pool, but the
 * arena fits in the primary m_sramx pool without it (see s_aiScratchPool
 * below) - most of it now just sits free in m_data, big enough for the
 * larger CPU stack TFLite Micro/CMSIS-NN needs (see __stack_size__ in
 * CMakeLists.txt).
 *
 * USB Video Class (UVC) streaming over USB High-Speed (source/usb/) is
 * ABANDONED - SmartDMA camera capture and the USB HS PHY need mutually
 * exclusive DCDC voltage levels on this chip, and this board's one USB
 * connector is hard-wired to the HS controller only, so there's no
 * software-only fix. Still builds (opt-in via CMakeLists.txt's
 * USB_STREAM_DIAGNOSTIC_DISABLE=OFF); runs time-multiplexed (periodic
 * Mid-voltage recapture / Overdrive-streaming switching) - see the #else
 * branch below and WORKLOG.md.
 */

#include <string.h>
#include "app.h"
/* #include "bbox_overlay.h" - only needed by the disabled image-display
 * path, see DEMO_DrawAiBoxes() below. */
#include "board.h"
#include "camera_capture.h"
#include "ei_sramx_alloc.h"
#include "fsl_common.h"
#include "fsl_debug_console.h"
#include "lcd_display.h"
#include "model_runner.h"
#include "usb_video_camera.h"

/* RGB565. Priority: CLOSED_EYE/YAWNING (red, most safety-critical) >
 * OPEN_EYE (green, normal) > nothing detected this frame (blue, idle). */
static uint16_t DEMO_StatusColorForResult(const ai_model_result_t *aiResult) {
  bool sawOpenEye = false;

  for (uint32_t i = 0; i < aiResult->boxCount; i++) {
    const char *label = aiResult->boxes[i].label;
    if ((strcmp(label, "closed_eye") == 0) || (strcmp(label, "yawning") == 0)) {
      return 0xF800U; /* red - drowsiness alert */
    }
    if (strcmp(label, "open_eye") == 0) {
      sawOpenEye = true;
    }
  }
  return sawOpenEye ? 0x07E0U /* green - awake */ : 0x001FU /* blue - no detection this frame */;
}

/* Fills the whole LCD with one solid color - a small (320-pixel, 640B)
 * line buffer pushed DEMO_BUFFER_HEIGHT times, instead of needing a full
 * 320x240 framebuffer like the old live-image path did.
 *
 * Must use LCD_PushPixelsOpen()/LCD_EndWindow(), NOT LCD_PushPixels() in
 * a loop: LCD_PushPixels() closes the transfer (deasserts CS) every time
 * it's called, so a loop of plain LCD_PushPixels() calls only actually
 * writes the FIRST row to the panel - every later call sends its bytes
 * with CS already closed, so the panel ignores them. Symptom on real
 * hardware: only one line of solid color (looked like a single vertical
 * stripe due to this panel's MADCTL MV=1 row/column swap), the rest of
 * the screen keeps whatever was already in GRAM (stale black/white
 * pattern). */
static void DEMO_ShowStatusColor(uint16_t color) {
  static uint16_t s_statusLine[DEMO_BUFFER_WIDTH];
  for (uint16_t i = 0; i < DEMO_BUFFER_WIDTH; i++) {
    s_statusLine[i] = color;
  }
  LCD_SetWindow(0U, 0U, DEMO_BUFFER_WIDTH - 1U, DEMO_BUFFER_HEIGHT - 1U);
  for (uint16_t row = 0U; row < DEMO_BUFFER_HEIGHT; row++) {
    LCD_PushPixelsOpen(s_statusLine, DEMO_BUFFER_WIDTH);
  }
  LCD_EndWindow();
}

#if 0
/* Disabled - see the file-level comment above. Kept for reference in case
 * live image + bounding-box display is revisited once the AI model's RAM
 * footprint is smaller (e.g. a lower-resolution retrain). Bounding boxes
 * are in the model's own input space (64x64, see
 * AI_MODEL_GetInputWidth/Height) - scale to the camera frame (320x240)
 * before drawing. */
static uint16_t DEMO_ColorForLabel(const char *label) {
  if (strcmp(label, "closed_eye") == 0) {
    return 0xF800U; /* red */
  } else if (strcmp(label, "open_eye") == 0) {
    return 0x07E0U; /* green */
  } else if (strcmp(label, "yawning") == 0) {
    return 0xFD20U; /* orange */
  }
  return 0xFFFFU; /* white */
}

static void DEMO_DrawAiBoxes(uint16_t *lcdBuffer, uint16_t bufWidth,
                             uint16_t bufHeight,
                             const ai_model_result_t *aiResult) {
  uint16_t modelWidth = AI_MODEL_GetInputWidth();
  uint16_t modelHeight = AI_MODEL_GetInputHeight();

  for (uint32_t i = 0; i < aiResult->boxCount; i++) {
    const ai_bbox_t *box = &aiResult->boxes[i];
    int x = (int)box->x * (int)bufWidth / (int)modelWidth;
    int y = (int)box->y * (int)bufHeight / (int)modelHeight;
    int w = (int)box->width * (int)bufWidth / (int)modelWidth;
    int h = (int)box->height * (int)bufHeight / (int)modelHeight;

    BBOX_DrawRect(lcdBuffer, bufWidth, bufHeight, x, y, w, h,
                  DEMO_ColorForLabel(box->label));
  }
}
#endif

/*
 * Cheap "is the camera actually sending real image data" signature:
 * min/max/average over a strided pixel sample. A dead/disconnected sensor
 * tends to produce a flat buffer (min==max, constant avg); a live one
 * doesn't.
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
/* Everything below, down to DEMO_CaptureFramesAtMidVoltage(), only exists
 * for the abandoned USB-streaming path - see the file-level comment above.
 *
 * Camera runs at Mid voltage - capture this many frames before stopping
 * SmartDMA and switching to Overdrive/USB. More than 1 so auto-exposure/
 * auto-gain have a few frames to converge (frame #1 tends to come back
 * flat/underexposed).
 */
#define DEMO_MID_VOLTAGE_WARMUP_FRAMES 10U

/*
 * PERIODIC REFRESH: how long to stay at Overdrive/streaming before
 * dropping back to Mid to grab a fresh frame. Confirmed stable at 5000 ms
 * on real hardware (see WORKLOG.md). Keep this comfortably longer than
 * DEMO_MID_VOLTAGE_WARMUP_FRAMES takes to capture plus USB enumeration
 * overhead, or USB barely gets a turn.
 */
#define DEMO_OVERDRIVE_HOLD_MS 5000U

/* Capture (or re-capture) DEMO_MID_VOLTAGE_WARMUP_FRAMES frames at Mid
 * voltage, log the last one, then stop SmartDMA. Caller must already be at
 * DCDC Mid before calling this. */
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
#if DEMO_LCD_ARDUINO_HEADER
  PRINTF("Display: Arduino-header LCD live preview (camera + AI hook)\r\n\r\n");
#else
  PRINTF("Display: J8 LCD live preview (camera + AI hook)\r\n\r\n");
#endif
#else
  PRINTF("Display: USB Video Class (UVC) webcam over USB High-Speed "
         "(abandoned path, time-multiplexed - see WORKLOG.md)\r\n\r\n");
#endif

  CAMERA_CAPTURE_Init();
  AI_MODEL_Init();

#if DEMO_USB_STREAM_DISABLE
  /* Default build: camera + AI loop, continuous, DCDC stays at Mid the
   * whole time. LCD shows a solid status color (see
   * DEMO_ShowStatusColor()) instead of the live camera image - see the
   * file-level comment above for why. */
  LCD_Init();

  /* Dedicated AI scratch pool - a small overflow area for
   * ei_sramx_alloc.c's allocator, used only if the primary 96KB m_sramx
   * pool isn't enough. It normally is: EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE
   * is 92876 bytes, comfortably inside the ~94KB primary pool (96KB minus
   * the LIFO free-record stack), so this is just a safety margin for the
   * DSP resize step's small per-page scratch buffers, not the tensor
   * arena itself.
   *
   * Was sized DEMO_BUFFER_WIDTH*DEMO_BUFFER_HEIGHT*sizeof(uint16_t)
   * (150KB, reusing the old s_lcdSnapshot buffer's space) - that starved
   * m_data of room to grow the CPU stack (see the STKOF fix below), and
   * per the arena-size math above was never actually needed at that size.
   * 16KB is generous headroom over actual DSP scratch usage. */
  static uint8_t s_aiScratchPool[16U * 1024U] __attribute__((aligned(16)));
  EI_SRAMX_SetOverflowPool(s_aiScratchPool, sizeof(s_aiScratchPool));

  while (1) {
    if (CAMERA_CAPTURE_IsFrameReady()) {
      CAMERA_CAPTURE_ClearFrameReady();

      uint16_t *frame = CAMERA_CAPTURE_GetFrameBuffer();
      uint32_t frameNumber = CAMERA_CAPTURE_GetFrameCount();

      /* Every 15th frame (~2x/sec at 30fps) so the log stays readable. */
      if ((frameNumber % 15U) == 1U) {
        CAMERA_CAPTURE_LogFrameSignature(frameNumber, frame);
      }

      /* Stop SmartDMA before inference: SMARTDMA_CAMERA_MEM_ADDR (the
       * coprocessor's own firmware/working RAM) is 0x04000000 - the same
       * physical bank as m_sramx, which is where the AI tensor arena
       * lives (ei_sramx_alloc.c's s_pool). Leaving SmartDMA running while
       * the arena writes into that bank corrupts its firmware/state; it
       * doesn't fault, it just silently stops delivering frame-ready
       * interrupts after a few frames (see WORKLOG.md "Bug #3"). Mirrors
       * the same capture/deinit/inference/reinit pattern already used by
       * the USB-streaming build below (DEMO_CaptureFramesAtMidVoltage()) -
       * capture and heavy compute already can't overlap on this chip. */
      CAMERA_CAPTURE_Deinit();

      ai_model_result_t aiResult;
      AI_MODEL_RunInference(frame, DEMO_BUFFER_WIDTH, DEMO_BUFFER_HEIGHT,
                            &aiResult);

      CAMERA_CAPTURE_Reinit();

      if (aiResult.valid) {
        for (uint32_t i = 0; i < aiResult.boxCount; i++) {
          const ai_bbox_t *box = &aiResult.boxes[i];
          /* debug_console_lite may not support %f, so print score as a
           * percentage integer. */
          PRINTF("AI result: box[%u] label=%s x=%u y=%u w=%u h=%u score=%d%%\r\n",
                 i, box->label, box->x, box->y, box->width, box->height,
                 (int)(box->score * 100.0f));
        }
      }

      DEMO_ShowStatusColor(DEMO_StatusColorForResult(&aiResult));
    }
  }
#else
  /* USB streaming build: time-multiplexed (see the file-level comment
   * above). Mid-voltage capture phase first - wait for a few settled
   * frames, then stop SmartDMA (capture and USB HS can't run at the same
   * time on this chip). */
  DEMO_CaptureFramesAtMidVoltage();
  PRINTF("Camera: switching to Overdrive for USB.\r\n");

  ai_model_result_t aiResult;
  AI_MODEL_RunInference(CAMERA_CAPTURE_GetFrameBuffer(), DEMO_BUFFER_WIDTH,
                        DEMO_BUFFER_HEIGHT, &aiResult);
  if (aiResult.valid) {
    for (uint32_t i = 0; i < aiResult.boxCount; i++) {
      const ai_bbox_t *box = &aiResult.boxes[i];
      /* debug_console_lite may not support %f, so print score as a
       * percentage integer. */
      PRINTF("AI result: box[%u] label=%s x=%u y=%u w=%u h=%u score=%d%%\r\n",
             i, box->label, box->x, box->y, box->width, box->height,
             (int)(box->score * 100.0f));
    }
  }

  /* USB_DeviceClockInit() (full PHY/PLL bring-up + enumeration) runs
   * exactly ONCE here. The periodic refresh loop below only calls the
   * lighter regulator-level helpers afterwards - see hardware_init.c. */
  USB_DeviceClockInit();
  USB_VideoCamera_Init();

  while (1) {
    USB_VideoCamera_Task();

    /* PERIODIC REFRESH - see the DEMO_OVERDRIVE_HOLD_MS comment above. */
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
