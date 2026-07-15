/* Mako built-in SIP library for proxies (and UAs / registrars).
 *
 * This is the platform SIP surface -- not an optional demo. Products like Madis
 * use these builtins on the data path: parse, Via/RR hop, Digest, TCP framing.
 *
 * RFCs: 3261 (core / proxy), 3581 (rport/received), 2617 Digest (no-qop MD5),
 * compact forms section 7.3.3. Transport: net udp/tcp/tls plus sip_udp and sip_tcp helpers.
 *
 * You own: transaction timers, dialog state, routing policy, rtpengine media.
 * Out of scope: SIPREC, WebRTC, full B2BUA engine.
 *
 * Pack: std/sip (thin re-exports; prefer pack names to avoid app sip_ name shadowing).
 * Hot path: call builtins directly; strings from sip_header are owned (malloc). */
#ifndef MAKO_SIP_H
#define MAKO_SIP_H

#include "mako_rt.h"
#include "mako_net.h"
#include <ctype.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- small utilities ---- */

/* Non-owned string from a C string literal / static buffer. Do NOT mako_str_free. */
#define MAKO_SIP_LIT(s) ((MakoString){(char *)(const char *)(s), sizeof(s) - 1})

static inline int mako_sip_ci_eq(const char *a, size_t al, const char *b, size_t bl) {
    if (al != bl) return 0;
    for (size_t i = 0; i < al; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return 1;
}

static inline int mako_sip_ci_eq_cstr(const char *a, size_t al, const char *b) {
    return mako_sip_ci_eq(a, al, b, strlen(b));
}

static inline MakoString mako_sip_slice_to_str(const char *p, size_t n) {
    if (!p || n == 0) return mako_str_from_cstr("");
    char *d = (char *)malloc(n + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, p, n);
    d[n] = 0;
    return (MakoString){d, n};
}

static inline const char *mako_sip_find_crlf(const char *p, const char *end) {
    for (const char *q = p; q + 1 < end; q++) {
        if (q[0] == '\r' && q[1] == '\n') return q;
    }
    return NULL;
}

/* First line end and header/body split. Returns body start or NULL. */
static inline const char *mako_sip_body_start(const char *msg, size_t len, size_t *hdr_end_out) {
    if (!msg || len < 4) return NULL;
    const char *end = msg + len;
    const char *p = msg;
    for (;;) {
        const char *crlf = mako_sip_find_crlf(p, end);
        if (!crlf) return NULL;
        if (crlf == p) {
            /* empty line */
            if (hdr_end_out) *hdr_end_out = (size_t)(p - msg);
            return crlf + 2;
        }
        p = crlf + 2;
        if (p >= end) return NULL;
    }
}

static inline int64_t mako_sip_is_request(MakoString msg) {
    if (!msg.data || msg.len < 8) return 0;
    /* Response starts with SIP/ */
    if (msg.len >= 4 && msg.data[0] == 'S' && msg.data[1] == 'I' &&
        msg.data[2] == 'P' && msg.data[3] == '/')
        return 0;
    /* Method is ALPHA */
    if (!isalpha((unsigned char)msg.data[0])) return 0;
    return 1;
}

static inline int64_t mako_sip_is_response(MakoString msg) {
    if (!msg.data || msg.len < 8) return 0;
    return (msg.data[0] == 'S' && msg.data[1] == 'I' && msg.data[2] == 'P' &&
            msg.data[3] == '/')
               ? 1
               : 0;
}

static inline int64_t mako_sip_ok(MakoString msg) {
    if (!msg.data || msg.len < 12) return 0;
    size_t he = 0;
    const char *body = mako_sip_body_start(msg.data, msg.len, &he);
    if (!body) return 0;
    if (mako_sip_is_request(msg) || mako_sip_is_response(msg)) return 1;
    return 0;
}

/* ---- start-line parse ---- */

static inline MakoString mako_sip_method(MakoString msg) {
    if (!mako_sip_is_request(msg)) return mako_str_from_cstr("");
    const char *p = msg.data;
    const char *end = msg.data + msg.len;
    const char *sp = p;
    while (sp < end && *sp != ' ' && *sp != '\r' && *sp != '\n') sp++;
    return mako_sip_slice_to_str(p, (size_t)(sp - p));
}

static inline MakoString mako_sip_request_uri(MakoString msg) {
    if (!mako_sip_is_request(msg)) return mako_str_from_cstr("");
    const char *p = msg.data;
    const char *end = msg.data + msg.len;
    while (p < end && *p != ' ') p++;
    if (p >= end || *p != ' ') return mako_str_from_cstr("");
    p++;
    const char *uri = p;
    while (p < end && *p != ' ' && *p != '\r' && *p != '\n') p++;
    return mako_sip_slice_to_str(uri, (size_t)(p - uri));
}

static inline int64_t mako_sip_status_code(MakoString msg) {
    if (!mako_sip_is_response(msg)) return 0;
    /* SIP/2.0 200 OK */
    const char *p = msg.data;
    const char *end = msg.data + msg.len;
    while (p < end && *p != ' ') p++;
    if (p >= end) return 0;
    p++;
    if (p + 3 > end) return 0;
    int code = 0;
    for (int i = 0; i < 3; i++) {
        if (p[i] < '0' || p[i] > '9') return 0;
        code = code * 10 + (p[i] - '0');
    }
    return code;
}

static inline MakoString mako_sip_reason(MakoString msg) {
    if (!mako_sip_is_response(msg)) return mako_str_from_cstr("");
    const char *p = msg.data;
    const char *end = msg.data + msg.len;
    /* skip version */
    while (p < end && *p != ' ') p++;
    if (p >= end) return mako_str_from_cstr("");
    p++;
    /* skip code */
    while (p < end && *p != ' ' && *p != '\r') p++;
    if (p < end && *p == ' ') p++;
    const char *r = p;
    while (p < end && *p != '\r' && *p != '\n') p++;
    return mako_sip_slice_to_str(r, (size_t)(p - r));
}

static inline MakoString mako_sip_version(MakoString msg) {
    if (!msg.data || msg.len < 7) return mako_str_from_cstr("");
    if (mako_sip_is_response(msg)) {
        const char *p = msg.data;
        const char *end = msg.data + msg.len;
        const char *sp = p;
        while (sp < end && *sp != ' ') sp++;
        return mako_sip_slice_to_str(p, (size_t)(sp - p));
    }
    if (mako_sip_is_request(msg)) {
        /* last token on start line */
        const char *end = msg.data + msg.len;
        const char *line = msg.data;
        const char *crlf = mako_sip_find_crlf(line, end);
        if (!crlf) return mako_str_from_cstr("");
        const char *p = crlf;
        while (p > line && p[-1] != ' ') p--;
        return mako_sip_slice_to_str(p, (size_t)(crlf - p));
    }
    return mako_str_from_cstr("");
}

/* ---- headers ---- */

static inline int mako_sip_header_name_match(
    const char *line, size_t line_len, const char *name, size_t nlen
) {
    if (!line || !name || nlen == 0 || line_len < nlen + 1) return 0;
    if (!mako_sip_ci_eq(line, nlen, name, nlen)) return 0;
    size_t i = nlen;
    while (i < line_len && (line[i] == ' ' || line[i] == '\t')) i++;
    return i < line_len && line[i] == ':';
}

/* Compact form aliases (RFC 3261 §7.3.3 + §20; Refer-To RFC 3515). Bidirectional. */
static inline int mako_sip_header_alias_pair(
    const char *name, size_t nlen, const char **a, size_t *alen, const char **b, size_t *blen
) {
    /* long -> compact */
    struct {
        const char *longn;
        const char *shortn;
    } pairs[] = {
        {"Call-ID", "i"},
        {"Via", "v"},
        {"From", "f"},
        {"To", "t"},
        {"Contact", "m"},
        {"Content-Length", "l"},
        {"Content-Type", "c"},
        {"Content-Encoding", "e"},
        {"Subject", "s"},
        {"Supported", "k"},
        {"Refer-To", "r"},
        {"Allow-Events", "u"},
        {"Event", "o"},
        {NULL, NULL},
    };
    for (int i = 0; pairs[i].longn; i++) {
        size_t ll = strlen(pairs[i].longn);
        size_t sl = strlen(pairs[i].shortn);
        if (mako_sip_ci_eq(name, nlen, pairs[i].longn, ll)) {
            *a = pairs[i].longn;
            *alen = ll;
            *b = pairs[i].shortn;
            *blen = sl;
            return 1;
        }
        if (mako_sip_ci_eq(name, nlen, pairs[i].shortn, sl)) {
            *a = pairs[i].shortn;
            *alen = sl;
            *b = pairs[i].longn;
            *blen = ll;
            return 1;
        }
    }
    return 0;
}

/* Match header line name against `name` or its compact/long form. */
static inline int mako_sip_header_line_is(
    const char *line, size_t line_len, const char *name, size_t nlen
) {
    if (mako_sip_header_name_match(line, line_len, name, nlen)) return 1;
    const char *a = NULL, *b = NULL;
    size_t al = 0, bl = 0;
    if (mako_sip_header_alias_pair(name, nlen, &a, &al, &b, &bl)) {
        if (mako_sip_header_name_match(line, line_len, a, al)) return 1;
        if (mako_sip_header_name_match(line, line_len, b, bl)) return 1;
    }
    return 0;
}

/* Value of nth occurrence (0-based) of header `name` (compact forms accepted).
 * Returns an owned MakoString (malloc); free with mako_str_free / scope drop.
 * Parse is length-bounded (msg need not be NUL-terminated). */
static inline MakoString mako_sip_header_n(MakoString msg, MakoString name, int64_t idx) {
    if (!msg.data || !name.data || name.len == 0 || idx < 0)
        return mako_str_from_cstr("");
    size_t he = 0;
    if (!mako_sip_body_start(msg.data, msg.len, &he)) return mako_str_from_cstr("");
    const char *p = msg.data;
    const char *hend = msg.data + he;
    /* skip start line */
    const char *crlf = mako_sip_find_crlf(p, hend);
    if (!crlf) return mako_str_from_cstr("");
    p = crlf + 2;
    int64_t seen = 0;
    while (p < hend) {
        if (p[0] == '\r' && p + 1 < hend && p[1] == '\n') break;
        const char *le = mako_sip_find_crlf(p, hend);
        if (!le) break;
        size_t llen = (size_t)(le - p);
        /* fold continuation: lines starting with SP/HTAB are continuations — skip as start */
        if (llen > 0 && (p[0] == ' ' || p[0] == '\t')) {
            p = le + 2;
            continue;
        }
        if (mako_sip_header_line_is(p, llen, name.data, name.len)) {
            if (seen == idx) {
                const char *colon = p;
                while (colon < le && *colon != ':') colon++;
                if (colon >= le) return mako_str_from_cstr("");
                colon++;
                while (colon < le && (*colon == ' ' || *colon == '\t')) colon++;
                /* include folded lines */
                const char *vend = le;
                const char *np = le + 2;
                while (np < hend && (*np == ' ' || *np == '\t')) {
                    const char *nle = mako_sip_find_crlf(np, hend);
                    if (!nle) break;
                    vend = nle;
                    np = nle + 2;
                }
                /* build value, normalize LWS folds to single space */
                size_t raw = (size_t)(vend - colon);
                char *d = (char *)malloc(raw + 1);
                if (!d) return mako_str_from_cstr("");
                size_t o = 0;
                for (const char *q = colon; q < vend; q++) {
                    if (*q == '\r') continue;
                    if (*q == '\n') {
                        if (o > 0 && d[o - 1] != ' ') d[o++] = ' ';
                        continue;
                    }
                    d[o++] = *q;
                }
                while (o > 0 && (d[o - 1] == ' ' || d[o - 1] == '\t')) o--;
                d[o] = 0;
                return (MakoString){d, o};
            }
            seen++;
        }
        p = le + 2;
    }
    return mako_str_from_cstr("");
}

static inline MakoString mako_sip_header(MakoString msg, MakoString name) {
    return mako_sip_header_n(msg, name, 0);
}

static inline int64_t mako_sip_header_count(MakoString msg, MakoString name) {
    if (!msg.data || !name.data) return 0;
    size_t he = 0;
    if (!mako_sip_body_start(msg.data, msg.len, &he)) return 0;
    const char *p = msg.data;
    const char *hend = msg.data + he;
    const char *crlf = mako_sip_find_crlf(p, hend);
    if (!crlf) return 0;
    p = crlf + 2;
    int64_t n = 0;
    while (p < hend) {
        if (p[0] == '\r' && p + 1 < hend && p[1] == '\n') break;
        const char *le = mako_sip_find_crlf(p, hend);
        if (!le) break;
        size_t llen = (size_t)(le - p);
        if (llen > 0 && (p[0] == ' ' || p[0] == '\t')) {
            p = le + 2;
            continue;
        }
        if (mako_sip_header_line_is(p, llen, name.data, name.len)) n++;
        p = le + 2;
    }
    return n;
}

static inline MakoString mako_sip_body(MakoString msg) {
    if (!msg.data) return mako_str_from_cstr("");
    size_t he = 0;
    const char *b = mako_sip_body_start(msg.data, msg.len, &he);
    if (!b) return mako_str_from_cstr("");
    size_t blen = msg.len - (size_t)(b - msg.data);
    return mako_sip_slice_to_str(b, blen);
}

static inline int64_t mako_sip_content_length(MakoString msg) {
    /* Compact form `l` resolved via alias in sip_header. Name is non-owned lit. */
    MakoString cl = mako_sip_header(msg, MAKO_SIP_LIT("Content-Length"));
    if (!cl.data || cl.len == 0) {
        mako_str_free(cl);
        return 0;
    }
    int64_t v = 0;
    for (size_t i = 0; i < cl.len; i++) {
        if (cl.data[i] < '0' || cl.data[i] > '9') break;
        v = v * 10 + (cl.data[i] - '0');
    }
    mako_str_free(cl);
    return v;
}

/* Content-Length from a raw header region without allocating a full msg copy. */
static inline int64_t mako_sip_content_length_raw(const char *hdrs, size_t hlen) {
    MakoString h = {(char *)(uintptr_t)hdrs, hlen};
    return mako_sip_content_length(h);
}

/* True if buffer holds a complete SIP message (headers + Content-Length body). */
static inline int64_t mako_sip_msg_complete(MakoString buf) {
    if (!buf.data || buf.len < 4) return 0;
    size_t he = 0;
    const char *body = mako_sip_body_start(buf.data, buf.len, &he);
    if (!body) return 0;
    size_t have = buf.len - (size_t)(body - buf.data);
    int64_t need = mako_sip_content_length_raw(buf.data, (size_t)(body - buf.data));
    return (int64_t)have >= need ? 1 : 0;
}

/* Bytes still needed for a complete message; 0 if complete; -1 if not parseable yet. */
static inline int64_t mako_sip_msg_needed(MakoString buf) {
    if (!buf.data || buf.len < 4) return -1;
    size_t he = 0;
    const char *body = mako_sip_body_start(buf.data, buf.len, &he);
    if (!body) return -1;
    size_t have = buf.len - (size_t)(body - buf.data);
    int64_t need = mako_sip_content_length_raw(buf.data, (size_t)(body - buf.data));
    if ((int64_t)have >= need) return 0;
    return need - (int64_t)have;
}

/* Total byte length of the first complete SIP message in buf, or 0 if incomplete.
 * For TCP/TLS framing: while (n = sip_first_message_len(buf)) { use buf[0..n); rest = buf[n..] }. */
static inline int64_t mako_sip_first_message_len(MakoString buf) {
    if (!buf.data || buf.len < 4) return 0;
    size_t he = 0;
    const char *body = mako_sip_body_start(buf.data, buf.len, &he);
    if (!body) return 0;
    int64_t need = mako_sip_content_length_raw(buf.data, (size_t)(body - buf.data));
    if (need < 0) need = 0;
    int64_t total = (int64_t)(body - buf.data) + need;
    if (total <= 0 || (size_t)total > buf.len) return 0;
    return total;
}

/* ---- message builders ---- */

static inline MakoString mako_sip_header_line(MakoString name, MakoString value) {
    size_t n = name.len + value.len + 8;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w = snprintf(
        d, n, "%.*s: %.*s\r\n",
        (int)name.len, name.data ? name.data : "",
        (int)value.len, value.data ? value.data : ""
    );
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

static inline MakoString mako_sip_headers_append(
    MakoString headers, MakoString name, MakoString value
) {
    MakoString line = mako_sip_header_line(name, value);
    size_t n = headers.len + line.len + 1;
    char *d = (char *)malloc(n);
    if (!d) {
        mako_str_free(line);
        return mako_str_from_cstr("");
    }
    size_t o = 0;
    if (headers.data && headers.len) {
        memcpy(d, headers.data, headers.len);
        o = headers.len;
    }
    memcpy(d + o, line.data, line.len);
    o += line.len;
    d[o] = 0;
    mako_str_free(line);
    return (MakoString){d, o};
}

/* Ensure Content-Length matches body; rebuild message. */
static inline MakoString mako_sip_request(
    MakoString method, MakoString uri, MakoString headers, MakoString body
) {
    size_t blen = body.data ? body.len : 0;
    char clv[32];
    snprintf(clv, sizeof(clv), "%zu", blen);
    MakoString h = headers;
    /* append Content-Length (caller may also set; we always append final) */
    MakoString h2 = mako_sip_headers_append(
        h, mako_str_from_cstr("Content-Length"), mako_str_from_cstr(clv)
    );
    size_t n = method.len + uri.len + h2.len + blen + 32;
    char *d = (char *)malloc(n);
    if (!d) {
        mako_str_free(h2);
        return mako_str_from_cstr("");
    }
    int w = snprintf(
        d, n, "%.*s %.*s SIP/2.0\r\n%.*s\r\n",
        (int)method.len, method.data ? method.data : "INVITE",
        (int)uri.len, uri.data ? uri.data : "sip:invalid",
        (int)h2.len, h2.data ? h2.data : ""
    );
    mako_str_free(h2);
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    if (blen > 0 && body.data) {
        if ((size_t)w + blen + 1 > n) {
            char *nd = (char *)realloc(d, (size_t)w + blen + 1);
            if (!nd) {
                free(d);
                return mako_str_from_cstr("");
            }
            d = nd;
        }
        memcpy(d + w, body.data, blen);
        w += (int)blen;
        d[w] = 0;
    }
    return (MakoString){d, (size_t)w};
}

static inline MakoString mako_sip_response(
    int64_t code, MakoString reason, MakoString headers, MakoString body
) {
    size_t blen = body.data ? body.len : 0;
    char clv[32];
    snprintf(clv, sizeof(clv), "%zu", blen);
    MakoString h2 = mako_sip_headers_append(
        headers, mako_str_from_cstr("Content-Length"), mako_str_from_cstr(clv)
    );
    const char *rs = reason.data && reason.len ? reason.data : "OK";
    size_t rlen = reason.data && reason.len ? reason.len : 2;
    size_t n = h2.len + blen + rlen + 48;
    char *d = (char *)malloc(n);
    if (!d) {
        mako_str_free(h2);
        return mako_str_from_cstr("");
    }
    int w = snprintf(
        d, n, "SIP/2.0 %lld %.*s\r\n%.*s\r\n",
        (long long)code, (int)rlen, rs,
        (int)h2.len, h2.data ? h2.data : ""
    );
    mako_str_free(h2);
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    if (blen > 0 && body.data) {
        if ((size_t)w + blen + 1 > n) {
            char *nd = (char *)realloc(d, (size_t)w + blen + 1);
            if (!nd) {
                free(d);
                return mako_str_from_cstr("");
            }
            d = nd;
        }
        memcpy(d + w, body.data, blen);
        w += (int)blen;
        d[w] = 0;
    }
    return (MakoString){d, (size_t)w};
}

/* RFC 3261 §8.1.1.7 / §16.6: branch must start with magic cookie "z9hG4bK". */
static inline int mako_sip_branch_has_cookie(const char *b, size_t n) {
    return n >= 7 && mako_sip_ci_eq(b, 7, "z9hG4bK", 7);
}

/* Uppercase protocol transport token for Via (UDP/TCP/TLS/SCTP). */
static inline void mako_sip_transport_upper(char *dst, size_t cap, MakoString tr) {
    if (!dst || cap == 0) return;
    const char *src = (tr.data && tr.len) ? tr.data : "UDP";
    size_t n = (tr.data && tr.len) ? tr.len : 3;
    if (n >= cap) n = cap - 1;
    for (size_t i = 0; i < n; i++) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        dst[i] = c;
    }
    dst[n] = 0;
}

/* Format sent-by host: IPv6 literals need brackets (RFC 3261 §25 / §20.42). */
static inline int mako_sip_host_needs_brackets(const char *h, size_t n) {
    if (!h || n == 0) return 0;
    if (h[0] == '[') return 0; /* already bracketed */
    for (size_t i = 0; i < n; i++)
        if (h[i] == ':') return 1;
    return 0;
}

/* Common header value builders (value only, without name:).
 * Via: SIP/2.0/TRANSPORT sent-by;branch=z9hG4bK…  (RFC 3261 §20.42). */
static inline MakoString mako_sip_via_value(
    MakoString transport, MakoString host, int64_t port, MakoString branch
) {
    char tr[16];
    mako_sip_transport_upper(tr, sizeof(tr), transport);
    const char *h = host.data && host.len ? host.data : "127.0.0.1";
    size_t hlen = host.data && host.len ? host.len : 9;
    int br_h = mako_sip_host_needs_brackets(h, hlen);
    /* Ensure magic cookie on branch */
    char brbuf[80];
    const char *br;
    size_t brlen;
    if (branch.data && branch.len > 0 && mako_sip_branch_has_cookie(branch.data, branch.len)) {
        br = branch.data;
        brlen = branch.len;
    } else if (branch.data && branch.len > 0) {
        snprintf(brbuf, sizeof(brbuf), "z9hG4bK%.*s", (int)branch.len, branch.data);
        br = brbuf;
        brlen = strlen(brbuf);
    } else {
        br = "z9hG4bKmako";
        brlen = 11;
    }
    size_t n = hlen + brlen + 80;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w;
    if (port > 0) {
        if (br_h) {
            w = snprintf(
                d, n, "SIP/2.0/%s [%.*s]:%lld;branch=%.*s",
                tr, (int)hlen, h, (long long)port, (int)brlen, br
            );
        } else {
            w = snprintf(
                d, n, "SIP/2.0/%s %.*s:%lld;branch=%.*s",
                tr, (int)hlen, h, (long long)port, (int)brlen, br
            );
        }
    } else {
        if (br_h) {
            w = snprintf(
                d, n, "SIP/2.0/%s [%.*s];branch=%.*s",
                tr, (int)hlen, h, (int)brlen, br
            );
        } else {
            w = snprintf(
                d, n, "SIP/2.0/%s %.*s;branch=%.*s",
                tr, (int)hlen, h, (int)brlen, br
            );
        }
    }
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

/* Strip existing ;received=… and ;rport[=…] params (RFC 3581 rewrite). */
static inline size_t mako_sip_via_strip_nat_params(const char *in, size_t inlen, char *out, size_t cap) {
    if (!in || !out || cap == 0) return 0;
    size_t o = 0;
    size_t i = 0;
    while (i < inlen && o + 1 < cap) {
        if (in[i] == ';') {
            size_t j = i + 1;
            while (j < inlen && in[j] != ';' && in[j] != ' ' && in[j] != '\t' && in[j] != ',') j++;
            size_t plen = j - (i + 1);
            const char *p = in + i + 1;
            int is_recv = plen >= 9 && mako_sip_ci_eq(p, 9, "received=", 9);
            int is_rport = (plen == 5 && mako_sip_ci_eq(p, 5, "rport", 5))
                || (plen > 6 && mako_sip_ci_eq(p, 6, "rport=", 6));
            if (is_recv || is_rport) {
                i = j;
                continue;
            }
        }
        out[o++] = in[i++];
    }
    out[o] = 0;
    return o;
}

/* Via value with RFC 3581 NAT params: ;rport[=N] and optional ;received=IP.
 * rport < 0 → omit; rport == 0 → bare ;rport (UAC request); rport > 0 → ;rport=N (response).
 * Param order after branch: ;received=… then ;rport… (common 3581 practice). */
static inline MakoString mako_sip_via_value_nat(
    MakoString transport,
    MakoString host,
    int64_t port,
    MakoString branch,
    MakoString received,
    int64_t rport
) {
    MakoString base = mako_sip_via_value(transport, host, port, branch);
    size_t extra = (received.data ? received.len : 0) + 48;
    size_t n = base.len + extra + 1;
    char *d = (char *)malloc(n);
    if (!d) {
        mako_str_free(base);
        return mako_str_from_cstr("");
    }
    size_t o = 0;
    if (base.data && base.len) {
        memcpy(d, base.data, base.len);
        o = base.len;
    }
    mako_str_free(base);
    /* RFC 3581: server adds received then rport value on responses */
    if (received.data && received.len > 0) {
        int w = snprintf(
            d + o, n - o, ";received=%.*s",
            (int)received.len, received.data
        );
        if (w > 0) o += (size_t)w;
    }
    if (rport == 0) {
        if (o + 6 < n) {
            memcpy(d + o, ";rport", 6);
            o += 6;
        }
    } else if (rport > 0) {
        int w = snprintf(d + o, n - o, ";rport=%lld", (long long)rport);
        if (w > 0) o += (size_t)w;
    }
    d[o] = 0;
    return (MakoString){d, o};
}

/* RFC 3581: rewrite Via for response — set/replace ;received= and ;rport=src_port.
 * host = source IP; rport > 0 → ;rport=N; rport == 0 → bare ;rport; rport < 0 → leave rport out. */
static inline MakoString mako_sip_via_add_received(MakoString via, MakoString host, int64_t rport) {
    if (!via.data) return mako_str_from_cstr("");
    char stripped[1024];
    size_t sl = mako_sip_via_strip_nat_params(via.data, via.len, stripped, sizeof(stripped));
    size_t n = sl + (host.data ? host.len : 0) + 48;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    size_t o = 0;
    if (sl) {
        memcpy(d, stripped, sl);
        o = sl;
    }
    if (host.data && host.len) {
        int w = snprintf(d + o, n - o, ";received=%.*s", (int)host.len, host.data);
        if (w > 0) o += (size_t)w;
    }
    if (rport > 0) {
        int w = snprintf(d + o, n - o, ";rport=%lld", (long long)rport);
        if (w > 0) o += (size_t)w;
    } else if (rport == 0) {
        if (o + 6 < n) {
            memcpy(d + o, ";rport", 6);
            o += 6;
        }
    }
    d[o] = 0;
    return (MakoString){d, o};
}

/* Top Via sent-by host (after transport). Supports IPv6 [addr]. */
static inline MakoString mako_sip_via_host(MakoString via) {
    if (!via.data || via.len < 8) return mako_str_from_cstr("");
    size_t i = 0;
    while (i < via.len && via.data[i] != ' ') i++;
    if (i >= via.len) return mako_str_from_cstr("");
    i++;
    while (i < via.len && (via.data[i] == ' ' || via.data[i] == '\t')) i++;
    if (i >= via.len) return mako_str_from_cstr("");
    if (via.data[i] == '[') {
        size_t start = i + 1;
        size_t j = start;
        while (j < via.len && via.data[j] != ']') j++;
        if (j >= via.len) return mako_str_from_cstr("");
        return mako_sip_slice_to_str(via.data + start, j - start);
    }
    size_t start = i;
    while (i < via.len && via.data[i] != ':' && via.data[i] != ';' && via.data[i] != ' ' &&
           via.data[i] != '\t')
        i++;
    return mako_sip_slice_to_str(via.data + start, i - start);
}

/* Top Via sent-by port, or 0 if absent (caller may default 5060 per RFC 3261). */
static inline int64_t mako_sip_via_port(MakoString via) {
    if (!via.data || via.len < 8) return 0;
    size_t i = 0;
    while (i < via.len && via.data[i] != ' ') i++;
    if (i >= via.len) return 0;
    i++;
    while (i < via.len && (via.data[i] == ' ' || via.data[i] == '\t')) i++;
    if (i < via.len && via.data[i] == '[') {
        while (i < via.len && via.data[i] != ']') i++;
        if (i < via.len) i++; /* skip ] */
    } else {
        while (i < via.len && via.data[i] != ':' && via.data[i] != ';' && via.data[i] != ' ') i++;
    }
    if (i >= via.len || via.data[i] != ':') return 0;
    i++;
    int64_t p = 0;
    while (i < via.len && via.data[i] >= '0' && via.data[i] <= '9') {
        p = p * 10 + (via.data[i] - '0');
        i++;
    }
    return p;
}

/* Record-Route value (RFC 3261 §16.6 / §20.30): name-addr with ;lr for loose routing.
 * <sip:host[:port][;transport=x];lr> — IPv6 host bracketed. */
static inline MakoString mako_sip_record_route(
    MakoString host, int64_t port, MakoString transport
) {
    const char *h = host.data && host.len ? host.data : "127.0.0.1";
    size_t hlen = host.data && host.len ? host.len : 9;
    int br = mako_sip_host_needs_brackets(h, hlen);
    /* Lowercase transport for URI param (RFC 3261 transport = "udp"/"tcp"/…) */
    char tr[16];
    size_t tlen = 0;
    if (transport.data && transport.len > 0) {
        tlen = transport.len < 15 ? transport.len : 15;
        for (size_t i = 0; i < tlen; i++) {
            char c = transport.data[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            tr[i] = c;
        }
        tr[tlen] = 0;
    }
    size_t n = hlen + 80;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w;
    if (tlen > 0 && port > 0) {
        w = br ? snprintf(
                     d, n, "<sip:[%.*s]:%lld;transport=%s;lr>",
                     (int)hlen, h, (long long)port, tr
                 )
               : snprintf(
                     d, n, "<sip:%.*s:%lld;transport=%s;lr>",
                     (int)hlen, h, (long long)port, tr
                 );
    } else if (tlen > 0) {
        w = br ? snprintf(d, n, "<sip:[%.*s];transport=%s;lr>", (int)hlen, h, tr)
               : snprintf(d, n, "<sip:%.*s;transport=%s;lr>", (int)hlen, h, tr);
    } else if (port > 0) {
        w = br ? snprintf(d, n, "<sip:[%.*s]:%lld;lr>", (int)hlen, h, (long long)port)
               : snprintf(d, n, "<sip:%.*s:%lld;lr>", (int)hlen, h, (long long)port);
    } else {
        w = br ? snprintf(d, n, "<sip:[%.*s];lr>", (int)hlen, h)
               : snprintf(d, n, "<sip:%.*s;lr>", (int)hlen, h);
    }
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

/* Prepend a header after the start line (proxy hop insert). Owned result. */
static inline MakoString mako_sip_prepend_header(
    MakoString msg, MakoString name, MakoString value
) {
    if (!msg.data || msg.len == 0) return mako_str_from_cstr("");
    const char *end = msg.data + msg.len;
    const char *crlf = mako_sip_find_crlf(msg.data, end);
    if (!crlf) return mako_sip_slice_to_str(msg.data, msg.len);
    size_t start_len = (size_t)(crlf + 2 - msg.data);
    MakoString line = mako_sip_header_line(name, value);
    size_t n = msg.len + line.len + 1;
    char *d = (char *)malloc(n);
    if (!d) {
        mako_str_free(line);
        return mako_str_from_cstr("");
    }
    memcpy(d, msg.data, start_len);
    memcpy(d + start_len, line.data ? line.data : "", line.len);
    memcpy(d + start_len + line.len, msg.data + start_len, msg.len - start_len);
    d[msg.len + line.len] = 0;
    mako_str_free(line);
    return (MakoString){d, msg.len + line.len};
}

static inline MakoString mako_sip_insert_via(MakoString msg, MakoString via_value) {
    return mako_sip_prepend_header(msg, MAKO_SIP_LIT("Via"), via_value);
}

/* Remove top Via (or compact v) header — RFC 3261 §16.7 response path. */
static inline MakoString mako_sip_strip_via(MakoString msg) {
    if (!msg.data || msg.len == 0) return mako_str_from_cstr("");
    size_t he = 0;
    const char *body = mako_sip_body_start(msg.data, msg.len, &he);
    if (!body) return mako_sip_slice_to_str(msg.data, msg.len);
    const char *hend = msg.data + he;
    const char *crlf = mako_sip_find_crlf(msg.data, hend);
    if (!crlf) return mako_sip_slice_to_str(msg.data, msg.len);
    const char *p = crlf + 2;
    const char *skip_start = NULL;
    const char *skip_end = NULL;
    while (p < hend) {
        if (p[0] == '\r' && p + 1 < hend && p[1] == '\n') break;
        const char *le = mako_sip_find_crlf(p, hend);
        if (!le) break;
        size_t llen = (size_t)(le - p);
        if (llen > 0 && (p[0] == ' ' || p[0] == '\t')) {
            p = le + 2;
            continue;
        }
        if (mako_sip_header_line_is(p, llen, "Via", 3)) {
            skip_start = p;
            skip_end = le + 2;
            /* include folded continuations */
            while (skip_end < hend && (*skip_end == ' ' || *skip_end == '\t')) {
                const char *nle = mako_sip_find_crlf(skip_end, hend);
                if (!nle) break;
                skip_end = nle + 2;
            }
            break;
        }
        p = le + 2;
    }
    if (!skip_start) return mako_sip_slice_to_str(msg.data, msg.len);
    size_t keep1 = (size_t)(skip_start - msg.data);
    size_t keep2 = msg.len - (size_t)(skip_end - msg.data);
    char *d = (char *)malloc(keep1 + keep2 + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, msg.data, keep1);
    memcpy(d + keep1, skip_end, keep2);
    d[keep1 + keep2] = 0;
    return (MakoString){d, keep1 + keep2};
}

static inline MakoString mako_sip_addr_value(
    MakoString display, MakoString uri, MakoString tag
) {
    size_t n = display.len + uri.len + tag.len + 16;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w;
    if (display.data && display.len > 0) {
        if (tag.data && tag.len > 0) {
            w = snprintf(
                d, n, "\"%.*s\" <%.*s>;tag=%.*s",
                (int)display.len, display.data,
                (int)uri.len, uri.data ? uri.data : "",
                (int)tag.len, tag.data
            );
        } else {
            w = snprintf(
                d, n, "\"%.*s\" <%.*s>",
                (int)display.len, display.data,
                (int)uri.len, uri.data ? uri.data : ""
            );
        }
    } else {
        if (tag.data && tag.len > 0) {
            w = snprintf(
                d, n, "<%.*s>;tag=%.*s",
                (int)uri.len, uri.data ? uri.data : "",
                (int)tag.len, tag.data
            );
        } else {
            w = snprintf(
                d, n, "<%.*s>",
                (int)uri.len, uri.data ? uri.data : ""
            );
        }
    }
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

static inline MakoString mako_sip_from_value(MakoString display, MakoString uri, MakoString tag) {
    return mako_sip_addr_value(display, uri, tag);
}

static inline MakoString mako_sip_to_value(MakoString display, MakoString uri, MakoString tag) {
    return mako_sip_addr_value(display, uri, tag);
}

static inline MakoString mako_sip_contact_value(MakoString uri) {
    size_t n = uri.len + 8;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w = snprintf(d, n, "<%.*s>", (int)uri.len, uri.data ? uri.data : "");
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

static inline MakoString mako_sip_cseq_value(int64_t seq, MakoString method) {
    size_t n = method.len + 24;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w = snprintf(
        d, n, "%lld %.*s",
        (long long)seq, (int)method.len, method.data ? method.data : "INVITE"
    );
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

/* Extract tag= from From/To header value. */
static inline MakoString mako_sip_addr_tag(MakoString addr) {
    if (!addr.data) return mako_str_from_cstr("");
    for (size_t i = 0; i + 4 < addr.len; i++) {
        if ((addr.data[i] == 't' || addr.data[i] == 'T') &&
            (addr.data[i + 1] == 'a' || addr.data[i + 1] == 'A') &&
            (addr.data[i + 2] == 'g' || addr.data[i + 2] == 'G') &&
            addr.data[i + 3] == '=') {
            size_t j = i + 4;
            size_t start = j;
            while (j < addr.len && addr.data[j] != ';' && addr.data[j] != ' ' &&
                   addr.data[j] != '\t' && addr.data[j] != ',')
                j++;
            return mako_sip_slice_to_str(addr.data + start, j - start);
        }
    }
    return mako_str_from_cstr("");
}

/* branch= from Via value. */
static inline MakoString mako_sip_via_branch(MakoString via) {
    if (!via.data) return mako_str_from_cstr("");
    const char *key = "branch=";
    for (size_t i = 0; i + 7 < via.len; i++) {
        if (mako_sip_ci_eq(via.data + i, 7, key, 7)) {
            size_t j = i + 7;
            size_t start = j;
            while (j < via.len && via.data[j] != ';' && via.data[j] != ' ' &&
                   via.data[j] != '\t' && via.data[j] != ',')
                j++;
            return mako_sip_slice_to_str(via.data + start, j - start);
        }
    }
    return mako_str_from_cstr("");
}

/* ---- ID generation ---- */

static inline uint64_t mako_sip_rng_mix(void) {
    uint64_t x = (uint64_t)mako_mono_ns();
    x ^= (uint64_t)(uintptr_t)&x;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    static uint64_t s = 0x9e3779b97f4a7c15ULL;
    s = s * 6364136223846793005ULL + x + 1;
    return s;
}

static inline void mako_sip_hex_u64(char *out, uint64_t v) {
    static const char *hex = "0123456789abcdef";
    for (int i = 15; i >= 0; i--) {
        out[i] = hex[v & 0xf];
        v >>= 4;
    }
    out[16] = 0;
}

/* RFC 3261 magic cookie branch z9hG4bK... */
static inline MakoString mako_sip_branch(void) {
    char buf[40];
    char hx[17];
    mako_sip_hex_u64(hx, mako_sip_rng_mix());
    snprintf(buf, sizeof(buf), "z9hG4bK%s", hx);
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_sip_tag(void) {
    char hx[17];
    mako_sip_hex_u64(hx, mako_sip_rng_mix());
    return mako_str_from_cstr(hx);
}

static inline MakoString mako_sip_call_id_new(MakoString host) {
    char hx[17];
    mako_sip_hex_u64(hx, mako_sip_rng_mix());
    char hx2[17];
    mako_sip_hex_u64(hx2, mako_sip_rng_mix());
    size_t n = host.len + 48;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w;
    if (host.data && host.len > 0) {
        w = snprintf(d, n, "%s-%s@%.*s", hx, hx2, (int)host.len, host.data);
    } else {
        w = snprintf(d, n, "%s-%s@localhost", hx, hx2);
    }
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

static inline int64_t mako_sip_cseq_new(void) {
    return (int64_t)(1 + (mako_sip_rng_mix() % 100000));
}

/* Dialog / transaction identity helpers (opaque strings for maps). */
static inline MakoString mako_sip_dialog_id(
    MakoString call_id, MakoString local_tag, MakoString remote_tag
) {
    size_t n = call_id.len + local_tag.len + remote_tag.len + 8;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w = snprintf(
        d, n, "%.*s|%.*s|%.*s",
        (int)call_id.len, call_id.data ? call_id.data : "",
        (int)local_tag.len, local_tag.data ? local_tag.data : "",
        (int)remote_tag.len, remote_tag.data ? remote_tag.data : ""
    );
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

static inline MakoString mako_sip_txn_key(MakoString branch, MakoString method) {
    size_t n = branch.len + method.len + 4;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w = snprintf(
        d, n, "%.*s|%.*s",
        (int)branch.len, branch.data ? branch.data : "",
        (int)method.len, method.data ? method.data : ""
    );
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

/* ---- SIP URI ---- */

static inline MakoString mako_sip_uri_scheme(MakoString uri) {
    if (!uri.data) return mako_str_from_cstr("");
    size_t i = 0;
    while (i < uri.len && uri.data[i] != ':') i++;
    if (i >= uri.len) return mako_str_from_cstr("");
    return mako_sip_slice_to_str(uri.data, i);
}

/* strip sip: / sips: and optional user@ ; params after ; */
static inline void mako_sip_uri_parts(
    MakoString uri,
    const char **user, size_t *ulen,
    const char **host, size_t *hlen,
    int64_t *port
) {
    *user = NULL;
    *ulen = 0;
    *host = NULL;
    *hlen = 0;
    *port = 0;
    if (!uri.data || uri.len == 0) return;
    const char *p = uri.data;
    size_t n = uri.len;
    /* skip scheme */
    size_t i = 0;
    while (i < n && p[i] != ':') i++;
    if (i < n && p[i] == ':') {
        i++;
        if (i < n && p[i] == '/') i++; /* rare sip:/ */
        if (i < n && p[i] == '/') i++;
    } else {
        i = 0;
    }
    /* cut params and headers */
    size_t end = i;
    while (end < n && p[end] != ';' && p[end] != '?' && p[end] != '>') end++;
    /* user@host */
    size_t at = (size_t)-1;
    for (size_t j = i; j < end; j++) {
        if (p[j] == '@') {
            at = j;
            break;
        }
    }
    size_t hs, he;
    if (at != (size_t)-1) {
        *user = p + i;
        *ulen = at - i;
        hs = at + 1;
    } else {
        hs = i;
    }
    he = end;
    /* [ipv6] or host:port */
    if (hs < he && p[hs] == '[') {
        size_t rb = hs;
        while (rb < he && p[rb] != ']') rb++;
        *host = p + hs + 1;
        *hlen = rb > hs + 1 ? rb - hs - 1 : 0;
        if (rb + 1 < he && p[rb + 1] == ':') {
            int64_t pt = 0;
            for (size_t k = rb + 2; k < he; k++) {
                if (p[k] < '0' || p[k] > '9') break;
                pt = pt * 10 + (p[k] - '0');
            }
            *port = pt;
        }
    } else {
        size_t colon = he;
        for (size_t j = hs; j < he; j++) {
            if (p[j] == ':') colon = j;
        }
        if (colon < he) {
            *host = p + hs;
            *hlen = colon - hs;
            int64_t pt = 0;
            for (size_t k = colon + 1; k < he; k++) {
                if (p[k] < '0' || p[k] > '9') break;
                pt = pt * 10 + (p[k] - '0');
            }
            *port = pt;
        } else {
            *host = p + hs;
            *hlen = he - hs;
        }
    }
}

static inline MakoString mako_sip_uri_user(MakoString uri) {
    const char *u, *h;
    size_t ul, hl;
    int64_t port;
    mako_sip_uri_parts(uri, &u, &ul, &h, &hl, &port);
    return mako_sip_slice_to_str(u, ul);
}

static inline MakoString mako_sip_uri_host(MakoString uri) {
    const char *u, *h;
    size_t ul, hl;
    int64_t port;
    mako_sip_uri_parts(uri, &u, &ul, &h, &hl, &port);
    return mako_sip_slice_to_str(h, hl);
}

static inline int64_t mako_sip_uri_port(MakoString uri) {
    const char *u, *h;
    size_t ul, hl;
    int64_t port;
    mako_sip_uri_parts(uri, &u, &ul, &h, &hl, &port);
    return port;
}

static inline MakoString mako_sip_uri_build(MakoString user, MakoString host, int64_t port) {
    size_t n = user.len + host.len + 32;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w;
    if (user.data && user.len > 0) {
        if (port > 0) {
            w = snprintf(
                d, n, "sip:%.*s@%.*s:%lld",
                (int)user.len, user.data,
                (int)host.len, host.data ? host.data : "localhost",
                (long long)port
            );
        } else {
            w = snprintf(
                d, n, "sip:%.*s@%.*s",
                (int)user.len, user.data,
                (int)host.len, host.data ? host.data : "localhost"
            );
        }
    } else {
        if (port > 0) {
            w = snprintf(
                d, n, "sip:%.*s:%lld",
                (int)host.len, host.data ? host.data : "localhost",
                (long long)port
            );
        } else {
            w = snprintf(
                d, n, "sip:%.*s",
                (int)host.len, host.data ? host.data : "localhost"
            );
        }
    }
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

/* ---- transport helpers (wrap net) ---- */

static inline int64_t mako_sip_udp_bind(MakoString host, int64_t port) {
    return mako_udp_bind_addr(host, port);
}

static inline int64_t mako_sip_udp_send(
    int64_t fd, MakoString host, int64_t port, MakoString msg
) {
    return mako_udp_send_to(fd, host, port, msg);
}

static inline MakoString mako_sip_udp_recv(int64_t fd, int64_t max_bytes) {
    if (max_bytes <= 0) max_bytes = 65535;
    return mako_udp_recv(fd, max_bytes);
}

/* TCP: write full SIP message (caller must have correct Content-Length). */
static inline int64_t mako_sip_tcp_send(int64_t fd, MakoString msg) {
    return mako_tcp_write_all(fd, msg);
}

/* ---- compact MD5 (RFC 1321) for Digest auth ---- */

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t buffer[64];
} MakoMd5Ctx;

static inline uint32_t mako_md5_F(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) | (~x & z);
}
static inline uint32_t mako_md5_G(uint32_t x, uint32_t y, uint32_t z) {
    return (x & z) | (y & ~z);
}
static inline uint32_t mako_md5_H(uint32_t x, uint32_t y, uint32_t z) {
    return x ^ y ^ z;
}
static inline uint32_t mako_md5_I(uint32_t x, uint32_t y, uint32_t z) {
    return y ^ (x | ~z);
}
static inline uint32_t mako_md5_rot(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static inline void mako_md5_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
    for (int i = 0, j = 0; i < 16; i++, j += 4)
        x[i] = (uint32_t)block[j] | ((uint32_t)block[j + 1] << 8) |
               ((uint32_t)block[j + 2] << 16) | ((uint32_t)block[j + 3] << 24);
#define MD5_STEP(f, a, b, c, d, xk, s, ac) \
    a += f(b, c, d) + xk + ac;             \
    a = mako_md5_rot(a, s);                \
    a += b
    MD5_STEP(mako_md5_F, a, b, c, d, x[0], 7, 0xd76aa478);
    MD5_STEP(mako_md5_F, d, a, b, c, x[1], 12, 0xe8c7b756);
    MD5_STEP(mako_md5_F, c, d, a, b, x[2], 17, 0x242070db);
    MD5_STEP(mako_md5_F, b, c, d, a, x[3], 22, 0xc1bdceee);
    MD5_STEP(mako_md5_F, a, b, c, d, x[4], 7, 0xf57c0faf);
    MD5_STEP(mako_md5_F, d, a, b, c, x[5], 12, 0x4787c62a);
    MD5_STEP(mako_md5_F, c, d, a, b, x[6], 17, 0xa8304613);
    MD5_STEP(mako_md5_F, b, c, d, a, x[7], 22, 0xfd469501);
    MD5_STEP(mako_md5_F, a, b, c, d, x[8], 7, 0x698098d8);
    MD5_STEP(mako_md5_F, d, a, b, c, x[9], 12, 0x8b44f7af);
    MD5_STEP(mako_md5_F, c, d, a, b, x[10], 17, 0xffff5bb1);
    MD5_STEP(mako_md5_F, b, c, d, a, x[11], 22, 0x895cd7be);
    MD5_STEP(mako_md5_F, a, b, c, d, x[12], 7, 0x6b901122);
    MD5_STEP(mako_md5_F, d, a, b, c, x[13], 12, 0xfd987193);
    MD5_STEP(mako_md5_F, c, d, a, b, x[14], 17, 0xa679438e);
    MD5_STEP(mako_md5_F, b, c, d, a, x[15], 22, 0x49b40821);
    MD5_STEP(mako_md5_G, a, b, c, d, x[1], 5, 0xf61e2562);
    MD5_STEP(mako_md5_G, d, a, b, c, x[6], 9, 0xc040b340);
    MD5_STEP(mako_md5_G, c, d, a, b, x[11], 14, 0x265e5a51);
    MD5_STEP(mako_md5_G, b, c, d, a, x[0], 20, 0xe9b6c7aa);
    MD5_STEP(mako_md5_G, a, b, c, d, x[5], 5, 0xd62f105d);
    MD5_STEP(mako_md5_G, d, a, b, c, x[10], 9, 0x02441453);
    MD5_STEP(mako_md5_G, c, d, a, b, x[15], 14, 0xd8a1e681);
    MD5_STEP(mako_md5_G, b, c, d, a, x[4], 20, 0xe7d3fbc8);
    MD5_STEP(mako_md5_G, a, b, c, d, x[9], 5, 0x21e1cde6);
    MD5_STEP(mako_md5_G, d, a, b, c, x[14], 9, 0xc33707d6);
    MD5_STEP(mako_md5_G, c, d, a, b, x[3], 14, 0xf4d50d87);
    MD5_STEP(mako_md5_G, b, c, d, a, x[8], 20, 0x455a14ed);
    MD5_STEP(mako_md5_G, a, b, c, d, x[13], 5, 0xa9e3e905);
    MD5_STEP(mako_md5_G, d, a, b, c, x[2], 9, 0xfcefa3f8);
    MD5_STEP(mako_md5_G, c, d, a, b, x[7], 14, 0x676f02d9);
    MD5_STEP(mako_md5_G, b, c, d, a, x[12], 20, 0x8d2a4c8a);
    MD5_STEP(mako_md5_H, a, b, c, d, x[5], 4, 0xfffa3942);
    MD5_STEP(mako_md5_H, d, a, b, c, x[8], 11, 0x8771f681);
    MD5_STEP(mako_md5_H, c, d, a, b, x[11], 16, 0x6d9d6122);
    MD5_STEP(mako_md5_H, b, c, d, a, x[14], 23, 0xfde5380c);
    MD5_STEP(mako_md5_H, a, b, c, d, x[1], 4, 0xa4beea44);
    MD5_STEP(mako_md5_H, d, a, b, c, x[4], 11, 0x4bdecfa9);
    MD5_STEP(mako_md5_H, c, d, a, b, x[7], 16, 0xf6bb4b60);
    MD5_STEP(mako_md5_H, b, c, d, a, x[10], 23, 0xbebfbc70);
    MD5_STEP(mako_md5_H, a, b, c, d, x[13], 4, 0x289b7ec6);
    MD5_STEP(mako_md5_H, d, a, b, c, x[0], 11, 0xeaa127fa);
    MD5_STEP(mako_md5_H, c, d, a, b, x[3], 16, 0xd4ef3085);
    MD5_STEP(mako_md5_H, b, c, d, a, x[6], 23, 0x04881d05);
    MD5_STEP(mako_md5_H, a, b, c, d, x[9], 4, 0xd9d4d039);
    MD5_STEP(mako_md5_H, d, a, b, c, x[12], 11, 0xe6db99e5);
    MD5_STEP(mako_md5_H, c, d, a, b, x[15], 16, 0x1fa27cf8);
    MD5_STEP(mako_md5_H, b, c, d, a, x[2], 23, 0xc4ac5665);
    MD5_STEP(mako_md5_I, a, b, c, d, x[0], 6, 0xf4292244);
    MD5_STEP(mako_md5_I, d, a, b, c, x[7], 10, 0x432aff97);
    MD5_STEP(mako_md5_I, c, d, a, b, x[14], 15, 0xab9423a7);
    MD5_STEP(mako_md5_I, b, c, d, a, x[5], 21, 0xfc93a039);
    MD5_STEP(mako_md5_I, a, b, c, d, x[12], 6, 0x655b59c3);
    MD5_STEP(mako_md5_I, d, a, b, c, x[3], 10, 0x8f0ccc92);
    MD5_STEP(mako_md5_I, c, d, a, b, x[10], 15, 0xffeff47d);
    MD5_STEP(mako_md5_I, b, c, d, a, x[1], 21, 0x85845dd1);
    MD5_STEP(mako_md5_I, a, b, c, d, x[8], 6, 0x6fa87e4f);
    MD5_STEP(mako_md5_I, d, a, b, c, x[15], 10, 0xfe2ce6e0);
    MD5_STEP(mako_md5_I, c, d, a, b, x[6], 15, 0xa3014314);
    MD5_STEP(mako_md5_I, b, c, d, a, x[13], 21, 0x4e0811a1);
    MD5_STEP(mako_md5_I, a, b, c, d, x[4], 6, 0xf7537e82);
    MD5_STEP(mako_md5_I, d, a, b, c, x[11], 10, 0xbd3af235);
    MD5_STEP(mako_md5_I, c, d, a, b, x[2], 15, 0x2ad7d2bb);
    MD5_STEP(mako_md5_I, b, c, d, a, x[9], 21, 0xeb86d391);
#undef MD5_STEP
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static inline void mako_md5_init(MakoMd5Ctx *ctx) {
    ctx->count = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

static inline void mako_md5_update(MakoMd5Ctx *ctx, const uint8_t *data, size_t len) {
    size_t i = 0;
    size_t idx = (size_t)((ctx->count / 8) % 64);
    ctx->count += (uint64_t)len * 8;
    size_t part = 64 - idx;
    if (len >= part) {
        memcpy(ctx->buffer + idx, data, part);
        mako_md5_transform(ctx->state, ctx->buffer);
        for (i = part; i + 63 < len; i += 64)
            mako_md5_transform(ctx->state, data + i);
        idx = 0;
    } else {
        i = 0;
    }
    memcpy(ctx->buffer + idx, data + i, len - i);
}

static inline void mako_md5_final(MakoMd5Ctx *ctx, uint8_t dig[16]) {
    uint8_t bits[8];
    for (int i = 0; i < 8; i++) bits[i] = (uint8_t)((ctx->count >> (8 * i)) & 0xff);
    size_t idx = (size_t)((ctx->count / 8) % 64);
    uint8_t pad[64];
    pad[0] = 0x80;
    memset(pad + 1, 0, 63);
    size_t pad_len = (idx < 56) ? (56 - idx) : (120 - idx);
    mako_md5_update(ctx, pad, pad_len);
    mako_md5_update(ctx, bits, 8);
    for (int i = 0; i < 4; i++) {
        dig[i * 4] = (uint8_t)(ctx->state[i] & 0xff);
        dig[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 8) & 0xff);
        dig[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 16) & 0xff);
        dig[i * 4 + 3] = (uint8_t)((ctx->state[i] >> 24) & 0xff);
    }
}

static inline MakoString mako_sip_md5_hex(MakoString s) {
    MakoMd5Ctx ctx;
    mako_md5_init(&ctx);
    if (s.data && s.len) mako_md5_update(&ctx, (const uint8_t *)s.data, s.len);
    uint8_t dig[16];
    mako_md5_final(&ctx, dig);
    char out[33];
    static const char *hex = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[i * 2] = hex[(dig[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[dig[i] & 0xf];
    }
    out[32] = 0;
    return mako_str_from_cstr(out);
}

/* Digest response from stored HA1 = MD5(user:realm:pass) hex (server-side, no plaintext). */
static inline MakoString mako_sip_digest_response_ha1(
    MakoString ha1, MakoString method, MakoString uri, MakoString nonce
) {
    size_t n2 = method.len + uri.len + 4;
    char *a2 = (char *)malloc(n2);
    if (!a2) return mako_str_from_cstr("");
    int w2 = snprintf(
        a2, n2, "%.*s:%.*s",
        (int)method.len, method.data ? method.data : "",
        (int)uri.len, uri.data ? uri.data : ""
    );
    MakoString ha2 = mako_sip_md5_hex((MakoString){a2, (size_t)(w2 > 0 ? w2 : 0)});
    free(a2);
    size_t n3 = ha1.len + nonce.len + ha2.len + 4;
    char *resp = (char *)malloc(n3);
    if (!resp) {
        mako_str_free(ha2);
        return mako_str_from_cstr("");
    }
    int w3 = snprintf(
        resp, n3, "%.*s:%.*s:%.*s",
        (int)ha1.len, ha1.data ? ha1.data : "",
        (int)nonce.len, nonce.data ? nonce.data : "",
        (int)ha2.len, ha2.data ? ha2.data : ""
    );
    mako_str_free(ha2);
    MakoString out = mako_sip_md5_hex((MakoString){resp, (size_t)(w3 > 0 ? w3 : 0)});
    free(resp);
    return out;
}

/* Digest response (MD5, qop absent): H(H(A1):nonce:H(A2)) */
static inline MakoString mako_sip_digest_response(
    MakoString username,
    MakoString realm,
    MakoString password,
    MakoString method,
    MakoString uri,
    MakoString nonce
) {
    /* A1 = user:realm:pass */
    size_t n1 = username.len + realm.len + password.len + 4;
    char *a1 = (char *)malloc(n1);
    if (!a1) return mako_str_from_cstr("");
    int w1 = snprintf(
        a1, n1, "%.*s:%.*s:%.*s",
        (int)username.len, username.data ? username.data : "",
        (int)realm.len, realm.data ? realm.data : "",
        (int)password.len, password.data ? password.data : ""
    );
    MakoString ha1 = mako_sip_md5_hex((MakoString){a1, (size_t)(w1 > 0 ? w1 : 0)});
    free(a1);
    MakoString out = mako_sip_digest_response_ha1(ha1, method, uri, nonce);
    mako_str_free(ha1);
    return out;
}

/* WWW-Authenticate / Proxy-Authenticate challenge values (header value only). */
static inline MakoString mako_sip_www_authenticate(MakoString realm, MakoString nonce) {
    size_t n = realm.len + nonce.len + 64;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w = snprintf(
        d, n,
        "Digest realm=\"%.*s\", nonce=\"%.*s\", algorithm=MD5",
        (int)realm.len, realm.data ? realm.data : "",
        (int)nonce.len, nonce.data ? nonce.data : ""
    );
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

static inline MakoString mako_sip_proxy_authenticate(MakoString realm, MakoString nonce) {
    return mako_sip_www_authenticate(realm, nonce);
}

/* Authorization header value for Digest (no qop). */
static inline MakoString mako_sip_authorization_digest(
    MakoString username,
    MakoString realm,
    MakoString nonce,
    MakoString uri,
    MakoString response
) {
    size_t n = username.len + realm.len + nonce.len + uri.len + response.len + 96;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w = snprintf(
        d, n,
        "Digest username=\"%.*s\", realm=\"%.*s\", nonce=\"%.*s\", uri=\"%.*s\", "
        "response=\"%.*s\", algorithm=MD5",
        (int)username.len, username.data ? username.data : "",
        (int)realm.len, realm.data ? realm.data : "",
        (int)nonce.len, nonce.data ? nonce.data : "",
        (int)uri.len, uri.data ? uri.data : "",
        (int)response.len, response.data ? response.data : ""
    );
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

/* Extract Digest param from WWW-Authenticate / Authorization value. */
static inline MakoString mako_sip_auth_param(MakoString hdr, MakoString key) {
    if (!hdr.data || !key.data) return mako_str_from_cstr("");
    for (size_t i = 0; i + key.len + 1 < hdr.len; i++) {
        if (mako_sip_ci_eq(hdr.data + i, key.len, key.data, key.len) &&
            hdr.data[i + key.len] == '=') {
            size_t j = i + key.len + 1;
            if (j < hdr.len && hdr.data[j] == '"') {
                j++;
                size_t start = j;
                while (j < hdr.len && hdr.data[j] != '"') j++;
                return mako_sip_slice_to_str(hdr.data + start, j - start);
            }
            size_t start = j;
            while (j < hdr.len && hdr.data[j] != ',' && hdr.data[j] != ' ') j++;
            return mako_sip_slice_to_str(hdr.data + start, j - start);
        }
    }
    return mako_str_from_cstr("");
}

/* ---- SDP (RFC 4566) ---- */

static inline int64_t mako_sdp_ok(MakoString sdp) {
    if (!sdp.data || sdp.len < 3) return 0;
    /* must start with v= */
    return (sdp.data[0] == 'v' && sdp.data[1] == '=') ? 1 : 0;
}

static inline MakoString mako_sdp_line(MakoString sdp, char type, int64_t nth) {
    if (!sdp.data || nth < 0) return mako_str_from_cstr("");
    int64_t seen = 0;
    size_t i = 0;
    while (i < sdp.len) {
        size_t start = i;
        while (i < sdp.len && sdp.data[i] != '\n') i++;
        size_t end = i;
        if (end > start && sdp.data[end - 1] == '\r') end--;
        if (end > start + 1 && sdp.data[start] == type && sdp.data[start + 1] == '=') {
            if (seen == nth) {
                return mako_sip_slice_to_str(sdp.data + start + 2, end - start - 2);
            }
            seen++;
        }
        if (i < sdp.len && sdp.data[i] == '\n') i++;
    }
    return mako_str_from_cstr("");
}

static inline int64_t mako_sdp_line_count(MakoString sdp, char type) {
    if (!sdp.data) return 0;
    int64_t n = 0;
    size_t i = 0;
    while (i < sdp.len) {
        size_t start = i;
        while (i < sdp.len && sdp.data[i] != '\n') i++;
        size_t end = i;
        if (end > start && sdp.data[end - 1] == '\r') end--;
        if (end > start + 1 && sdp.data[start] == type && sdp.data[start + 1] == '=') n++;
        if (i < sdp.len && sdp.data[i] == '\n') i++;
    }
    return n;
}

static inline MakoString mako_sdp_version(MakoString sdp) {
    return mako_sdp_line(sdp, 'v', 0);
}
static inline MakoString mako_sdp_origin(MakoString sdp) {
    return mako_sdp_line(sdp, 'o', 0);
}
static inline MakoString mako_sdp_session_name(MakoString sdp) {
    return mako_sdp_line(sdp, 's', 0);
}
static inline MakoString mako_sdp_connection(MakoString sdp) {
    return mako_sdp_line(sdp, 'c', 0);
}

static inline MakoString mako_sdp_connection_addr(MakoString sdp) {
    MakoString c = mako_sdp_connection(sdp);
    if (!c.data || c.len == 0) {
        mako_str_free(c);
        return mako_str_from_cstr("");
    }
    /* IN IP4 1.2.3.4 */
    size_t sp = 0, count = 0, start = 0;
    for (size_t i = 0; i <= c.len; i++) {
        if (i == c.len || c.data[i] == ' ') {
            if (i > sp) {
                count++;
                if (count == 3) {
                    start = sp;
                    MakoString out = mako_sip_slice_to_str(c.data + start, i - start);
                    mako_str_free(c);
                    return out;
                }
            }
            sp = i + 1;
        }
    }
    mako_str_free(c);
    return mako_str_from_cstr("");
}

static inline int64_t mako_sdp_media_count(MakoString sdp) {
    return mako_sdp_line_count(sdp, 'm');
}

static inline MakoString mako_sdp_media(MakoString sdp, int64_t i) {
    return mako_sdp_line(sdp, 'm', i);
}

static inline MakoString mako_sdp_media_type(MakoString sdp, int64_t i) {
    MakoString m = mako_sdp_media(sdp, i);
    if (!m.data) return mako_str_from_cstr("");
    size_t j = 0;
    while (j < m.len && m.data[j] != ' ') j++;
    MakoString out = mako_sip_slice_to_str(m.data, j);
    mako_str_free(m);
    return out;
}

static inline int64_t mako_sdp_media_port(MakoString sdp, int64_t i) {
    MakoString m = mako_sdp_media(sdp, i);
    if (!m.data) return 0;
    size_t j = 0;
    while (j < m.len && m.data[j] != ' ') j++;
    if (j >= m.len) {
        mako_str_free(m);
        return 0;
    }
    j++;
    int64_t port = 0;
    while (j < m.len && m.data[j] >= '0' && m.data[j] <= '9') {
        port = port * 10 + (m.data[j] - '0');
        j++;
    }
    mako_str_free(m);
    return port;
}

static inline MakoString mako_sdp_media_proto(MakoString sdp, int64_t i) {
    MakoString m = mako_sdp_media(sdp, i);
    if (!m.data) return mako_str_from_cstr("");
    /* type port proto formats... */
    int field = 0;
    size_t sp = 0, start = 0, end = 0;
    for (size_t j = 0; j <= m.len; j++) {
        if (j == m.len || m.data[j] == ' ') {
            if (j > sp) {
                field++;
                if (field == 3) {
                    start = sp;
                    end = j;
                    break;
                }
            }
            sp = j + 1;
        }
    }
    MakoString out = mako_sip_slice_to_str(m.data + start, end > start ? end - start : 0);
    mako_str_free(m);
    return out;
}

/* Session-level attribute a=name or a=name:value — return value or "1" if flag. */
static inline MakoString mako_sdp_attr(MakoString sdp, MakoString name) {
    if (!sdp.data || !name.data) return mako_str_from_cstr("");
    size_t i = 0;
    while (i < sdp.len) {
        size_t start = i;
        while (i < sdp.len && sdp.data[i] != '\n') i++;
        size_t end = i;
        if (end > start && sdp.data[end - 1] == '\r') end--;
        if (end > start + 2 && sdp.data[start] == 'a' && sdp.data[start + 1] == '=') {
            const char *v = sdp.data + start + 2;
            size_t vl = end - start - 2;
            if (vl >= name.len && mako_sip_ci_eq(v, name.len, name.data, name.len)) {
                if (vl == name.len) {
                    return mako_str_from_cstr("1");
                }
                if (v[name.len] == ':') {
                    return mako_sip_slice_to_str(v + name.len + 1, vl - name.len - 1);
                }
            }
        }
        if (i < sdp.len && sdp.data[i] == '\n') i++;
    }
    return mako_str_from_cstr("");
}

static inline MakoString mako_sdp_append_line(MakoString sdp, MakoString line) {
    size_t n = sdp.len + line.len + 4;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    size_t o = 0;
    if (sdp.data && sdp.len) {
        memcpy(d, sdp.data, sdp.len);
        o = sdp.len;
        if (o > 0 && d[o - 1] != '\n') {
            d[o++] = '\r';
            d[o++] = '\n';
        }
    }
    if (line.data && line.len) {
        memcpy(d + o, line.data, line.len);
        o += line.len;
        if (o > 0 && d[o - 1] != '\n') {
            d[o++] = '\r';
            d[o++] = '\n';
        }
    }
    d[o] = 0;
    return (MakoString){d, o};
}

/* Minimal audio SDP (one m=audio). codecs = space-separated payload types e.g. "0 8 101". */
static inline MakoString mako_sdp_build_audio(
    MakoString origin_user,
    MakoString session_name,
    MakoString ip,
    int64_t audio_port,
    MakoString codecs
) {
    const char *ou = origin_user.data && origin_user.len ? origin_user.data : "-";
    const char *sn = session_name.data && session_name.len ? session_name.data : "mako";
    const char *addr = ip.data && ip.len ? ip.data : "127.0.0.1";
    const char *cs = codecs.data && codecs.len ? codecs.data : "0 8";
    uint64_t sid = mako_sip_rng_mix() & 0xffffffffULL;
    uint64_t ver = mako_sip_rng_mix() & 0xffffffffULL;
    char buf[1024];
    int w = snprintf(
        buf, sizeof(buf),
        "v=0\r\n"
        "o=%s %llu %llu IN IP4 %s\r\n"
        "s=%s\r\n"
        "c=IN IP4 %s\r\n"
        "t=0 0\r\n"
        "m=audio %lld RTP/AVP %s\r\n"
        "a=rtpmap:0 PCMU/8000\r\n"
        "a=rtpmap:8 PCMA/8000\r\n"
        "a=sendrecv\r\n",
        ou, (unsigned long long)sid, (unsigned long long)ver, addr, sn, addr,
        (long long)audio_port, cs
    );
    if (w < 0 || (size_t)w >= sizeof(buf)) return mako_str_from_cstr("");
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_sdp_attr_rtpmap(int64_t pt, MakoString name, int64_t rate) {
    size_t n = name.len + 48;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w = snprintf(
        d, n, "a=rtpmap:%lld %.*s/%lld",
        (long long)pt, (int)name.len, name.data ? name.data : "PCMU",
        (long long)rate
    );
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

static inline MakoString mako_sdp_attr_fmtp(int64_t pt, MakoString params) {
    size_t n = params.len + 32;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    int w = snprintf(
        d, n, "a=fmtp:%lld %.*s",
        (long long)pt, (int)params.len, params.data ? params.data : ""
    );
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

/* ---- RTP (RFC 3550) fixed 12-byte header, no CSRC/extension in pack ---- */

static inline int64_t mako_rtp_header_len(void) { return 12; }

static inline int64_t mako_rtp_parse_ok(MakoString pkt) {
    if (!pkt.data || pkt.len < 12) return 0;
    int v = (pkt.data[0] >> 6) & 0x3;
    return v == 2 ? 1 : 0;
}

static inline int64_t mako_rtp_version(MakoString pkt) {
    if (!pkt.data || pkt.len < 1) return 0;
    return (pkt.data[0] >> 6) & 0x3;
}

static inline int64_t mako_rtp_marker(MakoString pkt) {
    if (!pkt.data || pkt.len < 2) return 0;
    return (pkt.data[1] >> 7) & 1;
}

static inline int64_t mako_rtp_payload_type(MakoString pkt) {
    if (!pkt.data || pkt.len < 2) return 0;
    return pkt.data[1] & 0x7f;
}

static inline int64_t mako_rtp_seq(MakoString pkt) {
    if (!pkt.data || pkt.len < 4) return 0;
    return ((int64_t)(uint8_t)pkt.data[2] << 8) | (uint8_t)pkt.data[3];
}

static inline int64_t mako_rtp_timestamp(MakoString pkt) {
    if (!pkt.data || pkt.len < 8) return 0;
    return ((int64_t)(uint8_t)pkt.data[4] << 24) |
           ((int64_t)(uint8_t)pkt.data[5] << 16) |
           ((int64_t)(uint8_t)pkt.data[6] << 8) |
           (int64_t)(uint8_t)pkt.data[7];
}

static inline int64_t mako_rtp_ssrc(MakoString pkt) {
    if (!pkt.data || pkt.len < 12) return 0;
    return ((int64_t)(uint8_t)pkt.data[8] << 24) |
           ((int64_t)(uint8_t)pkt.data[9] << 16) |
           ((int64_t)(uint8_t)pkt.data[10] << 8) |
           (int64_t)(uint8_t)pkt.data[11];
}

static inline MakoString mako_rtp_payload(MakoString pkt) {
    if (!pkt.data || pkt.len < 12) return mako_str_from_cstr("");
    int cc = pkt.data[0] & 0x0f;
    int x = (pkt.data[0] >> 4) & 1;
    size_t off = 12 + (size_t)cc * 4;
    if (x && off + 4 <= pkt.len) {
        size_t xlen = ((size_t)(uint8_t)pkt.data[off + 2] << 8) |
                      (size_t)(uint8_t)pkt.data[off + 3];
        off += 4 + xlen * 4;
    }
    if (off > pkt.len) return mako_str_from_cstr("");
    return mako_sip_slice_to_str(pkt.data + off, pkt.len - off);
}

static inline MakoString mako_rtp_pack(
    int64_t pt,
    int64_t seq,
    int64_t ts,
    int64_t ssrc,
    int64_t marker,
    MakoString payload
) {
    size_t plen = payload.data ? payload.len : 0;
    size_t n = 12 + plen;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    d[0] = (char)0x80; /* V=2 */
    d[1] = (char)(((marker ? 1 : 0) << 7) | (pt & 0x7f));
    d[2] = (char)((seq >> 8) & 0xff);
    d[3] = (char)(seq & 0xff);
    d[4] = (char)((ts >> 24) & 0xff);
    d[5] = (char)((ts >> 16) & 0xff);
    d[6] = (char)((ts >> 8) & 0xff);
    d[7] = (char)(ts & 0xff);
    d[8] = (char)((ssrc >> 24) & 0xff);
    d[9] = (char)((ssrc >> 16) & 0xff);
    d[10] = (char)((ssrc >> 8) & 0xff);
    d[11] = (char)(ssrc & 0xff);
    if (plen && payload.data) memcpy(d + 12, payload.data, plen);
    return (MakoString){d, n};
}

/* Convenience: reply 100/180/200 template headers from a request (copy Via/From/To/Call-ID/CSeq).
 * Uses non-owned name literals to avoid per-header malloc leaks under proxy load. */
static inline MakoString mako_sip_copy_headers_for_response(MakoString req) {
    MakoString h = mako_str_from_cstr("");
    MakoString via = mako_sip_header(req, MAKO_SIP_LIT("Via"));
    if (via.len) {
        MakoString nh = mako_sip_headers_append(h, MAKO_SIP_LIT("Via"), via);
        mako_str_free(h);
        h = nh;
    }
    mako_str_free(via);
    /* all Vias */
    int64_t vc = mako_sip_header_count(req, MAKO_SIP_LIT("Via"));
    for (int64_t i = 1; i < vc; i++) {
        MakoString v = mako_sip_header_n(req, MAKO_SIP_LIT("Via"), i);
        if (v.len) {
            MakoString nh = mako_sip_headers_append(h, MAKO_SIP_LIT("Via"), v);
            mako_str_free(h);
            h = nh;
        }
        mako_str_free(v);
    }
    MakoString from = mako_sip_header(req, MAKO_SIP_LIT("From"));
    if (from.len) {
        MakoString nh = mako_sip_headers_append(h, MAKO_SIP_LIT("From"), from);
        mako_str_free(h);
        h = nh;
    }
    mako_str_free(from);
    MakoString to = mako_sip_header(req, MAKO_SIP_LIT("To"));
    if (to.len) {
        MakoString nh = mako_sip_headers_append(h, MAKO_SIP_LIT("To"), to);
        mako_str_free(h);
        h = nh;
    }
    mako_str_free(to);
    MakoString cid = mako_sip_header(req, MAKO_SIP_LIT("Call-ID"));
    if (cid.len) {
        MakoString nh = mako_sip_headers_append(h, MAKO_SIP_LIT("Call-ID"), cid);
        mako_str_free(h);
        h = nh;
    }
    mako_str_free(cid);
    MakoString cseq = mako_sip_header(req, MAKO_SIP_LIT("CSeq"));
    if (cseq.len) {
        MakoString nh = mako_sip_headers_append(h, MAKO_SIP_LIT("CSeq"), cseq);
        mako_str_free(h);
        h = nh;
    }
    mako_str_free(cseq);
    return h;
}

static inline MakoString mako_sip_reply(
    MakoString req, int64_t code, MakoString reason, MakoString extra_headers, MakoString body
) {
    MakoString base = mako_sip_copy_headers_for_response(req);
    /* merge extra */
    if (extra_headers.data && extra_headers.len) {
        size_t n = base.len + extra_headers.len + 1;
        char *d = (char *)malloc(n);
        if (d) {
            memcpy(d, base.data ? base.data : "", base.len);
            memcpy(d + base.len, extra_headers.data, extra_headers.len);
            d[base.len + extra_headers.len] = 0;
            mako_str_free(base);
            base = (MakoString){d, base.len + extra_headers.len};
        }
    }
    MakoString out = mako_sip_response(code, reason, base, body);
    mako_str_free(base);
    return out;
}

/* Ensure To header has ;tag=… (proxy 1xx/2xx). Returns owned new message. */
static inline MakoString mako_sip_ensure_to_tag(MakoString msg, MakoString tag) {
    if (!msg.data || msg.len == 0) return mako_str_from_cstr("");
    if (!tag.data || tag.len == 0) return mako_sip_slice_to_str(msg.data, msg.len);
    MakoString to = mako_sip_header(msg, MAKO_SIP_LIT("To"));
    if (to.len == 0) {
        mako_str_free(to);
        return mako_sip_slice_to_str(msg.data, msg.len);
    }
    MakoString existing = mako_sip_addr_tag(to);
    if (existing.len > 0) {
        mako_str_free(existing);
        mako_str_free(to);
        return mako_sip_slice_to_str(msg.data, msg.len);
    }
    mako_str_free(existing);
    size_t nval = to.len + tag.len + 8;
    char *nv = (char *)malloc(nval);
    if (!nv) {
        mako_str_free(to);
        return mako_sip_slice_to_str(msg.data, msg.len);
    }
    int w = snprintf(
        nv, nval, "%.*s;tag=%.*s",
        (int)to.len, to.data ? to.data : "",
        (int)tag.len, tag.data
    );
    mako_str_free(to);
    if (w < 0) {
        free(nv);
        return mako_sip_slice_to_str(msg.data, msg.len);
    }
    MakoString new_to = {nv, (size_t)w};
    size_t he = 0;
    if (!mako_sip_body_start(msg.data, msg.len, &he)) {
        mako_str_free(new_to);
        return mako_sip_slice_to_str(msg.data, msg.len);
    }
    const char *hend = msg.data + he;
    const char *crlf = mako_sip_find_crlf(msg.data, hend);
    if (!crlf) {
        mako_str_free(new_to);
        return mako_sip_slice_to_str(msg.data, msg.len);
    }
    const char *p = crlf + 2;
    const char *rep_s = NULL, *rep_e = NULL;
    while (p < hend) {
        if (p[0] == '\r' && p + 1 < hend && p[1] == '\n') break;
        const char *le = mako_sip_find_crlf(p, hend);
        if (!le) break;
        size_t llen = (size_t)(le - p);
        if (llen > 0 && (p[0] == ' ' || p[0] == '\t')) {
            p = le + 2;
            continue;
        }
        if (mako_sip_header_line_is(p, llen, "To", 2)) {
            rep_s = p;
            rep_e = le + 2;
            while (rep_e < hend && (*rep_e == ' ' || *rep_e == '\t')) {
                const char *nle = mako_sip_find_crlf(rep_e, hend);
                if (!nle) break;
                rep_e = nle + 2;
            }
            break;
        }
        p = le + 2;
    }
    MakoString line = mako_sip_header_line(MAKO_SIP_LIT("To"), new_to);
    mako_str_free(new_to);
    if (!line.data || line.len == 0) {
        mako_str_free(line);
        return mako_sip_slice_to_str(msg.data, msg.len);
    }
    if (!rep_s) {
        size_t start_len = (size_t)(crlf + 2 - msg.data);
        size_t nn = msg.len + line.len + 1;
        char *d = (char *)malloc(nn);
        if (!d) {
            mako_str_free(line);
            return mako_str_from_cstr("");
        }
        memcpy(d, msg.data, start_len);
        memcpy(d + start_len, line.data, line.len);
        memcpy(d + start_len + line.len, msg.data + start_len, msg.len - start_len);
        size_t total = msg.len + line.len;
        d[total] = 0;
        mako_str_free(line);
        return (MakoString){d, total};
    }
    size_t keep1 = (size_t)(rep_s - msg.data);
    size_t keep2 = msg.len - (size_t)(rep_e - msg.data);
    size_t nn = keep1 + line.len + keep2 + 1;
    char *d = (char *)malloc(nn);
    if (!d) {
        mako_str_free(line);
        return mako_str_from_cstr("");
    }
    memcpy(d, msg.data, keep1);
    memcpy(d + keep1, line.data, line.len);
    memcpy(d + keep1 + line.len, rep_e, keep2);
    size_t total = keep1 + line.len + keep2;
    d[total] = 0;
    mako_str_free(line);
    return (MakoString){d, total};
}

/* Reply and ensure To has a tag (common for 1xx/2xx from proxy). */
static inline MakoString mako_sip_reply_with_to_tag(
    MakoString req,
    int64_t code,
    MakoString reason,
    MakoString extra_headers,
    MakoString body,
    MakoString tag
) {
    MakoString resp = mako_sip_reply(req, code, reason, extra_headers, body);
    MakoString out = mako_sip_ensure_to_tag(resp, tag);
    mako_str_free(resp);
    return out;
}

/* Method class helpers */
static inline int64_t mako_sip_method_is(MakoString msg, MakoString method) {
    MakoString m = mako_sip_method(msg);
    int eq = mako_sip_ci_eq(
        m.data ? m.data : "", m.len, method.data ? method.data : "", method.len
    );
    mako_str_free(m);
    return eq ? 1 : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_SIP_H */
