#include "tmuxremote_init.h"
#include "tmuxremote_iam.h"
#include "tmuxremote_keychain.h"
#include "tmuxremote.h"

#include <apps/common/device_config.h>
#include <apps/common/random_string.h>
#include <apps/common/string_file.h>

#include <modules/iam/nm_iam_serializer.h>
#include <modules/iam/nm_iam_state.h>

#include <modules/fs/posix/nm_fs_posix.h>

#include <nabto/nabto_device.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NEWLINE "\n"

static bool init_common(const char* homeDir, const char* productId,
                        const char* deviceId)
{
    struct nm_fs fsImpl = nm_fs_posix_get_impl();
    char buffer[512];

    snprintf(buffer, sizeof(buffer), "%s/config/device.json", homeDir);
    char* deviceConfigFile = strdup(buffer);
    char* deviceKeyFile = NULL;

    snprintf(buffer, sizeof(buffer), "%s/state/iam_state.json", homeDir);
    char* iamStateFile = strdup(buffer);

    /* Check if config already exists */
    if (string_file_exists(&fsImpl, deviceConfigFile)) {
        printf("Configuration already exists at %s" NEWLINE, homeDir);
        printf("Remove the directory to reinitialize." NEWLINE);
        free(deviceConfigFile);
        free(deviceKeyFile);
        free(iamStateFile);
        return false;
    }

    /* Get product/device IDs interactively if not provided */
    char pidBuf[64] = {0};
    char didBuf[64] = {0};

    if (productId != NULL && strlen(productId) > 0) {
        strncpy(pidBuf, productId, sizeof(pidBuf) - 1);
    } else {
        printf("Product ID: ");
        if (fgets(pidBuf, sizeof(pidBuf), stdin) == NULL) {
            free(deviceConfigFile);
            free(deviceKeyFile);
            free(iamStateFile);
            return false;
        }
        /* Trim newline */
        pidBuf[strcspn(pidBuf, "\n")] = 0;
    }

    if (deviceId != NULL && strlen(deviceId) > 0) {
        strncpy(didBuf, deviceId, sizeof(didBuf) - 1);
    } else {
        printf("Device ID:  ");
        if (fgets(didBuf, sizeof(didBuf), stdin) == NULL) {
            free(deviceConfigFile);
            free(deviceKeyFile);
            free(iamStateFile);
            return false;
        }
        didBuf[strcspn(didBuf, "\n")] = 0;
    }

    if (strlen(pidBuf) == 0 || strlen(didBuf) == 0) {
        printf("Product ID and Device ID are required." NEWLINE);
        free(deviceConfigFile);
        free(deviceKeyFile);
        free(iamStateFile);
        return false;
    }

    deviceKeyFile = tmuxremote_device_key_file_path(homeDir, pidBuf, didBuf);
    if (deviceKeyFile == NULL) {
        printf("Could not allocate device key file path" NEWLINE);
        free(deviceConfigFile);
        free(deviceKeyFile);
        free(iamStateFile);
        return false;
    }

    /* Prompt for keychain storage on macOS */
    bool useKeychain = false;
    if (tmuxremote_keychain_available()) {
        if (isatty(fileno(stdin))) {
            char kcBuf[16] = {0};
            printf("Store device key in macOS Keychain? [Y/n] ");
            if (fgets(kcBuf, sizeof(kcBuf), stdin) != NULL) {
                kcBuf[strcspn(kcBuf, "\n")] = 0;
                useKeychain = (kcBuf[0] != 'n' && kcBuf[0] != 'N');
            } else {
                useKeychain = true;
            }
        } else {
            const char* keyStorageEnv = getenv("TMUX_REMOTE_KEY_STORAGE");
            useKeychain = (keyStorageEnv != NULL &&
                           strcmp(keyStorageEnv, "keychain") == 0);
        }
    }

    /* Save device config */
    struct device_config dc;
    device_config_init(&dc);
    dc.productId = strdup(pidBuf);
    dc.deviceId = strdup(didBuf);
    if (!save_device_config(&fsImpl, deviceConfigFile, &dc)) {
        printf("Failed to save device configuration" NEWLINE);
        device_config_deinit(&dc);
        free(deviceConfigFile);
        free(deviceKeyFile);
        free(iamStateFile);
        return false;
    }

    /* Persist keychain preference */
    if (!tmuxremote_config_set_keychain_key(&fsImpl, deviceConfigFile,
                                            useKeychain))
    {
        if (useKeychain) {
            printf("Warning: Could not persist KeychainKey preference." NEWLINE);
            printf("Falling back to file-based key storage." NEWLINE);
            useKeychain = false;
            if (!tmuxremote_config_set_keychain_key(&fsImpl, deviceConfigFile,
                                                    false))
            {
                printf("Warning: Could not persist key storage preference in device config." NEWLINE);
            }
        } else {
            printf("Warning: Could not persist key storage preference in device config." NEWLINE);
        }
    }

    /* Create device and generate key */
    NabtoDevice* device = nabto_device_new();
    if (device == NULL) {
        printf("Failed to create Nabto device" NEWLINE);
        device_config_deinit(&dc);
        free(deviceConfigFile);
        free(deviceKeyFile);
        free(iamStateFile);
        return false;
    }

    struct nn_log logger;
    memset(&logger, 0, sizeof(logger));

    bool keychainUsed = false;
    if (!tmuxremote_load_or_create_private_key(device, &fsImpl, deviceKeyFile,
                                               &logger, pidBuf, didBuf,
                                               useKeychain, &keychainUsed))
    {
        printf("Failed to create device key" NEWLINE);
        nabto_device_free(device);
        device_config_deinit(&dc);
        free(deviceConfigFile);
        free(deviceKeyFile);
        free(iamStateFile);
        return false;
    }

    char* fingerprint = NULL;
    nabto_device_get_device_fingerprint(device, &fingerprint);

    /* Create IAM state */
    tmuxremote_iam_create_default_state(device, &fsImpl, iamStateFile, &logger, "owner");

    /* Load state back to get pairing string for owner */
    char* str = NULL;
    if (string_file_load(&fsImpl, iamStateFile, &str)) {
        struct nm_iam_state* state = nm_iam_state_new();
        if (nm_iam_serializer_state_load_json(state, str, &logger)) {
            struct nm_iam_user* user = NULL;
            NN_LLIST_FOREACH(user, &state->users) {
                if (user->username != NULL &&
                    strcmp(user->username, "owner") == 0) {
                    break;
                }
            }
            if (user != NULL && user->password != NULL && user->sct != NULL) {
                printf(NEWLINE);
                printf("Pairing string for initial user:" NEWLINE);
                printf("  p=%s,d=%s,u=owner,pwd=%s,sct=%s" NEWLINE,
                       pidBuf, didBuf,
                       user->password, user->sct);
            }
        }
        nm_iam_state_free(state);
        free(str);
    }

    printf(NEWLINE);
    printf("No configuration found. Creating initial setup." NEWLINE);
    printf(NEWLINE);
    printf("Product ID: %s" NEWLINE, pidBuf);
    printf("Device ID:  %s" NEWLINE, didBuf);
    printf(NEWLINE);
    printf("Generated device keypair." NEWLINE);
    if (keychainUsed) {
        printf("Key storage: macOS Keychain" NEWLINE);
    } else {
        printf("Key storage: %s" NEWLINE, deviceKeyFile);
    }
    printf("Fingerprint: %s" NEWLINE, fingerprint);
    printf(NEWLINE);
    printf("Register this fingerprint in the Nabto Cloud Console before starting." NEWLINE);
    printf(NEWLINE);
    printf("Configuration written to %s/" NEWLINE, homeDir);

    nabto_device_string_free(fingerprint);
    nabto_device_free(device);
    device_config_deinit(&dc);
    free(deviceConfigFile);
    free(deviceKeyFile);
    free(iamStateFile);

    return true;
}

bool tmuxremote_do_init(const char* homeDir, const char* productId,
                        const char* deviceId)
{
    return init_common(homeDir, productId, deviceId);
}

bool tmuxremote_do_demo_init(const char* homeDir, const char* productId,
                             const char* deviceId)
{
    (void)homeDir;
    (void)productId;
    (void)deviceId;
    printf("--demo-init has been removed. Use --init (invite-only pairing)." NEWLINE);
    return false;
}

bool tmuxremote_do_add_user(const char* homeDir, const char* username)
{
    struct nm_fs fsImpl = nm_fs_posix_get_impl();
    char buffer[512];

    snprintf(buffer, sizeof(buffer), "%s/state/iam_state.json", homeDir);
    char* iamStateFile = strdup(buffer);

    snprintf(buffer, sizeof(buffer), "%s/config/device.json", homeDir);
    char* deviceConfigFile = strdup(buffer);

    /* Load device config for product/device IDs */
    struct nn_log logger;
    memset(&logger, 0, sizeof(logger));

    struct device_config dc;
    device_config_init(&dc);
    if (!load_device_config(&fsImpl, deviceConfigFile, &dc, &logger)) {
        printf("Device configuration not found. Run --init first." NEWLINE);
        free(iamStateFile);
        free(deviceConfigFile);
        return false;
    }

    /* Load IAM state */
    char* str = NULL;
    if (!string_file_load(&fsImpl, iamStateFile, &str)) {
        printf("IAM state not found. Run --init first." NEWLINE);
        device_config_deinit(&dc);
        free(iamStateFile);
        free(deviceConfigFile);
        return false;
    }

    struct nm_iam_state* state = nm_iam_state_new();
    if (!nm_iam_serializer_state_load_json(state, str, &logger)) {
        printf("Failed to parse IAM state." NEWLINE);
        nm_iam_state_free(state);
        free(str);
        device_config_deinit(&dc);
        free(iamStateFile);
        free(deviceConfigFile);
        return false;
    }
    free(str);

    /* Check if user already exists */
    struct nm_iam_user* existing = NULL;
    NN_LLIST_FOREACH(existing, &state->users) {
        if (existing->username != NULL && strcmp(existing->username, username) == 0) {
            printf("User '%s' already exists." NEWLINE, username);
            nm_iam_state_free(state);
            device_config_deinit(&dc);
            free(iamStateFile);
            free(deviceConfigFile);
            return false;
        }
    }

    /* Create a temporary device to generate SCT */
    NabtoDevice* device = nabto_device_new();
    if (device == NULL) {
        nm_iam_state_free(state);
        device_config_deinit(&dc);
        free(iamStateFile);
        free(deviceConfigFile);
        return false;
    }

    /* Add new user */
    struct nm_iam_user* user = nm_iam_state_user_new(username);
    nm_iam_state_user_set_role(user, "Owner");
    char* pwd = random_password(12);
    nm_iam_state_user_set_password(user, pwd);

    char* sct = NULL;
    nabto_device_create_server_connect_token(device, &sct);
    nm_iam_state_user_set_sct(user, sct);

    nm_iam_state_add_user(state, user);

    /* Enable password invite pairing */
    nm_iam_state_set_password_invite_pairing(state, true);

    /* Save state */
    char* jsonStr = NULL;
    if (!nm_iam_serializer_state_dump_json(state, &jsonStr)) {
        printf("Failed to serialize IAM state." NEWLINE);
        nabto_device_string_free(sct);
        free(pwd);
        nabto_device_free(device);
        nm_iam_state_free(state);
        device_config_deinit(&dc);
        free(iamStateFile);
        free(deviceConfigFile);
        return false;
    }

    enum nm_fs_error ec = fsImpl.write_file(fsImpl.impl, iamStateFile,
                                            (uint8_t*)jsonStr, strlen(jsonStr));
    nm_iam_serializer_string_free(jsonStr);

    if (ec != NM_FS_OK) {
        printf("Failed to save IAM state." NEWLINE);
        nabto_device_string_free(sct);
        free(pwd);
        nabto_device_free(device);
        nm_iam_state_free(state);
        device_config_deinit(&dc);
        free(iamStateFile);
        free(deviceConfigFile);
        return false;
    }

    printf("Created invitation for user '%s'." NEWLINE, username);
    printf(NEWLINE);
    printf("Copy this string into the tmux-remote app on the new device:" NEWLINE);
    printf(NEWLINE);
    printf("  p=%s,d=%s,u=%s,pwd=%s,sct=%s" NEWLINE,
           dc.productId, dc.deviceId, username, pwd, sct);
    printf(NEWLINE);
    printf("This invitation can only be used once. Pairing will close" NEWLINE);
    printf("again after this device pairs." NEWLINE);

    nabto_device_string_free(sct);
    free(pwd);
    nabto_device_free(device);
    nm_iam_state_free(state);
    device_config_deinit(&dc);
    free(iamStateFile);
    free(deviceConfigFile);

    return true;
}

bool tmuxremote_do_remove_user(const char* homeDir, const char* username)
{
    struct nm_fs fsImpl = nm_fs_posix_get_impl();
    char buffer[512];

    snprintf(buffer, sizeof(buffer), "%s/state/iam_state.json", homeDir);
    char* iamStateFile = strdup(buffer);

    struct nn_log logger;
    memset(&logger, 0, sizeof(logger));

    /* Load IAM state */
    char* str = NULL;
    if (!string_file_load(&fsImpl, iamStateFile, &str)) {
        printf("IAM state not found. Run --init first." NEWLINE);
        free(iamStateFile);
        return false;
    }

    struct nm_iam_state* state = nm_iam_state_new();
    if (!nm_iam_serializer_state_load_json(state, str, &logger)) {
        printf("Failed to parse IAM state." NEWLINE);
        nm_iam_state_free(state);
        free(str);
        free(iamStateFile);
        return false;
    }
    free(str);

    /* Find and remove user */
    struct nm_iam_user* user = NULL;
    NN_LLIST_FOREACH(user, &state->users) {
        if (user->username != NULL && strcmp(user->username, username) == 0) {
            break;
        }
    }

    if (user == NULL) {
        printf("User '%s' not found." NEWLINE, username);
        nm_iam_state_free(state);
        free(iamStateFile);
        return false;
    }

    nn_llist_erase_node(&user->listNode);
    nm_iam_state_user_free(user);

    /* Save state */
    char* jsonStr = NULL;
    if (!nm_iam_serializer_state_dump_json(state, &jsonStr)) {
        printf("Failed to serialize IAM state." NEWLINE);
        nm_iam_state_free(state);
        free(iamStateFile);
        return false;
    }

    enum nm_fs_error ec = fsImpl.write_file(fsImpl.impl, iamStateFile,
                                            (uint8_t*)jsonStr, strlen(jsonStr));
    nm_iam_serializer_string_free(jsonStr);

    if (ec != NM_FS_OK) {
        printf("Failed to save IAM state." NEWLINE);
        nm_iam_state_free(state);
        free(iamStateFile);
        return false;
    }

    printf("Removed user '%s'. Their public key has been deleted." NEWLINE, username);
    printf("They will no longer be able to connect." NEWLINE);

    nm_iam_state_free(state);
    free(iamStateFile);
    return true;
}
