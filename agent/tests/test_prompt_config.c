#include <check.h>
#include <string.h>

#include "tmuxremote_pattern_config.h"

START_TEST(test_parse_v3_config)
{
    const char* json =
        "{"
        "  \"version\": 3,"
        "  \"agents\": {"
        "    \"agent-a\": {"
        "      \"name\": \"Agent A\","
        "      \"rules\": ["
        "        {"
        "          \"id\": \"yes-no\","
        "          \"type\": \"yes_no\","
        "          \"prompt_regex\": \"Continue\\\\? \\\\(y/n\\\\)\","
        "          \"actions\": ["
        "            {\"label\": \"Yes\", \"keys\": \"y\"},"
        "            {\"label\": \"No\", \"keys\": \"n\"}"
        "          ]"
        "        },"
        "        {"
        "          \"id\": \"menu\","
        "          \"type\": \"numbered_menu\","
        "          \"prompt_regex\": \"Pick one\","
        "          \"option_regex\": \"^([0-9]+)\\\\.\\\\s+(.+)$\","
        "          \"action_template\": {\"keys\": \"{number}\\\\n\"}"
        "        }"
        "      ]"
        "    }"
        "  }"
        "}";

    tmuxremote_pattern_config* config =
        tmuxremote_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(config);
    ck_assert_int_eq(config->version, 3);
    ck_assert_int_eq(config->agent_count, 1);

    const tmuxremote_agent_config* agent =
        tmuxremote_pattern_config_find_agent(config, "agent-a");
    ck_assert_ptr_nonnull(agent);
    ck_assert_int_eq(agent->pattern_count, 2);

    ck_assert_str_eq(agent->patterns[0].id, "yes-no");
    ck_assert_int_eq(agent->patterns[0].type, TMUXREMOTE_PROMPT_TYPE_YES_NO);
    ck_assert_int_eq(agent->patterns[0].action_count, 2);

    ck_assert_str_eq(agent->patterns[1].id, "menu");
    ck_assert_int_eq(agent->patterns[1].type, TMUXREMOTE_PROMPT_TYPE_NUMBERED_MENU);
    ck_assert_ptr_nonnull(agent->patterns[1].action_template);

    tmuxremote_pattern_config_free(config);
}
END_TEST

START_TEST(test_requires_version_3)
{
    const char* json =
        "{"
        "  \"version\": 2,"
        "  \"agents\": {}"
        "}";

    tmuxremote_pattern_config* config =
        tmuxremote_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_null(config);
}
END_TEST

START_TEST(test_invalid_rule_is_skipped)
{
    const char* json =
        "{"
        "  \"version\": 3,"
        "  \"agents\": {"
        "    \"a\": {"
        "      \"name\": \"A\","
        "      \"rules\": ["
        "        {\"id\":\"ok\",\"type\":\"yes_no\",\"prompt_regex\":\"x\","
        "         \"actions\":[{\"label\":\"Y\",\"keys\":\"y\"}]},"
        "        {\"id\":\"bad\",\"type\":\"yes_no\",\"actions\":[]}"
        "      ]"
        "    }"
        "  }"
        "}";

    tmuxremote_pattern_config* config =
        tmuxremote_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(config);

    const tmuxremote_agent_config* agent =
        tmuxremote_pattern_config_find_agent(config, "a");
    ck_assert_ptr_nonnull(agent);
    ck_assert_int_eq(agent->pattern_count, 1);
    ck_assert_str_eq(agent->patterns[0].id, "ok");

    tmuxremote_pattern_config_free(config);
}
END_TEST

Suite* prompt_config_suite(void)
{
    Suite* s = suite_create("PromptConfig");
    TCase* tc = tcase_create("Core");

    tcase_add_test(tc, test_parse_v3_config);
    tcase_add_test(tc, test_requires_version_3);
    tcase_add_test(tc, test_invalid_rule_is_skipped);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite* s = prompt_config_suite();
    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
