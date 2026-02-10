#include "nabtoshell_stream.h"
#include "nabtoshell.h"
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
#include <time.h>

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
static int64_t monotonic_ms(void);
static unsigned long current_tid(void);
static NabtoDeviceConnectionRef stream_ref(struct nabtoshell_active_stream* as);
static size_t count_active_streams(struct nabtoshell_stream_listener* sl);

static atomic_uint_fast64_t streamDebugSeq = 0;

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((int64_t)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

static unsigned long current_tid(void)
{
    return (unsigned long)pthread_self();
}

static NabtoDeviceConnectionRef stream_ref(struct nabtoshell_active_stream* as)
{
    if (as == NULL || as->stream == NULL) {
        return 0;
    }
    return nabto_device_stream_get_connection_ref(as->stream);
}

static size_t count_active_streams(struct nabtoshell_stream_listener* sl)
{
    size_t count = 0;
    struct nabtoshell_active_stream* as = sl->activeStreams;
    while (as != NULL) {
        count++;
        as = as->next;
    }
    return count;
}

static void start_stream_close_once(struct nabtoshell_active_stream* as, const char* reason)
{
    bool expected = false;
    if (atomic_compare_exchange_strong(&as->closeStarted, &expected, true)) {
        printf("[STREAMDBG] close_once trigger id=%llu ref=%llu reason=%s tid=%lu\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (reason != NULL ? reason : "?"),
               current_tid());
        start_stream_close(as);
    } else {
        printf("[STREAMDBG] close_once skip id=%llu ref=%llu reason=%s tid=%lu\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (reason != NULL ? reason : "?"),
               current_tid());
    }
}

void nabtoshell_stream_listener_init(struct nabtoshell_stream_listener* sl,
                                     NabtoDevice* device,
                                     struct nabtoshell* app)
{
    memset(sl, 0, sizeof(struct nabtoshell_stream_listener));
    sl->device = device;
    sl->app = app;

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
    printf("[STREAMDBG] listener_stop tid=%lu active=%zu\n",
           current_tid(), count_active_streams(sl));
    if (sl->listener != NULL) {
        nabto_device_listener_stop(sl->listener);
    }

    /* Mark active streams for shutdown without blocking in this thread. */
    struct nabtoshell_active_stream* as = sl->activeStreams;
    while (as != NULL) {
        atomic_store(&as->closing, true);
        if (as->childPid > 0) {
            kill(as->childPid, SIGTERM);
        }
        as = as->next;
    }
}

void nabtoshell_stream_listener_deinit(struct nabtoshell_stream_listener* sl)
{
    printf("[STREAMDBG] listener_deinit tid=%lu active=%zu\n",
           current_tid(), count_active_streams(sl));
    /* Wait for and clean up active streams */
    struct nabtoshell_active_stream* as = sl->activeStreams;
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
        if (as->stream != NULL) {
            nabto_device_stream_free(as->stream);
        }
        free(as);
        as = next;
    }
    sl->activeStreams = NULL;

    if (sl->future != NULL) {
        nabto_device_future_free(sl->future);
        sl->future = NULL;
    }
    if (sl->listener != NULL) {
        nabto_device_listener_free(sl->listener);
        sl->listener = NULL;
    }
}

int nabtoshell_stream_get_pty_fd(struct nabtoshell_stream_listener* sl,
                                 NabtoDeviceConnectionRef ref)
{
    struct nabtoshell_active_stream* as = sl->activeStreams;
    while (as != NULL) {
        if (as->stream != NULL && !atomic_load(&as->closing)) {
            NabtoDeviceConnectionRef streamRef =
                nabto_device_stream_get_connection_ref(as->stream);
            if (streamRef == ref && as->ptyFd >= 0) {
                return as->ptyFd;
            }
        }
        as = as->next;
    }
    return -1;
}

static void start_listen(struct nabtoshell_stream_listener* sl)
{
    nabto_device_listener_new_stream(sl->listener, sl->future, &sl->newStream);
    printf("[STREAMDBG] arm stream-listener listener=%p future=%p tid=%lu active=%zu\n",
           (void*)sl->listener, (void*)sl->future, current_tid(), count_active_streams(sl));
    nabto_device_future_set_callback(sl->future, stream_callback, sl);
}

static void stream_callback(NabtoDeviceFuture* future, NabtoDeviceError ec,
                            void* userData)
{
    int64_t cbStartMs = monotonic_ms();
    (void)future;
    struct nabtoshell_stream_listener* sl = userData;
    printf("[STREAMDBG] cb-enter stream_callback ec=%d tid=%lu newStream=%p active=%zu\n",
           (int)ec, current_tid(), (void*)sl->newStream, count_active_streams(sl));
    if (ec != NABTO_DEVICE_EC_OK) {
        printf("[STREAMDBG] cb-leave stream_callback ec=%d dur=%lldms tid=%lu\n",
               (int)ec, (long long)(monotonic_ms() - cbStartMs), current_tid());
        return;
    }

    /* Create active stream state */
    struct nabtoshell_active_stream* as =
        (struct nabtoshell_active_stream*)calloc(1, sizeof(struct nabtoshell_active_stream));
    if (as == NULL) {
        printf("[STREAMDBG] stream_callback alloc failed, dropping stream=%p\n", (void*)sl->newStream);
        nabto_device_stream_free(sl->newStream);
        start_listen(sl);
        printf("[STREAMDBG] cb-leave stream_callback alloc-failed dur=%lldms tid=%lu\n",
               (long long)(monotonic_ms() - cbStartMs), current_tid());
        return;
    }

    as->debugId = atomic_fetch_add(&streamDebugSeq, 1) + 1;
    as->createdAtMs = cbStartMs;
    as->device = sl->device;
    as->stream = sl->newStream;
    as->app = sl->app;
    as->ptyFd = -1;
    as->childPid = -1;
    atomic_init(&as->closing, false);
    atomic_init(&as->closeStarted, false);

    /* Add to linked list */
    as->next = sl->activeStreams;
    sl->activeStreams = as;
    printf("[STREAMDBG] stream accepted id=%llu ref=%llu stream=%p active=%zu\n",
           (unsigned long long)as->debugId,
           (unsigned long long)stream_ref(as),
           (void*)as->stream,
           count_active_streams(sl));

    /* Accept the stream */
    NabtoDeviceFuture* acceptFuture = nabto_device_future_new(sl->device);
    if (acceptFuture == NULL) {
        printf("[STREAMDBG] acceptFuture alloc failed id=%llu ref=%llu\n",
               (unsigned long long)as->debugId, (unsigned long long)stream_ref(as));
        cleanup_active_stream(as);
        start_listen(sl);
        printf("[STREAMDBG] cb-leave stream_callback accept-future-failed dur=%lldms tid=%lu\n",
               (long long)(monotonic_ms() - cbStartMs), current_tid());
        return;
    }
    nabto_device_stream_accept(as->stream, acceptFuture);
    nabto_device_future_set_callback(acceptFuture, stream_accepted, as);
    printf("[STREAMDBG] accept armed id=%llu ref=%llu acceptFuture=%p\n",
           (unsigned long long)as->debugId,
           (unsigned long long)stream_ref(as),
           (void*)acceptFuture);

    /* Ready for the next stream */
    sl->newStream = NULL;
    start_listen(sl);
    printf("[STREAMDBG] cb-leave stream_callback ec=0 id=%llu dur=%lldms tid=%lu\n",
           (unsigned long long)as->debugId,
           (long long)(monotonic_ms() - cbStartMs),
           current_tid());
}

static void stream_accepted(NabtoDeviceFuture* future, NabtoDeviceError ec,
                            void* userData)
{
    int64_t cbStartMs = monotonic_ms();
    unsigned long tid = current_tid();
    struct nabtoshell_active_stream* as = userData;
    printf("[STREAMDBG] cb-enter stream_accepted id=%llu ref=%llu ec=%d tid=%lu\n",
           (unsigned long long)as->debugId,
           (unsigned long long)stream_ref(as),
           (int)ec,
           tid);
    nabto_device_future_free(future);

    if (ec != NABTO_DEVICE_EC_OK) {
        printf("[STREAMDBG] stream_accepted ec=%d id=%llu ref=%llu -> cleanup\n",
               (int)ec,
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as));
        cleanup_active_stream(as);
        printf("[STREAMDBG] cb-leave stream_accepted ec=%d id=%llu dur=%lldms tid=%lu\n",
               (int)ec,
               (unsigned long long)as->debugId,
               (long long)(monotonic_ms() - cbStartMs),
               tid);
        return;
    }

    /* IAM check */
    NabtoDeviceConnectionRef ref =
        nabto_device_stream_get_connection_ref(as->stream);
    if (!nabtoshell_iam_check_access_ref(&as->app->iam, ref, "Terminal:Connect")) {
        printf("[STREAMDBG] stream_accepted id=%llu ref=%llu IAM deny\n",
               (unsigned long long)as->debugId, (unsigned long long)ref);
        start_stream_close_once(as, "stream_accepted:iam-deny");
        printf("[STREAMDBG] cb-leave stream_accepted denied id=%llu dur=%lldms tid=%lu\n",
               (unsigned long long)as->debugId,
               (long long)(monotonic_ms() - cbStartMs),
               tid);
        return;
    }

    /* Look up session target */
    struct nabtoshell_session_entry* entry =
        nabtoshell_session_find(&as->app->sessionMap, ref);
    if (entry == NULL) {
        printf("No session target set for connection, closing stream" NEWLINE);
        printf("[STREAMDBG] stream_accepted id=%llu ref=%llu no session map entry\n",
               (unsigned long long)as->debugId, (unsigned long long)ref);
        start_stream_close_once(as, "stream_accepted:no-session-entry");
        printf("[STREAMDBG] cb-leave stream_accepted no-entry id=%llu dur=%lldms tid=%lu\n",
               (unsigned long long)as->debugId,
               (long long)(monotonic_ms() - cbStartMs),
               tid);
        return;
    }

    /* Copy session target and do setup off the SDK callback thread. */
    strncpy(as->sessionName, entry->sessionName, sizeof(as->sessionName) - 1);
    as->sessionName[sizeof(as->sessionName) - 1] = '\0';
    as->sessionCols = entry->cols;
    as->sessionRows = entry->rows;

    if (pthread_create(&as->setupThread, NULL, stream_setup_thread, as) != 0) {
        int err = errno;
        printf("Failed to create stream setup thread" NEWLINE);
        printf("[STREAMDBG] stream_accepted id=%llu ref=%llu setup thread create failed errno=%d (%s)\n",
               (unsigned long long)as->debugId,
               (unsigned long long)ref,
               err,
               strerror(err));
        start_stream_close_once(as, "stream_accepted:setup-thread-create-failed");
        printf("[STREAMDBG] cb-leave stream_accepted setup-create-failed id=%llu dur=%lldms tid=%lu\n",
               (unsigned long long)as->debugId,
               (long long)(monotonic_ms() - cbStartMs),
               tid);
        return;
    }
    as->setupThreadStarted = true;
    printf("[STREAMDBG] stream_accepted id=%llu ref=%llu setup thread started tid=%lu\n",
           (unsigned long long)as->debugId,
           (unsigned long long)ref,
           tid);
    printf("[STREAMDBG] cb-leave stream_accepted ec=0 id=%llu dur=%lldms tid=%lu\n",
           (unsigned long long)as->debugId,
           (long long)(monotonic_ms() - cbStartMs),
           tid);
}

static void* stream_setup_thread(void* arg)
{
    struct nabtoshell_active_stream* as = arg;
    int64_t startMs = monotonic_ms();
    printf("[STREAMDBG] thread-enter stream_setup id=%llu ref=%llu tid=%lu\n",
           (unsigned long long)as->debugId,
           (unsigned long long)stream_ref(as),
           current_tid());

    if (atomic_load(&as->closing)) {
        printf("[STREAMDBG] thread-leave stream_setup id=%llu ref=%llu early-closing dur=%lldms\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (long long)(monotonic_ms() - startMs));
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
        printf("[STREAMDBG] stream_setup id=%llu ref=%llu forkpty failed errno=%d (%s)\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               errno,
               strerror(errno));
        start_stream_close_once(as, "stream_setup:forkpty-failed");
        printf("[STREAMDBG] thread-leave stream_setup id=%llu ref=%llu forkpty-failed dur=%lldms\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (long long)(monotonic_ms() - startMs));
        return NULL;
    }

    if (pid == 0) {
        execlp("tmux", "tmux", "attach-session", "-t", as->sessionName, (char*)NULL);
        _exit(1);
    }

    as->childPid = pid;
    printf("[STREAMDBG] stream_setup id=%llu ref=%llu childPid=%ld ptyFd=%d\n",
           (unsigned long long)as->debugId,
           (unsigned long long)stream_ref(as),
           (long)pid,
           as->ptyFd);

    if (pthread_create(&as->ptyReaderThread, NULL, pty_reader_thread, as) != 0) {
        int err = errno;
        printf("Failed to create PTY->stream thread" NEWLINE);
        printf("[STREAMDBG] stream_setup id=%llu ref=%llu create PTY->stream failed errno=%d (%s)\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               err,
               strerror(err));
        kill(pid, SIGTERM);
        close(as->ptyFd);
        as->ptyFd = -1;
        start_stream_close_once(as, "stream_setup:pty-reader-create-failed");
        printf("[STREAMDBG] thread-leave stream_setup id=%llu ref=%llu pty-thread-failed dur=%lldms\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (long long)(monotonic_ms() - startMs));
        return NULL;
    }
    as->ptyReaderThreadStarted = true;

    if (pthread_create(&as->streamReaderThread, NULL, stream_reader_thread, as) != 0) {
        int err = errno;
        printf("Failed to create stream->PTY thread" NEWLINE);
        printf("[STREAMDBG] stream_setup id=%llu ref=%llu create stream->PTY failed errno=%d (%s)\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               err,
               strerror(err));
        atomic_store(&as->closing, true);
        close(as->ptyFd);
        as->ptyFd = -1;
        start_stream_close_once(as, "stream_setup:stream-reader-create-failed");
        printf("[STREAMDBG] thread-leave stream_setup id=%llu ref=%llu stream-thread-failed dur=%lldms\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (long long)(monotonic_ms() - startMs));
        return NULL;
    }
    as->streamReaderThreadStarted = true;

    printf("[STREAMDBG] thread-leave stream_setup id=%llu ref=%llu ready dur=%lldms\n",
           (unsigned long long)as->debugId,
           (unsigned long long)stream_ref(as),
           (long long)(monotonic_ms() - startMs));
    return NULL;
}

/* Stream reader thread: reads from Nabto stream and writes to PTY. */
static void* stream_reader_thread(void* arg)
{
    struct nabtoshell_active_stream* as = arg;
    int64_t startMs = monotonic_ms();
    printf("[STREAMDBG] thread-enter stream_reader id=%llu ref=%llu tid=%lu\n",
           (unsigned long long)as->debugId,
           (unsigned long long)stream_ref(as),
           current_tid());
    uint8_t buf[NABTOSHELL_STREAM_BUFFER_SIZE];
    size_t readLength = 0;

    while (!atomic_load(&as->closing)) {
        NabtoDeviceFuture* readFuture = nabto_device_future_new(as->device);
        if (readFuture == NULL) {
            printf("[STREAMDBG] stream_reader id=%llu ref=%llu readFuture alloc failed\n",
                   (unsigned long long)as->debugId,
                   (unsigned long long)stream_ref(as));
            break;
        }
        nabto_device_stream_read_some(as->stream, readFuture, buf, sizeof(buf), &readLength);
        NabtoDeviceError ec = nabto_device_future_wait(readFuture);
        nabto_device_future_free(readFuture);

        if (ec != NABTO_DEVICE_EC_OK) {
            printf("[STREAMDBG] stream_reader id=%llu ref=%llu read wait ec=%d\n",
                   (unsigned long long)as->debugId,
                   (unsigned long long)stream_ref(as),
                   (int)ec);
            break;
        }

        if (readLength > 0 && as->ptyFd >= 0) {
            ssize_t written = write(as->ptyFd, buf, readLength);
            (void)written;
            if (written < 0) {
                printf("[STREAMDBG] stream_reader id=%llu ref=%llu write to pty failed errno=%d (%s)\n",
                       (unsigned long long)as->debugId,
                       (unsigned long long)stream_ref(as),
                       errno,
                       strerror(errno));
                break;
            }
        }
    }

    atomic_store(&as->closing, true);
    printf("[STREAMDBG] thread-leave stream_reader id=%llu ref=%llu dur=%lldms -> close\n",
           (unsigned long long)as->debugId,
           (unsigned long long)stream_ref(as),
           (long long)(monotonic_ms() - startMs));
    start_stream_close_once(as, "stream_reader_thread:loop-exit");
    return NULL;
}

/* PTY reader thread: reads from PTY and writes to Nabto stream. */
static void* pty_reader_thread(void* arg)
{
    struct nabtoshell_active_stream* as = arg;
    int64_t startMs = monotonic_ms();
    printf("[STREAMDBG] thread-enter pty_reader id=%llu ref=%llu tid=%lu ptyFd=%d\n",
           (unsigned long long)as->debugId,
           (unsigned long long)stream_ref(as),
           current_tid(),
           as->ptyFd);
    uint8_t buf[NABTOSHELL_STREAM_BUFFER_SIZE];

    while (!atomic_load(&as->closing)) {
        ssize_t n = read(as->ptyFd, buf, sizeof(buf));
        if (n <= 0) {
            if (n < 0) {
                printf("[STREAMDBG] pty_reader id=%llu ref=%llu read ec errno=%d (%s)\n",
                       (unsigned long long)as->debugId,
                       (unsigned long long)stream_ref(as),
                       errno,
                       strerror(errno));
            } else {
                printf("[STREAMDBG] pty_reader id=%llu ref=%llu read EOF\n",
                       (unsigned long long)as->debugId,
                       (unsigned long long)stream_ref(as));
            }
            break;
        }

        /* Write to Nabto stream (blocking) */
        NabtoDeviceFuture* writeFuture = nabto_device_future_new(as->device);
        if (writeFuture == NULL) {
            printf("[STREAMDBG] pty_reader id=%llu ref=%llu writeFuture alloc failed\n",
                   (unsigned long long)as->debugId,
                   (unsigned long long)stream_ref(as));
            break;
        }
        nabto_device_stream_write(as->stream, writeFuture, buf, (size_t)n);
        NabtoDeviceError ec = nabto_device_future_wait(writeFuture);
        nabto_device_future_free(writeFuture);

        if (ec != NABTO_DEVICE_EC_OK) {
            printf("[STREAMDBG] pty_reader id=%llu ref=%llu write wait ec=%d\n",
                   (unsigned long long)as->debugId,
                   (unsigned long long)stream_ref(as),
                   (int)ec);
            break;
        }
    }

    atomic_store(&as->closing, true);
    printf("[STREAMDBG] thread-leave pty_reader id=%llu ref=%llu dur=%lldms -> close\n",
           (unsigned long long)as->debugId,
           (unsigned long long)stream_ref(as),
           (long long)(monotonic_ms() - startMs));
    start_stream_close_once(as, "pty_reader_thread:loop-exit");
    return NULL;
}

static void start_stream_close(struct nabtoshell_active_stream* as)
{
    NabtoDeviceFuture* closeFuture = nabto_device_future_new(as->device);
    if (closeFuture == NULL) {
        printf("[STREAMDBG] close arm failed id=%llu ref=%llu closeFuture alloc failed\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as));
        cleanup_active_stream(as);
        return;
    }
    printf("[STREAMDBG] close arm id=%llu ref=%llu closeFuture=%p tid=%lu\n",
           (unsigned long long)as->debugId,
           (unsigned long long)stream_ref(as),
           (void*)closeFuture,
           current_tid());
    nabto_device_stream_close(as->stream, closeFuture);
    nabto_device_future_set_callback(closeFuture, stream_closed, as);
}

static void stream_closed(NabtoDeviceFuture* future, NabtoDeviceError ec,
                          void* userData)
{
    int64_t cbStartMs = monotonic_ms();
    struct nabtoshell_active_stream* as = userData;
    uint64_t debugId = as->debugId;
    NabtoDeviceConnectionRef ref = stream_ref(as);
    printf("[STREAMDBG] cb-enter stream_closed id=%llu ref=%llu ec=%d tid=%lu\n",
           (unsigned long long)debugId,
           (unsigned long long)ref,
           (int)ec,
           current_tid());
    nabto_device_future_free(future);
    cleanup_active_stream(as);
    printf("[STREAMDBG] cb-leave stream_closed id=%llu ref=%llu dur=%lldms tid=%lu\n",
           (unsigned long long)debugId,
           (unsigned long long)ref,
           (long long)(monotonic_ms() - cbStartMs),
           current_tid());
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
    int64_t startMs = monotonic_ms();
    printf("[STREAMDBG] thread-enter cleanup id=%llu ref=%llu tid=%lu\n",
           (unsigned long long)as->debugId,
           (unsigned long long)stream_ref(as),
           current_tid());

    if (as->setupThreadStarted) {
        int64_t t0 = monotonic_ms();
        printf("[STREAMDBG] cleanup join setup id=%llu ref=%llu\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as));
        pthread_join(as->setupThread, NULL);
        as->setupThreadStarted = false;
        printf("[STREAMDBG] cleanup joined setup id=%llu ref=%llu ms=%lld\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (long long)(monotonic_ms() - t0));
    }

    if (as->ptyFd >= 0) {
        int64_t t0 = monotonic_ms();
        int ptyFd = as->ptyFd;
        printf("[STREAMDBG] cleanup close pty id=%llu ref=%llu ptyFd=%d\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               ptyFd);
        int rc = close(ptyFd);
        int err = errno;
        printf("[STREAMDBG] cleanup close pty done id=%llu ref=%llu ptyFd=%d rc=%d errno=%d (%s) ms=%lld\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               ptyFd,
               rc,
               err,
               strerror(err),
               (long long)(monotonic_ms() - t0));
        as->ptyFd = -1;
    }

    if (as->childPid > 0) {
        int64_t t0 = monotonic_ms();
        pid_t child = as->childPid;
        printf("[STREAMDBG] cleanup SIGTERM child id=%llu ref=%llu child=%ld\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (long)child);
        int rc = kill(child, SIGTERM);
        int err = errno;
        printf("[STREAMDBG] cleanup SIGTERM done id=%llu ref=%llu child=%ld rc=%d errno=%d (%s) ms=%lld\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (long)child,
               rc,
               err,
               strerror(err),
               (long long)(monotonic_ms() - t0));
    }

    if (as->ptyReaderThreadStarted) {
        int64_t t0 = monotonic_ms();
        printf("[STREAMDBG] cleanup join pty_reader id=%llu ref=%llu\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as));
        pthread_join(as->ptyReaderThread, NULL);
        as->ptyReaderThreadStarted = false;
        printf("[STREAMDBG] cleanup joined pty_reader id=%llu ref=%llu ms=%lld\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (long long)(monotonic_ms() - t0));
    }

    if (as->streamReaderThreadStarted) {
        int64_t t0 = monotonic_ms();
        printf("[STREAMDBG] cleanup join stream_reader id=%llu ref=%llu\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as));
        pthread_join(as->streamReaderThread, NULL);
        as->streamReaderThreadStarted = false;
        printf("[STREAMDBG] cleanup joined stream_reader id=%llu ref=%llu ms=%lld\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (long long)(monotonic_ms() - t0));
    }

    if (as->childPid > 0) {
        int status;
        int64_t t0 = monotonic_ms();
        printf("[STREAMDBG] cleanup waitpid id=%llu ref=%llu child=%ld\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (long)as->childPid);
        waitpid(as->childPid, &status, 0);
        as->childPid = -1;
        printf("[STREAMDBG] cleanup waitpid done id=%llu ref=%llu ms=%lld status=%d\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (long long)(monotonic_ms() - t0),
               status);
    }

    if (as->stream != NULL) {
        printf("[STREAMDBG] cleanup stream_free id=%llu ref=%llu stream=%p\n",
               (unsigned long long)as->debugId,
               (unsigned long long)stream_ref(as),
               (void*)as->stream);
        nabto_device_stream_free(as->stream);
        as->stream = NULL;
    }
    printf("[STREAMDBG] thread-leave cleanup id=%llu total=%lldms\n",
           (unsigned long long)as->debugId,
           (long long)(monotonic_ms() - startMs));
    free(as);
    return NULL;
}

static void cleanup_active_stream(struct nabtoshell_active_stream* as)
{
    int64_t startMs = monotonic_ms();
    uint64_t debugId = as->debugId;
    NabtoDeviceConnectionRef ref = stream_ref(as);
    printf("[STREAMDBG] cleanup_active begin id=%llu ref=%llu tid=%lu\n",
           (unsigned long long)debugId,
           (unsigned long long)ref,
           current_tid());
    atomic_store(&as->closing, true);

    /* Remove from session map */
    if (as->stream != NULL && as->app != NULL) {
        int64_t t0 = monotonic_ms();
        NabtoDeviceConnectionRef mapRef =
            nabto_device_stream_get_connection_ref(as->stream);
        printf("[STREAMDBG] cleanup_active session remove id=%llu ref=%llu\n",
               (unsigned long long)debugId,
               (unsigned long long)mapRef);
        nabtoshell_session_remove(&as->app->sessionMap, mapRef);
        printf("[STREAMDBG] cleanup_active session remove done id=%llu ref=%llu ms=%lld\n",
               (unsigned long long)debugId,
               (unsigned long long)mapRef,
               (long long)(monotonic_ms() - t0));
    }

    /* Remove from the linked list */
    if (as->app != NULL) {
        struct nabtoshell_stream_listener* sl = &as->app->streamListener;
        struct nabtoshell_active_stream** pp = &sl->activeStreams;
        while (*pp != NULL) {
            if (*pp == as) {
                *pp = as->next;
                break;
            }
            pp = &(*pp)->next;
        }
        printf("[STREAMDBG] cleanup_active unlink id=%llu ref=%llu activeNow=%zu\n",
               (unsigned long long)debugId,
               (unsigned long long)ref,
               count_active_streams(sl));
    }

    /*
     * Defer blocking operations (pthread_join, waitpid) and final free
     * to a detached thread so we never block the SDK event loop here.
     */
    pthread_t cleanupThread;
    if (pthread_create(&cleanupThread, NULL, cleanup_thread_func, as) == 0) {
        pthread_detach(cleanupThread);
        printf("[STREAMDBG] cleanup_active deferred id=%llu ref=%llu cleanupTid=%lu setupMs=%lld\n",
               (unsigned long long)debugId,
               (unsigned long long)ref,
               (unsigned long)cleanupThread,
               (long long)(monotonic_ms() - startMs));
    } else {
        int err = errno;
        /* Never block SDK callback threads. If cleanup thread creation fails,
         * mark stream as orphaned and return; this is a fail-safe path. */
        printf("[STREAMDBG] cleanup_active thread-create FAILED id=%llu ref=%llu errno=%d (%s) -> ORPHAN (no inline cleanup) tid=%lu\n",
               (unsigned long long)debugId,
               (unsigned long long)ref,
               err,
               strerror(err),
               current_tid());
        if (as->childPid > 0) {
            int killRc = kill(as->childPid, SIGTERM);
            int killErr = errno;
            printf("[STREAMDBG] cleanup_active orphan SIGTERM id=%llu ref=%llu child=%ld rc=%d errno=%d (%s)\n",
                   (unsigned long long)debugId,
                   (unsigned long long)ref,
                   (long)as->childPid,
                   killRc,
                   killErr,
                   strerror(killErr));
        }
    }
}
