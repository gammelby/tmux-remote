#ifndef TMUXREMOTE_CLIENT_CONFIG_H_
#define TMUXREMOTE_CLIENT_CONFIG_H_

#include "tmuxremote_client.h"

#include <stdbool.h>

bool tmuxremote_config_init(struct tmuxremote_client_config* config);
void tmuxremote_config_deinit(struct tmuxremote_client_config* config);

bool tmuxremote_config_ensure_dirs(struct tmuxremote_client_config* config);
bool tmuxremote_config_load_devices(struct tmuxremote_client_config* config);
bool tmuxremote_config_save_devices(struct tmuxremote_client_config* config);
bool tmuxremote_config_add_device(struct tmuxremote_client_config* config,
                                  const struct tmuxremote_device_bookmark* device);
bool tmuxremote_config_replace_device(struct tmuxremote_client_config* config,
                                      const char* deviceId,
                                      const struct tmuxremote_device_bookmark* device);

struct tmuxremote_device_bookmark* tmuxremote_config_find_device(
    struct tmuxremote_client_config* config, const char* name);

bool tmuxremote_config_load_or_create_key(struct tmuxremote_client_config* config,
                                          NabtoClient* client, char** privateKey);

#endif
