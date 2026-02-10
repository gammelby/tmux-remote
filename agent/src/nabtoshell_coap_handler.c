#include "nabtoshell_coap_handler.h"

#include <stdlib.h>
#include <string.h>

struct nabtoshell_coap_request_node {
    NabtoDeviceCoapRequest* request;
    struct nabtoshell_coap_request_node* next;
};

static void start_listen(struct nabtoshell_coap_handler* handler);
static void request_callback(NabtoDeviceFuture* future, NabtoDeviceError ec,
                             void* userData);
static bool callback_enter(struct nabtoshell_coap_handler* handler);
static void callback_leave(struct nabtoshell_coap_handler* handler);
static bool enqueue_request(struct nabtoshell_coap_handler* handler,
                            NabtoDeviceCoapRequest* request);
static void* coap_worker_thread(void* userData);

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
    nabto_device_listener_new_coap_request(handler->listener, handler->future,
                                           &handler->request);
    nabto_device_future_set_callback(handler->future, request_callback, handler);
}

static void request_callback(NabtoDeviceFuture* future, NabtoDeviceError ec,
                             void* userData)
{
    (void)future;
    struct nabtoshell_coap_handler* handler = userData;

    if (ec != NABTO_DEVICE_EC_OK) {
        return;
    }

    if (!callback_enter(handler)) {
        if (handler->request != NULL) {
            nabto_device_coap_request_free(handler->request);
        }
        handler->request = NULL;
        return;
    }

    NabtoDeviceCoapRequest* request = handler->request;
    handler->request = NULL;

    if (!enqueue_request(handler, request)) {
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
}

static bool callback_enter(struct nabtoshell_coap_handler* handler)
{
    bool ok = false;
    pthread_mutex_lock(&handler->queueMutex);
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

    pthread_mutex_lock(&handler->queueMutex);
    if (handler->stopping) {
        pthread_mutex_unlock(&handler->queueMutex);
        free(node);
        return false;
    }

    if (handler->queueTail != NULL) {
        handler->queueTail->next = node;
    } else {
        handler->queueHead = node;
    }
    handler->queueTail = node;
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
        pthread_mutex_unlock(&handler->queueMutex);

        handler->requestHandler(handler, node->request);
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
