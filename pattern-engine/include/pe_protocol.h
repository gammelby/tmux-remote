#ifndef PE_PROTOCOL_H_
#define PE_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pe_prompt.h"

typedef struct {
    char* instance_id;
    char* decision;
    char* keys;
} pe_prompt_resolve_message;

uint8_t* pe_protocol_encode_present(
    const pe_prompt_instance* instance,
    size_t* out_len);

uint8_t* pe_protocol_encode_update(
    const pe_prompt_instance* instance,
    size_t* out_len);

uint8_t* pe_protocol_encode_gone(
    const char* instance_id,
    size_t* out_len);

bool pe_protocol_decode_resolve(
    const uint8_t* framed_data,
    size_t framed_len,
    pe_prompt_resolve_message* out);

void pe_protocol_free_resolve(
    pe_prompt_resolve_message* message);

#endif
