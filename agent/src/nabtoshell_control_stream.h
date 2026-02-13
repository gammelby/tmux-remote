#ifndef NABTOSHELL_CONTROL_STREAM_H_
#define NABTOSHELL_CONTROL_STREAM_H_

#include <nabto/nabto_device.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <pthread.h>

#define NABTOSHELL_CONTROL_STREAM_PORT 2
#define NABTOSHELL_SESSION_POLL_INTERVAL_MS 2000
#define MAX_CONTROL_STREAMS 16

struct nabtoshell;

struct nabtoshell_active_control_stream {
    NabtoDeviceStream* stream;
    NabtoDevice* device;
    struct nabtoshell* app;
    pthread_mutex_t writeMutex;
    atomic_bool closing;
    atomic_bool needsPatternSync;
    atomic_bool needsSessionSync;
    NabtoDeviceConnectionRef connectionRef;
    atomic_uint refCount;
    pthread_t readerThread;
    bool readerThreadStarted;
    struct nabtoshell_active_control_stream* next;
};

struct nabtoshell_control_stream_listener {
    NabtoDevice* device;
    struct nabtoshell* app;
    NabtoDeviceListener* listener;
    NabtoDeviceFuture* future;
    NabtoDeviceStream* newStream;

    /* Session monitor thread */
    pthread_t monitorThread;
    bool monitorStarted;
    atomic_bool monitorStop;

    /* Protects activeStreams linked list */
    pthread_mutex_t streamListMutex;
    struct nabtoshell_active_control_stream* activeStreams;

    /* Condition variable for waking monitor (notify or periodic) */
    pthread_mutex_t notifyMutex;
    pthread_cond_t notifyCond;
};

void nabtoshell_control_stream_listener_init(
    struct nabtoshell_control_stream_listener* csl,
    NabtoDevice* device,
    struct nabtoshell* app);

void nabtoshell_control_stream_listener_stop(
    struct nabtoshell_control_stream_listener* csl);

/* Join the monitor thread only. Must be called before destroying
 * resources the monitor accesses (e.g. stream listener mutex).
 * Safe to call even if monitor was never started. */
void nabtoshell_control_stream_listener_join_monitor(
    struct nabtoshell_control_stream_listener* csl);

void nabtoshell_control_stream_listener_deinit(
    struct nabtoshell_control_stream_listener* csl);

/* Wake the monitor thread to do an immediate poll+broadcast. Safe to call
 * from any thread including Nabto callback threads (non-blocking). */
void nabtoshell_control_stream_notify(
    struct nabtoshell_control_stream_listener* csl);

/* Send a pattern match event to control stream(s) matching the given
 * connectionRef. Safe to call from any thread. */
#include "nabtoshell_pattern_matcher.h"
void nabtoshell_control_stream_send_pattern_match_for_ref(
    struct nabtoshell_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    const nabtoshell_pattern_match* match);

/* Send a pattern dismiss event to control stream(s) matching the given
 * connectionRef. Safe to call from any thread. */
void nabtoshell_control_stream_send_pattern_dismiss_for_ref(
    struct nabtoshell_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref);

#ifdef NABTOSHELL_TESTING
/* Test seam: collect control streams matching ref. Retains each target.
 * Caller must release via control_stream_release after use. */
int nabtoshell_control_stream_collect_targets_for_ref(
    struct nabtoshell_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    struct nabtoshell_active_control_stream** out,
    int cap);

void nabtoshell_control_stream_release(struct nabtoshell_active_control_stream* cs);
#endif

#endif
