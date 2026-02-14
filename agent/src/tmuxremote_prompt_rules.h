#ifndef TMUXREMOTE_PROMPT_RULES_H_
#define TMUXREMOTE_PROMPT_RULES_H_

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <stdbool.h>

#include "tmuxremote_pattern_config.h"
#include "tmuxremote_prompt.h"
#include "tmuxremote_terminal_state.h"

typedef struct {
    char* pattern_id;
    tmuxremote_prompt_type pattern_type;
    char* prompt;
    tmuxremote_prompt_action actions[TMUXREMOTE_PROMPT_MAX_ACTIONS];
    int action_count;
    int anchor_row;
} tmuxremote_prompt_candidate;

typedef struct {
    char* pattern_id;
    tmuxremote_prompt_type pattern_type;
    int max_scan_lines;

    pcre2_code* prompt_regex;
    pcre2_code* option_regex;

    tmuxremote_prompt_action* static_actions;
    int static_action_count;
    char* action_template_keys;
} tmuxremote_compiled_prompt_rule;

typedef struct {
    tmuxremote_compiled_prompt_rule* rules;
    int count;
} tmuxremote_prompt_ruleset;

void tmuxremote_prompt_candidate_free(tmuxremote_prompt_candidate* candidate);

bool tmuxremote_prompt_candidate_to_instance(const tmuxremote_prompt_candidate* candidate,
                                             tmuxremote_prompt_instance* instance);

void tmuxremote_prompt_ruleset_init(tmuxremote_prompt_ruleset* ruleset);

void tmuxremote_prompt_ruleset_free(tmuxremote_prompt_ruleset* ruleset);

bool tmuxremote_prompt_ruleset_load(tmuxremote_prompt_ruleset* ruleset,
                                    const tmuxremote_pattern_definition* definitions,
                                    int definition_count);

bool tmuxremote_prompt_ruleset_match(const tmuxremote_prompt_ruleset* ruleset,
                                     const tmuxremote_terminal_snapshot* snapshot,
                                     tmuxremote_prompt_candidate* out_candidate);

#endif
