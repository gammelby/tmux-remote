#ifndef TMUXREMOTE_PROMPT_DETECTOR_H_
#define TMUXREMOTE_PROMPT_DETECTOR_H_

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "tmuxremote_pattern_config.h"
#include "tmuxremote_prompt_lifecycle.h"
#include "tmuxremote_prompt_rules.h"
#include "tmuxremote_terminal_state.h"

typedef void (*tmuxremote_prompt_detector_callback)(
    tmuxremote_prompt_event_type type,
    const tmuxremote_prompt_instance* instance,
    const char* instance_id,
    void* user_data);

typedef struct {
    tmuxremote_terminal_state terminal_state;
    tmuxremote_prompt_ruleset ruleset;
    tmuxremote_prompt_lifecycle lifecycle;

    const tmuxremote_pattern_config* config;
    char* active_agent;

    tmuxremote_prompt_detector_callback callback;
    void* callback_user_data;

    pthread_mutex_t mutex;
} tmuxremote_prompt_detector;

void tmuxremote_prompt_detector_init(tmuxremote_prompt_detector* detector,
                                     int rows,
                                     int cols);

void tmuxremote_prompt_detector_free(tmuxremote_prompt_detector* detector);

void tmuxremote_prompt_detector_set_callback(tmuxremote_prompt_detector* detector,
                                             tmuxremote_prompt_detector_callback callback,
                                             void* user_data);

void tmuxremote_prompt_detector_load_config(tmuxremote_prompt_detector* detector,
                                            const tmuxremote_pattern_config* config);

void tmuxremote_prompt_detector_select_agent(tmuxremote_prompt_detector* detector,
                                             const char* agent_id);

void tmuxremote_prompt_detector_feed(tmuxremote_prompt_detector* detector,
                                     const uint8_t* data,
                                     size_t len);

void tmuxremote_prompt_detector_resize(tmuxremote_prompt_detector* detector,
                                       int rows,
                                       int cols);

void tmuxremote_prompt_detector_resolve(tmuxremote_prompt_detector* detector,
                                        const char* instance_id,
                                        const char* decision,
                                        const char* keys);

tmuxremote_prompt_instance* tmuxremote_prompt_detector_copy_active(
    tmuxremote_prompt_detector* detector);

#endif
