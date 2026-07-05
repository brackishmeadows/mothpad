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

static void test_tab_is_one_character(void)
{
    Mothpad m;
    moth_init(&m);

    CHECK(moth_insert_char(&m, '\t') == MOTH_OK);

    CHECK(m.text_len == 1);
    CHECK(m.text[0] == '\t');
    CHECK(moth_cursor_col(&m) == 1);
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
    FILE *bak = fopen(bak_path, "rb");
    CHECK(bak != NULL);
    if(bak) fclose(bak);

    moth_init(&loaded);
    CHECK(moth_load_file(&loaded, path) == MOTH_OK);
    CHECK(strcmp(loaded.text, "new words\n") == 0);

    remove(path);
    remove(bak_path);
    remove(tmp_path);
}

int main(void)
{
    test_insert_and_lines();
    test_backspace_joins_lines();
    test_delete_joins_lines();
    test_tab_is_one_character();
    test_join_path();
    test_vertical_movement_clamps_column();
    test_find_wraps();
    test_render_status_and_cursor();
    test_safe_save_load();

    if(failures)
    {
        fprintf(stderr, "%d test failure(s)\n", failures);
        return 1;
    }

    printf("mothpad C tests passed\n");
    return 0;
}
