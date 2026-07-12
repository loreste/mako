/* Mako proxy / upstream hot path — TCP pool, HTTP forward/proxy, parse, splice.
 *
 * Designed for reverse proxies (Leba-class): reuse backend connections, expose
 * status+body, raw byte-pump proxying, chunked responses, nonblocking connect,
 * and efficient fd-to-fd copy. All APIs are ordinary sync Mako builtins.
 */
#ifndef MAKO_PROXY_H
#define MAKO_PROXY_H

#include "mako_net.h"
#include <ctype.h>
#if !defined(_WIN32)
#include <poll.h>
#if defined(__linux__)
#include <sys/sendfile.h>
#include <fcntl.h>
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Result types -------------------------------------------------------- */

typedef struct {
    int64_t ok;          /* 1 success, 0 failure */
    int64_t status;      /* HTTP status code (0 if not parsed) */
    int64_t body_len;    /* length of body */
    int64_t total_bytes; /* total response bytes read (headers+body raw) */
    MakoString body;
    MakoString headers;  /* raw header block (after status line, before body) */
} MakoHttpForwardResult;

typedef struct {
    int64_t ok;
    int64_t bytes_written; /* bytes written to client */
    int64_t bytes_read;    /* bytes read from backend */
} MakoProxyIoResult;

typedef struct {
    int64_t ok;
    int64_t content_length; /* -1 if unknown / chunked */
    int64_t chunked;        /* 1 if Transfer-Encoding: chunked */
    MakoString method;
    MakoString path;
    MakoString host;
    MakoString headers; /* raw "Name: value\r\n..." block */
    MakoString body;
} MakoHttpParsed;

static inline MakoHttpForwardResult mako_http_forward_result_fail(void) {
    MakoHttpForwardResult r;
    memset(&r, 0, sizeof(r));
    r.body = mako_str_from_cstr("");
    r.headers = mako_str_from_cstr("");
    return r;
}

static inline MakoProxyIoResult mako_proxy_io_fail(void) {
    MakoProxyIoResult r;
    memset(&r, 0, sizeof(r));
    return r;
}

static inline MakoHttpParsed mako_http_parsed_empty(void) {
    MakoHttpParsed r;
    memset(&r, 0, sizeof(r));
    r.content_length = -1;
    r.method = mako_str_from_cstr("GET");
    r.path = mako_str_from_cstr("/");
    r.host = mako_str_from_cstr("");
    r.headers = mako_str_from_cstr("");
    r.body = mako_str_from_cstr("");
    return r;
}

/* ---- Helpers ------------------------------------------------------------- */

static inline void mako_proxy_set_timeout(int fd, int64_t ms) {
    if (fd < 0 || ms <= 0) return;
#if defined(_WIN32)
    DWORD tv = (DWORD)ms;
    setsockopt((mako_sock_t)fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt((mako_sock_t)fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = (time_t)(ms / 1000);
    tv.tv_usec = (suseconds_t)((ms % 1000) * 1000);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

/* True if fd is still usable for reuse (no error, not half-closed readable EOF).
 * MUST NOT block: pool release is on the proxy hot path; SO_RCVTIMEO must not
 * apply to this probe. */
static inline int mako_tcp_fd_reusable(int fd) {
    if (fd < 0) return 0;
    int err = 0;
    socklen_t elen = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) != 0) return 0;
    if (err != 0) return 0;
    /* Nonblocking readability probe: 0ms select, then MSG_PEEK only if ready. */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    struct timeval tv = {0, 0};
    int pr = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (pr < 0) {
#if !defined(_WIN32)
        if (errno == EINTR) return 1; /* transient; keep connection */
#endif
        return 0;
    }
    if (pr == 0) return 1; /* not readable → still open, clean for reuse */
    /* Readable: EOF or unexpected buffered data → do not reuse. */
    char b;
#if defined(MSG_DONTWAIT)
    ssize_t n = recv(fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
#else
    ssize_t n = recv(fd, &b, 1, MSG_PEEK);
#endif
    if (n == 0) return 0; /* peer closed */
    if (n < 0) {
#if defined(_WIN32)
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK || e == WSAEINTR) return 1;
        return 0;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 1;
        return 0;
#endif
    }
    /* Unexpected buffered data — not clean for keep-alive reuse. */
    return 0;
}

static inline int mako_proxy_write_all(int fd, const char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, data + off, len - off, 0);
        if (n < 0) {
#if !defined(_WIN32)
            if (errno == EINTR) continue;
#endif
            return -1;
        }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

/* Case-insensitive ASCII compare of len bytes of a vs name (NUL-terminated). */
static inline int mako_proxy_ci_eq(const char *a, size_t alen, const char *name) {
    size_t nlen = strlen(name);
    if (alen != nlen) return 0;
    for (size_t k = 0; k < nlen; k++) {
        char x = a[k], y = name[k];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
    }
    return 1;
}

/* Advance past one line ending: prefers CRLF, accepts bare LF. Returns new index. */
static inline size_t mako_proxy_skip_eol(const char *buf, size_t n, size_t i) {
    if (i + 1 < n && buf[i] == '\r' && buf[i + 1] == '\n') return i + 2;
    if (i < n && buf[i] == '\n') return i + 1;
    return i;
}

/* Find end of line starting at i; returns index of first CR or LF (or n). */
static inline size_t mako_proxy_line_end(const char *buf, size_t n, size_t i) {
    while (i < n && buf[i] != '\r' && buf[i] != '\n') i++;
    return i;
}

/* Case-insensitive header value lookup into out (NUL-terminated). Returns 1 if found.
 * Accepts both CRLF and LF line endings. */
static inline int mako_proxy_find_header(
    const char *buf, size_t n, const char *name, char *out, size_t outcap
) {
    size_t nlen = strlen(name);
    size_t i = 0;
    /* Skip status/request line */
    i = mako_proxy_line_end(buf, n, i);
    i = mako_proxy_skip_eol(buf, n, i);
    while (i < n) {
        /* Blank line → end of headers */
        if ((i + 1 < n && buf[i] == '\r' && buf[i + 1] == '\n') || buf[i] == '\n')
            break;
        size_t line = i;
        size_t eol = mako_proxy_line_end(buf, n, i);
        size_t llen = eol - line;
        if (llen >= nlen + 1) {
            int match = 1;
            for (size_t k = 0; k < nlen; k++) {
                char a = buf[line + k], b = name[k];
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                if (a != b) { match = 0; break; }
            }
            if (match && buf[line + nlen] == ':') {
                size_t v = line + nlen + 1;
                while (v < line + llen && (buf[v] == ' ' || buf[v] == '\t')) v++;
                size_t vlen = (line + llen) - v;
                /* Trim trailing SP/HTAB */
                while (vlen > 0 && (buf[v + vlen - 1] == ' ' || buf[v + vlen - 1] == '\t'))
                    vlen--;
                if (vlen >= outcap) vlen = outcap - 1;
                if (out && outcap) {
                    memcpy(out, buf + v, vlen);
                    out[vlen] = 0;
                }
                return 1;
            }
        }
        i = mako_proxy_skip_eol(buf, n, eol);
        if (i == eol) break; /* no progress */
    }
    if (out && outcap) out[0] = 0;
    return 0;
}

/* Does header block (or full message) contain header `name`? */
static inline int mako_proxy_has_header(const char *buf, size_t n, const char *name) {
    char discard[4];
    return mako_proxy_find_header(buf, n, name, discard, sizeof(discard));
}

/* Does a raw caller header fragment contain `name` (case-insensitive)?
 * Fragment is lines of "Name: value" without the request line. */
static inline int mako_proxy_headers_have(const char *hdrs, size_t n, const char *name) {
    if (!hdrs || n == 0) return 0;
    /* Prefix a dummy request line so find_header can skip it. */
    size_t cap = n + 16;
    char *tmp = (char *)malloc(cap);
    if (!tmp) return 0;
    size_t off = 0;
    tmp[off++] = 'X';
    tmp[off++] = '\r';
    tmp[off++] = '\n';
    memcpy(tmp + off, hdrs, n);
    off += n;
    /* Ensure trailing CRLF so last header is visible */
    if (n >= 2 && hdrs[n - 2] == '\r' && hdrs[n - 1] == '\n') {
        /* ok */
    } else if (n >= 1 && hdrs[n - 1] == '\n') {
        /* ok */
    } else {
        if (off + 2 < cap) { tmp[off++] = '\r'; tmp[off++] = '\n'; }
    }
    if (off + 2 < cap) { tmp[off++] = '\r'; tmp[off++] = '\n'; }
    int found = mako_proxy_has_header(tmp, off, name);
    free(tmp);
    return found;
}

static inline int64_t mako_proxy_parse_status(const char *buf, size_t n) {
    /* HTTP/1.x NNN — also tolerate HTTP/2.0 style status line if present */
    if (n < 8) return 0;
    if (buf[0] != 'H' || buf[1] != 'T' || buf[2] != 'T' || buf[3] != 'P') return 0;
    size_t i = 0;
    while (i < n && buf[i] != ' ') i++;
    if (i >= n) return 0;
    i++;
    int64_t code = 0;
    int digits = 0;
    while (i < n && buf[i] >= '0' && buf[i] <= '9' && digits < 3) {
        code = code * 10 + (buf[i] - '0');
        i++;
        digits++;
    }
    return digits == 3 ? code : 0;
}

static inline int64_t mako_proxy_parse_content_length(const char *buf, size_t n) {
    char tmp[64];
    if (!mako_proxy_find_header(buf, n, "Content-Length", tmp, sizeof(tmp))) return -1;
    if (!tmp[0]) return -1;
    int64_t v = 0;
    for (char *p = tmp; *p; p++) {
        if (*p < '0' || *p > '9') {
            /* reject non-numeric / overflow-looking values */
            if (*p == ' ' || *p == '\t') continue;
            return -1;
        }
        if (v > (INT64_MAX - 9) / 10) return -1;
        v = v * 10 + (*p - '0');
    }
    return v;
}

static inline int mako_proxy_is_chunked(const char *buf, size_t n) {
    char tmp[128];
    if (!mako_proxy_find_header(buf, n, "Transfer-Encoding", tmp, sizeof(tmp))) return 0;
    for (char *p = tmp; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    }
    return strstr(tmp, "chunked") != NULL;
}

/* True for responses that must not have a body (RFC 9110). */
static inline int mako_proxy_status_no_body(int64_t status) {
    if (status == 204 || status == 304) return 1;
    if (status >= 100 && status < 200) return 1;
    return 0;
}

/* Find end of headers; returns body-start offset, or 0 if incomplete.
 * Accepts CRLFCRLF and bare LFLF. */
static inline size_t mako_proxy_header_end(const char *buf, size_t n) {
    for (size_t i = 0; i + 3 < n; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return i + 4;
    }
    for (size_t i = 0; i + 1 < n; i++) {
        if (buf[i] == '\n' && buf[i + 1] == '\n') return i + 2;
    }
    return 0;
}

/* Is the chunked body complete (terminal 0-chunk + trailers + final blank line)? */
static inline int mako_proxy_chunked_complete(const char *body, size_t blen) {
    size_t i = 0;
    while (i < blen) {
        size_t line = i;
        size_t eol = mako_proxy_line_end(body, blen, i);
        if (eol >= blen) return 0; /* incomplete size line */
        size_t he = line;
        while (he < eol && body[he] != ';') he++;
        size_t size = 0;
        int any = 0;
        for (size_t k = line; k < he; k++) {
            char c = body[k];
            size_t d;
            if (c >= '0' && c <= '9') d = (size_t)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (size_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = (size_t)(c - 'A' + 10);
            else if (c == ' ' || c == '\t') continue;
            else return 0;
            size = (size << 4) + d;
            any = 1;
        }
        if (!any) return 0;
        i = mako_proxy_skip_eol(body, blen, eol);
        if (size == 0) {
            /* trailers until blank line */
            while (i < blen) {
                if (i + 1 < blen && body[i] == '\r' && body[i + 1] == '\n') return 1;
                if (body[i] == '\n') return 1;
                size_t te = mako_proxy_line_end(body, blen, i);
                if (te >= blen) return 0;
                i = mako_proxy_skip_eol(body, blen, te);
            }
            return 0;
        }
        if (i + size > blen) return 0;
        i += size;
        /* require CRLF after chunk data */
        if (i + 1 < blen && body[i] == '\r' && body[i + 1] == '\n') i += 2;
        else if (i < blen && body[i] == '\n') i += 1;
        else return 0;
    }
    return 0;
}

/* Decode chunked body into newly allocated out. Returns body length or -1.
 * Handles extensions, trailers, empty body, incomplete input. */
static inline int64_t mako_proxy_decode_chunked(
    const char *raw, size_t raw_len, char **out_body, size_t *out_len
) {
    *out_body = NULL;
    *out_len = 0;
    if (!raw) return -1;
    if (raw_len == 0) return -1;
    char *acc = NULL;
    size_t acc_len = 0, acc_cap = 0;
    size_t i = 0;
    int saw_last = 0;
    while (i < raw_len) {
        size_t line = i;
        size_t eol = mako_proxy_line_end(raw, raw_len, i);
        if (eol >= raw_len) { free(acc); return -1; }
        size_t he = line;
        while (he < eol && raw[he] != ';') he++;
        size_t size = 0;
        int any = 0;
        for (size_t k = line; k < he; k++) {
            char c = raw[k];
            size_t d;
            if (c >= '0' && c <= '9') d = (size_t)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (size_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = (size_t)(c - 'A' + 10);
            else if (c == ' ' || c == '\t') continue;
            else { free(acc); return -1; }
            if (size > (SIZE_MAX >> 4)) { free(acc); return -1; }
            size = (size << 4) + d;
            any = 1;
        }
        if (!any) { free(acc); return -1; }
        i = mako_proxy_skip_eol(raw, raw_len, eol);
        if (size == 0) {
            saw_last = 1;
            /* consume optional trailers until blank line */
            while (i < raw_len) {
                if (i + 1 < raw_len && raw[i] == '\r' && raw[i + 1] == '\n') {
                    i += 2;
                    break;
                }
                if (raw[i] == '\n') { i += 1; break; }
                size_t te = mako_proxy_line_end(raw, raw_len, i);
                if (te >= raw_len) { free(acc); return -1; }
                i = mako_proxy_skip_eol(raw, raw_len, te);
            }
            break;
        }
        if (i + size > raw_len) { free(acc); return -1; }
        if (acc_len + size + 1 > acc_cap) {
            size_t nc = acc_cap ? acc_cap * 2 : 4096;
            while (nc < acc_len + size + 1) {
                if (nc > SIZE_MAX / 2) { free(acc); return -1; }
                nc *= 2;
            }
            char *nb = (char *)realloc(acc, nc);
            if (!nb) { free(acc); return -1; }
            acc = nb;
            acc_cap = nc;
        }
        memcpy(acc + acc_len, raw + i, size);
        acc_len += size;
        i += size;
        if (i + 1 < raw_len && raw[i] == '\r' && raw[i + 1] == '\n') i += 2;
        else if (i < raw_len && raw[i] == '\n') i += 1;
        else { free(acc); return -1; }
    }
    if (!saw_last) { free(acc); return -1; }
    if (!acc) {
        acc = (char *)malloc(1);
        if (!acc) return -1;
        acc[0] = 0;
    } else {
        acc[acc_len] = 0;
    }
    *out_body = acc;
    *out_len = acc_len;
    return (int64_t)acc_len;
}

/* Build an HTTP/1.1 request line + headers into `out` (cap bytes).
 * - Normalizes caller headers to end with CRLF (if non-empty).
 * - Adds Host only if not already present.
 * - Adds Content-Length only if not already present (always for correctness).
 * - connection: "close" or "keep-alive".
 * Returns header byte length, or -1 on overflow/error. */
static inline int mako_proxy_build_request(
    char *out, size_t cap,
    const char *method, int mlen,
    const char *path, int plen,
    const char *host, size_t host_len,
    const char *headers, size_t headers_len,
    size_t body_len,
    const char *connection
) {
    if (!out || cap < 64) return -1;
    if (!method || mlen <= 0) { method = "GET"; mlen = 3; }
    if (!path || plen <= 0) { path = "/"; plen = 1; }
    if (!host) host = "localhost";
    if (!connection) connection = "close";
    /* Reject CR/LF in method/path/host (request smuggling seed). */
    for (int k = 0; k < mlen; k++)
        if (method[k] == '\r' || method[k] == '\n' || method[k] == ' ') return -1;
    for (int k = 0; k < plen; k++)
        if (path[k] == '\r' || path[k] == '\n') return -1;
    for (size_t k = 0; k < host_len; k++)
        if (host[k] == '\r' || host[k] == '\n' || host[k] == 0) return -1;

    int need_host = 1;
    int need_cl = 1;
    if (headers && headers_len) {
        if (mako_proxy_headers_have(headers, headers_len, "Host")) need_host = 0;
        if (mako_proxy_headers_have(headers, headers_len, "Content-Length")) need_cl = 0;
    }

    size_t off = 0;
#define MAKO_PROXY_APPEND(fmt, ...) do { \
        int _n = snprintf(out + off, cap - off, fmt, ##__VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= cap - off) return -1; \
        off += (size_t)_n; \
    } while (0)

    MAKO_PROXY_APPEND("%.*s %.*s HTTP/1.1\r\n", mlen, method, plen, path);
    if (need_host) {
        MAKO_PROXY_APPEND("Host: %.*s\r\n", (int)host_len, host);
    }
    if (headers && headers_len) {
        if (off + headers_len >= cap) return -1;
        memcpy(out + off, headers, headers_len);
        off += headers_len;
        /* Ensure trailing CRLF after last header line */
        if (headers_len >= 2 && headers[headers_len - 2] == '\r' && headers[headers_len - 1] == '\n') {
            /* ok */
        } else if (headers_len >= 1 && headers[headers_len - 1] == '\n') {
            /* bare LF — leave as-is (HTTP allows) */
        } else {
            MAKO_PROXY_APPEND("\r\n");
        }
    }
    if (need_cl) {
        MAKO_PROXY_APPEND("Content-Length: %zu\r\n", body_len);
    }
    MAKO_PROXY_APPEND("Connection: %s\r\n\r\n", connection);
#undef MAKO_PROXY_APPEND
    return (int)off;
}

/* Read HTTP response fully from fd (Content-Length, chunked, or close).
 * Edge cases: 1xx/204/304 no body; CL=0; incomplete chunked → error on EOF;
 * max response 16 MiB. */
static inline int mako_proxy_read_http_response(
    int fd, char **out_buf, size_t *out_len, int64_t timeout_ms
) {
    if (fd < 0) return -1;
    if (timeout_ms > 0) mako_proxy_set_timeout(fd, timeout_ms);
    char *buf = NULL;
    size_t total = 0, cap = 0;
    size_t hdr_end = 0;
    int64_t content_len = -1;
    int chunked = 0;
    int headers_done = 0;
    int64_t status = 0;
    int no_body = 0;

    for (;;) {
        if (total + 8192 + 1 > cap) {
            size_t nc = cap ? cap * 2 : 16384;
            while (nc < total + 8192 + 1) {
                if (nc > 16 * 1024 * 1024) { free(buf); return -1; }
                nc *= 2;
            }
            if (nc > 16 * 1024 * 1024) { free(buf); return -1; }
            char *nb = (char *)realloc(buf, nc);
            if (!nb) { free(buf); return -1; }
            buf = nb;
            cap = nc;
        }
        ssize_t n = recv(fd, buf + total, 8192, 0);
        if (n < 0) {
#if !defined(_WIN32)
            if (errno == EINTR) continue;
            /* Timeout with partial headers is an error; with complete CL body ok. */
#endif
            free(buf);
            return -1;
        }
        if (n == 0) {
            if (!headers_done) { free(buf); return -1; }
            if (chunked) {
                /* Incomplete chunked is an error */
                if (!mako_proxy_chunked_complete(buf + hdr_end, total - hdr_end)) {
                    free(buf);
                    return -1;
                }
            }
            /* CL: if short body on close, still return what we have (caller decides). */
            break;
        }
        total += (size_t)n;
        buf[total] = 0;

        if (!headers_done) {
            hdr_end = mako_proxy_header_end(buf, total);
            if (!hdr_end) continue;
            headers_done = 1;
            status = mako_proxy_parse_status(buf, hdr_end);
            no_body = mako_proxy_status_no_body(status);
            content_len = mako_proxy_parse_content_length(buf, hdr_end);
            chunked = mako_proxy_is_chunked(buf, hdr_end);
            if (no_body) {
                total = hdr_end;
                break;
            }
            if (chunked) {
                /* fall through to completeness check below */
            } else if (content_len >= 0) {
                size_t need = hdr_end + (size_t)content_len;
                if (total >= need) {
                    total = need;
                    break;
                }
            }
            /* else: read until EOF (Connection: close / no CL) */
        } else if (!chunked && content_len >= 0) {
            if (total >= hdr_end + (size_t)content_len) {
                total = hdr_end + (size_t)content_len;
                break;
            }
        }

        if (chunked && headers_done) {
            if (mako_proxy_chunked_complete(buf + hdr_end, total - hdr_end)) {
                /* Trim any trailing bytes after complete message if present later */
                break;
            }
        }
    }
    *out_buf = buf;
    *out_len = total;
    return 0;
}

/* ---- TCP connection pool ------------------------------------------------- */

#define MAKO_TCP_POOL_MAX 32
#define MAKO_TCP_POOL_IDLE 128

typedef struct {
    int live;
    char host[256];
    int port;
    int max;
    int timeout_ms;
    int idle_fds[MAKO_TCP_POOL_IDLE];
    int idle_n;
    int open_n; /* total checked-out + idle */
} MakoTcpPoolSlot;

static MakoTcpPoolSlot mako_tcp_pools[MAKO_TCP_POOL_MAX];
static pthread_mutex_t mako_tcp_pools_mu = PTHREAD_MUTEX_INITIALIZER;

static inline int64_t mako_tcp_pool_open(
    MakoString host, int64_t port, int64_t max, int64_t timeout_ms
) {
    if (!host.data || host.len == 0 || host.len >= 256) return -1;
    /* Reject NULs / CR/LF in host (injection). */
    for (size_t i = 0; i < host.len; i++) {
        if (host.data[i] == 0 || host.data[i] == '\r' || host.data[i] == '\n') return -1;
    }
    if (port <= 0 || port > 65535) return -1;
    if (max <= 0) max = 8;
    if (max > MAKO_TCP_POOL_IDLE) max = MAKO_TCP_POOL_IDLE;
    if (timeout_ms < 0) timeout_ms = 0;
    pthread_mutex_lock(&mako_tcp_pools_mu);
    int slot = -1;
    for (int i = 0; i < MAKO_TCP_POOL_MAX; i++) {
        if (!mako_tcp_pools[i].live) { slot = i; break; }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&mako_tcp_pools_mu);
        return -1;
    }
    MakoTcpPoolSlot *p = &mako_tcp_pools[slot];
    memset(p, 0, sizeof(*p));
    memcpy(p->host, host.data, host.len);
    p->host[host.len] = 0;
    p->port = (int)port;
    p->max = (int)max;
    p->timeout_ms = (int)timeout_ms;
    p->live = 1;
    pthread_mutex_unlock(&mako_tcp_pools_mu);
    return (int64_t)slot;
}

static inline int64_t mako_tcp_pool_connect_one(MakoTcpPoolSlot *p) {
    MakoString h = {p->host, strlen(p->host)};
    int64_t fd = mako_tcp_connect(h, p->port);
    if (fd < 0) return -1;
    if (p->timeout_ms > 0) mako_proxy_set_timeout((int)fd, p->timeout_ms);
    /* Prefer low latency on pooled backends. */
    int flag = 1;
    setsockopt((int)fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));
    return fd;
}

static inline int64_t mako_tcp_pool_acquire(int64_t pool) {
    if (pool < 0 || pool >= MAKO_TCP_POOL_MAX) return -1;
    pthread_mutex_lock(&mako_tcp_pools_mu);
    MakoTcpPoolSlot *p = &mako_tcp_pools[pool];
    if (!p->live) {
        pthread_mutex_unlock(&mako_tcp_pools_mu);
        return -1;
    }
    /* Prefer idle reusable fds */
    while (p->idle_n > 0) {
        int fd = p->idle_fds[--p->idle_n];
        if (mako_tcp_fd_reusable(fd)) {
            if (p->timeout_ms > 0) mako_proxy_set_timeout(fd, p->timeout_ms);
            pthread_mutex_unlock(&mako_tcp_pools_mu);
            return (int64_t)fd;
        }
        mako_sock_close(fd);
        p->open_n--;
    }
    if (p->open_n >= p->max) {
        pthread_mutex_unlock(&mako_tcp_pools_mu);
        return -1;
    }
    /* Connect outside lock would race open_n; keep lock for bookkeeping. */
    int64_t fd = mako_tcp_pool_connect_one(p);
    if (fd < 0) {
        pthread_mutex_unlock(&mako_tcp_pools_mu);
        return -1;
    }
    p->open_n++;
    pthread_mutex_unlock(&mako_tcp_pools_mu);
    return fd;
}

static inline int64_t mako_tcp_pool_release(int64_t pool, int64_t fd, int64_t reusable) {
    if (pool < 0 || pool >= MAKO_TCP_POOL_MAX) return 0;
    /* Probe without holding the pool lock (may block briefly on peek). */
    int can_reuse = reusable && mako_tcp_fd_reusable((int)fd);
    pthread_mutex_lock(&mako_tcp_pools_mu);
    MakoTcpPoolSlot *p = &mako_tcp_pools[pool];
    if (!p->live || fd < 0) {
        pthread_mutex_unlock(&mako_tcp_pools_mu);
        return 0;
    }
    /* Cap idle list; never exceed max. */
    if (can_reuse && p->idle_n < p->max && p->idle_n < MAKO_TCP_POOL_IDLE) {
        p->idle_fds[p->idle_n++] = (int)fd;
        pthread_mutex_unlock(&mako_tcp_pools_mu);
        return 1;
    }
    mako_sock_close((int)fd);
    if (p->open_n > 0) p->open_n--;
    pthread_mutex_unlock(&mako_tcp_pools_mu);
    return 1;
}

static inline int64_t mako_tcp_pool_close(int64_t pool) {
    if (pool < 0 || pool >= MAKO_TCP_POOL_MAX) return 0;
    pthread_mutex_lock(&mako_tcp_pools_mu);
    MakoTcpPoolSlot *p = &mako_tcp_pools[pool];
    if (!p->live) {
        pthread_mutex_unlock(&mako_tcp_pools_mu);
        return 0;
    }
    for (int i = 0; i < p->idle_n; i++) {
        if (p->idle_fds[i] >= 0) mako_sock_close(p->idle_fds[i]);
    }
    memset(p, 0, sizeof(*p));
    pthread_mutex_unlock(&mako_tcp_pools_mu);
    return 1;
}

/* Idempotent close of already-closed / invalid pool → 0. */

static inline int64_t mako_tcp_pool_idle(int64_t pool) {
    if (pool < 0 || pool >= MAKO_TCP_POOL_MAX) return -1;
    pthread_mutex_lock(&mako_tcp_pools_mu);
    int64_t n = !mako_tcp_pools[pool].live ? -1 : mako_tcp_pools[pool].idle_n;
    pthread_mutex_unlock(&mako_tcp_pools_mu);
    return n;
}

static inline int64_t mako_tcp_pool_open_count(int64_t pool) {
    if (pool < 0 || pool >= MAKO_TCP_POOL_MAX) return -1;
    pthread_mutex_lock(&mako_tcp_pools_mu);
    int64_t n = !mako_tcp_pools[pool].live ? -1 : mako_tcp_pools[pool].open_n;
    pthread_mutex_unlock(&mako_tcp_pools_mu);
    return n;
}

/* ---- HTTP/1.1 forward full ----------------------------------------------- */

/* Split raw response into status/headers/body (handles chunked + no-body). */
static inline int mako_proxy_fill_forward_result(
    MakoHttpForwardResult *r, char *raw, size_t raw_len
) {
    if (!r || !raw) return -1;
    r->total_bytes = (int64_t)raw_len;
    r->status = mako_proxy_parse_status(raw, raw_len);
    size_t bstart = mako_proxy_header_end(raw, raw_len);
    if (!bstart) return -1;
    size_t line_end = mako_proxy_line_end(raw, bstart, 0);
    size_t h0 = mako_proxy_skip_eol(raw, bstart, line_end);
    size_t h1 = bstart;
    /* Strip final blank-line marker from headers view */
    if (h1 >= 4 && raw[h1 - 4] == '\r' && raw[h1 - 3] == '\n'
        && raw[h1 - 2] == '\r' && raw[h1 - 1] == '\n') {
        h1 -= 4;
    } else if (h1 >= 2 && raw[h1 - 2] == '\n' && raw[h1 - 1] == '\n') {
        h1 -= 2;
    }
    if (h1 < h0) h1 = h0;
    {
        size_t hlen2 = h1 - h0;
        char *hd = (char *)malloc(hlen2 + 1);
        if (hd) {
            memcpy(hd, raw + h0, hlen2);
            hd[hlen2] = 0;
            free(r->headers.data);
            r->headers = (MakoString){hd, hlen2};
        }
    }
    if (mako_proxy_status_no_body(r->status)) {
        free(r->body.data);
        r->body = mako_str_from_cstr("");
        r->body_len = 0;
        return 0;
    }
    if (mako_proxy_is_chunked(raw, bstart)) {
        char *body_out = NULL;
        size_t body_len = 0;
        if (mako_proxy_decode_chunked(raw + bstart, raw_len - bstart, &body_out, &body_len) < 0)
            return -1;
        free(r->body.data);
        r->body = (MakoString){body_out, body_len};
        r->body_len = (int64_t)body_len;
    } else {
        size_t blen = raw_len - bstart;
        int64_t cl = mako_proxy_parse_content_length(raw, bstart);
        if (cl >= 0 && (size_t)cl < blen) blen = (size_t)cl;
        char *bd = (char *)malloc(blen + 1);
        if (!bd) return -1;
        if (blen) memcpy(bd, raw + bstart, blen);
        bd[blen] = 0;
        free(r->body.data);
        r->body = (MakoString){bd, blen};
        r->body_len = (int64_t)blen;
    }
    return 0;
}

static inline MakoHttpForwardResult mako_http_forward_full(
    MakoString host, int64_t port,
    MakoString method, MakoString path,
    MakoString headers, MakoString body,
    int64_t timeout_ms
) {
    MakoHttpForwardResult r = mako_http_forward_result_fail();
    if (!host.data || host.len == 0 || port <= 0 || port > 65535) return r;
    int64_t fd = mako_tcp_connect(host, port);
    if (fd < 0) return r;
    if (timeout_ms > 0) mako_proxy_set_timeout((int)fd, timeout_ms);

    char hbuf[256];
    size_t hn = (host.len < sizeof(hbuf) - 1) ? host.len : sizeof(hbuf) - 1;
    memcpy(hbuf, host.data ? host.data : "", hn);
    hbuf[hn] = 0;
    const char *m = (method.len && method.data) ? method.data : "GET";
    int mlen = (int)(method.len ? method.len : 3);
    const char *pp = (path.len && path.data) ? path.data : "/";
    int plen = (int)(path.len ? path.len : 1);

    char head[8192];
    int hlen = mako_proxy_build_request(
        head, sizeof(head), m, mlen, pp, plen, hbuf, hn,
        headers.data, headers.len, body.len, "close"
    );
    if (hlen < 0) {
        mako_tcp_close(fd);
        return r;
    }
    if (mako_proxy_write_all((int)fd, head, (size_t)hlen) < 0) {
        mako_tcp_close(fd);
        return r;
    }
    if (body.len && body.data
        && mako_proxy_write_all((int)fd, body.data, body.len) < 0) {
        mako_tcp_close(fd);
        return r;
    }

    char *raw = NULL;
    size_t raw_len = 0;
    if (mako_proxy_read_http_response((int)fd, &raw, &raw_len, timeout_ms) < 0 || !raw) {
        mako_tcp_close(fd);
        return r;
    }
    mako_tcp_close(fd);

    if (mako_proxy_fill_forward_result(&r, raw, raw_len) < 0) {
        free(raw);
        return r;
    }
    free(raw);
    r.ok = 1;
    return r;
}

/* Accessors */
static inline int64_t mako_http_forward_ok(MakoHttpForwardResult r) { return r.ok; }
static inline int64_t mako_http_forward_status(MakoHttpForwardResult r) { return r.status; }
static inline int64_t mako_http_forward_body_len(MakoHttpForwardResult r) { return r.body_len; }
static inline int64_t mako_http_forward_total_bytes(MakoHttpForwardResult r) { return r.total_bytes; }
static inline MakoString mako_http_forward_body(MakoHttpForwardResult r) {
    return mako_str_clone(r.body);
}
static inline MakoString mako_http_forward_headers(MakoHttpForwardResult r) {
    return mako_str_clone(r.headers);
}

/* Forward using a pooled fd (does not close fd; caller releases with reusable flag). */
static inline MakoHttpForwardResult mako_http_forward_fd(
    int64_t fd,
    MakoString method, MakoString path, MakoString host_hdr,
    MakoString headers, MakoString body,
    int64_t timeout_ms
) {
    MakoHttpForwardResult r = mako_http_forward_result_fail();
    if (fd < 0) return r;
    if (timeout_ms > 0) mako_proxy_set_timeout((int)fd, timeout_ms);

    const char *m = (method.len && method.data) ? method.data : "GET";
    int mlen = (int)(method.len ? method.len : 3);
    const char *pp = (path.len && path.data) ? path.data : "/";
    int plen = (int)(path.len ? path.len : 1);
    char hbuf[256];
    size_t hn = 0;
    if (host_hdr.data && host_hdr.len) {
        hn = host_hdr.len < sizeof(hbuf) - 1 ? host_hdr.len : sizeof(hbuf) - 1;
        memcpy(hbuf, host_hdr.data, hn);
    } else {
        memcpy(hbuf, "localhost", 9);
        hn = 9;
    }
    hbuf[hn] = 0;

    char head[8192];
    int hlen = mako_proxy_build_request(
        head, sizeof(head), m, mlen, pp, plen, hbuf, hn,
        headers.data, headers.len, body.len, "keep-alive"
    );
    if (hlen < 0) return r;
    if (mako_proxy_write_all((int)fd, head, (size_t)hlen) < 0) return r;
    if (body.len && body.data
        && mako_proxy_write_all((int)fd, body.data, body.len) < 0) return r;

    char *raw = NULL;
    size_t raw_len = 0;
    if (mako_proxy_read_http_response((int)fd, &raw, &raw_len, timeout_ms) < 0 || !raw)
        return r;

    if (mako_proxy_fill_forward_result(&r, raw, raw_len) < 0) {
        free(raw);
        return r;
    }
    free(raw);
    r.ok = 1;
    return r;
}

/* ---- Raw HTTP proxy (byte pump) ------------------------------------------ */

static inline MakoProxyIoResult mako_http_proxy_raw(
    int64_t client_fd, int64_t backend_fd, MakoString raw_request, int64_t timeout_ms
) {
    MakoProxyIoResult r = mako_proxy_io_fail();
    if (client_fd < 0 || backend_fd < 0) return r;
    if (client_fd == backend_fd) return r; /* refuse same-fd loop */
    if (timeout_ms > 0) {
        mako_proxy_set_timeout((int)client_fd, timeout_ms);
        mako_proxy_set_timeout((int)backend_fd, timeout_ms);
    }
    if (!raw_request.data || raw_request.len == 0) return r;
    if (mako_proxy_write_all((int)backend_fd, raw_request.data, raw_request.len) < 0)
        return r;

    char *raw = NULL;
    size_t raw_len = 0;
    if (mako_proxy_read_http_response((int)backend_fd, &raw, &raw_len, timeout_ms) < 0 || !raw)
        return r;
    r.bytes_read = (int64_t)raw_len;
    if (raw_len == 0) {
        free(raw);
        return r;
    }
    if (mako_proxy_write_all((int)client_fd, raw, raw_len) < 0) {
        free(raw);
        return r;
    }
    free(raw);
    r.bytes_written = (int64_t)raw_len;
    r.ok = 1;
    return r;
}

static inline int64_t mako_proxy_io_ok(MakoProxyIoResult r) { return r.ok; }
static inline int64_t mako_proxy_io_bytes_written(MakoProxyIoResult r) { return r.bytes_written; }
static inline int64_t mako_proxy_io_bytes_read(MakoProxyIoResult r) { return r.bytes_read; }

/* ---- HTTP request parser (C hot path, no str_split) ---------------------- */

static inline MakoHttpParsed mako_http_parse(MakoString raw) {
    MakoHttpParsed r = mako_http_parsed_empty();
    if (!raw.data || raw.len == 0) return r; /* ok=0 */

    const char *s = raw.data;
    size_t n = raw.len;

    /* request-line: METHOD SP path [SP version] */
    size_t i = 0;
    while (i < n && s[i] != ' ' && s[i] != '\r' && s[i] != '\n') i++;
    if (i == 0 || i >= n || s[i] != ' ') return r; /* no method */
    {
        char *m = (char *)malloc(i + 1);
        if (!m) return r;
        memcpy(m, s, i);
        m[i] = 0;
        free(r.method.data);
        r.method = (MakoString){m, i};
    }
    size_t p0 = i + 1;
    size_t p1 = p0;
    while (p1 < n && s[p1] != ' ' && s[p1] != '\r' && s[p1] != '\n') p1++;
    if (p1 == p0) return r; /* empty path */
    {
        size_t plen = p1 - p0;
        char *p = (char *)malloc(plen + 1);
        if (!p) return r;
        memcpy(p, s + p0, plen);
        p[plen] = 0;
        free(r.path.data);
        r.path = (MakoString){p, plen};
    }

    size_t bstart = mako_proxy_header_end(s, n);
    if (!bstart) {
        /* Incomplete headers: method+path still useful */
        r.ok = 1;
        return r;
    }
    size_t line_end = mako_proxy_line_end(s, bstart, 0);
    size_t h0 = mako_proxy_skip_eol(s, bstart, line_end);
    size_t h1 = bstart;
    if (h1 >= 4 && s[h1 - 4] == '\r' && s[h1 - 3] == '\n'
        && s[h1 - 2] == '\r' && s[h1 - 1] == '\n') {
        h1 -= 4;
    } else if (h1 >= 2 && s[h1 - 2] == '\n' && s[h1 - 1] == '\n') {
        h1 -= 2;
    }
    if (h1 < h0) h1 = h0;
    {
        size_t hlen = h1 - h0;
        char *hd = (char *)malloc(hlen + 1);
        if (hd) {
            memcpy(hd, s + h0, hlen);
            hd[hlen] = 0;
            free(r.headers.data);
            r.headers = (MakoString){hd, hlen};
        }
    }
    char hostbuf[512];
    if (mako_proxy_find_header(s, bstart, "Host", hostbuf, sizeof(hostbuf))) {
        size_t hl = strlen(hostbuf);
        char *h = (char *)malloc(hl + 1);
        if (h) {
            memcpy(h, hostbuf, hl + 1);
            free(r.host.data);
            r.host = (MakoString){h, hl};
        }
    }
    r.content_length = mako_proxy_parse_content_length(s, bstart);
    r.chunked = mako_proxy_is_chunked(s, bstart) ? 1 : 0;

    size_t blen = n - bstart;
    if (r.chunked) {
        char *body_out = NULL;
        size_t body_len = 0;
        if (mako_proxy_decode_chunked(s + bstart, blen, &body_out, &body_len) >= 0) {
            free(r.body.data);
            r.body = (MakoString){body_out, body_len};
        }
        /* incomplete chunked: leave body empty, still ok=1 (headers parsed) */
    } else {
        /* Truncate to Content-Length when present and shorter than available. */
        if (r.content_length >= 0 && (size_t)r.content_length < blen)
            blen = (size_t)r.content_length;
        char *bd = (char *)malloc(blen + 1);
        if (bd) {
            if (blen) memcpy(bd, s + bstart, blen);
            bd[blen] = 0;
            free(r.body.data);
            r.body = (MakoString){bd, blen};
        }
    }
    r.ok = 1;
    return r;
}

static inline int64_t mako_http_parsed_ok(MakoHttpParsed r) { return r.ok; }
static inline int64_t mako_http_parsed_content_length(MakoHttpParsed r) { return r.content_length; }
static inline int64_t mako_http_parsed_chunked(MakoHttpParsed r) { return r.chunked; }
static inline MakoString mako_http_parsed_method(MakoHttpParsed r) { return mako_str_clone(r.method); }
static inline MakoString mako_http_parsed_path(MakoHttpParsed r) { return mako_str_clone(r.path); }
static inline MakoString mako_http_parsed_host(MakoHttpParsed r) { return mako_str_clone(r.host); }
static inline MakoString mako_http_parsed_headers(MakoHttpParsed r) { return mako_str_clone(r.headers); }
static inline MakoString mako_http_parsed_body(MakoHttpParsed r) { return mako_str_clone(r.body); }

/* Lookup a single header from a parsed headers block (or full request).
 * Name match is case-insensitive; empty name → "". */
static inline MakoString mako_http_parsed_header(MakoHttpParsed r, MakoString name) {
    if (!r.headers.data || !name.data || name.len == 0) return mako_str_from_cstr("");
    size_t cap = r.headers.len + 32;
    char *tmp = (char *)malloc(cap);
    if (!tmp) return mako_str_from_cstr("");
    /* Dummy request line + headers + blank line */
    size_t off = 0;
    tmp[off++] = 'X';
    tmp[off++] = '\r';
    tmp[off++] = '\n';
    memcpy(tmp + off, r.headers.data, r.headers.len);
    off += r.headers.len;
    if (!(r.headers.len >= 2 && r.headers.data[r.headers.len - 2] == '\r'
          && r.headers.data[r.headers.len - 1] == '\n')
        && !(r.headers.len >= 1 && r.headers.data[r.headers.len - 1] == '\n')) {
        if (off + 2 < cap) { tmp[off++] = '\r'; tmp[off++] = '\n'; }
    }
    if (off + 2 < cap) { tmp[off++] = '\r'; tmp[off++] = '\n'; }
    char out[1024];
    char nbuf[256];
    size_t nl = name.len < sizeof(nbuf) - 1 ? name.len : sizeof(nbuf) - 1;
    memcpy(nbuf, name.data, nl);
    nbuf[nl] = 0;
    int found = mako_proxy_find_header(tmp, off, nbuf, out, sizeof(out));
    free(tmp);
    if (!found) return mako_str_from_cstr("");
    return mako_str_from_cstr(out);
}

/* ---- Nonblocking connect ------------------------------------------------- */

static inline int64_t mako_tcp_connect_nb(MakoString host, int64_t port) {
    char hbuf[256];
    if (!host.data || host.len == 0 || host.len >= sizeof(hbuf)) return -1;
    if (port <= 0 || port > 65535) return -1;
    for (size_t i = 0; i < host.len; i++) {
        if (host.data[i] == 0 || host.data[i] == '\r' || host.data[i] == '\n') return -1;
    }
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
#if !defined(_WIN32)
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    u_long mode = 1UL;
    ioctlsocket((mako_sock_t)fd, FIONBIO, &mode);
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, hbuf, &addr.sin_addr) != 1) {
        mako_sock_close(fd);
        return -1;
    }
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc == 0) return (int64_t)fd;
#if defined(_WIN32)
    int e = WSAGetLastError();
    if (e == WSAEWOULDBLOCK || e == WSAEINPROGRESS) return (int64_t)fd;
#else
    if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK) return (int64_t)fd;
#endif
    mako_sock_close(fd);
    return -1;
}

/* 1 = connected, 0 = still pending / timeout, -1 = failed */
static inline int64_t mako_tcp_connect_check(int64_t fd) {
    if (fd < 0) return -1;
    int err = 0;
    socklen_t elen = sizeof(err);
    if (getsockopt((int)fd, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) != 0) return -1;
    if (err != 0) return -1;
    /* Writable means connect completed (success already checked via SO_ERROR). */
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET((int)fd, &wfds);
    struct timeval tv = {0, 0};
    int r = select((int)fd + 1, NULL, &wfds, NULL, &tv);
    if (r < 0) return -1;
    if (r == 0) return 0;
    return 1;
}

/* Poll until connect completes or timeout_ms. 1 ok, 0 timeout, -1 error. */
static inline int64_t mako_tcp_connect_wait(int64_t fd, int64_t timeout_ms) {
    if (fd < 0) return -1;
#if !defined(_WIN32)
    struct pollfd pfd;
    pfd.fd = (int)fd;
    pfd.events = POLLOUT;
    int pr = poll(&pfd, 1, (int)timeout_ms);
    if (pr < 0) return -1;
    if (pr == 0) return 0;
    int err = 0;
    socklen_t elen = sizeof(err);
    if (getsockopt((int)fd, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) != 0) return -1;
    if (err != 0) return -1;
    return 1;
#else
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET((mako_sock_t)fd, &wfds);
    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = (long)(timeout_ms / 1000);
        tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
        tvp = &tv;
    }
    int r = select((int)fd + 1, NULL, &wfds, NULL, tvp);
    if (r < 0) return -1;
    if (r == 0) return 0;
    int err = 0;
    socklen_t elen = sizeof(err);
    if (getsockopt((int)fd, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) != 0) return -1;
    return err == 0 ? 1 : -1;
#endif
}

/* ---- Efficient fd-to-fd copy --------------------------------------------- */

/* Copy up to max_bytes from src to dst. Returns bytes copied, or -1. */
static inline int64_t mako_tcp_fd_copy(int64_t src_fd, int64_t dst_fd, int64_t max_bytes) {
    if (src_fd < 0 || dst_fd < 0) return -1;
    if (max_bytes <= 0) max_bytes = 65536;
    int64_t total = 0;
#if defined(__linux__)
    /* splice via pipe when both are sockets */
    int pipefd[2];
    if (pipe(pipefd) == 0) {
        while (total < max_bytes) {
            size_t chunk = (size_t)(max_bytes - total);
            if (chunk > 65536) chunk = 65536;
            ssize_t n = splice((int)src_fd, NULL, pipefd[1], NULL, chunk, SPLICE_F_MOVE);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINVAL || errno == ENOSYS) {
                    /* fall through to userspace */
                    close(pipefd[0]);
                    close(pipefd[1]);
                    goto userspace;
                }
                close(pipefd[0]);
                close(pipefd[1]);
                return total > 0 ? total : -1;
            }
            if (n == 0) break;
            ssize_t left = n;
            while (left > 0) {
                ssize_t w = splice(pipefd[0], NULL, (int)dst_fd, NULL, (size_t)left, SPLICE_F_MOVE);
                if (w < 0) {
                    close(pipefd[0]);
                    close(pipefd[1]);
                    return total > 0 ? total : -1;
                }
                left -= w;
            }
            total += n;
        }
        close(pipefd[0]);
        close(pipefd[1]);
        return total;
    }
userspace:
#endif
    {
        char buf[65536];
        while (total < max_bytes) {
            size_t chunk = (size_t)(max_bytes - total);
            if (chunk > sizeof(buf)) chunk = sizeof(buf);
            ssize_t n = recv((int)src_fd, buf, chunk, 0);
            if (n < 0) {
#if !defined(_WIN32)
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
#endif
                return total > 0 ? total : -1;
            }
            if (n == 0) break;
            if (mako_proxy_write_all((int)dst_fd, buf, (size_t)n) < 0)
                return total > 0 ? total : -1;
            total += n;
        }
    }
    return total;
}

/* Linux sendfile-style name; portable alias of fd_copy for sockets. */
static inline int64_t mako_tcp_splice(int64_t src_fd, int64_t dst_fd, int64_t max_bytes) {
    return mako_tcp_fd_copy(src_fd, dst_fd, max_bytes);
}

/* Bidirectional pump until one side EOF or max_rounds. Returns total bytes moved. */
static inline int64_t mako_tcp_proxy_pump(
    int64_t fd_a, int64_t fd_b, int64_t timeout_ms, int64_t max_bytes
) {
    if (fd_a < 0 || fd_b < 0) return -1;
    if (max_bytes <= 0) max_bytes = 16 * 1024 * 1024;
    int64_t total = 0;
    for (;;) {
        int which = (int)mako_io_poll2(fd_a, fd_b, timeout_ms);
        if (which < 0) break;
        int64_t src = which == 0 ? fd_a : fd_b;
        int64_t dst = which == 0 ? fd_b : fd_a;
        int64_t n = mako_tcp_fd_copy(src, dst, 65536);
        if (n <= 0) break;
        total += n;
        if (total >= max_bytes) break;
    }
    return total;
}

/* ---- Socket tuning ------------------------------------------------------- */

static inline int64_t mako_tcp_set_recv_buf(int64_t fd, int64_t size) {
    if (fd < 0 || size <= 0) return 0;
    int v = (int)size;
    return setsockopt((int)fd, SOL_SOCKET, SO_RCVBUF, (const char *)&v, sizeof(v)) == 0 ? 1 : 0;
}

static inline int64_t mako_tcp_set_send_buf(int64_t fd, int64_t size) {
    if (fd < 0 || size <= 0) return 0;
    int v = (int)size;
    return setsockopt((int)fd, SOL_SOCKET, SO_SNDBUF, (const char *)&v, sizeof(v)) == 0 ? 1 : 0;
}

/* Listen with SO_REUSEPORT when available (multi-process accept load balance). */
static inline int64_t mako_tcp_listen_reuseport(
    MakoString host, int64_t port, int64_t backlog
) {
    if (!mako_net_init()) return -1;
    mako_sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == MAKO_INVALID_SOCK) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
#if defined(SO_REUSEPORT)
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const char *)&yes, sizeof(yes));
#endif
    struct sockaddr_in addr;
    if (!mako_bind_ipv4_addr(&addr, host, port)) {
        mako_sock_close(fd);
        return -1;
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        mako_sock_close(fd);
        return -1;
    }
    int bl = (backlog > 0 && backlog < 65536) ? (int)backlog : 4096;
    if (listen(fd, bl) < 0) {
        mako_sock_close(fd);
        return -1;
    }
    return (int64_t)fd;
}

/* Enable SO_REUSEPORT on an existing socket (must be before bind). */
static inline int64_t mako_tcp_reuseport(int64_t fd) {
    if (fd < 0) return 0;
#if defined(SO_REUSEPORT)
    int yes = 1;
    return setsockopt((int)fd, SOL_SOCKET, SO_REUSEPORT, (const char *)&yes, sizeof(yes)) == 0
               ? 1
               : 0;
#else
    (void)fd;
    return 0;
#endif
}

/* accept4 with SOCK_NONBLOCK | SOCK_CLOEXEC when available. */
static inline int64_t mako_tcp_accept4(int64_t listen_fd) {
    if (listen_fd < 0) return -1;
#if defined(__linux__) && defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
    int cfd = accept4((int)listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (cfd < 0) return -1;
    return (int64_t)cfd;
#else
    int cfd = accept((int)listen_fd, NULL, NULL);
    if (cfd < 0) return -1;
#if !defined(_WIN32)
    int flags = fcntl(cfd, F_GETFL, 0);
    if (flags >= 0) fcntl(cfd, F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(cfd, F_GETFD, 0);
    if (flags >= 0) fcntl(cfd, F_SETFD, flags | FD_CLOEXEC);
#endif
    return (int64_t)cfd;
#endif
}

/* Decode a standalone chunked body string (for tests / tools).
 * Empty / incomplete / malformed → "". */
static inline MakoString mako_http_decode_chunked(MakoString chunked_body) {
    if (!chunked_body.data || chunked_body.len == 0) return mako_str_from_cstr("");
    char *out = NULL;
    size_t out_len = 0;
    if (mako_proxy_decode_chunked(chunked_body.data, chunked_body.len, &out, &out_len) < 0)
        return mako_str_from_cstr("");
    return (MakoString){out, out_len};
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_PROXY_H */
