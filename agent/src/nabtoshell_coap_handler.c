#include "nabtoshell_coap_handler.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

struct nabtoshell_coap_request_node {
    NabtoDeviceCoapRequest* request;
    uint64_t requestId;
    NabtoDeviceConnectionRef connectionRef;
    int64_t enqueuedAtMs;
    struct nabtoshell_coap_request_node* next;
};

static int64_t monotonic_ms(void);
static unsigned long current_tid(void);
static void start_listen(struct nabtoshell_coap_handler* handler);
static void request_callback(NabtoDeviceFuture* future, NabtoDeviceError ec,
                             void* userData);
static bool callback_enter(struct nabtoshell_coap_handler* handler);
static void callback_leave(struct nabtoshell_coap_handler* handler);
static void build_debug_name(char* buf, size_t bufSize, NabtoDeviceCoapMethod method,
                             const char** paths);
static bool enqueue_request(struct nabtoshell_coap_handler* handler,
                            NabtoDeviceCoapRequest* request);
static void* coap_worker_thread(void* userData);

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((int64_t)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

static unsigned long current_tid(void)
{
    return (unsigned long)pthread_self();
}

static const char* method_to_string(NabtoDeviceCoapMethod method)
{
    switch (method) {
        case NABTO_DEVICE_COAP_GET: return "GET";
        case NABTO_DEVICE_COAP_POST: return "POST";
        case NABTO_DEVICE_COAP_PUT: return "PUT";
        case NABTO_DEVICE_COAP_DELETE: return "DELETE";
        default: return "UNKNOWN";
    }
}

static void build_debug_name(char* buf, size_t bufSize, NabtoDeviceCoapMethod method,
                             const char** paths)
{
    if (bufSize == 0) {
        return;
    }
    snprintf(buf, bufSize, "%s", method_to_string(method));
    size_t used = strlen(buf);
    for (size_t i = 0; paths != NULL && paths[i] != NULL && used < bufSize - 1; i++) {
        int written = snprintf(buf + used, bufSize - used, "%s%s",
                               (i == 0 ? " /" : "/"), paths[i]);
        if (written <= 0) {
            break;
        }
        used += (size_t)written;
        if (used >= bufSize) {
            buf[bufSize - 1] = '\0';
            break;
        }
    }
}

NabtoDeviceError nabtoshell_coap_handler_init(
    struct nabtoshell_coap_handler* handler,
    NabtoDevice* device,
    struct nabtoshell* app,
    NabtoDeviceCoapMethod method,
    const char** paths,
    nabtoshell_coap_request_handler requestHandler)
{
    memset(handler, 0, sizeof(struct nabtoshell_coap_handler));
    handler->device = device;
    handler->app = app;
    handler->requestHandler = requestHandler;
    build_debug_name(handler->debugName, sizeof(handler->debugName), method, paths);

    if (pthread_mutex_init(&handler->queueMutex, NULL) != 0) {
        return NABTO_DEVICE_EC_OUT_OF_MEMORY;
    }
    if (pthread_cond_init(&handler->queueCond, NULL) != 0) {
        pthread_mutex_destroy(&handler->queueMutex);
        return NABTO_DEVICE_EC_OUT_OF_MEMORY;
    }
    if (pthread_cond_init(&handler->callbackCond, NULL) != 0) {
        pthread_cond_destroy(&handler->queueCond);
        pthread_mutex_destroy(&handler->queueMutex);
        return NABTO_DEVICE_EC_OUT_OF_MEMORY;
    }
    if (pthread_create(&handler->workerThread, NULL, coap_worker_thread, handler) != 0) {
        pthread_cond_destroy(&handler->callbackCond);
        pthread_cond_destroy(&handler->queueCond);
        pthread_mutex_destroy(&handler->queueMutex);
        return NABTO_DEVICE_EC_OUT_OF_MEMORY;
    }
    handler->workerStarted = true;

    handler->future = nabto_device_future_new(device);
    handler->listener = nabto_device_listener_new(device);
    if (handler->future == NULL || handler->listener == NULL) {
        nabtoshell_coap_handler_deinit(handler);
        return NABTO_DEVICE_EC_OUT_OF_MEMORY;
    }

    NabtoDeviceError ec = nabto_device_coap_init_listener(
        device, handler->listener, method, paths);
    if (ec == NABTO_DEVICE_EC_OK) {
        start_listen(handler);
    } else {
        nabtoshell_coap_handler_deinit(handler);
    }
    return ec;
}

void nabtoshell_coap_handler_stop(struct nabtoshell_coap_handler* handler)
{
    pthread_mutex_lock(&handler->queueMutex);
    handler->stopping = true;
    pthread_cond_broadcast(&handler->queueCond);
    pthread_mutex_unlock(&handler->queueMutex);

    if (handler->device != NULL && handler->listener != NULL) {
        nabto_device_listener_stop(handler->listener);
    }
}

void nabtoshell_coap_handler_deinit(struct nabtoshell_coap_handler* handler)
{
    nabtoshell_coap_handler_stop(handler);

    pthread_mutex_lock(&handler->queueMutex);
    while (handler->activeCallbacks > 0) {
        pthread_cond_wait(&handler->callbackCond, &handler->queueMutex);
    }
    pthread_mutex_unlock(&handler->queueMutex);

    if (handler->workerStarted) {
        pthread_cond_broadcast(&handler->queueCond);
        pthread_join(handler->workerThread, NULL);
        handler->workerStarted = false;
    }

    pthread_mutex_lock(&handler->queueMutex);
    struct nabtoshell_coap_request_node* node = handler->queueHead;
    handler->queueHead = NULL;
    handler->queueTail = NULL;
    pthread_mutex_unlock(&handler->queueMutex);

    while (node != NULL) {
        struct nabtoshell_coap_request_node* next = node->next;
        nabto_device_coap_request_free(node->request);
        free(node);
        node = next;
    }

    if (handler->device != NULL) {
        if (handler->future != NULL) {
            nabto_device_future_free(handler->future);
        }
        if (handler->listener != NULL) {
            nabto_device_listener_free(handler->listener);
        }
        handler->device = NULL;
        handler->app = NULL;
        handler->listener = NULL;
        handler->future = NULL;
    }

    pthread_cond_destroy(&handler->callbackCond);
    pthread_cond_destroy(&handler->queueCond);
    pthread_mutex_destroy(&handler->queueMutex);
}

static void start_listen(struct nabtoshell_coap_handler* handler)
{
    handler->armedListenSequence = ++handler->listenSequence;
    handler->listenArmedAtMs = monotonic_ms();
    nabto_device_listener_new_coap_request(handler->listener, handler->future,
                                           &handler->request);
    printf("[COAPQ] %s arm seq=%llu requestSlot=%p tid=%lu\n",
           handler->debugName,
           (unsigned long long)handler->armedListenSequence,
           (void*)&handler->request,
           current_tid());
    nabto_device_future_set_callback(handler->future, request_callback, handler);
}

static void request_callback(NabtoDeviceFuture* future, NabtoDeviceError ec,
                             void* userData)
{
    (void)future;
    struct nabtoshell_coap_handler* handler = userData;
    int64_t cbStartMs = monotonic_ms();
    uint64_t callbackSeq = handler->armedListenSequence;
    int64_t callbackAtMs = monotonic_ms();
    int64_t armAgeMs = (handler->listenArmedAtMs > 0 ? callbackAtMs - handler->listenArmedAtMs : -1);
    if (ec != NABTO_DEVICE_EC_OK) {
        printf("[COAPQ] %s callback seq=%llu ec=%d armAge=%lldms tid=%lu\n",
               handler->debugName,
               (unsigned long long)callbackSeq,
               (int)ec,
               (long long)armAgeMs,
               current_tid());
        printf("[COAPQ] %s callback leave seq=%llu ec=%d dur=%lldms tid=%lu\n",
               handler->debugName,
               (unsigned long long)callbackSeq,
               (int)ec,
               (long long)(monotonic_ms() - cbStartMs),
               current_tid());
        return;
    }

    NabtoDeviceConnectionRef ref = 0;
    if (handler->request != NULL) {
        ref = nabto_device_coap_request_get_connection_ref(handler->request);
    }
    printf("[COAPQ] %s callback seq=%llu ec=0 armAge=%lldms req=%p ref=%llu tid=%lu\n",
           handler->debugName,
           (unsigned long long)callbackSeq,
           (long long)armAgeMs,
           (void*)handler->request,
           (unsigned long long)ref,
           current_tid());

    if (!callback_enter(handler)) {
        printf("[COAPQ] %s drop req=%p (stopping) tid=%lu\n", handler->debugName, (void*)handler->request, current_tid());
        if (handler->request != NULL) {
            nabto_device_coap_request_free(handler->request);
        }
        handler->request = NULL;
        printf("[COAPQ] %s callback leave seq=%llu ec=0 drop dur=%lldms tid=%lu\n",
               handler->debugName,
               (unsigned long long)callbackSeq,
               (long long)(monotonic_ms() - cbStartMs),
               current_tid());
        return;
    }

    NabtoDeviceCoapRequest* request = handler->request;
    handler->request = NULL;

    if (!enqueue_request(handler, request)) {
        printf("[COAPQ] %s enqueue failed req=%p tid=%lu\n", handler->debugName, (void*)request, current_tid());
        nabto_device_coap_error_response(request, 503, "Server busy");
        nabto_device_coap_request_free(request);
    }

    pthread_mutex_lock(&handler->queueMutex);
    bool rearm = !handler->stopping;
    pthread_mutex_unlock(&handler->queueMutex);
    if (rearm) {
        start_listen(handler);
    }

    callback_leave(handler);
    printf("[COAPQ] %s callback leave seq=%llu ec=0 dur=%lldms tid=%lu\n",
           handler->debugName,
           (unsigned long long)callbackSeq,
           (long long)(monotonic_ms() - cbStartMs),
           current_tid());
}

static bool callback_enter(struct nabtoshell_coap_handler* handler)
{
    bool ok = false;
    int64_t lockStartMs = monotonic_ms();
    pthread_mutex_lock(&handler->queueMutex);
    int64_t lockWaitMs = monotonic_ms() - lockStartMs;
    if (lockWaitMs > 0) {
        printf("[COAPQ] %s callback_enter lockWait=%lldms tid=%lu\n",
               handler->debugName,
               (long long)lockWaitMs,
               current_tid());
    }
    if (!handler->stopping) {
        handler->activeCallbacks++;
        ok = true;
    }
    pthread_mutex_unlock(&handler->queueMutex);
    return ok;
}

static void callback_leave(struct nabtoshell_coap_handler* handler)
{
    pthread_mutex_lock(&handler->queueMutex);
    if (handler->activeCallbacks > 0) {
        handler->activeCallbacks--;
    }
    if (handler->stopping && handler->activeCallbacks == 0) {
        pthread_cond_broadcast(&handler->callbackCond);
    }
    pthread_mutex_unlock(&handler->queueMutex);
}

static bool enqueue_request(struct nabtoshell_coap_handler* handler,
                            NabtoDeviceCoapRequest* request)
{
    struct nabtoshell_coap_request_node* node =
        (struct nabtoshell_coap_request_node*)calloc(1, sizeof(struct nabtoshell_coap_request_node));
    if (node == NULL) {
        return false;
    }
    node->request = request;

    int64_t lockStartMs = monotonic_ms();
    pthread_mutex_lock(&handler->queueMutex);
    int64_t lockWaitMs = monotonic_ms() - lockStartMs;
    if (lockWaitMs > 0) {
        printf("[COAPQ] %s enqueue lockWait=%lldms req=%p tid=%lu\n",
               handler->debugName,
               (long long)lockWaitMs,
               (void*)request,
               current_tid());
    }
    if (handler->stopping) {
        pthread_mutex_unlock(&handler->queueMutex);
        free(node);
        return false;
    }

    node->requestId = ++handler->nextRequestId;
    node->connectionRef = nabto_device_coap_request_get_connection_ref(request);
    node->enqueuedAtMs = monotonic_ms();
    if (handler->queueTail != NULL) {
        handler->queueTail->next = node;
    } else {
        handler->queueHead = node;
    }
    handler->queueTail = node;
    handler->queuedRequests++;
    printf("[COAPQ] %s enqueue id=%llu req=%p ref=%llu depth=%zu tid=%lu\n",
           handler->debugName,
           (unsigned long long)node->requestId,
           (void*)request,
           (unsigned long long)node->connectionRef,
           handler->queuedRequests,
           current_tid());
    pthread_cond_signal(&handler->queueCond);
    pthread_mutex_unlock(&handler->queueMutex);
    return true;
}

static void* coap_worker_thread(void* userData)
{
    struct nabtoshell_coap_handler* handler = userData;
    while (true) {
        pthread_mutex_lock(&handler->queueMutex);
        while (handler->queueHead == NULL && !handler->stopping) {
            pthread_cond_wait(&handler->queueCond, &handler->queueMutex);
        }
        if (handler->queueHead == NULL && handler->stopping) {
            pthread_mutex_unlock(&handler->queueMutex);
            break;
        }

        struct nabtoshell_coap_request_node* node = handler->queueHead;
        handler->queueHead = node->next;
        if (handler->queueHead == NULL) {
            handler->queueTail = NULL;
        }
        if (handler->queuedRequests > 0) {
            handler->queuedRequests--;
        }
        size_t depthAfterPop = handler->queuedRequests;
        pthread_mutex_unlock(&handler->queueMutex);

        int64_t nowMs = monotonic_ms();
        int64_t queueWaitMs = nowMs - node->enqueuedAtMs;
        printf("[COAPQ] %s start id=%llu req=%p ref=%llu queueWait=%lldms depth=%zu tid=%lu\n",
               handler->debugName,
               (unsigned long long)node->requestId,
               (void*)node->request,
               (unsigned long long)node->connectionRef,
               (long long)queueWaitMs,
               depthAfterPop,
               current_tid());
        int64_t handlerStartMs = monotonic_ms();
        handler->requestHandler(handler, node->request);
        int64_t handlerElapsedMs = monotonic_ms() - handlerStartMs;
        printf("[COAPQ] %s done id=%llu req=%p ref=%llu handlerMs=%lld tid=%lu\n",
               handler->debugName,
               (unsigned long long)node->requestId,
               (void*)node->request,
               (unsigned long long)node->connectionRef,
               (long long)handlerElapsedMs,
               current_tid());
        nabto_device_coap_request_free(node->request);
        free(node);
    }
    return NULL;
}

bool nabtoshell_init_cbor_parser(NabtoDeviceCoapRequest* request,
                                 CborParser* parser, CborValue* cborValue)
{
    uint16_t contentFormat = 0;
    NabtoDeviceError ec = nabto_device_coap_request_get_content_format(
        request, &contentFormat);
    if (ec || contentFormat != NABTO_DEVICE_COAP_CONTENT_FORMAT_APPLICATION_CBOR) {
        nabto_device_coap_error_response(request, 400, "Invalid Content Format");
        return false;
    }
    void* payload = NULL;
    size_t payloadSize = 0;
    if (nabto_device_coap_request_get_payload(request, &payload, &payloadSize) !=
        NABTO_DEVICE_EC_OK) {
        nabto_device_coap_error_response(request, 400, "Missing payload");
        return false;
    }
    cbor_parser_init((const uint8_t*)payload, payloadSize, 0, parser, cborValue);
    return true;
}
