/*
 * hardware_init.c - core1 (Camera_AI_Test1 dual-core RTOS migration - see
 * WORKLOG.md).
 *
 * Stage 3: camera + LCD clock/pin bring-up moved here from the legacy
 * board_port/cm33_core0/hardware_init.c - copied verbatim (same clock
 * sources, same real-hardware-confirmed fixes: FRO_HF-sourced camera XCLK,
 * FRO_HF-sourced shared LPSPI1 clock - see that file's comments/WORKLOG.md
 * for the full history of each). USB-HS/regulator-Overdrive code is NOT
 * carried over - USB streaming is abandoned and out of scope for the
 * dual-core build (see ARCHITECTURE.md Sec.4).
 */

#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "app.h"
#include "fsl_clock.h"
#include "fsl_inputmux.h"
#include "fsl_spc.h"

/*
 * CONFIRMED (see WORKLOG.md): SmartDMA camera capture only runs reliably
 * with DCDC_VDD_LVL at Mid (1.0V) - any other level stops it after ~2
 * frames, silently. core1 never needs Overdrive (no USB here), so unlike
 * the legacy core0 hardware_init.c this is a one-shot boot-time set, not a
 * pair of helpers callers flip between.
 */
static void BOARD_SetRegulatorsMidVoltage(void)
{
    spc_active_mode_core_ldo_option_t ldoOpt = {
        .CoreLDOVoltage       = kSPC_CoreLDO_MidDriveVoltage,
        .CoreLDODriveStrength = kSPC_CoreLDO_NormalDriveStrength,
    };
    SPC_SetActiveModeCoreLDORegulatorConfig(SPC0, &ldoOpt);

    spc_active_mode_dcdc_option_t dcdcOpt = {
        .DCDCVoltage       = kSPC_DCDC_MidVoltage,
        .DCDCDriveStrength = kSPC_DCDC_NormalDriveStrength,
    };
    SPC_SetActiveModeDCDCRegulatorConfig(SPC0, &dcdcOpt);
}

void BOARD_InitHardware(void)
{
    /* attach FRO 12M to FLEXCOMM4 (debug console) - matches Stage 1/2's
     * core1 bring-up, kept here since BOARD_InitBootClocks() below doesn't
     * do this itself. */
    CLOCK_SetClkDiv(kCLOCK_DivFlexcom4Clk, 1u);
    CLOCK_AttachClk(BOARD_DEBUG_UART_CLK_ATTACH);

    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    BOARD_SetRegulatorsMidVoltage();

    /* Camera XCLK: route CLKOUT (P2_2) from FRO_HF/2 = 24MHz - see
     * board_port/cm33_core0/hardware_init.c's comment (WORKLOG.md) for the
     * full XCLK-mismatch bug this fixes; unchanged here, just relocated. */
    CLOCK_AttachClk(kFRO_HF_to_CLKOUT);
    CLOCK_SetClkDiv(kCLOCK_DivClkOut, 2U);

    /* GPIO module clocks for the LCD pins (GPIO0, Arduino header). */
    CLOCK_EnableClock(kCLOCK_Gpio0);

    /* Camera I2C (SCCB) clock. */
    CLOCK_AttachClk(kFRO12M_to_FLEXCOMM7);
    CLOCK_EnableClock(kCLOCK_LPFlexComm7);
    CLOCK_EnableClock(kCLOCK_LPI2c7);
    CLOCK_SetClkDiv(kCLOCK_DivFlexcom7Clk, 1u);

    /* Shared LPSPI1 bus (Arduino D10..D13 - LCD/microSD/touch, see
     * source/spi1_bus.h). FRO_HF (48MHz), not FRO12M - see
     * board_port/cm33_core0/hardware_init.c's comment (WORKLOG.md) for why. */
    CLOCK_AttachClk(kFRO_HF_DIV_to_FLEXCOMM1);
    CLOCK_SetClkDiv(kCLOCK_DivFlexcom1Clk, 1u);

    /* Route camera VSYNC/HSYNC/PCLK (P0_4/P0_11/P0_5) to the SmartDMA. */
    INPUTMUX_Init(INPUTMUX0);
    INPUTMUX_AttachSignal(INPUTMUX0, 0, kINPUTMUX_GpioPort0Pin4ToSmartDma);
    INPUTMUX_AttachSignal(INPUTMUX0, 1, kINPUTMUX_GpioPort0Pin11ToSmartDma);
    INPUTMUX_AttachSignal(INPUTMUX0, 2, kINPUTMUX_GpioPort0Pin5ToSmartDma);
    INPUTMUX_Deinit(INPUTMUX0); /* Only needed during setup, save power. */

    BOARD_InitCameraPins();
    BOARD_InitArduinoLcdPins();
    /* Stage 4 (WORKLOG.md): microSD slot, same shared LPSPI1 bus as the
     * LCD - includes the real-hardware-confirmed SDI/DO pull-up fix (see
     * pin_mux.c's BOARD_InitSdCardPins(), a shared file - this project's
     * SD shield has no pull-up of its own). */
    BOARD_InitSdCardPins();
}
