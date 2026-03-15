#ifndef PE_PROMPT_H_
#define PE_PROMPT_H_

#include <stdbool.h>
#include <stdint.h>

#define PE_PROMPT_MAX_ACTIONS 16
#define PE_PROMPT_INSTANCE_ID_MAX 32

typedef enum {
    PE_PROMPT_TYPE_YES_NO = 0,
    PE_PROMPT_TYPE_NUMBERED_MENU = 1,
    PE_PROMPT_TYPE_ACCEPT_REJECT = 2
} pe_prompt_type;

typedef struct {
    char* label;
    char* keys;
} pe_prompt_action;

typedef struct {
    char instance_id[PE_PROMPT_INSTANCE_ID_MAX];
    char* pattern_id;
    pe_prompt_type pattern_type;
    char* prompt;
    pe_prompt_action actions[PE_PROMPT_MAX_ACTIONS];
    int action_count;
    uint32_t revision;
    int anchor_row;
} pe_prompt_instance;

typedef enum {
    PE_PROMPT_EVENT_PRESENT = 0,
    PE_PROMPT_EVENT_UPDATE = 1,
    PE_PROMPT_EVENT_GONE = 2
} pe_prompt_event_type;

void pe_prompt_instance_reset(pe_prompt_instance* instance);
void pe_prompt_instance_free(pe_prompt_instance* instance);

bool pe_prompt_instance_copy(const pe_prompt_instance* src,
                             pe_prompt_instance* dst);

bool pe_prompt_instance_same_semantics(
    const pe_prompt_instance* a,
    const pe_prompt_instance* b);

#endif
