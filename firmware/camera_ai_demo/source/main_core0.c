/*
 * main_core0.c - Camera_AI_Test1 dual-core RTOS migration (see WORKLOG.md).
 *
 * Stage 1 (confirmed on real hardware): boot core1 via MCMGR, copy-to-RAM
 * path - see app.h's comment for the whole embed mechanism.
 * Stage 2 (confirmed on real hardware): FreeRTOS scheduler on core0 + one
 * MCMGR event round-trip - proved the ISR-safe IPC design end-to-end
 * (source/shared/ipc_events.c). That demo task is retired now; Stage 5
 * reintroduces the same MCMGR event mechanism for the real frame-ready/
 * result-ready doorbells once AI inference has real work to do here.
 * Stage 3-4 (this revision): core0 has no work yet (camera/LCD/SD all
 * live on core1) - just boots core1 and idles.
 */
#include <string.h>
#include "fsl_debug_console.h"
#include "board.h"
#include "app.h"
#include "mcmgr.h"
#include "FreeRTOS.h"
#include "task.h"

int main(void)
{
    MCMGR_Init();
    BOARD_InitHardware();

    PRINTF("\r\nCamera_AI_Test1 - core0 (dual-core Stage 4: camera/LCD/SD all on core1)\r\n");

#ifdef CORE1_IMAGE_COPY_TO_RAM
    uint32_t core1_image_size = get_core1_image_size();
    PRINTF("core0: copying core1 image to 0x%x, size %u bytes\r\n", (unsigned)CORE1_BOOT_ADDRESS,
           (unsigned)core1_image_size);
    memcpy((void *)(uintptr_t)CORE1_BOOT_ADDRESS, CORE1_IMAGE_START, core1_image_size);
#endif

    PRINTF("core0: starting core1...\r\n");
    MCMGR_StartCore(kMCMGR_Core1, (void *)(uintptr_t)CORE1_BOOT_ADDRESS, 0, kMCMGR_Start_Synchronous);
    PRINTF("core0: core1 started.\r\n");

    vTaskStartScheduler();

    for (;;)
    {
        /* Should never reach here. */
    }
}
