#include <check.h>
#include <string.h>
#include <stdlib.h>
#include "nabtoshell_pattern_engine.h"
#include "nabtoshell_pattern_config.h"

static nabtoshell_pattern_engine engine;
static nabtoshell_pattern_config *config;

static const char *test_config_json =
    "{"
    "  \"version\": 1,"
    "  \"agents\": {"
    "    \"test\": {"
    "      \"name\": \"Test\","
    "      \"patterns\": [{"
    "        \"id\": \"yn\","
    "        \"type\": \"yes_no\","
    "        \"regex\": \"Continue\\\\? \\\\(y/n\\\\)\","
    "        \"actions\": ["
    "          {\"label\": \"Yes\", \"keys\": \"y\"},"
    "          {\"label\": \"No\", \"keys\": \"n\"}"
    "        ]"
    "      }]"
    "    }"
    "  }"
    "}";

static void setup(void)
{
    nabtoshell_pattern_engine_init(&engine);
    config = nabtoshell_pattern_config_parse(test_config_json, strlen(test_config_json));
    nabtoshell_pattern_engine_load_config(&engine, config);
}

static void teardown(void)
{
    nabtoshell_pattern_engine_free(&engine);
    nabtoshell_pattern_config_free(config);
    config = NULL;
}

static void feed_string(nabtoshell_pattern_engine *e, const char *s)
{
    nabtoshell_pattern_engine_feed(e, (const uint8_t *)s, strlen(s));
}

START_TEST(test_full_pipeline)
{
    nabtoshell_pattern_engine_select_agent(&engine, "test");

    // Feed ANSI-colored prompt: ESC[1m Continue? (y/n) ESC[0m
    uint8_t bytes[64];
    int n = 0;
    bytes[n++] = 0x1B; bytes[n++] = 0x5B; bytes[n++] = 0x31; bytes[n++] = 0x6D;
    memcpy(bytes+n, "Continue? (y/n)", 15); n += 15;
    bytes[n++] = 0x1B; bytes[n++] = 0x5B; bytes[n++] = 0x30; bytes[n++] = 0x6D;

    nabtoshell_pattern_engine_feed(&engine, bytes, n);

    ck_assert_ptr_nonnull(engine.active_match);
    ck_assert_str_eq(engine.active_match->id, "yn");
    ck_assert_int_eq(engine.active_match->action_count, 2);
}
END_TEST

START_TEST(test_no_match_without_agent)
{
    // Don't select an agent
    feed_string(&engine, "Continue? (y/n)");
    ck_assert_ptr_null(engine.active_match);
}
END_TEST

START_TEST(test_auto_dismiss)
{
    nabtoshell_pattern_engine_select_agent(&engine, "test");
    feed_string(&engine, "Continue? (y/n)");
    ck_assert_ptr_nonnull(engine.active_match);

    // Feed enough filler to push prompt out of match window
    char filler[2200];
    memset(filler, 'x', 2100);
    filler[2100] = '\0';
    feed_string(&engine, filler);

    ck_assert_ptr_null(engine.active_match);
}
END_TEST

START_TEST(test_manual_dismiss)
{
    nabtoshell_pattern_engine_select_agent(&engine, "test");
    feed_string(&engine, "Continue? (y/n)");
    ck_assert_ptr_nonnull(engine.active_match);

    nabtoshell_pattern_engine_dismiss(&engine);
    ck_assert_ptr_null(engine.active_match);

    // Feeding the same pattern again should not re-trigger
    feed_string(&engine, "Continue? (y/n)");
    ck_assert_ptr_null(engine.active_match);
}
END_TEST

START_TEST(test_dismiss_resets_after_new_content)
{
    nabtoshell_pattern_engine_select_agent(&engine, "test");
    feed_string(&engine, "Continue? (y/n)");
    nabtoshell_pattern_engine_dismiss(&engine);

    // Feed enough new content to clear user-dismiss (2000 chars)
    char filler[2200];
    memset(filler, 'z', 2100);
    filler[2100] = '\0';
    feed_string(&engine, filler);

    // Now the pattern should match again
    feed_string(&engine, "Continue? (y/n)");
    ck_assert_ptr_nonnull(engine.active_match);
}
END_TEST

START_TEST(test_agent_switching)
{
    const char *switch_json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"a\": {"
        "      \"name\": \"Agent A\","
        "      \"patterns\": [{"
        "        \"id\": \"pa\","
        "        \"type\": \"yes_no\","
        "        \"regex\": \"prompt-a\","
        "        \"actions\": [{\"label\": \"Y\", \"keys\": \"y\"}, {\"label\": \"N\", \"keys\": \"n\"}]"
        "      }]"
        "    },"
        "    \"b\": {"
        "      \"name\": \"Agent B\","
        "      \"patterns\": [{"
        "        \"id\": \"pb\","
        "        \"type\": \"yes_no\","
        "        \"regex\": \"prompt-b\","
        "        \"actions\": [{\"label\": \"Y\", \"keys\": \"y\"}, {\"label\": \"N\", \"keys\": \"n\"}]"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *cfg2 = nabtoshell_pattern_config_parse(switch_json, strlen(switch_json));
    nabtoshell_pattern_engine eng2;
    nabtoshell_pattern_engine_init(&eng2);
    nabtoshell_pattern_engine_load_config(&eng2, cfg2);

    nabtoshell_pattern_engine_select_agent(&eng2, "a");
    feed_string(&eng2, "prompt-a");
    ck_assert_ptr_nonnull(eng2.active_match);
    ck_assert_str_eq(eng2.active_match->id, "pa");

    nabtoshell_pattern_engine_select_agent(&eng2, "b");
    ck_assert_ptr_null(eng2.active_match);

    feed_string(&eng2, "prompt-b");
    ck_assert_ptr_nonnull(eng2.active_match);
    ck_assert_str_eq(eng2.active_match->id, "pb");

    // prompt-a should not match under agent b
    nabtoshell_pattern_engine_dismiss(&eng2);
    char filler[2200];
    memset(filler, 'z', 2100);
    filler[2100] = '\0';
    feed_string(&eng2, filler);
    feed_string(&eng2, "prompt-a");
    ck_assert_ptr_null(eng2.active_match);

    nabtoshell_pattern_engine_free(&eng2);
    nabtoshell_pattern_config_free(cfg2);
}
END_TEST

START_TEST(test_reset)
{
    nabtoshell_pattern_engine_select_agent(&engine, "test");
    feed_string(&engine, "Continue? (y/n)");
    ck_assert_ptr_nonnull(engine.active_match);

    nabtoshell_pattern_engine_reset(&engine);
    ck_assert_ptr_null(engine.active_match);
}
END_TEST

START_TEST(test_select_agent_off)
{
    nabtoshell_pattern_engine_select_agent(&engine, "test");
    feed_string(&engine, "Continue? (y/n)");
    ck_assert_ptr_nonnull(engine.active_match);

    nabtoshell_pattern_engine_select_agent(&engine, NULL);
    ck_assert_ptr_null(engine.active_match);
    ck_assert_ptr_null(engine.active_agent);

    // Should not match with agent off
    feed_string(&engine, "Continue? (y/n)");
    ck_assert_ptr_null(engine.active_match);
}
END_TEST

START_TEST(test_select_agent_evaluates_existing_buffer)
{
    nabtoshell_pattern_engine_select_agent(&engine, "test");
    feed_string(&engine, "Continue? (y/n)");
    ck_assert_ptr_nonnull(engine.active_match);

    // Dismiss, switch off, switch on: should re-evaluate
    nabtoshell_pattern_engine_dismiss(&engine);
    nabtoshell_pattern_engine_select_agent(&engine, NULL);
    ck_assert_ptr_null(engine.active_match);

    nabtoshell_pattern_engine_select_agent(&engine, "test");
    ck_assert_ptr_nonnull(engine.active_match);
    ck_assert_str_eq(engine.active_match->id, "yn");
}
END_TEST

START_TEST(test_buffer_populated_without_agent)
{
    // Feed data with NO agent selected
    feed_string(&engine, "Continue? (y/n)");
    ck_assert_ptr_null(engine.active_match);

    // Select agent: re-evaluation should find the buffered prompt
    nabtoshell_pattern_engine_select_agent(&engine, "test");
    ck_assert_ptr_nonnull(engine.active_match);
    ck_assert_str_eq(engine.active_match->id, "yn");
}
END_TEST

START_TEST(test_buffer_populated_without_agent_ansi)
{
    // Feed ANSI-escaped data with no agent
    uint8_t bytes[64];
    int n = 0;
    bytes[n++] = 0x1B; bytes[n++] = 0x5B; bytes[n++] = 0x31; bytes[n++] = 0x6D;
    memcpy(bytes+n, "Continue? (y/n)", 15); n += 15;
    bytes[n++] = 0x1B; bytes[n++] = 0x5B; bytes[n++] = 0x30; bytes[n++] = 0x6D;

    nabtoshell_pattern_engine_feed(&engine, bytes, n);
    ck_assert_ptr_null(engine.active_match);

    nabtoshell_pattern_engine_select_agent(&engine, "test");
    ck_assert_ptr_nonnull(engine.active_match);
    ck_assert_str_eq(engine.active_match->id, "yn");
}
END_TEST

START_TEST(test_auto_detect_claude_code)
{
    const char *auto_json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"claude-code\": {"
        "      \"name\": \"Claude Code\","
        "      \"patterns\": []"
        "    }"
        "  }"
        "}";
    nabtoshell_pattern_config *acfg = nabtoshell_pattern_config_parse(auto_json, strlen(auto_json));
    nabtoshell_pattern_engine ae;
    nabtoshell_pattern_engine_init(&ae);
    nabtoshell_pattern_engine_load_config(&ae, acfg);

    const char *text = "Welcome to Claude Code v1.0";
    nabtoshell_pattern_engine_auto_detect(&ae, text, strlen(text));
    ck_assert_ptr_nonnull(ae.active_agent);
    ck_assert_str_eq(ae.active_agent, "claude-code");

    nabtoshell_pattern_engine_free(&ae);
    nabtoshell_pattern_config_free(acfg);
}
END_TEST

START_TEST(test_auto_detect_does_not_override)
{
    const char *auto_json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"claude-code\": {\"name\": \"Claude Code\", \"patterns\": []},"
        "    \"aider\": {\"name\": \"Aider\", \"patterns\": []}"
        "  }"
        "}";
    nabtoshell_pattern_config *acfg = nabtoshell_pattern_config_parse(auto_json, strlen(auto_json));
    nabtoshell_pattern_engine ae;
    nabtoshell_pattern_engine_init(&ae);
    nabtoshell_pattern_engine_load_config(&ae, acfg);

    nabtoshell_pattern_engine_select_agent(&ae, "aider");
    const char *text = "Welcome to Claude Code v1.0";
    nabtoshell_pattern_engine_auto_detect(&ae, text, strlen(text));
    ck_assert_str_eq(ae.active_agent, "aider");

    nabtoshell_pattern_engine_free(&ae);
    nabtoshell_pattern_config_free(acfg);
}
END_TEST

// Full pipeline tests with cursor positioning and colors

START_TEST(test_full_pipeline_with_cursor_positioning)
{
    const char *menu_json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"claude-code\": {"
        "      \"name\": \"Claude Code\","
        "      \"patterns\": [{"
        "        \"id\": \"numbered_prompt\","
        "        \"type\": \"numbered_menu\","
        "        \"regex\": \"Do you want to (?:proceed|continue)\\\\?\\\\n.*1\\\\. .+\\\\n.*2\\\\. .+\","
        "        \"multi_line\": true,"
        "        \"action_template\": {\"keys\": \"{number}\\n\"}"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *mcfg = nabtoshell_pattern_config_parse(menu_json, strlen(menu_json));
    nabtoshell_pattern_engine me;
    nabtoshell_pattern_engine_init(&me);
    nabtoshell_pattern_engine_load_config(&me, mcfg);
    nabtoshell_pattern_engine_select_agent(&me, "claude-code");

    // CSI cursor positioning
    uint8_t bytes[128];
    int n = 0;
    // ESC[20;1H
    bytes[n++] = 0x1B; bytes[n++] = 0x5B; bytes[n++] = 0x32; bytes[n++] = 0x30; bytes[n++] = 0x3B; bytes[n++] = 0x31; bytes[n++] = 0x48;
    memcpy(bytes+n, "Do you want to proceed?", 23); n += 23;
    // ESC[21;1H
    bytes[n++] = 0x1B; bytes[n++] = 0x5B; bytes[n++] = 0x32; bytes[n++] = 0x31; bytes[n++] = 0x3B; bytes[n++] = 0x31; bytes[n++] = 0x48;
    // U+276F
    bytes[n++] = 0xE2; bytes[n++] = 0x9D; bytes[n++] = 0xAF;
    memcpy(bytes+n, " 1. Yes", 7); n += 7;
    // ESC[22;3H
    bytes[n++] = 0x1B; bytes[n++] = 0x5B; bytes[n++] = 0x32; bytes[n++] = 0x32; bytes[n++] = 0x3B; bytes[n++] = 0x33; bytes[n++] = 0x48;
    memcpy(bytes+n, "2. No", 5); n += 5;

    nabtoshell_pattern_engine_feed(&me, bytes, n);

    ck_assert_ptr_nonnull(me.active_match);
    ck_assert_str_eq(me.active_match->id, "numbered_prompt");

    nabtoshell_pattern_engine_free(&me);
    nabtoshell_pattern_config_free(mcfg);
}
END_TEST

START_TEST(test_full_pipeline_with_newlines_and_colors)
{
    const char *menu_json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"claude-code\": {"
        "      \"name\": \"Claude Code\","
        "      \"patterns\": [{"
        "        \"id\": \"numbered_prompt\","
        "        \"type\": \"numbered_menu\","
        "        \"regex\": \"Do you want to (?:proceed|continue)\\\\?\\\\n.*1\\\\. .+\\\\n.*2\\\\. .+\","
        "        \"multi_line\": true,"
        "        \"action_template\": {\"keys\": \"{number}\\n\"}"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *mcfg = nabtoshell_pattern_config_parse(menu_json, strlen(menu_json));
    nabtoshell_pattern_engine me;
    nabtoshell_pattern_engine_init(&me);
    nabtoshell_pattern_engine_load_config(&me, mcfg);
    nabtoshell_pattern_engine_select_agent(&me, "claude-code");

    // ANSI colors with \r\n
    uint8_t bytes[128];
    int n = 0;
    bytes[n++] = 0x1B; bytes[n++] = 0x5B; bytes[n++] = 0x31; bytes[n++] = 0x6D;
    memcpy(bytes+n, "Do you want to proceed?", 23); n += 23;
    bytes[n++] = 0x1B; bytes[n++] = 0x5B; bytes[n++] = 0x30; bytes[n++] = 0x6D;
    bytes[n++] = 0x0D; bytes[n++] = 0x0A;
    bytes[n++] = 0x1B; bytes[n++] = 0x5B; bytes[n++] = 0x33; bytes[n++] = 0x36; bytes[n++] = 0x6D;
    bytes[n++] = 0xE2; bytes[n++] = 0x9D; bytes[n++] = 0xAF;
    bytes[n++] = 0x1B; bytes[n++] = 0x5B; bytes[n++] = 0x30; bytes[n++] = 0x6D;
    memcpy(bytes+n, " 1. Yes", 7); n += 7;
    bytes[n++] = 0x0D; bytes[n++] = 0x0A;
    memcpy(bytes+n, "  2. No", 7); n += 7;
    bytes[n++] = 0x0D; bytes[n++] = 0x0A;
    memcpy(bytes+n, "  3. Cancel", 11); n += 11;

    nabtoshell_pattern_engine_feed(&me, bytes, n);

    ck_assert_ptr_nonnull(me.active_match);
    ck_assert_str_eq(me.active_match->id, "numbered_prompt");
    ck_assert_int_eq(me.active_match->action_count, 3);

    nabtoshell_pattern_engine_free(&me);
    nabtoshell_pattern_config_free(mcfg);
}
END_TEST

START_TEST(test_utf8_boundary_2byte)
{
    /* Fill buffer near capacity with ASCII, ending with first byte of a
     * 2-byte UTF-8 sequence (e.g. 0xC2 for U+00B7). After rolling buffer
     * trims, the orphan continuation byte at the start should be skipped,
     * and matching should still work. */
    nabtoshell_pattern_engine_select_agent(&engine, "test");

    /* Fill most of the 8192-byte buffer */
    char filler[8100];
    memset(filler, 'a', sizeof(filler) - 1);
    /* End with first byte of 2-byte seq (will be split when buffer rolls) */
    filler[sizeof(filler) - 2] = (char)0xC2;
    filler[sizeof(filler) - 1] = '\0';
    feed_string(&engine, filler);

    /* Feed second byte (continuation) + more content + prompt.
     * The continuation byte becomes orphaned at buffer start after trim. */
    char chunk2[256];
    chunk2[0] = (char)0xB7;  /* second byte of U+00B7 */
    memset(chunk2 + 1, 'b', 200);
    strcpy(chunk2 + 201, "Continue? (y/n)");
    feed_string(&engine, chunk2);

    ck_assert_ptr_nonnull(engine.active_match);
    ck_assert_str_eq(engine.active_match->id, "yn");
}
END_TEST

START_TEST(test_utf8_boundary_3byte)
{
    /* Same test with a 3-byte sequence (U+276F = E2 9D AF). */
    nabtoshell_pattern_engine_select_agent(&engine, "test");

    char filler[8100];
    memset(filler, 'a', sizeof(filler) - 1);
    /* End with first byte of 3-byte seq */
    filler[sizeof(filler) - 2] = (char)0xE2;
    filler[sizeof(filler) - 1] = '\0';
    feed_string(&engine, filler);

    /* Feed remaining 2 bytes of U+276F + prompt */
    char chunk2[256];
    chunk2[0] = (char)0x9D;
    chunk2[1] = (char)0xAF;
    memset(chunk2 + 2, 'b', 200);
    strcpy(chunk2 + 202, "Continue? (y/n)");
    feed_string(&engine, chunk2);

    ck_assert_ptr_nonnull(engine.active_match);
    ck_assert_str_eq(engine.active_match->id, "yn");
}
END_TEST

Suite *pattern_engine_suite(void)
{
    Suite *s = suite_create("PatternEngine");
    TCase *tc = tcase_create("Core");
    tcase_add_checked_fixture(tc, setup, teardown);

    tcase_add_test(tc, test_full_pipeline);
    tcase_add_test(tc, test_no_match_without_agent);
    tcase_add_test(tc, test_auto_dismiss);
    tcase_add_test(tc, test_manual_dismiss);
    tcase_add_test(tc, test_dismiss_resets_after_new_content);
    tcase_add_test(tc, test_agent_switching);
    tcase_add_test(tc, test_reset);
    tcase_add_test(tc, test_select_agent_off);
    tcase_add_test(tc, test_select_agent_evaluates_existing_buffer);
    tcase_add_test(tc, test_buffer_populated_without_agent);
    tcase_add_test(tc, test_buffer_populated_without_agent_ansi);
    tcase_add_test(tc, test_auto_detect_claude_code);
    tcase_add_test(tc, test_auto_detect_does_not_override);
    tcase_add_test(tc, test_full_pipeline_with_cursor_positioning);
    tcase_add_test(tc, test_full_pipeline_with_newlines_and_colors);
    tcase_add_test(tc, test_utf8_boundary_2byte);
    tcase_add_test(tc, test_utf8_boundary_3byte);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = pattern_engine_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
