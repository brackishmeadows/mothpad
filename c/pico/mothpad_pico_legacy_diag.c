#include "i2ckbd.h"
#include "lcdspi.h"
#include "pico/stdlib.h"

#include <stdio.h>

#define CELL_W 8
#define CELL_H 12
#define COLS   40

static void legacy_cell(int x, int y, char ch, int fg, int bg)
{
    lcd_set_cursor((short)(x * CELL_W), (short)(y * CELL_H));
    lcd_set_colors(fg, bg);
    lcd_put_char(ch, 1);
}

static void legacy_row(int y, int fg, int bg)
{
    for(int x = 0; x < COLS; ++x) legacy_cell(x, y, ' ', fg, bg);
}

static void legacy_text(int x, int y, const char *text, int fg, int bg)
{
    for(int i = 0; text && text[i] && x + i < COLS; ++i)
    {
        legacy_cell(x + i, y, text[i], fg, bg);
    }
}

int main(void)
{
    int last_key = -999;
    char line[40];

    stdio_init_all();
    sleep_ms(250);

    lcd_init();
    lcd_clear();
    init_i2c_kbd();

    legacy_row(0, BLACK, WHITE);
    legacy_text(0, 0, "MOTHPAD LEGACY DIAG", BLACK, WHITE);
    legacy_text(0, 2, "USES OLD LCD/KBD DRIVER", WHITE, BLACK);
    legacy_text(0, 4, "PRESS KEYS. KEY SHOULD CHANGE.", WHITE, BLACK);
    legacy_text(0, 6, "KEY: ----", WHITE, BLACK);
    legacy_row(25, BLACK, WHITE);
    legacy_text(0, 25, "UP/F1/F5 ON POWER FOR LOADER", BLACK, WHITE);

    for(;;)
    {
        int key = read_i2c_kbd();
        if(key != last_key)
        {
            snprintf(line, sizeof(line), "KEY: %04d", key);
            legacy_text(0, 6, "KEY: ----", WHITE, BLACK);
            legacy_text(0, 6, line, WHITE, BLACK);
            last_key = key;
        }
        sleep_ms(20);
    }
}
