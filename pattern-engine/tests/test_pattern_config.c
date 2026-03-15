#include <check.h>
#include <string.h>

#include "pe_pattern_config.h"

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

    pe_pattern_config* config =
        pe_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(config);
    ck_assert_int_eq(config->version, 3);
    ck_assert_int_eq(config->agent_count, 1);

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(config, "agent-a");
    ck_assert_ptr_nonnull(agent);
    ck_assert_int_eq(agent->pattern_count, 2);

    ck_assert_str_eq(agent->patterns[0].id, "yes-no");
    ck_assert_int_eq(agent->patterns[0].type, PE_PROMPT_TYPE_YES_NO);
    ck_assert_int_eq(agent->patterns[0].action_count, 2);

    ck_assert_str_eq(agent->patterns[1].id, "menu");
    ck_assert_int_eq(agent->patterns[1].type, PE_PROMPT_TYPE_NUMBERED_MENU);
    ck_assert_ptr_nonnull(agent->patterns[1].action_template);

    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_requires_version_3)
{
    const char* json =
        "{"
        "  \"version\": 2,"
        "  \"agents\": {}"
        "}";

    pe_pattern_config* config =
        pe_pattern_config_parse(json, strlen(json));
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

    pe_pattern_config* config =
        pe_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(config);

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(config, "a");
    ck_assert_ptr_nonnull(agent);
    ck_assert_int_eq(agent->pattern_count, 1);
    ck_assert_str_eq(agent->patterns[0].id, "ok");

    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_multi_agent_config)
{
    const char* json =
        "{"
        "  \"version\": 3,"
        "  \"agents\": {"
        "    \"a1\": {"
        "      \"name\": \"Agent One\","
        "      \"rules\": ["
        "        {\"id\":\"r1\",\"type\":\"yes_no\",\"prompt_regex\":\"x\","
        "         \"actions\":[{\"label\":\"Y\",\"keys\":\"y\"}]}"
        "      ]"
        "    },"
        "    \"a2\": {"
        "      \"name\": \"Agent Two\","
        "      \"rules\": ["
        "        {\"id\":\"r2\",\"type\":\"yes_no\",\"prompt_regex\":\"z\","
        "         \"actions\":[{\"label\":\"N\",\"keys\":\"n\"}]}"
        "      ]"
        "    }"
        "  }"
        "}";

    pe_pattern_config* config =
        pe_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(config);
    ck_assert_int_eq(config->agent_count, 2);

    const pe_agent_config* a1 =
        pe_pattern_config_find_agent(config, "a1");
    ck_assert_ptr_nonnull(a1);
    ck_assert_str_eq(a1->name, "Agent One");
    ck_assert_int_eq(a1->pattern_count, 1);
    ck_assert_str_eq(a1->patterns[0].id, "r1");

    const pe_agent_config* a2 =
        pe_pattern_config_find_agent(config, "a2");
    ck_assert_ptr_nonnull(a2);
    ck_assert_str_eq(a2->name, "Agent Two");
    ck_assert_int_eq(a2->pattern_count, 1);
    ck_assert_str_eq(a2->patterns[0].id, "r2");

    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_agent_not_found)
{
    const char* json =
        "{"
        "  \"version\": 3,"
        "  \"agents\": {"
        "    \"a1\": {"
        "      \"name\": \"Agent One\","
        "      \"rules\": ["
        "        {\"id\":\"r1\",\"type\":\"yes_no\",\"prompt_regex\":\"x\","
        "         \"actions\":[{\"label\":\"Y\",\"keys\":\"y\"}]}"
        "      ]"
        "    }"
        "  }"
        "}";

    pe_pattern_config* config =
        pe_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(config);

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(config, "unknown-agent");
    ck_assert_ptr_null(agent);

    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_accept_reject_type)
{
    const char* json =
        "{"
        "  \"version\": 3,"
        "  \"agents\": {"
        "    \"a\": {"
        "      \"name\": \"A\","
        "      \"rules\": ["
        "        {\"id\":\"ar\",\"type\":\"accept_reject\",\"prompt_regex\":\"Accept changes\\\\?\","
        "         \"actions\":[{\"label\":\"Accept\",\"keys\":\"a\"},{\"label\":\"Reject\",\"keys\":\"r\"}]}"
        "      ]"
        "    }"
        "  }"
        "}";

    pe_pattern_config* config =
        pe_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(config);

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(config, "a");
    ck_assert_ptr_nonnull(agent);
    ck_assert_int_eq(agent->pattern_count, 1);
    ck_assert_int_eq(agent->patterns[0].type, PE_PROMPT_TYPE_ACCEPT_REJECT);

    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_max_scan_lines_override)
{
    const char* json =
        "{"
        "  \"version\": 3,"
        "  \"agents\": {"
        "    \"a\": {"
        "      \"name\": \"A\","
        "      \"rules\": ["
        "        {\"id\":\"m\",\"type\":\"numbered_menu\",\"prompt_regex\":\"Choose\","
        "         \"option_regex\":\"^([0-9]+)\\\\.\\\\s+(.+)$\","
        "         \"action_template\":{\"keys\":\"{number}\"},"
        "         \"max_scan_lines\":20}"
        "      ]"
        "    }"
        "  }"
        "}";

    pe_pattern_config* config =
        pe_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(config);

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(config, "a");
    ck_assert_ptr_nonnull(agent);
    ck_assert_int_eq(agent->patterns[0].max_scan_lines, 20);

    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_malformed_json_returns_null)
{
    const char* json = "{{garbage}}";
    pe_pattern_config* config =
        pe_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_null(config);
}
END_TEST

Suite* pattern_config_suite(void)
{
    Suite* s = suite_create("PatternConfig");
    TCase* tc = tcase_create("Core");

    tcase_add_test(tc, test_parse_v3_config);
    tcase_add_test(tc, test_requires_version_3);
    tcase_add_test(tc, test_invalid_rule_is_skipped);
    tcase_add_test(tc, test_multi_agent_config);
    tcase_add_test(tc, test_agent_not_found);
    tcase_add_test(tc, test_accept_reject_type);
    tcase_add_test(tc, test_max_scan_lines_override);
    tcase_add_test(tc, test_malformed_json_returns_null);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite* s = pattern_config_suite();
    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
