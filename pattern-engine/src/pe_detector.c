#include "pe_detector.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWLINE "\n"

static void lifecycle_forwarder(pe_prompt_event_type type,
                                const pe_prompt_instance* instance,
                                const char* instance_id,
                                void* user_data)
{
    pe_detector* detector = user_data;
    if (detector->callback != NULL) {
        detector->callback(type,
                           instance,
                           instance_id,
                           detector->callback_user_data);
    }
}

static const pe_agent_config* default_agent(
    const pe_pattern_config* config)
{
    if (config == NULL) {
        return NULL;
    }

    for (int i = 0; i < config->agent_count; i++) {
        if (config->agents[i].pattern_count > 0 &&
            config->agents[i].id != NULL) {
            return &config->agents[i];
        }
    }

    return NULL;
}

static void reload_rules_locked(pe_detector* detector)
{
    pe_prompt_ruleset_load(&detector->ruleset, NULL, 0);

    if (detector->config == NULL || detector->active_agent == NULL) {
        return;
    }

    const pe_agent_config* agent =
        pe_pattern_config_find_agent(detector->config, detector->active_agent);
    if (agent == NULL) {
        return;
    }

    pe_prompt_ruleset_load(&detector->ruleset,
                            agent->patterns,
                            agent->pattern_count);
}

void pe_detector_init(pe_detector* detector,
                       int rows,
                       int cols)
{
    memset(detector, 0, sizeof(*detector));

    pe_terminal_state_init(&detector->terminal_state, rows, cols);
    pe_prompt_ruleset_init(&detector->ruleset);
    pe_prompt_lifecycle_init(&detector->lifecycle);
    pe_prompt_lifecycle_set_callback(&detector->lifecycle,
                                     lifecycle_forwarder,
                                     detector);

    pthread_mutex_init(&detector->mutex, NULL);
}

void pe_detector_free(pe_detector* detector)
{
    if (detector == NULL) {
        return;
    }

    pthread_mutex_lock(&detector->mutex);
    free(detector->active_agent);
    detector->active_agent = NULL;

    pe_prompt_lifecycle_free(&detector->lifecycle);
    pe_prompt_ruleset_free(&detector->ruleset);
    pe_terminal_state_free(&detector->terminal_state);
    pthread_mutex_unlock(&detector->mutex);

    pthread_mutex_destroy(&detector->mutex);
}

void pe_detector_set_callback(pe_detector* detector,
                               pe_detector_callback callback,
                               void* user_data)
{
    pthread_mutex_lock(&detector->mutex);
    detector->callback = callback;
    detector->callback_user_data = user_data;
    pthread_mutex_unlock(&detector->mutex);
}

void pe_detector_load_config(pe_detector* detector,
                              const pe_pattern_config* config)
{
    pthread_mutex_lock(&detector->mutex);

    detector->config = config;

    if (detector->active_agent == NULL && config != NULL) {
        const pe_agent_config* agent = default_agent(config);
        if (agent != NULL && agent->id != NULL) {
            detector->active_agent = strdup(agent->id);
        }
    }

    reload_rules_locked(detector);
    pthread_mutex_unlock(&detector->mutex);
}

void pe_detector_select_agent(pe_detector* detector,
                               const char* agent_id)
{
    pthread_mutex_lock(&detector->mutex);

    free(detector->active_agent);
    detector->active_agent = agent_id ? strdup(agent_id) : NULL;

    reload_rules_locked(detector);
    pthread_mutex_unlock(&detector->mutex);
}

void pe_detector_feed(pe_detector* detector,
                       const uint8_t* data,
                       size_t len)
{
    if (detector == NULL || data == NULL || len == 0) {
        return;
    }

    pthread_mutex_lock(&detector->mutex);

    pe_terminal_state_feed(&detector->terminal_state, data, len);

    pe_terminal_snapshot snapshot;
    if (!pe_terminal_state_snapshot(&detector->terminal_state, &snapshot)) {
        pthread_mutex_unlock(&detector->mutex);
        return;
    }

    pe_prompt_candidate candidate;
    bool has_candidate = pe_prompt_ruleset_match(
        &detector->ruleset,
        &snapshot,
        &candidate);

    if (has_candidate) {
        pe_prompt_lifecycle_process(&detector->lifecycle,
                                    &candidate,
                                    snapshot.sequence);
        pe_prompt_candidate_free(&candidate);
    } else {
        pe_prompt_lifecycle_process(&detector->lifecycle,
                                    NULL,
                                    snapshot.sequence);
    }

    pe_terminal_snapshot_free(&snapshot);

    pthread_mutex_unlock(&detector->mutex);
}

void pe_detector_resize(pe_detector* detector,
                         int rows,
                         int cols)
{
    if (detector == NULL) {
        return;
    }

    pthread_mutex_lock(&detector->mutex);
    pe_terminal_state_resize(&detector->terminal_state, rows, cols);
    pthread_mutex_unlock(&detector->mutex);
}

void pe_detector_resolve(pe_detector* detector,
                          const char* instance_id,
                          const char* decision,
                          const char* keys)
{
    (void)decision;
    (void)keys;

    if (detector == NULL || instance_id == NULL || instance_id[0] == '\0') {
        return;
    }

    pthread_mutex_lock(&detector->mutex);
    pe_prompt_lifecycle_resolve(&detector->lifecycle, instance_id);
    pthread_mutex_unlock(&detector->mutex);
}

pe_prompt_instance* pe_detector_copy_active(
    pe_detector* detector)
{
    if (detector == NULL) {
        return NULL;
    }

    pe_prompt_instance* copy = calloc(1, sizeof(*copy));
    if (copy == NULL) {
        return NULL;
    }

    pthread_mutex_lock(&detector->mutex);
    bool ok = pe_prompt_lifecycle_copy_active(&detector->lifecycle, copy);
    pthread_mutex_unlock(&detector->mutex);

    if (!ok) {
        pe_prompt_instance_free(copy);
        free(copy);
        return NULL;
    }

    return copy;
}
