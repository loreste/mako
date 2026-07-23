/* Mako cloud/distributed systems primitives.
 * Consistent hashing, rate limiting, circuit breaker, JWT, retry.
 * Designed for microservices, API gateways, distributed databases. */
#ifndef MAKO_CLOUD_H
#define MAKO_CLOUD_H

#include "mako_rt.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#if defined(MAKO_HAS_OPENSSL) || defined(MAKO_USE_OPENSSL)
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#define MAKO_CLOUD_OPENSSL 1
#endif

/* ============================================================
 * Consistent Hash Ring — distribute keys across N nodes.
 * Uses virtual nodes (vnodes) for even distribution.
 * Thread-safe reads (sorted array, binary search).
 * ============================================================ */

#define CHASH_MAX_VNODES 4096

typedef struct {
    uint32_t hash;
    int node_id;
} CHashPoint;

typedef struct {
    CHashPoint points[CHASH_MAX_VNODES];
    int count;
    int node_count;
    int vnodes_per_node;
} MakoCHash;

static inline uint32_t chash_fnv1a(const char *data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];
        h *= 16777619u;
    }
    return h;
}

static int chash_cmp(const void *a, const void *b) {
    uint32_t ha = ((const CHashPoint *)a)->hash;
    uint32_t hb = ((const CHashPoint *)b)->hash;
    return (ha > hb) - (ha < hb);
}

/* Create a consistent hash ring with `nodes` nodes, `vnodes` virtual nodes each. */
static inline MakoCHash *mako_chash_new(int64_t nodes, int64_t vnodes) {
    MakoCHash *r = (MakoCHash *)calloc(1, sizeof(MakoCHash));
    if (!r) return NULL;
    r->node_count = (int)nodes;
    r->vnodes_per_node = (int)(vnodes > 0 ? vnodes : 150);
    r->count = 0;

    for (int n = 0; n < (int)nodes && r->count < CHASH_MAX_VNODES; n++) {
        for (int v = 0; v < r->vnodes_per_node && r->count < CHASH_MAX_VNODES; v++) {
            char buf[64];
            int len = snprintf(buf, sizeof(buf), "node%d#%d", n, v);
            r->points[r->count].hash = chash_fnv1a(buf, (size_t)len);
            r->points[r->count].node_id = n;
            r->count++;
        }
    }
    qsort(r->points, (size_t)r->count, sizeof(CHashPoint), chash_cmp);
    return r;
}

/* Look up which node owns a key. Returns node_id (0-based). */
static inline int64_t mako_chash_get(MakoCHash *r, MakoString key) {
    if (!r || r->count == 0 || !key.data) return 0;
    uint32_t h = chash_fnv1a(key.data, key.len);

    /* Binary search for first point >= h */
    int lo = 0, hi = r->count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (r->points[mid].hash < h) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= r->count) lo = 0; /* wrap around */
    return (int64_t)r->points[lo].node_id;
}

/* Add a node to the ring. Returns new node_id. */
static inline int64_t mako_chash_add_node(MakoCHash *r) {
    if (!r) return -1;
    int n = r->node_count;
    r->node_count++;
    for (int v = 0; v < r->vnodes_per_node && r->count < CHASH_MAX_VNODES; v++) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "node%d#%d", n, v);
        r->points[r->count].hash = chash_fnv1a(buf, (size_t)len);
        r->points[r->count].node_id = n;
        r->count++;
    }
    qsort(r->points, (size_t)r->count, sizeof(CHashPoint), chash_cmp);
    return (int64_t)n;
}

/* Remove a node from the ring. */
static inline void mako_chash_remove_node(MakoCHash *r, int64_t node_id) {
    if (!r) return;
    int w = 0;
    for (int i = 0; i < r->count; i++) {
        if (r->points[i].node_id != (int)node_id) {
            r->points[w++] = r->points[i];
        }
    }
    r->count = w;
}

static inline int64_t mako_chash_node_count(MakoCHash *r) {
    return r ? (int64_t)r->node_count : 0;
}

static inline void mako_chash_free(MakoCHash *r) { free(r); }

/* ============================================================
 * Token Bucket Rate Limiter — thread-safe, microsecond precision.
 * ============================================================ */

typedef struct {
    atomic_int_fast64_t tokens;     /* current tokens (scaled by 1000 for precision) */
    atomic_int_fast64_t last_us;    /* last refill timestamp (microseconds) */
    int64_t max_tokens;             /* capacity (scaled by 1000) */
    int64_t refill_rate;            /* tokens per second (scaled by 1000) */
} MakoRateLimiter;

static inline int64_t mako_rl_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000LL;
}

/* Create rate limiter: `rate` requests/second, `burst` max burst size. */
static inline MakoRateLimiter *mako_ratelimit_new(int64_t rate, int64_t burst) {
    MakoRateLimiter *rl = (MakoRateLimiter *)calloc(1, sizeof(MakoRateLimiter));
    if (!rl) return NULL;
    rl->max_tokens = burst * 1000;
    rl->refill_rate = rate * 1000;
    atomic_store(&rl->tokens, rl->max_tokens);
    atomic_store(&rl->last_us, mako_rl_now_us());
    return rl;
}

/* Try to consume 1 token. Returns 1 if allowed, 0 if rate-limited. */
static inline int64_t mako_ratelimit_allow(MakoRateLimiter *rl) {
    if (!rl) return 0;
    int64_t now = mako_rl_now_us();
    int64_t last = atomic_load(&rl->last_us);
    int64_t elapsed_us = now - last;

    if (elapsed_us > 0) {
        /* Refill tokens */
        int64_t add = (elapsed_us * rl->refill_rate) / 1000000;
        if (add > 0) {
            atomic_store(&rl->last_us, now);
            int64_t cur = atomic_load(&rl->tokens);
            int64_t new_val = cur + add;
            if (new_val > rl->max_tokens) new_val = rl->max_tokens;
            atomic_store(&rl->tokens, new_val);
        }
    }

    /* Try to consume */
    int64_t cur = atomic_load(&rl->tokens);
    if (cur >= 1000) {
        atomic_fetch_sub(&rl->tokens, 1000);
        return 1;
    }
    return 0;
}

/* Check tokens remaining without consuming. */
static inline int64_t mako_ratelimit_remaining(MakoRateLimiter *rl) {
    if (!rl) return 0;
    return atomic_load(&rl->tokens) / 1000;
}

static inline void mako_ratelimit_free(MakoRateLimiter *rl) { free(rl); }

/* ============================================================
 * Circuit Breaker — resilient service calls.
 * States: 0=CLOSED (normal), 1=OPEN (failing), 2=HALF_OPEN (testing)
 * ============================================================ */

#define CB_CLOSED    0
#define CB_OPEN      1
#define CB_HALF_OPEN 2

typedef struct {
    atomic_int state;
    atomic_int_fast64_t failures;
    atomic_int_fast64_t successes;
    atomic_int_fast64_t last_failure_us;
    int64_t threshold;      /* failures before opening */
    int64_t timeout_us;     /* how long to stay open before half-open */
    int64_t half_open_max;  /* successes needed to close from half-open */
} MakoCircuitBreaker;

static inline MakoCircuitBreaker *mako_breaker_new(int64_t threshold, int64_t timeout_ms, int64_t half_open_max) {
    MakoCircuitBreaker *cb = (MakoCircuitBreaker *)calloc(1, sizeof(MakoCircuitBreaker));
    if (!cb) return NULL;
    cb->threshold = threshold > 0 ? threshold : 5;
    cb->timeout_us = timeout_ms * 1000;
    cb->half_open_max = half_open_max > 0 ? half_open_max : 3;
    atomic_store(&cb->state, CB_CLOSED);
    atomic_store(&cb->failures, 0);
    atomic_store(&cb->successes, 0);
    atomic_store(&cb->last_failure_us, 0);
    return cb;
}

/* Check if a request is allowed. Returns 1=allow, 0=blocked (circuit open). */
static inline int64_t mako_breaker_allow(MakoCircuitBreaker *cb) {
    if (!cb) return 1;
    int state = atomic_load(&cb->state);

    if (state == CB_CLOSED) return 1;

    if (state == CB_OPEN) {
        /* Check if timeout elapsed → transition to half-open */
        int64_t now = mako_rl_now_us();
        int64_t last = atomic_load(&cb->last_failure_us);
        if (now - last > cb->timeout_us) {
            atomic_store(&cb->state, CB_HALF_OPEN);
            atomic_store(&cb->successes, 0);
            return 1;
        }
        return 0; /* still open */
    }

    /* HALF_OPEN: allow limited requests */
    return 1;
}

/* Report success. */
static inline void mako_breaker_success(MakoCircuitBreaker *cb) {
    if (!cb) return;
    int state = atomic_load(&cb->state);
    if (state == CB_HALF_OPEN) {
        int64_t s = atomic_fetch_add(&cb->successes, 1) + 1;
        if (s >= cb->half_open_max) {
            atomic_store(&cb->state, CB_CLOSED);
            atomic_store(&cb->failures, 0);
        }
    } else if (state == CB_CLOSED) {
        atomic_store(&cb->failures, 0);
    }
}

/* Report failure. */
static inline void mako_breaker_failure(MakoCircuitBreaker *cb) {
    if (!cb) return;
    int state = atomic_load(&cb->state);
    atomic_store(&cb->last_failure_us, mako_rl_now_us());

    if (state == CB_HALF_OPEN) {
        /* Any failure in half-open → reopen */
        atomic_store(&cb->state, CB_OPEN);
    } else if (state == CB_CLOSED) {
        int64_t f = atomic_fetch_add(&cb->failures, 1) + 1;
        if (f >= cb->threshold) {
            atomic_store(&cb->state, CB_OPEN);
        }
    }
}

/* Get current state: 0=closed, 1=open, 2=half-open */
static inline int64_t mako_breaker_state(MakoCircuitBreaker *cb) {
    return cb ? (int64_t)atomic_load(&cb->state) : 0;
}

static inline void mako_breaker_reset(MakoCircuitBreaker *cb) {
    if (!cb) return;
    atomic_store(&cb->state, CB_CLOSED);
    atomic_store(&cb->failures, 0);
    atomic_store(&cb->successes, 0);
}

static inline void mako_breaker_free(MakoCircuitBreaker *cb) { free(cb); }

/* ============================================================
 * JWT — explicit HS256/ES256 signing and RS256 verification.
 * `jwt_sign` is HS256 and `jwt_sign_es256` is ES256 with an explicit P-256
 * private key. RS256 uses separate
 * verify APIs so a caller cannot accidentally pass a shared secret to an RSA
 * path or accept an algorithm selected by an untrusted token header.
 * ============================================================ */

/* Base64url encode (no padding) */
static inline size_t mako_b64url_encode(const uint8_t *src, size_t slen, char *dst, size_t dlen) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t o = 0;
    for (size_t i = 0; i < slen && o + 4 < dlen; i += 3) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < slen) v |= (uint32_t)src[i+1] << 8;
        if (i + 2 < slen) v |= (uint32_t)src[i+2];
        dst[o++] = t[(v >> 18) & 63];
        dst[o++] = t[(v >> 12) & 63];
        if (i + 1 < slen) dst[o++] = t[(v >> 6) & 63];
        if (i + 2 < slen) dst[o++] = t[v & 63];
    }
    dst[o] = 0;
    return o;
}

static inline size_t mako_b64url_decode(const char *src, size_t slen, uint8_t *dst, size_t dlen) {
    static const int8_t t[256] = {
        [0 ... 255] = -1,
        ['A'] = 0, ['B'] = 1, ['C'] = 2, ['D'] = 3, ['E'] = 4, ['F'] = 5,
        ['G'] = 6, ['H'] = 7, ['I'] = 8, ['J'] = 9, ['K'] = 10, ['L'] = 11,
        ['M'] = 12, ['N'] = 13, ['O'] = 14, ['P'] = 15, ['Q'] = 16, ['R'] = 17,
        ['S'] = 18, ['T'] = 19, ['U'] = 20, ['V'] = 21, ['W'] = 22, ['X'] = 23,
        ['Y'] = 24, ['Z'] = 25,
        ['a'] = 26, ['b'] = 27, ['c'] = 28, ['d'] = 29, ['e'] = 30, ['f'] = 31,
        ['g'] = 32, ['h'] = 33, ['i'] = 34, ['j'] = 35, ['k'] = 36, ['l'] = 37,
        ['m'] = 38, ['n'] = 39, ['o'] = 40, ['p'] = 41, ['q'] = 42, ['r'] = 43,
        ['s'] = 44, ['t'] = 45, ['u'] = 46, ['v'] = 47, ['w'] = 48, ['x'] = 49,
        ['y'] = 50, ['z'] = 51,
        ['0'] = 52, ['1'] = 53, ['2'] = 54, ['3'] = 55, ['4'] = 56,
        ['5'] = 57, ['6'] = 58, ['7'] = 59, ['8'] = 60, ['9'] = 61,
        ['-'] = 62, ['_'] = 63,
    };
    size_t o = 0;
    for (size_t i = 0; i < slen && o < dlen;) {
        uint32_t v = 0;
        int bits = 0;
        for (int j = 0; j < 4 && i < slen; j++, i++) {
            int8_t d = t[(uint8_t)src[i]];
            if (d < 0) break;
            v = (v << 6) | (uint32_t)d;
            bits += 6;
        }
        if (bits >= 8) { dst[o++] = (uint8_t)(v >> (bits - 8)); bits -= 8; }
        if (bits >= 8 && o < dlen) { dst[o++] = (uint8_t)(v >> (bits - 8)); bits -= 8; }
        if (bits >= 8 && o < dlen) { dst[o++] = (uint8_t)(v >> (bits - 8)); }
    }
    return o;
}

/* Strict, canonical base64url decoder for security-sensitive JWT fields. */
static inline int8_t mako_jwt_b64url_value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return (int8_t)(c - 'A');
    if (c >= 'a' && c <= 'z') return (int8_t)(c - 'a' + 26);
    if (c >= '0' && c <= '9') return (int8_t)(c - '0' + 52);
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

static inline int mako_jwt_b64url_decode_strict(
    const char *src, size_t slen, uint8_t *dst, size_t dcap, size_t *out_len
) {
    if (!src || !dst || !out_len || slen == 0 || (slen & 3u) == 1u) return -1;
    size_t expected = (slen / 4) * 3;
    if ((slen & 3u) == 2u) expected += 1;
    else if ((slen & 3u) == 3u) expected += 2;
    if (expected == 0 || expected > dcap) return -1;
    size_t o = 0, i = 0;
    while (i + 4 <= slen) {
        int8_t a = mako_jwt_b64url_value((unsigned char)src[i++]);
        int8_t b = mako_jwt_b64url_value((unsigned char)src[i++]);
        int8_t c = mako_jwt_b64url_value((unsigned char)src[i++]);
        int8_t d = mako_jwt_b64url_value((unsigned char)src[i++]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return -1;
        uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12)
                   | ((uint32_t)c << 6) | (uint32_t)d;
        dst[o++] = (uint8_t)(v >> 16);
        dst[o++] = (uint8_t)(v >> 8);
        dst[o++] = (uint8_t)v;
    }
    size_t rem = slen - i;
    if (rem == 2) {
        int8_t a = mako_jwt_b64url_value((unsigned char)src[i]);
        int8_t b = mako_jwt_b64url_value((unsigned char)src[i + 1]);
        if (a < 0 || b < 0 || (b & 15) != 0) return -1;
        dst[o++] = (uint8_t)(((uint32_t)a << 2) | ((uint32_t)b >> 4));
    } else if (rem == 3) {
        int8_t a = mako_jwt_b64url_value((unsigned char)src[i]);
        int8_t b = mako_jwt_b64url_value((unsigned char)src[i + 1]);
        int8_t c = mako_jwt_b64url_value((unsigned char)src[i + 2]);
        if (a < 0 || b < 0 || c < 0 || (c & 3) != 0) return -1;
        uint32_t v = ((uint32_t)a << 12) | ((uint32_t)b << 6) | (uint32_t)c;
        dst[o++] = (uint8_t)(v >> 10);
        dst[o++] = (uint8_t)(v >> 2);
    } else if (rem != 0) {
        return -1;
    }
    if (o != expected) return -1;
    *out_len = o;
    return 0;
}

static inline size_t mako_jwt_json_ws(const char *s, size_t len, size_t pos) {
    while (pos < len && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n'))
        pos++;
    return pos;
}

/* Parse a JSON string. Non-ASCII escapes are rejected deliberately: JWT
 * metadata used for algorithm/key selection is ASCII by contract. */
static inline int mako_jwt_json_string(
    const char *s, size_t len, size_t *pos, char *out, size_t cap, size_t *out_len
) {
    if (!s || !pos || *pos >= len || s[*pos] != '"') return -1;
    size_t p = *pos + 1, n = 0;
    while (p < len) {
        unsigned char c = (unsigned char)s[p++];
        if (c == '"') {
            if (out && cap > 0) out[n < cap ? n : cap - 1] = 0;
            if (out_len) *out_len = n;
            *pos = p;
            return n < cap || !out ? 0 : -1;
        }
        if (c < 0x20) return -1;
        if (c == '\\') {
            if (p >= len) return -1;
            unsigned char esc = (unsigned char)s[p++];
            if (esc == '"' || esc == '\\' || esc == '/') c = esc;
            else if (esc == 'b') c = '\b';
            else if (esc == 'f') c = '\f';
            else if (esc == 'n') c = '\n';
            else if (esc == 'r') c = '\r';
            else if (esc == 't') c = '\t';
            else if (esc == 'u') return -1;
            else return -1;
        }
        if (out) {
            if (n + 1 >= cap) return -1;
            out[n] = (char)c;
        }
        n++;
    }
    return -1;
}

static inline int mako_jwt_json_number(const char *s, size_t len, size_t *pos) {
    size_t p = *pos;
    if (p < len && s[p] == '-') p++;
    if (p >= len) return -1;
    if (s[p] == '0') {
        p++;
        if (p < len && s[p] >= '0' && s[p] <= '9') return -1;
    } else {
        if (s[p] < '1' || s[p] > '9') return -1;
        while (p < len && s[p] >= '0' && s[p] <= '9') p++;
    }
    if (p < len && s[p] == '.') {
        p++;
        size_t fraction = p;
        while (p < len && s[p] >= '0' && s[p] <= '9') p++;
        if (p == fraction) return -1;
    }
    if (p < len && (s[p] == 'e' || s[p] == 'E')) {
        p++;
        if (p < len && (s[p] == '+' || s[p] == '-')) p++;
        size_t exponent = p;
        while (p < len && s[p] >= '0' && s[p] <= '9') p++;
        if (p == exponent) return -1;
    }
    *pos = p;
    return 0;
}

static inline int mako_jwt_json_skip_value_depth(
    const char *s, size_t len, size_t *pos, size_t depth
) {
    if (!s || !pos || depth > 64) return -1;
    *pos = mako_jwt_json_ws(s, len, *pos);
    if (*pos >= len) return -1;
    if (s[*pos] == '"') return mako_jwt_json_string(s, len, pos, NULL, 0, NULL);
    if (s[*pos] == '{') {
        (*pos)++;
        *pos = mako_jwt_json_ws(s, len, *pos);
        if (*pos < len && s[*pos] == '}') { (*pos)++; return 0; }
        for (;;) {
            if (*pos >= len || s[*pos] != '"'
                || mako_jwt_json_string(s, len, pos, NULL, 0, NULL) != 0)
                return -1;
            *pos = mako_jwt_json_ws(s, len, *pos);
            if (*pos >= len || s[(*pos)++] != ':') return -1;
            if (mako_jwt_json_skip_value_depth(s, len, pos, depth + 1) != 0) return -1;
            *pos = mako_jwt_json_ws(s, len, *pos);
            if (*pos >= len) return -1;
            if (s[*pos] == '}') { (*pos)++; return 0; }
            if (s[(*pos)++] != ',') return -1;
            *pos = mako_jwt_json_ws(s, len, *pos);
            if (*pos >= len || s[*pos] == '}') return -1;
        }
    }
    if (s[*pos] == '[') {
        (*pos)++;
        *pos = mako_jwt_json_ws(s, len, *pos);
        if (*pos < len && s[*pos] == ']') { (*pos)++; return 0; }
        for (;;) {
            if (mako_jwt_json_skip_value_depth(s, len, pos, depth + 1) != 0) return -1;
            *pos = mako_jwt_json_ws(s, len, *pos);
            if (*pos >= len) return -1;
            if (s[*pos] == ']') { (*pos)++; return 0; }
            if (s[(*pos)++] != ',') return -1;
            *pos = mako_jwt_json_ws(s, len, *pos);
            if (*pos >= len || s[*pos] == ']') return -1;
        }
    }
    if (len - *pos >= 4 && memcmp(s + *pos, "true", 4) == 0) {
        *pos += 4;
        return 0;
    }
    if (len - *pos >= 5 && memcmp(s + *pos, "false", 5) == 0) {
        *pos += 5;
        return 0;
    }
    if (len - *pos >= 4 && memcmp(s + *pos, "null", 4) == 0) {
        *pos += 4;
        return 0;
    }
    return mako_jwt_json_number(s, len, pos);
}

static inline int mako_jwt_json_skip_value(
    const char *s, size_t len, size_t *pos
) {
    return mako_jwt_json_skip_value_depth(s, len, pos, 0);
}

/* Return the span of a direct JSON object field's value. */
static inline int mako_jwt_json_field(
    const char *s, size_t len, const char *key,
    const char **value, size_t *value_len, int *present
) {
    if (present) *present = 0;
    if (!s || !key || !value || !value_len) return -1;
    size_t pos = mako_jwt_json_ws(s, len, 0);
    if (pos >= len || s[pos++] != '{') return -1;
    const char *found_value = NULL;
    size_t found_len = 0;
    int found = 0;
    int after_comma = 0;
    for (;;) {
        pos = mako_jwt_json_ws(s, len, pos);
        if (pos < len && s[pos] == '}') {
            if (after_comma) return -1;
            return mako_jwt_json_ws(s, len, pos + 1) == len ? 0 : -1;
        }
        char field[128];
        size_t field_len = 0;
        if (mako_jwt_json_string(s, len, &pos, field, sizeof(field), &field_len) != 0)
            return -1;
        pos = mako_jwt_json_ws(s, len, pos);
        if (pos >= len || s[pos++] != ':') return -1;
        pos = mako_jwt_json_ws(s, len, pos);
        size_t start = pos;
        if (mako_jwt_json_skip_value(s, len, &pos) != 0) return -1;
        if (strlen(key) == field_len && memcmp(field, key, field_len) == 0) {
            if (found) return -1; /* duplicate security-relevant field */
            found_value = s + start;
            found_len = pos - start;
            found = 1;
        }
        after_comma = 0;
        pos = mako_jwt_json_ws(s, len, pos);
        if (pos >= len) return -1;
        if (s[pos] == ',') { pos++; after_comma = 1; continue; }
        if (s[pos] == '}') {
            if (found) {
                *value = found_value;
                *value_len = found_len;
                if (present) *present = 1;
            }
            return mako_jwt_json_ws(s, len, pos + 1) == len ? 0 : -1;
        }
        return -1;
    }
}

static inline int mako_jwt_json_field_string(
    const char *s, size_t len, const char *key,
    char *out, size_t cap, int *present
) {
    const char *value = NULL;
    size_t value_len = 0;
    int found = 0;
    if (mako_jwt_json_field(s, len, key, &value, &value_len, &found) != 0) return -1;
    if (present) *present = found;
    if (!found) return 0;
    size_t pos = 0, n = 0;
    if (mako_jwt_json_string(value, value_len, &pos, out, cap, &n) != 0
        || mako_jwt_json_ws(value, value_len, pos) != value_len)
        return -1;
    return 0;
}

static inline int mako_jwt_json_array_has_string(
    const char *s, size_t len, const char *wanted
) {
    size_t pos = mako_jwt_json_ws(s, len, 0);
    if (pos >= len || s[pos++] != '[') return -1;
    int found = 0;
    int after_comma = 0;
    for (;;) {
        pos = mako_jwt_json_ws(s, len, pos);
        if (pos < len && s[pos] == ']') return after_comma ? -1 : 0;
        char value[128];
        size_t n = 0;
        if (mako_jwt_json_string(s, len, &pos, value, sizeof(value), &n) != 0) return -1;
        if (strlen(wanted) == n && memcmp(value, wanted, n) == 0) found = 1;
        after_comma = 0;
        pos = mako_jwt_json_ws(s, len, pos);
        if (pos >= len) return -1;
        if (s[pos] == ',') { pos++; after_comma = 1; continue; }
        if (s[pos] == ']') return found;
        return -1;
    }
}

static inline int mako_jwt_parts(
    MakoString token, size_t *first, size_t *second
) {
    if (!token.data || token.len < 5 || token.len > (1u << 20)) return -1;
    size_t a = SIZE_MAX, b = SIZE_MAX;
    for (size_t i = 0; i < token.len; i++) {
        if (token.data[i] != '.') continue;
        if (a == SIZE_MAX) a = i;
        else if (b == SIZE_MAX) b = i;
        else return -1;
    }
    if (a == SIZE_MAX || b == SIZE_MAX || a == 0 || b <= a + 1 || b + 1 >= token.len
        || a > 4096 || b - a - 1 > 16384 || token.len - b - 1 > 8192)
        return -1;
    if (first) *first = a;
    if (second) *second = b;
    return 0;
}

static inline int mako_jwt_header(
    MakoString token, char *header, size_t header_cap, char *kid, size_t kid_cap,
    int require_kid, size_t *first_dot, size_t *second_dot
) {
    size_t a = 0, b = 0;
    if (mako_jwt_parts(token, &a, &b) != 0 || !header || header_cap == 0) return -1;
    size_t header_len = 0;
    if (mako_jwt_b64url_decode_strict(
            token.data, a, (uint8_t *)header, header_cap - 1, &header_len) != 0)
        return -1;
    header[header_len] = 0;
    int has_alg = 0;
    char alg[16];
    if (mako_jwt_json_field_string(
            header, header_len, "alg", alg, sizeof(alg), &has_alg) != 0
        || !has_alg || strcmp(alg, "RS256") != 0) return -1;
    if (kid) {
        int has_kid = 0;
        if (mako_jwt_json_field_string(
                header, header_len, "kid", kid, kid_cap, &has_kid) != 0
            || (require_kid && (!has_kid || kid[0] == 0))) return -1;
    }
    if (first_dot) *first_dot = a;
    if (second_dot) *second_dot = b;
    return 0;
}

#if defined(MAKO_CLOUD_OPENSSL)
static inline int64_t mako_jwt_verify_pkey(
    MakoString token, size_t first_dot, size_t second_dot, EVP_PKEY *pkey
) {
    if (!pkey || EVP_PKEY_base_id(pkey) != EVP_PKEY_RSA
        || EVP_PKEY_get_bits(pkey) < 2048 || EVP_PKEY_get_bits(pkey) > 16384) return 0;
    uint8_t signature[8192];
    size_t signature_len = 0;
    if (mako_jwt_b64url_decode_strict(
            token.data + second_dot + 1, token.len - second_dot - 1,
            signature, sizeof(signature), &signature_len) != 0)
        return 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return 0;
    int ok = EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey) == 1
        && EVP_DigestVerifyUpdate(ctx, token.data, second_dot) == 1
        && EVP_DigestVerifyFinal(ctx, signature, signature_len) == 1;
    OPENSSL_cleanse(signature, sizeof(signature));
    EVP_MD_CTX_free(ctx);
    return ok ? 1 : 0;
}
#endif

/* Uses mako_hmac_sha256_raw from mako_std.h (included before this file) */

/* Sign a JWT payload with HMAC-SHA256. Returns "header.payload.signature". */
static inline MakoString mako_jwt_sign(MakoString payload, MakoString secret) {
    /* Header is always {"alg":"HS256","typ":"JWT"} */
    const char *hdr_json = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    size_t hdr_len = strlen(hdr_json);
    if (payload.len > 0 && !payload.data) return mako_str_from_cstr("");

    /* Base64url encode header and payload */
    char hdr_b64[128], pay_b64[8192];
    if (payload.len > 6000) return mako_str_from_cstr("");
    size_t hdr_b64_len = mako_b64url_encode((const uint8_t *)hdr_json, hdr_len, hdr_b64, sizeof(hdr_b64));
    size_t pay_b64_len = mako_b64url_encode((const uint8_t *)payload.data, payload.len, pay_b64, sizeof(pay_b64));

    /* Build signing input: header.payload */
    size_t input_len = hdr_b64_len + 1 + pay_b64_len;
    char *input = (char *)malloc(input_len + 1);
    if (!input) return mako_str_from_cstr("");
    memcpy(input, hdr_b64, hdr_b64_len);
    input[hdr_b64_len] = '.';
    memcpy(input + hdr_b64_len + 1, pay_b64, pay_b64_len);
    input[input_len] = 0;

    /* HMAC-SHA256 */
    MakoString input_str = {input, input_len};
    MakoString sig_raw = mako_hmac_sha256_raw(secret, input_str);
    if (!sig_raw.data || sig_raw.len != 32) {
        free(input);
        mako_str_free(sig_raw);
        return mako_str_from_cstr("");
    }

    /* Base64url encode signature */
    char sig_b64[128];
    size_t sig_b64_len = mako_b64url_encode((const uint8_t *)sig_raw.data, sig_raw.len, sig_b64, sizeof(sig_b64));

    /* Build final token: header.payload.signature */
    size_t total = input_len + 1 + sig_b64_len;
    char *token = (char *)malloc(total + 1);
    if (!token) {
        free(input);
        mako_str_free(sig_raw);
        return mako_str_from_cstr("");
    }
    memcpy(token, input, input_len);
    token[input_len] = '.';
    memcpy(token + input_len + 1, sig_b64, sig_b64_len);
    token[total] = 0;

    free(input);
    mako_str_free(sig_raw);
    return (MakoString){token, total};
}

/* Sign a JWT payload with ES256 using a PEM-encoded P-256 private key.
 * JWT ES256 signatures are the fixed-width R || S form, not DER. */
static inline MakoString mako_jwt_sign_es256(MakoString payload, MakoString private_key_pem) {
#if !defined(MAKO_CLOUD_OPENSSL)
    (void)payload; (void)private_key_pem;
    return mako_str_from_cstr("");
#else
    const char *hdr_json = "{\"alg\":\"ES256\",\"typ\":\"JWT\"}";
    size_t hdr_len = strlen(hdr_json);
    if (!payload.data || payload.len > 6000 || !private_key_pem.data
        || private_key_pem.len == 0 || private_key_pem.len > 16384)
        return mako_str_from_cstr("");

    char hdr_b64[128], pay_b64[8192];
    size_t hdr_b64_len = mako_b64url_encode(
        (const uint8_t *)hdr_json, hdr_len, hdr_b64, sizeof(hdr_b64));
    size_t pay_b64_len = mako_b64url_encode(
        (const uint8_t *)payload.data, payload.len, pay_b64, sizeof(pay_b64));
    if (hdr_b64_len == 0 || pay_b64_len == 0) return mako_str_from_cstr("");

    size_t input_len = hdr_b64_len + 1 + pay_b64_len;
    char *input = (char *)malloc(input_len + 1);
    if (!input) return mako_str_from_cstr("");
    memcpy(input, hdr_b64, hdr_b64_len);
    input[hdr_b64_len] = '.';
    memcpy(input + hdr_b64_len + 1, pay_b64, pay_b64_len);
    input[input_len] = 0;

    BIO *bio = BIO_new_mem_buf(private_key_pem.data, (int)private_key_pem.len);
    EVP_PKEY *pkey = bio ? PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL) : NULL;
    if (bio) BIO_free(bio);
    if (!pkey || EVP_PKEY_base_id(pkey) != EVP_PKEY_EC
        || EVP_PKEY_get_bits(pkey) != 256) {
        if (pkey) EVP_PKEY_free(pkey);
        free(input);
        return mako_str_from_cstr("");
    }

    EC_KEY *ec = EVP_PKEY_get1_EC_KEY(pkey);
    const EC_GROUP *group = ec ? EC_KEY_get0_group(ec) : NULL;
    int curve = group ? EC_GROUP_get_curve_name(group) : NID_undef;
    if (ec) EC_KEY_free(ec);
    if (curve != NID_X9_62_prime256v1) {
        EVP_PKEY_free(pkey);
        free(input);
        return mako_str_from_cstr("");
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    size_t der_len = 0;
    unsigned char *der = NULL;
    int ok = ctx && EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, pkey) == 1
        && EVP_DigestSignUpdate(ctx, input, input_len) == 1
        && EVP_DigestSignFinal(ctx, NULL, &der_len) == 1;
    if (ok) {
        der = (unsigned char *)malloc(der_len);
        ok = der && EVP_DigestSignFinal(ctx, der, &der_len) == 1;
    }

    unsigned char raw_sig[64];
    memset(raw_sig, 0, sizeof(raw_sig));
    if (ok) {
        const unsigned char *der_ptr = der;
        ECDSA_SIG *ecdsa = d2i_ECDSA_SIG(NULL, &der_ptr, (long)der_len);
        const BIGNUM *r = NULL;
        const BIGNUM *s = NULL;
        if (ecdsa) ECDSA_SIG_get0(ecdsa, &r, &s);
        ok = ecdsa && r && s
            && BN_num_bits(r) <= 256 && BN_num_bits(s) <= 256
            && BN_bn2binpad(r, raw_sig, 32) == 32
            && BN_bn2binpad(s, raw_sig + 32, 32) == 32;
        if (ecdsa) ECDSA_SIG_free(ecdsa);
    }

    char sig_b64[128];
    size_t sig_b64_len = ok
        ? mako_b64url_encode(raw_sig, sizeof(raw_sig), sig_b64, sizeof(sig_b64)) : 0;
    size_t total = input_len + 1 + sig_b64_len;
    char *token = ok && sig_b64_len > 0 ? (char *)malloc(total + 1) : NULL;
    if (token) {
        memcpy(token, input, input_len);
        token[input_len] = '.';
        memcpy(token + input_len + 1, sig_b64, sig_b64_len);
        token[total] = 0;
    }

    OPENSSL_cleanse(raw_sig, sizeof(raw_sig));
    if (der) {
        OPENSSL_cleanse(der, der_len);
        free(der);
    }
    if (ctx) EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    OPENSSL_cleanse(input, input_len + 1);
    free(input);
    return token ? (MakoString){token, total} : mako_str_from_cstr("");
#endif
}

/* Verify an ES256 JWT against a PEM-encoded P-256 public key. */
static inline int64_t mako_jwt_verify_es256(MakoString token, MakoString public_key_pem) {
#if !defined(MAKO_CLOUD_OPENSSL)
    (void)token; (void)public_key_pem;
    return 0;
#else
    size_t first_dot = 0, second_dot = 0;
    if (!public_key_pem.data || public_key_pem.len == 0 || public_key_pem.len > 16384
        || mako_jwt_parts(token, &first_dot, &second_dot) != 0)
        return 0;

    char header[4097];
    size_t header_len = 0;
    if (mako_jwt_b64url_decode_strict(
            token.data, first_dot, (uint8_t *)header, sizeof(header) - 1, &header_len) != 0)
        return 0;
    header[header_len] = 0;
    char alg[16];
    int has_alg = 0;
    if (mako_jwt_json_field_string(
            header, header_len, "alg", alg, sizeof(alg), &has_alg) != 0
        || !has_alg || strcmp(alg, "ES256") != 0)
        return 0;

    BIO *bio = BIO_new_mem_buf(public_key_pem.data, (int)public_key_pem.len);
    EVP_PKEY *pkey = bio ? PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL) : NULL;
    if (bio) BIO_free(bio);
    if (!pkey || EVP_PKEY_base_id(pkey) != EVP_PKEY_EC
        || EVP_PKEY_get_bits(pkey) != 256) {
        if (pkey) EVP_PKEY_free(pkey);
        return 0;
    }
    EC_KEY *ec = EVP_PKEY_get1_EC_KEY(pkey);
    const EC_GROUP *group = ec ? EC_KEY_get0_group(ec) : NULL;
    int curve = group ? EC_GROUP_get_curve_name(group) : NID_undef;
    if (ec) EC_KEY_free(ec);
    if (curve != NID_X9_62_prime256v1) {
        EVP_PKEY_free(pkey);
        return 0;
    }

    uint8_t raw_sig[64];
    size_t raw_len = 0;
    if (token.len - second_dot - 1 != 86
        || mako_jwt_b64url_decode_strict(
               token.data + second_dot + 1, token.len - second_dot - 1,
               raw_sig, sizeof(raw_sig), &raw_len) != 0
        || raw_len != sizeof(raw_sig)) {
        EVP_PKEY_free(pkey);
        return 0;
    }

    ECDSA_SIG *ecdsa = ECDSA_SIG_new();
    BIGNUM *r = ecdsa ? BN_bin2bn(raw_sig, 32, NULL) : NULL;
    BIGNUM *s = ecdsa ? BN_bin2bn(raw_sig + 32, 32, NULL) : NULL;
    if (!ecdsa || !r || !s || ECDSA_SIG_set0(ecdsa, r, s) != 1) {
        if (r) BN_free(r);
        if (s) BN_free(s);
        if (ecdsa) ECDSA_SIG_free(ecdsa);
        OPENSSL_cleanse(raw_sig, sizeof(raw_sig));
        EVP_PKEY_free(pkey);
        return 0;
    }

    int der_len = i2d_ECDSA_SIG(ecdsa, NULL);
    unsigned char *der = der_len > 0 ? (unsigned char *)malloc((size_t)der_len) : NULL;
    unsigned char *der_ptr = der;
    if (der) i2d_ECDSA_SIG(ecdsa, &der_ptr);
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    int ok = der && ctx
        && EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey) == 1
        && EVP_DigestVerifyUpdate(ctx, token.data, second_dot) == 1
        && EVP_DigestVerifyFinal(ctx, der, (size_t)der_len) == 1;

    if (ctx) EVP_MD_CTX_free(ctx);
    if (der) {
        OPENSSL_cleanse(der, (size_t)der_len);
        free(der);
    }
    ECDSA_SIG_free(ecdsa);
    OPENSSL_cleanse(raw_sig, sizeof(raw_sig));
    EVP_PKEY_free(pkey);
    return ok ? 1 : 0;
#endif
}

/* Verify a JWT token. Returns 1 if valid, 0 if invalid. */
static inline int64_t mako_jwt_verify(MakoString token, MakoString secret) {
    size_t first_dot = 0, second_dot = 0;
    if (mako_jwt_parts(token, &first_dot, &second_dot) != 0) return 0;
    char header[4097];
    size_t header_len = 0;
    if (mako_jwt_b64url_decode_strict(
            token.data, first_dot, (uint8_t *)header, sizeof(header) - 1, &header_len) != 0)
        return 0;
    header[header_len] = 0;
    char alg[16];
    int has_alg = 0;
    if (mako_jwt_json_field_string(
            header, header_len, "alg", alg, sizeof(alg), &has_alg) != 0
        || !has_alg || strcmp(alg, "HS256") != 0)
        return 0;

    MakoString input = {token.data, second_dot};

    /* Recompute HMAC */
    MakoString sig_raw = mako_hmac_sha256_raw(secret, input);
    char sig_b64[128];
    size_t sig_b64_len = mako_b64url_encode((const uint8_t *)sig_raw.data, sig_raw.len, sig_b64, sizeof(sig_b64));

    /* Compare with provided signature */
    const char *provided = token.data + second_dot + 1;
    size_t provided_len = token.len - second_dot - 1;
    uint8_t provided_raw[64];
    size_t provided_raw_len = 0;
    if (provided_len != sig_b64_len
        || mako_jwt_b64url_decode_strict(
               provided, provided_len, provided_raw, sizeof(provided_raw), &provided_raw_len) != 0
        || provided_raw_len != sig_raw.len) {
        mako_str_free(sig_raw);
        return 0;
    }

    /* Constant-time compare to prevent timing attacks */
    uint8_t diff = 0;
    for (size_t i = 0; i < sig_b64_len; i++) {
        diff |= (uint8_t)(sig_b64[i] ^ provided[i]);
    }
    mako_str_free(sig_raw);
    return diff == 0 ? 1 : 0;
}

/* Verify an RS256 JWT with a PEM SubjectPublicKeyInfo RSA public key. */
static inline int64_t mako_jwt_verify_rs256(MakoString token, MakoString public_key_pem) {
#if !defined(MAKO_CLOUD_OPENSSL)
    (void)token; (void)public_key_pem;
    return 0;
#else
    char header[4097];
    size_t first_dot = 0, second_dot = 0;
    if (!public_key_pem.data || public_key_pem.len == 0 || public_key_pem.len > 16384
        || mako_jwt_header(token, header, sizeof(header), NULL, 0, 0,
                           &first_dot, &second_dot) != 0)
        return 0;
    BIO *bio = BIO_new_mem_buf(public_key_pem.data, (int)public_key_pem.len);
    if (!bio) return 0;
    EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (!pkey) return 0;
    int64_t ok = mako_jwt_verify_pkey(token, first_dot, second_dot, pkey);
    EVP_PKEY_free(pkey);
    return ok;
#endif
}

/* Verify an RS256 JWT against a JWKS JSON document.  Exactly one RSA signing
 * key with the token's kid must match; ambiguous or metadata-incompatible sets
 * fail closed. */
static inline int64_t mako_jwt_verify_jwks(MakoString token, MakoString jwks_json) {
#if !defined(MAKO_CLOUD_OPENSSL)
    (void)token; (void)jwks_json;
    return 0;
#else
    if (!jwks_json.data || jwks_json.len == 0 || jwks_json.len > (1u << 20)) return 0;
    char header[4097], kid[256];
    size_t first_dot = 0, second_dot = 0;
    if (mako_jwt_header(token, header, sizeof(header), kid, sizeof(kid), 1,
                        &first_dot, &second_dot) != 0)
        return 0;
    const char *keys = NULL;
    size_t keys_len = 0;
    int has_keys = 0;
    if (mako_jwt_json_field(
            jwks_json.data, jwks_json.len, "keys", &keys, &keys_len, &has_keys) != 0
        || !has_keys) return 0;
    size_t pos = mako_jwt_json_ws(keys, keys_len, 0);
    if (pos >= keys_len || keys[pos++] != '[') return 0;
    EVP_PKEY *selected = NULL;
    size_t matches = 0;
    for (;;) {
        pos = mako_jwt_json_ws(keys, keys_len, pos);
        if (pos >= keys_len) break;
        if (keys[pos] == ']') { pos++; break; }
        size_t start = pos;
        if (mako_jwt_json_skip_value(keys, keys_len, &pos) != 0) goto done;
        const char *obj = keys + start;
        size_t obj_len = pos - start;
        if (obj_len == 0 || obj[0] != '{') goto next;

        char key_kid[256], kty[16], alg[16], use[16], n_b64[8192], e_b64[64];
        int has_kid = 0, has_kty = 0, has_alg = 0, has_use = 0, has_n = 0, has_e = 0;
        if (mako_jwt_json_field_string(obj, obj_len, "kid", key_kid, sizeof(key_kid), &has_kid) != 0
            || mako_jwt_json_field_string(obj, obj_len, "kty", kty, sizeof(kty), &has_kty) != 0
            || mako_jwt_json_field_string(obj, obj_len, "alg", alg, sizeof(alg), &has_alg) != 0
            || mako_jwt_json_field_string(obj, obj_len, "use", use, sizeof(use), &has_use) != 0
            || mako_jwt_json_field_string(obj, obj_len, "n", n_b64, sizeof(n_b64), &has_n) != 0
            || mako_jwt_json_field_string(obj, obj_len, "e", e_b64, sizeof(e_b64), &has_e) != 0)
            goto done;
        if (!has_kid || strcmp(key_kid, kid) != 0) goto next;
        matches++;
        if (!has_kty || strcmp(kty, "RSA") != 0 || (has_alg && strcmp(alg, "RS256") != 0)
            || (has_use && strcmp(use, "sig") != 0) || !has_n || !has_e) goto next;
        const char *ops = NULL;
        size_t ops_len = 0;
        int has_ops = 0;
        if (mako_jwt_json_field(obj, obj_len, "key_ops", &ops, &ops_len, &has_ops) != 0)
            goto done;
        if (has_ops) {
            int verifies = mako_jwt_json_array_has_string(ops, ops_len, "verify");
            if (verifies != 1) goto next;
        }
        uint8_t n_raw[4096], e_raw[16];
        size_t n_len = 0, e_len = 0;
        if (mako_jwt_b64url_decode_strict(
                n_b64, strlen(n_b64), n_raw, sizeof(n_raw), &n_len) != 0
            || mako_jwt_b64url_decode_strict(
                e_b64, strlen(e_b64), e_raw, sizeof(e_raw), &e_len) != 0
            || n_len < 256 || e_len == 0 || e_len > 8)
            goto next;
        RSA *rsa = RSA_new();
        BIGNUM *bn = BN_bin2bn(n_raw, (int)n_len, NULL);
        BIGNUM *be = BN_bin2bn(e_raw, (int)e_len, NULL);
        if (!rsa || !bn || !be || BN_is_negative(bn) || BN_is_negative(be)
            || BN_is_zero(be) || !BN_is_odd(be) || RSA_set0_key(rsa, bn, be, NULL) != 1) {
            if (rsa) RSA_free(rsa);
            if (bn) BN_free(bn);
            if (be) BN_free(be);
            goto next;
        }
        bn = NULL; be = NULL;
        EVP_PKEY *pkey = EVP_PKEY_new();
        if (!pkey || EVP_PKEY_assign_RSA(pkey, rsa) != 1) {
            if (pkey) EVP_PKEY_free(pkey);
            else RSA_free(rsa);
            goto next;
        }
        if (selected) {
            EVP_PKEY_free(pkey);
        } else {
            selected = pkey;
        }
next:
        pos = mako_jwt_json_ws(keys, keys_len, pos);
        if (pos < keys_len && keys[pos] == ',') { pos++; continue; }
        if (pos < keys_len && keys[pos] == ']') { pos++; break; }
        goto done;
    }
done:
    {
        int64_t ok = 0;
        if (matches == 1 && selected)
            ok = mako_jwt_verify_pkey(token, first_dot, second_dot, selected);
        if (selected) EVP_PKEY_free(selected);
        return ok;
    }
#endif
}

/* Extract payload from JWT (base64url-decoded). Does NOT verify! */
static inline MakoString mako_jwt_payload(MakoString token) {
    if (!token.data) return mako_str_from_cstr("");
    size_t first_dot = 0, second_dot = 0;
    if (mako_jwt_parts(token, &first_dot, &second_dot) != 0)
        return mako_str_from_cstr("");

    const char *pay_b64 = token.data + first_dot + 1;
    size_t pay_b64_len = second_dot - first_dot - 1;

    uint8_t decoded[16384];
    size_t dlen = 0;
    if (mako_jwt_b64url_decode_strict(
            pay_b64, pay_b64_len, decoded, sizeof(decoded), &dlen) != 0)
        return mako_str_from_cstr("");

    char *out = (char *)malloc(dlen + 1);
    if (!out) return mako_str_from_cstr("");
    memcpy(out, decoded, dlen);
    out[dlen] = 0;
    return (MakoString){out, dlen};
}

/* ============================================================
 * Retry with exponential backoff.
 * Helper: compute sleep duration for attempt N.
 * ============================================================ */

/* Returns milliseconds to sleep before retry N (0-based).
 * Formula: min(base_ms * 2^attempt, max_ms) + jitter */
static inline int64_t mako_backoff_ms(int64_t attempt, int64_t base_ms, int64_t max_ms) {
    int64_t delay = base_ms;
    for (int64_t i = 0; i < attempt && delay < max_ms; i++) {
        delay *= 2;
    }
    if (delay > max_ms) delay = max_ms;
    /* Add ~10% jitter */
    delay += (delay / 10) * ((int64_t)rand() % 3);
    return delay;
}

/* ============================================================
 * Secure environment / secrets helpers.
 * ============================================================ */

/* Get env var or return default. Never returns NULL. */
static inline MakoString mako_env_get_or(MakoString name, MakoString def) {
    char nbuf[512];
    if (!name.data || name.len >= sizeof(nbuf)) return def;
    memcpy(nbuf, name.data, name.len);
    nbuf[name.len] = 0;
    const char *val = getenv(nbuf);
    if (!val || val[0] == 0) return def;
    return mako_str_from_cstr(val);
}

/* Check if env var is set (non-empty). */
static inline int64_t mako_env_has(MakoString name) {
    char nbuf[512];
    if (!name.data || name.len >= sizeof(nbuf)) return 0;
    memcpy(nbuf, name.data, name.len);
    nbuf[name.len] = 0;
    const char *val = getenv(nbuf);
    return (val && val[0] != 0) ? 1 : 0;
}

#endif /* MAKO_CLOUD_H */
