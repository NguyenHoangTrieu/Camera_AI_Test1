/*
 * hardware_init.c - Camera_AI_Test1 (FRDM-MCXN947)
 *
 * Clock/regulator bring-up copied from the NXP smartdma_camera_flexio_mculcd
 * board port (proven to work with this exact OV7670-on-J9 + FlexIO/ST7796S-
 * on-J8 combination).
 */

#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "app.h"
#include "fsl_clock.h"
#include "fsl_inputmux.h"
#include "fsl_spc.h"

void BOARD_InitHardware(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    /* Set the LDO_CORE VDD regulator to 1.0 V voltage level (needed for the
     * SmartDMA core clock used by the camera capture firmware). */
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

    /* Camera XCLK: route main clock out through CLKOUT (P2_2), divided down
     * to a clock rate the OV7670 accepts. */
    CLOCK_AttachClk(kMAIN_CLK_to_CLKOUT);
    CLOCK_SetClkDiv(kCLOCK_DivClkOut, 25U);

    /*
     * FlexIO clock, for the J8 8-bit parallel LCD bus. Divided down from
     * PLL0 (150 MHz) to 37.5 MHz here (was /1 = 150 MHz) so
     * FLEXIO_MCULCD_SetBaudRate()'s 8-bit timer divider (max 256) can reach
     * a slower per-pin write rate than the un-divided clock allowed - see
     * DEMO_FLEXIO_BAUDRATE_BPS in app.h for why: at 150 MHz source, the
     * divider maxed out around 293 kHz/pin, which turned out to still be
     * fast enough to corrupt pixel data on this specific (cheap/slow) panel
     * (black/white noise instead of a clean image - see WORKLOG.md).
     *
     * NOTE: an earlier attempt at /50 (3 MHz FlexIO clock, ~10 kHz/pin) hung
     * completely (LCD_Init() never returns - confirmed via serial log
     * showing no output at all past the boot banner, not even the
     * subsequent camera init line) - apparently below some real minimum
     * operating point for this peripheral/mode, not just the software
     * divider-field ceiling. /4 keeps the resulting SetBaudRate() divider
     * value (~188) in the same range that's already proven to work (the
     * un-divided 400 kHz/pin case used essentially the same divider
     * magnitude, just against a 150 MHz source instead of 37.5 MHz) - a
     * smaller, safer step to actually test the noise/speed hypothesis
     * instead of jumping to an extreme that broke something else. See
     * WORKLOG.md before changing this divider further.
     */
    CLOCK_SetClkDiv(kCLOCK_DivFlexioClk, 4u);
    CLOCK_AttachClk(kPLL0_to_FLEXIO);

    /* GPIO module clocks for the LCD RST/CS/RS pins (GPIO0, GPIO4). */
    CLOCK_EnableClock(kCLOCK_Gpio0);
    CLOCK_EnableClock(kCLOCK_Gpio4);

    /* Camera I2C (SCCB) clock. */
    CLOCK_AttachClk(kFRO12M_to_FLEXCOMM7);
    CLOCK_EnableClock(kCLOCK_LPFlexComm7);
    CLOCK_EnableClock(kCLOCK_LPI2c7);
    CLOCK_SetClkDiv(kCLOCK_DivFlexcom7Clk, 1u);

    /* Route camera VSYNC/HSYNC/PCLK (P0_4/P0_11/P0_5) to the SmartDMA. */
    INPUTMUX_Init(INPUTMUX0);
    INPUTMUX_AttachSignal(INPUTMUX0, 0, kINPUTMUX_GpioPort0Pin4ToSmartDma);
    INPUTMUX_AttachSignal(INPUTMUX0, 1, kINPUTMUX_GpioPort0Pin11ToSmartDma);
    INPUTMUX_AttachSignal(INPUTMUX0, 2, kINPUTMUX_GpioPort0Pin5ToSmartDma);
    INPUTMUX_Deinit(INPUTMUX0); /* Only needed during setup, save power. */

    BOARD_InitCameraPins();
    BOARD_InitFlexioPins();
}
