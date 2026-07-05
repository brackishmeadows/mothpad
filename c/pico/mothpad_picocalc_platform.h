#ifndef MOTHPAD_PICOCALC_PLATFORM_H
#define MOTHPAD_PICOCALC_PLATFORM_H

#include <stdint.h>

#define PICOCALC_LCD_WIDTH  320
#define PICOCALC_LCD_HEIGHT 320

#define PICOCALC_COLOR_BLACK 0x000000
#define PICOCALC_COLOR_WHITE 0xffffff

#define PICOCALC_COLOR_ORDER_RGB 0
#define PICOCALC_COLOR_ORDER_RBG 1
#define PICOCALC_COLOR_ORDER_GRB 2
#define PICOCALC_COLOR_ORDER_GBR 3
#define PICOCALC_COLOR_ORDER_BRG 4
#define PICOCALC_COLOR_ORDER_BGR 5

void picocalc_lcd_init(void);
void picocalc_lcd_clear(void);
void picocalc_lcd_set_color_order(int order);
void picocalc_lcd_set_panel_color_mode(int bgr, int invert);
void picocalc_lcd_set_pixel_format(int bits_per_pixel);
void picocalc_lcd_set_cursor(short x, short y);
void picocalc_lcd_set_colors(int fg, int bg);
void picocalc_lcd_put_char(char ch, int flush);

void picocalc_kbd_init(void);
int picocalc_kbd_read_raw(uint16_t *out);
int picocalc_kbd_read(void);
int picocalc_kbd_read_battery(int *percent, int *charging);

#endif
