#ifndef TMUXREMOTE_INIT_H_
#define TMUXREMOTE_INIT_H_

#include <stdbool.h>

bool tmuxremote_do_init(const char* homeDir, const char* productId,
                        const char* deviceId);

bool tmuxremote_do_demo_init(const char* homeDir, const char* productId,
                             const char* deviceId);

bool tmuxremote_do_add_user(const char* homeDir, const char* username);

bool tmuxremote_do_remove_user(const char* homeDir, const char* username);

bool tmuxremote_do_move_device_key(const char* homeDir, const char* targetStorage);

#endif
