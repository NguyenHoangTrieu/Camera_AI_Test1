/*
 * fault_handler.c - diagnostic HardFault dump.
 *
 * The default HardFault_Handler (startup_MCXN947_cm33_core0.S) is just an
 * infinite loop - matches the "log stops completely, forever" symptom seen
 * when the board hangs shortly after USB streaming starts, but gives no clue
 * *why*. This overrides it (it's a weak symbol) to print the fault status
 * registers and the PC/LR that were active when the fault happened, before
 * looping forever - temporary, for tracking down that hang. Remove once
 * the hang is understood and fixed.
 *
 * MemManage/Bus/Usage faults aren't individually enabled anywhere in this
 * project (SCB->SHCSR MEMFAULTENA/BUSFAULTENA/USGFAULTENA are 0 by default),
 * so any of those escalate straight to HardFault_Handler too - one handler
 * is enough to catch all of them.
 */
#include <stdint.h>
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"

__attribute__((used)) static void FaultHandlerC(uint32_t *stackedRegs)
{
    PRINTF("\r\n*** HardFault ***\r\n");
    PRINTF("CFSR  = 0x%08lX (MMFSR=0x%02lX BFSR=0x%02lX UFSR=0x%04lX)\r\n", (unsigned long)SCB->CFSR,
           (unsigned long)(SCB->CFSR & 0xFFUL), (unsigned long)((SCB->CFSR >> 8) & 0xFFUL),
           (unsigned long)((SCB->CFSR >> 16) & 0xFFFFUL));
    PRINTF("HFSR  = 0x%08lX\r\n", (unsigned long)SCB->HFSR);
    PRINTF("MMFAR = 0x%08lX\r\n", (unsigned long)SCB->MMFAR);
    PRINTF("BFAR  = 0x%08lX\r\n", (unsigned long)SCB->BFAR);
    PRINTF("Stacked r0=0x%08lX r1=0x%08lX r2=0x%08lX r3=0x%08lX\r\n", (unsigned long)stackedRegs[0],
           (unsigned long)stackedRegs[1], (unsigned long)stackedRegs[2], (unsigned long)stackedRegs[3]);
    PRINTF("Stacked r12=0x%08lX LR=0x%08lX PC=0x%08lX xPSR=0x%08lX\r\n", (unsigned long)stackedRegs[4],
           (unsigned long)stackedRegs[5], (unsigned long)stackedRegs[6], (unsigned long)stackedRegs[7]);
    while (1)
    {
    }
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4      \n"
        "ite eq          \n"
        "mrseq r0, msp   \n"
        "mrsne r0, psp   \n"
        "b FaultHandlerC \n");
}
