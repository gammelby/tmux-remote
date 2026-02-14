#ifndef TMUXREMOTE_PATTERN_CONFIG_H_
#define TMUXREMOTE_PATTERN_CONFIG_H_

#include <stddef.h>

#include "tmuxremote_prompt.h"

typedef struct {
    char* label;
    char* keys;
} tmuxremote_pattern_action;

typedef struct {
    char* keys;
} tmuxremote_pattern_action_template;

typedef struct {
    char* id;
    tmuxremote_prompt_type type;
    char* prompt_regex;
    char* option_regex;
    tmuxremote_pattern_action* actions;
    int action_count;
    tmuxremote_pattern_action_template* action_template;
    int max_scan_lines;
} tmuxremote_pattern_definition;

typedef struct {
    char* id;
    char* name;
    tmuxremote_pattern_definition* patterns;
    int pattern_count;
} tmuxremote_agent_config;

typedef struct {
    int version;
    tmuxremote_agent_config* agents;
    int agent_count;
} tmuxremote_pattern_config;

tmuxremote_pattern_config* tmuxremote_pattern_config_parse(const char* json,
                                                           size_t json_len);

void tmuxremote_pattern_config_free(tmuxremote_pattern_config* config);

const tmuxremote_agent_config* tmuxremote_pattern_config_find_agent(
    const tmuxremote_pattern_config* config,
    const char* agent_id);

#endif
