#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

#include "nabtoshell_pattern_config.h"
#include "nabtoshell_prompt_detector.h"

#ifndef TEST_FIXTURES_DIR
#error "TEST_FIXTURES_DIR must be defined"
#endif

#define RECORDING_PATH TEST_FIXTURES_DIR "/pty-log-2.ptyr"
#define RECORDING_PATH_STICKY TEST_FIXTURES_DIR "/pty-log-7.ptyr"
#define RECORDING_PATH_STICKY_2 TEST_FIXTURES_DIR "/pty-log-8.ptyr"
#define RECORDING_PATH_STICKY_3 TEST_FIXTURES_DIR "/pty-log-9.ptyr"
#define RECORDING_PATH_STICKY_4 TEST_FIXTURES_DIR "/pty-log-10.ptyr"
#define CONFIG_PATH TEST_FIXTURES_DIR "/patterns.json"

typedef struct {
    uint8_t* data;
    uint32_t len;
} ptyr_frame;

typedef struct {
    nabtoshell_prompt_event_type type;
    nabtoshell_prompt_type pattern_type;
    int action_count;
    bool has_primary_option;
    bool has_charset_artifact;
    char id[NABTOSHELL_PROMPT_INSTANCE_ID_MAX];
} replay_event;

static replay_event events[2048];
static int event_count = 0;

static void on_event(nabtoshell_prompt_event_type type,
                     const nabtoshell_prompt_instance* instance,
                     const char* instance_id,
                     void* user_data)
{
    (void)user_data;
    if (event_count >= (int)(sizeof(events) / sizeof(events[0]))) {
        return;
    }

    replay_event* ev = &events[event_count++];
    memset(ev, 0, sizeof(*ev));
    ev->type = type;
    const char* id = (instance != NULL) ? instance->instance_id : instance_id;
    if (id != NULL) {
        strncpy(ev->id, id, sizeof(ev->id) - 1);
        ev->id[sizeof(ev->id) - 1] = '\0';
    } else {
        ev->id[0] = '\0';
    }

    if (instance != NULL) {
        ev->pattern_type = instance->pattern_type;
        ev->action_count = instance->action_count;
        for (int i = 0; i < instance->action_count; i++) {
            const char* keys = instance->actions[i].keys;
            const char* label = instance->actions[i].label;
            if (keys != NULL && strcmp(keys, "1") == 0) {
                ev->has_primary_option = true;
            }
            if (label != NULL && strstr(label, "/B") != NULL) {
                ev->has_charset_artifact = true;
            }
        }
    }
}

static ptyr_frame* load_recording(const char* path, int* out_count)
{
    FILE* rf = fopen(path, "rb");
    ck_assert_msg(rf != NULL, "failed to open recording: %s", path);

    uint8_t header[16];
    ck_assert_int_eq(fread(header, 1, 16, rf), 16);
    ck_assert(memcmp(header, "PTYR", 4) == 0);

    ptyr_frame* frames = NULL;
    int count = 0;
    int cap = 0;

    while (!feof(rf)) {
        uint32_t frame_len_be;
        if (fread(&frame_len_be, 1, 4, rf) < 4) {
            break;
        }

        uint32_t frame_len = ntohl(frame_len_be);
        if (frame_len == 0 || frame_len > 65536) {
            break;
        }

        uint8_t* data = malloc(frame_len);
        ck_assert_ptr_nonnull(data);
        ck_assert_int_eq(fread(data, 1, frame_len, rf), (int)frame_len);

        if (count >= cap) {
            cap = cap == 0 ? 128 : cap * 2;
            frames = realloc(frames, (size_t)cap * sizeof(ptyr_frame));
            ck_assert_ptr_nonnull(frames);
        }

        frames[count].data = data;
        frames[count].len = frame_len;
        count++;
    }

    fclose(rf);
    *out_count = count;
    return frames;
}

static void free_frames(ptyr_frame* frames, int count)
{
    for (int i = 0; i < count; i++) {
        free(frames[i].data);
    }
    free(frames);
}

static nabtoshell_pattern_config* load_config(void)
{
    FILE* f = fopen(CONFIG_PATH, "r");
    ck_assert_ptr_nonnull(f);

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    ck_assert_int_gt(size, 0);

    char* json = malloc((size_t)size + 1);
    ck_assert_ptr_nonnull(json);

    size_t n = fread(json, 1, (size_t)size, f);
    fclose(f);
    json[n] = '\0';

    nabtoshell_pattern_config* config =
        nabtoshell_pattern_config_parse(json, n);
    free(json);
    ck_assert_ptr_nonnull(config);
    return config;
}

static int count_event_type(nabtoshell_prompt_event_type type)
{
    int count = 0;
    for (int i = 0; i < event_count; i++) {
        if (events[i].type == type) {
            count++;
        }
    }
    return count;
}

static void assert_no_present_gone_present_oscillation(void)
{
    for (int i = 0; i + 2 < event_count; i++) {
        bool oscillation =
            events[i].type == NABTOSHELL_PROMPT_EVENT_PRESENT &&
            events[i + 1].type == NABTOSHELL_PROMPT_EVENT_GONE &&
            events[i + 2].type == NABTOSHELL_PROMPT_EVENT_PRESENT &&
            strcmp(events[i].id, events[i + 2].id) == 0;
        ck_assert_msg(!oscillation,
                      "unexpected oscillation for instance %s",
                      events[i].id);
    }
}

static void assert_numbered_menu_events_are_complete(void)
{
    for (int i = 0; i < event_count; i++) {
        replay_event* ev = &events[i];
        bool is_prompt_event = (ev->type == NABTOSHELL_PROMPT_EVENT_PRESENT ||
                                ev->type == NABTOSHELL_PROMPT_EVENT_UPDATE);
        if (!is_prompt_event ||
            ev->pattern_type != NABTOSHELL_PROMPT_TYPE_NUMBERED_MENU) {
            continue;
        }

        ck_assert_msg(ev->action_count >= 3,
                      "numbered menu event had only %d actions (id=%s)",
                      ev->action_count,
                      ev->id);
        ck_assert_msg(ev->has_primary_option,
                      "numbered menu event missing option 1 (id=%s)",
                      ev->id);
        ck_assert_msg(!ev->has_charset_artifact,
                      "numbered menu label contained '/B' artifact (id=%s)",
                      ev->id);
    }
}

static void replay_frames(const ptyr_frame* frames,
                          int frame_count,
                          bool split_frames,
                          nabtoshell_prompt_instance** out_active)
{
    nabtoshell_pattern_config* config = load_config();

    nabtoshell_prompt_detector detector;
    nabtoshell_prompt_detector_init(&detector, 48, 160);
    nabtoshell_prompt_detector_set_callback(&detector, on_event, NULL);
    nabtoshell_prompt_detector_load_config(&detector, config);
    nabtoshell_prompt_detector_select_agent(&detector, "claude-code");

    event_count = 0;

    for (int i = 0; i < frame_count; i++) {
        if (!split_frames || frames[i].len < 8) {
            nabtoshell_prompt_detector_feed(&detector, frames[i].data, frames[i].len);
            continue;
        }

        size_t first = frames[i].len / 2;
        nabtoshell_prompt_detector_feed(&detector, frames[i].data, first);
        nabtoshell_prompt_detector_feed(&detector,
                                        frames[i].data + first,
                                        frames[i].len - first);
    }

    *out_active = nabtoshell_prompt_detector_copy_active(&detector);

    nabtoshell_prompt_detector_free(&detector);
    nabtoshell_pattern_config_free(config);
}

START_TEST(test_replay_detects_prompts)
{
    int frame_count = 0;
    ptyr_frame* frames = load_recording(RECORDING_PATH, &frame_count);

    nabtoshell_prompt_instance* active = NULL;
    replay_frames(frames, frame_count, false, &active);

    ck_assert_int_gt(count_event_type(NABTOSHELL_PROMPT_EVENT_PRESENT), 0);
    assert_no_present_gone_present_oscillation();

    if (active != NULL) {
        nabtoshell_prompt_instance_free(active);
        free(active);
    }

    free_frames(frames, frame_count);
}
END_TEST

START_TEST(test_replay_sticky_prompt_remains_active)
{
    int frame_count = 0;
    ptyr_frame* frames = load_recording(RECORDING_PATH_STICKY, &frame_count);

    nabtoshell_prompt_instance* active = NULL;
    replay_frames(frames, frame_count, false, &active);

    ck_assert_int_gt(count_event_type(NABTOSHELL_PROMPT_EVENT_PRESENT), 0);
    assert_no_present_gone_present_oscillation();
    ck_assert_ptr_nonnull(active);

    if (active != NULL) {
        nabtoshell_prompt_instance_free(active);
        free(active);
    }

    free_frames(frames, frame_count);
}
END_TEST

START_TEST(test_replay_sticky_prompt_remains_active_2)
{
    int frame_count = 0;
    ptyr_frame* frames = load_recording(RECORDING_PATH_STICKY_2, &frame_count);

    nabtoshell_prompt_instance* active = NULL;
    replay_frames(frames, frame_count, false, &active);

    ck_assert_int_gt(count_event_type(NABTOSHELL_PROMPT_EVENT_PRESENT), 0);
    assert_no_present_gone_present_oscillation();
    ck_assert_ptr_nonnull(active);

    if (active != NULL) {
        nabtoshell_prompt_instance_free(active);
        free(active);
    }

    free_frames(frames, frame_count);
}
END_TEST

START_TEST(test_chunk_split_keeps_terminal_result)
{
    int frame_count = 0;
    ptyr_frame* frames = load_recording(RECORDING_PATH, &frame_count);

    nabtoshell_prompt_instance* active_a = NULL;
    replay_frames(frames, frame_count, false, &active_a);

    nabtoshell_prompt_instance* active_b = NULL;
    replay_frames(frames, frame_count, true, &active_b);

    if (active_a == NULL || active_b == NULL) {
        ck_assert_ptr_eq(active_a, active_b);
    } else {
        ck_assert_str_eq(active_a->instance_id, active_b->instance_id);
    }

    if (active_a != NULL) {
        nabtoshell_prompt_instance_free(active_a);
        free(active_a);
    }
    if (active_b != NULL) {
        nabtoshell_prompt_instance_free(active_b);
        free(active_b);
    }

    free_frames(frames, frame_count);
}
END_TEST

START_TEST(test_replay_sticky_prompt_remains_complete_3)
{
    int frame_count = 0;
    ptyr_frame* frames = load_recording(RECORDING_PATH_STICKY_3, &frame_count);

    nabtoshell_prompt_instance* active = NULL;
    replay_frames(frames, frame_count, false, &active);

    ck_assert_int_gt(count_event_type(NABTOSHELL_PROMPT_EVENT_PRESENT), 0);
    assert_no_present_gone_present_oscillation();
    assert_numbered_menu_events_are_complete();
    ck_assert_ptr_nonnull(active);
    ck_assert_int_ge(active->action_count, 3);

    if (active != NULL) {
        nabtoshell_prompt_instance_free(active);
        free(active);
    }

    free_frames(frames, frame_count);
}
END_TEST

START_TEST(test_replay_sticky_prompt_remains_complete_4)
{
    int frame_count = 0;
    ptyr_frame* frames = load_recording(RECORDING_PATH_STICKY_4, &frame_count);

    nabtoshell_prompt_instance* active = NULL;
    replay_frames(frames, frame_count, false, &active);

    ck_assert_int_gt(count_event_type(NABTOSHELL_PROMPT_EVENT_PRESENT), 0);
    assert_no_present_gone_present_oscillation();
    assert_numbered_menu_events_are_complete();

    if (active != NULL) {
        nabtoshell_prompt_instance_free(active);
        free(active);
    }

    free_frames(frames, frame_count);
}
END_TEST

Suite* replay_suite(void)
{
    Suite* s = suite_create("PromptDetectorReplay");
    TCase* tc = tcase_create("Core");

    tcase_add_test(tc, test_replay_detects_prompts);
    tcase_add_test(tc, test_replay_sticky_prompt_remains_active);
    tcase_add_test(tc, test_replay_sticky_prompt_remains_active_2);
    tcase_add_test(tc, test_replay_sticky_prompt_remains_complete_3);
    tcase_add_test(tc, test_replay_sticky_prompt_remains_complete_4);
    tcase_add_test(tc, test_chunk_split_keeps_terminal_result);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite* s = replay_suite();
    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
