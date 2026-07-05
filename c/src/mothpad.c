#include "mothpad.h"

#include <stdio.h>
#include <string.h>

#define MOTH_COLOR_TEXT 7
#define MOTH_COLOR_BACK 0
#define MOTH_COLOR_STATUS_FG 0
#define MOTH_COLOR_STATUS_BG 7

static int moth_min_int(int a, int b)
{
    return (a < b) ? a : b;
}

static int moth_clamp_int(int value, int low, int high)
{
    if(value < low) return low;
    if(value > high) return high;
    return value;
}

static void moth_copy_path(char *dest, size_t dest_size, const char *src)
{
    if(!dest || !dest_size) return;
    if(!src) src = "";
    snprintf(dest, dest_size, "%s", src);
}

static int moth_rebuild_lines(Mothpad *m)
{
    int line_index = 0;
    int start = 0;

    if(!m) return 0;

    for(int i = 0; i <= m->text_len; ++i)
    {
        if((i == m->text_len) || (m->text[i] == '\n'))
        {
            if(line_index >= MOTH_MAX_LINES) return 0;
            m->lines[line_index].start = start;
            m->lines[line_index].end = i;
            ++line_index;
            start = i + 1;
        }
    }

    if(line_index == 0)
    {
        m->lines[0].start = 0;
        m->lines[0].end = 0;
        line_index = 1;
    }

    m->line_count = line_index;
    m->cursor = moth_clamp_int(m->cursor, 0, m->text_len);
    return 1;
}

static int moth_line_from_pos(const Mothpad *m, int pos)
{
    if(!m || m->line_count <= 0) return 0;
    pos = moth_clamp_int(pos, 0, m->text_len);

    for(int i = 0; i < m->line_count; ++i)
    {
        if(pos <= m->lines[i].end) return i;
        if((i + 1) < m->line_count && pos < m->lines[i + 1].start) return i;
    }

    return m->line_count - 1;
}

static int moth_pos_from_line_col(const Mothpad *m, int line, int col)
{
    if(!m || m->line_count <= 0) return 0;
    line = moth_clamp_int(line, 0, m->line_count - 1);
    int line_len = m->lines[line].end - m->lines[line].start;
    col = moth_clamp_int(col, 0, line_len);
    return m->lines[line].start + col;
}

static void moth_set_cursor_line_col(Mothpad *m, int line, int col)
{
    m->cursor = moth_pos_from_line_col(m, line, col);
}

static void moth_follow_cursor(Mothpad *m)
{
    int line = moth_cursor_line(m);
    int col = moth_cursor_col(m);

    if(line < m->scroll_line) m->scroll_line = line;
    if(line >= m->scroll_line + MOTH_TEXT_ROWS)
    {
        m->scroll_line = line - MOTH_TEXT_ROWS + 1;
    }

    if(col < m->scroll_col) m->scroll_col = col;
    if(col >= m->scroll_col + MOTH_COLS)
    {
        m->scroll_col = col - MOTH_COLS + 1;
    }

    if(m->scroll_line < 0) m->scroll_line = 0;
    if(m->scroll_col < 0) m->scroll_col = 0;
}

static void moth_clear_cells(Mothpad *m)
{
    for(int i = 0; i < MOTH_COLS * MOTH_ROWS; ++i)
    {
        m->cells[i].ch = ' ';
        m->cells[i].fg = MOTH_COLOR_TEXT;
        m->cells[i].bg = MOTH_COLOR_BACK;
        m->cells[i].flags = 0;
    }
}

static MothCell *moth_mut_cell_at(Mothpad *m, int x, int y)
{
    if(!m || x < 0 || y < 0 || x >= MOTH_COLS || y >= MOTH_ROWS) return NULL;
    return &m->cells[y * MOTH_COLS + x];
}

static void moth_put_cell(Mothpad *m, int x, int y, uint16_t ch, uint8_t fg, uint8_t bg, uint8_t flags)
{
    MothCell *cell = moth_mut_cell_at(m, x, y);
    if(cell)
    {
        cell->ch = ch;
        cell->fg = fg;
        cell->bg = bg;
        cell->flags = flags;
    }
}

static void moth_draw_text(Mothpad *m, int x, int y, const char *text, uint8_t fg, uint8_t bg, uint8_t flags)
{
    if(!text) return;
    for(int i = 0; text[i] && (x + i) < MOTH_COLS; ++i)
    {
        moth_put_cell(m, x + i, y, (uint8_t)text[i], fg, bg, flags);
    }
}

static void moth_fill_row(Mothpad *m, int y, uint8_t fg, uint8_t bg, uint8_t flags)
{
    for(int x = 0; x < MOTH_COLS; ++x)
    {
        moth_put_cell(m, x, y, ' ', fg, bg, flags);
    }
}

static const char *moth_display_path(const Mothpad *m)
{
    return (m && m->path[0]) ? m->path : "[new]";
}

void moth_init(Mothpad *m)
{
    if(!m) return;
    memset(m, 0, sizeof(*m));
    m->mode = MOTH_MODE_EDITING;
    moth_rebuild_lines(m);
    moth_clear_cells(m);
}

MothStatus moth_set_text(Mothpad *m, const char *text)
{
    size_t len = text ? strlen(text) : 0;
    if(!m) return MOTH_ERR_BAD_ARGUMENT;
    if(len > MOTH_MAX_FILE_SIZE) return MOTH_ERR_FULL;

    if(len) memcpy(m->text, text, len);
    m->text[len] = 0;
    m->text_len = (int)len;
    m->cursor = 0;
    m->preferred_col = 0;
    m->scroll_line = 0;
    m->scroll_col = 0;
    m->dirty = 0;

    if(!moth_rebuild_lines(m)) return MOTH_ERR_LINE_LIMIT;
    return MOTH_OK;
}

#ifndef MOTHPAD_NO_STDIO
MothStatus moth_load_file(Mothpad *m, const char *path)
{
    if(!m || !path) return MOTH_ERR_BAD_ARGUMENT;

    FILE *file = fopen(path, "rb");
    if(!file) return MOTH_ERR_IO;

    size_t read_count = fread(m->text, 1, MOTH_MAX_FILE_SIZE + 1, file);
    int read_failed = ferror(file);
    fclose(file);

    if(read_failed) return MOTH_ERR_IO;
    if(read_count > MOTH_MAX_FILE_SIZE) return MOTH_ERR_FULL;

    m->text[read_count] = 0;
    m->text_len = (int)read_count;
    m->cursor = 0;
    m->preferred_col = 0;
    m->scroll_line = 0;
    m->scroll_col = 0;
    m->dirty = 0;
    moth_copy_path(m->path, sizeof(m->path), path);

    if(!moth_rebuild_lines(m)) return MOTH_ERR_LINE_LIMIT;
    return MOTH_OK;
}

MothStatus moth_save_file(Mothpad *m, const char *path)
{
    char temp_path[320];
    char bak_path[320];

    if(!m) return MOTH_ERR_BAD_ARGUMENT;
    if(!path || !path[0]) path = m->path;
    if(!path || !path[0]) return MOTH_ERR_BAD_ARGUMENT;

    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    snprintf(bak_path, sizeof(bak_path), "%s.bak", path);

    FILE *file = fopen(temp_path, "wb");
    if(!file) return MOTH_ERR_IO;

    size_t wrote = fwrite(m->text, 1, (size_t)m->text_len, file);
    int close_failed = fclose(file);
    if(wrote != (size_t)m->text_len || close_failed)
    {
        remove(temp_path);
        return MOTH_ERR_IO;
    }

    remove(bak_path);
    rename(path, bak_path);

    if(rename(temp_path, path) != 0)
    {
        rename(bak_path, path);
        remove(temp_path);
        return MOTH_ERR_IO;
    }

    moth_copy_path(m->path, sizeof(m->path), path);
    m->dirty = 0;
    return MOTH_OK;
}
#endif

MothStatus moth_insert_char(Mothpad *m, char ch)
{
    if(!m) return MOTH_ERR_BAD_ARGUMENT;
    if(m->text_len >= MOTH_MAX_FILE_SIZE) return MOTH_ERR_FULL;

    memmove(m->text + m->cursor + 1,
            m->text + m->cursor,
            (size_t)(m->text_len - m->cursor + 1));
    m->text[m->cursor] = ch;
    ++m->cursor;
    ++m->text_len;
    m->preferred_col = moth_cursor_col(m);
    m->dirty = 1;

    if(!moth_rebuild_lines(m)) return MOTH_ERR_LINE_LIMIT;
    return MOTH_OK;
}

MothStatus moth_insert_text(Mothpad *m, const char *text)
{
    if(!m) return MOTH_ERR_BAD_ARGUMENT;
    if(!text) return MOTH_OK;

    for(int i = 0; text[i]; ++i)
    {
        MothStatus status = moth_insert_char(m, text[i]);
        if(status != MOTH_OK) return status;
    }
    return MOTH_OK;
}

MothStatus moth_join_path(char *dest, size_t dest_size, const char *dir, const char *name)
{
    int wrote;

    if(!dest || dest_size == 0 || !name || !name[0]) return MOTH_ERR_BAD_ARGUMENT;
    if(!dir || !dir[0]) dir = "/";

    if(strcmp(dir, "/") == 0)
    {
        wrote = snprintf(dest, dest_size, "/%s", name);
    }
    else
    {
        size_t len = strlen(dir);
        wrote = snprintf(dest, dest_size, "%s%s%s", dir, (dir[len - 1] == '/') ? "" : "/", name);
    }

    if(wrote < 0 || (size_t)wrote >= dest_size) return MOTH_ERR_FULL;
    return MOTH_OK;
}

void moth_backspace(Mothpad *m)
{
    if(!m || m->cursor <= 0) return;
    memmove(m->text + m->cursor - 1,
            m->text + m->cursor,
            (size_t)(m->text_len - m->cursor + 1));
    --m->cursor;
    --m->text_len;
    m->preferred_col = moth_cursor_col(m);
    m->dirty = 1;
    moth_rebuild_lines(m);
}

void moth_delete(Mothpad *m)
{
    if(!m || m->cursor >= m->text_len) return;
    memmove(m->text + m->cursor,
            m->text + m->cursor + 1,
            (size_t)(m->text_len - m->cursor));
    --m->text_len;
    m->dirty = 1;
    moth_rebuild_lines(m);
}

void moth_cursor_left(Mothpad *m)
{
    if(!m) return;
    if(m->cursor > 0) --m->cursor;
    m->preferred_col = moth_cursor_col(m);
}

void moth_cursor_right(Mothpad *m)
{
    if(!m) return;
    if(m->cursor < m->text_len) ++m->cursor;
    m->preferred_col = moth_cursor_col(m);
}

void moth_cursor_up(Mothpad *m)
{
    if(!m) return;
    int line = moth_cursor_line(m);
    if(line > 0) moth_set_cursor_line_col(m, line - 1, m->preferred_col);
}

void moth_cursor_down(Mothpad *m)
{
    if(!m) return;
    int line = moth_cursor_line(m);
    if(line < m->line_count - 1) moth_set_cursor_line_col(m, line + 1, m->preferred_col);
}

void moth_cursor_home(Mothpad *m)
{
    if(!m) return;
    moth_set_cursor_line_col(m, moth_cursor_line(m), 0);
    m->preferred_col = 0;
}

void moth_cursor_end(Mothpad *m)
{
    if(!m) return;
    int line = moth_cursor_line(m);
    m->cursor = m->lines[line].end;
    m->preferred_col = moth_cursor_col(m);
}

int moth_cursor_line(const Mothpad *m)
{
    return moth_line_from_pos(m, m ? m->cursor : 0);
}

int moth_cursor_col(const Mothpad *m)
{
    int line = moth_cursor_line(m);
    if(!m || m->line_count <= 0) return 0;
    return m->cursor - m->lines[line].start;
}

int moth_find_next(const Mothpad *m, const char *needle, int from_cursor)
{
    if(!m || !needle || !needle[0]) return -1;

    int start = from_cursor ? m->cursor : 0;
    if(start < 0 || start > m->text_len) start = 0;

    const char *hit = strstr(m->text + start, needle);
    if(!hit && start > 0) hit = strstr(m->text, needle);
    if(!hit) return -1;

    return (int)(hit - m->text);
}

void moth_render(Mothpad *m)
{
    if(!m) return;

    moth_follow_cursor(m);
    moth_clear_cells(m);
    moth_fill_row(m, MOTH_TOP_ROW, MOTH_COLOR_STATUS_FG, MOTH_COLOR_STATUS_BG, MOTH_CELL_STATUS);
    moth_fill_row(m, MOTH_BOTTOM_ROW, MOTH_COLOR_STATUS_FG, MOTH_COLOR_STATUS_BG, MOTH_CELL_STATUS);

    char top[96];
    snprintf(top, sizeof(top), "%s%s", moth_display_path(m), m->dirty ? "  MOD" : "");
    moth_draw_text(m, 0, MOTH_TOP_ROW, top, MOTH_COLOR_STATUS_FG, MOTH_COLOR_STATUS_BG, MOTH_CELL_STATUS);

    for(int row = 0; row < MOTH_TEXT_ROWS; ++row)
    {
        int line_index = m->scroll_line + row;
        if(line_index >= m->line_count) break;

        MothLine line = m->lines[line_index];
        int line_len = line.end - line.start;
        int visible = line_len - m->scroll_col;
        if(visible <= 0) continue;
        visible = moth_min_int(visible, MOTH_COLS);

        for(int col = 0; col < visible; ++col)
        {
            char ch = m->text[line.start + m->scroll_col + col];
            if(ch == '\t') ch = ' ';
            moth_put_cell(m, col, MOTH_TEXT_FIRST_ROW + row, (uint8_t)ch, MOTH_COLOR_TEXT, MOTH_COLOR_BACK, 0);
        }
    }

    char bottom[80];
    snprintf(bottom, sizeof(bottom), "Ln %02d Col %02d", moth_cursor_line(m) + 1, moth_cursor_col(m) + 1);
    moth_draw_text(m, 0, MOTH_BOTTOM_ROW, bottom, MOTH_COLOR_STATUS_FG, MOTH_COLOR_STATUS_BG, MOTH_CELL_STATUS);

    int cursor_y = MOTH_TEXT_FIRST_ROW + moth_cursor_line(m) - m->scroll_line;
    int cursor_x = moth_cursor_col(m) - m->scroll_col;
    MothCell *cursor = moth_mut_cell_at(m, cursor_x, cursor_y);
    if(cursor)
    {
        cursor->flags |= MOTH_CELL_CURSOR;
    }
}

const MothCell *moth_cell_at(const Mothpad *m, int x, int y)
{
    if(!m || x < 0 || y < 0 || x >= MOTH_COLS || y >= MOTH_ROWS) return NULL;
    return &m->cells[y * MOTH_COLS + x];
}
