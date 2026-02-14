#include "tmuxremote_coap_handler.h"
#include "tmuxremote.h"
#include "tmuxremote_control_stream.h"
#include "tmuxremote_tmux.h"

#include <tinycbor/cbor.h>

#include <string.h>
#include <stdio.h>

#define TMUXREMOTE_MAX_TERM_COLS 1000
#define TMUXREMOTE_MAX_TERM_ROWS 1000

static bool valid_terminal_size(uint64_t cols, uint64_t rows)
{
    return cols >= 1 && cols <= TMUXREMOTE_MAX_TERM_COLS &&
           rows >= 1 && rows <= TMUXREMOTE_MAX_TERM_ROWS;
}

static void handle_request(struct tmuxremote_coap_handler* handler,
                           NabtoDeviceCoapRequest* request);

NabtoDeviceError tmuxremote_coap_attach_init(
    struct tmuxremote_coap_handler* handler, NabtoDevice* device,
    struct tmuxremote* app)
{
    const char* paths[] = {"terminal", "attach", NULL};
    return tmuxremote_coap_handler_init(handler, device, app,
                                        NABTO_DEVICE_COAP_POST, paths,
                                        &handle_request);
}

static void handle_request(struct tmuxremote_coap_handler* handler,
                           NabtoDeviceCoapRequest* request)
{
    struct tmuxremote* app = handler->app;

    if (!tmuxremote_iam_check_access(&app->iam, request, "Terminal:Connect")) {
        nabto_device_coap_error_response(request, 403, "Access denied");
        return;
    }

    CborParser parser;
    CborValue value;
    if (!tmuxremote_init_cbor_parser(request, &parser, &value)) {
        return;
    }

    if (!cbor_value_is_map(&value)) {
        nabto_device_coap_error_response(request, 400, "Expected map");
        return;
    }

    CborValue map;
    cbor_value_enter_container(&value, &map);

    char sessionName[TMUXREMOTE_SESSION_NAME_MAX] = {0};
    uint64_t cols = 80, rows = 24;
    bool createIfMissing = false;

    while (!cbor_value_at_end(&map)) {
        if (!cbor_value_is_text_string(&map)) {
            if (cbor_value_advance(&map) != CborNoError ||
                cbor_value_at_end(&map) ||
                cbor_value_advance(&map) != CborNoError) {
                nabto_device_coap_error_response(request, 400, "Invalid CBOR payload");
                return;
            }
            continue;
        }
        char keyBuf[32];
        size_t keyLen = sizeof(keyBuf) - 1;
        if (cbor_value_copy_text_string(&map, keyBuf, &keyLen, NULL) != CborNoError) {
            nabto_device_coap_error_response(request, 400, "Invalid or oversized key");
            return;
        }
        keyBuf[keyLen] = '\0';
        if (cbor_value_advance(&map) != CborNoError) {
            nabto_device_coap_error_response(request, 400, "Invalid CBOR payload");
            return;
        }

        if (strcmp(keyBuf, "session") == 0 && cbor_value_is_text_string(&map)) {
            size_t sLen = sizeof(sessionName) - 1;
            if (cbor_value_copy_text_string(&map, sessionName, &sLen, NULL) != CborNoError) {
                nabto_device_coap_error_response(request, 400, "Invalid or oversized session");
                return;
            }
            sessionName[sLen] = '\0';
        } else if (strcmp(keyBuf, "cols") == 0 && cbor_value_is_unsigned_integer(&map)) {
            if (cbor_value_get_uint64(&map, &cols) != CborNoError) {
                nabto_device_coap_error_response(request, 400, "Invalid cols");
                return;
            }
        } else if (strcmp(keyBuf, "rows") == 0 && cbor_value_is_unsigned_integer(&map)) {
            if (cbor_value_get_uint64(&map, &rows) != CborNoError) {
                nabto_device_coap_error_response(request, 400, "Invalid rows");
                return;
            }
        } else if (strcmp(keyBuf, "create") == 0 && cbor_value_is_boolean(&map)) {
            if (cbor_value_get_boolean(&map, &createIfMissing) != CborNoError) {
                nabto_device_coap_error_response(request, 400, "Invalid create flag");
                return;
            }
        }
        if (cbor_value_advance(&map) != CborNoError) {
            nabto_device_coap_error_response(request, 400, "Invalid CBOR payload");
            return;
        }
    }

    if (strlen(sessionName) == 0) {
        nabto_device_coap_error_response(request, 400, "Missing session name");
        return;
    }

    if (!valid_terminal_size(cols, rows)) {
        nabto_device_coap_error_response(request, 400, "Invalid terminal size");
        return;
    }

    if (!tmuxremote_tmux_validate_session_name(sessionName)) {
        nabto_device_coap_error_response(request, 400, "Invalid session name");
        return;
    }

    /* Verify the session exists (or create it if requested) */
    if (!tmuxremote_tmux_session_exists(sessionName)) {
        if (createIfMissing) {
            if (!tmuxremote_tmux_create_session(sessionName, (uint16_t)cols,
                                                (uint16_t)rows, NULL)) {
                nabto_device_coap_error_response(request, 500, "Failed to create session");
                return;
            }
        } else {
            nabto_device_coap_error_response(request, 404, "Session not found");
            return;
        }
    }

    /* Store the session target for this connection */
    NabtoDeviceConnectionRef ref =
        nabto_device_coap_request_get_connection_ref(request);
    if (!tmuxremote_session_set(&app->sessionMap, ref, sessionName,
                                (uint16_t)cols, (uint16_t)rows)) {
        nabto_device_coap_error_response(request, 500, "Session map full");
        return;
    }

    tmuxremote_control_stream_notify(&app->controlStreamListener);

    nabto_device_coap_response_set_code(request, 201);
    nabto_device_coap_response_ready(request);
}
