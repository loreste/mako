/* Native SCTP transport (one-to-one associations + advanced options).
 *
 * Real path: Linux with kernel SCTP (IPPROTO_SCTP). libsctp enables:
 *   multistreaming (INITMSG), PPID send/recv, heartbeats, multihoming
 *   (bindx/connectx/getpaddrs/set_primary).
 * macOS / Windows / WASI: sctp_available()==0 stubs.
 *
 * Out of scope: one-to-many sockets, PR-SCTP policies, SCTP-AUTH, DTLS/SCTP.
 */
#ifndef MAKO_SCTP_H
#define MAKO_SCTP_H

#include "mako_rt.h"
#include "mako_net.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__linux__) && !defined(MAKO_WASI) && !defined(_WIN32)
#if defined(MAKO_HAS_SCTP) || defined(IPPROTO_SCTP)
#define MAKO_SCTP_REAL 1
#endif
/* Kernel constant is 132 even if headers omit the name. */
#ifndef IPPROTO_SCTP
#define IPPROTO_SCTP 132
#endif
#if !defined(MAKO_SCTP_REAL)
/* Try the socket; availability is confirmed at runtime via available(). */
#define MAKO_SCTP_REAL 1
#endif
#endif

#if defined(MAKO_SCTP_REAL)
/* Prefer libsctp helpers when present. */
#if defined(__has_include)
#  if __has_include(<netinet/sctp.h>)
#    include <netinet/sctp.h>
#    define MAKO_SCTP_LIBSCTP 1
#  endif
#endif
#endif

/* ---- side channel for last recv stream / PPID (thread-local; single-thread demux) ---- */
#if defined(_WIN32)
static __declspec(thread) int mako_sctp_last_stream_id = 0;
static __declspec(thread) uint32_t mako_sctp_last_ppid_val = 0;
#else
static __thread int mako_sctp_last_stream_id = 0;
static __thread uint32_t mako_sctp_last_ppid_val = 0;
#endif

static inline int64_t mako_sctp_last_stream(void) {
    return (int64_t)mako_sctp_last_stream_id;
}
static inline int64_t mako_sctp_last_ppid(void) {
    return (int64_t)mako_sctp_last_ppid_val;
}

#if defined(MAKO_SCTP_REAL)

static inline int mako_sctp_probe_kernel(void) {
    static int cached = -1;
    if (cached >= 0) return cached;
    int fd = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP);
    if (fd < 0) {
        cached = 0;
        return 0;
    }
    close(fd);
    cached = 1;
    return 1;
}

static inline int64_t mako_sctp_available(void) {
    return mako_sctp_probe_kernel() ? 1 : 0;
}

static inline mako_sock_t mako_sctp_sock_create_af(int family) {
#if defined(SOCK_CLOEXEC)
    mako_sock_t fd = socket(family, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_SCTP);
#else
    mako_sock_t fd = socket(family, SOCK_STREAM, IPPROTO_SCTP);
#endif
    if (fd != MAKO_INVALID_SOCK) mako_sock_set_cloexec(fd);
    return fd;
}

static inline int64_t mako_sctp_listen_addr(MakoString host, int64_t port) {
    if (!mako_net_init() || !mako_sctp_probe_kernel()) return -1;
    if (port <= 0 || port > 65535) return -1;
    struct sockaddr_storage ss;
    socklen_t slen = 0;
    int fam = AF_INET, dual = 0;
    if (!mako_bind_addr_any(host, port, &ss, &slen, &fam, &dual)) return -1;
    /* Prefer IPv4 ANY for * like UDP for simpler peer addressing. */
    if (dual && fam == AF_INET6) {
        struct sockaddr_in *a4 = (struct sockaddr_in *)&ss;
        memset(&ss, 0, sizeof(ss));
        a4->sin_family = AF_INET;
        a4->sin_port = htons((uint16_t)port);
        a4->sin_addr.s_addr = htonl(INADDR_ANY);
        slen = sizeof(struct sockaddr_in);
        fam = AF_INET;
    }
    mako_sock_t fd = mako_sctp_sock_create_af(fam);
    if (fd == MAKO_INVALID_SOCK) return -1;
    int on = 1;
    setsockopt((int)fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (bind((int)fd, (struct sockaddr *)&ss, slen) != 0) {
        mako_sock_close(fd);
        return -1;
    }
    if (listen((int)fd, 128) != 0) {
        mako_sock_close(fd);
        return -1;
    }
    return (int64_t)fd;
}

static inline int64_t mako_sctp_listen(int64_t port) {
    /* Borrowed view — no heap allocation to free. */
    return mako_sctp_listen_addr(mako_str_view("*", 1), port);
}

static inline int64_t mako_sctp_accept(int64_t lfd) {
    if (lfd < 0) return -1;
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int fd = accept((int)lfd, (struct sockaddr *)&ss, &slen);
    if (fd < 0) return -1;
    mako_sock_set_cloexec((mako_sock_t)fd);
    return (int64_t)fd;
}

static inline int64_t mako_sctp_connect_timeout(
    MakoString host, int64_t port, int64_t timeout_ms
) {
    if (!mako_net_init() || !mako_sctp_probe_kernel()) return -1;
    if (!host.data || host.len == 0 || port <= 0 || port > 65535) return -1;
    char hbuf[256], pbuf[16];
    if (host.len >= sizeof(hbuf)) return -1;
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
    /* timeout_ms must be explicit and positive (no invented default). */
    if (timeout_ms <= 0) return -1;

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_SCTP;
    if (getaddrinfo(hp, pbuf, &hints, &res) != 0 || !res) {
        /* Some resolvers reject IPPROTO_SCTP; retry without protocol. */
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(hp, pbuf, &hints, &res) != 0 || !res) return -1;
    }

    int64_t deadline = mako_mono_ms() + timeout_ms;
    int64_t out = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        if (mako_mono_ms() >= deadline) break;
        mako_sock_t fd = mako_sctp_sock_create_af(ai->ai_family);
        if (fd == MAKO_INVALID_SOCK) continue;
        mako_sock_set_nonblock(fd, 1);
        int rc = connect((int)fd, ai->ai_addr, (socklen_t)ai->ai_addrlen);
        if (rc == 0) {
            mako_sock_set_nonblock(fd, 0);
            out = (int64_t)fd;
            break;
        }
#if defined(EINPROGRESS)
        if (errno == EINPROGRESS || errno == EINTR) {
#else
        if (0) {
#endif
            int64_t left = deadline - mako_mono_ms();
            if (left < 1) left = 1;
            if (mako_tcp_wait_writable((int)fd, (int)left) == 1
                && mako_sock_error((int)fd) == 0) {
                mako_sock_set_nonblock(fd, 0);
                out = (int64_t)fd;
                break;
            }
        }
        mako_sock_close(fd);
    }
    freeaddrinfo(res);
    return out;
}

/* Blocking-style connect: requires an explicit timeout via connect_timeout. */
static inline int64_t mako_sctp_connect(MakoString host, int64_t port) {
    (void)host; (void)port;
    return -1;
}

static inline int64_t mako_sctp_close(int64_t fd) {
    return mako_tcp_close(fd);
}

static inline int64_t mako_sctp_shutdown(int64_t fd, int64_t how) {
    return mako_tcp_shutdown(fd, how);
}

static inline int64_t mako_sctp_set_timeout(int64_t fd, int64_t ms) {
    return mako_tcp_set_timeout(fd, ms);
}

static inline int64_t mako_sctp_write(int64_t fd, MakoString s) {
    return mako_tcp_write(fd, s);
}

static inline int64_t mako_sctp_write_all(int64_t fd, MakoString s) {
    return mako_tcp_write_all(fd, s);
}

static inline MakoString mako_sctp_read(int64_t fd, int64_t max) {
    if (fd < 0) return mako_str_from_cstr("");
    /* max must be explicit; clamp only for allocator DoS. */
    if (max <= 0) return mako_str_from_cstr("");
    if (max > 4 * 1024 * 1024) max = 4 * 1024 * 1024;
    char *buf = (char *)malloc((size_t)max + 1);
    if (!buf) return mako_str_from_cstr("");
    ssize_t n = recv((int)fd, buf, (size_t)max, 0);
    if (n <= 0) {
        free(buf);
        return mako_str_from_cstr("");
    }
    buf[n] = 0;
    return (MakoString){buf, (size_t)n};
}

static inline MakoString mako_sctp_read_n(int64_t fd, int64_t n) {
    return mako_tcp_read_n(fd, n);
}

static inline MakoString mako_sctp_peer_addr(int64_t fd) {
    return mako_tcp_peer_addr(fd);
}

static inline MakoString mako_sctp_local_addr(int64_t fd) {
    return mako_tcp_local_addr(fd);
}

/* Stream-aware send. Uses sctp_sendmsg when libsctp is present; else stream 0. */
static inline int64_t mako_sctp_send_stream(
    int64_t fd, int64_t stream_id, MakoString data
) {
    if (fd < 0 || !data.data) return -1;
    if (stream_id < 0) stream_id = 0;
#if defined(MAKO_SCTP_LIBSCTP)
    struct sctp_sndrcvinfo sri;
    memset(&sri, 0, sizeof(sri));
    sri.sinfo_stream = (uint16_t)stream_id;
    ssize_t n = sctp_send(
        (int)fd, data.data, data.len, &sri, 0
    );
    if (n < 0) {
        /* Fall back to plain send if sctp_send unavailable at link time. */
        return mako_tcp_write(fd, data);
    }
    return (int64_t)n;
#else
    (void)stream_id;
    return mako_tcp_write(fd, data);
#endif
}

static inline MakoString mako_sctp_recv_stream(int64_t fd, int64_t max) {
    if (fd < 0) return mako_str_from_cstr("");
    if (max <= 0) return mako_str_from_cstr("");
    if (max > 4 * 1024 * 1024) max = 4 * 1024 * 1024;
#if defined(MAKO_SCTP_LIBSCTP)
    char *buf = (char *)malloc((size_t)max + 1);
    if (!buf) return mako_str_from_cstr("");
    struct sctp_sndrcvinfo sri;
    memset(&sri, 0, sizeof(sri));
    int flags = 0;
    ssize_t n = sctp_recvmsg(
        (int)fd, buf, (size_t)max, NULL, 0, &sri, &flags
    );
    if (n <= 0) {
        free(buf);
        return mako_str_from_cstr("");
    }
    mako_sctp_last_stream_id = (int)sri.sinfo_stream;
    mako_sctp_last_ppid_val = (uint32_t)ntohl(sri.sinfo_ppid);
    buf[n] = 0;
    return (MakoString){buf, (size_t)n};
#else
    MakoString s = mako_sctp_read(fd, max);
    mako_sctp_last_stream_id = 0;
    mako_sctp_last_ppid_val = 0;
    return s;
#endif
}

/* ---- Multistreaming: INITMSG before connect/listen ---- */
/* out/in stream counts must be explicit (1..65535); attempts/timeo left at 0 (kernel default). */
static inline int64_t mako_sctp_set_streams(
    int64_t fd, int64_t out_streams, int64_t in_streams
) {
    if (fd < 0) return -1;
    if (out_streams <= 0 || in_streams <= 0) return -1;
    if (out_streams > 65535 || in_streams > 65535) return -1;
#if defined(MAKO_SCTP_LIBSCTP) && defined(SCTP_INITMSG)
    struct sctp_initmsg init;
    memset(&init, 0, sizeof(init));
    init.sinit_num_ostreams = (uint16_t)out_streams;
    init.sinit_max_instreams = (uint16_t)in_streams;
    /* sinit_max_attempts / sinit_max_init_timeo = 0 → kernel defaults */
    if (setsockopt((int)fd, IPPROTO_SCTP, SCTP_INITMSG, &init, sizeof(init)) != 0) {
        return -1;
    }
    return 0;
#else
    (void)out_streams; (void)in_streams;
    return -1;
#endif
}

/* Returns (out_streams << 16) | in_streams, or -1. */
static inline int64_t mako_sctp_get_streams(int64_t fd) {
    if (fd < 0) return -1;
#if defined(MAKO_SCTP_LIBSCTP) && defined(SCTP_STATUS)
    struct sctp_status st;
    memset(&st, 0, sizeof(st));
    socklen_t len = sizeof(st);
    if (getsockopt((int)fd, IPPROTO_SCTP, SCTP_STATUS, &st, &len) != 0) return -1;
    return ((int64_t)st.sstat_outstrms << 16) | (int64_t)st.sstat_instrms;
#else
    (void)fd;
    return -1;
#endif
}

/* Send on stream with PPID (host-order ppid; converted to network order). */
static inline int64_t mako_sctp_send_ex(
    int64_t fd, int64_t stream_id, int64_t ppid, MakoString data
) {
    if (fd < 0 || !data.data) return -1;
    if (stream_id < 0) stream_id = 0;
#if defined(MAKO_SCTP_LIBSCTP)
    struct sctp_sndrcvinfo sri;
    memset(&sri, 0, sizeof(sri));
    sri.sinfo_stream = (uint16_t)stream_id;
    sri.sinfo_ppid = htonl((uint32_t)ppid);
    ssize_t n = sctp_send((int)fd, data.data, data.len, &sri, 0);
    if (n < 0) return mako_tcp_write(fd, data);
    return (int64_t)n;
#else
    (void)stream_id; (void)ppid;
    return mako_tcp_write(fd, data);
#endif
}

/* Heartbeat: interval_ms==0 disables where supported; path_max_retrans must be >0. */
static inline int64_t mako_sctp_set_heartbeat(
    int64_t fd, int64_t interval_ms, int64_t path_max_retrans
) {
    if (fd < 0) return -1;
    if (interval_ms < 0 || path_max_retrans <= 0 || path_max_retrans > 65535) return -1;
#if defined(MAKO_SCTP_LIBSCTP) && defined(SCTP_PEER_ADDR_PARAMS)
    struct sctp_paddrparams spp;
    memset(&spp, 0, sizeof(spp));
    spp.spp_address.ss_family = AF_UNSPEC; /* association-wide when supported */
    spp.spp_hbinterval = (uint32_t)interval_ms;
    spp.spp_pathmaxrxt = (uint16_t)path_max_retrans;
#if defined(SPP_HB_ENABLE) && defined(SPP_HB_DISABLE)
    if (interval_ms == 0) spp.spp_flags = SPP_HB_DISABLE;
    else spp.spp_flags = SPP_HB_ENABLE;
#endif
    if (setsockopt((int)fd, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS, &spp, sizeof(spp)) != 0) {
        return -1;
    }
    return 0;
#else
    (void)interval_ms; (void)path_max_retrans;
    return -1;
#endif
}

/* All three RTO values must be explicit and positive. */
static inline int64_t mako_sctp_set_rto(
    int64_t fd, int64_t initial_ms, int64_t min_ms, int64_t max_ms
) {
    if (fd < 0) return -1;
    if (initial_ms <= 0 || min_ms <= 0 || max_ms <= 0) return -1;
    if (min_ms > max_ms || initial_ms < min_ms || initial_ms > max_ms) return -1;
#if defined(MAKO_SCTP_LIBSCTP) && defined(SCTP_RTOINFO)
    struct sctp_rtoinfo rto;
    memset(&rto, 0, sizeof(rto));
    rto.srto_initial = (uint32_t)initial_ms;
    rto.srto_min = (uint32_t)min_ms;
    rto.srto_max = (uint32_t)max_ms;
    if (setsockopt((int)fd, IPPROTO_SCTP, SCTP_RTOINFO, &rto, sizeof(rto)) != 0) {
        return -1;
    }
    return 0;
#else
    (void)initial_ms; (void)min_ms; (void)max_ms;
    return -1;
#endif
}

/* Multihoming: resolve host:port into sockaddr_storage. */
static inline int mako_sctp_resolve_one(
    MakoString host, int64_t port, struct sockaddr_storage *ss, socklen_t *slen
) {
    if (!host.data || host.len == 0 || host.len >= 256 || port <= 0 || port > 65535) return 0;
    for (size_t i = 0; i < host.len; i++) {
        if (host.data[i] == 0 || host.data[i] == '\r' || host.data[i] == '\n') return 0;
    }
    char hbuf[256], pbuf[16];
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    snprintf(pbuf, sizeof(pbuf), "%d", (int)port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(hbuf, pbuf, &hints, &res) != 0 || !res) return 0;
    if (res->ai_addrlen > sizeof(*ss)) {
        freeaddrinfo(res);
        return 0;
    }
    memcpy(ss, res->ai_addr, res->ai_addrlen);
    *slen = (socklen_t)res->ai_addrlen;
    freeaddrinfo(res);
    return 1;
}

static inline int64_t mako_sctp_bindx_add(int64_t fd, MakoString host, int64_t port) {
    if (fd < 0) return -1;
#if defined(MAKO_SCTP_LIBSCTP)
    struct sockaddr_storage ss;
    socklen_t slen = 0;
    if (!mako_sctp_resolve_one(host, port, &ss, &slen)) return -1;
    if (sctp_bindx((int)fd, (struct sockaddr *)&ss, 1, SCTP_BINDX_ADD_ADDR) != 0) return -1;
    return 0;
#else
    (void)host; (void)port;
    return -1;
#endif
}

static inline int64_t mako_sctp_bindx_rem(int64_t fd, MakoString host, int64_t port) {
    if (fd < 0) return -1;
#if defined(MAKO_SCTP_LIBSCTP)
    struct sockaddr_storage ss;
    socklen_t slen = 0;
    if (!mako_sctp_resolve_one(host, port, &ss, &slen)) return -1;
    if (sctp_bindx((int)fd, (struct sockaddr *)&ss, 1, SCTP_BINDX_REM_ADDR) != 0) return -1;
    return 0;
#else
    (void)host; (void)port;
    return -1;
#endif
}

/* hosts: comma-separated IP/host list (no spaces required). */
static inline int64_t mako_sctp_connectx(
    MakoString hosts, int64_t port, int64_t timeout_ms
) {
    if (!mako_net_init() || !mako_sctp_probe_kernel()) return -1;
    if (!hosts.data || hosts.len == 0 || port <= 0 || port > 65535) return -1;
#if defined(MAKO_SCTP_LIBSCTP)
    if (timeout_ms <= 0) return -1;
    /* Parse up to 8 addresses (stack bound for sockaddr array only). */
    enum { MAKO_SCTP_CX_MAX = 8 };
    struct sockaddr_storage addrs[MAKO_SCTP_CX_MAX];
    int naddr = 0;
    size_t i = 0;
    while (i < hosts.len && naddr < MAKO_SCTP_CX_MAX) {
        while (i < hosts.len && (hosts.data[i] == ',' || hosts.data[i] == ' ')) i++;
        size_t start = i;
        while (i < hosts.len && hosts.data[i] != ',' && hosts.data[i] != ' ') i++;
        if (start >= i) break;
        MakoString h = {hosts.data + start, i - start};
        socklen_t slen = 0;
        if (mako_sctp_resolve_one(h, port, &addrs[naddr], &slen)) naddr++;
    }
    if (naddr == 0) return -1;
    int fam = addrs[0].ss_family;
    mako_sock_t fd = mako_sctp_sock_create_af(fam == 0 ? AF_INET : fam);
    if (fd == MAKO_INVALID_SOCK) return -1;
    mako_sock_set_nonblock(fd, 1);
    int rc = sctp_connectx((int)fd, (struct sockaddr *)addrs, naddr, NULL);
    if (rc == 0) {
        mako_sock_set_nonblock(fd, 0);
        return (int64_t)fd;
    }
#if defined(EINPROGRESS)
    if (errno == EINPROGRESS || errno == EINTR) {
        if (mako_tcp_wait_writable((int)fd, (int)timeout_ms) == 1
            && mako_sock_error((int)fd) == 0) {
            mako_sock_set_nonblock(fd, 0);
            return (int64_t)fd;
        }
    }
#endif
    mako_sock_close(fd);
    return -1;
#else
    /* Fallback: first host only via connect_timeout. */
    size_t i = 0;
    while (i < hosts.len && (hosts.data[i] == ',' || hosts.data[i] == ' ')) i++;
    size_t start = i;
    while (i < hosts.len && hosts.data[i] != ',' && hosts.data[i] != ' ') i++;
    if (start >= i) return -1;
    MakoString h = {hosts.data + start, i - start};
    return mako_sctp_connect_timeout(h, port, timeout_ms);
#endif
}

static inline MakoString mako_sctp_addrs_format_raw(struct sockaddr *sa, int n) {
    if (!sa || n <= 0) return mako_str_from_cstr("");
    size_t cap = (size_t)n * 64 + 1;
    char *out = (char *)malloc(cap);
    if (!out) return mako_str_from_cstr("");
    size_t off = 0;
    char *p = (char *)sa;
    for (int i = 0; i < n; i++) {
        struct sockaddr *a = (struct sockaddr *)p;
        socklen_t alen = (a->sa_family == AF_INET6)
            ? (socklen_t)sizeof(struct sockaddr_in6)
            : (socklen_t)sizeof(struct sockaddr_in);
        MakoString one = mako_sockaddr_str(a, alen);
        if (one.data && one.len) {
            if (off && off + 1 < cap) out[off++] = ',';
            if (off + one.len < cap) {
                memcpy(out + off, one.data, one.len);
                off += one.len;
            }
        }
        mako_str_free(one);
        p += (a->sa_family == AF_INET6) ? sizeof(struct sockaddr_in6)
                                        : sizeof(struct sockaddr_in);
    }
    out[off] = 0;
    return (MakoString){out, off};
}

static inline MakoString mako_sctp_getpaddrs(int64_t fd) {
    if (fd < 0) return mako_str_from_cstr("");
#if defined(MAKO_SCTP_LIBSCTP)
    struct sockaddr *sa = NULL;
    int n = sctp_getpaddrs((int)fd, 0, &sa);
    if (n <= 0 || !sa) return mako_str_from_cstr("");
    MakoString out = mako_sctp_addrs_format_raw(sa, n);
    sctp_freepaddrs(sa);
    return out;
#else
    return mako_sctp_peer_addr(fd);
#endif
}

static inline MakoString mako_sctp_getladdrs(int64_t fd) {
    if (fd < 0) return mako_str_from_cstr("");
#if defined(MAKO_SCTP_LIBSCTP)
    struct sockaddr *sa = NULL;
    int n = sctp_getladdrs((int)fd, 0, &sa);
    if (n <= 0 || !sa) return mako_str_from_cstr("");
    MakoString out = mako_sctp_addrs_format_raw(sa, n);
    sctp_freeladdrs(sa);
    return out;
#else
    return mako_sctp_local_addr(fd);
#endif
}

static inline int64_t mako_sctp_set_primary(int64_t fd, MakoString host, int64_t port) {
    if (fd < 0) return -1;
#if defined(MAKO_SCTP_LIBSCTP) && defined(SCTP_PRIMARY_ADDR)
    struct sctp_setprim prim;
    memset(&prim, 0, sizeof(prim));
    socklen_t slen = 0;
    if (!mako_sctp_resolve_one(host, port, &prim.ssp_addr, &slen)) return -1;
    if (setsockopt((int)fd, IPPROTO_SCTP, SCTP_PRIMARY_ADDR, &prim, sizeof(prim)) != 0) {
        return -1;
    }
    return 0;
#else
    (void)host; (void)port;
    return -1;
#endif
}

#else /* !MAKO_SCTP_REAL */

static inline int64_t mako_sctp_available(void) { return 0; }
static inline int64_t mako_sctp_listen_addr(MakoString host, int64_t port) {
    (void)host; (void)port; return -1;
}
static inline int64_t mako_sctp_listen(int64_t port) { (void)port; return -1; }
static inline int64_t mako_sctp_accept(int64_t fd) { (void)fd; return -1; }
static inline int64_t mako_sctp_connect(MakoString host, int64_t port) {
    (void)host; (void)port; return -1;
}
static inline int64_t mako_sctp_connect_timeout(
    MakoString host, int64_t port, int64_t timeout_ms
) {
    (void)host; (void)port; (void)timeout_ms; return -1;
}
static inline int64_t mako_sctp_close(int64_t fd) { (void)fd; return -1; }
static inline int64_t mako_sctp_shutdown(int64_t fd, int64_t how) {
    (void)fd; (void)how; return -1;
}
static inline int64_t mako_sctp_set_timeout(int64_t fd, int64_t ms) {
    (void)fd; (void)ms; return -1;
}
static inline int64_t mako_sctp_write(int64_t fd, MakoString s) {
    (void)fd; (void)s; return -1;
}
static inline int64_t mako_sctp_write_all(int64_t fd, MakoString s) {
    (void)fd; (void)s; return -1;
}
static inline MakoString mako_sctp_read(int64_t fd, int64_t max) {
    (void)fd; (void)max; return mako_str_from_cstr("");
}
static inline MakoString mako_sctp_read_n(int64_t fd, int64_t n) {
    (void)fd; (void)n; return mako_str_from_cstr("");
}
static inline MakoString mako_sctp_peer_addr(int64_t fd) {
    (void)fd; return mako_str_from_cstr("");
}
static inline MakoString mako_sctp_local_addr(int64_t fd) {
    (void)fd; return mako_str_from_cstr("");
}
static inline int64_t mako_sctp_send_stream(
    int64_t fd, int64_t stream_id, MakoString data
) {
    (void)fd; (void)stream_id; (void)data; return -1;
}
static inline MakoString mako_sctp_recv_stream(int64_t fd, int64_t max) {
    (void)fd; (void)max; return mako_str_from_cstr("");
}
static inline int64_t mako_sctp_set_streams(int64_t fd, int64_t o, int64_t i) {
    (void)fd; (void)o; (void)i; return -1;
}
static inline int64_t mako_sctp_get_streams(int64_t fd) { (void)fd; return -1; }
static inline int64_t mako_sctp_send_ex(
    int64_t fd, int64_t stream_id, int64_t ppid, MakoString data
) {
    (void)fd; (void)stream_id; (void)ppid; (void)data; return -1;
}
static inline int64_t mako_sctp_set_heartbeat(int64_t fd, int64_t ms, int64_t r) {
    (void)fd; (void)ms; (void)r; return -1;
}
static inline int64_t mako_sctp_set_rto(
    int64_t fd, int64_t a, int64_t b, int64_t c
) {
    (void)fd; (void)a; (void)b; (void)c; return -1;
}
static inline int64_t mako_sctp_bindx_add(int64_t fd, MakoString h, int64_t p) {
    (void)fd; (void)h; (void)p; return -1;
}
static inline int64_t mako_sctp_bindx_rem(int64_t fd, MakoString h, int64_t p) {
    (void)fd; (void)h; (void)p; return -1;
}
static inline int64_t mako_sctp_connectx(
    MakoString hosts, int64_t port, int64_t timeout_ms
) {
    (void)hosts; (void)port; (void)timeout_ms; return -1;
}
static inline MakoString mako_sctp_getpaddrs(int64_t fd) {
    (void)fd; return mako_str_from_cstr("");
}
static inline MakoString mako_sctp_getladdrs(int64_t fd) {
    (void)fd; return mako_str_from_cstr("");
}
static inline int64_t mako_sctp_set_primary(int64_t fd, MakoString h, int64_t p) {
    (void)fd; (void)h; (void)p; return -1;
}

#endif /* MAKO_SCTP_REAL */

#ifdef __cplusplus
}
#endif

#endif /* MAKO_SCTP_H */
