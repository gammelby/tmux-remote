#include "nabtoshell_prompt_rules.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWLINE "\n"

static void free_rule(nabtoshell_compiled_prompt_rule* rule)
{
    if (rule == NULL) {
        return;
    }

    free(rule->pattern_id);
    rule->pattern_id = NULL;

    if (rule->prompt_regex != NULL) {
        pcre2_code_free(rule->prompt_regex);
        rule->prompt_regex = NULL;
    }

    if (rule->option_regex != NULL) {
        pcre2_code_free(rule->option_regex);
        rule->option_regex = NULL;
    }

    for (int i = 0; i < rule->static_action_count; i++) {
        free(rule->static_actions[i].label);
        free(rule->static_actions[i].keys);
    }
    free(rule->static_actions);
    rule->static_actions = NULL;
    rule->static_action_count = 0;

    free(rule->action_template_keys);
    rule->action_template_keys = NULL;
    rule->max_scan_lines = 0;
}

void nabtoshell_prompt_candidate_free(nabtoshell_prompt_candidate* candidate)
{
    if (candidate == NULL) {
        return;
    }

    free(candidate->pattern_id);
    candidate->pattern_id = NULL;

    free(candidate->prompt);
    candidate->prompt = NULL;

    for (int i = 0; i < candidate->action_count; i++) {
        free(candidate->actions[i].label);
        free(candidate->actions[i].keys);
    }

    candidate->action_count = 0;
    candidate->anchor_row = 0;
}

void nabtoshell_prompt_ruleset_init(nabtoshell_prompt_ruleset* ruleset)
{
    memset(ruleset, 0, sizeof(*ruleset));
}

void nabtoshell_prompt_ruleset_free(nabtoshell_prompt_ruleset* ruleset)
{
    if (ruleset == NULL) {
        return;
    }

    for (int i = 0; i < ruleset->count; i++) {
        free_rule(&ruleset->rules[i]);
    }

    free(ruleset->rules);
    ruleset->rules = NULL;
    ruleset->count = 0;
}

static pcre2_code* compile_regex(const char* pattern)
{
    int errorcode = 0;
    PCRE2_SIZE erroroffset = 0;
    pcre2_code* compiled = pcre2_compile(
        (PCRE2_SPTR)pattern,
        PCRE2_ZERO_TERMINATED,
        PCRE2_MULTILINE | PCRE2_UTF,
        &errorcode,
        &erroroffset,
        NULL);

    if (compiled == NULL) {
        printf("Prompt rule regex compile failed at offset %zu (error=%d): %s" NEWLINE,
               (size_t)erroroffset,
               errorcode,
               pattern);
    }

    return compiled;
}

static bool copy_static_actions(nabtoshell_compiled_prompt_rule* dst,
                                const nabtoshell_pattern_definition* src)
{
    if (src->action_count <= 0) {
        return true;
    }

    dst->static_actions = calloc((size_t)src->action_count,
                                 sizeof(nabtoshell_prompt_action));
    if (dst->static_actions == NULL) {
        return false;
    }

    for (int i = 0; i < src->action_count; i++) {
        dst->static_actions[i].label = src->actions[i].label ? strdup(src->actions[i].label) : NULL;
        dst->static_actions[i].keys = src->actions[i].keys ? strdup(src->actions[i].keys) : NULL;

        if (dst->static_actions[i].label == NULL || dst->static_actions[i].keys == NULL) {
            return false;
        }
    }

    dst->static_action_count = src->action_count;
    return true;
}

bool nabtoshell_prompt_ruleset_load(nabtoshell_prompt_ruleset* ruleset,
                                    const nabtoshell_pattern_definition* definitions,
                                    int definition_count)
{
    if (ruleset == NULL) {
        return false;
    }

    nabtoshell_prompt_ruleset_free(ruleset);

    if (definitions == NULL || definition_count <= 0) {
        return true;
    }

    ruleset->rules = calloc((size_t)definition_count,
                            sizeof(nabtoshell_compiled_prompt_rule));
    if (ruleset->rules == NULL) {
        return false;
    }

    int valid = 0;

    for (int i = 0; i < definition_count; i++) {
        const nabtoshell_pattern_definition* def = &definitions[i];
        nabtoshell_compiled_prompt_rule rule;
        memset(&rule, 0, sizeof(rule));

        if (def->id == NULL || def->prompt_regex == NULL) {
            continue;
        }

        rule.pattern_id = strdup(def->id);
        rule.pattern_type = def->type;
        rule.max_scan_lines = def->max_scan_lines > 0 ? def->max_scan_lines : 10;

        rule.prompt_regex = compile_regex(def->prompt_regex);
        if (rule.prompt_regex == NULL || rule.pattern_id == NULL) {
            free_rule(&rule);
            continue;
        }

        if (def->option_regex != NULL) {
            rule.option_regex = compile_regex(def->option_regex);
            if (rule.option_regex == NULL) {
                free_rule(&rule);
                continue;
            }
        }

        if (!copy_static_actions(&rule, def)) {
            free_rule(&rule);
            continue;
        }

        if (def->action_template != NULL && def->action_template->keys != NULL) {
            rule.action_template_keys = strdup(def->action_template->keys);
            if (rule.action_template_keys == NULL) {
                free_rule(&rule);
                continue;
            }
        }

        ruleset->rules[valid] = rule;
        valid++;
    }

    ruleset->count = valid;
    return true;
}

static bool regex_matches_line(pcre2_code* regex, const char* line)
{
    if (regex == NULL || line == NULL) {
        return false;
    }

    pcre2_match_data* match = pcre2_match_data_create_from_pattern(regex, NULL);
    if (match == NULL) {
        return false;
    }

    int rc = pcre2_match(regex,
                         (PCRE2_SPTR)line,
                         strlen(line),
                         0,
                         0,
                         match,
                         NULL);

    pcre2_match_data_free(match);
    return rc >= 0;
}

static char* trim_dup(const char* input)
{
    if (input == NULL) {
        return NULL;
    }

    const char* start = input;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    const char* end = input + strlen(input);
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }

    size_t len = (size_t)(end - start);
    char* out = malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }

    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char* replace_number_token(const char* template_keys, const char* number)
{
    if (template_keys == NULL || number == NULL) {
        return NULL;
    }

    const char* token = "{number}";
    const char* token_pos = strstr(template_keys, token);
    if (token_pos == NULL) {
        return strdup(template_keys);
    }

    size_t prefix_len = (size_t)(token_pos - template_keys);
    size_t suffix_len = strlen(token_pos + strlen(token));
    size_t number_len = strlen(number);

    size_t total_len = prefix_len + number_len + suffix_len;
    char* out = malloc(total_len + 1);
    if (out == NULL) {
        return NULL;
    }

    memcpy(out, template_keys, prefix_len);
    memcpy(out + prefix_len, number, number_len);
    memcpy(out + prefix_len + number_len,
           token_pos + strlen(token),
           suffix_len);
    out[total_len] = '\0';

    return out;
}

static bool append_action(nabtoshell_prompt_candidate* candidate,
                          const char* label,
                          const char* keys)
{
    if (candidate->action_count >= NABTOSHELL_PROMPT_MAX_ACTIONS) {
        return false;
    }

    char* label_copy = trim_dup(label);
    char* keys_copy = keys ? strdup(keys) : NULL;

    if (label_copy == NULL || keys_copy == NULL) {
        free(label_copy);
        free(keys_copy);
        return false;
    }

    int idx = candidate->action_count;
    candidate->actions[idx].label = label_copy;
    candidate->actions[idx].keys = keys_copy;
    candidate->action_count++;

    return true;
}

static char* dup_capture(const char* subject,
                         PCRE2_SIZE* ovector,
                         int capture_index)
{
    PCRE2_SIZE start = ovector[capture_index * 2];
    PCRE2_SIZE end = ovector[capture_index * 2 + 1];
    if (start == PCRE2_UNSET || end == PCRE2_UNSET || end < start) {
        return NULL;
    }

    size_t len = (size_t)(end - start);
    char* out = malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }

    memcpy(out, subject + start, len);
    out[len] = '\0';

    return out;
}

static const char* find_numbered_option_start(const char* line)
{
    if (line == NULL) {
        return NULL;
    }

    for (const unsigned char* p = (const unsigned char*)line; *p != '\0'; p++) {
        if (!isdigit(*p)) {
            continue;
        }

        const unsigned char* q = p;
        while (isdigit(*q)) {
            q++;
        }

        if (*q == '.' && isspace((unsigned char)q[1])) {
            return (const char*)p;
        }
    }

    return NULL;
}

static bool extract_numbered_menu_actions(const nabtoshell_compiled_prompt_rule* rule,
                                          const nabtoshell_terminal_snapshot* snapshot,
                                          int prompt_row,
                                          nabtoshell_prompt_candidate* candidate)
{
    if (rule->action_template_keys == NULL) {
        return false;
    }

    pcre2_code* option_regex = rule->option_regex;
    pcre2_code* fallback_regex = NULL;

    if (option_regex == NULL) {
        fallback_regex = compile_regex("^\\s*([0-9]+)\\.\\s+(.+)$");
        option_regex = fallback_regex;
    }

    if (option_regex == NULL) {
        return false;
    }

    int max_row = prompt_row + rule->max_scan_lines;
    if (max_row >= snapshot->rows) {
        max_row = snapshot->rows - 1;
    }

    pcre2_match_data* match = pcre2_match_data_create_from_pattern(option_regex, NULL);
    if (match == NULL) {
        if (fallback_regex != NULL) {
            pcre2_code_free(fallback_regex);
        }
        return false;
    }

    bool seen_numbers[NABTOSHELL_PROMPT_MAX_ACTIONS + 1];
    memset(seen_numbers, 0, sizeof(seen_numbers));

    for (int row = prompt_row + 1; row <= max_row; row++) {
        const char* line = snapshot->lines[row];
        if (line == NULL || line[0] == '\0') {
            continue;
        }

        const char* matched_subject = line;
        int rc = pcre2_match(option_regex,
                             (PCRE2_SPTR)matched_subject,
                             strlen(matched_subject),
                             0,
                             0,
                             match,
                             NULL);
        if (rc < 3) {
            const char* option_start = find_numbered_option_start(line);
            if (option_start != NULL && option_start != line) {
                matched_subject = option_start;
                rc = pcre2_match(option_regex,
                                 (PCRE2_SPTR)matched_subject,
                                 strlen(matched_subject),
                                 0,
                                 0,
                                 match,
                                 NULL);
            }
        }
        if (rc < 3) {
            continue;
        }

        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(match);
        char* number = dup_capture(matched_subject, ovector, 1);
        char* label = dup_capture(matched_subject, ovector, 2);

        if (number == NULL || label == NULL) {
            free(number);
            free(label);
            continue;
        }

        char* end = NULL;
        long option_number = strtol(number, &end, 10);
        if (end == number ||
            option_number <= 0 ||
            option_number > NABTOSHELL_PROMPT_MAX_ACTIONS) {
            free(number);
            free(label);
            continue;
        }
        if (seen_numbers[option_number]) {
            free(number);
            free(label);
            continue;
        }

        char* keys = replace_number_token(rule->action_template_keys, number);
        if (keys == NULL) {
            free(number);
            free(label);
            continue;
        }

        bool ok = append_action(candidate, label, keys);
        if (ok) {
            seen_numbers[option_number] = true;
        }
        free(number);
        free(label);
        free(keys);

        if (!ok) {
            break;
        }
    }

    pcre2_match_data_free(match);
    if (fallback_regex != NULL) {
        pcre2_code_free(fallback_regex);
    }

    if (candidate->action_count < 2 || !seen_numbers[1]) {
        return false;
    }

    for (int n = 1; n <= candidate->action_count; n++) {
        if (!seen_numbers[n]) {
            return false;
        }
    }

    return true;
}

static bool fill_candidate_from_rule(const nabtoshell_compiled_prompt_rule* rule,
                                     const nabtoshell_terminal_snapshot* snapshot,
                                     int prompt_row,
                                     nabtoshell_prompt_candidate* candidate)
{
    memset(candidate, 0, sizeof(*candidate));

    candidate->pattern_id = strdup(rule->pattern_id);
    candidate->pattern_type = rule->pattern_type;
    candidate->prompt = trim_dup(snapshot->lines[prompt_row]);
    candidate->anchor_row = prompt_row;

    if (candidate->pattern_id == NULL || candidate->prompt == NULL) {
        nabtoshell_prompt_candidate_free(candidate);
        return false;
    }

    if (rule->pattern_type == NABTOSHELL_PROMPT_TYPE_NUMBERED_MENU) {
        if (!extract_numbered_menu_actions(rule, snapshot, prompt_row, candidate)) {
            nabtoshell_prompt_candidate_free(candidate);
            return false;
        }
        return true;
    }

    for (int i = 0; i < rule->static_action_count; i++) {
        if (!append_action(candidate,
                           rule->static_actions[i].label,
                           rule->static_actions[i].keys)) {
            nabtoshell_prompt_candidate_free(candidate);
            return false;
        }
    }

    if (candidate->action_count <= 0) {
        nabtoshell_prompt_candidate_free(candidate);
        return false;
    }

    return true;
}

bool nabtoshell_prompt_ruleset_match(const nabtoshell_prompt_ruleset* ruleset,
                                     const nabtoshell_terminal_snapshot* snapshot,
                                     nabtoshell_prompt_candidate* out_candidate)
{
    if (out_candidate == NULL) {
        return false;
    }

    memset(out_candidate, 0, sizeof(*out_candidate));

    if (ruleset == NULL || snapshot == NULL || ruleset->count <= 0) {
        return false;
    }

    for (int r = 0; r < ruleset->count; r++) {
        const nabtoshell_compiled_prompt_rule* rule = &ruleset->rules[r];

        for (int row = snapshot->rows - 1; row >= 0; row--) {
            const char* line = snapshot->lines[row];
            if (line == NULL || line[0] == '\0') {
                continue;
            }

            if (!regex_matches_line(rule->prompt_regex, line)) {
                continue;
            }

            if (fill_candidate_from_rule(rule, snapshot, row, out_candidate)) {
                return true;
            }
        }
    }

    return false;
}

bool nabtoshell_prompt_candidate_to_instance(const nabtoshell_prompt_candidate* candidate,
                                             nabtoshell_prompt_instance* instance)
{
    if (candidate == NULL || instance == NULL) {
        return false;
    }

    nabtoshell_prompt_instance_free(instance);

    instance->pattern_id = candidate->pattern_id ? strdup(candidate->pattern_id) : NULL;
    instance->prompt = candidate->prompt ? strdup(candidate->prompt) : NULL;
    instance->pattern_type = candidate->pattern_type;
    instance->anchor_row = candidate->anchor_row;

    if (instance->pattern_id == NULL || instance->prompt == NULL) {
        nabtoshell_prompt_instance_free(instance);
        return false;
    }

    for (int i = 0; i < candidate->action_count; i++) {
        instance->actions[i].label = candidate->actions[i].label ? strdup(candidate->actions[i].label) : NULL;
        instance->actions[i].keys = candidate->actions[i].keys ? strdup(candidate->actions[i].keys) : NULL;

        if (instance->actions[i].label == NULL || instance->actions[i].keys == NULL) {
            nabtoshell_prompt_instance_free(instance);
            return false;
        }
    }

    instance->action_count = candidate->action_count;
    return true;
}
