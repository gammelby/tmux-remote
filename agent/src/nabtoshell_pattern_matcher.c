#include "nabtoshell_pattern_matcher.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

void nabtoshell_pattern_matcher_init(nabtoshell_pattern_matcher *m)
{
    m->patterns = NULL;
    m->pattern_count = 0;
}

void nabtoshell_pattern_matcher_reset(nabtoshell_pattern_matcher *m)
{
    for (int i = 0; i < m->pattern_count; i++) {
        if (m->patterns[i].regex) {
            pcre2_code_free(m->patterns[i].regex);
        }
    }
    free(m->patterns);
    m->patterns = NULL;
    m->pattern_count = 0;
}

void nabtoshell_pattern_matcher_load(nabtoshell_pattern_matcher *m,
                                      nabtoshell_pattern_definition *defs,
                                      int count)
{
    nabtoshell_pattern_matcher_reset(m);
    if (count <= 0) return;

    m->patterns = calloc(count, sizeof(nabtoshell_compiled_pattern));
    if (!m->patterns) return;

    int compiled = 0;
    for (int i = 0; i < count; i++) {
        uint32_t options = PCRE2_UTF;
        if (defs[i].multi_line) {
            options |= PCRE2_MULTILINE | PCRE2_DOTALL;
        }

        int errcode;
        PCRE2_SIZE erroffset;
        pcre2_code *re = pcre2_compile(
            (PCRE2_SPTR)defs[i].regex,
            PCRE2_ZERO_TERMINATED,
            options,
            &errcode,
            &erroffset,
            NULL);

        if (!re) continue;  // skip invalid regex

        m->patterns[compiled].def = &defs[i];
        m->patterns[compiled].regex = re;
        compiled++;
    }
    m->pattern_count = compiled;
}

static char *strdup_range(const char *s, size_t start, size_t len)
{
    char *r = malloc(len + 1);
    if (!r) return NULL;
    memcpy(r, s + start, len);
    r[len] = '\0';
    return r;
}

// Replace all occurrences of {number} with the given number string.
static char *apply_template(const char *tmpl, const char *number)
{
    const char *placeholder = "{number}";
    size_t ph_len = 8;
    size_t num_len = strlen(number);
    size_t tmpl_len = strlen(tmpl);

    // Count occurrences
    int count = 0;
    const char *p = tmpl;
    while ((p = strstr(p, placeholder)) != NULL) {
        count++;
        p += ph_len;
    }

    size_t result_len = tmpl_len + count * (num_len - ph_len);
    char *result = malloc(result_len + 1);
    if (!result) return NULL;
    char *dst = result;
    p = tmpl;
    while (*p) {
        if (strncmp(p, placeholder, ph_len) == 0) {
            memcpy(dst, number, num_len);
            dst += num_len;
            p += ph_len;
        } else {
            *dst++ = *p++;
        }
    }
    *dst = '\0';
    return result;
}

// Check if a line starts with a selection indicator (after trimming whitespace)
// and strip it. Returns pointer into the line past the indicator.
static const char *strip_indicator(const char *line)
{
    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;

    // Check for indicators: > * - or heavy angle (U+276F = e2 9d af)
    if (*line == '>' || *line == '*' || *line == '-') {
        line++;
        while (*line == ' ') line++;
        return line;
    }

    // Check for U+276F (heavy right-pointing angle): UTF-8 = 0xE2 0x9D 0xAF
    if ((uint8_t)line[0] == 0xE2 && line[1] != '\0' && (uint8_t)line[1] == 0x9D &&
        line[2] != '\0' && (uint8_t)line[2] == 0xAF) {
        line += 3;
        while (*line == ' ') line++;
        return line;
    }

    return line;
}

static void extract_menu_items(const nabtoshell_pattern_matcher *m,
                                const char *text, size_t text_len,
                                const char *matched_text, size_t matched_len,
                                const nabtoshell_pattern_action_template *tmpl,
                                nabtoshell_pattern_match *out)
{
    if (!tmpl) return;

    // Extract prompt: find text before first numbered item in matched_text
    // Process line by line
    char *matched_copy = strdup_range(matched_text, 0, matched_len);
    if (!matched_copy) return;
    char *prompt = NULL;

    char *saveptr;
    char *line = strtok_r(matched_copy, "\n", &saveptr);
    while (line) {
        // Trim leading whitespace
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '\0') {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        // Strip indicator and check if it starts with a number
        const char *stripped = strip_indicator(line);
        if (stripped[0] >= '0' && stripped[0] <= '9') {
            // Check for digit followed by dot
            const char *p = stripped;
            while (*p >= '0' && *p <= '9') p++;
            if (*p == '.') break;  // found first numbered item
        }

        // Non-empty, non-numbered line: update prompt candidate
        free(prompt);
        // Store the trimmed version
        prompt = strdup(line);

        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(matched_copy);

    out->prompt = prompt;

    // Extract numbered items from text_from_match using regex
    int errcode;
    PCRE2_SIZE erroffset;
    pcre2_code *menu_re = pcre2_compile(
        (PCRE2_SPTR)"(\\d+)\\.\\s+(.+)",
        PCRE2_ZERO_TERMINATED,
        PCRE2_UTF,
        &errcode, &erroffset, NULL);

    if (!menu_re) return;

    pcre2_match_data *md = pcre2_match_data_create_from_pattern(menu_re, NULL);
    PCRE2_SIZE offset = 0;
    int expected_number = 1;

    while (offset < text_len && out->action_count < PATTERN_MATCHER_MAX_ACTIONS) {
        int rc = pcre2_match(menu_re, (PCRE2_SPTR)text, text_len, offset, 0, md, NULL);
        if (rc < 0) break;

        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(md);

        // Extract number
        size_t num_start = ovector[2];
        size_t num_end = ovector[3];
        char num_buf[16];
        size_t num_len = num_end - num_start;
        if (num_len >= sizeof(num_buf)) num_len = sizeof(num_buf) - 1;
        memcpy(num_buf, text + num_start, num_len);
        num_buf[num_len] = '\0';

        int num = atoi(num_buf);
        if (num != expected_number) break;
        expected_number++;

        // Extract label
        size_t label_start = ovector[4];
        size_t label_end = ovector[5];
        char *label = strdup_range(text, label_start, label_end - label_start);
        char *keys = apply_template(tmpl->keys, num_buf);
        if (!label || !keys) {
            free(label);
            free(keys);
            break;
        }

        // Trim trailing whitespace
        size_t llen = strlen(label);
        while (llen > 0 && (label[llen-1] == ' ' || label[llen-1] == '\n' || label[llen-1] == '\r' || label[llen-1] == '\t')) {
            label[--llen] = '\0';
        }

        out->actions[out->action_count].label = label;
        out->actions[out->action_count].keys = keys;
        out->action_count++;

        offset = ovector[1];
    }

    pcre2_match_data_free(md);
    pcre2_code_free(menu_re);
}

nabtoshell_pattern_match *nabtoshell_pattern_matcher_match(
    const nabtoshell_pattern_matcher *m,
    const char *text, size_t text_len,
    size_t total_appended)
{
    if (!m || m->pattern_count == 0 || !text || text_len == 0) return NULL;

    for (int i = 0; i < m->pattern_count; i++) {
        pcre2_match_data *md = pcre2_match_data_create_from_pattern(m->patterns[i].regex, NULL);
        int rc = pcre2_match(m->patterns[i].regex, (PCRE2_SPTR)text, text_len, 0, 0, md, NULL);
        if (rc < 0) {
            pcre2_match_data_free(md);
            continue;
        }

        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(md);
        size_t match_start = ovector[0];
        size_t match_end = ovector[1];

        nabtoshell_pattern_match *match = calloc(1, sizeof(*match));
        if (!match) {
            pcre2_match_data_free(md);
            return NULL;
        }
        match->id = strdup(m->patterns[i].def->id);
        match->pattern_type = m->patterns[i].def->type;
        match->matched_text = strdup_range(text, match_start, match_end - match_start);
        match->match_position = total_appended;

        // Resolve actions based on type
        switch (m->patterns[i].def->type) {
        case PATTERN_TYPE_YES_NO:
        case PATTERN_TYPE_ACCEPT_REJECT:
            if (m->patterns[i].def->actions && m->patterns[i].def->action_count > 0) {
                for (int j = 0; j < m->patterns[i].def->action_count && j < PATTERN_MATCHER_MAX_ACTIONS; j++) {
                    match->actions[j].label = strdup(m->patterns[i].def->actions[j].label);
                    match->actions[j].keys = strdup(m->patterns[i].def->actions[j].keys);
                    match->action_count++;
                }
            }
            break;

        case PATTERN_TYPE_NUMBERED_MENU: {
            // text_from_match: from match_start to end of text
            const char *text_from_match = text + match_start;
            size_t text_from_match_len = text_len - match_start;
            extract_menu_items(m, text_from_match, text_from_match_len,
                              match->matched_text, match_end - match_start,
                              m->patterns[i].def->action_template,
                              match);
            break;
        }
        }

        pcre2_match_data_free(md);

        // If no actions resolved, skip this match
        if (match->action_count == 0) {
            nabtoshell_pattern_match_free(match);
            continue;
        }

        return match;
    }

    return NULL;
}

void nabtoshell_pattern_match_free(nabtoshell_pattern_match *match)
{
    if (!match) return;
    free(match->id);
    free(match->prompt);
    free(match->matched_text);
    for (int i = 0; i < match->action_count; i++) {
        free(match->actions[i].label);
        free(match->actions[i].keys);
    }
    free(match);
}

nabtoshell_pattern_match *nabtoshell_pattern_match_copy(const nabtoshell_pattern_match *src)
{
    if (!src) return NULL;

    nabtoshell_pattern_match *dst = calloc(1, sizeof(*dst));
    if (!dst) return NULL;

    dst->id = src->id ? strdup(src->id) : NULL;
    dst->pattern_type = src->pattern_type;
    dst->prompt = src->prompt ? strdup(src->prompt) : NULL;
    dst->matched_text = src->matched_text ? strdup(src->matched_text) : NULL;
    dst->match_position = src->match_position;
    dst->action_count = src->action_count;

    for (int i = 0; i < src->action_count; i++) {
        dst->actions[i].label = src->actions[i].label ? strdup(src->actions[i].label) : NULL;
        dst->actions[i].keys = src->actions[i].keys ? strdup(src->actions[i].keys) : NULL;
    }

    return dst;
}
