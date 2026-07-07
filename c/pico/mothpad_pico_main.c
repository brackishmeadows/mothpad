#include "mothpad.h"

#include "blockdevice/sd.h"
#include "filesystem/fat.h"
#include "filesystem/vfs.h"
#include "hardware/watchdog.h"
#include "mothpad_picocalc_platform.h"
#include "mothpad_splash_bitmap.h"
#include "pico/stdlib.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/dirent.h>

extern DIR *opendir(const char *path);
extern int closedir(DIR *dir);
extern struct dirent *readdir(DIR *dir);

#define PICOCALC_FONT_WIDTH  8
#define PICOCALC_FONT_HEIGHT 12

#define PICOCALC_KEY_BACKSPACE 0x08
#define PICOCALC_KEY_TAB       0x09
#define PICOCALC_KEY_ENTER     0x0A
#define PICOCALC_KEY_F1        0x81
#define PICOCALC_KEY_F2        0x82
#define PICOCALC_KEY_F3        0x83
#define PICOCALC_KEY_F4        0x84
#define PICOCALC_KEY_F5        0x85
#define PICOCALC_KEY_ESC       0xB1
#define PICOCALC_KEY_LEFT      0xB4
#define PICOCALC_KEY_UP        0xB5
#define PICOCALC_KEY_DOWN      0xB6
#define PICOCALC_KEY_RIGHT     0xB7
#define PICOCALC_KEY_HOME      0xD2
#define PICOCALC_KEY_DEL       0xD4
#define PICOCALC_KEY_END       0xD5
#define PICOCALC_KEY_PAGE_UP   0xD6
#define PICOCALC_KEY_PAGE_DOWN 0xD7

#define PICOCALC_CTRL_A        1
#define PICOCALC_CTRL_C        3
#define PICOCALC_CTRL_D        4
#define PICOCALC_CTRL_F        6
#define PICOCALC_CTRL_N        14
#define PICOCALC_CTRL_O        15
#define PICOCALC_CTRL_Q        17
#define PICOCALC_CTRL_S        19
#define PICOCALC_CTRL_V        22
#define PICOCALC_CTRL_X        24
#define PICOCALC_CTRL_Z        26

#define SD_SCLK_PIN            18
#define SD_MOSI_PIN            19
#define SD_MISO_PIN            16
#define SD_CS_PIN              17
#define SD_DET_PIN             22

#define MOTH_MAX_DIR_ENTRIES   96
#define MOTH_MESSAGE_MS        1800
#define MOTH_BATTERY_MS        250
#define MOTH_FILE_LIST_COLS    18
#define MOTH_PREVIEW_X         (MOTH_FILE_LIST_COLS + 1)
#define MOTH_PREVIEW_COLS      (MOTH_COLS - MOTH_PREVIEW_X)
#define MOTH_PREVIEW_LINES     (MOTH_TEXT_ROWS - 3)
#define PICO_CLIPBOARD_SIZE    4096
#define PICO_SHIFT_ARROW_INITIAL_MS 180
#define PICO_SHIFT_ARROW_REPEAT_MS 22
#define PICO_SHIFT_ARROW_RELEASE_MS 90
#define PICO_JOY_RIGHT_BIT     0x01
#define PICO_JOY_LEFT_BIT      0x08
#define PICO_RECOVERY_PATH     "/.mothpad-recovery.txt"
#define PICO_RECOVERY_META_PATH "/.mothpad-recovery.meta"
#define PICO_SETTINGS_PATH     "/.mothpad-settings.txt"
#define PICO_RECOVERY_DELAY_MS 8000
#define PICO_RECOVERY_RETRY_MS 30000

typedef enum {
    PICO_MODE_EDITING = 0,
    PICO_MODE_MENU,
    PICO_MODE_FILE_LIST,
    PICO_MODE_SAVE_AS,
    PICO_MODE_FIND,
    PICO_MODE_DIRTY_CONFIRM,
    PICO_MODE_RECOVERY_CONFIRM,
    PICO_MODE_CALC,
    PICO_MODE_ERROR_MESSAGE,
} PicoMode;

typedef enum {
    PICO_DIRTY_NONE = 0,
    PICO_DIRTY_NEW,
    PICO_DIRTY_OPEN,
    PICO_DIRTY_REBOOT,
} PicoDirtyAction;

typedef enum {
    PICO_MENU_FILE = 0,
    PICO_MENU_EDIT,
    PICO_MENU_SELECT,
    PICO_MENU_VIEW,
    PICO_MENU_COUNT,
} PicoMenu;

typedef struct {
    char ch;
    uint8_t fg;
    uint8_t bg;
} PicoCell;

typedef struct {
    char name[256];
    int is_dir;
} PicoDirEntry;

static Mothpad g_mothpad;
static PicoCell g_lcd_cells[MOTH_COLS * MOTH_ROWS];
static int g_lcd_cells_valid;
static int g_cursor_visible = 1;
static absolute_time_t g_next_cursor_blink;
static absolute_time_t g_next_battery_update;
static int g_battery_percent = -1;
static int g_battery_charging;
static int g_shift_down;
static int g_shift_arrow_dir;
static int g_shift_arrow_releasing;
static absolute_time_t g_next_shift_arrow;
static absolute_time_t g_shift_arrow_release_until;
static int g_recovery_pending;
static absolute_time_t g_recovery_due;

static PicoMode g_mode = PICO_MODE_EDITING;
static PicoMode g_return_mode = PICO_MODE_EDITING;
static int g_sd_ready;
static char g_cwd[256] = "/";
static char g_message[80];
static absolute_time_t g_message_until;

static PicoDirEntry g_entries[MOTH_MAX_DIR_ENTRIES];
static int g_entry_count;
static int g_entry_selected;
static int g_entry_scroll;
static int g_preview_selected = -1;
static char g_preview_cwd[256];
static char g_preview_lines[MOTH_PREVIEW_LINES][MOTH_PREVIEW_COLS + 1];
static int g_preview_line_count;
static int g_preview_visible;

static char g_save_name[256];
static int g_save_name_len;
static char g_find_query[80];
static int g_find_query_len;
static PicoDirtyAction g_after_save_action = PICO_DIRTY_NONE;
static PicoDirtyAction g_dirty_action = PICO_DIRTY_NONE;
static int g_dirty_selected;
static int g_recovery_selected;
static char g_clipboard[PICO_CLIPBOARD_SIZE];
static int g_clipboard_len;
static int g_keep_backups = 1;
static int g_settings_version = 2;
static int g_tab_insert_spaces = 0;
static int g_tab_width = 2;

static const char *g_file_menu_items[] = {
    "New",
    "Open...",
    "Save",
    "Save As...",
    "Keep Backups",
    "Reboot",
};
static const char *g_edit_menu_items[] = {
    "Undo",
    "Cut Line",
    "Copy Line",
    "Paste",
};
static const char *g_select_menu_items[] = {
    "Find...",
    "Select All",
    "Select None",
};
static const char *g_view_menu_items[] = {
    "Wrap",
    "Edit Mode",
    "Read Mode",
};
static int g_menu_selected[PICO_MENU_COUNT];
static PicoMenu g_active_menu;

static void pico_perform_dirty_action(PicoDirtyAction action);
static void pico_draw_popup_box(int x, int y, int width, int height);
static void pico_update_file_preview(void);
static void pico_write_recovery_meta(void);
static void pico_save_settings(void);
static int pico_can_write(void);

static int pico_ascii_lower(int ch)
{
    if(ch >= 'A' && ch <= 'Z') return ch + ('a' - 'A');
    return ch;
}

static int pico_name_compare_casefold(const char *a, const char *b)
{
    int i = 0;
    for(;;)
    {
        int ca = pico_ascii_lower((unsigned char)a[i]);
        int cb = pico_ascii_lower((unsigned char)b[i]);
        if(ca != cb) return ca - cb;
        if(ca == 0) return strcmp(a, b);
        ++i;
    }
}

static int pico_compare_dir_entries(const void *a, const void *b)
{
    const PicoDirEntry *ea = (const PicoDirEntry *)a;
    const PicoDirEntry *eb = (const PicoDirEntry *)b;

    if(strcmp(ea->name, "..") == 0) return -1;
    if(strcmp(eb->name, "..") == 0) return 1;
    if(ea->is_dir != eb->is_dir) return eb->is_dir - ea->is_dir;
    return pico_name_compare_casefold(ea->name, eb->name);
}

static int mothpad_pico_color(uint8_t color)
{
    if(color == 0) return PICOCALC_COLOR_BLACK;
    return PICOCALC_COLOR_WHITE;
}

static void pico_set_message(const char *message)
{
    snprintf(g_message, sizeof(g_message), "%s", message ? message : "");
    g_message_until = make_timeout_time_ms(MOTH_MESSAGE_MS);
}

static int pico_message_active(void)
{
    return g_message[0] && absolute_time_diff_us(get_absolute_time(), g_message_until) > 0;
}

static void pico_update_shift_state(void)
{
    g_shift_down = picocalc_kbd_shift_down();
}

static void pico_clear_recovery_timer(void)
{
    g_recovery_pending = 0;
}

static void pico_schedule_recovery_write(void)
{
    if(g_sd_ready && g_mothpad.dirty)
    {
        g_recovery_pending = 1;
        g_recovery_due = make_timeout_time_ms(PICO_RECOVERY_DELAY_MS);
    }
    else if(!g_mothpad.dirty)
    {
        pico_clear_recovery_timer();
    }
}

static int pico_recovery_write_ready(void)
{
    return g_recovery_pending &&
           g_sd_ready &&
           g_mothpad.dirty &&
           g_mode == PICO_MODE_EDITING &&
           absolute_time_diff_us(get_absolute_time(), g_recovery_due) <= 0;
}

static void pico_write_recovery_copy(void)
{
    MothStatus status = moth_write_recovery_file(&g_mothpad, PICO_RECOVERY_PATH);
    if(status == MOTH_OK)
    {
        pico_write_recovery_meta();
        g_recovery_pending = 0;
        pico_set_message("Recovery saved");
    }
    else
    {
        g_recovery_due = make_timeout_time_ms(PICO_RECOVERY_RETRY_MS);
        pico_set_message("Recovery failed");
    }
}

static int pico_recovery_file_exists(void)
{
    FILE *file;
    if(!g_sd_ready) return 0;
    file = fopen(PICO_RECOVERY_PATH, "rb");
    if(!file) return 0;
    fclose(file);
    return 1;
}

static void pico_delete_recovery_files(void)
{
    if(!g_sd_ready) return;
    remove(PICO_RECOVERY_PATH);
    remove(PICO_RECOVERY_PATH ".tmp");
    remove(PICO_RECOVERY_META_PATH);
    remove(PICO_RECOVERY_META_PATH ".tmp");
}

static void pico_trim_line(char *line)
{
    size_t len;
    if(!line) return;
    len = strlen(line);
    while(len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t'))
    {
        line[--len] = 0;
    }
}

static void pico_load_settings(void)
{
    char line[96];
    FILE *file;
    int settings_version = 0;

    if(!g_sd_ready) return;
    file = fopen(PICO_SETTINGS_PATH, "rb");
    if(!file) return;

    while(fgets(line, sizeof(line), file))
    {
        char key[40];
        char value[40];
        pico_trim_line(line);
        if(sscanf(line, "%39[^=]=%39s", key, value) != 2) continue;

        if(strcmp(key, "keep_backups") == 0) g_keep_backups = atoi(value) ? 1 : 0;
        else if(strcmp(key, "settings_version") == 0) settings_version = atoi(value);
        else if(strcmp(key, "soft_wrap") == 0) g_mothpad.soft_wrap = atoi(value) ? 1 : 0;
        else if(strcmp(key, "tab_insert_spaces") == 0) g_tab_insert_spaces = atoi(value) ? 1 : 0;
        else if(strcmp(key, "tab_width") == 0)
        {
            int width = atoi(value);
            g_tab_width = (width == 2) ? 2 : 4;
        }
    }

    fclose(file);
    if(settings_version < 2)
    {
        g_tab_insert_spaces = 0;
        g_tab_width = 2;
    }
    g_settings_version = 2;
    g_mothpad.tab_width = g_tab_width;
}

static void pico_save_settings(void)
{
    FILE *file;

    if(!g_sd_ready) return;
    file = fopen(PICO_SETTINGS_PATH ".tmp", "wb");
    if(!file) return;

    fprintf(file, "settings_version=%d\n", g_settings_version);
    fprintf(file, "keep_backups=%d\n", g_keep_backups ? 1 : 0);
    fprintf(file, "soft_wrap=%d\n", g_mothpad.soft_wrap ? 1 : 0);
    fprintf(file, "tab_insert_spaces=%d\n", g_tab_insert_spaces ? 1 : 0);
    fprintf(file, "tab_width=%d\n", g_tab_width);
    fclose(file);

    remove(PICO_SETTINGS_PATH);
    rename(PICO_SETTINGS_PATH ".tmp", PICO_SETTINGS_PATH);
}

static void pico_write_recovery_meta(void)
{
    FILE *file;

    if(!g_sd_ready) return;
    file = fopen(PICO_RECOVERY_META_PATH ".tmp", "wb");
    if(!file) return;
    fprintf(file, "%s\n", g_mothpad.path);
    fclose(file);
    remove(PICO_RECOVERY_META_PATH);
    rename(PICO_RECOVERY_META_PATH ".tmp", PICO_RECOVERY_META_PATH);
}

static void pico_read_recovery_meta(char *path, size_t path_size)
{
    FILE *file;

    if(!path || path_size == 0) return;
    path[0] = 0;
    if(!g_sd_ready) return;
    file = fopen(PICO_RECOVERY_META_PATH, "rb");
    if(!file) return;
    if(fgets(path, (int)path_size, file)) pico_trim_line(path);
    fclose(file);
}

static char mothpad_lcd_cell_char(const MothCell *cell)
{
    char ch = (cell && cell->ch) ? (char)cell->ch : ' ';
    if(cell && (cell->flags & MOTH_CELL_CURSOR) && g_cursor_visible)
    {
        ch = '_';
    }
    return ch;
}

static PicoCell mothpad_lcd_cell_from_moth(const MothCell *cell)
{
    PicoCell out;
    out.ch = mothpad_lcd_cell_char(cell);
    out.fg = cell ? cell->fg : 7;
    out.bg = cell ? cell->bg : 0;
    return out;
}

static int mothpad_lcd_cells_equal(PicoCell a, PicoCell b)
{
    return a.ch == b.ch && a.fg == b.fg && a.bg == b.bg;
}

static void mothpad_lcd_draw_cell(int x, int y, PicoCell cell)
{
    picocalc_lcd_set_cursor((short)(x * PICOCALC_FONT_WIDTH), (short)(y * PICOCALC_FONT_HEIGHT));
    picocalc_lcd_set_colors(mothpad_pico_color(cell.fg), mothpad_pico_color(cell.bg));
    picocalc_lcd_put_char(cell.ch, 1);
}

static void mothpad_lcd_flush_cells(const Mothpad *m, int force_full)
{
    if(force_full || !g_lcd_cells_valid)
    {
        picocalc_lcd_clear();
        g_lcd_cells_valid = 1;
        memset(g_lcd_cells, 0, sizeof(g_lcd_cells));
    }

    for(int y = 0; y < MOTH_ROWS; ++y)
    {
        for(int x = 0; x < MOTH_COLS; ++x)
        {
            const int index = y * MOTH_COLS + x;
            PicoCell cell = mothpad_lcd_cell_from_moth(moth_cell_at(m, x, y));
            if(force_full || !mothpad_lcd_cells_equal(g_lcd_cells[index], cell))
            {
                mothpad_lcd_draw_cell(x, y, cell);
                g_lcd_cells[index] = cell;
            }
        }
    }
}

static void pico_put_cell(int x, int y, char ch, uint8_t fg, uint8_t bg, uint8_t flags)
{
    if(x < 0 || y < 0 || x >= MOTH_COLS || y >= MOTH_ROWS) return;
    MothCell *cell = &g_mothpad.cells[y * MOTH_COLS + x];
    cell->ch = (uint8_t)ch;
    cell->fg = fg;
    cell->bg = bg;
    cell->flags = flags;
}

static void pico_fill_row(int y, uint8_t fg, uint8_t bg, uint8_t flags)
{
    for(int x = 0; x < MOTH_COLS; ++x) pico_put_cell(x, y, ' ', fg, bg, flags);
}

static void pico_clear_cells(void)
{
    for(int y = 0; y < MOTH_ROWS; ++y)
    {
        for(int x = 0; x < MOTH_COLS; ++x) pico_put_cell(x, y, ' ', 7, 0, 0);
    }
}

static void pico_draw_text(int x, int y, const char *text, uint8_t fg, uint8_t bg, uint8_t flags)
{
    for(int i = 0; text && text[i] && (x + i) < MOTH_COLS; ++i)
    {
        pico_put_cell(x + i, y, text[i], fg, bg, flags);
    }
}

static void pico_draw_hline(int x, int y, int width, char ch, uint8_t fg, uint8_t bg, uint8_t flags)
{
    for(int i = 0; i < width; ++i) pico_put_cell(x + i, y, ch, fg, bg, flags);
}

static void pico_draw_bottom_message(void)
{
    if(!pico_message_active()) return;
    pico_fill_row(MOTH_BOTTOM_ROW, 0, 7, MOTH_CELL_STATUS);
    pico_draw_text(0, MOTH_BOTTOM_ROW, g_message, 0, 7, MOTH_CELL_STATUS);
}

static void pico_update_battery(void)
{
    int percent = -1;
    int charging = 0;

    if(picocalc_kbd_read_battery(&percent, &charging) == 0)
    {
        g_battery_percent = percent;
        g_battery_charging = charging;
    }
    g_next_battery_update = make_timeout_time_ms(MOTH_BATTERY_MS);
}

static void pico_draw_battery(void)
{
    char text[12];
    int x;
    char left = PICOCALC_GLYPH_BAT_L_0;
    char right = PICOCALC_GLYPH_BAT_R_0;
    char charge = ' ';

    if(g_battery_percent < 0)
    {
        snprintf(text, sizeof(text), "--");
    }
    else
    {
        if(g_battery_percent >= 88)
        {
            left = PICOCALC_GLYPH_BAT_L_100;
            right = PICOCALC_GLYPH_BAT_R_100;
        }
        else if(g_battery_percent >= 63)
        {
            left = PICOCALC_GLYPH_BAT_L_100;
            right = PICOCALC_GLYPH_BAT_R_50;
        }
        else if(g_battery_percent >= 38)
        {
            left = PICOCALC_GLYPH_BAT_L_100;
            right = PICOCALC_GLYPH_BAT_R_0;
        }
        else if(g_battery_percent >= 13)
        {
            left = PICOCALC_GLYPH_BAT_L_50;
            right = PICOCALC_GLYPH_BAT_R_0;
        }
        if(g_battery_charging) charge = PICOCALC_GLYPH_BAT_CHARGE;
        snprintf(text, sizeof(text), "%d%%", g_battery_percent);
    }

    x = MOTH_COLS - (int)strlen(text) - 4;
    pico_draw_text(x, MOTH_TOP_ROW, text, 0, 7, MOTH_CELL_STATUS);
    x += (int)strlen(text);
    pico_put_cell(x++, MOTH_TOP_ROW, ' ', 0, 7, MOTH_CELL_STATUS);
    pico_put_cell(x++, MOTH_TOP_ROW, left, 0, 7, MOTH_CELL_STATUS);
    pico_put_cell(x++, MOTH_TOP_ROW, right, 0, 7, MOTH_CELL_STATUS);
    pico_put_cell(x, MOTH_TOP_ROW, charge, 0, 7, MOTH_CELL_STATUS);
}

static void pico_draw_moth_logo(void)
{
    pico_put_cell(MOTH_COLS - 3, MOTH_BOTTOM_ROW, PICOCALC_GLYPH_MOTH_L, 7, 0, MOTH_CELL_STATUS);
    pico_put_cell(MOTH_COLS - 2, MOTH_BOTTOM_ROW, PICOCALC_GLYPH_MOTH_R, 7, 0, MOTH_CELL_STATUS);
}

static void pico_render_editing(void)
{
    moth_render(&g_mothpad);
    pico_draw_battery();
    pico_draw_moth_logo();
    pico_draw_bottom_message();
}

static void pico_render_save_as(void)
{
    const int x = 4;
    const int y = 6;
    const int width = 24;
    const int height = 5;
    const int field_y = y + 2;
    const int field_width = width - 4;
    int text_start = 0;
    int cursor_x;

    pico_render_editing();
    pico_draw_popup_box(x, y, width, height);
    pico_draw_text(x + 2, y + 1, "Save as", 7, 0, MOTH_CELL_STATUS);

    for(int col = 2; col < width - 2; ++col) pico_put_cell(x + col, field_y, ' ', 0, 7, MOTH_CELL_SELECTION);
    if(g_save_name_len >= field_width) text_start = g_save_name_len - field_width + 1;
    for(int i = 0; i < field_width && g_save_name[text_start + i]; ++i)
    {
        pico_put_cell(x + 2 + i, field_y, g_save_name[text_start + i], 0, 7, MOTH_CELL_SELECTION);
    }
    cursor_x = x + 2 + g_save_name_len - text_start;
    if(cursor_x >= x + width - 2) cursor_x = x + width - 3;
    pico_put_cell(cursor_x, field_y, '_', 0, 7, MOTH_CELL_SELECTION);
    pico_draw_text(x + 2, y + 3, "Enter save  Esc cancel", 7, 0, MOTH_CELL_STATUS);
}

static void pico_render_find(void)
{
    const int x = 4;
    const int y = 6;
    const int width = 24;
    const int height = 5;
    const int field_y = y + 2;
    const int field_width = width - 4;
    int text_start = 0;
    int cursor_x;

    pico_render_editing();
    pico_draw_popup_box(x, y, width, height);
    pico_draw_text(x + 2, y + 1, "Find", 7, 0, MOTH_CELL_STATUS);

    for(int col = 2; col < width - 2; ++col) pico_put_cell(x + col, field_y, ' ', 0, 7, MOTH_CELL_SELECTION);
    if(g_find_query_len >= field_width) text_start = g_find_query_len - field_width + 1;
    for(int i = 0; i < field_width && g_find_query[text_start + i]; ++i)
    {
        pico_put_cell(x + 2 + i, field_y, g_find_query[text_start + i], 0, 7, MOTH_CELL_SELECTION);
    }
    cursor_x = x + 2 + g_find_query_len - text_start;
    if(cursor_x >= x + width - 2) cursor_x = x + width - 3;
    pico_put_cell(cursor_x, field_y, '_', 0, 7, MOTH_CELL_SELECTION);
    pico_draw_text(x + 2, y + 3, "Enter find  Esc", 7, 0, MOTH_CELL_STATUS);
}

static void pico_render_file_list(void)
{
    char top[96];
    int preview_row = MOTH_TEXT_FIRST_ROW + 3;
    int list_cols = g_preview_visible ? MOTH_FILE_LIST_COLS : MOTH_COLS;

    pico_update_file_preview();
    list_cols = g_preview_visible ? MOTH_FILE_LIST_COLS : MOTH_COLS;
    pico_clear_cells();
    pico_fill_row(MOTH_TOP_ROW, 0, 7, MOTH_CELL_STATUS);
    pico_fill_row(MOTH_BOTTOM_ROW, 0, 7, MOTH_CELL_STATUS);

    snprintf(top, sizeof(top), "Open: %s", g_cwd);
    pico_draw_text(0, MOTH_TOP_ROW, top, 0, 7, MOTH_CELL_STATUS);
    pico_draw_battery();

    for(int row = 0; row < MOTH_TEXT_ROWS; ++row)
    {
        int entry_index = g_entry_scroll + row;
        int y = MOTH_TEXT_FIRST_ROW + row;

        uint8_t fg = 7;
        uint8_t bg = 0;
        uint8_t flags = 0;
        char line[MOTH_COLS + 1];

        if(g_preview_visible) pico_put_cell(MOTH_FILE_LIST_COLS, y, PICOCALC_GLYPH_MENU_VL, 7, 0, MOTH_CELL_STATUS);
        if(entry_index >= g_entry_count) continue;

        if(entry_index == g_entry_selected)
        {
            fg = 0;
            bg = 7;
            flags = MOTH_CELL_SELECTION;
            for(int col = 0; col < list_cols; ++col) pico_put_cell(col, y, ' ', fg, bg, flags);
        }

        snprintf(line, sizeof(line), "%c %-*.*s",
                 g_entries[entry_index].is_dir ? '/' : ' ',
                 list_cols - 3,
                 list_cols - 3,
                 g_entries[entry_index].name);
        pico_draw_text(0, y, line, fg, bg, flags);
    }

    if(g_preview_visible)
    {
        pico_draw_text(MOTH_PREVIEW_X, MOTH_TEXT_FIRST_ROW, "Peek", 7, 0, MOTH_CELL_STATUS);
        if(g_entry_count > 0 && g_entry_selected >= 0 && g_entry_selected < g_entry_count)
        {
            char selected[32];
            snprintf(selected, sizeof(selected), "%.20s", g_entries[g_entry_selected].name);
            pico_draw_text(MOTH_PREVIEW_X, MOTH_TEXT_FIRST_ROW + 1, selected, 7, 0, MOTH_CELL_STATUS);
        }
        pico_draw_hline(MOTH_PREVIEW_X, MOTH_TEXT_FIRST_ROW + 2, MOTH_PREVIEW_COLS, PICOCALC_GLYPH_MENU_HT, 7, 0, MOTH_CELL_STATUS);

        for(int row = 0; row < g_preview_line_count && row < MOTH_PREVIEW_LINES; ++row)
        {
            pico_draw_text(MOTH_PREVIEW_X, preview_row + row, g_preview_lines[row], 7, 0, 0);
        }
    }

    pico_draw_text(0, MOTH_BOTTOM_ROW, "Enter open  Esc cancel", 0, 7, MOTH_CELL_STATUS);
}

static const char **pico_menu_items(PicoMenu menu, int *count)
{
    if(menu == PICO_MENU_VIEW)
    {
        *count = (int)(sizeof(g_view_menu_items) / sizeof(g_view_menu_items[0]));
        return g_view_menu_items;
    }
    if(menu == PICO_MENU_SELECT)
    {
        *count = (int)(sizeof(g_select_menu_items) / sizeof(g_select_menu_items[0]));
        return g_select_menu_items;
    }
    if(menu == PICO_MENU_EDIT)
    {
        *count = (int)(sizeof(g_edit_menu_items) / sizeof(g_edit_menu_items[0]));
        return g_edit_menu_items;
    }

    *count = (int)(sizeof(g_file_menu_items) / sizeof(g_file_menu_items[0]));
    return g_file_menu_items;
}

static const char *pico_toggle_label(char *buffer, size_t buffer_size, const char *label, int value)
{
    snprintf(buffer, buffer_size, "%c %s", value ? PICOCALC_GLYPH_CHECKBOX_ON : PICOCALC_GLYPH_CHECKBOX_OFF, label);
    return buffer;
}

static const char *pico_radio_label(char *buffer, size_t buffer_size, const char *label, int value)
{
    snprintf(buffer, buffer_size, "%c %s", value ? PICOCALC_GLYPH_RADIO_ON : PICOCALC_GLYPH_RADIO_OFF, label);
    return buffer;
}

static const char *pico_menu_item_label(PicoMenu menu, int index)
{
    int count;
    static char label[24];
    const char **items = pico_menu_items(menu, &count);
    if(index < 0 || index >= count) return "";

    if(menu == PICO_MENU_FILE && index == 4)
    {
        return pico_toggle_label(label, sizeof(label), "Keep Backups", g_keep_backups);
    }
    if(menu == PICO_MENU_VIEW)
    {
        if(index == 0) return pico_toggle_label(label, sizeof(label), "Wrap", g_mothpad.soft_wrap);
        if(index == 1) return pico_radio_label(label, sizeof(label), "Edit Mode", !g_mothpad.read_only);
        if(index == 2) return pico_radio_label(label, sizeof(label), "Read Mode", g_mothpad.read_only);
    }
    if(menu == PICO_MENU_EDIT)
    {
        if(index == 1 && moth_has_selection(&g_mothpad)) return "Cut";
        if(index == 2 && moth_has_selection(&g_mothpad)) return "Copy";
    }

    return items[index];
}

static const char *pico_menu_item_accel(PicoMenu menu, int index)
{
    if(menu == PICO_MENU_FILE)
    {
        if(index == 0) return "^N";
        if(index == 1) return "^O";
        if(index == 2) return "^S";
        if(index == 5) return "^Q";
    }
    else if(menu == PICO_MENU_EDIT)
    {
        if(index == 0) return "^Z";
        if(index == 1) return "^X";
        if(index == 2) return "^C";
        if(index == 3) return "^V";
    }
    else if(menu == PICO_MENU_SELECT)
    {
        if(index == 0) return "^F";
        if(index == 1) return "^A";
        if(index == 2) return "^D";
    }
    else if(menu == PICO_MENU_VIEW)
    {
        return "";
    }

    return "";
}

static int pico_menu_x(PicoMenu menu)
{
    if(menu == PICO_MENU_VIEW) return 20;
    if(menu == PICO_MENU_SELECT) return 12;
    return (menu == PICO_MENU_EDIT) ? 6 : 0;
}

static void pico_draw_menu_bar(void)
{
    pico_draw_text(0, MOTH_TOP_ROW, " File  Edit  Select  View ", 0, 7, MOTH_CELL_STATUS);
    if(g_active_menu == PICO_MENU_FILE)
    {
        pico_draw_text(0, MOTH_TOP_ROW, " File ", 7, 0, MOTH_CELL_STATUS);
    }
    else if(g_active_menu == PICO_MENU_EDIT)
    {
        pico_draw_text(6, MOTH_TOP_ROW, " Edit ", 7, 0, MOTH_CELL_STATUS);
    }
    else if(g_active_menu == PICO_MENU_SELECT)
    {
        pico_draw_text(12, MOTH_TOP_ROW, " Select ", 7, 0, MOTH_CELL_STATUS);
    }
    else
    {
        pico_draw_text(20, MOTH_TOP_ROW, " View ", 7, 0, MOTH_CELL_STATUS);
    }
}

static void pico_render_menu(void)
{
    int count;
    pico_menu_items(g_active_menu, &count);
    const int x = pico_menu_x(g_active_menu);
    const int y = 1;
    const int width = (g_active_menu == PICO_MENU_VIEW) ? 18 : ((g_active_menu == PICO_MENU_SELECT) ? 18 : ((g_active_menu == PICO_MENU_EDIT) ? 15 : 17));

    pico_render_editing();
    pico_draw_menu_bar();

    pico_put_cell(x, y, PICOCALC_GLYPH_MENU_TL, 7, 0, MOTH_CELL_STATUS);
    pico_draw_hline(x + 1, y, width - 2, PICOCALC_GLYPH_MENU_HT, 7, 0, MOTH_CELL_STATUS);
    pico_put_cell(x + width - 1, y, PICOCALC_GLYPH_MENU_TR, 7, 0, MOTH_CELL_STATUS);
    for(int i = 0; i < count; ++i)
    {
        int row = y + 1 + i;
        uint8_t fg = (i == g_menu_selected[g_active_menu]) ? 0 : 7;
        uint8_t bg = (i == g_menu_selected[g_active_menu]) ? 7 : 0;
        uint8_t flags = (i == g_menu_selected[g_active_menu]) ? MOTH_CELL_SELECTION : MOTH_CELL_STATUS;

        for(int col = 0; col < width; ++col) pico_put_cell(x + col, row, ' ', fg, bg, flags);
        pico_put_cell(x, row, PICOCALC_GLYPH_MENU_VL, 7, 0, flags);
        pico_put_cell(x + width - 1, row, PICOCALC_GLYPH_MENU_VR, 7, 0, flags);
        pico_draw_text(x + 2, row, pico_menu_item_label(g_active_menu, i), fg, bg, flags);
        const char *accel = pico_menu_item_accel(g_active_menu, i);
        if(accel[0])
        {
            int accel_x = x + width - 2 - (int)strlen(accel);
            pico_draw_text(accel_x, row, accel, fg, bg, flags);
        }
    }
    pico_put_cell(x, y + count + 1, PICOCALC_GLYPH_MENU_BL, 7, 0, MOTH_CELL_STATUS);
    pico_draw_hline(x + 1, y + count + 1, width - 2, PICOCALC_GLYPH_MENU_HB, 7, 0, MOTH_CELL_STATUS);
    pico_put_cell(x + width - 1, y + count + 1, PICOCALC_GLYPH_MENU_BR, 7, 0, MOTH_CELL_STATUS);
}

static void pico_draw_popup_box(int x, int y, int width, int height)
{
    pico_put_cell(x, y, PICOCALC_GLYPH_MENU_TL, 7, 0, MOTH_CELL_STATUS);
    pico_draw_hline(x + 1, y, width - 2, PICOCALC_GLYPH_MENU_HT, 7, 0, MOTH_CELL_STATUS);
    pico_put_cell(x + width - 1, y, PICOCALC_GLYPH_MENU_TR, 7, 0, MOTH_CELL_STATUS);

    for(int row = 1; row < height - 1; ++row)
    {
        for(int col = 0; col < width; ++col) pico_put_cell(x + col, y + row, ' ', 7, 0, MOTH_CELL_STATUS);
        pico_put_cell(x, y + row, PICOCALC_GLYPH_MENU_VL, 7, 0, MOTH_CELL_STATUS);
        pico_put_cell(x + width - 1, y + row, PICOCALC_GLYPH_MENU_VR, 7, 0, MOTH_CELL_STATUS);
    }

    pico_put_cell(x, y + height - 1, PICOCALC_GLYPH_MENU_BL, 7, 0, MOTH_CELL_STATUS);
    pico_draw_hline(x + 1, y + height - 1, width - 2, PICOCALC_GLYPH_MENU_HB, 7, 0, MOTH_CELL_STATUS);
    pico_put_cell(x + width - 1, y + height - 1, PICOCALC_GLYPH_MENU_BR, 7, 0, MOTH_CELL_STATUS);
}

static void pico_render_dirty_confirm(void)
{
    const char *items[3] = { "Cancel", "Discard", "Save First" };
    const int x = 6;
    const int y = 5;
    const int width = 22;
    const int height = 7;

    if(g_dirty_action == PICO_DIRTY_NEW) items[2] = "Save+New";
    else if(g_dirty_action == PICO_DIRTY_OPEN) items[2] = "Save+Open";
    else if(g_dirty_action == PICO_DIRTY_REBOOT) items[2] = "Save+Reboot";

    pico_render_editing();
    pico_draw_popup_box(x, y, width, height);
    pico_draw_text(x + 2, y + 1, "Unsaved changes", 7, 0, MOTH_CELL_STATUS);

    for(int i = 0; i < 3; ++i)
    {
        int row = y + 2 + i;
        uint8_t fg = (i == g_dirty_selected) ? 0 : 7;
        uint8_t bg = (i == g_dirty_selected) ? 7 : 0;
        uint8_t flags = (i == g_dirty_selected) ? MOTH_CELL_SELECTION : MOTH_CELL_STATUS;

        for(int col = 1; col < width - 1; ++col) pico_put_cell(x + col, row, ' ', fg, bg, flags);
        pico_draw_text(x + 2, row, items[i], fg, bg, flags);
    }
}

static void pico_render_recovery_confirm(void)
{
    static const char *items[] = { "Open", "Ignore" };
    const int x = 5;
    const int y = 6;
    const int width = 24;
    const int height = 6;

    pico_render_editing();
    pico_draw_popup_box(x, y, width, height);
    pico_draw_text(x + 2, y + 1, "Recovery found", 7, 0, MOTH_CELL_STATUS);

    for(int i = 0; i < 2; ++i)
    {
        int row = y + 2 + i;
        uint8_t fg = (i == g_recovery_selected) ? 0 : 7;
        uint8_t bg = (i == g_recovery_selected) ? 7 : 0;
        uint8_t flags = (i == g_recovery_selected) ? MOTH_CELL_SELECTION : MOTH_CELL_STATUS;

        for(int col = 1; col < width - 1; ++col) pico_put_cell(x + col, row, ' ', fg, bg, flags);
        pico_draw_text(x + 2, row, items[i], fg, bg, flags);
    }
}

static void pico_render_calc(void)
{
    pico_clear_cells();
    pico_fill_row(MOTH_TOP_ROW, 0, 7, MOTH_CELL_STATUS);
    pico_fill_row(MOTH_BOTTOM_ROW, 0, 7, MOTH_CELL_STATUS);
    pico_draw_text(0, MOTH_TOP_ROW, " Calc ", 0, 7, MOTH_CELL_STATUS);
    pico_draw_text(4, 8, "Calculator", 7, 0, 0);
    pico_draw_text(4, 10, "To be done", 7, 0, 0);
    pico_draw_text(0, MOTH_BOTTOM_ROW, "Esc/F5 editor", 0, 7, MOTH_CELL_STATUS);
}

static void pico_render(void)
{
    if(g_mode == PICO_MODE_MENU)
    {
        pico_render_menu();
    }
    else if(g_mode == PICO_MODE_FILE_LIST)
    {
        pico_render_file_list();
    }
    else if(g_mode == PICO_MODE_SAVE_AS)
    {
        pico_render_save_as();
    }
    else if(g_mode == PICO_MODE_FIND)
    {
        pico_render_find();
    }
    else if(g_mode == PICO_MODE_DIRTY_CONFIRM)
    {
        pico_render_dirty_confirm();
    }
    else if(g_mode == PICO_MODE_RECOVERY_CONFIRM)
    {
        pico_render_recovery_confirm();
    }
    else if(g_mode == PICO_MODE_CALC)
    {
        pico_render_calc();
    }
    else
    {
        pico_render_editing();
    }

    mothpad_lcd_flush_cells(&g_mothpad, 0);
}

static int pico_sd_inserted(void)
{
    return !gpio_get(SD_DET_PIN);
}

static int pico_init_sd(void)
{
    gpio_init(SD_DET_PIN);
    gpio_set_dir(SD_DET_PIN, GPIO_IN);
    gpio_pull_up(SD_DET_PIN);

    if(!pico_sd_inserted())
    {
        pico_set_message("No SD card");
        return 0;
    }

    blockdevice_t *sd = blockdevice_sd_create(spi0,
                                              SD_MOSI_PIN,
                                              SD_MISO_PIN,
                                              SD_SCLK_PIN,
                                              SD_CS_PIN,
                                              125000000 / 2 / 4,
                                              true);
    filesystem_t *fat = filesystem_fat_create();
    if(!sd || !fat)
    {
        pico_set_message("SD alloc failed");
        return 0;
    }

    if(fs_mount("/", fat, sd) != 0)
    {
        pico_set_message("SD mount failed");
        return 0;
    }

    pico_set_message("SD mounted");
    return 1;
}

static void pico_parent_dir(char *path)
{
    if(!path || strcmp(path, "/") == 0) return;
    char *slash = strrchr(path, '/');
    if(!slash || slash == path)
    {
        snprintf(path, 256, "/");
    }
    else
    {
        *slash = 0;
    }
}

static void pico_enter_dir(const char *name)
{
    char next[256];
    if(strcmp(name, "..") == 0)
    {
        pico_parent_dir(g_cwd);
        return;
    }

    if(moth_join_path(next, sizeof(next), g_cwd, name) == MOTH_OK)
    {
        snprintf(g_cwd, sizeof(g_cwd), "%s", next);
    }
}

static void pico_preview_clear(const char *message)
{
    memset(g_preview_lines, 0, sizeof(g_preview_lines));
    g_preview_line_count = 0;
    g_preview_visible = 0;
    if(message && message[0])
    {
        snprintf(g_preview_lines[0], sizeof(g_preview_lines[0]), "%s", message);
        g_preview_line_count = 1;
    }
}

static void pico_preview_append_char(char ch)
{
    int line;
    int len;

    if(g_preview_line_count <= 0) g_preview_line_count = 1;
    if(g_preview_line_count > MOTH_PREVIEW_LINES) return;

    line = g_preview_line_count - 1;
    len = (int)strlen(g_preview_lines[line]);
    if(ch == '\n')
    {
        if(g_preview_line_count < MOTH_PREVIEW_LINES) ++g_preview_line_count;
        return;
    }
    if(ch == '\t') ch = ' ';
    if(len >= MOTH_PREVIEW_COLS)
    {
        if(g_preview_line_count >= MOTH_PREVIEW_LINES) return;
        ++g_preview_line_count;
        line = g_preview_line_count - 1;
        len = 0;
    }

    g_preview_lines[line][len] = ch;
    g_preview_lines[line][len + 1] = 0;
}

static void pico_update_file_preview(void)
{
    char path[256];
    FILE *file;
    int saw_text = 0;

    if(g_preview_selected == g_entry_selected && strcmp(g_preview_cwd, g_cwd) == 0) return;

    g_preview_selected = g_entry_selected;
    snprintf(g_preview_cwd, sizeof(g_preview_cwd), "%s", g_cwd);

    if(g_entry_count <= 0 || g_entry_selected < 0 || g_entry_selected >= g_entry_count)
    {
        pico_preview_clear("No files");
        return;
    }

    if(g_entries[g_entry_selected].is_dir)
    {
        pico_preview_clear(NULL);
        return;
    }

    if(moth_join_path(path, sizeof(path), g_cwd, g_entries[g_entry_selected].name) != MOTH_OK)
    {
        pico_preview_clear("Bad path");
        return;
    }

    file = fopen(path, "rb");
    if(!file)
    {
        pico_preview_clear("No preview");
        return;
    }

    pico_preview_clear(NULL);
    for(int i = 0; i < 768; ++i)
    {
        int c = fgetc(file);
        if(c == EOF) break;
        if(c == 0 || (c < 32 && c != '\n' && c != '\r' && c != '\t'))
        {
            fclose(file);
            pico_preview_clear("No text preview");
            return;
        }
        if(c == '\r') continue;
        if(c >= 32 || c == '\n' || c == '\t')
        {
            pico_preview_append_char((char)c);
            if(c != '\n' && c != '\t' && c != ' ') saw_text = 1;
        }
    }

    fclose(file);
    if(!saw_text)
    {
        pico_preview_clear(NULL);
    }
    else
    {
        g_preview_visible = 1;
    }
}

static int pico_refresh_file_list(void)
{
    DIR *dir;
    struct dirent *ent;

    g_entry_count = 0;
    g_entry_selected = 0;
    g_entry_scroll = 0;

    if(strcmp(g_cwd, "/") != 0)
    {
        snprintf(g_entries[g_entry_count].name, sizeof(g_entries[g_entry_count].name), "..");
        g_entries[g_entry_count].is_dir = 1;
        ++g_entry_count;
    }

    dir = opendir(g_cwd);
    if(!dir)
    {
        pico_set_message("Open dir failed");
        return 0;
    }

    while((ent = readdir(dir)) != NULL && g_entry_count < MOTH_MAX_DIR_ENTRIES)
    {
        if(!ent->d_name[0] || strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        snprintf(g_entries[g_entry_count].name, sizeof(g_entries[g_entry_count].name), "%s", ent->d_name);
        g_entries[g_entry_count].is_dir = (ent->d_type == DT_DIR);
        ++g_entry_count;
    }

    closedir(dir);
    if(g_entry_count > 1)
    {
        qsort(g_entries, (size_t)g_entry_count, sizeof(g_entries[0]), pico_compare_dir_entries);
    }
    g_preview_selected = -1;
    return 1;
}

static int pico_load_path(const char *path)
{
    MothStatus status = moth_load_file(&g_mothpad, path);
    if(status != MOTH_OK)
    {
        pico_set_message("Open failed");
        return 0;
    }

    g_mothpad.cursor = 0;
    g_mothpad.preferred_col = 0;
    pico_clear_recovery_timer();
    g_mode = PICO_MODE_EDITING;
    pico_set_message("Opened");
    return 1;
}

static int pico_load_recovery(void)
{
    char recovered_path[256];
    MothStatus status = moth_load_file(&g_mothpad, PICO_RECOVERY_PATH);
    if(status != MOTH_OK)
    {
        pico_set_message("Recovery open failed");
        g_mode = PICO_MODE_EDITING;
        return 0;
    }

    pico_read_recovery_meta(recovered_path, sizeof(recovered_path));
    snprintf(g_mothpad.path, sizeof(g_mothpad.path), "%s", recovered_path);
    g_mothpad.dirty = 1;
    g_mothpad.cursor = g_mothpad.text_len;
    g_mothpad.preferred_col = moth_cursor_col(&g_mothpad);
    pico_delete_recovery_files();
    pico_clear_recovery_timer();
    g_mode = PICO_MODE_EDITING;
    pico_set_message("Recovered draft");
    return 1;
}

static int pico_save_path(const char *path)
{
    MothStatus status = moth_save_file_with_backup(&g_mothpad, path, g_keep_backups);
    if(status != MOTH_OK)
    {
        pico_set_message("Save failed");
        return 0;
    }

    pico_clear_recovery_timer();
    pico_delete_recovery_files();
    pico_set_message("Saved");
    return 1;
}

static void pico_begin_dirty_confirm(PicoDirtyAction action)
{
    g_dirty_action = action;
    g_dirty_selected = 0;
    g_mode = PICO_MODE_DIRTY_CONFIRM;
}

static void pico_begin_open_unchecked(void)
{
    if(!g_sd_ready)
    {
        pico_set_message("No SD mount");
        return;
    }

    if(pico_refresh_file_list())
    {
        g_mode = PICO_MODE_FILE_LIST;
    }
}

static void pico_begin_open(void)
{
    if(g_mothpad.dirty)
    {
        pico_begin_dirty_confirm(PICO_DIRTY_OPEN);
        return;
    }

    pico_begin_open_unchecked();
}

static void pico_begin_save_as_after(PicoDirtyAction action)
{
    g_save_name[0] = 0;
    g_save_name_len = 0;
    g_after_save_action = action;
    g_mode = PICO_MODE_SAVE_AS;
}

static void pico_begin_save_as(void)
{
    pico_begin_save_as_after(PICO_DIRTY_NONE);
}

static void pico_begin_find(void)
{
    g_mode = PICO_MODE_FIND;
}

static void pico_find_current_query(void)
{
    int hit;
    int len;

    if(g_find_query_len <= 0)
    {
        pico_set_message("Find empty");
        g_mode = PICO_MODE_EDITING;
        return;
    }

    if(moth_has_selection(&g_mothpad))
    {
        int start;
        int end;
        moth_selection_bounds(&g_mothpad, &start, &end);
        g_mothpad.cursor = end;
        moth_clear_selection(&g_mothpad);
    }

    hit = moth_find_next(&g_mothpad, g_find_query, 1);
    if(hit < 0)
    {
        pico_set_message("Not found");
        g_mode = PICO_MODE_EDITING;
        return;
    }

    len = (int)strlen(g_find_query);
    moth_select_range(&g_mothpad, hit, hit + len);
    pico_set_message("Found");
    g_mode = PICO_MODE_EDITING;
}

static void pico_new_file(void)
{
    if(g_mothpad.dirty)
    {
        pico_begin_dirty_confirm(PICO_DIRTY_NEW);
        return;
    }

    moth_init(&g_mothpad);
    moth_set_text(&g_mothpad, "");
    pico_clear_recovery_timer();
    pico_set_message("New file");
}

static void pico_save_current(void)
{
    if(!g_sd_ready)
    {
        pico_set_message("No SD mount");
        return;
    }

    if(g_mothpad.path[0])
    {
        pico_save_path(g_mothpad.path);
    }
    else
    {
        pico_begin_save_as();
    }
}

static void pico_reboot(void)
{
    if(g_mothpad.dirty)
    {
        pico_begin_dirty_confirm(PICO_DIRTY_REBOOT);
        return;
    }

    watchdog_reboot(0, 0, 0);
    for(;;) tight_loop_contents();
}

static void pico_perform_dirty_action(PicoDirtyAction action)
{
    g_dirty_action = PICO_DIRTY_NONE;

    switch(action)
    {
        case PICO_DIRTY_NEW:
            moth_init(&g_mothpad);
            moth_set_text(&g_mothpad, "");
            pico_clear_recovery_timer();
            g_mode = PICO_MODE_EDITING;
            pico_set_message("New file");
            break;
        case PICO_DIRTY_OPEN:
            g_mode = PICO_MODE_EDITING;
            pico_begin_open_unchecked();
            break;
        case PICO_DIRTY_REBOOT:
            watchdog_reboot(0, 0, 0);
            for(;;) tight_loop_contents();
            break;
        case PICO_DIRTY_NONE:
        default:
            g_mode = PICO_MODE_EDITING;
            break;
    }
}

static MothLine pico_current_line(void)
{
    int line = moth_cursor_line(&g_mothpad);
    if(line < 0) line = 0;
    if(line >= g_mothpad.line_count) line = g_mothpad.line_count - 1;
    return g_mothpad.lines[line];
}

static void pico_copy_line(void)
{
    MothLine line = pico_current_line();
    int len = line.end - line.start;
    if(moth_has_selection(&g_mothpad))
    {
        MothStatus status = moth_copy_selection(&g_mothpad, g_clipboard, sizeof(g_clipboard), &g_clipboard_len);
        if(status == MOTH_OK) pico_set_message("Copied");
        else pico_set_message("Copy failed");
        return;
    }

    if(line.end < g_mothpad.text_len && g_mothpad.text[line.end] == '\n') ++len;
    if(len <= 0)
    {
        pico_set_message("Line empty");
        return;
    }
    if(len >= PICO_CLIPBOARD_SIZE)
    {
        pico_set_message("Line too long");
        return;
    }

    memcpy(g_clipboard, g_mothpad.text + line.start, (size_t)len);
    g_clipboard[len] = 0;
    g_clipboard_len = len;
    pico_set_message("Copied line");
}

static void pico_cut_line(void)
{
    MothLine line = pico_current_line();
    int start = line.start;

    if(g_mothpad.read_only)
    {
        pico_set_message("Write locked");
        return;
    }

    if(moth_has_selection(&g_mothpad))
    {
        MothStatus status = moth_copy_selection(&g_mothpad, g_clipboard, sizeof(g_clipboard), &g_clipboard_len);
        if(status != MOTH_OK)
        {
            pico_set_message("Cut failed");
            return;
        }
        moth_delete_selection(&g_mothpad);
        pico_set_message("Cut");
        return;
    }

    pico_copy_line();
    if(g_clipboard_len <= 0) return;

    g_mothpad.cursor = line.end;
    if(line.end < g_mothpad.text_len && g_mothpad.text[line.end] == '\n')
    {
        ++g_mothpad.cursor;
    }
    moth_begin_undo_group(&g_mothpad);
    while(g_mothpad.cursor > start) moth_backspace(&g_mothpad);
    moth_end_undo_group(&g_mothpad);
    pico_set_message("Cut line");
}

static void pico_paste(void)
{
    if(g_mothpad.read_only)
    {
        pico_set_message("Write locked");
        return;
    }

    if(g_clipboard_len <= 0)
    {
        pico_set_message("Clipboard empty");
        return;
    }
    if(moth_insert_text(&g_mothpad, g_clipboard) == MOTH_OK) pico_set_message("Pasted");
    else pico_set_message("Paste failed");
}

static void pico_begin_menu(PicoMenu menu)
{
    g_active_menu = menu;
    g_mode = PICO_MODE_MENU;
}

static void pico_toggle_wrap(void)
{
    g_mothpad.soft_wrap = !g_mothpad.soft_wrap;
    g_mothpad.scroll_line = 0;
    g_mothpad.scroll_col = 0;
    pico_save_settings();
    pico_set_message(g_mothpad.soft_wrap ? "Wrap on" : "Wrap off");
}

static void pico_toggle_read_mode(void)
{
    if(g_mothpad.read_only)
    {
        moth_cursor_to_view_top(&g_mothpad);
        g_mothpad.read_only = 0;
        pico_set_message("Edit mode");
    }
    else
    {
        moth_clear_selection(&g_mothpad);
        g_mothpad.read_only = 1;
        pico_set_message("Read mode");
    }
}

static void pico_set_read_mode(int read_mode)
{
    if(read_mode)
    {
        moth_clear_selection(&g_mothpad);
        g_mothpad.read_only = 1;
        pico_set_message("Read mode");
    }
    else
    {
        moth_cursor_to_view_top(&g_mothpad);
        g_mothpad.read_only = 0;
        pico_set_message("Edit mode");
    }
}

static int pico_insert_tab(void)
{
    char spaces[5] = "    ";

    if(!pico_can_write()) return 1;
    if(!g_tab_insert_spaces) return moth_insert_char(&g_mothpad, '\t') == MOTH_OK;

    if(g_tab_width == 2) spaces[2] = 0;
    else spaces[4] = 0;
    return moth_insert_text(&g_mothpad, spaces) == MOTH_OK;
}

static int pico_can_write(void)
{
    if(g_mothpad.read_only)
    {
        pico_set_message("Read mode");
        return 0;
    }
    return 1;
}

static void pico_activate_file_menu_item(void)
{
    switch(g_menu_selected[PICO_MENU_FILE])
    {
        case 0:
            g_mode = PICO_MODE_EDITING;
            pico_new_file();
            break;
        case 1:
            g_mode = PICO_MODE_EDITING;
            pico_begin_open();
            break;
        case 2:
            g_mode = PICO_MODE_EDITING;
            pico_save_current();
            break;
        case 3:
            g_mode = PICO_MODE_EDITING;
            pico_begin_save_as();
            break;
        case 4:
            g_keep_backups = !g_keep_backups;
            pico_save_settings();
            g_mode = PICO_MODE_EDITING;
            pico_set_message(g_keep_backups ? "Backups on" : "Backups off");
            break;
        case 5:
            g_mode = PICO_MODE_EDITING;
            pico_reboot();
            break;
        default:
            g_mode = PICO_MODE_EDITING;
            break;
    }
}

static void pico_activate_edit_menu_item(void)
{
    switch(g_menu_selected[PICO_MENU_EDIT])
    {
        case 0:
            g_mode = PICO_MODE_EDITING;
            if(moth_undo(&g_mothpad) == MOTH_OK) pico_set_message("Undo");
            break;
        case 1:
            g_mode = PICO_MODE_EDITING;
            pico_cut_line();
            break;
        case 2:
            g_mode = PICO_MODE_EDITING;
            pico_copy_line();
            break;
        case 3:
            g_mode = PICO_MODE_EDITING;
            pico_paste();
            break;
        default:
            g_mode = PICO_MODE_EDITING;
            break;
    }
}

static void pico_activate_view_menu_item(void)
{
    switch(g_menu_selected[PICO_MENU_VIEW])
    {
        case 0:
            pico_toggle_wrap();
            g_mode = PICO_MODE_EDITING;
            break;
        case 1:
            pico_set_read_mode(0);
            g_mode = PICO_MODE_EDITING;
            break;
        case 2:
            pico_set_read_mode(1);
            g_mode = PICO_MODE_EDITING;
            break;
        default:
            g_mode = PICO_MODE_EDITING;
            break;
    }
}

static void pico_activate_menu_item(void)
{
    if(g_active_menu == PICO_MENU_SELECT)
    {
        switch(g_menu_selected[PICO_MENU_SELECT])
        {
            case 0:
                pico_begin_find();
                break;
            case 1:
                g_mode = PICO_MODE_EDITING;
                moth_select_all(&g_mothpad);
                pico_set_message("Selected all");
                break;
            case 2:
                g_mode = PICO_MODE_EDITING;
                moth_clear_selection(&g_mothpad);
                pico_set_message("Selection cleared");
                break;
            default:
                g_mode = PICO_MODE_EDITING;
                break;
        }
    }
    else if(g_active_menu == PICO_MENU_VIEW) pico_activate_view_menu_item();
    else if(g_active_menu == PICO_MENU_EDIT) pico_activate_edit_menu_item();
    else pico_activate_file_menu_item();
}

static void pico_move_cursor_with_selection(void (*move)(Mothpad *), int shift)
{
    if(shift) moth_begin_selection(&g_mothpad);
    move(&g_mothpad);
    if(shift) moth_update_selection(&g_mothpad);
    else moth_clear_selection(&g_mothpad);
}

static void pico_note_shift_arrow_released(void)
{
    if(g_shift_arrow_dir && !g_shift_arrow_releasing)
    {
        g_shift_arrow_releasing = 1;
        g_shift_arrow_release_until = make_timeout_time_ms(PICO_SHIFT_ARROW_RELEASE_MS);
    }
    else if(g_shift_arrow_releasing &&
            absolute_time_diff_us(get_absolute_time(), g_shift_arrow_release_until) <= 0)
    {
        g_shift_arrow_dir = 0;
        g_shift_arrow_releasing = 0;
    }
}

static int pico_handle_shift_arrow_joystick(void)
{
    uint8_t joy = 0xff;
    int dir = 0;

    if(g_mode != PICO_MODE_EDITING || g_mothpad.read_only || !g_shift_down)
    {
        pico_note_shift_arrow_released();
        return 0;
    }
    if(picocalc_kbd_read_joystick(&joy) != 0)
    {
        pico_note_shift_arrow_released();
        return 0;
    }

    if((joy & PICO_JOY_LEFT_BIT) == 0) dir = -1;
    else if((joy & PICO_JOY_RIGHT_BIT) == 0) dir = 1;
    else
    {
        pico_note_shift_arrow_released();
        return 0;
    }

    g_shift_arrow_releasing = 0;

    if(dir == g_shift_arrow_dir &&
       absolute_time_diff_us(get_absolute_time(), g_next_shift_arrow) > 0)
    {
        return 0;
    }

    if(dir < 0) pico_move_cursor_with_selection(moth_cursor_left, 1);
    else pico_move_cursor_with_selection(moth_cursor_right, 1);

    if(dir == g_shift_arrow_dir)
    {
        g_next_shift_arrow = make_timeout_time_ms(PICO_SHIFT_ARROW_REPEAT_MS);
    }
    else
    {
        g_shift_arrow_dir = dir;
        g_next_shift_arrow = make_timeout_time_ms(PICO_SHIFT_ARROW_INITIAL_MS);
    }

    return 1;
}

static int pico_handle_editing_key(int key, int shift)
{
    if(key < 0) return 0;
    int live_shift = g_shift_down;
    (void)shift;

    switch(key)
    {
        case PICOCALC_KEY_F1:
            pico_begin_menu(PICO_MENU_FILE);
            return 1;
        case PICOCALC_KEY_F2:
            pico_begin_menu(PICO_MENU_EDIT);
            return 1;
        case PICOCALC_KEY_F3:
            pico_begin_menu(PICO_MENU_SELECT);
            return 1;
        case PICOCALC_KEY_F4:
            pico_begin_menu(PICO_MENU_VIEW);
            return 1;
        case PICOCALC_KEY_F5:
            g_mode = PICO_MODE_CALC;
            return 1;
        case PICOCALC_CTRL_A:
            moth_select_all(&g_mothpad);
            pico_set_message("Selected all");
            return 1;
        case PICOCALC_CTRL_D:
            moth_clear_selection(&g_mothpad);
            pico_set_message("Selection cleared");
            return 1;
        case PICOCALC_CTRL_F:
            if(g_find_query_len > 0) pico_find_current_query();
            else pico_begin_find();
            return 1;
        case PICOCALC_CTRL_N:
            pico_new_file();
            return 1;
        case PICOCALC_CTRL_O:
            pico_begin_open();
            return 1;
        case PICOCALC_CTRL_S:
            pico_save_current();
            return 1;
        case PICOCALC_CTRL_Q:
            pico_reboot();
            return 1;
        case PICOCALC_CTRL_C:
            pico_copy_line();
            return 1;
        case PICOCALC_CTRL_X:
            pico_cut_line();
            return 1;
        case PICOCALC_CTRL_V:
            pico_paste();
            return 1;
        case PICOCALC_CTRL_Z:
            if(!pico_can_write()) return 1;
            return moth_undo(&g_mothpad) == MOTH_OK;
        case PICOCALC_KEY_LEFT:
            if(g_mothpad.read_only)
            {
                moth_scroll_view(&g_mothpad, -1);
                return 1;
            }
            if(live_shift) return 0;
            pico_move_cursor_with_selection(moth_cursor_left, live_shift);
            return 1;
        case PICOCALC_KEY_RIGHT:
            if(g_mothpad.read_only)
            {
                moth_scroll_view(&g_mothpad, 1);
                return 1;
            }
            if(live_shift) return 0;
            pico_move_cursor_with_selection(moth_cursor_right, live_shift);
            return 1;
        case PICOCALC_KEY_UP:
            if(g_mothpad.read_only)
            {
                moth_scroll_view(&g_mothpad, -MOTH_TEXT_ROWS);
                return 1;
            }
            pico_move_cursor_with_selection(moth_cursor_up, live_shift);
            return 1;
        case PICOCALC_KEY_DOWN:
            if(g_mothpad.read_only)
            {
                moth_scroll_view(&g_mothpad, MOTH_TEXT_ROWS);
                return 1;
            }
            pico_move_cursor_with_selection(moth_cursor_down, live_shift);
            return 1;
        case PICOCALC_KEY_PAGE_UP:
            if(g_mothpad.read_only)
            {
                moth_scroll_view(&g_mothpad, -MOTH_TEXT_ROWS);
                return 1;
            }
            pico_move_cursor_with_selection(moth_cursor_up, 1);
            return 1;
        case PICOCALC_KEY_PAGE_DOWN:
            if(g_mothpad.read_only)
            {
                moth_scroll_view(&g_mothpad, MOTH_TEXT_ROWS);
                return 1;
            }
            pico_move_cursor_with_selection(moth_cursor_down, 1);
            return 1;
        case PICOCALC_KEY_HOME:
            if(g_mothpad.read_only)
            {
                moth_scroll_view(&g_mothpad, -100000);
                return 1;
            }
            pico_move_cursor_with_selection(moth_cursor_home, live_shift);
            return 1;
        case PICOCALC_KEY_END:
            if(g_mothpad.read_only)
            {
                moth_scroll_view(&g_mothpad, 100000);
                return 1;
            }
            pico_move_cursor_with_selection(moth_cursor_end, live_shift);
            return 1;
        case PICOCALC_KEY_BACKSPACE:
            if(!pico_can_write()) return 1;
            moth_backspace(&g_mothpad);
            return 1;
        case PICOCALC_KEY_DEL:
            if(!pico_can_write()) return 1;
            moth_delete(&g_mothpad);
            return 1;
        case PICOCALC_KEY_ENTER:
        case '\r':
            if(!pico_can_write()) return 1;
            return moth_insert_char(&g_mothpad, '\n') == MOTH_OK;
        case PICOCALC_KEY_TAB:
            return pico_insert_tab();
        case PICOCALC_KEY_ESC:
            return 0;
        default:
            break;
    }

    if(key >= 32 && key <= 126)
    {
        if(!pico_can_write()) return 1;
        return moth_insert_char(&g_mothpad, (char)key) == MOTH_OK;
    }

    return 0;
}

static int pico_handle_menu_key(int key)
{
    int count;
    pico_menu_items(g_active_menu, &count);

    if(key < 0) return 0;
    switch(key)
    {
        case PICOCALC_KEY_F1:
            pico_begin_menu(PICO_MENU_FILE);
            return 1;
        case PICOCALC_KEY_F2:
            pico_begin_menu(PICO_MENU_EDIT);
            return 1;
        case PICOCALC_KEY_F3:
            pico_begin_menu(PICO_MENU_SELECT);
            return 1;
        case PICOCALC_KEY_F4:
            pico_begin_menu(PICO_MENU_VIEW);
            return 1;
        case PICOCALC_KEY_F5:
            g_mode = PICO_MODE_CALC;
            return 1;
        case PICOCALC_KEY_ESC:
        case PICOCALC_KEY_BACKSPACE:
            g_mode = PICO_MODE_EDITING;
            return 1;
        case PICOCALC_KEY_LEFT:
            pico_begin_menu((PicoMenu)((g_active_menu + PICO_MENU_COUNT - 1) % PICO_MENU_COUNT));
            return 1;
        case PICOCALC_KEY_RIGHT:
            pico_begin_menu((PicoMenu)((g_active_menu + 1) % PICO_MENU_COUNT));
            return 1;
        case PICOCALC_KEY_UP:
            if(g_menu_selected[g_active_menu] > 0) --g_menu_selected[g_active_menu];
            else g_menu_selected[g_active_menu] = count - 1;
            return 1;
        case PICOCALC_KEY_DOWN:
            if(g_menu_selected[g_active_menu] + 1 < count) ++g_menu_selected[g_active_menu];
            else g_menu_selected[g_active_menu] = 0;
            return 1;
        case PICOCALC_KEY_ENTER:
        case '\r':
            pico_activate_menu_item();
            return 1;
        case 'n':
        case 'N':
            g_active_menu = PICO_MENU_FILE;
            g_menu_selected[PICO_MENU_FILE] = 0;
            pico_activate_file_menu_item();
            return 1;
        case 'o':
        case 'O':
            g_active_menu = PICO_MENU_FILE;
            g_menu_selected[PICO_MENU_FILE] = 1;
            pico_activate_file_menu_item();
            return 1;
        case 's':
        case 'S':
            g_active_menu = PICO_MENU_FILE;
            g_menu_selected[PICO_MENU_FILE] = 2;
            pico_activate_file_menu_item();
            return 1;
        case 'u':
        case 'U':
            g_active_menu = PICO_MENU_EDIT;
            g_menu_selected[PICO_MENU_EDIT] = 0;
            pico_activate_edit_menu_item();
            return 1;
        case 'x':
        case 'X':
            g_active_menu = PICO_MENU_EDIT;
            g_menu_selected[PICO_MENU_EDIT] = 1;
            pico_activate_edit_menu_item();
            return 1;
        case 'c':
        case 'C':
            g_active_menu = PICO_MENU_EDIT;
            g_menu_selected[PICO_MENU_EDIT] = 2;
            pico_activate_edit_menu_item();
            return 1;
        case 'v':
        case 'V':
            g_active_menu = PICO_MENU_EDIT;
            g_menu_selected[PICO_MENU_EDIT] = 3;
            pico_activate_edit_menu_item();
            return 1;
        case 'f':
        case 'F':
            g_active_menu = PICO_MENU_SELECT;
            g_menu_selected[PICO_MENU_SELECT] = 0;
            pico_activate_menu_item();
            return 1;
        case 'a':
        case 'A':
            g_active_menu = PICO_MENU_SELECT;
            g_menu_selected[PICO_MENU_SELECT] = 1;
            pico_activate_menu_item();
            return 1;
        default:
            return 0;
    }
}

static int pico_handle_file_list_key(int key)
{
    char path[256];

    if(key < 0) return 0;
    switch(key)
    {
        case PICOCALC_KEY_ESC:
        case PICOCALC_KEY_BACKSPACE:
            g_after_save_action = PICO_DIRTY_NONE;
            g_mode = PICO_MODE_EDITING;
            return 1;
        case PICOCALC_KEY_UP:
            if(g_entry_selected > 0) --g_entry_selected;
            if(g_entry_selected < g_entry_scroll) g_entry_scroll = g_entry_selected;
            return 1;
        case PICOCALC_KEY_DOWN:
            if(g_entry_selected + 1 < g_entry_count) ++g_entry_selected;
            if(g_entry_selected >= g_entry_scroll + MOTH_TEXT_ROWS) g_entry_scroll = g_entry_selected - MOTH_TEXT_ROWS + 1;
            return 1;
        case PICOCALC_KEY_ENTER:
        case '\r':
            if(g_entry_count <= 0) return 1;
            if(g_entries[g_entry_selected].is_dir)
            {
                pico_enter_dir(g_entries[g_entry_selected].name);
                pico_refresh_file_list();
            }
            else if(moth_join_path(path, sizeof(path), g_cwd, g_entries[g_entry_selected].name) == MOTH_OK)
            {
                pico_load_path(path);
            }
            return 1;
        default:
            return 0;
    }
}

static int pico_handle_save_as_key(int key)
{
    char path[256];

    if(key < 0) return 0;
    switch(key)
    {
        case PICOCALC_KEY_ESC:
            g_mode = PICO_MODE_EDITING;
            return 1;
        case PICOCALC_KEY_BACKSPACE:
            if(g_save_name_len > 0) g_save_name[--g_save_name_len] = 0;
            return 1;
        case PICOCALC_KEY_ENTER:
        case '\r':
            if(g_save_name_len == 0)
            {
                pico_set_message("Name required");
                g_after_save_action = PICO_DIRTY_NONE;
                g_mode = PICO_MODE_EDITING;
                return 1;
            }
            if(moth_join_path(path, sizeof(path), g_cwd, g_save_name) != MOTH_OK)
            {
                pico_set_message("Bad filename");
                g_after_save_action = PICO_DIRTY_NONE;
                g_mode = PICO_MODE_EDITING;
                return 1;
            }
            if(pico_save_path(path))
            {
                PicoDirtyAction action = g_after_save_action;
                g_after_save_action = PICO_DIRTY_NONE;
                if(action != PICO_DIRTY_NONE)
                {
                    pico_perform_dirty_action(action);
                }
                else
                {
                    g_mode = PICO_MODE_EDITING;
                }
            }
            else
            {
                g_after_save_action = PICO_DIRTY_NONE;
                g_mode = PICO_MODE_EDITING;
            }
            return 1;
        default:
            break;
    }

    if(key >= 32 && key <= 126 && g_save_name_len < (int)sizeof(g_save_name) - 1)
    {
        g_save_name[g_save_name_len++] = (char)key;
        g_save_name[g_save_name_len] = 0;
        return 1;
    }

    return 0;
}

static int pico_handle_find_key(int key)
{
    if(key < 0) return 0;
    switch(key)
    {
        case PICOCALC_KEY_ESC:
            g_mode = PICO_MODE_EDITING;
            return 1;
        case PICOCALC_KEY_BACKSPACE:
            if(g_find_query_len > 0) g_find_query[--g_find_query_len] = 0;
            return 1;
        case PICOCALC_KEY_ENTER:
        case '\r':
            pico_find_current_query();
            return 1;
        default:
            break;
    }

    if(key >= 32 && key <= 126 && g_find_query_len < (int)sizeof(g_find_query) - 1)
    {
        g_find_query[g_find_query_len++] = (char)key;
        g_find_query[g_find_query_len] = 0;
        return 1;
    }

    return 0;
}

static int pico_handle_dirty_confirm_key(int key)
{
    PicoDirtyAction action;

    if(key < 0) return 0;
    switch(key)
    {
        case PICOCALC_KEY_ESC:
        case PICOCALC_KEY_BACKSPACE:
        case 'c':
        case 'C':
            g_dirty_action = PICO_DIRTY_NONE;
            g_mode = PICO_MODE_EDITING;
            return 1;
        case PICOCALC_KEY_UP:
            if(g_dirty_selected > 0) --g_dirty_selected;
            else g_dirty_selected = 2;
            return 1;
        case PICOCALC_KEY_DOWN:
            if(g_dirty_selected < 2) ++g_dirty_selected;
            else g_dirty_selected = 0;
            return 1;
        case 'd':
        case 'D':
        case 'q':
        case 'Q':
            g_dirty_selected = 1;
            break;
        case 's':
        case 'S':
            g_dirty_selected = 2;
            break;
        case PICOCALC_KEY_ENTER:
        case '\r':
            break;
        default:
            return 0;
    }

    action = g_dirty_action;
    if(g_dirty_selected == 0)
    {
        g_dirty_action = PICO_DIRTY_NONE;
        g_mode = PICO_MODE_EDITING;
    }
    else if(g_dirty_selected == 1)
    {
        pico_perform_dirty_action(action);
    }
    else if(g_dirty_selected == 2)
    {
        if(!g_sd_ready)
        {
            pico_set_message("No SD mount");
            g_dirty_action = PICO_DIRTY_NONE;
            g_mode = PICO_MODE_EDITING;
        }
        else if(g_mothpad.path[0])
        {
            if(pico_save_path(g_mothpad.path)) pico_perform_dirty_action(action);
            else
            {
                g_dirty_action = PICO_DIRTY_NONE;
                g_mode = PICO_MODE_EDITING;
            }
        }
        else
        {
            g_dirty_action = PICO_DIRTY_NONE;
            pico_begin_save_as_after(action);
        }
    }

    return 1;
}

static int pico_handle_recovery_confirm_key(int key)
{
    if(key < 0) return 0;
    switch(key)
    {
        case PICOCALC_KEY_ESC:
        case PICOCALC_KEY_BACKSPACE:
            g_mode = PICO_MODE_EDITING;
            return 1;
        case PICOCALC_KEY_UP:
        case PICOCALC_KEY_DOWN:
            g_recovery_selected = 1 - g_recovery_selected;
            return 1;
        case 'o':
        case 'O':
            g_recovery_selected = 0;
            break;
        case 'i':
        case 'I':
            g_recovery_selected = 1;
            break;
        case PICOCALC_KEY_ENTER:
        case '\r':
            break;
        default:
            return 0;
    }

    if(g_recovery_selected == 0)
    {
        pico_load_recovery();
    }
    else
    {
        pico_delete_recovery_files();
        g_mode = PICO_MODE_EDITING;
        pico_set_message("Recovery ignored");
    }
    return 1;
}

static int pico_handle_calc_key(int key)
{
    if(key < 0) return 0;
    if(key == PICOCALC_KEY_ESC || key == PICOCALC_KEY_F5 || key == PICOCALC_KEY_BACKSPACE)
    {
        g_mode = PICO_MODE_EDITING;
        return 1;
    }
    return 1;
}

static int pico_handle_key(int key, int shift)
{
    if(g_mode == PICO_MODE_MENU) return pico_handle_menu_key(key);
    if(g_mode == PICO_MODE_FILE_LIST) return pico_handle_file_list_key(key);
    if(g_mode == PICO_MODE_SAVE_AS) return pico_handle_save_as_key(key);
    if(g_mode == PICO_MODE_FIND) return pico_handle_find_key(key);
    if(g_mode == PICO_MODE_DIRTY_CONFIRM) return pico_handle_dirty_confirm_key(key);
    if(g_mode == PICO_MODE_RECOVERY_CONFIRM) return pico_handle_recovery_confirm_key(key);
    if(g_mode == PICO_MODE_CALC) return pico_handle_calc_key(key);
    return pico_handle_editing_key(key, shift);
}

int main(void)
{
    stdio_init_all();
    sleep_ms(250);

    picocalc_lcd_init();
    picocalc_lcd_draw_mono_bitmap(g_mothpad_splash_bits,
                                  MOTHPAD_SPLASH_WIDTH,
                                  MOTHPAD_SPLASH_HEIGHT,
                                  MOTHPAD_SPLASH_STRIDE,
                                  PICOCALC_COLOR_WHITE,
                                  PICOCALC_COLOR_BLACK);
    sleep_ms(500);
    picocalc_lcd_clear();
    picocalc_kbd_init();

    moth_init(&g_mothpad);
    moth_set_text(&g_mothpad, "");

    memset(g_lcd_cells, 0, sizeof(g_lcd_cells));
    g_lcd_cells_valid = 0;
    g_next_cursor_blink = make_timeout_time_ms(450);
    pico_update_battery();

    g_sd_ready = pico_init_sd();
    pico_load_settings();
    g_mothpad.tab_width = g_tab_width;
    if(pico_recovery_file_exists())
    {
        g_recovery_selected = 0;
        g_mode = PICO_MODE_RECOVERY_CONFIRM;
    }
    pico_render();

    for(;;)
    {
        int key = -1;
        int shift = 0;
        (void)picocalc_kbd_read_event(&key, &shift);
        pico_update_shift_state();
        if(pico_handle_key(key, shift))
        {
            pico_schedule_recovery_write();
            g_cursor_visible = 1;
            g_next_cursor_blink = make_timeout_time_ms(450);
            pico_render();
        }
        else if(pico_handle_shift_arrow_joystick())
        {
            pico_schedule_recovery_write();
            g_cursor_visible = 1;
            g_next_cursor_blink = make_timeout_time_ms(450);
            pico_render();
        }
        else if(pico_recovery_write_ready())
        {
            pico_write_recovery_copy();
            pico_render();
        }
        else if(g_mode == PICO_MODE_EDITING &&
                !g_mothpad.read_only &&
                absolute_time_diff_us(get_absolute_time(), g_next_cursor_blink) <= 0)
        {
            g_cursor_visible = !g_cursor_visible;
            g_next_cursor_blink = make_timeout_time_ms(450);
            pico_render();
        }
        else if(g_mode == PICO_MODE_EDITING && g_message[0] && !pico_message_active())
        {
            g_message[0] = 0;
            pico_render();
        }
        else if(absolute_time_diff_us(get_absolute_time(), g_next_battery_update) <= 0)
        {
            pico_update_battery();
            pico_render();
        }
        sleep_ms(8);
    }
}
