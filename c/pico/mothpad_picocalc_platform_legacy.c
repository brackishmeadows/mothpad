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

void picocalc_kbd_init(void)
{
    init_i2c_kbd();
}

int picocalc_kbd_read_raw(uint16_t *out)
{
    (void)out;
    return -1;
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
