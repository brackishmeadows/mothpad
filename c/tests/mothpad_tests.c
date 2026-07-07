#include "../src/mothpad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(expr) do { \
    if(!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++failures; \
    } \
} while(0)

static void test_temp_path(char *out, size_t out_size, const char *name)
{
    const char *tmp = getenv("TMP");
    if(!tmp || !tmp[0]) tmp = ".";
    snprintf(out, out_size, "%s/%s", tmp, name);
}

static void test_insert_and_lines(void)
{
    Mothpad m;
    moth_init(&m);

    CHECK(moth_insert_text(&m, "abc\ndef") == MOTH_OK);

    CHECK(strcmp(m.text, "abc\ndef") == 0);
    CHECK(m.line_count == 2);
    CHECK(moth_cursor_line(&m) == 1);
    CHECK(moth_cursor_col(&m) == 3);
    CHECK(m.dirty == 1);
}

static void test_backspace_joins_lines(void)
{
    Mothpad m;
    moth_init(&m);
    CHECK(moth_set_text(&m, "abc\ndef") == MOTH_OK);
    m.cursor = 4;

    moth_backspace(&m);

    CHECK(strcmp(m.text, "abcdef") == 0);
    CHECK(m.cursor == 3);
    CHECK(m.line_count == 1);
}

static void test_delete_joins_lines(void)
{
    Mothpad m;
    moth_init(&m);
    CHECK(moth_set_text(&m, "abc\ndef") == MOTH_OK);
    m.cursor = 3;

    moth_delete(&m);

    CHECK(strcmp(m.text, "abcdef") == 0);
    CHECK(m.cursor == 3);
    CHECK(m.line_count == 1);
}

static void test_undo_insert_restores_clean_text(void)
{
    Mothpad m;
    moth_init(&m);
    CHECK(moth_set_text(&m, "ab") == MOTH_OK);
    m.cursor = 1;

    CHECK(moth_insert_char(&m, 'X') == MOTH_OK);
    CHECK(strcmp(m.text, "aXb") == 0);
    CHECK(m.dirty == 1);

    CHECK(moth_undo(&m) == MOTH_OK);
    CHECK(strcmp(m.text, "ab") == 0);
    CHECK(m.cursor == 1);
    CHECK(m.dirty == 0);
}

static void test_undo_backspace_and_delete(void)
{
    Mothpad m;
    moth_init(&m);
    CHECK(moth_set_text(&m, "abc") == MOTH_OK);

    m.cursor = 2;
    moth_backspace(&m);
    CHECK(strcmp(m.text, "ac") == 0);
    CHECK(moth_undo(&m) == MOTH_OK);
    CHECK(strcmp(m.text, "abc") == 0);
    CHECK(m.cursor == 2);

    m.cursor = 1;
    moth_delete(&m);
    CHECK(strcmp(m.text, "ac") == 0);
    CHECK(moth_undo(&m) == MOTH_OK);
    CHECK(strcmp(m.text, "abc") == 0);
    CHECK(m.cursor == 1);
}

static void test_undo_groups_text_operations(void)
{
    Mothpad m;
    int start = 0;
    int end = 0;
    moth_init(&m);
    CHECK(moth_set_text(&m, "ab") == MOTH_OK);
    m.cursor = 1;

    CHECK(moth_insert_text(&m, "XYZ") == MOTH_OK);
    CHECK(strcmp(m.text, "aXYZb") == 0);
    CHECK(m.cursor == 4);

    CHECK(moth_undo(&m) == MOTH_OK);
    CHECK(strcmp(m.text, "ab") == 0);
    CHECK(m.cursor == 1);
    CHECK(m.dirty == 0);

    moth_select_range(&m, 0, 2);
    moth_delete_selection(&m);
    CHECK(strcmp(m.text, "") == 0);

    CHECK(moth_undo(&m) == MOTH_OK);
    CHECK(strcmp(m.text, "ab") == 0);
    CHECK(moth_has_selection(&m));
    moth_selection_bounds(&m, &start, &end);
    CHECK(start == 0);
    CHECK(end == 2);
    CHECK(m.cursor == 2);
}

static void test_undo_replace_selection_restores_selection(void)
{
    Mothpad m;
    int start = 0;
    int end = 0;

    moth_init(&m);
    CHECK(moth_set_text(&m, "abcdef") == MOTH_OK);
    moth_select_range(&m, 1, 4);

    CHECK(moth_insert_char(&m, 'X') == MOTH_OK);
    CHECK(strcmp(m.text, "aXef") == 0);
    CHECK(!moth_has_selection(&m));

    CHECK(moth_undo(&m) == MOTH_OK);
    CHECK(strcmp(m.text, "abcdef") == 0);
    CHECK(moth_has_selection(&m));
    moth_selection_bounds(&m, &start, &end);
    CHECK(start == 1);
    CHECK(end == 4);
    CHECK(m.cursor == 4);
}

static void test_selection_copy_delete_and_render(void)
{
    Mothpad m;
    char clip[16];
    int clip_len = 0;
    const MothCell *cell;

    moth_init(&m);
    CHECK(moth_set_text(&m, "abcdef") == MOTH_OK);
    m.cursor = 1;
    moth_begin_selection(&m);
    m.cursor = 4;
    moth_update_selection(&m);

    CHECK(moth_has_selection(&m));
    CHECK(moth_copy_selection(&m, clip, sizeof(clip), &clip_len) == MOTH_OK);
    CHECK(strcmp(clip, "bcd") == 0);
    CHECK(clip_len == 3);

    moth_render(&m);
    cell = moth_cell_at(&m, 1, MOTH_TEXT_FIRST_ROW);
    CHECK(cell && cell->ch == 'b' && (cell->flags & MOTH_CELL_SELECTION));
    CHECK(cell && cell->fg == 0 && cell->bg == 7);

    moth_delete_selection(&m);
    CHECK(strcmp(m.text, "aef") == 0);
    CHECK(m.cursor == 1);
    CHECK(!moth_has_selection(&m));

    moth_select_all(&m);
    CHECK(moth_has_selection(&m));
    CHECK(moth_copy_selection(&m, clip, sizeof(clip), &clip_len) == MOTH_OK);
    CHECK(strcmp(clip, "aef") == 0);

    moth_select_range(&m, 1, 1);
    CHECK(!moth_has_selection(&m));
}

static void test_tab_is_one_character_two_cells_wide(void)
{
    Mothpad m;
    moth_init(&m);

    CHECK(moth_insert_char(&m, '\t') == MOTH_OK);

    CHECK(m.text_len == 1);
    CHECK(m.text[0] == '\t');
    CHECK(moth_cursor_col(&m) == 2);
    moth_render(&m);
    CHECK(moth_cell_at(&m, 0, MOTH_TEXT_FIRST_ROW)->ch == ' ');
    CHECK(moth_cell_at(&m, 1, MOTH_TEXT_FIRST_ROW)->ch == ' ');
    CHECK((moth_cell_at(&m, 2, MOTH_TEXT_FIRST_ROW)->flags & MOTH_CELL_CURSOR) != 0);
}

static void test_join_path(void)
{
    char path[32];

    CHECK(moth_join_path(path, sizeof(path), "/", "mothpad.txt") == MOTH_OK);
    CHECK(strcmp(path, "/mothpad.txt") == 0);

    CHECK(moth_join_path(path, sizeof(path), "/notes", "today.txt") == MOTH_OK);
    CHECK(strcmp(path, "/notes/today.txt") == 0);

    CHECK(moth_join_path(path, sizeof(path), "/notes/", "today.txt") == MOTH_OK);
    CHECK(strcmp(path, "/notes/today.txt") == 0);

    CHECK(moth_join_path(path, 6, "/", "toolong.txt") == MOTH_ERR_FULL);
}

static void test_vertical_movement_clamps_column(void)
{
    Mothpad m;
    moth_init(&m);
    CHECK(moth_set_text(&m, "abcdef\nxy") == MOTH_OK);
    m.cursor = 6;
    m.preferred_col = 6;

    moth_cursor_down(&m);

    CHECK(moth_cursor_line(&m) == 1);
    CHECK(moth_cursor_col(&m) == 2);
}

static void test_find_wraps(void)
{
    Mothpad m;
    moth_init(&m);
    CHECK(moth_set_text(&m, "alpha beta alpha") == MOTH_OK);
    m.cursor = 8;

    CHECK(moth_find_next(&m, "alpha", 1) == 11);
    m.cursor = 12;
    CHECK(moth_find_next(&m, "beta", 1) == 6);
}

static void test_render_status_and_cursor(void)
{
    Mothpad m;
    const MothCell *cell;
    const MothCell *status_tail;
    moth_init(&m);
    CHECK(moth_set_text(&m, "hello") == MOTH_OK);
    m.cursor = 1;
    m.dirty = 1;

    moth_render(&m);

    cell = moth_cell_at(&m, 0, 0);
    CHECK(cell && cell->ch == '[' && (cell->flags & MOTH_CELL_STATUS));
    status_tail = moth_cell_at(&m, MOTH_COLS - 1, 0);
    CHECK(status_tail && status_tail->ch == ' ' && (status_tail->flags & MOTH_CELL_STATUS));
    cell = moth_cell_at(&m, 1, MOTH_TEXT_FIRST_ROW);
    CHECK(cell && cell->ch == 'e' && (cell->flags & MOTH_CELL_CURSOR));
    CHECK(cell && cell->fg == 7 && cell->bg == 0);
}

static void test_safe_save_load(void)
{
    Mothpad m;
    Mothpad loaded;
    char path[512];
    char bak_path[512];
    char tmp_path[512];

    test_temp_path(path, sizeof(path), "mothpad_test_save.txt");
    test_temp_path(bak_path, sizeof(bak_path), "mothpad_test_save.txt.bak");
    test_temp_path(tmp_path, sizeof(tmp_path), "mothpad_test_save.txt.tmp");

    moth_init(&m);
    CHECK(moth_set_text(&m, "saved words\n") == MOTH_OK);
    m.dirty = 1;

    CHECK(moth_save_file(&m, "") == MOTH_ERR_BAD_ARGUMENT);
    CHECK(m.dirty == 1);

    CHECK(moth_save_file(&m, path) == MOTH_OK);
    CHECK(m.dirty == 0);

    CHECK(moth_set_text(&m, "new words\n") == MOTH_OK);
    m.dirty = 1;
    CHECK(moth_save_file(&m, path) == MOTH_OK);
    m.cursor = m.text_len;
    CHECK(moth_insert_char(&m, '!') == MOTH_OK);
    CHECK(moth_save_file(&m, path) == MOTH_OK);
    CHECK(moth_undo(&m) == MOTH_OK);
    CHECK(strcmp(m.text, "new words\n!") == 0);

    FILE *bak = fopen(bak_path, "rb");
    CHECK(bak != NULL);
    if(bak) fclose(bak);

    moth_init(&loaded);
    CHECK(moth_load_file(&loaded, path) == MOTH_OK);
    CHECK(strcmp(loaded.text, "new words\n!") == 0);

    remove(path);
    remove(bak_path);
    remove(tmp_path);
}

static void test_save_without_backup(void)
{
    Mothpad m;
    FILE *bak;
    char path[512];
    char bak_path[512];
    char tmp_path[512];

    test_temp_path(path, sizeof(path), "mothpad_test_no_backup.txt");
    test_temp_path(bak_path, sizeof(bak_path), "mothpad_test_no_backup.txt.bak");
    test_temp_path(tmp_path, sizeof(tmp_path), "mothpad_test_no_backup.txt.tmp");

    remove(path);
    remove(bak_path);
    remove(tmp_path);

    moth_init(&m);
    CHECK(moth_set_text(&m, "first\n") == MOTH_OK);
    CHECK(moth_save_file_with_backup(&m, path, 1) == MOTH_OK);
    CHECK(moth_set_text(&m, "second\n") == MOTH_OK);
    CHECK(moth_save_file_with_backup(&m, path, 1) == MOTH_OK);
    bak = fopen(bak_path, "rb");
    CHECK(bak != NULL);
    if(bak) fclose(bak);

    CHECK(moth_set_text(&m, "third\n") == MOTH_OK);
    CHECK(moth_save_file_with_backup(&m, path, 0) == MOTH_OK);
    bak = fopen(bak_path, "rb");
    CHECK(bak == NULL);
    if(bak) fclose(bak);

    remove(path);
    remove(bak_path);
    remove(tmp_path);
}

static void test_soft_wrap_rendering(void)
{
    Mothpad m;

    moth_init(&m);
    CHECK(moth_set_text(&m, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") == MOTH_OK);
    m.soft_wrap = 1;
    moth_render(&m);

    CHECK(moth_cell_at(&m, 0, MOTH_TEXT_FIRST_ROW)->ch == 'a');
    CHECK(moth_cell_at(&m, 39, MOTH_TEXT_FIRST_ROW)->ch == 'N');
    CHECK(moth_cell_at(&m, 0, MOTH_TEXT_FIRST_ROW + 1)->ch == 'O');
}

static void test_soft_wrap_prefers_word_boundary(void)
{
    Mothpad m;

    moth_init(&m);
    CHECK(moth_set_text(&m, "one two three four five six seven eight nine ten") == MOTH_OK);
    m.soft_wrap = 1;
    moth_render(&m);

    CHECK(moth_cell_at(&m, 0, MOTH_TEXT_FIRST_ROW)->ch == 'o');
    CHECK(moth_cell_at(&m, 38, MOTH_TEXT_FIRST_ROW)->ch == 't');
    CHECK(moth_cell_at(&m, 39, MOTH_TEXT_FIRST_ROW)->ch == ' ');
    CHECK(moth_cell_at(&m, 0, MOTH_TEXT_FIRST_ROW + 1)->ch == 'n');
}

static void test_read_only_scroll_does_not_follow_cursor(void)
{
    Mothpad m;

    moth_init(&m);
    CHECK(moth_set_text(&m, "one\ntwo\nthree\nfour\n") == MOTH_OK);
    m.read_only = 1;
    moth_scroll_view(&m, 1);
    moth_render(&m);

    CHECK(moth_cell_at(&m, 0, MOTH_TEXT_FIRST_ROW)->ch == 't');
    CHECK((moth_cell_at(&m, 0, MOTH_TEXT_FIRST_ROW)->flags & MOTH_CELL_CURSOR) == 0);

    moth_cursor_to_view_top(&m);
    CHECK(moth_cursor_line(&m) == 1);
    CHECK(moth_cursor_col(&m) == 0);
}

static void test_read_only_soft_wrap_last_page_does_not_repeat_last_line(void)
{
    Mothpad m;

    moth_init(&m);
    CHECK(moth_set_text(&m, "one two three four five six seven eight nine ten") == MOTH_OK);
    m.soft_wrap = 1;
    m.read_only = 1;
    moth_scroll_view(&m, 1);
    moth_render(&m);

    CHECK(moth_cell_at(&m, 0, MOTH_TEXT_FIRST_ROW)->ch == 'n');
    CHECK(moth_cell_at(&m, 0, MOTH_TEXT_FIRST_ROW + 1)->ch == ' ');
}

static void test_recovery_file_does_not_mark_clean_or_clear_undo(void)
{
    Mothpad m;
    Mothpad loaded;
    char path[512];
    char recovery_path[512];
    char recovery_tmp_path[600];

    test_temp_path(path, sizeof(path), "mothpad_test_recovery.txt");
    test_temp_path(recovery_path, sizeof(recovery_path), "mothpad_recovery.txt");
    snprintf(recovery_tmp_path, sizeof(recovery_tmp_path), "%s.tmp", recovery_path);

    remove(path);
    remove(recovery_path);
    remove(recovery_tmp_path);

    moth_init(&m);
    CHECK(moth_set_text(&m, "base\n") == MOTH_OK);
    CHECK(moth_save_file(&m, path) == MOTH_OK);

    m.cursor = 0;
    CHECK(moth_insert_text(&m, "draft\n") == MOTH_OK);
    CHECK(m.dirty == 1);
    CHECK(m.undo_count > 0);

    CHECK(moth_write_recovery_file(&m, recovery_path) == MOTH_OK);
    CHECK(m.dirty == 1);
    CHECK(m.undo_count > 0);

    moth_init(&loaded);
    CHECK(moth_load_file(&loaded, recovery_path) == MOTH_OK);
    CHECK(strcmp(loaded.text, "draft\nbase\n") == 0);

    CHECK(moth_undo(&m) == MOTH_OK);
    CHECK(strcmp(m.text, "base\n") == 0);

    remove(path);
    remove(recovery_path);
    remove(recovery_tmp_path);
}

int main(void)
{
    test_insert_and_lines();
    test_backspace_joins_lines();
    test_delete_joins_lines();
    test_undo_insert_restores_clean_text();
    test_undo_backspace_and_delete();
    test_undo_groups_text_operations();
    test_undo_replace_selection_restores_selection();
    test_selection_copy_delete_and_render();
    test_tab_is_one_character_two_cells_wide();
    test_join_path();
    test_vertical_movement_clamps_column();
    test_find_wraps();
    test_render_status_and_cursor();
    test_safe_save_load();
    test_save_without_backup();
    test_soft_wrap_rendering();
    test_soft_wrap_prefers_word_boundary();
    test_read_only_scroll_does_not_follow_cursor();
    test_read_only_soft_wrap_last_page_does_not_repeat_last_line();
    test_recovery_file_does_not_mark_clean_or_clear_undo();

    if(failures)
    {
        fprintf(stderr, "%d test failure(s)\n", failures);
        return 1;
    }

    printf("mothpad C tests passed\n");
    return 0;
}
