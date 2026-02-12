#include "nabtoshell_pattern_config.h"
#include <cjson/cJSON.h>
#include <stdlib.h>
#include <string.h>

static char *strdup_safe(const char *s)
{
    return s ? strdup(s) : NULL;
}

static nabtoshell_pattern_type parse_type(const char *s)
{
    if (!s) return PATTERN_TYPE_YES_NO;
    if (strcmp(s, "yes_no") == 0) return PATTERN_TYPE_YES_NO;
    if (strcmp(s, "numbered_menu") == 0) return PATTERN_TYPE_NUMBERED_MENU;
    if (strcmp(s, "accept_reject") == 0) return PATTERN_TYPE_ACCEPT_REJECT;
    return PATTERN_TYPE_YES_NO;
}

static void free_definition(nabtoshell_pattern_definition *d)
{
    free(d->id);
    free(d->regex);
    for (int i = 0; i < d->action_count; i++) {
        free(d->actions[i].label);
        free(d->actions[i].keys);
    }
    free(d->actions);
    if (d->action_template) {
        free(d->action_template->keys);
        free(d->action_template);
    }
}

nabtoshell_pattern_config *nabtoshell_pattern_config_parse(const char *json, size_t json_len)
{
    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return NULL;

    cJSON *version = cJSON_GetObjectItem(root, "version");
    if (!cJSON_IsNumber(version)) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *agents_obj = cJSON_GetObjectItem(root, "agents");
    if (!cJSON_IsObject(agents_obj)) {
        cJSON_Delete(root);
        return NULL;
    }

    nabtoshell_pattern_config *config = calloc(1, sizeof(*config));
    config->version = version->valueint;

    // Count agents
    int agent_count = cJSON_GetArraySize(agents_obj);
    config->agents = calloc(agent_count, sizeof(nabtoshell_agent_config));
    config->agent_count = agent_count;

    int ai = 0;
    cJSON *agent_item = NULL;
    cJSON_ArrayForEach(agent_item, agents_obj) {
        nabtoshell_agent_config *ac = &config->agents[ai];
        ac->id = strdup(agent_item->string);

        cJSON *name = cJSON_GetObjectItem(agent_item, "name");
        ac->name = strdup_safe(cJSON_GetStringValue(name));

        cJSON *patterns = cJSON_GetObjectItem(agent_item, "patterns");
        int pcount = cJSON_IsArray(patterns) ? cJSON_GetArraySize(patterns) : 0;
        ac->patterns = calloc(pcount > 0 ? pcount : 1, sizeof(nabtoshell_pattern_definition));
        ac->pattern_count = pcount;

        int valid = 0;
        for (int pi = 0; pi < pcount; pi++) {
            cJSON *pitem = cJSON_GetArrayItem(patterns, pi);
            nabtoshell_pattern_definition *pd = &ac->patterns[valid];

            cJSON *id = cJSON_GetObjectItem(pitem, "id");
            pd->id = strdup_safe(cJSON_GetStringValue(id));
            if (!pd->id) {
                continue;  /* skip: id is required */
            }

            cJSON *regex = cJSON_GetObjectItem(pitem, "regex");
            pd->regex = strdup_safe(cJSON_GetStringValue(regex));
            if (!pd->regex) {
                free(pd->id);
                memset(pd, 0, sizeof(*pd));
                continue;  /* skip: regex is required */
            }

            cJSON *type = cJSON_GetObjectItem(pitem, "type");
            pd->type = parse_type(cJSON_GetStringValue(type));

            cJSON *multi_line = cJSON_GetObjectItem(pitem, "multi_line");
            pd->multi_line = cJSON_IsBool(multi_line) && cJSON_IsTrue(multi_line);

            // Actions array (for yes_no, accept_reject)
            cJSON *actions = cJSON_GetObjectItem(pitem, "actions");
            if (cJSON_IsArray(actions)) {
                int acount = cJSON_GetArraySize(actions);
                pd->actions = calloc(acount > 0 ? acount : 1, sizeof(nabtoshell_pattern_action));
                int avalid = 0;
                for (int j = 0; j < acount; j++) {
                    cJSON *a = cJSON_GetArrayItem(actions, j);
                    char *label = strdup_safe(cJSON_GetStringValue(cJSON_GetObjectItem(a, "label")));
                    char *keys = strdup_safe(cJSON_GetStringValue(cJSON_GetObjectItem(a, "keys")));
                    if (!label || !keys) {
                        free(label);
                        free(keys);
                        continue;  /* skip invalid action */
                    }
                    pd->actions[avalid].label = label;
                    pd->actions[avalid].keys = keys;
                    avalid++;
                }
                pd->action_count = avalid;
            }

            // Action template (for numbered_menu)
            cJSON *tmpl = cJSON_GetObjectItem(pitem, "action_template");
            if (cJSON_IsObject(tmpl)) {
                char *tmpl_keys = strdup_safe(cJSON_GetStringValue(cJSON_GetObjectItem(tmpl, "keys")));
                if (tmpl_keys) {
                    pd->action_template = calloc(1, sizeof(nabtoshell_pattern_action_template));
                    pd->action_template->keys = tmpl_keys;
                }
            }

            valid++;
        }
        ac->pattern_count = valid;
        ai++;
    }

    cJSON_Delete(root);
    return config;
}

void nabtoshell_pattern_config_free(nabtoshell_pattern_config *config)
{
    if (!config) return;
    for (int i = 0; i < config->agent_count; i++) {
        nabtoshell_agent_config *ac = &config->agents[i];
        free(ac->id);
        free(ac->name);
        for (int j = 0; j < ac->pattern_count; j++) {
            free_definition(&ac->patterns[j]);
        }
        free(ac->patterns);
    }
    free(config->agents);
    free(config);
}

const nabtoshell_agent_config *nabtoshell_pattern_config_find_agent(
    const nabtoshell_pattern_config *config, const char *agent_id)
{
    if (!config || !agent_id) return NULL;
    for (int i = 0; i < config->agent_count; i++) {
        if (strcmp(config->agents[i].id, agent_id) == 0) {
            return &config->agents[i];
        }
    }
    return NULL;
}
