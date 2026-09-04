/*
 * hardware_init.c - Camera_AI_Test1 (FRDM-MCXN947)
 *
 * Camera clock bring-up copied from NXP's smartdma_camera_flexio_mculcd
 * board port. LCD pin init calls whichever of
 * BOARD_InitArduinoLcdPins()/BOARD_InitFlexioPins() (pin_mux.c)
 * DEMO_LCD_ARDUINO_HEADER (app.h) selects - Arduino header is the current
 * default.
 */

#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "app.h"
#include "fsl_clock.h"
#include "fsl_inputmux.h"
#include "fsl_spc.h"

#ifndef DUALCORE_RTOS
/* USB Video Class streaming is abandoned and not carried forward into the
 * dual-core RTOS build - see ARCHITECTURE.md Sec.4/WORKLOG.md. Guarded so
 * this file can be shared between the legacy single-core build and the
 * dual-core Stage 1+ bring-up (-DDUALCORE_RTOS=1, see CMakeLists.txt)
 * without needing source/usb/ on the include path in the latter. */
#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_class.h"
#include "usb_video_camera.h"
#include "usb_phy.h"
#endif /* DUALCORE_RTOS */

/*
 * CONFIRMED (see WORKLOG.md): SmartDMA camera capture only runs reliably
 * with DCDC_VDD_LVL at Mid (1.0V) - any other level stops it after ~2
 * frames, silently. USB HS PHY needs DCDC_VDD_LVL at Overdrive
 * (CLOCK_EnableUsbhsPhyPllClock() spins forever in PLL_LOCK otherwise).
 * Since both can't be true at once, these two helpers isolate just the
 * regulator-level switch (no USB clock/PHY bring-up, no camera re-init)
 * so callers can flip between them repeatedly - see main()'s
 * periodic-refresh loop.
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
    /* Do NOT raise CORELDO_VDD_LVL alone without DCDC_VDD_LVL - left the
     * chip's SWD/debug port completely unreachable on real hardware (see
     * WORKLOG.md). Always raise both together, or neither. */
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

#ifndef DUALCORE_RTOS
/*
 * USB High-Speed (EHCI + dedicated HS PHY) bring-up for the UVC camera
 * stream (source/usb/usb_video_camera.c). Copied near-verbatim from the
 * SDK board port for this exact board+example - MCXN947-specific
 * SPC/SCG/SYSCON register bring-up for the USB HS PHY's PLL.
 *
 * Call exactly ONCE, from main() after CAMERA_CAPTURE_Deinit() - camera
 * capture and USB HS can't run at the same time on this chip (see
 * BOARD_SetRegulatorsOverdriveVoltage() above). Unlike the two regulator
 * helpers above, don't call this repeatedly.
 *
 * Not carried into the dual-core RTOS build - see the include guard above.
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
#endif /* DUALCORE_RTOS */

void BOARD_InitHardware(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    /* Boot at Mid voltage - main() switches to Overdrive itself later,
     * only after camera capture has produced a few frames and stopped
     * for good (or drops back to Mid briefly each periodic-refresh
     * cycle). */
    BOARD_SetRegulatorsMidVoltage();

    /* Camera XCLK: route a clock out through CLKOUT (P2_2) to the OV7670's
     * XCLK pin.
     *
     * BUG FOUND AND FIXED (2026-09-04, see WORKLOG.md's camera-fps entry):
     * the original code here (copied from NXP's own
     * smartdma_camera_flexio_mculcd reference example for this exact
     * board - confirmed present in NXP's own source too, not something
     * introduced by this project) routed MAIN_CLK (150MHz, from
     * BOARD_BootClockPLL150M()) through this divider, giving
     * 150,000,000 / 25 = 6,000,000 Hz - a real, exact 6MHz - while
     * camera_capture.c's ov7670_resource_t declares
     * `.xclock = kOV7670_InputClock24MHZ` and requests 30fps.
     * fsl_ov7670.c's OV7670_Configure() picks its CLKRC/timing register
     * values from a lookup table keyed on the DECLARED xclock, assuming
     * the sensor really receives that rate - it has no way to detect the
     * real one. Since the OV7670's entire internal frame-timing state
     * machine derives from XCLK, actually feeding it 1/4 of the rate its
     * own CLKRC setting assumes makes it run its whole capture cycle ~4x
     * slower than intended - CONFIRMED on real hardware via a dedicated
     * diagnostic (main.c's `CAMERA-DIAG`, isolated the camera's free-
     * running rate with zero consumption): measured ~7.3fps against a
     * configured 30fps target, matching a ~4x slowdown almost exactly
     * (30/4 = 7.5).
     *
     * Fix: source CLKOUT from FRO_HF (48MHz) instead of MAIN_CLK - FRO_HF
     * is already running and independently confirmed stable at 48MHz on
     * this exact board (see spi1_bus.c's SPI1_BUS_GetSourceClockFreq()
     * diagnostic, used for LPSPI1). 48,000,000 / 2 = 24,000,000 Hz - a
     * genuine, exact 24MHz, actually matching what
     * camera_capture.c/fsl_ov7670.c's CLKRC math assumes, unlike the
     * previous 6MHz. 150MHz (MAIN_CLK) has no integer divisor landing on
     * any of the 4 XCLK rates fsl_ov7670.c's lookup table supports
     * (24/12/26/13 MHz) - that's WHY the reference example's own divisor
     * choice (25, giving a suspiciously clean 6MHz) never actually hit
     * the rate its own driver call declares; FRO_HF/2 lands on 24MHz
     * exactly, which MAIN_CLK/any-integer-divisor cannot. */
    CLOCK_AttachClk(kFRO_HF_to_CLKOUT);
    CLOCK_SetClkDiv(kCLOCK_DivClkOut, 2U);

    /*
     * FlexIO clock, for the J8 8-bit parallel LCD bus. Divided down from
     * PLL0 (150 MHz) to 37.5 MHz so FLEXIO_MCULCD_SetBaudRate()'s divider
     * can reach a slower per-pin rate than the undivided clock allowed -
     * see DEMO_FLEXIO_BAUDRATE_BPS in app.h. See WORKLOG.md before
     * changing this divider further (values below ~3 MHz have hung
     * LCD_Init() completely on this panel).
     */
    CLOCK_SetClkDiv(kCLOCK_DivFlexioClk, 4u);
    CLOCK_AttachClk(kPLL0_to_FLEXIO);

    /* GPIO module clocks for the LCD pins (GPIO0 for the Arduino header's
     * SPI panel; GPIO4 additionally needed for the J8 header's RST/BLK).
     * GPIO1 is enabled defensively too - not needed by the LCD pins
     * anymore (the old 8-bit parallel bus's D3/D5/D6 were the only GPIO1
     * users here), left on in case some other GPIO1 pin needs it; harmless
     * either way. */
    CLOCK_EnableClock(kCLOCK_Gpio0);
    CLOCK_EnableClock(kCLOCK_Gpio1);
    CLOCK_EnableClock(kCLOCK_Gpio4);

    /* Camera I2C (SCCB) clock. */
    CLOCK_AttachClk(kFRO12M_to_FLEXCOMM7);
    CLOCK_EnableClock(kCLOCK_LPFlexComm7);
    CLOCK_EnableClock(kCLOCK_LPI2c7);
    CLOCK_SetClkDiv(kCLOCK_DivFlexcom7Clk, 1u);

    /* Shared LPSPI1 bus (Arduino D10..D13 - LCD/microSD/touch, see
     * source/spi1_bus.h). Just attach+divide the source clock here, same
     * as the SDK's own lpspi/interrupt_b2b_transfer example for this
     * board - SPI1_BUS_Init()/LPSPI_MasterInit() (source/spi1_bus.c)
     * gates/ungates and resets the LPSPI1 peripheral itself, no separate
     * CLOCK_EnableClock() needed for it.
     *
     * FRO_HF (48MHz), not FRO12M (12MHz) - CONFIRMED on real hardware
     * (2026-09-04, see WORKLOG.md) that FRO12M capped the achievable SPI
     * baud rate at ~6MHz (LPSPI's baud divider can't go below srcClock/2),
     * making the LCD camera-preview build's frame rate far short of a
     * ~24fps target - the math needs >=~30Mbps just for one 320x240
     * RGB565 frame's raw pixel bits, impossible from a 12MHz source
     * regardless of software optimization. FRO_HF is already enabled by
     * this board's BOOTCLOCKPLL150M profile (see the SDK's
     * clock_config.c - FRO_HF feeds into the PLL0 setup, then stays
     * available as an independent 48MHz source afterward) and not used by
     * anything else in this project, so switching to it is a low-risk,
     * ~4x ceiling increase (max SPI baud ~6MHz -> ~24MHz) with no new
     * clock tree to configure. A yet-higher-frequency source (PLL0 itself,
     * 150MHz, via the separate PLLCLKDIV clock tree branch) could push
     * the ceiling further still, but was deliberately NOT used here: this
     * project's own history includes an abandoned LCD bus (J8 FlexIO,
     * PLL0-derived) that produced unresolved signal-integrity noise at
     * a much lower effective rate on this same class of breadboard
     * wiring (see WORKLOG.md/ARCHITECTURE.md §4) - 48MHz/~24MHz was
     * judged the more conservative next step; see lcd_spi_hw.c's
     * LCD_SPI_BAUDRATE_HZ comment for the fps math this unlocks and what
     * to do if 24MHz turns out to be too fast for this specific wiring. */
    CLOCK_AttachClk(kFRO_HF_DIV_to_FLEXCOMM1);
    CLOCK_SetClkDiv(kCLOCK_DivFlexcom1Clk, 1u);

    /* Route camera VSYNC/HSYNC/PCLK (P0_4/P0_11/P0_5) to the SmartDMA. */
    INPUTMUX_Init(INPUTMUX0);
    INPUTMUX_AttachSignal(INPUTMUX0, 0, kINPUTMUX_GpioPort0Pin4ToSmartDma);
    INPUTMUX_AttachSignal(INPUTMUX0, 1, kINPUTMUX_GpioPort0Pin11ToSmartDma);
    INPUTMUX_AttachSignal(INPUTMUX0, 2, kINPUTMUX_GpioPort0Pin5ToSmartDma);
    INPUTMUX_Deinit(INPUTMUX0); /* Only needed during setup, save power. */

    BOARD_InitCameraPins();
#if DEMO_LCD_ARDUINO_HEADER
    BOARD_InitArduinoLcdPins();
    BOARD_InitTouchPins(); /* XPT2046 touch controller - Arduino header only, see pin_mux.c. */
#else
    BOARD_InitFlexioPins();
#endif
    BOARD_InitSdCardPins();
}

#ifdef CORE1_IMAGE_COPY_TO_RAM
/* core1_image_size (components/misc_utilities/fsl_incbin.S) is the actual
 * symbol - see app.h's comment for the whole embed mechanism. */
uint32_t get_core1_image_size(void)
{
    return (uint32_t)core1_image_size;
}
#endif
