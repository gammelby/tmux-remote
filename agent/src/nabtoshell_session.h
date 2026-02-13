#ifndef NABTOSHELL_SESSION_H_
#define NABTOSHELL_SESSION_H_

#include <nabto/nabto_device.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#define NABTOSHELL_MAX_SESSIONS 8
#define NABTOSHELL_SESSION_NAME_MAX 64

struct nabtoshell_session_entry {
    NabtoDeviceConnectionRef connectionRef;
    char sessionName[NABTOSHELL_SESSION_NAME_MAX];
    uint16_t cols;
    uint16_t rows;
    bool valid;
};

struct nabtoshell_session_map {
    pthread_mutex_t mutex;
    struct nabtoshell_session_entry entries[NABTOSHELL_MAX_SESSIONS];
};

void nabtoshell_session_map_init(struct nabtoshell_session_map* map);
void nabtoshell_session_map_deinit(struct nabtoshell_session_map* map);

bool nabtoshell_session_set(struct nabtoshell_session_map* map,
                            NabtoDeviceConnectionRef ref,
                            const char* sessionName,
                            uint16_t cols, uint16_t rows);

bool nabtoshell_session_get(struct nabtoshell_session_map* map,
                            NabtoDeviceConnectionRef ref,
                            struct nabtoshell_session_entry* out);

bool nabtoshell_session_update_size(struct nabtoshell_session_map* map,
                                    NabtoDeviceConnectionRef ref,
                                    uint16_t cols, uint16_t rows);

void nabtoshell_session_remove(struct nabtoshell_session_map* map,
                               NabtoDeviceConnectionRef ref);

#endif
