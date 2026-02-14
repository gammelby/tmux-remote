#ifndef TMUXREMOTE_PROMPT_H_
#define TMUXREMOTE_PROMPT_H_

#include <stdbool.h>
#include <stdint.h>

#define TMUXREMOTE_PROMPT_MAX_ACTIONS 16
#define TMUXREMOTE_PROMPT_INSTANCE_ID_MAX 32

typedef enum {
    TMUXREMOTE_PROMPT_TYPE_YES_NO = 0,
    TMUXREMOTE_PROMPT_TYPE_NUMBERED_MENU = 1,
    TMUXREMOTE_PROMPT_TYPE_ACCEPT_REJECT = 2
} tmuxremote_prompt_type;

typedef struct {
    char* label;
    char* keys;
} tmuxremote_prompt_action;

typedef struct {
    char instance_id[TMUXREMOTE_PROMPT_INSTANCE_ID_MAX];
    char* pattern_id;
    tmuxremote_prompt_type pattern_type;
    char* prompt;
    tmuxremote_prompt_action actions[TMUXREMOTE_PROMPT_MAX_ACTIONS];
    int action_count;
    uint32_t revision;
    int anchor_row;
} tmuxremote_prompt_instance;

typedef enum {
    TMUXREMOTE_PROMPT_EVENT_PRESENT = 0,
    TMUXREMOTE_PROMPT_EVENT_UPDATE = 1,
    TMUXREMOTE_PROMPT_EVENT_GONE = 2
} tmuxremote_prompt_event_type;

void tmuxremote_prompt_instance_reset(tmuxremote_prompt_instance* instance);
void tmuxremote_prompt_instance_free(tmuxremote_prompt_instance* instance);

bool tmuxremote_prompt_instance_copy(const tmuxremote_prompt_instance* src,
                                     tmuxremote_prompt_instance* dst);

bool tmuxremote_prompt_instance_same_semantics(
    const tmuxremote_prompt_instance* a,
    const tmuxremote_prompt_instance* b);

#endif
