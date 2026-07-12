/* Mako event loop — kqueue (macOS/BSD) / epoll (Linux) / IOCP (Windows).
 * Handles thousands of concurrent connections with one thread.
 * Designed for: Redis-like servers, SIP/RTP media, WebSocket, video backends. */
#ifndef MAKO_EVLOOP_H
#define MAKO_EVLOOP_H

#include "mako_rt.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#if defined(_WIN32)
/* ---- Windows IOCP / Winsock ---- */
#define MAKO_USE_IOCP 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")
typedef SOCKET mako_sock_ev_t;
#define MAKO_INVALID_SOCK INVALID_SOCKET
#define MAKO_SOCK_CLOSE closesocket

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
/* ---- kqueue ---- */
#define MAKO_USE_KQUEUE 1
#include <fcntl.h>
#include <unistd.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
typedef int mako_sock_ev_t;
#define MAKO_INVALID_SOCK (-1)
#define MAKO_SOCK_CLOSE close

#elif defined(__linux__)
/* ---- epoll ---- */
#define MAKO_USE_EPOLL 1
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
typedef int mako_sock_ev_t;
#define MAKO_INVALID_SOCK (-1)
#define MAKO_SOCK_CLOSE close

#else
#error "Unsupported platform: need kqueue, epoll, or IOCP"
#endif

/* Event flags */
#define MAKO_EV_READ   1
#define MAKO_EV_WRITE  2
#define MAKO_EV_ERROR  4
#define MAKO_EV_HUP    8
#define MAKO_EV_ET     16  /* edge-triggered */

#define MAKO_EVLOOP_MAX_EVENTS 4096

typedef struct {
    int fd;
    int flags;
} MakoEvent;

typedef struct {
    int backend_fd;     /* kqueue/epoll fd, or -1 for IOCP */
    MakoEvent events[MAKO_EVLOOP_MAX_EVENTS];
    int ready_count;
#if MAKO_USE_KQUEUE
    struct kevent kevents[MAKO_EVLOOP_MAX_EVENTS];
#elif MAKO_USE_EPOLL
    struct epoll_event eevents[MAKO_EVLOOP_MAX_EVENTS];
#elif MAKO_USE_IOCP
    HANDLE iocp;
    OVERLAPPED_ENTRY entries[MAKO_EVLOOP_MAX_EVENTS];
#endif
} MakoEvLoop;

/* ---- Platform helpers ---- */

static inline void mako_ev_set_nonblock(mako_sock_ev_t fd) {
#if defined(_WIN32)
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

#if defined(_WIN32)
static inline void mako_wsa_init(void) {
    static int done = 0;
    if (!done) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        done = 1;
    }
}
#endif

/* ---- Event loop core ---- */

static inline MakoEvLoop *mako_evloop_new(void) {
    MakoEvLoop *el = (MakoEvLoop *)calloc(1, sizeof(MakoEvLoop));
    if (!el) return NULL;
    el->ready_count = 0;

#if MAKO_USE_KQUEUE
    el->backend_fd = kqueue();
    if (el->backend_fd < 0) { free(el); return NULL; }
#elif MAKO_USE_EPOLL
    el->backend_fd = epoll_create1(EPOLL_CLOEXEC);
    if (el->backend_fd < 0) { free(el); return NULL; }
#elif MAKO_USE_IOCP
    mako_wsa_init();
    el->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (!el->iocp) { free(el); return NULL; }
    el->backend_fd = -1;
#endif

    return el;
}

static inline int64_t mako_evloop_add(MakoEvLoop *el, int64_t fd, int64_t flags) {
    if (!el || fd < 0) return -1;
    mako_ev_set_nonblock((mako_sock_ev_t)fd);

#if MAKO_USE_KQUEUE
    struct kevent changes[2];
    int nchanges = 0;
    unsigned short kflags = EV_ADD | EV_ENABLE;
    if (flags & MAKO_EV_ET) kflags |= EV_CLEAR;

    if (flags & MAKO_EV_READ) {
        EV_SET(&changes[nchanges], (uintptr_t)fd, EVFILT_READ, kflags, 0, 0, NULL);
        nchanges++;
    }
    if (flags & MAKO_EV_WRITE) {
        EV_SET(&changes[nchanges], (uintptr_t)fd, EVFILT_WRITE, kflags, 0, 0, NULL);
        nchanges++;
    }
    return kevent(el->backend_fd, changes, nchanges, NULL, 0, NULL) == 0 ? 0 : -1;

#elif MAKO_USE_EPOLL
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = (int)fd;
    if (flags & MAKO_EV_READ) ev.events |= EPOLLIN;
    if (flags & MAKO_EV_WRITE) ev.events |= EPOLLOUT;
    if (flags & MAKO_EV_ET) ev.events |= EPOLLET;
    return epoll_ctl(el->backend_fd, EPOLL_CTL_ADD, (int)fd, &ev) == 0 ? 0 : -1;

#elif MAKO_USE_IOCP
    /* Associate socket with IOCP. Key = fd value. */
    HANDLE r = CreateIoCompletionPort((HANDLE)(uintptr_t)fd, el->iocp,
                                      (ULONG_PTR)fd, 0);
    return r ? 0 : -1;
#endif
}

static inline int64_t mako_evloop_mod(MakoEvLoop *el, int64_t fd, int64_t flags) {
    if (!el || fd < 0) return -1;

#if MAKO_USE_KQUEUE
    struct kevent changes[4];
    int n = 0;
    EV_SET(&changes[n++], (uintptr_t)fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&changes[n++], (uintptr_t)fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(el->backend_fd, changes, n, NULL, 0, NULL);

    n = 0;
    unsigned short kflags = EV_ADD | EV_ENABLE;
    if (flags & MAKO_EV_ET) kflags |= EV_CLEAR;
    if (flags & MAKO_EV_READ) {
        EV_SET(&changes[n++], (uintptr_t)fd, EVFILT_READ, kflags, 0, 0, NULL);
    }
    if (flags & MAKO_EV_WRITE) {
        EV_SET(&changes[n++], (uintptr_t)fd, EVFILT_WRITE, kflags, 0, 0, NULL);
    }
    return kevent(el->backend_fd, changes, n, NULL, 0, NULL) == 0 ? 0 : -1;

#elif MAKO_USE_EPOLL
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = (int)fd;
    if (flags & MAKO_EV_READ) ev.events |= EPOLLIN;
    if (flags & MAKO_EV_WRITE) ev.events |= EPOLLOUT;
    if (flags & MAKO_EV_ET) ev.events |= EPOLLET;
    return epoll_ctl(el->backend_fd, EPOLL_CTL_MOD, (int)fd, &ev) == 0 ? 0 : -1;

#elif MAKO_USE_IOCP
    /* IOCP doesn't need mod — re-post for next operation */
    return 0;
#endif
}

static inline int64_t mako_evloop_del(MakoEvLoop *el, int64_t fd) {
    if (!el || fd < 0) return -1;

#if MAKO_USE_KQUEUE
    struct kevent changes[2];
    EV_SET(&changes[0], (uintptr_t)fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&changes[1], (uintptr_t)fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(el->backend_fd, changes, 2, NULL, 0, NULL);
    return 0;

#elif MAKO_USE_EPOLL
    return epoll_ctl(el->backend_fd, EPOLL_CTL_DEL, (int)fd, NULL) == 0 ? 0 : -1;

#elif MAKO_USE_IOCP
    /* IOCP: fd is auto-removed when closed */
    return 0;
#endif
}

static inline int64_t mako_evloop_wait(MakoEvLoop *el, int64_t timeout_ms) {
    if (!el) return -1;

#if MAKO_USE_KQUEUE
    struct timespec ts;
    struct timespec *pts = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        pts = &ts;
    }
    int n = kevent(el->backend_fd, NULL, 0, el->kevents, MAKO_EVLOOP_MAX_EVENTS, pts);
    if (n < 0) { el->ready_count = 0; return -1; }

    for (int i = 0; i < n; i++) {
        el->events[i].fd = (int)el->kevents[i].ident;
        el->events[i].flags = 0;
        if (el->kevents[i].filter == EVFILT_READ) el->events[i].flags |= MAKO_EV_READ;
        if (el->kevents[i].filter == EVFILT_WRITE) el->events[i].flags |= MAKO_EV_WRITE;
        if (el->kevents[i].flags & EV_EOF) el->events[i].flags |= MAKO_EV_HUP;
        if (el->kevents[i].flags & EV_ERROR) el->events[i].flags |= MAKO_EV_ERROR;
    }
    el->ready_count = n;
    return (int64_t)n;

#elif MAKO_USE_EPOLL
    int n = epoll_wait(el->backend_fd, el->eevents, MAKO_EVLOOP_MAX_EVENTS, (int)timeout_ms);
    if (n < 0) { el->ready_count = 0; return -1; }

    for (int i = 0; i < n; i++) {
        el->events[i].fd = el->eevents[i].data.fd;
        el->events[i].flags = 0;
        if (el->eevents[i].events & EPOLLIN) el->events[i].flags |= MAKO_EV_READ;
        if (el->eevents[i].events & EPOLLOUT) el->events[i].flags |= MAKO_EV_WRITE;
        if (el->eevents[i].events & EPOLLHUP) el->events[i].flags |= MAKO_EV_HUP;
        if (el->eevents[i].events & EPOLLERR) el->events[i].flags |= MAKO_EV_ERROR;
    }
    el->ready_count = n;
    return (int64_t)n;

#elif MAKO_USE_IOCP
    ULONG count = 0;
    BOOL ok = GetQueuedCompletionStatusEx(el->iocp, el->entries,
                                          MAKO_EVLOOP_MAX_EVENTS, &count,
                                          timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms,
                                          FALSE);
    if (!ok) { el->ready_count = 0; return count > 0 ? (int64_t)count : -1; }

    for (ULONG i = 0; i < count; i++) {
        el->events[i].fd = (int)el->entries[i].lpCompletionKey;
        el->events[i].flags = MAKO_EV_READ; /* IOCP signals completion, treat as readable */
    }
    el->ready_count = (int)count;
    return (int64_t)count;
#endif
}

static inline int64_t mako_evloop_event_fd(MakoEvLoop *el, int64_t index) {
    if (!el || index < 0 || index >= el->ready_count) return -1;
    return (int64_t)el->events[index].fd;
}

static inline int64_t mako_evloop_event_flags(MakoEvLoop *el, int64_t index) {
    if (!el || index < 0 || index >= el->ready_count) return 0;
    return (int64_t)el->events[index].flags;
}

static inline int64_t mako_evloop_close(MakoEvLoop *el) {
    if (!el) return -1;
#if MAKO_USE_IOCP
    if (el->iocp) CloseHandle(el->iocp);
#else
    if (el->backend_fd >= 0) close(el->backend_fd);
#endif
    free(el);
    return 0;
}

/* Graceful shutdown: close backend (kqueue/epoll/IOCP) and free the loop.
 * Callers must close application sockets separately (or via close_listeners).
 * Safe to call once; subsequent ops on el are invalid. */
static inline int64_t mako_evloop_shutdown(MakoEvLoop *el) {
    return mako_evloop_close(el);
}

/* ---- Non-blocking TCP helpers ---- */

static inline int64_t mako_nb_listen(int64_t port) {
#if defined(_WIN32)
    mako_wsa_init();
    SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) return -1;
#else
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
#endif

    int yes = 1;
    setsockopt((int)fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
#if !defined(_WIN32) && defined(SO_REUSEPORT)
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind((int)fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        MAKO_SOCK_CLOSE((mako_sock_ev_t)fd);
        return -1;
    }
    if (listen((int)fd, 4096) < 0) {
        MAKO_SOCK_CLOSE((mako_sock_ev_t)fd);
        return -1;
    }

    mako_ev_set_nonblock((mako_sock_ev_t)fd);
    return (int64_t)fd;
}

static inline int64_t mako_nb_accept(int64_t listen_fd) {
#if defined(_WIN32)
    SOCKET cfd = accept((SOCKET)listen_fd, NULL, NULL);
    if (cfd == INVALID_SOCKET) return -1;
#else
    int cfd = accept((int)listen_fd, NULL, NULL);
    if (cfd < 0) return -1;
#endif
    mako_ev_set_nonblock((mako_sock_ev_t)cfd);
    int yes = 1;
    setsockopt((int)cfd, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof(yes));
    return (int64_t)cfd;
}

static inline MakoString mako_nb_read(int64_t fd) {
    char buf[65536];
    ssize_t n = recv((int)fd, buf, sizeof(buf), 0);
    if (n <= 0) return mako_str_from_cstr("");
    char *d = (char *)malloc((size_t)n + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, buf, (size_t)n);
    d[n] = 0;
    return (MakoString){d, (size_t)n};
}

static inline int64_t mako_nb_write(int64_t fd, MakoString data) {
    if (fd < 0 || !data.data || data.len == 0) return -1;
    ssize_t n = send((int)fd, data.data, data.len, 0);
    if (n < 0) {
#if defined(_WIN32)
        if (WSAGetLastError() == WSAEWOULDBLOCK) return 0;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
#endif
    }
    return (int64_t)n;
}

static inline int64_t mako_nb_udp_bind(int64_t port) {
#if defined(_WIN32)
    mako_wsa_init();
    SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == INVALID_SOCKET) return -1;
#else
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
#endif

    int yes = 1;
    setsockopt((int)fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind((int)fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        MAKO_SOCK_CLOSE((mako_sock_ev_t)fd);
        return -1;
    }

    mako_ev_set_nonblock((mako_sock_ev_t)fd);
    return (int64_t)fd;
}

static inline MakoString mako_nb_udp_recv(int64_t fd) {
    char buf[65536];
    ssize_t n = recvfrom((int)fd, buf, sizeof(buf), 0, NULL, NULL);
    if (n <= 0) return mako_str_from_cstr("");
    char *d = (char *)malloc((size_t)n + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, buf, (size_t)n);
    d[n] = 0;
    return (MakoString){d, (size_t)n};
}

static inline int64_t mako_nb_close(int64_t fd) {
    if (fd < 0) return -1;
    return MAKO_SOCK_CLOSE((mako_sock_ev_t)fd) == 0 ? 0 : -1;
}

#endif /* MAKO_EVLOOP_H */
