#ifndef TMUXREMOTE_SESSION_H_
#define TMUXREMOTE_SESSION_H_

#include <nabto/nabto_device.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#define TMUXREMOTE_MAX_SESSIONS 8
#define TMUXREMOTE_SESSION_NAME_MAX 64

struct tmuxremote_session_entry {
    NabtoDeviceConnectionRef connectionRef;
    char sessionName[TMUXREMOTE_SESSION_NAME_MAX];
    uint16_t cols;
    uint16_t rows;
    bool valid;
};

struct tmuxremote_session_map {
    pthread_mutex_t mutex;
    struct tmuxremote_session_entry entries[TMUXREMOTE_MAX_SESSIONS];
};

void tmuxremote_session_map_init(struct tmuxremote_session_map* map);
void tmuxremote_session_map_deinit(struct tmuxremote_session_map* map);

bool tmuxremote_session_set(struct tmuxremote_session_map* map,
                            NabtoDeviceConnectionRef ref,
                            const char* sessionName,
                            uint16_t cols, uint16_t rows);

bool tmuxremote_session_get(struct tmuxremote_session_map* map,
                            NabtoDeviceConnectionRef ref,
                            struct tmuxremote_session_entry* out);

bool tmuxremote_session_update_size(struct tmuxremote_session_map* map,
                                    NabtoDeviceConnectionRef ref,
                                    uint16_t cols, uint16_t rows);

void tmuxremote_session_remove(struct tmuxremote_session_map* map,
                               NabtoDeviceConnectionRef ref);

#endif
