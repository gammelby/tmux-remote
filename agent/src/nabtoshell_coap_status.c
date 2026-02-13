#include "nabtoshell_coap_handler.h"
#include "nabtoshell.h"
#include "nabtoshell_tmux.h"

#include <tinycbor/cbor.h>

#include <string.h>
#include <time.h>

static void handle_request(struct nabtoshell_coap_handler* handler,
                           NabtoDeviceCoapRequest* request);

NabtoDeviceError nabtoshell_coap_status_init(
    struct nabtoshell_coap_handler* handler, NabtoDevice* device,
    struct nabtoshell* app)
{
    const char* paths[] = {"terminal", "status", NULL};
    return nabtoshell_coap_handler_init(handler, device, app,
                                        NABTO_DEVICE_COAP_GET, paths,
                                        &handle_request);
}

static void handle_request(struct nabtoshell_coap_handler* handler,
                           NabtoDeviceCoapRequest* request)
{
    struct nabtoshell* app = handler->app;

    if (!nabtoshell_iam_check_access(&app->iam, request, "Terminal:Status")) {
        nabto_device_coap_error_response(request, 403, "Access denied");
        return;
    }

    struct nabtoshell_tmux_list list;
    memset(&list, 0, sizeof(list));
    nabtoshell_tmux_list_sessions(&list);

    time_t now = time(NULL);
    uint64_t uptime = (uint64_t)(now - app->startTime);

    uint8_t cborBuf[256];
    CborEncoder encoder;
    cbor_encoder_init(&encoder, cborBuf, sizeof(cborBuf), 0);

    CborEncoder mapEncoder;
    cbor_encoder_create_map(&encoder, &mapEncoder, 3);

    cbor_encode_text_stringz(&mapEncoder, "version");
    cbor_encode_text_stringz(&mapEncoder, NABTOSHELL_VERSION);

    cbor_encode_text_stringz(&mapEncoder, "active_sessions");
    cbor_encode_uint(&mapEncoder, (uint64_t)list.count);

    cbor_encode_text_stringz(&mapEncoder, "uptime_seconds");
    cbor_encode_uint(&mapEncoder, uptime);

    CborError err = cbor_encoder_close_container(&encoder, &mapEncoder);
    if (err != CborNoError || cbor_encoder_get_extra_bytes_needed(&encoder) > 0) {
        nabto_device_coap_error_response(request, 500, "Failed to encode status");
        return;
    }

    size_t cborLen = cbor_encoder_get_buffer_size(&encoder, cborBuf);

    nabto_device_coap_response_set_code(request, 205);
    nabto_device_coap_response_set_content_format(
        request, NABTO_DEVICE_COAP_CONTENT_FORMAT_APPLICATION_CBOR);
    nabto_device_coap_response_set_payload(request, cborBuf, cborLen);
    nabto_device_coap_response_ready(request);
}
