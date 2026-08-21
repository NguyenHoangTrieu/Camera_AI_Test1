/*
 * main.c - Camera_AI_Test1
 *
 * Capture loop: OV7670 (via SmartDMA, J9 header) -> AI model hook (stub,
 * see source/ai/model_runner.c) -> J8 FlexIO/ST7796S TFT (hardware-
 * accelerated 16-bit parallel bus).
 */

#include "app.h"
#include "board.h"
#include "fsl_debug_console.h"
#include "camera_capture.h"
#include "lcd_display.h"
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
    PRINTF("Display: HSD024131-C1 TFT (ILI9341-family) on the J8 header\r\n\r\n");

    LCD_Init();
    CAMERA_CAPTURE_Init();
    AI_MODEL_Init();

    /* Blank the panel so stale RAM contents don't flash on screen. */
    static uint16_t s_blankLine[DEMO_PANEL_WIDTH];
    for (uint16_t y = 0; y < DEMO_PANEL_HEIGHT; y++)
    {
        LCD_DrawImage(0, y, DEMO_PANEL_WIDTH, 1, s_blankLine);
    }

    while (1)
    {
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

            /* Camera buffer (320x240) drawn into the panel's top-left corner
             * as-is - see "Adjusting for a different panel resolution" in
             * README.md to scale/center it instead. */
            LCD_DrawImage(0, 0, DEMO_BUFFER_WIDTH, DEMO_BUFFER_HEIGHT, frame);
        }
    }
}
