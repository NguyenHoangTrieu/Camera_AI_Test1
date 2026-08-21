/*
 * pin_mux.c - Camera_AI_Test1 (FRDM-MCXN947)
 *
 * See pin_mux.h and ../../../README.md for background. Three groups of pins are
 * configured here:
 *   1. Debug UART      - copied from the stock frdmmcxn947 project_template.
 *   2. OV7670 camera    - copied from the verified NXP example
 *                         `display_examples/smartdma_camera_flexio_mculcd`
 *                         (board port for frdmmcxn947), because that example's
 *                         camera wiring (J9 SmartDMA/Camera header, pin 5..22)
 *                         is exactly the hardware described in requirement.md.
 *   3. J8 FlexIO/LCD header - copied verbatim (mux/pin numbers) from the
 *                         same NXP smartdma_camera_flexio_mculcd board
 *                         port, since this is the hardware-accelerated,
 *                         officially-supported way to drive a 16-bit
 *                         parallel LCD on this exact board - see
 *                         ../../../README.md for J8 pinout/wiring.
 */

#include "fsl_common.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "pin_mux.h"
#include "app.h"

/* ---------------------------------------------------------------------- */
/* 1. Debug UART pins (P1_8 = FC4_P0 RX, P1_9 = FC4_P1 TX)                 */
/* ---------------------------------------------------------------------- */

void BOARD_InitBootPins(void)
{
    BOARD_InitDebugUartPins();
}

void BOARD_InitDebugUartPins(void)
{
    CLOCK_EnableClock(kCLOCK_Port1);

    const port_pin_config_t debugUartRx = {
        .pullSelect          = kPORT_PullUp,
        .pullValueSelect     = kPORT_LowPullResistor,
        .slewRate            = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable     = kPORT_OpenDrainDisable,
        .driveStrength       = kPORT_LowDriveStrength,
        .mux                 = kPORT_MuxAlt2, /* FC4_P0 */
        .inputBuffer         = kPORT_InputBufferEnable,
        .invertInput         = kPORT_InputNormal,
        .lockRegister        = kPORT_UnlockRegister};
    PORT_SetPinConfig(PORT1, 8U, &debugUartRx);

    const port_pin_config_t debugUartTx = {
        .pullSelect          = kPORT_PullDisable,
        .pullValueSelect     = kPORT_LowPullResistor,
        .slewRate            = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable     = kPORT_OpenDrainDisable,
        .driveStrength       = kPORT_LowDriveStrength,
        .mux                 = kPORT_MuxAlt2, /* FC4_P1 */
        .inputBuffer         = kPORT_InputBufferEnable,
        .invertInput         = kPORT_InputNormal,
        .lockRegister        = kPORT_UnlockRegister};
    PORT_SetPinConfig(PORT1, 9U, &debugUartTx);
}

/* ---------------------------------------------------------------------- */
/* 2. OV7670 camera pins - J9 SmartDMA/Camera header                       */
/*    (copied from the NXP smartdma_camera_flexio_mculcd board port)       */
/* ---------------------------------------------------------------------- */

void BOARD_InitCameraPins(void)
{
    CLOCK_EnableClock(kCLOCK_Port0);
    CLOCK_EnableClock(kCLOCK_Port1);
    CLOCK_EnableClock(kCLOCK_Port2);
    CLOCK_EnableClock(kCLOCK_Port3);

    /* PORT0_11 = PIO0_11, plain GPIO input -> camera HSYNC/HREF. */
    PORT_SetPinMux(PORT0, 11U, kPORT_MuxAlt0);
    PORT0->PCR[11] = ((PORT0->PCR[11] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));

    /* PORT0_4 = PIO0_4, plain GPIO input -> camera VSYNC. */
    PORT_SetPinMux(PORT0, 4U, kPORT_MuxAlt0);
    PORT0->PCR[4] = ((PORT0->PCR[4] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));

    /* PORT0_5 = PIO0_5, plain GPIO input -> camera PCLK. */
    PORT_SetPinMux(PORT0, 5U, kPORT_MuxAlt0);
    PORT0->PCR[5] = ((PORT0->PCR[5] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));

    /* Camera 8-bit data bus D0..D7, routed to EZH/SmartDMA GPIO (ALT7),
     * matching the SmartDMA camera firmware's expected pin group. */
    PORT_SetPinMux(PORT1, 4U, kPORT_MuxAlt7);  /* P1_4  = D0 */
    PORT1->PCR[4] = ((PORT1->PCR[4] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT1, 5U, kPORT_MuxAlt7);  /* P1_5  = D1 */
    PORT1->PCR[5] = ((PORT1->PCR[5] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT1, 6U, kPORT_MuxAlt7);  /* P1_6  = D2 */
    PORT1->PCR[6] = ((PORT1->PCR[6] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT1, 7U, kPORT_MuxAlt7);  /* P1_7  = D3 */
    PORT1->PCR[7] = ((PORT1->PCR[7] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT3, 4U, kPORT_MuxAlt7);  /* P3_4  = D4 */
    PORT3->PCR[4] = ((PORT3->PCR[4] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT3, 5U, kPORT_MuxAlt7);  /* P3_5  = D5 */
    PORT3->PCR[5] = ((PORT3->PCR[5] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT1, 10U, kPORT_MuxAlt7); /* P1_10 = D6 */
    PORT1->PCR[10] = ((PORT1->PCR[10] & ~(PORT_PCR_SRE_MASK | PORT_PCR_IBE_MASK)) |
                       PORT_PCR_SRE(0U) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT1, 11U, kPORT_MuxAlt7); /* P1_11 = D7 */
    PORT1->PCR[11] = ((PORT1->PCR[11] & ~(PORT_PCR_SRE_MASK | PORT_PCR_IBE_MASK)) |
                       PORT_PCR_SRE(0U) | PORT_PCR_IBE(1U));

    /* PORT2_2 = CLKOUT -> camera XCLK. */
    PORT_SetPinMux(PORT2, 2U, kPORT_MuxAlt1);
    PORT2->PCR[2] = ((PORT2->PCR[2] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));

    /* PORT3_2/PORT3_3 = FC7_P0/P1 -> camera SCCB (I2C-like) SDA/SCL. */
    PORT_SetPinMux(PORT3, 2U, kPORT_MuxAlt2);
    PORT3->PCR[2] = ((PORT3->PCR[2] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT3, 3U, kPORT_MuxAlt2);
    PORT3->PCR[3] = ((PORT3->PCR[3] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
}

/* ---------------------------------------------------------------------- */
/* 3. J8 FlexIO/LCD header - 8-bit parallel MIPI-DBI/8080 bus              */
/*    (panel in hand only has LCD_D0..D7, not the full 16-bit bus J8       */
/*    supports - see FLEXIO_MCULCD_DATA_BUS_WIDTH=8 in CMakeLists.txt)     */
/* ---------------------------------------------------------------------- */

void BOARD_InitFlexioPins(void)
{
    CLOCK_EnableClock(kCLOCK_Port0);
    CLOCK_EnableClock(kCLOCK_Port2);
    CLOCK_EnableClock(kCLOCK_Port4);

    /* LCD_CS (P0_12) and LCD_RS/DC (P0_7) - plain GPIO, driven by the
     * ST7796S driver's chip-select/data-command callbacks. */
    PORT_SetPinMux(PORT0, 12U, kPORT_MuxAlt0);
    PORT0->PCR[12] = ((PORT0->PCR[12] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT0, 7U, kPORT_MuxAlt0);
    PORT0->PCR[7] = ((PORT0->PCR[7] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));

#if DEMO_LCD_BITBANG
    /*
     * Diagnostic bit-bang mode (LCD_BITBANG_DIAGNOSTIC=ON in CMakeLists.txt):
     * mux the same 10 signals below as plain GPIO (Alt0) instead of FlexIO
     * (Alt6), so lcd_bitbang_j8.c can toggle them directly with
     * GPIO_PinWrite() loops - see WORKLOG.md "Have not tried reverting to
     * bit-banged GPIO on J8's pins" for why this exists. Pin numbers are
     * identical to the FlexIO path below; only the mux ALT function and
     * (for the data pins) the GPIO direction differ.
     */
    CLOCK_EnableClock(kCLOCK_Port2);

    PORT_SetPinMux(PORT0, 8U, kPORT_MuxAlt0); /* P0_8  = LCD_RD (GPIO) */
    PORT0->PCR[8] = ((PORT0->PCR[8] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT0, 9U, kPORT_MuxAlt0); /* P0_9  = LCD_WR (GPIO) */
    PORT0->PCR[9] = ((PORT0->PCR[9] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));

    PORT_SetPinMux(PORT2, 8U, kPORT_MuxAlt0); /* P2_8  = LCD_D0 (GPIO) */
    PORT2->PCR[8] = ((PORT2->PCR[8] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT2, 9U, kPORT_MuxAlt0); /* P2_9  = LCD_D1 (GPIO) */
    PORT2->PCR[9] = ((PORT2->PCR[9] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT2, 10U, kPORT_MuxAlt0); /* P2_10 = LCD_D2 (GPIO) */
    PORT2->PCR[10] = ((PORT2->PCR[10] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT2, 11U, kPORT_MuxAlt0); /* P2_11 = LCD_D3 (GPIO) */
    PORT2->PCR[11] = ((PORT2->PCR[11] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT4, 12U, kPORT_MuxAlt0); /* P4_12 = LCD_D4 (GPIO) */
    PORT4->PCR[12] = ((PORT4->PCR[12] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT4, 13U, kPORT_MuxAlt0); /* P4_13 = LCD_D5 (GPIO) */
    PORT4->PCR[13] = ((PORT4->PCR[13] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT4, 14U, kPORT_MuxAlt0); /* P4_14 = LCD_D6 (GPIO) */
    PORT4->PCR[14] = ((PORT4->PCR[14] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT4, 15U, kPORT_MuxAlt0); /* P4_15 = LCD_D7 (GPIO) */
    PORT4->PCR[15] = ((PORT4->PCR[15] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
#else
    /*
     * LCD_RD (P0_8) is plain GPIO here, NOT FlexIO0_D0 - deliberately not
     * muxed to FlexIO. The SDK's FLEXIO_MCULCD driver only actively drives
     * the RD pin during an explicit read transfer; between reads (i.e.
     * during every write, which is all this project ever does other than
     * the now-removed diagnostic ID read) its timer config reverts to
     * "output disabled" and the pin floats. That floating RD line during
     * writes was suspected as a real contributor to the "solid white, no
     * image" bring-up bug - see WORKLOG.md. lcd_flexio_mculcd.c instead
     * drives this pin as a continuously-held-high plain GPIO output (same
     * as the bit-bang diagnostic driver already did, and that one displays
     * an image correctly), so it can never float mid-write.
     */
    PORT_SetPinMux(PORT0, 8U, kPORT_MuxAlt0);
    PORT0->PCR[8] = ((PORT0->PCR[8] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));

    /* LCD_WR = FLEXIO0_D1 (P0_9). */
    PORT_SetPinMux(PORT0, 9U, kPORT_MuxAlt6);
    PORT0->PCR[9] = ((PORT0->PCR[9] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));

    /* 8-bit data bus LCD_D0..D7 = FLEXIO0_D16..D23. FLEXIO0_D24..D31
     * (LCD_D8..D15, P4_16..P4_23) are intentionally left unconfigured -
     * not used in 8-bit mode, and not wired to this panel anyway. */
    PORT_SetPinMux(PORT2, 8U, kPORT_MuxAlt6); /* P2_8  = FLEXIO0_D16 = LCD_D0 */
    PORT2->PCR[8] = ((PORT2->PCR[8] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT2, 9U, kPORT_MuxAlt6); /* P2_9  = FLEXIO0_D17 = LCD_D1 */
    PORT2->PCR[9] = ((PORT2->PCR[9] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT2, 10U, kPORT_MuxAlt6); /* P2_10 = FLEXIO0_D18 = LCD_D2 */
    PORT2->PCR[10] = ((PORT2->PCR[10] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT2, 11U, kPORT_MuxAlt6); /* P2_11 = FLEXIO0_D19 = LCD_D3 */
    PORT2->PCR[11] = ((PORT2->PCR[11] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT4, 12U, kPORT_MuxAlt6); /* P4_12 = FLEXIO0_D20 = LCD_D4 */
    PORT4->PCR[12] = ((PORT4->PCR[12] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT4, 13U, kPORT_MuxAlt6); /* P4_13 = FLEXIO0_D21 = LCD_D5 */
    PORT4->PCR[13] = ((PORT4->PCR[13] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT4, 14U, kPORT_MuxAlt6); /* P4_14 = FLEXIO0_D22 = LCD_D6 */
    PORT4->PCR[14] = ((PORT4->PCR[14] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT4, 15U, kPORT_MuxAlt6); /* P4_15 = FLEXIO0_D23 = LCD_D7 */
    PORT4->PCR[15] = ((PORT4->PCR[15] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
#endif /* DEMO_LCD_BITBANG */

    /* LCD_RST (P4_7) - plain GPIO. */
    PORT_SetPinMux(PORT4, 7U, kPORT_MuxAlt0);
    PORT4->PCR[7] = ((PORT4->PCR[7] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));

    /* LCD_BLK / backlight enable (P4_6) - plain GPIO, driven high in
     * LCD_Init(). See app.h for why this exists. */
    PORT_SetPinMux(PORT4, 6U, kPORT_MuxAlt0);
    PORT4->PCR[6] = ((PORT4->PCR[6] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
}
