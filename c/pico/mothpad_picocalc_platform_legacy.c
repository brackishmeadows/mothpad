#include "mothpad_picocalc_platform.h"

#include "i2ckbd.h"
#include "lcdspi.h"

void picocalc_lcd_init(void)
{
    lcd_init();
}

void picocalc_lcd_clear(void)
{
    lcd_clear();
}

void picocalc_lcd_set_cursor(short x, short y)
{
    lcd_set_cursor(x, y);
}

void picocalc_lcd_set_colors(int fg, int bg)
{
    lcd_set_colors(fg, bg);
}

void picocalc_lcd_put_char(char ch, int flush)
{
    lcd_put_char(ch, flush);
}

void picocalc_lcd_draw_mono_bitmap(const uint8_t *bits, int width, int height, int stride, int fg, int bg)
{
    (void)bits;
    (void)width;
    (void)height;
    (void)stride;
    (void)fg;
    (void)bg;
}

void picocalc_kbd_init(void)
{
    init_i2c_kbd();
}

int picocalc_kbd_read_raw(uint16_t *out)
{
    (void)out;
    return -1;
}

int picocalc_kbd_read_matrix(uint8_t *out, size_t out_size)
{
    (void)out;
    (void)out_size;
    return -1;
}

int picocalc_kbd_read_joystick(uint8_t *out)
{
    (void)out;
    return -1;
}

int picocalc_kbd_read_event(int *key, int *shift)
{
    int k = read_i2c_kbd();
    if(shift) *shift = 0;
    if(k < 0)
    {
        if(key) *key = -1;
        return -1;
    }
    if(key) *key = k;
    return 0;
}

int picocalc_kbd_shift_down(void)
{
    return 0;
}

int picocalc_kbd_read(void)
{
    return read_i2c_kbd();
}

int picocalc_kbd_read_battery(int *percent, int *charging)
{
    int raw = read_battery();
    if(raw < 0) return -1;

    raw = (raw >> 8) & 0xff;
    if(charging) *charging = (raw & 0x80) != 0;
    raw &= 0x7f;
    if(raw > 100) raw = 100;
    if(percent) *percent = raw;
    return 0;
}
