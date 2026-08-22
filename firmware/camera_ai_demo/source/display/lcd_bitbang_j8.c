/*
 * lcd_bitbang_j8.c - see lcd_bitbang_j8.h
 *
 * Bypasses the FlexIO peripheral entirely and drives the same 14 J8 signals
 * (see README.md pin table) with plain GPIO_PinWrite() loops - the same
 * approach the project's earlier (now-deleted) Arduino-header bit-bang
 * driver used, which was proven to display a recognizable camera image on
 * this exact physical panel (see WORKLOG.md, LCD bring-up timeline step 1).
 *
 * Purpose: isolate whether the LCD stays blank because of something
 * specific to the FlexIO hardware bus (speed, edge timing, an init-time
 * glitch) or because of something that would fail with ANY transport
 * (wiring, panel, init sequence) - see WORKLOG.md "Have not tried reverting
 * to bit-banged GPIO on J8's pins". If the screen lights up here but not
 * with the FlexIO driver, the bug is FlexIO-specific. If it stays blank
 * here too, the bug is downstream of the transport (wiring/panel/init
 * sequence), and FlexIO itself can be ruled out.
 *
 * Enabled by building with LCD_BITBANG_DIAGNOSTIC=ON in CMakeLists.txt
 * (sets -DDEMO_LCD_BITBANG=1 globally), which also switches
 * pin_mux.c's BOARD_InitFlexioPins() to mux these pins as plain GPIO
 * instead of FlexIO.
 *
 * RD (P0_8) is held high (inactive) here for this driver's whole lifetime -
 * deliberately, as a second experiment: the FlexIO driver only ever drives
 * RD during its one-off diagnostic ID read and otherwise leaves it
 * floating between transfers (its timer's pin config reverts to
 * "output disabled" whenever a read isn't in progress - see
 * lcd_flexio_mculcd.c's LCD_DiagnosticReadId comment and WORKLOG.md, which
 * already suspected the RD line). This driver has no read path at all, so
 * RD is simply init'd as a GPIO output and left continuously high.
 *
 * Not optimized - deliberately simple/slow (microsecond-scale delays) to
 * maximize the odds of working on a slow/cheap panel, matching why bit-bang
 * worked before when the fast FlexIO bus didn't. A full 320x240 frame push
 * takes on the order of half a second; this is a bring-up diagnostic, not
 * meant to replace the FlexIO path for the real camera-to-LCD loop.
 */

#include "lcd_bitbang_j8.h"
#include <stdbool.h>
#include "app.h"
#include "board.h"
#include "fsl_gpio.h"
#include "fsl_debug_console.h"

/* ~1us-scale delay between bus transitions - generous setup/hold margin for
 * a slow panel. Not calibrated to a specific core clock; errs slow on
 * purpose, same reasoning as WORKLOG.md's FlexIO baud-rate-drop experiment. */
static void LCD_BitbangDelay(void)
{
    SDK_DelayAtLeastUs(1U, SystemCoreClock);
}

static void LCD_SetCSPin(bool set)
{
    GPIO_PinWrite(DEMO_LCD_CS_GPIO, DEMO_LCD_CS_PIN, set ? 1U : 0U);
}

static void LCD_SetRSPin(bool set)
{
    GPIO_PinWrite(DEMO_LCD_RS_GPIO, DEMO_LCD_RS_PIN, set ? 1U : 0U);
}

static void LCD_SetResetPin(bool set)
{
    GPIO_PinWrite(DEMO_LCD_RST_GPIO, DEMO_LCD_RST_PIN, set ? 1U : 0U);
}

static void LCD_SetBacklight(bool on)
{
    GPIO_PinWrite(DEMO_LCD_BLK_GPIO, DEMO_LCD_BLK_PIN, on ? 1U : 0U);
}

static void LCD_SetDataBus(uint8_t value)
{
    GPIO_PinWrite(DEMO_LCD_D0_GPIO, DEMO_LCD_D0_PIN, (value >> 0U) & 1U);
    GPIO_PinWrite(DEMO_LCD_D1_GPIO, DEMO_LCD_D1_PIN, (value >> 1U) & 1U);
    GPIO_PinWrite(DEMO_LCD_D2_GPIO, DEMO_LCD_D2_PIN, (value >> 2U) & 1U);
    GPIO_PinWrite(DEMO_LCD_D3_GPIO, DEMO_LCD_D3_PIN, (value >> 3U) & 1U);
    GPIO_PinWrite(DEMO_LCD_D4_GPIO, DEMO_LCD_D4_PIN, (value >> 4U) & 1U);
    GPIO_PinWrite(DEMO_LCD_D5_GPIO, DEMO_LCD_D5_PIN, (value >> 5U) & 1U);
    GPIO_PinWrite(DEMO_LCD_D6_GPIO, DEMO_LCD_D6_PIN, (value >> 6U) & 1U);
    GPIO_PinWrite(DEMO_LCD_D7_GPIO, DEMO_LCD_D7_PIN, (value >> 7U) & 1U);
}

/* One 8080 write cycle: data must already be stable on the bus. WR idles
 * high; pulse it low then back high (data latched on the rising edge). */
static void LCD_PulseWR(void)
{
    GPIO_PinWrite(DEMO_LCD_WR_GPIO, DEMO_LCD_WR_PIN, 0U);
    LCD_BitbangDelay();
    GPIO_PinWrite(DEMO_LCD_WR_GPIO, DEMO_LCD_WR_PIN, 1U);
    LCD_BitbangDelay();
}

static void LCD_WriteByte(uint8_t value)
{
    LCD_SetDataBus(value);
    LCD_BitbangDelay(); /* Data setup time before WR falls. */
    LCD_PulseWR();
}

/* Command/data helpers below assume CS is already asserted (low) by the
 * caller - mirrors lcd_flexio_mculcd.c's StartTransfer/StopTransfer split,
 * so LCD_SetWindow() can keep CS asserted across multiple commands and
 * LCD_PushPixels() closes it, instead of toggling CS per command. */
static void LCD_WriteCommandOpen(uint8_t command)
{
    LCD_SetRSPin(false); /* RS low = command */
    LCD_WriteByte(command);
    LCD_SetRSPin(true);
}

static void LCD_WriteDataArrayOpen(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        LCD_WriteByte(data[i]);
    }
}

/* Write one command byte with no data, in its own CS-bracketed transfer. */
static void LCD_WriteCommand(uint8_t command)
{
    LCD_SetCSPin(false);
    LCD_WriteCommandOpen(command);
    LCD_SetCSPin(true);
}

/* Write one command byte followed by 1-4 data bytes, in one CS-bracketed transfer. */
static void LCD_WriteCommandData(uint8_t command, const uint8_t *data, uint32_t len)
{
    LCD_SetCSPin(false);
    LCD_WriteCommandOpen(command);
    LCD_WriteDataArrayOpen(data, len);
    LCD_SetCSPin(true);
}

static void LCD_InitGpioPins(void)
{
    const gpio_pin_config_t outConfig = {.pinDirection = kGPIO_DigitalOutput, .outputLogic = 1};

    GPIO_PinInit(DEMO_LCD_RST_GPIO, DEMO_LCD_RST_PIN, &outConfig);
    GPIO_PinInit(DEMO_LCD_CS_GPIO, DEMO_LCD_CS_PIN, &outConfig);
    GPIO_PinInit(DEMO_LCD_RS_GPIO, DEMO_LCD_RS_PIN, &outConfig);
    GPIO_PinInit(DEMO_LCD_WR_GPIO, DEMO_LCD_WR_PIN, &outConfig);

    /* RD held high (inactive) for this driver's whole lifetime - see the
     * file header comment above for why this is deliberate. */
    GPIO_PinInit(DEMO_LCD_RD_GPIO, DEMO_LCD_RD_PIN, &outConfig);
    GPIO_PinWrite(DEMO_LCD_RD_GPIO, DEMO_LCD_RD_PIN, 1U);

    GPIO_PinInit(DEMO_LCD_D0_GPIO, DEMO_LCD_D0_PIN, &outConfig);
    GPIO_PinInit(DEMO_LCD_D1_GPIO, DEMO_LCD_D1_PIN, &outConfig);
    GPIO_PinInit(DEMO_LCD_D2_GPIO, DEMO_LCD_D2_PIN, &outConfig);
    GPIO_PinInit(DEMO_LCD_D3_GPIO, DEMO_LCD_D3_PIN, &outConfig);
    GPIO_PinInit(DEMO_LCD_D4_GPIO, DEMO_LCD_D4_PIN, &outConfig);
    GPIO_PinInit(DEMO_LCD_D5_GPIO, DEMO_LCD_D5_PIN, &outConfig);
    GPIO_PinInit(DEMO_LCD_D6_GPIO, DEMO_LCD_D6_PIN, &outConfig);
    GPIO_PinInit(DEMO_LCD_D7_GPIO, DEMO_LCD_D7_PIN, &outConfig);

    /* Backlight: init as output and turn on immediately. */
    GPIO_PinInit(DEMO_LCD_BLK_GPIO, DEMO_LCD_BLK_PIN, &outConfig);
    LCD_SetBacklight(true);
}

/* Identical generic MIPI-DCS sequence to lcd_flexio_mculcd.c's LCD_InitPanel()
 * - the same one WORKLOG.md documents as proven working against this exact
 * physical panel on the earlier Arduino-header bit-bang revision - kept
 * byte-for-byte the same on purpose, so this test isolates the transport
 * (FlexIO vs bit-bang), not the init sequence itself. */
static void LCD_InitPanel(void)
{
    LCD_SetResetPin(false);
    SDK_DelayAtLeastUs(20000, SystemCoreClock);
    LCD_SetResetPin(true);
    SDK_DelayAtLeastUs(150000, SystemCoreClock);

    LCD_WriteCommand(0x01U); /* Software reset */
    SDK_DelayAtLeastUs(20000, SystemCoreClock);

    LCD_WriteCommand(0x11U); /* Sleep out */
    SDK_DelayAtLeastUs(150000, SystemCoreClock);

    /* MADCTL: memory access control (row/column exchange + BGR order).
     * 0x60, not 0x68 - see lcd_flexio_mculcd.c's LCD_InitPanel() comment:
     * the camera buffer is RGB565 and is sent unmodified, so the BGR bit
     * (0x08) must stay clear or R/B come out swapped (red/magenta tint). */
    LCD_WriteCommandData(0x36U, (const uint8_t[]){0x60U}, 1U);

    LCD_WriteCommandData(0x3AU, (const uint8_t[]){0x55U}, 1U); /* Pixel format: 16bpp RGB565 */

    LCD_WriteCommand(0x29U); /* Display ON */
    SDK_DelayAtLeastUs(50000, SystemCoreClock);
}

void LCD_Init(void)
{
    PRINTF("LCD: using bit-bang GPIO diagnostic driver (DEMO_LCD_BITBANG=1)\r\n");
    LCD_InitGpioPins();
    LCD_InitPanel();
}

void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    const uint8_t colBuf[4] = {(uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFFU), (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFFU)};
    const uint8_t rowBuf[4] = {(uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFFU), (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFFU)};

    LCD_SetCSPin(false);
    LCD_WriteCommandOpen(0x2AU); /* Column address set */
    LCD_WriteDataArrayOpen(colBuf, sizeof(colBuf));
    LCD_WriteCommandOpen(0x2BU); /* Page (row) address set */
    LCD_WriteDataArrayOpen(rowBuf, sizeof(rowBuf));
    LCD_WriteCommandOpen(0x2CU); /* Memory write - following bytes are pixels */
    /* CS stays asserted - LCD_PushPixels() closes it. */
}

void LCD_PushPixels(const uint16_t *pixels, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        LCD_WriteByte((uint8_t)(pixels[i] >> 8));
        LCD_WriteByte((uint8_t)(pixels[i] & 0xFFU));
    }

    LCD_SetCSPin(true);
}

void LCD_DrawImage(uint16_t x0, uint16_t y0, uint16_t width, uint16_t height, const uint16_t *pixels)
{
    LCD_SetWindow(x0, y0, (uint16_t)(x0 + width - 1U), (uint16_t)(y0 + height - 1U));
    LCD_PushPixels(pixels, (uint32_t)width * height);
}
