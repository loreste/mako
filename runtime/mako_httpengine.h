/* Mako HTTP Engine — zero-allocation, multi-core, pre-computed responses.
 * Each worker: own kqueue/epoll + calls accept() on shared listener.
 * Hot path: recv → memcmp route → writev pre-built response. Zero malloc. */
#ifndef MAKO_HTTPENGINE_H
#define MAKO_HTTPENGINE_H

#include "mako_rt.h"
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32)
/* HTTP Engine is POSIX-only for now (uses kqueue/epoll + writev). */
typedef struct { int dummy; } MakoHttpEngine;
static inline MakoHttpEngine *mako_httpengine_new(void) { return NULL; }
static inline int64_t mako_httpengine_route(MakoHttpEngine *e, MakoString m, MakoString p, int64_t h) { (void)e;(void)m;(void)p;(void)h; return -1; }
static inline int64_t mako_httpengine_start(MakoHttpEngine *e, int64_t p) { (void)e;(void)p; return -1; }
static inline void mako_httpengine_stop(MakoHttpEngine *e) { (void)e; }
static inline void mako_httpengine_free(MakoHttpEngine *e) { (void)e; }
#else /* POSIX */

#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#define HENG_USE_KQUEUE 1
#elif defined(__linux__)
#include <sys/epoll.h>
#define HENG_USE_EPOLL 1
#endif

#define HENG_MAX_ROUTES    64
#define HENG_MAX_EVENTS    2048
#define HENG_READ_BUF      65536

typedef struct {
    char *data;
    size_t len;
} HengResponse;

typedef struct {
    const char *path;
    size_t path_len;
    HengResponse resp;
} HengRoute;

typedef struct {
    int kq;                         /* kqueue/epoll fd */
    int listen_fd;                  /* shared listener */
    char read_buf[HENG_READ_BUF];  /* per-thread read buffer */
    HengRoute *routes;
    int num_routes;
    HengResponse default_404;
    volatile int *running;
} HengWorker;

typedef struct {
    int listen_fd;
    int num_workers;
    pthread_t *threads;
    HengWorker *workers;
    HengRoute routes[HENG_MAX_ROUTES];
    int num_routes;
    HengResponse default_404;
    volatile int running;
} MakoHttpEngine;

static inline void heng_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static inline HengResponse heng_build_response(int status, const char *ctype, const char *body) {
    size_t body_len = strlen(body);
    const char *st = "OK";
    if (status == 404) st = "Not Found";
    else if (status == 429) st = "Too Many Requests";
    else if (status == 503) st = "Service Unavailable";
    char hdr[512];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: keep-alive\r\nServer: mako/1.0\r\n\r\n",
        status, st, ctype, body_len);
    size_t total = (size_t)hdr_len + body_len;
    char *buf = (char *)malloc(total);
    memcpy(buf, hdr, (size_t)hdr_len);
    memcpy(buf + hdr_len, body, body_len);
    return (HengResponse){buf, total};
}

/* Match path in raw buffer without allocation */
static inline int heng_path_match(const char *buf, size_t buf_len, size_t start, const char *path, size_t path_len) {
    size_t i = start;
    while (i < buf_len && buf[i] != ' ') i++;  /* skip method */
    /* `>=`: buf[i + path_len] is read below, so that byte must be in range. */
    if (++i + path_len >= buf_len) return 0;
    if (memcmp(buf + i, path, path_len) != 0) return 0;
    char next = buf[i + path_len];
    return (next == ' ' || next == '?' || next == '\r');
}

static inline ssize_t heng_find_req_end(const char *buf, size_t len, size_t start) {
    for (size_t i = start; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n')
            return (ssize_t)(i + 4);
    }
    return -1;
}

/* ---- Worker ---- */

static void heng_handle_data(HengWorker *w, int fd) {
    ssize_t nr = recv(fd, w->read_buf, HENG_READ_BUF, 0);
    if (nr <= 0) { close(fd); return; }

    struct iovec iov[128];
    int iov_count = 0;
    size_t pos = 0;

    while (pos < (size_t)nr && iov_count < 128) {
        ssize_t req_end = heng_find_req_end(w->read_buf, (size_t)nr, pos);

        int matched = 0;
        for (int r = 0; r < w->num_routes; r++) {
            if (heng_path_match(w->read_buf, (size_t)nr, pos,
                               w->routes[r].path, w->routes[r].path_len)) {
                iov[iov_count].iov_base = w->routes[r].resp.data;
                iov[iov_count].iov_len = w->routes[r].resp.len;
                iov_count++;
                matched = 1;
                break;
            }
        }
        if (!matched) {
            iov[iov_count].iov_base = w->default_404.data;
            iov[iov_count].iov_len = w->default_404.len;
            iov_count++;
        }
        if (req_end < 0) break;
        pos = (size_t)req_end;
    }

    if (iov_count > 0) writev(fd, iov, iov_count);
}

static void *heng_worker_run(void *arg) {
    HengWorker *w = (HengWorker *)arg;

#if HENG_USE_KQUEUE
    w->kq = kqueue();
    struct kevent change;
    EV_SET(&change, (uintptr_t)w->listen_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
    kevent(w->kq, &change, 1, NULL, 0, NULL);

    struct kevent events[HENG_MAX_EVENTS];
    while (*w->running) {
        int n = kevent(w->kq, NULL, 0, events, HENG_MAX_EVENTS, NULL);
        for (int i = 0; i < n; i++) {
            int fd = (int)events[i].ident;

            if (fd == w->listen_fd) {
                /* Accept as many as possible */
                for (;;) {
                    int cfd = accept(w->listen_fd, NULL, NULL);
                    if (cfd < 0) break;
                    heng_set_nonblock(cfd);
                    int yes = 1;
                    setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
                    EV_SET(&change, (uintptr_t)cfd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
                    kevent(w->kq, &change, 1, NULL, 0, NULL);
                }
                continue;
            }

            if (events[i].flags & (EV_EOF | EV_ERROR)) {
                close(fd);
                continue;
            }

            heng_handle_data(w, fd);
        }
    }

#elif HENG_USE_EPOLL
    w->kq = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = w->listen_fd;
    epoll_ctl(w->kq, EPOLL_CTL_ADD, w->listen_fd, &ev);

    struct epoll_event events[HENG_MAX_EVENTS];
    while (*w->running) {
        int n = epoll_wait(w->kq, events, HENG_MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == w->listen_fd) {
                for (;;) {
                    int cfd = accept(w->listen_fd, NULL, NULL);
                    if (cfd < 0) break;
                    heng_set_nonblock(cfd);
                    int yes = 1;
                    setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = cfd;
                    epoll_ctl(w->kq, EPOLL_CTL_ADD, cfd, &ev);
                }
                continue;
            }

            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                close(fd);
                continue;
            }

            heng_handle_data(w, fd);
        }
    }
#endif

    return NULL;
}

/* ---- Public API ---- */

static inline MakoHttpEngine *mako_httpengine_new(int64_t port, int64_t num_workers) {
    MakoHttpEngine *e = (MakoHttpEngine *)calloc(1, sizeof(MakoHttpEngine));
    if (!e) return NULL;
    if (num_workers <= 0) num_workers = 4;
    if (num_workers > 32) num_workers = 32;
    e->num_workers = (int)num_workers;
    e->running = 1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { free(e); return NULL; }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); free(e); return NULL; }
    if (listen(fd, 65535) < 0) { close(fd); free(e); return NULL; }
    heng_set_nonblock(fd);
    e->listen_fd = fd;
    e->default_404 = heng_build_response(404, "application/json", "{\"error\":\"not found\"}\n");
    return e;
}

static inline void mako_httpengine_route(MakoHttpEngine *e, MakoString path, int64_t status, MakoString ctype, MakoString body) {
    if (!e || e->num_routes >= HENG_MAX_ROUTES) return;
    int idx = e->num_routes++;
    char *p = (char *)malloc(path.len + 1);
    memcpy(p, path.data, path.len);
    p[path.len] = 0;
    e->routes[idx].path = p;
    e->routes[idx].path_len = path.len;

    char ct[128] = {0}, bd[8192] = {0};
    memcpy(ct, ctype.data, ctype.len < 127 ? ctype.len : 127);
    memcpy(bd, body.data, body.len < 8191 ? body.len : 8191);
    e->routes[idx].resp = heng_build_response((int)status, ct, bd);
}

static inline int64_t mako_httpengine_serve(MakoHttpEngine *e) {
    if (!e) return -1;
    e->workers = (HengWorker *)calloc((size_t)e->num_workers, sizeof(HengWorker));
    e->threads = (pthread_t *)calloc((size_t)e->num_workers, sizeof(pthread_t));

    for (int i = 0; i < e->num_workers; i++) {
        e->workers[i].listen_fd = e->listen_fd;
        e->workers[i].routes = e->routes;
        e->workers[i].num_routes = e->num_routes;
        e->workers[i].default_404 = e->default_404;
        e->workers[i].running = &e->running;
        pthread_create(&e->threads[i], NULL, heng_worker_run, &e->workers[i]);
    }

    /* Main thread also works as a worker */
    HengWorker main_w = {0};
    main_w.listen_fd = e->listen_fd;
    main_w.routes = e->routes;
    main_w.num_routes = e->num_routes;
    main_w.default_404 = e->default_404;
    main_w.running = &e->running;
    heng_worker_run(&main_w);
    return 0;
}

#endif /* !_WIN32 (POSIX) */

#endif /* MAKO_HTTPENGINE_H */
