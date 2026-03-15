#include <check.h>
#include <string.h>

#include "pe_terminal_state.h"

START_TEST(test_basic_render)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 5, 20);

    const char* text = "hello\nworld";
    pe_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    pe_terminal_snapshot snap;
    ck_assert(pe_terminal_state_snapshot(&state, &snap));

    ck_assert_str_eq(snap.lines[0], "hello");
    ck_assert_str_eq(snap.lines[1], "world");

    pe_terminal_snapshot_free(&snap);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_cursor_positioning)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 6, 20);

    const char* text = "\x1b[4;2HChoose\x1b[5;2H1. Yes\x1b[6;2H2. No";
    pe_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    pe_terminal_snapshot snap;
    ck_assert(pe_terminal_state_snapshot(&state, &snap));

    ck_assert_str_eq(snap.lines[3], " Choose");
    ck_assert_str_eq(snap.lines[4], " 1. Yes");
    ck_assert_str_eq(snap.lines[5], " 2. No");

    pe_terminal_snapshot_free(&snap);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_chunked_csi_sequence)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 4, 20);

    const uint8_t part1[] = {0x1b, '[', '2', ';'};
    const uint8_t part2[] = {'1', 'H', 'O', 'K'};

    pe_terminal_state_feed(&state, part1, sizeof(part1));
    pe_terminal_state_feed(&state, part2, sizeof(part2));

    pe_terminal_snapshot snap;
    ck_assert(pe_terminal_state_snapshot(&state, &snap));

    ck_assert_str_eq(snap.lines[1], "OK");

    pe_terminal_snapshot_free(&snap);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_resize_expands_cursor_addressable_area)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 4, 10);

    const char* before = "\x1b[5;1HX";
    pe_terminal_state_feed(&state, (const uint8_t*)before, strlen(before));

    pe_terminal_snapshot snap_before;
    ck_assert(pe_terminal_state_snapshot(&state, &snap_before));
    ck_assert_str_eq(snap_before.lines[3], "X");
    pe_terminal_snapshot_free(&snap_before);

    pe_terminal_state_resize(&state, 8, 20);

    const char* after = "\x1b[5;1HY";
    pe_terminal_state_feed(&state, (const uint8_t*)after, strlen(after));

    pe_terminal_snapshot snap_after;
    ck_assert(pe_terminal_state_snapshot(&state, &snap_after));

    ck_assert_str_eq(snap_after.lines[3], "X");
    ck_assert_str_eq(snap_after.lines[4], "Y");

    pe_terminal_snapshot_free(&snap_after);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_esc_charset_sequence_is_consumed)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 4, 30);

    const char* text = "read /etc\x1b(B/passwd";
    pe_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    pe_terminal_snapshot snap;
    ck_assert(pe_terminal_state_snapshot(&state, &snap));
    ck_assert_str_eq(snap.lines[0], "read /etc/passwd");

    pe_terminal_snapshot_free(&snap);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_csi_x_erases_characters_without_moving_cursor)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 4, 40);

    const char* text =
        "\x1b[1;1HABCDEFGHIJ"
        "\x1b[1;3H\x1b[2X"
        "\x1b[1;3H12";
    pe_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    pe_terminal_snapshot snap;
    ck_assert(pe_terminal_state_snapshot(&state, &snap));
    ck_assert_str_eq(snap.lines[0], "AB12EFGHIJ");

    pe_terminal_snapshot_free(&snap);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_scroll_up_on_overflow)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 3, 20);

    const char* text = "A\nB\nC\nD";
    pe_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    pe_terminal_snapshot snap;
    ck_assert(pe_terminal_state_snapshot(&state, &snap));

    ck_assert_str_eq(snap.lines[0], "B");
    ck_assert_str_eq(snap.lines[1], "C");
    ck_assert_str_eq(snap.lines[2], "D");

    pe_terminal_snapshot_free(&snap);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_line_wrapping)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 4, 5);

    const char* text = "ABCDEFGH";
    pe_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    pe_terminal_snapshot snap;
    ck_assert(pe_terminal_state_snapshot(&state, &snap));

    ck_assert_str_eq(snap.lines[0], "ABCDE");
    ck_assert_str_eq(snap.lines[1], "FGH");

    pe_terminal_snapshot_free(&snap);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_clear_screen_csi_2J)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 4, 20);

    const char* text = "Hello\nWorld";
    pe_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    const char* clear = "\x1b[2J";
    pe_terminal_state_feed(&state, (const uint8_t*)clear, strlen(clear));

    pe_terminal_snapshot snap;
    ck_assert(pe_terminal_state_snapshot(&state, &snap));

    for (int i = 0; i < snap.rows; i++) {
        ck_assert_str_eq(snap.lines[i], "");
    }
    ck_assert_int_eq(snap.cursor_row, 0);
    ck_assert_int_eq(snap.cursor_col, 0);

    pe_terminal_snapshot_free(&snap);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_alt_screen_enter_exit)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 4, 20);

    /* Enter alt screen */
    const char* enter = "\x1b[?1049h";
    pe_terminal_state_feed(&state, (const uint8_t*)enter, strlen(enter));

    pe_terminal_snapshot snap1;
    ck_assert(pe_terminal_state_snapshot(&state, &snap1));
    ck_assert(snap1.alt_screen);
    pe_terminal_snapshot_free(&snap1);

    /* Exit alt screen */
    const char* exit_seq = "\x1b[?1049l";
    pe_terminal_state_feed(&state, (const uint8_t*)exit_seq, strlen(exit_seq));

    pe_terminal_snapshot snap2;
    ck_assert(pe_terminal_state_snapshot(&state, &snap2));
    ck_assert(!snap2.alt_screen);
    pe_terminal_snapshot_free(&snap2);

    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_tab_stops)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 4, 20);

    const char* text = "\tX";
    pe_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    pe_terminal_snapshot snap;
    ck_assert(pe_terminal_state_snapshot(&state, &snap));

    /* Tab advances to column 8, so X is at column 8 (0-indexed) */
    ck_assert(snap.lines[0][8] == 'X');

    pe_terminal_snapshot_free(&snap);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_backspace)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 4, 20);

    const char* text = "AB\bC";
    pe_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    pe_terminal_snapshot snap;
    ck_assert(pe_terminal_state_snapshot(&state, &snap));
    ck_assert_str_eq(snap.lines[0], "AC");

    pe_terminal_snapshot_free(&snap);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_cursor_save_restore)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 4, 20);

    /* Write "AB", save cursor, write "CD", restore cursor, write "XY" */
    const char* text = "AB\x1b[sCD\x1b[uXY";
    pe_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    pe_terminal_snapshot snap;
    ck_assert(pe_terminal_state_snapshot(&state, &snap));
    ck_assert_str_eq(snap.lines[0], "ABXY");

    pe_terminal_snapshot_free(&snap);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_csi_d_absolute_row)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 5, 20);

    /* ESC[3d moves cursor to row 3 (1-based), so row 2 (0-based) */
    const char* text = "\x1b[3dX";
    pe_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    pe_terminal_snapshot snap;
    ck_assert(pe_terminal_state_snapshot(&state, &snap));
    ck_assert(snap.lines[2][0] == 'X');

    pe_terminal_snapshot_free(&snap);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_csi_G_absolute_col)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 4, 20);

    /* ESC[5G moves cursor to column 5 (1-based), so column 4 (0-based) */
    const char* text = "\x1b[5GX";
    pe_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    pe_terminal_snapshot snap;
    ck_assert(pe_terminal_state_snapshot(&state, &snap));
    ck_assert(snap.lines[0][4] == 'X');

    pe_terminal_snapshot_free(&snap);
    pe_terminal_state_free(&state);
}
END_TEST

START_TEST(test_clear_line_csi_K_modes)
{
    pe_terminal_state state;
    pe_terminal_state_init(&state, 4, 10);

    /* Test ESC[0K: clear from cursor to end of line */
    const char* fill1 = "\x1b[1;1HABCDEFGHIJ";
    pe_terminal_state_feed(&state, (const uint8_t*)fill1, strlen(fill1));
    const char* clear_to_end = "\x1b[1;4H\x1b[0K";
    pe_terminal_state_feed(&state, (const uint8_t*)clear_to_end, strlen(clear_to_end));

    pe_terminal_snapshot snap1;
    ck_assert(pe_terminal_state_snapshot(&state, &snap1));
    ck_assert_str_eq(snap1.lines[0], "ABC");
    pe_terminal_snapshot_free(&snap1);

    /* Test ESC[1K: clear from start of line to cursor */
    const char* fill2 = "\x1b[2;1HABCDEFGHIJ";
    pe_terminal_state_feed(&state, (const uint8_t*)fill2, strlen(fill2));
    const char* clear_from_start = "\x1b[2;4H\x1b[1K";
    pe_terminal_state_feed(&state, (const uint8_t*)clear_from_start, strlen(clear_from_start));

    pe_terminal_snapshot snap2;
    ck_assert(pe_terminal_state_snapshot(&state, &snap2));
    /* Columns 0-3 cleared (1K clears up to and including cursor position) */
    ck_assert(snap2.lines[1][0] == ' ');
    ck_assert(snap2.lines[1][1] == ' ');
    ck_assert(snap2.lines[1][2] == ' ');
    ck_assert(snap2.lines[1][3] == ' ');
    ck_assert(snap2.lines[1][4] == 'E');
    pe_terminal_snapshot_free(&snap2);

    /* Test ESC[2K: clear entire line */
    const char* fill3 = "\x1b[3;1HABCDEFGHIJ";
    pe_terminal_state_feed(&state, (const uint8_t*)fill3, strlen(fill3));
    const char* clear_entire = "\x1b[3;4H\x1b[2K";
    pe_terminal_state_feed(&state, (const uint8_t*)clear_entire, strlen(clear_entire));

    pe_terminal_snapshot snap3;
    ck_assert(pe_terminal_state_snapshot(&state, &snap3));
    ck_assert_str_eq(snap3.lines[2], "");
    pe_terminal_snapshot_free(&snap3);

    pe_terminal_state_free(&state);
}
END_TEST

Suite* terminal_state_suite(void)
{
    Suite* s = suite_create("TerminalState");
    TCase* tc = tcase_create("Core");

    tcase_add_test(tc, test_basic_render);
    tcase_add_test(tc, test_cursor_positioning);
    tcase_add_test(tc, test_chunked_csi_sequence);
    tcase_add_test(tc, test_resize_expands_cursor_addressable_area);
    tcase_add_test(tc, test_esc_charset_sequence_is_consumed);
    tcase_add_test(tc, test_csi_x_erases_characters_without_moving_cursor);
    tcase_add_test(tc, test_scroll_up_on_overflow);
    tcase_add_test(tc, test_line_wrapping);
    tcase_add_test(tc, test_clear_screen_csi_2J);
    tcase_add_test(tc, test_alt_screen_enter_exit);
    tcase_add_test(tc, test_tab_stops);
    tcase_add_test(tc, test_backspace);
    tcase_add_test(tc, test_cursor_save_restore);
    tcase_add_test(tc, test_csi_d_absolute_row);
    tcase_add_test(tc, test_csi_G_absolute_col);
    tcase_add_test(tc, test_clear_line_csi_K_modes);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite* s = terminal_state_suite();
    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
