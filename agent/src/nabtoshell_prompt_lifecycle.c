#include "nabtoshell_prompt_lifecycle.h"

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

static void build_instance_id(const nabtoshell_prompt_candidate* candidate,
                              char out_id[NABTOSHELL_PROMPT_INSTANCE_ID_MAX])
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

    snprintf(out_id, NABTOSHELL_PROMPT_INSTANCE_ID_MAX, "%016" PRIx64, hash);
}

static void emit_event(nabtoshell_prompt_lifecycle* lifecycle,
                       nabtoshell_prompt_event_type type,
                       const nabtoshell_prompt_instance* instance,
                       const char* instance_id)
{
    if (lifecycle->callback != NULL) {
        lifecycle->callback(type,
                            instance,
                            instance_id,
                            lifecycle->callback_user_data);
    }
}

static void clear_active(nabtoshell_prompt_lifecycle* lifecycle)
{
    if (!lifecycle->has_active) {
        return;
    }

    nabtoshell_prompt_instance_free(&lifecycle->active);
    lifecycle->has_active = false;
    lifecycle->absence_snapshots = 0;
}

static bool candidate_to_instance_with_id(const nabtoshell_prompt_candidate* candidate,
                                          nabtoshell_prompt_instance* out_instance)
{
    if (!nabtoshell_prompt_candidate_to_instance(candidate, out_instance)) {
        return false;
    }

    build_instance_id(candidate, out_instance->instance_id);
    return true;
}

void nabtoshell_prompt_lifecycle_init(nabtoshell_prompt_lifecycle* lifecycle)
{
    memset(lifecycle, 0, sizeof(*lifecycle));
}

void nabtoshell_prompt_lifecycle_free(nabtoshell_prompt_lifecycle* lifecycle)
{
    if (lifecycle == NULL) {
        return;
    }
    clear_active(lifecycle);
}

void nabtoshell_prompt_lifecycle_set_callback(nabtoshell_prompt_lifecycle* lifecycle,
                                              nabtoshell_prompt_lifecycle_callback callback,
                                              void* user_data)
{
    lifecycle->callback = callback;
    lifecycle->callback_user_data = user_data;
}

static bool is_suppressed(nabtoshell_prompt_lifecycle* lifecycle,
                          const char* instance_id)
{
    return lifecycle->suppress_resolved &&
           instance_id != NULL &&
           lifecycle->resolved_instance_id[0] != '\0' &&
           strcmp(instance_id, lifecycle->resolved_instance_id) == 0;
}

void nabtoshell_prompt_lifecycle_process(nabtoshell_prompt_lifecycle* lifecycle,
                                         const nabtoshell_prompt_candidate* candidate,
                                         uint64_t snapshot_sequence)
{
    lifecycle->last_sequence = snapshot_sequence;

    nabtoshell_prompt_instance incoming;
    nabtoshell_prompt_instance_reset(&incoming);

    bool has_incoming = false;
    bool suppressed_incoming = false;
    if (candidate != NULL) {
        has_incoming = candidate_to_instance_with_id(candidate, &incoming);
        if (has_incoming && is_suppressed(lifecycle, incoming.instance_id)) {
            nabtoshell_prompt_instance_free(&incoming);
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
            if (nabtoshell_prompt_instance_copy(&incoming, &lifecycle->active)) {
                lifecycle->has_active = true;
                lifecycle->absence_snapshots = 0;
                emit_event(lifecycle,
                           NABTOSHELL_PROMPT_EVENT_PRESENT,
                           &lifecycle->active,
                           lifecycle->active.instance_id);
            }
            nabtoshell_prompt_instance_free(&incoming);
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
        if (lifecycle->absence_snapshots >= NABTOSHELL_PROMPT_ABSENCE_SNAPSHOTS) {
            char gone_id[NABTOSHELL_PROMPT_INSTANCE_ID_MAX];
            strncpy(gone_id, lifecycle->active.instance_id, sizeof(gone_id) - 1);
            gone_id[sizeof(gone_id) - 1] = '\0';

            clear_active(lifecycle);
            emit_event(lifecycle,
                       NABTOSHELL_PROMPT_EVENT_GONE,
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

        if (!nabtoshell_prompt_instance_same_semantics(&lifecycle->active, &incoming)) {
            incoming.revision = lifecycle->active.revision + 1;
            clear_active(lifecycle);
            if (nabtoshell_prompt_instance_copy(&incoming, &lifecycle->active)) {
                lifecycle->has_active = true;
                emit_event(lifecycle,
                           NABTOSHELL_PROMPT_EVENT_UPDATE,
                           &lifecycle->active,
                           lifecycle->active.instance_id);
            }
        }
        nabtoshell_prompt_instance_free(&incoming);
        return;
    }

    char gone_id[NABTOSHELL_PROMPT_INSTANCE_ID_MAX];
    strncpy(gone_id, lifecycle->active.instance_id, sizeof(gone_id) - 1);
    gone_id[sizeof(gone_id) - 1] = '\0';

    clear_active(lifecycle);
    emit_event(lifecycle, NABTOSHELL_PROMPT_EVENT_GONE, NULL, gone_id);

    incoming.revision = 1;
    if (nabtoshell_prompt_instance_copy(&incoming, &lifecycle->active)) {
        lifecycle->has_active = true;
        emit_event(lifecycle,
                   NABTOSHELL_PROMPT_EVENT_PRESENT,
                   &lifecycle->active,
                   lifecycle->active.instance_id);
    }

    nabtoshell_prompt_instance_free(&incoming);
}

void nabtoshell_prompt_lifecycle_resolve(nabtoshell_prompt_lifecycle* lifecycle,
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
        char gone_id[NABTOSHELL_PROMPT_INSTANCE_ID_MAX];
        strncpy(gone_id, lifecycle->active.instance_id, sizeof(gone_id) - 1);
        gone_id[sizeof(gone_id) - 1] = '\0';

        clear_active(lifecycle);
        emit_event(lifecycle, NABTOSHELL_PROMPT_EVENT_GONE, NULL, gone_id);
    }
}

bool nabtoshell_prompt_lifecycle_copy_active(
    nabtoshell_prompt_lifecycle* lifecycle,
    nabtoshell_prompt_instance* out_instance)
{
    if (lifecycle == NULL || out_instance == NULL || !lifecycle->has_active) {
        return false;
    }

    return nabtoshell_prompt_instance_copy(&lifecycle->active, out_instance);
}
