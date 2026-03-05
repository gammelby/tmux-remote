#include "tmuxremote_coap_handler.h"
#include "tmuxremote.h"
#include "tmuxremote_tmux.h"

#include <modules/iam/nm_iam.h>
#include <modules/iam/nm_iam_state.h>
#include <tinycbor/cbor.h>

#include <string.h>
#include <time.h>

static void handle_request(struct tmuxremote_coap_handler* handler,
                           NabtoDeviceCoapRequest* request);

NabtoDeviceError tmuxremote_coap_status_init(
    struct tmuxremote_coap_handler* handler, NabtoDevice* device,
    struct tmuxremote* app)
{
    const char* paths[] = {"terminal", "status", NULL};
    return tmuxremote_coap_handler_init(handler, device, app,
                                        NABTO_DEVICE_COAP_GET, paths,
                                        &handle_request);
}

static void handle_request(struct tmuxremote_coap_handler* handler,
                           NabtoDeviceCoapRequest* request)
{
    struct tmuxremote* app = handler->app;

    if (!tmuxremote_iam_check_access(&app->iam, request, "Terminal:Status")) {
        nabto_device_coap_error_response(request, 403, "Access denied");
        return;
    }

    struct tmuxremote_tmux_list list;
    memset(&list, 0, sizeof(list));
    tmuxremote_tmux_list_sessions(&list);

    time_t now = time(NULL);
    uint64_t uptime = (uint64_t)(now - app->startTime);

    /* Get friendly name from IAM state */
    const char* friendlyName = "tmux-remote";
    struct nm_iam_state* state = nm_iam_dump_state(&app->iam.iam);
    if (state != NULL && state->friendlyName != NULL) {
        friendlyName = state->friendlyName;
    }

    uint8_t cborBuf[512];
    CborEncoder encoder;
    cbor_encoder_init(&encoder, cborBuf, sizeof(cborBuf), 0);

    CborEncoder mapEncoder;
    cbor_encoder_create_map(&encoder, &mapEncoder, 4);

    cbor_encode_text_stringz(&mapEncoder, "version");
    cbor_encode_text_stringz(&mapEncoder, TMUXREMOTE_VERSION);

    cbor_encode_text_stringz(&mapEncoder, "active_sessions");
    cbor_encode_uint(&mapEncoder, (uint64_t)list.count);

    cbor_encode_text_stringz(&mapEncoder, "uptime_seconds");
    cbor_encode_uint(&mapEncoder, uptime);

    cbor_encode_text_stringz(&mapEncoder, "friendly_name");
    cbor_encode_text_stringz(&mapEncoder, friendlyName);

    CborError err = cbor_encoder_close_container(&encoder, &mapEncoder);
    if (err != CborNoError || cbor_encoder_get_extra_bytes_needed(&encoder) > 0) {
        if (state != NULL) { nm_iam_state_free(state); }
        nabto_device_coap_error_response(request, 500, "Failed to encode status");
        return;
    }

    size_t cborLen = cbor_encoder_get_buffer_size(&encoder, cborBuf);

    nabto_device_coap_response_set_code(request, 205);
    nabto_device_coap_response_set_content_format(
        request, NABTO_DEVICE_COAP_CONTENT_FORMAT_APPLICATION_CBOR);
    nabto_device_coap_response_set_payload(request, cborBuf, cborLen);
    nabto_device_coap_response_ready(request);

    if (state != NULL) { nm_iam_state_free(state); }
}
