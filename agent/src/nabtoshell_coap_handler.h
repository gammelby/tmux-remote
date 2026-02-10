#ifndef NABTOSHELL_COAP_HANDLER_H_
#define NABTOSHELL_COAP_HANDLER_H_

#include <nabto/nabto_device.h>
#include <tinycbor/cbor.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

struct nabtoshell;
struct nabtoshell_coap_handler;
struct nabtoshell_coap_request_node;

typedef void (*nabtoshell_coap_request_handler)(
    struct nabtoshell_coap_handler* handler,
    NabtoDeviceCoapRequest* request);

struct nabtoshell_coap_handler {
    NabtoDevice* device;
    struct nabtoshell* app;
    NabtoDeviceFuture* future;
    NabtoDeviceListener* listener;
    NabtoDeviceCoapRequest* request;
    nabtoshell_coap_request_handler requestHandler;
    char debugName[64];

    pthread_t workerThread;
    bool workerStarted;

    pthread_mutex_t queueMutex;
    pthread_cond_t queueCond;
    pthread_cond_t callbackCond;
    struct nabtoshell_coap_request_node* queueHead;
    struct nabtoshell_coap_request_node* queueTail;
    size_t queuedRequests;
    uint64_t nextRequestId;
    uint64_t listenSequence;
    uint64_t armedListenSequence;
    int64_t listenArmedAtMs;
    int activeCallbacks;
    bool stopping;
};

NabtoDeviceError nabtoshell_coap_handler_init(
    struct nabtoshell_coap_handler* handler,
    NabtoDevice* device,
    struct nabtoshell* app,
    NabtoDeviceCoapMethod method,
    const char** paths,
    nabtoshell_coap_request_handler requestHandler);

void nabtoshell_coap_handler_stop(struct nabtoshell_coap_handler* handler);
void nabtoshell_coap_handler_deinit(struct nabtoshell_coap_handler* handler);

bool nabtoshell_init_cbor_parser(NabtoDeviceCoapRequest* request,
                                 CborParser* parser, CborValue* cborValue);

/* Per-endpoint init functions */
NabtoDeviceError nabtoshell_coap_resize_init(
    struct nabtoshell_coap_handler* handler, NabtoDevice* device,
    struct nabtoshell* app);

NabtoDeviceError nabtoshell_coap_sessions_init(
    struct nabtoshell_coap_handler* handler, NabtoDevice* device,
    struct nabtoshell* app);

NabtoDeviceError nabtoshell_coap_attach_init(
    struct nabtoshell_coap_handler* handler, NabtoDevice* device,
    struct nabtoshell* app);

NabtoDeviceError nabtoshell_coap_create_init(
    struct nabtoshell_coap_handler* handler, NabtoDevice* device,
    struct nabtoshell* app);

NabtoDeviceError nabtoshell_coap_status_init(
    struct nabtoshell_coap_handler* handler, NabtoDevice* device,
    struct nabtoshell* app);

#endif
