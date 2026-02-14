#include "tmuxremote_sessions.h"
#include "tmuxremote_client.h"
#include "tmuxremote_client_config.h"
#include "tmuxremote_client_coap.h"
#include "tmuxremote_client_util.h"

#include <nabto/nabto_client.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tmuxremote_cmd_sessions(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: tmux-remote sessions <device-name>\n");
        return 1;
    }

    const char* deviceName = argv[1];

    /* Load config */
    struct tmuxremote_client_config config;
    if (!tmuxremote_config_init(&config)) {
        return 1;
    }
    tmuxremote_config_load_devices(&config);

    struct tmuxremote_device_bookmark* dev =
        tmuxremote_config_find_device(&config, deviceName);
    if (dev == NULL) {
        printf("Device '%s' not found.\n", deviceName);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    /* Create client and connection */
    NabtoClient* client = nabto_client_new();
    if (client == NULL) {
        tmuxremote_config_deinit(&config);
        return 1;
    }

    char* privateKey = NULL;
    if (!tmuxremote_config_load_or_create_key(&config, client, &privateKey)) {
        nabto_client_free(client);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    NabtoClientConnection* conn = nabto_client_connection_new(client);

    char* optionsJson = tmuxremote_build_connection_options(
        dev->productId, dev->deviceId, privateKey, dev->sct);
    free(privateKey);
    if (optionsJson == NULL) {
        nabto_client_connection_free(conn);
        nabto_client_free(client);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    NabtoClientError ec2 = nabto_client_connection_set_options(conn, optionsJson);
    free(optionsJson);
    if (ec2 != NABTO_CLIENT_EC_OK) {
        printf("Failed to set connection options: %s\n",
               nabto_client_error_get_message(ec2));
        nabto_client_connection_free(conn);
        nabto_client_free(client);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    NabtoClientFuture* future = nabto_client_future_new(client);
    nabto_client_connection_connect(conn, future);
    NabtoClientError ec = nabto_client_future_wait(future);
    nabto_client_future_free(future);

    if (ec != NABTO_CLIENT_EC_OK) {
        printf("Failed to connect: %s\n", nabto_client_error_get_message(ec));
        nabto_client_connection_free(conn);
        nabto_client_free(client);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    /* List sessions */
    struct tmuxremote_client_sessions_list list;
    if (!tmuxremote_coap_list_sessions(conn, client, &list)) {
        printf("Failed to list sessions\n");
        nabto_client_connection_free(conn);
        nabto_client_free(client);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    /* Print results */
    if (list.count == 0) {
        printf("No tmux sessions found on '%s'.\n", deviceName);
    } else {
        printf("Sessions on '%s':\n", deviceName);
        printf("  %-16s %-12s %s\n", "NAME", "SIZE", "STATUS");
        for (int i = 0; i < list.count; i++) {
            printf("  %-16s %dx%-9d %s\n",
                   list.sessions[i].name,
                   list.sessions[i].cols,
                   list.sessions[i].rows,
                   list.sessions[i].attached > 0 ? "(attached)" : "");
        }
    }

    /* Cleanup */
    future = nabto_client_future_new(client);
    nabto_client_connection_close(conn, future);
    nabto_client_future_wait(future);
    nabto_client_future_free(future);

    nabto_client_connection_free(conn);
    nabto_client_free(client);
    tmuxremote_config_deinit(&config);

    return 0;
}
