#include <check.h>
#include <string.h>

#include "nabtoshell_pattern_config.h"
#include "nabtoshell_prompt_rules.h"
#include "nabtoshell_terminal_state.h"

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
    nabtoshell_pattern_config* config =
        nabtoshell_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const nabtoshell_agent_config* agent =
        nabtoshell_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    nabtoshell_prompt_ruleset ruleset;
    nabtoshell_prompt_ruleset_init(&ruleset);
    ck_assert(nabtoshell_prompt_ruleset_load(&ruleset,
                                             agent->patterns,
                                             agent->pattern_count));

    nabtoshell_terminal_state state;
    nabtoshell_terminal_state_init(&state, 8, 80);
    const char* screen = "prefix\nContinue? (y/n)\n";
    nabtoshell_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    nabtoshell_terminal_snapshot snapshot;
    ck_assert(nabtoshell_terminal_state_snapshot(&state, &snapshot));

    nabtoshell_prompt_candidate candidate;
    bool matched = nabtoshell_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(matched);
    ck_assert_str_eq(candidate.pattern_id, "yn");
    ck_assert_int_eq(candidate.action_count, 2);
    ck_assert_str_eq(candidate.actions[0].keys, "y");

    nabtoshell_prompt_candidate_free(&candidate);
    nabtoshell_terminal_snapshot_free(&snapshot);
    nabtoshell_terminal_state_free(&state);
    nabtoshell_prompt_ruleset_free(&ruleset);
    nabtoshell_pattern_config_free(config);
}
END_TEST

START_TEST(test_match_numbered_menu)
{
    nabtoshell_pattern_config* config =
        nabtoshell_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const nabtoshell_agent_config* agent =
        nabtoshell_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    nabtoshell_prompt_ruleset ruleset;
    nabtoshell_prompt_ruleset_init(&ruleset);
    ck_assert(nabtoshell_prompt_ruleset_load(&ruleset,
                                             agent->patterns,
                                             agent->pattern_count));

    nabtoshell_terminal_state state;
    nabtoshell_terminal_state_init(&state, 10, 80);
    const char* screen = "Pick one\n1. Build\n2. Test\n3. Quit\n";
    nabtoshell_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    nabtoshell_terminal_snapshot snapshot;
    ck_assert(nabtoshell_terminal_state_snapshot(&state, &snapshot));

    nabtoshell_prompt_candidate candidate;
    bool matched = nabtoshell_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(matched);
    ck_assert_str_eq(candidate.pattern_id, "menu");
    ck_assert_int_eq(candidate.action_count, 3);
    ck_assert_str_eq(candidate.actions[2].label, "Quit");
    ck_assert_str_eq(candidate.actions[0].keys, "1");

    nabtoshell_prompt_candidate_free(&candidate);
    nabtoshell_terminal_snapshot_free(&snapshot);
    nabtoshell_terminal_state_free(&state);
    nabtoshell_prompt_ruleset_free(&ruleset);
    nabtoshell_pattern_config_free(config);
}
END_TEST

START_TEST(test_match_numbered_menu_with_selected_prefix)
{
    nabtoshell_pattern_config* config =
        nabtoshell_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const nabtoshell_agent_config* agent =
        nabtoshell_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    nabtoshell_prompt_ruleset ruleset;
    nabtoshell_prompt_ruleset_init(&ruleset);
    ck_assert(nabtoshell_prompt_ruleset_load(&ruleset,
                                             agent->patterns,
                                             agent->pattern_count));

    nabtoshell_terminal_state state;
    nabtoshell_terminal_state_init(&state, 10, 120);
    const char* screen =
        "Pick one\n"
        "\xE2\x9D\xAF 1. Yes\n"
        "2. Yes, allow reading from /etc during this session\n"
        "3. No\n";
    nabtoshell_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    nabtoshell_terminal_snapshot snapshot;
    ck_assert(nabtoshell_terminal_state_snapshot(&state, &snapshot));

    nabtoshell_prompt_candidate candidate;
    bool matched = nabtoshell_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(matched);
    ck_assert_str_eq(candidate.pattern_id, "menu");
    ck_assert_int_eq(candidate.action_count, 3);
    ck_assert_str_eq(candidate.actions[0].label, "Yes");
    ck_assert_str_eq(candidate.actions[0].keys, "1");
    ck_assert_str_eq(candidate.actions[2].label, "No");

    nabtoshell_prompt_candidate_free(&candidate);
    nabtoshell_terminal_snapshot_free(&snapshot);
    nabtoshell_terminal_state_free(&state);
    nabtoshell_prompt_ruleset_free(&ruleset);
    nabtoshell_pattern_config_free(config);
}
END_TEST

START_TEST(test_reject_numbered_menu_missing_primary_option)
{
    nabtoshell_pattern_config* config =
        nabtoshell_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const nabtoshell_agent_config* agent =
        nabtoshell_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    nabtoshell_prompt_ruleset ruleset;
    nabtoshell_prompt_ruleset_init(&ruleset);
    ck_assert(nabtoshell_prompt_ruleset_load(&ruleset,
                                             agent->patterns,
                                             agent->pattern_count));

    nabtoshell_terminal_state state;
    nabtoshell_terminal_state_init(&state, 10, 80);
    const char* screen = "Pick one\n2. Test\n3. Quit\n";
    nabtoshell_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    nabtoshell_terminal_snapshot snapshot;
    ck_assert(nabtoshell_terminal_state_snapshot(&state, &snapshot));

    nabtoshell_prompt_candidate candidate;
    bool matched = nabtoshell_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(!matched);

    nabtoshell_terminal_snapshot_free(&snapshot);
    nabtoshell_terminal_state_free(&state);
    nabtoshell_prompt_ruleset_free(&ruleset);
    nabtoshell_pattern_config_free(config);
}
END_TEST

START_TEST(test_reject_numbered_menu_with_gaps)
{
    nabtoshell_pattern_config* config =
        nabtoshell_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    const nabtoshell_agent_config* agent =
        nabtoshell_pattern_config_find_agent(config, "test");
    ck_assert_ptr_nonnull(agent);

    nabtoshell_prompt_ruleset ruleset;
    nabtoshell_prompt_ruleset_init(&ruleset);
    ck_assert(nabtoshell_prompt_ruleset_load(&ruleset,
                                             agent->patterns,
                                             agent->pattern_count));

    nabtoshell_terminal_state state;
    nabtoshell_terminal_state_init(&state, 10, 80);
    const char* screen = "Pick one\n1. Build\n3. Quit\n";
    nabtoshell_terminal_state_feed(&state, (const uint8_t*)screen, strlen(screen));

    nabtoshell_terminal_snapshot snapshot;
    ck_assert(nabtoshell_terminal_state_snapshot(&state, &snapshot));

    nabtoshell_prompt_candidate candidate;
    bool matched = nabtoshell_prompt_ruleset_match(&ruleset, &snapshot, &candidate);
    ck_assert(!matched);

    nabtoshell_terminal_snapshot_free(&snapshot);
    nabtoshell_terminal_state_free(&state);
    nabtoshell_prompt_ruleset_free(&ruleset);
    nabtoshell_pattern_config_free(config);
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
