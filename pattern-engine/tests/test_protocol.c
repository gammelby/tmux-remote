#include <check.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <tinycbor/cbor.h>

#include "pe_protocol.h"

static void build_test_instance(pe_prompt_instance* instance)
{
    pe_prompt_instance_reset(instance);
    strncpy(instance->instance_id, "abc123", sizeof(instance->instance_id) - 1);
    instance->pattern_id = strdup("rule-1");
    instance->pattern_type = PE_PROMPT_TYPE_YES_NO;
    instance->prompt = strdup("Continue?");
    instance->actions[0].label = strdup("Yes");
    instance->actions[0].keys = strdup("y");
    instance->actions[1].label = strdup("No");
    instance->actions[1].keys = strdup("n");
    instance->action_count = 2;
    instance->revision = 3;
}

static char* decode_type_field(const uint8_t* framed, size_t len)
{
    if (len < 4) {
        return NULL;
    }

    uint32_t be;
    memcpy(&be, framed, 4);
    uint32_t payload_len = ntohl(be);
    if ((size_t)payload_len + 4 > len) {
        return NULL;
    }

    CborParser parser;
    CborValue root;
    if (cbor_parser_init(framed + 4, payload_len, 0, &parser, &root) != CborNoError ||
        !cbor_value_is_map(&root)) {
        return NULL;
    }

    CborValue type;
    if (cbor_value_map_find_value(&root, "type", &type) != CborNoError ||
        !cbor_value_is_text_string(&type)) {
        return NULL;
    }

    char* out = NULL;
    size_t out_len = 0;
    if (cbor_value_dup_text_string(&type, &out, &out_len, NULL) != CborNoError) {
        return NULL;
    }
    return out;
}

START_TEST(test_encode_present_update_gone)
{
    pe_prompt_instance instance;
    build_test_instance(&instance);

    size_t len = 0;
    uint8_t* present = pe_protocol_encode_present(&instance, &len);
    ck_assert_ptr_nonnull(present);
    ck_assert_int_gt((int)len, 4);

    char* type = decode_type_field(present, len);
    ck_assert_ptr_nonnull(type);
    ck_assert_str_eq(type, "pattern_present");
    free(type);

    free(present);

    uint8_t* update = pe_protocol_encode_update(&instance, &len);
    ck_assert_ptr_nonnull(update);
    type = decode_type_field(update, len);
    ck_assert_ptr_nonnull(type);
    ck_assert_str_eq(type, "pattern_update");
    free(type);
    free(update);

    uint8_t* gone = pe_protocol_encode_gone("abc123", &len);
    ck_assert_ptr_nonnull(gone);
    type = decode_type_field(gone, len);
    ck_assert_ptr_nonnull(type);
    ck_assert_str_eq(type, "pattern_gone");
    free(type);
    free(gone);

    pe_prompt_instance_free(&instance);
}
END_TEST

START_TEST(test_decode_resolve)
{
    uint8_t payload[256];
    CborEncoder root;
    cbor_encoder_init(&root, payload, sizeof(payload), 0);

    CborEncoder map;
    cbor_encoder_create_map(&root, &map, 4);
    cbor_encode_text_stringz(&map, "type");
    cbor_encode_text_stringz(&map, "pattern_resolve");
    cbor_encode_text_stringz(&map, "instance_id");
    cbor_encode_text_stringz(&map, "abc123");
    cbor_encode_text_stringz(&map, "decision");
    cbor_encode_text_stringz(&map, "action");
    cbor_encode_text_stringz(&map, "keys");
    cbor_encode_text_stringz(&map, "1\n");
    ck_assert_int_eq(cbor_encoder_close_container(&root, &map), CborNoError);

    size_t payload_len = cbor_encoder_get_buffer_size(&root, payload);
    uint8_t framed[260];
    uint32_t be = htonl((uint32_t)payload_len);
    memcpy(framed, &be, 4);
    memcpy(framed + 4, payload, payload_len);

    pe_prompt_resolve_message msg;
    ck_assert(pe_protocol_decode_resolve(framed, payload_len + 4, &msg));
    ck_assert_str_eq(msg.instance_id, "abc123");
    ck_assert_str_eq(msg.decision, "action");
    ck_assert_str_eq(msg.keys, "1\n");

    pe_protocol_free_resolve(&msg);
}
END_TEST

START_TEST(test_reject_invalid_decision)
{
    uint8_t payload[256];
    CborEncoder root;
    cbor_encoder_init(&root, payload, sizeof(payload), 0);

    CborEncoder map;
    cbor_encoder_create_map(&root, &map, 4);
    cbor_encode_text_stringz(&map, "type");
    cbor_encode_text_stringz(&map, "pattern_resolve");
    cbor_encode_text_stringz(&map, "instance_id");
    cbor_encode_text_stringz(&map, "abc123");
    cbor_encode_text_stringz(&map, "decision");
    cbor_encode_text_stringz(&map, "foo");
    cbor_encode_text_stringz(&map, "keys");
    cbor_encode_text_stringz(&map, "x");
    ck_assert_int_eq(cbor_encoder_close_container(&root, &map), CborNoError);

    size_t payload_len = cbor_encoder_get_buffer_size(&root, payload);
    uint8_t framed[260];
    uint32_t be = htonl((uint32_t)payload_len);
    memcpy(framed, &be, 4);
    memcpy(framed + 4, payload, payload_len);

    pe_prompt_resolve_message msg;
    bool ok = pe_protocol_decode_resolve(framed, payload_len + 4, &msg);
    ck_assert(!ok);
}
END_TEST

START_TEST(test_reject_truncated_frame)
{
    /* Build a valid framed message, then truncate it */
    uint8_t payload[256];
    CborEncoder root;
    cbor_encoder_init(&root, payload, sizeof(payload), 0);

    CborEncoder map;
    cbor_encoder_create_map(&root, &map, 4);
    cbor_encode_text_stringz(&map, "type");
    cbor_encode_text_stringz(&map, "pattern_resolve");
    cbor_encode_text_stringz(&map, "instance_id");
    cbor_encode_text_stringz(&map, "abc123");
    cbor_encode_text_stringz(&map, "decision");
    cbor_encode_text_stringz(&map, "action");
    cbor_encode_text_stringz(&map, "keys");
    cbor_encode_text_stringz(&map, "y");
    ck_assert_int_eq(cbor_encoder_close_container(&root, &map), CborNoError);

    size_t payload_len = cbor_encoder_get_buffer_size(&root, payload);
    uint8_t framed[260];
    /* Declare a length larger than actual data */
    uint32_t be = htonl((uint32_t)(payload_len + 100));
    memcpy(framed, &be, 4);
    memcpy(framed + 4, payload, payload_len);

    pe_prompt_resolve_message msg;
    /* Pass the actual total size (header + real payload), but header says more */
    bool ok = pe_protocol_decode_resolve(framed, 4 + payload_len, &msg);
    ck_assert(!ok);
}
END_TEST

START_TEST(test_reject_empty_payload)
{
    uint8_t framed[4];
    uint32_t be = htonl(0);
    memcpy(framed, &be, 4);

    pe_prompt_resolve_message msg;
    bool ok = pe_protocol_decode_resolve(framed, 4, &msg);
    ck_assert(!ok);
}
END_TEST

Suite* protocol_suite(void)
{
    Suite* s = suite_create("Protocol");
    TCase* tc = tcase_create("Core");

    tcase_add_test(tc, test_encode_present_update_gone);
    tcase_add_test(tc, test_decode_resolve);
    tcase_add_test(tc, test_reject_invalid_decision);
    tcase_add_test(tc, test_reject_truncated_frame);
    tcase_add_test(tc, test_reject_empty_payload);

    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    Suite* s = protocol_suite();
    SRunner* sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
