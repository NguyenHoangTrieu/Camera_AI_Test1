/*
 * main_core0.c - Camera_AI_Test1 dual-core RTOS migration (see WORKLOG.md).
 *
 * Stage 1 (confirmed on real hardware): boot core1 via MCMGR, copy-to-RAM
 * path - see app.h's comment for the whole embed mechanism.
 * Stage 2 (confirmed on real hardware): FreeRTOS scheduler on core0 + one
 * MCMGR event round-trip - proved the ISR-safe IPC design end-to-end
 * (source/shared/ipc_events.c). That demo task is retired now that Stage 5
 * (this revision) gives this core its real job.
 * Stage 3-4: core0 had no work yet (camera/LCD/SD all live on core1) - just
 * booted core1 and idled.
 * Stage 5 (this revision): AI inference. AiInferenceTask blocks on core1's
 * IPC_SignalFrameReady() doorbell (source/shared/ipc_events.h), reads the
 * shared frame buffer (source/shared/ipc_layout.h - only safe to read
 * between core1's CAMERA_CAPTURE_Deinit()/Reinit() calls, which is exactly
 * the window core1 blocks in waiting for this task's reply), runs
 * inference via the same model_runner.h API the legacy single-core build
 * uses unmodified, writes the result into the shared region as a plain-
 * data ai_ipc_result_t (not the pointer-carrying ai_model_result_t - see
 * ipc_layout.h's comment for why), and replies with
 * IPC_SignalResultReady() carrying the same frame sequence number.
 */
#include <string.h>
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "board.h"
#include "app.h"
#include "mcmgr.h"
#include "FreeRTOS.h"
#include "task.h"
#include "model_runner.h"
#include "ipc_layout.h"
#include "ipc_events.h"

static void AiInferenceTask(void *pvParameters)
{
    (void)pvParameters;

    AI_MODEL_Init();

    for (;;)
    {
        uint32_t frameSeq;
        if (xTaskNotifyWait(0, 0xFFFFFFFFU, &frameSeq, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        /* Safe to read: core1 stopped SmartDMA (CAMERA_CAPTURE_Deinit())
         * before signaling frame-ready, and won't touch the buffer again
         * (CAMERA_CAPTURE_Reinit()) until it receives this task's reply
         * below - see ipc_events.h's IPC_SignalFrameReady() comment. */
        const uint16_t *frame = (const uint16_t *)IPC_FRAME_BUFFER_ADDR;

        ai_model_result_t result;
        AI_MODEL_RunInference(frame, DEMO_BUFFER_WIDTH, DEMO_BUFFER_HEIGHT, &result);

        /* Convert to the wire-safe, pointer-free shape (ipc_layout.h) -
         * result.boxes[i].label is a `const char *` into core0's own
         * flash, not portable data to hand to core1 as-is. */
        ai_ipc_result_t ipcResult = {0};
        ipcResult.valid    = result.valid;
        ipcResult.boxCount = (result.boxCount > AI_IPC_MAX_BOXES) ? AI_IPC_MAX_BOXES : result.boxCount;
        for (uint32_t i = 0; i < ipcResult.boxCount; i++)
        {
            const ai_bbox_t *box = &result.boxes[i];
            strncpy(ipcResult.boxes[i].label, box->label, AI_IPC_LABEL_LEN - 1U);
            ipcResult.boxes[i].label[AI_IPC_LABEL_LEN - 1U] = '\0';
            ipcResult.boxes[i].x      = box->x;
            ipcResult.boxes[i].y      = box->y;
            ipcResult.boxes[i].width  = box->width;
            ipcResult.boxes[i].height = box->height;
            ipcResult.boxes[i].score  = box->score;
        }
        memcpy(IPC_RESULT_ADDR, &ipcResult, sizeof(ipcResult));

        IPC_SignalResultReady((uint16_t)frameSeq);
    }
}

int main(void)
{
    MCMGR_Init();
    BOARD_InitHardware();

    PRINTF("\r\nCamera_AI_Test1 - core0 (dual-core Stage 5: AI inference)\r\n");

    /* Grant core1 permission to drive the LCD control pins it owns
     * (WORKLOG.md, dual-core Stage 5 SEVENTH FOLLOW-UP) - months of "core1's
     * GPIO write doesn't stick" investigation turned out to match a known
     * MCXN947 issue (NXP Community: "MCXN947 failed to control GPIO in
     * slave core (CPU1)"): core1 has no SAU, so it's always Non-Secure, and
     * GPIO gates Non-Secure access per pin via its own PCNS register -
     * every pin reads back Secure-only (PCNS=0) after reset. Must happen on
     * core0 (the only core with a SAU, so the only one that can act Secure)
     * BEFORE MCMGR_StartCore() below releases core1, same boot-ordering
     * reason as everything else in this function - PCNS is a plain
     * register, not a FreeRTOS API, so this is safe here. Pin numbers
     * hardcoded to match core1's own app.h (DEMO_LCD_*_GPIO/PIN) - core0
     * can't include core1's board_port headers. */
    GPIO0->PCNS |= GPIO_PCNS_NSE15_MASK  /* LCD RST, P0_15 */
                 | GPIO_PCNS_NSE22_MASK  /* LCD CS,  P0_22 */
                 | GPIO_PCNS_NSE23_MASK; /* LCD BLK, P0_23 */
    GPIO1->PCNS |= GPIO_PCNS_NSE23_MASK; /* LCD DC,  P1_23 (Arduino D3) */

#ifdef CORE1_IMAGE_COPY_TO_RAM
    uint32_t core1_image_size = get_core1_image_size();
    PRINTF("core0: copying core1 image to 0x%x, size %u bytes\r\n", (unsigned)CORE1_BOOT_ADDRESS,
           (unsigned)core1_image_size);
    memcpy((void *)(uintptr_t)CORE1_BOOT_ADDRESS, CORE1_IMAGE_START, core1_image_size);
#endif

    /* CONFIRMED on real hardware (WORKLOG.md, Stage 5 bring-up): calling
     * ANY FreeRTOS API (xTaskCreate() - and, separately, just
     * IPC_EVENTS_RegisterHandler(), tested independently) BEFORE
     * MCMGR_StartCore() hangs the boot handshake completely - core0 stuck
     * forever in MCMGR_StartCore()'s busy-wait, core1 stuck forever in its
     * own MCMGR_GetStartupData() call (confirmed via SWD halt on both
     * cores: core1's PC was static across a 200ms resume/re-halt, i.e.
     * truly stuck, not just sampled mid-poll). Exact mechanism not fully
     * root-caused (plausibly an interrupt-priority/BASEPRI side effect of
     * FreeRTOS critical-section macros running before the scheduler has
     * initialized anything, interfering with the mailbox IRQ the boot
     * handshake depends on - not confirmed to that level of detail).
     * MCMGR_StartCore() itself is plain C, not a FreeRTOS API - safe to
     * call from this fully bare-metal, pre-scheduler context, which is
     * exactly why boot ordering matters here: finish ALL of core0<->core1's
     * MCMGR-level handshaking FIRST, only touch FreeRTOS (xTaskCreate(),
     * IPC_EVENTS_RegisterHandler()) after. */
    PRINTF("core0: starting core1...\r\n");
    MCMGR_StartCore(kMCMGR_Core1, (void *)(uintptr_t)CORE1_BOOT_ADDRESS, 0, kMCMGR_Start_Synchronous);
    PRINTF("core0: core1 started.\r\n");

    TaskHandle_t aiTaskHandle;
    xTaskCreate(AiInferenceTask, "AiInferenceTask", configMINIMAL_STACK_SIZE + 512, NULL, tskIDLE_PRIORITY + 1,
                &aiTaskHandle);
    IPC_EVENTS_RegisterHandler(aiTaskHandle);

    vTaskStartScheduler();

    for (;;)
    {
        /* Should never reach here. */
    }
}
