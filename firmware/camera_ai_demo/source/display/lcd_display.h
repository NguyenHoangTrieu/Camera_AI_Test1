/*
 * lcd_display.h - selects the LCD backend main.c actually talks to.
 *
 * Both backends expose the identical public API (LCD_Init/SetWindow/
 * PushPixels/DrawImage) - see lcd_flexio_mculcd.h (production, hardware-
 * accelerated) and lcd_bitbang_j8.h (diagnostic, see that file's header
 * comment for why it exists). Selected by DEMO_LCD_BITBANG, which
 * CMakeLists.txt's LCD_BITBANG_DIAGNOSTIC option sets globally - so this
 * file, not main.c, is the only place that needs to know which one is
 * active.
 */
#ifndef _LCD_DISPLAY_H_
#define _LCD_DISPLAY_H_

#include "app.h" /* for DEMO_LCD_BITBANG */

#if DEMO_LCD_BITBANG
#include "lcd_bitbang_j8.h"
#else
#include "lcd_flexio_mculcd.h"
#endif

#endif /* _LCD_DISPLAY_H_ */
