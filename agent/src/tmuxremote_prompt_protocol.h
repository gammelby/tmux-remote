#ifndef TMUXREMOTE_PROMPT_PROTOCOL_H_
#define TMUXREMOTE_PROMPT_PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tmuxremote_prompt.h"

typedef struct {
    char* instance_id;
    char* decision;
    char* keys;
} tmuxremote_prompt_resolve_message;

uint8_t* tmuxremote_prompt_protocol_encode_present(
    const tmuxremote_prompt_instance* instance,
    size_t* out_len);

uint8_t* tmuxremote_prompt_protocol_encode_update(
    const tmuxremote_prompt_instance* instance,
    size_t* out_len);

uint8_t* tmuxremote_prompt_protocol_encode_gone(
    const char* instance_id,
    size_t* out_len);

bool tmuxremote_prompt_protocol_decode_resolve(
    const uint8_t* framed_data,
    size_t framed_len,
    tmuxremote_prompt_resolve_message* out);

void tmuxremote_prompt_protocol_free_resolve(
    tmuxremote_prompt_resolve_message* message);

#endif
