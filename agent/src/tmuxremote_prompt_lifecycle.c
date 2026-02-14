#include "tmuxremote_prompt_lifecycle.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWLINE "\n"

static uint64_t fnv1a64(const void* data, size_t len, uint64_t seed)
{
    const uint8_t* ptr = data;
    uint64_t hash = seed;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)ptr[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void build_instance_id(const tmuxremote_prompt_candidate* candidate,
                              char out_id[TMUXREMOTE_PROMPT_INSTANCE_ID_MAX])
{
    uint64_t hash = 1469598103934665603ULL;

    if (candidate->pattern_id != NULL) {
        hash = fnv1a64(candidate->pattern_id, strlen(candidate->pattern_id), hash);
    }

    hash = fnv1a64(&candidate->pattern_type, sizeof(candidate->pattern_type), hash);

    if (candidate->prompt != NULL) {
        hash = fnv1a64(candidate->prompt, strlen(candidate->prompt), hash);
    }

    for (int i = 0; i < candidate->action_count; i++) {
        if (candidate->actions[i].label != NULL) {
            hash = fnv1a64(candidate->actions[i].label,
                          strlen(candidate->actions[i].label),
                          hash);
        }
        if (candidate->actions[i].keys != NULL) {
            hash = fnv1a64(candidate->actions[i].keys,
                          strlen(candidate->actions[i].keys),
                          hash);
        }
    }

    hash = fnv1a64(&candidate->anchor_row, sizeof(candidate->anchor_row), hash);

    snprintf(out_id, TMUXREMOTE_PROMPT_INSTANCE_ID_MAX, "%016" PRIx64, hash);
}

static void emit_event(tmuxremote_prompt_lifecycle* lifecycle,
                       tmuxremote_prompt_event_type type,
                       const tmuxremote_prompt_instance* instance,
                       const char* instance_id)
{
    if (lifecycle->callback != NULL) {
        lifecycle->callback(type,
                            instance,
                            instance_id,
                            lifecycle->callback_user_data);
    }
}

static void clear_active(tmuxremote_prompt_lifecycle* lifecycle)
{
    if (!lifecycle->has_active) {
        return;
    }

    tmuxremote_prompt_instance_free(&lifecycle->active);
    lifecycle->has_active = false;
    lifecycle->absence_snapshots = 0;
}

static bool candidate_to_instance_with_id(const tmuxremote_prompt_candidate* candidate,
                                          tmuxremote_prompt_instance* out_instance)
{
    if (!tmuxremote_prompt_candidate_to_instance(candidate, out_instance)) {
        return false;
    }

    build_instance_id(candidate, out_instance->instance_id);
    return true;
}

void tmuxremote_prompt_lifecycle_init(tmuxremote_prompt_lifecycle* lifecycle)
{
    memset(lifecycle, 0, sizeof(*lifecycle));
}

void tmuxremote_prompt_lifecycle_free(tmuxremote_prompt_lifecycle* lifecycle)
{
    if (lifecycle == NULL) {
        return;
    }
    clear_active(lifecycle);
}

void tmuxremote_prompt_lifecycle_set_callback(tmuxremote_prompt_lifecycle* lifecycle,
                                              tmuxremote_prompt_lifecycle_callback callback,
                                              void* user_data)
{
    lifecycle->callback = callback;
    lifecycle->callback_user_data = user_data;
}

static bool is_suppressed(tmuxremote_prompt_lifecycle* lifecycle,
                          const char* instance_id)
{
    return lifecycle->suppress_resolved &&
           instance_id != NULL &&
           lifecycle->resolved_instance_id[0] != '\0' &&
           strcmp(instance_id, lifecycle->resolved_instance_id) == 0;
}

void tmuxremote_prompt_lifecycle_process(tmuxremote_prompt_lifecycle* lifecycle,
                                         const tmuxremote_prompt_candidate* candidate,
                                         uint64_t snapshot_sequence)
{
    lifecycle->last_sequence = snapshot_sequence;

    tmuxremote_prompt_instance incoming;
    tmuxremote_prompt_instance_reset(&incoming);

    bool has_incoming = false;
    bool suppressed_incoming = false;
    if (candidate != NULL) {
        has_incoming = candidate_to_instance_with_id(candidate, &incoming);
        if (has_incoming && is_suppressed(lifecycle, incoming.instance_id)) {
            tmuxremote_prompt_instance_free(&incoming);
            has_incoming = false;
            suppressed_incoming = true;
        }
    }

    if (has_incoming && lifecycle->suppress_resolved) {
        lifecycle->suppress_resolved = false;
        lifecycle->resolved_instance_id[0] = '\0';
    }

    if (!lifecycle->has_active) {
        if (has_incoming) {
            incoming.revision = 1;
            if (tmuxremote_prompt_instance_copy(&incoming, &lifecycle->active)) {
                lifecycle->has_active = true;
                lifecycle->absence_snapshots = 0;
                emit_event(lifecycle,
                           TMUXREMOTE_PROMPT_EVENT_PRESENT,
                           &lifecycle->active,
                           lifecycle->active.instance_id);
            }
            tmuxremote_prompt_instance_free(&incoming);
            return;
        }

        if (lifecycle->suppress_resolved && !suppressed_incoming) {
            lifecycle->suppress_resolved = false;
            lifecycle->resolved_instance_id[0] = '\0';
        }

        return;
    }

    if (!has_incoming) {
        lifecycle->absence_snapshots++;
        if (lifecycle->absence_snapshots >= TMUXREMOTE_PROMPT_ABSENCE_SNAPSHOTS) {
            char gone_id[TMUXREMOTE_PROMPT_INSTANCE_ID_MAX];
            strncpy(gone_id, lifecycle->active.instance_id, sizeof(gone_id) - 1);
            gone_id[sizeof(gone_id) - 1] = '\0';

            clear_active(lifecycle);
            emit_event(lifecycle,
                       TMUXREMOTE_PROMPT_EVENT_GONE,
                       NULL,
                       gone_id);

            lifecycle->suppress_resolved = false;
            lifecycle->resolved_instance_id[0] = '\0';
        }
        return;
    }

    lifecycle->absence_snapshots = 0;

    if (strcmp(lifecycle->active.instance_id, incoming.instance_id) == 0) {
        incoming.revision = lifecycle->active.revision;

        if (!tmuxremote_prompt_instance_same_semantics(&lifecycle->active, &incoming)) {
            incoming.revision = lifecycle->active.revision + 1;
            clear_active(lifecycle);
            if (tmuxremote_prompt_instance_copy(&incoming, &lifecycle->active)) {
                lifecycle->has_active = true;
                emit_event(lifecycle,
                           TMUXREMOTE_PROMPT_EVENT_UPDATE,
                           &lifecycle->active,
                           lifecycle->active.instance_id);
            }
        }
        tmuxremote_prompt_instance_free(&incoming);
        return;
    }

    char gone_id[TMUXREMOTE_PROMPT_INSTANCE_ID_MAX];
    strncpy(gone_id, lifecycle->active.instance_id, sizeof(gone_id) - 1);
    gone_id[sizeof(gone_id) - 1] = '\0';

    clear_active(lifecycle);
    emit_event(lifecycle, TMUXREMOTE_PROMPT_EVENT_GONE, NULL, gone_id);

    incoming.revision = 1;
    if (tmuxremote_prompt_instance_copy(&incoming, &lifecycle->active)) {
        lifecycle->has_active = true;
        emit_event(lifecycle,
                   TMUXREMOTE_PROMPT_EVENT_PRESENT,
                   &lifecycle->active,
                   lifecycle->active.instance_id);
    }

    tmuxremote_prompt_instance_free(&incoming);
}

void tmuxremote_prompt_lifecycle_resolve(tmuxremote_prompt_lifecycle* lifecycle,
                                         const char* instance_id)
{
    if (instance_id == NULL || instance_id[0] == '\0') {
        return;
    }

    strncpy(lifecycle->resolved_instance_id,
            instance_id,
            sizeof(lifecycle->resolved_instance_id) - 1);
    lifecycle->resolved_instance_id[sizeof(lifecycle->resolved_instance_id) - 1] = '\0';
    lifecycle->suppress_resolved = true;

    if (lifecycle->has_active &&
        strcmp(lifecycle->active.instance_id, instance_id) == 0) {
        char gone_id[TMUXREMOTE_PROMPT_INSTANCE_ID_MAX];
        strncpy(gone_id, lifecycle->active.instance_id, sizeof(gone_id) - 1);
        gone_id[sizeof(gone_id) - 1] = '\0';

        clear_active(lifecycle);
        emit_event(lifecycle, TMUXREMOTE_PROMPT_EVENT_GONE, NULL, gone_id);
    }
}

bool tmuxremote_prompt_lifecycle_copy_active(
    tmuxremote_prompt_lifecycle* lifecycle,
    tmuxremote_prompt_instance* out_instance)
{
    if (lifecycle == NULL || out_instance == NULL || !lifecycle->has_active) {
        return false;
    }

    return tmuxremote_prompt_instance_copy(&lifecycle->active, out_instance);
}
