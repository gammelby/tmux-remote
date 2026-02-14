#include "tmuxremote_prompt.h"

#include <stdlib.h>
#include <string.h>

void tmuxremote_prompt_instance_reset(tmuxremote_prompt_instance* instance)
{
    if (instance == NULL) {
        return;
    }
    memset(instance, 0, sizeof(*instance));
}

void tmuxremote_prompt_instance_free(tmuxremote_prompt_instance* instance)
{
    if (instance == NULL) {
        return;
    }

    free(instance->pattern_id);
    instance->pattern_id = NULL;

    free(instance->prompt);
    instance->prompt = NULL;

    for (int i = 0; i < instance->action_count; i++) {
        free(instance->actions[i].label);
        free(instance->actions[i].keys);
        instance->actions[i].label = NULL;
        instance->actions[i].keys = NULL;
    }
    instance->action_count = 0;
    instance->revision = 0;
    instance->anchor_row = 0;
    instance->instance_id[0] = '\0';
}

bool tmuxremote_prompt_instance_copy(const tmuxremote_prompt_instance* src,
                                     tmuxremote_prompt_instance* dst)
{
    if (src == NULL || dst == NULL) {
        return false;
    }

    tmuxremote_prompt_instance_free(dst);

    if (src->pattern_id != NULL) {
        dst->pattern_id = strdup(src->pattern_id);
        if (dst->pattern_id == NULL) {
            tmuxremote_prompt_instance_free(dst);
            return false;
        }
    }

    if (src->prompt != NULL) {
        dst->prompt = strdup(src->prompt);
        if (dst->prompt == NULL) {
            tmuxremote_prompt_instance_free(dst);
            return false;
        }
    }

    int copy_actions = src->action_count;
    if (copy_actions > TMUXREMOTE_PROMPT_MAX_ACTIONS) {
        copy_actions = TMUXREMOTE_PROMPT_MAX_ACTIONS;
    }

    for (int i = 0; i < copy_actions; i++) {
        if (src->actions[i].label != NULL) {
            dst->actions[i].label = strdup(src->actions[i].label);
            if (dst->actions[i].label == NULL) {
                tmuxremote_prompt_instance_free(dst);
                return false;
            }
        }

        if (src->actions[i].keys != NULL) {
            dst->actions[i].keys = strdup(src->actions[i].keys);
            if (dst->actions[i].keys == NULL) {
                tmuxremote_prompt_instance_free(dst);
                return false;
            }
        }
    }

    dst->action_count = copy_actions;
    dst->pattern_type = src->pattern_type;
    dst->revision = src->revision;
    dst->anchor_row = src->anchor_row;
    strncpy(dst->instance_id, src->instance_id, sizeof(dst->instance_id) - 1);
    dst->instance_id[sizeof(dst->instance_id) - 1] = '\0';

    return true;
}

static bool strings_equal(const char* a, const char* b)
{
    if (a == NULL && b == NULL) {
        return true;
    }
    if (a == NULL || b == NULL) {
        return false;
    }
    return strcmp(a, b) == 0;
}

bool tmuxremote_prompt_instance_same_semantics(
    const tmuxremote_prompt_instance* a,
    const tmuxremote_prompt_instance* b)
{
    if (a == NULL || b == NULL) {
        return false;
    }

    if (!strings_equal(a->pattern_id, b->pattern_id)) {
        return false;
    }

    if (a->pattern_type != b->pattern_type) {
        return false;
    }

    if (!strings_equal(a->prompt, b->prompt)) {
        return false;
    }

    if (a->action_count != b->action_count) {
        return false;
    }

    for (int i = 0; i < a->action_count; i++) {
        if (!strings_equal(a->actions[i].label, b->actions[i].label)) {
            return false;
        }
        if (!strings_equal(a->actions[i].keys, b->actions[i].keys)) {
            return false;
        }
    }

    if (a->anchor_row != b->anchor_row) {
        return false;
    }

    return true;
}
