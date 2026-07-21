/* Mako net — TCP/UDP dual-stack (IPv4+IPv6) + Happy Eyeballs connect. */
#ifndef MAKO_NET_H
#define MAKO_NET_H

#include "mako_rt.h"
#include <errno.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Happy Eyeballs connection attempt delay (RFC 8305 lite). Default 250ms. */
static int mako_tcp_he_delay_ms = 250;

static inline void mako_tcp_set_he_delay_ms(int64_t ms) {
    if (ms < 0) ms = 0;
    if (ms > 5000) ms = 5000;
    mako_tcp_he_delay_ms = (int)ms;
}

static inline int64_t mako_tcp_get_he_delay_ms(void) {
    return (int64_t)mako_tcp_he_delay_ms;
}

/* Best-effort CLOEXEC on a socket fd (production default). */
static inline void mako_sock_set_cloexec(mako_sock_t fd) {
#if !defined(_WIN32)
    int fl = fcntl((int)fd, F_GETFD);
    if (fl >= 0) (void)fcntl((int)fd, F_SETFD, fl | FD_CLOEXEC);
#else
    (void)fd;
#endif
}

static inline void mako_sock_set_nonblock(mako_sock_t fd, int on) {
#if !defined(_WIN32)
    int fl = fcntl((int)fd, F_GETFL, 0);
    if (fl < 0) return;
    if (on) fcntl((int)fd, F_SETFL, fl | O_NONBLOCK);
    else fcntl((int)fd, F_SETFL, fl & ~O_NONBLOCK);
#else
    u_long mode = on ? 1UL : 0UL;
    ioctlsocket(fd, FIONBIO, &mode);
#endif
}

/* Create socket for family (AF_INET or AF_INET6). */
static inline mako_sock_t mako_sock_create_af(int family, int type) {
#if defined(SOCK_CLOEXEC) && !defined(_WIN32)
    mako_sock_t fd = socket(family, type | SOCK_CLOEXEC, 0);
#else
    mako_sock_t fd = socket(family, type, 0);
#endif
    if (fd != MAKO_INVALID_SOCK) mako_sock_set_cloexec(fd);
    return fd;
}

/* Create IPv4 TCP/UDP socket (compat). */
static inline mako_sock_t mako_sock_create(int type) {
    return mako_sock_create_af(AF_INET, type);
}

/* Format sockaddr as "a.b.c.d:port" or "[v6]:port". */
static inline MakoString mako_sockaddr_str(const struct sockaddr *sa, socklen_t slen) {
    char ip[INET6_ADDRSTRLEN];
    char out[INET6_ADDRSTRLEN + 16];
    if (!sa) return mako_str_from_cstr("");
    if (sa->sa_family == AF_INET && slen >= (socklen_t)sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)sa;
        if (!inet_ntop(AF_INET, &a->sin_addr, ip, sizeof(ip))) return mako_str_from_cstr("");
        snprintf(out, sizeof(out), "%s:%u", ip, (unsigned)ntohs(a->sin_port));
        return mako_str_from_cstr(out);
    }
    if (sa->sa_family == AF_INET6 && slen >= (socklen_t)sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)sa;
        if (!inet_ntop(AF_INET6, &a->sin6_addr, ip, sizeof(ip))) return mako_str_from_cstr("");
        snprintf(out, sizeof(out), "[%s]:%u", ip, (unsigned)ntohs(a->sin6_port));
        return mako_str_from_cstr(out);
    }
    return mako_str_from_cstr("");
}

static inline MakoString mako_sockaddr_in_str(const struct sockaddr_in *addr) {
    return mako_sockaddr_str((const struct sockaddr *)addr, sizeof(*addr));
}

/* Fill sockaddr for bind. Supports *, 0.0.0.0, ::, IPv4/IPv6 literals.
 * dual=1 for * / empty: prefer IPv6 :: with V6ONLY=0 (dual-stack) when possible. */
static inline int mako_bind_addr_any(
    MakoString host, int64_t port,
    struct sockaddr_storage *ss, socklen_t *slen, int *family_out, int *dual_out
) {
    memset(ss, 0, sizeof(*ss));
    *dual_out = 0;
    char hbuf[256];
    size_t hl = host.data ? host.len : 0;
    if (hl >= sizeof(hbuf)) return 0;
    if (hl) memcpy(hbuf, host.data, hl);
    hbuf[hl] = 0;
    int any4 = (!hl || (hl == 1 && hbuf[0] == '*') || strcmp(hbuf, "0.0.0.0") == 0);
    int any6 = (strcmp(hbuf, "::") == 0 || strcmp(hbuf, "[::]") == 0);
    if (any4 || any6 || !hl) {
        /* Prefer dual-stack IPv6 wildcard when host is * or empty */
        if (any6 || any4 || !hl) {
            struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)ss;
            a6->sin6_family = AF_INET6;
            a6->sin6_port = htons((uint16_t)port);
            a6->sin6_addr = in6addr_any;
            *slen = sizeof(struct sockaddr_in6);
            *family_out = AF_INET6;
            *dual_out = (any4 || !hl) ? 1 : 0; /* V6ONLY=0 for dual */
            if (any6 && !any4 && hl) *dual_out = 0; /* explicit :: may stay v6-only */
            if (any4 && hl && strcmp(hbuf, "0.0.0.0") == 0) {
                /* explicit IPv4 any */
                struct sockaddr_in *a4 = (struct sockaddr_in *)ss;
                memset(ss, 0, sizeof(*ss));
                a4->sin_family = AF_INET;
                a4->sin_port = htons((uint16_t)port);
                a4->sin_addr.s_addr = htonl(INADDR_ANY);
                *slen = sizeof(struct sockaddr_in);
                *family_out = AF_INET;
                *dual_out = 0;
            }
            return 1;
        }
    }
    /* Strip [brackets] from IPv6 literals */
    char *hp = hbuf;
    if (hp[0] == '[') {
        char *rb = strchr(hp, ']');
        if (rb) {
            *rb = 0;
            hp++;
        }
    }
    struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)ss;
    if (inet_pton(AF_INET6, hp, &a6->sin6_addr) == 1) {
        a6->sin6_family = AF_INET6;
        a6->sin6_port = htons((uint16_t)port);
        *slen = sizeof(struct sockaddr_in6);
        *family_out = AF_INET6;
        *dual_out = 0;
        return 1;
    }
    struct sockaddr_in *a4 = (struct sockaddr_in *)ss;
    memset(ss, 0, sizeof(*ss));
    if (inet_pton(AF_INET, hp, &a4->sin_addr) == 1) {
        a4->sin_family = AF_INET;
        a4->sin_port = htons((uint16_t)port);
        *slen = sizeof(struct sockaddr_in);
        *family_out = AF_INET;
        *dual_out = 0;
        return 1;
    }
    return 0;
}

/* Compat IPv4-only bind helper. */
static inline int mako_bind_ipv4_addr(struct sockaddr_in *addr, MakoString host, int64_t port) {
    struct sockaddr_storage ss;
    socklen_t slen = 0;
    int fam = 0, dual = 0;
    if (!mako_bind_addr_any(host, port, &ss, &slen, &fam, &dual)) return 0;
    if (fam != AF_INET) return 0;
    memcpy(addr, &ss, sizeof(*addr));
    return 1;
}

/* Bind host:port and listen. IPv4, IPv6, or dual-stack (* / empty → :: + V6ONLY=0). */
static inline int64_t mako_tcp_listen_backlog(MakoString host, int64_t port, int64_t backlog) {
    if (!mako_net_init()) return -1;
    struct sockaddr_storage ss;
    socklen_t slen = 0;
    int fam = AF_INET, dual = 0;
    if (!mako_bind_addr_any(host, port, &ss, &slen, &fam, &dual)) {
        /* Hostname: resolve with AI_PASSIVE */
        char hbuf[256], pbuf[16];
        size_t hl = host.data ? host.len : 0;
        if (hl >= sizeof(hbuf)) return -1;
        if (hl) memcpy(hbuf, host.data, hl);
        hbuf[hl] = 0;
        snprintf(pbuf, sizeof(pbuf), "%d", (int)port);
        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        if (getaddrinfo(hl ? hbuf : NULL, pbuf, &hints, &res) != 0 || !res) {
            fprintf(stderr, "error: tcp: invalid bind host\n");
            return -1;
        }
        fam = res->ai_family;
        slen = (socklen_t)res->ai_addrlen;
        memcpy(&ss, res->ai_addr, slen);
        freeaddrinfo(res);
        dual = 0;
    }
    mako_sock_t fd = mako_sock_create_af(fam, SOCK_STREAM);
    if (fd == MAKO_INVALID_SOCK) {
        /* Fallback: IPv4 any if dual IPv6 socket unavailable */
        if (fam == AF_INET6) {
            fam = AF_INET;
            struct sockaddr_in *a4 = (struct sockaddr_in *)&ss;
            memset(&ss, 0, sizeof(ss));
            a4->sin_family = AF_INET;
            a4->sin_port = htons((uint16_t)port);
            a4->sin_addr.s_addr = htonl(INADDR_ANY);
            slen = sizeof(struct sockaddr_in);
            dual = 0;
            fd = mako_sock_create_af(AF_INET, SOCK_STREAM);
        }
        if (fd == MAKO_INVALID_SOCK) {
            fprintf(stderr, "error: tcp: socket() failed\n");
            return -1;
        }
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
#if defined(SO_REUSEPORT)
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const char *)&yes, sizeof(yes));
#endif
#if defined(IPV6_V6ONLY)
    if (fam == AF_INET6) {
        int v6only = dual ? 0 : 1;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&v6only, sizeof(v6only));
    }
#endif
    if (bind(fd, (struct sockaddr *)&ss, slen) < 0) {
        /* Dual-stack bind failed → try IPv4-only */
        if (fam == AF_INET6 && dual) {
            mako_sock_close(fd);
            struct sockaddr_in a4;
            memset(&a4, 0, sizeof(a4));
            a4.sin_family = AF_INET;
            a4.sin_port = htons((uint16_t)port);
            a4.sin_addr.s_addr = htonl(INADDR_ANY);
            fd = mako_sock_create_af(AF_INET, SOCK_STREAM);
            if (fd == MAKO_INVALID_SOCK) return -1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
#if defined(SO_REUSEPORT)
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const char *)&yes, sizeof(yes));
#endif
            if (bind(fd, (struct sockaddr *)&a4, sizeof(a4)) < 0) {
                fprintf(stderr, "error: tcp: bind failed\n");
                mako_sock_close(fd);
                return -1;
            }
        } else {
            fprintf(stderr, "error: tcp: bind failed\n");
            mako_sock_close(fd);
            return -1;
        }
    }
    int bl = (backlog > 0 && backlog < 65536) ? (int)backlog : 4096;
    if (listen(fd, bl) < 0) {
        fprintf(stderr, "error: tcp: listen failed\n");
        mako_sock_close(fd);
        return -1;
    }
    return (int64_t)fd;
}

static inline int64_t mako_tcp_listen_addr(MakoString host, int64_t port) {
    return mako_tcp_listen_backlog(host, port, 4096);
}

static inline int64_t mako_tcp_listen(int64_t port) {
    return mako_tcp_listen_addr(mako_str_from_cstr(""), port);
}

/* Last accepted peer address (process-global; copy after accept if multi-thread). */
static char mako_tcp_last_peer[128];
static char mako_tcp_last_local[128];

static inline int64_t mako_tcp_accept(int64_t listen_fd) {
    struct sockaddr_storage peer;
    socklen_t plen = sizeof(peer);
    memset(&peer, 0, sizeof(peer));
    int cfd = accept((int)listen_fd, (struct sockaddr *)&peer, &plen);
    if (cfd < 0) {
        fprintf(stderr, "error: tcp: accept failed\n");
        return -1;
    }
    mako_sock_set_cloexec((mako_sock_t)cfd);
    MakoString ps = mako_sockaddr_str((struct sockaddr *)&peer, plen);
    if (ps.data) {
        size_t n = ps.len < sizeof(mako_tcp_last_peer) - 1 ? ps.len : sizeof(mako_tcp_last_peer) - 1;
        memcpy(mako_tcp_last_peer, ps.data, n);
        mako_tcp_last_peer[n] = 0;
        mako_str_free(ps);
    } else {
        mako_tcp_last_peer[0] = 0;
    }
    return (int64_t)cfd;
}

/* Peer address of last accept, or of `fd` if valid (getpeername). "ip:port" / "[v6]:port". */
static inline MakoString mako_tcp_peer_addr(int64_t fd) {
    if (fd < 0) {
        return mako_str_from_cstr(mako_tcp_last_peer);
    }
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    if (getpeername((int)fd, (struct sockaddr *)&addr, &len) != 0) {
        return mako_str_from_cstr(mako_tcp_last_peer);
    }
    return mako_sockaddr_str((struct sockaddr *)&addr, len);
}

/* Local address bound to fd (getsockname). */
static inline MakoString mako_tcp_local_addr(int64_t fd) {
    if (fd < 0) return mako_str_from_cstr("");
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    if (getsockname((int)fd, (struct sockaddr *)&addr, &len) != 0) {
        return mako_str_from_cstr("");
    }
    MakoString s = mako_sockaddr_str((struct sockaddr *)&addr, len);
    if (s.data) {
        size_t n = s.len < sizeof(mako_tcp_last_local) - 1 ? s.len : sizeof(mako_tcp_last_local) - 1;
        memcpy(mako_tcp_last_local, s.data, n);
        mako_tcp_last_local[n] = 0;
    }
    return s;
}

/* Half-close. how: 0=read, 1=write, 2=both. Returns 0 ok.
 * Unix: SHUT_RD/WR/RDWR · Windows Winsock: SD_RECEIVE/SEND/BOTH. */
static inline int64_t mako_tcp_shutdown(int64_t fd, int64_t how) {
    if (fd < 0) return -1;
#if defined(_WIN32) || defined(_WIN64)
    int h = SD_BOTH;
    if (how == 0) h = SD_RECEIVE;
    else if (how == 1) h = SD_SEND;
    return shutdown((SOCKET)fd, h) == 0 ? 0 : -1;
#else
    int h = SHUT_RDWR;
    if (how == 0) h = SHUT_RD;
    else if (how == 1) h = SHUT_WR;
    return shutdown((int)fd, h) == 0 ? 0 : -1;
#endif
}

/* SO_ERROR after nonblocking connect / async error. 0 = ok, else errno. */
static inline int64_t mako_sock_error(int64_t fd) {
    if (fd < 0) return -1;
    int err = 0;
    socklen_t elen = sizeof(err);
    if (getsockopt((int)fd, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) != 0) return -1;
    return (int64_t)err;
}

/* SO_LINGER: onoff 0/1, linger_sec. Returns 0 ok. */
static inline int64_t mako_tcp_linger(int64_t fd, int64_t onoff, int64_t linger_sec) {
    if (fd < 0) return -1;
    struct linger lg;
    lg.l_onoff = onoff ? 1 : 0;
    lg.l_linger = linger_sec > 0 ? (int)linger_sec : 0;
    return setsockopt((int)fd, SOL_SOCKET, SO_LINGER, (const char *)&lg, sizeof(lg)) == 0
        ? 0 : -1;
}

static inline int64_t mako_tcp_close(int64_t fd) {
    if (fd < 0) return 0;
    return mako_sock_close((int)fd) == 0 ? 1 : 0;
}

/* Wait until fd is writable (connect complete) or timeout. 1=ready, 0=timeout, -1=err. */
static inline int mako_tcp_wait_writable(int fd, int timeout_ms) {
    fd_set wfds, efds;
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    FD_SET(fd, &wfds);
    FD_SET(fd, &efds);
    struct timeval tv, *tvp = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }
    int r = select(fd + 1, NULL, &wfds, &efds, tvp);
    if (r < 0) return -1;
    if (r == 0) return 0;
    return 1;
}

static inline int mako_tcp_connect_finished_ok(int fd) {
    int err = 0;
    socklen_t elen = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) != 0) return 0;
    return err == 0;
}

/* Start nonblocking connect to one addrinfo. Returns fd (>=0) or -1. */
static inline int mako_tcp_start_one(struct addrinfo *ai) {
    if (!ai) return -1;
    int fd = (int)mako_sock_create_af(ai->ai_family, SOCK_STREAM);
    if (fd < 0) return -1;
    mako_sock_set_nonblock((mako_sock_t)fd, 1);
    int rc = connect(fd, ai->ai_addr, (socklen_t)ai->ai_addrlen);
    if (rc == 0) return fd; /* immediate success (e.g. local) */
    if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EAGAIN) return fd;
    mako_sock_close(fd);
    return -1;
}

/* Happy Eyeballs (RFC 8305 lite): resolve AF_UNSPEC, interleave AAAA/A,
 * race up to a few connects with he_delay_ms between starts. */
static inline int64_t mako_tcp_connect_timeout(
    MakoString host, int64_t port, int64_t timeout_ms
) {
    if (!mako_net_init()) return -1;
    char hbuf[256], pbuf[16];
    if (!host.data || host.len == 0 || host.len >= sizeof(hbuf)) return -1;
    for (size_t i = 0; i < host.len; i++) if (host.data[i] == 0) return -1;
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    char *hp = hbuf;
    if (hp[0] == '[') {
        char *rb = strchr(hp, ']');
        if (rb) {
            *rb = 0;
            hp++;
        }
    }
    snprintf(pbuf, sizeof(pbuf), "%d", (int)port);
    if (timeout_ms <= 0) timeout_ms = 30000;

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (getaddrinfo(hp, pbuf, &hints, &res) != 0 || !res) return -1;

#define MAKO_HE_MAX 8
    struct addrinfo *ordered[MAKO_HE_MAX];
    int naddr = 0;
    struct addrinfo *v6[MAKO_HE_MAX], *v4[MAKO_HE_MAX];
    int n6 = 0, n4 = 0;
    for (struct addrinfo *ai = res; ai && (n6 + n4) < MAKO_HE_MAX; ai = ai->ai_next) {
        if (ai->ai_family == AF_INET6 && n6 < MAKO_HE_MAX) v6[n6++] = ai;
        else if (ai->ai_family == AF_INET && n4 < MAKO_HE_MAX) v4[n4++] = ai;
    }
    {
        int i6 = 0, i4 = 0;
        while (naddr < MAKO_HE_MAX && (i6 < n6 || i4 < n4)) {
            if (i6 < n6) ordered[naddr++] = v6[i6++];
            if (naddr < MAKO_HE_MAX && i4 < n4) ordered[naddr++] = v4[i4++];
        }
    }
    if (naddr == 0) {
        freeaddrinfo(res);
        return -1;
    }

    int fds[MAKO_HE_MAX];
    int live[MAKO_HE_MAX];
    for (int i = 0; i < MAKO_HE_MAX; i++) {
        fds[i] = -1;
        live[i] = 0;
    }

    int64_t deadline = mako_mono_ms() + timeout_ms;
    int next = 0;
    int win = -1;

    /* Kick first attempt immediately */
    fds[0] = mako_tcp_start_one(ordered[0]);
    if (fds[0] >= 0) {
        live[0] = 1;
        if (mako_tcp_connect_finished_ok(fds[0])) {
            /* may still be in progress; check via select below */
        }
    }
    next = 1;

    while (win < 0 && mako_mono_ms() < deadline) {
        int64_t left = deadline - mako_mono_ms();
        if (left <= 0) break;

        fd_set wfds;
        FD_ZERO(&wfds);
        int maxfd = -1, nlive = 0;
        for (int i = 0; i < next; i++) {
            if (live[i] && fds[i] >= 0) {
                FD_SET(fds[i], &wfds);
                if (fds[i] > maxfd) maxfd = fds[i];
                nlive++;
            }
        }

        if (nlive == 0) {
            if (next >= naddr) break;
            fds[next] = mako_tcp_start_one(ordered[next]);
            if (fds[next] >= 0) live[next] = 1;
            next++;
            continue;
        }

        int slice = (next < naddr) ? mako_tcp_he_delay_ms : (int)left;
        if (slice > (int)left) slice = (int)left;
        if (slice < 1) slice = 1;

        struct timeval tv;
        tv.tv_sec = slice / 1000;
        tv.tv_usec = (slice % 1000) * 1000;
        int r = select(maxfd + 1, NULL, &wfds, NULL, &tv);

        if (r > 0) {
            for (int i = 0; i < next; i++) {
                if (!live[i] || fds[i] < 0) continue;
                if (!FD_ISSET(fds[i], &wfds)) continue;
                if (mako_tcp_connect_finished_ok(fds[i])) {
                    win = i;
                    mako_sock_set_nonblock((mako_sock_t)fds[i], 0);
                    break;
                }
                mako_sock_close(fds[i]);
                fds[i] = -1;
                live[i] = 0;
            }
        }
        if (win >= 0) break;

        /* HE: after delay (or poll), start next address while others race */
        if (next < naddr) {
            fds[next] = mako_tcp_start_one(ordered[next]);
            if (fds[next] >= 0) live[next] = 1;
            next++;
        } else {
            int any = 0;
            for (int i = 0; i < next; i++) if (live[i]) any = 1;
            if (!any) break;
        }
    }

    int64_t out = -1;
    if (win >= 0 && fds[win] >= 0) {
        out = (int64_t)fds[win];
        fds[win] = -1;
    }
    for (int i = 0; i < MAKO_HE_MAX; i++) {
        if (fds[i] >= 0) mako_sock_close(fds[i]);
    }
    freeaddrinfo(res);
#undef MAKO_HE_MAX
    return out;
}

/* Connect to host:port (IPv4/IPv6/hostname) with Happy Eyeballs. Returns fd or -1. */
static inline int64_t mako_tcp_connect(MakoString host, int64_t port) {
    return mako_tcp_connect_timeout(host, port, 30000);
}

static inline int64_t mako_tcp_write(int64_t fd, MakoString s) {
    if (fd < 0) return -1;
    ssize_t n = send((int)fd, s.data, s.len, 0);
    return (int64_t)n;
}

/* Write all bytes, retrying short sends. Returns total written or -1. */
static inline int64_t mako_tcp_write_all(int64_t fd, MakoString s) {
    if (fd < 0) return -1;
    if (!s.data || s.len == 0) return 0;
    size_t off = 0;
    while (off < s.len) {
        ssize_t n = send((int)fd, s.data + off, s.len - off, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return (int64_t)off;
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

/* Read exactly n bytes (or until EOF). Empty if nothing read. */
static inline MakoString mako_tcp_read_n(int64_t fd, int64_t n) {
    if (fd < 0 || n <= 0) return mako_str_from_cstr("");
    if (n > 16 * 1024 * 1024) n = 16 * 1024 * 1024;
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) return mako_str_from_cstr("");
    size_t total = 0;
    while (total < (size_t)n) {
        ssize_t r = recv((int)fd, buf + total, (size_t)n - total, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) break;
        total += (size_t)r;
    }
    if (total == 0) { free(buf); return mako_str_from_cstr(""); }
    buf[total] = 0;
    return (MakoString){buf, total};
}

/* Disable Nagle's algorithm for low-latency writes. */
static inline int64_t mako_tcp_nodelay(int64_t fd) {
    int flag = 1;
    return setsockopt((int)fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == 0 ? 1 : 0;
}

/* Set recv+send timeouts on a connection so a stalled peer cannot hold a
 * session open indefinitely. ms<=0 clears the timeout (blocks forever).
 * Returns 1 on success, 0 on error. */
static inline int64_t mako_tcp_set_timeout(int64_t fd, int64_t ms) {
#if defined(_WIN32)
    DWORD tv = (ms > 0) ? (DWORD)ms : 0;
    int a = setsockopt((mako_sock_t)fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    int b = setsockopt((mako_sock_t)fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = (ms > 0) ? (time_t)(ms / 1000) : 0;
    tv.tv_usec = (ms > 0) ? (suseconds_t)((ms % 1000) * 1000) : 0;
    int a = setsockopt((int)fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int b = setsockopt((int)fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    return (a == 0 && b == 0) ? 1 : 0;
}

/* Enable TCP keepalive and tune idle/interval/count so dead peers are detected
 * and half-open connections reaped. idle/interval in seconds; any arg <=0 keeps
 * the OS default for that knob. Returns 1 on success, 0 on error. */
static inline int64_t mako_tcp_keepalive(int64_t fd, int64_t idle, int64_t interval, int64_t count) {
    int on = 1;
    if (setsockopt((mako_sock_t)fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&on, sizeof(on)) != 0) {
        return 0;
    }
#if defined(__linux__)
    if (idle > 0)     { int v = (int)idle;     setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPIDLE, &v, sizeof(v)); }
    if (interval > 0) { int v = (int)interval; setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPINTVL, &v, sizeof(v)); }
    if (count > 0)    { int v = (int)count;    setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPCNT, &v, sizeof(v)); }
#elif defined(__APPLE__)
#if defined(TCP_KEEPALIVE)
    if (idle > 0)     { int v = (int)idle;     setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPALIVE, &v, sizeof(v)); }
#endif
#if defined(TCP_KEEPINTVL)
    if (interval > 0) { int v = (int)interval; setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPINTVL, &v, sizeof(v)); }
#endif
#if defined(TCP_KEEPCNT)
    if (count > 0)    { int v = (int)count;    setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPCNT, &v, sizeof(v)); }
#endif
#else
    (void)idle; (void)interval; (void)count;
#endif
    return 1;
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
        if (chunk.len == 0) { mako_str_free(chunk); break; }
        char *nb = (char *)realloc(buf, total + chunk.len);
        if (!nb) { mako_str_free(chunk); break; }
        buf = nb;
        memcpy(buf + total, chunk.data, chunk.len);
        total += chunk.len;
        mako_str_free(chunk);
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

/* ---- UDP datagram helpers (IPv4 + IPv6) ---- */

static char mako_udp_last_from_host[INET6_ADDRSTRLEN];
static int mako_udp_last_from_port = 0;

static inline int64_t mako_udp_bind_addr(MakoString host, int64_t port) {
    if (!mako_net_init()) return -1;
    struct sockaddr_storage ss;
    socklen_t slen = 0;
    int fam = AF_INET, dual = 0;
    if (!mako_bind_addr_any(host, port, &ss, &slen, &fam, &dual)) {
        fprintf(stderr, "error: udp: invalid bind host\n");
        return -1;
    }
    /* Dual-stack UDP is awkward for mixed sendto families on some OSes;
     * for * / empty prefer IPv4 ANY (legacy-compatible). Explicit :: stays v6. */
    if (dual && fam == AF_INET6) {
        struct sockaddr_in *a4 = (struct sockaddr_in *)&ss;
        memset(&ss, 0, sizeof(ss));
        a4->sin_family = AF_INET;
        a4->sin_port = htons((uint16_t)port);
        a4->sin_addr.s_addr = htonl(INADDR_ANY);
        slen = sizeof(struct sockaddr_in);
        fam = AF_INET;
        dual = 0;
    }
    mako_sock_t fd = mako_sock_create_af(fam, SOCK_DGRAM);
    if (fd == MAKO_INVALID_SOCK && fam == AF_INET6) {
        struct sockaddr_in *a4 = (struct sockaddr_in *)&ss;
        memset(&ss, 0, sizeof(ss));
        a4->sin_family = AF_INET;
        a4->sin_port = htons((uint16_t)port);
        a4->sin_addr.s_addr = htonl(INADDR_ANY);
        slen = sizeof(struct sockaddr_in);
        fam = AF_INET;
        dual = 0;
        fd = mako_sock_create_af(AF_INET, SOCK_DGRAM);
    }
    if (fd == MAKO_INVALID_SOCK) {
        fprintf(stderr, "error: udp: socket() failed: %s\n", strerror(errno));
        return -1;
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
#if defined(SO_REUSEPORT)
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const char *)&yes, sizeof(yes));
#endif
#if defined(IPV6_V6ONLY)
    if (fam == AF_INET6) {
        int v6only = dual ? 0 : 1;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&v6only, sizeof(v6only));
    }
#endif
    if (bind(fd, (struct sockaddr *)&ss, slen) < 0) {
        if (fam == AF_INET6 && dual) {
            mako_sock_close(fd);
            struct sockaddr_in a4;
            memset(&a4, 0, sizeof(a4));
            a4.sin_family = AF_INET;
            a4.sin_port = htons((uint16_t)port);
            a4.sin_addr.s_addr = htonl(INADDR_ANY);
            fd = mako_sock_create_af(AF_INET, SOCK_DGRAM);
            if (fd == MAKO_INVALID_SOCK) return -1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
#if defined(SO_REUSEPORT)
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const char *)&yes, sizeof(yes));
#endif
            if (bind(fd, (struct sockaddr *)&a4, sizeof(a4)) < 0) {
                mako_sock_close(fd);
                return -1;
            }
        } else {
            fprintf(stderr, "error: udp: bind failed: %s\n", strerror(errno));
            mako_sock_close(fd);
            return -1;
        }
    }
    return (int64_t)fd;
}

static inline int64_t mako_udp_bind(int64_t port) {
    return mako_udp_bind_addr(mako_str_from_cstr("*"), port);
}

static inline int64_t mako_udp_send_to(int64_t fd, MakoString host, int64_t port, MakoString data) {
    if (fd < 0) return -1;
    char hbuf[256], pbuf[16];
    if (!host.data || host.len == 0 || host.len >= sizeof(hbuf)) return -1;
    for (size_t i = 0; i < host.len; i++) if (host.data[i] == 0) return -1;
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    char *hp = hbuf;
    if (hp[0] == '[') {
        char *rb = strchr(hp, ']');
        if (rb) {
            *rb = 0;
            hp++;
        }
    }
    snprintf(pbuf, sizeof(pbuf), "%d", (int)port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(hp, pbuf, &hints, &res) != 0 || !res) return -1;
    ssize_t n = sendto(
        (mako_sock_t)fd, data.data, data.len, 0, res->ai_addr, (socklen_t)res->ai_addrlen
    );
    freeaddrinfo(res);
    return (int64_t)n;
}

static inline MakoString mako_udp_recv(int64_t fd, int64_t max_bytes) {
    if (fd < 0) return mako_str_from_cstr("");
    if (max_bytes <= 0 || max_bytes > 65535) max_bytes = 65535;
    char *buf = (char *)malloc((size_t)max_bytes + 1);
    if (!buf) mako_abort("udp_recv: out of memory");
    struct sockaddr_storage peer;
    socklen_t plen = sizeof(peer);
    memset(&peer, 0, sizeof(peer));
    ssize_t n = recvfrom(
        (mako_sock_t)fd, buf, (size_t)max_bytes, 0, (struct sockaddr *)&peer, &plen
    );
    if (n <= 0) {
        free(buf);
        return mako_str_from_cstr("");
    }
    buf[n] = 0;
    mako_udp_last_from_host[0] = 0;
    mako_udp_last_from_port = 0;
    if (peer.ss_family == AF_INET) {
        struct sockaddr_in *a = (struct sockaddr_in *)&peer;
        inet_ntop(AF_INET, &a->sin_addr, mako_udp_last_from_host, sizeof(mako_udp_last_from_host));
        mako_udp_last_from_port = (int)ntohs(a->sin_port);
    } else if (peer.ss_family == AF_INET6) {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)&peer;
        inet_ntop(AF_INET6, &a->sin6_addr, mako_udp_last_from_host, sizeof(mako_udp_last_from_host));
        mako_udp_last_from_port = (int)ntohs(a->sin6_port);
    }
    return (MakoString){buf, (size_t)n};
}

static inline MakoString mako_udp_recv_from(int64_t fd, int64_t max_bytes) {
    return mako_udp_recv(fd, max_bytes);
}

static inline MakoString mako_udp_last_sender_host(void) {
    return mako_str_from_cstr(mako_udp_last_from_host);
}

static inline int64_t mako_udp_last_sender_port(void) {
    return (int64_t)mako_udp_last_from_port;
}

static inline MakoString mako_udp_last_sender(void) {
    if (!mako_udp_last_from_host[0]) return mako_str_from_cstr("");
    char out[INET6_ADDRSTRLEN + 16];
    if (strchr(mako_udp_last_from_host, ':'))
        snprintf(out, sizeof(out), "[%s]:%d", mako_udp_last_from_host, mako_udp_last_from_port);
    else
        snprintf(out, sizeof(out), "%s:%d", mako_udp_last_from_host, mako_udp_last_from_port);
    return mako_str_from_cstr(out);
}

static inline int64_t mako_udp_local_port(int64_t fd) {
    if (fd < 0) return -1;
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    if (getsockname((mako_sock_t)fd, (struct sockaddr *)&addr, &len) != 0) return -1;
    if (addr.ss_family == AF_INET)
        return (int64_t)ntohs(((struct sockaddr_in *)&addr)->sin_port);
    if (addr.ss_family == AF_INET6)
        return (int64_t)ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
    return -1;
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


/* Fast TCP read for HTTP servers: thread-local buffer, no malloc per read. */
static __thread char mako_tcp_read_fast_buf[8192];
static inline MakoString mako_tcp_read_fast(int64_t fd) {
    ssize_t n = recv((int)fd, mako_tcp_read_fast_buf, sizeof(mako_tcp_read_fast_buf) - 1, 0);
    if (n <= 0) return mako_str_empty;
    mako_tcp_read_fast_buf[n] = 0;
    return (MakoString){mako_tcp_read_fast_buf, (size_t)n};
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_NET_H */
