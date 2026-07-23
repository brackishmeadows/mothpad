#ifndef MOTHPAD_PICOCALC_PLATFORM_H
#define MOTHPAD_PICOCALC_PLATFORM_H

#include <stddef.h>
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

#define PICOCALC_GLYPH_MENU_TL   ((char)0x80)
#define PICOCALC_GLYPH_MENU_TR   ((char)0x81)
#define PICOCALC_GLYPH_MENU_BL   ((char)0x82)
#define PICOCALC_GLYPH_MENU_BR   ((char)0x83)
#define PICOCALC_GLYPH_MENU_HT   ((char)0x84)
#define PICOCALC_GLYPH_MENU_VL   ((char)0x85)
#define PICOCALC_GLYPH_MENU_HB   ((char)0x86)
#define PICOCALC_GLYPH_MENU_VR   ((char)0x87)
#define PICOCALC_GLYPH_BAT_L_0   ((char)0x88)
#define PICOCALC_GLYPH_BAT_L_50  ((char)0x89)
#define PICOCALC_GLYPH_BAT_L_100 ((char)0x8a)
#define PICOCALC_GLYPH_BAT_R_0   ((char)0x8b)
#define PICOCALC_GLYPH_BAT_R_50  ((char)0x8c)
#define PICOCALC_GLYPH_BAT_R_100 ((char)0x8d)
#define PICOCALC_GLYPH_BAT_CHARGE ((char)0x8e)
#define PICOCALC_GLYPH_CHECKMARK ((char)0x8f)
#define PICOCALC_GLYPH_CHECKBOX_OFF ((char)0x90)
#define PICOCALC_GLYPH_CHECKBOX_ON  ((char)0x91)
#define PICOCALC_GLYPH_RADIO_OFF ((char)0x92)
#define PICOCALC_GLYPH_RADIO_ON  ((char)0x93)
#define PICOCALC_GLYPH_ARROW_LEFT ((char)0x94)
#define PICOCALC_GLYPH_MOTH_L    ((char)0x95)
#define PICOCALC_GLYPH_MOTH_R    ((char)0x96)
#define PICOCALC_GLYPH_SOLID     ((char)0x97)

void picocalc_lcd_init(void);
void picocalc_lcd_clear(void);
void picocalc_lcd_set_color_order(int order);
void picocalc_lcd_set_panel_color_mode(int bgr, int invert);
void picocalc_lcd_set_pixel_format(int bits_per_pixel);
void picocalc_lcd_set_cursor(short x, short y);
void picocalc_lcd_set_colors(int fg, int bg);
void picocalc_lcd_put_char(char ch, int flush);
void picocalc_lcd_put_char_flags(char ch, int flags, int flush);
void picocalc_lcd_draw_mono_bitmap(const uint8_t *bits, int width, int height, int stride, int fg, int bg);

void picocalc_kbd_init(void);
int picocalc_kbd_read_raw(uint16_t *out);
int picocalc_kbd_read_matrix(uint8_t *out, size_t out_size);
int picocalc_kbd_read_joystick(uint8_t *out);
int picocalc_kbd_read_event(int *key, int *shift);
int picocalc_kbd_shift_down(void);
int picocalc_kbd_read(void);
int picocalc_kbd_read_battery(int *percent, int *charging);

#endif
