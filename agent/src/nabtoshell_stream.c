#include "nabtoshell_stream.h"
#include "nabtoshell.h"
#include "nabtoshell_control_stream.h"
#include "nabtoshell_pattern_engine.h"
#include "nabtoshell_session.h"
#include "nabtoshell_tmux.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <errno.h>

#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

#define NEWLINE "\n"

static void start_listen(struct nabtoshell_stream_listener* sl);
static void stream_callback(NabtoDeviceFuture* future, NabtoDeviceError ec,
                            void* userData);
static void stream_accepted(NabtoDeviceFuture* future, NabtoDeviceError ec,
                            void* userData);
static void start_stream_close(struct nabtoshell_active_stream* as);
static void stream_closed(NabtoDeviceFuture* future, NabtoDeviceError ec,
                          void* userData);
static void cleanup_active_stream(struct nabtoshell_active_stream* as);
static void* stream_setup_thread(void* arg);
static void* pty_reader_thread(void* arg);
static void* stream_reader_thread(void* arg);
static bool write_all_fd(int fd, const uint8_t* data, size_t len);

static void start_stream_close_once(struct nabtoshell_active_stream* as)
{
    bool expected = false;
    if (atomic_compare_exchange_strong(&as->closeStarted, &expected, true)) {
        start_stream_close(as);
    }
}

void nabtoshell_stream_listener_init(struct nabtoshell_stream_listener* sl,
                                     NabtoDevice* device,
                                     struct nabtoshell* app)
{
    memset(sl, 0, sizeof(struct nabtoshell_stream_listener));
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

void nabtoshell_stream_listener_stop(struct nabtoshell_stream_listener* sl)
{
    if (sl->listener != NULL) {
        nabto_device_listener_stop(sl->listener);
    }

    /* Mark active streams for shutdown without blocking in this thread. */
    pthread_mutex_lock(&sl->activeStreamsMutex);
    struct nabtoshell_active_stream* as = sl->activeStreams;
    while (as != NULL) {
        atomic_store(&as->closing, true);
        if (as->childPid > 0) {
            kill(as->childPid, SIGTERM);
        }
        as = as->next;
    }
    pthread_mutex_unlock(&sl->activeStreamsMutex);
}

void nabtoshell_stream_listener_deinit(struct nabtoshell_stream_listener* sl)
{
    /* Snapshot the list under lock, then join/free without lock
     * (safe because stop was called and no new streams are accepted). */
    pthread_mutex_lock(&sl->activeStreamsMutex);
    struct nabtoshell_active_stream* as = sl->activeStreams;
    sl->activeStreams = NULL;
    pthread_mutex_unlock(&sl->activeStreamsMutex);

    while (as != NULL) {
        struct nabtoshell_active_stream* next = as->next;
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
        if (as->patternEngineInitialized) {
            nabtoshell_pattern_engine_free(&as->patternEngine);
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

/* Returns a copy of the active pattern match for the data stream belonging
 * to the given connection ref.  The protocol guarantees at most one active
 * data stream per connection (attach + open on port 1), so the first
 * non-closing match is authoritative.  If this invariant is ever violated,
 * we still return the first match found. */
nabtoshell_pattern_match* nabtoshell_stream_copy_active_match_for_ref(
    struct nabtoshell_stream_listener* sl,
    NabtoDeviceConnectionRef ref)
{
    nabtoshell_pattern_match* result = NULL;
    pthread_mutex_lock(&sl->activeStreamsMutex);
    struct nabtoshell_active_stream* as = sl->activeStreams;
    while (as != NULL) {
        if (!atomic_load(&as->closing) && as->patternEngineInitialized &&
            as->connectionRef == ref) {
            result = nabtoshell_pattern_engine_copy_active_match(&as->patternEngine);
            break;
        }
        as = as->next;
    }
    pthread_mutex_unlock(&sl->activeStreamsMutex);
    return result;
}

void nabtoshell_stream_dismiss_pattern_for_ref(
    struct nabtoshell_stream_listener* sl,
    NabtoDeviceConnectionRef ref)
{
    pthread_mutex_lock(&sl->activeStreamsMutex);
    struct nabtoshell_active_stream* as = sl->activeStreams;
    while (as != NULL) {
        if (!atomic_load(&as->closing) && as->patternEngineInitialized &&
            as->connectionRef == ref) {
            nabtoshell_pattern_engine_dismiss(&as->patternEngine);
            break;
        }
        as = as->next;
    }
    pthread_mutex_unlock(&sl->activeStreamsMutex);
}

int nabtoshell_stream_get_pty_fd(struct nabtoshell_stream_listener* sl,
                                 NabtoDeviceConnectionRef ref)
{
    int fd = -1;
    pthread_mutex_lock(&sl->activeStreamsMutex);
    struct nabtoshell_active_stream* as = sl->activeStreams;
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

static void start_listen(struct nabtoshell_stream_listener* sl)
{
    nabto_device_listener_new_stream(sl->listener, sl->future, &sl->newStream);
    nabto_device_future_set_callback(sl->future, stream_callback, sl);
}

static void stream_callback(NabtoDeviceFuture* future, NabtoDeviceError ec,
                            void* userData)
{
    (void)future;
    struct nabtoshell_stream_listener* sl = userData;
    if (ec != NABTO_DEVICE_EC_OK) {
        return;
    }

    /* Create active stream state */
    struct nabtoshell_active_stream* as =
        (struct nabtoshell_active_stream*)calloc(1, sizeof(struct nabtoshell_active_stream));
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

    /* Add to linked list */
    pthread_mutex_lock(&sl->activeStreamsMutex);
    as->next = sl->activeStreams;
    sl->activeStreams = as;
    pthread_mutex_unlock(&sl->activeStreamsMutex);

    /* Accept the stream */
    NabtoDeviceFuture* acceptFuture = nabto_device_future_new(sl->device);
    if (acceptFuture == NULL) {
        cleanup_active_stream(as);
        start_listen(sl);
        return;
    }
    nabto_device_stream_accept(as->stream, acceptFuture);
    nabto_device_future_set_callback(acceptFuture, stream_accepted, as);

    /* Ready for the next stream */
    sl->newStream = NULL;
    start_listen(sl);
}

static void stream_accepted(NabtoDeviceFuture* future, NabtoDeviceError ec,
                            void* userData)
{
    struct nabtoshell_active_stream* as = userData;
    nabto_device_future_free(future);

    if (ec != NABTO_DEVICE_EC_OK) {
        cleanup_active_stream(as);
        return;
    }

    /* IAM check */
    NabtoDeviceConnectionRef ref =
        nabto_device_stream_get_connection_ref(as->stream);
    as->connectionRef = ref;
    if (!nabtoshell_iam_check_access_ref(&as->app->iam, ref, "Terminal:Connect")) {
        start_stream_close_once(as);
        return;
    }

    /* Look up session target */
    struct nabtoshell_session_entry entry;
    if (!nabtoshell_session_get(&as->app->sessionMap, ref, &entry)) {
        printf("No session target set for connection, closing stream" NEWLINE);
        start_stream_close_once(as);
        return;
    }

    /* Copy session target and do setup off the SDK callback thread. */
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

static void pattern_stream_callback(const nabtoshell_pattern_match* match,
                                     void* user_data)
{
    struct nabtoshell_active_stream* as = user_data;
    if (match != NULL) {
        printf("[Pattern] pattern_stream_callback: match id=%s, type=%d, actions=%d, ref=%u" NEWLINE,
               match->id, match->pattern_type, match->action_count, (unsigned)as->connectionRef);
        nabtoshell_control_stream_send_pattern_match_for_ref(
            &as->app->controlStreamListener, as->connectionRef, match);
    } else {
        printf("[Pattern] pattern_stream_callback: dismiss, ref=%u" NEWLINE,
               (unsigned)as->connectionRef);
        nabtoshell_control_stream_send_pattern_dismiss_for_ref(
            &as->app->controlStreamListener, as->connectionRef);
    }
}

static void* stream_setup_thread(void* arg)
{
    struct nabtoshell_active_stream* as = arg;

    if (atomic_load(&as->closing)) {
        return NULL;
    }

    struct winsize ws;
    ws.ws_col = as->sessionCols;
    ws.ws_row = as->sessionRows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    /* Spawn tmux attach in a PTY. */
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

    /* Initialize per-stream pattern engine */
    nabtoshell_pattern_engine_init(&as->patternEngine);
    as->patternEngineInitialized = true;
    if (as->app->patternConfig) {
        nabtoshell_pattern_engine_load_config(&as->patternEngine, as->app->patternConfig);
        if (as->app->patternConfig->agent_count == 1) {
            nabtoshell_pattern_engine_select_agent(
                &as->patternEngine, as->app->patternConfig->agents[0].id);
        }
        /* Multiple agents: auto-detect from PTY data */
    }
    nabtoshell_pattern_engine_set_callback(&as->patternEngine, pattern_stream_callback, as);

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

/* Stream reader thread: reads from Nabto stream and writes to PTY. */
static void* stream_reader_thread(void* arg)
{
    struct nabtoshell_active_stream* as = arg;
    uint8_t buf[NABTOSHELL_STREAM_BUFFER_SIZE];
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
        if (written < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        return false;
    }
    return true;
}

/* PTY reader thread: reads from PTY and writes to Nabto stream. */
static void* pty_reader_thread(void* arg)
{
    struct nabtoshell_active_stream* as = arg;
    uint8_t buf[NABTOSHELL_STREAM_BUFFER_SIZE];

    while (!atomic_load(&as->closing)) {
        ssize_t n = read(as->ptyFd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }

        /* Feed PTY output through per-stream pattern detection */
        if (as->patternEngineInitialized) {
            nabtoshell_pattern_engine_feed(&as->patternEngine, buf, (size_t)n);
        }

        /* Write to Nabto stream (blocking) */
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
    /* Notify control stream monitor: session may have died. */
    nabtoshell_control_stream_notify(&as->app->controlStreamListener);
    start_stream_close_once(as);
    return NULL;
}

static void start_stream_close(struct nabtoshell_active_stream* as)
{
    NabtoDeviceFuture* closeFuture = nabto_device_future_new(as->device);
    if (closeFuture == NULL) {
        cleanup_active_stream(as);
        return;
    }
    nabto_device_stream_close(as->stream, closeFuture);
    nabto_device_future_set_callback(closeFuture, stream_closed, as);
}

static void stream_closed(NabtoDeviceFuture* future, NabtoDeviceError ec,
                          void* userData)
{
    (void)ec;
    struct nabtoshell_active_stream* as = userData;
    nabto_device_future_free(future);
    cleanup_active_stream(as);
}

/*
 * Blocking cleanup that runs on a detached thread.
 * Waits for the PTY reader thread to exit and reaps the child process,
 * then frees all resources. This MUST NOT run on the SDK event loop
 * thread because pthread_join can deadlock if the PTY reader thread is
 * itself blocked inside nabto_device_future_wait.
 */
static void* cleanup_thread_func(void* arg)
{
    struct nabtoshell_active_stream* as = arg;

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

    if (as->patternEngineInitialized) {
        nabtoshell_pattern_engine_free(&as->patternEngine);
        as->patternEngineInitialized = false;
    }

    if (as->stream != NULL) {
        nabto_device_stream_free(as->stream);
        as->stream = NULL;
    }
    free(as);
    return NULL;
}

static void cleanup_active_stream(struct nabtoshell_active_stream* as)
{
    atomic_store(&as->closing, true);

    /* Remove from session map */
    if (as->stream != NULL && as->app != NULL) {
        NabtoDeviceConnectionRef mapRef =
            nabto_device_stream_get_connection_ref(as->stream);
        nabtoshell_session_remove(&as->app->sessionMap, mapRef);
    }

    /* Remove from the linked list */
    if (as->app != NULL) {
        struct nabtoshell_stream_listener* sl = &as->app->streamListener;
        pthread_mutex_lock(&sl->activeStreamsMutex);
        struct nabtoshell_active_stream** pp = &sl->activeStreams;
        while (*pp != NULL) {
            if (*pp == as) {
                *pp = as->next;
                break;
            }
            pp = &(*pp)->next;
        }
        pthread_mutex_unlock(&sl->activeStreamsMutex);
    }

    /*
     * Defer blocking operations (pthread_join, waitpid) and final free
     * to a detached thread so we never block the SDK event loop here.
     */
    pthread_t cleanupThread;
    if (pthread_create(&cleanupThread, NULL, cleanup_thread_func, as) == 0) {
        pthread_detach(cleanupThread);
    } else {
        /* Never block SDK callback threads. If cleanup thread creation fails,
         * mark stream as orphaned and return; this is a fail-safe path. */
        if (as->childPid > 0) {
            kill(as->childPid, SIGTERM);
        }
    }
}
