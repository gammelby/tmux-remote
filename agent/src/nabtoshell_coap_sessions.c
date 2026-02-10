#include "nabtoshell_coap_handler.h"
#include "nabtoshell.h"
#include "nabtoshell_tmux.h"

#include <tinycbor/cbor.h>

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((int64_t)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

static void handle_request(struct nabtoshell_coap_handler* handler,
                           NabtoDeviceCoapRequest* request);

NabtoDeviceError nabtoshell_coap_sessions_init(
    struct nabtoshell_coap_handler* handler, NabtoDevice* device,
    struct nabtoshell* app)
{
    const char* paths[] = {"terminal", "sessions", NULL};
    return nabtoshell_coap_handler_init(handler, device, app,
                                        NABTO_DEVICE_COAP_GET, paths,
                                        &handle_request);
}

static void handle_request(struct nabtoshell_coap_handler* handler,
                           NabtoDeviceCoapRequest* request)
{
    int64_t requestStartMs = monotonic_ms();
    NabtoDeviceConnectionRef ref = nabto_device_coap_request_get_connection_ref(request);
    printf("[SESS] handle_request enter req=%p ref=%llu\n",
           (void*)request, (unsigned long long)ref);
    struct nabtoshell* app = handler->app;

    int64_t iamStartMs = monotonic_ms();
    bool allowed = nabtoshell_iam_check_access(&app->iam, request, "Terminal:ListSessions");
    int64_t iamElapsedMs = monotonic_ms() - iamStartMs;
    printf("[SESS] IAM check req=%p ref=%llu allowed=%d iamMs=%lld\n",
           (void*)request,
           (unsigned long long)ref,
           allowed ? 1 : 0,
           (long long)iamElapsedMs);
    if (!allowed) {
        nabto_device_coap_error_response(request, 403, "Access denied");
        printf("[SESS] response 403 req=%p ref=%llu totalMs=%lld\n",
               (void*)request,
               (unsigned long long)ref,
               (long long)(monotonic_ms() - requestStartMs));
        return;
    }

    int64_t tmuxStartMs = monotonic_ms();
    struct nabtoshell_tmux_list list;
    memset(&list, 0, sizeof(list));
    nabtoshell_tmux_list_sessions(&list);
    int64_t tmuxElapsedMs = monotonic_ms() - tmuxStartMs;
    printf("[SESS] tmux list req=%p ref=%llu sessions=%d tmuxMs=%lld\n",
           (void*)request,
           (unsigned long long)ref,
           list.count,
           (long long)tmuxElapsedMs);

    /* Encode as CBOR array */
    uint8_t cborBuf[2048];
    CborEncoder encoder;
    cbor_encoder_init(&encoder, cborBuf, sizeof(cborBuf), 0);

    CborEncoder arrayEncoder;
    cbor_encoder_create_array(&encoder, &arrayEncoder, list.count);

    for (int i = 0; i < list.count; i++) {
        CborEncoder mapEncoder;
        cbor_encoder_create_map(&arrayEncoder, &mapEncoder, 4);

        cbor_encode_text_stringz(&mapEncoder, "name");
        cbor_encode_text_stringz(&mapEncoder, list.sessions[i].name);

        cbor_encode_text_stringz(&mapEncoder, "cols");
        cbor_encode_uint(&mapEncoder, list.sessions[i].cols);

        cbor_encode_text_stringz(&mapEncoder, "rows");
        cbor_encode_uint(&mapEncoder, list.sessions[i].rows);

        cbor_encode_text_stringz(&mapEncoder, "attached");
        cbor_encode_uint(&mapEncoder, list.sessions[i].attached);

        cbor_encoder_close_container(&arrayEncoder, &mapEncoder);
    }

    cbor_encoder_close_container(&encoder, &arrayEncoder);

    size_t cborLen = cbor_encoder_get_buffer_size(&encoder, cborBuf);

    nabto_device_coap_response_set_code(request, 205);
    nabto_device_coap_response_set_content_format(
        request, NABTO_DEVICE_COAP_CONTENT_FORMAT_APPLICATION_CBOR);
    nabto_device_coap_response_set_payload(request, cborBuf, cborLen);
    nabto_device_coap_response_ready(request);
    printf("[SESS] response 205 req=%p ref=%llu cborLen=%zu totalMs=%lld\n",
           (void*)request,
           (unsigned long long)ref,
           cborLen,
           (long long)(monotonic_ms() - requestStartMs));
}
