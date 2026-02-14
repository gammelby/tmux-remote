#ifndef TMUXREMOTE_STREAM_H_
#define TMUXREMOTE_STREAM_H_

#include <nabto/nabto_device.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "tmuxremote_prompt_detector.h"
#include "tmuxremote_prompt.h"
#include "tmuxremote_session.h"

#define TMUXREMOTE_STREAM_BUFFER_SIZE 4096
#define TMUXREMOTE_MAX_ACTIVE_STREAMS 8

struct tmuxremote;

struct tmuxremote_active_stream {
    NabtoDevice* device;
    NabtoDeviceStream* stream;
    struct tmuxremote* app;
    int ptyFd;
    pid_t childPid;
    atomic_bool closing;
    atomic_bool closeStarted;

    tmuxremote_prompt_detector promptDetector;
    bool promptDetectorInitialized;
    NabtoDeviceConnectionRef connectionRef;

    FILE* ptyRecordFile;

    char sessionName[TMUXREMOTE_SESSION_NAME_MAX];
    uint16_t sessionCols;
    uint16_t sessionRows;

    pthread_t setupThread;
    bool setupThreadStarted;
    pthread_t ptyReaderThread;
    bool ptyReaderThreadStarted;
    pthread_t streamReaderThread;
    bool streamReaderThreadStarted;

    struct tmuxremote_active_stream* next;
};

struct tmuxremote_stream_listener {
    NabtoDevice* device;
    struct tmuxremote* app;
    NabtoDeviceListener* listener;
    NabtoDeviceFuture* future;
    NabtoDeviceStream* newStream;

    pthread_mutex_t activeStreamsMutex;
    struct tmuxremote_active_stream* activeStreams;
};

void tmuxremote_stream_listener_init(struct tmuxremote_stream_listener* sl,
                                     NabtoDevice* device,
                                     struct tmuxremote* app);

void tmuxremote_stream_listener_stop(struct tmuxremote_stream_listener* sl);
void tmuxremote_stream_listener_deinit(struct tmuxremote_stream_listener* sl);

int tmuxremote_stream_get_pty_fd(struct tmuxremote_stream_listener* sl,
                                 NabtoDeviceConnectionRef ref);

void tmuxremote_stream_resize_prompt_detector_for_ref(
    struct tmuxremote_stream_listener* sl,
    NabtoDeviceConnectionRef ref,
    int cols,
    int rows);

tmuxremote_prompt_instance* tmuxremote_stream_copy_active_prompt_for_ref(
    struct tmuxremote_stream_listener* sl,
    NabtoDeviceConnectionRef ref);

void tmuxremote_stream_resolve_prompt_for_ref(
    struct tmuxremote_stream_listener* sl,
    NabtoDeviceConnectionRef ref,
    const char* instance_id,
    const char* decision,
    const char* keys);

#endif
