#include "nabtoshell_pattern_cbor.h"

#include <tinycbor/cbor.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

uint8_t *nabtoshell_pattern_cbor_encode_match(const nabtoshell_pattern_match *match,
                                               size_t *outLen)
{
    if (match == NULL) return NULL;

    uint8_t cborBuf[4096];
    CborEncoder encoder;
    cbor_encoder_init(&encoder, cborBuf, sizeof(cborBuf), 0);

    int mapSize = 4;
    if (match->prompt != NULL) mapSize = 5;

    CborEncoder mapEncoder;
    cbor_encoder_create_map(&encoder, &mapEncoder, mapSize);

    cbor_encode_text_stringz(&mapEncoder, "type");
    cbor_encode_text_stringz(&mapEncoder, "pattern_match");

    cbor_encode_text_stringz(&mapEncoder, "pattern_id");
    cbor_encode_text_stringz(&mapEncoder, match->id);

    cbor_encode_text_stringz(&mapEncoder, "pattern_type");
    const char *typeStr = "yes_no";
    if (match->pattern_type == PATTERN_TYPE_NUMBERED_MENU) typeStr = "numbered_menu";
    else if (match->pattern_type == PATTERN_TYPE_ACCEPT_REJECT) typeStr = "accept_reject";
    cbor_encode_text_stringz(&mapEncoder, typeStr);

    if (match->prompt != NULL) {
        cbor_encode_text_stringz(&mapEncoder, "prompt");
        cbor_encode_text_stringz(&mapEncoder, match->prompt);
    }

    cbor_encode_text_stringz(&mapEncoder, "actions");
    CborEncoder arrayEncoder;
    cbor_encoder_create_array(&mapEncoder, &arrayEncoder, match->action_count);
    for (int i = 0; i < match->action_count; i++) {
        CborEncoder actionMap;
        cbor_encoder_create_map(&arrayEncoder, &actionMap, 2);
        cbor_encode_text_stringz(&actionMap, "label");
        cbor_encode_text_stringz(&actionMap, match->actions[i].label);
        cbor_encode_text_stringz(&actionMap, "keys");
        cbor_encode_text_stringz(&actionMap, match->actions[i].keys);
        cbor_encoder_close_container(&arrayEncoder, &actionMap);
    }
    cbor_encoder_close_container(&mapEncoder, &arrayEncoder);

    CborError err = cbor_encoder_close_container(&encoder, &mapEncoder);

    if (err != CborNoError || cbor_encoder_get_extra_bytes_needed(&encoder) > 0)
        return NULL;

    size_t cborLen = cbor_encoder_get_buffer_size(&encoder, cborBuf);

    size_t totalLen = 4 + cborLen;
    uint8_t *buf = malloc(totalLen);
    if (buf == NULL) return NULL;

    uint32_t lenBE = htonl((uint32_t)cborLen);
    memcpy(buf, &lenBE, 4);
    memcpy(buf + 4, cborBuf, cborLen);

    *outLen = totalLen;
    return buf;
}

uint8_t *nabtoshell_pattern_cbor_encode_dismiss(size_t *outLen)
{
    uint8_t cborBuf[64];
    CborEncoder encoder;
    cbor_encoder_init(&encoder, cborBuf, sizeof(cborBuf), 0);

    CborEncoder mapEncoder;
    cbor_encoder_create_map(&encoder, &mapEncoder, 1);
    cbor_encode_text_stringz(&mapEncoder, "type");
    cbor_encode_text_stringz(&mapEncoder, "pattern_dismiss");
    CborError err = cbor_encoder_close_container(&encoder, &mapEncoder);

    if (err != CborNoError || cbor_encoder_get_extra_bytes_needed(&encoder) > 0)
        return NULL;

    size_t cborLen = cbor_encoder_get_buffer_size(&encoder, cborBuf);

    size_t totalLen = 4 + cborLen;
    uint8_t *buf = malloc(totalLen);
    if (buf == NULL) return NULL;

    uint32_t lenBE = htonl((uint32_t)cborLen);
    memcpy(buf, &lenBE, 4);
    memcpy(buf + 4, cborBuf, cborLen);

    *outLen = totalLen;
    return buf;
}

/* Helper: find a text string value for a given key in a CBOR map iterator.
 * Advances the iterator. Returns strdup'd string or NULL. */
static char *cbor_map_find_string(CborValue *map, const char *key)
{
    CborValue it;
    if (cbor_value_map_find_value(map, key, &it) != CborNoError)
        return NULL;
    if (!cbor_value_is_text_string(&it))
        return NULL;
    char *str = NULL;
    size_t len = 0;
    if (cbor_value_dup_text_string(&it, &str, &len, NULL) != CborNoError)
        return NULL;
    return str;
}

nabtoshell_pattern_match *nabtoshell_pattern_cbor_decode(const uint8_t *data,
                                                          size_t len,
                                                          bool *is_dismiss)
{
    *is_dismiss = false;

    if (data == NULL || len < 4) return NULL;

    uint32_t lenBE;
    memcpy(&lenBE, data, 4);
    uint32_t cborLen = ntohl(lenBE);
    if (4 + cborLen > len) return NULL;

    const uint8_t *cborData = data + 4;

    CborParser parser;
    CborValue root;
    if (cbor_parser_init(cborData, cborLen, 0, &parser, &root) != CborNoError)
        return NULL;
    if (!cbor_value_is_map(&root))
        return NULL;

    /* Get type field */
    char *type = cbor_map_find_string(&root, "type");
    if (type == NULL) return NULL;

    if (strcmp(type, "pattern_dismiss") == 0) {
        free(type);
        *is_dismiss = true;
        return NULL;
    }

    if (strcmp(type, "pattern_match") != 0) {
        free(type);
        return NULL;
    }
    free(type);

    nabtoshell_pattern_match *match = calloc(1, sizeof(*match));
    if (!match) return NULL;

    match->id = cbor_map_find_string(&root, "pattern_id");
    if (!match->id) {
        nabtoshell_pattern_match_free(match);
        return NULL;
    }

    char *patternType = cbor_map_find_string(&root, "pattern_type");
    if (patternType) {
        if (strcmp(patternType, "numbered_menu") == 0)
            match->pattern_type = PATTERN_TYPE_NUMBERED_MENU;
        else if (strcmp(patternType, "accept_reject") == 0)
            match->pattern_type = PATTERN_TYPE_ACCEPT_REJECT;
        else
            match->pattern_type = PATTERN_TYPE_YES_NO;
        free(patternType);
    }

    match->prompt = cbor_map_find_string(&root, "prompt");

    /* Decode actions array */
    CborValue actionsVal;
    if (cbor_value_map_find_value(&root, "actions", &actionsVal) == CborNoError &&
        cbor_value_is_array(&actionsVal)) {
        CborValue it;
        if (cbor_value_enter_container(&actionsVal, &it) == CborNoError) {
            while (!cbor_value_at_end(&it) && match->action_count < PATTERN_MATCHER_MAX_ACTIONS) {
                if (!cbor_value_is_map(&it)) break;

                char *label = NULL;
                char *keys = NULL;

                CborValue labelVal;
                if (cbor_value_map_find_value(&it, "label", &labelVal) == CborNoError &&
                    cbor_value_is_text_string(&labelVal)) {
                    size_t slen = 0;
                    cbor_value_dup_text_string(&labelVal, &label, &slen, NULL);
                }

                CborValue keysVal;
                if (cbor_value_map_find_value(&it, "keys", &keysVal) == CborNoError &&
                    cbor_value_is_text_string(&keysVal)) {
                    size_t slen = 0;
                    cbor_value_dup_text_string(&keysVal, &keys, &slen, NULL);
                }

                if (label && keys) {
                    match->actions[match->action_count].label = label;
                    match->actions[match->action_count].keys = keys;
                    match->action_count++;
                } else {
                    free(label);
                    free(keys);
                }

                CborError err = cbor_value_advance(&it);
                if (err != CborNoError) break;
            }
        }
    }

    return match;
}
