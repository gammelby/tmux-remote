#include "tmuxremote_client_util.h"
#include "3rdparty/cjson/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

bool tmuxremote_parse_pairing_string(const char* str,
                                     struct tmuxremote_pairing_info* info)
{
    memset(info, 0, sizeof(struct tmuxremote_pairing_info));

    if (str == NULL || strlen(str) == 0) {
        return false;
    }

    /* Parse comma-separated key=value pairs:
       p=<productId>,d=<deviceId>,u=<username>,pwd=<password>,sct=<sct> */

    char* copy = strdup(str);
    if (copy == NULL) {
        return false;
    }

    char* token = copy;
    while (token != NULL && *token != '\0') {
        char* comma = strchr(token, ',');
        if (comma != NULL) {
            *comma = '\0';
        }

        char* eq = strchr(token, '=');
        if (eq != NULL) {
            *eq = '\0';
            char* key = token;
            char* value = eq + 1;

            if (strcmp(key, "p") == 0) {
                strncpy(info->productId, value, sizeof(info->productId) - 1);
            } else if (strcmp(key, "d") == 0) {
                strncpy(info->deviceId, value, sizeof(info->deviceId) - 1);
            } else if (strcmp(key, "u") == 0) {
                strncpy(info->username, value, sizeof(info->username) - 1);
            } else if (strcmp(key, "pwd") == 0) {
                strncpy(info->password, value, sizeof(info->password) - 1);
            } else if (strcmp(key, "sct") == 0) {
                strncpy(info->sct, value, sizeof(info->sct) - 1);
            }
        }

        if (comma != NULL) {
            token = comma + 1;
        } else {
            break;
        }
    }

    free(copy);

    /* Validate required fields */
    if (strlen(info->productId) == 0 || strlen(info->deviceId) == 0 ||
        strlen(info->username) == 0 || strlen(info->password) == 0 ||
        strlen(info->sct) == 0) {
        printf("Invalid pairing string: missing required fields (p, d, u, pwd, sct)\n");
        return false;
    }

    return true;
}

char* tmuxremote_build_connection_options(const char* productId,
                                          const char* deviceId,
                                          const char* privateKey,
                                          const char* sct)
{
    cJSON* root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    if (cJSON_AddStringToObject(root, "ProductId", productId) == NULL ||
        cJSON_AddStringToObject(root, "DeviceId", deviceId) == NULL ||
        cJSON_AddStringToObject(root, "PrivateKey", privateKey) == NULL ||
        cJSON_AddStringToObject(root, "ServerConnectToken", sct) == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

bool tmuxremote_terminal_get_size(uint16_t* cols, uint16_t* rows)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
        *cols = 80;
        *rows = 24;
        return false;
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return true;
}

bool tmuxremote_terminal_set_raw(struct termios* saved)
{
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, saved) < 0) {
        return false;
    }

    raw = *saved;
    cfmakeraw(&raw);

    /* Keep output processing for newline translation */
    raw.c_oflag |= OPOST;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
        return false;
    }

    return true;
}

bool tmuxremote_terminal_restore(const struct termios* saved)
{
    return tcsetattr(STDIN_FILENO, TCSAFLUSH, saved) == 0;
}
