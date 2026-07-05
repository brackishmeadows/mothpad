#include "mothpad_picocalc_platform.h"

#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>

#define CELL_W 8
#define CELL_H 12
#define COLS   40
#define ROWS   26

static void diag_cell(int x, int y, char ch, int fg, int bg)
{
    picocalc_lcd_set_cursor((short)(x * CELL_W), (short)(y * CELL_H));
    picocalc_lcd_set_colors(fg, bg);
    picocalc_lcd_put_char(ch, 1);
}

static void diag_row(int y, int fg, int bg)
{
    for(int x = 0; x < COLS; ++x) diag_cell(x, y, ' ', fg, bg);
}

static void diag_text(int x, int y, const char *text, int fg, int bg)
{
    for(int i = 0; text && text[i] && x + i < COLS; ++i)
    {
        diag_cell(x + i, y, text[i], fg, bg);
    }
}

static void diag_color_blocks(int x, int y, int color_order, int pixel_bits)
{
    picocalc_lcd_set_panel_color_mode(1, 1);
    picocalc_lcd_set_pixel_format(pixel_bits);
    picocalc_lcd_set_color_order(color_order);
    for(int i = 0; i < 3; ++i) diag_cell(x + i, y, ' ', PICOCALC_COLOR_BLACK, 0xff0000);
    for(int i = 0; i < 3; ++i) diag_cell(x + 4 + i, y, ' ', PICOCALC_COLOR_BLACK, 0x00ff00);
    for(int i = 0; i < 3; ++i) diag_cell(x + 8 + i, y, ' ', PICOCALC_COLOR_BLACK, 0x0000ff);
    for(int i = 0; i < 3; ++i) diag_cell(x + 12 + i, y, ' ', PICOCALC_COLOR_BLACK, 0xffffff);
    for(int i = 0; i < 3; ++i) diag_cell(x + 16 + i, y, ' ', PICOCALC_COLOR_BLACK, 0x00ffff);
    for(int i = 0; i < 3; ++i) diag_cell(x + 20 + i, y, ' ', PICOCALC_COLOR_BLACK, 0xff00ff);
    for(int i = 0; i < 3; ++i) diag_cell(x + 24 + i, y, ' ', PICOCALC_COLOR_BLACK, 0xffff00);
    picocalc_lcd_set_color_order(PICOCALC_COLOR_ORDER_RGB);
    picocalc_lcd_set_pixel_format(18);
}

static void diag_screen_base(void)
{
    picocalc_lcd_set_colors(PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    picocalc_lcd_clear();

    diag_row(0, PICOCALC_COLOR_BLACK, PICOCALC_COLOR_WHITE);
    diag_text(0, 0, "MOTHPAD DIAG  NO SD  NO FILES", PICOCALC_COLOR_BLACK, PICOCALC_COLOR_WHITE);

    diag_text(0, 2, "IF YOU SEE THIS, LCD TEXT WORKS.", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_text(0, 4, "PRESS KEYS. RAW PACKET SHOULD CHANGE.", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_text(0, 6, "RAW: ----  KEY: ---", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_text(0, 8, "CTRL+S AND CTRL+O SHOULD SHOW KEY 019/015.", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_text(0, 10, "GLYPHS: abc xyz ~ { } [ ] \\ |", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_text(0, 12, "3RGB:", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_color_blocks(6, 12, PICOCALC_COLOR_ORDER_RGB, 18);
    diag_text(0, 13, "3RBG:", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_color_blocks(6, 13, PICOCALC_COLOR_ORDER_RBG, 18);
    diag_text(0, 14, "3GRB:", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_color_blocks(6, 14, PICOCALC_COLOR_ORDER_GRB, 18);
    diag_text(0, 15, "3GBR:", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_color_blocks(6, 15, PICOCALC_COLOR_ORDER_GBR, 18);
    diag_text(0, 16, "3BRG:", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_color_blocks(6, 16, PICOCALC_COLOR_ORDER_BRG, 18);
    diag_text(0, 17, "3BGR:", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_color_blocks(6, 17, PICOCALC_COLOR_ORDER_BGR, 18);

    diag_text(0, 18, "2RGB:", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_color_blocks(6, 18, PICOCALC_COLOR_ORDER_RGB, 16);
    diag_text(0, 19, "2RBG:", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_color_blocks(6, 19, PICOCALC_COLOR_ORDER_RBG, 16);
    diag_text(0, 20, "2GRB:", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_color_blocks(6, 20, PICOCALC_COLOR_ORDER_GRB, 16);
    diag_text(0, 21, "2GBR:", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_color_blocks(6, 21, PICOCALC_COLOR_ORDER_GBR, 16);
    diag_text(0, 22, "2BRG:", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_color_blocks(6, 22, PICOCALC_COLOR_ORDER_BRG, 16);
    diag_text(0, 23, "2BGR:", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    diag_color_blocks(6, 23, PICOCALC_COLOR_ORDER_BGR, 16);

    diag_row(25, PICOCALC_COLOR_BLACK, PICOCALC_COLOR_WHITE);
    diag_text(0, 25, "HOLD UP/F1/F5 ON POWER FOR LOADER", PICOCALC_COLOR_BLACK, PICOCALC_COLOR_WHITE);
}

int main(void)
{
    char line[40];
    uint16_t last_raw = 0xffff;

    stdio_init_all();
    sleep_ms(250);

    picocalc_lcd_init();
    picocalc_kbd_init();
    diag_screen_base();

    for(;;)
    {
        uint16_t raw = 0;
        if(picocalc_kbd_read_raw(&raw) == 0 && raw != last_raw)
        {
            int key = raw >> 8;
            int state = raw & 0xff;
            snprintf(line, sizeof(line), "RAW: %04X  KEY:%03d  ST:%03d", raw, key, state);
            diag_text(0, 6, "RAW: ----  KEY:---  ST:---", PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
            diag_text(0, 6, line, PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
            last_raw = raw;
        }

        sleep_ms(20);
    }
}
