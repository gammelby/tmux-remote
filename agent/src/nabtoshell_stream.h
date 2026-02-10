#ifndef NABTOSHELL_STREAM_H_
#define NABTOSHELL_STREAM_H_

#include <nabto/nabto_device.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>
#include "nabtoshell_session.h"

#define NABTOSHELL_STREAM_BUFFER_SIZE 4096
#define NABTOSHELL_MAX_ACTIVE_STREAMS 8

struct nabtoshell;

struct nabtoshell_active_stream {
    uint64_t debugId;
    int64_t createdAtMs;
    NabtoDevice* device;
    NabtoDeviceStream* stream;
    struct nabtoshell* app;
    int ptyFd;
    pid_t childPid;
    atomic_bool closing;
    atomic_bool closeStarted;

    char sessionName[NABTOSHELL_SESSION_NAME_MAX];
    uint16_t sessionCols;
    uint16_t sessionRows;

    pthread_t setupThread;
    bool setupThreadStarted;
    pthread_t ptyReaderThread;
    bool ptyReaderThreadStarted;
    pthread_t streamReaderThread;
    bool streamReaderThreadStarted;

    struct nabtoshell_active_stream* next;
};

struct nabtoshell_stream_listener {
    NabtoDevice* device;
    struct nabtoshell* app;
    NabtoDeviceListener* listener;
    NabtoDeviceFuture* future;
    NabtoDeviceStream* newStream;

    struct nabtoshell_active_stream* activeStreams;
};

void nabtoshell_stream_listener_init(struct nabtoshell_stream_listener* sl,
                                     NabtoDevice* device,
                                     struct nabtoshell* app);
void nabtoshell_stream_listener_stop(struct nabtoshell_stream_listener* sl);
void nabtoshell_stream_listener_deinit(struct nabtoshell_stream_listener* sl);

int nabtoshell_stream_get_pty_fd(struct nabtoshell_stream_listener* sl,
                                 NabtoDeviceConnectionRef ref);

#endif
