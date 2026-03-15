#ifndef PE_PATTERN_CONFIG_H_
#define PE_PATTERN_CONFIG_H_

#include <stddef.h>

#include "pe_prompt.h"

typedef struct {
    char* label;
    char* keys;
} pe_pattern_action;

typedef struct {
    char* keys;
} pe_pattern_action_template;

typedef struct {
    char* id;
    pe_prompt_type type;
    char* prompt_regex;
    char* option_regex;
    pe_pattern_action* actions;
    int action_count;
    pe_pattern_action_template* action_template;
    int max_scan_lines;
} pe_pattern_definition;

typedef struct {
    char* id;
    char* name;
    pe_pattern_definition* patterns;
    int pattern_count;
} pe_agent_config;

typedef struct {
    int version;
    pe_agent_config* agents;
    int agent_count;
} pe_pattern_config;

pe_pattern_config* pe_pattern_config_parse(const char* json,
                                           size_t json_len);

void pe_pattern_config_free(pe_pattern_config* config);

const pe_agent_config* pe_pattern_config_find_agent(
    const pe_pattern_config* config,
    const char* agent_id);

#endif
