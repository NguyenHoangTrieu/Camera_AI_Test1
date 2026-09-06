/*
 * FreeRTOSConfig.h - Camera_AI_Test1 dual-core RTOS migration (see
 * WORKLOG.md), shared by both cores.
 *
 * Hand-written, not generated from Kconfig - deliberately pulling FreeRTOS
 * in directly via CMakeLists.txt's DUALCORE_RTOS branch (mcux_add_source,
 * same "pull directly, not via Kconfig" pattern this project already uses
 * for fsl_sdspi.c/ff.c/fsl_incbin.S) instead of enabling
 * CONFIG_MCUX_COMPONENT_middleware.freertos-kernel via prj.conf. Reasoning:
 * prj.conf is a file SHARED with the legacy single-core bare-metal build
 * (board_port/cm33_core0/prj.conf has no DUALCORE_RTOS-conditional Kconfig
 * mechanism) - enabling FreeRTOS there would also compile FreeRTOS's
 * port.c into the legacy build, which #defines vPortSVCHandler/
 * vPortPendSVHandler/vPortSysTickHandler to the literal CMSIS handler names
 * (SVC_Handler/PendSV_Handler/SysTick_Handler, see below), silently
 * overriding the SDK's default bare-metal handlers even though the legacy
 * build never calls vTaskStartScheduler() - a real, if probably-latent,
 * regression risk not worth taking. Scoping FreeRTOS entirely inside the
 * DUALCORE_RTOS CMake branch avoids this category of risk outright.
 *
 * Values below match the SDK's own Kconfig-generated defaults
 * (FreeRTOSConfig_Gen.h, seen while building the reference freertos_hello
 * example for this board) plus this board's own interrupt-priority scheme
 * (examples/_boards/frdmmcxn947/FreeRTOSConfigBoard.h) - not invented from
 * scratch. configMAX_SYSCALL_INTERRUPT_PRIORITY=2 (library scale, see
 * below) is confirmed compatible with MCMGR's MAILBOX_IRQn priorities
 * (level 5 on core0, level 2 on core1, set in
 * mcmgr_internal_core_api_mcxnx4x.c) - both are numerically >= this
 * threshold, so mcmgr's ISR-context event callbacks (source/shared/
 * ipc_events.c) can safely call xTaskNotifyFromISR().
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
extern uint32_t SystemCoreClock;

#define configUSE_PREEMPTION                   1
#define configUSE_TIME_SLICING                 1
#define configCPU_CLOCK_HZ                     (SystemCoreClock)
#define configTICK_RATE_HZ                     1000U
#define configMAX_PRIORITIES                   5
#define configMINIMAL_STACK_SIZE               128U /* words */
#define configMAX_TASK_NAME_LEN                16
#define configUSE_16_BIT_TICKS                 0
#define configIDLE_SHOULD_YIELD                1
#define configUSE_TASK_NOTIFICATIONS           1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES  1
#define configUSE_MUTEXES                      1
#define configUSE_RECURSIVE_MUTEXES            1
#define configUSE_COUNTING_SEMAPHORES          1
#define configQUEUE_REGISTRY_SIZE              8
#define configUSE_QUEUE_SETS                   0
/* No software timers used anywhere in this codebase - timers.c isn't even
 * compiled in (see CMakeLists.txt's DUALCORE_RTOS branch), trimmed for
 * core1's tight m_text budget (WORKLOG.md, Stage 3). */
#define configUSE_TIMERS                       0
#define configUSE_TICKLESS_IDLE                0
#define configUSE_IDLE_HOOK                    0
#define configUSE_TICK_HOOK                    0
#define configSUPPORT_STATIC_ALLOCATION        0
#define configSUPPORT_DYNAMIC_ALLOCATION       1
#define configTOTAL_HEAP_SIZE                  (24U * 1024U) /* tune per stage - see WORKLOG.md RAM budget */
#define configUSE_MALLOC_FAILED_HOOK           1
#define configCHECK_FOR_STACK_OVERFLOW         2 /* cheap insurance - this project has hit real stack-overflow bugs before, see WORKLOG.md */
#define configUSE_TRACE_FACILITY               0
#define configGENERATE_RUN_TIME_STATS          0
#define configRECORD_STACK_HIGH_ADDRESS        1

/* API inclusions actually used by this project's IPC design
 * (source/shared/ipc_events.c) and task bring-up. */
#define INCLUDE_vTaskDelay                     1
#define INCLUDE_vTaskSuspend                   1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark    1

#define configASSERT(x)                                          \
    if ((x) == 0)                                                \
    {                                                             \
        taskDISABLE_INTERRUPTS();                                 \
        for (;;)                                                  \
        {                                                          \
        }                                                          \
    }

/* Cortex-M33 (ARM_CM33_NTZ port, no TrustZone - this project has none
 * configured, see ARCHITECTURE.md). configENABLE_FPU auto-detects per-core:
 * core0 compiles with a hard-float ABI (-mfloat-abi=hard), core1 does not
 * (-mfloat-abi=soft, confirmed from this project's own real core1 compile
 * flags) - __ARM_FP is only defined by GCC when hardware FP is actually
 * enabled for that compile, so this one shared header gets the right
 * answer for each core automatically. */
#if defined(__ARM_FP) && (__ARM_FP != 0)
#define configENABLE_FPU 1
#else
#define configENABLE_FPU 0
#endif
#define configENABLE_MPU       0
#define configENABLE_TRUSTZONE 0
/* REQUIRED by port.c's own #if checks - undefined evaluates to 0 in the
 * preprocessor, which is the WRONG combination for this "NTZ" (No
 * TrustZone) port: port.c's own header comment lists
 * "configRUN_FREERTOS_SECURE_ONLY=1 and configENABLE_TRUSTZONE=0" as the
 * valid no-TrustZone combo, not both-0. Confirmed the hard way on real
 * hardware: leaving this undefined (=0 by default) produced a genuine
 * UsageFault (INVSTATE, CFSR=0x00040000) escalated to HardFault (HFSR
 * FORCED bit set, since configCHECK_HANDLER_INSTALLATION-related UsageFault
 * enable wasn't set either) during vTaskStartScheduler()'s first SVC-based
 * task start - SVCALLACT was set in SHCSR at fault time, confirming the
 * fault happened while starting the very first task, consistent with the
 * initial fake exception stack frame's xPSR having the wrong execution-
 * state assumption baked in for this SECURE_ONLY/TRUSTZONE combination. */
#define configRUN_FREERTOS_SECURE_ONLY 1

#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 3
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY     ((1U << (configPRIO_BITS)) - 1)
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 2
#define configKERNEL_INTERRUPT_PRIORITY             (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY        (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* Names confirmed against this exact port version's real function names by
 * diffing against the SDK's own Kconfig-generated FreeRTOSConfig_Gen.h
 * (built from the reference freertos_hello example for this board) -
 * FIRST attempt used vPortPendSVHandler/vPortSysTickHandler (an older
 * FreeRTOS naming convention) which silently renamed nothing (no matching
 * symbol in this port's port.c), leaving the SDK's non-functional default
 * PendSV_Handler/SysTick_Handler installed - confirmed on real hardware:
 * core0 printed "starting scheduler..." and then hung completely, no
 * crash, no further output, because vTaskStartScheduler()'s SysTick/PendSV
 * setup silently had no real handler wired to either vector. */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
