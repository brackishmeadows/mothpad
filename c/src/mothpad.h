#ifndef MOTHPAD_H
#define MOTHPAD_H

#include <stddef.h>
#include <stdint.h>

#define MOTH_MAX_FILE_SIZE 65536
#define MOTH_MAX_LINES 4096
#define MOTH_MAX_UNDO 128

#define MOTH_COLS 40
#define MOTH_ROWS 26
#define MOTH_TOP_ROW 0
#define MOTH_BOTTOM_ROW (MOTH_ROWS - 1)
#define MOTH_TEXT_FIRST_ROW 1
#define MOTH_TEXT_ROWS (MOTH_ROWS - 2)

typedef enum {
    MOTH_OK = 0,
    MOTH_ERR_FULL,
    MOTH_ERR_LINE_LIMIT,
    MOTH_ERR_IO,
    MOTH_ERR_BAD_ARGUMENT,
} MothStatus;

typedef enum {
    MOTH_MODE_EDITING = 0,
    MOTH_MODE_MENU,
    MOTH_MODE_OPEN_FILE,
    MOTH_MODE_SAVE_AS,
    MOTH_MODE_FIND,
    MOTH_MODE_GOTO_LINE,
    MOTH_MODE_CONFIRM_QUIT,
    MOTH_MODE_ERROR,
} MothMode;

typedef enum {
    MOTH_EDIT_SAVED = 0,
    MOTH_EDIT_CANCELLED,
    MOTH_EDIT_UNCHANGED,
    MOTH_EDIT_ERROR,
} MothEditResult;

typedef enum {
    MOTH_CELL_CURSOR = 0x01,
    MOTH_CELL_STATUS = 0x02,
    MOTH_CELL_SELECTION = 0x04,
} MothCellFlags;

typedef struct {
    int start;
    int end;
} MothLine;

typedef struct {
    uint16_t ch;
    uint8_t fg;
    uint8_t bg;
    uint8_t flags;
} MothCell;

typedef enum {
    MOTH_UNDO_NONE = 0,
    MOTH_UNDO_INSERT_CHAR,
    MOTH_UNDO_DELETE_CHAR,
} MothUndoKind;

typedef struct {
    MothUndoKind kind;
    int pos;
    char ch;
    int cursor_before;
    int dirty_before;
    int selection_active_before;
    int selection_anchor_before;
    int selection_cursor_before;
    int group;
} MothUndoRecord;

typedef struct {
    char text[MOTH_MAX_FILE_SIZE + 1];
    int text_len;
    int cursor;
    int preferred_col;
    int scroll_line;
    int scroll_col;
    int dirty;
    int read_only;
    int soft_wrap;
    int selection_active;
    int selection_anchor;
    int selection_cursor;
    MothMode mode;
    char path[256];
    MothLine lines[MOTH_MAX_LINES];
    int line_count;
    MothUndoRecord undo[MOTH_MAX_UNDO];
    int undo_count;
    int undo_group;
    int undo_group_depth;
    int next_undo_group;
    MothCell cells[MOTH_COLS * MOTH_ROWS];
} Mothpad;

void moth_init(Mothpad *m);
MothStatus moth_set_text(Mothpad *m, const char *text);

#ifndef MOTHPAD_NO_STDIO
MothStatus moth_load_file(Mothpad *m, const char *path);
MothStatus moth_save_file(Mothpad *m, const char *path);
MothStatus moth_save_file_with_backup(Mothpad *m, const char *path, int keep_backup);
MothStatus moth_write_recovery_file(const Mothpad *m, const char *path);
#endif

MothStatus moth_insert_char(Mothpad *m, char ch);
MothStatus moth_insert_text(Mothpad *m, const char *text);
MothStatus moth_join_path(char *dest, size_t dest_size, const char *dir, const char *name);
MothStatus moth_undo(Mothpad *m);
void moth_begin_undo_group(Mothpad *m);
void moth_end_undo_group(Mothpad *m);
void moth_clear_selection(Mothpad *m);
void moth_begin_selection(Mothpad *m);
void moth_update_selection(Mothpad *m);
void moth_select_range(Mothpad *m, int start, int end);
void moth_select_all(Mothpad *m);
int moth_has_selection(const Mothpad *m);
void moth_selection_bounds(const Mothpad *m, int *start, int *end);
MothStatus moth_copy_selection(const Mothpad *m, char *dest, size_t dest_size, int *out_len);
void moth_delete_selection(Mothpad *m);
void moth_backspace(Mothpad *m);
void moth_delete(Mothpad *m);

void moth_cursor_left(Mothpad *m);
void moth_cursor_right(Mothpad *m);
void moth_cursor_up(Mothpad *m);
void moth_cursor_down(Mothpad *m);
void moth_cursor_home(Mothpad *m);
void moth_cursor_end(Mothpad *m);

int moth_cursor_line(const Mothpad *m);
int moth_cursor_col(const Mothpad *m);
int moth_find_next(const Mothpad *m, const char *needle, int from_cursor);

void moth_render(Mothpad *m);
const MothCell *moth_cell_at(const Mothpad *m, int x, int y);

#endif
