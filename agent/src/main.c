#include "nabtoshell.h"
#include "nabtoshell_init.h"
#include "nabtoshell_banner.h"
#include "nabtoshell_device.h"

#include <apps/common/device_config.h>
#include <apps/common/logging.h>
#include <apps/common/private_key.h>
#include <apps/common/string_file.h>

#include <nabto/nabto_device.h>

#include <gopt/gopt.h>

#include <modules/fs/posix/nm_fs_posix.h>

#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>

#define NEWLINE "\n"

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
    OPTION_DEVICE_ID
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
static void load_pattern_configs(struct nabtoshell* app);
static void print_help(void);

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
        printf("%s" NEWLINE, NABTOSHELL_VERSION);
    } else if (args.init || args.demoInit) {
        char* homeDir = args.homeDir ? args.homeDir : get_default_home_dir();
        if (homeDir == NULL) {
            printf("Cannot determine home directory" NEWLINE);
            status = false;
        } else {
            make_directories(homeDir);
            if (args.demoInit) {
                status = nabtoshell_do_demo_init(homeDir, args.productId, args.deviceId);
            } else {
                status = nabtoshell_do_init(homeDir, args.productId, args.deviceId);
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
            status = nabtoshell_do_add_user(homeDir, args.addUser);
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
            status = nabtoshell_do_remove_user(homeDir, args.removeUser);
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
    struct nabtoshell app;
    nabtoshell_init(&app);

    globalDevice = nabto_device_new();
    if (globalDevice == NULL) {
        printf("Failed to create Nabto device" NEWLINE);
        return false;
    }
    app.device = globalDevice;
    app.homeDir = strdup(args->homeDir);

    logging_init(app.device, &app.logger, args->logLevel);

    struct nm_fs fsImpl = nm_fs_posix_get_impl();

    /* Set up file paths */
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s/config/device.json", args->homeDir);
    app.deviceConfigFile = strdup(buffer);
    snprintf(buffer, sizeof(buffer), "%s/keys/device.key", args->homeDir);
    app.deviceKeyFile = strdup(buffer);
    snprintf(buffer, sizeof(buffer), "%s/state/iam_state.json", args->homeDir);
    app.iamStateFile = strdup(buffer);

    /* Load device config */
    struct device_config deviceConfig;
    device_config_init(&deviceConfig);
    if (!load_device_config(&fsImpl, app.deviceConfigFile, &deviceConfig, &app.logger)) {
        printf("Device configuration not found (%s)." NEWLINE, app.deviceConfigFile);
        printf("Run nabtoshell-agent --init to create initial configuration." NEWLINE);
        nabtoshell_deinit(&app);
        nabto_device_free(globalDevice);
        globalDevice = NULL;
        return false;
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

    if (!load_or_create_private_key(app.device, &fsImpl, app.deviceKeyFile, &app.logger)) {
        printf("Could not load or create the private key" NEWLINE);
        device_config_deinit(&deviceConfig);
        nabtoshell_deinit(&app);
        nabto_device_free(globalDevice);
        globalDevice = NULL;
        return false;
    }

    nabto_device_set_app_name(app.device, "NabtoShell");
    nabto_device_set_app_version(app.device, NABTOSHELL_VERSION);
    nabto_device_enable_mdns(app.device);
    nabto_device_mdns_add_subtype(app.device, "nabtoshell");

    /* Initialize IAM */
    nabtoshell_iam_init(&app.iam, app.device, &fsImpl, app.iamStateFile, &app.logger);
    if (!nabtoshell_iam_load_state(&app.iam)) {
        printf("Failed to load IAM state" NEWLINE);
        device_config_deinit(&deviceConfig);
        nabtoshell_deinit(&app);
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
        nabtoshell_deinit(&app);
        nabto_device_free(globalDevice);
        globalDevice = NULL;
        return false;
    }

    app.startTime = time(NULL);

    /* Initialize CoAP handlers */
    nabtoshell_coap_resize_init(&app.coapResize, app.device, &app);
    nabtoshell_coap_sessions_init(&app.coapSessions, app.device, &app);
    nabtoshell_coap_attach_init(&app.coapAttach, app.device, &app);
    nabtoshell_coap_create_init(&app.coapCreate, app.device, &app);
    nabtoshell_coap_status_init(&app.coapStatus, app.device, &app);

    /* Initialize stream listeners */
    nabtoshell_stream_listener_init(&app.streamListener, app.device, &app);
    nabtoshell_control_stream_listener_init(&app.controlStreamListener, app.device, &app);

    /* Load pattern detection configs (activates per-stream on connect) */
    load_pattern_configs(&app);

    /* Print banner */
    char* deviceFingerprint = NULL;
    nabto_device_get_device_fingerprint(app.device, &deviceFingerprint);

    char* pairingString = nabtoshell_iam_create_pairing_string(
        &app.iam.iam,
        nabto_device_get_product_id(app.device),
        nabto_device_get_device_id(app.device));

    nabtoshell_print_banner(&app, deviceFingerprint, pairingString);

    free(pairingString);
    nabto_device_string_free(deviceFingerprint);

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
        printf("\rCaught signal %d" NEWLINE, SIGINT);
    }

    /* Shutdown */
    nabto_device_listener_stop(eventState.listener);
    nabtoshell_coap_handler_stop(&app.coapResize);
    nabtoshell_coap_handler_stop(&app.coapSessions);
    nabtoshell_coap_handler_stop(&app.coapAttach);
    nabtoshell_coap_handler_stop(&app.coapCreate);
    nabtoshell_coap_handler_stop(&app.coapStatus);
    nabtoshell_stream_listener_stop(&app.streamListener);
    nabtoshell_control_stream_listener_stop(&app.controlStreamListener);

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

    nabtoshell_coap_handler_deinit(&app.coapResize);
    nabtoshell_coap_handler_deinit(&app.coapSessions);
    nabtoshell_coap_handler_deinit(&app.coapAttach);
    nabtoshell_coap_handler_deinit(&app.coapCreate);
    nabtoshell_coap_handler_deinit(&app.coapStatus);
    /* Shutdown ordering: join the control monitor thread first (it calls
     * copy_active_match_for_ref which locks activeStreamsMutex), then join
     * PTY reader threads (they call send_pattern_*_for_ref which locks
     * streamListMutex), then destroy both listeners and their mutexes. */
    nabtoshell_control_stream_listener_join_monitor(&app.controlStreamListener);
    nabtoshell_stream_listener_deinit(&app.streamListener);
    nabtoshell_control_stream_listener_deinit(&app.controlStreamListener);

    device_config_deinit(&deviceConfig);
    nabtoshell_iam_deinit(&app.iam);
    nabtoshell_deinit(&app);
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
        printf("Attached to the basestation" NEWLINE);
    } else if (state->event == NABTO_DEVICE_EVENT_DETACHED) {
        printf("Detached from the basestation" NEWLINE);
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
    snprintf(buffer, sizeof(buffer), "%s/.nabtoshell", home);
    return strdup(buffer);
}

/*
 * Load pattern config files from ~/.nabtoshell/patterns/*.json.
 */
void load_pattern_configs(struct nabtoshell* app)
{
    char dirPath[512];
    snprintf(dirPath, sizeof(dirPath), "%s/patterns", app->homeDir);

    DIR* dir = opendir(dirPath);
    if (dir == NULL) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t nameLen = strlen(entry->d_name);
        if (nameLen < 6 || strcmp(entry->d_name + nameLen - 5, ".json") != 0) {
            continue;
        }

        char filePath[512];
        snprintf(filePath, sizeof(filePath), "%s/%s", dirPath, entry->d_name);

        FILE* f = fopen(filePath, "r");
        if (f == NULL) {
            continue;
        }

        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (fsize <= 0 || fsize > 1024 * 1024) {
            fclose(f);
            continue;
        }

        char* json = malloc(fsize + 1);
        size_t readLen = fread(json, 1, fsize, f);
        fclose(f);
        json[readLen] = '\0';

        nabtoshell_pattern_config* cfg = nabtoshell_pattern_config_parse(json, readLen);
        free(json);

        if (cfg == NULL) {
            printf("Warning: failed to parse pattern config %s" NEWLINE, filePath);
            continue;
        }

        if (app->patternConfig != NULL) {
            nabtoshell_pattern_config_free(app->patternConfig);
        }
        app->patternConfig = cfg;
        printf("Loaded pattern config: %s (%d agents)" NEWLINE,
               entry->d_name, cfg->agent_count);
    }

    closedir(dir);

    if (app->patternConfig != NULL) {
        printf("Pattern config loaded (%d agents), activates per-session" NEWLINE,
               app->patternConfig->agent_count);
    }
}

void print_help(void)
{
    printf("NabtoShell Agent v%s" NEWLINE, NABTOSHELL_VERSION);
    printf(NEWLINE);
    printf("Usage: nabtoshell-agent [options]" NEWLINE);
    printf(NEWLINE);
    printf("Options:" NEWLINE);
    printf("  -h, --help                Show this help" NEWLINE);
    printf("  -v, --version             Show version" NEWLINE);
    printf("  -H, --home-dir <dir>      Home directory (default: ~/.nabtoshell/)" NEWLINE);
    printf("      --init                Initialize configuration" NEWLINE);
    printf("      --demo-init           Initialize with open pairing (for demos)" NEWLINE);
    printf("      --add-user <name>     Create a pairing invitation for a new user" NEWLINE);
    printf("      --remove-user <name>  Revoke access for a user" NEWLINE);
    printf("  -p, --product-id <id>     Product ID (used with --init)" NEWLINE);
    printf("  -d, --device-id <id>      Device ID (used with --init)" NEWLINE);
    printf("      --log-level <level>   Log level (error|info|trace|debug)" NEWLINE);
    printf("      --random-ports        Use random ports" NEWLINE);
}
