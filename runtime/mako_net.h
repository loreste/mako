/* Mako net — TCP listen/accept (POSIX). See docs/STDLIB.md */
#ifndef MAKO_NET_H
#define MAKO_NET_H

#include "mako_rt.h"
#include <errno.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline int64_t mako_tcp_listen(int64_t port) {
    if (!mako_net_init()) return -1;
    mako_sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == MAKO_INVALID_SOCK) {
        fprintf(stderr, "error: tcp: socket() failed\n");
        return -1;
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "error: tcp: bind(:%lld) failed\n", (long long)port);
        mako_sock_close(fd);
        return -1;
    }
    if (listen(fd, 128) < 0) {
        fprintf(stderr, "error: tcp: listen failed\n");
        mako_sock_close(fd);
        return -1;
    }
    return (int64_t)fd;
}

static inline int64_t mako_tcp_accept(int64_t listen_fd) {
    int cfd = accept((int)listen_fd, NULL, NULL);
    if (cfd < 0) {
        fprintf(stderr, "error: tcp: accept failed\n");
        return -1;
    }
    return (int64_t)cfd;
}

static inline int64_t mako_tcp_close(int64_t fd) {
    if (fd < 0) return 0;
    return mako_sock_close((int)fd) == 0 ? 1 : 0;
}

/* Connect to host:port (IPv4 dotted). Returns fd or -1. */
static inline int64_t mako_tcp_connect(MakoString host, int64_t port) {
    char hbuf[256];
    if (host.len >= sizeof(hbuf)) return -1;
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, hbuf, &addr.sin_addr) != 1) {
        mako_sock_close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        mako_sock_close(fd);
        return -1;
    }
    return (int64_t)fd;
}

static inline int64_t mako_tcp_write(int64_t fd, MakoString s) {
    if (fd < 0) return -1;
    ssize_t n = send((int)fd, s.data, s.len, 0);
    return (int64_t)n;
}

/* Read up to 512 bytes; returns count (also prints to stdout for demos). */
static inline int64_t mako_tcp_read_print(int64_t fd) {
    char buf[512];
    ssize_t n = recv((int)fd, buf, sizeof(buf), 0);
    if (n <= 0) return 0;
    fwrite(buf, 1, (size_t)n, stdout);
    return (int64_t)n;
}

/* Read up to 65536 bytes from fd; returns data as MakoString. */
static inline MakoString mako_tcp_read(int64_t fd) {
    char buf[65536];
    ssize_t n = recv((int)fd, buf, sizeof(buf), 0);
    if (n <= 0) return mako_str_from_cstr("");
    char *d = (char *)malloc((size_t)n + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, buf, (size_t)n);
    d[n] = 0;
    return (MakoString){d, (size_t)n};
}

/* Disable Nagle's algorithm for low-latency writes. */
static inline int64_t mako_tcp_nodelay(int64_t fd) {
    int flag = 1;
    return setsockopt((int)fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == 0 ? 1 : 0;
}

/* Reverse-proxy upstream forward: open an HTTP/1.1 connection to host:port, send
 * `method path` with `body`, read the whole response, and return its body (the
 * bytes after the header block). "" on connect failure. Uses Connection: close,
 * so it works with any plain-HTTP backend. */
static inline MakoString mako_http_forward(MakoString host, int64_t port,
                                           MakoString method, MakoString path,
                                           MakoString body) {
    int64_t fd = mako_tcp_connect(host, port);
    if (fd < 0) return mako_str_from_cstr("");
    char hbuf[256];
    size_t hn = (host.len < sizeof(hbuf) - 1) ? host.len : sizeof(hbuf) - 1;
    memcpy(hbuf, host.data ? host.data : "", hn);
    hbuf[hn] = 0;
    const char *m = (method.len && method.data) ? method.data : "GET";
    int mlen = (int)(method.len ? method.len : 3);
    const char *pp = (path.len && path.data) ? path.data : "/";
    int plen = (int)(path.len ? path.len : 1);
    char head[2560];
    int hlen = snprintf(head, sizeof(head),
        "%.*s %.*s HTTP/1.1\r\nHost: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        mlen, m, plen, pp, hbuf, body.len);
    if (hlen < 0 || (size_t)hlen >= sizeof(head)) { mako_tcp_close(fd); return mako_str_from_cstr(""); }
    mako_tcp_write(fd, (MakoString){head, (size_t)hlen});
    if (body.len) mako_tcp_write(fd, body);
    char *buf = NULL;
    size_t total = 0;
    for (;;) {
        MakoString chunk = mako_tcp_read(fd);
        if (chunk.len == 0) { free(chunk.data); break; }
        char *nb = (char *)realloc(buf, total + chunk.len);
        if (!nb) { free(chunk.data); break; }
        buf = nb;
        memcpy(buf + total, chunk.data, chunk.len);
        total += chunk.len;
        free(chunk.data);
    }
    mako_tcp_close(fd);
    if (!buf) return mako_str_from_cstr("");
    size_t bstart = total; /* default: no body */
    for (size_t i = 0; i + 3 < total; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            bstart = i + 4;
            break;
        }
    }
    size_t blen = total - bstart;
    char *out = (char *)malloc(blen + 1);
    if (!out) { free(buf); return mako_str_from_cstr(""); }
    memcpy(out, buf + bstart, blen);
    out[blen] = 0;
    free(buf);
    return (MakoString){out, blen};
}

/* ---- UDP datagram helpers (IPv4 dotted hosts) ---- */

static inline int64_t mako_udp_bind(int64_t port) {
    if (!mako_net_init()) return -1;
    mako_sock_t fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == MAKO_INVALID_SOCK) {
        fprintf(stderr, "error: udp: socket() failed: %s\n", strerror(errno));
        return -1;
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "error: udp: bind(:%lld) failed: %s\n", (long long)port, strerror(errno));
        mako_sock_close(fd);
        return -1;
    }
    return (int64_t)fd;
}

static inline int64_t mako_udp_send_to(int64_t fd, MakoString host, int64_t port, MakoString data) {
    if (fd < 0) return -1;
    char hbuf[256];
    if (host.len >= sizeof(hbuf)) return -1;
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, hbuf, &addr.sin_addr) != 1) return -1;
    ssize_t n = sendto((mako_sock_t)fd, data.data, data.len, 0, (struct sockaddr *)&addr, sizeof(addr));
    return (int64_t)n;
}

static inline MakoString mako_udp_recv(int64_t fd, int64_t max_bytes) {
    if (fd < 0) return mako_str_from_cstr("");
    if (max_bytes <= 0 || max_bytes > 65535) max_bytes = 65535;
    char *buf = (char *)malloc((size_t)max_bytes + 1);
    if (!buf) mako_abort("udp_recv: out of memory");
    ssize_t n = recvfrom((mako_sock_t)fd, buf, (size_t)max_bytes, 0, NULL, NULL);
    if (n <= 0) {
        free(buf);
        return mako_str_from_cstr("");
    }
    buf[n] = 0;
    MakoString out = {buf, (size_t)n};
    return out;
}

static inline int64_t mako_udp_local_port(int64_t fd) {
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    if (getsockname((mako_sock_t)fd, (struct sockaddr *)&addr, &len) != 0) return -1;
    return (int64_t)ntohs(addr.sin_port);
}

static inline int64_t mako_udp_close(int64_t fd) {
    if (fd < 0) return 0;
    return mako_sock_close((mako_sock_t)fd) == 0 ? 1 : 0;
}

/* ---- Unix socket seed (POSIX socketpair; unsupported platforms return -1) ---- */
static int64_t mako_unix_socket_pair_last_peer = -1;

static inline int64_t mako_unix_socket_pair(void) {
#if !defined(_WIN32)
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return -1;
    mako_unix_socket_pair_last_peer = (int64_t)fds[1];
    return (int64_t)fds[0];
#else
    mako_unix_socket_pair_last_peer = -1;
    return -1;
#endif
}

static inline int64_t mako_unix_socket_pair_peer(void) {
    return mako_unix_socket_pair_last_peer;
}

static inline int64_t mako_unix_write(int64_t fd, MakoString data) {
    if (fd < 0) return -1;
    return (int64_t)send((mako_sock_t)fd, data.data, data.len, 0);
}

static inline MakoString mako_unix_read(int64_t fd, int64_t max_bytes) {
    if (fd < 0) return mako_str_from_cstr("");
    if (max_bytes <= 0 || max_bytes > 65535) max_bytes = 65535;
    char *buf = (char *)malloc((size_t)max_bytes + 1);
    if (!buf) mako_abort("unix_read: out of memory");
    ssize_t n = recv((mako_sock_t)fd, buf, (size_t)max_bytes, 0);
    if (n <= 0) {
        free(buf);
        return mako_str_from_cstr("");
    }
    buf[n] = 0;
    MakoString out = {buf, (size_t)n};
    return out;
}

static inline int64_t mako_unix_close(int64_t fd) {
    if (fd < 0) return 0;
    return mako_sock_close((mako_sock_t)fd) == 0 ? 1 : 0;
}

/* ---- Async I/O seed: normal sync fn, runtime poll/select underneath ---- */

/* Wait until fd is readable or timeout_ms elapses.
 * Returns 1 if readable, 0 on timeout, -1 on error.
 * Looks like ordinary sync code in Mako — no colored async. */
static inline int64_t mako_io_wait(int64_t fd, int64_t timeout_ms) {
    if (fd < 0) return -1;
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET((int)fd, &rfds);
    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }
    int r = select((int)fd + 1, &rfds, NULL, NULL, tvp);
    if (r < 0) return -1;
    if (r == 0) return 0;
    return 1;
}

/* Wait until one of two fds is readable, or timeout.
 * Returns 0 if fd_a ready, 1 if fd_b ready, -1 timeout/error.
 * Colorless — ordinary sync call; select() underneath. */
static inline int64_t mako_io_poll2(int64_t fd_a, int64_t fd_b, int64_t timeout_ms) {
    if (fd_a < 0 && fd_b < 0) return -1;
    fd_set rfds;
    FD_ZERO(&rfds);
    int maxfd = -1;
    if (fd_a >= 0) {
        FD_SET((int)fd_a, &rfds);
        maxfd = (int)fd_a;
    }
    if (fd_b >= 0) {
        FD_SET((int)fd_b, &rfds);
        if ((int)fd_b > maxfd) maxfd = (int)fd_b;
    }
    if (maxfd < 0) return -1;
    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }
    int r = select(maxfd + 1, &rfds, NULL, NULL, tvp);
    if (r < 0) return -1;
    if (r == 0) return -1;
    /* Prefer lower index if both ready */
    if (fd_a >= 0 && FD_ISSET((int)fd_a, &rfds)) return 0;
    if (fd_b >= 0 && FD_ISSET((int)fd_b, &rfds)) return 1;
    return -1;
}

static inline int64_t mako_io_poll3(
    int64_t fd_a,
    int64_t fd_b,
    int64_t fd_c,
    int64_t timeout_ms
) {
    fd_set rfds;
    FD_ZERO(&rfds);
    int maxfd = -1;
    int64_t fds[3] = {fd_a, fd_b, fd_c};
    for (int i = 0; i < 3; i++) {
        if (fds[i] >= 0) {
            FD_SET((int)fds[i], &rfds);
            if ((int)fds[i] > maxfd) maxfd = (int)fds[i];
        }
    }
    if (maxfd < 0) return -1;
    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }
    int r = select(maxfd + 1, &rfds, NULL, NULL, tvp);
    if (r <= 0) return -1;
    for (int i = 0; i < 3; i++) {
        if (fds[i] >= 0 && FD_ISSET((int)fds[i], &rfds)) return i;
    }
    return -1;
}

/* Wait among up to 4 fds. Returns index 0..3 or -1 timeout/error.
 * Colorless — select() underneath. */
static inline int64_t mako_io_poll4(
    int64_t fd_a,
    int64_t fd_b,
    int64_t fd_c,
    int64_t fd_d,
    int64_t timeout_ms
) {
    fd_set rfds;
    FD_ZERO(&rfds);
    int maxfd = -1;
    int64_t fds[4] = {fd_a, fd_b, fd_c, fd_d};
    for (int i = 0; i < 4; i++) {
        if (fds[i] >= 0) {
            FD_SET((int)fds[i], &rfds);
            if ((int)fds[i] > maxfd) maxfd = (int)fds[i];
        }
    }
    if (maxfd < 0) return -1;
    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }
    int r = select(maxfd + 1, &rfds, NULL, NULL, tvp);
    if (r <= 0) return -1;
    for (int i = 0; i < 4; i++) {
        if (fds[i] >= 0 && FD_ISSET((int)fds[i], &rfds)) return i;
    }
    return -1;
}

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#include <sys/time.h>

/* Darwin/BSD kqueue seed — colorless multi-fd wait (not select()).
 * Returns 0 if fd_a ready, 1 if fd_b ready, -1 timeout/error. */
static inline int64_t mako_io_kq_poll2(int64_t fd_a, int64_t fd_b, int64_t timeout_ms) {
    int kq = kqueue();
    if (kq < 0) return -1;
    struct kevent ch[2];
    int nch = 0;
    if (fd_a >= 0) {
        EV_SET(&ch[nch], (uintptr_t)fd_a, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, (void *)(intptr_t)0);
        nch++;
    }
    if (fd_b >= 0) {
        EV_SET(&ch[nch], (uintptr_t)fd_b, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, (void *)(intptr_t)1);
        nch++;
    }
    if (nch == 0) {
        mako_sock_close(kq);
        return -1;
    }
    struct timespec ts;
    struct timespec *tsp = NULL;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        tsp = &ts;
    }
    struct kevent ev;
    int n = kevent(kq, ch, nch, &ev, 1, tsp);
    mako_sock_close(kq);
    if (n <= 0) return -1;
    return (int64_t)(intptr_t)ev.udata;
}

/* Symbol always present; on Darwin this is kqueue (not Linux epoll). */
static inline int64_t mako_io_epoll_poll2(int64_t fd_a, int64_t fd_b, int64_t timeout_ms) {
    return mako_io_kq_poll2(fd_a, fd_b, timeout_ms);
}

#elif defined(__linux__)
#include <sys/epoll.h>

/* Linux epoll seed — same contract as io_kq_poll2. */
static inline int64_t mako_io_epoll_poll2(int64_t fd_a, int64_t fd_b, int64_t timeout_ms) {
    int ep = epoll_create1(0);
    if (ep < 0) return -1;
    struct epoll_event ev;
    ev.events = EPOLLIN;
    if (fd_a >= 0) {
        ev.data.u64 = 0;
        if (epoll_ctl(ep, EPOLL_CTL_ADD, (int)fd_a, &ev) < 0) {
            mako_sock_close(ep);
            return -1;
        }
    }
    if (fd_b >= 0) {
        ev.data.u64 = 1;
        if (epoll_ctl(ep, EPOLL_CTL_ADD, (int)fd_b, &ev) < 0) {
            mako_sock_close(ep);
            return -1;
        }
    }
    struct epoll_event out;
    int n = epoll_wait(ep, &out, 1, (int)timeout_ms);
    mako_sock_close(ep);
    if (n <= 0) return -1;
    return (int64_t)out.data.u64;
}

/* On Linux, io_kq_poll2 is an alias to epoll (portable name for native poll). */
static inline int64_t mako_io_kq_poll2(int64_t fd_a, int64_t fd_b, int64_t timeout_ms) {
    return mako_io_epoll_poll2(fd_a, fd_b, timeout_ms);
}

#else
/* Other platforms: select()-based poll2 (honest Partial). */
static inline int64_t mako_io_epoll_poll2(int64_t fd_a, int64_t fd_b, int64_t timeout_ms) {
    return mako_io_poll2(fd_a, fd_b, timeout_ms);
}
static inline int64_t mako_io_kq_poll2(int64_t fd_a, int64_t fd_b, int64_t timeout_ms) {
    return mako_io_poll2(fd_a, fd_b, timeout_ms);
}
#endif

/* Portable native poll: kqueue (BSD), epoll (Linux), else select. */
static inline int64_t mako_io_native_poll2(int64_t fd_a, int64_t fd_b, int64_t timeout_ms) {
    return mako_io_kq_poll2(fd_a, fd_b, timeout_ms);
}

static inline int64_t mako_io_read_ready(int64_t fd, int64_t timeout_ms) {
    return mako_io_wait(fd, timeout_ms);
}

static inline int64_t mako_io_write_ready(int64_t fd, int64_t timeout_ms) {
    if (fd < 0) return -1;
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET((int)fd, &wfds);
    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }
    int r = select((int)fd + 1, NULL, &wfds, NULL, tvp);
    if (r < 0) return -1;
    if (r == 0) return 0;
    return FD_ISSET((int)fd, &wfds) ? 1 : 0;
}

static inline int64_t mako_io_set_nonblocking(int64_t fd, int64_t enabled) {
    if (fd < 0) return -1;
#if defined(_WIN32)
    u_long mode = enabled ? 1UL : 0UL;
    return ioctlsocket((mako_sock_t)fd, FIONBIO, &mode) == 0 ? 1 : -1;
#else
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (enabled) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl((int)fd, F_SETFL, flags) == 0 ? 1 : -1;
#endif
}

static inline int64_t mako_io_try_write(int64_t fd, MakoString data) {
    if (fd < 0) return -1;
    ssize_t n = send((mako_sock_t)fd, data.data, data.len, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    return (int64_t)n;
}

static inline int64_t mako_io_backoff_ms(int64_t attempt, int64_t min_ms, int64_t max_ms) {
    if (attempt < 0) attempt = 0;
    if (min_ms < 0) min_ms = 0;
    if (max_ms < min_ms) max_ms = min_ms;
    int64_t delay = min_ms;
    while (attempt > 0 && delay < max_ms) {
        if (delay > max_ms / 2) {
            delay = max_ms;
            break;
        }
        delay *= 2;
        attempt--;
    }
    return delay > max_ms ? max_ms : delay;
}

static inline int64_t mako_io_should_pause(int64_t pending_bytes, int64_t high_watermark, int64_t writable) {
    if (high_watermark <= 0) return 0;
    if (pending_bytes >= high_watermark) return 1;
    if (writable <= 0 && pending_bytes > 0) return 1;
    return 0;
}

/* Nonblocking accept: returns client fd, or -1 if none ready / error.
 * Pair with io_wait(listen_fd, timeout). */
static inline int64_t mako_tcp_accept_nb(int64_t listen_fd) {
    if (listen_fd < 0) return -1;
#if defined(_WIN32)
    u_long mode = 1UL;
    (void)ioctlsocket((mako_sock_t)listen_fd, FIONBIO, &mode);
    mako_sock_t cfd = accept((mako_sock_t)listen_fd, NULL, NULL);
    if (cfd == MAKO_INVALID_SOCK) return -1;
    return (int64_t)cfd;
#else
    int flags = fcntl((int)listen_fd, F_GETFL, 0);
    if (flags >= 0) fcntl((int)listen_fd, F_SETFL, flags | O_NONBLOCK);
    int cfd = accept((int)listen_fd, NULL, NULL);
    if (cfd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
        return -1;
    }
    return (int64_t)cfd;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_NET_H */
