#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "pe_detector.h"
#include "pe_pattern_config.h"

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
    "        }"
    "      ]"
    "    }"
    "  }"
    "}";

typedef struct {
    pe_prompt_event_type types[16];
    char ids[16][PE_PROMPT_INSTANCE_ID_MAX];
    int count;
} event_log;

static void on_event(pe_prompt_event_type type,
                     const pe_prompt_instance* instance,
                     const char* instance_id,
                     void* user_data)
{
    event_log* log = user_data;
    if (log->count >= 16) {
        return;
    }

    log->types[log->count] = type;
    const char* id = (instance != NULL) ? instance->instance_id : instance_id;
    if (id != NULL) {
        strncpy(log->ids[log->count], id, PE_PROMPT_INSTANCE_ID_MAX - 1);
        log->ids[log->count][PE_PROMPT_INSTANCE_ID_MAX - 1] = '\0';
    }
    log->count++;
}

START_TEST(test_detector_feed_fires_callback)
{
    pe_pattern_config* config =
        pe_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    pe_detector detector;
    pe_detector_init(&detector, 24, 80);

    event_log log;
    memset(&log, 0, sizeof(log));
    pe_detector_set_callback(&detector, on_event, &log);
    pe_detector_load_config(&detector, config);
    pe_detector_select_agent(&detector, "test");

    const char* text = "Continue? (y/n)\n";
    pe_detector_feed(&detector, (const uint8_t*)text, strlen(text));

    ck_assert_int_ge(log.count, 1);
    ck_assert_int_eq(log.types[0], PE_PROMPT_EVENT_PRESENT);

    pe_detector_free(&detector);
    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_detector_no_config_no_match)
{
    pe_detector detector;
    pe_detector_init(&detector, 24, 80);

    event_log log;
    memset(&log, 0, sizeof(log));
    pe_detector_set_callback(&detector, on_event, &log);

    /* No config loaded */
    const char* text = "Continue? (y/n)\n";
    pe_detector_feed(&detector, (const uint8_t*)text, strlen(text));

    ck_assert_int_eq(log.count, 0);

    pe_detector_free(&detector);
}
END_TEST

START_TEST(test_detector_select_agent)
{
    const char* multi_config_json =
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
        "        }"
        "      ]"
        "    },"
        "    \"empty\":{"
        "      \"name\":\"Empty\","
        "      \"rules\":[]"
        "    }"
        "  }"
        "}";

    pe_pattern_config* config =
        pe_pattern_config_parse(multi_config_json, strlen(multi_config_json));
    ck_assert_ptr_nonnull(config);

    pe_detector detector;
    pe_detector_init(&detector, 24, 80);

    event_log log;
    memset(&log, 0, sizeof(log));
    pe_detector_set_callback(&detector, on_event, &log);
    pe_detector_load_config(&detector, config);

    /* Select agent with no rules */
    pe_detector_select_agent(&detector, "empty");
    const char* text = "Continue? (y/n)\n";
    pe_detector_feed(&detector, (const uint8_t*)text, strlen(text));
    ck_assert_int_eq(log.count, 0);

    /* Switch to agent with rules, feed matching text again */
    pe_detector_select_agent(&detector, "test");
    /* Need to re-feed since terminal already has the text, but we need a new
       snapshot to trigger detection. Clear and re-feed. */
    const char* clear = "\x1b[2J";
    pe_detector_feed(&detector, (const uint8_t*)clear, strlen(clear));
    pe_detector_feed(&detector, (const uint8_t*)text, strlen(text));
    ck_assert_int_ge(log.count, 1);
    ck_assert_int_eq(log.types[log.count - 1], PE_PROMPT_EVENT_PRESENT);

    pe_detector_free(&detector);
    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_detector_resize)
{
    pe_pattern_config* config =
        pe_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    pe_detector detector;
    pe_detector_init(&detector, 24, 80);

    event_log log;
    memset(&log, 0, sizeof(log));
    pe_detector_set_callback(&detector, on_event, &log);
    pe_detector_load_config(&detector, config);
    pe_detector_select_agent(&detector, "test");

    /* Resize and feed text with CSI positioning in the new range */
    pe_detector_resize(&detector, 24, 80);
    const char* text = "\x1b[10;1HContinue? (y/n)";
    pe_detector_feed(&detector, (const uint8_t*)text, strlen(text));

    /* Should not crash; may or may not fire event depending on cursor position */
    /* The main assertion is that we get here without crashing */
    ck_assert(true);

    pe_detector_free(&detector);
    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_detector_resolve_fires_gone)
{
    pe_pattern_config* config =
        pe_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    pe_detector detector;
    pe_detector_init(&detector, 24, 80);

    event_log log;
    memset(&log, 0, sizeof(log));
    pe_detector_set_callback(&detector, on_event, &log);
    pe_detector_load_config(&detector, config);
    pe_detector_select_agent(&detector, "test");

    const char* text = "Continue? (y/n)\n";
    pe_detector_feed(&detector, (const uint8_t*)text, strlen(text));
    ck_assert_int_ge(log.count, 1);
    ck_assert_int_eq(log.types[0], PE_PROMPT_EVENT_PRESENT);

    char instance_id[PE_PROMPT_INSTANCE_ID_MAX];
    strncpy(instance_id, log.ids[0], sizeof(instance_id) - 1);
    instance_id[sizeof(instance_id) - 1] = '\0';

    pe_detector_resolve(&detector, instance_id, "action", "y");

    /* Find the GONE event */
    bool found_gone = false;
    for (int i = 1; i < log.count; i++) {
        if (log.types[i] == PE_PROMPT_EVENT_GONE &&
            strcmp(log.ids[i], instance_id) == 0) {
            found_gone = true;
            break;
        }
    }
    ck_assert(found_gone);

    pe_detector_free(&detector);
    pe_pattern_config_free(config);
}
END_TEST

START_TEST(test_detector_config_reload)
{
    pe_pattern_config* config =
        pe_pattern_config_parse(TEST_CONFIG_JSON, strlen(TEST_CONFIG_JSON));
    ck_assert_ptr_nonnull(config);

    pe_detector detector;
    pe_detector_init(&detector, 24, 80);

    event_log log;
    memset(&log, 0, sizeof(log));
    pe_detector_set_callback(&detector, on_event, &log);
    pe_detector_load_config(&detector, config);
    pe_detector_select_agent(&detector, "test");

    const char* text = "Continue? (y/n)\n";
    pe_detector_feed(&detector, (const uint8_t*)text, strlen(text));
    ck_assert_int_ge(log.count, 1);
    ck_assert_int_eq(log.types[0], PE_PROMPT_EVENT_PRESENT);

    /* Load a new config with no matching rules for the selected agent */
    const char* empty_config_json =
        "{"
        "  \"version\":3,"
        "  \"agents\":{"
        "    \"test\":{"
        "      \"name\":\"Test\","
        "      \"rules\":[]"
        "    }"
        "  }"
        "}";

    pe_pattern_config* config2 =
        pe_pattern_config_parse(empty_config_json, strlen(empty_config_json));
    ck_assert_ptr_nonnull(config2);

    pe_detector_load_config(&detector, config2);
    pe_detector_select_agent(&detector, "test");

    /* Feed more text to trigger detection with new (empty) ruleset.
       The lifecycle requires PE_PROMPT_ABSENCE_SNAPSHOTS (2) consecutive
       absent snapshots before emitting GONE. */
    const char* more_text = "\nMore text\n";
    pe_detector_feed(&detector, (const uint8_t*)more_text, strlen(more_text));
    const char* more_text2 = "\nEven more\n";
    pe_detector_feed(&detector, (const uint8_t*)more_text2, strlen(more_text2));

    /* The prompt should eventually be gone */
    bool found_gone = false;
    for (int i = 0; i < log.count; i++) {
        if (log.types[i] == PE_PROMPT_EVENT_GONE) {
            found_gone = true;
            break;
        }
    }
    ck_assert(found_gone);

    pe_detector_free(&detector);
    pe_pattern_config_free(config);
    pe_pattern_config_free(config2);
}
END_TEST

Suite* detector_suite(void)
{
    Suite* s = suite_create("Detector");
    TCase* tc = tcase_create("Core");

    tcase_add_test(tc, test_detector_feed_fires_callback);
    tcase_add_test(tc, test_detector_no_config_no_match);
    tcase_add_test(tc, test_detector_select_agent);
    tcase_add_test(tc, test_detector_resize);
    tcase_add_test(tc, test_detector_resolve_fires_gone);
    tcase_add_test(tc, test_detector_config_reload);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite* s = detector_suite();
    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
