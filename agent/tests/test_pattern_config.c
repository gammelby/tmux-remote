#include <check.h>
#include <string.h>
#include <stdlib.h>
#include "nabtoshell_pattern_config.h"

START_TEST(test_decode_bundled_format)
{
    const char *json =
        "{"
        "  \"version\": 2,"
        "  \"agents\": {"
        "    \"test-agent\": {"
        "      \"name\": \"Test Agent\","
        "      \"patterns\": [{"
        "        \"id\": \"yes_prompt\","
        "        \"type\": \"yes_no\","
        "        \"regex\": \"Continue\\\\?.*\\\\(y\\\\/n\\\\)\","
        "        \"actions\": ["
        "          {\"label\": \"Yes\", \"keys\": \"y\"},"
        "          {\"label\": \"No\", \"keys\": \"n\"}"
        "        ]"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *cfg = nabtoshell_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(cfg);
    ck_assert_int_eq(cfg->version, 2);
    ck_assert_int_eq(cfg->agent_count, 1);

    const nabtoshell_agent_config *agent = nabtoshell_pattern_config_find_agent(cfg, "test-agent");
    ck_assert_ptr_nonnull(agent);
    ck_assert_str_eq(agent->name, "Test Agent");
    ck_assert_int_eq(agent->pattern_count, 1);

    ck_assert_str_eq(agent->patterns[0].id, "yes_prompt");
    ck_assert_int_eq(agent->patterns[0].type, PATTERN_TYPE_YES_NO);
    ck_assert_int_eq(agent->patterns[0].action_count, 2);

    nabtoshell_pattern_config_free(cfg);
}
END_TEST

START_TEST(test_decode_numbered_menu)
{
    const char *json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"a\": {"
        "      \"name\": \"A\","
        "      \"patterns\": [{"
        "        \"id\": \"menu\","
        "        \"type\": \"numbered_menu\","
        "        \"regex\": \"\\\\[\\\\d+\\\\]\\\\s+.+\","
        "        \"multi_line\": true,"
        "        \"action_template\": {\"keys\": \"{number}\\n\"}"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *cfg = nabtoshell_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(cfg);

    const nabtoshell_agent_config *agent = nabtoshell_pattern_config_find_agent(cfg, "a");
    ck_assert_ptr_nonnull(agent);

    ck_assert_int_eq(agent->patterns[0].type, PATTERN_TYPE_NUMBERED_MENU);
    ck_assert(agent->patterns[0].multi_line);
    ck_assert_ptr_nonnull(agent->patterns[0].action_template);
    ck_assert_str_eq(agent->patterns[0].action_template->keys, "{number}\n");
    ck_assert_int_eq(agent->patterns[0].action_count, 0);

    nabtoshell_pattern_config_free(cfg);
}
END_TEST

START_TEST(test_decode_empty_patterns)
{
    const char *json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"empty\": {"
        "      \"name\": \"Empty\","
        "      \"patterns\": []"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *cfg = nabtoshell_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(cfg);
    const nabtoshell_agent_config *agent = nabtoshell_pattern_config_find_agent(cfg, "empty");
    ck_assert_int_eq(agent->pattern_count, 0);

    nabtoshell_pattern_config_free(cfg);
}
END_TEST

START_TEST(test_invalid_json)
{
    const char *json = "not json";
    nabtoshell_pattern_config *cfg = nabtoshell_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_null(cfg);
}
END_TEST

START_TEST(test_missing_required_fields)
{
    const char *json = "{\"agents\": {}}";
    nabtoshell_pattern_config *cfg = nabtoshell_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_null(cfg);
}
END_TEST

START_TEST(test_optional_multi_line)
{
    const char *json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"a\": {"
        "      \"name\": \"A\","
        "      \"patterns\": [{"
        "        \"id\": \"p\","
        "        \"type\": \"yes_no\","
        "        \"regex\": \"test\","
        "        \"actions\": [{\"label\": \"Y\", \"keys\": \"y\"}, {\"label\": \"N\", \"keys\": \"n\"}]"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *cfg = nabtoshell_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(cfg);
    const nabtoshell_agent_config *agent = nabtoshell_pattern_config_find_agent(cfg, "a");
    ck_assert(!agent->patterns[0].multi_line);

    nabtoshell_pattern_config_free(cfg);
}
END_TEST

START_TEST(test_missing_id_skips_pattern)
{
    const char *json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"a\": {"
        "      \"name\": \"A\","
        "      \"patterns\": [{"
        "        \"type\": \"yes_no\","
        "        \"regex\": \"test\","
        "        \"actions\": [{\"label\": \"Y\", \"keys\": \"y\"}]"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *cfg = nabtoshell_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(cfg);
    const nabtoshell_agent_config *agent = nabtoshell_pattern_config_find_agent(cfg, "a");
    ck_assert_ptr_nonnull(agent);
    ck_assert_int_eq(agent->pattern_count, 0);

    nabtoshell_pattern_config_free(cfg);
}
END_TEST

START_TEST(test_missing_regex_skips_pattern)
{
    const char *json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"a\": {"
        "      \"name\": \"A\","
        "      \"patterns\": [{"
        "        \"id\": \"p1\","
        "        \"type\": \"yes_no\","
        "        \"actions\": [{\"label\": \"Y\", \"keys\": \"y\"}]"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *cfg = nabtoshell_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(cfg);
    const nabtoshell_agent_config *agent = nabtoshell_pattern_config_find_agent(cfg, "a");
    ck_assert_ptr_nonnull(agent);
    ck_assert_int_eq(agent->pattern_count, 0);

    nabtoshell_pattern_config_free(cfg);
}
END_TEST

START_TEST(test_missing_type_defaults_yes_no)
{
    const char *json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"a\": {"
        "      \"name\": \"A\","
        "      \"patterns\": [{"
        "        \"id\": \"p1\","
        "        \"regex\": \"test\","
        "        \"actions\": [{\"label\": \"Y\", \"keys\": \"y\"}]"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *cfg = nabtoshell_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(cfg);
    const nabtoshell_agent_config *agent = nabtoshell_pattern_config_find_agent(cfg, "a");
    ck_assert_ptr_nonnull(agent);
    ck_assert_int_eq(agent->pattern_count, 1);
    ck_assert_int_eq(agent->patterns[0].type, PATTERN_TYPE_YES_NO);

    nabtoshell_pattern_config_free(cfg);
}
END_TEST

START_TEST(test_null_action_fields_skipped)
{
    const char *json =
        "{"
        "  \"version\": 1,"
        "  \"agents\": {"
        "    \"a\": {"
        "      \"name\": \"A\","
        "      \"patterns\": [{"
        "        \"id\": \"p1\","
        "        \"type\": \"yes_no\","
        "        \"regex\": \"test\","
        "        \"actions\": ["
        "          {\"label\": \"Y\", \"keys\": \"y\"},"
        "          {\"keys\": \"n\"},"
        "          {\"label\": \"Maybe\"}"
        "        ]"
        "      }]"
        "    }"
        "  }"
        "}";

    nabtoshell_pattern_config *cfg = nabtoshell_pattern_config_parse(json, strlen(json));
    ck_assert_ptr_nonnull(cfg);
    const nabtoshell_agent_config *agent = nabtoshell_pattern_config_find_agent(cfg, "a");
    ck_assert_ptr_nonnull(agent);
    ck_assert_int_eq(agent->pattern_count, 1);
    ck_assert_int_eq(agent->patterns[0].action_count, 1);
    ck_assert_str_eq(agent->patterns[0].actions[0].label, "Y");
    ck_assert_str_eq(agent->patterns[0].actions[0].keys, "y");

    nabtoshell_pattern_config_free(cfg);
}
END_TEST

Suite *pattern_config_suite(void)
{
    Suite *s = suite_create("PatternConfig");
    TCase *tc = tcase_create("Core");

    tcase_add_test(tc, test_decode_bundled_format);
    tcase_add_test(tc, test_decode_numbered_menu);
    tcase_add_test(tc, test_decode_empty_patterns);
    tcase_add_test(tc, test_invalid_json);
    tcase_add_test(tc, test_missing_required_fields);
    tcase_add_test(tc, test_optional_multi_line);
    tcase_add_test(tc, test_missing_id_skips_pattern);
    tcase_add_test(tc, test_missing_regex_skips_pattern);
    tcase_add_test(tc, test_missing_type_defaults_yes_no);
    tcase_add_test(tc, test_null_action_fields_skipped);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = pattern_config_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
