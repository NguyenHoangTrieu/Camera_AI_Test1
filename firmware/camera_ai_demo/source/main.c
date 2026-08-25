/*
 * main.c - Camera_AI_Test1
 *
 * Default build: OV7670 capture (via SmartDMA, J9 header, 320x240 RGB565
 * - see app.h/camera_capture.c) -> Edge Impulse FOMO face-detection
 * inference (source/ai/model_runner.cpp or model_runner_npu.cpp,
 * single class `face`) -> LCD shows one text status line ("FACE: 1"/
 * "FACE: 0" for "a face was/wasn't detected this frame" - see
 * DEMO_DrawStatusLine()), no USB. See WORKLOG.md "LCD history" for why
 * Arduino-header bit-bang (not J8/FlexIO) is the active default.
 *
 * Earlier revisions of this file tried drawing the live camera frame with
 * bounding boxes on the LCD (source/display/bbox_overlay.c/h) - dropped in
 * favor of this plain text status readout: much less data to push over
 * the bit-bang LCD bus per frame, and the 3 lines are what's actually
 * useful for a drowsiness-alert readout. bbox_overlay.c/h is kept in the
 * tree (unused, still builds) in case box-on-image display is revisited.
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

#include <stdbool.h>
#include <string.h>
#include "app.h"
#include "board.h"
#include "camera_capture.h"
#include "ei_sramx_alloc.h"
#include "font5x7.h"
#include "fsl_common.h"
#include "fsl_debug_console.h"
#include "lcd_display.h"
#include "model_runner.h"
#include "text_overlay.h"
#include "usb_video_camera.h"

#if !DEMO_LCD_CAMERA_PREVIEW
/* Text status readout - see the file-level comment above. A single fixed-
 * width label + ": " + a '1'/'0' digit for whether a face was detected
 * this frame, scale=3 -> 15x21px per glyph, comfortably inside the
 * 320x240 panel. lineIndex is kept as a parameter (not hardcoded to 0)
 * in case a second status line is ever added back. Unused (and left out
 * of the build) in the raw camera-preview build - see
 * DEMO_LCD_CAMERA_PREVIEW in main(). */
#define DEMO_STATUS_TEXT_SCALE 3U
#define DEMO_STATUS_LINE_X 8U
#define DEMO_STATUS_LINE_Y0 40U
#define DEMO_STATUS_LINE_GAP_PX 14U

static void DEMO_DrawStatusLine(uint16_t lineIndex, const char *paddedLabel, bool detected) {
  char text[16];
  size_t n = strlen(paddedLabel);

  memcpy(text, paddedLabel, n);
  text[n] = ':';
  text[n + 1U] = ' ';
  text[n + 2U] = detected ? '1' : '0';
  text[n + 3U] = '\0';

  uint16_t lineHeight = (uint16_t)((FONT5X7_HEIGHT * DEMO_STATUS_TEXT_SCALE) + DEMO_STATUS_LINE_GAP_PX);
  uint16_t y = (uint16_t)(DEMO_STATUS_LINE_Y0 + lineIndex * lineHeight);
  uint16_t fgColor = detected ? 0x07E0U /* green - detected this frame */ : 0x7BEFU /* mid gray - not detected */;

  TEXT_DrawString(DEMO_STATUS_LINE_X, y, text, fgColor, 0x0000U /* black background */, DEMO_STATUS_TEXT_SCALE);
}
#endif /* !DEMO_LCD_CAMERA_PREVIEW */

/* Fills the whole LCD with one solid color - called once at startup so
 * old/garbage GRAM content doesn't show around the text lines. Not used
 * per-frame (see DEMO_DrawStatusLine() above, which only repaints its own
 * small line band each frame).
 *
 * Must use LCD_PushPixelsOpen()/LCD_EndWindow(), NOT LCD_PushPixels() in
 * a loop: LCD_PushPixels() closes the transfer (deasserts CS) every time
 * it's called, so a loop of plain LCD_PushPixels() calls only actually
 * writes the FIRST row to the panel - every later call sends its bytes
 * with CS already closed, so the panel ignores them. */
static void DEMO_ClearScreen(uint16_t color) {
  static uint16_t s_clearLine[DEMO_PANEL_WIDTH];
  for (uint16_t i = 0U; i < DEMO_PANEL_WIDTH; i++) {
    s_clearLine[i] = color;
  }
  LCD_SetWindow(0U, 0U, DEMO_PANEL_WIDTH - 1U, DEMO_PANEL_HEIGHT - 1U);
  for (uint16_t row = 0U; row < DEMO_PANEL_HEIGHT; row++) {
    LCD_PushPixelsOpen(s_clearLine, DEMO_PANEL_WIDTH);
  }
  LCD_EndWindow();
}

#if !DEMO_LCD_CAMERA_PREVIEW
/*
 * Cheap "is the camera actually sending real image data" signature:
 * min/max/average over a strided pixel sample. A dead/disconnected sensor
 * tends to produce a flat buffer (min==max, constant avg); a live one
 * doesn't. Unused in the raw camera-preview build (main() pushes the
 * frame straight to the LCD there, no need for a numeric signature).
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
#endif /* !DEMO_LCD_CAMERA_PREVIEW */

#if !DEMO_USB_STREAM_DISABLE && !DEMO_LCD_CAMERA_PREVIEW
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
#endif /* !DEMO_USB_STREAM_DISABLE && !DEMO_LCD_CAMERA_PREVIEW */

int main(void) {
  BOARD_InitHardware();

  PRINTF("\r\nCamera_AI_Test1 - FRDM-MCXN947\r\n");
  PRINTF("Camera: OV7670 on J9 SmartDMA/Camera header\r\n");

#if DEMO_LCD_CAMERA_PREVIEW
  /* Lens-focus diagnostic build (-DLCD_CAMERA_PREVIEW=ON): no AI, no
   * status text - just the raw camera feed pushed straight to the LCD as
   * fast as frames arrive, so the image can be focused by eye. Camera
   * resolution (app.h) matches the panel 1:1, so no scaling needed.
   * SmartDMA never has to stop/restart here (no AI tensor arena writes to
   * conflict with, unlike the normal loop below), so this runs at the
   * camera's native ~30fps rather than being inference-latency-bound. */
  PRINTF("Display: raw camera preview on LCD, no AI (LCD_CAMERA_PREVIEW=ON) "
         "- for focusing the lens\r\n\r\n");

  CAMERA_CAPTURE_Init();
  LCD_Init();
  DEMO_ClearScreen(0x0000U);

  while (1) {
    if (CAMERA_CAPTURE_IsFrameReady()) {
      CAMERA_CAPTURE_ClearFrameReady();
      LCD_DrawImage(0U, 0U, DEMO_BUFFER_WIDTH, DEMO_BUFFER_HEIGHT,
                    CAMERA_CAPTURE_GetFrameBuffer());
    }
  }
#else

#if DEMO_USB_STREAM_DISABLE
#if DEMO_LCD_ARDUINO_HEADER
  PRINTF("Display: Arduino-header LCD status text (camera + AI hook)\r\n\r\n");
#else
  PRINTF("Display: J8 LCD status text (camera + AI hook)\r\n\r\n");
#endif
#else
  PRINTF("Display: USB Video Class (UVC) webcam over USB High-Speed "
         "(abandoned path, time-multiplexed - see WORKLOG.md)\r\n\r\n");
#endif

  CAMERA_CAPTURE_Init();
  AI_MODEL_Init();

#if DEMO_USB_STREAM_DISABLE
  /* Default build: camera + AI loop, continuous, DCDC stays at Mid the
   * whole time. LCD shows 3 fixed text status lines - see the file-level
   * comment above. */
  LCD_Init();
  DEMO_ClearScreen(0x0000U);

#if !DEMO_AI_MODEL_USE_NPU
  /* Dedicated AI scratch pool - overflow area for ei_sramx_alloc.c's
   * allocator, used once the primary 96KB m_sramx pool is exhausted.
   * IMPORTANT (learned the hard way, see WORKLOG.md's top entry): the
   * tensor arena is allocated as ONE single ei_calloc() call, and
   * ei_sramx_alloc.c's two-tier allocator can only satisfy a single
   * allocation that fits ENTIRELY within one tier - it cannot split one
   * allocation across primary (m_sramx) + this overflow pool (m_data),
   * since those are physically non-contiguous memory banks (0x04000000
   * vs 0x20000000) that a single C pointer can't span. So this pool
   * alone, not "primary + this combined", must be >= whatever
   * EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE is (currently 112,460 bytes,
   * this project's deploy-version-2 face model, 72x72 input - the
   * deploy-version-1 96x96 model needed 185,036 bytes here, which
   * didn't fit *at all* no matter how this pool was sized, since
   * 185,036 + the 153,600-byte camera frame buffer alone exceeds
   * m_data's entire 312KB stock capacity - that model had to be
   * retrained smaller, not worked around in firmware). 120KB leaves
   * ~10KB of margin over the current model's bare 112,460-byte
   * requirement for the DSP resize step's small per-page scratch
   * buffers - if AllocateTensors()/ei_calloc() still fails at runtime
   * (see EI_SRAMX_GetHighWaterMark() in model_runner.cpp's error path),
   * there's ~43KB of further headroom free in m_data to grow this
   * (153,600 + 122,880 = 276,480 of 319,488 bytes stock m_data - see the
   * comment on kTensorArenaSize in model_runner_npu.cpp for the matching
   * NPU-side budget math). Only declared for the CPU/CMSIS-NN build -
   * the NPU build (model_runner_npu.cpp) never calls into
   * ei_sramx_alloc.c, so this would just be dead weight competing with
   * that build's own static tensor arena in the same m_data budget. */
  static uint8_t s_aiScratchPool[120U * 1024U] __attribute__((aligned(16)));
  EI_SRAMX_SetOverflowPool(s_aiScratchPool, sizeof(s_aiScratchPool));
#endif /* !DEMO_AI_MODEL_USE_NPU */

  /* Set right after CAMERA_CAPTURE_Reinit() below, cleared once the
   * following frame has been consumed. DIAGNOSTIC (2026-08-25): every
   * frame logged by CAMERA_CAPTURE_LogFrameSignature() in this loop was
   * observed reading back as completely flat (min==max==avg==0x0000)
   * on real hardware, every time, even confirmed after a genuine power
   * cycle (not just a probe-triggered reset) - but the exact same
   * Deinit()/inference/Reinit() cycle at a similarly fast cadence was
   * confirmed working (real, varying pixel data) with the project's
   * earlier 3-class model, and camera_capture.c itself is byte-identical
   * to that working version. Working theory being tested here: SmartDMA
   * needs the frame immediately following a fresh CAMERA_CAPTURE_Reinit()
   * to fully (re-)synchronize with the OV7670's HREF/VSYNC/PCLK timing,
   * and that very first post-reinit frame isn't trustworthy - discard it
   * and use the *second* frame after each reinit instead. If this fixes
   * it, the frame rate this loop can consume is effectively halved (two
   * real camera frames spent per inference cycle instead of one) - if it
   * does NOT fix it, this diagnostic comment/flag should be removed and
   * the investigation continued elsewhere (see WORKLOG.md). */
  bool skipNextFrame = false;

  while (1) {
    if (CAMERA_CAPTURE_IsFrameReady()) {
      CAMERA_CAPTURE_ClearFrameReady();

      if (skipNextFrame) {
        skipNextFrame = false;
        continue;
      }

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
      skipNextFrame = true;

      bool sawFace = false;
      if (aiResult.valid) {
        for (uint32_t i = 0; i < aiResult.boxCount; i++) {
          const ai_bbox_t *box = &aiResult.boxes[i];
          if (strcmp(box->label, "face") == 0) {
            sawFace = true;
          }
          /* debug_console_lite may not support %f, so print score as a
           * percentage integer. */
          PRINTF("AI result: box[%u] label=%s x=%u y=%u w=%u h=%u score=%d%%\r\n",
                 i, box->label, box->x, box->y, box->width, box->height,
                 (int)(box->score * 100.0f));
        }
      }

      /* Text draw doesn't touch the camera frame buffer at all, so unlike
       * the earlier live-image display it has no ordering dependency on
       * CAMERA_CAPTURE_Deinit()/Reinit() above. */
      DEMO_DrawStatusLine(0U, "FACE      ", sawFace);
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
#endif /* DEMO_LCD_CAMERA_PREVIEW */
}
