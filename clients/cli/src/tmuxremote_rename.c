#include "tmuxremote_rename.h"
#include "tmuxremote_client.h"
#include "tmuxremote_client_config.h"

#include <stdio.h>
#include <string.h>

int tmuxremote_cmd_rename(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: tmux-remote rename <current-name> <new-name>\n");
        return 1;
    }

    const char* currentName = argv[1];
    const char* newName = argv[2];

    struct tmuxremote_client_config config;
    if (!tmuxremote_config_init(&config)) {
        return 1;
    }
    tmuxremote_config_load_devices(&config);

    struct tmuxremote_device_bookmark* dev =
        tmuxremote_config_find_device(&config, currentName);
    if (dev == NULL) {
        printf("Device '%s' not found.\n", currentName);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    strncpy(dev->name, newName, sizeof(dev->name) - 1);
    dev->name[sizeof(dev->name) - 1] = '\0';

    if (!tmuxremote_config_save_devices(&config)) {
        printf("Failed to save devices.\n");
        tmuxremote_config_deinit(&config);
        return 1;
    }

    printf("Renamed '%s' to '%s'.\n", currentName, newName);

    tmuxremote_config_deinit(&config);
    return 0;
}
