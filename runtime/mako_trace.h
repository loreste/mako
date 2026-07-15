/* Distributed tracing — 128-bit trace ids, TLS context, span ring, OTLP JSON. */
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

#define MAKO_TRACE_SPAN_RING 32

typedef struct {
    uint64_t hi;
    uint64_t lo;
    int active;
    char name[64];
    int64_t start_ms;
    int64_t start_ns;
    uint64_t span_id;
    uint64_t parent_span_id;
} MakoTraceCtx;

typedef struct {
    uint64_t hi;
    uint64_t lo;
    uint64_t span_id;
    uint64_t parent_span_id;
    char name[64];
    int64_t start_ns;
    int64_t end_ns;
    int used;
} MakoTraceSpanRec;

static __thread MakoTraceCtx mako_trace_tls = {0, 0, 0, {0}, 0, 0, 0, 0};
static __thread char mako_trace_id_hex[33];
static __thread char mako_span_id_hex[17];

static MakoTraceSpanRec mako_trace_span_ring[MAKO_TRACE_SPAN_RING];
static atomic_llong mako_trace_span_next = 0;
static atomic_ullong mako_trace_span_seq = 1;

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

static inline void mako_trace_format_span(uint64_t id, char out[17]) {
    static const char *hexd = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        unsigned shift = (unsigned)(56 - i * 8);
        unsigned byte = (unsigned)((id >> shift) & 0xffu);
        out[i * 2] = hexd[byte >> 4];
        out[i * 2 + 1] = hexd[byte & 0xf];
    }
    out[16] = 0;
}

static inline uint64_t mako_trace_new_span_id(void) {
    unsigned long long s =
        atomic_fetch_add_explicit(&mako_trace_span_seq, 1, memory_order_relaxed);
    uint64_t t = (uint64_t)mako_now_ns();
    uint64_t id = ((uint64_t)s << 32) ^ t ^ (t >> 17) ^ 0x9e3779b97f4a7c15ULL;
    if (id == 0) id = 1;
    return id;
}

static inline void mako_trace_record_span(void) {
    if (!mako_trace_tls.active || mako_trace_tls.span_id == 0) return;
    long long idx =
        atomic_fetch_add_explicit(&mako_trace_span_next, 1, memory_order_relaxed);
    MakoTraceSpanRec *r =
        &mako_trace_span_ring[(size_t)idx % (size_t)MAKO_TRACE_SPAN_RING];
    r->hi = mako_trace_tls.hi;
    r->lo = mako_trace_tls.lo;
    r->span_id = mako_trace_tls.span_id;
    r->parent_span_id = mako_trace_tls.parent_span_id;
    memcpy(r->name, mako_trace_tls.name, sizeof(r->name));
    r->start_ns = mako_trace_tls.start_ns;
    r->end_ns = mako_now_ns();
    if (r->end_ns < r->start_ns) r->end_ns = r->start_ns;
    r->used = 1;
}

/* Generate and install a new trace id; returns owned hex string. */
static inline MakoString mako_trace_id(void) {
    uint64_t hi = 0, lo = 0;
    mako_trace_rand128(&hi, &lo);
    mako_trace_tls.hi = hi;
    mako_trace_tls.lo = lo;
    mako_trace_tls.active = 1;
    mako_trace_tls.span_id = 0;
    mako_trace_tls.parent_span_id = 0;
    mako_trace_format_id(hi, lo, mako_trace_id_hex);
    return mako_str_from_cstr(mako_trace_id_hex);
}

/* Current trace id hex, or empty if none. */
static inline MakoString mako_trace_current(void) {
    if (!mako_trace_tls.active) return mako_str_from_cstr("");
    mako_trace_format_id(mako_trace_tls.hi, mako_trace_tls.lo, mako_trace_id_hex);
    return mako_str_from_cstr(mako_trace_id_hex);
}

/* Current span id hex (16 chars), or empty. */
static inline MakoString mako_trace_span_id(void) {
    if (!mako_trace_tls.active || mako_trace_tls.span_id == 0) {
        return mako_str_from_cstr("");
    }
    mako_trace_format_span(mako_trace_tls.span_id, mako_span_id_hex);
    return mako_str_from_cstr(mako_span_id_hex);
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
    /* Nesting seed: current span becomes parent of the new one. */
    mako_trace_tls.parent_span_id = mako_trace_tls.span_id;
    mako_trace_tls.span_id = mako_trace_new_span_id();
    size_t n = name.len < 63 ? name.len : 63;
    if (name.data && n) memcpy(mako_trace_tls.name, name.data, n);
    mako_trace_tls.name[n] = 0;
    mako_trace_tls.start_ms = mako_now_ms();
    mako_trace_tls.start_ns = mako_now_ns();
    return 1;
}

/* End span; returns duration ms. Emits one JSON line to stderr if active. */
static inline int64_t mako_trace_end(void) {
    if (!mako_trace_tls.active) return -1;
    int64_t dur = mako_now_ms() - mako_trace_tls.start_ms;
    if (dur < 0) dur = 0;
    mako_trace_record_span();
    mako_trace_format_id(mako_trace_tls.hi, mako_trace_tls.lo, mako_trace_id_hex);
    mako_trace_format_span(mako_trace_tls.span_id, mako_span_id_hex);
    fprintf(stderr,
            "{\"trace\":\"%s\",\"spanId\":\"%s\",\"span\":\"%s\",\"duration_ms\":%lld}\n",
            mako_trace_id_hex,
            mako_span_id_hex,
            mako_trace_tls.name[0] ? mako_trace_tls.name : "",
            (long long)dur);
    /* Pop to parent for shallow nesting. */
    mako_trace_tls.span_id = mako_trace_tls.parent_span_id;
    mako_trace_tls.parent_span_id = 0;
    mako_trace_tls.name[0] = 0;
    mako_trace_tls.start_ms = 0;
    mako_trace_tls.start_ns = 0;
    return dur;
}

/* OTel-ish JSON for the current trace context (span-lite). */
static inline MakoString mako_trace_export_json(void) {
    char buf[640];
    if (!mako_trace_tls.active) {
        return mako_str_from_cstr(
            "{\"traceId\":\"\",\"spanId\":\"\",\"name\":\"\",\"active\":0}");
    }
    mako_trace_format_id(mako_trace_tls.hi, mako_trace_tls.lo, mako_trace_id_hex);
    mako_trace_format_span(mako_trace_tls.span_id, mako_span_id_hex);
    int n = snprintf(
        buf, sizeof(buf),
        "{\"traceId\":\"%s\",\"spanId\":\"%s\",\"name\":\"%s\",\"startMs\":%" PRId64
        ",\"startNs\":%" PRId64 ",\"active\":1}",
        mako_trace_id_hex,
        mako_trace_tls.span_id ? mako_span_id_hex : "",
        mako_trace_tls.name[0] ? mako_trace_tls.name : "",
        mako_trace_tls.start_ms,
        mako_trace_tls.start_ns
    );
    if (n < 0) return mako_str_from_cstr("{}");
    return mako_str_from_cstr(buf);
}

/* OTLP/HTTP JSON export (seed) for completed spans in the ring buffer.
 * Compatible with collectors that accept application/json OTLP. */
static inline MakoString mako_trace_export_otlp_json(void) {
    size_t cap = 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        fprintf(stderr, "mako: OOM in trace_export_otlp_json\n");
        abort();
    }
    size_t len = 0;
    int n = snprintf(
        buf, cap,
        "{\"resourceSpans\":[{\"resource\":{\"attributes\":["
        "{\"key\":\"service.name\",\"value\":{\"stringValue\":\"mako\"}}"
        "]},\"scopeSpans\":[{\"scope\":{\"name\":\"mako.trace\",\"version\":\"0.1.2\"},"
        "\"spans\":["
    );
    if (n < 0) n = 0;
    len = (size_t)n;

    int first = 1;
    for (int i = 0; i < MAKO_TRACE_SPAN_RING; i++) {
        MakoTraceSpanRec *r = &mako_trace_span_ring[i];
        if (!r->used) continue;
        char tid[33];
        char sid[17];
        char pid[17];
        mako_trace_format_id(r->hi, r->lo, tid);
        mako_trace_format_span(r->span_id, sid);
        if (r->parent_span_id) {
            mako_trace_format_span(r->parent_span_id, pid);
        } else {
            pid[0] = 0;
        }
        /* Escape name for JSON (names are short ASCII). */
        char ename[128];
        size_t ej = 0;
        for (size_t k = 0; r->name[k] && ej + 2 < sizeof(ename); k++) {
            char c = r->name[k];
            if (c == '"' || c == '\\') {
                ename[ej++] = '\\';
                ename[ej++] = c;
            } else if ((unsigned char)c < 0x20) {
                ename[ej++] = '?';
            } else {
                ename[ej++] = c;
            }
        }
        ename[ej] = 0;

        if (len + 512 >= cap) {
            cap *= 2;
            char *next = (char *)realloc(buf, cap);
            if (!next) {
                free(buf);
                fprintf(stderr, "mako: OOM in trace_export_otlp_json grow\n");
                abort();
            }
            buf = next;
        }
        n = snprintf(
            buf + len,
            cap > len ? cap - len : 0,
            "%s{\"traceId\":\"%s\",\"spanId\":\"%s\"%s%s%s,\"name\":\"%s\","
            "\"kind\":1,\"startTimeUnixNano\":\"%" PRId64
            "\",\"endTimeUnixNano\":\"%" PRId64 "\"}",
            first ? "" : ",",
            tid,
            sid,
            r->parent_span_id ? ",\"parentSpanId\":\"" : "",
            r->parent_span_id ? pid : "",
            r->parent_span_id ? "\"" : "",
            ename,
            r->start_ns,
            r->end_ns
        );
        if (n < 0) break;
        len += (size_t)n;
        first = 0;
    }

    /* Also emit the in-flight span if any. */
    if (mako_trace_tls.active && mako_trace_tls.span_id != 0
        && mako_trace_tls.name[0]) {
        char tid[33];
        char sid[17];
        mako_trace_format_id(mako_trace_tls.hi, mako_trace_tls.lo, tid);
        mako_trace_format_span(mako_trace_tls.span_id, sid);
        if (len + 400 >= cap) {
            cap *= 2;
            char *next = (char *)realloc(buf, cap);
            if (!next) {
                free(buf);
                fprintf(stderr, "mako: OOM in trace_export_otlp_json active\n");
                abort();
            }
            buf = next;
        }
        int64_t end_ns = mako_now_ns();
        n = snprintf(
            buf + len,
            cap > len ? cap - len : 0,
            "%s{\"traceId\":\"%s\",\"spanId\":\"%s\",\"name\":\"%s\","
            "\"kind\":1,\"startTimeUnixNano\":\"%" PRId64
            "\",\"endTimeUnixNano\":\"%" PRId64 "\"}",
            first ? "" : ",",
            tid,
            sid,
            mako_trace_tls.name,
            mako_trace_tls.start_ns,
            end_ns
        );
        if (n > 0) len += (size_t)n;
    }

    const char *tail = "]}]}}";
    size_t tlen = strlen(tail);
    if (len + tlen + 1 >= cap) {
        cap = len + tlen + 8;
        char *next = (char *)realloc(buf, cap);
        if (!next) {
            free(buf);
            fprintf(stderr, "mako: OOM in trace_export_otlp_json tail\n");
            abort();
        }
        buf = next;
    }
    memcpy(buf + len, tail, tlen + 1);
    len += tlen;
    return (MakoString){buf, len};
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
