#ifndef TMUXREMOTE_CONTROL_STREAM_H_
#define TMUXREMOTE_CONTROL_STREAM_H_

#include <nabto/nabto_device.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>

#include "pe_prompt.h"

#define TMUXREMOTE_CONTROL_STREAM_PORT 2
#define TMUXREMOTE_SESSION_POLL_INTERVAL_MS 2000
#define MAX_CONTROL_STREAMS 16

struct tmuxremote;

struct tmuxremote_active_control_stream {
    NabtoDeviceStream* stream;
    NabtoDevice* device;
    struct tmuxremote* app;
    pthread_mutex_t writeMutex;
    atomic_bool closing;
    atomic_bool needsPromptSync;
    atomic_bool needsSessionSync;
    NabtoDeviceConnectionRef connectionRef;
    atomic_uint refCount;
    pthread_t readerThread;
    bool readerThreadStarted;
    struct tmuxremote_active_control_stream* next;
};

struct tmuxremote_control_stream_listener {
    NabtoDevice* device;
    struct tmuxremote* app;
    NabtoDeviceListener* listener;
    NabtoDeviceFuture* future;
    NabtoDeviceStream* newStream;

    pthread_t monitorThread;
    bool monitorStarted;
    atomic_bool monitorStop;

    pthread_mutex_t streamListMutex;
    struct tmuxremote_active_control_stream* activeStreams;

    pthread_mutex_t notifyMutex;
    pthread_cond_t notifyCond;
};

void tmuxremote_control_stream_listener_init(
    struct tmuxremote_control_stream_listener* csl,
    NabtoDevice* device,
    struct tmuxremote* app);

void tmuxremote_control_stream_listener_stop(
    struct tmuxremote_control_stream_listener* csl);

void tmuxremote_control_stream_listener_join_monitor(
    struct tmuxremote_control_stream_listener* csl);

void tmuxremote_control_stream_listener_deinit(
    struct tmuxremote_control_stream_listener* csl);

void tmuxremote_control_stream_notify(
    struct tmuxremote_control_stream_listener* csl);

void tmuxremote_control_stream_send_prompt_present_for_ref(
    struct tmuxremote_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    const pe_prompt_instance* instance);

void tmuxremote_control_stream_send_prompt_update_for_ref(
    struct tmuxremote_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    const pe_prompt_instance* instance);

void tmuxremote_control_stream_send_prompt_gone_for_ref(
    struct tmuxremote_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    const char* instance_id);

#ifdef TMUXREMOTE_TESTING
int tmuxremote_control_stream_collect_targets_for_ref(
    struct tmuxremote_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    struct tmuxremote_active_control_stream** out,
    int cap);

void tmuxremote_control_stream_release(struct tmuxremote_active_control_stream* cs);
#endif

#endif
