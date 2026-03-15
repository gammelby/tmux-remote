#include "pe_protocol.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include <tinycbor/cbor.h>

#define PROMPT_CBOR_MAX 8192

static const char* prompt_type_to_string(pe_prompt_type type)
{
    switch (type) {
        case PE_PROMPT_TYPE_NUMBERED_MENU:
            return "numbered_menu";
        case PE_PROMPT_TYPE_ACCEPT_REJECT:
            return "accept_reject";
        case PE_PROMPT_TYPE_YES_NO:
        default:
            return "yes_no";
    }
}

static uint8_t* wrap_framed_payload(const uint8_t* payload,
                                    size_t payload_len,
                                    size_t* out_len)
{
    size_t total_len = payload_len + 4;
    uint8_t* framed = malloc(total_len);
    if (framed == NULL) {
        return NULL;
    }

    uint32_t len_be = htonl((uint32_t)payload_len);
    memcpy(framed, &len_be, 4);
    memcpy(framed + 4, payload, payload_len);

    *out_len = total_len;
    return framed;
}

static bool encode_actions(CborEncoder* map,
                           const pe_prompt_instance* instance)
{
    cbor_encode_text_stringz(map, "actions");
    CborEncoder actions;
    cbor_encoder_create_array(map, &actions, instance->action_count);

    for (int i = 0; i < instance->action_count; i++) {
        CborEncoder action_map;
        cbor_encoder_create_map(&actions, &action_map, 2);

        cbor_encode_text_stringz(&action_map, "label");
        cbor_encode_text_stringz(&action_map,
                                 instance->actions[i].label != NULL
                                     ? instance->actions[i].label
                                     : "");

        cbor_encode_text_stringz(&action_map, "keys");
        cbor_encode_text_stringz(&action_map,
                                 instance->actions[i].keys != NULL
                                     ? instance->actions[i].keys
                                     : "");

        CborError err = cbor_encoder_close_container(&actions, &action_map);
        if (err != CborNoError) {
            return false;
        }
    }

    return cbor_encoder_close_container(map, &actions) == CborNoError;
}

static uint8_t* encode_instance_message(const char* type,
                                        const pe_prompt_instance* instance,
                                        size_t* out_len)
{
    if (instance == NULL || out_len == NULL) {
        return NULL;
    }

    uint8_t payload[PROMPT_CBOR_MAX];
    CborEncoder root;
    cbor_encoder_init(&root, payload, sizeof(payload), 0);

    CborEncoder map;
    cbor_encoder_create_map(&root, &map, 7);

    cbor_encode_text_stringz(&map, "type");
    cbor_encode_text_stringz(&map, type);

    cbor_encode_text_stringz(&map, "instance_id");
    cbor_encode_text_stringz(&map, instance->instance_id);

    cbor_encode_text_stringz(&map, "pattern_id");
    cbor_encode_text_stringz(&map,
                             instance->pattern_id != NULL
                                 ? instance->pattern_id
                                 : "");

    cbor_encode_text_stringz(&map, "pattern_type");
    cbor_encode_text_stringz(&map, prompt_type_to_string(instance->pattern_type));

    cbor_encode_text_stringz(&map, "prompt");
    cbor_encode_text_stringz(&map,
                             instance->prompt != NULL
                                 ? instance->prompt
                                 : "");

    cbor_encode_text_stringz(&map, "revision");
    cbor_encode_uint(&map, instance->revision);

    if (!encode_actions(&map, instance)) {
        return NULL;
    }

    if (cbor_encoder_close_container(&root, &map) != CborNoError) {
        return NULL;
    }

    size_t payload_len = cbor_encoder_get_buffer_size(&root, payload);
    return wrap_framed_payload(payload, payload_len, out_len);
}

uint8_t* pe_protocol_encode_present(
    const pe_prompt_instance* instance,
    size_t* out_len)
{
    return encode_instance_message("pattern_present", instance, out_len);
}

uint8_t* pe_protocol_encode_update(
    const pe_prompt_instance* instance,
    size_t* out_len)
{
    return encode_instance_message("pattern_update", instance, out_len);
}

uint8_t* pe_protocol_encode_gone(
    const char* instance_id,
    size_t* out_len)
{
    if (instance_id == NULL || out_len == NULL) {
        return NULL;
    }

    uint8_t payload[128];
    CborEncoder root;
    cbor_encoder_init(&root, payload, sizeof(payload), 0);

    CborEncoder map;
    cbor_encoder_create_map(&root, &map, 2);

    cbor_encode_text_stringz(&map, "type");
    cbor_encode_text_stringz(&map, "pattern_gone");

    cbor_encode_text_stringz(&map, "instance_id");
    cbor_encode_text_stringz(&map, instance_id);

    if (cbor_encoder_close_container(&root, &map) != CborNoError) {
        return NULL;
    }

    size_t payload_len = cbor_encoder_get_buffer_size(&root, payload);
    return wrap_framed_payload(payload, payload_len, out_len);
}

static char* map_string(CborValue* map, const char* key)
{
    CborValue value;
    if (cbor_value_map_find_value(map, key, &value) != CborNoError ||
        !cbor_value_is_text_string(&value)) {
        return NULL;
    }

    char* out = NULL;
    size_t len = 0;
    if (cbor_value_dup_text_string(&value, &out, &len, NULL) != CborNoError) {
        return NULL;
    }

    return out;
}

bool pe_protocol_decode_resolve(
    const uint8_t* framed_data,
    size_t framed_len,
    pe_prompt_resolve_message* out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    if (framed_data == NULL || framed_len < 4) {
        return false;
    }

    uint32_t len_be;
    memcpy(&len_be, framed_data, 4);
    uint32_t payload_len = ntohl(len_be);
    if (payload_len == 0 || (size_t)(payload_len + 4) > framed_len) {
        return false;
    }

    const uint8_t* payload = framed_data + 4;

    CborParser parser;
    CborValue root;
    if (cbor_parser_init(payload, payload_len, 0, &parser, &root) != CborNoError ||
        !cbor_value_is_map(&root)) {
        return false;
    }

    char* type = map_string(&root, "type");
    if (type == NULL || strcmp(type, "pattern_resolve") != 0) {
        free(type);
        return false;
    }
    free(type);

    out->instance_id = map_string(&root, "instance_id");
    out->decision = map_string(&root, "decision");
    out->keys = map_string(&root, "keys");

    if (out->instance_id == NULL || out->decision == NULL) {
        pe_protocol_free_resolve(out);
        return false;
    }

    if (strcmp(out->decision, "action") != 0 &&
        strcmp(out->decision, "dismiss") != 0) {
        pe_protocol_free_resolve(out);
        return false;
    }

    return true;
}

void pe_protocol_free_resolve(
    pe_prompt_resolve_message* message)
{
    if (message == NULL) {
        return;
    }

    free(message->instance_id);
    free(message->decision);
    free(message->keys);
    memset(message, 0, sizeof(*message));
}
