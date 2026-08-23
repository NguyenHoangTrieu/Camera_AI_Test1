/*
 * lcd_bitbang.h - GPIO bit-bang LCD driver (8080 8-bit bus).
 *
 * Same public API as lcd_flexio_mculcd.h. Pin set comes from app.h's
 * DEMO_LCD_* macros (Arduino header or J8, see DEMO_LCD_ARDUINO_HEADER).
 */
#ifndef _LCD_BITBANG_H_
#define _LCD_BITBANG_H_

#include <stdint.h>

/*! @brief Init the bit-banged GPIO bus and reset/configure the panel. */
void LCD_Init(void);

/*! @brief Set the active drawing window (inclusive pixel coordinates) and push pixels. */
void LCD_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/*! @brief Push `count` RGB565 pixels into the window set by LCD_SetWindow. */
void LCD_PushPixels(const uint16_t *pixels, uint32_t count);

/*! @brief Convenience: set window then push a full width*height block. */
void LCD_DrawImage(uint16_t x0, uint16_t y0, uint16_t width, uint16_t height, const uint16_t *pixels);

#endif /* _LCD_BITBANG_H_ */
