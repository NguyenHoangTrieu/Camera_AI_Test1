/*
 * pin_mux.c - Camera_AI_Test1 (FRDM-MCXN947)
 *
 * See pin_mux.h and ../../../README.md. Four groups of pins are configured
 * here: debug UART, OV7670 camera (J9, copied from NXP's
 * smartdma_camera_flexio_mculcd example), J8 FlexIO/LCD header (abandoned,
 * kept for reference), and Arduino header LCD (current default).
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

    /* Camera 8-bit data bus D0..D7, routed to EZH/SmartDMA GPIO (ALT7). */
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
/* 3. J8 FlexIO/LCD header - 8-bit parallel MIPI-DBI/8080 bus (abandoned)  */
/* ---------------------------------------------------------------------- */

void BOARD_InitFlexioPins(void)
{
    CLOCK_EnableClock(kCLOCK_Port0);
    CLOCK_EnableClock(kCLOCK_Port2);
    CLOCK_EnableClock(kCLOCK_Port4);

    /* LCD_CS (P0_12) and LCD_RS/DC (P0_7) - plain GPIO. */
    PORT_SetPinMux(PORT0, 12U, kPORT_MuxAlt0);
    PORT0->PCR[12] = ((PORT0->PCR[12] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
    PORT_SetPinMux(PORT0, 7U, kPORT_MuxAlt0);
    PORT0->PCR[7] = ((PORT0->PCR[7] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));

#if DEMO_LCD_BITBANG
    /* Diagnostic bit-bang mode (LCD_BITBANG_DIAGNOSTIC=ON): mux the same
     * 10 signals as plain GPIO (Alt0) instead of FlexIO (Alt6), so
     * lcd_bitbang.c can toggle them directly. */
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
    /* LCD_RD (P0_8) is plain GPIO here, NOT FlexIO0_D0 - the SDK's
     * FLEXIO_MCULCD driver only drives RD during a read transfer, leaving
     * it floating between writes, which was a suspected cause of the
     * "solid white, no image" bring-up bug. Held continuously high here
     * instead. */
    PORT_SetPinMux(PORT0, 8U, kPORT_MuxAlt0);
    PORT0->PCR[8] = ((PORT0->PCR[8] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));

    /* LCD_WR = FLEXIO0_D1 (P0_9). */
    PORT_SetPinMux(PORT0, 9U, kPORT_MuxAlt6);
    PORT0->PCR[9] = ((PORT0->PCR[9] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));

    /* 8-bit data bus LCD_D0..D7 = FLEXIO0_D16..D23. FLEXIO0_D24..D31
     * (LCD_D8..D15) are unused in 8-bit mode and not wired to this panel. */
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

    /* LCD_BLK / backlight enable (P4_6) - plain GPIO, driven high in LCD_Init(). */
    PORT_SetPinMux(PORT4, 6U, kPORT_MuxAlt0);
    PORT4->PCR[6] = ((PORT4->PCR[6] & ~PORT_PCR_IBE_MASK) | PORT_PCR_IBE(1U));
}

/* ---------------------------------------------------------------------- */
/* 4. Arduino header - GPIO bit-bang 8080 8-bit bus (current default)      */
/* ---------------------------------------------------------------------- */

void BOARD_InitArduinoLcdPins(void)
{
    const gpio_pin_config_t outputConfig   = {.pinDirection = kGPIO_DigitalOutput, .outputLogic = 0U};
    const gpio_pin_config_t idleHighConfig = {.pinDirection = kGPIO_DigitalOutput, .outputLogic = 1U};

    CLOCK_EnableClock(kCLOCK_Port0);
    CLOCK_EnableClock(kCLOCK_Port1);
    CLOCK_EnableClock(kCLOCK_Port4);

    /* Data bus D0..D7. */
    PORT_SetPinMux(PORT0, DEMO_LCD_D0_PIN, kPORT_MuxAlt0);
    PORT_SetPinMux(PORT0, DEMO_LCD_D1_PIN, kPORT_MuxAlt0);
    PORT_SetPinMux(PORT0, DEMO_LCD_D2_PIN, kPORT_MuxAlt0);
    PORT_SetPinMux(PORT1, DEMO_LCD_D3_PIN, kPORT_MuxAlt0);
    PORT_SetPinMux(PORT0, DEMO_LCD_D4_PIN, kPORT_MuxAlt0);
    PORT_SetPinMux(PORT1, DEMO_LCD_D5_PIN, kPORT_MuxAlt0);
    PORT_SetPinMux(PORT1, DEMO_LCD_D6_PIN, kPORT_MuxAlt0);
    PORT_SetPinMux(PORT0, DEMO_LCD_D7_PIN, kPORT_MuxAlt0);
    GPIO_PinInit(DEMO_LCD_D0_GPIO, DEMO_LCD_D0_PIN, &outputConfig);
    GPIO_PinInit(DEMO_LCD_D1_GPIO, DEMO_LCD_D1_PIN, &outputConfig);
    GPIO_PinInit(DEMO_LCD_D2_GPIO, DEMO_LCD_D2_PIN, &outputConfig);
    GPIO_PinInit(DEMO_LCD_D3_GPIO, DEMO_LCD_D3_PIN, &outputConfig);
    GPIO_PinInit(DEMO_LCD_D4_GPIO, DEMO_LCD_D4_PIN, &outputConfig);
    GPIO_PinInit(DEMO_LCD_D5_GPIO, DEMO_LCD_D5_PIN, &outputConfig);
    GPIO_PinInit(DEMO_LCD_D6_GPIO, DEMO_LCD_D6_PIN, &outputConfig);
    GPIO_PinInit(DEMO_LCD_D7_GPIO, DEMO_LCD_D7_PIN, &outputConfig);

    /* RS/CS/RST - plugged in directly (Arduino A2/A3/A4). */
    PORT_SetPinMux(PORT0, DEMO_LCD_RS_PIN, kPORT_MuxAlt0);
    PORT_SetPinMux(PORT0, DEMO_LCD_CS_PIN, kPORT_MuxAlt0);
    PORT_SetPinMux(PORT0, DEMO_LCD_RST_PIN, kPORT_MuxAlt0);
    GPIO_PinInit(DEMO_LCD_RS_GPIO, DEMO_LCD_RS_PIN, &outputConfig);
    GPIO_PinInit(DEMO_LCD_CS_GPIO, DEMO_LCD_CS_PIN, &idleHighConfig);
    GPIO_PinInit(DEMO_LCD_RST_GPIO, DEMO_LCD_RST_PIN, &idleHighConfig);

    /* RD/WR - jumper-wired to Arduino D0/D1 (GPIO4); their native A0/A1
     * socket positions have no GPIO function - see app.h. */
    PORT_SetPinMux(PORT4, DEMO_LCD_RD_PIN, kPORT_MuxAlt0);
    PORT_SetPinMux(PORT4, DEMO_LCD_WR_PIN, kPORT_MuxAlt0);
    GPIO_PinInit(DEMO_LCD_RD_GPIO, DEMO_LCD_RD_PIN, &idleHighConfig);
    GPIO_PinInit(DEMO_LCD_WR_GPIO, DEMO_LCD_WR_PIN, &idleHighConfig);

    /* BLK (Arduino A5) - see app.h; safe default even if unconnected. */
    PORT_SetPinMux(PORT0, DEMO_LCD_BLK_PIN, kPORT_MuxAlt0);
    GPIO_PinInit(DEMO_LCD_BLK_GPIO, DEMO_LCD_BLK_PIN, &outputConfig);
}
