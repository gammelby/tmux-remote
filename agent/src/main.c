#include "tmuxremote.h"
#include "tmuxremote_info.h"
#include "tmuxremote_init.h"
#include "tmuxremote_banner.h"
#include "tmuxremote_device.h"
#include "tmuxremote_keychain.h"

#include <apps/common/device_config.h>
#include <apps/common/logging.h>

#include <nabto/nabto_device.h>

#include <gopt/gopt.h>

#include <modules/fs/posix/nm_fs_posix.h>

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define NEWLINE "\n"

bool tmuxremote_silent = false;

enum {
    OPTION_HELP = 1,
    OPTION_VERSION,
    OPTION_HOME_DIR,
    OPTION_LOG_LEVEL,
    OPTION_RANDOM_PORTS,
    OPTION_INIT,
    OPTION_DEMO_INIT,
    OPTION_ADD_USER,
    OPTION_REMOVE_USER,
    OPTION_PRODUCT_ID,
    OPTION_DEVICE_ID,
    OPTION_RECORD_PTY,
    OPTION_MOVE_DEVICE_KEY,
    OPTION_BACKGROUND,
    OPTION_SILENT,
    OPTION_LIST_USERS
};

static volatile sig_atomic_t signalCount = 0;
static NabtoDevice* globalDevice = NULL;

struct device_event_state {
    NabtoDevice* device;
    NabtoDeviceListener* listener;
    NabtoDeviceFuture* future;
    NabtoDeviceEvent event;
    atomic_bool active;
};

struct device_close_state {
    NabtoDeviceError ec;
    atomic_bool done;
};

struct args {
    bool showHelp;
    bool showVersion;
    char* homeDir;
    char* logLevel;
    bool randomPorts;
    bool init;
    bool demoInit;
    char* addUser;
    char* removeUser;
    char* productId;
    char* deviceId;
    char* recordPtyFile;
    char* moveDeviceKey;
    bool background;
    bool silent;
    bool listUsers;
};

static void args_init(struct args* args);
static void args_deinit(struct args* args);
static bool parse_args(int argc, char** argv, struct args* args);
static void signal_handler(int s);
static void start_wait_for_device_event(struct device_event_state* state);
static void device_event_callback(NabtoDeviceFuture* future, NabtoDeviceError ec,
                                  void* userData);
static void device_close_callback(NabtoDeviceFuture* future, NabtoDeviceError ec,
                                  void* userData);
static bool make_directory(const char* directory);
static bool make_directories(const char* homeDir);
static char* get_default_home_dir(void);
static bool run_agent(const struct args* args);
static bool load_pattern_configs(struct tmuxremote* app);
static bool load_default_pattern_config(struct tmuxremote* app);
static void print_help(void);
static int cmp_str_ptr(const void* a, const void* b);
static bool merge_agent_into_config(tmuxremote_pattern_config* merged,
                                    tmuxremote_agent_config* agent);
static bool has_duplicate_pattern_ids(const tmuxremote_agent_config* agent);

static const char* TMUXREMOTE_DEFAULT_PATTERN_CONFIG_JSON =
    "{\n"
    "  \"version\": 3,\n"
    "  \"agents\": {\n"
    "    \"claude-code\": {\n"
    "      \"name\": \"Claude Code\",\n"
    "      \"rules\": [\n"
    "        {\n"
    "          \"id\": \"numbered_prompt\",\n"
    "          \"type\": \"numbered_menu\",\n"
    "          \"prompt_regex\": \"Do you want to .+\\\\?\",\n"
    "          \"option_regex\": \"^\\\\s*([0-9]+)\\\\.\\\\s+(.+)$\",\n"
    "          \"action_template\": { \"keys\": \"{number}\" },\n"
    "          \"max_scan_lines\": 8\n"
    "        },\n"
    "        {\n"
    "          \"id\": \"yes_no_prompt\",\n"
    "          \"type\": \"yes_no\",\n"
    "          \"prompt_regex\": \"(?:Allow|Proceed|Run|Execute).*\\\\? \\\\(y\\\\/n\\\\)\",\n"
    "          \"actions\": [\n"
    "            { \"label\": \"Allow\", \"keys\": \"y\" },\n"
    "            { \"label\": \"Deny\", \"keys\": \"n\" }\n"
    "          ],\n"
    "          \"max_scan_lines\": 4\n"
    "        },\n"
    "        {\n"
    "          \"id\": \"diff_review\",\n"
    "          \"type\": \"accept_reject\",\n"
    "          \"prompt_regex\": \"Do you want to apply these changes\",\n"
    "          \"actions\": [\n"
    "            { \"label\": \"Accept\", \"keys\": \"y\" },\n"
    "            { \"label\": \"Reject\", \"keys\": \"n\" }\n"
    "          ],\n"
    "          \"max_scan_lines\": 4\n"
    "        }\n"
    "      ]\n"
    "    },\n"
    "    \"codex\": {\n"
    "      \"name\": \"OpenAI Codex CLI\",\n"
    "      \"rules\": []\n"
    "    },\n"
    "    \"aider\": {\n"
    "      \"name\": \"Aider\",\n"
    "      \"rules\": []\n"
    "    }\n"
    "  }\n"
    "}\n";

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    bool status = true;
    struct args args;
    args_init(&args);

    if (!parse_args(argc, argv, &args)) {
        printf("Cannot parse arguments" NEWLINE);
        status = false;
    } else if (args.showHelp) {
        print_help();
    } else if (args.showVersion) {
        printf("%s" NEWLINE, TMUXREMOTE_VERSION);
    } else if (args.init || args.demoInit) {
        char* homeDir = args.homeDir ? args.homeDir : get_default_home_dir();
        if (homeDir == NULL) {
            printf("Cannot determine home directory" NEWLINE);
            status = false;
        } else {
            make_directories(homeDir);
            if (args.demoInit) {
                printf("--demo-init has been removed. Use --init (invite-only pairing)." NEWLINE);
                status = false;
            } else {
                status = tmuxremote_do_init(homeDir, args.productId, args.deviceId);
            }
            if (homeDir != args.homeDir) {
                free(homeDir);
            }
        }
    } else if (args.addUser != NULL) {
        char* homeDir = args.homeDir ? args.homeDir : get_default_home_dir();
        if (homeDir == NULL) {
            printf("Cannot determine home directory" NEWLINE);
            status = false;
        } else {
            status = tmuxremote_do_add_user(homeDir, args.addUser);
            if (homeDir != args.homeDir) {
                free(homeDir);
            }
        }
    } else if (args.removeUser != NULL) {
        char* homeDir = args.homeDir ? args.homeDir : get_default_home_dir();
        if (homeDir == NULL) {
            printf("Cannot determine home directory" NEWLINE);
            status = false;
        } else {
            status = tmuxremote_do_remove_user(homeDir, args.removeUser);
            if (homeDir != args.homeDir) {
                free(homeDir);
            }
        }
    } else if (args.listUsers) {
        char* homeDir = args.homeDir ? args.homeDir : get_default_home_dir();
        if (homeDir == NULL) {
            printf("Cannot determine home directory" NEWLINE);
            status = false;
        } else {
            status = tmuxremote_do_list_users(homeDir);
            if (homeDir != args.homeDir) {
                free(homeDir);
            }
        }
    } else if (args.moveDeviceKey != NULL) {
        char* homeDir = args.homeDir ? args.homeDir : get_default_home_dir();
        if (homeDir == NULL) {
            printf("Cannot determine home directory" NEWLINE);
            status = false;
        } else {
            status = tmuxremote_do_move_device_key(homeDir, args.moveDeviceKey);
            if (homeDir != args.homeDir) {
                free(homeDir);
            }
        }
    } else {
        char* homeDir = args.homeDir ? args.homeDir : get_default_home_dir();
        if (homeDir == NULL) {
            printf("Cannot determine home directory" NEWLINE);
            status = false;
        } else {
            make_directories(homeDir);
            /* Temporarily set homeDir in args for run_agent */
            char* saved = args.homeDir;
            args.homeDir = homeDir;
            status = run_agent(&args);
            args.homeDir = saved;
            if (homeDir != args.homeDir) {
                free(homeDir);
            }
        }
    }

    args_deinit(&args);
    return status ? 0 : 1;
}

bool run_agent(const struct args* args)
{
    tmuxremote_silent = args->silent;

    struct tmuxremote app;
    tmuxremote_init(&app);

    globalDevice = nabto_device_new();
    if (globalDevice == NULL) {
        printf("Failed to create Nabto device" NEWLINE);
        return false;
    }
    app.device = globalDevice;
    app.homeDir = strdup(args->homeDir);
    if (args->recordPtyFile) {
        app.recordPtyFile = strdup(args->recordPtyFile);
    }

    logging_init(app.device, &app.logger, args->logLevel);

    struct nm_fs fsImpl = nm_fs_posix_get_impl();

    /* Set up file paths */
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s/config/device.json", args->homeDir);
    app.deviceConfigFile = strdup(buffer);
    snprintf(buffer, sizeof(buffer), "%s/state/iam_state.json", args->homeDir);
    app.iamStateFile = strdup(buffer);

    /* Load pattern detection configs before starting listeners/device. */
    if (!load_pattern_configs(&app)) {
        tmuxremote_deinit(&app);
        nabto_device_free(globalDevice);
        globalDevice = NULL;
        return false;
    }

    /* Load device config */
    struct device_config deviceConfig;
    device_config_init(&deviceConfig);
    if (!load_device_config(&fsImpl, app.deviceConfigFile, &deviceConfig, &app.logger)) {
        printf("Device configuration not found (%s)." NEWLINE, app.deviceConfigFile);
        printf("Run tmux-remote-agent --init to create initial configuration." NEWLINE);
        tmuxremote_deinit(&app);
        nabto_device_free(globalDevice);
        globalDevice = NULL;
        return false;
    }

    bool useKeychain =
        tmuxremote_config_get_keychain_key(&fsImpl, app.deviceConfigFile);
    char legacyDeviceKeyFile[512];
    snprintf(legacyDeviceKeyFile, sizeof(legacyDeviceKeyFile),
             "%s/keys/device.key", args->homeDir);

    app.deviceKeyFile = tmuxremote_device_key_file_path(
        args->homeDir, deviceConfig.productId, deviceConfig.deviceId);
    if (app.deviceKeyFile == NULL) {
        printf("Could not allocate device key file path" NEWLINE);
        device_config_deinit(&deviceConfig);
        tmuxremote_deinit(&app);
        nabto_device_free(globalDevice);
        globalDevice = NULL;
        return false;
    }

    if (!tmuxremote_ensure_namespaced_key_file(&fsImpl, legacyDeviceKeyFile,
                                               app.deviceKeyFile))
    {
        printf("Warning: Could not prepare namespaced key file (%s)." NEWLINE,
               app.deviceKeyFile);
        printf("Falling back to legacy path: %s" NEWLINE, legacyDeviceKeyFile);
        free(app.deviceKeyFile);
        app.deviceKeyFile = strdup(legacyDeviceKeyFile);
        if (app.deviceKeyFile == NULL) {
            printf("Could not allocate legacy device key file path" NEWLINE);
            device_config_deinit(&deviceConfig);
            tmuxremote_deinit(&app);
            nabto_device_free(globalDevice);
            globalDevice = NULL;
            return false;
        }
    }

    nabto_device_set_product_id(app.device, deviceConfig.productId);
    nabto_device_set_device_id(app.device, deviceConfig.deviceId);

    if (deviceConfig.server != NULL) {
        nabto_device_set_server_url(app.device, deviceConfig.server);
    }
    if (deviceConfig.serverPort != 0) {
        nabto_device_set_server_port(app.device, deviceConfig.serverPort);
    }

    if (args->randomPorts) {
        nabto_device_set_local_port(app.device, 0);
        nabto_device_set_p2p_port(app.device, 0);
    }

    bool keychainUsed = false;
    if (!tmuxremote_load_or_create_private_key(app.device, &fsImpl,
                                               app.deviceKeyFile, &app.logger,
                                               deviceConfig.productId,
                                               deviceConfig.deviceId,
                                               useKeychain, &keychainUsed))
    {
        printf("Could not load or create the private key" NEWLINE);
        device_config_deinit(&deviceConfig);
        tmuxremote_deinit(&app);
        nabto_device_free(globalDevice);
        globalDevice = NULL;
        return false;
    }
    app.keychainKey = keychainUsed;

    nabto_device_set_app_name(app.device, "tmux-remote");
    nabto_device_set_app_version(app.device, TMUXREMOTE_VERSION);
    nabto_device_enable_mdns(app.device);
    nabto_device_mdns_add_subtype(app.device, "tmux-remote");

    /* Initialize IAM */
    tmuxremote_iam_init(&app.iam, app.device, &fsImpl, app.iamStateFile, &app.logger);
    if (!tmuxremote_iam_load_state(&app.iam)) {
        printf("Failed to load IAM state" NEWLINE);
        device_config_deinit(&deviceConfig);
        tmuxremote_deinit(&app);
        nabto_device_free(globalDevice);
        globalDevice = NULL;
        return false;
    }

    /* Start device */
    NabtoDeviceFuture* fut = nabto_device_future_new(app.device);
    nabto_device_start(app.device, fut);
    NabtoDeviceError ec = nabto_device_future_wait(fut);
    nabto_device_future_free(fut);

    if (ec != NABTO_DEVICE_EC_OK) {
        printf("Failed to start device: %s" NEWLINE, nabto_device_error_get_message(ec));
        device_config_deinit(&deviceConfig);
        tmuxremote_deinit(&app);
        nabto_device_free(globalDevice);
        globalDevice = NULL;
        return false;
    }

    app.startTime = time(NULL);

    /* Initialize CoAP handlers */
    tmuxremote_coap_resize_init(&app.coapResize, app.device, &app);
    tmuxremote_coap_sessions_init(&app.coapSessions, app.device, &app);
    tmuxremote_coap_attach_init(&app.coapAttach, app.device, &app);
    tmuxremote_coap_create_init(&app.coapCreate, app.device, &app);
    tmuxremote_coap_status_init(&app.coapStatus, app.device, &app);

    /* Initialize stream listeners */
    tmuxremote_stream_listener_init(&app.streamListener, app.device, &app);
    tmuxremote_control_stream_listener_init(&app.controlStreamListener, app.device, &app);

    /* Print banner */
    char* deviceFingerprint = NULL;
    nabto_device_get_device_fingerprint(app.device, &deviceFingerprint);

    tmuxremote_print_banner(&app, deviceFingerprint);
    nabto_device_string_free(deviceFingerprint);

    if (args->background) {
        char pidPath[512];
        char logPath[512];
        snprintf(pidPath, sizeof(pidPath), "%s/tmux-remote-agent.pid", app.homeDir);
        snprintf(logPath, sizeof(logPath), "%s/agent.log", app.homeDir);

        fflush(stdout);
        fflush(stderr);

        pid_t pid = fork();
        if (pid < 0) {
            printf("Failed to fork: %s" NEWLINE, strerror(errno));
            device_config_deinit(&deviceConfig);
            tmuxremote_deinit(&app);
            nabto_device_free(globalDevice);
            globalDevice = NULL;
            return false;
        } else if (pid > 0) {
            /* Parent: print info and exit */
            info_printf("Backgrounding (PID %d)" NEWLINE, pid);
            info_printf("  PID file: %s" NEWLINE, pidPath);
            info_printf("  Log file: %s" NEWLINE, logPath);
            _exit(0);
        }

        /* Child continues as daemon */
        setsid();

        /* Write PID file */
        FILE* pf = fopen(pidPath, "w");
        if (pf != NULL) {
            fprintf(pf, "%d\n", getpid());
            fclose(pf);
        }

        /* Redirect stdout/stderr to log file, close stdin */
        FILE* lf = fopen(logPath, "a");
        if (lf != NULL) {
            dup2(fileno(lf), STDOUT_FILENO);
            dup2(fileno(lf), STDERR_FILENO);
            fclose(lf);
        }
        close(STDIN_FILENO);

        /* Re-apply unbuffered mode after fd swap and write startup marker */
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        info_printf("Agent started (PID %d)" NEWLINE, getpid());
    }

    /* Device event listener and signal handling.
       The SIGINT handler only sets flags; all SDK calls happen here. */
    signal(SIGINT, &signal_handler);
    struct device_event_state eventState;
    memset(&eventState, 0, sizeof(eventState));
    eventState.device = app.device;
    eventState.listener = nabto_device_listener_new(app.device);
    eventState.future = nabto_device_future_new(app.device);
    atomic_init(&eventState.active, true);
    nabto_device_device_events_init_listener(app.device, eventState.listener);
    start_wait_for_device_event(&eventState);

    while (signalCount == 0 && atomic_load(&eventState.active)) {
        usleep(100 * 1000);
    }

    if (signalCount > 0) {
        info_printf("\rCaught signal %d" NEWLINE, SIGINT);
    }

    /* Shutdown */
    nabto_device_listener_stop(eventState.listener);
    tmuxremote_coap_handler_stop(&app.coapResize);
    tmuxremote_coap_handler_stop(&app.coapSessions);
    tmuxremote_coap_handler_stop(&app.coapAttach);
    tmuxremote_coap_handler_stop(&app.coapCreate);
    tmuxremote_coap_handler_stop(&app.coapStatus);
    tmuxremote_stream_listener_stop(&app.streamListener);
    tmuxremote_control_stream_listener_stop(&app.controlStreamListener);

    struct device_close_state closeState;
    closeState.ec = NABTO_DEVICE_EC_OK;
    atomic_init(&closeState.done, false);

    fut = nabto_device_future_new(app.device);
    nabto_device_close(app.device, fut);
    nabto_device_future_set_callback(fut, device_close_callback, &closeState);

    int closeWaitIterations = 0;
    while (!atomic_load(&closeState.done) && signalCount < 2 && closeWaitIterations < 50) {
        usleep(100 * 1000);
        closeWaitIterations++;
    }

    nabto_device_stop(app.device);

    nabto_device_future_free(eventState.future);
    nabto_device_listener_free(eventState.listener);

    tmuxremote_coap_handler_deinit(&app.coapResize);
    tmuxremote_coap_handler_deinit(&app.coapSessions);
    tmuxremote_coap_handler_deinit(&app.coapAttach);
    tmuxremote_coap_handler_deinit(&app.coapCreate);
    tmuxremote_coap_handler_deinit(&app.coapStatus);
    /* Shutdown ordering: join the control monitor thread first (it calls
     * copy_active_prompt_for_ref which locks activeStreamsMutex), then join
     * PTY reader threads (they call send_prompt_*_for_ref which locks
     * streamListMutex), then destroy both listeners and their mutexes. */
    tmuxremote_control_stream_listener_join_monitor(&app.controlStreamListener);
    tmuxremote_stream_listener_deinit(&app.streamListener);
    tmuxremote_control_stream_listener_deinit(&app.controlStreamListener);

    if (args->background) {
        char pidPath[512];
        snprintf(pidPath, sizeof(pidPath), "%s/tmux-remote-agent.pid", app.homeDir);
        unlink(pidPath);
    }

    device_config_deinit(&deviceConfig);
    tmuxremote_iam_deinit(&app.iam);
    tmuxremote_deinit(&app);
    nabto_device_free(globalDevice);
    globalDevice = NULL;

    return true;
}

void args_init(struct args* args)
{
    memset(args, 0, sizeof(struct args));
}

void args_deinit(struct args* args)
{
    free(args->homeDir);
    free(args->logLevel);
    free(args->addUser);
    free(args->removeUser);
    free(args->productId);
    free(args->deviceId);
    free(args->recordPtyFile);
    free(args->moveDeviceKey);
}

bool parse_args(int argc, char** argv, struct args* args)
{
    const char x1s[] = "h";  const char* x1l[] = { "help", 0 };
    const char x2s[] = "v";  const char* x2l[] = { "version", 0 };
    const char x3s[] = "H";  const char* x3l[] = { "home-dir", 0 };
    const char x4s[] = "";   const char* x4l[] = { "log-level", 0 };
    const char x5s[] = "";   const char* x5l[] = { "random-ports", 0 };
    const char x6s[] = "";   const char* x6l[] = { "init", 0 };
    const char x7s[] = "";   const char* x7l[] = { "demo-init", 0 };
    const char x8s[] = "";   const char* x8l[] = { "add-user", 0 };
    const char x9s[] = "";   const char* x9l[] = { "remove-user", 0 };
    const char x10s[] = "p"; const char* x10l[] = { "product-id", 0 };
    const char x11s[] = "d"; const char* x11l[] = { "device-id", 0 };
    const char x12s[] = "";  const char* x12l[] = { "record-pty", 0 };
    const char x13s[] = "";  const char* x13l[] = { "move-device-key", 0 };
    const char x14s[] = "b"; const char* x14l[] = { "background", 0 };
    const char x15s[] = "s"; const char* x15l[] = { "silent", 0 };
    const char x16s[] = "";  const char* x16l[] = { "list-users", 0 };

    const struct { int k; int f; const char* s; const char* const* l; } opts[] = {
        { OPTION_HELP,        GOPT_NOARG, x1s,  x1l },
        { OPTION_VERSION,     GOPT_NOARG, x2s,  x2l },
        { OPTION_HOME_DIR,    GOPT_ARG,   x3s,  x3l },
        { OPTION_LOG_LEVEL,   GOPT_ARG,   x4s,  x4l },
        { OPTION_RANDOM_PORTS,GOPT_NOARG, x5s,  x5l },
        { OPTION_INIT,        GOPT_NOARG, x6s,  x6l },
        { OPTION_DEMO_INIT,   GOPT_NOARG, x7s,  x7l },
        { OPTION_ADD_USER,    GOPT_ARG,   x8s,  x8l },
        { OPTION_REMOVE_USER, GOPT_ARG,   x9s,  x9l },
        { OPTION_PRODUCT_ID,  GOPT_ARG,   x10s, x10l },
        { OPTION_DEVICE_ID,   GOPT_ARG,   x11s, x11l },
        { OPTION_RECORD_PTY,  GOPT_ARG,   x12s, x12l },
        { OPTION_MOVE_DEVICE_KEY, GOPT_ARG, x13s, x13l },
        { OPTION_BACKGROUND,  GOPT_NOARG, x14s, x14l },
        { OPTION_SILENT,      GOPT_NOARG, x15s, x15l },
        { OPTION_LIST_USERS,  GOPT_NOARG, x16s, x16l },
        {0, 0, 0, 0}
    };

    void* options = gopt_sort(&argc, (const char**)argv, opts);

    if (gopt(options, OPTION_HELP)) {
        args->showHelp = true;
    }
    if (gopt(options, OPTION_VERSION)) {
        args->showVersion = true;
    }
    if (gopt(options, OPTION_RANDOM_PORTS)) {
        args->randomPorts = true;
    }
    if (gopt(options, OPTION_INIT)) {
        args->init = true;
    }
    if (gopt(options, OPTION_DEMO_INIT)) {
        args->demoInit = true;
    }
    if (gopt(options, OPTION_BACKGROUND)) {
        args->background = true;
    }
    if (gopt(options, OPTION_SILENT)) {
        args->silent = true;
    }
    if (gopt(options, OPTION_LIST_USERS)) {
        args->listUsers = true;
    }

    const char* tmp = NULL;
    if (gopt_arg(options, OPTION_LOG_LEVEL, &tmp)) {
        args->logLevel = strdup(tmp);
    } else {
        args->logLevel = strdup("error");
    }
    if (gopt_arg(options, OPTION_HOME_DIR, &tmp)) {
        args->homeDir = strdup(tmp);
    }
    if (gopt_arg(options, OPTION_ADD_USER, &tmp)) {
        args->addUser = strdup(tmp);
    }
    if (gopt_arg(options, OPTION_REMOVE_USER, &tmp)) {
        args->removeUser = strdup(tmp);
    }
    if (gopt_arg(options, OPTION_PRODUCT_ID, &tmp)) {
        args->productId = strdup(tmp);
    }
    if (gopt_arg(options, OPTION_DEVICE_ID, &tmp)) {
        args->deviceId = strdup(tmp);
    }
    if (gopt_arg(options, OPTION_RECORD_PTY, &tmp)) {
        args->recordPtyFile = strdup(tmp);
    }
    if (gopt_arg(options, OPTION_MOVE_DEVICE_KEY, &tmp)) {
        args->moveDeviceKey = strdup(tmp);
    }

    gopt_free(options);
    return true;
}

static void signal_handler(int s)
{
    (void)s;
    if (signalCount < 2) {
        signalCount++;
    }
}

static void start_wait_for_device_event(struct device_event_state* state)
{
    if (!atomic_load(&state->active)) {
        return;
    }
    nabto_device_listener_device_event(state->listener, state->future, &state->event);
    nabto_device_future_set_callback(state->future, device_event_callback, state);
}

static void device_event_callback(NabtoDeviceFuture* future, NabtoDeviceError ec,
                                  void* userData)
{
    (void)future;
    struct device_event_state* state = userData;

    if (ec != NABTO_DEVICE_EC_OK) {
        atomic_store(&state->active, false);
        return;
    }


    if (state->event == NABTO_DEVICE_EVENT_CLOSED) {
        atomic_store(&state->active, false);
        return;
    } else if (state->event == NABTO_DEVICE_EVENT_ATTACHED) {
        info_printf("Attached to the basestation" NEWLINE);
    } else if (state->event == NABTO_DEVICE_EVENT_DETACHED) {
        info_printf("Detached from the basestation" NEWLINE);
    } else if (state->event == NABTO_DEVICE_EVENT_UNKNOWN_FINGERPRINT) {
        printf("The device fingerprint is not known by the basestation" NEWLINE);
    } else if (state->event == NABTO_DEVICE_EVENT_WRONG_PRODUCT_ID) {
        printf("The provided Product ID did not match the fingerprint" NEWLINE);
    } else if (state->event == NABTO_DEVICE_EVENT_WRONG_DEVICE_ID) {
        printf("The provided Device ID did not match the fingerprint" NEWLINE);
    }

    start_wait_for_device_event(state);
}

static void device_close_callback(NabtoDeviceFuture* future, NabtoDeviceError ec,
                                  void* userData)
{
    struct device_close_state* closeState = userData;
    closeState->ec = ec;
    atomic_store(&closeState->done, true);
    nabto_device_future_free(future);
}

bool make_directory(const char* directory)
{
    mkdir(directory, 0700);
    return true;
}

bool make_directories(const char* homeDir)
{
    char buffer[512];

    make_directory(homeDir);
    snprintf(buffer, sizeof(buffer), "%s/config", homeDir);
    make_directory(buffer);
    snprintf(buffer, sizeof(buffer), "%s/state", homeDir);
    make_directory(buffer);
    snprintf(buffer, sizeof(buffer), "%s/keys", homeDir);
    make_directory(buffer);
    snprintf(buffer, sizeof(buffer), "%s/patterns", homeDir);
    make_directory(buffer);
    return true;
}

char* get_default_home_dir(void)
{
    const char* home = getenv("HOME");
    if (home == NULL) {
        return NULL;
    }
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s/.tmux-remote", home);
    return strdup(buffer);
}

/*
 * Load pattern config files from ~/.tmux-remote/patterns/ (all .json files).
 */
bool load_pattern_configs(struct tmuxremote* app)
{
    char dirPath[512];
    snprintf(dirPath, sizeof(dirPath), "%s/patterns", app->homeDir);

    DIR* dir = opendir(dirPath);
    if (dir == NULL) {
        return load_default_pattern_config(app);
    }

    if (app->patternConfig != NULL) {
        tmuxremote_pattern_config_free(app->patternConfig);
        app->patternConfig = NULL;
    }

    char** fileNames = NULL;
    size_t fileCount = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t nameLen = strlen(entry->d_name);
        if (nameLen < 6 || strcmp(entry->d_name + nameLen - 5, ".json") != 0) {
            continue;
        }

        char** newFiles = realloc(fileNames, sizeof(char*) * (fileCount + 1));
        if (newFiles == NULL) {
            continue;
        }
        fileNames = newFiles;
        fileNames[fileCount] = strdup(entry->d_name);
        if (fileNames[fileCount] == NULL) {
            continue;
        }
        fileCount++;
    }
    closedir(dir);

    if (fileCount == 0) {
        free(fileNames);
        return load_default_pattern_config(app);
    }

    qsort(fileNames, fileCount, sizeof(char*), cmp_str_ptr);

    tmuxremote_pattern_config* merged = calloc(1, sizeof(tmuxremote_pattern_config));
    if (merged == NULL) {
        for (size_t i = 0; i < fileCount; i++) {
            free(fileNames[i]);
        }
        free(fileNames);
        printf("Error: out of memory while loading pattern configs" NEWLINE);
        return false;
    }

    bool versionSet = false;
    bool ok = true;
    for (size_t i = 0; i < fileCount; i++) {
        char filePath[512];
        snprintf(filePath, sizeof(filePath), "%s/%s", dirPath, fileNames[i]);

        FILE* f = fopen(filePath, "r");
        if (f == NULL) {
            printf("Error: failed to open pattern config %s" NEWLINE, filePath);
            ok = false;
            break;
        }

        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (fsize <= 0 || fsize > 1024 * 1024) {
            fclose(f);
            printf("Error: invalid pattern config size in %s" NEWLINE, filePath);
            ok = false;
            break;
        }

        char* json = malloc(fsize + 1);
        if (json == NULL) {
            fclose(f);
            printf("Error: out of memory while reading pattern config %s" NEWLINE,
                   filePath);
            ok = false;
            break;
        }
        size_t readLen = fread(json, 1, fsize, f);
        fclose(f);
        if (readLen != (size_t)fsize) {
            free(json);
            printf("Error: failed to read pattern config %s" NEWLINE, filePath);
            ok = false;
            break;
        }
        json[readLen] = '\0';

        tmuxremote_pattern_config* cfg = tmuxremote_pattern_config_parse(json, readLen);
        free(json);

        if (cfg == NULL) {
            printf("Error: failed to parse pattern config %s" NEWLINE, filePath);
            ok = false;
            break;
        }

        if (!versionSet) {
            merged->version = cfg->version;
            versionSet = true;
        } else if (cfg->version != merged->version) {
            printf("Error: pattern config version mismatch in %s (expected %d, got %d)" NEWLINE,
                   filePath, merged->version, cfg->version);
            tmuxremote_pattern_config_free(cfg);
            ok = false;
            break;
        }

        for (int ai = 0; ai < cfg->agent_count; ai++) {
            tmuxremote_agent_config* agent = &cfg->agents[ai];

            if (has_duplicate_pattern_ids(agent)) {
                printf("Error: duplicate pattern ids for agent '%s' in %s" NEWLINE,
                       agent->id ? agent->id : "(null)", filePath);
                ok = false;
                break;
            }

            if (!merge_agent_into_config(merged, agent)) {
                printf("Error: duplicate or invalid agent '%s' in %s" NEWLINE,
                       agent->id ? agent->id : "(null)", filePath);
                ok = false;
                break;
            }
        }

        tmuxremote_pattern_config_free(cfg);
        if (!ok) {
            break;
        }
    }

    for (size_t i = 0; i < fileCount; i++) {
        free(fileNames[i]);
    }
    free(fileNames);

    if (!ok) {
        tmuxremote_pattern_config_free(merged);
        return false;
    }

    if (merged->agent_count > 0) {
        app->patternConfig = merged;
        info_printf("Pattern config loaded (%d agents), activates per-session" NEWLINE,
                    merged->agent_count);
    } else {
        tmuxremote_pattern_config_free(merged);
        printf("Error: no valid agents loaded from pattern config files" NEWLINE);
        return false;
    }

    return true;
}

static bool load_default_pattern_config(struct tmuxremote* app)
{
    if (app->patternConfig != NULL) {
        tmuxremote_pattern_config_free(app->patternConfig);
        app->patternConfig = NULL;
    }

    size_t jsonLen = strlen(TMUXREMOTE_DEFAULT_PATTERN_CONFIG_JSON);
    tmuxremote_pattern_config* cfg =
        tmuxremote_pattern_config_parse(TMUXREMOTE_DEFAULT_PATTERN_CONFIG_JSON,
                                        jsonLen);
    if (cfg == NULL || cfg->agent_count <= 0) {
        if (cfg != NULL) {
            tmuxremote_pattern_config_free(cfg);
        }
        printf("Error: failed to parse embedded default pattern config" NEWLINE);
        return false;
    }

    app->patternConfig = cfg;
    info_printf("Pattern config loaded from embedded defaults (%d agents)" NEWLINE,
                cfg->agent_count);
    return true;
}

static int cmp_str_ptr(const void* a, const void* b)
{
    const char* const* sa = a;
    const char* const* sb = b;
    return strcmp(*sa, *sb);
}

static bool merge_agent_into_config(tmuxremote_pattern_config* merged,
                                    tmuxremote_agent_config* agent)
{
    if (merged == NULL || agent == NULL || agent->id == NULL) {
        return false;
    }

    for (int i = 0; i < merged->agent_count; i++) {
        if (merged->agents[i].id != NULL &&
            strcmp(merged->agents[i].id, agent->id) == 0) {
            return false;
        }
    }

    tmuxremote_agent_config* newAgents = realloc(
        merged->agents, sizeof(tmuxremote_agent_config) * (merged->agent_count + 1));
    if (newAgents == NULL) {
        return false;
    }
    merged->agents = newAgents;
    merged->agents[merged->agent_count] = *agent;
    merged->agent_count++;

    memset(agent, 0, sizeof(*agent));
    return true;
}

static bool has_duplicate_pattern_ids(const tmuxremote_agent_config* agent)
{
    if (agent == NULL) {
        return true;
    }
    for (int i = 0; i < agent->pattern_count; i++) {
        const char* id = agent->patterns[i].id;
        if (id == NULL) {
            return true;
        }
        for (int j = i + 1; j < agent->pattern_count; j++) {
            if (agent->patterns[j].id != NULL &&
                strcmp(id, agent->patterns[j].id) == 0) {
                return true;
            }
        }
    }
    return false;
}

void print_help(void)
{
    printf("tmux-remote agent v%s" NEWLINE, TMUXREMOTE_VERSION);
    printf(NEWLINE);
    printf("Usage: tmux-remote-agent [options]" NEWLINE);
    printf(NEWLINE);
    printf("Options:" NEWLINE);
    printf("  -h, --help                Show this help" NEWLINE);
    printf("  -v, --version             Show version" NEWLINE);
    printf("  -H, --home-dir <dir>      Home directory (default: ~/.tmux-remote/)" NEWLINE);
    printf("      --init                Initialize configuration" NEWLINE);
    printf("      --demo-init           Removed (invite-only pairing enforced)" NEWLINE);
    printf("      --add-user <name>     Create a pairing invitation for a new user" NEWLINE);
    printf("      --remove-user <name>  Revoke access for a user" NEWLINE);
    printf("      --list-users          List all users (paired and pending)" NEWLINE);
    printf("      --move-device-key <filesystem|keychain>" NEWLINE);
    printf("                            Move device private key between storage backends" NEWLINE);
    printf("  -p, --product-id <id>     Product ID (used with --init)" NEWLINE);
    printf("  -d, --device-id <id>      Device ID (used with --init)" NEWLINE);
    printf("      --log-level <level>   Log level (error|info|trace|debug)" NEWLINE);
    printf("      --random-ports        Use random ports" NEWLINE);
    printf("      --record-pty <path>   Record raw PTY data to file" NEWLINE);
    printf("  -b, --background          Run in background (daemon mode)" NEWLINE);
    printf("  -s, --silent              Suppress informational output (errors only)" NEWLINE);
}
