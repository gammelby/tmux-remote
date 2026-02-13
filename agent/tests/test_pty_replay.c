#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "nabtoshell_pattern_engine.h"
#include "nabtoshell_pattern_config.h"

#define MAX_EVENTS 4096

typedef struct {
    bool is_match;   /* true = match, false = dismiss */
    char id[64];
    int pattern_type;
    int action_count;
    size_t bytes_fed;
} replay_event;

static replay_event events[MAX_EVENTS];
static int event_count = 0;
static size_t total_bytes_fed = 0;

static void replay_callback(const nabtoshell_pattern_match *match, void *user_data)
{
    (void)user_data;
    if (event_count >= MAX_EVENTS) return;

    replay_event *ev = &events[event_count++];
    if (match) {
        ev->is_match = true;
        strncpy(ev->id, match->id ? match->id : "", sizeof(ev->id) - 1);
        ev->id[sizeof(ev->id) - 1] = '\0';
        ev->pattern_type = match->pattern_type;
        ev->action_count = match->action_count;
    } else {
        ev->is_match = false;
        ev->id[0] = '\0';
        ev->pattern_type = 0;
        ev->action_count = 0;
    }
    ev->bytes_fed = total_bytes_fed;
}

static void print_timeline(void)
{
    fprintf(stderr, "\n=== PTY Replay Event Timeline (%d events) ===\n", event_count);
    for (int i = 0; i < event_count; i++) {
        replay_event *ev = &events[i];
        if (ev->is_match) {
            fprintf(stderr, "[%6zu bytes] MATCH   id=%-30s type=%d actions=%d\n",
                    ev->bytes_fed, ev->id, ev->pattern_type, ev->action_count);
        } else {
            fprintf(stderr, "[%6zu bytes] DISMISS\n", ev->bytes_fed);
        }
    }
    fprintf(stderr, "=== End Timeline ===\n\n");
}

START_TEST(test_pty_replay)
{
    const char *recording_path = getenv("PTY_RECORDING");
    const char *config_path = getenv("PATTERN_CONFIG");
    const char *agent_id = getenv("PATTERN_AGENT");

    if (!recording_path || !config_path) {
        fprintf(stderr, "SKIP: PTY_RECORDING and PATTERN_CONFIG env vars required\n");
        return;
    }
    if (!agent_id) {
        agent_id = "claude-code";
    }

    /* Load pattern config */
    FILE *cf = fopen(config_path, "r");
    ck_assert_msg(cf != NULL, "Failed to open pattern config: %s", config_path);
    fseek(cf, 0, SEEK_END);
    long csize = ftell(cf);
    fseek(cf, 0, SEEK_SET);
    ck_assert_msg(csize > 0 && csize < 1024 * 1024, "Invalid config file size");
    char *json = malloc(csize + 1);
    ck_assert_ptr_nonnull(json);
    size_t cread = fread(json, 1, csize, cf);
    fclose(cf);
    json[cread] = '\0';

    nabtoshell_pattern_config *config = nabtoshell_pattern_config_parse(json, cread);
    free(json);
    ck_assert_msg(config != NULL, "Failed to parse pattern config");

    /* Open recording */
    FILE *rf = fopen(recording_path, "rb");
    ck_assert_msg(rf != NULL, "Failed to open recording: %s", recording_path);

    /* Verify header */
    uint8_t header[16];
    size_t hread = fread(header, 1, 16, rf);
    ck_assert_int_eq(hread, 16);
    ck_assert(memcmp(header, "PTYR", 4) == 0);
    uint32_t version;
    memcpy(&version, header + 4, 4);
    version = ntohl(version);
    ck_assert_int_eq(version, 1);

    /* Initialize pattern engine */
    nabtoshell_pattern_engine engine;
    nabtoshell_pattern_engine_init(&engine);
    nabtoshell_pattern_engine_load_config(&engine, config);
    nabtoshell_pattern_engine_select_agent(&engine, agent_id);
    nabtoshell_pattern_engine_set_callback(&engine, replay_callback, NULL);

    /* Replay frames */
    event_count = 0;
    total_bytes_fed = 0;
    int frame_count = 0;

    while (!feof(rf)) {
        uint32_t frame_len_be;
        size_t n = fread(&frame_len_be, 1, 4, rf);
        if (n < 4) break;

        uint32_t frame_len = ntohl(frame_len_be);
        ck_assert_msg(frame_len > 0 && frame_len <= 65536,
                      "Invalid frame length: %u", frame_len);

        uint8_t *frame_data = malloc(frame_len);
        ck_assert_ptr_nonnull(frame_data);
        n = fread(frame_data, 1, frame_len, rf);
        ck_assert_int_eq(n, frame_len);

        nabtoshell_pattern_engine_feed(&engine, frame_data, frame_len);
        total_bytes_fed += frame_len;
        frame_count++;

        free(frame_data);
    }

    fclose(rf);

    fprintf(stderr, "Replayed %d frames, %zu total bytes\n", frame_count, total_bytes_fed);
    print_timeline();

    nabtoshell_pattern_engine_free(&engine);
    nabtoshell_pattern_config_free(config);
}
END_TEST

Suite *pty_replay_suite(void)
{
    Suite *s = suite_create("PTY Replay");
    TCase *tc = tcase_create("Replay");
    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test_pty_replay);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite *s = pty_replay_suite();
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
