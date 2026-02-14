#ifndef TMUXREMOTE_COAP_HANDLER_H_
#define TMUXREMOTE_COAP_HANDLER_H_

#include <nabto/nabto_device.h>
#include <tinycbor/cbor.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

struct tmuxremote;
struct tmuxremote_coap_handler;
struct tmuxremote_coap_request_node;

typedef void (*tmuxremote_coap_request_handler)(
    struct tmuxremote_coap_handler* handler,
    NabtoDeviceCoapRequest* request);

struct tmuxremote_coap_handler {
    NabtoDevice* device;
    struct tmuxremote* app;
    NabtoDeviceFuture* future;
    NabtoDeviceListener* listener;
    NabtoDeviceCoapRequest* request;
    tmuxremote_coap_request_handler requestHandler;

    pthread_t workerThread;
    bool workerStarted;

    pthread_mutex_t queueMutex;
    pthread_cond_t queueCond;
    pthread_cond_t callbackCond;
    struct tmuxremote_coap_request_node* queueHead;
    struct tmuxremote_coap_request_node* queueTail;
    int activeCallbacks;
    bool stopping;
};

NabtoDeviceError tmuxremote_coap_handler_init(
    struct tmuxremote_coap_handler* handler,
    NabtoDevice* device,
    struct tmuxremote* app,
    NabtoDeviceCoapMethod method,
    const char** paths,
    tmuxremote_coap_request_handler requestHandler);

void tmuxremote_coap_handler_stop(struct tmuxremote_coap_handler* handler);
void tmuxremote_coap_handler_deinit(struct tmuxremote_coap_handler* handler);

bool tmuxremote_init_cbor_parser(NabtoDeviceCoapRequest* request,
                                 CborParser* parser, CborValue* cborValue);

/* Per-endpoint init functions */
NabtoDeviceError tmuxremote_coap_resize_init(
    struct tmuxremote_coap_handler* handler, NabtoDevice* device,
    struct tmuxremote* app);

NabtoDeviceError tmuxremote_coap_sessions_init(
    struct tmuxremote_coap_handler* handler, NabtoDevice* device,
    struct tmuxremote* app);

NabtoDeviceError tmuxremote_coap_attach_init(
    struct tmuxremote_coap_handler* handler, NabtoDevice* device,
    struct tmuxremote* app);

NabtoDeviceError tmuxremote_coap_create_init(
    struct tmuxremote_coap_handler* handler, NabtoDevice* device,
    struct tmuxremote* app);

NabtoDeviceError tmuxremote_coap_status_init(
    struct tmuxremote_coap_handler* handler, NabtoDevice* device,
    struct tmuxremote* app);

#endif
