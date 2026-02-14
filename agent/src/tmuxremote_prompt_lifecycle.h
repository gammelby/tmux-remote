#ifndef TMUXREMOTE_PROMPT_LIFECYCLE_H_
#define TMUXREMOTE_PROMPT_LIFECYCLE_H_

#include <stdbool.h>
#include <stdint.h>

#include "tmuxremote_prompt.h"
#include "tmuxremote_prompt_rules.h"

#define TMUXREMOTE_PROMPT_ABSENCE_SNAPSHOTS 1

typedef void (*tmuxremote_prompt_lifecycle_callback)(
    tmuxremote_prompt_event_type type,
    const tmuxremote_prompt_instance* instance,
    const char* instance_id,
    void* user_data);

typedef struct {
    tmuxremote_prompt_instance active;
    bool has_active;

    int absence_snapshots;
    uint64_t last_sequence;

    char resolved_instance_id[TMUXREMOTE_PROMPT_INSTANCE_ID_MAX];
    bool suppress_resolved;

    tmuxremote_prompt_lifecycle_callback callback;
    void* callback_user_data;
} tmuxremote_prompt_lifecycle;

void tmuxremote_prompt_lifecycle_init(tmuxremote_prompt_lifecycle* lifecycle);

void tmuxremote_prompt_lifecycle_free(tmuxremote_prompt_lifecycle* lifecycle);

void tmuxremote_prompt_lifecycle_set_callback(tmuxremote_prompt_lifecycle* lifecycle,
                                              tmuxremote_prompt_lifecycle_callback callback,
                                              void* user_data);

void tmuxremote_prompt_lifecycle_process(tmuxremote_prompt_lifecycle* lifecycle,
                                         const tmuxremote_prompt_candidate* candidate,
                                         uint64_t snapshot_sequence);

void tmuxremote_prompt_lifecycle_resolve(tmuxremote_prompt_lifecycle* lifecycle,
                                         const char* instance_id);

bool tmuxremote_prompt_lifecycle_copy_active(
    tmuxremote_prompt_lifecycle* lifecycle,
    tmuxremote_prompt_instance* out_instance);

#endif
