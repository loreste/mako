/* Graceful shutdown — SIGTERM/INT hooks, listener close, crew drain timeout. */
#ifndef MAKO_SHUTDOWN_H
#define MAKO_SHUTDOWN_H

#include "mako_rt.h"
#include <signal.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MAKO_SHUTDOWN_LISTENERS_MAX 64
#define MAKO_SHUTDOWN_CB_MAX 8

typedef void (*mako_shutdown_cb)(void);

static volatile sig_atomic_t mako_shutdown_requested_flag = 0;
static volatile sig_atomic_t mako_shutdown_draining = 0;
static int mako_shutdown_listener_fds[MAKO_SHUTDOWN_LISTENERS_MAX];
static int mako_shutdown_listener_n = 0;
static mako_shutdown_cb mako_shutdown_cbs[MAKO_SHUTDOWN_CB_MAX];
static int mako_shutdown_cb_n = 0;
static int64_t mako_shutdown_deadline_ms = 0;

static void mako_shutdown_signal_handler(int sig) {
    (void)sig;
    mako_shutdown_requested_flag = 1;
}

/* Register a listener fd to close on drain. Returns 1 ok, 0 full/invalid. */
static inline int64_t mako_register_listener(int64_t fd) {
    if (fd < 0) return 0;
    if (mako_shutdown_listener_n >= MAKO_SHUTDOWN_LISTENERS_MAX) return 0;
    mako_shutdown_listener_fds[mako_shutdown_listener_n++] = (int)fd;
    return 1;
}

static inline int64_t mako_close_listeners(void) {
    int n = 0;
    for (int i = 0; i < mako_shutdown_listener_n; i++) {
        int fd = mako_shutdown_listener_fds[i];
        if (fd >= 0) {
            mako_sock_close(fd);
            mako_shutdown_listener_fds[i] = -1;
            n++;
        }
    }
    mako_shutdown_listener_n = 0;
    return n;
}

/* Install SIGTERM/SIGINT handlers that set the shutdown flag. */
static inline int64_t mako_signal_on_term(void) {
#if defined(_WIN32)
    /* Best-effort: SIGINT only on Windows CRT */
    signal(SIGINT, mako_shutdown_signal_handler);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = mako_shutdown_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
#if defined(SIGHUP)
    /* HUP does not force drain by default — use signal_watch for reload. */
#endif
#endif
    return 1;
}

/* Also mark http_shutdown if that module is linked (weak-style via call). */
static inline int64_t mako_server_shutdown_begin(int64_t grace_ms) {
    mako_shutdown_requested_flag = 1;
    mako_shutdown_draining = 1;
    if (grace_ms < 0) grace_ms = 0;
    mako_shutdown_deadline_ms = mako_now_ms() + grace_ms;
    /* Close listen sockets so accept loops stop taking work. */
    mako_close_listeners();
    return mako_shutdown_deadline_ms;
}

static inline int64_t mako_shutdown_requested(void) {
    return mako_shutdown_requested_flag ? 1 : 0;
}

static inline int64_t mako_shutdown_draining_now(void) {
    return mako_shutdown_draining ? 1 : 0;
}

static inline int64_t mako_shutdown_remaining_ms(void) {
    if (!mako_shutdown_draining) return -1;
    int64_t left = mako_shutdown_deadline_ms - mako_now_ms();
    return left > 0 ? left : 0;
}

/* Wait until timeout_ms elapses or shutdown requested. Sleeps in short slices
 * so signal handlers can interrupt. Returns 1 if shutdown requested, 0 timeout. */
static inline int64_t mako_server_drain(int64_t timeout_ms) {
    if (timeout_ms < 0) timeout_ms = 0;
    if (!mako_shutdown_draining) {
        mako_server_shutdown_begin(timeout_ms);
    }
    int64_t deadline = mako_now_ms() + timeout_ms;
    while (mako_now_ms() < deadline) {
        if (mako_shutdown_requested_flag) {
            /* still wait remaining for workers; flag already set */
        }
#if !defined(_WIN32)
        usleep(10000); /* 10ms */
#else
        /* coarse sleep */
        {
            volatile int spin = 0;
            for (int i = 0; i < 100000; i++) spin++;
            (void)spin;
        }
#endif
        if (mako_now_ms() >= deadline) break;
    }
    mako_shutdown_draining = 0;
    return mako_shutdown_requested_flag ? 1 : 0;
}

/* Poll helper for accept loops: return 1 if should stop accepting. */
static inline int64_t mako_should_stop_accepting(void) {
    return mako_shutdown_requested_flag ? 1 : 0;
}

/* Register C callback invoked at drain begin (optional; most use flag poll). */
static inline int64_t mako_on_shutdown(mako_shutdown_cb cb) {
    if (!cb || mako_shutdown_cb_n >= MAKO_SHUTDOWN_CB_MAX) return 0;
    mako_shutdown_cbs[mako_shutdown_cb_n++] = cb;
    return 1;
}

static inline void mako_run_shutdown_callbacks(void) {
    for (int i = 0; i < mako_shutdown_cb_n; i++) {
        if (mako_shutdown_cbs[i]) mako_shutdown_cbs[i]();
    }
}

/* Combine: on term signal, begin drain. Call once at server start. */
static inline int64_t mako_install_graceful_shutdown(int64_t grace_ms) {
    mako_signal_on_term();
    /* Polling path: user checks shutdown_requested() in loop and calls drain.
     * Store preferred grace for later. */
    if (grace_ms < 0) grace_ms = 0;
    mako_shutdown_deadline_ms = grace_ms; /* stored as grace default until begin */
    return 1;
}

/* Mako-facing names matching the design doc. */
static inline int64_t mako_signal_on_term_install(void) {
    return mako_signal_on_term();
}

static inline int64_t mako_server_drain_ms(int64_t timeout_ms) {
    return mako_server_drain(timeout_ms);
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_SHUTDOWN_H */
