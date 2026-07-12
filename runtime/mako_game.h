/* Mako game server primitives — binary buffers, UDP with peer addressing,
 * tick loops, ring buffers. Designed for multiplayer game backends. */
#ifndef MAKO_GAME_H
#define MAKO_GAME_H

#include "mako_rt.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <time.h>

/* ============================================================
 * Binary Buffer — pack/unpack integers and floats for network packets.
 * Little-endian by default (game standard), with BE variants.
 * ============================================================ */

typedef struct {
    uint8_t *data;
    size_t cap;
    size_t len;     /* write cursor */
    size_t pos;     /* read cursor */
} MakoBuf;

static inline MakoBuf *mako_buf_new(int64_t capacity) {
    MakoBuf *b = (MakoBuf *)malloc(sizeof(MakoBuf));
    if (!b) return NULL;
    b->cap = (size_t)(capacity > 0 ? capacity : 256);
    b->data = (uint8_t *)calloc(b->cap, 1);
    if (!b->data) { free(b); return NULL; }
    b->len = 0;
    b->pos = 0;
    return b;
}

static inline MakoBuf *mako_buf_from_string(MakoString s) {
    MakoBuf *b = (MakoBuf *)malloc(sizeof(MakoBuf));
    if (!b) return NULL;
    b->cap = s.len > 0 ? s.len : 1;
    b->data = (uint8_t *)malloc(b->cap);
    if (!b->data) { free(b); return NULL; }
    if (s.len > 0) memcpy(b->data, s.data, s.len);
    b->len = s.len;
    b->pos = 0;
    return b;
}

static inline MakoString mako_buf_to_string(MakoBuf *b) {
    if (!b || b->len == 0) return mako_str_from_cstr("");
    char *d = (char *)malloc(b->len + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, b->data, b->len);
    d[b->len] = 0;
    return (MakoString){d, b->len};
}

static inline int64_t mako_buf_len(MakoBuf *b) {
    return b ? (int64_t)b->len : 0;
}

static inline int64_t mako_buf_pos(MakoBuf *b) {
    return b ? (int64_t)b->pos : 0;
}

static inline void mako_buf_reset(MakoBuf *b) {
    if (b) { b->len = 0; b->pos = 0; }
}

static inline void mako_buf_seek(MakoBuf *b, int64_t pos) {
    if (b && pos >= 0 && (size_t)pos <= b->len) b->pos = (size_t)pos;
}

static inline void mako_buf_free(MakoBuf *b) {
    if (b) { free(b->data); free(b); }
}

/* Ensure capacity for `extra` more bytes */
static inline void mako_buf_grow(MakoBuf *b, size_t extra) {
    if (b->len + extra > b->cap) {
        size_t nc = b->cap * 2;
        while (nc < b->len + extra) nc *= 2;
        uint8_t *nd = (uint8_t *)realloc(b->data, nc);
        if (nd) { b->data = nd; b->cap = nc; }
    }
}

/* ---- Write (pack) ---- */

static inline void mako_buf_write_u8(MakoBuf *b, int64_t v) {
    if (!b) return;
    mako_buf_grow(b, 1);
    b->data[b->len++] = (uint8_t)(v & 0xFF);
}

static inline void mako_buf_write_u16(MakoBuf *b, int64_t v) {
    if (!b) return;
    mako_buf_grow(b, 2);
    uint16_t x = (uint16_t)v;
    memcpy(b->data + b->len, &x, 2); /* little-endian on LE arch */
    b->len += 2;
}

static inline void mako_buf_write_u32(MakoBuf *b, int64_t v) {
    if (!b) return;
    mako_buf_grow(b, 4);
    uint32_t x = (uint32_t)v;
    memcpy(b->data + b->len, &x, 4);
    b->len += 4;
}

static inline void mako_buf_write_u64(MakoBuf *b, int64_t v) {
    if (!b) return;
    mako_buf_grow(b, 8);
    uint64_t x = (uint64_t)v;
    memcpy(b->data + b->len, &x, 8);
    b->len += 8;
}

static inline void mako_buf_write_i32(MakoBuf *b, int64_t v) {
    if (!b) return;
    mako_buf_grow(b, 4);
    int32_t x = (int32_t)v;
    memcpy(b->data + b->len, &x, 4);
    b->len += 4;
}

static inline void mako_buf_write_f32(MakoBuf *b, double v) {
    if (!b) return;
    mako_buf_grow(b, 4);
    float f = (float)v;
    memcpy(b->data + b->len, &f, 4);
    b->len += 4;
}

static inline void mako_buf_write_f64(MakoBuf *b, double v) {
    if (!b) return;
    mako_buf_grow(b, 8);
    memcpy(b->data + b->len, &v, 8);
    b->len += 8;
}

static inline void mako_buf_write_bytes(MakoBuf *b, MakoString s) {
    if (!b || !s.data || s.len == 0) return;
    mako_buf_grow(b, s.len);
    memcpy(b->data + b->len, s.data, s.len);
    b->len += s.len;
}

/* Write length-prefixed string (u16 len + data) */
static inline void mako_buf_write_str(MakoBuf *b, MakoString s) {
    if (!b) return;
    uint16_t slen = (uint16_t)(s.len > 65535 ? 65535 : s.len);
    mako_buf_grow(b, 2 + slen);
    memcpy(b->data + b->len, &slen, 2);
    b->len += 2;
    if (slen > 0 && s.data) {
        memcpy(b->data + b->len, s.data, slen);
        b->len += slen;
    }
}

/* ---- Big-endian write (network byte order) ---- */

static inline void mako_buf_write_u16be(MakoBuf *b, int64_t v) {
    if (!b) return;
    mako_buf_grow(b, 2);
    uint16_t x = htons((uint16_t)v);
    memcpy(b->data + b->len, &x, 2);
    b->len += 2;
}

static inline void mako_buf_write_u32be(MakoBuf *b, int64_t v) {
    if (!b) return;
    mako_buf_grow(b, 4);
    uint32_t x = htonl((uint32_t)v);
    memcpy(b->data + b->len, &x, 4);
    b->len += 4;
}

/* ---- Read (unpack) ---- */

static inline int64_t mako_buf_read_u8(MakoBuf *b) {
    if (!b || b->pos + 1 > b->len) return 0;
    return (int64_t)b->data[b->pos++];
}

static inline int64_t mako_buf_read_u16(MakoBuf *b) {
    if (!b || b->pos + 2 > b->len) return 0;
    uint16_t x;
    memcpy(&x, b->data + b->pos, 2);
    b->pos += 2;
    return (int64_t)x;
}

static inline int64_t mako_buf_read_u32(MakoBuf *b) {
    if (!b || b->pos + 4 > b->len) return 0;
    uint32_t x;
    memcpy(&x, b->data + b->pos, 4);
    b->pos += 4;
    return (int64_t)x;
}

static inline int64_t mako_buf_read_u64(MakoBuf *b) {
    if (!b || b->pos + 8 > b->len) return 0;
    uint64_t x;
    memcpy(&x, b->data + b->pos, 8);
    b->pos += 8;
    return (int64_t)x;
}

static inline int64_t mako_buf_read_i32(MakoBuf *b) {
    if (!b || b->pos + 4 > b->len) return 0;
    int32_t x;
    memcpy(&x, b->data + b->pos, 4);
    b->pos += 4;
    return (int64_t)x;
}

static inline double mako_buf_read_f32(MakoBuf *b) {
    if (!b || b->pos + 4 > b->len) return 0.0;
    float f;
    memcpy(&f, b->data + b->pos, 4);
    b->pos += 4;
    return (double)f;
}

static inline double mako_buf_read_f64(MakoBuf *b) {
    if (!b || b->pos + 8 > b->len) return 0.0;
    double d;
    memcpy(&d, b->data + b->pos, 8);
    b->pos += 8;
    return d;
}

static inline MakoString mako_buf_read_bytes(MakoBuf *b, int64_t count) {
    if (!b || count <= 0 || b->pos + (size_t)count > b->len)
        return mako_str_from_cstr("");
    char *d = (char *)malloc((size_t)count + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, b->data + b->pos, (size_t)count);
    d[count] = 0;
    b->pos += (size_t)count;
    return (MakoString){d, (size_t)count};
}

/* Read length-prefixed string (u16 len + data) */
static inline MakoString mako_buf_read_str(MakoBuf *b) {
    if (!b || b->pos + 2 > b->len) return mako_str_from_cstr("");
    uint16_t slen;
    memcpy(&slen, b->data + b->pos, 2);
    b->pos += 2;
    if (b->pos + slen > b->len) return mako_str_from_cstr("");
    char *d = (char *)malloc((size_t)slen + 1);
    if (!d) { b->pos += slen; return mako_str_from_cstr(""); }
    memcpy(d, b->data + b->pos, slen);
    d[slen] = 0;
    b->pos += slen;
    return (MakoString){d, (size_t)slen};
}

/* Big-endian reads */
static inline int64_t mako_buf_read_u16be(MakoBuf *b) {
    if (!b || b->pos + 2 > b->len) return 0;
    uint16_t x;
    memcpy(&x, b->data + b->pos, 2);
    b->pos += 2;
    return (int64_t)ntohs(x);
}

static inline int64_t mako_buf_read_u32be(MakoBuf *b) {
    if (!b || b->pos + 4 > b->len) return 0;
    uint32_t x;
    memcpy(&x, b->data + b->pos, 4);
    b->pos += 4;
    return (int64_t)ntohl(x);
}

/* ============================================================
 * UDP with peer addressing — recvfrom returns peer ID, sendto by peer ID.
 * Peers are tracked by IP:port in a table for fast lookup.
 * ============================================================ */

#define MAKO_MAX_PEERS 65536

typedef struct {
    struct sockaddr_in addr;
    int active;
} MakoPeer;

typedef struct {
    int fd;
    MakoPeer peers[MAKO_MAX_PEERS];
    int peer_count;
    int last_recv_peer;  /* peer index from last recvfrom */
    struct sockaddr_in last_addr;
} MakoGameUDP;

static inline MakoGameUDP *mako_game_udp_bind_addr(MakoString host, int64_t port) {
    MakoGameUDP *u = (MakoGameUDP *)calloc(1, sizeof(MakoGameUDP));
    if (!u) return NULL;

#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    u->fd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
    u->fd = socket(AF_INET, SOCK_DGRAM, 0);
#endif
    if (u->fd < 0) { free(u); return NULL; }

    int yes = 1;
    setsockopt(u->fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    if (!mako_bind_ipv4_addr(&addr, host, port)) {
#if defined(_WIN32)
        closesocket(u->fd);
#else
        close(u->fd);
#endif
        free(u);
        return NULL;
    }

    if (bind(u->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
#if defined(_WIN32)
        closesocket(u->fd);
#else
        close(u->fd);
#endif
        free(u);
        return NULL;
    }

    /* Set non-blocking */
#if defined(_WIN32)
    u_long mode = 1;
    ioctlsocket(u->fd, FIONBIO, &mode);
#else
    int flags = fcntl(u->fd, F_GETFL, 0);
    fcntl(u->fd, F_SETFL, flags | O_NONBLOCK);
#endif

    u->peer_count = 0;
    u->last_recv_peer = -1;
    return u;
}

/* Find or create peer index for an address */
static inline int mako_game_udp_find_peer(MakoGameUDP *u, struct sockaddr_in *addr) {
    for (int i = 0; i < u->peer_count; i++) {
        if (u->peers[i].active &&
            u->peers[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            u->peers[i].addr.sin_port == addr->sin_port) {
            return i;
        }
    }
    /* New peer */
    if (u->peer_count >= MAKO_MAX_PEERS) return -1;
    int idx = u->peer_count++;
    u->peers[idx].addr = *addr;
    u->peers[idx].active = 1;
    return idx;
}

/* Receive a packet. Returns data. Sets last_recv_peer for reply. */
static inline MakoString mako_game_udp_recv(MakoGameUDP *u) {
    if (!u) return mako_str_from_cstr("");
    char buf[65536];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    ssize_t n = recvfrom(u->fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
    if (n <= 0) { u->last_recv_peer = -1; return mako_str_from_cstr(""); }
    u->last_addr = from;
    u->last_recv_peer = mako_game_udp_find_peer(u, &from);
    char *d = (char *)malloc((size_t)n + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, buf, (size_t)n);
    d[n] = 0;
    return (MakoString){d, (size_t)n};
}

/* Get peer index of last received packet */
static inline int64_t mako_game_udp_sender(MakoGameUDP *u) {
    return u ? (int64_t)u->last_recv_peer : -1;
}

/* Address `host:port` of the last received packet's sender. Enables routing a
 * response back to whoever sent the request (proxy / SIP transaction mapping). */
static inline MakoString mako_game_udp_sender_addr(MakoGameUDP *u) {
    if (!u) return mako_str_from_cstr("");
    char host[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &u->last_addr.sin_addr, host, sizeof(host)))
        return mako_str_from_cstr("");
    char out[80];
    snprintf(out, sizeof(out), "%s:%d", host, (int)ntohs(u->last_addr.sin_port));
    return mako_str_from_cstr(out);
}

/* Send to an arbitrary `host` / `port` (IPv4 dotted). Lets a frontend forward to
 * an upstream that is not a registered peer. Returns bytes sent, or -1. */
static inline int64_t mako_game_udp_send_to(MakoGameUDP *u, MakoString host,
                                            int64_t port, MakoString data) {
    if (!u || !host.data || !data.data) return -1;
    char hbuf[64];
    size_t hn = host.len < sizeof(hbuf) - 1 ? host.len : sizeof(hbuf) - 1;
    memcpy(hbuf, host.data, hn);
    hbuf[hn] = 0;
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, hbuf, &dst.sin_addr) != 1) return -1;
    ssize_t n = sendto(u->fd, data.data, data.len, 0,
                       (struct sockaddr *)&dst, sizeof(dst));
    return (int64_t)n;
}

/* Send to a specific peer by index */
static inline int64_t mako_game_udp_send(MakoGameUDP *u, int64_t peer, MakoString data) {
    if (!u || peer < 0 || peer >= u->peer_count || !data.data) return -1;
    ssize_t n = sendto(u->fd, data.data, data.len, 0,
                       (struct sockaddr *)&u->peers[peer].addr, sizeof(struct sockaddr_in));
    return (int64_t)n;
}

/* Send to all connected peers (broadcast) */
static inline int64_t mako_game_udp_broadcast(MakoGameUDP *u, MakoString data) {
    if (!u || !data.data) return -1;
    int sent = 0;
    for (int i = 0; i < u->peer_count; i++) {
        if (u->peers[i].active) {
            sendto(u->fd, data.data, data.len, 0,
                   (struct sockaddr *)&u->peers[i].addr, sizeof(struct sockaddr_in));
            sent++;
        }
    }
    return (int64_t)sent;
}

/* Disconnect a peer */
static inline void mako_game_udp_kick(MakoGameUDP *u, int64_t peer) {
    if (u && peer >= 0 && peer < u->peer_count) {
        u->peers[peer].active = 0;
    }
}

/* Get peer count */
static inline int64_t mako_game_udp_peers(MakoGameUDP *u) {
    if (!u) return 0;
    int count = 0;
    for (int i = 0; i < u->peer_count; i++) {
        if (u->peers[i].active) count++;
    }
    return (int64_t)count;
}

/* Get fd for use with evloop */
static inline int64_t mako_game_udp_fd(MakoGameUDP *u) {
    return u ? (int64_t)u->fd : -1;
}

/* Close */
static inline void mako_game_udp_close(MakoGameUDP *u) {
    if (!u) return;
#if defined(_WIN32)
    closesocket(u->fd);
#else
    close(u->fd);
#endif
    free(u);
}

/* ============================================================
 * High-precision tick timer — for game loops (30Hz, 60Hz, 128Hz).
 * ============================================================ */

static inline int64_t mako_tick_now_us(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (int64_t)((now.QuadPart * 1000000LL) / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
#endif
}

/* Returns microseconds to sleep until next tick. Call at start of loop.
 * tick_interval_us = 1000000 / hz (e.g., 16667 for 60Hz) */
static inline int64_t mako_tick_sleep_us(int64_t tick_start_us, int64_t tick_interval_us) {
    int64_t now = mako_tick_now_us();
    int64_t elapsed = now - tick_start_us;
    int64_t remaining = tick_interval_us - elapsed;
    if (remaining <= 0) return 0;

#if defined(_WIN32)
    Sleep((DWORD)(remaining / 1000));
#else
    struct timespec ts;
    ts.tv_sec = remaining / 1000000;
    ts.tv_nsec = (remaining % 1000000) * 1000;
    nanosleep(&ts, NULL);
#endif
    return remaining;
}

/* ============================================================
 * Math helpers for game physics (exposed as builtins)
 * ============================================================ */

static inline double mako_sqrt(double x) { return sqrt(x); }
static inline double mako_sin(double x) { return sin(x); }
static inline double mako_cos(double x) { return cos(x); }
static inline double mako_atan2(double y, double x) { return atan2(y, x); }
static inline double mako_floor(double x) { return floor(x); }
static inline double mako_ceil(double x) { return ceil(x); }
static inline double mako_abs_f(double x) { return fabs(x); }

/* Distance between two 2D points */
static inline double mako_dist2d(double x1, double y1, double x2, double y2) {
    double dx = x2 - x1, dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

/* Lerp */
static inline double mako_lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

/* Clamp */
static inline double mako_clamp_f(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

#endif /* MAKO_GAME_H */
