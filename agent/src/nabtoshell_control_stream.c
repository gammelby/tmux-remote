#include "nabtoshell_control_stream.h"
#include "nabtoshell.h"
#include "nabtoshell_tmux.h"
#include "nabtoshell_stream.h"
#include "nabtoshell_pattern_matcher.h"
#include "nabtoshell_pattern_cbor.h"

#include <tinycbor/cbor.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NEWLINE "\n"

static void start_listen(struct nabtoshell_control_stream_listener* csl);
static void stream_callback(NabtoDeviceFuture* future, NabtoDeviceError ec,
                            void* userData);
static void* monitor_thread_func(void* arg);
static void ensure_monitor_started(struct nabtoshell_control_stream_listener* csl);
static uint8_t* encode_session_snapshot(const struct nabtoshell_tmux_list* list,
                                        size_t* outLen);
static bool tmux_lists_equal(const struct nabtoshell_tmux_list* a,
                             const struct nabtoshell_tmux_list* b);
static void send_to_stream(struct nabtoshell_active_control_stream* cs,
                           const uint8_t* data, size_t len);
static void broadcast_to_all(struct nabtoshell_control_stream_listener* csl,
                             const uint8_t* data, size_t len);

/* ---- Refcount-based lifetime management ---- */

static void control_stream_retain(struct nabtoshell_active_control_stream* cs)
{
    atomic_fetch_add_explicit(&cs->refCount, 1, memory_order_relaxed);
}

static void control_stream_release_impl(struct nabtoshell_active_control_stream* cs)
{
    if (atomic_fetch_sub_explicit(&cs->refCount, 1, memory_order_acq_rel) == 1) {
        if (cs->stream != NULL) {
            nabto_device_stream_free(cs->stream);
        }
        pthread_mutex_destroy(&cs->writeMutex);
        free(cs);
    }
}

#ifdef NABTOSHELL_TESTING
void nabtoshell_control_stream_release(struct nabtoshell_active_control_stream* cs)
{
    control_stream_release_impl(cs);
}
#endif

/* Collect control streams matching a given connectionRef.
 * Each returned pointer is retained; caller must release after use. */
#ifdef NABTOSHELL_TESTING
int nabtoshell_control_stream_collect_targets_for_ref(
    struct nabtoshell_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    struct nabtoshell_active_control_stream** out,
    int cap)
#else
static int collect_targets_for_ref(
    struct nabtoshell_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    struct nabtoshell_active_control_stream** out,
    int cap)
#endif
{
    int count = 0;
    pthread_mutex_lock(&csl->streamListMutex);
    struct nabtoshell_active_control_stream* cs = csl->activeStreams;
    while (cs != NULL && count < cap) {
        if (!atomic_load(&cs->closing) && cs->connectionRef == ref) {
            control_stream_retain(cs);
            out[count++] = cs;
        }
        cs = cs->next;
    }
    pthread_mutex_unlock(&csl->streamListMutex);
    return count;
}

/* ---- Public / Module API ---- */

void nabtoshell_control_stream_listener_init(
    struct nabtoshell_control_stream_listener* csl,
    NabtoDevice* device,
    struct nabtoshell* app)
{
    memset(csl, 0, sizeof(*csl));
    csl->device = device;
    csl->app = app;
    atomic_init(&csl->monitorStop, false);

    pthread_mutex_init(&csl->streamListMutex, NULL);
    pthread_mutex_init(&csl->notifyMutex, NULL);
    pthread_cond_init(&csl->notifyCond, NULL);

    csl->listener = nabto_device_listener_new(device);
    csl->future = nabto_device_future_new(device);

    NabtoDeviceError ec = nabto_device_stream_init_listener(
        device, csl->listener, NABTOSHELL_CONTROL_STREAM_PORT);
    if (ec != NABTO_DEVICE_EC_OK) {
        printf("Failed to init control stream listener: %s" NEWLINE,
               nabto_device_error_get_message(ec));
        return;
    }

    start_listen(csl);
}

void nabtoshell_control_stream_listener_stop(
    struct nabtoshell_control_stream_listener* csl)
{
    if (csl->listener != NULL) {
        nabto_device_listener_stop(csl->listener);
    }

    /* Signal monitor thread to stop */
    atomic_store(&csl->monitorStop, true);
    pthread_mutex_lock(&csl->notifyMutex);
    pthread_cond_signal(&csl->notifyCond);
    pthread_mutex_unlock(&csl->notifyMutex);

    /* Mark all active control streams for closure */
    pthread_mutex_lock(&csl->streamListMutex);
    struct nabtoshell_active_control_stream* cs = csl->activeStreams;
    while (cs != NULL) {
        atomic_store(&cs->closing, true);
        cs = cs->next;
    }
    pthread_mutex_unlock(&csl->streamListMutex);
}

void nabtoshell_control_stream_listener_join_monitor(
    struct nabtoshell_control_stream_listener* csl)
{
    if (csl->monitorStarted) {
        pthread_join(csl->monitorThread, NULL);
        csl->monitorStarted = false;
    }
}

void nabtoshell_control_stream_listener_deinit(
    struct nabtoshell_control_stream_listener* csl)
{
    /* Join monitor if not already joined */
    nabtoshell_control_stream_listener_join_monitor(csl);

    /* Free all active control streams */
    pthread_mutex_lock(&csl->streamListMutex);
    struct nabtoshell_active_control_stream* cs = csl->activeStreams;
    csl->activeStreams = NULL;
    pthread_mutex_unlock(&csl->streamListMutex);

    while (cs != NULL) {
        struct nabtoshell_active_control_stream* next = cs->next;
        control_stream_release_impl(cs);  /* drop list ownership */
        cs = next;
    }

    if (csl->future != NULL) {
        nabto_device_future_free(csl->future);
        csl->future = NULL;
    }
    if (csl->listener != NULL) {
        nabto_device_listener_free(csl->listener);
        csl->listener = NULL;
    }

    pthread_cond_destroy(&csl->notifyCond);
    pthread_mutex_destroy(&csl->notifyMutex);
    pthread_mutex_destroy(&csl->streamListMutex);
}

void nabtoshell_control_stream_notify(
    struct nabtoshell_control_stream_listener* csl)
{
    /* Non-blocking: just signal the condition variable to wake the monitor. */
    pthread_mutex_lock(&csl->notifyMutex);
    pthread_cond_signal(&csl->notifyCond);
    pthread_mutex_unlock(&csl->notifyMutex);
}

/* ---- Internal ---- */

static void start_listen(struct nabtoshell_control_stream_listener* csl)
{
    nabto_device_listener_new_stream(csl->listener, csl->future, &csl->newStream);
    nabto_device_future_set_callback(csl->future, stream_callback, csl);
}

/*
 * Called on the Nabto SDK event loop thread. MUST NOT block.
 * Accepts the control stream, does IAM check, adds to linked list,
 * then ensures the monitor thread is running.
 */
static void stream_accepted(NabtoDeviceFuture* future, NabtoDeviceError ec,
                            void* userData);

static void stream_callback(NabtoDeviceFuture* future, NabtoDeviceError ec,
                            void* userData)
{
    (void)future;
    struct nabtoshell_control_stream_listener* csl = userData;
    if (ec != NABTO_DEVICE_EC_OK) {
        return;
    }

    struct nabtoshell_active_control_stream* cs =
        calloc(1, sizeof(struct nabtoshell_active_control_stream));
    if (cs == NULL) {
        nabto_device_stream_free(csl->newStream);
        start_listen(csl);
        return;
    }

    cs->stream = csl->newStream;
    cs->device = csl->device;
    cs->app = csl->app;
    atomic_init(&cs->closing, false);
    atomic_init(&cs->needsPatternSync, true);
    atomic_init(&cs->refCount, 1);
    pthread_mutex_init(&cs->writeMutex, NULL);

    /* Accept the stream */
    NabtoDeviceFuture* acceptFuture = nabto_device_future_new(csl->device);
    if (acceptFuture == NULL) {
        nabto_device_stream_free(cs->stream);
        pthread_mutex_destroy(&cs->writeMutex);
        free(cs);
        start_listen(csl);
        return;
    }
    nabto_device_stream_accept(cs->stream, acceptFuture);
    nabto_device_future_set_callback(acceptFuture, stream_accepted, cs);

    csl->newStream = NULL;
    start_listen(csl);
}

static void stream_accepted(NabtoDeviceFuture* future, NabtoDeviceError ec,
                            void* userData)
{
    struct nabtoshell_active_control_stream* cs = userData;
    nabto_device_future_free(future);

    if (ec != NABTO_DEVICE_EC_OK) {
        control_stream_release_impl(cs);
        return;
    }

    /* IAM check */
    NabtoDeviceConnectionRef ref =
        nabto_device_stream_get_connection_ref(cs->stream);
    cs->connectionRef = ref;
    if (!nabtoshell_iam_check_access_ref(&cs->app->iam, ref, "Terminal:ListSessions")) {
        control_stream_release_impl(cs);
        return;
    }

    /* Add to linked list (under mutex, quick operation) */
    struct nabtoshell_control_stream_listener* csl =
        &cs->app->controlStreamListener;
    pthread_mutex_lock(&csl->streamListMutex);
    cs->next = csl->activeStreams;
    csl->activeStreams = cs;
    pthread_mutex_unlock(&csl->streamListMutex);

    /* Ensure monitor thread is running and wake it for initial snapshot */
    ensure_monitor_started(csl);
    nabtoshell_control_stream_notify(csl);
}

static void ensure_monitor_started(struct nabtoshell_control_stream_listener* csl)
{
    pthread_mutex_lock(&csl->streamListMutex);
    if (!csl->monitorStarted) {
        if (pthread_create(&csl->monitorThread, NULL, monitor_thread_func, csl) == 0) {
            csl->monitorStarted = true;
        } else {
            printf("Failed to start control stream monitor thread" NEWLINE);
        }
    }
    pthread_mutex_unlock(&csl->streamListMutex);
}

/*
 * Monitor thread: polls tmux session list periodically and on notify,
 * broadcasts changes to all connected control streams. This thread owns
 * all blocking I/O (tmux fork, stream writes), so it is safe to block here.
 */
static void* monitor_thread_func(void* arg)
{
    struct nabtoshell_control_stream_listener* csl = arg;
    struct nabtoshell_tmux_list prevList;
    memset(&prevList, 0, sizeof(prevList));
    bool hasPrev = false;

    while (!atomic_load(&csl->monitorStop)) {
        /* Poll tmux sessions */
        struct nabtoshell_tmux_list currentList;
        memset(&currentList, 0, sizeof(currentList));
        nabtoshell_tmux_list_sessions(&currentList);

        bool changed = !hasPrev || !tmux_lists_equal(&prevList, &currentList);

        if (changed) {
            size_t msgLen = 0;
            uint8_t* msg = encode_session_snapshot(&currentList, &msgLen);
            if (msg != NULL) {
                broadcast_to_all(csl, msg, msgLen);
                free(msg);
            }
            prevList = currentList;
            hasPrev = true;
        }

        /* Replay active pattern match to newly connected control streams.
         * Each control stream gets the match from the data stream sharing
         * its connectionRef. */
        {
            struct nabtoshell_active_control_stream* syncSnapshot[MAX_CONTROL_STREAMS];
            int syncCount = 0;

            pthread_mutex_lock(&csl->streamListMutex);
            struct nabtoshell_active_control_stream* sc = csl->activeStreams;
            while (sc != NULL && syncCount < MAX_CONTROL_STREAMS) {
                if (atomic_load(&sc->needsPatternSync) &&
                    !atomic_load(&sc->closing)) {
                    control_stream_retain(sc);
                    syncSnapshot[syncCount++] = sc;
                    atomic_store(&sc->needsPatternSync, false);
                }
                sc = sc->next;
            }
            pthread_mutex_unlock(&csl->streamListMutex);

            if (syncCount > 0) {
                for (int i = 0; i < syncCount; i++) {
                    nabtoshell_pattern_match* match =
                        nabtoshell_stream_copy_active_match_for_ref(
                            &csl->app->streamListener,
                            syncSnapshot[i]->connectionRef);
                    if (match != NULL) {
                        size_t matchMsgLen = 0;
                        uint8_t* matchMsg =
                            nabtoshell_pattern_cbor_encode_match(match, &matchMsgLen);
                        if (matchMsg != NULL) {
                            send_to_stream(syncSnapshot[i], matchMsg, matchMsgLen);
                            free(matchMsg);
                        }
                        nabtoshell_pattern_match_free(match);
                    }
                    control_stream_release_impl(syncSnapshot[i]);
                }
            }
        }

        /* Remove closed streams from the list */
        pthread_mutex_lock(&csl->streamListMutex);
        struct nabtoshell_active_control_stream** pp = &csl->activeStreams;
        while (*pp != NULL) {
            if (atomic_load(&(*pp)->closing)) {
                struct nabtoshell_active_control_stream* dead = *pp;
                *pp = dead->next;
                control_stream_release_impl(dead);  /* drop list ownership */
            } else {
                pp = &(*pp)->next;
            }
        }
        bool hasStreams = (csl->activeStreams != NULL);
        pthread_mutex_unlock(&csl->streamListMutex);

        if (!hasStreams && hasPrev) {
            /* No more control streams connected. Reset state so
             * we send a full snapshot on the next connection. */
            hasPrev = false;
        }

        /* Wait for notify or timeout */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        long ms = NABTOSHELL_SESSION_POLL_INTERVAL_MS;
        ts.tv_sec += ms / 1000;
        ts.tv_nsec += (ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }

        pthread_mutex_lock(&csl->notifyMutex);
        pthread_cond_timedwait(&csl->notifyCond, &csl->notifyMutex, &ts);
        pthread_mutex_unlock(&csl->notifyMutex);
    }

    return NULL;
}

static bool tmux_lists_equal(const struct nabtoshell_tmux_list* a,
                             const struct nabtoshell_tmux_list* b)
{
    if (a->count != b->count) {
        return false;
    }
    for (int i = 0; i < a->count; i++) {
        if (strcmp(a->sessions[i].name, b->sessions[i].name) != 0 ||
            a->sessions[i].cols != b->sessions[i].cols ||
            a->sessions[i].rows != b->sessions[i].rows ||
            a->sessions[i].attached != b->sessions[i].attached) {
            return false;
        }
    }
    return true;
}

/*
 * Encode a session list as a length-prefixed CBOR message:
 *   [4 bytes: big-endian uint32 payload length][N bytes: CBOR]
 *
 * CBOR format: {"sessions": [{name, cols, rows, attached}, ...]}
 */
static uint8_t* encode_session_snapshot(const struct nabtoshell_tmux_list* list,
                                        size_t* outLen)
{
    uint8_t cborBuf[2048];
    CborEncoder encoder;
    cbor_encoder_init(&encoder, cborBuf, sizeof(cborBuf), 0);

    CborEncoder mapEncoder;
    cbor_encoder_create_map(&encoder, &mapEncoder, 1);

    cbor_encode_text_stringz(&mapEncoder, "sessions");

    CborEncoder arrayEncoder;
    cbor_encoder_create_array(&mapEncoder, &arrayEncoder, list->count);

    for (int i = 0; i < list->count; i++) {
        CborEncoder sessionMap;
        cbor_encoder_create_map(&arrayEncoder, &sessionMap, 4);

        cbor_encode_text_stringz(&sessionMap, "name");
        cbor_encode_text_stringz(&sessionMap, list->sessions[i].name);

        cbor_encode_text_stringz(&sessionMap, "cols");
        cbor_encode_uint(&sessionMap, list->sessions[i].cols);

        cbor_encode_text_stringz(&sessionMap, "rows");
        cbor_encode_uint(&sessionMap, list->sessions[i].rows);

        cbor_encode_text_stringz(&sessionMap, "attached");
        cbor_encode_uint(&sessionMap, list->sessions[i].attached);

        cbor_encoder_close_container(&arrayEncoder, &sessionMap);
    }

    cbor_encoder_close_container(&mapEncoder, &arrayEncoder);
    CborError err = cbor_encoder_close_container(&encoder, &mapEncoder);

    if (err != CborNoError || cbor_encoder_get_extra_bytes_needed(&encoder) > 0)
        return NULL;

    size_t cborLen = cbor_encoder_get_buffer_size(&encoder, cborBuf);

    /* Allocate: 4-byte length prefix + CBOR payload */
    size_t totalLen = 4 + cborLen;
    uint8_t* buf = malloc(totalLen);
    if (buf == NULL) {
        return NULL;
    }

    /* Big-endian uint32 length prefix */
    uint32_t lenBE = htonl((uint32_t)cborLen);
    memcpy(buf, &lenBE, 4);
    memcpy(buf + 4, cborBuf, cborLen);

    *outLen = totalLen;
    return buf;
}

/*
 * Write a length-prefixed message to a single control stream.
 * Called from the monitor thread, so blocking writes are safe.
 * The stream's writeMutex serializes I/O and the closing flag guards
 * against writes to freed streams.
 */
static void send_to_stream(struct nabtoshell_active_control_stream* cs,
                           const uint8_t* data, size_t len)
{
    if (atomic_load(&cs->closing)) return;
    pthread_mutex_lock(&cs->writeMutex);
    if (!atomic_load(&cs->closing)) {
        NabtoDeviceFuture* f = nabto_device_future_new(cs->device);
        if (f != NULL) {
            nabto_device_stream_write(cs->stream, f, data, len);
            NabtoDeviceError ec = nabto_device_future_wait(f);
            nabto_device_future_free(f);
            if (ec != NABTO_DEVICE_EC_OK) {
                atomic_store(&cs->closing, true);
            }
        }
    }
    pthread_mutex_unlock(&cs->writeMutex);
}

/*
 * Send the encoded message to all connected control streams.
 * Called from the monitor thread, so blocking writes are safe.
 *
 * Takes a snapshot of active stream pointers under the mutex, then
 * iterates the snapshot without holding the list lock.
 */
static void broadcast_to_all(struct nabtoshell_control_stream_listener* csl,
                             const uint8_t* data, size_t len)
{
    struct nabtoshell_active_control_stream* snapshot[MAX_CONTROL_STREAMS];
    int count = 0;

    pthread_mutex_lock(&csl->streamListMutex);
    struct nabtoshell_active_control_stream* cs = csl->activeStreams;
    while (cs != NULL && count < MAX_CONTROL_STREAMS) {
        if (!atomic_load(&cs->closing)) {
            snapshot[count++] = cs;
        }
        cs = cs->next;
    }
    pthread_mutex_unlock(&csl->streamListMutex);

    for (int i = 0; i < count; i++) {
        send_to_stream(snapshot[i], data, len);
    }
}

static void send_to_ref(struct nabtoshell_control_stream_listener* csl,
                         NabtoDeviceConnectionRef ref,
                         const uint8_t* data, size_t len)
{
    struct nabtoshell_active_control_stream* snapshot[MAX_CONTROL_STREAMS];
#ifdef NABTOSHELL_TESTING
    int count = nabtoshell_control_stream_collect_targets_for_ref(
        csl, ref, snapshot, MAX_CONTROL_STREAMS);
#else
    int count = collect_targets_for_ref(csl, ref, snapshot, MAX_CONTROL_STREAMS);
#endif
    for (int i = 0; i < count; i++) {
        send_to_stream(snapshot[i], data, len);
        control_stream_release_impl(snapshot[i]);
    }
}

void nabtoshell_control_stream_send_pattern_match_for_ref(
    struct nabtoshell_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    const nabtoshell_pattern_match* match)
{
    size_t msgLen = 0;
    uint8_t* buf = nabtoshell_pattern_cbor_encode_match(match, &msgLen);
    if (buf == NULL) return;

    send_to_ref(csl, ref, buf, msgLen);
    free(buf);
}

void nabtoshell_control_stream_send_pattern_dismiss_for_ref(
    struct nabtoshell_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref)
{
    size_t msgLen = 0;
    uint8_t* buf = nabtoshell_pattern_cbor_encode_dismiss(&msgLen);
    if (buf == NULL) return;

    send_to_ref(csl, ref, buf, msgLen);
    free(buf);
}
