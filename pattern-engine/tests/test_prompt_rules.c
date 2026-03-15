#include <check.h>
#include <string.h>

#include "pe_pattern_config.h"
#include "pe_prompt_rules.h"
#include "pe_terminal_state.h"

static const char* TEST_CONFIG_JSON =
    "{"
    "  \"version\":3,"
    "  \"agents\":{"
    "    \"test\":{"
    "      \"name\":\"Test\","
    "      \"rules\":["
    "        {"
    "          \"id\":\"yn\","
    "          \"type\":\"yes_no\","
    "          \"prompt_regex\":\"Continue\\\\? \\\\(y/n\\\\)\","
    "          \"actions\":[{\"label\":\"Yes\",\"keys\":\"y\"},{\"label\":\"No\",\"keys\":\"n\"}]"
    "        },"
    "        {"
    "          \"id\":\"menu\","
    "          \"type\":\"numbered_menu\","
    "          \"prompt_regex\":\"Pick one\","
    "          \"option_regex\":\"^\\\\s*([0-9]+)\\\\.\\\\s+(.+)$\","
    "          \"action_template\":{\"keys\":\"{number}\"},"
    "          \"max_scan_lines\":5"
    "        }"
    "      ]"
    "    }"
    "  }"
    "}";

START_TEST(test_match_yes_no)
{
    pe_pattern_config* config =
        pe_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    pe_prompt_ruleset ruleset;
    pe_prompt_ruleset_init(&ruleset);
    ck_assert(pe_prompt_ruleset_load(&ruleset,
                                     agent->patterns,
                                     agent->pattern_count));

    pe_terminal_state state;
    pe_terminal_state_init(&state, 8, 80);
    const char* screen = "prefix\nContinue? (y/n)\n";
    pe_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    pe_terminal_snapshot snapshot;
    ck_assert(pe_terminal_state_snapshot(&state, &snapshot));

    pe_prompt_candidate candidate;
    bool matched = pe_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(matched);
    ck_assert_str_eq(candidate.pattern_id, "yn");
    ck_assert_int_eq(candidate.action_count, 2);
    ck_assert_str_eq(candidate.actions[0].keys, "y");

    pe_prompt_candidate_free(&candidate);
    pe_terminal_snapshot_free(&snapshot);
    pe_terminal_state_free(&state);
    pe_prompt_ruleset_free(&ruleset);
    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_match_numbered_menu)
{
    pe_pattern_config* config =
        pe_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    pe_prompt_ruleset ruleset;
    pe_prompt_ruleset_init(&ruleset);
    ck_assert(pe_prompt_ruleset_load(&ruleset,
                                     agent->patterns,
                                     agent->pattern_count));

    pe_terminal_state state;
    pe_terminal_state_init(&state, 10, 80);
    const char* screen = "Pick one\n1. Build\n2. Test\n3. Quit\n";
    pe_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    pe_terminal_snapshot snapshot;
    ck_assert(pe_terminal_state_snapshot(&state, &snapshot));

    pe_prompt_candidate candidate;
    bool matched = pe_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(matched);
    ck_assert_str_eq(candidate.pattern_id, "menu");
    ck_assert_int_eq(candidate.action_count, 3);
    ck_assert_str_eq(candidate.actions[2].label, "Quit");
    ck_assert_str_eq(candidate.actions[0].keys, "1");

    pe_prompt_candidate_free(&candidate);
    pe_terminal_snapshot_free(&snapshot);
    pe_terminal_state_free(&state);
    pe_prompt_ruleset_free(&ruleset);
    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_match_numbered_menu_with_selected_prefix)
{
    pe_pattern_config* config =
        pe_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    pe_prompt_ruleset ruleset;
    pe_prompt_ruleset_init(&ruleset);
    ck_assert(pe_prompt_ruleset_load(&ruleset,
                                     agent->patterns,
                                     agent->pattern_count));

    pe_terminal_state state;
    pe_terminal_state_init(&state, 10, 120);
    const char* screen =
        "Pick one\n"
        "\xE2\x9D\xAF 1. Yes\n"
        "2. Yes, allow reading from /etc during this session\n"
        "3. No\n";
    pe_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    pe_terminal_snapshot snapshot;
    ck_assert(pe_terminal_state_snapshot(&state, &snapshot));

    pe_prompt_candidate candidate;
    bool matched = pe_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(matched);
    ck_assert_str_eq(candidate.pattern_id, "menu");
    ck_assert_int_eq(candidate.action_count, 3);
    ck_assert_str_eq(candidate.actions[0].label, "Yes");
    ck_assert_str_eq(candidate.actions[0].keys, "1");
    ck_assert_str_eq(candidate.actions[2].label, "No");

    pe_prompt_candidate_free(&candidate);
    pe_terminal_snapshot_free(&snapshot);
    pe_terminal_state_free(&state);
    pe_prompt_ruleset_free(&ruleset);
    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_reject_numbered_menu_missing_primary_option)
{
    pe_pattern_config* config =
        pe_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    pe_prompt_ruleset ruleset;
    pe_prompt_ruleset_init(&ruleset);
    ck_assert(pe_prompt_ruleset_load(&ruleset,
                                     agent->patterns,
                                     agent->pattern_count));

    pe_terminal_state state;
    pe_terminal_state_init(&state, 10, 80);
    const char* screen = "Pick one\n2. Test\n3. Quit\n";
    pe_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    pe_terminal_snapshot snapshot;
    ck_assert(pe_terminal_state_snapshot(&state, &snapshot));

    pe_prompt_candidate candidate;
    bool matched = pe_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(!matched);

    pe_terminal_snapshot_free(&snapshot);
    pe_terminal_state_free(&state);
    pe_prompt_ruleset_free(&ruleset);
    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_reject_numbered_menu_with_gaps)
{
    pe_pattern_config* config =
        pe_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    pe_prompt_ruleset ruleset;
    pe_prompt_ruleset_init(&ruleset);
    ck_assert(pe_prompt_ruleset_load(&ruleset,
                                     agent->patterns,
                                     agent->pattern_count));

    pe_terminal_state state;
    pe_terminal_state_init(&state, 10, 80);
    const char* screen = "Pick one\n1. Build\n3. Quit\n";
    pe_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    pe_terminal_snapshot snapshot;
    ck_assert(pe_terminal_state_snapshot(&state, &snapshot));

    pe_prompt_candidate candidate;
    bool matched = pe_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(!matched);

    pe_terminal_snapshot_free(&snapshot);
    pe_terminal_state_free(&state);
    pe_prompt_ruleset_free(&ruleset);
    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_no_match_empty_ruleset)
{
    pe_prompt_ruleset ruleset;
    pe_prompt_ruleset_init(&ruleset);
    /* Load with 0 definitions */
    ck_assert(pe_prompt_ruleset_load(&ruleset, NULL, 0));

    pe_terminal_state state;
    pe_terminal_state_init(&state, 8, 80);
    const char* screen = "Continue? (y/n)\n";
    pe_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    pe_terminal_snapshot snapshot;
    ck_assert(pe_terminal_state_snapshot(&state, &snapshot));

    pe_prompt_candidate candidate;
    bool matched = pe_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(!matched);

    pe_terminal_snapshot_free(&snapshot);
    pe_terminal_state_free(&state);
    pe_prompt_ruleset_free(&ruleset);
}
END_TEST

START_TEST(test_no_match_empty_screen)
{
    pe_pattern_config* config =
        pe_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    pe_prompt_ruleset ruleset;
    pe_prompt_ruleset_init(&ruleset);
    ck_assert(pe_prompt_ruleset_load(&ruleset,
                                     agent->patterns,
                                     agent->pattern_count));

    pe_terminal_state state;
    pe_terminal_state_init(&state, 8, 80);
    /* No text fed: blank terminal */

    pe_terminal_snapshot snapshot;
    ck_assert(pe_terminal_state_snapshot(&state, &snapshot));

    pe_prompt_candidate candidate;
    bool matched = pe_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(!matched);

    pe_terminal_snapshot_free(&snapshot);
    pe_terminal_state_free(&state);
    pe_prompt_ruleset_free(&ruleset);
    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_accept_reject_match)
{
    const char* json =
        "{"
        "  \"version\":3,"
        "  \"agents\":{"
        "    \"test\":{"
        "      \"name\":\"Test\","
        "      \"rules\":["
        "        {"
        "          \"id\":\"ar\","
        "          \"type\":\"accept_reject\","
        "          \"prompt_regex\":\"Apply changes\\\\?\","
        "          \"actions\":[{\"label\":\"Accept\",\"keys\":\"a\"},{\"label\":\"Reject\",\"keys\":\"r\"}]"
        "        }"
        "      ]"
        "    }"
        "  }"
        "}";

    pe_pattern_config* config =
        pe_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(config);

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    pe_prompt_ruleset ruleset;
    pe_prompt_ruleset_init(&ruleset);
    ck_assert(pe_prompt_ruleset_load(&ruleset,
                                     agent->patterns,
                                     agent->pattern_count));

    pe_terminal_state state;
    pe_terminal_state_init(&state, 8, 80);
    const char* screen = "Apply changes?\n";
    pe_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    pe_terminal_snapshot snapshot;
    ck_assert(pe_terminal_state_snapshot(&state, &snapshot));

    pe_prompt_candidate candidate;
    bool matched = pe_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(matched);
    ck_assert_str_eq(candidate.pattern_id, "ar");
    ck_assert_int_eq(candidate.pattern_type, PE_PROMPT_TYPE_ACCEPT_REJECT);

    pe_prompt_candidate_free(&candidate);
    pe_terminal_snapshot_free(&snapshot);
    pe_terminal_state_free(&state);
    pe_prompt_ruleset_free(&ruleset);
    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_cursor_far_from_prompt_no_match)
{
    pe_pattern_config* config =
        pe_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    pe_prompt_ruleset ruleset;
    pe_prompt_ruleset_init(&ruleset);
    ck_assert(pe_prompt_ruleset_load(&ruleset,
                                     agent->patterns,
                                     agent->pattern_count));

    pe_terminal_state state;
    pe_terminal_state_init(&state, 48, 80);
    /* Place prompt text at row 5 using CSI positioning */
    const char* screen = "\x1b[6;1HContinue? (y/n)";
    pe_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));
    /* Move cursor far away to row 47 */
    const char* move = "\x1b[48;1H";
    pe_terminal_state_feed(&state, (const uint8_t*)move, strlen(move));

    pe_terminal_snapshot snapshot;
    ck_assert(pe_terminal_state_snapshot(&state, &snapshot));

    pe_prompt_candidate candidate;
    bool matched = pe_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(!matched);

    pe_terminal_snapshot_free(&snapshot);
    pe_terminal_state_free(&state);
    pe_prompt_ruleset_free(&ruleset);
    pe_pattern_config_free(config);
}
END_TEST

Suite* prompt_rules_suite(void)
{
    Suite* s = suite_create("PromptRules");
    TCase* tc = tcase_create("Core");

    tcase_add_test(tc, test_match_yes_no);
    tcase_add_test(tc, test_match_numbered_menu);
    tcase_add_test(tc, test_match_numbered_menu_with_selected_prefix);
    tcase_add_test(tc, test_reject_numbered_menu_missing_primary_option);
    tcase_add_test(tc, test_reject_numbered_menu_with_gaps);
    tcase_add_test(tc, test_no_match_empty_ruleset);
    tcase_add_test(tc, test_no_match_empty_screen);
    tcase_add_test(tc, test_accept_reject_match);
    tcase_add_test(tc, test_cursor_far_from_prompt_no_match);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite* s = prompt_rules_suite();
    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
