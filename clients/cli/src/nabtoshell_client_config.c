#include "nabtoshell_client_config.h"
#include "3rdparty/cjson/cJSON.h"

#include <nabto/nabto_client.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static char* read_file(const char* path);
static bool write_file(const char* path, const char* content);

bool nabtoshell_config_init(struct nabtoshell_client_config* config)
{
    memset(config, 0, sizeof(struct nabtoshell_client_config));

    const char* baseDir = getenv("NABTOSHELL_HOME");
    if (baseDir != NULL && baseDir[0] != '\0') {
        config->configDir = strdup(baseDir);
    } else {
        const char* home = getenv("HOME");
        if (home == NULL) {
            printf("Cannot determine HOME directory\n");
            return false;
        }
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%s/.nabtoshell-client", home);
        config->configDir = strdup(buffer);
    }

    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s/client.key", config->configDir);
    config->keyFile = strdup(buffer);

    snprintf(buffer, sizeof(buffer), "%s/devices.json", config->configDir);
    config->devicesFile = strdup(buffer);

    return true;
}

void nabtoshell_config_deinit(struct nabtoshell_client_config* config)
{
    free(config->configDir);
    free(config->keyFile);
    free(config->devicesFile);
    config->configDir = NULL;
    config->keyFile = NULL;
    config->devicesFile = NULL;
}

bool nabtoshell_config_ensure_dirs(struct nabtoshell_client_config* config)
{
    if (mkdir(config->configDir, 0700) != 0 && errno != EEXIST) {
        return false;
    }
    chmod(config->configDir, 0700);
    return true;
}

bool nabtoshell_config_load_devices(struct nabtoshell_client_config* config)
{
    config->deviceCount = 0;

    char* json = read_file(config->devicesFile);
    if (json == NULL) {
        return true; /* No devices file yet, that's OK */
    }
    chmod(config->devicesFile, S_IRUSR | S_IWUSR);

    cJSON* root = cJSON_Parse(json);
    free(json);
    if (root == NULL) {
        printf("Failed to parse devices.json\n");
        return false;
    }

    cJSON* devices = cJSON_GetObjectItem(root, "devices");
    if (devices == NULL || !cJSON_IsArray(devices)) {
        cJSON_Delete(root);
        return true;
    }

    int count = cJSON_GetArraySize(devices);
    if (count > NABTOSHELL_MAX_DEVICES) {
        count = NABTOSHELL_MAX_DEVICES;
    }

    for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(devices, i);
        if (!cJSON_IsObject(item)) continue;

        struct nabtoshell_device_bookmark* dev = &config->devices[config->deviceCount];

        cJSON* name = cJSON_GetObjectItem(item, "name");
        cJSON* productId = cJSON_GetObjectItem(item, "productId");
        cJSON* deviceId = cJSON_GetObjectItem(item, "deviceId");
        cJSON* fingerprint = cJSON_GetObjectItem(item, "fingerprint");
        cJSON* sct = cJSON_GetObjectItem(item, "sct");

        if (name && cJSON_IsString(name))
            strncpy(dev->name, name->valuestring, sizeof(dev->name) - 1);
        if (productId && cJSON_IsString(productId))
            strncpy(dev->productId, productId->valuestring, sizeof(dev->productId) - 1);
        if (deviceId && cJSON_IsString(deviceId))
            strncpy(dev->deviceId, deviceId->valuestring, sizeof(dev->deviceId) - 1);
        if (fingerprint && cJSON_IsString(fingerprint))
            strncpy(dev->fingerprint, fingerprint->valuestring, sizeof(dev->fingerprint) - 1);
        if (sct && cJSON_IsString(sct))
            strncpy(dev->sct, sct->valuestring, sizeof(dev->sct) - 1);

        config->deviceCount++;
    }

    cJSON_Delete(root);
    return true;
}

bool nabtoshell_config_save_devices(struct nabtoshell_client_config* config)
{
    cJSON* root = cJSON_CreateObject();
    cJSON* devices = cJSON_CreateArray();

    for (int i = 0; i < config->deviceCount; i++) {
        struct nabtoshell_device_bookmark* dev = &config->devices[i];
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", dev->name);
        cJSON_AddStringToObject(item, "productId", dev->productId);
        cJSON_AddStringToObject(item, "deviceId", dev->deviceId);
        cJSON_AddStringToObject(item, "fingerprint", dev->fingerprint);
        cJSON_AddStringToObject(item, "sct", dev->sct);
        cJSON_AddItemToArray(devices, item);
    }

    cJSON_AddItemToObject(root, "devices", devices);

    char* json = cJSON_Print(root);
    cJSON_Delete(root);

    if (json == NULL) {
        return false;
    }

    bool ok = write_file(config->devicesFile, json);
    free(json);
    return ok;
}

bool nabtoshell_config_add_device(struct nabtoshell_client_config* config,
                                  const struct nabtoshell_device_bookmark* device)
{
    if (config->deviceCount >= NABTOSHELL_MAX_DEVICES) {
        printf("Too many saved devices\n");
        return false;
    }

    memcpy(&config->devices[config->deviceCount], device,
           sizeof(struct nabtoshell_device_bookmark));
    config->deviceCount++;

    return nabtoshell_config_save_devices(config);
}

struct nabtoshell_device_bookmark* nabtoshell_config_find_device(
    struct nabtoshell_client_config* config, const char* name)
{
    for (int i = 0; i < config->deviceCount; i++) {
        if (strcmp(config->devices[i].name, name) == 0 ||
            strcmp(config->devices[i].deviceId, name) == 0) {
            return &config->devices[i];
        }
    }
    return NULL;
}

bool nabtoshell_config_load_or_create_key(struct nabtoshell_client_config* config,
                                          NabtoClient* client, char** privateKey)
{
    /* Try to load existing key */
    char* key = read_file(config->keyFile);
    if (key != NULL) {
        chmod(config->keyFile, S_IRUSR | S_IWUSR);
        *privateKey = key;
        return true;
    }

    /* Generate new key */
    NabtoClientError ec = nabto_client_create_private_key(client, privateKey);
    if (ec != NABTO_CLIENT_EC_OK) {
        printf("Failed to create private key\n");
        return false;
    }

    /* Save the key */
    if (!write_file(config->keyFile, *privateKey)) {
        printf("Failed to save private key to %s\n", config->keyFile);
        nabto_client_string_free(*privateKey);
        *privateKey = NULL;
        return false;
    }

    /* Make a copy since we'll free the nabto-allocated string */
    char* copy = strdup(*privateKey);
    nabto_client_string_free(*privateKey);
    *privateKey = copy;

    return true;
}

static char* read_file(const char* path)
{
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    char* buf = (char*)malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)size, f);
    buf[read] = '\0';
    fclose(f);

    return buf;
}

static bool write_file(const char* path, const char* content)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return false;
    }
    (void)fchmod(fd, S_IRUSR | S_IWUSR);

    FILE* f = fdopen(fd, "w");
    if (f == NULL) {
        close(fd);
        return false;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    return written == len;
}
