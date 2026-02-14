#include "tmuxremote_stream.h"

#include "tmuxremote.h"
#include "tmuxremote_control_stream.h"
#include "tmuxremote_session.h"
#include "tmuxremote_tmux.h"

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

#define NEWLINE "\n"

static void start_listen(struct tmuxremote_stream_listener* sl);
static void stream_callback(NabtoDeviceFuture* future,
                            NabtoDeviceError ec,
                            void* userData);
static void stream_accepted(NabtoDeviceFuture* future,
                            NabtoDeviceError ec,
                            void* userData);
static void start_stream_close(struct tmuxremote_active_stream* as);
static void stream_closed(NabtoDeviceFuture* future,
                          NabtoDeviceError ec,
                          void* userData);
static void cleanup_active_stream(struct tmuxremote_active_stream* as);
static void* stream_setup_thread(void* arg);
static void* pty_reader_thread(void* arg);
static void* stream_reader_thread(void* arg);
static bool write_all_fd(int fd, const uint8_t* data, size_t len);

static void start_stream_close_once(struct tmuxremote_active_stream* as)
{
    bool expected = false;
    if (atomic_compare_exchange_strong(&as->closeStarted, &expected, true)) {
        start_stream_close(as);
    }
}

void tmuxremote_stream_listener_init(struct tmuxremote_stream_listener* sl,
                                     NabtoDevice* device,
                                     struct tmuxremote* app)
{
    memset(sl, 0, sizeof(struct tmuxremote_stream_listener));
    sl->device = device;
    sl->app = app;
    pthread_mutex_init(&sl->activeStreamsMutex, NULL);

    sl->listener = nabto_device_listener_new(device);
    sl->future = nabto_device_future_new(device);

    NabtoDeviceError ec = nabto_device_stream_init_listener(device, sl->listener, 1);
    if (ec != NABTO_DEVICE_EC_OK) {
        printf("Failed to init stream listener: %s" NEWLINE,
               nabto_device_error_get_message(ec));
        return;
    }

    start_listen(sl);
}

void tmuxremote_stream_listener_stop(struct tmuxremote_stream_listener* sl)
{
    if (sl->listener != NULL) {
        nabto_device_listener_stop(sl->listener);
    }

    pthread_mutex_lock(&sl->activeStreamsMutex);
    struct tmuxremote_active_stream* as = sl->activeStreams;
    while (as != NULL) {
        atomic_store(&as->closing, true);
        if (as->childPid > 0) {
            kill(as->childPid, SIGTERM);
        }
        as = as->next;
    }
    pthread_mutex_unlock(&sl->activeStreamsMutex);
}

void tmuxremote_stream_listener_deinit(struct tmuxremote_stream_listener* sl)
{
    pthread_mutex_lock(&sl->activeStreamsMutex);
    struct tmuxremote_active_stream* as = sl->activeStreams;
    sl->activeStreams = NULL;
    pthread_mutex_unlock(&sl->activeStreamsMutex);

    while (as != NULL) {
        struct tmuxremote_active_stream* next = as->next;
        atomic_store(&as->closing, true);
        if (as->ptyFd >= 0) {
            close(as->ptyFd);
            as->ptyFd = -1;
        }
        if (as->childPid > 0) {
            kill(as->childPid, SIGTERM);
        }
        if (as->setupThreadStarted) {
            if (!pthread_equal(as->setupThread, pthread_self())) {
                pthread_join(as->setupThread, NULL);
            }
        }
        if (as->ptyReaderThreadStarted) {
            if (!pthread_equal(as->ptyReaderThread, pthread_self())) {
                pthread_join(as->ptyReaderThread, NULL);
            }
        }
        if (as->streamReaderThreadStarted) {
            if (!pthread_equal(as->streamReaderThread, pthread_self())) {
                pthread_join(as->streamReaderThread, NULL);
            }
        }
        if (as->childPid > 0) {
            waitpid(as->childPid, NULL, 0);
            as->childPid = -1;
        }
        if (as->promptDetectorInitialized) {
            tmuxremote_prompt_detector_free(&as->promptDetector);
            as->promptDetectorInitialized = false;
        }
        if (as->stream != NULL) {
            nabto_device_stream_free(as->stream);
        }
        free(as);
        as = next;
    }

    if (sl->future != NULL) {
        nabto_device_future_free(sl->future);
        sl->future = NULL;
    }
    if (sl->listener != NULL) {
        nabto_device_listener_free(sl->listener);
        sl->listener = NULL;
    }
    pthread_mutex_destroy(&sl->activeStreamsMutex);
}

tmuxremote_prompt_instance* tmuxremote_stream_copy_active_prompt_for_ref(
    struct tmuxremote_stream_listener* sl,
    NabtoDeviceConnectionRef ref)
{
    tmuxremote_prompt_instance* result = NULL;

    pthread_mutex_lock(&sl->activeStreamsMutex);
    struct tmuxremote_active_stream* as = sl->activeStreams;
    while (as != NULL) {
        if (!atomic_load(&as->closing) && as->promptDetectorInitialized &&
            as->connectionRef == ref) {
            result = tmuxremote_prompt_detector_copy_active(&as->promptDetector);
            break;
        }
        as = as->next;
    }
    pthread_mutex_unlock(&sl->activeStreamsMutex);

    return result;
}

void tmuxremote_stream_resolve_prompt_for_ref(
    struct tmuxremote_stream_listener* sl,
    NabtoDeviceConnectionRef ref,
    const char* instance_id,
    const char* decision,
    const char* keys)
{
    pthread_mutex_lock(&sl->activeStreamsMutex);
    struct tmuxremote_active_stream* as = sl->activeStreams;
    while (as != NULL) {
        if (!atomic_load(&as->closing) && as->promptDetectorInitialized &&
            as->connectionRef == ref) {
            tmuxremote_prompt_detector_resolve(&as->promptDetector,
                                               instance_id,
                                               decision,
                                               keys);
            break;
        }
        as = as->next;
    }
    pthread_mutex_unlock(&sl->activeStreamsMutex);
}

int tmuxremote_stream_get_pty_fd(struct tmuxremote_stream_listener* sl,
                                 NabtoDeviceConnectionRef ref)
{
    int fd = -1;

    pthread_mutex_lock(&sl->activeStreamsMutex);
    struct tmuxremote_active_stream* as = sl->activeStreams;
    while (as != NULL) {
        if (as->stream != NULL && !atomic_load(&as->closing)) {
            if (as->connectionRef == ref && as->ptyFd >= 0) {
                fd = as->ptyFd;
                break;
            }
        }
        as = as->next;
    }
    pthread_mutex_unlock(&sl->activeStreamsMutex);

    return fd;
}

void tmuxremote_stream_resize_prompt_detector_for_ref(
    struct tmuxremote_stream_listener* sl,
    NabtoDeviceConnectionRef ref,
    int cols,
    int rows)
{
    pthread_mutex_lock(&sl->activeStreamsMutex);
    struct tmuxremote_active_stream* as = sl->activeStreams;
    while (as != NULL) {
        if (!atomic_load(&as->closing) && as->promptDetectorInitialized &&
            as->connectionRef == ref) {
            as->sessionCols = (uint16_t)cols;
            as->sessionRows = (uint16_t)rows;
            tmuxremote_prompt_detector_resize(&as->promptDetector, rows, cols);
            break;
        }
        as = as->next;
    }
    pthread_mutex_unlock(&sl->activeStreamsMutex);
}

static void start_listen(struct tmuxremote_stream_listener* sl)
{
    nabto_device_listener_new_stream(sl->listener, sl->future, &sl->newStream);
    nabto_device_future_set_callback(sl->future, stream_callback, sl);
}

static void stream_callback(NabtoDeviceFuture* future,
                            NabtoDeviceError ec,
                            void* userData)
{
    (void)future;

    struct tmuxremote_stream_listener* sl = userData;
    if (ec != NABTO_DEVICE_EC_OK) {
        return;
    }

    struct tmuxremote_active_stream* as =
        (struct tmuxremote_active_stream*)calloc(1, sizeof(struct tmuxremote_active_stream));
    if (as == NULL) {
        nabto_device_stream_free(sl->newStream);
        start_listen(sl);
        return;
    }

    as->device = sl->device;
    as->stream = sl->newStream;
    as->app = sl->app;
    as->ptyFd = -1;
    as->childPid = -1;
    atomic_init(&as->closing, false);
    atomic_init(&as->closeStarted, false);

    pthread_mutex_lock(&sl->activeStreamsMutex);
    as->next = sl->activeStreams;
    sl->activeStreams = as;
    pthread_mutex_unlock(&sl->activeStreamsMutex);

    NabtoDeviceFuture* acceptFuture = nabto_device_future_new(sl->device);
    if (acceptFuture == NULL) {
        cleanup_active_stream(as);
        start_listen(sl);
        return;
    }
    nabto_device_stream_accept(as->stream, acceptFuture);
    nabto_device_future_set_callback(acceptFuture, stream_accepted, as);

    sl->newStream = NULL;
    start_listen(sl);
}

static void stream_accepted(NabtoDeviceFuture* future,
                            NabtoDeviceError ec,
                            void* userData)
{
    struct tmuxremote_active_stream* as = userData;
    nabto_device_future_free(future);

    if (ec != NABTO_DEVICE_EC_OK) {
        cleanup_active_stream(as);
        return;
    }

    NabtoDeviceConnectionRef ref = nabto_device_stream_get_connection_ref(as->stream);
    as->connectionRef = ref;
    if (!tmuxremote_iam_check_access_ref(&as->app->iam, ref, "Terminal:Connect")) {
        start_stream_close_once(as);
        return;
    }

    struct tmuxremote_session_entry entry;
    if (!tmuxremote_session_get(&as->app->sessionMap, ref, &entry)) {
        printf("No session target set for connection, closing stream" NEWLINE);
        start_stream_close_once(as);
        return;
    }

    strncpy(as->sessionName, entry.sessionName, sizeof(as->sessionName) - 1);
    as->sessionName[sizeof(as->sessionName) - 1] = '\0';
    as->sessionCols = entry.cols;
    as->sessionRows = entry.rows;

    if (pthread_create(&as->setupThread, NULL, stream_setup_thread, as) != 0) {
        printf("Failed to create stream setup thread" NEWLINE);
        start_stream_close_once(as);
        return;
    }
    as->setupThreadStarted = true;
}

static void prompt_stream_callback(tmuxremote_prompt_event_type type,
                                   const tmuxremote_prompt_instance* instance,
                                   const char* instance_id,
                                   void* user_data)
{
    struct tmuxremote_active_stream* as = user_data;

    switch (type) {
        case TMUXREMOTE_PROMPT_EVENT_PRESENT:
            if (instance != NULL) {
                tmuxremote_control_stream_send_prompt_present_for_ref(
                    &as->app->controlStreamListener,
                    as->connectionRef,
                    instance);
            }
            break;
        case TMUXREMOTE_PROMPT_EVENT_UPDATE:
            if (instance != NULL) {
                tmuxremote_control_stream_send_prompt_update_for_ref(
                    &as->app->controlStreamListener,
                    as->connectionRef,
                    instance);
            }
            break;
        case TMUXREMOTE_PROMPT_EVENT_GONE:
            if (instance_id != NULL) {
                tmuxremote_control_stream_send_prompt_gone_for_ref(
                    &as->app->controlStreamListener,
                    as->connectionRef,
                    instance_id);
            }
            break;
        default:
            break;
    }
}

static void* stream_setup_thread(void* arg)
{
    struct tmuxremote_active_stream* as = arg;

    if (atomic_load(&as->closing)) {
        return NULL;
    }

    struct winsize ws;
    ws.ws_col = as->sessionCols;
    ws.ws_row = as->sessionRows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    pid_t pid = forkpty(&as->ptyFd, NULL, NULL, &ws);
    if (pid < 0) {
        printf("forkpty failed: %s" NEWLINE, strerror(errno));
        start_stream_close_once(as);
        return NULL;
    }

    if (pid == 0) {
        execlp("tmux", "tmux", "attach-session", "-t", as->sessionName, (char*)NULL);
        _exit(1);
    }

    as->childPid = pid;

    tmuxremote_prompt_detector_init(&as->promptDetector,
                                    as->sessionRows > 0 ? as->sessionRows : 48,
                                    as->sessionCols > 0 ? as->sessionCols : 160);
    as->promptDetectorInitialized = true;

    if (as->app->patternConfig != NULL) {
        tmuxremote_prompt_detector_load_config(&as->promptDetector,
                                               as->app->patternConfig);
    }

    tmuxremote_prompt_detector_set_callback(&as->promptDetector,
                                            prompt_stream_callback,
                                            as);

    if (as->app->recordPtyFile) {
        as->ptyRecordFile = fopen(as->app->recordPtyFile, "wb");
        if (as->ptyRecordFile) {
            uint8_t header[16] = {0};
            memcpy(header, "PTYR", 4);
            uint32_t version = htonl(1);
            memcpy(header + 4, &version, 4);
            fwrite(header, 1, 16, as->ptyRecordFile);
            fflush(as->ptyRecordFile);
            printf("Recording PTY data to %s" NEWLINE, as->app->recordPtyFile);
        } else {
            printf("Warning: failed to open PTY recording file %s" NEWLINE,
                   as->app->recordPtyFile);
        }
    }

    if (pthread_create(&as->ptyReaderThread, NULL, pty_reader_thread, as) != 0) {
        printf("Failed to create PTY->stream thread" NEWLINE);
        kill(pid, SIGTERM);
        close(as->ptyFd);
        as->ptyFd = -1;
        start_stream_close_once(as);
        return NULL;
    }
    as->ptyReaderThreadStarted = true;

    if (pthread_create(&as->streamReaderThread, NULL, stream_reader_thread, as) != 0) {
        printf("Failed to create stream->PTY thread" NEWLINE);
        atomic_store(&as->closing, true);
        close(as->ptyFd);
        as->ptyFd = -1;
        start_stream_close_once(as);
        return NULL;
    }
    as->streamReaderThreadStarted = true;

    return NULL;
}

static void* stream_reader_thread(void* arg)
{
    struct tmuxremote_active_stream* as = arg;
    uint8_t buf[TMUXREMOTE_STREAM_BUFFER_SIZE];
    size_t readLength = 0;

    while (!atomic_load(&as->closing)) {
        NabtoDeviceFuture* readFuture = nabto_device_future_new(as->device);
        if (readFuture == NULL) {
            break;
        }

        nabto_device_stream_read_some(as->stream, readFuture, buf, sizeof(buf), &readLength);
        NabtoDeviceError ec = nabto_device_future_wait(readFuture);
        nabto_device_future_free(readFuture);

        if (ec != NABTO_DEVICE_EC_OK) {
            break;
        }

        if (readLength > 0 && as->ptyFd >= 0) {
            if (!write_all_fd(as->ptyFd, buf, readLength)) {
                break;
            }
        }
    }

    atomic_store(&as->closing, true);
    start_stream_close_once(as);
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
        if (written < 0 &&
            (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        return false;
    }
    return true;
}

static void* pty_reader_thread(void* arg)
{
    struct tmuxremote_active_stream* as = arg;
    uint8_t buf[TMUXREMOTE_STREAM_BUFFER_SIZE];

    while (!atomic_load(&as->closing)) {
        ssize_t n = read(as->ptyFd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }

        if (as->ptyRecordFile) {
            uint32_t frameLen = htonl((uint32_t)n);
            fwrite(&frameLen, 1, 4, as->ptyRecordFile);
            fwrite(buf, 1, (size_t)n, as->ptyRecordFile);
            fflush(as->ptyRecordFile);
        }

        if (as->promptDetectorInitialized) {
            tmuxremote_prompt_detector_feed(&as->promptDetector, buf, (size_t)n);
        }

        NabtoDeviceFuture* writeFuture = nabto_device_future_new(as->device);
        if (writeFuture == NULL) {
            break;
        }
        nabto_device_stream_write(as->stream, writeFuture, buf, (size_t)n);
        NabtoDeviceError ec = nabto_device_future_wait(writeFuture);
        nabto_device_future_free(writeFuture);

        if (ec != NABTO_DEVICE_EC_OK) {
            break;
        }
    }

    atomic_store(&as->closing, true);
    tmuxremote_control_stream_notify(&as->app->controlStreamListener);
    start_stream_close_once(as);
    return NULL;
}

static void start_stream_close(struct tmuxremote_active_stream* as)
{
    NabtoDeviceFuture* closeFuture = nabto_device_future_new(as->device);
    if (closeFuture == NULL) {
        cleanup_active_stream(as);
        return;
    }
    nabto_device_stream_close(as->stream, closeFuture);
    nabto_device_future_set_callback(closeFuture, stream_closed, as);
}

static void stream_closed(NabtoDeviceFuture* future,
                          NabtoDeviceError ec,
                          void* userData)
{
    (void)ec;
    struct tmuxremote_active_stream* as = userData;
    nabto_device_future_free(future);
    cleanup_active_stream(as);
}

static void* cleanup_thread_func(void* arg)
{
    struct tmuxremote_active_stream* as = arg;

    if (as->setupThreadStarted) {
        pthread_join(as->setupThread, NULL);
        as->setupThreadStarted = false;
    }

    if (as->ptyFd >= 0) {
        int ptyFd = as->ptyFd;
        close(ptyFd);
        as->ptyFd = -1;
    }

    if (as->childPid > 0) {
        pid_t child = as->childPid;
        kill(child, SIGTERM);
    }

    if (as->ptyReaderThreadStarted) {
        pthread_join(as->ptyReaderThread, NULL);
        as->ptyReaderThreadStarted = false;
    }

    if (as->streamReaderThreadStarted) {
        pthread_join(as->streamReaderThread, NULL);
        as->streamReaderThreadStarted = false;
    }

    if (as->childPid > 0) {
        int status;
        waitpid(as->childPid, &status, 0);
        as->childPid = -1;
    }

    if (as->ptyRecordFile) {
        fclose(as->ptyRecordFile);
        as->ptyRecordFile = NULL;
    }

    if (as->promptDetectorInitialized) {
        tmuxremote_prompt_detector_free(&as->promptDetector);
        as->promptDetectorInitialized = false;
    }

    if (as->stream != NULL) {
        nabto_device_stream_free(as->stream);
        as->stream = NULL;
    }

    free(as);
    return NULL;
}

static void cleanup_active_stream(struct tmuxremote_active_stream* as)
{
    atomic_store(&as->closing, true);

    if (as->stream != NULL && as->app != NULL) {
        NabtoDeviceConnectionRef mapRef =
            nabto_device_stream_get_connection_ref(as->stream);
        tmuxremote_session_remove(&as->app->sessionMap, mapRef);
    }

    if (as->app != NULL) {
        struct tmuxremote_stream_listener* sl = &as->app->streamListener;
        pthread_mutex_lock(&sl->activeStreamsMutex);
        struct tmuxremote_active_stream** pp = &sl->activeStreams;
        while (*pp != NULL) {
            if (*pp == as) {
                *pp = as->next;
                break;
            }
            pp = &(*pp)->next;
        }
        pthread_mutex_unlock(&sl->activeStreamsMutex);
    }

    pthread_t cleanupThread;
    if (pthread_create(&cleanupThread, NULL, cleanup_thread_func, as) == 0) {
        pthread_detach(cleanupThread);
    } else {
        if (as->childPid > 0) {
            kill(as->childPid, SIGTERM);
        }
    }
}
