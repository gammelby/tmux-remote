#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "nabtoshell_prompt_lifecycle.h"

typedef struct {
    nabtoshell_prompt_event_type types[16];
    char ids[16][NABTOSHELL_PROMPT_INSTANCE_ID_MAX];
    int count;
} event_log;

static void on_event(nabtoshell_prompt_event_type type,
                     const nabtoshell_prompt_instance* instance,
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
        strncpy(log->ids[log->count], id, NABTOSHELL_PROMPT_INSTANCE_ID_MAX - 1);
        log->ids[log->count][NABTOSHELL_PROMPT_INSTANCE_ID_MAX - 1] = '\0';
    }
    log->count++;
}

static nabtoshell_prompt_candidate make_candidate(const char* pattern_id,
                                                  const char* prompt,
                                                  int anchor_row)
{
    nabtoshell_prompt_candidate c;
    memset(&c, 0, sizeof(c));
    c.pattern_id = strdup(pattern_id);
    c.pattern_type = NABTOSHELL_PROMPT_TYPE_YES_NO;
    c.prompt = strdup(prompt);
    c.anchor_row = anchor_row;
    c.actions[0].label = strdup("Yes");
    c.actions[0].keys = strdup("y");
    c.actions[1].label = strdup("No");
    c.actions[1].keys = strdup("n");
    c.action_count = 2;
    return c;
}

START_TEST(test_present_replacement_and_gone)
{
    nabtoshell_prompt_lifecycle lifecycle;
    nabtoshell_prompt_lifecycle_init(&lifecycle);

    event_log log;
    memset(&log, 0, sizeof(log));
    nabtoshell_prompt_lifecycle_set_callback(&lifecycle, on_event, &log);

    nabtoshell_prompt_candidate c1 = make_candidate("p1", "Continue?", 10);
    nabtoshell_prompt_lifecycle_process(&lifecycle, &c1, 1);
    ck_assert_int_eq(log.count, 1);
    ck_assert_int_eq(log.types[0], NABTOSHELL_PROMPT_EVENT_PRESENT);

    nabtoshell_prompt_candidate c2 = make_candidate("p1", "Continue now?", 10);
    nabtoshell_prompt_lifecycle_process(&lifecycle, &c2, 2);
    ck_assert_int_eq(log.count, 3);
    ck_assert_int_eq(log.types[1], NABTOSHELL_PROMPT_EVENT_GONE);
    ck_assert_int_eq(log.types[2], NABTOSHELL_PROMPT_EVENT_PRESENT);

    for (int i = 0; i < NABTOSHELL_PROMPT_ABSENCE_SNAPSHOTS - 1; i++) {
        nabtoshell_prompt_lifecycle_process(&lifecycle, NULL, 3 + (uint64_t)i);
        ck_assert_int_eq(log.count, 3);
    }

    nabtoshell_prompt_lifecycle_process(&lifecycle,
                                        NULL,
                                        3 + (uint64_t)NABTOSHELL_PROMPT_ABSENCE_SNAPSHOTS - 1);
    ck_assert_int_eq(log.count, 4);
    ck_assert_int_eq(log.types[3], NABTOSHELL_PROMPT_EVENT_GONE);

    nabtoshell_prompt_candidate_free(&c1);
    nabtoshell_prompt_candidate_free(&c2);
    nabtoshell_prompt_lifecycle_free(&lifecycle);
}
END_TEST

START_TEST(test_resolve_suppresses_same_instance)
{
    nabtoshell_prompt_lifecycle lifecycle;
    nabtoshell_prompt_lifecycle_init(&lifecycle);

    event_log log;
    memset(&log, 0, sizeof(log));
    nabtoshell_prompt_lifecycle_set_callback(&lifecycle, on_event, &log);

    nabtoshell_prompt_candidate c1 = make_candidate("p1", "Continue?", 10);
    nabtoshell_prompt_lifecycle_process(&lifecycle, &c1, 1);
    ck_assert_int_eq(log.count, 1);

    char instance_id[NABTOSHELL_PROMPT_INSTANCE_ID_MAX];
    strncpy(instance_id, log.ids[0], sizeof(instance_id) - 1);
    instance_id[sizeof(instance_id) - 1] = '\0';

    nabtoshell_prompt_lifecycle_resolve(&lifecycle, instance_id);
    ck_assert_int_eq(log.count, 2);
    ck_assert_int_eq(log.types[1], NABTOSHELL_PROMPT_EVENT_GONE);

    nabtoshell_prompt_lifecycle_process(&lifecycle, &c1, 2);
    ck_assert_int_eq(log.count, 2);

    nabtoshell_prompt_lifecycle_process(&lifecycle, &c1, 3);
    ck_assert_int_eq(log.count, 2);

    nabtoshell_prompt_lifecycle_process(&lifecycle, NULL, 4);
    ck_assert_int_eq(log.count, 2);

    nabtoshell_prompt_lifecycle_process(&lifecycle, &c1, 5);
    ck_assert_int_eq(log.count, 3);
    ck_assert_int_eq(log.types[2], NABTOSHELL_PROMPT_EVENT_PRESENT);

    nabtoshell_prompt_candidate_free(&c1);
    nabtoshell_prompt_lifecycle_free(&lifecycle);
}
END_TEST

Suite* prompt_lifecycle_suite(void)
{
    Suite* s = suite_create("PromptLifecycle");
    TCase* tc = tcase_create("Core");

    tcase_add_test(tc, test_present_replacement_and_gone);
    tcase_add_test(tc, test_resolve_suppresses_same_instance);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite* s = prompt_lifecycle_suite();
    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
