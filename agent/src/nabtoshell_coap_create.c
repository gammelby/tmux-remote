#include "nabtoshell_coap_handler.h"
#include "nabtoshell.h"
#include "nabtoshell_control_stream.h"
#include "nabtoshell_tmux.h"

#include <tinycbor/cbor.h>

#include <string.h>

#define NABTOSHELL_MAX_TERM_COLS 1000
#define NABTOSHELL_MAX_TERM_ROWS 1000

static bool valid_terminal_size(uint64_t cols, uint64_t rows)
{
    return cols >= 1 && cols <= NABTOSHELL_MAX_TERM_COLS &&
           rows >= 1 && rows <= NABTOSHELL_MAX_TERM_ROWS;
}

static void handle_request(struct nabtoshell_coap_handler* handler,
                           NabtoDeviceCoapRequest* request);

NabtoDeviceError nabtoshell_coap_create_init(
    struct nabtoshell_coap_handler* handler, NabtoDevice* device,
    struct nabtoshell* app)
{
    const char* paths[] = {"terminal", "create", NULL};
    return nabtoshell_coap_handler_init(handler, device, app,
                                        NABTO_DEVICE_COAP_POST, paths,
                                        &handle_request);
}

static void handle_request(struct nabtoshell_coap_handler* handler,
                           NabtoDeviceCoapRequest* request)
{
    struct nabtoshell* app = handler->app;

    if (!nabtoshell_iam_check_access(&app->iam, request, "Terminal:CreateSession")) {
        nabto_device_coap_error_response(request, 403, "Access denied");
        return;
    }

    CborParser parser;
    CborValue value;
    if (!nabtoshell_init_cbor_parser(request, &parser, &value)) {
        return;
    }

    if (!cbor_value_is_map(&value)) {
        nabto_device_coap_error_response(request, 400, "Expected map");
        return;
    }

    CborValue map;
    cbor_value_enter_container(&value, &map);

    char sessionName[NABTOSHELL_SESSION_NAME_MAX] = {0};
    char command[256] = {0};
    uint64_t cols = 80, rows = 24;

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
        } else if (strcmp(keyBuf, "command") == 0 && cbor_value_is_text_string(&map)) {
            size_t cLen = sizeof(command) - 1;
            if (cbor_value_copy_text_string(&map, command, &cLen, NULL) != CborNoError) {
                nabto_device_coap_error_response(request, 400, "Invalid or oversized command");
                return;
            }
            command[cLen] = '\0';
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

    if (!nabtoshell_tmux_validate_session_name(sessionName)) {
        nabto_device_coap_error_response(request, 400, "Invalid session name");
        return;
    }

    const char* cmd = strlen(command) > 0 ? command : NULL;

    if (!nabtoshell_tmux_create_session(sessionName, (uint16_t)cols,
                                        (uint16_t)rows, cmd)) {
        nabto_device_coap_error_response(request, 500, "Failed to create session");
        return;
    }

    nabtoshell_control_stream_notify(&app->controlStreamListener);

    nabto_device_coap_response_set_code(request, 201);
    nabto_device_coap_response_ready(request);
}
