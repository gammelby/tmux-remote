#include "nabtoshell_pattern_engine.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

void nabtoshell_pattern_engine_init(nabtoshell_pattern_engine *e)
{
    nabtoshell_ansi_stripper_init(&e->stripper);
    nabtoshell_rolling_buffer_init(&e->buffer, PATTERN_ENGINE_BUFFER_CAPACITY);
    nabtoshell_pattern_matcher_init(&e->matcher);
    e->config = NULL;
    e->active_match = NULL;
    e->active_agent = NULL;
    e->dismissed = false;
    e->user_dismissed = false;
    e->dismissed_at_position = 0;
    e->on_change = NULL;
    e->on_change_user_data = NULL;
    pthread_mutex_init(&e->mutex, NULL);
}

void nabtoshell_pattern_engine_free(nabtoshell_pattern_engine *e)
{
    nabtoshell_pattern_matcher_reset(&e->matcher);
    nabtoshell_rolling_buffer_free(&e->buffer);
    nabtoshell_pattern_match_free(e->active_match);
    free(e->active_agent);
    pthread_mutex_destroy(&e->mutex);
    // config is not owned by engine
}

void nabtoshell_pattern_engine_reset(nabtoshell_pattern_engine *e)
{
    nabtoshell_pattern_match *notify_match = NULL;
    bool should_notify = false;

    pthread_mutex_lock(&e->mutex);
    nabtoshell_ansi_stripper_reset(&e->stripper);
    nabtoshell_rolling_buffer_reset(&e->buffer);
    if (e->active_match != NULL) {
        should_notify = true;
    }
    nabtoshell_pattern_match_free(e->active_match);
    e->active_match = NULL;
    e->dismissed = false;
    e->user_dismissed = false;
    e->dismissed_at_position = 0;
    pthread_mutex_unlock(&e->mutex);

    if (should_notify && e->on_change) {
        e->on_change(NULL, e->on_change_user_data);
    }
}

void nabtoshell_pattern_engine_load_config(nabtoshell_pattern_engine *e,
                                            nabtoshell_pattern_config *config)
{
    pthread_mutex_lock(&e->mutex);
    e->config = config;
    if (e->active_agent) {
        const nabtoshell_agent_config *ac = nabtoshell_pattern_config_find_agent(config, e->active_agent);
        if (ac) {
            nabtoshell_pattern_matcher_load(&e->matcher, ac->patterns, ac->pattern_count);
        }
    }
    pthread_mutex_unlock(&e->mutex);
}

void nabtoshell_pattern_engine_select_agent(nabtoshell_pattern_engine *e,
                                             const char *agent_id)
{
    nabtoshell_pattern_match *notify_copy = NULL;
    bool should_notify = false;

    pthread_mutex_lock(&e->mutex);

    free(e->active_agent);
    nabtoshell_pattern_match_free(e->active_match);
    e->active_match = NULL;
    e->dismissed = false;

    if (!agent_id) {
        e->active_agent = NULL;
        nabtoshell_pattern_matcher_reset(&e->matcher);
        should_notify = true;
        /* notify_copy stays NULL (dismiss) */
    } else {
        e->active_agent = strdup(agent_id);

        if (e->config) {
            const nabtoshell_agent_config *ac = nabtoshell_pattern_config_find_agent(e->config, agent_id);
            if (ac) {
                nabtoshell_pattern_matcher_load(&e->matcher, ac->patterns, ac->pattern_count);
            } else {
                nabtoshell_pattern_matcher_reset(&e->matcher);
            }
        }

        // Re-evaluate existing buffer
        if (e->buffer.len > 0) {
            size_t tail_len;
            const char *tail = nabtoshell_rolling_buffer_tail(&e->buffer, PATTERN_ENGINE_MATCH_WINDOW, &tail_len);
            e->active_match = nabtoshell_pattern_matcher_match(&e->matcher, tail, tail_len, e->buffer.total_appended);
        }

        should_notify = true;
        notify_copy = nabtoshell_pattern_match_copy(e->active_match);
    }

    pthread_mutex_unlock(&e->mutex);

    if (should_notify && e->on_change) {
        e->on_change(notify_copy, e->on_change_user_data);
    }
    nabtoshell_pattern_match_free(notify_copy);
}

void nabtoshell_pattern_engine_feed(nabtoshell_pattern_engine *e,
                                     const uint8_t *data, size_t len)
{
    // Use stack buffer for typical sizes, malloc fallback for large inputs
    uint8_t stack_buf[4100];
    uint8_t *stripped = stack_buf;
    size_t stripped_cap = sizeof(stack_buf);
    bool heap_allocated = false;

    if (len + 4 > sizeof(stack_buf)) {
        stripped = malloc(len + 4);
        if (!stripped) return;  /* drop chunk on malloc failure */
        stripped_cap = len + 4;
        heap_allocated = true;
    }

    nabtoshell_pattern_match *notify_copy = NULL;
    bool should_notify = false;

    pthread_mutex_lock(&e->mutex);

    // Strip ANSI under the lock to protect stripper state
    size_t stripped_len = nabtoshell_ansi_stripper_feed(&e->stripper, data, len, stripped, stripped_cap);

    if (stripped_len == 0) {
        pthread_mutex_unlock(&e->mutex);
        if (heap_allocated) free(stripped);
        return;
    }

    nabtoshell_rolling_buffer_append(&e->buffer, (const char *)stripped, stripped_len);

    /* Auto-detect agent from buffered text if none selected yet.
     * Note: auto_detect calls select_agent which would re-lock the mutex,
     * so we do detection inline here. */
    if (!e->active_agent && e->config) {
        size_t detect_len;
        const char *detect_text = nabtoshell_rolling_buffer_tail(
            &e->buffer, 512, &detect_len);

        /* Inline auto-detect (avoids recursive mutex lock via select_agent) */
        const char *detected = NULL;
        char *lower = malloc(detect_len + 1);
        if (lower) {
            for (size_t i = 0; i < detect_len; i++) {
                lower[i] = tolower((unsigned char)detect_text[i]);
            }
            lower[detect_len] = '\0';

            if (strstr(lower, "claude code") || strstr(lower, "claude.ai")) {
                detected = "claude-code";
            } else if (strstr(lower, "aider v") || strstr(lower, "aider ")) {
                detected = "aider";
            } else if (strstr(lower, "codex")) {
                detected = "codex";
            }
            free(lower);
        }

        if (detected) {
            e->active_agent = strdup(detected);
            if (e->config) {
                const nabtoshell_agent_config *ac = nabtoshell_pattern_config_find_agent(e->config, detected);
                if (ac) {
                    nabtoshell_pattern_matcher_load(&e->matcher, ac->patterns, ac->pattern_count);
                }
            }
            // Re-evaluate buffer
            if (e->buffer.len > 0) {
                size_t tail_len;
                const char *tail = nabtoshell_rolling_buffer_tail(&e->buffer, PATTERN_ENGINE_MATCH_WINDOW, &tail_len);
                e->active_match = nabtoshell_pattern_matcher_match(&e->matcher, tail, tail_len, e->buffer.total_appended);
            }
            should_notify = true;
            notify_copy = nabtoshell_pattern_match_copy(e->active_match);
            pthread_mutex_unlock(&e->mutex);
            if (heap_allocated) free(stripped);
            if (should_notify && e->on_change) {
                e->on_change(notify_copy, e->on_change_user_data);
            }
            nabtoshell_pattern_match_free(notify_copy);
            return;
        }
    }

    if (!e->active_agent) {
        pthread_mutex_unlock(&e->mutex);
        if (heap_allocated) free(stripped);
        return;
    }

    // Check auto-dismiss for existing match
    if (e->active_match) {
        size_t chars_since_match = e->buffer.total_appended - e->active_match->match_position;
        if (chars_since_match > PATTERN_ENGINE_AUTO_DISMISS) {
            size_t tail_len;
            const char *tail = nabtoshell_rolling_buffer_tail(&e->buffer, PATTERN_ENGINE_MATCH_WINDOW, &tail_len);
            nabtoshell_pattern_match *check = nabtoshell_pattern_matcher_match(&e->matcher, tail, tail_len, e->buffer.total_appended);

            if (!check || strcmp(check->id, e->active_match->id) != 0) {
                e->dismissed = true;
                e->dismissed_at_position = e->buffer.total_appended;
                nabtoshell_pattern_match_free(e->active_match);
                e->active_match = NULL;
                should_notify = true;
                /* notify_copy stays NULL (dismiss) */
            }
            nabtoshell_pattern_match_free(check);
        }
    }

    // Reset dismissed flag if enough new content arrived
    if (e->dismissed) {
        size_t chars_since_dismiss = e->buffer.total_appended - e->dismissed_at_position;
        size_t threshold = e->user_dismissed ? PATTERN_ENGINE_MATCH_WINDOW : PATTERN_ENGINE_AUTO_DISMISS;
        if (chars_since_dismiss > threshold) {
            e->dismissed = false;
            e->user_dismissed = false;
        }
    }

    // Try to find a new match
    if (!e->active_match && !e->dismissed) {
        size_t tail_len;
        const char *tail = nabtoshell_rolling_buffer_tail(&e->buffer, PATTERN_ENGINE_MATCH_WINDOW, &tail_len);
        nabtoshell_pattern_match *new_match = nabtoshell_pattern_matcher_match(&e->matcher, tail, tail_len, e->buffer.total_appended);
        if (new_match) {
            e->active_match = new_match;
            should_notify = true;
            notify_copy = nabtoshell_pattern_match_copy(e->active_match);
        }
    }

    pthread_mutex_unlock(&e->mutex);
    if (heap_allocated) free(stripped);

    if (should_notify && e->on_change) {
        e->on_change(notify_copy, e->on_change_user_data);
    }
    nabtoshell_pattern_match_free(notify_copy);
}

void nabtoshell_pattern_engine_dismiss(nabtoshell_pattern_engine *e)
{
    bool should_notify = false;

    pthread_mutex_lock(&e->mutex);
    e->dismissed = true;
    e->user_dismissed = true;
    e->dismissed_at_position = e->buffer.total_appended;
    if (e->active_match != NULL) {
        should_notify = true;
    }
    nabtoshell_pattern_match_free(e->active_match);
    e->active_match = NULL;
    pthread_mutex_unlock(&e->mutex);

    if (should_notify && e->on_change) {
        e->on_change(NULL, e->on_change_user_data);
    }
}

void nabtoshell_pattern_engine_set_callback(nabtoshell_pattern_engine *e,
                                             nabtoshell_pattern_engine_callback cb,
                                             void *user_data)
{
    e->on_change = cb;
    e->on_change_user_data = user_data;
}

void nabtoshell_pattern_engine_auto_detect(nabtoshell_pattern_engine *e,
                                            const char *text, size_t len)
{
    if (e->active_agent) return;

    // Simple substring search (case-insensitive)
    char *lower = malloc(len + 1);
    if (!lower) return;
    for (size_t i = 0; i < len; i++) {
        lower[i] = tolower((unsigned char)text[i]);
    }
    lower[len] = '\0';

    if (strstr(lower, "claude code") || strstr(lower, "claude.ai")) {
        free(lower);
        nabtoshell_pattern_engine_select_agent(e, "claude-code");
    } else if (strstr(lower, "aider v") || strstr(lower, "aider ")) {
        free(lower);
        nabtoshell_pattern_engine_select_agent(e, "aider");
    } else if (strstr(lower, "codex")) {
        free(lower);
        nabtoshell_pattern_engine_select_agent(e, "codex");
    } else {
        free(lower);
    }
}

nabtoshell_pattern_match *nabtoshell_pattern_engine_copy_active_match(
    nabtoshell_pattern_engine *e)
{
    pthread_mutex_lock(&e->mutex);
    nabtoshell_pattern_match *copy = nabtoshell_pattern_match_copy(e->active_match);
    pthread_mutex_unlock(&e->mutex);
    return copy;
}
