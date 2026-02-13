#include "nabtoshell_attach.h"
#include "nabtoshell_client.h"
#include "nabtoshell_client_config.h"
#include "nabtoshell_client_util.h"
#include "nabtoshell_client_coap.h"

#include <nabto/nabto_client.h>

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define READ_BUFFER_SIZE 4096

struct reader_thread_ctx {
    NabtoClient* client;
    NabtoClientStream* stream;
    atomic_bool* done;
};

static volatile sig_atomic_t sigwinch_received = 0;
static bool write_all_fd(int fd, const uint8_t* data, size_t len);

static void sigwinch_handler(int sig)
{
    (void)sig;
    sigwinch_received = 1;
}

static void* stream_reader_thread(void* arg)
{
    struct reader_thread_ctx* ctx = arg;
    uint8_t buf[READ_BUFFER_SIZE];
    size_t readLen = 0;

    while (!atomic_load(ctx->done)) {
        NabtoClientFuture* fut = nabto_client_future_new(ctx->client);
        nabto_client_stream_read_some(ctx->stream, fut, buf, sizeof(buf), &readLen);
        NabtoClientError ec = nabto_client_future_wait(fut);
        nabto_client_future_free(fut);

        if (ec == NABTO_CLIENT_EC_EOF || ec != NABTO_CLIENT_EC_OK) {
            break;
        }

        if (readLen > 0) {
            if (!write_all_fd(STDOUT_FILENO, buf, readLen)) {
                break;
            }
        }
    }

    atomic_store(ctx->done, true);
    return NULL;
}

static bool write_all_fd(int fd, const uint8_t* data, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t written = write(fd, data + total, len - total);
        if (written > 0) {
            total += (size_t)written;
            continue;
        }
        if (written < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        return false;
    }
    return true;
}

/* Shared stream relay loop: open stream, set raw mode, relay stdin<->stream.
   conn and client must be a live connection with a session already attached. */
static int run_stream_relay(NabtoClientConnection* conn, NabtoClient* client,
                            struct nabtoshell_client_config* config)
{
    int ret = 1;

    /* Open stream on port 1 */
    NabtoClientStream* stream = nabto_client_stream_new(conn);
    if (stream == NULL) {
        printf("Failed to create stream\n");
        return 1;
    }

    NabtoClientFuture* future = nabto_client_future_new(client);
    nabto_client_stream_open(stream, future, 1);
    NabtoClientError ec = nabto_client_future_wait(future);
    nabto_client_future_free(future);

    if (ec != NABTO_CLIENT_EC_OK) {
        printf("Failed to open stream: %s\n", nabto_client_error_get_message(ec));
        nabto_client_stream_free(stream);
        return 1;
    }

    /* Set terminal to raw mode */
    struct termios savedTermios;
    if (!nabtoshell_terminal_set_raw(&savedTermios)) {
        printf("Failed to set terminal to raw mode\n");
        nabto_client_stream_free(stream);
        return 1;
    }

    /* Install SIGWINCH handler */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigwinch_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, NULL);

    /* Start reader thread (stream -> stdout) */
    atomic_bool done = false;
    struct reader_thread_ctx readerCtx = {
        .client = client,
        .stream = stream,
        .done = &done,
    };

    pthread_t readerThread;
    if (pthread_create(&readerThread, NULL, stream_reader_thread, &readerCtx) != 0) {
        nabtoshell_terminal_restore(&savedTermios);
        printf("Failed to create reader thread\n");
        nabto_client_stream_free(stream);
        return 1;
    }

    /* Main loop: read stdin, send to stream, handle SIGWINCH */
    uint8_t inputBuf[READ_BUFFER_SIZE];
    ret = 0;

    while (!atomic_load(&done)) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; /* 100ms timeout */

        int selret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);

        /* Check for SIGWINCH */
        if (sigwinch_received) {
            sigwinch_received = 0;
            uint16_t newCols, newRows;
            if (nabtoshell_terminal_get_size(&newCols, &newRows)) {
                nabtoshell_coap_resize(conn, client, newCols, newRows);
            }
        }

        if (selret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
            ssize_t n = read(STDIN_FILENO, inputBuf, sizeof(inputBuf));
            if (n <= 0) {
                break;
            }

            future = nabto_client_future_new(client);
            nabto_client_stream_write(stream, future, inputBuf, (size_t)n);
            ec = nabto_client_future_wait(future);
            nabto_client_future_free(future);

            if (ec != NABTO_CLIENT_EC_OK) {
                break;
            }
        }
    }

    /* Cleanup */
    atomic_store(&done, true);

    /* Close stream */
    future = nabto_client_future_new(client);
    nabto_client_stream_close(stream, future);
    nabto_client_future_wait(future);
    nabto_client_future_free(future);

    pthread_join(readerThread, NULL);

    /* Restore terminal */
    nabtoshell_terminal_restore(&savedTermios);

    nabto_client_stream_free(stream);

    return ret;
}

/* Connect to a device by bookmark name. On success, sets *out_client,
   *out_conn. Caller must free both on success. */
static bool connect_to_device(const char* deviceName,
                              struct nabtoshell_client_config* config,
                              NabtoClient** out_client,
                              NabtoClientConnection** out_conn)
{
    struct nabtoshell_device_bookmark* dev =
        nabtoshell_config_find_device(config, deviceName);
    if (dev == NULL) {
        printf("Device '%s' not found. Run 'nabtoshell devices' to see saved devices.\n",
               deviceName);
        return false;
    }

    NabtoClient* client = nabto_client_new();
    if (client == NULL) {
        printf("Failed to create Nabto client\n");
        return false;
    }

    char* privateKey = NULL;
    if (!nabtoshell_config_load_or_create_key(config, client, &privateKey)) {
        nabto_client_free(client);
        return false;
    }

    NabtoClientConnection* conn = nabto_client_connection_new(client);
    if (conn == NULL) {
        free(privateKey);
        nabto_client_free(client);
        return false;
    }

    char* optionsJson = nabtoshell_build_connection_options(
        dev->productId, dev->deviceId, privateKey, dev->sct);
    free(privateKey);
    if (optionsJson == NULL) {
        nabto_client_connection_free(conn);
        nabto_client_free(client);
        return false;
    }

    NabtoClientError ec = nabto_client_connection_set_options(conn, optionsJson);
    free(optionsJson);
    if (ec != NABTO_CLIENT_EC_OK) {
        printf("Failed to set connection options: %s\n",
               nabto_client_error_get_message(ec));
        nabto_client_connection_free(conn);
        nabto_client_free(client);
        return false;
    }

    NabtoClientFuture* future = nabto_client_future_new(client);
    nabto_client_connection_connect(conn, future);
    ec = nabto_client_future_wait(future);
    nabto_client_future_free(future);

    if (ec != NABTO_CLIENT_EC_OK) {
        printf("Failed to connect to '%s': %s\n", deviceName,
               nabto_client_error_get_message(ec));
        nabto_client_connection_free(conn);
        nabto_client_free(client);
        return false;
    }

    *out_client = client;
    *out_conn = conn;
    return true;
}

static void close_connection(NabtoClient* client, NabtoClientConnection* conn,
                             struct nabtoshell_client_config* config)
{
    NabtoClientFuture* future = nabto_client_future_new(client);
    nabto_client_connection_close(conn, future);
    nabto_client_future_wait(future);
    nabto_client_future_free(future);

    nabto_client_connection_free(conn);
    nabto_client_free(client);
    nabtoshell_config_deinit(config);
}

int nabtoshell_cmd_attach(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: nabtoshell attach <device> [session]\n");
        printf("\n");
        printf("Attach to an existing tmux session. Fails if the session does not exist.\n");
        printf("Default session: \"main\"\n");
        return 1;
    }

    const char* deviceName = argv[1];
    const char* sessionName = (argc >= 3) ? argv[2] : "main";

    /* Load config */
    struct nabtoshell_client_config config;
    if (!nabtoshell_config_init(&config)) {
        return 1;
    }
    nabtoshell_config_load_devices(&config);

    /* Connect */
    NabtoClient* client = NULL;
    NabtoClientConnection* conn = NULL;
    if (!connect_to_device(deviceName, &config, &client, &conn)) {
        nabtoshell_config_deinit(&config);
        return 1;
    }

    /* Get terminal size */
    uint16_t cols, rows;
    nabtoshell_terminal_get_size(&cols, &rows);

    /* Attach to existing session (create=false) */
    if (!nabtoshell_coap_attach(conn, client, sessionName, cols, rows, false)) {
        printf("Session '%s' not found on '%s'.\n", sessionName, deviceName);
        close_connection(client, conn, &config);
        return 1;
    }

    /* Run the stream relay */
    int ret = run_stream_relay(conn, client, &config);

    close_connection(client, conn, &config);
    return ret;
}

int nabtoshell_cmd_new_session(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: nabtoshell create <device> [session] [--command <cmd>]\n");
        printf("\n");
        printf("Create a new tmux session and attach to it.\n");
        printf("Default session name: auto-generated (ns-<pid>).\n");
        return 1;
    }

    const char* deviceName = argv[1];
    const char* sessionName = NULL;
    const char* command = NULL;

    /* Parse arguments: positional session name, then optional --command */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--command") == 0 && i + 1 < argc) {
            command = argv[i + 1];
            i++;
        } else if (sessionName == NULL) {
            sessionName = argv[i];
        }
    }

    /* Auto-generate session name if not provided */
    char generatedName[64] = {0};
    if (sessionName == NULL) {
        snprintf(generatedName, sizeof(generatedName), "ns-%d", (int)getpid());
        sessionName = generatedName;
    }

    /* Load config */
    struct nabtoshell_client_config config;
    if (!nabtoshell_config_init(&config)) {
        return 1;
    }
    nabtoshell_config_load_devices(&config);

    /* Connect */
    NabtoClient* client = NULL;
    NabtoClientConnection* conn = NULL;
    if (!connect_to_device(deviceName, &config, &client, &conn)) {
        nabtoshell_config_deinit(&config);
        return 1;
    }

    /* Get terminal size */
    uint16_t cols, rows;
    nabtoshell_terminal_get_size(&cols, &rows);

    /* Create the session */
    if (!nabtoshell_coap_create_session(conn, client, sessionName,
                                        cols, rows, command)) {
        printf("Failed to create session '%s' on '%s'.\n", sessionName, deviceName);
        close_connection(client, conn, &config);
        return 1;
    }

    /* Attach to the newly created session (create=false) */
    if (!nabtoshell_coap_attach(conn, client, sessionName, cols, rows, false)) {
        printf("Failed to attach to session '%s'.\n", sessionName);
        close_connection(client, conn, &config);
        return 1;
    }

    /* Run the stream relay */
    int ret = run_stream_relay(conn, client, &config);

    close_connection(client, conn, &config);
    return ret;
}
