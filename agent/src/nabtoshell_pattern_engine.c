#include "nabtoshell_pattern_engine.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define NEWLINE "\n"
#define PATTERN_DEBUG_LOG 0

/* Compare two matches for semantic identity. Returns true if they represent
 * the same prompt (TUI redraw), false if the prompt changed (new question).
 * For numbered_menu: compare the extracted prompt text (question line).
 * For yes_no/accept_reject: compare the full matched_text. */
static bool match_same_prompt(const nabtoshell_pattern_match *a,
                              const nabtoshell_pattern_match *b)
{
    if (a == NULL || b == NULL) return false;
    if (a->id == NULL || b->id == NULL) return false;
    if (strcmp(a->id, b->id) != 0) return false;

    /* For numbered_menu, the prompt field is a stable extraction of the
     * question line (before numbered items). The matched_text includes
     * trailing TUI content that varies between feeds, so don't use it. */
    if (a->prompt != NULL && b->prompt != NULL) {
        return strcmp(a->prompt, b->prompt) == 0;
    }

    /* For yes_no / accept_reject, matched_text is the full regex match
     * which is typically short and stable. */
    if (a->matched_text != NULL && b->matched_text != NULL) {
        return strcmp(a->matched_text, b->matched_text) == 0;
    }

    /* If one has matched_text and the other doesn't, they differ. */
    return (a->matched_text == NULL && b->matched_text == NULL);
}
#define PATTERN_LOG(...) \
    do { if (PATTERN_DEBUG_LOG) fprintf(stderr, __VA_ARGS__); } while (0)

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
    PATTERN_LOG("[Pattern] select_agent: %s, config=%s" NEWLINE,
                agent_id ? agent_id : "NULL",
                e->config ? "loaded" : "NULL");
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
                PATTERN_LOG("[Pattern] select_agent: found agent '%s' with %d patterns" NEWLINE,
                            agent_id, ac->pattern_count);
                nabtoshell_pattern_matcher_load(&e->matcher, ac->patterns, ac->pattern_count);
            } else {
                PATTERN_LOG("[Pattern] select_agent: agent '%s' NOT FOUND in config" NEWLINE, agent_id);
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

    /* Periodic logging: every ~2000 chars of total input */
    if (e->buffer.total_appended % 2000 < stripped_len) {
        PATTERN_LOG("[Pattern] engine_feed: total=%zu, stripped=%zu, agent=%s, match=%s, dismissed=%d" NEWLINE,
                    e->buffer.total_appended, stripped_len,
                    e->active_agent ? e->active_agent : "NULL",
                    e->active_match ? e->active_match->id : "NULL",
                    e->dismissed);
    }

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

    // Check for numbered_menu action upgrade on every feed.
    // Items may arrive across PTY chunks, so the initial match can have
    // fewer actions than the final prompt. Re-scan a SMALL recent window
    // (not the full match window) so the regex finds the LATEST prompt
    // instance, not an older partial copy still in the buffer.
    if (e->active_match &&
        e->active_match->pattern_type == PATTERN_TYPE_NUMBERED_MENU) {
        size_t chars_since_match = e->buffer.total_appended - e->active_match->match_position;
        if (chars_since_match < PATTERN_ENGINE_AUTO_DISMISS) {
            // Use a window just large enough for a prompt (~500 chars max)
            size_t upgrade_window = chars_since_match + 500;
            if (upgrade_window > PATTERN_ENGINE_MATCH_WINDOW)
                upgrade_window = PATTERN_ENGINE_MATCH_WINDOW;
            size_t tail_len;
            const char *tail = nabtoshell_rolling_buffer_tail(&e->buffer, upgrade_window, &tail_len);
            nabtoshell_pattern_match *check = nabtoshell_pattern_matcher_match(&e->matcher, tail, tail_len, e->buffer.total_appended);
            if (check && strcmp(check->id, e->active_match->id) == 0 &&
                check->action_count > e->active_match->action_count) {
                PATTERN_LOG("[Pattern] engine_feed: upgrade match=%s, actions %d->%d" NEWLINE,
                            e->active_match->id,
                            e->active_match->action_count,
                            check->action_count);
                nabtoshell_pattern_match_free(e->active_match);
                e->active_match = check;
                check = NULL;
                e->active_match->match_position = e->buffer.total_appended;
                should_notify = true;
                notify_copy = nabtoshell_pattern_match_copy(e->active_match);
            }
            nabtoshell_pattern_match_free(check);
        }
    }

    // Check auto-dismiss for existing match
    if (e->active_match) {
        size_t chars_since_match = e->buffer.total_appended - e->active_match->match_position;

        if (chars_since_match > PATTERN_ENGINE_AUTO_DISMISS) {
            PATTERN_LOG("[Pattern] auto-dismiss check: match=%s, chars_since=%zu, total=%zu\n",
                        e->active_match->id, chars_since_match, e->buffer.total_appended);
            size_t tail_len;
            /* Re-scan the match window to see if the prompt is still near
             * the end of the buffer (i.e. being actively redrawn by a TUI).
             * Use a smaller scan window (chars_since + 500) to find the
             * LATEST prompt copy, not an older partial one. This ensures
             * action count upgrades work correctly when the buffer has
             * multiple prompt copies from TUI redraws. */
            size_t scan_window = chars_since_match + 500;
            if (scan_window > PATTERN_ENGINE_MATCH_WINDOW)
                scan_window = PATTERN_ENGINE_MATCH_WINDOW;
            const char *tail = nabtoshell_rolling_buffer_tail(&e->buffer, scan_window, &tail_len);
            nabtoshell_pattern_match *check = nabtoshell_pattern_matcher_match(&e->matcher, tail, tail_len, e->buffer.total_appended);

            if (check && strcmp(check->id, e->active_match->id) == 0) {
                /* Prompt still in match window (TUI redraw). Reset age.
                 * If the rescan found more actions (items arrived across
                 * feeds), upgrade the active match and notify. */
                if (check->action_count > e->active_match->action_count) {
                    PATTERN_LOG("[Pattern] engine_feed: upgrade match=%s, actions %d->%d" NEWLINE,
                                e->active_match->id,
                                e->active_match->action_count,
                                check->action_count);
                    nabtoshell_pattern_match_free(e->active_match);
                    e->active_match = check;
                    check = NULL;  /* prevent double-free below */
                    e->active_match->match_position = e->buffer.total_appended;
                    should_notify = true;
                    notify_copy = nabtoshell_pattern_match_copy(e->active_match);
                } else {
                    e->active_match->match_position = e->buffer.total_appended;
                }
                PATTERN_LOG("[Pattern] engine_feed: age-reset match=%s, actions=%d, chars_since=%zu" NEWLINE,
                            e->active_match->id, e->active_match->action_count, chars_since_match);
            } else {
                PATTERN_LOG("[Pattern] engine_feed: auto-dismiss old=%s, chars_since=%zu, rescan_len=%zu" NEWLINE,
                            e->active_match->id, chars_since_match, tail_len);
                nabtoshell_pattern_match_free(e->active_match);
                e->active_match = NULL;
                should_notify = true;
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
        PATTERN_LOG("[Pattern] try-new-match: tail_len=%zu, total=%zu, result=%s\n",
                    tail_len, e->buffer.total_appended, new_match ? new_match->id : "NULL");
        if (new_match) {
            PATTERN_LOG("[Pattern] engine_feed: new match id=%s, type=%d, actions=%d" NEWLINE,
                        new_match->id, new_match->pattern_type, new_match->action_count);
            e->active_match = new_match;
            should_notify = true;
            notify_copy = nabtoshell_pattern_match_copy(e->active_match);
        } else if (e->buffer.total_appended % 2000 < stripped_len) {
            PATTERN_LOG("[Pattern] engine_feed: no match, tail_len=%zu\n", tail_len);
        }
    }

    // Detect prompt replacement: active match is set, but a DIFFERENT
    // prompt appeared in recent data. Scan only the recent portion of
    // the buffer (since last match position) to isolate the new prompt
    // from the old one still in the full match window.
    if (e->active_match && !e->dismissed) {
        size_t chars_since = e->buffer.total_appended - e->active_match->match_position;
        if (chars_since > 0 && chars_since < PATTERN_ENGINE_MATCH_WINDOW) {
            size_t recent_len;
            const char *recent = nabtoshell_rolling_buffer_tail(&e->buffer, chars_since, &recent_len);
            nabtoshell_pattern_match *recent_match = nabtoshell_pattern_matcher_match(
                &e->matcher, recent, recent_len, e->buffer.total_appended);
            if (recent_match && !match_same_prompt(e->active_match, recent_match)) {
                PATTERN_LOG("[Pattern] engine_feed: prompt replaced, old prompt=%s new prompt=%s" NEWLINE,
                            e->active_match->prompt ? e->active_match->prompt : e->active_match->id,
                            recent_match->prompt ? recent_match->prompt : recent_match->id);
                nabtoshell_pattern_match_free(e->active_match);
                e->active_match = recent_match;
                recent_match = NULL;
                should_notify = true;
                nabtoshell_pattern_match_free(notify_copy);
                notify_copy = nabtoshell_pattern_match_copy(e->active_match);
            }
            nabtoshell_pattern_match_free(recent_match);
        }
    }

    if (e->active_match && !should_notify) {
        PATTERN_LOG("[Pattern] skip-new-match: active=%s, dismissed=%d, user_dismissed=%d, total=%zu\n",
                    e->active_match->id, e->dismissed, e->user_dismissed, e->buffer.total_appended);
    }

    pthread_mutex_unlock(&e->mutex);
    if (heap_allocated) free(stripped);

    if (should_notify && e->on_change) {
        PATTERN_LOG("[Pattern] engine_feed: firing on_change, match=%s" NEWLINE,
                    notify_copy ? notify_copy->id : "NULL(dismiss)");
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
