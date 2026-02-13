#ifndef NABTOSHELL_H_
#define NABTOSHELL_H_

#include "nabtoshell_coap_handler.h"
#include "nabtoshell_control_stream.h"
#include "nabtoshell_pattern_config.h"
#include "nabtoshell_session.h"
#include "nabtoshell_stream.h"
#include "nabtoshell_iam.h"

#include <nabto/nabto_device.h>
#include <modules/iam/nm_iam.h>
#include <nn/log.h>

#include <time.h>

#define NABTOSHELL_VERSION "0.1.0"

struct nabtoshell {
    NabtoDevice* device;
    struct nabtoshell_iam iam;
    struct nn_log logger;

    /* File paths */
    char* homeDir;
    char* deviceConfigFile;
    char* deviceKeyFile;
    char* iamStateFile;

    /* CoAP handlers */
    struct nabtoshell_coap_handler coapResize;
    struct nabtoshell_coap_handler coapSessions;
    struct nabtoshell_coap_handler coapAttach;
    struct nabtoshell_coap_handler coapCreate;
    struct nabtoshell_coap_handler coapStatus;

    /* Stream listeners */
    struct nabtoshell_stream_listener streamListener;
    struct nabtoshell_control_stream_listener controlStreamListener;

    /* Session tracking */
    struct nabtoshell_session_map sessionMap;

    /* Pattern detection config (shared by per-stream engines) */
    nabtoshell_pattern_config* patternConfig;

    /* PTY recording file path (NULL if not recording) */
    char* recordPtyFile;

    /* Uptime tracking */
    time_t startTime;
};

void nabtoshell_init(struct nabtoshell* app);
void nabtoshell_deinit(struct nabtoshell* app);

#endif
