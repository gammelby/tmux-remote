#include "tmuxremote_client.h"
#include "tmuxremote_client_log.h"
#include "tmuxremote_pair.h"
#include "tmuxremote_attach.h"
#include "tmuxremote_sessions.h"
#include "tmuxremote_devices.h"
#include "tmuxremote_rename.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_help(void);

int main(int argc, char** argv)
{
    const char* logFile = NULL;
    const char* nabtoLogLevel = NULL;

    /* Parse global options and build a new argv without them */
    int newArgc = 0;
    char** newArgv = malloc(sizeof(char*) * (size_t)argc);
    if (newArgv == NULL) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    newArgv[newArgc++] = argv[0];

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--log-file") == 0 && i + 1 < argc) {
            logFile = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--nabto-log-level") == 0 && i + 1 < argc) {
            nabtoLogLevel = argv[i + 1];
            i++;
        } else {
            newArgv[newArgc++] = argv[i];
        }
    }

    /* Open log file if requested */
    if (logFile != NULL) {
        if (!tmuxremote_log_open(logFile)) {
            free(newArgv);
            return 1;
        }
    }
    g_nabto_log_level = nabtoLogLevel;

    int ret = 1;

    if (newArgc < 2) {
        print_help();
        ret = 1;
    } else {
        const char* command = newArgv[1];

        if (strcmp(command, "pair") == 0) {
            ret = tmuxremote_cmd_pair(newArgc - 1, newArgv + 1);
        } else if (strcmp(command, "attach") == 0 || strcmp(command, "a") == 0) {
            ret = tmuxremote_cmd_attach(newArgc - 1, newArgv + 1);
        } else if (strcmp(command, "create") == 0 ||
                   strcmp(command, "new") == 0 ||
                   strcmp(command, "new-session") == 0 ||
                   strcmp(command, "c") == 0 ||
                   strcmp(command, "n") == 0) {
            ret = tmuxremote_cmd_new_session(newArgc - 1, newArgv + 1);
        } else if (strcmp(command, "sessions") == 0) {
            ret = tmuxremote_cmd_sessions(newArgc - 1, newArgv + 1);
        } else if (strcmp(command, "agents") == 0 ||
                   strcmp(command, "devices") == 0) {
            ret = tmuxremote_cmd_devices(newArgc - 1, newArgv + 1);
        } else if (strcmp(command, "rename") == 0) {
            ret = tmuxremote_cmd_rename(newArgc - 1, newArgv + 1);
        } else if (strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
            print_help();
            ret = 0;
        } else if (strcmp(command, "--version") == 0 || strcmp(command, "-v") == 0) {
            printf("%s\n", TMUXREMOTE_CLIENT_VERSION);
            ret = 0;
        } else {
            printf("Unknown command: %s\n", command);
            print_help();
            ret = 1;
        }
    }

    tmuxremote_log_close();
    free(newArgv);
    return ret;
}

void print_help(void)
{
    printf("tmux-remote CLI Client v%s\n", TMUXREMOTE_CLIENT_VERSION);
    printf("\n");
    printf("Usage: tmux-remote [global options] <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  pair <pairing-string> [--force] One-time pairing with an agent\n");
    printf("  attach <agent> [session]       Attach to an existing tmux session (alias: a)\n");
    printf("  create <agent> [session]       Create a new session and attach (aliases: new, n, c)\n");
    printf("  sessions <agent>               List tmux sessions on an agent\n");
    printf("  agents                         List saved agents and their status\n");
    printf("  rename <current> <new-name>    Rename an agent bookmark\n");
    printf("\n");
    printf("Global options:\n");
    printf("  --log-file <path>              Write debug log to file\n");
    printf("  --nabto-log-level <level>      Nabto SDK log level (error, warn, info, debug, trace)\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help                     Show this help\n");
    printf("  -v, --version                  Show version\n");
}
