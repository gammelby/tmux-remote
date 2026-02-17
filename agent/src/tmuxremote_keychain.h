#ifndef TMUXREMOTE_KEYCHAIN_H_
#define TMUXREMOTE_KEYCHAIN_H_

#include <stdbool.h>

struct NabtoDevice_;
typedef struct NabtoDevice_ NabtoDevice;
struct nm_fs;
struct nn_log;

bool tmuxremote_keychain_available(void);

char* tmuxremote_device_key_file_path(const char* homeDir,
                                      const char* productId,
                                      const char* deviceId);
bool tmuxremote_ensure_namespaced_key_file(struct nm_fs* fsImpl,
                                           const char* legacyKeyFile,
                                           const char* namespacedKeyFile);

bool tmuxremote_load_or_create_private_key(NabtoDevice* device,
                                            struct nm_fs* fsImpl,
                                            const char* keyFile,
                                            struct nn_log* logger,
                                            const char* productId,
                                            const char* deviceId,
                                            bool useKeychain,
                                            bool* keychainUsedOut);

bool tmuxremote_config_get_keychain_key(struct nm_fs* fsImpl,
                                         const char* deviceConfigFile);
bool tmuxremote_config_set_keychain_key(struct nm_fs* fsImpl,
                                         const char* deviceConfigFile,
                                         bool useKeychain);

bool tmuxremote_move_private_key_storage(struct nm_fs* fsImpl,
                                          const char* deviceConfigFile,
                                          const char* keyFile,
                                          const char* productId,
                                          const char* deviceId,
                                          bool sourceUseKeychain,
                                          bool targetUseKeychain);

#endif
