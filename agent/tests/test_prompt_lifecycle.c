#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "tmuxremote_prompt_lifecycle.h"

typedef struct {
    tmuxremote_prompt_event_type types[16];
    char ids[16][TMUXREMOTE_PROMPT_INSTANCE_ID_MAX];
    int count;
} event_log;

static void on_event(tmuxremote_prompt_event_type type,
                     const tmuxremote_prompt_instance* instance,
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
        strncpy(log->ids[log->count], id, TMUXREMOTE_PROMPT_INSTANCE_ID_MAX - 1);
        log->ids[log->count][TMUXREMOTE_PROMPT_INSTANCE_ID_MAX - 1] = '\0';
    }
    log->count++;
}

static tmuxremote_prompt_candidate make_candidate(const char* pattern_id,
                                                  const char* prompt,
                                                  int anchor_row)
{
    tmuxremote_prompt_candidate c;
    memset(&c, 0, sizeof(c));
    c.pattern_id = strdup(pattern_id);
    c.pattern_type = TMUXREMOTE_PROMPT_TYPE_YES_NO;
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
    tmuxremote_prompt_lifecycle lifecycle;
    tmuxremote_prompt_lifecycle_init(&lifecycle);

    event_log log;
    memset(&log, 0, sizeof(log));
    tmuxremote_prompt_lifecycle_set_callback(&lifecycle, on_event, &log);

    tmuxremote_prompt_candidate c1 = make_candidate("p1", "Continue?", 10);
    tmuxremote_prompt_lifecycle_process(&lifecycle, &c1, 1);
    ck_assert_int_eq(log.count, 1);
    ck_assert_int_eq(log.types[0], TMUXREMOTE_PROMPT_EVENT_PRESENT);

    tmuxremote_prompt_candidate c2 = make_candidate("p1", "Continue now?", 10);
    tmuxremote_prompt_lifecycle_process(&lifecycle, &c2, 2);
    ck_assert_int_eq(log.count, 3);
    ck_assert_int_eq(log.types[1], TMUXREMOTE_PROMPT_EVENT_GONE);
    ck_assert_int_eq(log.types[2], TMUXREMOTE_PROMPT_EVENT_PRESENT);

    for (int i = 0; i < TMUXREMOTE_PROMPT_ABSENCE_SNAPSHOTS - 1; i++) {
        tmuxremote_prompt_lifecycle_process(&lifecycle, NULL, 3 + (uint64_t)i);
        ck_assert_int_eq(log.count, 3);
    }

    tmuxremote_prompt_lifecycle_process(&lifecycle,
                                        NULL,
                                        3 + (uint64_t)TMUXREMOTE_PROMPT_ABSENCE_SNAPSHOTS - 1);
    ck_assert_int_eq(log.count, 4);
    ck_assert_int_eq(log.types[3], TMUXREMOTE_PROMPT_EVENT_GONE);

    tmuxremote_prompt_candidate_free(&c1);
    tmuxremote_prompt_candidate_free(&c2);
    tmuxremote_prompt_lifecycle_free(&lifecycle);
}
END_TEST

START_TEST(test_resolve_suppresses_same_instance)
{
    tmuxremote_prompt_lifecycle lifecycle;
    tmuxremote_prompt_lifecycle_init(&lifecycle);

    event_log log;
    memset(&log, 0, sizeof(log));
    tmuxremote_prompt_lifecycle_set_callback(&lifecycle, on_event, &log);

    tmuxremote_prompt_candidate c1 = make_candidate("p1", "Continue?", 10);
    tmuxremote_prompt_lifecycle_process(&lifecycle, &c1, 1);
    ck_assert_int_eq(log.count, 1);

    char instance_id[TMUXREMOTE_PROMPT_INSTANCE_ID_MAX];
    strncpy(instance_id, log.ids[0], sizeof(instance_id) - 1);
    instance_id[sizeof(instance_id) - 1] = '\0';

    tmuxremote_prompt_lifecycle_resolve(&lifecycle, instance_id);
    ck_assert_int_eq(log.count, 2);
    ck_assert_int_eq(log.types[1], TMUXREMOTE_PROMPT_EVENT_GONE);

    tmuxremote_prompt_lifecycle_process(&lifecycle, &c1, 2);
    ck_assert_int_eq(log.count, 2);

    tmuxremote_prompt_lifecycle_process(&lifecycle, &c1, 3);
    ck_assert_int_eq(log.count, 2);

    tmuxremote_prompt_lifecycle_process(&lifecycle, NULL, 4);
    ck_assert_int_eq(log.count, 2);

    tmuxremote_prompt_lifecycle_process(&lifecycle, &c1, 5);
    ck_assert_int_eq(log.count, 3);
    ck_assert_int_eq(log.types[2], TMUXREMOTE_PROMPT_EVENT_PRESENT);

    tmuxremote_prompt_candidate_free(&c1);
    tmuxremote_prompt_lifecycle_free(&lifecycle);
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
