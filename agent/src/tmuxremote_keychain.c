#include "tmuxremote_keychain.h"

#include <apps/common/private_key.h>
#include <apps/common/string_file.h>

#include <modules/fs/nm_fs.h>

#include <nabto/nabto_device.h>

#include <cjson/cJSON.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

#define NEWLINE "\n"
#define KEYCHAIN_CONFIG_KEY "KeychainKey"

static bool tmuxremote_is_key_component_char(char c)
{
    unsigned char uc = (unsigned char)c;
    return (isalnum(uc) || c == '-' || c == '_' || c == '.');
}

static char* tmuxremote_sanitize_key_component(const char* input)
{
    const char* src = ((input != NULL && input[0] != '\0') ? input : "unknown");
    size_t len = strlen(src);
    char* out = malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }

    size_t i = 0;
    for (i = 0; i < len; i++) {
        out[i] = tmuxremote_is_key_component_char(src[i]) ? src[i] : '_';
    }
    out[len] = '\0';
    return out;
}

char* tmuxremote_device_key_file_path(const char* homeDir,
                                      const char* productId,
                                      const char* deviceId)
{
    if (homeDir == NULL) {
        return NULL;
    }

    char* safeProductId = tmuxremote_sanitize_key_component(productId);
    char* safeDeviceId = tmuxremote_sanitize_key_component(deviceId);
    if (safeProductId == NULL || safeDeviceId == NULL) {
        free(safeProductId);
        free(safeDeviceId);
        return NULL;
    }

    int pathLen = snprintf(NULL, 0, "%s/keys/%s_%s.key",
                           homeDir, safeProductId, safeDeviceId);
    if (pathLen < 0) {
        free(safeProductId);
        free(safeDeviceId);
        return NULL;
    }

    char* path = malloc((size_t)pathLen + 1);
    if (path == NULL) {
        free(safeProductId);
        free(safeDeviceId);
        return NULL;
    }

    snprintf(path, (size_t)pathLen + 1, "%s/keys/%s_%s.key",
             homeDir, safeProductId, safeDeviceId);
    free(safeProductId);
    free(safeDeviceId);
    return path;
}

bool tmuxremote_ensure_namespaced_key_file(struct nm_fs* fsImpl,
                                           const char* legacyKeyFile,
                                           const char* namespacedKeyFile)
{
    if (fsImpl == NULL || legacyKeyFile == NULL || namespacedKeyFile == NULL) {
        return false;
    }

    if (strcmp(legacyKeyFile, namespacedKeyFile) == 0) {
        return true;
    }

    if (string_file_exists(fsImpl, namespacedKeyFile)) {
        return true;
    }

    if (!string_file_exists(fsImpl, legacyKeyFile)) {
        return true;
    }

    char* keyData = NULL;
    if (!string_file_load(fsImpl, legacyKeyFile, &keyData)) {
        return false;
    }

    bool saved = string_file_save(fsImpl, namespacedKeyFile, keyData);
    free(keyData);
    if (!saved) {
        return false;
    }

    printf("Migrated legacy key file to %s" NEWLINE, namespacedKeyFile);
    return true;
}

#ifdef __APPLE__

static const char* KEYCHAIN_SERVICE = "dk.ulrik.tmux-remote.devicekey";
static const char* KEYCHAIN_LABEL = "tmux-remote Device Key";
static const char* KEYCHAIN_DESCRIPTION = "EC Private Key";

static char* tmuxremote_keychain_account_for_device(const char* productId,
                                                    const char* deviceId)
{
    if (productId == NULL || deviceId == NULL ||
        productId[0] == '\0' || deviceId[0] == '\0') {
        return NULL;
    }

    int len = snprintf(NULL, 0, "p=%s,d=%s", productId, deviceId);
    if (len < 0) {
        return NULL;
    }

    char* account = malloc((size_t)len + 1);
    if (account == NULL) {
        return NULL;
    }
    snprintf(account, (size_t)len + 1, "p=%s,d=%s", productId, deviceId);
    return account;
}

static bool tmuxremote_keychain_save_private_key_slot(const char* serviceUtf8,
                                                      const char* accountUtf8,
                                                      const char* pemKey)
{
    if (serviceUtf8 == NULL || accountUtf8 == NULL || pemKey == NULL) {
        return false;
    }

    CFStringRef service = CFStringCreateWithCString(
        kCFAllocatorDefault, serviceUtf8, kCFStringEncodingUTF8);
    CFStringRef account = CFStringCreateWithCString(
        kCFAllocatorDefault, accountUtf8, kCFStringEncodingUTF8);
    CFDataRef keyData = CFDataCreate(
        kCFAllocatorDefault, (const UInt8*)pemKey, (CFIndex)strlen(pemKey));

    if (service == NULL || account == NULL || keyData == NULL) {
        if (keyData != NULL) {
            CFRelease(keyData);
        }
        if (account != NULL) {
            CFRelease(account);
        }
        if (service != NULL) {
            CFRelease(service);
        }
        return false;
    }

    /* Delete any existing item first */
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (query == NULL) {
        CFRelease(keyData);
        CFRelease(account);
        CFRelease(service);
        return false;
    }
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    SecItemDelete(query);

    /* Add the new item with descriptive labels */
    CFDictionarySetValue(query, kSecValueData, keyData);
    CFDictionarySetValue(query, kSecAttrAccessible,
                         kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);

    CFStringRef label = CFStringCreateWithCString(
        kCFAllocatorDefault, KEYCHAIN_LABEL, kCFStringEncodingUTF8);
    CFStringRef description = CFStringCreateWithCString(
        kCFAllocatorDefault, KEYCHAIN_DESCRIPTION, kCFStringEncodingUTF8);
    if (label != NULL) {
        CFDictionarySetValue(query, kSecAttrLabel, label);
    }
    if (description != NULL) {
        CFDictionarySetValue(query, kSecAttrDescription, description);
    }

    OSStatus status = SecItemAdd(query, NULL);

    if (description != NULL) {
        CFRelease(description);
    }
    if (label != NULL) {
        CFRelease(label);
    }
    CFRelease(query);
    CFRelease(keyData);
    CFRelease(account);
    CFRelease(service);

    if (status != errSecSuccess) {
        printf("Failed to save key to macOS Keychain slot '%s' (error %d)." NEWLINE,
               accountUtf8, (int)status);
        printf("Is the login keychain unlocked?" NEWLINE);
        return false;
    }
    return true;
}

static bool tmuxremote_keychain_load_private_key_slot(const char* serviceUtf8,
                                                      const char* accountUtf8,
                                                      char** pemKeyOut)
{
    if (serviceUtf8 == NULL || accountUtf8 == NULL || pemKeyOut == NULL) {
        return false;
    }

    *pemKeyOut = NULL;

    CFStringRef service = CFStringCreateWithCString(
        kCFAllocatorDefault, serviceUtf8, kCFStringEncodingUTF8);
    CFStringRef account = CFStringCreateWithCString(
        kCFAllocatorDefault, accountUtf8, kCFStringEncodingUTF8);
    if (service == NULL || account == NULL) {
        if (account != NULL) {
            CFRelease(account);
        }
        if (service != NULL) {
            CFRelease(service);
        }
        return false;
    }

    CFMutableDictionaryRef query = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (query == NULL) {
        CFRelease(account);
        CFRelease(service);
        return false;
    }
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef result = NULL;
    OSStatus status = SecItemCopyMatching(query, &result);

    CFRelease(query);
    CFRelease(account);
    CFRelease(service);

    if (status != errSecSuccess || result == NULL) {
        return false;
    }

    CFDataRef data = (CFDataRef)result;
    CFIndex len = CFDataGetLength(data);
    char* pem = malloc((size_t)len + 1);
    if (pem == NULL) {
        CFRelease(data);
        return false;
    }
    memcpy(pem, CFDataGetBytePtr(data), (size_t)len);
    pem[len] = '\0';
    CFRelease(data);

    *pemKeyOut = pem;
    return true;
}

bool tmuxremote_keychain_available(void)
{
    return true;
}

#else /* !__APPLE__ */

bool tmuxremote_keychain_available(void)
{
    return false;
}

#endif /* __APPLE__ */

bool tmuxremote_load_or_create_private_key(NabtoDevice* device,
                                            struct nm_fs* fsImpl,
                                            const char* keyFile,
                                            struct nn_log* logger,
                                            const char* productId,
                                            const char* deviceId,
                                            bool useKeychain,
                                            bool* keychainUsedOut)
{
    if (keychainUsedOut != NULL) {
        *keychainUsedOut = false;
    }

    if (!useKeychain) {
        return load_or_create_private_key(device, fsImpl, keyFile, logger);
    }

#ifdef __APPLE__
    char* keychainAccount =
        tmuxremote_keychain_account_for_device(productId, deviceId);
    if (keychainAccount == NULL) {
        printf("Warning: Missing product/device ID for keychain slot naming." NEWLINE);
        printf("Falling back to file-based key storage." NEWLINE);
        return load_or_create_private_key(device, fsImpl, keyFile, logger);
    }

    /* Try to load existing key from keychain */
    char* pemKey = NULL;
    if (tmuxremote_keychain_load_private_key_slot(KEYCHAIN_SERVICE,
                                                  keychainAccount, &pemKey))
    {
        NabtoDeviceError ec = nabto_device_set_private_key(device, pemKey);
        free(pemKey);
        if (ec != NABTO_DEVICE_EC_OK) {
            free(keychainAccount);
            printf("Failed to set private key from keychain." NEWLINE);
            return false;
        }
        if (keychainUsedOut != NULL) {
            *keychainUsedOut = true;
        }
        free(keychainAccount);
        return true;
    }

    /* No key in keychain: generate a new one */
    char* newKey = NULL;
    NabtoDeviceError ec = nabto_device_create_private_key(device, &newKey);
    if (ec != NABTO_DEVICE_EC_OK) {
        free(keychainAccount);
        printf("Failed to generate device private key." NEWLINE);
        return false;
    }

    if (!tmuxremote_keychain_save_private_key_slot(KEYCHAIN_SERVICE,
                                                   keychainAccount, newKey))
    {
        free(keychainAccount);
        nabto_device_string_free(newKey);
        return false;
    }

    free(keychainAccount);
    ec = nabto_device_set_private_key(device, newKey);
    nabto_device_string_free(newKey);
    if (ec != NABTO_DEVICE_EC_OK) {
        printf("Failed to set private key." NEWLINE);
        return false;
    }

    if (keychainUsedOut != NULL) {
        *keychainUsedOut = true;
    }
    return true;
#else
    printf("Warning: KeychainKey is set but macOS Keychain is not available." NEWLINE);
    printf("Falling back to file-based key storage." NEWLINE);
    return load_or_create_private_key(device, fsImpl, keyFile, logger);
#endif
}

bool tmuxremote_config_get_keychain_key(struct nm_fs* fsImpl,
                                         const char* deviceConfigFile)
{
    char* str = NULL;
    if (!string_file_load(fsImpl, deviceConfigFile, &str)) {
        return false;
    }

    cJSON* json = cJSON_Parse(str);
    free(str);
    if (json == NULL) {
        return false;
    }

    cJSON* item = cJSON_GetObjectItemCaseSensitive(json, KEYCHAIN_CONFIG_KEY);
    bool result = (cJSON_IsBool(item) && cJSON_IsTrue(item));
    cJSON_Delete(json);
    return result;
}

bool tmuxremote_config_set_keychain_key(struct nm_fs* fsImpl,
                                         const char* deviceConfigFile,
                                         bool useKeychain)
{
    char* str = NULL;
    if (!string_file_load(fsImpl, deviceConfigFile, &str)) {
        return false;
    }

    cJSON* json = cJSON_Parse(str);
    free(str);
    if (json == NULL) {
        return false;
    }

    /* Remove existing KeychainKey if present */
    cJSON_DeleteItemFromObjectCaseSensitive(json, KEYCHAIN_CONFIG_KEY);

    /* Add the new value */
    cJSON_AddBoolToObject(json, KEYCHAIN_CONFIG_KEY, useKeychain);

    char* out = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (out == NULL) {
        return false;
    }

    enum nm_fs_error ec = fsImpl->write_file(
        fsImpl->impl, deviceConfigFile, (uint8_t*)out, strlen(out));
    cJSON_free(out);
    return (ec == NM_FS_OK);
}
