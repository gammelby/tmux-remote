#include "nabtoshell_coap_handler.h"
#include "nabtoshell.h"
#include "nabtoshell_tmux.h"

#include <tinycbor/cbor.h>

#include <string.h>

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
    struct nabtoshell* app = handler->app;

    bool allowed = nabtoshell_iam_check_access(&app->iam, request, "Terminal:ListSessions");
    if (!allowed) {
        nabto_device_coap_error_response(request, 403, "Access denied");
        return;
    }

    struct nabtoshell_tmux_list list;
    memset(&list, 0, sizeof(list));
    nabtoshell_tmux_list_sessions(&list);

    /* Encode as CBOR array */
    uint8_t cborBuf[8192];
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

    CborError err = cbor_encoder_close_container(&encoder, &arrayEncoder);
    if (err != CborNoError || cbor_encoder_get_extra_bytes_needed(&encoder) > 0) {
        nabto_device_coap_error_response(request, 500, "Failed to encode sessions");
        return;
    }

    size_t cborLen = cbor_encoder_get_buffer_size(&encoder, cborBuf);

    nabto_device_coap_response_set_code(request, 205);
    nabto_device_coap_response_set_content_format(
        request, NABTO_DEVICE_COAP_CONTENT_FORMAT_APPLICATION_CBOR);
    nabto_device_coap_response_set_payload(request, cborBuf, cborLen);
    nabto_device_coap_response_ready(request);
}
