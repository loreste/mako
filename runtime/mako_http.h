/* HTTP connection API — synchronous, one-request-at-a-time server.
 *
 * Usage: bind a port, accept connections in a loop, read method/path/headers/body,
 * send a response, close. Handler logic stays in Mako (no colored async).
 *
 * Security: header names/values are validated (rejects CR/LF/NUL injection).
 * Content-Length is enforced on responses. No HTTP/1.0 chunked by default.
 *
 * Thread safety: each connection fd is independent. Do not share a single fd
 * across threads. Use one fd per crew task for concurrent request handling.
 *
 * Limits: header buffer is 8KB. Request body reads up to Content-Length or 1MB
 * (whichever is smaller) unless overridden.
 */
#ifndef MAKO_HTTP_H
#define MAKO_HTTP_H

#include "mako_rt.h"
#include "mako_hpack_huffman.h"
#include <errno.h>
#include <string.h>
#if defined(_WIN32)
/* Winsock + platform shims come from mako_platform.h via mako_rt.h */
#ifndef MSG_PEEK
#define MSG_PEEK 0x2
#endif
#else
#include <sys/select.h>
#include <fcntl.h>
#include <sys/time.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline int mako_http_listen_fd(int64_t port) {
    if (!mako_net_init()) return -1;
    mako_sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == MAKO_INVALID_SOCK) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        mako_sock_close(fd);
        return -1;
    }
    if (listen(fd, 128) < 0) {
        mako_sock_close(fd);
        return -1;
    }
    return (int)fd;
}

static inline const char *mako_http_reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}

static inline void mako_http_reply_conn(
    int cfd,
    int status,
    const char *ctype,
    MakoString body,
    int keep_alive
) {
    /* Reject CR/LF/NUL in Content-Type (header injection). */
    if (ctype) {
        size_t clen = strlen(ctype);
        if (!mako_http_header_value_ok(ctype, clen) ||
            !mako_http_header_name_ok("Content-Type", 12)) {
            fprintf(stderr, "error: http: illegal Content-Type (CR/LF/NUL or bad token)\n");
            return;
        }
    }
    const char *reason = mako_http_reason(status);
    const char *conn = keep_alive ? "keep-alive" : "close";
    char hdr[360];
    int n = snprintf(
        hdr,
        sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "\r\n",
        status,
        reason,
        ctype ? ctype : "application/octet-stream",
        body.len,
        conn
    );
    if (n > 0) (void)send(cfd, hdr, (size_t)n, 0);
    if (body.len) (void)send(cfd, body.data, body.len, 0);
}

static inline void mako_http_reply(int cfd, int status, const char *ctype, MakoString body) {
    mako_http_reply_conn(cfd, status, ctype, body, 0);
}

/* ---- Parsed request connection table (sync handler surface) ---- */
typedef struct {
    int fd;
    bool live;
    bool keep_alive;
    MakoArena arena;
    MakoString method;
    MakoString path;
    MakoString body;
    MakoString host;
    MakoString user_agent;
    MakoString content_type_req;
    char raw[8192];
    size_t raw_len;
} MakoHttpConn;

#define MAKO_HTTP_CONN_MAX 32
static MakoHttpConn mako_http_conns[MAKO_HTTP_CONN_MAX];

typedef struct {
    volatile int requested;
    volatile int ready;
    int64_t started_ms;
    int64_t deadline_ms;
    int64_t grace_ms;
} MakoHttpShutdownState;

static MakoHttpShutdownState mako_http_shutdown_state = {0, 1, 0, 0, 0};

static inline int64_t mako_http_shutdown_begin(int64_t grace_ms) {
    if (grace_ms < 0) grace_ms = 0;
    int64_t now = mako_now_ms();
    mako_http_shutdown_state.requested = 1;
    mako_http_shutdown_state.ready = 0;
    mako_http_shutdown_state.started_ms = now;
    mako_http_shutdown_state.grace_ms = grace_ms;
    mako_http_shutdown_state.deadline_ms = now + grace_ms;
    return mako_http_shutdown_state.deadline_ms;
}

static inline int64_t mako_http_shutdown_reset(void) {
    mako_http_shutdown_state.requested = 0;
    mako_http_shutdown_state.ready = 1;
    mako_http_shutdown_state.started_ms = 0;
    mako_http_shutdown_state.deadline_ms = 0;
    mako_http_shutdown_state.grace_ms = 0;
    return 1;
}

static inline int64_t mako_http_shutdown_requested(void) {
    return mako_http_shutdown_state.requested ? 1 : 0;
}

static inline int64_t mako_http_shutdown_ready(void) {
    return mako_http_shutdown_state.ready ? 1 : 0;
}

static inline int64_t mako_http_shutdown_deadline(void) {
    return mako_http_shutdown_state.deadline_ms;
}

static inline int64_t mako_http_shutdown_remaining(void) {
    if (!mako_http_shutdown_state.requested) return -1;
    int64_t left = mako_http_shutdown_state.deadline_ms - mako_now_ms();
    return left > 0 ? left : 0;
}

static inline int64_t mako_http_shutdown_expired(void) {
    if (!mako_http_shutdown_state.requested) return 0;
    return mako_now_ms() >= mako_http_shutdown_state.deadline_ms ? 1 : 0;
}

static inline int64_t mako_http_active_connections(void) {
    int64_t n = 0;
    for (int i = 0; i < MAKO_HTTP_CONN_MAX; i++) {
        if (mako_http_conns[i].live) n++;
    }
    return n;
}

typedef struct {
    MakoString method;
    MakoString path;
    MakoString body;
} MakoHttpRequest;

static inline int mako_http_find_header(
    const char *req,
    const char *name,
    char *out,
    size_t outcap
) {
    size_t nlen = strlen(name);
    const char *p = req;
    while (p && *p) {
        const char *line = p;
        const char *nl = strstr(p, "\r\n");
        size_t llen = nl ? (size_t)(nl - p) : strlen(p);
        if (llen == 0) break;
        if (llen >= nlen + 1) {
            int match = 1;
            for (size_t i = 0; i < nlen; i++) {
                char a = line[i], b = name[i];
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                if (a != b) {
                    match = 0;
                    break;
                }
            }
            if (match && line[nlen] == ':') {
                const char *v = line + nlen + 1;
                while (*v == ' ') v++;
                size_t vlen = (size_t)((line + llen) - v);
                if (vlen >= outcap) vlen = outcap - 1;
                memcpy(out, v, vlen);
                out[vlen] = 0;
                return 1;
            }
        }
        if (!nl) break;
        p = nl + 2;
    }
    out[0] = 0;
    return 0;
}

static inline MakoHttpRequest mako_http_parse_request(MakoArena *arena, const char *raw, size_t n) {
    MakoHttpRequest r;
    r.method = mako_arena_cstr(arena, "GET");
    r.path = mako_arena_cstr(arena, "/");
    r.body = mako_arena_text_n(arena, "", 0);
    if (!raw || n == 0) return r;
    char line[1024];
    size_t line_len = n;
    const char *le = strstr(raw, "\r\n");
    if (le) line_len = (size_t)(le - raw);
    if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
    memcpy(line, raw, line_len);
    line[line_len] = 0;
    char *sp1 = strchr(line, ' ');
    if (sp1) {
        *sp1 = 0;
        r.method = mako_arena_cstr(arena, line);
        char *sp2 = strchr(sp1 + 1, ' ');
        if (sp2) *sp2 = 0;
        r.path = mako_arena_cstr(arena, sp1 + 1);
    }
    const char *body = strstr(raw, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t blen = n - (size_t)(body - raw);
        r.body = mako_arena_text_n(arena, body, blen);
    }
    return r;
}

static inline void mako_http_fill_conn(MakoHttpConn *c, const char *req, size_t n) {
    c->raw_len = n;
    if (c->raw_len >= sizeof(c->raw)) c->raw_len = sizeof(c->raw) - 1;
    memcpy(c->raw, req, c->raw_len);
    c->raw[c->raw_len] = 0;
    MakoHttpRequest hr = mako_http_parse_request(&c->arena, req, n);
    c->method = hr.method;
    c->path = hr.path;
    c->body = hr.body;
    char tmp[512];
    if (mako_http_find_header(req, "Host", tmp, sizeof(tmp)))
        c->host = mako_arena_cstr(&c->arena, tmp);
    else
        c->host = mako_arena_text_n(&c->arena, "", 0);
    if (mako_http_find_header(req, "User-Agent", tmp, sizeof(tmp)))
        c->user_agent = mako_arena_cstr(&c->arena, tmp);
    else
        c->user_agent = mako_arena_text_n(&c->arena, "", 0);
    if (mako_http_find_header(req, "Content-Type", tmp, sizeof(tmp)))
        c->content_type_req = mako_arena_cstr(&c->arena, tmp);
    else
        c->content_type_req = mako_arena_text_n(&c->arena, "", 0);
    /* Keep-alive if Connection: keep-alive, or HTTP/1.1 without Connection: close */
    c->keep_alive = 0;
    if (mako_http_find_header(req, "Connection", tmp, sizeof(tmp))) {
        for (char *p = tmp; *p; p++)
            if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
        if (strstr(tmp, "keep-alive")) c->keep_alive = 1;
        if (strstr(tmp, "close")) c->keep_alive = 0;
    } else if (strstr(req, "HTTP/1.1")) {
        c->keep_alive = 1; /* HTTP/1.1 default */
    }
}

static inline int64_t mako_http_bind(int64_t port) {
    int fd = mako_http_listen_fd(port);
    if (fd < 0) {
        fprintf(stderr, "error: http_bind(:%lld) failed\n", (long long)port);
        return -1;
    }
    fprintf(stderr, "mako http_bind on :%lld\n", (long long)port);
    return (int64_t)fd;
}

/* Reap keep-alive slots whose peer has closed (curl/default KA left open).
 * Without this, one-shot servers that never call http_next/http_close fill
 * MAKO_HTTP_CONN_MAX after ~32 requests. */
static inline void mako_http_reap_dead(void) {
    for (int i = 0; i < MAKO_HTTP_CONN_MAX; i++) {
        MakoHttpConn *c = &mako_http_conns[i];
        if (!c->live || c->fd < 0) continue;
        fd_set rfds, efds;
        FD_ZERO(&rfds);
        FD_ZERO(&efds);
        FD_SET(c->fd, &rfds);
        FD_SET(c->fd, &efds);
        struct timeval tv = {0, 0};
        int pr = select(c->fd + 1, &rfds, NULL, &efds, &tv);
        if (pr < 0) continue;
        if (FD_ISSET(c->fd, &efds)) {
            mako_sock_close(c->fd);
            mako_arena_free(&c->arena);
            c->live = false;
            continue;
        }
        if (!FD_ISSET(c->fd, &rfds)) continue;
        /* Readable: peer closed (EOF) or next request waiting for http_next. */
        char b;
        ssize_t n = recv(c->fd, &b, 1, MSG_PEEK);
        if (n == 0) {
            mako_sock_close(c->fd);
            mako_arena_free(&c->arena);
            c->live = false;
        }
        /* n > 0 → leave for http_next; n < 0 → leave (EAGAIN etc.) */
    }
}

static inline int64_t mako_http_accept(int64_t listen_fd) {
    if (listen_fd < 0) return -1;
    if (mako_http_shutdown_state.requested) return -1;
    mako_http_reap_dead();
    int cfd = accept((int)listen_fd, NULL, NULL);
    if (cfd < 0) return -1;
    char req[8192];
    ssize_t n = recv(cfd, req, sizeof(req) - 1, 0);
    if (n < 0) n = 0;
    req[n] = 0;

    int slot = -1;
    for (int i = 0; i < MAKO_HTTP_CONN_MAX; i++) {
        if (!mako_http_conns[i].live) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        mako_sock_close(cfd);
        fprintf(stderr, "error: http_accept: connection table full\n");
        return -1;
    }
    MakoHttpConn *c = &mako_http_conns[slot];
    memset(c, 0, sizeof(*c));
    c->fd = cfd;
    c->live = true;
    c->arena = mako_arena_new();
    mako_http_fill_conn(c, req, (size_t)n);
    return (int64_t)slot;
}

/* Read next request on same connection. Returns conn id, or -1 if closed/error.
 * Only valid after respond that left the connection open (keep-alive). */
static inline int64_t mako_http_next(int64_t conn) {
    if (conn < 0 || conn >= MAKO_HTTP_CONN_MAX) return -1;
    MakoHttpConn *c = &mako_http_conns[conn];
    if (!c->live || c->fd < 0) return -1;
    char req[8192];
    ssize_t n = recv(c->fd, req, sizeof(req) - 1, 0);
    if (n <= 0) {
        mako_sock_close(c->fd);
        mako_arena_free(&c->arena);
        c->live = false;
        return -1;
    }
    req[n] = 0;
    mako_arena_free(&c->arena);
    c->arena = mako_arena_new();
    mako_http_fill_conn(c, req, (size_t)n);
    return conn;
}

static inline int64_t mako_http_keepalive(int64_t conn) {
    if (conn < 0 || conn >= MAKO_HTTP_CONN_MAX || !mako_http_conns[conn].live)
        return 0;
    return mako_http_conns[conn].keep_alive ? 1 : 0;
}

static inline MakoString mako_http_method(int64_t conn) {
    if (conn < 0 || conn >= MAKO_HTTP_CONN_MAX || !mako_http_conns[conn].live)
        return mako_str_from_cstr("");
    return mako_http_conns[conn].method;
}

static inline MakoString mako_http_path(int64_t conn) {
    if (conn < 0 || conn >= MAKO_HTTP_CONN_MAX || !mako_http_conns[conn].live)
        return mako_str_from_cstr("");
    return mako_http_conns[conn].path;
}

static inline MakoString mako_http_body(int64_t conn) {
    if (conn < 0 || conn >= MAKO_HTTP_CONN_MAX || !mako_http_conns[conn].live)
        return mako_str_from_cstr("");
    return mako_http_conns[conn].body;
}

/* ---- Typed HttpRequest (value type for handlers / TLS docs) ---- */

static inline MakoHttpRequest mako_http_request_empty(void) {
    MakoHttpRequest r;
    r.method = mako_str_from_cstr("GET");
    r.path = mako_str_from_cstr("/");
    r.body = mako_str_from_cstr("");
    return r;
}

static inline MakoHttpRequest mako_http_request_parse(MakoString raw) {
    MakoHttpRequest r = mako_http_request_empty();
    if (!raw.data || raw.len == 0) return r;
    size_t i = 0;
    while (i < raw.len && raw.data[i] != ' ') i++;
    if (i == 0 || i >= raw.len) return r;
    free(r.method.data);
    r.method = mako_str_slice(raw, 0, (int64_t)i);
    size_t p0 = i + 1;
    size_t p1 = p0;
    while (p1 < raw.len && raw.data[p1] != ' ' && raw.data[p1] != '\r' && raw.data[p1] != '\n')
        p1++;
    free(r.path.data);
    r.path = mako_str_slice(raw, (int64_t)p0, (int64_t)p1);
    const char *sep = NULL;
    for (size_t k = 0; k + 3 < raw.len; k++) {
        if (raw.data[k] == '\r' && raw.data[k + 1] == '\n' && raw.data[k + 2] == '\r' &&
            raw.data[k + 3] == '\n') {
            sep = raw.data + k + 4;
            break;
        }
    }
    if (!sep) {
        for (size_t k = 0; k + 1 < raw.len; k++) {
            if (raw.data[k] == '\n' && raw.data[k + 1] == '\n') {
                sep = raw.data + k + 2;
                break;
            }
        }
    }
    if (sep) {
        size_t off = (size_t)(sep - raw.data);
        free(r.body.data);
        r.body = mako_str_slice(raw, (int64_t)off, (int64_t)raw.len);
    }
    return r;
}

static inline MakoHttpRequest mako_http_request_from_conn(int64_t conn) {
    MakoHttpRequest r;
    r.method = mako_str_clone(mako_http_method(conn));
    r.path = mako_str_clone(mako_http_path(conn));
    r.body = mako_str_clone(mako_http_body(conn));
    return r;
}

static inline MakoString mako_http_request_method(MakoHttpRequest r) {
    return mako_str_clone(r.method);
}

static inline MakoString mako_http_request_path(MakoHttpRequest r) {
    return mako_str_clone(r.path);
}

static inline MakoString mako_http_request_body(MakoHttpRequest r) {
    return mako_str_clone(r.body);
}

static inline int mako_http_route_next_seg(MakoString s, size_t *pos, size_t *start, size_t *len) {
    if (!s.data || !pos || !start || !len) return 0;
    size_t p = *pos;
    while (p < s.len && s.data[p] == '/') p++;
    if (p >= s.len) {
        *pos = p;
        return 0;
    }
    size_t st = p;
    while (p < s.len && s.data[p] != '/') p++;
    *start = st;
    *len = p - st;
    *pos = p;
    return 1;
}

static inline int mako_http_route_seg_eq(MakoString s, size_t a, size_t alen, MakoString t, size_t b, size_t blen) {
    if (alen != blen) return 0;
    if (alen == 0) return 1;
    return s.data && t.data && memcmp(s.data + a, t.data + b, alen) == 0;
}

static inline int mako_http_route_is_param(MakoString pattern, size_t start, size_t len) {
    return len >= 3 && pattern.data && pattern.data[start] == '{' &&
           pattern.data[start + len - 1] == '}';
}

static inline int mako_http_route_match_path(MakoString path, MakoString pattern) {
    if (!path.data || !pattern.data) return 0;
    if (path.len == 0 || pattern.len == 0) return 0;
    size_t pp = 0, qp = 0;
    for (;;) {
        size_t ps = 0, pl = 0, qs = 0, ql = 0;
        int ph = mako_http_route_next_seg(path, &pp, &ps, &pl);
        int qh = mako_http_route_next_seg(pattern, &qp, &qs, &ql);
        if (!ph || !qh) return ph == qh;
        if (mako_http_route_is_param(pattern, qs, ql)) continue;
        if (!mako_http_route_seg_eq(path, ps, pl, pattern, qs, ql)) return 0;
    }
}

static inline bool mako_http_route_match(MakoHttpRequest r, MakoString method, MakoString pattern) {
    return mako_str_eq(r.method, method) && mako_http_route_match_path(r.path, pattern);
}

static inline MakoString mako_http_route_param(MakoHttpRequest r, MakoString pattern, MakoString name) {
    if (!r.path.data || !pattern.data || !name.data) return mako_str_from_cstr("");
    size_t pp = 0, qp = 0;
    for (;;) {
        size_t ps = 0, pl = 0, qs = 0, ql = 0;
        int ph = mako_http_route_next_seg(r.path, &pp, &ps, &pl);
        int qh = mako_http_route_next_seg(pattern, &qp, &qs, &ql);
        if (!ph || !qh) return mako_str_from_cstr("");
        if (!mako_http_route_is_param(pattern, qs, ql)) {
            if (!mako_http_route_seg_eq(r.path, ps, pl, pattern, qs, ql)) return mako_str_from_cstr("");
            continue;
        }
        size_t ns = qs + 1;
        size_t nl = ql - 2;
        if (nl == name.len && memcmp(pattern.data + ns, name.data, nl) == 0) {
            return mako_str_slice(r.path, (int64_t)ps, (int64_t)(ps + pl));
        }
    }
}

#define MAKO_ROUTER_MAX 64
#define MAKO_ROUTER_ROUTE_MAX 512

typedef struct {
    int live;
    int64_t root;
    MakoString prefix;
} MakoRouter;

typedef struct {
    int live;
    int64_t root;
    MakoString method;
    MakoString pattern;
    MakoString handler;
} MakoRouteEntry;

static MakoRouter mako_routers[MAKO_ROUTER_MAX];
static MakoRouteEntry mako_router_routes[MAKO_ROUTER_ROUTE_MAX];
static atomic_llong mako_router_next_id = 0;
static atomic_llong mako_router_route_next_id = 0;

static inline MakoString mako_router_join_path(MakoString prefix, MakoString pattern) {
    if (!prefix.data || prefix.len == 0 || (prefix.len == 1 && prefix.data[0] == '/')) {
        return mako_str_clone(pattern);
    }
    if (!pattern.data || pattern.len == 0 || (pattern.len == 1 && pattern.data[0] == '/')) {
        return mako_str_clone(prefix);
    }
    int prefix_slash = prefix.data[prefix.len - 1] == '/';
    int pattern_slash = pattern.data[0] == '/';
    size_t trim = (prefix_slash && pattern_slash) ? 1 : 0;
    size_t add = (!prefix_slash && !pattern_slash) ? 1 : 0;
    size_t n = prefix.len + pattern.len - trim + add;
    char *d = (char *)malloc(n + 1);
    if (!d) mako_abort("router_join_path: out of memory");
    size_t pos = 0;
    memcpy(d + pos, prefix.data, prefix.len);
    pos += prefix.len;
    if (add) d[pos++] = '/';
    size_t pat_start = trim ? 1 : 0;
    memcpy(d + pos, pattern.data + pat_start, pattern.len - pat_start);
    pos += pattern.len - pat_start;
    d[pos] = 0;
    return (MakoString){d, pos};
}

static inline MakoRouter *mako_router_get(int64_t id) {
    if (id <= 0 || id > MAKO_ROUTER_MAX) return NULL;
    MakoRouter *r = &mako_routers[id - 1];
    return r->live ? r : NULL;
}

static inline int64_t mako_router_new(void) {
    long long idx = atomic_fetch_add_explicit(&mako_router_next_id, 1, memory_order_relaxed);
    if (idx < 0 || idx >= MAKO_ROUTER_MAX) return 0;
    MakoRouter *r = &mako_routers[idx];
    r->live = 1;
    r->root = idx + 1;
    r->prefix = mako_str_from_cstr("");
    return idx + 1;
}

static inline int64_t mako_router_group(int64_t router_id, MakoString prefix) {
    MakoRouter *parent = mako_router_get(router_id);
    if (!parent) return 0;
    long long idx = atomic_fetch_add_explicit(&mako_router_next_id, 1, memory_order_relaxed);
    if (idx < 0 || idx >= MAKO_ROUTER_MAX) return 0;
    MakoRouter *r = &mako_routers[idx];
    r->live = 1;
    r->root = parent->root;
    r->prefix = mako_router_join_path(parent->prefix, prefix);
    return idx + 1;
}

static inline int64_t mako_router_add(int64_t router_id, MakoString method, MakoString pattern, MakoString handler) {
    MakoRouter *router = mako_router_get(router_id);
    if (!router || !method.data || !pattern.data || !handler.data) return 0;
    long long idx = atomic_fetch_add_explicit(&mako_router_route_next_id, 1, memory_order_relaxed);
    if (idx < 0 || idx >= MAKO_ROUTER_ROUTE_MAX) return 0;
    MakoRouteEntry *e = &mako_router_routes[idx];
    e->live = 1;
    e->root = router->root;
    e->method = mako_str_clone(method);
    e->pattern = mako_router_join_path(router->prefix, pattern);
    e->handler = mako_str_clone(handler);
    return idx + 1;
}

static inline MakoString mako_router_match_path(int64_t router_id, MakoString method, MakoString path) {
    MakoRouter *router = mako_router_get(router_id);
    if (!router || !method.data || !path.data) return mako_str_from_cstr("");
    for (int64_t i = 0; i < MAKO_ROUTER_ROUTE_MAX; i++) {
        MakoRouteEntry *e = &mako_router_routes[i];
        if (!e->live || e->root != router->root) continue;
        if (!mako_str_eq(e->method, method)) continue;
        if (mako_http_route_match_path(path, e->pattern)) return mako_str_clone(e->handler);
    }
    return mako_str_from_cstr("");
}

static inline MakoString mako_router_match(int64_t router_id, MakoHttpRequest req) {
    return mako_router_match_path(router_id, req.method, req.path);
}

static inline MakoString mako_router_param(int64_t router_id, MakoHttpRequest req, MakoString name) {
    MakoRouter *router = mako_router_get(router_id);
    if (!router || !name.data) return mako_str_from_cstr("");
    for (int64_t i = 0; i < MAKO_ROUTER_ROUTE_MAX; i++) {
        MakoRouteEntry *e = &mako_router_routes[i];
        if (!e->live || e->root != router->root) continue;
        if (!mako_str_eq(e->method, req.method)) continue;
        if (!mako_http_route_match_path(req.path, e->pattern)) continue;
        return mako_http_route_param(req, e->pattern, name);
    }
    return mako_str_from_cstr("");
}

static inline int64_t mako_router_count(int64_t router_id) {
    MakoRouter *router = mako_router_get(router_id);
    if (!router) return 0;
    int64_t n = 0;
    for (int64_t i = 0; i < MAKO_ROUTER_ROUTE_MAX; i++) {
        MakoRouteEntry *e = &mako_router_routes[i];
        if (e->live && e->root == router->root) n++;
    }
    return n;
}

#define MAKO_REQCTX_MAX 128
#define MAKO_REQCTX_ENTRY_MAX 1024
#define MAKO_MIDDLEWARE_TRACE_MAX 1024

typedef struct {
    int live;
} MakoReqCtx;

typedef struct {
    int live;
    int64_t ctx;
    MakoString key;
    MakoString value;
} MakoReqCtxEntry;

typedef struct {
    int live;
    int64_t ctx;
    MakoString name;
} MakoMiddlewareTraceEntry;

static MakoReqCtx mako_reqctxs[MAKO_REQCTX_MAX];
static MakoReqCtxEntry mako_reqctx_entries[MAKO_REQCTX_ENTRY_MAX];
static MakoMiddlewareTraceEntry mako_middleware_trace_entries[MAKO_MIDDLEWARE_TRACE_MAX];
static atomic_llong mako_reqctx_next_id = 0;
static atomic_llong mako_reqctx_entry_next_id = 0;
static atomic_llong mako_middleware_trace_next_id = 0;

static inline MakoReqCtx *mako_reqctx_get(int64_t id) {
    if (id <= 0 || id > MAKO_REQCTX_MAX) return NULL;
    MakoReqCtx *ctx = &mako_reqctxs[id - 1];
    return ctx->live ? ctx : NULL;
}

static inline int64_t mako_reqctx_new(void) {
    long long idx = atomic_fetch_add_explicit(&mako_reqctx_next_id, 1, memory_order_relaxed);
    if (idx < 0 || idx >= MAKO_REQCTX_MAX) return 0;
    mako_reqctxs[idx].live = 1;
    return idx + 1;
}

static inline int64_t mako_reqctx_set(int64_t ctx_id, MakoString key, MakoString value) {
    if (!mako_reqctx_get(ctx_id) || !key.data || key.len == 0) return 0;
    for (int64_t i = 0; i < MAKO_REQCTX_ENTRY_MAX; i++) {
        MakoReqCtxEntry *e = &mako_reqctx_entries[i];
        if (e->live && e->ctx == ctx_id && mako_str_eq(e->key, key)) {
            e->value = mako_str_clone(value);
            return 1;
        }
    }
    long long idx = atomic_fetch_add_explicit(&mako_reqctx_entry_next_id, 1, memory_order_relaxed);
    if (idx < 0 || idx >= MAKO_REQCTX_ENTRY_MAX) return 0;
    MakoReqCtxEntry *e = &mako_reqctx_entries[idx];
    e->live = 1;
    e->ctx = ctx_id;
    e->key = mako_str_clone(key);
    e->value = mako_str_clone(value);
    return 1;
}

static inline MakoString mako_reqctx_get_value(int64_t ctx_id, MakoString key) {
    if (!mako_reqctx_get(ctx_id) || !key.data) return mako_str_from_cstr("");
    for (int64_t i = 0; i < MAKO_REQCTX_ENTRY_MAX; i++) {
        MakoReqCtxEntry *e = &mako_reqctx_entries[i];
        if (e->live && e->ctx == ctx_id && mako_str_eq(e->key, key)) return mako_str_clone(e->value);
    }
    return mako_str_from_cstr("");
}

static inline int64_t mako_reqctx_has(int64_t ctx_id, MakoString key) {
    if (!mako_reqctx_get(ctx_id) || !key.data) return 0;
    for (int64_t i = 0; i < MAKO_REQCTX_ENTRY_MAX; i++) {
        MakoReqCtxEntry *e = &mako_reqctx_entries[i];
        if (e->live && e->ctx == ctx_id && mako_str_eq(e->key, key)) return 1;
    }
    return 0;
}

static inline int64_t mako_reqctx_count(int64_t ctx_id) {
    if (!mako_reqctx_get(ctx_id)) return 0;
    int64_t n = 0;
    for (int64_t i = 0; i < MAKO_REQCTX_ENTRY_MAX; i++) {
        MakoReqCtxEntry *e = &mako_reqctx_entries[i];
        if (e->live && e->ctx == ctx_id) n++;
    }
    return n;
}

static inline int mako_http_csv_contains(MakoString csv, MakoString item) {
    if (!csv.data || !item.data || item.len == 0) return 0;
    size_t i = 0;
    while (i <= csv.len) {
        while (i < csv.len && (csv.data[i] == ' ' || csv.data[i] == '\t' || csv.data[i] == ',')) i++;
        size_t start = i;
        while (i < csv.len && csv.data[i] != ',') i++;
        size_t end = i;
        while (end > start && (csv.data[end - 1] == ' ' || csv.data[end - 1] == '\t')) end--;
        if (end - start == item.len && memcmp(csv.data + start, item.data, item.len) == 0) return 1;
        if (i >= csv.len) break;
        i++;
    }
    return 0;
}

static inline int64_t mako_middleware_allow_methods(MakoHttpRequest req, MakoString methods_csv) {
    return mako_http_csv_contains(methods_csv, req.method) ? 1 : 0;
}

static inline int64_t mako_middleware_next(int64_t ctx_id, MakoString name) {
    if (!mako_reqctx_get(ctx_id) || !name.data || name.len == 0) return 0;
    long long idx = atomic_fetch_add_explicit(&mako_middleware_trace_next_id, 1, memory_order_relaxed);
    if (idx < 0 || idx >= MAKO_MIDDLEWARE_TRACE_MAX) return 0;
    MakoMiddlewareTraceEntry *e = &mako_middleware_trace_entries[idx];
    e->live = 1;
    e->ctx = ctx_id;
    e->name = mako_str_clone(name);
    return 1;
}

static inline int64_t mako_middleware_ran(int64_t ctx_id, MakoString name) {
    if (!mako_reqctx_get(ctx_id) || !name.data) return 0;
    for (int64_t i = 0; i < MAKO_MIDDLEWARE_TRACE_MAX; i++) {
        MakoMiddlewareTraceEntry *e = &mako_middleware_trace_entries[i];
        if (e->live && e->ctx == ctx_id && mako_str_eq(e->name, name)) return 1;
    }
    return 0;
}

static inline MakoString mako_middleware_trace(int64_t ctx_id) {
    if (!mako_reqctx_get(ctx_id)) return mako_str_from_cstr("");
    size_t total = 1;
    int64_t count = 0;
    for (int64_t i = 0; i < MAKO_MIDDLEWARE_TRACE_MAX; i++) {
        MakoMiddlewareTraceEntry *e = &mako_middleware_trace_entries[i];
        if (e->live && e->ctx == ctx_id) {
            total += e->name.len + (count > 0 ? 1 : 0);
            count++;
        }
    }
    char *d = (char *)malloc(total);
    if (!d) mako_abort("middleware_trace: out of memory");
    size_t pos = 0;
    count = 0;
    for (int64_t i = 0; i < MAKO_MIDDLEWARE_TRACE_MAX; i++) {
        MakoMiddlewareTraceEntry *e = &mako_middleware_trace_entries[i];
        if (e->live && e->ctx == ctx_id) {
            if (count > 0) d[pos++] = '>';
            memcpy(d + pos, e->name.data, e->name.len);
            pos += e->name.len;
            count++;
        }
    }
    d[pos] = 0;
    return (MakoString){d, pos};
}

static inline int64_t mako_middleware_require_context(int64_t ctx_id, MakoString key) {
    return mako_reqctx_has(ctx_id, key);
}

static inline MakoString mako_http_header(int64_t conn, MakoString name) {
    if (conn < 0 || conn >= MAKO_HTTP_CONN_MAX || !mako_http_conns[conn].live)
        return mako_str_from_cstr("");
    MakoHttpConn *c = &mako_http_conns[conn];
    char nbuf[128];
    if (name.len >= sizeof(nbuf)) return mako_str_from_cstr("");
    memcpy(nbuf, name.data, name.len);
    nbuf[name.len] = 0;
    /* fast paths */
    if (strcmp(nbuf, "Host") == 0 || strcmp(nbuf, "host") == 0) return c->host;
    if (strcmp(nbuf, "User-Agent") == 0 || strcmp(nbuf, "user-agent") == 0)
        return c->user_agent;
    if (strcmp(nbuf, "Content-Type") == 0 || strcmp(nbuf, "content-type") == 0)
        return c->content_type_req;
    char tmp[512];
    if (mako_http_find_header(c->raw, nbuf, tmp, sizeof(tmp)))
        return mako_arena_text(&c->arena, mako_str_from_cstr(tmp));
    return mako_str_from_cstr("");
}

static inline int64_t mako_http_respond(int64_t conn, int64_t status, MakoString body) {
    if (conn < 0 || conn >= MAKO_HTTP_CONN_MAX || !mako_http_conns[conn].live)
        return 0;
    MakoHttpConn *c = &mako_http_conns[conn];
    int ka = c->keep_alive ? 1 : 0;
    mako_http_reply_conn(c->fd, (int)status, "text/plain; charset=utf-8", body, ka);
    if (!ka) {
        mako_sock_close(c->fd);
        mako_arena_free(&c->arena);
        c->live = false;
    }
    return 1;
}

static inline int64_t mako_http_respond_ct(
    int64_t conn,
    int64_t status,
    MakoString content_type,
    MakoString body
) {
    if (conn < 0 || conn >= MAKO_HTTP_CONN_MAX || !mako_http_conns[conn].live)
        return 0;
    MakoHttpConn *c = &mako_http_conns[conn];
    char ctype[128];
    size_t n = content_type.len < sizeof(ctype) - 1 ? content_type.len : sizeof(ctype) - 1;
    memcpy(ctype, content_type.data, n);
    ctype[n] = 0;
    int ka = c->keep_alive ? 1 : 0;
    mako_http_reply_conn(c->fd, (int)status, ctype, body, ka);
    if (!ka) {
        mako_sock_close(c->fd);
        mako_arena_free(&c->arena);
        c->live = false;
    }
    return 1;
}

/* JSON response helper for API handlers (Content-Type: application/json). */
static inline int64_t mako_http_respond_json(int64_t conn, int64_t status, MakoString body) {
    return mako_http_respond_ct(
        conn, status, mako_str_from_cstr("application/json; charset=utf-8"), body
    );
}

static inline MakoString mako_http_health_json(MakoString service, int64_t ready) {
    size_t cap = service.len * 2 + 96;
    char *buf = (char *)malloc(cap);
    if (!buf) mako_abort("http_health_json: out of memory");
    const char *status = ready ? "ready" : "not_ready";
    size_t len = (size_t)snprintf(
        buf,
        cap,
        "{\"ok\":%s,\"status\":\"%s\",\"service\":\"",
        ready ? "true" : "false",
        status
    );
    for (size_t i = 0; i < service.len; i++) {
        unsigned char c = (unsigned char)service.data[i];
        if (len + 8 >= cap) {
            cap *= 2;
            char *next = (char *)realloc(buf, cap);
            if (!next) {
                free(buf);
                mako_abort("http_health_json: out of memory");
            }
            buf = next;
        }
        if (c == '"' || c == '\\') {
            buf[len++] = '\\';
            buf[len++] = (char)c;
        } else if (c == '\n') {
            buf[len++] = '\\';
            buf[len++] = 'n';
        } else if (c == '\r') {
            buf[len++] = '\\';
            buf[len++] = 'r';
        } else if (c == '\t') {
            buf[len++] = '\\';
            buf[len++] = 't';
        } else if (c >= 0x20) {
            buf[len++] = (char)c;
        }
    }
    if (len + 4 >= cap) {
        char *next = (char *)realloc(buf, len + 4);
        if (!next) {
            free(buf);
            mako_abort("http_health_json: out of memory");
        }
        buf = next;
        cap = len + 4;
    }
    (void)cap;
    buf[len++] = '"';
    buf[len++] = '}';
    buf[len++] = '\n';
    buf[len] = 0;
    MakoString out = {buf, len};
    return out;
}

static inline int64_t mako_http_respond_health(int64_t conn, MakoString service, int64_t ready) {
    if (mako_http_shutdown_state.requested) ready = 0;
    MakoString body = mako_http_health_json(service, ready);
    return mako_http_respond_json(conn, ready ? 200 : 503, body);
}

static inline int64_t mako_http_shutdown_drain_conn(int64_t conn) {
    if (conn < 0 || conn >= MAKO_HTTP_CONN_MAX || !mako_http_conns[conn].live)
        return 0;
    MakoHttpConn *c = &mako_http_conns[conn];
    c->keep_alive = 0;
    if (!mako_http_shutdown_state.requested) return 1;
    if (mako_http_shutdown_expired()) {
        mako_sock_close(c->fd);
        mako_arena_free(&c->arena);
        c->live = false;
        return 2;
    }
    return 1;
}

/* Force-close a keep-alive connection after last response. */
static inline int64_t mako_http_close(int64_t conn) {
    if (conn < 0 || conn >= MAKO_HTTP_CONN_MAX || !mako_http_conns[conn].live)
        return 0;
    MakoHttpConn *c = &mako_http_conns[conn];
    mako_sock_close(c->fd);
    mako_arena_free(&c->arena);
    c->live = false;
    return 1;
}

/* Close the listen socket from `http_bind` (demo / graceful exit). */
static inline int64_t mako_http_close_listener(int64_t listen_fd) {
    if (listen_fd < 0) return 0;
    return mako_sock_close((mako_sock_t)listen_fd) == 0 ? 1 : 0;
}

/* Fixed-body server (legacy hello). */

/* HTTP/2 connection preface (RFC 7540 §3.5). Detect only — no full H2 stack. */
static const char MAKO_HTTP2_PREFACE[] =
    "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

static inline int mako_http2_is_preface(const char *buf, size_t n) {
    const size_t need = sizeof(MAKO_HTTP2_PREFACE) - 1;
    if (n < need || !buf) return 0;
    return memcmp(buf, MAKO_HTTP2_PREFACE, need) == 0;
}

static inline bool mako_http2_detect(MakoString s) {
    return mako_http2_is_preface(s.data ? s.data : "", s.len) != 0;
}

/* HTTP/2 frame header: length(24) type(8) flags(8) stream(31). SETTINGS type = 0x4. */
static inline int mako_http2_parse_settings(const unsigned char *buf, size_t n, int64_t *out_len) {
    if (out_len) *out_len = -1;
    if (!buf || n < 9) return 0;
    unsigned int len = ((unsigned int)buf[0] << 16) | ((unsigned int)buf[1] << 8) | (unsigned int)buf[2];
    unsigned char typ = buf[3];
    /* unsigned char flags = buf[4]; */
    unsigned int stream = ((unsigned int)(buf[5] & 0x7f) << 24) | ((unsigned int)buf[6] << 16)
                    | ((unsigned int)buf[7] << 8) | (unsigned int)buf[8];
    if (typ != 0x04) return 0; /* SETTINGS */
    if (stream != 0) return 0; /* SETTINGS must be stream 0 */
    if (n < 9 + len) return 0;
    if (len % 6 != 0) return 0; /* settings pairs are 6 bytes */
    if (out_len) *out_len = (int64_t)len;
    return 1;
}

/* After client preface, expect SETTINGS. Returns settings payload length or -1. */
static inline int64_t mako_http2_settings_len(MakoString s) {
    const unsigned char *p = (const unsigned char *)(s.data ? s.data : "");
    size_t n = s.len;
    size_t pref = sizeof(MAKO_HTTP2_PREFACE) - 1;
    if (n >= pref && mako_http2_is_preface((const char *)p, n)) {
        p += pref;
        n -= pref;
    }
    int64_t slen = -1;
    if (!mako_http2_parse_settings(p, n, &slen)) return -1;
    return slen;
}

/* Empty SETTINGS frame (server connection preface after client preface). 9-byte header, len=0. */
static inline MakoString mako_http2_empty_settings(void) {
    static const unsigned char frame[9] = {
        0, 0, 0, /* length */
        0x04,    /* SETTINGS */
        0x00,    /* flags */
        0, 0, 0, 0 /* stream 0 */
    };
    char *d = (char *)malloc(10);
    memcpy(d, frame, 9);
    d[9] = 0;
    return (MakoString){d, 9};
}

/* SETTINGS with MAX_CONCURRENT_STREAMS (id=0x3). */
static inline MakoString mako_http2_settings_max_concurrent(int64_t n) {
    if (n < 0) n = 0;
    unsigned char frame[15] = {
        0, 0, 6, /* length */
        0x04, 0x00,
        0, 0, 0, 0,
        0, 0x03,
        (unsigned char)((n >> 24) & 0xff),
        (unsigned char)((n >> 16) & 0xff),
        (unsigned char)((n >> 8) & 0xff),
        (unsigned char)(n & 0xff)
    };
    char *d = (char *)malloc(16);
    memcpy(d, frame, 15);
    d[15] = 0;
    return (MakoString){d, 15};
}

/* SETTINGS with ACK flag (0x1) — empty payload. */
static inline MakoString mako_http2_settings_ack(void) {
    static const unsigned char frame[9] = {
        0, 0, 0,
        0x04, /* SETTINGS */
        0x01, /* ACK */
        0, 0, 0, 0
    };
    char *d = (char *)malloc(10);
    memcpy(d, frame, 9);
    d[9] = 0;
    return (MakoString){d, 9};
}

/* Client connection preface: magic + empty SETTINGS. */
static inline MakoString mako_http2_client_preface(void) {
    size_t pref = sizeof(MAKO_HTTP2_PREFACE) - 1;
    MakoString settings = mako_http2_empty_settings();
    size_t n = pref + settings.len;
    char *d = (char *)malloc(n + 1);
    memcpy(d, MAKO_HTTP2_PREFACE, pref);
    memcpy(d + pref, settings.data, settings.len);
    d[n] = 0;
    free(settings.data);
    return (MakoString){d, n};
}

/* Server connection bootstrap after client preface: SETTINGS + SETTINGS ACK. */
static inline MakoString mako_http2_server_preface(void) {
    MakoString s = mako_http2_empty_settings();
    MakoString a = mako_http2_settings_ack();
    MakoString out = mako_str_concat(s, a);
    free(s.data);
    free(a.data);
    return out;
}



/* Generic HTTP/2 frame header parse. Types: DATA=0 HEADERS=1 SETTINGS=4 ...
 * Returns 1 on success; out_* may be NULL. Limits: header only, no HPACK. */
static inline int mako_http2_parse_frame(
    const unsigned char *buf, size_t n,
    int64_t *out_len, int64_t *out_type, int64_t *out_flags, int64_t *out_stream
) {
    if (out_len) *out_len = -1;
    if (out_type) *out_type = -1;
    if (out_flags) *out_flags = -1;
    if (out_stream) *out_stream = -1;
    if (!buf || n < 9) return 0;
    unsigned int len = ((unsigned int)buf[0] << 16) | ((unsigned int)buf[1] << 8) | (unsigned int)buf[2];
    unsigned char typ = buf[3];
    unsigned char flags = buf[4];
    unsigned int stream = ((unsigned int)(buf[5] & 0x7f) << 24) | ((unsigned int)buf[6] << 16)
                    | ((unsigned int)buf[7] << 8) | (unsigned int)buf[8];
    if (n < 9 + len) return 0;
    if (out_len) *out_len = (int64_t)len;
    if (out_type) *out_type = (int64_t)typ;
    if (out_flags) *out_flags = (int64_t)flags;
    if (out_stream) *out_stream = (int64_t)stream;
    return 1;
}

static inline int64_t mako_http2_frame_type(MakoString s) {
    const unsigned char *p = (const unsigned char *)(s.data ? s.data : "");
    size_t n = s.len;
    size_t pref = sizeof(MAKO_HTTP2_PREFACE) - 1;
    if (n >= pref && mako_http2_is_preface((const char *)p, n)) {
        p += pref; n -= pref;
    }
    int64_t typ = -1;
    if (!mako_http2_parse_frame(p, n, NULL, &typ, NULL, NULL)) return -1;
    return typ;
}

static inline int64_t mako_http2_frame_stream(MakoString s) {
    const unsigned char *p = (const unsigned char *)(s.data ? s.data : "");
    size_t n = s.len;
    size_t pref = sizeof(MAKO_HTTP2_PREFACE) - 1;
    if (n >= pref && mako_http2_is_preface((const char *)p, n)) {
        p += pref; n -= pref;
    }
    int64_t stream = -1;
    if (!mako_http2_parse_frame(p, n, NULL, NULL, NULL, &stream)) return -1;
    return stream;
}

static inline int64_t mako_http2_frame_len(MakoString s) {
    const unsigned char *p = (const unsigned char *)(s.data ? s.data : "");
    size_t n = s.len;
    size_t pref = sizeof(MAKO_HTTP2_PREFACE) - 1;
    if (n >= pref && mako_http2_is_preface((const char *)p, n)) {
        p += pref; n -= pref;
    }
    int64_t len = -1;
    if (!mako_http2_parse_frame(p, n, &len, NULL, NULL, NULL)) return -1;
    return len;
}

static inline int64_t mako_http2_frame_flags(MakoString s) {
    const unsigned char *p = (const unsigned char *)(s.data ? s.data : "");
    size_t n = s.len;
    size_t pref = sizeof(MAKO_HTTP2_PREFACE) - 1;
    if (n >= pref && mako_http2_is_preface((const char *)p, n)) {
        p += pref; n -= pref;
    }
    int64_t flags = -1;
    if (!mako_http2_parse_frame(p, n, NULL, NULL, &flags, NULL)) return -1;
    return flags;
}

/* True if first frame is SETTINGS with ACK flag. */
static inline bool mako_http2_is_settings_ack(MakoString s) {
    int64_t typ = mako_http2_frame_type(s);
    int64_t flags = mako_http2_frame_flags(s);
    return typ == 4 && (flags & 0x1) != 0;
}

/* Multi-entry dynamic table (RFC 7541 §2.3). Newest @ HPACK index 62.
 * Limits: capacity 4, FIFO eviction of oldest, process-global, no Huffman. */
#define MAKO_HPACK_DYN_CAP 4
typedef struct {
    char *n;
    size_t nlen;
    char *v;
    size_t vlen;
} MakoHpackDynEntry;
static MakoHpackDynEntry mako_hpack_dyn_tab[MAKO_HPACK_DYN_CAP];
static int mako_hpack_dyn_count = 0;

static inline void mako_hpack_dyn_free_entry(MakoHpackDynEntry *e) {
    free(e->n); e->n = NULL; e->nlen = 0;
    free(e->v); e->v = NULL; e->vlen = 0;
}

static inline void mako_hpack_dyn_clear(void) {
    for (int i = 0; i < mako_hpack_dyn_count; i++) {
        mako_hpack_dyn_free_entry(&mako_hpack_dyn_tab[i]);
    }
    mako_hpack_dyn_count = 0;
}

/* Insert at front (newest). Evicts oldest if full. Returns HPACK index 62, or -1. */
static inline int64_t mako_hpack_dyn_insert(MakoString name, MakoString value) {
    size_t nl = name.data ? name.len : 0;
    size_t vl = value.data ? value.len : 0;
    if (nl == 0 || nl > 127 || vl > 127) return -1;
    char *nn = (char *)malloc(nl + 1);
    char *vv = (char *)malloc(vl + 1);
    if (!nn || !vv) {
        free(nn);
        free(vv);
        return -1;
    }
    memcpy(nn, name.data, nl);
    nn[nl] = 0;
    if (vl && value.data) memcpy(vv, value.data, vl);
    vv[vl] = 0;
    if (mako_hpack_dyn_count == MAKO_HPACK_DYN_CAP) {
        mako_hpack_dyn_free_entry(&mako_hpack_dyn_tab[MAKO_HPACK_DYN_CAP - 1]);
        mako_hpack_dyn_count = MAKO_HPACK_DYN_CAP - 1;
    }
    for (int i = mako_hpack_dyn_count; i > 0; i--) {
        mako_hpack_dyn_tab[i] = mako_hpack_dyn_tab[i - 1];
    }
    mako_hpack_dyn_tab[0].n = nn;
    mako_hpack_dyn_tab[0].nlen = nl;
    mako_hpack_dyn_tab[0].v = vv;
    mako_hpack_dyn_tab[0].vlen = vl;
    mako_hpack_dyn_count++;
    return 62;
}

static inline int64_t mako_hpack_dyn_len(void) {
    return (int64_t)mako_hpack_dyn_count;
}

/* HPACK index → dyn slot (0 = newest). Returns -1 if not dynamic. */
static inline int mako_hpack_dyn_slot(int64_t index) {
    if (index < 62) return -1;
    int slot = (int)(index - 62);
    if (slot < 0 || slot >= mako_hpack_dyn_count) return -1;
    return slot;
}

static inline MakoString mako_hpack_dyn_name(void) {
    if (mako_hpack_dyn_count < 1) return (MakoString){NULL, 0};
    return mako_str_slice(
        (MakoString){mako_hpack_dyn_tab[0].n, mako_hpack_dyn_tab[0].nlen},
        0,
        (int64_t)mako_hpack_dyn_tab[0].nlen
    );
}

static inline MakoString mako_hpack_dyn_value(void) {
    if (mako_hpack_dyn_count < 1) return (MakoString){NULL, 0};
    return mako_str_slice(
        (MakoString){
            mako_hpack_dyn_tab[0].v ? mako_hpack_dyn_tab[0].v : (char *)"",
            mako_hpack_dyn_tab[0].vlen
        },
        0,
        (int64_t)mako_hpack_dyn_tab[0].vlen
    );
}

static inline MakoString mako_hpack_dyn_name_at(int64_t index) {
    int slot = mako_hpack_dyn_slot(index);
    if (slot < 0) return (MakoString){NULL, 0};
    return mako_str_slice(
        (MakoString){mako_hpack_dyn_tab[slot].n, mako_hpack_dyn_tab[slot].nlen},
        0,
        (int64_t)mako_hpack_dyn_tab[slot].nlen
    );
}

static inline MakoString mako_hpack_dyn_value_at(int64_t index) {
    int slot = mako_hpack_dyn_slot(index);
    if (slot < 0) return (MakoString){NULL, 0};
    return mako_str_slice(
        (MakoString){
            mako_hpack_dyn_tab[slot].v ? mako_hpack_dyn_tab[slot].v : (char *)"",
            mako_hpack_dyn_tab[slot].vlen
        },
        0,
        (int64_t)mako_hpack_dyn_tab[slot].vlen
    );
}

/* HPACK indexed header (RFC 7541 §6.1). Index 62..61+N = dynamic.
 * Limits: no Huffman; N ≤ MAKO_HPACK_DYN_CAP. */
static inline MakoString mako_hpack_encode_indexed(int64_t index) {
    if (index < 1) return (MakoString){NULL, 0};
    if (index >= 62) {
        if (mako_hpack_dyn_slot(index) < 0) return (MakoString){NULL, 0};
    } else if (index > 61) {
        return (MakoString){NULL, 0};
    }
    if (index > 127) return (MakoString){NULL, 0}; /* single-byte only */
    char *d = (char *)malloc(2);
    d[0] = (char)(0x80 | (unsigned)(index & 0x7f));
    d[1] = 0;
    return (MakoString){d, 1};
}

/* Decode first indexed header; returns table index or -1. */
static inline int64_t mako_hpack_decode_indexed(MakoString block) {
    if (!block.data || block.len < 1) return -1;
    unsigned char b = (unsigned char)block.data[0];
    if ((b & 0x80) == 0) return -1;
    int64_t idx = (int64_t)(b & 0x7f);
    if (idx < 1) return -1;
    if (idx >= 62) {
        if (mako_hpack_dyn_slot(idx) < 0) return -1;
        return idx;
    }
    if (idx > 61) return -1;
    return idx;
}

/* Legacy name: now decodes indexed form (or -1). */
static inline int64_t mako_hpack_decode_stub(MakoString block) {
    return mako_hpack_decode_indexed(block);
}

/* Encode HPACK string (RFC 7541 §5.2): 7-bit length, no Huffman. Max len 127. */
static inline int mako_hpack_put_string(char *dst, size_t cap, MakoString s, size_t *wrote) {
    size_t n = s.data ? s.len : 0;
    if (n > 127 || cap < 1 + n) return 0;
    dst[0] = (char)(n & 0x7f); /* H=0 */
    if (n && s.data) memcpy(dst + 1, s.data, n);
    *wrote = 1 + n;
    return 1;
}

/* HPACK Huffman roundtrip helpers (RFC 7541 Appendix B). See mako_hpack_huffman.h. */

/* Literal Header Field without Indexing — New Name (RFC 7541 §6.2.2): 0x00 + name + value.
 * Limits: no Huffman, lengths ≤127, no dynamic table. */
static inline MakoString mako_hpack_encode_literal(MakoString name, MakoString value) {
    size_t nlen = name.data ? name.len : 0;
    size_t vlen = value.data ? value.len : 0;
    if (nlen > 127 || vlen > 127) return (MakoString){NULL, 0};
    size_t n = 1 + 1 + nlen + 1 + vlen;
    char *d = (char *)malloc(n + 1);
    if (!d) return (MakoString){NULL, 0};
    d[0] = 0x00;
    size_t off = 1, w = 0;
    if (!mako_hpack_put_string(d + off, n - off, name, &w)) { free(d); return (MakoString){NULL, 0}; }
    off += w;
    if (!mako_hpack_put_string(d + off, n - off, value, &w)) { free(d); return (MakoString){NULL, 0}; }
    off += w;
    d[off] = 0;
    return (MakoString){d, off};
}

static inline int mako_hpack_parse_string(
    const unsigned char *p, size_t n, size_t *off, size_t *out_start, size_t *out_len
) {
    if (!p || !off || *off >= n) return 0;
    unsigned char b = p[*off];
    if (b & 0x80) return 0; /* Huffman not supported */
    size_t len = (size_t)(b & 0x7f);
    size_t start = *off + 1;
    if (start + len > n) return 0;
    if (out_start) *out_start = start;
    if (out_len) *out_len = len;
    *off = start + len;
    return 1;
}

/* Decode literal-new-name block → name. Empty if not that form. */
static inline MakoString mako_hpack_literal_name(MakoString block) {
    if (!block.data || block.len < 1 || (unsigned char)block.data[0] != 0x00) {
        return (MakoString){NULL, 0};
    }
    const unsigned char *p = (const unsigned char *)block.data;
    size_t off = 1, ns = 0, nl = 0;
    if (!mako_hpack_parse_string(p, block.len, &off, &ns, &nl)) return (MakoString){NULL, 0};
    return mako_str_slice(block, (int64_t)ns, (int64_t)(ns + nl));
}

static inline MakoString mako_hpack_literal_value(MakoString block) {
    if (!block.data || block.len < 1 || (unsigned char)block.data[0] != 0x00) {
        return (MakoString){NULL, 0};
    }
    const unsigned char *p = (const unsigned char *)block.data;
    size_t off = 1, ns = 0, nl = 0, vs = 0, vl = 0;
    if (!mako_hpack_parse_string(p, block.len, &off, &ns, &nl)) return (MakoString){NULL, 0};
    if (!mako_hpack_parse_string(p, block.len, &off, &vs, &vl)) return (MakoString){NULL, 0};
    (void)ns; (void)nl;
    return mako_str_slice(block, (int64_t)vs, (int64_t)(vs + vl));
}

/* Static table index → name (RFC 7541 Appendix A subset). Index ≥62 → dynamic.
 * Limits: names only for static; dyn cap MAKO_HPACK_DYN_CAP; no Huffman. */
static inline MakoString mako_hpack_static_name(int64_t index) {
    if (index >= 62) return mako_hpack_dyn_name_at(index);
    const char *n = NULL;
    switch (index) {
        case 1: n = ":authority"; break;
        case 2: n = ":method"; break;
        case 3: n = ":method"; break;
        case 4: n = ":path"; break;
        case 5: n = ":path"; break;
        case 6: n = ":scheme"; break;
        case 7: n = ":scheme"; break;
        case 8: n = ":status"; break;
        case 15: n = "accept-encoding"; break;
        case 31: n = "content-type"; break;
        case 54: n = "user-agent"; break;
        default: break;
    }
    if (!n) return (MakoString){NULL, 0};
    return mako_str_from_cstr(n);
}

/* Static table index → value when the entry has a fixed value; else empty. */
static inline MakoString mako_hpack_static_value(int64_t index) {
    if (index >= 62) return mako_hpack_dyn_value_at(index);
    const char *v = NULL;
    switch (index) {
        case 2: v = "GET"; break;
        case 3: v = "POST"; break;
        case 4: v = "/"; break;
        case 5: v = "/index.html"; break;
        case 6: v = "http"; break;
        case 7: v = "https"; break;
        case 8: v = "200"; break;
        default: break;
    }
    if (!v) return (MakoString){NULL, 0};
    return mako_str_from_cstr(v);
}

/* Decode a full HPACK header block (indexed + literal-new-name only) into a
 * process-global table. For use after http2_header_block merge. Limits: no
 * Huffman, no name-indexed literals, max MAKO_HPACK_DECODE_MAX fields. */
#define MAKO_HPACK_DECODE_MAX 16
static char *mako_hpack_dec_names[MAKO_HPACK_DECODE_MAX];
static size_t mako_hpack_dec_nlen[MAKO_HPACK_DECODE_MAX];
static char *mako_hpack_dec_vals[MAKO_HPACK_DECODE_MAX];
static size_t mako_hpack_dec_vlen[MAKO_HPACK_DECODE_MAX];
static int mako_hpack_dec_count = 0;

static inline void mako_hpack_decode_clear(void) {
    for (int i = 0; i < mako_hpack_dec_count; i++) {
        free(mako_hpack_dec_names[i]);
        free(mako_hpack_dec_vals[i]);
        mako_hpack_dec_names[i] = NULL;
        mako_hpack_dec_vals[i] = NULL;
    }
    mako_hpack_dec_count = 0;
}

static inline int mako_hpack_decode_push(MakoString name, MakoString value) {
    if (mako_hpack_dec_count >= MAKO_HPACK_DECODE_MAX) return 0;
    size_t nl = name.data ? name.len : 0;
    size_t vl = value.data ? value.len : 0;
    char *nn = (char *)malloc(nl + 1);
    char *vv = (char *)malloc(vl + 1);
    if (!nn || !vv) { free(nn); free(vv); return 0; }
    if (nl && name.data) memcpy(nn, name.data, nl);
    nn[nl] = 0;
    if (vl && value.data) memcpy(vv, value.data, vl);
    vv[vl] = 0;
    int i = mako_hpack_dec_count++;
    mako_hpack_dec_names[i] = nn;
    mako_hpack_dec_nlen[i] = nl;
    mako_hpack_dec_vals[i] = vv;
    mako_hpack_dec_vlen[i] = vl;
    return 1;
}

/* Returns field count, or -1 on malformed block. */
static inline int64_t mako_hpack_decode_block(MakoString block) {
    mako_hpack_decode_clear();
    if (!block.data) return 0;
    const unsigned char *p = (const unsigned char *)block.data;
    size_t n = block.len;
    size_t off = 0;
    while (off < n) {
        unsigned char b = p[off];
        if ((b & 0x80) != 0) {
            /* Indexed Header Field */
            int64_t idx = (int64_t)(b & 0x7f);
            off += 1;
            if (idx < 1) { mako_hpack_decode_clear(); return -1; }
            MakoString nm = mako_hpack_static_name(idx);
            MakoString vl = mako_hpack_static_value(idx);
            if (!nm.data && idx < 62) { free(vl.data); mako_hpack_decode_clear(); return -1; }
            if (!mako_hpack_decode_push(nm, vl)) {
                free(nm.data); free(vl.data);
                mako_hpack_decode_clear();
                return -1;
            }
            free(nm.data);
            free(vl.data);
        } else if (b == 0x00) {
            /* Literal without Indexing — New Name */
            size_t start = off;
            off += 1;
            size_t ns = 0, nl = 0, vs = 0, vl = 0;
            if (!mako_hpack_parse_string(p, n, &off, &ns, &nl)) {
                mako_hpack_decode_clear();
                return -1;
            }
            if (!mako_hpack_parse_string(p, n, &off, &vs, &vl)) {
                mako_hpack_decode_clear();
                return -1;
            }
            (void)start;
            MakoString nm = mako_str_slice(block, (int64_t)ns, (int64_t)(ns + nl));
            MakoString vv = mako_str_slice(block, (int64_t)vs, (int64_t)(vs + vl));
            if (!mako_hpack_decode_push(nm, vv)) {
                free(nm.data); free(vv.data);
                mako_hpack_decode_clear();
                return -1;
            }
            free(nm.data);
            free(vv.data);
        } else {
            /* Other forms (name-indexed literal, table size update) unsupported */
            mako_hpack_decode_clear();
            return -1;
        }
    }
    return (int64_t)mako_hpack_dec_count;
}

static inline int64_t mako_hpack_decoded_count(void) {
    return (int64_t)mako_hpack_dec_count;
}

static inline MakoString mako_hpack_decoded_name(int64_t index) {
    if (index < 0 || index >= mako_hpack_dec_count) return (MakoString){NULL, 0};
    return mako_str_from_cstr(mako_hpack_dec_names[index] ? mako_hpack_dec_names[index] : "");
}

static inline MakoString mako_hpack_decoded_value(int64_t index) {
    if (index < 0 || index >= mako_hpack_dec_count) return (MakoString){NULL, 0};
    return mako_str_from_cstr(mako_hpack_dec_vals[index] ? mako_hpack_dec_vals[index] : "");
}

/* Build HTTP/2 frame: type + stream + flags + payload. */
static inline MakoString mako_http2_build_frame(
    int64_t typ, int64_t stream, int64_t flags, MakoString payload
) {
    size_t plen = payload.data ? payload.len : 0;
    if (plen > 0xFFFFFFu) return (MakoString){NULL, 0};
    size_t n = 9 + plen;
    char *d = (char *)malloc(n + 1);
    if (!d) return (MakoString){NULL, 0};
    d[0] = (char)((plen >> 16) & 0xff);
    d[1] = (char)((plen >> 8) & 0xff);
    d[2] = (char)(plen & 0xff);
    d[3] = (char)(typ & 0xff);
    d[4] = (char)(flags & 0xff);
    unsigned int sid = (unsigned int)(stream & 0x7fffffff);
    d[5] = (char)((sid >> 24) & 0xff);
    d[6] = (char)((sid >> 16) & 0xff);
    d[7] = (char)((sid >> 8) & 0xff);
    d[8] = (char)(sid & 0xff);
    if (plen && payload.data) memcpy(d + 9, payload.data, plen);
    d[n] = 0;
    return (MakoString){d, n};
}

/* Build HTTP/2 HEADERS frame (type=1): 9-byte header + HPACK block.
 * flags: END_STREAM=0x1, END_HEADERS=0x4. Limits: no padding/priority. */
static inline MakoString mako_http2_headers_frame(int64_t stream, MakoString block, int64_t flags) {
    return mako_http2_build_frame(1, stream, flags, block);
}

/* Build HTTP/2 DATA frame (type=0). Limits: no padding. */
static inline MakoString mako_http2_data_frame(int64_t stream, MakoString payload, int64_t flags) {
    return mako_http2_build_frame(0, stream, flags, payload);
}

/* Build a complete HTTP/2 response for `stream`: a HEADERS frame carrying
 * `:status` + `content-length`, followed by a DATA frame with the body and
 * END_STREAM. Returns the bytes to write to the connection. Encapsulates the
 * HPACK encode + framing so an accept loop is just: read request → build
 * response → write. */
static inline MakoString mako_http2_response(int64_t stream, int64_t status, MakoString body) {
    /* :status — indexed for the common static-table codes, else literal. */
    MakoString sh;
    switch (status) {
        case 200: sh = mako_hpack_encode_indexed(8); break;
        case 204: sh = mako_hpack_encode_indexed(9); break;
        case 206: sh = mako_hpack_encode_indexed(10); break;
        case 304: sh = mako_hpack_encode_indexed(11); break;
        case 400: sh = mako_hpack_encode_indexed(12); break;
        case 404: sh = mako_hpack_encode_indexed(13); break;
        case 500: sh = mako_hpack_encode_indexed(14); break;
        default: {
            char code[16];
            snprintf(code, sizeof(code), "%lld", (long long)status);
            sh = mako_hpack_encode_literal(mako_str_from_cstr(":status"),
                                           mako_str_from_cstr(code));
        }
    }
    char clen[24];
    snprintf(clen, sizeof(clen), "%zu", body.len);
    MakoString clh = mako_hpack_encode_literal(mako_str_from_cstr("content-length"),
                                               mako_str_from_cstr(clen));
    MakoString block = mako_str_concat(sh, clh);
    free(sh.data);
    free(clh.data);
    MakoString hf = mako_http2_headers_frame(stream, block, 0x4); /* END_HEADERS */
    free(block.data);
    MakoString df = mako_http2_data_frame(stream, body, 0x1);     /* END_STREAM */
    MakoString out = mako_str_concat(hf, df);
    free(hf.data);
    free(df.data);
    return out;
}

/* CONTINUATION (type=9) — header-block fragment. END_HEADERS=0x4. */
static inline MakoString mako_http2_continuation_frame(int64_t stream, MakoString block, int64_t flags) {
    return mako_http2_build_frame(9, stream, flags, block);
}

/* GOAWAY (type=7, stream=0): last_stream_id(4) + error_code(4). No debug data. */
static inline MakoString mako_http2_goaway_frame(int64_t last_stream, int64_t error_code) {
    char pl[8];
    unsigned int sid = (unsigned int)(last_stream & 0x7fffffff);
    unsigned int err = (unsigned int)error_code;
    pl[0] = (char)((sid >> 24) & 0xff);
    pl[1] = (char)((sid >> 16) & 0xff);
    pl[2] = (char)((sid >> 8) & 0xff);
    pl[3] = (char)(sid & 0xff);
    pl[4] = (char)((err >> 24) & 0xff);
    pl[5] = (char)((err >> 16) & 0xff);
    pl[6] = (char)((err >> 8) & 0xff);
    pl[7] = (char)(err & 0xff);
    return mako_http2_build_frame(7, 0, 0, (MakoString){pl, 8});
}

/* PING (type=6, stream=0): 8-byte opaque. flags: ACK=0x1. Pads short payloads with 0. */
static inline MakoString mako_http2_ping_frame(MakoString opaque, int64_t flags) {
    char pl[8];
    memset(pl, 0, 8);
    size_t n = opaque.data && opaque.len > 0 ? opaque.len : 0;
    if (n > 8) n = 8;
    if (n && opaque.data) memcpy(pl, opaque.data, n);
    return mako_http2_build_frame(6, 0, flags & 0xff, (MakoString){pl, 8});
}

/* WINDOW_UPDATE (type=8): 4-byte window size increment (31-bit). */
static inline MakoString mako_http2_window_update_frame(int64_t stream, int64_t increment) {
    char pl[4];
    unsigned int inc = (unsigned int)(increment & 0x7fffffff);
    pl[0] = (char)((inc >> 24) & 0xff);
    pl[1] = (char)((inc >> 16) & 0xff);
    pl[2] = (char)((inc >> 8) & 0xff);
    pl[3] = (char)(inc & 0xff);
    return mako_http2_build_frame(8, stream, 0, (MakoString){pl, 4});
}

/* RST_STREAM (type=3): 4-byte error code. */
static inline MakoString mako_http2_rst_stream_frame(int64_t stream, int64_t error_code) {
    char pl[4];
    unsigned int err = (unsigned int)error_code;
    pl[0] = (char)((err >> 24) & 0xff);
    pl[1] = (char)((err >> 16) & 0xff);
    pl[2] = (char)((err >> 8) & 0xff);
    pl[3] = (char)(err & 0xff);
    return mako_http2_build_frame(3, stream, 0, (MakoString){pl, 4});
}

/* PRIORITY (type=2): exclusive(1bit)+stream_dep(31) + weight(1). weight stored as weight-1. */
static inline MakoString mako_http2_priority_frame(
    int64_t stream, int64_t dep_stream, int64_t weight, int64_t exclusive
) {
    char pl[5];
    unsigned int dep = (unsigned int)(dep_stream & 0x7fffffff);
    if (exclusive) dep |= 0x80000000u;
    pl[0] = (char)((dep >> 24) & 0xff);
    pl[1] = (char)((dep >> 16) & 0xff);
    pl[2] = (char)((dep >> 8) & 0xff);
    pl[3] = (char)(dep & 0xff);
    int64_t w = weight;
    if (w < 1) w = 1;
    if (w > 256) w = 256;
    pl[4] = (char)((w - 1) & 0xff);
    return mako_http2_build_frame(2, stream, 0, (MakoString){pl, 5});
}

/* PUSH_PROMISE (type=5): promised_stream_id(4) + header block. flags: END_HEADERS=0x4. */
static inline MakoString mako_http2_push_promise_frame(
    int64_t stream, int64_t promised_stream, MakoString block, int64_t flags
) {
    size_t blen = block.data ? block.len : 0;
    size_t plen = 4 + blen;
    char *pl = (char *)malloc(plen + 1);
    if (!pl) return (MakoString){NULL, 0};
    unsigned int psid = (unsigned int)(promised_stream & 0x7fffffff);
    pl[0] = (char)((psid >> 24) & 0xff);
    pl[1] = (char)((psid >> 16) & 0xff);
    pl[2] = (char)((psid >> 8) & 0xff);
    pl[3] = (char)(psid & 0xff);
    if (blen && block.data) memcpy(pl + 4, block.data, blen);
    pl[plen] = 0;
    MakoString payload = (MakoString){pl, plen};
    MakoString frame = mako_http2_build_frame(5, stream, flags & 0xff, payload);
    free(pl);
    return frame;
}

/* Concatenate two frames (e.g. HEADERS without END_HEADERS + CONTINUATION). */
static inline MakoString mako_http2_concat_frames(MakoString a, MakoString b) {
    return mako_str_concat(a, b);
}

/* Assemble one stream's header block from HEADERS + CONTINUATION* frames.
 * Walks `buf`, finds first HEADERS for `stream`, concatenates payloads until
 * END_HEADERS (flag 0x4). Empty if missing/truncated/wrong stream. */
static inline MakoString mako_http2_header_block(MakoString buf, int64_t stream) {
    if (!buf.data || stream <= 0) return (MakoString){NULL, 0};
    const unsigned char *p = (const unsigned char *)buf.data;
    size_t n = buf.len;
    size_t pref = sizeof(MAKO_HTTP2_PREFACE) - 1;
    size_t base = 0;
    if (n >= pref && mako_http2_is_preface((const char *)p, n)) {
        base = pref;
        p += pref;
        n -= pref;
    }
    size_t off = 0;
    int started = 0;
    char *acc = NULL;
    size_t acc_len = 0;
    while (off + 9 <= n) {
        int64_t len = -1, typ = -1, flags = -1, sid = -1;
        if (!mako_http2_parse_frame(p + off, n - off, &len, &typ, &flags, &sid)) break;
        if (!started) {
            if (typ == 1 && sid == stream) {
                started = 1;
            } else {
                off += 9 + (size_t)len;
                continue;
            }
        } else {
            if (typ != 9 || sid != stream) {
                free(acc);
                return (MakoString){NULL, 0};
            }
        }
        /* append payload */
        if (len > 0) {
            char *nacc = (char *)realloc(acc, acc_len + (size_t)len + 1);
            if (!nacc) { free(acc); return (MakoString){NULL, 0}; }
            acc = nacc;
            memcpy(acc + acc_len, p + off + 9, (size_t)len);
            acc_len += (size_t)len;
            acc[acc_len] = 0;
        }
        off += 9 + (size_t)len;
        if ((flags & 0x4) != 0) {
            if (!acc) return mako_str_from_cstr("");
            return (MakoString){acc, acc_len};
        }
        (void)base;
    }
    free(acc);
    return (MakoString){NULL, 0};
}

/* Skip to Nth frame (0-based) in a buffer; returns slice starting at that frame. */
static inline MakoString mako_http2_frame_at(MakoString s, int64_t index) {
    if (!s.data || index < 0) return (MakoString){NULL, 0};
    const unsigned char *p = (const unsigned char *)s.data;
    size_t n = s.len;
    size_t pref = sizeof(MAKO_HTTP2_PREFACE) - 1;
    size_t base = 0;
    if (n >= pref && mako_http2_is_preface((const char *)p, n)) {
        base = pref;
        p += pref;
        n -= pref;
    }
    size_t off = 0;
    for (int64_t i = 0; i <= index; i++) {
        if (off + 9 > n) return (MakoString){NULL, 0};
        int64_t len = -1;
        if (!mako_http2_parse_frame(p + off, n - off, &len, NULL, NULL, NULL)) {
            return (MakoString){NULL, 0};
        }
        if (i == index) {
            size_t start = base + off;
            return mako_str_slice(s, (int64_t)start, (int64_t)s.len);
        }
        off += 9 + (size_t)len;
    }
    return (MakoString){NULL, 0};
}

/* Extract frame payload (after 9-byte header). Empty if truncated/invalid. */
static inline MakoString mako_http2_frame_payload(MakoString s) {
    const unsigned char *p = (const unsigned char *)(s.data ? s.data : "");
    size_t n = s.len;
    size_t pref = sizeof(MAKO_HTTP2_PREFACE) - 1;
    if (n >= pref && mako_http2_is_preface((const char *)p, n)) {
        p += pref; n -= pref;
    }
    int64_t len = -1;
    if (!mako_http2_parse_frame(p, n, &len, NULL, NULL, NULL)) return (MakoString){NULL, 0};
    if (len < 0) return (MakoString){NULL, 0};
    /* slice relative to original string: find frame start offset */
    size_t frame_off = (size_t)((const char *)p - (s.data ? s.data : (const char *)p));
    size_t start = frame_off + 9;
    size_t end = start + (size_t)len;
    if (!s.data || end > s.len) return (MakoString){NULL, 0};
    return mako_str_slice(s, (int64_t)start, (int64_t)end);
}

static inline bool mako_http2_is_goaway(MakoString s) {
    return mako_http2_frame_type(s) == 7;
}
static inline bool mako_http2_is_ping(MakoString s) {
    return mako_http2_frame_type(s) == 6;
}
static inline bool mako_http2_is_window_update(MakoString s) {
    return mako_http2_frame_type(s) == 8;
}
static inline bool mako_http2_is_rst_stream(MakoString s) {
    return mako_http2_frame_type(s) == 3;
}
static inline bool mako_http2_is_priority(MakoString s) {
    return mako_http2_frame_type(s) == 2;
}
static inline bool mako_http2_is_push_promise(MakoString s) {
    return mako_http2_frame_type(s) == 5;
}

/* GOAWAY last-stream / error from payload; -1 if not GOAWAY. */
static inline int64_t mako_http2_goaway_last_stream(MakoString s) {
    if (!mako_http2_is_goaway(s)) return -1;
    MakoString pl = mako_http2_frame_payload(s);
    if (!pl.data || pl.len < 8) { free(pl.data); return -1; }
    int64_t v = ((int64_t)(unsigned char)pl.data[0] << 24)
              | ((int64_t)(unsigned char)pl.data[1] << 16)
              | ((int64_t)(unsigned char)pl.data[2] << 8)
              | ((int64_t)(unsigned char)pl.data[3]);
    free(pl.data);
    return v & 0x7fffffff;
}
static inline int64_t mako_http2_goaway_error(MakoString s) {
    if (!mako_http2_is_goaway(s)) return -1;
    MakoString pl = mako_http2_frame_payload(s);
    if (!pl.data || pl.len < 8) { free(pl.data); return -1; }
    int64_t v = ((int64_t)(unsigned char)pl.data[4] << 24)
              | ((int64_t)(unsigned char)pl.data[5] << 16)
              | ((int64_t)(unsigned char)pl.data[6] << 8)
              | ((int64_t)(unsigned char)pl.data[7]);
    free(pl.data);
    return v;
}
static inline int64_t mako_http2_window_update_increment(MakoString s) {
    if (!mako_http2_is_window_update(s)) return -1;
    MakoString pl = mako_http2_frame_payload(s);
    if (!pl.data || pl.len < 4) { free(pl.data); return -1; }
    int64_t v = ((int64_t)(unsigned char)pl.data[0] << 24)
              | ((int64_t)(unsigned char)pl.data[1] << 16)
              | ((int64_t)(unsigned char)pl.data[2] << 8)
              | ((int64_t)(unsigned char)pl.data[3]);
    free(pl.data);
    return v & 0x7fffffff;
}
static inline int64_t mako_http2_rst_stream_error(MakoString s) {
    if (!mako_http2_is_rst_stream(s)) return -1;
    MakoString pl = mako_http2_frame_payload(s);
    if (!pl.data || pl.len < 4) { free(pl.data); return -1; }
    int64_t v = ((int64_t)(unsigned char)pl.data[0] << 24)
              | ((int64_t)(unsigned char)pl.data[1] << 16)
              | ((int64_t)(unsigned char)pl.data[2] << 8)
              | ((int64_t)(unsigned char)pl.data[3]);
    free(pl.data);
    return v;
}
static inline int64_t mako_http2_priority_dep(MakoString s) {
    if (!mako_http2_is_priority(s)) return -1;
    MakoString pl = mako_http2_frame_payload(s);
    if (!pl.data || pl.len < 5) { free(pl.data); return -1; }
    int64_t v = ((int64_t)(unsigned char)pl.data[0] << 24)
              | ((int64_t)(unsigned char)pl.data[1] << 16)
              | ((int64_t)(unsigned char)pl.data[2] << 8)
              | ((int64_t)(unsigned char)pl.data[3]);
    free(pl.data);
    return v & 0x7fffffff;
}
static inline int64_t mako_http2_priority_weight(MakoString s) {
    if (!mako_http2_is_priority(s)) return -1;
    MakoString pl = mako_http2_frame_payload(s);
    if (!pl.data || pl.len < 5) { free(pl.data); return -1; }
    int64_t w = (int64_t)(unsigned char)pl.data[4] + 1;
    free(pl.data);
    return w;
}
static inline int64_t mako_http2_priority_exclusive(MakoString s) {
    if (!mako_http2_is_priority(s)) return -1;
    MakoString pl = mako_http2_frame_payload(s);
    if (!pl.data || pl.len < 5) { free(pl.data); return -1; }
    int64_t ex = ((unsigned char)pl.data[0] & 0x80) ? 1 : 0;
    free(pl.data);
    return ex;
}

static inline int64_t mako_http2_push_promise_stream(MakoString s) {
    if (!mako_http2_is_push_promise(s)) return -1;
    MakoString pl = mako_http2_frame_payload(s);
    if (!pl.data || pl.len < 4) { free(pl.data); return -1; }
    int64_t v = ((int64_t)(unsigned char)pl.data[0] << 24)
              | ((int64_t)(unsigned char)pl.data[1] << 16)
              | ((int64_t)(unsigned char)pl.data[2] << 8)
              | ((int64_t)(unsigned char)pl.data[3]);
    free(pl.data);
    return v & 0x7fffffff;
}

/* HTTP/2 stream state seed (up to 3 stream ids — priority tree seed).
 * States: 0 idle, 1 open, 2 half-closed (local and/or remote), 3 closed.
 * Direction flags track END_STREAM: remote=received, local=sent. */
#define MAKO_H2_STREAM_SLOTS 3
#define MAKO_H2_DEFAULT_WINDOW 65535
static int64_t mako_h2_sids[MAKO_H2_STREAM_SLOTS];
static int64_t mako_h2_states[MAKO_H2_STREAM_SLOTS];
static int mako_h2_es_remote[MAKO_H2_STREAM_SLOTS];
static int mako_h2_es_local[MAKO_H2_STREAM_SLOTS];
static int64_t mako_h2_stream_windows[MAKO_H2_STREAM_SLOTS];
static int64_t mako_h2_pri_dep[MAKO_H2_STREAM_SLOTS];
static int64_t mako_h2_pri_weight[MAKO_H2_STREAM_SLOTS];
static int64_t mako_h2_pri_excl[MAKO_H2_STREAM_SLOTS];
static int64_t mako_h2_last_sid = 0;

/* Connection-level flow-control window (outbound DATA budget seed). */
static int64_t mako_h2_conn_window = MAKO_H2_DEFAULT_WINDOW;

/* CONTINUATION header-block assembly (Partial): buffer until END_HEADERS. */
#define MAKO_H2_HDR_MAX 4096
static int mako_h2_hdr_assembling = 0;
static int64_t mako_h2_hdr_stream = 0;
static char mako_h2_hdr_acc[MAKO_H2_HDR_MAX];
static size_t mako_h2_hdr_acc_len = 0;
/* Last completed header block from conn_recv. */
static int64_t mako_h2_hdr_done_stream = 0;
static char mako_h2_hdr_done[MAKO_H2_HDR_MAX];
static size_t mako_h2_hdr_done_len = 0;

static inline void mako_http2_hdr_assembly_reset(void) {
    mako_h2_hdr_assembling = 0;
    mako_h2_hdr_stream = 0;
    mako_h2_hdr_acc_len = 0;
    mako_h2_hdr_done_stream = 0;
    mako_h2_hdr_done_len = 0;
}

static inline int mako_http2_hdr_append(const unsigned char *p, size_t n) {
    if (mako_h2_hdr_acc_len + n > MAKO_H2_HDR_MAX) return 0;
    if (n) memcpy(mako_h2_hdr_acc + mako_h2_hdr_acc_len, p, n);
    mako_h2_hdr_acc_len += n;
    return 1;
}

static inline void mako_http2_hdr_finish(void) {
    size_t n = mako_h2_hdr_acc_len;
    if (n > MAKO_H2_HDR_MAX) n = MAKO_H2_HDR_MAX;
    memcpy(mako_h2_hdr_done, mako_h2_hdr_acc, n);
    mako_h2_hdr_done_len = n;
    mako_h2_hdr_done_stream = mako_h2_hdr_stream;
    mako_h2_hdr_assembling = 0;
    mako_h2_hdr_stream = 0;
    mako_h2_hdr_acc_len = 0;
}

/* Abort incomplete CONTINUATION assembly (e.g. RST_STREAM on that stream). */
static inline void mako_http2_hdr_abort(void) {
    mako_h2_hdr_assembling = 0;
    mako_h2_hdr_stream = 0;
    mako_h2_hdr_acc_len = 0;
}

static inline void mako_http2_stream_reset(void) {
    for (int i = 0; i < MAKO_H2_STREAM_SLOTS; i++) {
        mako_h2_sids[i] = 0;
        mako_h2_states[i] = 0;
        mako_h2_es_remote[i] = 0;
        mako_h2_es_local[i] = 0;
        mako_h2_stream_windows[i] = MAKO_H2_DEFAULT_WINDOW;
        mako_h2_pri_dep[i] = 0;
        mako_h2_pri_weight[i] = 16; /* RFC default weight */
        mako_h2_pri_excl[i] = 0;
    }
    mako_h2_last_sid = 0;
    mako_h2_conn_window = MAKO_H2_DEFAULT_WINDOW;
    mako_http2_hdr_assembly_reset();
}

/* Connection-level state seed (process-global).
 * Flags: preface_rx, settings_*, closing (after GOAWAY). */
static int mako_h2_conn_preface_rx = 0;
static int mako_h2_conn_settings_rx = 0;
static int mako_h2_conn_settings_ack_rx = 0;
static int mako_h2_conn_settings_tx = 0;
static int mako_h2_conn_settings_ack_tx = 0;
static int mako_h2_conn_closing = 0;
/* Last-stream-id from peer GOAWAY; -1 if none. New streams with id > last rejected. */
static int64_t mako_h2_goaway_last = -1;
/* Set when peer sends non-ACK SETTINGS — ACK response needed. */
static int mako_h2_conn_settings_ack_needed = 0;
/* SETTINGS_MAX_CONCURRENT_STREAMS (0x3). 0 = unlimited (default until peer sets). */
static int64_t mako_h2_max_concurrent = 0;
/* Pending PING ACK: opaque[8] when peer sent non-ACK PING. */
static int mako_h2_ping_ack_needed = 0;
static unsigned char mako_h2_ping_opaque[8];
/* Stream-id parity for newly opened HEADERS: 0=client (odd), 1=server (even). */
static int mako_h2_is_server = 0;

static inline void mako_http2_conn_reset(void) {
    mako_h2_conn_preface_rx = 0;
    mako_h2_conn_settings_rx = 0;
    mako_h2_conn_settings_ack_rx = 0;
    mako_h2_conn_settings_tx = 0;
    mako_h2_conn_settings_ack_tx = 0;
    mako_h2_conn_closing = 0;
    mako_h2_goaway_last = -1;
    mako_h2_conn_settings_ack_needed = 0;
    mako_h2_max_concurrent = 0;
    mako_h2_ping_ack_needed = 0;
    memset(mako_h2_ping_opaque, 0, 8);
    mako_h2_is_server = 0;
    mako_http2_stream_reset();
}

/* ------------------------------------------------------------------------- */
/* Per-connection HTTP/2 state.                                                */
/*                                                                             */
/* The frame processor above works on process-global state (one connection).  */
/* A `MakoHttp2Conn` handle snapshots that whole block so a server/proxy can   */
/* juggle several connections on one thread: `http2_conn_use(c)` saves the     */
/* active connection's state and loads `c` before you pump its bytes. Leaving  */
/* the handles unused keeps the original single-connection behaviour exactly.  */
/* ------------------------------------------------------------------------- */

typedef struct {
    int64_t sids[MAKO_H2_STREAM_SLOTS];
    int64_t states[MAKO_H2_STREAM_SLOTS];
    int es_remote[MAKO_H2_STREAM_SLOTS];
    int es_local[MAKO_H2_STREAM_SLOTS];
    int64_t stream_windows[MAKO_H2_STREAM_SLOTS];
    int64_t pri_dep[MAKO_H2_STREAM_SLOTS];
    int64_t pri_weight[MAKO_H2_STREAM_SLOTS];
    int64_t pri_excl[MAKO_H2_STREAM_SLOTS];
    int64_t last_sid;
    int64_t conn_window;
    int hdr_assembling;
    int64_t hdr_stream;
    char hdr_acc[MAKO_H2_HDR_MAX];
    size_t hdr_acc_len;
    int64_t hdr_done_stream;
    char hdr_done[MAKO_H2_HDR_MAX];
    size_t hdr_done_len;
    int conn_preface_rx, conn_settings_rx, conn_settings_ack_rx;
    int conn_settings_tx, conn_settings_ack_tx, conn_closing;
    int64_t goaway_last;
    int conn_settings_ack_needed;
    int64_t max_concurrent;
    int ping_ack_needed;
    unsigned char ping_opaque[8];
    int is_server;
} MakoHttp2Conn;

/* The handle whose state is currently loaded into the globals (NULL = the
 * implicit default connection). */
static MakoHttp2Conn *mako_h2_active = NULL;

/* Copy the live globals into `c`. */
static inline void mako_h2_conn_save(MakoHttp2Conn *c) {
    memcpy(c->sids, mako_h2_sids, sizeof(mako_h2_sids));
    memcpy(c->states, mako_h2_states, sizeof(mako_h2_states));
    memcpy(c->es_remote, mako_h2_es_remote, sizeof(mako_h2_es_remote));
    memcpy(c->es_local, mako_h2_es_local, sizeof(mako_h2_es_local));
    memcpy(c->stream_windows, mako_h2_stream_windows, sizeof(mako_h2_stream_windows));
    memcpy(c->pri_dep, mako_h2_pri_dep, sizeof(mako_h2_pri_dep));
    memcpy(c->pri_weight, mako_h2_pri_weight, sizeof(mako_h2_pri_weight));
    memcpy(c->pri_excl, mako_h2_pri_excl, sizeof(mako_h2_pri_excl));
    c->last_sid = mako_h2_last_sid;
    c->conn_window = mako_h2_conn_window;
    c->hdr_assembling = mako_h2_hdr_assembling;
    c->hdr_stream = mako_h2_hdr_stream;
    memcpy(c->hdr_acc, mako_h2_hdr_acc, sizeof(mako_h2_hdr_acc));
    c->hdr_acc_len = mako_h2_hdr_acc_len;
    c->hdr_done_stream = mako_h2_hdr_done_stream;
    memcpy(c->hdr_done, mako_h2_hdr_done, sizeof(mako_h2_hdr_done));
    c->hdr_done_len = mako_h2_hdr_done_len;
    c->conn_preface_rx = mako_h2_conn_preface_rx;
    c->conn_settings_rx = mako_h2_conn_settings_rx;
    c->conn_settings_ack_rx = mako_h2_conn_settings_ack_rx;
    c->conn_settings_tx = mako_h2_conn_settings_tx;
    c->conn_settings_ack_tx = mako_h2_conn_settings_ack_tx;
    c->conn_closing = mako_h2_conn_closing;
    c->goaway_last = mako_h2_goaway_last;
    c->conn_settings_ack_needed = mako_h2_conn_settings_ack_needed;
    c->max_concurrent = mako_h2_max_concurrent;
    c->ping_ack_needed = mako_h2_ping_ack_needed;
    memcpy(c->ping_opaque, mako_h2_ping_opaque, sizeof(mako_h2_ping_opaque));
    c->is_server = mako_h2_is_server;
}

/* Load `c` into the live globals. */
static inline void mako_h2_conn_load(const MakoHttp2Conn *c) {
    memcpy(mako_h2_sids, c->sids, sizeof(mako_h2_sids));
    memcpy(mako_h2_states, c->states, sizeof(mako_h2_states));
    memcpy(mako_h2_es_remote, c->es_remote, sizeof(mako_h2_es_remote));
    memcpy(mako_h2_es_local, c->es_local, sizeof(mako_h2_es_local));
    memcpy(mako_h2_stream_windows, c->stream_windows, sizeof(mako_h2_stream_windows));
    memcpy(mako_h2_pri_dep, c->pri_dep, sizeof(mako_h2_pri_dep));
    memcpy(mako_h2_pri_weight, c->pri_weight, sizeof(mako_h2_pri_weight));
    memcpy(mako_h2_pri_excl, c->pri_excl, sizeof(mako_h2_pri_excl));
    mako_h2_last_sid = c->last_sid;
    mako_h2_conn_window = c->conn_window;
    mako_h2_hdr_assembling = c->hdr_assembling;
    mako_h2_hdr_stream = c->hdr_stream;
    memcpy(mako_h2_hdr_acc, c->hdr_acc, sizeof(mako_h2_hdr_acc));
    mako_h2_hdr_acc_len = c->hdr_acc_len;
    mako_h2_hdr_done_stream = c->hdr_done_stream;
    memcpy(mako_h2_hdr_done, c->hdr_done, sizeof(mako_h2_hdr_done));
    mako_h2_hdr_done_len = c->hdr_done_len;
    mako_h2_conn_preface_rx = c->conn_preface_rx;
    mako_h2_conn_settings_rx = c->conn_settings_rx;
    mako_h2_conn_settings_ack_rx = c->conn_settings_ack_rx;
    mako_h2_conn_settings_tx = c->conn_settings_tx;
    mako_h2_conn_settings_ack_tx = c->conn_settings_ack_tx;
    mako_h2_conn_closing = c->conn_closing;
    mako_h2_goaway_last = c->goaway_last;
    mako_h2_conn_settings_ack_needed = c->conn_settings_ack_needed;
    mako_h2_max_concurrent = c->max_concurrent;
    mako_h2_ping_ack_needed = c->ping_ack_needed;
    memcpy(mako_h2_ping_opaque, c->ping_opaque, sizeof(mako_h2_ping_opaque));
    mako_h2_is_server = c->is_server;
}

/* Allocate a fresh connection in the default (reset) state. */
static inline MakoHttp2Conn *mako_http2_conn_new(void) {
    MakoHttp2Conn *c = (MakoHttp2Conn *)malloc(sizeof(MakoHttp2Conn));
    if (!c) return NULL;
    /* Derive the default field values from conn_reset without disturbing the
     * currently-active connection: stash globals, reset, snapshot, restore. */
    MakoHttp2Conn tmp;
    mako_h2_conn_save(&tmp);
    mako_http2_conn_reset();
    mako_h2_conn_save(c);
    mako_h2_conn_load(&tmp);
    return c;
}

/* Make `conn` the active connection (saving whichever was active). */
static inline int64_t mako_http2_conn_use(MakoHttp2Conn *conn) {
    if (!conn) return -1;
    if (mako_h2_active) mako_h2_conn_save(mako_h2_active);
    mako_h2_conn_load(conn);
    mako_h2_active = conn;
    return 0;
}

/* Free a connection handle (deactivating it if it was current). */
static inline void mako_http2_conn_free(MakoHttp2Conn *conn) {
    if (!conn) return;
    if (mako_h2_active == conn) mako_h2_active = NULL;
    free(conn);
}

static inline int64_t mako_http2_conn_set_server(int64_t is_server) {
    mako_h2_is_server = is_server ? 1 : 0;
    return mako_h2_is_server;
}

static inline int64_t mako_http2_conn_is_server(void) {
    return mako_h2_is_server ? 1 : 0;
}

static inline int64_t mako_http2_conn_preface_received(void) {
    return mako_h2_conn_preface_rx ? 1 : 0;
}
static inline int64_t mako_http2_conn_settings_exchanged(void) {
    return (mako_h2_conn_settings_rx && mako_h2_conn_settings_ack_rx
            && mako_h2_conn_settings_tx && mako_h2_conn_settings_ack_tx)
               ? 1
               : 0;
}
static inline int64_t mako_http2_conn_closing(void) {
    return mako_h2_conn_closing ? 1 : 0;
}
static inline int64_t mako_http2_conn_goaway_last(void) {
    return mako_h2_goaway_last;
}
static inline int64_t mako_http2_conn_max_concurrent(void) {
    return mako_h2_max_concurrent;
}
/* Count non-idle, non-closed streams (open or half-closed). */
static inline int64_t mako_http2_conn_active_streams(void) {
    int64_t n = 0;
    for (int i = 0; i < MAKO_H2_STREAM_SLOTS; i++) {
        if (mako_h2_states[i] == 1 || mako_h2_states[i] == 2) n++;
    }
    return n;
}

/* Flow-control window seed (Partial). Default 65535.
 * stream=0 → connection window. blocked when window == 0. */
static inline int64_t mako_http2_window_of(int64_t stream) {
    if (stream == 0) return mako_h2_conn_window;
    for (int i = 0; i < MAKO_H2_STREAM_SLOTS; i++) {
        if (mako_h2_states[i] != 0 && mako_h2_sids[i] == stream)
            return mako_h2_stream_windows[i];
    }
    return -1; /* unknown stream */
}
static inline int64_t mako_http2_window_conn(void) {
    return mako_h2_conn_window;
}
static inline int64_t mako_http2_window_blocked(int64_t stream) {
    int64_t w = mako_http2_window_of(stream);
    if (w < 0) return -1;
    return w == 0 ? 1 : 0;
}
/* Consume nbytes from stream + connection windows (outbound DATA). Returns 0 ok, -1 blocked/error. */
static inline int64_t mako_http2_window_consume(int64_t stream, int64_t nbytes) {
    if (nbytes < 0) return -1;
    if (stream == 0) {
        if (mako_h2_conn_window < nbytes) return -1;
        mako_h2_conn_window -= nbytes;
        return 0;
    }
    int i = -1;
    for (int j = 0; j < MAKO_H2_STREAM_SLOTS; j++) {
        if (mako_h2_states[j] != 0 && mako_h2_sids[j] == stream) { i = j; break; }
    }
    if (i < 0) return -1;
    if (mako_h2_stream_windows[i] < nbytes || mako_h2_conn_window < nbytes) return -1;
    mako_h2_stream_windows[i] -= nbytes;
    mako_h2_conn_window -= nbytes;
    return 0;
}
/* Apply WINDOW_UPDATE increment (stream 0 = connection). Returns new window or -1. */
static inline int64_t mako_http2_window_increment(int64_t stream, int64_t inc) {
    if (inc <= 0 || inc > 0x7fffffff) return -1;
    if (stream == 0) {
        int64_t n = mako_h2_conn_window + inc;
        if (n > 0x7fffffff) return -1;
        mako_h2_conn_window = n;
        return mako_h2_conn_window;
    }
    int i = -1;
    for (int j = 0; j < MAKO_H2_STREAM_SLOTS; j++) {
        if (mako_h2_states[j] != 0 && mako_h2_sids[j] == stream) { i = j; break; }
    }
    if (i < 0) {
        /* allocate idle slot for window tracking */
        for (int j = 0; j < MAKO_H2_STREAM_SLOTS; j++) {
            if (mako_h2_states[j] == 0) {
                mako_h2_sids[j] = stream;
                mako_h2_states[j] = 1;
                mako_h2_es_remote[j] = 0;
                mako_h2_es_local[j] = 0;
                mako_h2_stream_windows[j] = MAKO_H2_DEFAULT_WINDOW;
                mako_h2_pri_dep[j] = 0;
                mako_h2_pri_weight[j] = 16;
                mako_h2_pri_excl[j] = 0;
                i = j;
                break;
            }
        }
        if (i < 0) return -1;
    }
    int64_t n = mako_h2_stream_windows[i] + inc;
    if (n > 0x7fffffff) return -1;
    mako_h2_stream_windows[i] = n;
    return n;
}

/* Forward: apply one frame to stream SM (defined below). */
static inline int64_t mako_http2_stream_apply(MakoString frame);
/* Forward: PRIORITY exclusive reparent (defined below). */
static inline int64_t mako_http2_priority_apply(MakoString frame);
static inline int mako_http2_stream_find(int64_t sid);
static inline int64_t mako_http2_conn_active_streams(void);

/* Feed inbound bytes (client preface and/or frames). Returns 0 ok, -1 error.
 * After GOAWAY: closing=1, last-stream stored. PING/GOAWAY always ok.
 * Frames on stream_id <= last-stream allowed; HEADERS opening stream > last → -1.
 * WINDOW_UPDATE increments the matching window.
 * HEADERS/DATA/RST_STREAM on stream>0 update the stream state machine.
 * HEADERS without END_HEADERS starts CONTINUATION assembly; only CONTINUATION
 * for that stream accepted until END_HEADERS; assembled block queryable. */
static inline int64_t mako_http2_conn_recv(MakoString s) {
    if (!s.data || s.len == 0) return -1;
    const unsigned char *base = (const unsigned char *)s.data;
    const unsigned char *p = base;
    size_t n = s.len;
    size_t pref = sizeof(MAKO_HTTP2_PREFACE) - 1;
    if (!mako_h2_conn_preface_rx && n >= pref && mako_http2_is_preface((const char *)p, n)) {
        mako_h2_conn_preface_rx = 1;
        p += pref;
        n -= pref;
    }
    size_t off = 0;
    while (off + 9 <= n) {
        int64_t len = -1, typ = -1, flags = -1, stream = -1;
        if (!mako_http2_parse_frame(p + off, n - off, &len, &typ, &flags, &stream)) break;
        if (mako_h2_conn_closing) {
            if (typ == 7 || typ == 6) {
                /* GOAWAY / PING ok while closing */
            } else if (stream > 0 && mako_h2_goaway_last >= 0 && stream <= mako_h2_goaway_last) {
                /* existing streams <= last-stream may continue */
            } else {
                return -1; /* new streams / conn frames after GOAWAY */
            }
        }
        /* While assembling headers: CONTINUATION for that stream, or RST aborts. */
        if (mako_h2_hdr_assembling) {
            if (typ == 3 && stream == mako_h2_hdr_stream) {
                /* RST_STREAM on assembling stream → drop incomplete block */
                size_t abs = (size_t)(p - base) + off;
                MakoString frame = mako_str_slice(s, (int64_t)abs, (int64_t)(abs + 9 + (size_t)len));
                int64_t st = mako_http2_stream_apply(frame);
                free(frame.data);
                mako_http2_hdr_abort();
                if (st < 0) return -1;
                off += 9 + (size_t)len;
                continue;
            }
            if (typ != 9 || stream != mako_h2_hdr_stream) return -1;
            if (len > 0 && !mako_http2_hdr_append(p + off + 9, (size_t)len)) return -1;
            if ((flags & 0x4) != 0) mako_http2_hdr_finish();
            off += 9 + (size_t)len;
            continue;
        }
        if (typ == 4 && stream == 0) {
            if ((flags & 0x1) != 0) {
                mako_h2_conn_settings_ack_rx = 1;
                mako_h2_conn_settings_ack_needed = 0;
            } else {
                mako_h2_conn_settings_rx = 1;
                mako_h2_conn_settings_ack_needed = 1; /* peer SETTINGS → ACK owed */
                /* Parse SETTINGS_MAX_CONCURRENT_STREAMS (id=0x3). */
                size_t so = off + 9;
                size_t send = so + (size_t)len;
                while (so + 6 <= send) {
                    int64_t sid = ((int64_t)p[so] << 8) | (int64_t)p[so + 1];
                    int64_t val = ((int64_t)p[so + 2] << 24) | ((int64_t)p[so + 3] << 16)
                                | ((int64_t)p[so + 4] << 8) | (int64_t)p[so + 5];
                    val &= 0xffffffff;
                    if (sid == 0x3) mako_h2_max_concurrent = val;
                    so += 6;
                }
            }
        }
        if (typ == 7) {
            /* GOAWAY → closing; capture last-stream-id from payload */
            mako_h2_conn_closing = 1;
            if (len >= 4) {
                int64_t ls = ((int64_t)p[off + 9] << 24)
                           | ((int64_t)p[off + 10] << 16)
                           | ((int64_t)p[off + 11] << 8)
                           | ((int64_t)p[off + 12]);
                mako_h2_goaway_last = ls & 0x7fffffff;
            } else {
                mako_h2_goaway_last = 0;
            }
        }
        /* Non-ACK PING → store opaque for auto ACK in conn_pump. */
        if (typ == 6 && stream == 0 && (flags & 0x1) == 0 && len >= 8) {
            memcpy(mako_h2_ping_opaque, p + off + 9, 8);
            mako_h2_ping_ack_needed = 1;
        }
        if (typ == 8 && len >= 4) {
            int64_t inc = ((int64_t)p[off + 9] << 24)
                        | ((int64_t)p[off + 10] << 16)
                        | ((int64_t)p[off + 11] << 8)
                        | ((int64_t)p[off + 12]);
            inc &= 0x7fffffff;
            if (mako_http2_window_increment(stream, inc) < 0) return -1;
        }
        /* HEADERS: start or finish header-block assembly. */
        if (typ == 1 && stream > 0) {
            /* Reject HEADERS on already-closed stream. */
            {
                int ei = mako_http2_stream_find(stream);
                if (ei >= 0 && mako_h2_states[ei] == 3) return -1;
            }
            /* Reject new streams with id > GOAWAY last-stream-id. */
            if (mako_h2_conn_closing && mako_h2_goaway_last >= 0
                && stream > mako_h2_goaway_last) {
                return -1;
            }
            /* Enforce SETTINGS_MAX_CONCURRENT_STREAMS on newly opened streams. */
            if (mako_h2_max_concurrent > 0 && mako_http2_stream_find(stream) < 0) {
                if (mako_http2_conn_active_streams() >= mako_h2_max_concurrent) return -1;
            }
            /* Stream-id parity (RFC 7540 §5.1.1): a *received* HEADERS that opens a
             * new stream is client-initiated, so its id must be odd. (Server push,
             * which would use even ids, is not supported.) This holds whether or not
             * `is_server` was set, so a server reads client requests either way. */
            if (mako_http2_stream_find(stream) < 0) {
                if ((stream & 1) == 0) return -1; /* new stream must be client-odd */
            }
            size_t abs = (size_t)(p - base) + off;
            MakoString frame = mako_str_slice(s, (int64_t)abs, (int64_t)(abs + 9 + (size_t)len));
            int64_t st = mako_http2_stream_apply(frame);
            free(frame.data);
            if (st < 0) return -1;
            mako_h2_hdr_assembling = 1;
            mako_h2_hdr_stream = stream;
            mako_h2_hdr_acc_len = 0;
            if (len > 0 && !mako_http2_hdr_append(p + off + 9, (size_t)len)) return -1;
            if ((flags & 0x4) != 0) mako_http2_hdr_finish();
            off += 9 + (size_t)len;
            continue;
        }
        /* Orphan CONTINUATION without open assembly → error. */
        if (typ == 9) return -1;
        /* PRIORITY (type=2): exclusive reparent via priority_apply. */
        if (typ == 2 && stream > 0) {
            size_t abs = (size_t)(p - base) + off;
            MakoString frame = mako_str_slice(s, (int64_t)abs, (int64_t)(abs + 9 + (size_t)len));
            int64_t pr = mako_http2_priority_apply(frame);
            free(frame.data);
            if (pr < 0) return -1;
            off += 9 + (size_t)len;
            continue;
        }
        /* Stream SM: DATA(0), RST_STREAM(3) */
        if (stream > 0 && (typ == 0 || typ == 3)) {
            /* Reject DATA on idle stream (no prior HEADERS). */
            if (typ == 0 && mako_http2_stream_find(stream) < 0) return -1;
            size_t abs = (size_t)(p - base) + off;
            MakoString frame = mako_str_slice(s, (int64_t)abs, (int64_t)(abs + 9 + (size_t)len));
            /* Inbound DATA decreases stream + connection windows (flow-control seed). */
            if (typ == 0 && len > 0) {
                if (mako_http2_window_consume(stream, len) < 0) {
                    free(frame.data);
                    return -1;
                }
            }
            int64_t st = mako_http2_stream_apply(frame);
            free(frame.data);
            if (st < 0) return -1;
        }
        off += 9 + (size_t)len;
    }
    return 0;
}

/* Last header block completed by conn_recv for `stream` (empty if none/mismatch). */
static inline MakoString mako_http2_conn_header_block(int64_t stream) {
    if (stream <= 0 || stream != mako_h2_hdr_done_stream)
        return (MakoString){NULL, 0};
    if (mako_h2_hdr_done_len == 0) return mako_str_from_cstr("");
    char *d = (char *)malloc(mako_h2_hdr_done_len + 1);
    if (!d) return (MakoString){NULL, 0};
    memcpy(d, mako_h2_hdr_done, mako_h2_hdr_done_len);
    d[mako_h2_hdr_done_len] = 0;
    return (MakoString){d, mako_h2_hdr_done_len};
}
static inline int64_t mako_http2_conn_header_stream(void) {
    return mako_h2_hdr_done_stream;
}
static inline int64_t mako_http2_conn_header_assembling(void) {
    return mako_h2_hdr_assembling ? 1 : 0;
}

/* Record that we sent server/client SETTINGS bootstrap. */
static inline int64_t mako_http2_conn_send_settings(void) {
    mako_h2_conn_settings_tx = 1;
    return 0;
}
static inline int64_t mako_http2_conn_send_settings_ack(void) {
    mako_h2_conn_settings_ack_tx = 1;
    mako_h2_conn_settings_ack_needed = 0;
    return 0;
}
/* True if peer sent non-ACK SETTINGS and we have not yet ACKed. */
static inline int64_t mako_http2_conn_settings_ack_needed(void) {
    return mako_h2_conn_settings_ack_needed ? 1 : 0;
}
/* Build SETTINGS ACK frame and clear ack-needed (auto-response helper). */
static inline MakoString mako_http2_conn_auto_settings_ack(void) {
    mako_h2_conn_settings_ack_tx = 1;
    mako_h2_conn_settings_ack_needed = 0;
    return mako_http2_settings_ack();
}

/* Pump: conn_recv, then auto-emit SETTINGS ACK and/or PING ACK if owed.
 * Returns concatenated outbound frames (possibly empty), or empty on recv error. */
static inline MakoString mako_http2_conn_pump(MakoString buf) {
    if (mako_http2_conn_recv(buf) < 0) return mako_str_from_cstr("");
    MakoString out = mako_str_from_cstr("");
    if (mako_h2_conn_settings_ack_needed) {
        MakoString ack = mako_http2_conn_auto_settings_ack();
        MakoString next = mako_http2_concat_frames(out, ack);
        free(out.data);
        free(ack.data);
        out = next;
    }
    if (mako_h2_ping_ack_needed) {
        MakoString opaque = {(char *)mako_h2_ping_opaque, 8};
        MakoString ping_ack = mako_http2_ping_frame(opaque, 1);
        mako_h2_ping_ack_needed = 0;
        MakoString next = mako_http2_concat_frames(out, ping_ack);
        free(out.data);
        free(ping_ack.data);
        out = next;
    }
    return out;
}

/* Record outbound GOAWAY — connection enters closing. */
static inline int64_t mako_http2_conn_send_goaway(void) {
    mako_h2_conn_closing = 1;
    return 0;
}

static inline int mako_http2_stream_find(int64_t sid) {
    for (int i = 0; i < MAKO_H2_STREAM_SLOTS; i++) {
        if (mako_h2_states[i] != 0 && mako_h2_sids[i] == sid) return i;
    }
    return -1;
}

static inline int mako_http2_stream_alloc(int64_t sid) {
    for (int i = 0; i < MAKO_H2_STREAM_SLOTS; i++) {
        if (mako_h2_states[i] == 0) {
            mako_h2_sids[i] = sid;
            mako_h2_states[i] = 1; /* open */
            mako_h2_es_remote[i] = 0;
            mako_h2_es_local[i] = 0;
            mako_h2_stream_windows[i] = MAKO_H2_DEFAULT_WINDOW;
            mako_h2_pri_dep[i] = 0;
            mako_h2_pri_weight[i] = 16;
            mako_h2_pri_excl[i] = 0;
            return i;
        }
    }
    return -1;
}

/* Apply PRIORITY frame into stream priority table (dep/weight/exclusive).
 * Exclusive (RFC 7540 §5.3.1): former children of `dep` are reparented under
 * this stream. Allocates a stream slot if needed. Returns 0 ok, -1 error. */
static inline int64_t mako_http2_priority_apply(MakoString frame) {
    if (!mako_http2_is_priority(frame)) return -1;
    int64_t sid = mako_http2_frame_stream(frame);
    if (sid <= 0) return -1;
    int64_t dep = mako_http2_priority_dep(frame);
    int64_t w = mako_http2_priority_weight(frame);
    int64_t ex = mako_http2_priority_exclusive(frame);
    if (dep < 0 || w < 0 || ex < 0) return -1;
    if (dep == sid) return -1; /* self-dependency */
    int i = mako_http2_stream_find(sid);
    if (i < 0) {
        i = mako_http2_stream_alloc(sid);
        if (i < 0) return -1;
    }
    if (ex) {
        for (int j = 0; j < MAKO_H2_STREAM_SLOTS; j++) {
            if (mako_h2_states[j] == 0) continue;
            if (mako_h2_sids[j] == sid) continue;
            if (mako_h2_pri_dep[j] == dep) mako_h2_pri_dep[j] = sid;
        }
    }
    mako_h2_pri_dep[i] = dep;
    mako_h2_pri_weight[i] = w;
    mako_h2_pri_excl[i] = ex;
    mako_h2_last_sid = sid;
    return 0;
}

/* Count streams whose parent dependency is `parent` (priority tree query). */
static inline int64_t mako_http2_stream_priority_child_count(int64_t parent) {
    int64_t n = 0;
    for (int i = 0; i < MAKO_H2_STREAM_SLOTS; i++) {
        if (mako_h2_states[i] != 0 && mako_h2_pri_dep[i] == parent) n++;
    }
    return n;
}

static inline int64_t mako_http2_stream_priority_dep(int64_t stream) {
    int i = mako_http2_stream_find(stream);
    if (i < 0) return -1;
    return mako_h2_pri_dep[i];
}
static inline int64_t mako_http2_stream_priority_weight(int64_t stream) {
    int i = mako_http2_stream_find(stream);
    if (i < 0) return -1;
    return mako_h2_pri_weight[i];
}
static inline int64_t mako_http2_stream_priority_exclusive(int64_t stream) {
    int i = mako_http2_stream_find(stream);
    if (i < 0) return -1;
    return mako_h2_pri_excl[i];
}

/* Parent weight for scheduling: dep 0 (root) → 0; unknown parent → 0;
 * known parent stream → its priority weight. */
static inline int64_t mako_http2_schedule_parent_weight(int64_t dep) {
    if (dep <= 0) return 0;
    int pi = mako_http2_stream_find(dep);
    if (pi < 0) return 0;
    return mako_h2_pri_weight[pi];
}

/* Tree-aware weighted scheduling seed (Partial): among open streams (state==1),
 * score = parent_weight * 256 + own_weight (prefer children of higher-weight
 * parents). Ties → lowest stream id. Not a full RFC priority-tree scheduler. */
static inline int64_t mako_http2_schedule_next(void) {
    int best = -1;
    int64_t best_score = -1;
    for (int i = 0; i < MAKO_H2_STREAM_SLOTS; i++) {
        if (mako_h2_states[i] != 1) continue; /* open only */
        int64_t pw = mako_http2_schedule_parent_weight(mako_h2_pri_dep[i]);
        int64_t score = pw * 256 + mako_h2_pri_weight[i];
        if (best < 0 || score > best_score
            || (score == best_score && mako_h2_sids[i] < mako_h2_sids[best])) {
            best = i;
            best_score = score;
        }
    }
    if (best < 0) return -1;
    return mako_h2_sids[best];
}

/* Last applied stream id (compat). */
static inline int64_t mako_http2_stream_id(void) { return mako_h2_last_sid; }

static inline int64_t mako_http2_stream_state_of(int64_t sid) {
    int i = mako_http2_stream_find(sid);
    if (i < 0) return 0;
    return mako_h2_states[i];
}

static inline int64_t mako_http2_stream_half_closed_remote(int64_t sid) {
    int i = mako_http2_stream_find(sid);
    if (i < 0) return 0;
    return mako_h2_es_remote[i] ? 1 : 0;
}

static inline int64_t mako_http2_stream_half_closed_local(int64_t sid) {
    int i = mako_http2_stream_find(sid);
    if (i < 0) return 0;
    return mako_h2_es_local[i] ? 1 : 0;
}

/* State of last applied stream (compat with single-stream API). */
static inline int64_t mako_http2_stream_state(void) {
    if (mako_h2_last_sid == 0) return 0;
    return mako_http2_stream_state_of(mako_h2_last_sid);
}

/* Recompute aggregate state from END_STREAM direction flags. */
static inline void mako_http2_stream_recompute(int i) {
    if (mako_h2_es_remote[i] && mako_h2_es_local[i])
        mako_h2_states[i] = 3; /* closed */
    else if (mako_h2_es_remote[i] || mako_h2_es_local[i])
        mako_h2_states[i] = 2; /* half-closed */
    else
        mako_h2_states[i] = 1; /* open */
}

/* Apply one frame. Returns new state for that stream, or -1 on error.
 * HEADERS opens idle→open.
 * END_STREAM inbound → half-closed remote; outbound → half-closed local.
 * Both directions → closed. RST_STREAM → closed.
 * direction: 0=inbound (remote), 1=outbound (local). */
static inline int64_t mako_http2_stream_apply_dir(MakoString frame, int64_t direction) {
    int64_t typ = mako_http2_frame_type(frame);
    int64_t sid = mako_http2_frame_stream(frame);
    int64_t flags = mako_http2_frame_flags(frame);
    if (typ < 0 || sid <= 0) return -1;
    int i = mako_http2_stream_find(sid);
    if (typ == 3) { /* RST_STREAM → closed */
        if (i < 0) {
            i = mako_http2_stream_alloc(sid);
            if (i < 0) return -1;
        }
        mako_h2_states[i] = 3;
        mako_h2_es_remote[i] = 1;
        mako_h2_es_local[i] = 1;
        mako_h2_last_sid = sid;
        return 3;
    }
    if (i < 0) {
        if (typ != 1) return -1; /* only HEADERS opens */
        i = mako_http2_stream_alloc(sid);
        if (i < 0) return -1;
    } else if (mako_h2_states[i] == 3) {
        return -1;
    }
    mako_h2_last_sid = sid;
    if ((typ == 0 || typ == 1) && (flags & 0x1) != 0) {
        if (direction != 0)
            mako_h2_es_local[i] = 1;
        else
            mako_h2_es_remote[i] = 1;
        mako_http2_stream_recompute(i);
    }
    return mako_h2_states[i];
}

/* Inbound (received) END_STREAM — default for conn_recv / stream_apply. */
static inline int64_t mako_http2_stream_apply(MakoString frame) {
    return mako_http2_stream_apply_dir(frame, 0);
}
/* Outbound (sent) END_STREAM. */
static inline int64_t mako_http2_stream_apply_local(MakoString frame) {
    return mako_http2_stream_apply_dir(frame, 1);
}

/* QUIC header form (Partial).
 * Long: Header Form=1 + Fixed Bit=1 → 0xc0 mask (quic_detect = long, historical).
 * Short: Header Form=0 + Fixed Bit=1 → 0x40 mask — ambiguous with ASCII; use
 * quic_short_header explicitly. Spin Bit=bit5, Key Phase=bit2. */
static inline bool mako_quic_long_header(MakoString s) {
    if (!s.data || s.len < 1) return false;
    return ((unsigned char)s.data[0] & 0xc0) == 0xc0;
}
static inline bool mako_quic_short_header(MakoString s) {
    if (!s.data || s.len < 1) return false;
    return ((unsigned char)s.data[0] & 0xc0) == 0x40;
}
/* Historical: long-header only (short is too ambiguous with text). */
static inline bool mako_quic_detect(MakoString s) {
    return mako_quic_long_header(s);
}
/* Long-header packet type nibble (RFC 9000 §17.2): bits 4–5 → 0 Initial,
 * 1 0-RTT, 2 Handshake, 3 Retry. -1 if not long-header. */
static inline int64_t mako_quic_long_type(MakoString s) {
    if (!mako_quic_long_header(s)) return -1;
    return (int64_t)(((unsigned char)s.data[0] >> 4) & 0x3);
}
/* Version Negotiation: long header with Version field == 0 (RFC 9000 §17.2.1). */
static inline bool mako_quic_is_version_negotiation(MakoString s) {
    if (!mako_quic_long_header(s) || !s.data || s.len < 5) return false;
    return s.data[1] == 0 && s.data[2] == 0 && s.data[3] == 0 && s.data[4] == 0;
}
/* Retry long-header packet (type nibble == 3). */
static inline bool mako_quic_is_retry(MakoString s) {
    return mako_quic_long_type(s) == 3;
}
/* Short-header Spin Bit (bit 5) / Key Phase (bit 2); -1 if not short. */
static inline int64_t mako_quic_spin_bit(MakoString s) {
    if (!mako_quic_short_header(s)) return -1;
    return (((unsigned char)s.data[0] & 0x20) != 0) ? 1 : 0;
}
static inline int64_t mako_quic_key_phase(MakoString s) {
    if (!mako_quic_short_header(s)) return -1;
    return (((unsigned char)s.data[0] & 0x04) != 0) ? 1 : 0;
}

/* Returns QUIC version as int, or -1 if not long-header / truncated. */
static inline int64_t mako_quic_version(MakoString s) {
    if (!mako_quic_long_header(s) || !s.data || s.len < 5) return -1;
    return ((int64_t)(unsigned char)s.data[1] << 24)
         | ((int64_t)(unsigned char)s.data[2] << 16)
         | ((int64_t)(unsigned char)s.data[3] << 8)
         | ((int64_t)(unsigned char)s.data[4]);
}

/* Long-header layout after version: DCID len (1) + DCID + SCID len (1) + SCID ...
 * Returns DCID length, or -1 if truncated / not long-header. Max DCID 20. */
static inline int64_t mako_quic_dcid_len(MakoString s) {
    if (!mako_quic_long_header(s) || !s.data || s.len < 6) return -1;
    int64_t dlen = (int64_t)(unsigned char)s.data[5];
    if (dlen < 0 || dlen > 20) return -1;
    if (s.len < 6 + (size_t)dlen) return -1;
    return dlen;
}

/* Extract DCID bytes; empty if truncated. */
static inline MakoString mako_quic_dcid(MakoString s) {
    int64_t dlen = mako_quic_dcid_len(s);
    if (dlen < 0) return (MakoString){NULL, 0};
    return mako_str_slice(s, 6, 6 + dlen);
}

/* SCID length after DCID; -1 if truncated. */
static inline int64_t mako_quic_scid_len(MakoString s) {
    int64_t dlen = mako_quic_dcid_len(s);
    if (dlen < 0) return -1;
    size_t off = 6 + (size_t)dlen;
    if (s.len < off + 1) return -1;
    int64_t slen = (int64_t)(unsigned char)s.data[off];
    if (slen < 0 || slen > 20) return -1;
    if (s.len < off + 1 + (size_t)slen) return -1;
    return slen;
}

static inline MakoString mako_quic_scid(MakoString s) {
    int64_t dlen = mako_quic_dcid_len(s);
    int64_t slen = mako_quic_scid_len(s);
    if (dlen < 0 || slen < 0) return (MakoString){NULL, 0};
    size_t start = 6 + (size_t)dlen + 1;
    return mako_str_slice(s, (int64_t)start, (int64_t)(start + (size_t)slen));
}

/* Byte offset where packet-number / token region begins (after SCID).
 * Long-header Initial has token next; we only report the offset. -1 if truncated. */
static inline int64_t mako_quic_payload_offset(MakoString s) {
    int64_t dlen = mako_quic_dcid_len(s);
    int64_t slen = mako_quic_scid_len(s);
    if (dlen < 0 || slen < 0) return -1;
    return (int64_t)(6 + (size_t)dlen + 1 + (size_t)slen);
}

/* Version Negotiation supported-version list (RFC 9000 §17.2.1).
 * After SCID: zero or more 4-byte BE version numbers to end of packet. */
static inline int64_t mako_quic_vn_version_count(MakoString s) {
    if (!mako_quic_is_version_negotiation(s)) return -1;
    int64_t off = mako_quic_payload_offset(s);
    if (off < 0) return -1;
    if (s.len < (size_t)off) return -1;
    size_t rem = s.len - (size_t)off;
    if (rem % 4 != 0) return -1;
    return (int64_t)(rem / 4);
}
static inline int64_t mako_quic_vn_version_at(MakoString s, int64_t index) {
    int64_t n = mako_quic_vn_version_count(s);
    if (n < 0 || index < 0 || index >= n) return -1;
    int64_t off = mako_quic_payload_offset(s);
    size_t i = (size_t)off + (size_t)index * 4;
    return ((int64_t)(unsigned char)s.data[i] << 24)
         | ((int64_t)(unsigned char)s.data[i + 1] << 16)
         | ((int64_t)(unsigned char)s.data[i + 2] << 8)
         | ((int64_t)(unsigned char)s.data[i + 3]);
}
/* Pick preferred version if present in VN list; else first entry; else -1. */
static inline int64_t mako_quic_vn_select(MakoString s, int64_t preferred) {
    int64_t n = mako_quic_vn_version_count(s);
    if (n <= 0) return -1;
    for (int64_t i = 0; i < n; i++) {
        if (mako_quic_vn_version_at(s, i) == preferred) return preferred;
    }
    return mako_quic_vn_version_at(s, 0);
}

/* Build unprotected CRYPTO frame (type=0x06) + optional PADDING (0x00).
 * offset/length use 1-byte varints (values < 64) — A.2-closer seed, not full varint.
 * Pads with PADDING frames so total payload length >= min_len (for HP sample room). */
static inline MakoString mako_quic_crypto_frame(MakoString data, int64_t offset) {
    if (!data.data || data.len == 0 || data.len > 63 || offset < 0 || offset > 63)
        return mako_str_from_cstr("");
    size_t n = 3 + data.len; /* type + off + len + data */
    char *d = (char *)malloc(n + 1);
    if (!d) return mako_str_from_cstr("");
    d[0] = 0x06;
    d[1] = (char)(offset & 0x3f);
    d[2] = (char)(data.len & 0x3f);
    memcpy(d + 3, data.data, data.len);
    d[n] = 0;
    return (MakoString){d, n};
}

static inline MakoString mako_quic_crypto_payload(MakoString data, int64_t offset, int64_t min_len) {
    MakoString fr = mako_quic_crypto_frame(data, offset);
    if (!fr.data || fr.len == 0) return mako_str_from_cstr("");
    if (min_len < 0) min_len = 0;
    if ((int64_t)fr.len >= min_len) return fr;
    size_t pad = (size_t)min_len - fr.len;
    char *d = (char *)malloc(fr.len + pad + 1);
    if (!d) { free(fr.data); return mako_str_from_cstr(""); }
    memcpy(d, fr.data, fr.len);
    memset(d + fr.len, 0, pad); /* PADDING frames = 0x00 */
    d[fr.len + pad] = 0;
    size_t n = fr.len + pad;
    free(fr.data);
    return (MakoString){d, n};
}

/* Parse CRYPTO from an unprotected payload buffer (not a full packet).
 * Expects CRYPTO at byte 0 (after decryption of Initial payload). */
static inline int64_t mako_quic_payload_crypto_data_offset(MakoString payload) {
    if (!payload.data || payload.len < 3) return -1;
    if ((unsigned char)payload.data[0] != 0x06) return -1;
    unsigned char offb = (unsigned char)payload.data[1];
    unsigned char lenb = (unsigned char)payload.data[2];
    if ((offb & 0xc0) != 0 || (lenb & 0xc0) != 0) return -1;
    if (3 + (size_t)lenb > payload.len) return -1;
    return 3;
}
static inline int64_t mako_quic_payload_crypto_data_len(MakoString payload) {
    if (!payload.data || payload.len < 3) return -1;
    if ((unsigned char)payload.data[0] != 0x06) return -1;
    unsigned char lenb = (unsigned char)payload.data[2];
    if ((lenb & 0xc0) != 0) return -1;
    if (3 + (size_t)lenb > payload.len) return -1;
    return (int64_t)lenb;
}
static inline MakoString mako_quic_payload_crypto_data(MakoString payload) {
    int64_t start = mako_quic_payload_crypto_data_offset(payload);
    int64_t n = mako_quic_payload_crypto_data_len(payload);
    if (start < 0 || n < 0) return mako_str_from_cstr("");
    return mako_str_slice(payload, start, start + n);
}

/* Encode 1-byte QUIC varint (value < 64). Returns -1 if too large. */
static inline int mako_quic_put_varint1(char *d, int64_t v) {
    if (v < 0 || v > 63) return -1;
    d[0] = (char)(v & 0x3f);
    return 1;
}

/* ACK frame (type=0x02) seed — single contiguous range, 1-byte varints.
 * Layout: type | Largest Acknowledged | ACK Delay | ACK Range Count(=0) | First ACK Range.
 * First ACK Range = largest - smallest (inclusive span minus 1 in RFC terms when
 * acknowledging [smallest..largest] with count=0).
 * Only values < 64 supported. Not ECN / multi-range. */
static inline MakoString mako_quic_ack_frame(int64_t largest, int64_t delay, int64_t first_range) {
    if (largest < 0 || largest > 63 || delay < 0 || delay > 63
        || first_range < 0 || first_range > 63)
        return mako_str_from_cstr("");
    char *d = (char *)malloc(6);
    if (!d) return mako_str_from_cstr("");
    d[0] = 0x02; /* ACK */
    if (mako_quic_put_varint1(d + 1, largest) < 0
        || mako_quic_put_varint1(d + 2, delay) < 0
        || mako_quic_put_varint1(d + 3, 0) < 0
        || mako_quic_put_varint1(d + 4, first_range) < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    d[5] = 0;
    return (MakoString){d, 5};
}

static inline int64_t mako_quic_ack_largest(MakoString frame) {
    if (!frame.data || frame.len < 5) return -1;
    if ((unsigned char)frame.data[0] != 0x02) return -1;
    unsigned char v = (unsigned char)frame.data[1];
    if ((v & 0xc0) != 0) return -1;
    return (int64_t)v;
}
static inline int64_t mako_quic_ack_delay(MakoString frame) {
    if (!frame.data || frame.len < 5) return -1;
    if ((unsigned char)frame.data[0] != 0x02) return -1;
    unsigned char v = (unsigned char)frame.data[2];
    if ((v & 0xc0) != 0) return -1;
    return (int64_t)v;
}
static inline int64_t mako_quic_ack_range_count(MakoString frame) {
    if (!frame.data || frame.len < 5) return -1;
    if ((unsigned char)frame.data[0] != 0x02) return -1;
    unsigned char v = (unsigned char)frame.data[3];
    if ((v & 0xc0) != 0) return -1;
    return (int64_t)v;
}
static inline int64_t mako_quic_ack_first_range(MakoString frame) {
    if (!frame.data || frame.len < 5) return -1;
    if ((unsigned char)frame.data[0] != 0x02) return -1;
    unsigned char v = (unsigned char)frame.data[4];
    if ((v & 0xc0) != 0) return -1;
    return (int64_t)v;
}
/* Smallest PN in the first range: largest - first_range (when range_count=0). */
static inline int64_t mako_quic_ack_smallest(MakoString frame) {
    int64_t largest = mako_quic_ack_largest(frame);
    int64_t fr = mako_quic_ack_first_range(frame);
    if (largest < 0 || fr < 0) return -1;
    if (fr > largest) return -1;
    return largest - fr;
}
static inline int64_t mako_quic_is_ack(MakoString frame) {
    if (!frame.data || frame.len < 1) return 0;
    return ((unsigned char)frame.data[0] == 0x02) ? 1 : 0;
}

/* STREAM frame seed (RFC 9000 §19.8) — type 0x08..0x0f bits: OFF/LEN/FIN.
 * We use type = 0x08 | (off?0x04:0) | (len?0x02:0) | (fin?0x01:0).
 * Stream id / offset / length are 1-byte varints (<64). Data length must match. */
static inline MakoString mako_quic_stream_frame(
    int64_t stream_id, int64_t offset, MakoString data, int64_t fin
) {
    if (stream_id < 0 || stream_id > 63 || offset < 0 || offset > 63) return mako_str_from_cstr("");
    size_t dlen = (data.data && data.len > 0) ? data.len : 0;
    if (dlen > 63) return mako_str_from_cstr("");
    int has_off = offset > 0 ? 1 : 0;
    int has_len = 1; /* always include length for easy decode */
    int has_fin = fin ? 1 : 0;
    unsigned char typ = (unsigned char)(0x08 | (has_off ? 0x04 : 0) | (has_len ? 0x02 : 0) | (has_fin ? 0x01 : 0));
    size_t n = 1 + 1 + (has_off ? 1 : 0) + (has_len ? 1 : 0) + dlen;
    char *d = (char *)malloc(n + 1);
    if (!d) return mako_str_from_cstr("");
    size_t i = 0;
    d[i++] = (char)typ;
    d[i++] = (char)(stream_id & 0x3f);
    if (has_off) d[i++] = (char)(offset & 0x3f);
    if (has_len) d[i++] = (char)(dlen & 0x3f);
    if (dlen && data.data) memcpy(d + i, data.data, dlen);
    i += dlen;
    d[i] = 0;
    return (MakoString){d, i};
}

static inline int64_t mako_quic_is_stream(MakoString frame) {
    if (!frame.data || frame.len < 1) return 0;
    unsigned char t = (unsigned char)frame.data[0];
    return (t >= 0x08 && t <= 0x0f) ? 1 : 0;
}
static inline int64_t mako_quic_stream_fin(MakoString frame) {
    if (!mako_quic_is_stream(frame)) return -1;
    return ((unsigned char)frame.data[0] & 0x01) ? 1 : 0;
}
static inline int64_t mako_quic_stream_id_of(MakoString frame) {
    if (!mako_quic_is_stream(frame) || frame.len < 2) return -1;
    unsigned char v = (unsigned char)frame.data[1];
    if ((v & 0xc0) != 0) return -1;
    return (int64_t)v;
}
static inline int64_t mako_quic_stream_offset(MakoString frame) {
    if (!mako_quic_is_stream(frame) || frame.len < 2) return -1;
    unsigned char typ = (unsigned char)frame.data[0];
    if ((typ & 0x04) == 0) return 0;
    if (frame.len < 3) return -1;
    unsigned char v = (unsigned char)frame.data[2];
    if ((v & 0xc0) != 0) return -1;
    return (int64_t)v;
}
static inline int64_t mako_quic_stream_data_len(MakoString frame) {
    if (!mako_quic_is_stream(frame) || frame.len < 2) return -1;
    unsigned char typ = (unsigned char)frame.data[0];
    size_t i = 2; /* after type + stream_id */
    if (typ & 0x04) {
        if (frame.len < i + 1) return -1;
        i += 1; /* skip offset */
    }
    if (typ & 0x02) {
        if (frame.len < i + 1) return -1;
        unsigned char lenb = (unsigned char)frame.data[i];
        if ((lenb & 0xc0) != 0) return -1;
        if (i + 1 + (size_t)lenb > frame.len) return -1;
        return (int64_t)lenb;
    }
    /* no length bit: rest is data */
    return (int64_t)(frame.len - i);
}
static inline MakoString mako_quic_stream_data(MakoString frame) {
    if (!mako_quic_is_stream(frame) || frame.len < 2) return mako_str_from_cstr("");
    unsigned char typ = (unsigned char)frame.data[0];
    size_t i = 2;
    if (typ & 0x04) {
        if (frame.len < i + 1) return mako_str_from_cstr("");
        i += 1;
    }
    size_t dlen = 0;
    if (typ & 0x02) {
        if (frame.len < i + 1) return mako_str_from_cstr("");
        dlen = (size_t)(unsigned char)frame.data[i];
        i += 1;
        if (i + dlen > frame.len) return mako_str_from_cstr("");
    } else {
        dlen = frame.len - i;
    }
    if (dlen == 0) return mako_str_from_cstr("");
    return mako_str_slice(frame, (int64_t)i, (int64_t)(i + dlen));
}

/* CRYPTO frame type detect in long-header payload region (Partial).
 * QUIC frames: type CRYPTO = 0x06. Scans first few bytes after payload_offset
 * for a CRYPTO type byte (ignores packet number / encryption — seed only).
 * Returns byte offset of CRYPTO type relative to packet start, or -1.
 *
 * Limit: real Initial packets encrypt the payload; this only works for
 * unprotected test bytes (or after decryption). Encrypted CRYPTO remains Target. */
static inline int64_t mako_quic_crypto_offset(MakoString s) {
    int64_t base = mako_quic_payload_offset(s);
    if (base < 0 || !s.data) return -1;
    /* Skip a short unprotected PN guess (1–4 bytes) then look for 0x06. */
    size_t start = (size_t)base;
    if (start >= s.len) return -1;
    size_t end = s.len;
    if (end - start > 64) end = start + 64;
    for (size_t i = start; i < end; i++) {
        if ((unsigned char)s.data[i] == 0x06) return (int64_t)i;
    }
    return -1;
}
static inline bool mako_quic_has_crypto(MakoString s) {
    return mako_quic_crypto_offset(s) >= 0;
}

/* Parse unprotected CRYPTO frame at offset from quic_crypto_offset (Partial).
 * Layout: type(0x06) + offset varint + length varint + data.
 * Only 1-byte varints (values < 64) are supported — test-seed only. */
static inline int64_t mako_quic_crypto_data_offset(MakoString s) {
    int64_t toff = mako_quic_crypto_offset(s);
    if (toff < 0 || !s.data) return -1;
    size_t i = (size_t)toff + 1;
    if (i >= s.len) return -1;
    unsigned char offb = (unsigned char)s.data[i];
    if ((offb & 0xc0) != 0) return -1; /* multi-byte varint unsupported */
    i += 1;
    if (i >= s.len) return -1;
    unsigned char lenb = (unsigned char)s.data[i];
    if ((lenb & 0xc0) != 0) return -1;
    i += 1;
    if (i + (size_t)lenb > s.len) return -1;
    return (int64_t)i;
}
static inline int64_t mako_quic_crypto_data_len(MakoString s) {
    int64_t toff = mako_quic_crypto_offset(s);
    if (toff < 0 || !s.data) return -1;
    size_t i = (size_t)toff + 1;
    if (i >= s.len) return -1;
    unsigned char offb = (unsigned char)s.data[i];
    if ((offb & 0xc0) != 0) return -1;
    i += 1;
    if (i >= s.len) return -1;
    unsigned char lenb = (unsigned char)s.data[i];
    if ((lenb & 0xc0) != 0) return -1;
    i += 1;
    if (i + (size_t)lenb > s.len) return -1;
    return (int64_t)lenb;
}
static inline MakoString mako_quic_crypto_data(MakoString s) {
    int64_t start = mako_quic_crypto_data_offset(s);
    int64_t n = mako_quic_crypto_data_len(s);
    if (start < 0 || n < 0) return (MakoString){NULL, 0};
    return mako_str_slice(s, start, start + n);
}

/* TLS record header parse (RFC 8446): type(1) + legacy_version(2) + length(2).
 * Returns content type (20..24), or -1. */
static inline int64_t mako_tls_record_type(MakoString s) {
    if (!s.data || s.len < 5) return -1;
    unsigned char t = (unsigned char)s.data[0];
    if (t < 20 || t > 24) return -1;
    return (int64_t)t;
}

static inline int64_t mako_tls_record_version(MakoString s) {
    if (!s.data || s.len < 5) return -1;
    if (mako_tls_record_type(s) < 0) return -1;
    return ((int64_t)(unsigned char)s.data[1] << 8) | (int64_t)(unsigned char)s.data[2];
}

static inline int64_t mako_tls_record_len(MakoString s) {
    if (!s.data || s.len < 5) return -1;
    if (mako_tls_record_type(s) < 0) return -1;
    return ((int64_t)(unsigned char)s.data[3] << 8) | (int64_t)(unsigned char)s.data[4];
}

static inline int64_t mako_http_serve(int64_t port, MakoString body) {
    int fd = mako_http_listen_fd(port);
    if (fd < 0) {
        fprintf(stderr, "error: http: bind(:%lld) failed\n", (long long)port);
        return 1;
    }
    fprintf(stderr, "mako http listening on :%lld\n", (long long)port);
    char req[4096];
    for (;;) {
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0) continue;
        (void)recv(cfd, req, sizeof(req) - 1, 0);
        mako_http_reply(cfd, 200, "text/plain; charset=utf-8", body);
        mako_sock_close(cfd);
    }
}

/* Echo method + path as JSON. */
static inline int64_t mako_http_echo(int64_t port) {
    int fd = mako_http_listen_fd(port);
    if (fd < 0) {
        fprintf(stderr, "error: http: bind(:%lld) failed\n", (long long)port);
        return 1;
    }
    fprintf(stderr, "mako http echo on :%lld\n", (long long)port);
    for (;;) {
        int64_t c = mako_http_accept((int64_t)fd);
        if (c < 0) continue;
        MakoString method = mako_http_method(c);
        MakoString path = mako_http_path(c);
        char buf[640];
        int blen = snprintf(
            buf,
            sizeof(buf),
            "{\"method\":\"%.*s\",\"path\":\"%.*s\"}\n",
            (int)method.len,
            method.data,
            (int)path.len,
            path.data
        );
        if (blen < 0) blen = 0;
        MakoString body = {buf, (size_t)blen};
        /* respond copies body before close — stack buf ok for send */
        MakoHttpConn *hc = &mako_http_conns[c];
        mako_http_reply(hc->fd, 200, "application/json; charset=utf-8", body);
        mako_sock_close(hc->fd);
        mako_arena_free(&hc->arena);
        hc->live = false;
    }
}

/* Serve forever: reply with fixed body but print method/path per request. */
static inline int64_t mako_http_listen(int64_t port, MakoString body) {
    int64_t fd = mako_http_bind(port);
    if (fd < 0) return 1;
    for (;;) {
        int64_t c = mako_http_accept(fd);
        if (c < 0) continue;
        fprintf(stderr, "http %.*s %.*s\n",
                (int)mako_http_method(c).len, mako_http_method(c).data,
                (int)mako_http_path(c).len, mako_http_path(c).data);
        mako_http_respond(c, 200, body);
    }
}


/* ---- HTTP client library (plain HTTP/1.0; TLS via tls_* / nghttp2_*) ----
 *
 * Coherent surface for Mako callers:
 *   http_get(url) / http_post(url, body)
 *   http_request(method, url, body, timeout_ms)
 *   http_last_status() / http_last_header(name)  — from most recent client call
 * Server surface (existing): http_bind/accept/method/path/header/body/
 *   respond/respond_ct/respond_json/close/close_listener/next/keepalive
 */

static int mako_http_client_last_status = 0;
static char mako_http_client_last_raw[8192];
static size_t mako_http_client_last_raw_len = 0;

static inline int64_t mako_http_last_status(void) {
    return (int64_t)mako_http_client_last_status;
}

static inline MakoString mako_http_last_header(MakoString name) {
    if (mako_http_client_last_raw_len == 0) return mako_str_from_cstr("");
    char tmp[512];
    char nbuf[128];
    size_t nlen = name.len < sizeof(nbuf) - 1 ? name.len : sizeof(nbuf) - 1;
    if (!name.data || nlen == 0) return mako_str_from_cstr("");
    memcpy(nbuf, name.data, nlen);
    nbuf[nlen] = 0;
    if (mako_http_find_header(mako_http_client_last_raw, nbuf, tmp, sizeof(tmp)))
        return mako_str_from_cstr(tmp);
    return mako_str_from_cstr("");
}

static inline void mako_http_client_set_timeout(int fd, int64_t timeout_ms) {
    if (timeout_ms <= 0) return;
#if defined(_WIN32)
    DWORD ms = (DWORD)timeout_ms;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof(ms));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof(ms));
#else
    struct timeval tv;
    tv.tv_sec = (time_t)(timeout_ms / 1000);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000) * 1000);
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

/* method: "GET"/"POST"/…; body may be empty; timeout_ms <=0 means no socket timeout. */
static inline MakoString mako_http_request(
    MakoString method,
    MakoString url,
    MakoString body,
    int64_t timeout_ms
) {
    mako_http_client_last_status = 0;
    mako_http_client_last_raw_len = 0;
    mako_http_client_last_raw[0] = 0;

    if (!mako_net_init()) return mako_str_from_cstr("");

    const char *u = url.data ? url.data : "";
    if (strncmp(u, "http://", 7) != 0) {
        return mako_str_from_cstr("");
    }
    u += 7;
    char host[256];
    char path[1024];
    int port = 80;
    const char *slash = strchr(u, '/');
    size_t host_len;
    if (slash) {
        host_len = (size_t)(slash - u);
        snprintf(path, sizeof(path), "%s", slash);
    } else {
        host_len = strlen(u);
        snprintf(path, sizeof(path), "/");
    }
    if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
    memcpy(host, u, host_len);
    host[host_len] = 0;
    char *colon = strchr(host, ':');
    if (colon) {
        port = atoi(colon + 1);
        *colon = 0;
    }
    struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr_list[0]) return mako_str_from_cstr("");
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return mako_str_from_cstr("");
    mako_http_client_set_timeout(fd, timeout_ms);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        mako_sock_close(fd);
        return mako_str_from_cstr("");
    }

    const char *meth = method.data && method.len ? method.data : "GET";
    char methbuf[16];
    size_t mlen = method.len < sizeof(methbuf) - 1 ? method.len : sizeof(methbuf) - 1;
    if (method.data && method.len) {
        memcpy(methbuf, method.data, mlen);
        methbuf[mlen] = 0;
        meth = methbuf;
    }

    size_t blen = body.data ? body.len : 0;
    char req[4096];
    int n;
    if (blen > 0) {
        n = snprintf(
            req,
            sizeof(req),
            "%s %s HTTP/1.0\r\nHost: %s\r\nContent-Length: %zu\r\n"
            "Content-Type: application/octet-stream\r\nConnection: close\r\n\r\n",
            meth,
            path,
            host,
            blen
        );
    } else {
        n = snprintf(
            req,
            sizeof(req),
            "%s %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
            meth,
            path,
            host
        );
    }
    if (n < 0 || (size_t)n >= sizeof(req) || send(fd, req, (size_t)n, 0) < 0) {
        mako_sock_close(fd);
        return mako_str_from_cstr("");
    }
    if (blen > 0 && body.data) {
        if (send(fd, body.data, blen, 0) < 0) {
            mako_sock_close(fd);
            return mako_str_from_cstr("");
        }
    }

    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        mako_sock_close(fd);
        return mako_str_from_cstr("");
    }
    for (;;) {
        if (len + 1024 > cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) break;
            buf = nb;
        }
        ssize_t r = recv(fd, buf + len, 1024, 0);
        if (r <= 0) break;
        len += (size_t)r;
    }
    mako_sock_close(fd);
    buf[len] = 0;

    /* Stash raw (capped) for http_last_header / status. */
    size_t stash = len < sizeof(mako_http_client_last_raw) - 1
                       ? len
                       : sizeof(mako_http_client_last_raw) - 1;
    memcpy(mako_http_client_last_raw, buf, stash);
    mako_http_client_last_raw[stash] = 0;
    mako_http_client_last_raw_len = stash;
    if (len >= 12 && strncmp(buf, "HTTP/", 5) == 0) {
        const char *sp = strchr(buf, ' ');
        if (sp) mako_http_client_last_status = atoi(sp + 1);
    }

    char *bstart = strstr(buf, "\r\n\r\n");
    if (bstart) {
        bstart += 4;
        size_t bl = len - (size_t)(bstart - buf);
        char *out = (char *)malloc(bl + 1);
        if (!out) {
            free(buf);
            return mako_str_from_cstr("");
        }
        memcpy(out, bstart, bl);
        out[bl] = 0;
        free(buf);
        return (MakoString){out, bl};
    }
    return (MakoString){buf, len};
}

static inline MakoString mako_http_get(MakoString url) {
    MakoString meth = {(char *)(uintptr_t)"GET", 3};
    MakoString empty = {(char *)(uintptr_t)"", 0};
    return mako_http_request(meth, url, empty, 0);
}

static inline MakoString mako_http_post(MakoString url, MakoString body) {
    MakoString meth = {(char *)(uintptr_t)"POST", 4};
    return mako_http_request(meth, url, body, 0);
}

static inline MakoString mako_http_get_timeout(MakoString url, int64_t timeout_ms) {
    MakoString meth = {(char *)(uintptr_t)"GET", 3};
    MakoString empty = {(char *)(uintptr_t)"", 0};
    return mako_http_request(meth, url, empty, timeout_ms);
}

static inline MakoString mako_http_post_timeout(
    MakoString url,
    MakoString body,
    int64_t timeout_ms
) {
    MakoString meth = {(char *)(uintptr_t)"POST", 4};
    return mako_http_request(meth, url, body, timeout_ms);
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_HTTP_H */
