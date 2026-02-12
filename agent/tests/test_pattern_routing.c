#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>

#include "nabtoshell_pattern_engine.h"
#include "nabtoshell_pattern_config.h"
#include "nabtoshell_pattern_matcher.h"
#include "nabtoshell_pattern_cbor.h"
#include "nabtoshell_stream.h"
#include "nabtoshell_control_stream.h"

/* ----------------------------------------------------------------
 * Test-local implementations of functions from stream.c and
 * control_stream.c. These avoid linking the full agent which
 * depends on the Nabto SDK runtime.
 * ---------------------------------------------------------------- */

nabtoshell_pattern_match *nabtoshell_stream_copy_active_match_for_ref(
    struct nabtoshell_stream_listener *sl,
    NabtoDeviceConnectionRef ref)
{
    nabtoshell_pattern_match *result = NULL;
    pthread_mutex_lock(&sl->activeStreamsMutex);
    struct nabtoshell_active_stream *as = sl->activeStreams;
    while (as != NULL) {
        if (!atomic_load(&as->closing) && as->patternEngineInitialized &&
            as->connectionRef == ref) {
            result = nabtoshell_pattern_engine_copy_active_match(&as->patternEngine);
            break;
        }
        as = as->next;
    }
    pthread_mutex_unlock(&sl->activeStreamsMutex);
    return result;
}

static void control_stream_retain(struct nabtoshell_active_control_stream *cs)
{
    atomic_fetch_add_explicit(&cs->refCount, 1, memory_order_relaxed);
}

void nabtoshell_control_stream_release(struct nabtoshell_active_control_stream *cs)
{
    if (atomic_fetch_sub_explicit(&cs->refCount, 1, memory_order_acq_rel) == 1) {
        /* In tests, objects are stack-allocated, so don't free */
    }
}

int nabtoshell_control_stream_collect_targets_for_ref(
    struct nabtoshell_control_stream_listener *csl,
    NabtoDeviceConnectionRef ref,
    struct nabtoshell_active_control_stream **out,
    int cap)
{
    int count = 0;
    pthread_mutex_lock(&csl->streamListMutex);
    struct nabtoshell_active_control_stream *cs = csl->activeStreams;
    while (cs != NULL && count < cap) {
        if (!atomic_load(&cs->closing) && cs->connectionRef == ref) {
            control_stream_retain(cs);
            out[count++] = cs;
        }
        cs = cs->next;
    }
    pthread_mutex_unlock(&csl->streamListMutex);
    return count;
}

/* ----------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------- */

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
    "      },"
    "      {"
    "        \"id\": \"menu\","
    "        \"type\": \"numbered_menu\","
    "        \"regex\": \"Pick one:\\\\n.*1\\\\. .+\\\\n.*2\\\\. .+\","
    "        \"multi_line\": true,"
    "        \"action_template\": {\"keys\": \"{number}\\n\"}"
    "      }]"
    "    }"
    "  }"
    "}";

static const char *multi_agent_config_json =
    "{"
    "  \"version\": 1,"
    "  \"agents\": {"
    "    \"claude-code\": {"
    "      \"name\": \"Claude Code\","
    "      \"patterns\": [{"
    "        \"id\": \"yn\","
    "        \"type\": \"yes_no\","
    "        \"regex\": \"Continue\\\\? \\\\(y/n\\\\)\","
    "        \"actions\": ["
    "          {\"label\": \"Yes\", \"keys\": \"y\"},"
    "          {\"label\": \"No\", \"keys\": \"n\"}"
    "        ]"
    "      }]"
    "    },"
    "    \"aider\": {"
    "      \"name\": \"Aider\","
    "      \"patterns\": [{"
    "        \"id\": \"confirm\","
    "        \"type\": \"yes_no\","
    "        \"regex\": \"Proceed\\\\? \\\\(y/n\\\\)\","
    "        \"actions\": ["
    "          {\"label\": \"OK\", \"keys\": \"y\"},"
    "          {\"label\": \"Cancel\", \"keys\": \"n\"}"
    "        ]"
    "      }]"
    "    }"
    "  }"
    "}";

/* Per-engine callback state */
struct engine_cb_state {
    nabtoshell_pattern_match *last_match;
    bool last_was_dismiss;
    int cb_count;
};

static void engine_callback(const nabtoshell_pattern_match *match, void *user_data)
{
    struct engine_cb_state *state = user_data;
    state->cb_count++;
    nabtoshell_pattern_match_free(state->last_match);
    state->last_match = nabtoshell_pattern_match_copy(match);
    state->last_was_dismiss = (match == NULL);
}

static void cb_state_init(struct engine_cb_state *state)
{
    memset(state, 0, sizeof(*state));
}

static void cb_state_free(struct engine_cb_state *state)
{
    nabtoshell_pattern_match_free(state->last_match);
    state->last_match = NULL;
}

static void feed_string(nabtoshell_pattern_engine *e, const char *s)
{
    nabtoshell_pattern_engine_feed(e, (const uint8_t *)s, strlen(s));
}

/* ================================================================
 * Test 1: Two engines get independent matches
 * ================================================================ */

START_TEST(test_two_engines_independent_matches)
{
    nabtoshell_pattern_config *config =
        nabtoshell_pattern_config_parse(test_config_json, strlen(test_config_json));
    ck_assert_ptr_nonnull(config);

    nabtoshell_pattern_engine engine_a, engine_b;
    struct engine_cb_state cb_a, cb_b;
    cb_state_init(&cb_a);
    cb_state_init(&cb_b);

    nabtoshell_pattern_engine_init(&engine_a);
    nabtoshell_pattern_engine_init(&engine_b);
    nabtoshell_pattern_engine_load_config(&engine_a, config);
    nabtoshell_pattern_engine_load_config(&engine_b, config);
    nabtoshell_pattern_engine_set_callback(&engine_a, engine_callback, &cb_a);
    nabtoshell_pattern_engine_set_callback(&engine_b, engine_callback, &cb_b);
    nabtoshell_pattern_engine_select_agent(&engine_a, "test");
    nabtoshell_pattern_engine_select_agent(&engine_b, "test");

    /* Feed yes/no prompt to engine A */
    feed_string(&engine_a, "Continue? (y/n)");
    /* Feed numbered menu to engine B */
    feed_string(&engine_b, "Pick one:\n1. Alpha\n2. Beta\n3. Gamma");

    /* Verify A matched yes/no */
    ck_assert_ptr_nonnull(cb_a.last_match);
    ck_assert_str_eq(cb_a.last_match->id, "yn");
    ck_assert(!cb_a.last_was_dismiss);

    /* Verify B matched menu */
    ck_assert_ptr_nonnull(cb_b.last_match);
    ck_assert_str_eq(cb_b.last_match->id, "menu");
    ck_assert(!cb_b.last_was_dismiss);

    cb_state_free(&cb_a);
    cb_state_free(&cb_b);
    nabtoshell_pattern_engine_free(&engine_a);
    nabtoshell_pattern_engine_free(&engine_b);
    nabtoshell_pattern_config_free(config);
}
END_TEST

/* ================================================================
 * Test 2: Dismiss one engine, no effect on other
 * ================================================================ */

START_TEST(test_dismiss_one_engine_no_effect_on_other)
{
    nabtoshell_pattern_config *config =
        nabtoshell_pattern_config_parse(test_config_json, strlen(test_config_json));

    nabtoshell_pattern_engine engine_a, engine_b;
    struct engine_cb_state cb_a, cb_b;
    cb_state_init(&cb_a);
    cb_state_init(&cb_b);

    nabtoshell_pattern_engine_init(&engine_a);
    nabtoshell_pattern_engine_init(&engine_b);
    nabtoshell_pattern_engine_load_config(&engine_a, config);
    nabtoshell_pattern_engine_load_config(&engine_b, config);
    nabtoshell_pattern_engine_set_callback(&engine_a, engine_callback, &cb_a);
    nabtoshell_pattern_engine_set_callback(&engine_b, engine_callback, &cb_b);
    nabtoshell_pattern_engine_select_agent(&engine_a, "test");
    nabtoshell_pattern_engine_select_agent(&engine_b, "test");

    /* Both get the same prompt */
    feed_string(&engine_a, "Continue? (y/n)");
    feed_string(&engine_b, "Continue? (y/n)");

    ck_assert_ptr_nonnull(cb_a.last_match);
    ck_assert_ptr_nonnull(cb_b.last_match);

    /* Dismiss engine A */
    nabtoshell_pattern_engine_dismiss(&engine_a);
    ck_assert(cb_a.last_was_dismiss);

    /* Engine B's match should be unchanged */
    nabtoshell_pattern_match *b_copy =
        nabtoshell_pattern_engine_copy_active_match(&engine_b);
    ck_assert_ptr_nonnull(b_copy);
    ck_assert_str_eq(b_copy->id, "yn");
    nabtoshell_pattern_match_free(b_copy);

    cb_state_free(&cb_a);
    cb_state_free(&cb_b);
    nabtoshell_pattern_engine_free(&engine_a);
    nabtoshell_pattern_engine_free(&engine_b);
    nabtoshell_pattern_config_free(config);
}
END_TEST

/* ================================================================
 * Test 3: copy_active_match_for_ref returns correct ref
 * ================================================================ */

START_TEST(test_copy_match_for_ref_correct_ref)
{
    nabtoshell_pattern_config *config =
        nabtoshell_pattern_config_parse(test_config_json, strlen(test_config_json));

    /* Build a minimal stream listener with 2 entries */
    struct nabtoshell_stream_listener sl;
    memset(&sl, 0, sizeof(sl));
    pthread_mutex_init(&sl.activeStreamsMutex, NULL);

    struct nabtoshell_active_stream as1, as2;
    memset(&as1, 0, sizeof(as1));
    memset(&as2, 0, sizeof(as2));
    atomic_init(&as1.closing, false);
    atomic_init(&as2.closing, false);
    as1.connectionRef = 1;
    as2.connectionRef = 2;

    nabtoshell_pattern_engine_init(&as1.patternEngine);
    as1.patternEngineInitialized = true;
    nabtoshell_pattern_engine_load_config(&as1.patternEngine, config);
    nabtoshell_pattern_engine_select_agent(&as1.patternEngine, "test");
    nabtoshell_pattern_engine_feed(&as1.patternEngine,
        (const uint8_t *)"Continue? (y/n)", 15);

    nabtoshell_pattern_engine_init(&as2.patternEngine);
    as2.patternEngineInitialized = true;
    nabtoshell_pattern_engine_load_config(&as2.patternEngine, config);
    nabtoshell_pattern_engine_select_agent(&as2.patternEngine, "test");
    /* No prompt fed to engine 2 */

    as1.next = &as2;
    as2.next = NULL;
    sl.activeStreams = &as1;

    /* Ref 1 should return a match */
    nabtoshell_pattern_match *m1 =
        nabtoshell_stream_copy_active_match_for_ref(&sl, 1);
    ck_assert_ptr_nonnull(m1);
    ck_assert_str_eq(m1->id, "yn");
    nabtoshell_pattern_match_free(m1);

    /* Ref 2 should return NULL (no match) */
    nabtoshell_pattern_match *m2 =
        nabtoshell_stream_copy_active_match_for_ref(&sl, 2);
    ck_assert_ptr_null(m2);

    /* Ref 999 should return NULL (no such stream) */
    nabtoshell_pattern_match *m999 =
        nabtoshell_stream_copy_active_match_for_ref(&sl, 999);
    ck_assert_ptr_null(m999);

    nabtoshell_pattern_engine_free(&as1.patternEngine);
    nabtoshell_pattern_engine_free(&as2.patternEngine);
    pthread_mutex_destroy(&sl.activeStreamsMutex);
    nabtoshell_pattern_config_free(config);
}
END_TEST

/* ================================================================
 * Test 4: copy_active_match_for_ref after dismiss
 * ================================================================ */

START_TEST(test_copy_match_for_ref_after_dismiss)
{
    nabtoshell_pattern_config *config =
        nabtoshell_pattern_config_parse(test_config_json, strlen(test_config_json));

    struct nabtoshell_stream_listener sl;
    memset(&sl, 0, sizeof(sl));
    pthread_mutex_init(&sl.activeStreamsMutex, NULL);

    struct nabtoshell_active_stream as1;
    memset(&as1, 0, sizeof(as1));
    atomic_init(&as1.closing, false);
    as1.connectionRef = 1;

    nabtoshell_pattern_engine_init(&as1.patternEngine);
    as1.patternEngineInitialized = true;
    nabtoshell_pattern_engine_load_config(&as1.patternEngine, config);
    nabtoshell_pattern_engine_select_agent(&as1.patternEngine, "test");
    nabtoshell_pattern_engine_feed(&as1.patternEngine,
        (const uint8_t *)"Continue? (y/n)", 15);

    as1.next = NULL;
    sl.activeStreams = &as1;

    /* Verify match exists first */
    nabtoshell_pattern_match *m =
        nabtoshell_stream_copy_active_match_for_ref(&sl, 1);
    ck_assert_ptr_nonnull(m);
    nabtoshell_pattern_match_free(m);

    /* Dismiss */
    nabtoshell_pattern_engine_dismiss(&as1.patternEngine);

    /* Should now return NULL */
    m = nabtoshell_stream_copy_active_match_for_ref(&sl, 1);
    ck_assert_ptr_null(m);

    nabtoshell_pattern_engine_free(&as1.patternEngine);
    pthread_mutex_destroy(&sl.activeStreamsMutex);
    nabtoshell_pattern_config_free(config);
}
END_TEST

/* ================================================================
 * Test 5: copy_active_match_for_ref skips closing streams
 * ================================================================ */

START_TEST(test_copy_match_for_ref_closing_stream_skipped)
{
    nabtoshell_pattern_config *config =
        nabtoshell_pattern_config_parse(test_config_json, strlen(test_config_json));

    struct nabtoshell_stream_listener sl;
    memset(&sl, 0, sizeof(sl));
    pthread_mutex_init(&sl.activeStreamsMutex, NULL);

    struct nabtoshell_active_stream as1;
    memset(&as1, 0, sizeof(as1));
    atomic_init(&as1.closing, false);
    as1.connectionRef = 1;

    nabtoshell_pattern_engine_init(&as1.patternEngine);
    as1.patternEngineInitialized = true;
    nabtoshell_pattern_engine_load_config(&as1.patternEngine, config);
    nabtoshell_pattern_engine_select_agent(&as1.patternEngine, "test");
    nabtoshell_pattern_engine_feed(&as1.patternEngine,
        (const uint8_t *)"Continue? (y/n)", 15);

    as1.next = NULL;
    sl.activeStreams = &as1;

    /* Mark as closing */
    atomic_store(&as1.closing, true);

    /* Should return NULL (closing streams are skipped) */
    nabtoshell_pattern_match *m =
        nabtoshell_stream_copy_active_match_for_ref(&sl, 1);
    ck_assert_ptr_null(m);

    nabtoshell_pattern_engine_free(&as1.patternEngine);
    pthread_mutex_destroy(&sl.activeStreamsMutex);
    nabtoshell_pattern_config_free(config);
}
END_TEST

/* ================================================================
 * Test 6: Auto-detect per engine
 * ================================================================ */

START_TEST(test_auto_detect_per_engine)
{
    nabtoshell_pattern_config *config =
        nabtoshell_pattern_config_parse(multi_agent_config_json,
                                         strlen(multi_agent_config_json));
    ck_assert_ptr_nonnull(config);
    ck_assert_int_eq(config->agent_count, 2);

    nabtoshell_pattern_engine engine_a, engine_b;
    struct engine_cb_state cb_a, cb_b;
    cb_state_init(&cb_a);
    cb_state_init(&cb_b);

    nabtoshell_pattern_engine_init(&engine_a);
    nabtoshell_pattern_engine_init(&engine_b);
    nabtoshell_pattern_engine_load_config(&engine_a, config);
    nabtoshell_pattern_engine_load_config(&engine_b, config);
    nabtoshell_pattern_engine_set_callback(&engine_a, engine_callback, &cb_a);
    nabtoshell_pattern_engine_set_callback(&engine_b, engine_callback, &cb_b);
    /* No agent selected: auto-detect */

    /* Feed "claude code" text + prompt to engine A */
    feed_string(&engine_a, "Welcome to Claude Code v1.0\n");
    feed_string(&engine_a, "Continue? (y/n)");

    /* Engine A should auto-detect and match */
    ck_assert_ptr_nonnull(cb_a.last_match);
    ck_assert_str_eq(cb_a.last_match->id, "yn");

    /* Engine B has no agent text, no match */
    feed_string(&engine_b, "Some random text\n");
    nabtoshell_pattern_match *b_copy =
        nabtoshell_pattern_engine_copy_active_match(&engine_b);
    ck_assert_ptr_null(b_copy);

    cb_state_free(&cb_a);
    cb_state_free(&cb_b);
    nabtoshell_pattern_engine_free(&engine_a);
    nabtoshell_pattern_engine_free(&engine_b);
    nabtoshell_pattern_config_free(config);
}
END_TEST

/* ================================================================
 * Test 7: collect_targets_for_ref filters correctly
 * ================================================================ */

START_TEST(test_collect_targets_for_ref_filters_correctly)
{
    struct nabtoshell_control_stream_listener csl;
    memset(&csl, 0, sizeof(csl));
    pthread_mutex_init(&csl.streamListMutex, NULL);

    /* Create 4 control stream entries: refs {1, 2, 1, 1(closing)} */
    struct nabtoshell_active_control_stream cs1, cs2, cs3, cs4;
    memset(&cs1, 0, sizeof(cs1));
    memset(&cs2, 0, sizeof(cs2));
    memset(&cs3, 0, sizeof(cs3));
    memset(&cs4, 0, sizeof(cs4));

    atomic_init(&cs1.closing, false);
    atomic_init(&cs2.closing, false);
    atomic_init(&cs3.closing, false);
    atomic_init(&cs4.closing, true);  /* closing */
    atomic_init(&cs1.refCount, 1);
    atomic_init(&cs2.refCount, 1);
    atomic_init(&cs3.refCount, 1);
    atomic_init(&cs4.refCount, 1);
    cs1.connectionRef = 1;
    cs2.connectionRef = 2;
    cs3.connectionRef = 1;
    cs4.connectionRef = 1;

    cs1.next = &cs2;
    cs2.next = &cs3;
    cs3.next = &cs4;
    cs4.next = NULL;
    csl.activeStreams = &cs1;

    struct nabtoshell_active_control_stream *out[MAX_CONTROL_STREAMS];
    int count = nabtoshell_control_stream_collect_targets_for_ref(
        &csl, 1, out, MAX_CONTROL_STREAMS);

    /* Should find cs1 and cs3 (cs4 is closing) */
    ck_assert_int_eq(count, 2);
    ck_assert_ptr_eq(out[0], &cs1);
    ck_assert_ptr_eq(out[1], &cs3);

    /* Verify refcounts were bumped */
    ck_assert_uint_eq(atomic_load(&cs1.refCount), 2);
    ck_assert_uint_eq(atomic_load(&cs3.refCount), 2);
    ck_assert_uint_eq(atomic_load(&cs2.refCount), 1);  /* not collected */

    /* Release the retained references */
    for (int i = 0; i < count; i++) {
        nabtoshell_control_stream_release(out[i]);
    }

    /* Refcounts should be back to 1 */
    ck_assert_uint_eq(atomic_load(&cs1.refCount), 1);
    ck_assert_uint_eq(atomic_load(&cs3.refCount), 1);

    pthread_mutex_destroy(&csl.streamListMutex);
}
END_TEST

/* ================================================================
 * Test 8: collect_targets_for_ref refcount balanced
 * ================================================================ */

START_TEST(test_collect_targets_for_ref_refcount_balanced)
{
    struct nabtoshell_control_stream_listener csl;
    memset(&csl, 0, sizeof(csl));
    pthread_mutex_init(&csl.streamListMutex, NULL);

    struct nabtoshell_active_control_stream cs1, cs2;
    memset(&cs1, 0, sizeof(cs1));
    memset(&cs2, 0, sizeof(cs2));
    atomic_init(&cs1.closing, false);
    atomic_init(&cs2.closing, false);
    atomic_init(&cs1.refCount, 1);
    atomic_init(&cs2.refCount, 1);
    cs1.connectionRef = 42;
    cs2.connectionRef = 42;
    cs1.next = &cs2;
    cs2.next = NULL;
    csl.activeStreams = &cs1;

    struct nabtoshell_active_control_stream *out[MAX_CONTROL_STREAMS];
    int count = nabtoshell_control_stream_collect_targets_for_ref(
        &csl, 42, out, MAX_CONTROL_STREAMS);
    ck_assert_int_eq(count, 2);

    /* Both should have refCount 2 (1 original + 1 from collect) */
    ck_assert_uint_eq(atomic_load(&cs1.refCount), 2);
    ck_assert_uint_eq(atomic_load(&cs2.refCount), 2);

    /* Release all */
    for (int i = 0; i < count; i++) {
        nabtoshell_control_stream_release(out[i]);
    }

    /* Back to 1: net delta is zero */
    ck_assert_uint_eq(atomic_load(&cs1.refCount), 1);
    ck_assert_uint_eq(atomic_load(&cs2.refCount), 1);

    pthread_mutex_destroy(&csl.streamListMutex);
}
END_TEST

/* ================================================================
 * Suite setup
 * ================================================================ */

Suite *routing_suite(void)
{
    Suite *s = suite_create("Pattern Routing");
    TCase *tc = tcase_create("Core");

    tcase_add_test(tc, test_two_engines_independent_matches);
    tcase_add_test(tc, test_dismiss_one_engine_no_effect_on_other);
    tcase_add_test(tc, test_copy_match_for_ref_correct_ref);
    tcase_add_test(tc, test_copy_match_for_ref_after_dismiss);
    tcase_add_test(tc, test_copy_match_for_ref_closing_stream_skipped);
    tcase_add_test(tc, test_auto_detect_per_engine);
    tcase_add_test(tc, test_collect_targets_for_ref_filters_correctly);
    tcase_add_test(tc, test_collect_targets_for_ref_refcount_balanced);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    SRunner *sr = srunner_create(routing_suite());
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
