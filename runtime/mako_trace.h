/* Distributed tracing seed — 128-bit trace ids, TLS context, span begin/end. */
#ifndef MAKO_TRACE_H
#define MAKO_TRACE_H

#include "mako_rt.h"
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t hi;
    uint64_t lo;
    int active;
    char name[64];
    int64_t start_ms;
} MakoTraceCtx;

static __thread MakoTraceCtx mako_trace_tls = {0, 0, 0, {0}, 0};
static __thread char mako_trace_id_hex[33];

static inline void mako_trace_rand128(uint64_t *hi, uint64_t *lo) {
#if !defined(_WIN32)
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        uint64_t buf[2] = {0, 0};
        if (read(fd, buf, sizeof(buf)) == (ssize_t)sizeof(buf)) {
            *hi = buf[0];
            *lo = buf[1];
            close(fd);
            return;
        }
        close(fd);
    }
#endif
    /* Fallback: mix time + stack address */
    uint64_t t = (uint64_t)mako_now_ms();
    *hi = t ^ (t << 17) ^ (uint64_t)(uintptr_t)hi;
    *lo = (t * 0x9e3779b97f4a7c15ULL) ^ (uint64_t)(uintptr_t)lo;
}

static inline void mako_trace_format_id(uint64_t hi, uint64_t lo, char out[33]) {
    static const char *hexd = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        unsigned shift = (unsigned)(56 - i * 8);
        unsigned byte = (unsigned)((hi >> shift) & 0xffu);
        out[i * 2] = hexd[byte >> 4];
        out[i * 2 + 1] = hexd[byte & 0xf];
    }
    for (int i = 0; i < 8; i++) {
        unsigned shift = (unsigned)(56 - i * 8);
        unsigned byte = (unsigned)((lo >> shift) & 0xffu);
        out[16 + i * 2] = hexd[byte >> 4];
        out[16 + i * 2 + 1] = hexd[byte & 0xf];
    }
    out[32] = 0;
}

/* Generate and install a new trace id; returns owned hex string. */
static inline MakoString mako_trace_id(void) {
    uint64_t hi = 0, lo = 0;
    mako_trace_rand128(&hi, &lo);
    mako_trace_tls.hi = hi;
    mako_trace_tls.lo = lo;
    mako_trace_tls.active = 1;
    mako_trace_format_id(hi, lo, mako_trace_id_hex);
    return mako_str_from_cstr(mako_trace_id_hex);
}

/* Current trace id hex, or empty if none. */
static inline MakoString mako_trace_current(void) {
    if (!mako_trace_tls.active) return mako_str_from_cstr("");
    mako_trace_format_id(mako_trace_tls.hi, mako_trace_tls.lo, mako_trace_id_hex);
    return mako_str_from_cstr(mako_trace_id_hex);
}

/* Install hex id (32 hex chars) as current context. Returns 1 ok. */
static inline int64_t mako_trace_set(MakoString hex) {
    if (!hex.data || hex.len != 32) return 0;
    uint64_t hi = 0, lo = 0;
    for (int i = 0; i < 16; i++) {
        char c = hex.data[i];
        int v = (c >= '0' && c <= '9') ? c - '0'
              : (c >= 'a' && c <= 'f') ? c - 'a' + 10
              : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (v < 0) return 0;
        hi = (hi << 4) | (uint64_t)v;
    }
    for (int i = 16; i < 32; i++) {
        char c = hex.data[i];
        int v = (c >= '0' && c <= '9') ? c - '0'
              : (c >= 'a' && c <= 'f') ? c - 'a' + 10
              : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (v < 0) return 0;
        lo = (lo << 4) | (uint64_t)v;
    }
    mako_trace_tls.hi = hi;
    mako_trace_tls.lo = lo;
    mako_trace_tls.active = 1;
    return 1;
}

static inline int64_t mako_trace_clear(void) {
    memset(&mako_trace_tls, 0, sizeof(mako_trace_tls));
    return 1;
}

/* Begin a named span under current (or new) trace. */
static inline int64_t mako_trace_begin(MakoString name) {
    if (!mako_trace_tls.active) {
        MakoString id = mako_trace_id();
        mako_str_free(id);
    }
    size_t n = name.len < 63 ? name.len : 63;
    if (name.data && n) memcpy(mako_trace_tls.name, name.data, n);
    mako_trace_tls.name[n] = 0;
    mako_trace_tls.start_ms = mako_now_ms();
    return 1;
}

/* End span; returns duration ms. Emits one JSON line to stderr if active. */
static inline int64_t mako_trace_end(void) {
    if (!mako_trace_tls.active) return -1;
    int64_t dur = mako_now_ms() - mako_trace_tls.start_ms;
    if (dur < 0) dur = 0;
    mako_trace_format_id(mako_trace_tls.hi, mako_trace_tls.lo, mako_trace_id_hex);
    fprintf(stderr,
            "{\"trace\":\"%s\",\"span\":\"%s\",\"duration_ms\":%lld}\n",
            mako_trace_id_hex,
            mako_trace_tls.name[0] ? mako_trace_tls.name : "",
            (long long)dur);
    mako_trace_tls.name[0] = 0;
    mako_trace_tls.start_ms = 0;
    return dur;
}

/* Structured log line with current trace context. */
/* OTel-ish JSON for the current trace context (span-lite). */
static inline MakoString mako_trace_export_json(void) {
    char buf[512];
    if (!mako_trace_tls.active) {
        return mako_str_from_cstr("{\"traceId\":\"\",\"name\":\"\",\"active\":0}");
    }
    mako_trace_format_id(mako_trace_tls.hi, mako_trace_tls.lo, mako_trace_id_hex);
    int n = snprintf(
        buf, sizeof(buf),
        "{\"traceId\":\"%s\",\"name\":\"%s\",\"startMs\":%" PRId64 ",\"active\":1}",
        mako_trace_id_hex,
        mako_trace_tls.name[0] ? mako_trace_tls.name : "",
        mako_trace_tls.start_ms
    );
    if (n < 0) return mako_str_from_cstr("{}");
    return mako_str_from_cstr(buf);
}

static inline int64_t mako_trace_log(MakoString msg) {
    mako_trace_format_id(
        mako_trace_tls.active ? mako_trace_tls.hi : 0,
        mako_trace_tls.active ? mako_trace_tls.lo : 0,
        mako_trace_id_hex
    );
    fprintf(stderr,
            "{\"trace\":\"%s\",\"msg\":\"%.*s\"}\n",
            mako_trace_tls.active ? mako_trace_id_hex : "",
            (int)(msg.len > 200 ? 200 : msg.len),
            msg.data ? msg.data : "");
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_TRACE_H */
