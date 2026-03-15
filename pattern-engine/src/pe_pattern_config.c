#include "pe_pattern_config.h"

#include <cjson/cJSON.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define PE_PATTERN_CONFIG_VERSION 3
#define PE_DEFAULT_MAX_SCAN_LINES 10

static char* strdup_safe(const char* s)
{
    return (s != NULL) ? strdup(s) : NULL;
}

static pe_prompt_type parse_type(const char* s, bool* ok)
{
    if (ok != NULL) {
        *ok = true;
    }

    if (s == NULL) {
        if (ok != NULL) {
            *ok = false;
        }
        return PE_PROMPT_TYPE_YES_NO;
    }

    if (strcmp(s, "yes_no") == 0) {
        return PE_PROMPT_TYPE_YES_NO;
    }
    if (strcmp(s, "numbered_menu") == 0) {
        return PE_PROMPT_TYPE_NUMBERED_MENU;
    }
    if (strcmp(s, "accept_reject") == 0) {
        return PE_PROMPT_TYPE_ACCEPT_REJECT;
    }

    if (ok != NULL) {
        *ok = false;
    }
    return PE_PROMPT_TYPE_YES_NO;
}

static void free_definition(pe_pattern_definition* d)
{
    if (d == NULL) {
        return;
    }

    free(d->id);
    free(d->prompt_regex);
    free(d->option_regex);

    for (int i = 0; i < d->action_count; i++) {
        free(d->actions[i].label);
        free(d->actions[i].keys);
    }
    free(d->actions);

    if (d->action_template != NULL) {
        free(d->action_template->keys);
        free(d->action_template);
    }

    memset(d, 0, sizeof(*d));
}

static bool parse_actions(cJSON* actions,
                          pe_pattern_action** out_actions,
                          int* out_count)
{
    *out_actions = NULL;
    *out_count = 0;

    if (!cJSON_IsArray(actions)) {
        return true;
    }

    int count = cJSON_GetArraySize(actions);
    if (count <= 0) {
        return true;
    }

    pe_pattern_action* parsed = calloc((size_t)count,
                                       sizeof(pe_pattern_action));
    if (parsed == NULL) {
        return false;
    }

    int valid = 0;
    for (int i = 0; i < count; i++) {
        cJSON* action = cJSON_GetArrayItem(actions, i);
        char* label = strdup_safe(cJSON_GetStringValue(
            cJSON_GetObjectItem(action, "label")));
        char* keys = strdup_safe(cJSON_GetStringValue(
            cJSON_GetObjectItem(action, "keys")));

        if (label == NULL || keys == NULL) {
            free(label);
            free(keys);
            continue;
        }

        parsed[valid].label = label;
        parsed[valid].keys = keys;
        valid++;
    }

    if (valid == 0) {
        free(parsed);
        return true;
    }

    *out_actions = parsed;
    *out_count = valid;
    return true;
}

static bool parse_action_template(cJSON* item,
                                  pe_pattern_action_template** out_template)
{
    *out_template = NULL;

    cJSON* tmpl = cJSON_GetObjectItem(item, "action_template");
    if (!cJSON_IsObject(tmpl)) {
        return true;
    }

    const char* keys = cJSON_GetStringValue(cJSON_GetObjectItem(tmpl, "keys"));
    if (keys == NULL) {
        return true;
    }

    pe_pattern_action_template* parsed = calloc(1, sizeof(*parsed));
    if (parsed == NULL) {
        return false;
    }

    parsed->keys = strdup(keys);
    if (parsed->keys == NULL) {
        free(parsed);
        return false;
    }

    *out_template = parsed;
    return true;
}

static bool parse_definition(cJSON* item, pe_pattern_definition* out)
{
    memset(out, 0, sizeof(*out));

    const char* id = cJSON_GetStringValue(cJSON_GetObjectItem(item, "id"));
    const char* type_string = cJSON_GetStringValue(cJSON_GetObjectItem(item, "type"));
    const char* prompt_regex = cJSON_GetStringValue(
        cJSON_GetObjectItem(item, "prompt_regex"));
    const char* option_regex = cJSON_GetStringValue(
        cJSON_GetObjectItem(item, "option_regex"));

    bool type_ok = false;
    pe_prompt_type type = parse_type(type_string, &type_ok);

    if (id == NULL || prompt_regex == NULL || !type_ok) {
        return false;
    }

    out->id = strdup(id);
    out->prompt_regex = strdup(prompt_regex);
    out->option_regex = strdup_safe(option_regex);
    out->type = type;

    if (out->id == NULL || out->prompt_regex == NULL) {
        free_definition(out);
        return false;
    }

    if (!parse_actions(cJSON_GetObjectItem(item, "actions"),
                       &out->actions, &out->action_count)) {
        free_definition(out);
        return false;
    }

    if (!parse_action_template(item, &out->action_template)) {
        free_definition(out);
        return false;
    }

    cJSON* max_scan_lines = cJSON_GetObjectItem(item, "max_scan_lines");
    out->max_scan_lines = PE_DEFAULT_MAX_SCAN_LINES;
    if (cJSON_IsNumber(max_scan_lines) && max_scan_lines->valueint > 0) {
        out->max_scan_lines = max_scan_lines->valueint;
    }

    if (out->type == PE_PROMPT_TYPE_NUMBERED_MENU) {
        if (out->action_template == NULL || out->action_template->keys == NULL) {
            free_definition(out);
            return false;
        }
    } else {
        if (out->action_count <= 0) {
            free_definition(out);
            return false;
        }
    }

    return true;
}

pe_pattern_config* pe_pattern_config_parse(const char* json,
                                            size_t json_len)
{
    cJSON* root = cJSON_ParseWithLength(json, json_len);
    if (root == NULL) {
        return NULL;
    }

    cJSON* version = cJSON_GetObjectItem(root, "version");
    cJSON* agents_obj = cJSON_GetObjectItem(root, "agents");

    if (!cJSON_IsNumber(version) || version->valueint != PE_PATTERN_CONFIG_VERSION ||
        !cJSON_IsObject(agents_obj)) {
        cJSON_Delete(root);
        return NULL;
    }

    pe_pattern_config* config = calloc(1, sizeof(*config));
    if (config == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    config->version = version->valueint;

    int agent_count = cJSON_GetArraySize(agents_obj);
    config->agents = calloc((size_t)(agent_count > 0 ? agent_count : 1),
                            sizeof(pe_agent_config));
    if (config->agents == NULL) {
        pe_pattern_config_free(config);
        cJSON_Delete(root);
        return NULL;
    }

    int valid_agents = 0;
    cJSON* agent_item = NULL;
    cJSON_ArrayForEach(agent_item, agents_obj) {
        const char* agent_id = agent_item->string;
        if (agent_id == NULL || !cJSON_IsObject(agent_item)) {
            continue;
        }

        pe_agent_config* agent = &config->agents[valid_agents];
        agent->id = strdup(agent_id);
        agent->name = strdup_safe(cJSON_GetStringValue(
            cJSON_GetObjectItem(agent_item, "name")));

        cJSON* rules = cJSON_GetObjectItem(agent_item, "rules");
        int rules_count = cJSON_IsArray(rules) ? cJSON_GetArraySize(rules) : 0;

        if (agent->id == NULL) {
            free(agent->name);
            memset(agent, 0, sizeof(*agent));
            continue;
        }

        agent->patterns = calloc((size_t)(rules_count > 0 ? rules_count : 1),
                                 sizeof(pe_pattern_definition));
        if (agent->patterns == NULL) {
            free(agent->id);
            free(agent->name);
            memset(agent, 0, sizeof(*agent));
            continue;
        }

        int valid_rules = 0;
        for (int i = 0; i < rules_count; i++) {
            cJSON* rule = cJSON_GetArrayItem(rules, i);
            pe_pattern_definition parsed;
            if (!parse_definition(rule, &parsed)) {
                continue;
            }

            agent->patterns[valid_rules] = parsed;
            valid_rules++;
        }

        agent->pattern_count = valid_rules;
        valid_agents++;
    }

    config->agent_count = valid_agents;
    cJSON_Delete(root);

    if (config->agent_count == 0) {
        pe_pattern_config_free(config);
        return NULL;
    }

    return config;
}

void pe_pattern_config_free(pe_pattern_config* config)
{
    if (config == NULL) {
        return;
    }

    for (int i = 0; i < config->agent_count; i++) {
        pe_agent_config* agent = &config->agents[i];

        free(agent->id);
        free(agent->name);

        for (int j = 0; j < agent->pattern_count; j++) {
            free_definition(&agent->patterns[j]);
        }
        free(agent->patterns);
    }

    free(config->agents);
    free(config);
}

const pe_agent_config* pe_pattern_config_find_agent(
    const pe_pattern_config* config,
    const char* agent_id)
{
    if (config == NULL || agent_id == NULL) {
        return NULL;
    }

    for (int i = 0; i < config->agent_count; i++) {
        if (config->agents[i].id != NULL &&
            strcmp(config->agents[i].id, agent_id) == 0) {
            return &config->agents[i];
        }
    }

    return NULL;
}
