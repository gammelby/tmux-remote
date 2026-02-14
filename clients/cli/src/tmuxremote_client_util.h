#ifndef TMUXREMOTE_CLIENT_UTIL_H_
#define TMUXREMOTE_CLIENT_UTIL_H_

#include "tmuxremote_client.h"

#include <stdbool.h>
#include <stdint.h>
#include <termios.h>

bool tmuxremote_parse_pairing_string(const char* str,
                                     struct tmuxremote_pairing_info* info);

/* Build connection options JSON. Caller must free() the returned string. */
char* tmuxremote_build_connection_options(const char* productId,
                                          const char* deviceId,
                                          const char* privateKey,
                                          const char* sct);

bool tmuxremote_terminal_get_size(uint16_t* cols, uint16_t* rows);
bool tmuxremote_terminal_set_raw(struct termios* saved);
bool tmuxremote_terminal_restore(const struct termios* saved);

#endif
