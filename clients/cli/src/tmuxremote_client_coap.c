#include "tmuxremote_client_coap.h"

#include <nabto/nabto_client.h>
#include <cbor.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

bool tmuxremote_coap_attach(NabtoClientConnection* conn, NabtoClient* client,
                            const char* session, uint16_t cols, uint16_t rows,
                            bool create)
{
    NabtoClientCoap* coap = nabto_client_coap_new(conn, "POST", "/terminal/attach");
    if (coap == NULL) {
        return false;
    }

    /* Encode CBOR payload */
    uint8_t cborBuf[256];
    CborEncoder encoder;
    cbor_encoder_init(&encoder, cborBuf, sizeof(cborBuf), 0);

    CborEncoder map;
    cbor_encoder_create_map(&encoder, &map, create ? 4 : 3);

    cbor_encode_text_stringz(&map, "session");
    cbor_encode_text_stringz(&map, session);

    cbor_encode_text_stringz(&map, "cols");
    cbor_encode_uint(&map, cols);

    cbor_encode_text_stringz(&map, "rows");
    cbor_encode_uint(&map, rows);

    if (create) {
        cbor_encode_text_stringz(&map, "create");
        cbor_encode_boolean(&map, true);
    }

    cbor_encoder_close_container(&encoder, &map);
    size_t cborLen = cbor_encoder_get_buffer_size(&encoder, cborBuf);

    nabto_client_coap_set_request_payload(coap,
        NABTO_CLIENT_COAP_CONTENT_FORMAT_APPLICATION_CBOR, cborBuf, cborLen);

    NabtoClientFuture* future = nabto_client_future_new(client);
    nabto_client_coap_execute(coap, future);
    NabtoClientError ec = nabto_client_future_wait(future);
    nabto_client_future_free(future);

    if (ec != NABTO_CLIENT_EC_OK) {
        printf("Attach CoAP request failed: %s\n",
               nabto_client_error_get_message(ec));
        nabto_client_coap_free(coap);
        return false;
    }

    uint16_t statusCode = 0;
    nabto_client_coap_get_response_status_code(coap, &statusCode);
    nabto_client_coap_free(coap);

    if (statusCode != 201) {
        printf("Attach failed with status %u\n", statusCode);
        return false;
    }

    return true;
}

bool tmuxremote_coap_create_session(NabtoClientConnection* conn,
                                    NabtoClient* client,
                                    const char* session, uint16_t cols,
                                    uint16_t rows, const char* command)
{
    NabtoClientCoap* coap = nabto_client_coap_new(conn, "POST", "/terminal/create");
    if (coap == NULL) {
        return false;
    }

    uint8_t cborBuf[512];
    CborEncoder encoder;
    cbor_encoder_init(&encoder, cborBuf, sizeof(cborBuf), 0);

    int mapSize = 3;
    if (command != NULL) mapSize = 4;

    CborEncoder map;
    cbor_encoder_create_map(&encoder, &map, mapSize);

    cbor_encode_text_stringz(&map, "session");
    cbor_encode_text_stringz(&map, session);

    cbor_encode_text_stringz(&map, "cols");
    cbor_encode_uint(&map, cols);

    cbor_encode_text_stringz(&map, "rows");
    cbor_encode_uint(&map, rows);

    if (command != NULL) {
        cbor_encode_text_stringz(&map, "command");
        cbor_encode_text_stringz(&map, command);
    }

    cbor_encoder_close_container(&encoder, &map);
    size_t cborLen = cbor_encoder_get_buffer_size(&encoder, cborBuf);

    nabto_client_coap_set_request_payload(coap,
        NABTO_CLIENT_COAP_CONTENT_FORMAT_APPLICATION_CBOR, cborBuf, cborLen);

    NabtoClientFuture* future = nabto_client_future_new(client);
    nabto_client_coap_execute(coap, future);
    NabtoClientError ec = nabto_client_future_wait(future);
    nabto_client_future_free(future);

    if (ec != NABTO_CLIENT_EC_OK) {
        printf("Create session CoAP request failed: %s\n",
               nabto_client_error_get_message(ec));
        nabto_client_coap_free(coap);
        return false;
    }

    uint16_t statusCode = 0;
    nabto_client_coap_get_response_status_code(coap, &statusCode);
    nabto_client_coap_free(coap);

    if (statusCode != 201) {
        printf("Create session failed with status %u\n", statusCode);
        return false;
    }

    return true;
}

bool tmuxremote_coap_resize(NabtoClientConnection* conn, NabtoClient* client,
                            uint16_t cols, uint16_t rows)
{
    NabtoClientCoap* coap = nabto_client_coap_new(conn, "POST", "/terminal/resize");
    if (coap == NULL) {
        return false;
    }

    uint8_t cborBuf[64];
    CborEncoder encoder;
    cbor_encoder_init(&encoder, cborBuf, sizeof(cborBuf), 0);

    CborEncoder map;
    cbor_encoder_create_map(&encoder, &map, 2);

    cbor_encode_text_stringz(&map, "cols");
    cbor_encode_uint(&map, cols);

    cbor_encode_text_stringz(&map, "rows");
    cbor_encode_uint(&map, rows);

    cbor_encoder_close_container(&encoder, &map);
    size_t cborLen = cbor_encoder_get_buffer_size(&encoder, cborBuf);

    nabto_client_coap_set_request_payload(coap,
        NABTO_CLIENT_COAP_CONTENT_FORMAT_APPLICATION_CBOR, cborBuf, cborLen);

    NabtoClientFuture* future = nabto_client_future_new(client);
    nabto_client_coap_execute(coap, future);
    NabtoClientError ec = nabto_client_future_wait(future);
    nabto_client_future_free(future);

    nabto_client_coap_free(coap);

    return ec == NABTO_CLIENT_EC_OK;
}

bool tmuxremote_coap_pair_password_invite(NabtoClientConnection* conn,
                                          NabtoClient* client,
                                          const char* username)
{
    NabtoClientCoap* coap = nabto_client_coap_new(conn, "POST",
                                                   "/iam/pairing/password-invite");
    if (coap == NULL) {
        return false;
    }

    uint8_t cborBuf[128];
    CborEncoder encoder;
    cbor_encoder_init(&encoder, cborBuf, sizeof(cborBuf), 0);

    CborEncoder map;
    cbor_encoder_create_map(&encoder, &map, 1);

    cbor_encode_text_stringz(&map, "Username");
    cbor_encode_text_stringz(&map, username);

    cbor_encoder_close_container(&encoder, &map);
    size_t cborLen = cbor_encoder_get_buffer_size(&encoder, cborBuf);

    nabto_client_coap_set_request_payload(coap,
        NABTO_CLIENT_COAP_CONTENT_FORMAT_APPLICATION_CBOR, cborBuf, cborLen);

    NabtoClientFuture* future = nabto_client_future_new(client);
    nabto_client_coap_execute(coap, future);
    NabtoClientError ec = nabto_client_future_wait(future);
    nabto_client_future_free(future);

    if (ec != NABTO_CLIENT_EC_OK) {
        printf("Pairing CoAP request failed: %s\n",
               nabto_client_error_get_message(ec));
        nabto_client_coap_free(coap);
        return false;
    }

    uint16_t statusCode = 0;
    nabto_client_coap_get_response_status_code(coap, &statusCode);
    nabto_client_coap_free(coap);

    if (statusCode < 200 || statusCode >= 300) {
        printf("Pairing failed with status %u\n", statusCode);
        return false;
    }

    return true;
}

bool tmuxremote_coap_list_sessions(NabtoClientConnection* conn,
                                   NabtoClient* client,
                                   struct tmuxremote_client_sessions_list* list)
{
    memset(list, 0, sizeof(struct tmuxremote_client_sessions_list));

    NabtoClientCoap* coap = nabto_client_coap_new(conn, "GET", "/terminal/sessions");
    if (coap == NULL) {
        return false;
    }

    NabtoClientFuture* future = nabto_client_future_new(client);
    nabto_client_coap_execute(coap, future);
    NabtoClientError ec = nabto_client_future_wait(future);
    nabto_client_future_free(future);

    if (ec != NABTO_CLIENT_EC_OK) {
        nabto_client_coap_free(coap);
        return false;
    }

    uint16_t statusCode = 0;
    nabto_client_coap_get_response_status_code(coap, &statusCode);

    if (statusCode != 205) {
        nabto_client_coap_free(coap);
        return false;
    }

    void* payload = NULL;
    size_t payloadLen = 0;
    nabto_client_coap_get_response_payload(coap, &payload, &payloadLen);

    if (payload == NULL || payloadLen == 0) {
        nabto_client_coap_free(coap);
        return true;
    }

    /* Parse CBOR array */
    bool parseOk = true;
    CborParser parser;
    CborValue value;
    if (cbor_parser_init((const uint8_t*)payload, payloadLen, 0, &parser, &value) != CborNoError) {
        nabto_client_coap_free(coap);
        return false;
    }

    if (!cbor_value_is_array(&value)) {
        nabto_client_coap_free(coap);
        return true;
    }

    CborValue arr;
    if (cbor_value_enter_container(&value, &arr) != CborNoError) {
        nabto_client_coap_free(coap);
        return false;
    }

    while (!cbor_value_at_end(&arr) &&
           list->count < TMUXREMOTE_CLIENT_MAX_SESSIONS) {
        if (!cbor_value_is_map(&arr)) {
            if (cbor_value_advance(&arr) != CborNoError) {
                parseOk = false;
                break;
            }
            continue;
        }

        CborValue mapVal;
        if (cbor_value_enter_container(&arr, &mapVal) != CborNoError) {
            parseOk = false;
            break;
        }

        struct tmuxremote_client_session_info* info = &list->sessions[list->count];
        bool validEntry = true;

        while (!cbor_value_at_end(&mapVal)) {
            if (!cbor_value_is_text_string(&mapVal)) {
                if (cbor_value_advance(&mapVal) != CborNoError ||
                    cbor_value_at_end(&mapVal) ||
                    cbor_value_advance(&mapVal) != CborNoError) {
                    validEntry = false;
                    break;
                }
                continue;
            }

            char key[32];
            size_t keyLen = sizeof(key) - 1;
            if (cbor_value_copy_text_string(&mapVal, key, &keyLen, NULL) != CborNoError) {
                validEntry = false;
                break;
            }
            key[keyLen] = '\0';
            if (cbor_value_advance(&mapVal) != CborNoError) {
                validEntry = false;
                break;
            }

            if (strcmp(key, "name") == 0 && cbor_value_is_text_string(&mapVal)) {
                size_t nameLen = sizeof(info->name) - 1;
                if (cbor_value_copy_text_string(&mapVal, info->name, &nameLen, NULL) != CborNoError) {
                    validEntry = false;
                    break;
                }
                info->name[nameLen] = '\0';
            } else if (strcmp(key, "cols") == 0 &&
                       cbor_value_is_unsigned_integer(&mapVal)) {
                uint64_t v;
                if (cbor_value_get_uint64(&mapVal, &v) != CborNoError ||
                    v > UINT16_MAX) {
                    validEntry = false;
                    break;
                }
                info->cols = (uint16_t)v;
            } else if (strcmp(key, "rows") == 0 &&
                       cbor_value_is_unsigned_integer(&mapVal)) {
                uint64_t v;
                if (cbor_value_get_uint64(&mapVal, &v) != CborNoError ||
                    v > UINT16_MAX) {
                    validEntry = false;
                    break;
                }
                info->rows = (uint16_t)v;
            } else if (strcmp(key, "attached") == 0 &&
                       cbor_value_is_unsigned_integer(&mapVal)) {
                uint64_t v;
                if (cbor_value_get_uint64(&mapVal, &v) != CborNoError ||
                    v > INT_MAX) {
                    validEntry = false;
                    break;
                }
                info->attached = (int)v;
            }
            if (cbor_value_advance(&mapVal) != CborNoError) {
                validEntry = false;
                break;
            }
        }

        if (!validEntry) {
            parseOk = false;
            break;
        }
        if (cbor_value_leave_container(&arr, &mapVal) != CborNoError) {
            parseOk = false;
            break;
        }
        list->count++;
    }

    nabto_client_coap_free(coap);
    return parseOk;
}

bool tmuxremote_coap_get_status(NabtoClientConnection* conn,
                                NabtoClient* client,
                                struct tmuxremote_client_status_info* info)
{
    memset(info, 0, sizeof(struct tmuxremote_client_status_info));

    NabtoClientCoap* coap = nabto_client_coap_new(conn, "GET", "/terminal/status");
    if (coap == NULL) {
        return false;
    }

    NabtoClientFuture* future = nabto_client_future_new(client);
    nabto_client_coap_execute(coap, future);
    NabtoClientError ec = nabto_client_future_wait(future);
    nabto_client_future_free(future);

    if (ec != NABTO_CLIENT_EC_OK) {
        nabto_client_coap_free(coap);
        return false;
    }

    uint16_t statusCode = 0;
    nabto_client_coap_get_response_status_code(coap, &statusCode);

    if (statusCode != 205) {
        nabto_client_coap_free(coap);
        return false;
    }

    void* payload = NULL;
    size_t payloadLen = 0;
    nabto_client_coap_get_response_payload(coap, &payload, &payloadLen);

    bool parseOk = true;
    if (payload != NULL && payloadLen > 0) {
        CborParser parser;
        CborValue value;
        if (cbor_parser_init((const uint8_t*)payload, payloadLen, 0, &parser, &value) != CborNoError) {
            nabto_client_coap_free(coap);
            return false;
        }

        if (cbor_value_is_map(&value)) {
            CborValue map;
            if (cbor_value_enter_container(&value, &map) != CborNoError) {
                nabto_client_coap_free(coap);
                return false;
            }

            while (!cbor_value_at_end(&map)) {
                if (!cbor_value_is_text_string(&map)) {
                    if (cbor_value_advance(&map) != CborNoError ||
                        cbor_value_at_end(&map) ||
                        cbor_value_advance(&map) != CborNoError) {
                        parseOk = false;
                        break;
                    }
                    continue;
                }

                char key[32];
                size_t keyLen = sizeof(key) - 1;
                if (cbor_value_copy_text_string(&map, key, &keyLen, NULL) != CborNoError) {
                    parseOk = false;
                    break;
                }
                key[keyLen] = '\0';
                if (cbor_value_advance(&map) != CborNoError) {
                    parseOk = false;
                    break;
                }

                if (strcmp(key, "version") == 0 &&
                    cbor_value_is_text_string(&map)) {
                    size_t vLen = sizeof(info->version) - 1;
                    if (cbor_value_copy_text_string(&map, info->version, &vLen, NULL) != CborNoError) {
                        parseOk = false;
                        break;
                    }
                    info->version[vLen] = '\0';
                } else if (strcmp(key, "active_sessions") == 0 &&
                           cbor_value_is_unsigned_integer(&map)) {
                    uint64_t v;
                    if (cbor_value_get_uint64(&map, &v) != CborNoError ||
                        v > INT_MAX) {
                        parseOk = false;
                        break;
                    }
                    info->activeSessions = (int)v;
                } else if (strcmp(key, "uptime_seconds") == 0 &&
                           cbor_value_is_unsigned_integer(&map)) {
                    if (cbor_value_get_uint64(&map, &info->uptimeSeconds) != CborNoError) {
                        parseOk = false;
                        break;
                    }
                }
                if (cbor_value_advance(&map) != CborNoError) {
                    parseOk = false;
                    break;
                }
            }
            if (parseOk && cbor_value_leave_container(&value, &map) != CborNoError) {
                parseOk = false;
            }
        }
    }

    nabto_client_coap_free(coap);
    return parseOk;
}
