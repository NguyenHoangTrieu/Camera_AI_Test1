/*
 * main_core1.c - Camera_AI_Test1 dual-core RTOS migration (see WORKLOG.md).
 *
 * Stage 1 (confirmed on real hardware): print a banner once released by
 * core0.
 * Stage 2 (confirmed on real hardware): FreeRTOS scheduler + one MCMGR
 * event round-trip - proved the ISR-safe IPC design works; that demo task
 * is retired now that Stage 3 has real work for this core.
 * Stage 3 (confirmed on real hardware): camera capture + LCD preview,
 * ported from the legacy main.c's DEMO_LCD_CAMERA_PREVIEW loop - same
 * tearing fix (Deinit/Reinit around the LCD push + skipNextFrame), same
 * fps diagnostic, running as a single FreeRTOS task (not split across two
 * tasks yet - splitting capture and push into separate tasks would
 * reintroduce the exact single-buffer race this project already fixed
 * once, see ARCHITECTURE.md/WORKLOG.md).
 * Stage 4 (confirmed on real hardware): SD card snapshot (source/storage/
 * snapshot.c, sd_spi_disk.c) as a standalone StorageTask, manually
 * triggered every few seconds with a synthesized "face" box (AI wasn't
 * wired up yet) - proved SD/FatFs mechanics work correctly when driven by
 * a FreeRTOS task instead of a bare-metal loop.
 * Stage 5 (this revision): real AI results from core0, over the
 * frame-ready/result-ready doorbell round trip (source/shared/
 * ipc_events.h). StorageTask is RETIRED (exactly as its own Stage 4
 * comments predicted) - SNAPSHOT_Init()/SNAPSHOT_OnFrame() move into
 * CameraLcdTask's own per-frame loop, replacing the fake periodic trigger
 * with the real "is there actually a face in THIS frame" signal, same
 * sequencing the legacy single-core main.c's AI loop always used
 * (Deinit -> inference -> snapshot -> Reinit) - now with "inference"
 * meaning "ask core0 and wait for its answer" instead of a local call. */
#include <string.h>
#include "fsl_debug_console.h"
#include "board.h"
#include "app.h"
#include "mcmgr.h"
#include "FreeRTOS.h"
#include "task.h"
#include "camera_capture.h"
#include "lcd_display.h"
#include "bbox_overlay.h"
#include "snapshot.h"
#include "spi1_bus.h"
#include "ipc_layout.h"
#include "ipc_events.h"

/* Model's fixed input resolution - AI_MODEL_GetInputWidth/Height()
 * (model_runner.h) can't be called from core1: their implementation
 * (model_runner_npu.cpp/model_runner.cpp) only builds into core0's image
 * (see CMakeLists.txt's DUALCORE_RTOS branch - core1 links model_runner.h
 * for its TYPES only, same as Stage 4's SNAPSHOT_OnFrame() call already
 * hardcoded this exact value). Matches the model actually deployed on
 * core0 (see its own "AI_MODEL_Init: ... (72x72 input...)" boot log) -
 * would need updating by hand if the model is ever retrained to a
 * different input size, same as any other cross-core constant in
 * ipc_layout.h. */
#define AI_MODEL_INPUT_WIDTH  72U
#define AI_MODEL_INPUT_HEIGHT 72U

/* Generous vs. the ~3.9ms NPU inference time actually measured (see
 * WORKLOG.md's NPU bring-up entry) - same "fail loud, don't hang forever"
 * philosophy as this project's other cross-boundary waits
 * (SD_SPI_INIT_TIMEOUT_MS, SPI1_BUS_DMA_TIMEOUT_MS). A timeout here means
 * core0 didn't answer in time (e.g. still starting up) - this frame just
 * skips the AI overlay/snapshot check, not a hang. */
#define AI_RESULT_TIMEOUT_MS 100U

/* Fills the whole LCD with one solid color - identical to main.c's
 * DEMO_ClearScreen(), copied rather than shared since main.c stays the
 * legacy single-core entry point (see CMakeLists.txt's DUALCORE_RTOS
 * branch - the two builds don't share source files, only headers/drivers). */
static void DEMO_ClearScreen(uint16_t color)
{
    static uint16_t s_clearLine[DEMO_PANEL_WIDTH];
    for (uint16_t i = 0U; i < DEMO_PANEL_WIDTH; i++)
    {
        s_clearLine[i] = color;
    }
    /* See spi1_bus.h's SPI1_BUS_Lock() comment (WORKLOG.md, Stage 4) - this
     * whole multi-call sequence must be atomic against StorageTask. */
    SPI1_BUS_Lock();
    LCD_SetWindow(0U, 0U, DEMO_PANEL_WIDTH - 1U, DEMO_PANEL_HEIGHT - 1U);
    for (uint16_t row = 0U; row < DEMO_PANEL_HEIGHT; row++)
    {
        LCD_PushPixelsOpen(s_clearLine, DEMO_PANEL_WIDTH);
    }
    LCD_EndWindow();
    SPI1_BUS_Unlock();
}

/* Diagnostic (WORKLOG.md, Stage 4 follow-up): the fps counter below only
 * measures how often CAMERA_CAPTURE_IsFrameReady()+LCD_DrawImage()
 * complete, NOT whether the pixel data is real - it would keep counting
 * "frames" even if the buffer were stuck at its initial all-zero (black)
 * state forever. Same min/max/avg signature technique as the legacy
 * main.c's CAMERA_CAPTURE_LogFrameSignature() (see WORKLOG.md's original
 * "is the camera actually sending real image data" entry) - a flat
 * min==max reading is the tell for dead/never-written data. */
static void DEMO_LogFrameSignature(const uint16_t *frame)
{
    const uint32_t pixelCount = (uint32_t)DEMO_BUFFER_WIDTH * DEMO_BUFFER_HEIGHT;
    const uint32_t stride     = 97U;
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

    PRINTF("Camera: %u samples, pixel range 0x%04X..0x%04X, avg=0x%04X%s\r\n", samples, minPixel, maxPixel,
           (uint16_t)(sum / samples), (minPixel == maxPixel) ? " (flat - dead/no data)" : "");
}

/* Stage 5: converts the wire-safe ai_ipc_result_t (source/shared/
 * ipc_layout.h - plain data, no cross-core pointers) read from shared RAM
 * into the ai_model_result_t shape snapshot.h/bbox drawing already expect.
 * `out`'s box label pointers point INTO `ipc` - `ipc` must stay in scope
 * for as long as `out` is used (both are always local, same-scope
 * variables at every call site below, never returned or stored). */
static void ConvertIpcResult(const ai_ipc_result_t *ipc, ai_model_result_t *out)
{
    out->valid    = ipc->valid;
    out->boxCount = (ipc->boxCount > AI_MODEL_MAX_BOXES) ? AI_MODEL_MAX_BOXES : ipc->boxCount;
    for (uint32_t i = 0; i < out->boxCount; i++)
    {
        out->boxes[i].label  = ipc->boxes[i].label;
        out->boxes[i].x      = ipc->boxes[i].x;
        out->boxes[i].y      = ipc->boxes[i].y;
        out->boxes[i].width  = ipc->boxes[i].width;
        out->boxes[i].height = ipc->boxes[i].height;
        out->boxes[i].score  = ipc->boxes[i].score;
    }
}

static void CameraLcdTask(void *pvParameters)
{
    (void)pvParameters;

    CAMERA_CAPTURE_Init();
    LCD_Init();
    DEMO_ClearScreen(0x0000U);

    /* Moved here from the retired StorageTask (Stage 4) - see this file's
     * Stage 5 header comment. NOT wrapped in an outer lock - see
     * sd_spi_disk.c's disk_initialize() comment (WORKLOG.md, Stage 4
     * follow-up): every attempt to lock this whole call made SD card mount
     * fail consistently - reverted to the narrow, tight per-diskio-call
     * locking that's confirmed working for mount instead of guessing
     * further. */
    SNAPSHOT_Init();

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t fpsFrameCount      = 0U;
    uint32_t fpsWindowStartCycle = DWT->CYCCNT;
    uint16_t frameSeq            = 0U;

    /* Set right after CAMERA_CAPTURE_Reinit(), cleared once the following
     * frame has been consumed - SmartDMA needs one cycle to resync with
     * the sensor's HREF/VSYNC/PCLK timing after a fresh Reinit(), same
     * proven workaround as the legacy main.c loop (see WORKLOG.md's LCD
     * tearing fix entry). */
    bool skipNextFrame = false;

    for (;;)
    {
        if (CAMERA_CAPTURE_IsFrameReady())
        {
            CAMERA_CAPTURE_ClearFrameReady();

            if (skipNextFrame)
            {
                skipNextFrame = false;
                continue;
            }

            /* Stop SmartDMA before reading the frame buffer, restart it
             * after - tearing fix, see WORKLOG.md. Stage 5 extends this
             * same "buffer only stable while SmartDMA is stopped" window
             * to cover core0's AI read too - the round trip below must
             * fully finish before CAMERA_CAPTURE_Reinit() runs. */
            CAMERA_CAPTURE_Deinit();
            uint16_t *frame = CAMERA_CAPTURE_GetFrameBuffer();

            frameSeq++;
            IPC_SignalFrameReady(frameSeq);

            uint32_t notifiedSeq;
            bool haveResult = (xTaskNotifyWait(0, 0xFFFFFFFFU, &notifiedSeq, pdMS_TO_TICKS(AI_RESULT_TIMEOUT_MS)) ==
                               pdTRUE) &&
                              ((uint16_t)notifiedSeq == frameSeq);
            if (!haveResult)
            {
                PRINTF("AI: no result from core0 for frame seq %u within %ums - skipping AI overlay/snapshot this "
                       "frame.\r\n",
                       frameSeq, AI_RESULT_TIMEOUT_MS);
            }

            ai_model_result_t aiResult = {0};
            if (haveResult)
            {
                ai_ipc_result_t ipcResult;
                memcpy(&ipcResult, IPC_RESULT_ADDR, sizeof(ipcResult));
                ConvertIpcResult(&ipcResult, &aiResult);

                if (aiResult.valid)
                {
                    float scaleX = (float)DEMO_BUFFER_WIDTH / (float)AI_MODEL_INPUT_WIDTH;
                    float scaleY = (float)DEMO_BUFFER_HEIGHT / (float)AI_MODEL_INPUT_HEIGHT;
                    for (uint32_t i = 0; i < aiResult.boxCount; i++)
                    {
                        const ai_bbox_t *box = &aiResult.boxes[i];
                        BBOX_DrawRect(frame, DEMO_BUFFER_WIDTH, DEMO_BUFFER_HEIGHT, (int)((float)box->x * scaleX),
                                      (int)((float)box->y * scaleY), (int)((float)box->width * scaleX),
                                      (int)((float)box->height * scaleY), 0x07E0U /* green */);
                        /* Same format the legacy single-core main.c's AI
                         * loop used - debug_console_lite may not support
                         * %f, so print score as a percentage integer. */
                        PRINTF("AI result: box[%u] label=%s x=%u y=%u w=%u h=%u score=%d%%\r\n", i, box->label,
                               box->x, box->y, box->width, box->height, (int)(box->score * 100.0f));
                    }
                }

                /* Internally rate-limited to at most 1 capture/sec (see
                 * snapshot.h) - a no-op most frames. */
                (void)SNAPSHOT_OnFrame(frame, DEMO_BUFFER_WIDTH, DEMO_BUFFER_HEIGHT, &aiResult, AI_MODEL_INPUT_WIDTH,
                                       AI_MODEL_INPUT_HEIGHT);
            }

            LCD_DrawImage(0U, 0U, DEMO_BUFFER_WIDTH, DEMO_BUFFER_HEIGHT, frame);

            fpsFrameCount++;
            bool logSignature = false;
            if ((int32_t)(DWT->CYCCNT - fpsWindowStartCycle) >= (int32_t)SystemCoreClock)
            {
                PRINTF("LCD preview: %u fps\r\n", fpsFrameCount);
                /* Must run BEFORE CAMERA_CAPTURE_Reinit() below - Reinit()
                 * memsets the frame buffer back to zero to prepare for the
                 * next capture (see CAMERA_CAPTURE_InitSmartDma()), so
                 * logging after it would always see a freshly-cleared
                 * buffer regardless of whether real pixel data had just
                 * been captured and drawn - this was confirmed on real
                 * hardware via SWD memory reads on 2026-09-04 (see
                 * WORKLOG.md): the buffer actually contains live, changing
                 * camera data, this function's own call ordering was the
                 * only bug, not SmartDMA or the shared-buffer address. */
                logSignature = true;
                fpsFrameCount       = 0U;
                fpsWindowStartCycle = DWT->CYCCNT;
            }
            if (logSignature)
            {
                DEMO_LogFrameSignature(frame);
            }

            CAMERA_CAPTURE_Reinit();
            skipNextFrame = true;
        }
    }
}

int main(void)
{
    MCMGR_Init();
    BOARD_InitHardware();

    uint32_t startupData;
    mcmgr_status_t status;
    do
    {
        status = MCMGR_GetStartupData(kMCMGR_Core0, &startupData);
    } while (status != kStatus_MCMGR_Success);

    PRINTF("\r\nCamera_AI_Test1 - core1 (dual-core Stage 5: camera + LCD preview + AI overlay + SD snapshot)\r\n");

    /* Must exist before either task can touch the shared LPSPI1 bus - see
     * spi1_bus.h's SPI1_BUS_Lock() comment (WORKLOG.md, Stage 4). */
    SPI1_BUS_CreateLock();

    /* Only one task on core1 touches the shared bus now that StorageTask
     * is retired (Stage 5, see this file's header comment) - the
     * equal-vs-higher-priority starvation lesson from Stage 4 no longer
     * applies to anything on THIS core (still relevant background: see
     * WORKLOG.md), but is kept at tskIDLE_PRIORITY + 1 regardless, nothing
     * to contend with it here. */
    TaskHandle_t cameraLcdTaskHandle;
    xTaskCreate(CameraLcdTask, "CameraLcdTask", configMINIMAL_STACK_SIZE + 512, NULL, tskIDLE_PRIORITY + 1,
                &cameraLcdTaskHandle);

    /* Stage 5: core0's AiInferenceTask replies to every IPC_SignalFrameReady()
     * with IPC_SignalResultReady() carrying the same sequence number as its
     * notification value - wakes CameraLcdTask's xTaskNotifyWait() call
     * directly, no extra queue/semaphore needed (same ISR->task pattern
     * Stage 2 already proved end-to-end). */
    IPC_EVENTS_RegisterHandler(cameraLcdTaskHandle);

    vTaskStartScheduler();

    for (;;)
    {
        /* Should never reach here. */
    }
}
