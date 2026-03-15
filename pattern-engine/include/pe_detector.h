#ifndef PE_DETECTOR_H_
#define PE_DETECTOR_H_

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "pe_pattern_config.h"
#include "pe_prompt_lifecycle.h"
#include "pe_prompt_rules.h"
#include "pe_terminal_state.h"

typedef void (*pe_detector_callback)(
    pe_prompt_event_type type,
    const pe_prompt_instance* instance,
    const char* instance_id,
    void* user_data);

typedef struct {
    pe_terminal_state terminal_state;
    pe_prompt_ruleset ruleset;
    pe_prompt_lifecycle lifecycle;

    const pe_pattern_config* config;
    char* active_agent;

    pe_detector_callback callback;
    void* callback_user_data;

    pthread_mutex_t mutex;
} pe_detector;

void pe_detector_init(pe_detector* detector,
                      int rows,
                      int cols);

void pe_detector_free(pe_detector* detector);

void pe_detector_set_callback(pe_detector* detector,
                              pe_detector_callback callback,
                              void* user_data);

void pe_detector_load_config(pe_detector* detector,
                             const pe_pattern_config* config);

void pe_detector_select_agent(pe_detector* detector,
                              const char* agent_id);

void pe_detector_feed(pe_detector* detector,
                      const uint8_t* data,
                      size_t len);

void pe_detector_resize(pe_detector* detector,
                        int rows,
                        int cols);

void pe_detector_resolve(pe_detector* detector,
                         const char* instance_id,
                         const char* decision,
                         const char* keys);

pe_prompt_instance* pe_detector_copy_active(
    pe_detector* detector);

#endif
