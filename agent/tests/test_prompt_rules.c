#include <check.h>
#include <string.h>

#include "tmuxremote_pattern_config.h"
#include "tmuxremote_prompt_rules.h"
#include "tmuxremote_terminal_state.h"

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
    tmuxremote_pattern_config* config =
        tmuxremote_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const tmuxremote_agent_config* agent =
        tmuxremote_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    tmuxremote_prompt_ruleset ruleset;
    tmuxremote_prompt_ruleset_init(&ruleset);
    ck_assert(tmuxremote_prompt_ruleset_load(&ruleset,
                                             agent->patterns,
                                             agent->pattern_count));

    tmuxremote_terminal_state state;
    tmuxremote_terminal_state_init(&state, 8, 80);
    const char* screen = "prefix\nContinue? (y/n)\n";
    tmuxremote_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    tmuxremote_terminal_snapshot snapshot;
    ck_assert(tmuxremote_terminal_state_snapshot(&state, &snapshot));

    tmuxremote_prompt_candidate candidate;
    bool matched = tmuxremote_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(matched);
    ck_assert_str_eq(candidate.pattern_id, "yn");
    ck_assert_int_eq(candidate.action_count, 2);
    ck_assert_str_eq(candidate.actions[0].keys, "y");

    tmuxremote_prompt_candidate_free(&candidate);
    tmuxremote_terminal_snapshot_free(&snapshot);
    tmuxremote_terminal_state_free(&state);
    tmuxremote_prompt_ruleset_free(&ruleset);
    tmuxremote_pattern_config_free(config);
}
END_TEST

START_TEST(test_match_numbered_menu)
{
    tmuxremote_pattern_config* config =
        tmuxremote_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const tmuxremote_agent_config* agent =
        tmuxremote_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    tmuxremote_prompt_ruleset ruleset;
    tmuxremote_prompt_ruleset_init(&ruleset);
    ck_assert(tmuxremote_prompt_ruleset_load(&ruleset,
                                             agent->patterns,
                                             agent->pattern_count));

    tmuxremote_terminal_state state;
    tmuxremote_terminal_state_init(&state, 10, 80);
    const char* screen = "Pick one\n1. Build\n2. Test\n3. Quit\n";
    tmuxremote_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    tmuxremote_terminal_snapshot snapshot;
    ck_assert(tmuxremote_terminal_state_snapshot(&state, &snapshot));

    tmuxremote_prompt_candidate candidate;
    bool matched = tmuxremote_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(matched);
    ck_assert_str_eq(candidate.pattern_id, "menu");
    ck_assert_int_eq(candidate.action_count, 3);
    ck_assert_str_eq(candidate.actions[2].label, "Quit");
    ck_assert_str_eq(candidate.actions[0].keys, "1");

    tmuxremote_prompt_candidate_free(&candidate);
    tmuxremote_terminal_snapshot_free(&snapshot);
    tmuxremote_terminal_state_free(&state);
    tmuxremote_prompt_ruleset_free(&ruleset);
    tmuxremote_pattern_config_free(config);
}
END_TEST

START_TEST(test_match_numbered_menu_with_selected_prefix)
{
    tmuxremote_pattern_config* config =
        tmuxremote_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const tmuxremote_agent_config* agent =
        tmuxremote_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    tmuxremote_prompt_ruleset ruleset;
    tmuxremote_prompt_ruleset_init(&ruleset);
    ck_assert(tmuxremote_prompt_ruleset_load(&ruleset,
                                             agent->patterns,
                                             agent->pattern_count));

    tmuxremote_terminal_state state;
    tmuxremote_terminal_state_init(&state, 10, 120);
    const char* screen =
        "Pick one\n"
        "\xE2\x9D\xAF 1. Yes\n"
        "2. Yes, allow reading from /etc during this session\n"
        "3. No\n";
    tmuxremote_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    tmuxremote_terminal_snapshot snapshot;
    ck_assert(tmuxremote_terminal_state_snapshot(&state, &snapshot));

    tmuxremote_prompt_candidate candidate;
    bool matched = tmuxremote_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(matched);
    ck_assert_str_eq(candidate.pattern_id, "menu");
    ck_assert_int_eq(candidate.action_count, 3);
    ck_assert_str_eq(candidate.actions[0].label, "Yes");
    ck_assert_str_eq(candidate.actions[0].keys, "1");
    ck_assert_str_eq(candidate.actions[2].label, "No");

    tmuxremote_prompt_candidate_free(&candidate);
    tmuxremote_terminal_snapshot_free(&snapshot);
    tmuxremote_terminal_state_free(&state);
    tmuxremote_prompt_ruleset_free(&ruleset);
    tmuxremote_pattern_config_free(config);
}
END_TEST

START_TEST(test_reject_numbered_menu_missing_primary_option)
{
    tmuxremote_pattern_config* config =
        tmuxremote_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const tmuxremote_agent_config* agent =
        tmuxremote_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    tmuxremote_prompt_ruleset ruleset;
    tmuxremote_prompt_ruleset_init(&ruleset);
    ck_assert(tmuxremote_prompt_ruleset_load(&ruleset,
                                             agent->patterns,
                                             agent->pattern_count));

    tmuxremote_terminal_state state;
    tmuxremote_terminal_state_init(&state, 10, 80);
    const char* screen = "Pick one\n2. Test\n3. Quit\n";
    tmuxremote_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    tmuxremote_terminal_snapshot snapshot;
    ck_assert(tmuxremote_terminal_state_snapshot(&state, &snapshot));

    tmuxremote_prompt_candidate candidate;
    bool matched = tmuxremote_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(!matched);

    tmuxremote_terminal_snapshot_free(&snapshot);
    tmuxremote_terminal_state_free(&state);
    tmuxremote_prompt_ruleset_free(&ruleset);
    tmuxremote_pattern_config_free(config);
}
END_TEST

START_TEST(test_reject_numbered_menu_with_gaps)
{
    tmuxremote_pattern_config* config =
        tmuxremote_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const tmuxremote_agent_config* agent =
        tmuxremote_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    tmuxremote_prompt_ruleset ruleset;
    tmuxremote_prompt_ruleset_init(&ruleset);
    ck_assert(tmuxremote_prompt_ruleset_load(&ruleset,
                                             agent->patterns,
                                             agent->pattern_count));

    tmuxremote_terminal_state state;
    tmuxremote_terminal_state_init(&state, 10, 80);
    const char* screen = "Pick one\n1. Build\n3. Quit\n";
    tmuxremote_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    tmuxremote_terminal_snapshot snapshot;
    ck_assert(tmuxremote_terminal_state_snapshot(&state, &snapshot));

    tmuxremote_prompt_candidate candidate;
    bool matched = tmuxremote_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(!matched);

    tmuxremote_terminal_snapshot_free(&snapshot);
    tmuxremote_terminal_state_free(&state);
    tmuxremote_prompt_ruleset_free(&ruleset);
    tmuxremote_pattern_config_free(config);
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
