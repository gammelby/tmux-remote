#ifndef PE_PROMPT_RULES_H_
#define PE_PROMPT_RULES_H_

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <stdbool.h>

#include "pe_pattern_config.h"
#include "pe_prompt.h"
#include "pe_terminal_state.h"

typedef struct {
    char* pattern_id;
    pe_prompt_type pattern_type;
    char* prompt;
    pe_prompt_action actions[PE_PROMPT_MAX_ACTIONS];
    int action_count;
    int anchor_row;
} pe_prompt_candidate;

typedef struct {
    char* pattern_id;
    pe_prompt_type pattern_type;
    int max_scan_lines;

    pcre2_code* prompt_regex;
    pcre2_code* option_regex;

    pe_prompt_action* static_actions;
    int static_action_count;
    char* action_template_keys;
} pe_compiled_prompt_rule;

typedef struct {
    pe_compiled_prompt_rule* rules;
    int count;
} pe_prompt_ruleset;

void pe_prompt_candidate_free(pe_prompt_candidate* candidate);

bool pe_prompt_candidate_to_instance(const pe_prompt_candidate* candidate,
                                     pe_prompt_instance* instance);

void pe_prompt_ruleset_init(pe_prompt_ruleset* ruleset);

void pe_prompt_ruleset_free(pe_prompt_ruleset* ruleset);

bool pe_prompt_ruleset_load(pe_prompt_ruleset* ruleset,
                            const pe_pattern_definition* definitions,
                            int definition_count);

bool pe_prompt_ruleset_match(const pe_prompt_ruleset* ruleset,
                             const pe_terminal_snapshot* snapshot,
                             pe_prompt_candidate* out_candidate);

#endif
