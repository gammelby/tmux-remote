#ifndef TMUXREMOTE_CLIENT_COAP_H_
#define TMUXREMOTE_CLIENT_COAP_H_

#include <nabto/nabto_client.h>
#include <stdbool.h>
#include <stdint.h>

bool tmuxremote_coap_attach(NabtoClientConnection* conn, NabtoClient* client,
                            const char* session, uint16_t cols, uint16_t rows,
                            bool create);

bool tmuxremote_coap_create_session(NabtoClientConnection* conn,
                                    NabtoClient* client,
                                    const char* session, uint16_t cols,
                                    uint16_t rows, const char* command);

bool tmuxremote_coap_resize(NabtoClientConnection* conn, NabtoClient* client,
                            uint16_t cols, uint16_t rows);

bool tmuxremote_coap_pair_password_invite(NabtoClientConnection* conn,
                                          NabtoClient* client,
                                          const char* username);

/* sessions list result */
#define TMUXREMOTE_CLIENT_MAX_SESSIONS 32

struct tmuxremote_client_session_info {
    char name[64];
    uint16_t cols;
    uint16_t rows;
    int attached;
};

struct tmuxremote_client_sessions_list {
    struct tmuxremote_client_session_info sessions[TMUXREMOTE_CLIENT_MAX_SESSIONS];
    int count;
};

bool tmuxremote_coap_list_sessions(NabtoClientConnection* conn,
                                   NabtoClient* client,
                                   struct tmuxremote_client_sessions_list* list);

struct tmuxremote_client_status_info {
    char version[32];
    int activeSessions;
    uint64_t uptimeSeconds;
};

bool tmuxremote_coap_get_status(NabtoClientConnection* conn,
                                NabtoClient* client,
                                struct tmuxremote_client_status_info* info);

#endif
