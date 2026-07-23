#include "mothpad.h"

#include <stdio.h>
#include <string.h>

#define MOTH_COLOR_TEXT 7
#define MOTH_COLOR_BACK 0
#define MOTH_COLOR_STATUS_FG 7
#define MOTH_COLOR_STATUS_BG 0

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

static void moth_clear_undo(Mothpad *m)
{
    if(m)
    {
        m->undo_count = 0;
        m->undo_group = 0;
        m->undo_group_depth = 0;
        m->next_undo_group = 1;
    }
}

void moth_clear_selection(Mothpad *m)
{
    if(!m) return;
    m->selection_active = 0;
    m->selection_anchor = m->cursor;
    m->selection_cursor = m->cursor;
}

void moth_begin_selection(Mothpad *m)
{
    if(!m) return;
    if(!m->selection_active)
    {
        m->selection_active = 1;
        m->selection_anchor = m->cursor;
        m->selection_cursor = m->cursor;
    }
}

void moth_update_selection(Mothpad *m)
{
    if(!m) return;
    if(!m->selection_active) moth_begin_selection(m);
    m->selection_cursor = m->cursor;
    if(m->selection_cursor == m->selection_anchor) moth_clear_selection(m);
}

void moth_select_range(Mothpad *m, int start, int end)
{
    if(!m) return;
    start = moth_clamp_int(start, 0, m->text_len);
    end = moth_clamp_int(end, 0, m->text_len);
    if(start == end)
    {
        m->cursor = end;
        moth_clear_selection(m);
        return;
    }
    m->selection_active = 1;
    m->selection_anchor = start;
    m->selection_cursor = end;
    m->cursor = end;
    m->preferred_col = moth_cursor_col(m);
}

void moth_select_all(Mothpad *m)
{
    if(!m) return;
    moth_select_range(m, 0, m->text_len);
}

int moth_has_selection(const Mothpad *m)
{
    return m && m->selection_active && m->selection_anchor != m->selection_cursor;
}

void moth_selection_bounds(const Mothpad *m, int *start, int *end)
{
    int a = 0;
    int b = 0;
    if(m && moth_has_selection(m))
    {
        a = m->selection_anchor;
        b = m->selection_cursor;
        if(a > b)
        {
            int tmp = a;
            a = b;
            b = tmp;
        }
    }
    if(start) *start = a;
    if(end) *end = b;
}

MothStatus moth_copy_selection(const Mothpad *m, char *dest, size_t dest_size, int *out_len)
{
    int start;
    int end;
    int len;

    if(!m || !dest || dest_size == 0) return MOTH_ERR_BAD_ARGUMENT;
    if(!moth_has_selection(m)) return MOTH_ERR_BAD_ARGUMENT;
    moth_selection_bounds(m, &start, &end);
    len = end - start;
    if((size_t)len >= dest_size) return MOTH_ERR_FULL;
    memcpy(dest, m->text + start, (size_t)len);
    dest[len] = 0;
    if(out_len) *out_len = len;
    return MOTH_OK;
}

static void moth_capture_undo_before(const Mothpad *m, MothUndoRecord *undo)
{
    if(!m || !undo) return;
    undo->cursor_before = m->cursor;
    undo->dirty_before = m->dirty;
    undo->selection_active_before = m->selection_active;
    undo->selection_anchor_before = m->selection_anchor;
    undo->selection_cursor_before = m->selection_cursor;
}

static void moth_restore_undo_before(Mothpad *m, const MothUndoRecord *undo)
{
    if(!m || !undo) return;

    if(undo->selection_active_before && undo->selection_anchor_before != undo->selection_cursor_before)
    {
        m->selection_active = 1;
        m->selection_anchor = moth_clamp_int(undo->selection_anchor_before, 0, m->text_len);
        m->selection_cursor = moth_clamp_int(undo->selection_cursor_before, 0, m->text_len);
        m->cursor = moth_clamp_int(m->selection_cursor, 0, m->text_len);
    }
    else
    {
        m->cursor = moth_clamp_int(undo->cursor_before, 0, m->text_len);
        moth_clear_selection(m);
    }

    m->preferred_col = moth_cursor_col(m);
    m->dirty = undo->dirty_before;
}

static void moth_push_undo(Mothpad *m, MothUndoRecord record)
{
    if(!m) return;
    record.group = m->undo_group;
    if(m->undo_count >= MOTH_MAX_UNDO)
    {
        memmove(m->undo,
                m->undo + 1,
                sizeof(m->undo[0]) * (MOTH_MAX_UNDO - 1));
        m->undo_count = MOTH_MAX_UNDO - 1;
    }
    m->undo[m->undo_count++] = record;
}

void moth_begin_undo_group(Mothpad *m)
{
    if(!m) return;
    if(m->undo_group)
    {
        ++m->undo_group_depth;
        return;
    }
    m->undo_group = m->next_undo_group++;
    m->undo_group_depth = 1;
    if(m->next_undo_group <= 0) m->next_undo_group = 1;
}

void moth_end_undo_group(Mothpad *m)
{
    if(!m) return;
    if(m->undo_group_depth > 0) --m->undo_group_depth;
    if(m->undo_group_depth == 0) m->undo_group = 0;
}

static MothStatus moth_raw_insert_char(Mothpad *m, int pos, char ch)
{
    if(!m) return MOTH_ERR_BAD_ARGUMENT;
    if(m->text_len >= MOTH_MAX_FILE_SIZE) return MOTH_ERR_FULL;
    pos = moth_clamp_int(pos, 0, m->text_len);

    memmove(m->text + pos + 1,
            m->text + pos,
            (size_t)(m->text_len - pos + 1));
    m->text[pos] = ch;
    ++m->text_len;
    if(!moth_rebuild_lines(m))
    {
        memmove(m->text + pos,
                m->text + pos + 1,
                (size_t)(m->text_len - pos));
        --m->text_len;
        moth_rebuild_lines(m);
        return MOTH_ERR_LINE_LIMIT;
    }
    return MOTH_OK;
}

static MothStatus moth_raw_delete_char(Mothpad *m, int pos)
{
    if(!m) return MOTH_ERR_BAD_ARGUMENT;
    if(pos < 0 || pos >= m->text_len) return MOTH_ERR_BAD_ARGUMENT;

    memmove(m->text + pos,
            m->text + pos + 1,
            (size_t)(m->text_len - pos));
    --m->text_len;
    if(!moth_rebuild_lines(m)) return MOTH_ERR_LINE_LIMIT;
    return MOTH_OK;
}

static int moth_effective_tab_width(const Mothpad *m)
{
    return (m && m->tab_width == 4) ? 4 : 2;
}

static int moth_char_visual_width(const Mothpad *m, char ch, int visual_col)
{
    if(ch == '\t')
    {
        int tab_width = moth_effective_tab_width(m);
        int next = ((visual_col / tab_width) + 1) * tab_width;
        return next - visual_col;
    }
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

static int moth_text_col_to_visual_col(const Mothpad *m, int line, int text_col)
{
    int visual_col = 0;
    int line_len;

    if(!m || m->line_count <= 0) return 0;
    line = moth_clamp_int(line, 0, m->line_count - 1);
    line_len = m->lines[line].end - m->lines[line].start;
    text_col = moth_clamp_int(text_col, 0, line_len);

    for(int col = 0; col < text_col; ++col)
    {
        visual_col += moth_char_visual_width(m, m->text[m->lines[line].start + col], visual_col);
    }

    return visual_col;
}

static int moth_text_col_from_visual_col(const Mothpad *m, int line, int visual_col)
{
    int col = 0;
    int line_len;
    int current_visual = 0;

    if(!m || m->line_count <= 0) return 0;
    line = moth_clamp_int(line, 0, m->line_count - 1);
    line_len = m->lines[line].end - m->lines[line].start;
    if(visual_col <= 0) return 0;

    while(col < line_len)
    {
        int width = moth_char_visual_width(m, m->text[m->lines[line].start + col], current_visual);
        if(visual_col < current_visual + width) break;
        current_visual += width;
        ++col;
    }

    return col;
}

static int moth_wrap_space(char ch)
{
    return ch == ' ' || ch == '\t';
}

static int moth_leading_indent_width(const Mothpad *m, int line)
{
    int visual_col = 0;
    int line_len;

    if(!m || line < 0 || line >= m->line_count) return 0;
    line_len = m->lines[line].end - m->lines[line].start;
    for(int col = 0; col < line_len; ++col)
    {
        char ch = m->text[m->lines[line].start + col];
        if(!moth_wrap_space(ch)) break;
        visual_col += moth_char_visual_width(m, ch, visual_col);
        if(visual_col >= MOTH_COLS - 1) return MOTH_COLS - 1;
    }

    return visual_col;
}

static int moth_wrap_row_indent(const Mothpad *m, int line, int start_col)
{
    if(start_col <= 0) return 0;
    return moth_leading_indent_width(m, line);
}

static int moth_pos_from_line_col(const Mothpad *m, int line, int col)
{
    int text_col;

    if(!m || m->line_count <= 0) return 0;
    line = moth_clamp_int(line, 0, m->line_count - 1);
    text_col = moth_text_col_from_visual_col(m, line, col);
    return m->lines[line].start + text_col;
}

static void moth_set_cursor_line_col(Mothpad *m, int line, int col)
{
    m->cursor = moth_pos_from_line_col(m, line, col);
}

static int moth_wrap_break_col(const Mothpad *m, int line, int start_col)
{
    MothLine text_line;
    int line_len;
    int screen_col;
    int visual_col;
    int ws_start = -1;
    int ws_end = -1;

    if(!m || line < 0 || line >= m->line_count) return start_col + MOTH_COLS;
    text_line = m->lines[line];
    line_len = text_line.end - text_line.start;
    if(start_col < 0) start_col = 0;
    if(start_col >= line_len) return line_len;

    screen_col = moth_wrap_row_indent(m, line, start_col);
    visual_col = moth_text_col_to_visual_col(m, line, start_col);

    for(int col = start_col; col < line_len; ++col)
    {
        char ch = m->text[text_line.start + col];
        int width = moth_char_visual_width(m, ch, visual_col);
        if(width < 1) width = 1;

        if(screen_col > moth_wrap_row_indent(m, line, start_col) &&
           screen_col + width > MOTH_COLS)
        {
            if(ws_start > start_col)
            {
                if(ws_end == ws_start + 1 && m->text[text_line.start + ws_start] == ' ')
                {
                    return ws_end;
                }
                return ws_start;
            }
            return col > start_col ? col : col + 1;
        }

        if(moth_wrap_space(ch))
        {
            if(ws_end == col)
            {
                ws_end = col + 1;
            }
            else
            {
                ws_start = col;
                ws_end = col + 1;
            }
        }

        screen_col += width;
        visual_col += width;
    }

    return line_len;
}

static int moth_visual_rows_for_line(const Mothpad *m, int line)
{
    int rows = 1;
    int col = 0;
    int line_len;

    if(!m || line < 0 || line >= m->line_count) return 1;
    line_len = m->lines[line].end - m->lines[line].start;
    while(col < line_len)
    {
        int next = moth_wrap_break_col(m, line, col);
        if(next >= line_len) break;
        if(next <= col) next = col + MOTH_COLS;
        col = next;
        ++rows;
    }

    return rows;
}

static int moth_visual_line_from_logical(const Mothpad *m, int logical_line, int col)
{
    int visual = 0;
    if(!m || m->line_count <= 0) return 0;
    logical_line = moth_clamp_int(logical_line, 0, m->line_count - 1);

    for(int i = 0; i < logical_line; ++i)
    {
        visual += moth_visual_rows_for_line(m, i);
    }

    if(col < 0) col = 0;
    for(int start_col = 0;;)
    {
        int next = moth_wrap_break_col(m, logical_line, start_col);
        if(col < next || next <= start_col) break;
        if(next >= m->lines[logical_line].end - m->lines[logical_line].start) break;
        start_col = next;
        ++visual;
    }
    return visual;
}

static int moth_wrap_start_col_for_visual(const Mothpad *m, int line, int visual_offset)
{
    int start_col = 0;

    if(visual_offset <= 0) return 0;
    for(int i = 0; i < visual_offset; ++i)
    {
        int next = moth_wrap_break_col(m, line, start_col);
        if(next <= start_col) break;
        start_col = next;
    }

    return start_col;
}

static int moth_wrap_start_col_for_text_col(const Mothpad *m, int line, int text_col)
{
    int start_col = 0;
    int line_len;

    if(!m || line < 0 || line >= m->line_count) return 0;
    line_len = m->lines[line].end - m->lines[line].start;
    text_col = moth_clamp_int(text_col, 0, line_len);
    while(start_col < line_len)
    {
        int next = moth_wrap_break_col(m, line, start_col);
        if(text_col < next || next <= start_col || next >= line_len) break;
        start_col = next;
    }

    return start_col;
}

static void moth_logical_from_visual_line(const Mothpad *m, int visual, int *out_line, int *out_start_col)
{
    int line = 0;
    int start_col = 0;
    if(!m || m->line_count <= 0)
    {
        if(out_line) *out_line = 0;
        if(out_start_col) *out_start_col = 0;
        return;
    }

    if(visual < 0) visual = 0;
    for(line = 0; line < m->line_count; ++line)
    {
        int rows = moth_visual_rows_for_line(m, line);
        if(visual < rows)
        {
            start_col = moth_wrap_start_col_for_visual(m, line, visual);
            break;
        }
        visual -= rows;
    }

    if(line >= m->line_count)
    {
        line = m->line_count - 1;
        start_col = moth_wrap_start_col_for_visual(m, line, moth_visual_rows_for_line(m, line) - 1);
    }

    if(out_line) *out_line = line;
    if(out_start_col) *out_start_col = start_col;
}

static int moth_try_logical_from_visual_line(const Mothpad *m, int visual, int *out_line, int *out_start_col)
{
    int line = 0;
    int start_col = 0;

    if(!m || m->line_count <= 0 || visual < 0) return 0;
    for(line = 0; line < m->line_count; ++line)
    {
        int rows = moth_visual_rows_for_line(m, line);
        if(visual < rows)
        {
            start_col = moth_wrap_start_col_for_visual(m, line, visual);
            if(out_line) *out_line = line;
            if(out_start_col) *out_start_col = start_col;
            return 1;
        }
        visual -= rows;
    }

    return 0;
}

static int moth_total_visual_rows(const Mothpad *m)
{
    int rows = 0;

    if(!m || m->line_count <= 0) return 1;
    for(int line = 0; line < m->line_count; ++line)
    {
        rows += moth_visual_rows_for_line(m, line);
    }

    return rows > 0 ? rows : 1;
}

static void moth_follow_cursor(Mothpad *m)
{
    int line = moth_cursor_line(m);
    int col = moth_cursor_col(m);

    if(m->soft_wrap)
    {
        int visual_line = moth_visual_line_from_logical(m, line, col);
        if(visual_line < m->scroll_line) m->scroll_line = visual_line;
        if(visual_line >= m->scroll_line + MOTH_TEXT_ROWS)
        {
            m->scroll_line = visual_line - MOTH_TEXT_ROWS + 1;
        }
        m->scroll_col = 0;
        if(m->scroll_line < 0) m->scroll_line = 0;
        return;
    }

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
    m->tab_width = 2;
    moth_clear_undo(m);
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
    moth_clear_undo(m);
    moth_clear_selection(m);

    if(!moth_rebuild_lines(m)) return MOTH_ERR_LINE_LIMIT;
    return MOTH_OK;
}

#ifndef MOTHPAD_NO_STDIO
MothStatus moth_load_file(Mothpad *m, const char *path)
{
    int ch;
    int line_count = 1;
    size_t byte_count = 0;

    if(!m || !path) return MOTH_ERR_BAD_ARGUMENT;

    FILE *file = fopen(path, "rb");
    if(!file) return MOTH_ERR_IO;

    while((ch = fgetc(file)) != EOF)
    {
        if(++byte_count > MOTH_MAX_FILE_SIZE)
        {
            fclose(file);
            return MOTH_ERR_FULL;
        }
        if(ch == '\n' && ++line_count > MOTH_MAX_LINES)
        {
            fclose(file);
            return MOTH_ERR_LINE_LIMIT;
        }
    }

    if(ferror(file) || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return MOTH_ERR_IO;
    }

    size_t read_count = fread(m->text, 1, byte_count, file);
    int read_failed = ferror(file) || read_count != byte_count;
    fclose(file);

    if(read_failed) return MOTH_ERR_IO;

    m->text[read_count] = 0;
    m->text_len = (int)read_count;
    m->cursor = 0;
    m->preferred_col = 0;
    m->scroll_line = 0;
    m->scroll_col = 0;
    m->dirty = 0;
    moth_clear_undo(m);
    moth_clear_selection(m);
    moth_copy_path(m->path, sizeof(m->path), path);

    if(!moth_rebuild_lines(m)) return MOTH_ERR_LINE_LIMIT;
    return MOTH_OK;
}

MothStatus moth_save_file_with_backup(Mothpad *m, const char *path, int keep_backup)
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

    if(keep_backup)
    {
        remove(bak_path);
        rename(path, bak_path);
    }
    else
    {
        remove(path);
        remove(bak_path);
    }

    if(rename(temp_path, path) != 0)
    {
        if(keep_backup) rename(bak_path, path);
        remove(temp_path);
        return MOTH_ERR_IO;
    }

    moth_copy_path(m->path, sizeof(m->path), path);
    m->dirty = 0;
    moth_clear_undo(m);
    moth_clear_selection(m);
    return MOTH_OK;
}

MothStatus moth_save_file(Mothpad *m, const char *path)
{
    return moth_save_file_with_backup(m, path, 1);
}

MothStatus moth_write_recovery_file(const Mothpad *m, const char *path)
{
    char temp_path[320];

    if(!m || !path || !path[0]) return MOTH_ERR_BAD_ARGUMENT;
    if(snprintf(temp_path, sizeof(temp_path), "%s.tmp", path) >= (int)sizeof(temp_path))
    {
        return MOTH_ERR_FULL;
    }

    FILE *file = fopen(temp_path, "wb");
    if(!file) return MOTH_ERR_IO;

    size_t wrote = fwrite(m->text, 1, (size_t)m->text_len, file);
    int close_failed = fclose(file);
    if(wrote != (size_t)m->text_len || close_failed)
    {
        remove(temp_path);
        return MOTH_ERR_IO;
    }

    remove(path);
    if(rename(temp_path, path) != 0)
    {
        remove(temp_path);
        return MOTH_ERR_IO;
    }

    return MOTH_OK;
}
#endif

MothStatus moth_insert_char(Mothpad *m, char ch)
{
    MothUndoRecord undo;
    int pos;
    int replace_selection;

    if(!m) return MOTH_ERR_BAD_ARGUMENT;
    memset(&undo, 0, sizeof(undo));
    moth_capture_undo_before(m, &undo);
    replace_selection = moth_has_selection(m);
    if(replace_selection)
    {
        moth_begin_undo_group(m);
        moth_delete_selection(m);
    }
    pos = m->cursor;
    undo.kind = MOTH_UNDO_DELETE_CHAR;
    undo.pos = pos;
    undo.ch = ch;

    MothStatus status = moth_raw_insert_char(m, pos, ch);
    if(status != MOTH_OK)
    {
        if(replace_selection) moth_end_undo_group(m);
        return status;
    }

    ++m->cursor;
    m->preferred_col = moth_cursor_col(m);
    m->dirty = 1;
    moth_clear_selection(m);
    moth_push_undo(m, undo);
    if(replace_selection) moth_end_undo_group(m);
    return MOTH_OK;
}

MothStatus moth_insert_text(Mothpad *m, const char *text)
{
    if(!m) return MOTH_ERR_BAD_ARGUMENT;
    if(!text) return MOTH_OK;

    moth_begin_undo_group(m);
    for(int i = 0; text[i]; ++i)
    {
        MothStatus status = moth_insert_char(m, text[i]);
        if(status != MOTH_OK)
        {
            moth_end_undo_group(m);
            return status;
        }
    }
    moth_end_undo_group(m);
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

MothStatus moth_undo(Mothpad *m)
{
    MothUndoRecord undo;
    MothUndoRecord restore;
    MothStatus status;
    int group;

    if(!m) return MOTH_ERR_BAD_ARGUMENT;
    if(m->undo_count <= 0) return MOTH_OK;
    moth_clear_selection(m);

    undo = m->undo[m->undo_count - 1];
    group = undo.group;
    restore = undo;

    do
    {
        undo = m->undo[--m->undo_count];
        restore = undo;

        if(undo.kind == MOTH_UNDO_INSERT_CHAR)
        {
            status = moth_raw_insert_char(m, undo.pos, undo.ch);
            if(status != MOTH_OK)
            {
                moth_push_undo(m, undo);
                return status;
            }
        }
        else if(undo.kind == MOTH_UNDO_DELETE_CHAR)
        {
            status = moth_raw_delete_char(m, undo.pos);
            if(status != MOTH_OK)
            {
                moth_push_undo(m, undo);
                return status;
            }
        }
        else
        {
            return MOTH_OK;
        }
    } while(group && m->undo_count > 0 && m->undo[m->undo_count - 1].group == group);

    moth_restore_undo_before(m, &restore);
    return MOTH_OK;
}

void moth_delete_selection(Mothpad *m)
{
    MothUndoRecord undo;
    int start;
    int end;
    int len;

    if(!m || !moth_has_selection(m)) return;
    memset(&undo, 0, sizeof(undo));
    moth_capture_undo_before(m, &undo);
    moth_selection_bounds(m, &start, &end);
    len = end - start;
    moth_clear_selection(m);
    m->cursor = start;
    moth_begin_undo_group(m);
    for(int i = 0; i < len; ++i)
    {
        MothUndoRecord item = undo;
        item.kind = MOTH_UNDO_INSERT_CHAR;
        item.pos = start;
        item.ch = m->text[start];
        if(moth_raw_delete_char(m, start) != MOTH_OK) break;
        m->dirty = 1;
        moth_push_undo(m, item);
    }
    moth_end_undo_group(m);
    m->cursor = start;
    m->preferred_col = moth_cursor_col(m);
}

void moth_backspace(Mothpad *m)
{
    MothUndoRecord undo;
    if(!m) return;
    if(moth_has_selection(m))
    {
        moth_delete_selection(m);
        return;
    }
    if(m->cursor <= 0) return;

    memset(&undo, 0, sizeof(undo));
    undo.kind = MOTH_UNDO_INSERT_CHAR;
    undo.pos = m->cursor - 1;
    undo.ch = m->text[m->cursor - 1];
    moth_capture_undo_before(m, &undo);

    if(moth_raw_delete_char(m, m->cursor - 1) != MOTH_OK) return;

    --m->cursor;
    m->preferred_col = moth_cursor_col(m);
    m->dirty = 1;
    moth_clear_selection(m);
    moth_push_undo(m, undo);
}

void moth_delete(Mothpad *m)
{
    MothUndoRecord undo;
    if(!m) return;
    if(moth_has_selection(m))
    {
        moth_delete_selection(m);
        return;
    }
    if(m->cursor >= m->text_len) return;

    memset(&undo, 0, sizeof(undo));
    undo.kind = MOTH_UNDO_INSERT_CHAR;
    undo.pos = m->cursor;
    undo.ch = m->text[m->cursor];
    moth_capture_undo_before(m, &undo);

    if(moth_raw_delete_char(m, m->cursor) != MOTH_OK) return;

    m->dirty = 1;
    moth_clear_selection(m);
    moth_push_undo(m, undo);
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
    if(m->soft_wrap)
    {
        int line = moth_cursor_line(m);
        int text_col = m->cursor - m->lines[line].start;
        int current_visual = moth_visual_line_from_logical(m, line, text_col);
        int current_start_col = moth_wrap_start_col_for_text_col(m, line, text_col);
        int current_start_visual = moth_text_col_to_visual_col(m, line, current_start_col);
        int screen_col = m->preferred_col - current_start_visual;
        int target_line = 0;
        int target_start_col = 0;

        if(screen_col < 0) screen_col = 0;
        if(!moth_try_logical_from_visual_line(m, current_visual - 1, &target_line, &target_start_col)) return;

        int target_start_visual = moth_text_col_to_visual_col(m, target_line, target_start_col);
        int target_end_col = moth_wrap_break_col(m, target_line, target_start_col);
        int target_col = moth_text_col_from_visual_col(m, target_line, target_start_visual + screen_col);
        target_col = moth_clamp_int(target_col, target_start_col, target_end_col);
        m->cursor = m->lines[target_line].start + target_col;
        m->preferred_col = target_start_visual + screen_col;
        return;
    }
    int line = moth_cursor_line(m);
    if(line > 0) moth_set_cursor_line_col(m, line - 1, m->preferred_col);
}

void moth_cursor_down(Mothpad *m)
{
    if(!m) return;
    if(m->soft_wrap)
    {
        int line = moth_cursor_line(m);
        int text_col = m->cursor - m->lines[line].start;
        int current_visual = moth_visual_line_from_logical(m, line, text_col);
        int current_start_col = moth_wrap_start_col_for_text_col(m, line, text_col);
        int current_start_visual = moth_text_col_to_visual_col(m, line, current_start_col);
        int screen_col = m->preferred_col - current_start_visual;
        int target_line = 0;
        int target_start_col = 0;

        if(screen_col < 0) screen_col = 0;
        if(!moth_try_logical_from_visual_line(m, current_visual + 1, &target_line, &target_start_col)) return;

        int target_start_visual = moth_text_col_to_visual_col(m, target_line, target_start_col);
        int target_end_col = moth_wrap_break_col(m, target_line, target_start_col);
        int target_col = moth_text_col_from_visual_col(m, target_line, target_start_visual + screen_col);
        target_col = moth_clamp_int(target_col, target_start_col, target_end_col);
        m->cursor = m->lines[target_line].start + target_col;
        m->preferred_col = target_start_visual + screen_col;
        return;
    }
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

void moth_scroll_view(Mothpad *m, int rows)
{
    int max_scroll;

    if(!m) return;
    if(m->soft_wrap)
    {
        max_scroll = moth_total_visual_rows(m) - 1;
    }
    else
    {
        max_scroll = m->line_count - 1;
    }
    if(max_scroll < 0) max_scroll = 0;

    m->scroll_line = moth_clamp_int(m->scroll_line + rows, 0, max_scroll);
    if(m->soft_wrap) m->scroll_col = 0;
}

void moth_cursor_to_view_top(Mothpad *m)
{
    int line;
    int start_col;

    if(!m || m->line_count <= 0) return;
    if(m->soft_wrap)
    {
        moth_logical_from_visual_line(m, m->scroll_line, &line, &start_col);
        line = moth_clamp_int(line, 0, m->line_count - 1);
        start_col = moth_clamp_int(start_col, 0, m->lines[line].end - m->lines[line].start);
        m->cursor = m->lines[line].start + start_col;
    }
    else
    {
        line = moth_clamp_int(m->scroll_line, 0, m->line_count - 1);
        m->cursor = moth_pos_from_line_col(m, line, m->scroll_col);
    }
    moth_clear_selection(m);
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
    return moth_text_col_to_visual_col(m, line, m->cursor - m->lines[line].start);
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
    int selection_start = 0;
    int selection_end = 0;
    int has_selection = moth_has_selection(m);
    if(has_selection) moth_selection_bounds(m, &selection_start, &selection_end);

    if(!m->read_only) moth_follow_cursor(m);
    moth_clear_cells(m);
    moth_fill_row(m, MOTH_TOP_ROW, MOTH_COLOR_STATUS_FG, MOTH_COLOR_STATUS_BG, MOTH_CELL_STATUS);

    char top[96];
    snprintf(top, sizeof(top), "%s%s", moth_display_path(m), m->dirty ? "  *" : "");
    moth_draw_text(m, 0, MOTH_TOP_ROW, top, MOTH_COLOR_STATUS_FG, MOTH_COLOR_STATUS_BG, MOTH_CELL_STATUS | MOTH_CELL_BOLD);

    for(int row = 0; row < MOTH_TEXT_ROWS; ++row)
    {
        int line_index = m->scroll_line + row;
        int line_start_col = m->scroll_col;
        int line_end_col;
        int screen_col = 0;
        int visual_col;
        int row_indent = 0;
        if(m->soft_wrap)
        {
            if(!moth_try_logical_from_visual_line(m, m->scroll_line + row, &line_index, &line_start_col)) break;
            row_indent = moth_wrap_row_indent(m, line_index, line_start_col);
        }
        else if(line_index >= m->line_count) break;
        else
        {
            line_start_col = moth_text_col_from_visual_col(m, line_index, m->scroll_col);
        }

        MothLine line = m->lines[line_index];
        int line_len = line.end - line.start;
        int visible = line_len - line_start_col;
        if(visible <= 0) continue;
        if(m->soft_wrap)
        {
            line_end_col = moth_wrap_break_col(m, line_index, line_start_col);
            visible = line_end_col - line_start_col;
        }
        else
        {
            visible = moth_min_int(visible, MOTH_COLS);
        }
        line_end_col = moth_min_int(line_start_col + visible, line_len);
        visual_col = moth_text_col_to_visual_col(m, line_index, line_start_col);
        screen_col = row_indent;

        for(int text_col = line_start_col; text_col < line_end_col && screen_col < MOTH_COLS; ++text_col)
        {
            int pos = line.start + text_col;
            char ch = m->text[pos];
            int width = moth_char_visual_width(m, ch, visual_col);
            if(width < 1) width = 1;
            if(ch == '\t') ch = ' ';
            if(has_selection && pos >= selection_start && pos < selection_end)
            {
                for(int i = 0; i < width && screen_col < MOTH_COLS; ++i)
                {
                    moth_put_cell(m, screen_col++, MOTH_TEXT_FIRST_ROW + row, (uint8_t)ch, MOTH_COLOR_BACK, MOTH_COLOR_TEXT, MOTH_CELL_SELECTION);
                }
            }
            else
            {
                for(int i = 0; i < width && screen_col < MOTH_COLS; ++i)
                {
                    moth_put_cell(m, screen_col++, MOTH_TEXT_FIRST_ROW + row, (uint8_t)ch, MOTH_COLOR_TEXT, MOTH_COLOR_BACK, 0);
                }
            }
            visual_col += width;
        }
    }

    int cursor_y;
    int cursor_x;
    if(m->soft_wrap)
    {
        int cursor_line = moth_cursor_line(m);
        int cursor_col = moth_cursor_col(m);
        int cursor_start_col = 0;
        moth_logical_from_visual_line(m, moth_visual_line_from_logical(m, cursor_line, cursor_col), NULL, &cursor_start_col);
        cursor_y = MOTH_TEXT_FIRST_ROW + moth_visual_line_from_logical(m, cursor_line, cursor_col) - m->scroll_line;
        cursor_x = moth_wrap_row_indent(m, cursor_line, cursor_start_col) +
                   cursor_col - moth_text_col_to_visual_col(m, cursor_line, cursor_start_col);
    }
    else
    {
        cursor_y = MOTH_TEXT_FIRST_ROW + moth_cursor_line(m) - m->scroll_line;
        cursor_x = moth_cursor_col(m) - m->scroll_col;
    }
    MothCell *cursor = m->read_only ? NULL : moth_mut_cell_at(m, cursor_x, cursor_y);
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
