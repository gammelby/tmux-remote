#include "tmuxremote_control_stream.h"

#include "tmuxremote.h"
#include "tmuxremote_prompt_protocol.h"
#include "tmuxremote_stream.h"
#include "tmuxremote_tmux.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <tinycbor/cbor.h>

#define NEWLINE "\n"
#define MAX_SESSION_SNAPSHOT_CBOR 65535

static void start_listen(struct tmuxremote_control_stream_listener* csl);
static void stream_callback(NabtoDeviceFuture* future,
                            NabtoDeviceError ec,
                            void* userData);
static void* monitor_thread_func(void* arg);
static void* reader_thread_func(void* arg);
static void ensure_monitor_started(struct tmuxremote_control_stream_listener* csl);
static uint8_t* encode_session_snapshot(const struct tmuxremote_tmux_list* list,
                                        size_t* outLen);
static bool tmux_lists_equal(const struct tmuxremote_tmux_list* a,
                             const struct tmuxremote_tmux_list* b);
static void send_to_stream(struct tmuxremote_active_control_stream* cs,
                           const uint8_t* data,
                           size_t len);
static void broadcast_to_all(struct tmuxremote_control_stream_listener* csl,
                             const uint8_t* data,
                             size_t len);

static void control_stream_retain(struct tmuxremote_active_control_stream* cs)
{
    atomic_fetch_add_explicit(&cs->refCount, 1, memory_order_relaxed);
}

static void control_stream_release_impl(struct tmuxremote_active_control_stream* cs)
{
    if (atomic_fetch_sub_explicit(&cs->refCount, 1, memory_order_acq_rel) == 1) {
        if (cs->readerThreadStarted) {
            pthread_join(cs->readerThread, NULL);
            cs->readerThreadStarted = false;
        }
        if (cs->stream != NULL) {
            nabto_device_stream_free(cs->stream);
        }
        pthread_mutex_destroy(&cs->writeMutex);
        free(cs);
    }
}

#ifdef TMUXREMOTE_TESTING
void tmuxremote_control_stream_release(struct tmuxremote_active_control_stream* cs)
{
    control_stream_release_impl(cs);
}
#endif

#ifdef TMUXREMOTE_TESTING
int tmuxremote_control_stream_collect_targets_for_ref(
    struct tmuxremote_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    struct tmuxremote_active_control_stream** out,
    int cap)
#else
static int collect_targets_for_ref(
    struct tmuxremote_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    struct tmuxremote_active_control_stream** out,
    int cap)
#endif
{
    int count = 0;

    pthread_mutex_lock(&csl->streamListMutex);
    struct tmuxremote_active_control_stream* cs = csl->activeStreams;
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

void tmuxremote_control_stream_listener_init(
    struct tmuxremote_control_stream_listener* csl,
    NabtoDevice* device,
    struct tmuxremote* app)
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
        device, csl->listener, TMUXREMOTE_CONTROL_STREAM_PORT);
    if (ec != NABTO_DEVICE_EC_OK) {
        printf("Failed to init control stream listener: %s" NEWLINE,
               nabto_device_error_get_message(ec));
        return;
    }

    start_listen(csl);
}

void tmuxremote_control_stream_listener_stop(
    struct tmuxremote_control_stream_listener* csl)
{
    if (csl->listener != NULL) {
        nabto_device_listener_stop(csl->listener);
    }

    atomic_store(&csl->monitorStop, true);
    pthread_mutex_lock(&csl->notifyMutex);
    pthread_cond_signal(&csl->notifyCond);
    pthread_mutex_unlock(&csl->notifyMutex);

    pthread_mutex_lock(&csl->streamListMutex);
    struct tmuxremote_active_control_stream* cs = csl->activeStreams;
    while (cs != NULL) {
        atomic_store(&cs->closing, true);
        cs = cs->next;
    }
    pthread_mutex_unlock(&csl->streamListMutex);
}

void tmuxremote_control_stream_listener_join_monitor(
    struct tmuxremote_control_stream_listener* csl)
{
    if (csl->monitorStarted) {
        pthread_join(csl->monitorThread, NULL);
        csl->monitorStarted = false;
    }
}

void tmuxremote_control_stream_listener_deinit(
    struct tmuxremote_control_stream_listener* csl)
{
    tmuxremote_control_stream_listener_join_monitor(csl);

    pthread_mutex_lock(&csl->streamListMutex);
    struct tmuxremote_active_control_stream* cs = csl->activeStreams;
    csl->activeStreams = NULL;
    pthread_mutex_unlock(&csl->streamListMutex);

    while (cs != NULL) {
        struct tmuxremote_active_control_stream* next = cs->next;
        control_stream_release_impl(cs);
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

void tmuxremote_control_stream_notify(
    struct tmuxremote_control_stream_listener* csl)
{
    pthread_mutex_lock(&csl->notifyMutex);
    pthread_cond_signal(&csl->notifyCond);
    pthread_mutex_unlock(&csl->notifyMutex);
}

static void start_listen(struct tmuxremote_control_stream_listener* csl)
{
    nabto_device_listener_new_stream(csl->listener, csl->future, &csl->newStream);
    nabto_device_future_set_callback(csl->future, stream_callback, csl);
}

static void stream_accepted(NabtoDeviceFuture* future,
                            NabtoDeviceError ec,
                            void* userData);

static void stream_callback(NabtoDeviceFuture* future,
                            NabtoDeviceError ec,
                            void* userData)
{
    (void)future;

    struct tmuxremote_control_stream_listener* csl = userData;
    if (ec != NABTO_DEVICE_EC_OK) {
        return;
    }

    struct tmuxremote_active_control_stream* cs =
        calloc(1, sizeof(struct tmuxremote_active_control_stream));
    if (cs == NULL) {
        nabto_device_stream_free(csl->newStream);
        start_listen(csl);
        return;
    }

    cs->stream = csl->newStream;
    cs->device = csl->device;
    cs->app = csl->app;
    atomic_init(&cs->closing, false);
    atomic_init(&cs->needsPromptSync, true);
    atomic_init(&cs->needsSessionSync, true);
    atomic_init(&cs->refCount, 1);
    pthread_mutex_init(&cs->writeMutex, NULL);

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

static void stream_accepted(NabtoDeviceFuture* future,
                            NabtoDeviceError ec,
                            void* userData)
{
    struct tmuxremote_active_control_stream* cs = userData;
    nabto_device_future_free(future);

    if (ec != NABTO_DEVICE_EC_OK) {
        control_stream_release_impl(cs);
        return;
    }

    NabtoDeviceConnectionRef ref =
        nabto_device_stream_get_connection_ref(cs->stream);
    cs->connectionRef = ref;

    if (!tmuxremote_iam_check_access_ref(&cs->app->iam, ref, "Terminal:ListSessions")) {
        control_stream_release_impl(cs);
        return;
    }

    if (pthread_create(&cs->readerThread, NULL, reader_thread_func, cs) == 0) {
        cs->readerThreadStarted = true;
    } else {
        printf("Failed to start control stream reader thread" NEWLINE);
    }

    struct tmuxremote_control_stream_listener* csl =
        &cs->app->controlStreamListener;
    pthread_mutex_lock(&csl->streamListMutex);
    cs->next = csl->activeStreams;
    csl->activeStreams = cs;
    pthread_mutex_unlock(&csl->streamListMutex);

    ensure_monitor_started(csl);
    tmuxremote_control_stream_notify(csl);
}

static bool read_exactly(struct tmuxremote_active_control_stream* cs,
                         uint8_t* buf,
                         size_t count)
{
    size_t total = 0;
    while (total < count && !atomic_load(&cs->closing)) {
        size_t readLen = 0;
        NabtoDeviceFuture* f = nabto_device_future_new(cs->device);
        if (f == NULL) {
            return false;
        }
        nabto_device_stream_read_some(cs->stream, f, buf + total, count - total, &readLen);
        NabtoDeviceError ec = nabto_device_future_wait(f);
        nabto_device_future_free(f);
        if (ec != NABTO_DEVICE_EC_OK) {
            return false;
        }
        total += readLen;
    }
    return total == count;
}

static void* reader_thread_func(void* arg)
{
    struct tmuxremote_active_control_stream* cs = arg;

    while (!atomic_load(&cs->closing)) {
        uint8_t lenBuf[4];
        if (!read_exactly(cs, lenBuf, 4)) {
            break;
        }

        uint32_t payloadLen = ((uint32_t)lenBuf[0] << 24) |
                              ((uint32_t)lenBuf[1] << 16) |
                              ((uint32_t)lenBuf[2] << 8) |
                              ((uint32_t)lenBuf[3]);

        if (payloadLen == 0 || payloadLen > 65536) {
            break;
        }

        uint8_t* payload = malloc(payloadLen);
        if (payload == NULL) {
            break;
        }

        if (!read_exactly(cs, payload, payloadLen)) {
            free(payload);
            break;
        }

        size_t framedLen = 4 + payloadLen;
        uint8_t* framed = malloc(framedLen);
        if (framed == NULL) {
            free(payload);
            break;
        }
        memcpy(framed, lenBuf, 4);
        memcpy(framed + 4, payload, payloadLen);
        free(payload);

        tmuxremote_prompt_resolve_message resolve;
        bool ok = tmuxremote_prompt_protocol_decode_resolve(
            framed,
            framedLen,
            &resolve);
        free(framed);

        if (ok) {
            tmuxremote_stream_resolve_prompt_for_ref(
                &cs->app->streamListener,
                cs->connectionRef,
                resolve.instance_id,
                resolve.decision,
                resolve.keys);
            tmuxremote_prompt_protocol_free_resolve(&resolve);
        }
    }

    atomic_store(&cs->closing, true);
    return NULL;
}

static void ensure_monitor_started(struct tmuxremote_control_stream_listener* csl)
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

static void* monitor_thread_func(void* arg)
{
    struct tmuxremote_control_stream_listener* csl = arg;
    struct tmuxremote_tmux_list prevList;
    memset(&prevList, 0, sizeof(prevList));
    bool hasPrev = false;

    while (!atomic_load(&csl->monitorStop)) {
        struct tmuxremote_tmux_list currentList;
        memset(&currentList, 0, sizeof(currentList));
        tmuxremote_tmux_list_sessions(&currentList);

        bool changed = !hasPrev || !tmux_lists_equal(&prevList, &currentList);

        struct tmuxremote_active_control_stream* sessionSyncSnapshot[MAX_CONTROL_STREAMS];
        int sessionSyncCount = 0;
        pthread_mutex_lock(&csl->streamListMutex);
        struct tmuxremote_active_control_stream* sss = csl->activeStreams;
        while (sss != NULL && sessionSyncCount < MAX_CONTROL_STREAMS) {
            if (atomic_load(&sss->needsSessionSync) &&
                !atomic_load(&sss->closing)) {
                control_stream_retain(sss);
                sessionSyncSnapshot[sessionSyncCount++] = sss;
                atomic_store(&sss->needsSessionSync, false);
            }
            sss = sss->next;
        }
        pthread_mutex_unlock(&csl->streamListMutex);

        if (changed || sessionSyncCount > 0) {
            size_t msgLen = 0;
            uint8_t* msg = encode_session_snapshot(&currentList, &msgLen);
            if (msg != NULL) {
                if (changed) {
                    broadcast_to_all(csl, msg, msgLen);
                } else {
                    for (int i = 0; i < sessionSyncCount; i++) {
                        send_to_stream(sessionSyncSnapshot[i], msg, msgLen);
                    }
                }
                free(msg);
            } else {
                printf("Failed to encode control stream session snapshot" NEWLINE);
            }
        }

        for (int i = 0; i < sessionSyncCount; i++) {
            control_stream_release_impl(sessionSyncSnapshot[i]);
        }

        if (changed) {
            prevList = currentList;
            hasPrev = true;
        }

        {
            struct tmuxremote_active_control_stream* syncSnapshot[MAX_CONTROL_STREAMS];
            int syncCount = 0;

            pthread_mutex_lock(&csl->streamListMutex);
            struct tmuxremote_active_control_stream* sc = csl->activeStreams;
            while (sc != NULL && syncCount < MAX_CONTROL_STREAMS) {
                if (atomic_load(&sc->needsPromptSync) &&
                    !atomic_load(&sc->closing)) {
                    control_stream_retain(sc);
                    syncSnapshot[syncCount++] = sc;
                    atomic_store(&sc->needsPromptSync, false);
                }
                sc = sc->next;
            }
            pthread_mutex_unlock(&csl->streamListMutex);

            for (int i = 0; i < syncCount; i++) {
                tmuxremote_prompt_instance* active =
                    tmuxremote_stream_copy_active_prompt_for_ref(
                        &csl->app->streamListener,
                        syncSnapshot[i]->connectionRef);

                if (active != NULL) {
                    size_t msgLen = 0;
                    uint8_t* msg =
                        tmuxremote_prompt_protocol_encode_present(active, &msgLen);
                    if (msg != NULL) {
                        send_to_stream(syncSnapshot[i], msg, msgLen);
                        free(msg);
                    }
                    tmuxremote_prompt_instance_free(active);
                    free(active);
                }

                control_stream_release_impl(syncSnapshot[i]);
            }
        }

        pthread_mutex_lock(&csl->streamListMutex);
        struct tmuxremote_active_control_stream** pp = &csl->activeStreams;
        while (*pp != NULL) {
            if (atomic_load(&(*pp)->closing)) {
                struct tmuxremote_active_control_stream* dead = *pp;
                *pp = dead->next;
                control_stream_release_impl(dead);
            } else {
                pp = &(*pp)->next;
            }
        }
        bool hasStreams = (csl->activeStreams != NULL);
        pthread_mutex_unlock(&csl->streamListMutex);

        if (!hasStreams && hasPrev) {
            hasPrev = false;
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        long ms = TMUXREMOTE_SESSION_POLL_INTERVAL_MS;
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

static bool tmux_lists_equal(const struct tmuxremote_tmux_list* a,
                             const struct tmuxremote_tmux_list* b)
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

static uint8_t* encode_session_snapshot(const struct tmuxremote_tmux_list* list,
                                        size_t* outLen)
{
    size_t cborCap = 2048;

    while (cborCap <= MAX_SESSION_SNAPSHOT_CBOR) {
        uint8_t* cborBuf = malloc(cborCap);
        if (cborBuf == NULL) {
            return NULL;
        }

        CborEncoder encoder;
        cbor_encoder_init(&encoder, cborBuf, cborCap, 0);

        CborEncoder mapEncoder;
        cbor_encoder_create_map(&encoder, &mapEncoder, 2);

        cbor_encode_text_stringz(&mapEncoder, "type");
        cbor_encode_text_stringz(&mapEncoder, "sessions");

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
        size_t extra = cbor_encoder_get_extra_bytes_needed(&encoder);

        if (err == CborNoError && extra == 0) {
            size_t cborLen = cbor_encoder_get_buffer_size(&encoder, cborBuf);
            if (cborLen >= 65536) {
                free(cborBuf);
                return NULL;
            }

            size_t totalLen = 4 + cborLen;
            uint8_t* buf = malloc(totalLen);
            if (buf == NULL) {
                free(cborBuf);
                return NULL;
            }

            uint32_t lenBE = htonl((uint32_t)cborLen);
            memcpy(buf, &lenBE, 4);
            memcpy(buf + 4, cborBuf, cborLen);
            free(cborBuf);

            *outLen = totalLen;
            return buf;
        }

        free(cborBuf);
        if (extra == 0 || cborCap + extra + 512 > MAX_SESSION_SNAPSHOT_CBOR) {
            return NULL;
        }
        cborCap += extra + 512;
    }

    return NULL;
}

static void send_to_stream(struct tmuxremote_active_control_stream* cs,
                           const uint8_t* data,
                           size_t len)
{
    if (atomic_load(&cs->closing)) {
        return;
    }

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

static void broadcast_to_all(struct tmuxremote_control_stream_listener* csl,
                             const uint8_t* data,
                             size_t len)
{
    struct tmuxremote_active_control_stream* snapshot[MAX_CONTROL_STREAMS];
    int count = 0;

    pthread_mutex_lock(&csl->streamListMutex);
    struct tmuxremote_active_control_stream* cs = csl->activeStreams;
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

static void send_to_ref(struct tmuxremote_control_stream_listener* csl,
                        NabtoDeviceConnectionRef ref,
                        const uint8_t* data,
                        size_t len)
{
    struct tmuxremote_active_control_stream* snapshot[MAX_CONTROL_STREAMS];
#ifdef TMUXREMOTE_TESTING
    int count = tmuxremote_control_stream_collect_targets_for_ref(
        csl, ref, snapshot, MAX_CONTROL_STREAMS);
#else
    int count = collect_targets_for_ref(csl, ref, snapshot, MAX_CONTROL_STREAMS);
#endif

    for (int i = 0; i < count; i++) {
        send_to_stream(snapshot[i], data, len);
        control_stream_release_impl(snapshot[i]);
    }
}

void tmuxremote_control_stream_send_prompt_present_for_ref(
    struct tmuxremote_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    const tmuxremote_prompt_instance* instance)
{
    size_t msgLen = 0;
    uint8_t* buf = tmuxremote_prompt_protocol_encode_present(instance, &msgLen);
    if (buf == NULL) {
        return;
    }

    send_to_ref(csl, ref, buf, msgLen);
    free(buf);
}

void tmuxremote_control_stream_send_prompt_update_for_ref(
    struct tmuxremote_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    const tmuxremote_prompt_instance* instance)
{
    size_t msgLen = 0;
    uint8_t* buf = tmuxremote_prompt_protocol_encode_update(instance, &msgLen);
    if (buf == NULL) {
        return;
    }

    send_to_ref(csl, ref, buf, msgLen);
    free(buf);
}

void tmuxremote_control_stream_send_prompt_gone_for_ref(
    struct tmuxremote_control_stream_listener* csl,
    NabtoDeviceConnectionRef ref,
    const char* instance_id)
{
    size_t msgLen = 0;
    uint8_t* buf = tmuxremote_prompt_protocol_encode_gone(instance_id, &msgLen);
    if (buf == NULL) {
        return;
    }

    send_to_ref(csl, ref, buf, msgLen);
    free(buf);
}
