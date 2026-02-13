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

    // Auto-dismiss re-scans the FULL buffer (capacity 8192), not just
    // the match window. But MAX_MATCH_AGE (4000) force-dismisses before
    // the prompt leaves the buffer. So we just need >MAX_MATCH_AGE chars.
    char filler[4200];
    memset(filler, 'x', 4100);
    filler[4100] = '\0';
    feed_string(&engine, filler);

    ck_assert_ptr_null(engine.active_match);
}
END_TEST

START_TEST(test_auto_dismiss_keeps_match_in_buffer)
{
    /* When a match is still within the full buffer (but past the match window),
     * auto-dismiss re-scan should find it and KEEP the match active. This is
     * the key fix: previously the re-scan only used MATCH_WINDOW and would
     * incorrectly dismiss matches that were still in the buffer. */
    nabtoshell_pattern_engine_select_agent(&engine, "test");
    feed_string(&engine, "Continue? (y/n)");
    ck_assert_ptr_nonnull(engine.active_match);

    // Feed enough to trigger auto-dismiss check (>1500) and push past
    // match window (4000), but NOT past MAX_MATCH_AGE (4000).
    // Use exactly 2500 chars: > AUTO_DISMISS (1500) but < MAX_MATCH_AGE (4000).
    char filler[2600];
    memset(filler, 'x', 2500);
    filler[2500] = '\0';
    feed_string(&engine, filler);

    // Match should still be active because the prompt is in the full buffer
    // and hasn't reached the hard age cap.
    ck_assert_ptr_nonnull(engine.active_match);
    ck_assert_str_eq(engine.active_match->id, "yn");
}
END_TEST

START_TEST(test_tui_redraw_keeps_match_then_clears)
{
    /* TUI redraws keep the prompt in the match window. The match stays active
     * with its age reset on each auto-dismiss check. No dismiss events are
     * fired during redraws. When the prompt stops being redrawn and leaves
     * the match window, a single dismiss event fires. */
    nabtoshell_pattern_engine_select_agent(&engine, "test");
    feed_string(&engine, "Continue? (y/n)");
    ck_assert_ptr_nonnull(engine.active_match);

    /* Simulate TUI redraws: interleave prompt with filler. Each round adds
     * ~2000 chars. The prompt stays in the match window because it's
     * re-sent on each redraw. Match age is reset, so no force-dismiss. */
    for (int i = 0; i < 5; i++) {
        char filler[2100];
        memset(filler, 'x', 2000);
        filler[2000] = '\0';
        feed_string(&engine, filler);
        feed_string(&engine, "Continue? (y/n)");  /* TUI redraw */
    }
    /* Match should still be active (age keeps resetting) */
    ck_assert_ptr_nonnull(engine.active_match);

    /* Now stop redrawing the prompt and push it out of the match window. */
    char filler[4200];
    memset(filler, 'x', 4100);
    filler[4100] = '\0';
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

    // Feed enough new content to clear user-dismiss (MATCH_WINDOW=4000 chars)
    char filler[4200];
    memset(filler, 'z', 4100);
    filler[4100] = '\0';
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
    char filler[4200];
    memset(filler, 'z', 4100);
    filler[4100] = '\0';
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

START_TEST(test_incremental_item_arrival)
{
    /* Reproduces the real-world bug: the regex matches as soon as items
     * 1-2 are visible in the match window, but item 3 hasn't arrived yet
     * (it's in the next PTY chunk). The initial match fires with 2 actions.
     * On the next feed (TUI redraw), the age-reset rescan finds 3 items.
     * The active match must be UPGRADED and a new callback fired. */
    const char *menu_json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"claude-code\": {"
        "      \"name\": \"Claude Code\","
        "      \"patterns\": [{"
        "        \"id\": \"numbered_prompt\","
        "        \"type\": \"numbered_menu\","
        "        \"regex\": \"Do you want to .+\\\\?\\\\n.*1\\\\. .+\\\\n.*2\\\\. .+\","
        "        \"multi_line\": true,"
        "        \"action_template\": {\"keys\": \"{number}\"}"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *mcfg = nabtoshell_pattern_config_parse(menu_json, strlen(menu_json));
    nabtoshell_pattern_engine me;
    nabtoshell_pattern_engine_init(&me);
    nabtoshell_pattern_engine_load_config(&me, mcfg);
    nabtoshell_pattern_engine_select_agent(&me, "claude-code");

    /* Fill buffer so the prompt lands near end of match window.
     * MATCH_WINDOW=4000. Put 3900 chars of filler first. */
    char filler[3950];
    memset(filler, 'x', 3900);
    filler[3900] = '\0';
    feed_string(&me, filler);

    /* Feed items 1-2 only (item 3 hasn't arrived in this PTY chunk). */
    feed_string(&me, "Do you want to create bar.txt?\n"
                     "\xe2\x9d\xaf 1. Yes\n"
                     "  2. Yes, allow all\n");

    /* Match fires with 2 actions (item 3 not yet in buffer) */
    ck_assert_ptr_nonnull(me.active_match);
    ck_assert_str_eq(me.active_match->id, "numbered_prompt");
    ck_assert_int_eq(me.active_match->action_count, 2);

    /* Simulate TUI redraw: enough filler to trigger age-reset check
     * (> AUTO_DISMISS=1500), then the full prompt with all 3 items. */
    char redraw_filler[1600];
    memset(redraw_filler, 'z', 1550);
    redraw_filler[1550] = '\0';
    feed_string(&me, redraw_filler);

    feed_string(&me, "Do you want to create bar.txt?\n"
                     "\xe2\x9d\xaf 1. Yes\n"
                     "  2. Yes, allow all\n"
                     "  3. No\n");

    /* After age-reset rescan with upgrade, match should have 3 actions */
    ck_assert_ptr_nonnull(me.active_match);
    ck_assert_int_eq(me.active_match->action_count, 3);

    nabtoshell_pattern_engine_free(&me);
    nabtoshell_pattern_config_free(mcfg);
}
END_TEST

START_TEST(test_duplicate_prompt_uses_latest)
{
    /* When auto-dismiss fires and then a new match is found, the match
     * window may contain two prompt copies (old partial + new complete).
     * The matcher must use the LAST occurrence to get the most items. */
    const char *menu_json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"claude-code\": {"
        "      \"name\": \"Claude Code\","
        "      \"patterns\": [{"
        "        \"id\": \"numbered_prompt\","
        "        \"type\": \"numbered_menu\","
        "        \"regex\": \"Do you want to .+\\\\?\\\\n.*1\\\\. .+\\\\n.*2\\\\. .+\","
        "        \"multi_line\": true,"
        "        \"action_template\": {\"keys\": \"{number}\"}"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *mcfg = nabtoshell_pattern_config_parse(menu_json, strlen(menu_json));
    nabtoshell_pattern_engine me;
    nabtoshell_pattern_engine_init(&me);
    nabtoshell_pattern_engine_load_config(&me, mcfg);
    nabtoshell_pattern_engine_select_agent(&me, "claude-code");

    /* Feed a complete 3-item prompt. */
    feed_string(&me, "Do you want to create bar.txt?\n"
                     "\xe2\x9d\xaf 1. Yes\n"
                     "  2. Yes, allow all\n"
                     "  3. No\n");
    ck_assert_ptr_nonnull(me.active_match);
    ck_assert_int_eq(me.active_match->action_count, 3);

    /* Push enough filler to trigger auto-dismiss (>1500) and then
     * another complete prompt, but also push the old match past the
     * scan window so auto-dismiss fires. Total filler > MATCH_WINDOW. */
    char filler[4200];
    memset(filler, 'x', 4100);
    filler[4100] = '\0';
    feed_string(&me, filler);

    /* Old match should be auto-dismissed. */
    ck_assert_ptr_null(me.active_match);

    /* Now feed a partial prompt (2 items) followed by a complete one (3 items).
     * Both are within the match window. The matcher must prefer the last. */
    feed_string(&me, "Do you want to create bar.txt?\n"
                     "\xe2\x9d\xaf 1. Yes\n"
                     "  2. Yes, allow all\n");

    /* This might match with 2 or 3 depending on whether item 3 is there yet.
     * We allow 2 here since only 2 items were fed. */
    ck_assert_ptr_nonnull(me.active_match);
    ck_assert_int_ge(me.active_match->action_count, 2);

    /* Now simulate: auto-dismiss the partial, then feed 2-item + 3-item in window. */
    memset(filler, 'y', 4100);
    filler[4100] = '\0';
    feed_string(&me, filler);
    ck_assert_ptr_null(me.active_match);

    /* Both prompts in a single feed: old 2-item then new 3-item. */
    feed_string(&me, "Do you want to create bar.txt?\n"
                     "\xe2\x9d\xaf 1. Yes\n"
                     "  2. Yes, allow all\n"
                     "some filler between prompts\n"
                     "Do you want to create bar.txt?\n"
                     "\xe2\x9d\xaf 1. Yes\n"
                     "  2. Yes, allow all\n"
                     "  3. No\n");

    /* Must find 3 actions from the last prompt copy, not 2 from the first. */
    ck_assert_ptr_nonnull(me.active_match);
    ck_assert_int_eq(me.active_match->action_count, 3);

    nabtoshell_pattern_engine_free(&me);
    nabtoshell_pattern_config_free(mcfg);
}
END_TEST

START_TEST(test_consume_then_new_prompt)
{
    /* Reproduces the bug from pty-log-1: first prompt matches, user acts
     * on it (iOS consume(), no dismiss sent to agent), TUI redraws keep
     * the match alive via age-reset, then the first prompt leaves and a
     * second prompt appears. The engine must detect the second prompt.
     *
     * Without a fix, the active_match from the first prompt is never
     * cleared, so the engine stays in "skip-new-match" and never fires
     * a callback for the second prompt. */
    const char *menu_json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"claude-code\": {"
        "      \"name\": \"Claude Code\","
        "      \"patterns\": [{"
        "        \"id\": \"numbered_prompt\","
        "        \"type\": \"numbered_menu\","
        "        \"regex\": \"Do you want to .+\\\\?\\\\n.*1\\\\. .+\\\\n.*2\\\\. .+\","
        "        \"multi_line\": true,"
        "        \"action_template\": {\"keys\": \"{number}\"}"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *mcfg = nabtoshell_pattern_config_parse(menu_json, strlen(menu_json));
    nabtoshell_pattern_engine me;
    nabtoshell_pattern_engine_init(&me);
    nabtoshell_pattern_engine_load_config(&me, mcfg);
    nabtoshell_pattern_engine_select_agent(&me, "claude-code");

    /* Track callbacks */
    int match_count = 0;
    int dismiss_count = 0;

    /* We cannot easily use a callback with local state, so just check
     * engine state directly (tests access struct internals). */

    /* Step 1: First prompt appears and matches. */
    feed_string(&me, "Do you want to create foo.txt?\n"
                     "\xe2\x9d\xaf 1. Yes\n"
                     "  2. Yes, allow all\n"
                     "  3. No\n");
    ck_assert_ptr_nonnull(me.active_match);
    ck_assert_str_eq(me.active_match->id, "numbered_prompt");
    ck_assert_int_eq(me.active_match->action_count, 3);

    /* Step 2: User acts on the prompt (presses "3"). On the iOS side,
     * consume() is called, which does NOT send pattern_dismiss to the
     * agent. The agent never learns the user acted. Meanwhile, the TUI
     * continues redrawing the same prompt. Feed ~1600 chars of filler
     * then the same prompt again (TUI redraw). */
    char filler[1700];
    memset(filler, 'x', 1600);
    filler[1600] = '\0';
    feed_string(&me, filler);

    /* TUI redraw of the first prompt (age-reset should fire). */
    feed_string(&me, "Do you want to create foo.txt?\n"
                     "\xe2\x9d\xaf 1. Yes\n"
                     "  2. Yes, allow all\n"
                     "  3. No\n");

    /* Step 3: The first prompt leaves the screen. Feed enough filler to
     * push it past the auto-dismiss window AND ensure the auto-dismiss
     * check fires (> 1500 chars from last age-reset). */
    char filler2[4200];
    memset(filler2, 'y', 4100);
    filler2[4100] = '\0';
    feed_string(&me, filler2);

    /* At this point the first prompt should be auto-dismissed. */
    ck_assert_ptr_null(me.active_match);

    /* Step 4: Second prompt appears. */
    feed_string(&me, "Do you want to create bar.txt?\n"
                     "\xe2\x9d\xaf 1. Yes\n"
                     "  2. Yes, allow all\n"
                     "  3. No\n");

    /* The engine must detect the second prompt. */
    ck_assert_ptr_nonnull(me.active_match);
    ck_assert_str_eq(me.active_match->id, "numbered_prompt");
    ck_assert_int_eq(me.active_match->action_count, 3);

    nabtoshell_pattern_engine_free(&me);
    nabtoshell_pattern_config_free(mcfg);
}
END_TEST

START_TEST(test_consume_no_dismiss_short_gap)
{
    /* Variant: the gap between prompts is short (< AUTO_DISMISS), so auto-
     * dismiss never fires. The engine has active_match from prompt 1 and
     * the TUI replaces it with prompt 2 in place. The engine must detect
     * that the prompt TEXT changed and fire a new match event.
     *
     * In the real recording, the gap is about 1200 chars (below 1500
     * AUTO_DISMISS threshold), so auto-dismiss never checks. The old match
     * blocks detection of the new prompt indefinitely. */
    const char *menu_json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"claude-code\": {"
        "      \"name\": \"Claude Code\","
        "      \"patterns\": [{"
        "        \"id\": \"numbered_prompt\","
        "        \"type\": \"numbered_menu\","
        "        \"regex\": \"Do you want to .+\\\\?\\\\n.*1\\\\. .+\\\\n.*2\\\\. .+\","
        "        \"multi_line\": true,"
        "        \"action_template\": {\"keys\": \"{number}\"}"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *mcfg = nabtoshell_pattern_config_parse(menu_json, strlen(menu_json));
    nabtoshell_pattern_engine me;
    nabtoshell_pattern_engine_init(&me);
    nabtoshell_pattern_engine_load_config(&me, mcfg);
    nabtoshell_pattern_engine_select_agent(&me, "claude-code");

    /* Step 1: First prompt matches. */
    feed_string(&me, "Do you want to create foo.txt?\n"
                     "\xe2\x9d\xaf 1. Yes\n"
                     "  2. Yes, allow all\n"
                     "  3. No\n");
    ck_assert_ptr_nonnull(me.active_match);
    ck_assert_int_eq(me.active_match->action_count, 3);

    /* Step 2: Short gap (< 1500 chars), then second prompt appears.
     * In a real session the user pressed "3", CC processed it, and
     * immediately showed a new prompt. The gap is too short for auto-
     * dismiss to trigger, so the old active_match is still set. */
    char filler[1200];
    memset(filler, 'z', 1100);
    filler[1100] = '\0';
    feed_string(&me, filler);

    feed_string(&me, "Do you want to create bar.txt?\n"
                     "\xe2\x9d\xaf 1. Yes\n"
                     "  2. Yes, allow all\n"
                     "  3. No\n");

    /* The engine must detect the SECOND prompt, even though the first
     * active_match was never dismissed. The prompt text changed. */
    ck_assert_ptr_nonnull(me.active_match);
    /* Verify the prompt text is from the second prompt. */
    ck_assert_ptr_nonnull(me.active_match->prompt);
    ck_assert(strstr(me.active_match->prompt, "bar.txt") != NULL);
    /* Verify match position advanced past the first prompt + filler. */
    ck_assert(me.active_match->match_position > 1100);

    nabtoshell_pattern_engine_free(&me);
    nabtoshell_pattern_config_free(mcfg);
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
    tcase_add_test(tc, test_auto_dismiss_keeps_match_in_buffer);
    tcase_add_test(tc, test_tui_redraw_keeps_match_then_clears);
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
    tcase_add_test(tc, test_incremental_item_arrival);
    tcase_add_test(tc, test_duplicate_prompt_uses_latest);
    tcase_add_test(tc, test_consume_then_new_prompt);
    tcase_add_test(tc, test_consume_no_dismiss_short_gap);

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
