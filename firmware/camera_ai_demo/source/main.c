/*
 * main.c - Camera_AI_Test1
 *
 * Capture loop: OV7670 (via SmartDMA, J9 header) -> AI model hook (stub,
 * see source/ai/model_runner.c) -> USB Video Class (UVC) over USB
 * High-Speed (source/usb/usb_video_camera.c), so the board shows up as a
 * standard webcam on the host PC - no LCD, no vendor driver needed. See
 * WORKLOG.md for why this replaced the original J8 FlexIO/TFT display path
 * (the LCD driver code is still in source/display/, just unused now).
 *
 * The actual frame delivery to the host isn't driven from this loop -
 * USB_VideoCamera_Init() registers a class callback
 * (kUSB_DeviceVideoEventStreamSendResponse in usb_video_camera.c) that
 * pulls straight from CAMERA_CAPTURE_GetFrameBuffer() and converts to YUY2
 * on demand, each time the host is ready for the next USB packet. This
 * loop's job is just the AI hook and the periodic debug log, same as
 * before.
 */

#include "app.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "camera_capture.h"
#include "usb_video_camera.h"
#include "model_runner.h"

/*
 * Cheap "is the camera actually sending real image data" signature: min/max/
 * average over a strided sample of pixels (not the whole 76800-pixel frame,
 * to keep this fast). A dead/disconnected sensor tends to produce a flat
 * buffer (all-0x0000 or a fixed pattern), so min==max and avg is constant
 * frame to frame. A live camera pointed at anything but a perfectly uniform
 * surface produces min!=max, and the avg drifts as the scene changes.
 */
static void CAMERA_CAPTURE_LogFrameSignature(uint32_t frameNumber, const uint16_t *frame)
{
    const uint32_t pixelCount = (uint32_t)DEMO_BUFFER_WIDTH * DEMO_BUFFER_HEIGHT;
    const uint32_t stride     = 97U; /* prime, avoids lining up with row width */
    uint16_t minPixel         = 0xFFFFU;
    uint16_t maxPixel         = 0x0000U;
    uint32_t sum              = 0U;
    uint32_t samples          = 0U;

    for (uint32_t i = 0; i < pixelCount; i += stride)
    {
        uint16_t p = frame[i];
        if (p < minPixel)
        {
            minPixel = p;
        }
        if (p > maxPixel)
        {
            maxPixel = p;
        }
        sum += p;
        samples++;
    }

    PRINTF("Camera: frame #%u ready, %u samples, pixel range 0x%04X..0x%04X, avg=0x%04X%s\r\n", frameNumber,
           samples, minPixel, maxPixel, (uint16_t)(sum / samples),
           (minPixel == maxPixel) ? " (flat - lens cap on, or no real image data)" : "");
}

int main(void)
{
    BOARD_InitHardware();

    PRINTF("\r\nCamera_AI_Test1 - FRDM-MCXN947\r\n");
    PRINTF("Camera: OV7670 on J9 SmartDMA/Camera header\r\n");
    PRINTF("Display: USB Video Class (UVC) webcam over USB High-Speed\r\n\r\n");

    CAMERA_CAPTURE_Init();
    AI_MODEL_Init();
#if !DEMO_USB_STREAM_DISABLE
    USB_VideoCamera_Init();
#endif

    while (1)
    {
#if !DEMO_USB_STREAM_DISABLE
        USB_VideoCamera_Task();
#endif

        if (CAMERA_CAPTURE_IsFrameReady())
        {
            CAMERA_CAPTURE_ClearFrameReady();

            uint16_t *frame      = CAMERA_CAPTURE_GetFrameBuffer();
            uint32_t frameNumber = CAMERA_CAPTURE_GetFrameCount();

            /* Every 15th frame (~2x/sec at 30fps) so the log stays readable
             * instead of flooding the 115200-baud UART every frame. */
            if ((frameNumber % 15U) == 1U)
            {
                CAMERA_CAPTURE_LogFrameSignature(frameNumber, frame);
            }

            ai_model_result_t aiResult;
            AI_MODEL_RunInference(frame, DEMO_BUFFER_WIDTH, DEMO_BUFFER_HEIGHT, &aiResult);
            if (aiResult.valid)
            {
                /* debug_console_lite may not support %f, so print score as a percentage integer. */
                PRINTF("AI result: class=%d score=%d%%\r\n", aiResult.classId, (int)(aiResult.score * 100.0f));
            }
        }
    }
}
