#include "mothpad.h"

#include "blockdevice/sd.h"
#include "filesystem/fat.h"
#include "filesystem/vfs.h"
#include "mothpad_picocalc_platform.h"
#include "pico/stdlib.h"

#include <errno.h>
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
#define PICOCALC_KEY_ESC       0xB1
#define PICOCALC_KEY_LEFT      0xB4
#define PICOCALC_KEY_UP        0xB5
#define PICOCALC_KEY_DOWN      0xB6
#define PICOCALC_KEY_RIGHT     0xB7
#define PICOCALC_KEY_HOME      0xD2
#define PICOCALC_KEY_DEL       0xD4
#define PICOCALC_KEY_END       0xD5

#define PICOCALC_CTRL_O        15
#define PICOCALC_CTRL_S        19

#define SD_SCLK_PIN            18
#define SD_MOSI_PIN            19
#define SD_MISO_PIN            16
#define SD_CS_PIN              17
#define SD_DET_PIN             22

#define MOTH_MAX_DIR_ENTRIES   96
#define MOTH_MESSAGE_MS        1800

typedef enum {
    PICO_MODE_EDITING = 0,
    PICO_MODE_FILE_LIST,
    PICO_MODE_SAVE_AS,
    PICO_MODE_ERROR_MESSAGE,
} PicoMode;

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

static char g_save_name[256];
static int g_save_name_len;

static int mothpad_pico_color(uint8_t color)
{
    return color ? PICOCALC_COLOR_WHITE : PICOCALC_COLOR_BLACK;
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

static void pico_draw_bottom_message(void)
{
    if(!pico_message_active()) return;
    pico_fill_row(MOTH_BOTTOM_ROW, 0, 7, MOTH_CELL_STATUS);
    pico_draw_text(0, MOTH_BOTTOM_ROW, g_message, 0, 7, MOTH_CELL_STATUS);
}

static void pico_render_editing(void)
{
    moth_render(&g_mothpad);
    pico_draw_bottom_message();
}

static void pico_render_save_as(void)
{
    pico_render_editing();
    pico_fill_row(MOTH_BOTTOM_ROW, 0, 7, MOTH_CELL_STATUS);
    pico_draw_text(0, MOTH_BOTTOM_ROW, "Save as: ", 0, 7, MOTH_CELL_STATUS);
    pico_draw_text(9, MOTH_BOTTOM_ROW, g_save_name, 0, 7, MOTH_CELL_STATUS);
}

static void pico_render_file_list(void)
{
    char top[96];
    pico_clear_cells();
    pico_fill_row(MOTH_TOP_ROW, 0, 7, MOTH_CELL_STATUS);
    pico_fill_row(MOTH_BOTTOM_ROW, 0, 7, MOTH_CELL_STATUS);

    snprintf(top, sizeof(top), "Open: %s", g_cwd);
    pico_draw_text(0, MOTH_TOP_ROW, top, 0, 7, MOTH_CELL_STATUS);

    for(int row = 0; row < MOTH_TEXT_ROWS; ++row)
    {
        int entry_index = g_entry_scroll + row;
        if(entry_index >= g_entry_count) break;

        uint8_t fg = 7;
        uint8_t bg = 0;
        uint8_t flags = 0;
        char line[300];

        if(entry_index == g_entry_selected)
        {
            fg = 0;
            bg = 7;
            flags = MOTH_CELL_SELECTION;
            pico_fill_row(MOTH_TEXT_FIRST_ROW + row, fg, bg, flags);
        }

        snprintf(line, sizeof(line), "%c %s", g_entries[entry_index].is_dir ? '/' : ' ', g_entries[entry_index].name);
        pico_draw_text(0, MOTH_TEXT_FIRST_ROW + row, line, fg, bg, flags);
    }

    pico_draw_text(0, MOTH_BOTTOM_ROW, "Enter open  Esc cancel", 0, 7, MOTH_CELL_STATUS);
}

static void pico_render(void)
{
    if(g_mode == PICO_MODE_FILE_LIST)
    {
        pico_render_file_list();
    }
    else if(g_mode == PICO_MODE_SAVE_AS)
    {
        pico_render_save_as();
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
    g_mode = PICO_MODE_EDITING;
    pico_set_message("Opened");
    return 1;
}

static int pico_save_path(const char *path)
{
    MothStatus status = moth_save_file(&g_mothpad, path);
    if(status != MOTH_OK)
    {
        pico_set_message("Save failed");
        return 0;
    }

    pico_set_message("Saved");
    return 1;
}

static void pico_begin_open(void)
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

static void pico_begin_save_as(void)
{
    g_save_name[0] = 0;
    g_save_name_len = 0;
    g_mode = PICO_MODE_SAVE_AS;
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

static int pico_handle_editing_key(int key)
{
    if(key < 0) return 0;

    switch(key)
    {
        case PICOCALC_CTRL_O:
            pico_begin_open();
            return 1;
        case PICOCALC_CTRL_S:
            pico_save_current();
            return 1;
        case PICOCALC_KEY_LEFT:
            moth_cursor_left(&g_mothpad);
            return 1;
        case PICOCALC_KEY_RIGHT:
            moth_cursor_right(&g_mothpad);
            return 1;
        case PICOCALC_KEY_UP:
            moth_cursor_up(&g_mothpad);
            return 1;
        case PICOCALC_KEY_DOWN:
            moth_cursor_down(&g_mothpad);
            return 1;
        case PICOCALC_KEY_HOME:
            moth_cursor_home(&g_mothpad);
            return 1;
        case PICOCALC_KEY_END:
            moth_cursor_end(&g_mothpad);
            return 1;
        case PICOCALC_KEY_BACKSPACE:
            moth_backspace(&g_mothpad);
            return 1;
        case PICOCALC_KEY_DEL:
            moth_delete(&g_mothpad);
            return 1;
        case PICOCALC_KEY_ENTER:
        case '\r':
            return moth_insert_char(&g_mothpad, '\n') == MOTH_OK;
        case PICOCALC_KEY_TAB:
            return moth_insert_char(&g_mothpad, '\t') == MOTH_OK;
        case PICOCALC_KEY_ESC:
            return 0;
        default:
            break;
    }

    if(key >= 32 && key <= 126)
    {
        return moth_insert_char(&g_mothpad, (char)key) == MOTH_OK;
    }

    return 0;
}

static int pico_handle_file_list_key(int key)
{
    char path[256];

    if(key < 0) return 0;
    switch(key)
    {
        case PICOCALC_KEY_ESC:
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
                g_mode = PICO_MODE_EDITING;
                return 1;
            }
            if(moth_join_path(path, sizeof(path), g_cwd, g_save_name) != MOTH_OK)
            {
                pico_set_message("Bad filename");
                g_mode = PICO_MODE_EDITING;
                return 1;
            }
            pico_save_path(path);
            g_mode = PICO_MODE_EDITING;
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

static int pico_handle_key(int key)
{
    if(g_mode == PICO_MODE_FILE_LIST) return pico_handle_file_list_key(key);
    if(g_mode == PICO_MODE_SAVE_AS) return pico_handle_save_as_key(key);
    return pico_handle_editing_key(key);
}

int main(void)
{
    stdio_init_all();
    sleep_ms(250);

    picocalc_lcd_init();
    picocalc_lcd_clear();
    picocalc_kbd_init();

    moth_init(&g_mothpad);
    moth_set_text(&g_mothpad, "");

    memset(g_lcd_cells, 0, sizeof(g_lcd_cells));
    g_lcd_cells_valid = 0;
    g_next_cursor_blink = make_timeout_time_ms(450);

    g_sd_ready = pico_init_sd();
    pico_render();

    for(;;)
    {
        int key = picocalc_kbd_read();
        if(pico_handle_key(key))
        {
            g_cursor_visible = 1;
            g_next_cursor_blink = make_timeout_time_ms(450);
            pico_render();
        }
        else if(g_mode == PICO_MODE_EDITING &&
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
        sleep_ms(8);
    }
}
