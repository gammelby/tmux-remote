#include "tmuxremote_coap_handler.h"
#include "tmuxremote.h"
#include "tmuxremote_stream.h"

#include <tinycbor/cbor.h>

#include <sys/ioctl.h>
#include <signal.h>
#include <string.h>

#define TMUXREMOTE_MAX_TERM_COLS 1000
#define TMUXREMOTE_MAX_TERM_ROWS 1000

static bool valid_terminal_size(uint64_t cols, uint64_t rows)
{
    return cols >= 1 && cols <= TMUXREMOTE_MAX_TERM_COLS &&
           rows >= 1 && rows <= TMUXREMOTE_MAX_TERM_ROWS;
}

static void handle_request(struct tmuxremote_coap_handler* handler,
                           NabtoDeviceCoapRequest* request);

NabtoDeviceError tmuxremote_coap_resize_init(
    struct tmuxremote_coap_handler* handler, NabtoDevice* device,
    struct tmuxremote* app)
{
    const char* paths[] = {"terminal", "resize", NULL};
    return tmuxremote_coap_handler_init(handler, device, app,
                                        NABTO_DEVICE_COAP_POST, paths,
                                        &handle_request);
}

static void handle_request(struct tmuxremote_coap_handler* handler,
                           NabtoDeviceCoapRequest* request)
{
    struct tmuxremote* app = handler->app;

    if (!tmuxremote_iam_check_access(&app->iam, request, "Terminal:Resize")) {
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
    if (cbor_value_enter_container(&value, &map) != CborNoError) {
        nabto_device_coap_error_response(request, 400, "Invalid CBOR payload");
        return;
    }

    uint64_t cols = 0, rows = 0;
    bool hasCols = false, hasRows = false;

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

        if (strcmp(keyBuf, "cols") == 0 && cbor_value_is_unsigned_integer(&map)) {
            if (cbor_value_get_uint64(&map, &cols) != CborNoError) {
                nabto_device_coap_error_response(request, 400, "Invalid cols");
                return;
            }
            hasCols = true;
        } else if (strcmp(keyBuf, "rows") == 0 && cbor_value_is_unsigned_integer(&map)) {
            if (cbor_value_get_uint64(&map, &rows) != CborNoError) {
                nabto_device_coap_error_response(request, 400, "Invalid rows");
                return;
            }
            hasRows = true;
        }
        if (cbor_value_advance(&map) != CborNoError) {
            nabto_device_coap_error_response(request, 400, "Invalid CBOR payload");
            return;
        }
    }

    if (!hasCols || !hasRows) {
        nabto_device_coap_error_response(request, 400, "Missing cols or rows");
        return;
    }

    if (!valid_terminal_size(cols, rows)) {
        nabto_device_coap_error_response(request, 400, "Invalid terminal size");
        return;
    }

    /* Find the PTY fd for this connection and resize it */
    NabtoDeviceConnectionRef ref = nabto_device_coap_request_get_connection_ref(request);
    int ptyFd = tmuxremote_stream_get_pty_fd(&app->streamListener, ref);
    if (ptyFd >= 0) {
        struct winsize ws;
        ws.ws_col = (unsigned short)cols;
        ws.ws_row = (unsigned short)rows;
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;
        ioctl(ptyFd, TIOCSWINSZ, &ws);

        /* Also update session map dimensions */
        tmuxremote_session_update_size(&app->sessionMap, ref, (uint16_t)cols, (uint16_t)rows);
    }

    tmuxremote_stream_resize_prompt_detector_for_ref(&app->streamListener,
                                                     ref,
                                                     (int)cols,
                                                     (int)rows);

    nabto_device_coap_response_set_code(request, 204);
    nabto_device_coap_response_ready(request);
}
