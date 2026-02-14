#include "tmuxremote_prompt_detector.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWLINE "\n"

static void lifecycle_forwarder(tmuxremote_prompt_event_type type,
                                const tmuxremote_prompt_instance* instance,
                                const char* instance_id,
                                void* user_data)
{
    tmuxremote_prompt_detector* detector = user_data;
    if (detector->callback != NULL) {
        detector->callback(type,
                           instance,
                           instance_id,
                           detector->callback_user_data);
    }
}

static const tmuxremote_agent_config* default_agent(
    const tmuxremote_pattern_config* config)
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

static void reload_rules_locked(tmuxremote_prompt_detector* detector)
{
    tmuxremote_prompt_ruleset_load(&detector->ruleset, NULL, 0);

    if (detector->config == NULL || detector->active_agent == NULL) {
        return;
    }

    const tmuxremote_agent_config* agent =
        tmuxremote_pattern_config_find_agent(detector->config, detector->active_agent);
    if (agent == NULL) {
        return;
    }

    tmuxremote_prompt_ruleset_load(&detector->ruleset,
                                   agent->patterns,
                                   agent->pattern_count);
}

void tmuxremote_prompt_detector_init(tmuxremote_prompt_detector* detector,
                                     int rows,
                                     int cols)
{
    memset(detector, 0, sizeof(*detector));

    tmuxremote_terminal_state_init(&detector->terminal_state, rows, cols);
    tmuxremote_prompt_ruleset_init(&detector->ruleset);
    tmuxremote_prompt_lifecycle_init(&detector->lifecycle);
    tmuxremote_prompt_lifecycle_set_callback(&detector->lifecycle,
                                             lifecycle_forwarder,
                                             detector);

    pthread_mutex_init(&detector->mutex, NULL);
}

void tmuxremote_prompt_detector_free(tmuxremote_prompt_detector* detector)
{
    if (detector == NULL) {
        return;
    }

    pthread_mutex_lock(&detector->mutex);
    free(detector->active_agent);
    detector->active_agent = NULL;

    tmuxremote_prompt_lifecycle_free(&detector->lifecycle);
    tmuxremote_prompt_ruleset_free(&detector->ruleset);
    tmuxremote_terminal_state_free(&detector->terminal_state);
    pthread_mutex_unlock(&detector->mutex);

    pthread_mutex_destroy(&detector->mutex);
}

void tmuxremote_prompt_detector_set_callback(tmuxremote_prompt_detector* detector,
                                             tmuxremote_prompt_detector_callback callback,
                                             void* user_data)
{
    pthread_mutex_lock(&detector->mutex);
    detector->callback = callback;
    detector->callback_user_data = user_data;
    pthread_mutex_unlock(&detector->mutex);
}

void tmuxremote_prompt_detector_load_config(tmuxremote_prompt_detector* detector,
                                            const tmuxremote_pattern_config* config)
{
    pthread_mutex_lock(&detector->mutex);

    detector->config = config;

    if (detector->active_agent == NULL && config != NULL) {
        const tmuxremote_agent_config* agent = default_agent(config);
        if (agent != NULL && agent->id != NULL) {
            detector->active_agent = strdup(agent->id);
        }
    }

    reload_rules_locked(detector);
    pthread_mutex_unlock(&detector->mutex);
}

void tmuxremote_prompt_detector_select_agent(tmuxremote_prompt_detector* detector,
                                             const char* agent_id)
{
    pthread_mutex_lock(&detector->mutex);

    free(detector->active_agent);
    detector->active_agent = agent_id ? strdup(agent_id) : NULL;

    reload_rules_locked(detector);
    pthread_mutex_unlock(&detector->mutex);
}

void tmuxremote_prompt_detector_feed(tmuxremote_prompt_detector* detector,
                                     const uint8_t* data,
                                     size_t len)
{
    if (detector == NULL || data == NULL || len == 0) {
        return;
    }

    pthread_mutex_lock(&detector->mutex);

    tmuxremote_terminal_state_feed(&detector->terminal_state, data, len);

    tmuxremote_terminal_snapshot snapshot;
    if (!tmuxremote_terminal_state_snapshot(&detector->terminal_state, &snapshot)) {
        pthread_mutex_unlock(&detector->mutex);
        return;
    }

    tmuxremote_prompt_candidate candidate;
    bool has_candidate = tmuxremote_prompt_ruleset_match(
        &detector->ruleset,
        &snapshot,
        &candidate);

    if (has_candidate) {
        tmuxremote_prompt_lifecycle_process(&detector->lifecycle,
                                            &candidate,
                                            snapshot.sequence);
        tmuxremote_prompt_candidate_free(&candidate);
    } else {
        tmuxremote_prompt_lifecycle_process(&detector->lifecycle,
                                            NULL,
                                            snapshot.sequence);
    }

    tmuxremote_terminal_snapshot_free(&snapshot);

    pthread_mutex_unlock(&detector->mutex);
}

void tmuxremote_prompt_detector_resize(tmuxremote_prompt_detector* detector,
                                       int rows,
                                       int cols)
{
    if (detector == NULL) {
        return;
    }

    pthread_mutex_lock(&detector->mutex);
    tmuxremote_terminal_state_resize(&detector->terminal_state, rows, cols);
    pthread_mutex_unlock(&detector->mutex);
}

void tmuxremote_prompt_detector_resolve(tmuxremote_prompt_detector* detector,
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
    tmuxremote_prompt_lifecycle_resolve(&detector->lifecycle, instance_id);
    pthread_mutex_unlock(&detector->mutex);
}

tmuxremote_prompt_instance* tmuxremote_prompt_detector_copy_active(
    tmuxremote_prompt_detector* detector)
{
    if (detector == NULL) {
        return NULL;
    }

    tmuxremote_prompt_instance* copy = calloc(1, sizeof(*copy));
    if (copy == NULL) {
        return NULL;
    }

    pthread_mutex_lock(&detector->mutex);
    bool ok = tmuxremote_prompt_lifecycle_copy_active(&detector->lifecycle, copy);
    pthread_mutex_unlock(&detector->mutex);

    if (!ok) {
        tmuxremote_prompt_instance_free(copy);
        free(copy);
        return NULL;
    }

    return copy;
}
