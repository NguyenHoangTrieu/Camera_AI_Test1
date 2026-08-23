/*
 * lcd_display.h - selects the LCD backend main.c talks to.
 *
 * Both backends expose the identical API (LCD_Init/SetWindow/PushPixels/
 * DrawImage): lcd_bitbang.h (current default) or lcd_flexio_mculcd.h
 * (abandoned, see WORKLOG.md). Selected by DEMO_LCD_BITBANG (set globally
 * by CMakeLists.txt), so this file is the only place that needs to know
 * which one is active.
 */
#ifndef _LCD_DISPLAY_H_
#define _LCD_DISPLAY_H_

#include "app.h" /* for DEMO_LCD_BITBANG */

#if DEMO_LCD_BITBANG
#include "lcd_bitbang.h"
#else
#include "lcd_flexio_mculcd.h"
#endif

#endif /* _LCD_DISPLAY_H_ */
