#ifndef PE_PROMPT_LIFECYCLE_H_
#define PE_PROMPT_LIFECYCLE_H_

#include <stdbool.h>
#include <stdint.h>

#include "pe_prompt.h"
#include "pe_prompt_rules.h"

#define PE_PROMPT_ABSENCE_SNAPSHOTS 2

typedef void (*pe_prompt_lifecycle_callback)(
    pe_prompt_event_type type,
    const pe_prompt_instance* instance,
    const char* instance_id,
    void* user_data);

typedef struct {
    pe_prompt_instance active;
    bool has_active;

    int absence_snapshots;
    uint64_t last_sequence;

    char resolved_instance_id[PE_PROMPT_INSTANCE_ID_MAX];
    bool suppress_resolved;

    pe_prompt_lifecycle_callback callback;
    void* callback_user_data;
} pe_prompt_lifecycle;

void pe_prompt_lifecycle_init(pe_prompt_lifecycle* lifecycle);

void pe_prompt_lifecycle_free(pe_prompt_lifecycle* lifecycle);

void pe_prompt_lifecycle_set_callback(pe_prompt_lifecycle* lifecycle,
                                      pe_prompt_lifecycle_callback callback,
                                      void* user_data);

void pe_prompt_lifecycle_process(pe_prompt_lifecycle* lifecycle,
                                 const pe_prompt_candidate* candidate,
                                 uint64_t snapshot_sequence);

void pe_prompt_lifecycle_resolve(pe_prompt_lifecycle* lifecycle,
                                 const char* instance_id);

bool pe_prompt_lifecycle_copy_active(
    pe_prompt_lifecycle* lifecycle,
    pe_prompt_instance* out_instance);

#endif
