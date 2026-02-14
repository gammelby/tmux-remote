#include "tmuxremote_pair.h"
#include "tmuxremote_client.h"
#include "tmuxremote_client_config.h"
#include "tmuxremote_client_util.h"
#include "tmuxremote_client_coap.h"

#include <nabto/nabto_client.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tmuxremote_cmd_pair(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: tmux-remote pair <pairing-string> [--name <friendly-name>]\n");
        return 1;
    }

    const char* pairingStr = argv[1];
    const char* friendlyName = NULL;

    /* Parse optional --name argument */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            friendlyName = argv[i + 1];
            i++;
        }
    }

    /* Parse pairing string */
    struct tmuxremote_pairing_info info;
    if (!tmuxremote_parse_pairing_string(pairingStr, &info)) {
        printf("Invalid pairing string.\n");
        printf("Expected format: p=<product>,d=<device>,u=<user>,pwd=<pass>,sct=<token>\n");
        return 1;
    }

    /* Initialize config */
    struct tmuxremote_client_config config;
    if (!tmuxremote_config_init(&config)) {
        return 1;
    }
    tmuxremote_config_ensure_dirs(&config);
    tmuxremote_config_load_devices(&config);

    /* Check if already paired with this device */
    struct tmuxremote_device_bookmark* existing =
        tmuxremote_config_find_device(&config, info.deviceId);
    if (existing != NULL) {
        printf("Already paired with device '%s' (saved as '%s').\n",
               info.deviceId, existing->name);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    /* Create client and connection */
    NabtoClient* client = nabto_client_new();
    if (client == NULL) {
        printf("Failed to create Nabto client\n");
        tmuxremote_config_deinit(&config);
        return 1;
    }

    /* Load or create private key */
    char* privateKey = NULL;
    if (!tmuxremote_config_load_or_create_key(&config, client, &privateKey)) {
        nabto_client_free(client);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    NabtoClientConnection* conn = nabto_client_connection_new(client);
    if (conn == NULL) {
        printf("Failed to create connection\n");
        free(privateKey);
        nabto_client_free(client);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    /* Set connection options */
    char* optionsJson = tmuxremote_build_connection_options(
        info.productId, info.deviceId, privateKey, info.sct);
    free(privateKey);
    if (optionsJson == NULL) {
        nabto_client_connection_free(conn);
        nabto_client_free(client);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    NabtoClientError ec = nabto_client_connection_set_options(conn, optionsJson);
    free(optionsJson);
    if (ec != NABTO_CLIENT_EC_OK) {
        printf("Failed to set connection options: %s\n",
               nabto_client_error_get_message(ec));
        nabto_client_connection_free(conn);
        nabto_client_free(client);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    /* Connect */
    printf("Connecting to %s.%s...\n", info.productId, info.deviceId);

    NabtoClientFuture* future = nabto_client_future_new(client);
    nabto_client_connection_connect(conn, future);
    ec = nabto_client_future_wait(future);
    nabto_client_future_free(future);

    if (ec != NABTO_CLIENT_EC_OK) {
        printf("Failed to connect: %s\n", nabto_client_error_get_message(ec));
        nabto_client_connection_free(conn);
        nabto_client_free(client);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    /* Password authenticate */
    printf("Authenticating as '%s'...\n", info.username);

    future = nabto_client_future_new(client);
    nabto_client_connection_password_authenticate(conn, info.username,
                                                  info.password, future);
    ec = nabto_client_future_wait(future);
    nabto_client_future_free(future);

    if (ec != NABTO_CLIENT_EC_OK) {
        printf("Password authentication failed: %s\n",
               nabto_client_error_get_message(ec));
        nabto_client_connection_free(conn);
        nabto_client_free(client);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    /* Send pairing CoAP request */
    printf("Completing pairing...\n");

    if (!tmuxremote_coap_pair_password_invite(conn, client, info.username)) {
        printf("Pairing failed. Ensure the invitation is valid and unused.\n");
        nabto_client_connection_free(conn);
        nabto_client_free(client);
        tmuxremote_config_deinit(&config);
        return 1;
    }

    /* Get device fingerprint */
    char* deviceFingerprint = NULL;
    nabto_client_connection_get_device_fingerprint(conn, &deviceFingerprint);

    /* Save bookmark */
    struct tmuxremote_device_bookmark bookmark;
    memset(&bookmark, 0, sizeof(bookmark));

    if (friendlyName != NULL) {
        strncpy(bookmark.name, friendlyName, sizeof(bookmark.name) - 1);
    } else {
        strncpy(bookmark.name, info.deviceId, sizeof(bookmark.name) - 1);
    }

    strncpy(bookmark.productId, info.productId, sizeof(bookmark.productId) - 1);
    strncpy(bookmark.deviceId, info.deviceId, sizeof(bookmark.deviceId) - 1);
    strncpy(bookmark.sct, info.sct, sizeof(bookmark.sct) - 1);

    if (deviceFingerprint != NULL) {
        strncpy(bookmark.fingerprint, deviceFingerprint,
                sizeof(bookmark.fingerprint) - 1);
        nabto_client_string_free(deviceFingerprint);
    }

    if (!tmuxremote_config_add_device(&config, &bookmark)) {
        printf("Failed to save device bookmark\n");
    }

    /* Close connection */
    future = nabto_client_future_new(client);
    nabto_client_connection_close(conn, future);
    nabto_client_future_wait(future);
    nabto_client_future_free(future);

    nabto_client_connection_free(conn);
    nabto_client_free(client);
    tmuxremote_config_deinit(&config);

    printf("Paired with device '%s'. Saved as '%s'.\n",
           info.deviceId, bookmark.name);
    if (friendlyName == NULL) {
        printf("Use --name to set a friendly name, or rename later with: "
               "tmux-remote rename %s <name>\n", info.deviceId);
    }

    return 0;
}
