#include "tmuxremote_banner.h"
#include "tmuxremote.h"
#include "tmuxremote_tmux.h"

#include <modules/iam/nm_iam.h>
#include <modules/iam/nm_iam_state.h>
#include <nn/llist.h>

#include <stdio.h>
#include <string.h>

#define NEWLINE "\n"

void tmuxremote_print_banner(struct tmuxremote* app, const char* fingerprint)
{
    const char* productId = nabto_device_get_product_id(app->device);
    const char* deviceId = nabto_device_get_device_id(app->device);

    /* Get IAM state to check for paired users */
    struct nm_iam_state* state = nm_iam_dump_state(&app->iam.iam);
    bool hasPairedUsers = false;
    bool hasPendingInvite = false;
    int userCount = 0;

    if (state != NULL) {
        struct nm_iam_user* user = NULL;
        NN_LLIST_FOREACH(user, &state->users) {
            userCount++;
            if (!nn_llist_empty(&user->fingerprints)) {
                hasPairedUsers = true;
            } else if (user->password != NULL) {
                hasPendingInvite = true;
            }
        }
    }

    printf(NEWLINE);
    printf("######## tmux-remote ########" NEWLINE);
    printf("# Product ID:     %s" NEWLINE, productId);
    printf("# Device ID:      %s" NEWLINE, deviceId);

    if (fingerprint != NULL) {
        /* Show truncated fingerprint */
        char fpShort[20];
        memset(fpShort, 0, sizeof(fpShort));
        strncpy(fpShort, fingerprint, 12);
        strcat(fpShort, "...");
        printf("# Fingerprint:    %s" NEWLINE, fpShort);
    }

    printf("# Version:        %s" NEWLINE, TMUXREMOTE_VERSION);
    if (app->keychainKey) {
        printf("# Key storage:    macOS Keychain" NEWLINE);
    } else {
        printf("# Key storage:    %s" NEWLINE,
               (app->deviceKeyFile != NULL) ? app->deviceKeyFile : "(unknown)");
    }
    printf("#" NEWLINE);

    if (!hasPairedUsers) {
        /* No users paired yet: show security info and pairing string */
        printf("# -- Security -------------------------------------------" NEWLINE);
        printf("#" NEWLINE);
        printf("#  tmux-remote uses the same trust model as SSH: client and" NEWLINE);
        printf("#  device exchange public keys once during pairing. All" NEWLINE);
        printf("#  subsequent connections are authenticated with these keys." NEWLINE);
        printf("#  No passwords are stored after pairing. All traffic is" NEWLINE);
        printf("#  end-to-end encrypted (DTLS with ECC)." NEWLINE);
        printf("#" NEWLINE);
        printf("#  Unlike SSH, no ports are opened on this machine and no" NEWLINE);
        printf("#  firewall or port forwarding is needed. The Nabto" NEWLINE);
        printf("#  basestation mediates P2P connection setup but never" NEWLINE);
        printf("#  sees your data." NEWLINE);
        printf("#" NEWLINE);

        if (hasPendingInvite) {
            /* Invite pairing mode: show invite string */
            struct nm_iam_user* user = NULL;
            if (state != NULL) {
                NN_LLIST_FOREACH(user, &state->users) {
                    if (user->password != NULL && nn_llist_empty(&user->fingerprints)) {
                        break;
                    }
                }
            }
            if (user != NULL && user->sct != NULL && user->password != NULL) {
                printf("# -- Pairing --------------------------------------------" NEWLINE);
                printf("#" NEWLINE);
                printf("#  No users paired yet. Pair by copying" NEWLINE);
                printf("#  this string into the tmux-remote client:" NEWLINE);
                printf("#" NEWLINE);
                printf("#  p=%s,d=%s,u=%s,pwd=%s,sct=%s" NEWLINE,
                       productId, deviceId, user->username,
                       user->password, user->sct);
                printf("#" NEWLINE);
                printf("#  This is a one-time pairing password. After you pair," NEWLINE);
                printf("#  it is invalidated and pairing is closed." NEWLINE);
                printf("#" NEWLINE);
            }
        }
    } else {
        /* Users are paired */
        printf("# Paired users:   ");
        struct nm_iam_user* user = NULL;
        bool first = true;
        if (state != NULL) {
            NN_LLIST_FOREACH(user, &state->users) {
                if (!nn_llist_empty(&user->fingerprints)) {
                    if (!first) {
                        printf(", ");
                    }
                    printf("%s", user->username);
                    if (user->displayName != NULL) {
                        printf(" (%s)", user->displayName);
                    }
                    first = false;
                }
            }
        }
        printf(NEWLINE);
        printf("# Pairing:        closed" NEWLINE);
        printf("#" NEWLINE);
        printf("# To add another device, run:" NEWLINE);
        printf("#   tmux-remote-agent --add-user <name>" NEWLINE);
        printf("#" NEWLINE);
    }

    /* List tmux sessions */
    struct tmuxremote_tmux_list tmuxList;
    tmuxremote_tmux_list_sessions(&tmuxList);

    if (tmuxList.count > 0) {
        printf("# -- tmux sessions --------------------------------------" NEWLINE);
        for (int i = 0; i < tmuxList.count; i++) {
            printf("#  %-12s %dx%d", tmuxList.sessions[i].name,
                   tmuxList.sessions[i].cols, tmuxList.sessions[i].rows);
            if (tmuxList.sessions[i].attached > 0) {
                printf("  (attached)");
            }
            printf(NEWLINE);
        }
        printf("#" NEWLINE);
    }

    if (!hasPairedUsers) {
        printf("######## Waiting for pairing... ########" NEWLINE);
    } else {
        printf("######## Attached to basestation ########" NEWLINE);
    }
    printf(NEWLINE);

    if (state != NULL) {
        nm_iam_state_free(state);
    }
}
