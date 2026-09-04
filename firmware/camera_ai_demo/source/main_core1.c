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
 * Stage 4 (this revision): SD card snapshot (source/storage/snapshot.c,
 * sd_spi_disk.c) as a standalone StorageTask, manually triggered every
 * few seconds with a synthesized "face" box (AI isn't wired up until
 * Stage 5) - proves SD/FatFs mechanics work correctly when driven by a
 * FreeRTOS task instead of a bare-metal loop.
 */
#include "fsl_debug_console.h"
#include "board.h"
#include "app.h"
#include "mcmgr.h"
#include "FreeRTOS.h"
#include "task.h"
#include "camera_capture.h"
#include "lcd_display.h"
#include "snapshot.h"
#include "spi1_bus.h"

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

static void CameraLcdTask(void *pvParameters)
{
    (void)pvParameters;

    CAMERA_CAPTURE_Init();
    LCD_Init();
    DEMO_ClearScreen(0x0000U);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t fpsFrameCount      = 0U;
    uint32_t fpsWindowStartCycle = DWT->CYCCNT;

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
             * after - tearing fix, see WORKLOG.md. */
            CAMERA_CAPTURE_Deinit();
            uint16_t *frame = CAMERA_CAPTURE_GetFrameBuffer();
            LCD_DrawImage(0U, 0U, DEMO_BUFFER_WIDTH, DEMO_BUFFER_HEIGHT, frame);
            CAMERA_CAPTURE_Reinit();
            skipNextFrame = true;

            fpsFrameCount++;
            if ((int32_t)(DWT->CYCCNT - fpsWindowStartCycle) >= (int32_t)SystemCoreClock)
            {
                PRINTF("LCD preview: %u fps\r\n", fpsFrameCount);
                DEMO_LogFrameSignature(frame);
                fpsFrameCount       = 0U;
                fpsWindowStartCycle = DWT->CYCCNT;
            }
        }
    }
}

/* Stage 4, standalone test only - AI (Stage 5) supplies the real result.
 * Manual re-trigger every 5s, well clear of snapshot.c's own 1s internal
 * rate limit, so every call is expected to actually save. */
#define STORAGE_TASK_TEST_PERIOD_MS 5000U

static void StorageTask(void *pvParameters)
{
    (void)pvParameters;

    /* Idempotent - CameraLcdTask also enables this; whichever task starts
     * first wins, harmless either way. SNAPSHOT_OnFrame()'s rate-limit
     * check needs DWT->CYCCNT running before its first call. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* NOT wrapped in an outer lock - see sd_spi_disk.c's disk_initialize()
     * comment (WORKLOG.md, Stage 4 follow-up): every attempt to lock this
     * whole call (plain or recursive mutex, single-level or nested) made
     * SD card mount fail consistently and reproducibly (survived a real
     * power cycle, ruling out a stuck card) - root cause not pinned down,
     * reverted to the narrow, tight per-diskio-call locking that's
     * confirmed working for mount instead of guessing further. */
    SNAPSHOT_Init();

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(STORAGE_TASK_TEST_PERIOD_MS));

        /* Synthesized "face" box standing in for real AI output (Stage 5)
         * - coordinates are arbitrary, this is purely a connectivity test
         * for the SD/FatFs pipeline (rate limit, BMP header, box draw,
         * write), not a detection-accuracy test. Reads/draws into the
         * live camera frame buffer with no synchronization against
         * CameraLcdTask's concurrent SmartDMA/LCD-push use of the same
         * buffer yet - that's Stage 5's job (explicit buffer-ownership
         * handoff, see the approved plan); worst case here is a cosmetic
         * tear in the saved BMP, not a crash, acceptable for this
         * standalone test. */
        ai_bbox_t fakeBox = {
            .label  = "face",
            .x      = 16U,
            .y      = 16U,
            .width  = 40U,
            .height = 40U,
            .score  = 0.99f,
        };
        ai_model_result_t fakeResult = {
            .valid    = true,
            .boxCount = 1U,
            .boxes    = {fakeBox},
        };
        /* NOT wrapped in an outer lock either - see the comment on
         * SNAPSHOT_Init() above; every whole-sequence locking attempt for
         * THIS call broke SD mount earlier at boot, before this call is
         * even reached, so it was never actually tested in isolation
         * against just the write-failure problem. Left as a known
         * limitation for this stage (see WORKLOG.md) - each individual
         * disk_*() call is still protected by its own tight lock
         * (sd_spi_disk.c), same as the confirmed-working mount path;
         * Stage 5's real design (explicit buffer-ownership handoff, not
         * ad-hoc mutex scope experiments) is the right place to solve
         * this properly. */
        (void)SNAPSHOT_OnFrame(CAMERA_CAPTURE_GetFrameBuffer(), DEMO_BUFFER_WIDTH, DEMO_BUFFER_HEIGHT, &fakeResult,
                               72U, 72U);
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

    PRINTF("\r\nCamera_AI_Test1 - core1 (dual-core Stage 4: camera + LCD preview + SD snapshot test)\r\n");

    /* Must exist before either task can touch the shared LPSPI1 bus - see
     * spi1_bus.h's SPI1_BUS_Lock() comment (WORKLOG.md, Stage 4). */
    SPI1_BUS_CreateLock();

    /* Equal priority, NOT CameraLcdTask higher - confirmed the hard way
     * (WORKLOG.md, Stage 4): CameraLcdTask's loop body is a tight busy-poll
     * with no vTaskDelay/blocking call in its "no frame yet" branch, so it
     * is *always* ready and never voluntarily yields. Under strict
     * priority-based preemption, a lower-priority task that's never
     * blocked-against by the higher one is starved completely, not just
     * occasionally preempted - StorageTask never ran at all (not even its
     * boot-time SNAPSHOT_Init() print showed up) until this was fixed to
     * equal priority, which lets configUSE_TIME_SLICING's round-robin
     * give both tasks real CPU time every tick regardless of blocking
     * behavior. */
    xTaskCreate(CameraLcdTask, "CameraLcdTask", configMINIMAL_STACK_SIZE + 512, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(StorageTask, "StorageTask", configMINIMAL_STACK_SIZE + 512, NULL, tskIDLE_PRIORITY + 1, NULL);
    vTaskStartScheduler();

    for (;;)
    {
        /* Should never reach here. */
    }
}
