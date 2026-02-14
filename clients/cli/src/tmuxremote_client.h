#ifndef TMUXREMOTE_CLIENT_H_
#define TMUXREMOTE_CLIENT_H_

#include <nabto/nabto_client.h>
#include <stdbool.h>
#include <stdint.h>

#define TMUXREMOTE_CLIENT_VERSION "0.1.0"
#define TMUXREMOTE_MAX_DEVICES 32
#define TMUXREMOTE_MAX_NAME_LEN 64

struct tmuxremote_device_bookmark {
    char name[TMUXREMOTE_MAX_NAME_LEN];
    char productId[TMUXREMOTE_MAX_NAME_LEN];
    char deviceId[TMUXREMOTE_MAX_NAME_LEN];
    char fingerprint[128];
    char sct[TMUXREMOTE_MAX_NAME_LEN];
};

struct tmuxremote_client_config {
    char* configDir;
    char* keyFile;
    char* devicesFile;
    struct tmuxremote_device_bookmark devices[TMUXREMOTE_MAX_DEVICES];
    int deviceCount;
};

struct tmuxremote_pairing_info {
    char productId[TMUXREMOTE_MAX_NAME_LEN];
    char deviceId[TMUXREMOTE_MAX_NAME_LEN];
    char username[TMUXREMOTE_MAX_NAME_LEN];
    char password[TMUXREMOTE_MAX_NAME_LEN];
    char sct[TMUXREMOTE_MAX_NAME_LEN];
};

#endif
