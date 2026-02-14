#include <check.h>
#include <string.h>

#include "tmuxremote_terminal_state.h"

START_TEST(test_basic_render)
{
    tmuxremote_terminal_state state;
    tmuxremote_terminal_state_init(&state, 5, 20);

    const char* text = "hello\nworld";
    tmuxremote_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    tmuxremote_terminal_snapshot snap;
    ck_assert(tmuxremote_terminal_state_snapshot(&state, &snap));

    ck_assert_str_eq(snap.lines[0], "hello");
    ck_assert_str_eq(snap.lines[1], "world");

    tmuxremote_terminal_snapshot_free(&snap);
    tmuxremote_terminal_state_free(&state);
}
END_TEST

START_TEST(test_cursor_positioning)
{
    tmuxremote_terminal_state state;
    tmuxremote_terminal_state_init(&state, 6, 20);

    const char* text = "\x1b[4;2HChoose\x1b[5;2H1. Yes\x1b[6;2H2. No";
    tmuxremote_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    tmuxremote_terminal_snapshot snap;
    ck_assert(tmuxremote_terminal_state_snapshot(&state, &snap));

    ck_assert_str_eq(snap.lines[3], " Choose");
    ck_assert_str_eq(snap.lines[4], " 1. Yes");
    ck_assert_str_eq(snap.lines[5], " 2. No");

    tmuxremote_terminal_snapshot_free(&snap);
    tmuxremote_terminal_state_free(&state);
}
END_TEST

START_TEST(test_chunked_csi_sequence)
{
    tmuxremote_terminal_state state;
    tmuxremote_terminal_state_init(&state, 4, 20);

    const uint8_t part1[] = {0x1b, '[', '2', ';'};
    const uint8_t part2[] = {'1', 'H', 'O', 'K'};

    tmuxremote_terminal_state_feed(&state, part1, sizeof(part1));
    tmuxremote_terminal_state_feed(&state, part2, sizeof(part2));

    tmuxremote_terminal_snapshot snap;
    ck_assert(tmuxremote_terminal_state_snapshot(&state, &snap));

    ck_assert_str_eq(snap.lines[1], "OK");

    tmuxremote_terminal_snapshot_free(&snap);
    tmuxremote_terminal_state_free(&state);
}
END_TEST

START_TEST(test_resize_expands_cursor_addressable_area)
{
    tmuxremote_terminal_state state;
    tmuxremote_terminal_state_init(&state, 4, 10);

    const char* before = "\x1b[5;1HX";
    tmuxremote_terminal_state_feed(&state, (const uint8_t*)before, strlen(before));

    tmuxremote_terminal_snapshot snap_before;
    ck_assert(tmuxremote_terminal_state_snapshot(&state, &snap_before));
    ck_assert_str_eq(snap_before.lines[3], "X");
    tmuxremote_terminal_snapshot_free(&snap_before);

    tmuxremote_terminal_state_resize(&state, 8, 20);

    const char* after = "\x1b[5;1HY";
    tmuxremote_terminal_state_feed(&state, (const uint8_t*)after, strlen(after));

    tmuxremote_terminal_snapshot snap_after;
    ck_assert(tmuxremote_terminal_state_snapshot(&state, &snap_after));

    ck_assert_str_eq(snap_after.lines[3], "X");
    ck_assert_str_eq(snap_after.lines[4], "Y");

    tmuxremote_terminal_snapshot_free(&snap_after);
    tmuxremote_terminal_state_free(&state);
}
END_TEST

START_TEST(test_esc_charset_sequence_is_consumed)
{
    tmuxremote_terminal_state state;
    tmuxremote_terminal_state_init(&state, 4, 30);

    const char* text = "read /etc\x1b(B/passwd";
    tmuxremote_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    tmuxremote_terminal_snapshot snap;
    ck_assert(tmuxremote_terminal_state_snapshot(&state, &snap));
    ck_assert_str_eq(snap.lines[0], "read /etc/passwd");

    tmuxremote_terminal_snapshot_free(&snap);
    tmuxremote_terminal_state_free(&state);
}
END_TEST

START_TEST(test_csi_x_erases_characters_without_moving_cursor)
{
    tmuxremote_terminal_state state;
    tmuxremote_terminal_state_init(&state, 4, 40);

    const char* text =
        "\x1b[1;1HABCDEFGHIJ"
        "\x1b[1;3H\x1b[2X"
        "\x1b[1;3H12";
    tmuxremote_terminal_state_feed(&state, (const uint8_t*)text, strlen(text));

    tmuxremote_terminal_snapshot snap;
    ck_assert(tmuxremote_terminal_state_snapshot(&state, &snap));
    ck_assert_str_eq(snap.lines[0], "AB12EFGHIJ");

    tmuxremote_terminal_snapshot_free(&snap);
    tmuxremote_terminal_state_free(&state);
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
