/* Mako security helpers — secrets, constant-time compare, header validation.
 * Included from mako_rt.h / mako_http.h / mako_std.h as needed.
 */
#ifndef MAKO_SECURITY_H
#define MAKO_SECURITY_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Secure zero (best-effort; resists dead-store elimination) ---- */
static inline void mako_secure_zero(void *p, size_t n) {
    if (!p || n == 0) return;
#if defined(__linux__) && defined(__GLIBC__) && !defined(__ANDROID__) && defined(_DEFAULT_SOURCE)
    explicit_bzero(p, n);
#else
    /* Portable: volatile byte loop (works on Apple/MSVC/mingw without memset_s). */
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) *v++ = 0;
#endif
}

/* Owned secret buffer — wipe on free. Use for tokens/keys. */
typedef struct {
    unsigned char *data;
    size_t len;
} MakoSecret;

static inline MakoSecret mako_secret_from_bytes(const void *src, size_t n) {
    MakoSecret s;
    s.len = n;
    s.data = (unsigned char *)malloc(n ? n : 1);
    if (!s.data) {
        fprintf(stderr, "mako: OOM in secret_from_bytes\n");
        abort();
    }
    if (n && src) memcpy(s.data, src, n);
    else if (n) memset(s.data, 0, n);
    return s;
}

static inline MakoSecret mako_secret_from_str(MakoString str) {
    return mako_secret_from_bytes(str.data ? str.data : "", str.len);
}

static inline void mako_secret_drop(MakoSecret *s) {
    if (!s) return;
    if (s->data) {
        mako_secure_zero(s->data, s->len);
        free(s->data);
    }
    s->data = NULL;
    s->len = 0;
}

static inline int64_t mako_secret_len(MakoSecret s) {
    return (int64_t)s.len;
}

/* Constant-time equality for tokens (length mismatch still scans max). */
static inline int64_t mako_const_eq_bytes(const void *a, size_t an, const void *b, size_t bn) {
    const unsigned char *x = (const unsigned char *)(a ? a : "");
    const unsigned char *y = (const unsigned char *)(b ? b : "");
    size_t n = an > bn ? an : bn;
    unsigned char diff = (unsigned char)(an != bn ? 1 : 0);
    for (size_t i = 0; i < n; i++) {
        unsigned char xa = i < an ? x[i] : 0;
        unsigned char yb = i < bn ? y[i] : 0;
        diff |= (unsigned char)(xa ^ yb);
    }
    return diff == 0 ? 1 : 0;
}

static inline int64_t mako_const_eq(MakoString a, MakoString b) {
    return mako_const_eq_bytes(
        a.data ? a.data : "", a.len, b.data ? b.data : "", b.len
    );
}

/* Alias */
static inline int64_t mako_crypto_eq(MakoString a, MakoString b) {
    return mako_const_eq(a, b);
}

/* Constant-time compare secret to a string (token check without early exit). */
static inline int64_t mako_secret_eq_str(MakoSecret s, MakoString other) {
    return mako_const_eq_bytes(
        s.data, s.len, other.data ? other.data : "", other.len
    );
}

/* HTTP header name: token chars only (RFC 7230 tchar), no CR/LF/NUL/space/colon. */
static inline int mako_http_header_name_ok(const char *name, size_t n) {
    if (!name || n == 0 || n > 256) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c <= 0x20 || c >= 0x7f) return 0;
        if (c == '(' || c == ')' || c == '<' || c == '>' || c == '@' || c == ',' ||
            c == ';' || c == ':' || c == '\\' || c == '"' || c == '/' || c == '[' ||
            c == ']' || c == '?' || c == '=' || c == '{' || c == '}')
            return 0;
    }
    return 1;
}

/* Header value: no CR/LF/NUL; allow HTAB and VCHAR / obs-text loosely. */
static inline int mako_http_header_value_ok(const char *val, size_t n) {
    if (!val && n) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)val[i];
        if (c == 0 || c == '\r' || c == '\n') return 0;
    }
    return 1;
}

static inline int mako_http_header_pair_ok(MakoString name, MakoString value) {
    return mako_http_header_name_ok(name.data ? name.data : "", name.len) &&
           mako_http_header_value_ok(value.data ? value.data : "", value.len);
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_SECURITY_H */
