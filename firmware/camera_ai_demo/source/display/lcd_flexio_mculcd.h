/*
 * lcd_flexio_mculcd.h - hardware-accelerated 8-bit parallel LCD driver
 *
 * Wraps the SDK's FLEXIO_MCULCD + ST7796S drivers (FlexIO peripheral,
 * MIPI-DBI Type-B / 8080 bus) for the J8 FlexIO/LCD header on FRDM-MCXN947 -
 * the same connector and driver stack as NXP's own
 * `display_examples/smartdma_camera_flexio_mculcd` reference example, just
 * configured for an 8-bit data bus (FLEXIO_MCULCD_DATA_BUS_WIDTH=8 in
 * CMakeLists.txt) since the panel in hand only has LCD_D0..D7, not J8's full
 * 16-bit bus. See ../../README.md for J8 pinout and the panel this targets
 * (NXP LCD-PAR-S035 or another ST7796S-based panel).
 */
#ifndef _LCD_FLEXIO_MCULCD_H_
#define _LCD_FLEXIO_MCULCD_H_

#include <stdint.h>

/*! @brief Init the FlexIO MCULCD bus and reset/configure the ST7796S panel. */
void LCD_Init(void);

/*! @brief Set the active drawing window (inclusive pixel coordinates) and push pixels. */
void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/*! @brief Push `count` RGB565 pixels into the window set by LCD_SetWindow. */
void LCD_PushPixels(const uint16_t *pixels, uint32_t count);

/*! @brief Convenience: set window then push a full width*height block. */
void LCD_DrawImage(uint16_t x0, uint16_t y0, uint16_t width, uint16_t height, const uint16_t *pixels);

#endif /* _LCD_FLEXIO_MCULCD_H_ */
