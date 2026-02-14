#include "tmuxremote_session.h"

#include <string.h>

void tmuxremote_session_map_init(struct tmuxremote_session_map* map)
{
    memset(map, 0, sizeof(struct tmuxremote_session_map));
    pthread_mutex_init(&map->mutex, NULL);
}

void tmuxremote_session_map_deinit(struct tmuxremote_session_map* map)
{
    pthread_mutex_destroy(&map->mutex);
}

bool tmuxremote_session_set(struct tmuxremote_session_map* map,
                            NabtoDeviceConnectionRef ref,
                            const char* sessionName,
                            uint16_t cols, uint16_t rows)
{
    bool ok = false;
    pthread_mutex_lock(&map->mutex);

    /* First check if this connection already has an entry */
    for (int i = 0; i < TMUXREMOTE_MAX_SESSIONS; i++) {
        if (map->entries[i].valid && map->entries[i].connectionRef == ref) {
            strncpy(map->entries[i].sessionName, sessionName,
                    TMUXREMOTE_SESSION_NAME_MAX - 1);
            map->entries[i].sessionName[TMUXREMOTE_SESSION_NAME_MAX - 1] = '\0';
            map->entries[i].cols = cols;
            map->entries[i].rows = rows;
            ok = true;
            goto end;
        }
    }

    /* Find a free slot */
    for (int i = 0; i < TMUXREMOTE_MAX_SESSIONS; i++) {
        if (!map->entries[i].valid) {
            map->entries[i].connectionRef = ref;
            strncpy(map->entries[i].sessionName, sessionName,
                    TMUXREMOTE_SESSION_NAME_MAX - 1);
            map->entries[i].sessionName[TMUXREMOTE_SESSION_NAME_MAX - 1] = '\0';
            map->entries[i].cols = cols;
            map->entries[i].rows = rows;
            map->entries[i].valid = true;
            ok = true;
            goto end;
        }
    }

end:
    pthread_mutex_unlock(&map->mutex);
    return ok;
}

bool tmuxremote_session_get(struct tmuxremote_session_map* map,
                            NabtoDeviceConnectionRef ref,
                            struct tmuxremote_session_entry* out)
{
    bool found = false;
    pthread_mutex_lock(&map->mutex);
    for (int i = 0; i < TMUXREMOTE_MAX_SESSIONS; i++) {
        if (map->entries[i].valid && map->entries[i].connectionRef == ref) {
            if (out != NULL) {
                *out = map->entries[i];
            }
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&map->mutex);
    return found;
}

bool tmuxremote_session_update_size(struct tmuxremote_session_map* map,
                                    NabtoDeviceConnectionRef ref,
                                    uint16_t cols, uint16_t rows)
{
    bool updated = false;
    pthread_mutex_lock(&map->mutex);
    for (int i = 0; i < TMUXREMOTE_MAX_SESSIONS; i++) {
        if (map->entries[i].valid && map->entries[i].connectionRef == ref) {
            map->entries[i].cols = cols;
            map->entries[i].rows = rows;
            updated = true;
            break;
        }
    }
    pthread_mutex_unlock(&map->mutex);
    return updated;
}

void tmuxremote_session_remove(struct tmuxremote_session_map* map,
                               NabtoDeviceConnectionRef ref)
{
    pthread_mutex_lock(&map->mutex);
    for (int i = 0; i < TMUXREMOTE_MAX_SESSIONS; i++) {
        if (map->entries[i].valid && map->entries[i].connectionRef == ref) {
            memset(&map->entries[i], 0, sizeof(struct tmuxremote_session_entry));
        }
    }
    pthread_mutex_unlock(&map->mutex);
}
