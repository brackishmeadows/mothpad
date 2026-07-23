#include "mothpad.h"

#include "blockdevice/sd.h"
#include "filesystem/fat.h"
#include "filesystem/vfs.h"
#include "hardware/watchdog.h"
#ifdef MOTHPAD_EXPERIMENTAL
#include "mothpad_file_browser_app.h"
#endif
#if defined(MOTHPAD_EXPERIMENTAL) || defined(PORTMANTEAU_DEMO)
#include "pico/multicore.h"
#endif
#include "mothpad_picocalc_platform.h"
#include "mothpad_splash_bitmap.h"
#ifdef PORTMANTEAU_DEMO
#include "mothpad_duolith_suite.h"
#include "portmanteau_runtime.h"
#endif
#include "pico/stdlib.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/dirent.h>
#include <sys/stat.h>

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
#define MOTH_FILE_LIST_COLS    20
#define MOTH_PREVIEW_X         (MOTH_FILE_LIST_COLS + 1)
#define MOTH_PREVIEW_COLS      (MOTH_COLS - MOTH_PREVIEW_X)
#define MOTH_PREVIEW_LINES     (MOTH_TEXT_ROWS - 3)
#define MOTH_BATTERY_RESERVED_COLS 8
#define PICO_CLIPBOARD_SIZE    4096
#define PICO_CALC_EXPR_SIZE    80
#define PICO_CALC_RESULT_SIZE  80
#define PICO_CALC_HISTORY      128
#define PICO_CALC_HISTORY_FIRST_ROW 15
#define PICO_ARROW_INITIAL_MS 280
#define PICO_ARROW_REPEAT_MS 90
#define PICO_SHIFT_ARROW_INITIAL_MS 180
#define PICO_SHIFT_ARROW_REPEAT_MS 22
#define PICO_SHIFT_ARROW_RELEASE_MS 90
#define PICO_SHIFT_DEBOUNCE_DOWN_SAMPLES 3
#define PICO_SHIFT_DEBOUNCE_UP_SAMPLES 1
#define PICO_STATUS_FG         7
#define PICO_STATUS_BG         0
#define PICO_SCREENSAVER_IDLE_MS 60000
#define PICO_SCREENSAVER_STEP_MS 90
#define PICO_JOY_RIGHT_BIT     0x01
#define PICO_JOY_LEFT_BIT      0x08
#define PICO_RECOVERY_PATH     "/.mothpad-recovery.txt"
#define PICO_RECOVERY_META_PATH "/.mothpad-recovery.meta"
#define PICO_FOLDARIUM_HANDOFF_PATH "/.foldarium-open"
#define PICO_SETTINGS_PATH     "/.mothpad-settings.txt"
#define PICO_RECENT_PATH       "/.mothpad-recent.txt"
#define PICO_RECOVERY_DELAY_MS 8000
#define PICO_RECOVERY_RETRY_MS 30000
#define PICO_MAX_RECENT_FILES  5

typedef enum {
    PICO_MODE_EDITING = 0,
    PICO_MODE_MENU,
    PICO_MODE_FILE_LIST,
    PICO_MODE_FILE_ACTION_MENU,
    PICO_MODE_SAVE_AS,
    PICO_MODE_FIND,
    PICO_MODE_DIRTY_CONFIRM,
    PICO_MODE_RECOVERY_CONFIRM,
    PICO_MODE_CALC,
#ifdef PORTMANTEAU_DEMO
    PICO_MODE_PORTMANTEAU_FOLDARIUM,
#endif
    PICO_MODE_ERROR_MESSAGE,
} PicoMode;

typedef enum {
    PICO_DIRTY_NONE = 0,
    PICO_DIRTY_NEW,
    PICO_DIRTY_OPEN,
    PICO_DIRTY_LOAD_PENDING,
    PICO_DIRTY_REBOOT,
} PicoDirtyAction;

#ifdef PORTMANTEAU_DEMO
static volatile DuolithMailbox g_duolith_mailbox;
static MothpadDuolithSuite g_duolith_suite;
static PortmanteauApp *g_duolith_foreground_peer;
static char g_portmanteau_pending_path[DUOLITH_PATH_SIZE];
static void pico_duolith_show_mothpad(void *host);
static void pico_duolith_show_foldarium(void *host);
static void pico_duolith_show_calculator(void *host);
static int pico_duolith_core1_calculator_active(void);
#endif

typedef enum {
    PICO_MENU_FILE = 0,
    PICO_MENU_EDIT,
    PICO_MENU_SELECT,
    PICO_MENU_VIEW,
    PICO_MENU_COUNT,
} PicoMenu;

typedef enum {
    PICO_FILE_ACTION_OPEN = 0,
    PICO_FILE_ACTION_NEW_FOLDER,
    PICO_FILE_ACTION_COPY,
    PICO_FILE_ACTION_CUT,
    PICO_FILE_ACTION_PASTE,
    PICO_FILE_ACTION_RENAME,
    PICO_FILE_ACTION_DELETE,
    PICO_FILE_ACTION_RECENT_AT_ROOT,
    PICO_FILE_ACTION_CANCEL,
    PICO_FILE_ACTION_COUNT
} PicoFileAction;

typedef enum {
    PICO_FILE_CLIP_NONE = 0,
    PICO_FILE_CLIP_COPY,
    PICO_FILE_CLIP_CUT
} PicoFileClipMode;

typedef struct {
    char ch;
    uint8_t fg;
    uint8_t bg;
    uint8_t flags;
} PicoCell;

typedef struct {
    char name[256];
    char path[256];
    int is_dir;
    int is_recent;
} PicoDirEntry;

static Mothpad g_mothpad;
static PicoCell g_lcd_cells[MOTH_COLS * MOTH_ROWS];
static int g_lcd_cells_valid;
static int g_cursor_visible = 1;
static absolute_time_t g_next_cursor_blink;
static absolute_time_t g_next_battery_update;
static absolute_time_t g_screensaver_due;
static absolute_time_t g_screensaver_next_step;
static int g_screensaver_active;
static int g_screensaver_step;
static int g_battery_percent = -1;
static int g_battery_charging;
static int g_shift_down;
static int g_shift_down_samples;
static int g_shift_up_samples;
static int g_arrow_hold_dir;
static int g_arrow_hold_shift;
static absolute_time_t g_next_arrow_hold;
static int g_shift_arrow_dir;
static int g_shift_arrow_releasing;
static absolute_time_t g_next_shift_arrow;
static absolute_time_t g_shift_arrow_release_until;
static int g_recovery_pending;
static absolute_time_t g_recovery_due;

static PicoMode g_mode = PICO_MODE_EDITING;
static PicoMode g_return_mode = PICO_MODE_EDITING;
static PicoMode g_file_action_return_mode = PICO_MODE_FILE_LIST;
static int g_sd_ready;
static char g_cwd[256] = "/";
static char g_message[80];
static absolute_time_t g_message_until;

static PicoDirEntry g_entries[MOTH_MAX_DIR_ENTRIES];
static int g_entry_count;
static int g_entry_selected;
static int g_entry_scroll;
static int g_file_action_selected;
static PicoFileClipMode g_file_clip_mode;
static char g_file_clip_path[256];
static char g_file_clip_name[256];
static int g_file_clip_is_dir;
static int g_show_recent_at_root = 1;
static char g_recent_paths[PICO_MAX_RECENT_FILES][256];
static int g_recent_count;
static int g_preview_selected = -1;
static char g_preview_cwd[256];
static char g_preview_lines[MOTH_PREVIEW_LINES][MOTH_PREVIEW_COLS + 1];
static int g_preview_line_count;
static int g_preview_visible;
static int g_preview_source_indent;
static int g_preview_source_line_start;

static char g_save_name[256];
static int g_save_name_len;
static int g_save_name_cursor;
static char g_find_query[80];
static int g_find_query_len;
static int g_find_query_cursor;
static PicoDirtyAction g_after_save_action = PICO_DIRTY_NONE;
static PicoDirtyAction g_dirty_action = PICO_DIRTY_NONE;
static int g_dirty_selected;
static int g_recovery_selected;
static char g_clipboard[PICO_CLIPBOARD_SIZE];
static int g_clipboard_len;
static int g_keep_backups = 1;
static int g_settings_version = 3;
static int g_tab_insert_spaces = 0;
static int g_tab_width = 2;
static char g_calc_expr[PICO_CALC_EXPR_SIZE];
static char g_calc_stored_expr[PICO_CALC_EXPR_SIZE];
static int g_calc_stored_expr_active;
static int g_calc_expr_len;
static int g_calc_cursor;
static char g_calc_result[PICO_CALC_RESULT_SIZE] = "?";
static char g_calc_status[40] = "Enter stores";
static char g_calc_history_expr[PICO_CALC_HISTORY][PICO_CALC_EXPR_SIZE];
static char g_calc_history_result[PICO_CALC_HISTORY][PICO_CALC_RESULT_SIZE];
static int g_calc_history_count;
static int g_calc_history_recall = -1;
static int g_calc_history_top = -1;

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
static void mothpad_lcd_flush_cells(const Mothpad *m, int force_full);
static void pico_clear_cells(void);
static void pico_render_screensaver(void);
static void pico_update_file_preview(void);
static void pico_write_recovery_meta(void);
static void pico_save_settings(void);
static int pico_can_write(void);
static void pico_trim_line(char *line);
static const char *pico_toggle_label(char *buffer, size_t buffer_size, const char *label, int value);
static const char *pico_file_entry_text(int index);
static int pico_path_exists(const char *path);

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

    if(ea->is_recent != eb->is_recent) return eb->is_recent - ea->is_recent;
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

static void pico_reset_screensaver_timer(void)
{
    g_screensaver_due = make_timeout_time_ms(PICO_SCREENSAVER_IDLE_MS);
}

static void pico_wake_screensaver(void)
{
    if(!g_screensaver_active) return;
    g_screensaver_active = 0;
    pico_clear_cells();
    picocalc_lcd_set_colors(PICOCALC_COLOR_WHITE, PICOCALC_COLOR_BLACK);
    mothpad_lcd_flush_cells(&g_mothpad, 1);
    g_cursor_visible = 1;
    g_next_cursor_blink = make_timeout_time_ms(450);
    pico_reset_screensaver_timer();
}

static void pico_start_screensaver(void)
{
    g_screensaver_active = 1;
    g_screensaver_step = 0;
    g_screensaver_next_step = make_timeout_time_ms(PICO_SCREENSAVER_STEP_MS);
    g_cursor_visible = 0;
    pico_render_screensaver();
}

static void pico_step_screensaver(void)
{
    ++g_screensaver_step;
    g_screensaver_next_step = make_timeout_time_ms(PICO_SCREENSAVER_STEP_MS);
    pico_render_screensaver();
}

static void pico_update_shift_state(void)
{
    g_shift_down = picocalc_kbd_shift_down();
    g_shift_down_samples = g_shift_down ? 1 : 0;
    g_shift_up_samples = g_shift_down ? 0 : 1;
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

static int pico_read_foldarium_handoff(char *path, size_t path_size)
{
    FILE *file;

    if(!g_sd_ready || !path || path_size == 0) return 0;
    path[0] = 0;
    file = fopen(PICO_FOLDARIUM_HANDOFF_PATH, "rb");
    if(!file) return 0;
    if(!fgets(path, (int)path_size, file))
    {
        fclose(file);
        return 0;
    }
    fclose(file);
    pico_trim_line(path);
    return path[0] != 0;
}

static void pico_delete_foldarium_handoff(void)
{
    if(!g_sd_ready) return;
    remove(PICO_FOLDARIUM_HANDOFF_PATH);
    remove(PICO_FOLDARIUM_HANDOFF_PATH ".tmp");
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
        else if(strcmp(key, "show_recent_at_root") == 0) g_show_recent_at_root = atoi(value) ? 1 : 0;
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
    g_settings_version = 3;
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
    fprintf(file, "show_recent_at_root=%d\n", g_show_recent_at_root ? 1 : 0);
    fclose(file);

    remove(PICO_SETTINGS_PATH);
    rename(PICO_SETTINGS_PATH ".tmp", PICO_SETTINGS_PATH);
}

static int pico_recent_index_of(const char *path)
{
    if(!path || !path[0]) return -1;
    for(int i = 0; i < g_recent_count; ++i)
    {
        if(strcmp(g_recent_paths[i], path) == 0) return i;
    }
    return -1;
}

static const char *pico_path_basename(const char *path)
{
    const char *name;

    if(!path || !path[0]) return "";
    name = strrchr(path, '/');
    return name ? name + 1 : path;
}

static void pico_save_recent_files(void)
{
    FILE *file;

    if(!g_sd_ready) return;
    file = fopen(PICO_RECENT_PATH ".tmp", "wb");
    if(!file) return;
    for(int i = 0; i < g_recent_count; ++i)
    {
        if(g_recent_paths[i][0]) fprintf(file, "%s\n", g_recent_paths[i]);
    }
    fclose(file);
    remove(PICO_RECENT_PATH);
    rename(PICO_RECENT_PATH ".tmp", PICO_RECENT_PATH);
}

static void pico_load_recent_files(void)
{
    char line[256];
    FILE *file;
    int pruned = 0;

    g_recent_count = 0;
    if(!g_sd_ready) return;
    file = fopen(PICO_RECENT_PATH, "rb");
    if(!file) return;
    while(g_recent_count < PICO_MAX_RECENT_FILES && fgets(line, sizeof(line), file))
    {
        pico_trim_line(line);
        if(line[0] != '/' || pico_recent_index_of(line) >= 0) continue;
        snprintf(g_recent_paths[g_recent_count], sizeof(g_recent_paths[g_recent_count]), "%s", line);
        ++g_recent_count;
    }
    fclose(file);
    for(int i = 0; i < g_recent_count;)
    {
        if(!pico_path_exists(g_recent_paths[i]))
        {
            for(int j = i; j + 1 < g_recent_count; ++j)
            {
                snprintf(g_recent_paths[j], sizeof(g_recent_paths[j]), "%s", g_recent_paths[j + 1]);
            }
            --g_recent_count;
            g_recent_paths[g_recent_count][0] = 0;
            pruned = 1;
        }
        else
        {
            ++i;
        }
    }
    if(pruned) pico_save_recent_files();
}

static void pico_note_recent_file(const char *path)
{
    int existing;

    if(!g_sd_ready || !path || path[0] != '/') return;
    if(strcmp(path, PICO_RECOVERY_PATH) == 0) return;

    existing = pico_recent_index_of(path);
    if(existing == 0) return;
    if(existing > 0)
    {
        for(int i = existing; i > 0; --i)
        {
            snprintf(g_recent_paths[i], sizeof(g_recent_paths[i]), "%s", g_recent_paths[i - 1]);
        }
    }
    else
    {
        if(g_recent_count < PICO_MAX_RECENT_FILES) ++g_recent_count;
        for(int i = g_recent_count - 1; i > 0; --i)
        {
            snprintf(g_recent_paths[i], sizeof(g_recent_paths[i]), "%s", g_recent_paths[i - 1]);
        }
    }

    snprintf(g_recent_paths[0], sizeof(g_recent_paths[0]), "%s", path);
    pico_save_recent_files();
}

static void pico_forget_recent_file(const char *path)
{
    int index = pico_recent_index_of(path);
    if(index < 0) return;

    for(int i = index; i + 1 < g_recent_count; ++i)
    {
        snprintf(g_recent_paths[i], sizeof(g_recent_paths[i]), "%s", g_recent_paths[i + 1]);
    }
    if(g_recent_count > 0)
    {
        --g_recent_count;
        g_recent_paths[g_recent_count][0] = 0;
    }
    pico_save_recent_files();
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
    out.flags = cell ? cell->flags : 0;
    return out;
}

static int mothpad_lcd_cells_equal(PicoCell a, PicoCell b)
{
    return a.ch == b.ch && a.fg == b.fg && a.bg == b.bg && a.flags == b.flags;
}

static void mothpad_lcd_draw_cell(int x, int y, PicoCell cell)
{
    picocalc_lcd_set_cursor((short)(x * PICOCALC_FONT_WIDTH), (short)(y * PICOCALC_FONT_HEIGHT));
    picocalc_lcd_set_colors(mothpad_pico_color(cell.fg), mothpad_pico_color(cell.bg));
    picocalc_lcd_put_char_flags(cell.ch, (cell.flags & MOTH_CELL_BOLD) ? 1 : 0, 1);
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

static void pico_draw_text_clipped(int x, int y, const char *text, int max_cols, uint8_t fg, uint8_t bg, uint8_t flags)
{
    if(max_cols <= 0) return;
    for(int i = 0; text && text[i] && i < max_cols && (x + i) < MOTH_COLS; ++i)
    {
        pico_put_cell(x + i, y, text[i], fg, bg, flags);
    }
}

static int pico_top_text_cols_before_battery(void)
{
    int cols = MOTH_COLS - MOTH_BATTERY_RESERVED_COLS;
    return cols > 0 ? cols : 0;
}

static void pico_text_field_insert(char *text, int *len, int *cursor, int capacity, char ch)
{
    if(!text || !len || !cursor || capacity <= 1) return;
    if(*len < 0) *len = (int)strlen(text);
    if(*cursor < 0) *cursor = 0;
    if(*cursor > *len) *cursor = *len;
    if(*len >= capacity - 1) return;

    memmove(text + *cursor + 1, text + *cursor, (size_t)(*len - *cursor + 1));
    text[*cursor] = ch;
    ++(*cursor);
    ++(*len);
}

static void pico_text_field_backspace(char *text, int *len, int *cursor)
{
    if(!text || !len || !cursor || *cursor <= 0) return;
    if(*len < 0) *len = (int)strlen(text);
    if(*cursor > *len) *cursor = *len;
    memmove(text + *cursor - 1, text + *cursor, (size_t)(*len - *cursor + 1));
    --(*cursor);
    --(*len);
}

static void pico_text_field_delete(char *text, int *len, int *cursor)
{
    if(!text || !len || !cursor) return;
    if(*len < 0) *len = (int)strlen(text);
    if(*cursor < 0) *cursor = 0;
    if(*cursor >= *len) return;
    memmove(text + *cursor, text + *cursor + 1, (size_t)(*len - *cursor));
    --(*len);
}

static int pico_text_field_start_for_cursor(int len, int cursor, int field_width)
{
    int start = 0;
    if(field_width <= 0) return 0;
    if(cursor < 0) cursor = 0;
    if(cursor > len) cursor = len;
    if(cursor >= field_width) start = cursor - field_width + 1;
    if(start < 0) start = 0;
    return start;
}

static void pico_draw_text_field(int x, int y, int width, const char *text, int len, int cursor)
{
    int text_start = pico_text_field_start_for_cursor(len, cursor, width);
    int cursor_x;

    if(width <= 0) return;
    for(int col = 0; col < width; ++col) pico_put_cell(x + col, y, ' ', 0, 7, MOTH_CELL_SELECTION);
    for(int i = 0; i < width && text && text[text_start + i]; ++i)
    {
        pico_put_cell(x + i, y, text[text_start + i], 0, 7, MOTH_CELL_SELECTION);
    }
    cursor_x = x + cursor - text_start;
    if(cursor_x < x) cursor_x = x;
    if(cursor_x >= x + width) cursor_x = x + width - 1;
    pico_put_cell(cursor_x, y, '_', 0, 7, MOTH_CELL_SELECTION);
}

static void pico_draw_hline(int x, int y, int width, char ch, uint8_t fg, uint8_t bg, uint8_t flags)
{
    for(int i = 0; i < width; ++i) pico_put_cell(x + i, y, ch, fg, bg, flags);
}

static void pico_render_screensaver(void)
{
    int step = g_screensaver_step;
    int mode = (step / 64) % 4;
    int phase = step % 64;
    int band = 7;
    int span = MOTH_COLS;

    pico_clear_cells();
    if(mode == 1) span = MOTH_ROWS;
    else if(mode == 2 || mode == 3) span = MOTH_COLS + MOTH_ROWS;

    for(int y = 0; y < MOTH_ROWS; ++y)
    {
        for(int x = 0; x < MOTH_COLS; ++x)
        {
            int coord;
            int center = ((phase * (span + band * 2)) / 64) - band;
            int lit;

            switch(mode)
            {
                case 0:
                    coord = x;
                    break;
                case 1:
                    coord = y;
                    break;
                case 3:
                    coord = (MOTH_COLS - 1 - x) + y;
                    break;
                default:
                    coord = x + y;
                    break;
            }
            lit = coord >= center && coord < center + band;
            pico_put_cell(x, y, PICOCALC_GLYPH_SOLID, lit ? 7 : 0, lit ? 0 : 7, 0);
        }
    }
    mothpad_lcd_flush_cells(&g_mothpad, 0);
}

static void pico_draw_bottom_message(void)
{
    if(!pico_message_active()) return;
    pico_fill_row(MOTH_BOTTOM_ROW, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    pico_draw_text(0, MOTH_BOTTOM_ROW, g_message, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
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
    pico_draw_text(x, MOTH_TOP_ROW, text, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    x += (int)strlen(text);
    pico_put_cell(x++, MOTH_TOP_ROW, ' ', PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    pico_put_cell(x++, MOTH_TOP_ROW, left, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    pico_put_cell(x++, MOTH_TOP_ROW, right, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    pico_put_cell(x, MOTH_TOP_ROW, charge, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
}

static void pico_render_editing(void)
{
    moth_render(&g_mothpad);
    pico_draw_battery();
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

    pico_render_editing();
    pico_draw_popup_box(x, y, width, height);
    pico_draw_text(x + 2, y + 1, "Save as", 7, 0, MOTH_CELL_STATUS);
    pico_draw_text_field(x + 2, field_y, field_width, g_save_name, g_save_name_len, g_save_name_cursor);
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

    pico_render_editing();
    pico_draw_popup_box(x, y, width, height);
    pico_draw_text(x + 2, y + 1, "Find", 7, 0, MOTH_CELL_STATUS);
    pico_draw_text_field(x + 2, field_y, field_width, g_find_query, g_find_query_len, g_find_query_cursor);
    pico_draw_text(x + 2, y + 3, "Enter find  Esc", 7, 0, MOTH_CELL_STATUS);
}

static void pico_render_file_list(void)
{
    char top[96];
    int list_cols = MOTH_COLS;

    pico_update_file_preview();
    pico_clear_cells();
    pico_fill_row(MOTH_TOP_ROW, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    pico_fill_row(MOTH_BOTTOM_ROW, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);

#ifdef PORTMANTEAU_DEMO
    if(g_mode == PICO_MODE_PORTMANTEAU_FOLDARIUM ||
       (g_mode == PICO_MODE_FILE_ACTION_MENU && g_file_action_return_mode == PICO_MODE_PORTMANTEAU_FOLDARIUM))
    {
        snprintf(top, sizeof(top), "Portmanteau/Foldarium: %s", g_cwd);
    }
    else
#endif
    {
        snprintf(top, sizeof(top), "Open: %s", g_cwd);
    }
    pico_draw_text_clipped(0, MOTH_TOP_ROW, top, pico_top_text_cols_before_battery(), PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    pico_draw_battery();

    for(int row = 0, entry_index = g_entry_scroll; row < MOTH_TEXT_ROWS && entry_index < g_entry_count; ++entry_index)
    {
        const char *entry_text = pico_file_entry_text(entry_index);
        int text_len = (int)strlen(entry_text);
        int pos = 0;
        int first = 1;
        int payload_cols = list_cols - 2;

        if(payload_cols < 1) payload_cols = 1;
        if(text_len <= 0) text_len = 1;

        while(row < MOTH_TEXT_ROWS && pos < text_len)
        {
            int y = MOTH_TEXT_FIRST_ROW + row;
            int chunk = text_len - pos;
            uint8_t fg = 7;
            uint8_t bg = 0;
            uint8_t flags = 0;
            char line[MOTH_COLS + 1];

            if(entry_index == g_entry_selected)
            {
                fg = 0;
                bg = 7;
                flags = MOTH_CELL_SELECTION;
                for(int col = 0; col < list_cols; ++col) pico_put_cell(col, y, ' ', fg, bg, flags);
            }

            if(chunk > payload_cols) chunk = payload_cols;
            snprintf(line,
                     sizeof(line),
                     "%c %.*s",
                     first ? (g_entries[entry_index].is_recent ? '*' : (g_entries[entry_index].is_dir ? '/' : ' ')) : ' ',
                     chunk,
                     entry_text + pos);
            pico_draw_text(0, y, line, fg, bg, flags);
            pos += chunk;
            first = 0;
            ++row;
        }
    }

    if(g_preview_visible)
    {
        for(int row = 0; row < MOTH_TEXT_ROWS; ++row)
        {
            int y = MOTH_TEXT_FIRST_ROW + row;
            pico_put_cell(MOTH_FILE_LIST_COLS, y, PICOCALC_GLYPH_MENU_VL, 7, 0, MOTH_CELL_STATUS);
            for(int col = MOTH_PREVIEW_X; col < MOTH_COLS; ++col)
            {
                pico_put_cell(col, y, ' ', 7, 0, 0);
            }
        }
        pico_draw_text(MOTH_PREVIEW_X, MOTH_TEXT_FIRST_ROW, "Peek", 7, 0, MOTH_CELL_STATUS);
        if(g_entry_count > 0 && g_entry_selected >= 0 && g_entry_selected < g_entry_count)
        {
            const char *selected = pico_file_entry_text(g_entry_selected);
            int len = (int)strlen(selected);
            int title_rows = len > MOTH_PREVIEW_COLS ? 2 : 1;

            for(int row = 0; row < title_rows; ++row)
            {
                int offset = row * MOTH_PREVIEW_COLS;
                if(offset >= len) break;
                pico_draw_text_clipped(MOTH_PREVIEW_X,
                                       MOTH_TEXT_FIRST_ROW + 1 + row,
                                       selected + offset,
                                       MOTH_PREVIEW_COLS,
                                       7,
                                       0,
                                       MOTH_CELL_STATUS);
            }
            pico_draw_hline(MOTH_PREVIEW_X,
                            MOTH_TEXT_FIRST_ROW + 1 + title_rows,
                            MOTH_PREVIEW_COLS,
                            PICOCALC_GLYPH_MENU_HT,
                            7,
                            0,
                            MOTH_CELL_STATUS);

            for(int row = 0; row < g_preview_line_count && row < MOTH_TEXT_ROWS - title_rows - 2; ++row)
            {
                pico_draw_text(MOTH_PREVIEW_X, MOTH_TEXT_FIRST_ROW + title_rows + 2 + row, g_preview_lines[row], 7, 0, 0);
            }
        }
        else
        {
            pico_draw_hline(MOTH_PREVIEW_X, MOTH_TEXT_FIRST_ROW + 2, MOTH_PREVIEW_COLS, PICOCALC_GLYPH_MENU_HT, 7, 0, MOTH_CELL_STATUS);
        }
    }

#ifdef PORTMANTEAU_DEMO
    if(g_mode == PICO_MODE_PORTMANTEAU_FOLDARIUM ||
       (g_mode == PICO_MODE_FILE_ACTION_MENU && g_file_action_return_mode == PICO_MODE_PORTMANTEAU_FOLDARIUM))
    {
        char bottom[80];
        snprintf(bottom,
                 sizeof(bottom),
                 "Enter handoff  Esc Mothpad  C1:%lu",
                 (unsigned long)g_duolith_mailbox.payload_heartbeat[DUOLITH_APP_FOLDARIUM]);
        pico_draw_text(0, MOTH_BOTTOM_ROW, bottom, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    }
    else
#endif
    {
        pico_draw_text(0, MOTH_BOTTOM_ROW, "F1 File  Enter open  Esc cancel", PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    }
}

static const char *pico_file_action_label(int index)
{
    static char label[24];

    switch(index)
    {
        case PICO_FILE_ACTION_OPEN: return "Open";
        case PICO_FILE_ACTION_NEW_FOLDER: return "New Folder";
        case PICO_FILE_ACTION_COPY: return "Copy";
        case PICO_FILE_ACTION_CUT: return "Cut";
        case PICO_FILE_ACTION_PASTE: return "Paste";
        case PICO_FILE_ACTION_RENAME: return "Rename";
        case PICO_FILE_ACTION_DELETE: return "Delete";
        case PICO_FILE_ACTION_RECENT_AT_ROOT:
            return pico_toggle_label(label, sizeof(label), "Recent Root", g_show_recent_at_root);
        case PICO_FILE_ACTION_CANCEL: return "Cancel";
        default: return "";
    }
}

static const char *pico_file_action_accel(int index)
{
    switch(index)
    {
        case PICO_FILE_ACTION_OPEN: return "O";
        case PICO_FILE_ACTION_NEW_FOLDER: return "N";
        case PICO_FILE_ACTION_COPY: return "C";
        case PICO_FILE_ACTION_CUT: return "X";
        case PICO_FILE_ACTION_PASTE: return "V";
        case PICO_FILE_ACTION_RENAME: return "R";
        case PICO_FILE_ACTION_DELETE: return "D";
        case PICO_FILE_ACTION_RECENT_AT_ROOT: return "E";
        default: return "";
    }
}

static int pico_file_action_for_key(int key)
{
    switch(key)
    {
        case 'o':
        case 'O':
            return PICO_FILE_ACTION_OPEN;
        case 'n':
        case 'N':
            return PICO_FILE_ACTION_NEW_FOLDER;
        case 'c':
        case 'C':
            return PICO_FILE_ACTION_COPY;
        case 'x':
        case 'X':
            return PICO_FILE_ACTION_CUT;
        case 'v':
        case 'V':
            return PICO_FILE_ACTION_PASTE;
        case 'r':
        case 'R':
            return PICO_FILE_ACTION_RENAME;
        case 'd':
        case 'D':
            return PICO_FILE_ACTION_DELETE;
        case 'e':
        case 'E':
            return PICO_FILE_ACTION_RECENT_AT_ROOT;
        default:
            return -1;
    }
}

static void pico_render_file_action_menu(void)
{
    const int x = 0;
    const int y = 1;
    const int width = 19;

    pico_render_file_list();
    pico_draw_text(0, MOTH_TOP_ROW, " File ", 0, 7, MOTH_CELL_SELECTION);
    pico_put_cell(x, y, PICOCALC_GLYPH_MENU_TL, 7, 0, MOTH_CELL_STATUS);
    pico_draw_hline(x + 1, y, width - 2, PICOCALC_GLYPH_MENU_HT, 7, 0, MOTH_CELL_STATUS);
    pico_put_cell(x + width - 1, y, PICOCALC_GLYPH_MENU_TR, 7, 0, MOTH_CELL_STATUS);

    for(int i = 0; i < PICO_FILE_ACTION_COUNT; ++i)
    {
        const char *accel = pico_file_action_accel(i);
        int row = y + 1 + i;
        uint8_t fg = (i == g_file_action_selected) ? 0 : 7;
        uint8_t bg = (i == g_file_action_selected) ? 7 : 0;
        uint8_t flags = (i == g_file_action_selected) ? MOTH_CELL_SELECTION : MOTH_CELL_STATUS;

        for(int col = 0; col < width; ++col) pico_put_cell(x + col, row, ' ', fg, bg, flags);
        pico_put_cell(x, row, PICOCALC_GLYPH_MENU_VL, 7, 0, flags);
        pico_put_cell(x + width - 1, row, PICOCALC_GLYPH_MENU_VR, 7, 0, flags);
        pico_draw_text(x + 2, row, pico_file_action_label(i), fg, bg, flags);
        if(accel[0])
        {
            int accel_x = x + width - 2 - (int)strlen(accel);
            pico_draw_text(accel_x, row, accel, fg, bg, flags);
        }
    }

    pico_put_cell(x, y + PICO_FILE_ACTION_COUNT + 1, PICOCALC_GLYPH_MENU_BL, 7, 0, MOTH_CELL_STATUS);
    pico_draw_hline(x + 1, y + PICO_FILE_ACTION_COUNT + 1, width - 2, PICOCALC_GLYPH_MENU_HB, 7, 0, MOTH_CELL_STATUS);
    pico_put_cell(x + width - 1, y + PICO_FILE_ACTION_COUNT + 1, PICOCALC_GLYPH_MENU_BR, 7, 0, MOTH_CELL_STATUS);
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
        if(index == 0) return "N";
        if(index == 1) return "O";
        if(index == 2) return "S";
        if(index == 5) return "Q";
    }
    else if(menu == PICO_MENU_EDIT)
    {
        if(index == 0) return "Z";
        if(index == 1) return "X";
        if(index == 2) return "C";
        if(index == 3) return "V";
    }
    else if(menu == PICO_MENU_SELECT)
    {
        if(index == 0) return "F";
        if(index == 1) return "A";
        if(index == 2) return "D";
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
    pico_draw_text(0, MOTH_TOP_ROW, " File  Edit  Select  View ", PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
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

static int pico_calc_space(int ch)
{
    return ch == ' ' || ch == '\t';
}

static void pico_calc_skip_spaces(const char **expr)
{
    while(expr && *expr && pico_calc_space((unsigned char)**expr)) ++(*expr);
}

static int pico_calc_parse_expr(const char **expr, double *out, char *error, size_t error_size);

static int pico_calc_parse_factor(const char **expr, double *out, char *error, size_t error_size)
{
    char *end = NULL;

    pico_calc_skip_spaces(expr);
    if(!expr || !*expr || !out) return 0;

    if(**expr == '+')
    {
        ++(*expr);
        return pico_calc_parse_factor(expr, out, error, error_size);
    }
    if(**expr == '-')
    {
        ++(*expr);
        if(!pico_calc_parse_factor(expr, out, error, error_size)) return 0;
        *out = -*out;
        return 1;
    }
    if(**expr == '(')
    {
        ++(*expr);
        if(!pico_calc_parse_expr(expr, out, error, error_size)) return 0;
        pico_calc_skip_spaces(expr);
        if(**expr != ')')
        {
            snprintf(error, error_size, "Missing )");
            return 0;
        }
        ++(*expr);
        return 1;
    }

    *out = strtod(*expr, &end);
    if(end == *expr)
    {
        snprintf(error, error_size, "Expected number");
        return 0;
    }
    *expr = end;
    return 1;
}

static int pico_calc_parse_term(const char **expr, double *out, char *error, size_t error_size)
{
    if(!pico_calc_parse_factor(expr, out, error, error_size)) return 0;

    for(;;)
    {
        double rhs = 0.0;
        char op;

        pico_calc_skip_spaces(expr);
        op = **expr;
        if(op != '*' && op != '/' && op != 'x' && op != 'X') return 1;
        ++(*expr);
        if(!pico_calc_parse_factor(expr, &rhs, error, error_size)) return 0;

        if(op == '/')
        {
            if(rhs == 0.0)
            {
                snprintf(error, error_size, "Divide by zero");
                return 0;
            }
            *out /= rhs;
        }
        else
        {
            *out *= rhs;
        }
    }
}

static int pico_calc_parse_expr(const char **expr, double *out, char *error, size_t error_size)
{
    if(!pico_calc_parse_term(expr, out, error, error_size)) return 0;

    for(;;)
    {
        double rhs = 0.0;
        char op;

        pico_calc_skip_spaces(expr);
        op = **expr;
        if(op != '+' && op != '-') return 1;
        ++(*expr);
        if(!pico_calc_parse_term(expr, &rhs, error, error_size)) return 0;
        if(op == '+') *out += rhs;
        else *out -= rhs;
    }
}

static int pico_calc_evaluate(const char *expr, double *out, char *error, size_t error_size)
{
    const char *cursor = expr;

    if(!expr || !expr[0])
    {
        snprintf(error, error_size, "No expression");
        return 0;
    }
    if(!pico_calc_parse_expr(&cursor, out, error, error_size)) return 0;
    pico_calc_skip_spaces(&cursor);
    if(*cursor)
    {
        snprintf(error, error_size, "Unexpected %c", *cursor);
        return 0;
    }
    return 1;
}

static void pico_calc_set_expr(const char *expr)
{
    snprintf(g_calc_expr, sizeof(g_calc_expr), "%s", expr ? expr : "");
    g_calc_expr_len = (int)strlen(g_calc_expr);
    g_calc_cursor = g_calc_expr_len;
}

static void pico_calc_format_result(double result)
{
    if(result >= -9007199254740992.0 && result <= 9007199254740992.0)
    {
        long long integer = (long long)result;
        if((double)integer == result)
        {
            snprintf(g_calc_result, sizeof(g_calc_result), "%lld", integer);
            return;
        }
    }

    snprintf(g_calc_result, sizeof(g_calc_result), "%.10g", result);
}

static int pico_calc_eval_to_result(char *status, size_t status_size)
{
    double result = 0.0;
    char error[40] = "";

    if(!g_calc_expr[0])
    {
        snprintf(g_calc_result, sizeof(g_calc_result), "?");
        snprintf(status, status_size, "Enter stores");
        return 0;
    }

    if(pico_calc_evaluate(g_calc_expr, &result, error, sizeof(error)))
    {
        pico_calc_format_result(result);
        snprintf(status, status_size, "OK");
        return 1;
    }

    snprintf(g_calc_result, sizeof(g_calc_result), "?");
    snprintf(status, status_size, "%s", error[0] ? error : "Error");
    return 0;
}

static void pico_calc_live_update(void)
{
    (void)pico_calc_eval_to_result(g_calc_status, sizeof(g_calc_status));
}

static void pico_calc_push_history(void)
{
    if(!g_calc_expr[0] || g_calc_status[0] != 'O' || g_calc_status[1] != 'K') return;
    if(g_calc_history_count > 0 &&
       strcmp(g_calc_history_expr[g_calc_history_count - 1], g_calc_expr) == 0 &&
       strcmp(g_calc_history_result[g_calc_history_count - 1], g_calc_result) == 0)
    {
        return;
    }

    if(g_calc_history_count >= PICO_CALC_HISTORY)
    {
        memmove(g_calc_history_expr,
                g_calc_history_expr + 1,
                sizeof(g_calc_history_expr[0]) * (PICO_CALC_HISTORY - 1));
        memmove(g_calc_history_result,
                g_calc_history_result + 1,
                sizeof(g_calc_history_result[0]) * (PICO_CALC_HISTORY - 1));
        g_calc_history_count = PICO_CALC_HISTORY - 1;
    }

    snprintf(g_calc_history_expr[g_calc_history_count], sizeof(g_calc_history_expr[0]), "%s", g_calc_expr);
    snprintf(g_calc_history_result[g_calc_history_count], sizeof(g_calc_history_result[0]), "%s", g_calc_result);
    ++g_calc_history_count;
    if(g_calc_history_recall < 0) g_calc_history_top = g_calc_history_count - 1;
}

static void pico_calc_run(void)
{
    if(pico_calc_eval_to_result(g_calc_status, sizeof(g_calc_status)))
    {
        snprintf(g_calc_stored_expr, sizeof(g_calc_stored_expr), "%s", g_calc_expr);
        pico_calc_push_history();
        pico_calc_set_expr(g_calc_result);
        g_calc_history_recall = -1;
        g_calc_history_top = g_calc_history_count - 1;
        g_calc_stored_expr_active = 1;
        pico_set_message("Stored");
    }
}

static void pico_calc_copy_input(void)
{
    const char *expression = g_calc_stored_expr_active
                           ? g_calc_stored_expr
                           : g_calc_expr;

    snprintf(g_clipboard, sizeof(g_clipboard), "%s = %s", expression, g_calc_result);
    g_clipboard_len = (int)strlen(g_clipboard);
    pico_set_message("Copied");
}

static void pico_calc_insert_char(char ch)
{
    if(g_calc_expr_len + 1 >= (int)sizeof(g_calc_expr))
    {
        snprintf(g_calc_status, sizeof(g_calc_status), "Expression full");
        return;
    }

    if(g_calc_cursor < 0) g_calc_cursor = 0;
    if(g_calc_cursor > g_calc_expr_len) g_calc_cursor = g_calc_expr_len;
    memmove(g_calc_expr + g_calc_cursor + 1,
            g_calc_expr + g_calc_cursor,
            (size_t)(g_calc_expr_len - g_calc_cursor + 1));
    g_calc_expr[g_calc_cursor++] = ch;
    ++g_calc_expr_len;
    g_calc_history_recall = -1;
    g_calc_stored_expr_active = 0;
    pico_calc_live_update();
}

static void pico_calc_backspace(void)
{
    if(g_calc_cursor <= 0) return;
    memmove(g_calc_expr + g_calc_cursor - 1,
            g_calc_expr + g_calc_cursor,
            (size_t)(g_calc_expr_len - g_calc_cursor + 1));
    --g_calc_cursor;
    --g_calc_expr_len;
    g_calc_history_recall = -1;
    g_calc_stored_expr_active = 0;
    pico_calc_live_update();
}

static void pico_calc_delete(void)
{
    if(g_calc_cursor < 0) g_calc_cursor = 0;
    if(g_calc_cursor >= g_calc_expr_len) return;
    memmove(g_calc_expr + g_calc_cursor,
            g_calc_expr + g_calc_cursor + 1,
            (size_t)(g_calc_expr_len - g_calc_cursor));
    --g_calc_expr_len;
    g_calc_history_recall = -1;
    g_calc_stored_expr_active = 0;
    pico_calc_live_update();
}

static int pico_calc_history_entry_rows(int history_index)
{
    char line[PICO_CALC_EXPR_SIZE + PICO_CALC_RESULT_SIZE + 4];
    int width = MOTH_COLS - 2;
    int length;

    if(history_index < 0 || history_index >= g_calc_history_count) return 0;
    snprintf(line,
             sizeof(line),
             "%s = %s",
             g_calc_history_expr[history_index],
             g_calc_history_result[history_index]);
    length = (int)strlen(line);
    return length > 0 ? (length + width - 1) / width : 1;
}

static int pico_calc_history_visible(int top, int selected)
{
    int rows = 0;
    int available_rows = MOTH_BOTTOM_ROW - PICO_CALC_HISTORY_FIRST_ROW;

    for(int history = top; history >= 0 && rows < available_rows; --history)
    {
        int entry_rows = pico_calc_history_entry_rows(history);
        if(history == selected) return rows + entry_rows <= available_rows;
        rows += entry_rows;
    }
    return 0;
}

static void pico_calc_ensure_history_visible(void)
{
    if(g_calc_history_recall < 0)
    {
        g_calc_history_top = g_calc_history_count - 1;
        return;
    }
    if(g_calc_history_top < 0 || g_calc_history_top >= g_calc_history_count)
    {
        g_calc_history_top = g_calc_history_count - 1;
    }
    if(g_calc_history_recall > g_calc_history_top)
    {
        g_calc_history_top = g_calc_history_recall;
    }
    while(g_calc_history_top > g_calc_history_recall &&
          !pico_calc_history_visible(g_calc_history_top, g_calc_history_recall))
    {
        --g_calc_history_top;
    }
}

static void pico_calc_recall_history(int direction)
{
    if(g_calc_history_count <= 0) return;

    if(g_calc_history_recall < 0)
    {
        g_calc_history_recall = g_calc_history_count - 1;
    }
    else
    {
        if(direction < 0 && g_calc_history_recall < g_calc_history_count - 1)
        {
            ++g_calc_history_recall;
        }
        else if(direction > 0 && g_calc_history_recall > 0)
        {
            --g_calc_history_recall;
        }
    }

    pico_calc_ensure_history_visible();
    pico_calc_set_expr(g_calc_history_expr[g_calc_history_recall]);
    snprintf(g_calc_result, sizeof(g_calc_result), "%s", g_calc_history_result[g_calc_history_recall]);
    snprintf(g_calc_status, sizeof(g_calc_status), "OK");
    g_calc_stored_expr_active = 0;
}

static void pico_render_calc(void)
{
#ifdef PORTMANTEAU_DEMO
    const volatile DuolithCalculatorState *calc = &g_duolith_mailbox.calculator;
#endif
    pico_clear_cells();
    pico_fill_row(MOTH_TOP_ROW, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    pico_fill_row(MOTH_BOTTOM_ROW, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    pico_draw_text(0, MOTH_TOP_ROW, " Mothulator ", PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    pico_draw_popup_box(1, 2, 38, 5);
    pico_draw_popup_box(1, 8, 38, 5);
    pico_draw_text(3, 2, " Input ", 7, 0, MOTH_CELL_STATUS);
    pico_draw_text(3, 8, " Result ", 7, 0, MOTH_CELL_STATUS);

    {
        int start = 0;
        int field_cols = 34;
        int cursor_x;
#ifdef PORTMANTEAU_DEMO
        int expr_len = calc->expr_len;
        int cursor = calc->cursor;
        if(expr_len > field_cols - 1) start = expr_len - (field_cols - 1);
        if(cursor < start) start = cursor;
        pico_draw_text(3, 4, (const char *)calc->expr + start, 7, 0, 0);
        cursor_x = 3 + cursor - start;
#else
        if(g_calc_expr_len > field_cols - 1) start = g_calc_expr_len - (field_cols - 1);
        if(g_calc_cursor < start) start = g_calc_cursor;
        pico_draw_text(3, 4, g_calc_expr + start, 7, 0, 0);
        cursor_x = 3 + g_calc_cursor - start;
#endif
        if(cursor_x >= 37) cursor_x = 36;
        pico_put_cell(cursor_x, 5, '_', 7, 0, MOTH_CELL_CURSOR);
    }

#ifdef PORTMANTEAU_DEMO
    pico_draw_text(3, 10, (const char *)calc->result, 7, 0, 0);
    if(calc->status[0] && !(calc->status[0] == 'O' && calc->status[1] == 'K'))
    {
        pico_draw_text(3, 12, (const char *)calc->status, 7, 0, 0);
    }
    pico_draw_text(2, 14, "History", 7, 0, 0);
    for(int i = 0; i < 4 && i < calc->history_count; ++i)
    {
        int hist = calc->history_count - 1 - i;
        char line[40];
        snprintf(line, sizeof(line), "%s = %s", (const char *)calc->history_expr[hist], (const char *)calc->history_result[hist]);
        pico_draw_text(2, 16 + i, line, hist == calc->history_recall ? 0 : 7, hist == calc->history_recall ? 7 : 0, hist == calc->history_recall ? MOTH_CELL_SELECTION : 0);
    }
#else
    pico_draw_text(3, 10, g_calc_result, 7, 0, 0);
    if(g_calc_status[0] && !(g_calc_status[0] == 'O' && g_calc_status[1] == 'K'))
    {
        pico_draw_text(3, 12, g_calc_status, 7, 0, 0);
    }
    pico_draw_text(2, PICO_CALC_HISTORY_FIRST_ROW - 1, "History", 7, 0, 0);
    {
        int row = PICO_CALC_HISTORY_FIRST_ROW;
        int top = g_calc_history_top;
        if(top < 0 || top >= g_calc_history_count) top = g_calc_history_count - 1;
        for(int hist = top; hist >= 0 && row < MOTH_BOTTOM_ROW; --hist)
        {
            char line[PICO_CALC_EXPR_SIZE + PICO_CALC_RESULT_SIZE + 4];
            int offset = 0;
            int selected = hist == g_calc_history_recall;
            snprintf(line, sizeof(line), "%s = %s", g_calc_history_expr[hist], g_calc_history_result[hist]);
            while(line[offset] && row < MOTH_BOTTOM_ROW)
            {
                char fragment[MOTH_COLS - 1];
                int remaining = (int)strlen(line + offset);
                int chunk = remaining < MOTH_COLS - 2 ? remaining : MOTH_COLS - 2;

                memcpy(fragment, line + offset, (size_t)chunk);
                fragment[chunk] = 0;
                pico_draw_text(2, row, fragment,
                               selected ? 0 : 7, selected ? 7 : 0,
                               selected ? MOTH_CELL_SELECTION : 0);
                offset += chunk;
                ++row;
            }
        }
    }
#endif
#ifdef PORTMANTEAU_DEMO
    {
        char bottom[80];
        snprintf(bottom,
                 sizeof(bottom),
                 "Enter eval  Hist  C clear  Esc/F5  C1:%lu K:%lu",
                 (unsigned long)g_duolith_mailbox.payload_heartbeat[DUOLITH_APP_CALCULATOR],
                 (unsigned long)g_duolith_mailbox.calculator.handled_keys);
        pico_draw_text(0, MOTH_BOTTOM_ROW, bottom, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    }
#else
    if(pico_message_active()) pico_draw_bottom_message();
    else pico_draw_text(0, MOTH_BOTTOM_ROW, "Enter store C copy Q clr Up/Dn Esc/F5", PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
#endif
}

static void pico_render(void)
{
#ifdef PORTMANTEAU_DEMO
    if(pico_duolith_core1_calculator_active())
    {
        return;
    }
#endif

    if(g_mode == PICO_MODE_MENU)
    {
        pico_render_menu();
    }
    else if(g_mode == PICO_MODE_FILE_LIST)
    {
        pico_render_file_list();
    }
    else if(g_mode == PICO_MODE_FILE_ACTION_MENU)
    {
        pico_render_file_action_menu();
    }
#ifdef PORTMANTEAU_DEMO
    else if(g_mode == PICO_MODE_PORTMANTEAU_FOLDARIUM)
    {
        pico_render_file_list();
    }
#endif
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
    g_preview_source_indent = 0;
    g_preview_source_line_start = 1;
    if(message && message[0])
    {
        snprintf(g_preview_lines[0], sizeof(g_preview_lines[0]), "%s", message);
        g_preview_line_count = 1;
    }
}

static int pico_selected_entry_path(char *out, size_t out_size)
{
    if(g_entry_count <= 0 || g_entry_selected < 0 || g_entry_selected >= g_entry_count) return 0;
    if(g_entries[g_entry_selected].is_recent)
    {
        if(!g_entries[g_entry_selected].path[0]) return 0;
        snprintf(out, out_size, "%s", g_entries[g_entry_selected].path);
        return 1;
    }
    return moth_join_path(out, out_size, g_cwd, g_entries[g_entry_selected].name) == MOTH_OK;
}

static const char *pico_file_entry_text(int index)
{
    if(index < 0 || index >= g_entry_count) return "";
    return g_entries[index].is_recent ? g_entries[index].path : g_entries[index].name;
}

static int pico_file_entry_visual_rows(int index)
{
    int payload_cols = MOTH_COLS - 2;
    int text_len;

    if(payload_cols < 1) payload_cols = 1;
    text_len = (int)strlen(pico_file_entry_text(index));
    if(text_len <= 0) text_len = 1;
    return (text_len + payload_cols - 1) / payload_cols;
}

static int pico_preview_indent_width(void)
{
    int indent = g_preview_source_indent;
    if(indent >= MOTH_PREVIEW_COLS) indent = MOTH_PREVIEW_COLS - 1;
    if(indent < 0) indent = 0;
    return indent;
}

static void pico_preview_apply_indent(int line)
{
    int indent = pico_preview_indent_width();
    if(line < 0 || line >= MOTH_PREVIEW_LINES) return;
    for(int i = 0; i < indent; ++i) g_preview_lines[line][i] = ' ';
    g_preview_lines[line][indent] = 0;
}

static int pico_preview_new_visual_line(int with_indent)
{
    if(g_preview_line_count >= MOTH_PREVIEW_LINES) return 0;
    ++g_preview_line_count;
    if(with_indent) pico_preview_apply_indent(g_preview_line_count - 1);
    return 1;
}

static int pico_preview_wrap_line_for_char(char ch)
{
    int line = g_preview_line_count - 1;
    int len = (int)strlen(g_preview_lines[line]);
    int break_col = -1;
    int indent = pico_preview_indent_width();

    if(g_preview_line_count >= MOTH_PREVIEW_LINES) return 0;

    if(ch == ' ')
    {
        return pico_preview_new_visual_line(1);
    }

    for(int i = len - 1; i > indent; --i)
    {
        if(g_preview_lines[line][i] == ' ')
        {
            break_col = i;
            break;
        }
    }

    if(!pico_preview_new_visual_line(1))
    {
        return 1;
    }

    if(break_col >= indent)
    {
        char carry[MOTH_PREVIEW_COLS + 1];
        int carry_len;
        snprintf(carry, sizeof(carry), "%s", g_preview_lines[line] + break_col + 1);
        g_preview_lines[line][break_col] = 0;
        carry_len = (int)strlen(carry);
        if(indent + carry_len >= MOTH_PREVIEW_COLS) carry_len = MOTH_PREVIEW_COLS - indent - 1;
        if(carry_len > 0)
        {
            memcpy(g_preview_lines[g_preview_line_count - 1] + indent, carry, (size_t)carry_len);
            g_preview_lines[g_preview_line_count - 1][indent + carry_len] = 0;
        }
    }

    return 1;
}

static void pico_preview_append_visible_char(char ch)
{
    int line;
    int len;

    if(g_preview_line_count <= 0) g_preview_line_count = 1;
    if(g_preview_line_count > MOTH_PREVIEW_LINES) return;

    line = g_preview_line_count - 1;
    len = (int)strlen(g_preview_lines[line]);
    if(len >= MOTH_PREVIEW_COLS)
    {
        if(!pico_preview_wrap_line_for_char(ch)) return;
        if(ch == ' ') return;
        line = g_preview_line_count - 1;
        len = (int)strlen(g_preview_lines[line]);
        if(len >= MOTH_PREVIEW_COLS) return;
    }

    g_preview_lines[line][len] = ch;
    g_preview_lines[line][len + 1] = 0;
}

static void pico_preview_append_char(char ch)
{
    if(g_preview_line_count <= 0) g_preview_line_count = 1;
    if(g_preview_line_count > MOTH_PREVIEW_LINES) return;

    if(ch == '\n')
    {
        g_preview_source_indent = 0;
        g_preview_source_line_start = 1;
        if(g_preview_line_count < MOTH_PREVIEW_LINES) ++g_preview_line_count;
        return;
    }

    if(g_preview_source_line_start && (ch == ' ' || ch == '\t'))
    {
        int count = 1;
        if(ch == '\t')
        {
            int tab_width = g_tab_width == 4 ? 4 : 2;
            int next = ((g_preview_source_indent / tab_width) + 1) * tab_width;
            count = next - g_preview_source_indent;
        }
        for(int i = 0; i < count; ++i)
        {
            ++g_preview_source_indent;
            pico_preview_append_visible_char(' ');
        }
        return;
    }

    g_preview_source_line_start = 0;
    if(ch == '\t')
    {
        int line = g_preview_line_count - 1;
        int len = (int)strlen(g_preview_lines[line]);
        int tab_width = g_tab_width == 4 ? 4 : 2;
        int next = ((len / tab_width) + 1) * tab_width;
        int count = next - len;
        for(int i = 0; i < count; ++i) pico_preview_append_visible_char(' ');
        return;
    }

    pico_preview_append_visible_char(ch);
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

    if(!pico_selected_entry_path(path, sizeof(path)))
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
    int sort_start = 0;

    g_entry_count = 0;
    g_entry_selected = 0;
    g_entry_scroll = 0;

    if(strcmp(g_cwd, "/") == 0 && g_show_recent_at_root)
    {
        pico_load_recent_files();
        for(int i = 0; i < g_recent_count && g_entry_count < MOTH_MAX_DIR_ENTRIES; ++i)
        {
            snprintf(g_entries[g_entry_count].name,
                     sizeof(g_entries[g_entry_count].name),
                     "%s",
                     pico_path_basename(g_recent_paths[i]));
            snprintf(g_entries[g_entry_count].path,
                     sizeof(g_entries[g_entry_count].path),
                     "%s",
                     g_recent_paths[i]);
            g_entries[g_entry_count].is_dir = 0;
            g_entries[g_entry_count].is_recent = 1;
            ++g_entry_count;
        }
        sort_start = g_entry_count;
    }
    else if(strcmp(g_cwd, "/") != 0)
    {
        snprintf(g_entries[g_entry_count].name, sizeof(g_entries[g_entry_count].name), "..");
        g_entries[g_entry_count].path[0] = 0;
        g_entries[g_entry_count].is_dir = 1;
        g_entries[g_entry_count].is_recent = 0;
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
        g_entries[g_entry_count].path[0] = 0;
        g_entries[g_entry_count].is_dir = (ent->d_type == DT_DIR);
        g_entries[g_entry_count].is_recent = 0;
        ++g_entry_count;
    }

    closedir(dir);
    if(g_entry_count - sort_start > 1)
    {
        qsort(g_entries + sort_start, (size_t)(g_entry_count - sort_start), sizeof(g_entries[0]), pico_compare_dir_entries);
    }
    g_preview_selected = -1;
    return 1;
}

static int pico_is_parent_entry(const PicoDirEntry *entry)
{
    return entry && entry->is_dir && strcmp(entry->name, "..") == 0;
}

static int pico_selected_file_path(char *out, size_t out_size)
{
    if(g_entry_count <= 0 || g_entry_selected < 0 || g_entry_selected >= g_entry_count) return 0;
    if(g_entries[g_entry_selected].is_recent) return 0;
    if(pico_is_parent_entry(&g_entries[g_entry_selected])) return 0;
    return moth_join_path(out, out_size, g_cwd, g_entries[g_entry_selected].name) == MOTH_OK;
}

static int pico_path_exists(const char *path)
{
    DIR *dir;
    FILE *file;

    if(!path || !path[0]) return 0;
    dir = opendir(path);
    if(dir)
    {
        closedir(dir);
        return 1;
    }

    file = fopen(path, "rb");
    if(file)
    {
        fclose(file);
        return 1;
    }

    return 0;
}

static int pico_dir_empty(const char *path)
{
    DIR *dir;
    struct dirent *ent;

    dir = opendir(path);
    if(!dir) return 0;
    while((ent = readdir(dir)) != NULL)
    {
        if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        closedir(dir);
        return 0;
    }
    closedir(dir);
    return 1;
}

static void pico_file_list_ensure_selected_visible(void)
{
    int rows_before = 0;
    int selected_rows;

    if(g_entry_count <= 0)
    {
        g_entry_selected = 0;
        g_entry_scroll = 0;
        return;
    }

    if(g_entry_selected < 0) g_entry_selected = 0;
    if(g_entry_selected >= g_entry_count) g_entry_selected = g_entry_count - 1;
    if(g_entry_selected < g_entry_scroll) g_entry_scroll = g_entry_selected;

    selected_rows = pico_file_entry_visual_rows(g_entry_selected);
    if(selected_rows >= MOTH_TEXT_ROWS)
    {
        g_entry_scroll = g_entry_selected;
    }
    else
    {
        for(int i = g_entry_scroll; i < g_entry_selected; ++i)
        {
            rows_before += pico_file_entry_visual_rows(i);
        }
        while(g_entry_scroll < g_entry_selected &&
              rows_before + selected_rows > MOTH_TEXT_ROWS)
        {
            rows_before -= pico_file_entry_visual_rows(g_entry_scroll);
            ++g_entry_scroll;
        }
    }

    if(g_entry_scroll < 0) g_entry_scroll = 0;
}

static void pico_refresh_file_list_keep_name(const char *name)
{
    pico_refresh_file_list();
    if(name && name[0])
    {
        for(int i = 0; i < g_entry_count; ++i)
        {
            if(strcmp(g_entries[i].name, name) == 0)
            {
                g_entry_selected = i;
                pico_file_list_ensure_selected_visible();
                return;
            }
        }
    }
}

static int pico_file_copy_bytes(const char *src, const char *dest)
{
    FILE *in;
    FILE *out;
    char buffer[512];
    size_t n;

    if(pico_path_exists(dest)) return 0;
    in = fopen(src, "rb");
    if(!in) return 0;
    out = fopen(dest, "wb");
    if(!out)
    {
        fclose(in);
        return 0;
    }

    while((n = fread(buffer, 1, sizeof(buffer), in)) > 0)
    {
        if(fwrite(buffer, 1, n, out) != n)
        {
            fclose(in);
            fclose(out);
            remove(dest);
            return 0;
        }
    }

    if(fclose(in) != 0)
    {
        fclose(out);
        remove(dest);
        return 0;
    }
    if(fclose(out) != 0)
    {
        remove(dest);
        return 0;
    }

    return 1;
}

static void pico_render_file_prompt(const char *label, const char *text, int len, int cursor)
{
    int label_len = label ? (int)strlen(label) : 0;
    int field_width;

    pico_render_file_list();
    pico_fill_row(MOTH_BOTTOM_ROW, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    if(label_len > MOTH_COLS - 1) label_len = MOTH_COLS - 1;
    pico_draw_text_clipped(0, MOTH_BOTTOM_ROW, label ? label : "", label_len, PICO_STATUS_FG, PICO_STATUS_BG, MOTH_CELL_STATUS);
    field_width = MOTH_COLS - label_len;
    pico_draw_text_field(label_len, MOTH_BOTTOM_ROW, field_width, text, len, cursor);
    mothpad_lcd_flush_cells(&g_mothpad, 0);
}

static int pico_file_prompt_text(const char *label, const char *initial, char *out, size_t out_size)
{
    int len;
    int cursor;

    if(!out || out_size == 0) return 0;
    snprintf(out, out_size, "%s", initial ? initial : "");
    len = (int)strlen(out);
    cursor = len;
    pico_render_file_prompt(label, out, len, cursor);

    for(;;)
    {
        int key = -1;
        int shift = 0;
        (void)picocalc_kbd_read_event(&key, &shift);
        (void)shift;

        if(key == PICOCALC_KEY_ENTER || key == '\r') return out[0] != 0;
        if(key == PICOCALC_KEY_ESC) return 0;
        if(key == PICOCALC_KEY_BACKSPACE)
        {
            pico_text_field_backspace(out, &len, &cursor);
            pico_render_file_prompt(label, out, len, cursor);
        }
        else if(key == PICOCALC_KEY_DEL)
        {
            pico_text_field_delete(out, &len, &cursor);
            pico_render_file_prompt(label, out, len, cursor);
        }
        else if(key == PICOCALC_KEY_LEFT)
        {
            if(cursor > 0) --cursor;
            pico_render_file_prompt(label, out, len, cursor);
        }
        else if(key == PICOCALC_KEY_RIGHT)
        {
            if(cursor < len) ++cursor;
            pico_render_file_prompt(label, out, len, cursor);
        }
        else if(key == PICOCALC_KEY_HOME)
        {
            cursor = 0;
            pico_render_file_prompt(label, out, len, cursor);
        }
        else if(key == PICOCALC_KEY_END)
        {
            cursor = len;
            pico_render_file_prompt(label, out, len, cursor);
        }
        else if(key >= 32 && key < 127 && len + 1 < (int)out_size)
        {
            char ch = (char)key;
            if(ch != '/' && ch != '\\' && ch != ':' && ch != '*' && ch != '?' &&
               ch != '"' && ch != '<' && ch != '>' && ch != '|')
            {
                pico_text_field_insert(out, &len, &cursor, (int)out_size, ch);
                pico_render_file_prompt(label, out, len, cursor);
            }
        }
        sleep_ms(12);
    }
}

static void pico_render_file_confirm_choice(const char *label, int selected)
{
    const int x = 7;
    const int y = 9;
    const int width = 26;
    const int height = 5;
    const char *cancel = "Cancel";
    const char *confirm = "Delete";
    const int cancel_x = x + 5;
    const int confirm_x = x + 15;
    uint8_t cancel_fg = selected == 0 ? 0 : 7;
    uint8_t cancel_bg = selected == 0 ? 7 : 0;
    uint8_t cancel_flags = selected == 0 ? MOTH_CELL_SELECTION : MOTH_CELL_STATUS;
    uint8_t confirm_fg = selected == 1 ? 0 : 7;
    uint8_t confirm_bg = selected == 1 ? 7 : 0;
    uint8_t confirm_flags = selected == 1 ? MOTH_CELL_SELECTION : MOTH_CELL_STATUS;

    pico_render_file_list();
    pico_draw_popup_box(x, y, width, height);
    pico_draw_text_clipped(x + 2, y + 1, label ? label : "Confirm", width - 4, 7, 0, MOTH_CELL_STATUS);
    pico_draw_text(cancel_x, y + 2, cancel, cancel_fg, cancel_bg, cancel_flags);
    pico_draw_text(confirm_x, y + 2, confirm, confirm_fg, confirm_bg, confirm_flags);
    pico_draw_text(x + 2, y + 3, "Left/Right  Enter", 7, 0, MOTH_CELL_STATUS);
    mothpad_lcd_flush_cells(&g_mothpad, 0);
}

static int pico_file_confirm_delete(void)
{
    int selected = 0;

    pico_render_file_confirm_choice("Delete selected?", selected);
    for(;;)
    {
        int key = -1;
        int shift = 0;
        (void)picocalc_kbd_read_event(&key, &shift);
        (void)shift;

        if(key == PICOCALC_KEY_ENTER || key == '\r') return selected == 1;
        if(key == PICOCALC_KEY_ESC || key == PICOCALC_KEY_BACKSPACE) return 0;
        if(key == PICOCALC_KEY_LEFT || key == PICOCALC_KEY_RIGHT ||
           key == PICOCALC_KEY_UP || key == PICOCALC_KEY_DOWN)
        {
            selected = selected ? 0 : 1;
            pico_render_file_confirm_choice("Delete selected?", selected);
        }
        else if(key == 'y' || key == 'Y')
        {
            return 1;
        }
        else if(key == 'n' || key == 'N')
        {
            return 0;
        }
        sleep_ms(12);
    }
}

static void pico_file_copy_selected(int cut)
{
    char path[256];

    if(g_entry_count > 0 && g_entry_selected >= 0 && g_entry_selected < g_entry_count &&
       g_entries[g_entry_selected].is_recent)
    {
        if(cut)
        {
            pico_set_message("Recent cannot move");
            return;
        }
        if(!pico_selected_entry_path(path, sizeof(path)))
        {
            pico_set_message("Bad recent path");
            return;
        }
    }
    else if(!pico_selected_file_path(path, sizeof(path)))
    {
        pico_set_message("No item selected");
        return;
    }

    if(g_entries[g_entry_selected].is_dir)
    {
        pico_set_message("Copy dir blocked");
        return;
    }

    g_file_clip_mode = cut ? PICO_FILE_CLIP_CUT : PICO_FILE_CLIP_COPY;
    snprintf(g_file_clip_path, sizeof(g_file_clip_path), "%s", path);
    snprintf(g_file_clip_name, sizeof(g_file_clip_name), "%s", g_entries[g_entry_selected].name);
    g_file_clip_is_dir = g_entries[g_entry_selected].is_dir;
    pico_set_message(cut ? "Cut file item" : "Copied file item");
}

static void pico_file_new_folder(void)
{
    char path[256];
    char name[256];

    if(!pico_file_prompt_text("New folder: ", "", name, sizeof(name)))
    {
        pico_set_message("New folder cancelled");
        return;
    }
    if(moth_join_path(path, sizeof(path), g_cwd, name) != MOTH_OK)
    {
        pico_set_message("Bad name");
        return;
    }
    if(pico_path_exists(path))
    {
        pico_set_message("Name exists");
        return;
    }
    if(mkdir(path, 0777) != 0)
    {
        pico_set_message("Create failed");
        return;
    }

    pico_set_message("Folder created");
    pico_refresh_file_list_keep_name(name);
}

static void pico_file_paste_clipboard(void)
{
    char dest[256];
    char pasted_name[256];

    if(g_file_clip_mode == PICO_FILE_CLIP_NONE || !g_file_clip_path[0] || !g_file_clip_name[0])
    {
        pico_set_message("Clipboard empty");
        return;
    }

    snprintf(pasted_name, sizeof(pasted_name), "%s", g_file_clip_name);
    if(moth_join_path(dest, sizeof(dest), g_cwd, g_file_clip_name) != MOTH_OK)
    {
        pico_set_message("Bad dest");
        return;
    }
    if(strcmp(dest, g_file_clip_path) == 0)
    {
        pico_set_message("Same folder");
        return;
    }
    if(pico_path_exists(dest))
    {
        pico_set_message("Dest exists");
        return;
    }

    if(g_file_clip_mode == PICO_FILE_CLIP_CUT)
    {
        if(rename(g_file_clip_path, dest) != 0)
        {
            pico_set_message("Move failed");
            return;
        }
        g_file_clip_mode = PICO_FILE_CLIP_NONE;
        g_file_clip_path[0] = 0;
        g_file_clip_name[0] = 0;
        pico_set_message("Moved");
        pico_refresh_file_list_keep_name(pasted_name);
        return;
    }

    if(g_file_clip_is_dir)
    {
        pico_set_message("Copy dir blocked");
        return;
    }
    if(!pico_file_copy_bytes(g_file_clip_path, dest))
    {
        pico_set_message("Copy failed");
        return;
    }
    pico_set_message("Copied file");
    pico_refresh_file_list_keep_name(pasted_name);
}

static void pico_file_rename_selected(void)
{
    char src[256];
    char dest[256];
    char new_name[256];

    if(g_entry_count > 0 && g_entry_selected >= 0 && g_entry_selected < g_entry_count &&
       g_entries[g_entry_selected].is_recent)
    {
        pico_set_message("Recent opens only");
        return;
    }
    if(!pico_selected_file_path(src, sizeof(src)))
    {
        pico_set_message("No item selected");
        return;
    }
    if(!pico_file_prompt_text("Rename: ", g_entries[g_entry_selected].name, new_name, sizeof(new_name)))
    {
        pico_set_message("Rename cancelled");
        return;
    }
    if(moth_join_path(dest, sizeof(dest), g_cwd, new_name) != MOTH_OK)
    {
        pico_set_message("Bad name");
        return;
    }
    if(pico_path_exists(dest))
    {
        pico_set_message("Name exists");
        return;
    }
    if(rename(src, dest) != 0)
    {
        pico_set_message("Rename failed");
        return;
    }

    pico_set_message("Renamed");
    pico_refresh_file_list_keep_name(new_name);
}

static void pico_file_delete_selected(void)
{
    char path[256];
    char old_name[256];

    if(g_entry_count > 0 && g_entry_selected >= 0 && g_entry_selected < g_entry_count &&
       g_entries[g_entry_selected].is_recent)
    {
        pico_set_message("Recent opens only");
        return;
    }
    if(!pico_selected_file_path(path, sizeof(path)))
    {
        pico_set_message("No item selected");
        return;
    }
    if(g_entries[g_entry_selected].is_dir && !pico_dir_empty(path))
    {
        pico_set_message("Dir not empty");
        return;
    }

    snprintf(old_name, sizeof(old_name), "%s", g_entries[g_entry_selected].name);
    if(!pico_file_confirm_delete())
    {
        pico_set_message("Delete cancelled");
        return;
    }
    if(remove(path) != 0)
    {
        pico_set_message("Delete failed");
        return;
    }
    pico_forget_recent_file(path);
    if(strcmp(g_file_clip_path, path) == 0)
    {
        g_file_clip_mode = PICO_FILE_CLIP_NONE;
        g_file_clip_path[0] = 0;
        g_file_clip_name[0] = 0;
    }

    pico_set_message("Deleted");
    (void)old_name;
    pico_refresh_file_list();
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
    pico_note_recent_file(path);
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
    pico_note_recent_file(path);
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

#ifdef PORTMANTEAU_DEMO
static void pico_duolith_show_mothpad(void *host)
{
    (void)host;
    g_mode = PICO_MODE_EDITING;
    g_lcd_cells_valid = 0;
}

static void pico_duolith_show_foldarium(void *host)
{
    (void)host;
    g_mode = PICO_MODE_PORTMANTEAU_FOLDARIUM;
    g_lcd_cells_valid = 0;
}

static void pico_duolith_show_calculator(void *host)
{
    (void)host;
    g_mode = PICO_MODE_CALC;
    g_lcd_cells_valid = 0;
}

static int pico_duolith_core1_calculator_active(void)
{
    return g_mode == PICO_MODE_CALC &&
           g_duolith_mailbox.active_app == DUOLITH_APP_CALCULATOR;
}

static void pico_begin_duolith_peer(PortmanteauApp *app)
{
    if(!app) return;

    g_duolith_foreground_peer = app;
    portmanteau_runtime_enter_app(&g_duolith_mailbox, app, &g_duolith_suite);
}

static void pico_begin_portmanteau_foldarium(void)
{
    if(!g_sd_ready)
    {
        pico_set_message("No SD mount");
        return;
    }

    if(pico_refresh_file_list())
    {
        pico_begin_duolith_peer(mothpad_duolith_suite_foldarium(&g_duolith_suite));
        pico_set_message("Foldarium foreground");
    }
}

static void pico_begin_portmanteau_calculator(void)
{
    pico_begin_duolith_peer(mothpad_duolith_suite_calculator(&g_duolith_suite));
    pico_set_message("Calculator foreground");
}

static void pico_return_from_portmanteau_foldarium(void)
{
    if(g_duolith_foreground_peer)
    {
        portmanteau_runtime_leave_app(&g_duolith_mailbox,
                                      g_duolith_foreground_peer,
                                      &g_duolith_suite);
        g_duolith_foreground_peer = NULL;
    }
    portmanteau_runtime_enter_app(&g_duolith_mailbox,
                                  mothpad_duolith_suite_mothpad(&g_duolith_suite),
                                  &g_duolith_suite);
}

static void pico_open_portmanteau_pending(void)
{
    if(!g_portmanteau_pending_path[0])
    {
        g_mode = PICO_MODE_EDITING;
        pico_set_message("No handoff path");
        return;
    }

    pico_load_path(g_portmanteau_pending_path);
    g_portmanteau_pending_path[0] = 0;
}

static void pico_portmanteau_select_path(const char *path)
{
    if(!path || !path[0])
    {
        pico_set_message("Bad path");
        return;
    }

    snprintf(g_portmanteau_pending_path, sizeof(g_portmanteau_pending_path), "%s", path);
    portmanteau_runtime_publish_open_path(&g_duolith_mailbox, path);
    pico_return_from_portmanteau_foldarium();
    if(g_mothpad.dirty)
    {
        pico_begin_dirty_confirm(PICO_DIRTY_LOAD_PENDING);
    }
    else
    {
        pico_open_portmanteau_pending();
    }
}
#endif

static void pico_begin_open(void)
{
#ifdef PORTMANTEAU_DEMO
    pico_begin_portmanteau_foldarium();
    return;
#endif
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
    g_save_name_cursor = 0;
    g_after_save_action = action;
    g_mode = PICO_MODE_SAVE_AS;
}

static void pico_begin_save_as(void)
{
    pico_begin_save_as_after(PICO_DIRTY_NONE);
}

static void pico_begin_find(void)
{
    g_find_query_cursor = g_find_query_len;
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
        case PICO_DIRTY_LOAD_PENDING:
#ifdef PORTMANTEAU_DEMO
            pico_open_portmanteau_pending();
#else
            g_mode = PICO_MODE_EDITING;
#endif
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

static MothStatus pico_insert_newline_with_indent(void)
{
    char indent[80];
    int indent_len = 0;
    int line = moth_cursor_line(&g_mothpad);
    int pos;
    int end;
    MothStatus status;

    if(line < 0 || line >= g_mothpad.line_count) return moth_insert_char(&g_mothpad, '\n');

    pos = g_mothpad.lines[line].start;
    end = g_mothpad.lines[line].end;
    while(pos < end && indent_len + 1 < (int)sizeof(indent))
    {
        char ch = g_mothpad.text[pos];
        if(ch != ' ' && ch != '\t') break;
        indent[indent_len++] = ch;
        ++pos;
    }
    indent[indent_len] = 0;

    moth_begin_undo_group(&g_mothpad);
    status = moth_insert_char(&g_mothpad, '\n');
    if(status == MOTH_OK && indent_len > 0)
    {
        status = moth_insert_text(&g_mothpad, indent);
    }
    moth_end_undo_group(&g_mothpad);
    return status;
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

static void pico_note_arrow_released(void)
{
    g_arrow_hold_dir = 0;
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

static void pico_arm_arrow_hold(int dir, int shift)
{
    g_arrow_hold_dir = dir;
    g_arrow_hold_shift = shift ? 1 : 0;
    g_next_arrow_hold = make_timeout_time_ms(PICO_ARROW_INITIAL_MS);
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

static int pico_handle_arrow_joystick_hold(void)
{
    uint8_t joy = 0xff;
    int dir = 0;
    int shift = g_shift_down ? 1 : 0;

    if(g_mode != PICO_MODE_EDITING || g_mothpad.read_only || g_shift_down)
    {
        pico_note_arrow_released();
        return 0;
    }
    if(picocalc_kbd_read_joystick(&joy) != 0)
    {
        pico_note_arrow_released();
        return 0;
    }

    if((joy & PICO_JOY_LEFT_BIT) == 0) dir = -1;
    else if((joy & PICO_JOY_RIGHT_BIT) == 0) dir = 1;
    else
    {
        pico_note_arrow_released();
        return 0;
    }

    if(dir != g_arrow_hold_dir || shift != g_arrow_hold_shift)
    {
        pico_arm_arrow_hold(dir, shift);
        return 0;
    }

    if(absolute_time_diff_us(get_absolute_time(), g_next_arrow_hold) > 0)
    {
        return 0;
    }

    if(dir < 0) pico_move_cursor_with_selection(moth_cursor_left, shift);
    else pico_move_cursor_with_selection(moth_cursor_right, shift);

    g_next_arrow_hold = make_timeout_time_ms(PICO_ARROW_REPEAT_MS);

    return 1;
}

static int pico_handle_arrow_key_move(int dir, int shift)
{
    if(dir == g_arrow_hold_dir && (shift ? 1 : 0) == g_arrow_hold_shift)
    {
        if(absolute_time_diff_us(get_absolute_time(), g_next_arrow_hold) > 0)
        {
            return 1;
        }

        if(dir < 0) pico_move_cursor_with_selection(moth_cursor_left, shift);
        else pico_move_cursor_with_selection(moth_cursor_right, shift);
        g_next_arrow_hold = make_timeout_time_ms(PICO_ARROW_REPEAT_MS);
        return 1;
    }

    if(dir < 0) pico_move_cursor_with_selection(moth_cursor_left, shift);
    else pico_move_cursor_with_selection(moth_cursor_right, shift);
    pico_arm_arrow_hold(dir, shift);
    return 1;
}

static int pico_handle_editing_key(int key, int shift)
{
    if(key < 0) return 0;
    int live_shift = (shift || g_shift_down) ? 1 : 0;

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
#ifdef PORTMANTEAU_DEMO
            pico_begin_portmanteau_calculator();
#else
            g_mode = PICO_MODE_CALC;
#endif
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
            return pico_handle_arrow_key_move(-1, live_shift);
        case PICOCALC_KEY_RIGHT:
            if(g_mothpad.read_only)
            {
                moth_scroll_view(&g_mothpad, 1);
                return 1;
            }
            if(live_shift) return 0;
            return pico_handle_arrow_key_move(1, live_shift);
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
            return pico_insert_newline_with_indent() == MOTH_OK;
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
#ifdef PORTMANTEAU_DEMO
            pico_begin_portmanteau_calculator();
#else
            g_mode = PICO_MODE_CALC;
#endif
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
        case 'd':
        case 'D':
            g_active_menu = PICO_MENU_SELECT;
            g_menu_selected[PICO_MENU_SELECT] = 2;
            pico_activate_menu_item();
            return 1;
        default:
            return 0;
    }
}

static void pico_file_open_selected(void)
{
    char path[256];

    if(g_entry_count <= 0) return;
    if(g_entries[g_entry_selected].is_recent)
    {
        if(pico_selected_entry_path(path, sizeof(path)))
        {
#ifdef PORTMANTEAU_DEMO
            if(g_mode == PICO_MODE_PORTMANTEAU_FOLDARIUM)
            {
                pico_portmanteau_select_path(path);
                return;
            }
#endif
            pico_load_path(path);
        }
        else
        {
            pico_set_message("Bad recent path");
        }
        return;
    }
    if(g_entries[g_entry_selected].is_dir)
    {
        pico_enter_dir(g_entries[g_entry_selected].name);
        pico_refresh_file_list();
    }
    else if(moth_join_path(path, sizeof(path), g_cwd, g_entries[g_entry_selected].name) == MOTH_OK)
    {
#ifdef PORTMANTEAU_DEMO
        if(g_mode == PICO_MODE_PORTMANTEAU_FOLDARIUM)
        {
            pico_portmanteau_select_path(path);
            return;
        }
#endif
        pico_load_path(path);
    }
}

static void pico_begin_file_action_menu(void)
{
    g_file_action_return_mode = g_mode;
    g_mode = PICO_MODE_FILE_ACTION_MENU;
    g_file_action_selected = 0;
}

static void pico_activate_file_action(int action)
{
    g_mode = g_file_action_return_mode;
    pico_render_file_list();

    switch(action)
    {
        case PICO_FILE_ACTION_OPEN:
            pico_file_open_selected();
            break;
        case PICO_FILE_ACTION_NEW_FOLDER:
            pico_file_new_folder();
            break;
        case PICO_FILE_ACTION_COPY:
            pico_file_copy_selected(0);
            break;
        case PICO_FILE_ACTION_CUT:
            pico_file_copy_selected(1);
            break;
        case PICO_FILE_ACTION_PASTE:
            pico_file_paste_clipboard();
            break;
        case PICO_FILE_ACTION_RENAME:
            pico_file_rename_selected();
            break;
        case PICO_FILE_ACTION_DELETE:
            pico_file_delete_selected();
            break;
        case PICO_FILE_ACTION_RECENT_AT_ROOT:
            g_show_recent_at_root = !g_show_recent_at_root;
            pico_save_settings();
            pico_set_message(g_show_recent_at_root ? "Recent shelf on" : "Recent shelf off");
            if(strcmp(g_cwd, "/") == 0) pico_refresh_file_list();
            break;
        case PICO_FILE_ACTION_CANCEL:
        default:
            break;
    }
}

static int pico_handle_file_action_menu_key(int key)
{
    int action;

    if(key < 0) return 0;
    action = pico_file_action_for_key(key);
    if(action >= 0)
    {
        pico_activate_file_action(action);
        return 1;
    }

    switch(key)
    {
        case PICOCALC_KEY_F1:
        case PICOCALC_KEY_ESC:
        case PICOCALC_KEY_BACKSPACE:
            g_mode = g_file_action_return_mode;
            return 1;
        case PICOCALC_KEY_UP:
            if(g_file_action_selected > 0) --g_file_action_selected;
            else g_file_action_selected = PICO_FILE_ACTION_COUNT - 1;
            return 1;
        case PICOCALC_KEY_DOWN:
            if(g_file_action_selected + 1 < PICO_FILE_ACTION_COUNT) ++g_file_action_selected;
            else g_file_action_selected = 0;
            return 1;
        case PICOCALC_KEY_ENTER:
        case '\r':
            pico_activate_file_action(g_file_action_selected);
            return 1;
        default:
            return 0;
    }
}

static int pico_handle_file_list_key(int key)
{
    if(key < 0) return 0;
    switch(key)
    {
        case PICOCALC_KEY_F1:
            pico_begin_file_action_menu();
            return 1;
        case PICOCALC_KEY_ESC:
        case PICOCALC_KEY_BACKSPACE:
            g_after_save_action = PICO_DIRTY_NONE;
#ifdef PORTMANTEAU_DEMO
            if(g_mode == PICO_MODE_PORTMANTEAU_FOLDARIUM)
            {
                pico_return_from_portmanteau_foldarium();
                pico_set_message("Mothpad foreground");
                return 1;
            }
#endif
            g_mode = PICO_MODE_EDITING;
            return 1;
        case PICOCALC_KEY_UP:
            if(g_entry_count > 0)
            {
                if(g_entry_selected > 0) --g_entry_selected;
                else g_entry_selected = g_entry_count - 1;
                pico_file_list_ensure_selected_visible();
            }
            return 1;
        case PICOCALC_KEY_DOWN:
            if(g_entry_count > 0)
            {
                if(g_entry_selected + 1 < g_entry_count) ++g_entry_selected;
                else g_entry_selected = 0;
                pico_file_list_ensure_selected_visible();
            }
            return 1;
        case PICOCALC_KEY_ENTER:
        case '\r':
            pico_file_open_selected();
            return 1;
        case PICOCALC_CTRL_C:
            pico_file_copy_selected(0);
            return 1;
        case PICOCALC_CTRL_X:
            pico_file_copy_selected(1);
            return 1;
        case PICOCALC_CTRL_V:
            pico_file_paste_clipboard();
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
            pico_text_field_backspace(g_save_name, &g_save_name_len, &g_save_name_cursor);
            return 1;
        case PICOCALC_KEY_DEL:
            pico_text_field_delete(g_save_name, &g_save_name_len, &g_save_name_cursor);
            return 1;
        case PICOCALC_KEY_LEFT:
            if(g_save_name_cursor > 0) --g_save_name_cursor;
            return 1;
        case PICOCALC_KEY_RIGHT:
            if(g_save_name_cursor < g_save_name_len) ++g_save_name_cursor;
            return 1;
        case PICOCALC_KEY_HOME:
            g_save_name_cursor = 0;
            return 1;
        case PICOCALC_KEY_END:
            g_save_name_cursor = g_save_name_len;
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
        pico_text_field_insert(g_save_name, &g_save_name_len, &g_save_name_cursor, (int)sizeof(g_save_name), (char)key);
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
            pico_text_field_backspace(g_find_query, &g_find_query_len, &g_find_query_cursor);
            return 1;
        case PICOCALC_KEY_DEL:
            pico_text_field_delete(g_find_query, &g_find_query_len, &g_find_query_cursor);
            return 1;
        case PICOCALC_KEY_LEFT:
            if(g_find_query_cursor > 0) --g_find_query_cursor;
            return 1;
        case PICOCALC_KEY_RIGHT:
            if(g_find_query_cursor < g_find_query_len) ++g_find_query_cursor;
            return 1;
        case PICOCALC_KEY_HOME:
            g_find_query_cursor = 0;
            return 1;
        case PICOCALC_KEY_END:
            g_find_query_cursor = g_find_query_len;
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
        pico_text_field_insert(g_find_query, &g_find_query_len, &g_find_query_cursor, (int)sizeof(g_find_query), (char)key);
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
#ifdef PORTMANTEAU_DEMO
            if(g_dirty_action == PICO_DIRTY_LOAD_PENDING) g_portmanteau_pending_path[0] = 0;
#endif
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
#ifdef PORTMANTEAU_DEMO
        if(g_dirty_action == PICO_DIRTY_LOAD_PENDING) g_portmanteau_pending_path[0] = 0;
#endif
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
    if(key == PICOCALC_KEY_ESC || key == PICOCALC_KEY_F5)
    {
#ifdef PORTMANTEAU_DEMO
        pico_return_from_portmanteau_foldarium();
        pico_set_message("Mothpad foreground");
#else
        g_mode = PICO_MODE_EDITING;
#endif
        return 1;
    }
#ifdef PORTMANTEAU_DEMO
    {
        uint32_t target_epoch;
        absolute_time_t timeout;

        portmanteau_runtime_submit_calculator_key(&g_duolith_mailbox, key);
        target_epoch = g_duolith_mailbox.calculator_key_epoch;
        timeout = make_timeout_time_ms(30);
        while(g_duolith_mailbox.calculator_ack_epoch != target_epoch &&
              absolute_time_diff_us(get_absolute_time(), timeout) > 0)
        {
            tight_loop_contents();
        }
        return 1;
    }
#endif
    if(key == PICOCALC_KEY_BACKSPACE)
    {
        pico_calc_backspace();
        return 1;
    }
    if(key == PICOCALC_KEY_DEL)
    {
        pico_calc_delete();
        return 1;
    }
    if(key == PICOCALC_KEY_LEFT)
    {
        if(g_calc_cursor > 0) --g_calc_cursor;
        return 1;
    }
    if(key == PICOCALC_KEY_RIGHT)
    {
        if(g_calc_cursor < g_calc_expr_len) ++g_calc_cursor;
        return 1;
    }
    if(key == PICOCALC_KEY_HOME)
    {
        g_calc_cursor = 0;
        return 1;
    }
    if(key == PICOCALC_KEY_END)
    {
        g_calc_cursor = g_calc_expr_len;
        return 1;
    }
    if(key == PICOCALC_KEY_UP)
    {
        pico_calc_recall_history(-1);
        return 1;
    }
    if(key == PICOCALC_KEY_DOWN)
    {
        pico_calc_recall_history(1);
        return 1;
    }
    if(key == PICOCALC_KEY_ENTER || key == '\r' || key == '=')
    {
        pico_calc_run();
        return 1;
    }
    if(key == 'c' || key == 'C')
    {
        pico_calc_copy_input();
        return 1;
    }
    if(key == 'q' || key == 'Q')
    {
        pico_calc_set_expr("");
        snprintf(g_calc_result, sizeof(g_calc_result), "?");
        snprintf(g_calc_status, sizeof(g_calc_status), "OK");
        g_calc_history_recall = -1;
        g_calc_stored_expr_active = 0;
        pico_set_message("Cleared");
        return 1;
    }
    if((key >= '0' && key <= '9') ||
       key == '+' || key == '-' || key == '*' || key == '/' ||
       key == 'x' || key == 'X' || key == '.' || key == '(' || key == ')' || key == ' ')
    {
        if(g_calc_expr_len + 1 < (int)sizeof(g_calc_expr))
        {
            pico_calc_insert_char((char)key);
        }
        else
        {
            snprintf(g_calc_status, sizeof(g_calc_status), "Expression full");
        }
        return 1;
    }
    return 1;
}

static int pico_handle_key(int key, int shift)
{
    if(g_mode == PICO_MODE_MENU) return pico_handle_menu_key(key);
    if(g_mode == PICO_MODE_FILE_LIST) return pico_handle_file_list_key(key);
    if(g_mode == PICO_MODE_FILE_ACTION_MENU) return pico_handle_file_action_menu_key(key);
#ifdef PORTMANTEAU_DEMO
    if(g_mode == PICO_MODE_PORTMANTEAU_FOLDARIUM) return pico_handle_file_list_key(key);
#endif
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
    pico_reset_screensaver_timer();
    pico_update_battery();

    g_sd_ready = pico_init_sd();
#ifdef PORTMANTEAU_DEMO
    portmanteau_runtime_init(&g_duolith_mailbox);
    {
        MothpadDuolithHost host = {
            pico_duolith_show_mothpad,
            pico_duolith_show_foldarium,
            pico_duolith_show_calculator,
            NULL,
        };
        mothpad_duolith_suite_init(&g_duolith_suite, &host, &g_duolith_mailbox);
        portmanteau_runtime_enter_app(&g_duolith_mailbox,
                                      mothpad_duolith_suite_mothpad(&g_duolith_suite),
                                      &g_duolith_suite);
    }
    multicore_launch_core1(portmanteau_runtime_core1_main);
    sleep_ms(50);
    pico_set_message("Portmanteau on Duolith");
#endif
#ifdef MOTHPAD_EXPERIMENTAL
    if(g_sd_ready)
    {
        multicore_launch_core1(mothpad_file_browser_app_core1);
        sleep_ms(100);
        {
            const volatile MothpadFileBrowserAppStatus *status = mothpad_file_browser_app_status();
            char message[80];
            snprintf(message, sizeof(message), "Exp core1 browser: %lu entries", (unsigned long)status->entries);
            pico_set_message(message);
        }
    }
#endif
    pico_load_settings();
    pico_load_recent_files();
    g_mothpad.tab_width = g_tab_width;
    if(pico_recovery_file_exists())
    {
        g_recovery_selected = 0;
        g_mode = PICO_MODE_RECOVERY_CONFIRM;
    }
#ifdef MOTHNOTE_EXPERIMENTAL
    else
    {
        char handoff_path[256];
        if(pico_read_foldarium_handoff(handoff_path, sizeof(handoff_path)))
        {
            if(pico_load_path(handoff_path))
            {
                pico_delete_foldarium_handoff();
                pico_set_message("Opened from Foldarium");
            }
            else
            {
                pico_set_message("Foldarium open failed");
            }
        }
    }
#endif
    pico_render();

    for(;;)
    {
        int key = -1;
        int shift = 0;

#ifdef PORTMANTEAU_DEMO
        if(g_mode == PICO_MODE_CALC)
        {
            if(g_duolith_mailbox.active_app == DUOLITH_APP_CALCULATOR)
            {
                sleep_ms(8);
                continue;
            }

            g_duolith_foreground_peer = NULL;
            portmanteau_runtime_enter_app(&g_duolith_mailbox,
                                          mothpad_duolith_suite_mothpad(&g_duolith_suite),
                                          &g_duolith_suite);
            pico_set_message("Mothpad foreground");
            pico_render();
            continue;
        }
#endif

        (void)picocalc_kbd_read_event(&key, &shift);
        pico_update_shift_state();
        if(g_screensaver_active && key >= 0)
        {
            pico_wake_screensaver();
            pico_render();
        }
        else if(g_screensaver_active &&
                absolute_time_diff_us(get_absolute_time(), g_screensaver_next_step) <= 0)
        {
            pico_step_screensaver();
        }
        else if(!g_screensaver_active && pico_handle_key(key, shift))
        {
            pico_reset_screensaver_timer();
            pico_schedule_recovery_write();
            g_cursor_visible = 1;
            g_next_cursor_blink = make_timeout_time_ms(450);
            pico_render();
        }
        else if(!g_screensaver_active && pico_handle_shift_arrow_joystick())
        {
            pico_reset_screensaver_timer();
            pico_schedule_recovery_write();
            g_cursor_visible = 1;
            g_next_cursor_blink = make_timeout_time_ms(450);
            pico_render();
        }
        else if(!g_screensaver_active && pico_handle_arrow_joystick_hold())
        {
            pico_reset_screensaver_timer();
            pico_schedule_recovery_write();
            g_cursor_visible = 1;
            g_next_cursor_blink = make_timeout_time_ms(450);
            pico_render();
        }
        else if(pico_recovery_write_ready())
        {
            pico_write_recovery_copy();
            if(g_screensaver_active) pico_render_screensaver();
            else pico_render();
        }
        else if(!g_screensaver_active &&
                g_mode == PICO_MODE_EDITING &&
                !g_mothpad.read_only &&
                absolute_time_diff_us(get_absolute_time(), g_next_cursor_blink) <= 0)
        {
            g_cursor_visible = !g_cursor_visible;
            g_next_cursor_blink = make_timeout_time_ms(450);
            pico_render();
        }
        else if(!g_screensaver_active &&
                g_mode == PICO_MODE_EDITING && g_message[0] && !pico_message_active())
        {
            g_message[0] = 0;
            pico_render();
        }
        else if(!g_screensaver_active &&
                absolute_time_diff_us(get_absolute_time(), g_next_battery_update) <= 0)
        {
            pico_update_battery();
            pico_render();
        }
        else if(!g_screensaver_active &&
                absolute_time_diff_us(get_absolute_time(), g_screensaver_due) <= 0)
        {
            pico_start_screensaver();
        }
        sleep_ms(8);
    }
}
