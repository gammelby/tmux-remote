#ifndef TMUXREMOTE_H_
#define TMUXREMOTE_H_

#include "tmuxremote_coap_handler.h"
#include "tmuxremote_control_stream.h"
#include "pe_pattern_config.h"
#include "tmuxremote_session.h"
#include "tmuxremote_stream.h"
#include "tmuxremote_iam.h"

#include <nabto/nabto_device.h>
#include <modules/iam/nm_iam.h>
#include <nn/log.h>

#include <time.h>

#define TMUXREMOTE_VERSION "0.1.0"

struct tmuxremote {
    NabtoDevice* device;
    struct tmuxremote_iam iam;
    struct nn_log logger;

    /* File paths */
    char* homeDir;
    char* deviceConfigFile;
    char* deviceKeyFile;
    char* iamStateFile;

    /* CoAP handlers */
    struct tmuxremote_coap_handler coapResize;
    struct tmuxremote_coap_handler coapSessions;
    struct tmuxremote_coap_handler coapAttach;
    struct tmuxremote_coap_handler coapCreate;
    struct tmuxremote_coap_handler coapStatus;

    /* Stream listeners */
    struct tmuxremote_stream_listener streamListener;
    struct tmuxremote_control_stream_listener controlStreamListener;

    /* Session tracking */
    struct tmuxremote_session_map sessionMap;

    /* Pattern detection config (shared by per-stream engines) */
    pe_pattern_config* patternConfig;

    /* PTY recording file path (NULL if not recording) */
    char* recordPtyFile;

    /* Key storage: true if using macOS Keychain */
    bool keychainKey;

    /* Uptime tracking */
    time_t startTime;
};

void tmuxremote_init(struct tmuxremote* app);
void tmuxremote_deinit(struct tmuxremote* app);

#endif
