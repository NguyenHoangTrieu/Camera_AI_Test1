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

#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_class.h"
#include "usb_video_camera.h"
#include "usb_phy.h"

/*
 * CONFIRMED (see WORKLOG.md "decisive isolation test" entry): SmartDMA
 * camera capture only runs reliably with DCDC_VDD_LVL at Mid (1.0V) - *any*
 * other level (Normal 1.1V or Overdrive 1.2V) makes it stop after ~2
 * frames, silently (no HardFault). USB HS PHY genuinely needs DCDC_VDD_LVL
 * raised to Overdrive (CLOCK_EnableUsbhsPhyPllClock() spins forever in its
 * PLL_LOCK busy-wait otherwise). Since both can't be true at once, these two
 * helpers isolate just the regulator-level switch (no USB clock/PHY
 * bring-up, no camera re-init) so callers can flip between the two voltage
 * levels repeatedly - see main()'s periodic-refresh loop, which drops back
 * to Mid every few seconds to grab a fresh frame, then returns to Overdrive
 * - without having to redo either USB_DeviceClockInit()'s one-time PHY/PLL
 * bring-up or CAMERA_CAPTURE_Init()'s OV7670 SCCB init each cycle.
 */
void BOARD_SetRegulatorsMidVoltage(void)
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

void BOARD_SetRegulatorsOverdriveVoltage(void)
{
    /* NOTE: do NOT try raising CORELDO_VDD_LVL alone without DCDC_VDD_LVL -
     * tried this on real hardware (see WORKLOG.md) and it left the chip's
     * SWD/debug port completely unreachable (pyOCD couldn't reconnect even
     * under reset, needed a physical power cycle to recover). Always raise
     * both together, matching NXP's stock example, or neither. */
    spc_active_mode_dcdc_option_t usbDcdcOpt = {
        .DCDCVoltage       = kSPC_DCDC_OverdriveVoltage,
        .DCDCDriveStrength = kSPC_DCDC_NormalDriveStrength,
    };
    SPC_SetActiveModeDCDCRegulatorConfig(SPC0, &usbDcdcOpt); /* waits for SPC_SC_BUSY internally */

    spc_active_mode_core_ldo_option_t usbLdoOpt = {
        .CoreLDOVoltage       = kSPC_CoreLDO_OverDriveVoltage,
        .CoreLDODriveStrength = kSPC_CoreLDO_NormalDriveStrength,
    };
    SPC_SetActiveModeCoreLDORegulatorConfig(SPC0, &usbLdoOpt);
    while (SPC0->SC & SPC_SC_BUSY_MASK)
    {
    }
}

/*
 * USB High-Speed (EHCI + dedicated HS PHY) bring-up for the UVC camera
 * stream (source/usb/usb_video_camera.c). Copied near-verbatim from the SDK
 * board port for this exact board+example
 * (examples/_boards/frdmmcxn947/usb_examples/usb_device_video_virtual_camera/
 * bm/hardware_init.c) - this sequence is MCXN947-specific SPC/SCG/SYSCON
 * register bring-up for the USB HS PHY's PLL, not something worth
 * re-deriving. No pin muxing is needed: unlike the camera/LCD signals, the
 * USB HS D+/D- lines are dedicated pins, not routed through PORT/pin mux.
 *
 * Call exactly ONCE, from main() after CAMERA_CAPTURE_Deinit() - see
 * BOARD_SetRegulatorsOverdriveVoltage()'s comment above for why this can't
 * run before/during camera capture. Unlike the two regulator helpers above,
 * this is NOT meant to be called repeatedly (re-running USB_EhciPhyInit()/
 * CLOCK_EnableUsbhsPhyPllClock() on an already-enumerated session is
 * untested and risky - see WORKLOG.md "periodic refresh" entry).
 */
void USB_DeviceClockInit(void)
{
    usb_phy_config_struct_t phyConfig = {
        BOARD_USB_PHY_D_CAL,
        BOARD_USB_PHY_TXCAL45DP,
        BOARD_USB_PHY_TXCAL45DM,
    };

    SPC0->ACTIVE_VDELAY = 0x0500;
    BOARD_SetRegulatorsOverdriveVoltage();

    SPC0->ACTIVE_CFG |= SPC_ACTIVE_CFG_SYSLDO_VDD_DS_MASK;

    if (0u == (SCG0->LDOCSR & SCG_LDOCSR_LDOEN_MASK))
    {
        SCG0->TRIM_LOCK = 0x5a5a0001U;
        SCG0->LDOCSR |= SCG_LDOCSR_LDOEN_MASK;
        while (0U == (SCG0->LDOCSR & SCG_LDOCSR_VOUT_OK_MASK))
        {
        }
    }
    SYSCON->AHBCLKCTRLSET[2] |= SYSCON_AHBCLKCTRL2_USB_HS_MASK | SYSCON_AHBCLKCTRL2_USB_HS_PHY_MASK;
    SCG0->SOSCCFG &= ~(SCG_SOSCCFG_RANGE_MASK | SCG_SOSCCFG_EREFS_MASK);
    /* xtal = 20 ~ 30MHz */
    SCG0->SOSCCFG = (1U << SCG_SOSCCFG_RANGE_SHIFT) | (1U << SCG_SOSCCFG_EREFS_SHIFT);
    SCG0->SOSCCSR |= SCG_SOSCCSR_SOSCEN_MASK;
    while (0U == (SCG0->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK))
    {
    }
    SYSCON->CLOCK_CTRL |= SYSCON_CLOCK_CTRL_CLKIN_ENA_MASK | SYSCON_CLOCK_CTRL_CLKIN_ENA_FM_USBH_LPT_MASK;
    CLOCK_EnableClock(kCLOCK_UsbHs);
    CLOCK_EnableClock(kCLOCK_UsbHsPhy);
    CLOCK_EnableUsbhsPhyPllClock(kCLOCK_Usbphy480M, 24000000U);
    CLOCK_EnableUsbhsClock();
    USB_EhciPhyInit(CONTROLLER_ID, BOARD_XTAL0_CLK_HZ, &phyConfig);
}

void USB1_HS_IRQHandler(void)
{
    extern usb_video_virtual_camera_struct_t g_UsbDeviceVideoVirtualCamera;
    USB_DeviceEhciIsrFunction(g_UsbDeviceVideoVirtualCamera.deviceHandle);
}

void USB_DeviceIsrEnable(void)
{
    uint8_t usbDeviceEhciIrq[] = USBHS_IRQS;
    uint8_t irqNumber          = usbDeviceEhciIrq[CONTROLLER_ID - kUSB_ControllerEhci0];

    NVIC_SetPriority((IRQn_Type)irqNumber, USB_DEVICE_INTERRUPT_PRIORITY);
    EnableIRQ((IRQn_Type)irqNumber);
}

void BOARD_InitHardware(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    /* Boot at Mid voltage - see BOARD_SetRegulatorsMidVoltage()'s comment
     * above for why. main() switches to Overdrive itself later, only after
     * camera capture has produced a few frames and stopped for good (or,
     * for the periodic-refresh loop, drops back to Mid again briefly on
     * each cycle) - see main() for the full time-multiplexed sequencing. */
    BOARD_SetRegulatorsMidVoltage();

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
