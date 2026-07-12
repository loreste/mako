/* Mako runtime — structured concurrency + parallel map */
#ifndef MAKO_RT_H
#define MAKO_RT_H

#ifndef _WIN32
/* _GNU_SOURCE (glibc) exposes splice/accept4 used by the proxy hot path; it
 * implies _DEFAULT_SOURCE/_POSIX_C_SOURCE. Must precede any system header. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#endif
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <stdatomic.h>
#include "mako_platform.h"
#if !defined(_WIN32)
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Lightweight runtime observability ---- */
static atomic_llong mako_rt_tasks_spawned = 0;
static atomic_llong mako_rt_tasks_joined = 0;
static atomic_llong mako_rt_channels_created = 0;
static atomic_llong mako_rt_channel_sends = 0;
static atomic_llong mako_rt_channel_try_send_drops = 0;
static atomic_llong mako_rt_channel_recvs = 0;
static atomic_llong mako_rt_channel_select_timeouts = 0;
static atomic_llong mako_rt_channel_peak_depth = 0;

static inline void mako_rt_counter_inc(atomic_llong *counter) {
    atomic_fetch_add_explicit(counter, 1, memory_order_relaxed);
}

static inline void mako_rt_observe_channel_depth(size_t depth) {
    long long d = (long long)depth;
    long long old = atomic_load_explicit(&mako_rt_channel_peak_depth, memory_order_relaxed);
    while (d > old
           && !atomic_compare_exchange_weak_explicit(
               &mako_rt_channel_peak_depth,
               &old,
               d,
               memory_order_relaxed,
               memory_order_relaxed
           )) {
    }
}

static inline void mako_runtime_stats_reset(void) {
    atomic_store_explicit(&mako_rt_tasks_spawned, 0, memory_order_relaxed);
    atomic_store_explicit(&mako_rt_tasks_joined, 0, memory_order_relaxed);
    atomic_store_explicit(&mako_rt_channels_created, 0, memory_order_relaxed);
    atomic_store_explicit(&mako_rt_channel_sends, 0, memory_order_relaxed);
    atomic_store_explicit(&mako_rt_channel_try_send_drops, 0, memory_order_relaxed);
    atomic_store_explicit(&mako_rt_channel_recvs, 0, memory_order_relaxed);
    atomic_store_explicit(&mako_rt_channel_select_timeouts, 0, memory_order_relaxed);
    atomic_store_explicit(&mako_rt_channel_peak_depth, 0, memory_order_relaxed);
}

/* ---- Strings (owned, null-terminated) ----
 * MakoString is the primary string type. Strings own their buffer (heap-allocated,
 * NUL-terminated). `data` is never NULL for owned strings. `len` does not count
 * the trailing NUL. Free with free(s.data) when done, or let scope/arena handle it.
 */
typedef struct {
    char *data;
    size_t len;
} MakoString;

static inline MakoString mako_str_from_cstr(const char *s);

#include "mako_security.h"

/* Create an owned MakoString by copying a C string. NULL is treated as "". */
static inline MakoString mako_str_from_cstr(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (!d) {
        fprintf(stderr, "mako: OOM in str_from_cstr\n");
        abort();
    }
    memcpy(d, s, n + 1);
    MakoString out = {d, n};
    return out;
}

static inline MakoString mako_runtime_stats_json(void) {
    char *d = (char *)malloc(384);
    if (!d) {
        fprintf(stderr, "mako: OOM in runtime_stats_json\n");
        abort();
    }
    int n = snprintf(
        d,
        384,
        "{\"tasks_spawned\":%lld,\"tasks_joined\":%lld,\"channels_created\":%lld,"
        "\"channel_sends\":%lld,\"channel_try_send_drops\":%lld,\"channel_recvs\":%lld,"
        "\"channel_select_timeouts\":%lld,\"channel_peak_depth\":%lld}",
        atomic_load_explicit(&mako_rt_tasks_spawned, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_tasks_joined, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channels_created, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channel_sends, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channel_try_send_drops, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channel_recvs, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channel_select_timeouts, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channel_peak_depth, memory_order_relaxed)
    );
    if (n < 0) n = 0;
    return (MakoString){d, (size_t)n};
}

/* Borrowed (non-owning) view of [p, p+n). Caller must keep the underlying
 * storage alive for the lifetime of the returned MakoString. Do NOT free. */
static inline MakoString mako_str_view(const char *p, size_t n) {
    static char empty_ch = 0;
    MakoString out = {(char *)(uintptr_t)(p ? p : &empty_ch), n};
    return out;
}

static inline MakoString mako_str_concat(MakoString a, MakoString b) {
    char *d = (char *)malloc(a.len + b.len + 1);
    memcpy(d, a.data, a.len);
    memcpy(d + a.len, b.data, b.len);
    d[a.len + b.len] = 0;
    MakoString out = {d, a.len + b.len};
    return out;
}

static inline void mako_print_str(MakoString s) {
    fwrite(s.data, 1, s.len, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static inline void mako_print_int(int64_t n) {
    printf("%lld\n", (long long)n);
    fflush(stdout);
}

static inline void mako_print_float(double n) {
    printf("%g\n", n);
}

static inline void mako_print_bool(bool b) {
    puts(b ? "true" : "false");
}

/* ---- Dynamic int arrays ----
 * Growable int64 slice. `len` is the number of live elements, `cap` is the
 * allocated capacity. Append grows with 2x strategy. Thread-unsafe — protect
 * with hold/share at the Mako level.
 */
typedef struct {
    int64_t *data;
    size_t len;
    size_t cap;
} MakoIntArray;

static inline MakoIntArray mako_int_array_new(size_t n) {
    MakoIntArray a;
    a.data = (int64_t *)calloc(n ? n : 1, sizeof(int64_t));
    a.len = n;
    a.cap = n ? n : 1;
    return a;
}

static inline MakoIntArray mako_int_array_of(const int64_t *vals, size_t n) {
    MakoIntArray a = mako_int_array_new(n);
    if (n) memcpy(a.data, vals, n * sizeof(int64_t));
    return a;
}

/* Go-like make([]int, len) / make([]int, len, cap).
 * Allocates `cap` slots, zeroes the first `len`. Aborts on OOM. */
static inline MakoIntArray mako_int_array_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    size_t c = (size_t)(cap ? cap : 1);
    size_t l = (size_t)len;
    /* malloc + zero only the live prefix — avoid zeroing unused capacity (CPU+RSS). */
    int64_t *data = (int64_t *)malloc(c * sizeof(int64_t));
    if (!data) {
        fprintf(stderr, "mako: OOM in int_array_make\n");
        abort();
    }
    if (l) memset(data, 0, l * sizeof(int64_t));
    MakoIntArray a;
    a.data = data;
    a.len = l;
    a.cap = c;
    return a;
}

static inline void mako_abort(const char *msg); /* defined below */
static inline MakoString mako_str_clone(MakoString s); /* defined below */

/* ---- Go-like []byte (uint8) ----
 * Growable byte slice. Same semantics as MakoIntArray but for uint8.
 * Used for raw I/O buffers, binary data, and byte-level string operations.
 */
typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} MakoByteArray;

static inline MakoByteArray mako_byte_array_new(size_t n) {
    MakoByteArray a;
    a.data = (uint8_t *)calloc(n ? n : 1, 1);
    a.len = n;
    a.cap = n ? n : 1;
    return a;
}

static inline MakoByteArray mako_byte_array_of(const int64_t *vals, size_t n) {
    MakoByteArray a = mako_byte_array_new(n);
    for (size_t i = 0; i < n; i++) {
        int64_t v = vals[i];
        if (v < 0 || v > 255) mako_abort("byte literal out of range 0..255");
        a.data[i] = (uint8_t)v;
    }
    return a;
}

static inline MakoByteArray mako_byte_array_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    size_t c = (size_t)(cap ? cap : 1);
    size_t l = (size_t)len;
    uint8_t *data = (uint8_t *)malloc(c);
    if (!data) {
        fprintf(stderr, "mako: OOM in byte_array_make\n");
        abort();
    }
    if (l) memset(data, 0, l);
    MakoByteArray a;
    a.data = data;
    a.len = l;
    a.cap = c;
    return a;
}

static inline int64_t mako_byte_array_len(MakoByteArray a) { return (int64_t)a.len; }
static inline int64_t mako_byte_array_cap(MakoByteArray a) { return (int64_t)a.cap; }

static inline int64_t mako_byte_get(MakoByteArray a, int64_t i) {
#ifndef NDEBUG
    if (i < 0 || (size_t)i >= a.len) mako_abort("byte index out of bounds");
#endif
    return (int64_t)a.data[i];
}

static inline void mako_byte_set(MakoByteArray a, int64_t i, int64_t v) {
#ifndef NDEBUG
    if (i < 0 || (size_t)i >= a.len) mako_abort("byte index out of bounds");
    if (v < 0 || v > 255) mako_abort("byte value out of range 0..255");
#endif
    a.data[i] = (uint8_t)v;
}

static inline MakoByteArray mako_byte_append(MakoByteArray s, int64_t v) {
#ifndef NDEBUG
    if (v < 0 || v > 255) mako_abort("byte value out of range 0..255");
#endif
    if (s.len < s.cap) {
        s.data[s.len++] = (uint8_t)v;
        return s;
    }
    size_t ncap = s.cap ? s.cap * 2 : 1;
    if (ncap < s.len + 1) ncap = s.len + 1;
    /* Fresh backing on grow (never free the old) — see mako_slice_append. */
    uint8_t *nd = (uint8_t *)malloc(ncap);
    if (!nd) mako_abort("append: out of memory");
    if (s.len) memcpy(nd, s.data, s.len);
    s.data = nd;
    s.cap = ncap;
    s.data[s.len++] = (uint8_t)v;
    return s;
}

static inline MakoByteArray mako_byte_slice_expr(
    MakoByteArray s, int64_t low, int64_t high, int64_t max, int has_max
) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoByteArray out;
    out.data = s.data + (size_t)low;
    out.len = (size_t)(high - low);
    if (has_max) {
        if (max < high) max = high;
        if (max > cap) max = cap;
        if (max < low) max = low;
        out.cap = (size_t)(max - low);
    } else {
        out.cap = (size_t)(cap - low);
    }
    return out;
}

static inline int64_t mako_byte_copy(MakoByteArray dst, MakoByteArray src) {
    size_t n = dst.len < src.len ? dst.len : src.len;
    if (n == 0) return 0;
    memmove(dst.data, src.data, n);
    return (int64_t)n;
}

/* string <-> []byte (copying; Go-like bridge) */
static inline MakoByteArray mako_bytes_from_string(MakoString s) {
    MakoByteArray a = mako_byte_array_new(s.len);
    if (s.len && s.data) memcpy(a.data, s.data, s.len);
    return a;
}

static inline MakoString mako_string_from_bytes(MakoByteArray a) {
    char *d = (char *)malloc(a.len + 1);
    if (a.len && a.data) memcpy(d, a.data, a.len);
    d[a.len] = 0;
    MakoString out = {d, a.len};
    return out;
}

/* ---- Zero-copy views (borrowed; do not free .data; keep backing alive) ----
 * Convention: cap == 0 means borrowed view (not heap-owned). */
static inline MakoByteArray mako_bytes_view(const void *p, size_t n) {
    MakoByteArray a;
    a.data = (uint8_t *)(uintptr_t)(p ? p : "");
    a.len = n;
    a.cap = 0; /* borrowed */
    return a;
}

/* View string bytes without copy (alias of underlying buffer). */
static inline MakoByteArray mako_as_bytes(MakoString s) {
    return mako_bytes_view(s.data ? s.data : "", s.len);
}

/* View []byte as string without copy (not necessarily NUL-terminated beyond len). */
static inline MakoString mako_bytes_as_str(MakoByteArray a) {
    return mako_str_view((const char *)(a.data ? a.data : (uint8_t *)""), a.len);
}

/* True if this []byte is a borrowed view (cap==0). */
static inline int64_t mako_bytes_is_view(MakoByteArray a) {
    return a.cap == 0 ? 1 : 0;
}

/* Tiny buffer pool for hot-path reuse (fixed slots; OOM → malloc fallback). */
#define MAKO_BUF_POOL_SLOTS 8
#define MAKO_BUF_POOL_SIZE 4096
typedef struct {
    uint8_t slots[MAKO_BUF_POOL_SLOTS][MAKO_BUF_POOL_SIZE];
    int used[MAKO_BUF_POOL_SLOTS];
} MakoBufPool;

static inline void mako_buf_pool_init(MakoBufPool *p) {
    if (!p) return;
    memset(p->used, 0, sizeof(p->used));
}

static inline MakoByteArray mako_buf_pool_get(MakoBufPool *p, int64_t need) {
    if (need < 0) need = 0;
    if (p && need <= MAKO_BUF_POOL_SIZE) {
        for (int i = 0; i < MAKO_BUF_POOL_SLOTS; i++) {
            if (!p->used[i]) {
                p->used[i] = 1;
                MakoByteArray a;
                a.data = p->slots[i];
                a.len = 0;
                a.cap = (size_t)MAKO_BUF_POOL_SIZE;
                return a;
            }
        }
    }
    /* fallback heap */
    return mako_byte_array_make(0, need > 0 ? need : 64);
}

static inline void mako_buf_pool_put(MakoBufPool *p, MakoByteArray a) {
    if (!p || !a.data) return;
    for (int i = 0; i < MAKO_BUF_POOL_SLOTS; i++) {
        if (a.data == p->slots[i]) {
            p->used[i] = 0;
            return;
        }
    }
    /* heap-owned (cap>0 and not a pool slot) */
    if (a.cap > 0) free(a.data);
}

static MakoBufPool mako_global_buf_pool;

static inline MakoByteArray mako_buf_get(int64_t need) {
    return mako_buf_pool_get(&mako_global_buf_pool, need);
}

static inline void mako_buf_put(MakoByteArray a) {
    mako_buf_pool_put(&mako_global_buf_pool, a);
}

/* SIMD-ish seed: XOR-fold bytes (autovectorizable loop; not ISA intrinsics). */
static inline int64_t mako_simd_xor_bytes(MakoByteArray a) {
    uint64_t acc = 0;
    size_t i = 0;
    const uint8_t *d = a.data ? a.data : (const uint8_t *)"";
    for (; i + 8 <= a.len; i += 8) {
        uint64_t w;
        memcpy(&w, d + i, 8);
        acc ^= w;
    }
    for (; i < a.len; i++) acc ^= d[i];
    return (int64_t)acc;
}


/* ---- Go-like []string ---- */
typedef struct {
    MakoString *data;
    size_t len;
    size_t cap;
} MakoStrArray;

static inline MakoStrArray mako_str_array_new(size_t n) {
    MakoStrArray a;
    a.data = (MakoString *)calloc(n ? n : 1, sizeof(MakoString));
    a.len = n;
    a.cap = n ? n : 1;
    return a;
}

static inline MakoStrArray mako_str_array_of(const MakoString *vals, size_t n) {
    MakoStrArray a = mako_str_array_new(n);
    for (size_t i = 0; i < n; i++) {
        a.data[i] = mako_str_clone(vals[i]);
    }
    return a;
}

static inline MakoStrArray mako_str_array_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoStrArray a;
    a.data = (MakoString *)calloc((size_t)(cap ? cap : 1), sizeof(MakoString));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}

static inline int64_t mako_str_array_len(MakoStrArray a) { return (int64_t)a.len; }
static inline int64_t mako_str_array_cap(MakoStrArray a) { return (int64_t)a.cap; }

static inline MakoString mako_str_array_get(MakoStrArray a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("string slice index out of bounds");
    return a.data[i];
}

static inline void mako_str_array_set(MakoStrArray a, int64_t i, MakoString v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("string slice index out of bounds");
    a.data[i] = mako_str_clone(v);
}

static inline MakoStrArray mako_str_array_append(MakoStrArray s, MakoString v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        /* Fresh backing on grow (never free the old) — see mako_slice_append. */
        MakoString *nd = (MakoString *)malloc(ncap * sizeof(MakoString));
        if (!nd) mako_abort("append: out of memory");
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoString));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = mako_str_clone(v);
    return s;
}

static inline MakoStrArray mako_str_array_slice_expr(
    MakoStrArray s, int64_t low, int64_t high, int64_t max, int has_max
) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoStrArray out;
    out.data = s.data + (size_t)low;
    out.len = (size_t)(high - low);
    if (has_max) {
        if (max < high) max = high;
        if (max > cap) max = cap;
        if (max < low) max = low;
        out.cap = (size_t)(max - low);
    } else {
        out.cap = (size_t)(cap - low);
    }
    return out;
}

static inline int64_t mako_str_array_copy(MakoStrArray dst, MakoStrArray src) {
    size_t n = dst.len < src.len ? dst.len : src.len;
    for (size_t i = 0; i < n; i++) {
        dst.data[i] = mako_str_clone(src.data[i]);
    }
    return (int64_t)n;
}

/* ---- Go-like []float / []float64 ---- */
typedef struct {
    double *data;
    size_t len;
    size_t cap;
} MakoFloatArray;

static inline MakoFloatArray mako_float_array_new(size_t n) {
    MakoFloatArray a;
    a.data = (double *)calloc(n ? n : 1, sizeof(double));
    a.len = n;
    a.cap = n ? n : 1;
    return a;
}

static inline MakoFloatArray mako_float_array_of(const double *vals, size_t n) {
    MakoFloatArray a = mako_float_array_new(n);
    if (n) memcpy(a.data, vals, n * sizeof(double));
    return a;
}

static inline MakoFloatArray mako_float_array_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoFloatArray a;
    a.data = (double *)calloc((size_t)(cap ? cap : 1), sizeof(double));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}

static inline int64_t mako_float_array_len(MakoFloatArray a) { return (int64_t)a.len; }
static inline int64_t mako_float_array_cap(MakoFloatArray a) { return (int64_t)a.cap; }

static inline double mako_float_array_get(MakoFloatArray a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("float slice index out of bounds");
    return a.data[i];
}

static inline void mako_float_array_set(MakoFloatArray a, int64_t i, double v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("float slice index out of bounds");
    a.data[i] = v;
}

static inline MakoFloatArray mako_float_array_append(MakoFloatArray s, double v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        /* Fresh backing on grow (never free the old) — see mako_slice_append. */
        double *nd = (double *)malloc(ncap * sizeof(double));
        if (!nd) mako_abort("append: out of memory");
        if (s.len) memcpy(nd, s.data, s.len * sizeof(double));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}

static inline MakoFloatArray mako_float_array_slice_expr(
    MakoFloatArray s, int64_t low, int64_t high, int64_t max, int has_max
) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoFloatArray out;
    out.data = s.data + (size_t)low;
    out.len = (size_t)(high - low);
    if (has_max) {
        if (max < high) max = high;
        if (max > cap) max = cap;
        if (max < low) max = low;
        out.cap = (size_t)(max - low);
    } else {
        out.cap = (size_t)(cap - low);
    }
    return out;
}

static inline int64_t mako_float_array_copy(MakoFloatArray dst, MakoFloatArray src) {
    size_t n = dst.len < src.len ? dst.len : src.len;
    if (n) memmove(dst.data, src.data, n * sizeof(double));
    return (int64_t)n;
}

static inline int64_t mako_to_int8(int64_t v) {
    if (v < -128 || v > 127) mako_abort("int8 conversion out of range -128..127");
    return (int64_t)(int8_t)v;
}

static inline int64_t mako_to_uint64_from_signed(int64_t v) {
    /* bit-preserving reinterpret for non-negative; reject negative for honesty */
    if (v < 0) mako_abort("uint64 conversion from negative signed value");
    return v;
}

/* ---- Result[T, E] (common backend shape) ----
 * Ok: int in `value`; string in `ok_s`; float in `ok_f` (others unused).
 * E may be string (err_kind=0) or a user enum (err_kind=1):
 *   err_kind 0: err string is the error payload
 *   err_kind 1: err_tag / err_i0..err_i2 / err_s0..err_s1 reconstruct the enum
 */
typedef struct {
    bool ok;
    int64_t value;
    MakoString ok_s;  /* Ok string payload for Result[string, E] */
    double ok_f;      /* Ok float payload for Result[float, E] */
    MakoString err;   /* string Err payload (err_kind==0) */
    int err_kind;     /* 0=string, 1=enum */
    int err_tag;      /* enum .tag when err_kind==1 */
    int64_t err_i0;
    int64_t err_i1;
    int64_t err_i2;
    MakoString err_s0;
    MakoString err_s1;
} MakoResultInt;

static inline MakoResultInt mako_ok_int(int64_t v) {
    MakoResultInt r;
    memset(&r, 0, sizeof(r));
    r.ok = true;
    r.value = v;
    return r;
}

static inline MakoResultInt mako_ok_str(MakoString s) {
    MakoResultInt r;
    memset(&r, 0, sizeof(r));
    r.ok = true;
    r.ok_s = s;
    return r;
}

static inline MakoResultInt mako_ok_float_res(double v) {
    MakoResultInt r;
    memset(&r, 0, sizeof(r));
    r.ok = true;
    r.ok_f = v;
    return r;
}

/* Ok payload for Result[Struct, E]: heap-owned box; match Ok unboxes and frees. */
static inline MakoResultInt mako_ok_ptr(void *p) {
    MakoResultInt r;
    memset(&r, 0, sizeof(r));
    r.ok = true;
    r.value = (int64_t)(intptr_t)p; /* pointer bits in value slot */
    return r;
}

static inline void *mako_result_ok_ptr(MakoResultInt r) {
    return r.ok ? (void *)(intptr_t)r.value : NULL;
}

static inline MakoResultInt mako_err_int(MakoString e) {
    MakoResultInt r;
    memset(&r, 0, sizeof(r));
    r.ok = false;
    r.value = 0;
    r.err = e;
    r.err_kind = 0;
    return r;
}

/* Err with enum payload: tag + optional int/string fields (unit enums use tag only). */
static inline MakoResultInt mako_err_enum(int tag, int64_t i0, int64_t i1, MakoString s0) {
    MakoResultInt r;
    memset(&r, 0, sizeof(r));
    r.ok = false;
    r.value = 0;
    r.err_kind = 1;
    r.err_tag = tag;
    r.err_i0 = i0;
    r.err_i1 = i1;
    r.err_s0 = s0;
    return r;
}

/* Extended enum Err packing (3 ints + 2 strings). */
static inline MakoResultInt mako_err_enum_ex(
    int tag, int64_t i0, int64_t i1, int64_t i2, MakoString s0, MakoString s1
) {
    MakoResultInt r;
    memset(&r, 0, sizeof(r));
    r.ok = false;
    r.err_kind = 1;
    r.err_tag = tag;
    r.err_i0 = i0;
    r.err_i1 = i1;
    r.err_i2 = i2;
    r.err_s0 = s0;
    r.err_s1 = s1;
    return r;
}

static inline int64_t mako_result_err_tag(MakoResultInt r) {
    return r.ok ? -1 : (r.err_kind == 1 ? (int64_t)r.err_tag : -1);
}

typedef struct {
    bool ok;
    double value;
    MakoString err;
} MakoResultFloat;

static inline MakoResultFloat mako_ok_float(double v) {
    MakoResultFloat r;
    r.ok = true;
    r.value = v;
    r.err = (MakoString){NULL, 0};
    return r;
}

static inline MakoResultFloat mako_err_float(MakoString e) {
    MakoResultFloat r;
    r.ok = false;
    r.value = 0;
    r.err = e;
    return r;
}

/* Prefix an Err message: wrap_err(r, "context") → Err("context: …") if Err, else r.
 * Go-like: fmt.Errorf("context: %w", err) — message chain for humans + error_is. */
static inline MakoResultInt mako_wrap_err(MakoResultInt r, MakoString prefix) {
    if (r.ok) return r;
    MakoString sep = mako_str_from_cstr(": ");
    MakoString mid = mako_str_concat(prefix, sep);
    MakoString out = mako_str_concat(mid, r.err);
    return mako_err_int(out);
}

/* Join two Results: prefer first Err; if both Err, combine messages; if both Ok, keep a. */
static inline MakoResultInt mako_error_join(MakoResultInt a, MakoResultInt b) {
    if (a.ok && b.ok) return a;
    if (!a.ok && b.ok) return a;
    if (a.ok && !b.ok) return b;
    MakoString sep = mako_str_from_cstr("; ");
    MakoString mid = mako_str_concat(a.err, sep);
    return mako_err_int(mako_str_concat(mid, b.err));
}

/* Enum-like tagged errors: error_tag("NotFound", "user 7") → Err("NotFound: user 7"). */
static inline MakoResultInt mako_error_tag(MakoString tag, MakoString msg) {
    MakoString sep = mako_str_from_cstr(": ");
    MakoString mid = mako_str_concat(tag, sep);
    return mako_err_int(mako_str_concat(mid, msg));
}

/* errorf("open %s", path) — format a string error (printf-style, one %s or plain). */
static inline MakoResultInt mako_errorf(MakoString fmt, MakoString arg) {
    /* Simple: if fmt contains "%s", substitute arg; else concat fmt + ": " + arg. */
    int has_pct = 0;
    for (size_t i = 0; i + 1 < fmt.len; i++) {
        if (fmt.data[i] == '%' && fmt.data[i + 1] == 's') {
            has_pct = 1;
            break;
        }
    }
    if (!has_pct) {
        MakoString sep = mako_str_from_cstr(": ");
        MakoString mid = mako_str_concat(fmt, sep);
        return mako_err_int(mako_str_concat(mid, arg));
    }
    /* Split on first "%s" */
    size_t pos = 0;
    for (; pos + 1 < fmt.len; pos++) {
        if (fmt.data[pos] == '%' && fmt.data[pos + 1] == 's') break;
    }
    MakoString left = {fmt.data, pos};
    MakoString right = {fmt.data + pos + 2, fmt.len - pos - 2};
    MakoString mid = mako_str_concat(left, arg);
    return mako_err_int(mako_str_concat(mid, right));
}

/* Extract Err message; empty string if Ok. */
static inline MakoString mako_error_string(MakoResultInt r) {
    if (r.ok) return mako_str_from_cstr("");
    return r.err;
}

/* ---- Option[int] ---- */
typedef struct {
    bool some;
    int64_t value;
} MakoOptionInt;

static inline MakoOptionInt mako_some_int(int64_t v) {
    MakoOptionInt o;
    o.some = true;
    o.value = v;
    return o;
}

static inline MakoOptionInt mako_none_int(void) {
    MakoOptionInt o;
    o.some = false;
    o.value = 0;
    return o;
}

/* ---- Small stdlib helpers ---- */
static inline int64_t mako_str_len(MakoString s) {
    return (int64_t)s.len;
}

/* Go-like: s[i] is the i-th byte (0..255), not a rune */
static inline int64_t mako_str_get(MakoString s, int64_t i) {
    if (i < 0 || (size_t)i >= s.len) {
        mako_abort("string index out of bounds");
    }
    return (int64_t)(uint8_t)s.data[i];
}

/* Go-like substring by byte offsets [low:high); copies */
static inline MakoString mako_str_slice(MakoString s, int64_t low, int64_t high) {
    if (low < 0) low = 0;
    if (high < low) high = low;
    if ((size_t)high > s.len) high = (int64_t)s.len;
    if ((size_t)low > s.len) low = (int64_t)s.len;
    size_t n = (size_t)(high - low);
    char *d = (char *)malloc(n + 1);
    if (n) memcpy(d, s.data + (size_t)low, n);
    d[n] = 0;
    MakoString out = {d, n};
    return out;
}

/* Decode one UTF-8 rune at byte offset `off`. Writes code point to *out.
   Returns byte width (1..4). Invalid sequences → U+FFFD and width 1. */
static inline size_t mako_utf8_decode(const char *data, size_t len, size_t off, int64_t *out) {
    if (off >= len) {
        *out = 0xFFFD;
        return 1;
    }
    const unsigned char *p = (const unsigned char *)data + off;
    size_t rem = len - off;
    unsigned char b0 = p[0];
    if (b0 < 0x80) {
        *out = (int64_t)b0;
        return 1;
    }
    if ((b0 & 0xE0) == 0xC0 && rem >= 2 && (p[1] & 0xC0) == 0x80) {
        int64_t cp = ((int64_t)(b0 & 0x1F) << 6) | (int64_t)(p[1] & 0x3F);
        if (cp < 0x80) {
            *out = 0xFFFD;
            return 1;
        }
        *out = cp;
        return 2;
    }
    if ((b0 & 0xF0) == 0xE0 && rem >= 3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        int64_t cp = ((int64_t)(b0 & 0x0F) << 12) | ((int64_t)(p[1] & 0x3F) << 6) | (int64_t)(p[2] & 0x3F);
        if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) {
            *out = 0xFFFD;
            return 1;
        }
        *out = cp;
        return 3;
    }
    if ((b0 & 0xF8) == 0xF0 && rem >= 4 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80
        && (p[3] & 0xC0) == 0x80) {
        int64_t cp = ((int64_t)(b0 & 0x07) << 18) | ((int64_t)(p[1] & 0x3F) << 12)
            | ((int64_t)(p[2] & 0x3F) << 6) | (int64_t)(p[3] & 0x3F);
        if (cp < 0x10000 || cp > 0x10FFFF) {
            *out = 0xFFFD;
            return 1;
        }
        *out = cp;
        return 4;
    }
    *out = 0xFFFD;
    return 1;
}

static inline int64_t mako_rune_count(MakoString s) {
    int64_t n = 0;
    size_t i = 0;
    int64_t r = 0;
    while (i < s.len) {
        i += mako_utf8_decode(s.data, s.len, i, &r);
        n++;
    }
    return n;
}

static inline bool mako_str_eq(MakoString a, MakoString b) {
    return a.len == b.len && memcmp(a.data, b.data, a.len) == 0;
}

static inline bool mako_str_contains(MakoString hay, MakoString needle) {
    if (needle.len == 0) return true;
    if (needle.len > hay.len) return false;
    for (size_t i = 0; i + needle.len <= hay.len; i++) {
        if (memcmp(hay.data + i, needle.data, needle.len) == 0) return true;
    }
    return false;
}

/* True if Err message contains needle (Go errors.Is-style substring match on wrap chain). */
static inline int64_t mako_error_is(MakoResultInt r, MakoString needle) {
    if (r.ok) return 0;
    return mako_str_contains(r.err, needle) ? 1 : 0;
}

static inline MakoString mako_int_to_string(int64_t n) {
    char buf[32];
    int written = snprintf(buf, sizeof(buf), "%lld", (long long)n);
    if (written < 0) written = 0;
    return mako_str_from_cstr(buf);
}

/* ---- String builder (growable buffer → string) ---- */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} MakoStrBuilder;

static inline MakoStrBuilder *mako_str_builder_new(void) {
    MakoStrBuilder *b = (MakoStrBuilder *)malloc(sizeof(MakoStrBuilder));
    b->data = (char *)malloc(16);
    b->data[0] = 0;
    b->len = 0;
    b->cap = 16;
    return b;
}

static inline void mako_str_builder_grow(MakoStrBuilder *b, size_t need) {
    if (need <= b->cap) return;
    size_t ncap = b->cap ? b->cap * 2 : 16;
    while (ncap < need) ncap *= 2;
    char *nd = (char *)realloc(b->data, ncap);
    if (!nd) mako_abort("str_builder: out of memory");
    b->data = nd;
    b->cap = ncap;
}

static inline void mako_str_builder_write(MakoStrBuilder *b, MakoString s) {
    mako_str_builder_grow(b, b->len + s.len + 1);
    if (s.len) memcpy(b->data + b->len, s.data, s.len);
    b->len += s.len;
    b->data[b->len] = 0;
}

static inline void mako_str_builder_write_byte(MakoStrBuilder *b, int64_t v) {
    if (v < 0 || v > 255) mako_abort("str_builder write_byte: out of range 0..255");
    mako_str_builder_grow(b, b->len + 2);
    b->data[b->len++] = (char)(uint8_t)v;
    b->data[b->len] = 0;
}

static inline MakoString mako_str_builder_string(MakoStrBuilder *b) {
    char *d = (char *)malloc(b->len + 1);
    if (b->len) memcpy(d, b->data, b->len);
    d[b->len] = 0;
    MakoString out = {d, b->len};
    return out;
}

static inline int64_t mako_str_builder_len(MakoStrBuilder *b) {
    return (int64_t)b->len;
}

/* ---- Maps: open-addressing hash tables (map[string]int / map[int]int) ---- */
enum { MAKO_MAP_EMPTY = 0, MAKO_MAP_FULL = 1, MAKO_MAP_TOMB = 2 };

typedef struct {
    uint8_t *state;
    MakoString *keys;
    int64_t *vals;
    size_t cap;
    size_t len;
} MakoMapSI;

typedef struct {
    uint8_t *state;
    int64_t *keys;
    int64_t *vals;
    size_t cap;
    size_t len;
} MakoMapII;

static inline uint64_t mako_hash_bytes(const char *p, size_t n) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= (unsigned char)p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static inline uint64_t mako_hash_i64(int64_t k) {
    uint64_t x = (uint64_t)k;
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static inline MakoString mako_str_clone(MakoString s) {
    char *d = (char *)malloc(s.len + 1);
    if (s.len) memcpy(d, s.data, s.len);
    d[s.len] = 0;
    MakoString out = {d, s.len};
    return out;
}

static inline MakoMapSI mako_map_si_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 10 / 7 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapSI m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (MakoString *)calloc(cap, sizeof(MakoString));
    m.vals = (int64_t *)malloc(cap * sizeof(int64_t));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_si_new\n");
        abort();
    }
    m.cap = cap;
    m.len = 0;
    return m;
}

static inline void mako_map_si_rehash(MakoMapSI *m, size_t ncap);

static inline void mako_map_si_set(MakoMapSI *m, MakoString key, int64_t val) {
    if ((m->len + 1) * 10 >= m->cap * 7) {
        mako_map_si_rehash(m, m->cap * 2);
    }
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    size_t first_tomb = (size_t)-1;
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = mako_str_clone(key);
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (m->state[i] == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (m->keys[i].len == key.len
                   && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_si_rehash(MakoMapSI *m, size_t ncap) {
    /* Move owned keys into the new table — no clone/free churn (vs Go map grow). */
    uint8_t *ostate = m->state;
    MakoString *okeys = m->keys;
    int64_t *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapSI n = mako_map_si_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        MakoString key = okeys[i];
        int64_t val = ovals[i];
        uint64_t h = mako_hash_bytes(key.data, key.len);
        size_t j = (size_t)(h & (n.cap - 1));
        while (n.state[j] == MAKO_MAP_FULL) {
            j = (j + 1) & (n.cap - 1);
        }
        n.state[j] = MAKO_MAP_FULL;
        n.keys[j] = key;
        n.vals[j] = val;
        n.len++;
    }
    free(ostate);
    free(okeys);
    free(ovals);
    *m = n;
}

static inline int64_t mako_map_si_get(MakoMapSI *m, MakoString key) {
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return 0;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i].len == key.len
            && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            return m->vals[i];
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline bool mako_map_si_has(MakoMapSI *m, MakoString key) {
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i].len == key.len
            && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            return true;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_si_delete(MakoMapSI *m, MakoString key) {
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i].len == key.len
            && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            free(m->keys[i].data);
            m->keys[i].data = NULL;
            m->keys[i].len = 0;
            m->state[i] = MAKO_MAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline int64_t mako_map_si_len(MakoMapSI *m) { return (int64_t)m->len; }

static inline MakoMapSI *mako_map_si_make(int64_t hint) {
    MakoMapSI *m = (MakoMapSI *)malloc(sizeof(MakoMapSI));
    *m = mako_map_si_new(hint > 0 ? (size_t)hint : 0);
    return m;
}

static inline MakoMapII mako_map_ii_new(size_t hint) {
    size_t cap = 8;
    /* Pre-size so hint entries fit under 70% load without immediate rehash. */
    size_t need = hint ? (hint * 10 / 7 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapII m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (int64_t *)malloc(cap * sizeof(int64_t));
    m.vals = (int64_t *)malloc(cap * sizeof(int64_t));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_ii_new\n");
        abort();
    }
    m.cap = cap;
    m.len = 0;
    return m;
}

static inline void mako_map_ii_rehash(MakoMapII *m, size_t ncap);

static inline void mako_map_ii_set(MakoMapII *m, int64_t key, int64_t val) {
    if ((m->len + 1) * 10 >= m->cap * 7) {
        mako_map_ii_rehash(m, m->cap * 2);
    }
    uint64_t h = mako_hash_i64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    size_t first_tomb = (size_t)-1;
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (m->state[i] == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (m->keys[i] == key) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_ii_rehash(MakoMapII *m, size_t ncap) {
    /* Move keys (no string clone — int keys). Avoids double-hash via set(). */
    uint8_t *ostate = m->state;
    int64_t *okeys = m->keys;
    int64_t *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapII n = mako_map_ii_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        int64_t key = okeys[i];
        int64_t val = ovals[i];
        uint64_t h = mako_hash_i64(key);
        size_t j = (size_t)(h & (n.cap - 1));
        while (n.state[j] == MAKO_MAP_FULL) {
            j = (j + 1) & (n.cap - 1);
        }
        n.state[j] = MAKO_MAP_FULL;
        n.keys[j] = key;
        n.vals[j] = val;
        n.len++;
    }
    free(ostate);
    free(okeys);
    free(ovals);
    *m = n;
}

static inline int64_t mako_map_ii_get(MakoMapII *m, int64_t key) {
    uint64_t h = mako_hash_i64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return 0;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_ii_delete(MakoMapII *m, int64_t key) {
    uint64_t h = mako_hash_i64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) {
            m->state[i] = MAKO_MAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline int64_t mako_map_ii_len(MakoMapII *m) { return (int64_t)m->len; }

static inline bool mako_map_ii_has(MakoMapII *m, int64_t key) {
    uint64_t h = mako_hash_i64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return true;
        i = (i + 1) & (m->cap - 1);
    }
}

static inline MakoMapII *mako_map_ii_make(int64_t hint) {
    MakoMapII *m = (MakoMapII *)malloc(sizeof(MakoMapII));
    *m = mako_map_ii_new(hint > 0 ? (size_t)hint : 0);
    return m;
}

/* map[string]string */
typedef struct {
    uint8_t *state;
    MakoString *keys;
    MakoString *vals;
    size_t cap;
    size_t len;
} MakoMapSS;

static inline MakoMapSS mako_map_ss_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 10 / 7 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapSS m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (MakoString *)calloc(cap, sizeof(MakoString));
    m.vals = (MakoString *)calloc(cap, sizeof(MakoString));
    m.cap = cap;
    m.len = 0;
    return m;
}

static inline void mako_map_ss_rehash(MakoMapSS *m, size_t ncap);

static inline void mako_map_ss_set(MakoMapSS *m, MakoString key, MakoString val) {
    if ((m->len + 1) * 10 >= m->cap * 7) {
        mako_map_ss_rehash(m, m->cap * 2);
    }
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    size_t first_tomb = (size_t)-1;
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = mako_str_clone(key);
            m->vals[slot] = mako_str_clone(val);
            m->len++;
            return;
        }
        if (m->state[i] == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (m->keys[i].len == key.len
                   && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            free(m->vals[i].data);
            m->vals[i] = mako_str_clone(val);
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_ss_rehash(MakoMapSS *m, size_t ncap) {
    MakoMapSS n = mako_map_ss_new(ncap / 2);
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) {
            mako_map_ss_set(&n, m->keys[i], m->vals[i]);
            free(m->keys[i].data);
            free(m->vals[i].data);
        }
    }
    free(m->state);
    free(m->keys);
    free(m->vals);
    *m = n;
}

static inline MakoString mako_map_ss_get(MakoMapSS *m, MakoString key) {
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) {
            return mako_str_from_cstr("");
        }
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i].len == key.len
            && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            return mako_str_clone(m->vals[i]);
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline bool mako_map_ss_has(MakoMapSS *m, MakoString key) {
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i].len == key.len
            && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            return true;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_ss_delete(MakoMapSS *m, MakoString key) {
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i].len == key.len
            && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            free(m->keys[i].data);
            free(m->vals[i].data);
            m->keys[i].data = NULL;
            m->keys[i].len = 0;
            m->vals[i].data = NULL;
            m->vals[i].len = 0;
            m->state[i] = MAKO_MAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline int64_t mako_map_ss_len(MakoMapSS *m) { return (int64_t)m->len; }

static inline MakoMapSS *mako_map_ss_make(int64_t hint) {
    MakoMapSS *m = (MakoMapSS *)malloc(sizeof(MakoMapSS));
    *m = mako_map_ss_new(hint > 0 ? (size_t)hint : 0);
    return m;
}

/* ---- Debug / abort (early — used by slice/array helpers) ---- */
static inline void mako_abort(const char *msg) {
    fprintf(stderr, "error: %s\n", msg ? msg : "runtime abort");
    fprintf(stderr, "  help: lldb/gdb the binary (debug builds use clang -g); see docs/DEBUG.md\n");
    abort();
}

/* ---- Bounds checks policy ----
 * Debug builds always check. Release (`-DNDEBUG`) strips checks unless
 * MAKO_BOUNDS_ALWAYS is set (codegen `--bounds always` / `mako build --release`).
 */
#if defined(MAKO_BOUNDS_ALWAYS)
#define MAKO_BOUNDS_CHECK(cond, msg) \
    do { if (cond) mako_abort(msg); } while (0)
#elif defined(NDEBUG)
#define MAKO_BOUNDS_CHECK(cond, msg) ((void)0)
#else
#define MAKO_BOUNDS_CHECK(cond, msg) \
    do { if (cond) mako_abort(msg); } while (0)
#endif

/* Abort with file:line (prefer this from generated code). */
static inline void mako_abort_at(const char *file, int line, const char *msg) {
    fprintf(stderr, "error: %s\n", msg ? msg : "runtime abort");
    if (file && line > 0) {
        fprintf(stderr, "  --> %s:%d\n", file, line);
    }
    fprintf(stderr, "  help: lldb ./binary  or  gdb ./binary — see docs/DEBUG.md\n");
    abort();
}

/* dbg!(x) — print file:line + int value to stderr (debug builds). */
static inline int64_t mako_dbg_int(const char *file, int line, const char *expr, int64_t v) {
    fprintf(stderr, "[dbg] %s:%d: %s = %lld\n",
            file ? file : "?", line, expr ? expr : "?", (long long)v);
    fflush(stderr);
    return v;
}

static inline MakoString mako_dbg_str(const char *file, int line, const char *expr, MakoString s) {
    fprintf(stderr, "[dbg] %s:%d: %s = \"%.*s\"\n",
            file ? file : "?", line, expr ? expr : "?",
            (int)s.len, s.data ? s.data : "");
    fflush(stderr);
    return s;
}

static inline int64_t mako_array_len(MakoIntArray a) {
    return (int64_t)a.len;
}

static inline int64_t mako_array_cap(MakoIntArray a) {
    return (int64_t)a.cap;
}

/* Go-like append: may reallocate; returns new header (caller must assign).
 * Growing allocates a *fresh* backing array and copies — it never frees the old
 * one. This matches Go: after a growing append the source slice still points at
 * a valid (older) backing array, so a struct passed by value (which shallow-
 * copies the slice header, sharing the backing store) can't be left holding a
 * freed pointer. Freeing here would double-free / use-after-free such aliases. */
static inline MakoIntArray mako_slice_append(MakoIntArray s, int64_t v) {
    if (s.len < s.cap) {
        s.data[s.len++] = v;
        return s;
    }
    size_t ncap = s.cap ? s.cap * 2 : 1;
    if (ncap < s.len + 1) ncap = s.len + 1;
    int64_t *nd = (int64_t *)malloc(ncap * sizeof(int64_t));
    if (!nd) mako_abort("append: out of memory");
    if (s.len) memcpy(nd, s.data, s.len * sizeof(int64_t));
    s.data = nd;
    s.cap = ncap;
    s.data[s.len++] = v;
    return s;
}

/* Go-like s[low:high] / s[low:high:max] — shares backing store. */
static inline MakoIntArray mako_slice_expr(
    MakoIntArray s,
    int64_t low,
    int64_t high,
    int64_t max,
    int has_max
) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoIntArray out;
    out.data = s.data + (size_t)low;
    out.len = (size_t)(high - low);
    if (has_max) {
        if (max < high) max = high;
        if (max > cap) max = cap;
        if (max < low) max = low;
        out.cap = (size_t)(max - low);
    } else {
        out.cap = (size_t)(cap - low);
    }
    return out;
}

/* Go-like copy(dst, src): copies min(len(dst), len(src)) elements; returns count. */
static inline int64_t mako_slice_copy(MakoIntArray dst, MakoIntArray src) {
    size_t n = dst.len < src.len ? dst.len : src.len;
    if (n == 0) return 0;
    /* memmove: overlapping slices (Go copy semantics) */
    memmove(dst.data, src.data, n * sizeof(int64_t));
    return (int64_t)n;
}

/* ---- Int channels (pthread-safe ring buffer) ----
 * Bounded MPMC channel for int64 values. Thread-safe via pthread mutex + condvars.
 * send() blocks when full; recv() blocks when empty. close() wakes all waiters.
 * After close, recv() returns 0 once drained. send() after close is a no-op (-1).
 * select() polls multiple channels with optional timeout.
 */
typedef struct {
    int64_t *buf;
    size_t cap;
    size_t head;
    size_t tail;
    size_t count;
    bool closed;
    pthread_mutex_t mu;
    pthread_cond_t can_send;
    pthread_cond_t can_recv;
} MakoChan;

static inline MakoChan *mako_chan_new(int64_t capacity) {
    size_t cap = capacity < 1 ? 1 : (size_t)capacity;
    MakoChan *c = (MakoChan *)calloc(1, sizeof(MakoChan));
    c->buf = (int64_t *)calloc(cap, sizeof(int64_t));
    c->cap = cap;
    pthread_mutex_init(&c->mu, NULL);
    pthread_cond_init(&c->can_send, NULL);
    pthread_cond_init(&c->can_recv, NULL);
    mako_rt_counter_inc(&mako_rt_channels_created);
    return c;
}

static inline int64_t mako_chan_send(MakoChan *c, int64_t v) {
    pthread_mutex_lock(&c->mu);
    while (c->count == c->cap && !c->closed) {
        pthread_cond_wait(&c->can_send, &c->mu);
    }
    if (c->closed) {
        pthread_mutex_unlock(&c->mu);
        return 0; /* fail */
    }
    c->buf[c->tail] = v;
    c->tail = (c->tail + 1) % c->cap;
    c->count++;
    mako_rt_counter_inc(&mako_rt_channel_sends);
    mako_rt_observe_channel_depth(c->count);
    pthread_cond_signal(&c->can_recv);
    pthread_mutex_unlock(&c->mu);
    return 1;
}

static inline int64_t mako_chan_try_send(MakoChan *c, int64_t v) {
    pthread_mutex_lock(&c->mu);
    if (c->closed || c->count == c->cap) {
        mako_rt_counter_inc(&mako_rt_channel_try_send_drops);
        pthread_mutex_unlock(&c->mu);
        return 0;
    }
    c->buf[c->tail] = v;
    c->tail = (c->tail + 1) % c->cap;
    c->count++;
    mako_rt_counter_inc(&mako_rt_channel_sends);
    mako_rt_observe_channel_depth(c->count);
    pthread_cond_signal(&c->can_recv);
    pthread_mutex_unlock(&c->mu);
    return 1;
}

static inline int64_t mako_chan_len(MakoChan *c) {
    pthread_mutex_lock(&c->mu);
    int64_t n = (int64_t)c->count;
    pthread_mutex_unlock(&c->mu);
    return n;
}

static inline int64_t mako_chan_cap(MakoChan *c) {
    pthread_mutex_lock(&c->mu);
    int64_t n = (int64_t)c->cap;
    pthread_mutex_unlock(&c->mu);
    return n;
}

static inline int64_t mako_chan_recv(MakoChan *c) {
    pthread_mutex_lock(&c->mu);
    while (c->count == 0 && !c->closed) {
        pthread_cond_wait(&c->can_recv, &c->mu);
    }
    if (c->count == 0 && c->closed) {
        pthread_mutex_unlock(&c->mu);
        return 0; /* closed empty */
    }
    int64_t v = c->buf[c->head];
    c->head = (c->head + 1) % c->cap;
    c->count--;
    mako_rt_counter_inc(&mako_rt_channel_recvs);
    pthread_cond_signal(&c->can_send);
    pthread_mutex_unlock(&c->mu);
    return v;
}

/* Recv until close: returns 1 and writes *out, or 0 if channel closed and empty. */
static inline int64_t mako_chan_recv_ok(MakoChan *c, int64_t *out) {
    pthread_mutex_lock(&c->mu);
    while (c->count == 0 && !c->closed) {
        pthread_cond_wait(&c->can_recv, &c->mu);
    }
    if (c->count == 0 && c->closed) {
        pthread_mutex_unlock(&c->mu);
        return 0;
    }
    int64_t v = c->buf[c->head];
    c->head = (c->head + 1) % c->cap;
    c->count--;
    mako_rt_counter_inc(&mako_rt_channel_recvs);
    pthread_cond_signal(&c->can_send);
    pthread_mutex_unlock(&c->mu);
    if (out) *out = v;
    return 1;
}

static inline void mako_chan_close(MakoChan *c) {
    pthread_mutex_lock(&c->mu);
    c->closed = true;
    pthread_cond_broadcast(&c->can_send);
    pthread_cond_broadcast(&c->can_recv);
    pthread_mutex_unlock(&c->mu);
}

/* Non-blocking try-recv: 1 + value via out, or 0 if empty (not closed wait). */
static inline int64_t mako_chan_try_recv(MakoChan *c, int64_t *out) {
    pthread_mutex_lock(&c->mu);
    if (c->count == 0) {
        pthread_mutex_unlock(&c->mu);
        return 0;
    }
    int64_t v = c->buf[c->head];
    c->head = (c->head + 1) % c->cap;
    c->count--;
    mako_rt_counter_inc(&mako_rt_channel_recvs);
    pthread_cond_signal(&c->can_send);
    pthread_mutex_unlock(&c->mu);
    if (out) *out = v;
    return 1;
}

/* Select between two channels with timeout_ms.
 * Returns 0 if a ready, 1 if b ready, -1 on timeout.
 * Value available via mako_chan_select_value(). */
static int64_t mako_select_last_val = 0;

static inline int64_t mako_chan_select_value(void) {
    return mako_select_last_val;
}

/* Select among up to 16 channels. Returns arm index or -1 on timeout.
 * Round-robin fairness: after a hit at index i, next scan starts at i+1. */
#define MAKO_SELECT_MAX 16
static int64_t mako_select_rr = 0;

static inline int64_t mako_chan_selectn(
    MakoChan **chs,
    int64_t n,
    int64_t timeout_ms
) {
    if (n < 1 || n > MAKO_SELECT_MAX || chs == NULL) return -1;
    struct timeval start, now;
    mako_gettimeofday(&start, NULL);
    for (;;) {
        int64_t start_i = mako_select_rr % n;
        for (int64_t k = 0; k < n; k++) {
            int64_t i = (start_i + k) % n;
            if (chs[i] == NULL) continue;
            int64_t v = 0;
            if (mako_chan_try_recv(chs[i], &v)) {
                mako_select_last_val = v;
                mako_select_rr = (i + 1) % n;
                return i;
            }
        }
        if (timeout_ms >= 0) {
            mako_gettimeofday(&now, NULL);
            int64_t elapsed =
                (now.tv_sec - start.tv_sec) * 1000 +
                (now.tv_usec - start.tv_usec) / 1000;
            if (elapsed >= timeout_ms) {
                mako_rt_counter_inc(&mako_rt_channel_select_timeouts);
                return -1;
            }
        }
        struct timespec ts = {0, 2000000L};
        nanosleep(&ts, NULL);
    }
}

/* Convenience wrappers keep older call sites working. */
static inline int64_t mako_chan_select2(
    MakoChan *a,
    MakoChan *b,
    int64_t timeout_ms
) {
    MakoChan *chs[2] = {a, b};
    return mako_chan_selectn(chs, 2, timeout_ms);
}

/* Select among 3 channels. Returns 0/1/2 or -1 on timeout. */
static inline int64_t mako_chan_select3(
    MakoChan *a,
    MakoChan *b,
    MakoChan *c,
    int64_t timeout_ms
) {
    MakoChan *chs[3] = {a, b, c};
    return mako_chan_selectn(chs, 3, timeout_ms);
}

static inline int64_t mako_chan_select4(
    MakoChan *a,
    MakoChan *b,
    MakoChan *c,
    MakoChan *d,
    int64_t timeout_ms
) {
    MakoChan *chs[4] = {a, b, c, d};
    return mako_chan_selectn(chs, 4, timeout_ms);
}

/* ---- String channels (ownership transfer of MakoString payloads) ----
 * Same ring-buffer design as int channels; send copies string into the buffer.
 * recv returns an owned MakoString (caller owns the result).
 */
typedef struct {
    MakoString *buf;
    size_t cap;
    size_t head;
    size_t tail;
    size_t count;
    bool closed;
    pthread_mutex_t mu;
    pthread_cond_t can_send;
    pthread_cond_t can_recv;
} MakoChanStr;

static inline MakoChanStr *mako_chan_str_new(int64_t capacity) {
    size_t cap = capacity < 1 ? 1 : (size_t)capacity;
    MakoChanStr *c = (MakoChanStr *)calloc(1, sizeof(MakoChanStr));
    c->buf = (MakoString *)calloc(cap, sizeof(MakoString));
    c->cap = cap;
    pthread_mutex_init(&c->mu, NULL);
    pthread_cond_init(&c->can_send, NULL);
    pthread_cond_init(&c->can_recv, NULL);
    return c;
}

static inline int64_t mako_chan_str_send(MakoChanStr *c, MakoString v) {
    pthread_mutex_lock(&c->mu);
    while (c->count == c->cap && !c->closed) {
        pthread_cond_wait(&c->can_send, &c->mu);
    }
    if (c->closed) {
        pthread_mutex_unlock(&c->mu);
        return 0;
    }
    c->buf[c->tail] = mako_str_clone(v);
    c->tail = (c->tail + 1) % c->cap;
    c->count++;
    pthread_cond_signal(&c->can_recv);
    pthread_mutex_unlock(&c->mu);
    return 1;
}

static inline MakoString mako_chan_str_recv(MakoChanStr *c) {
    pthread_mutex_lock(&c->mu);
    while (c->count == 0 && !c->closed) {
        pthread_cond_wait(&c->can_recv, &c->mu);
    }
    if (c->count == 0 && c->closed) {
        pthread_mutex_unlock(&c->mu);
        return mako_str_from_cstr("");
    }
    MakoString v = c->buf[c->head];
    c->buf[c->head] = (MakoString){NULL, 0};
    c->head = (c->head + 1) % c->cap;
    c->count--;
    pthread_cond_signal(&c->can_send);
    pthread_mutex_unlock(&c->mu);
    return v;
}

static inline void mako_chan_str_close(MakoChanStr *c) {
    if (!c) return;
    pthread_mutex_lock(&c->mu);
    c->closed = true;
    pthread_cond_broadcast(&c->can_send);
    pthread_cond_broadcast(&c->can_recv);
    pthread_mutex_unlock(&c->mu);
}

/* Nonblocking try-recv for string channels. Returns 1 and owns *out on success. */
static inline int64_t mako_chan_str_try_recv(MakoChanStr *c, MakoString *out) {
    if (!c) return 0;
    pthread_mutex_lock(&c->mu);
    if (c->count == 0) {
        pthread_mutex_unlock(&c->mu);
        return 0;
    }
    MakoString v = c->buf[c->head];
    c->buf[c->head] = (MakoString){NULL, 0};
    c->head = (c->head + 1) % c->cap;
    c->count--;
    pthread_cond_signal(&c->can_send);
    pthread_mutex_unlock(&c->mu);
    if (out) *out = v;
    else free(v.data);
    return 1;
}

/* Select among string channels. Returns arm index or -1; value via mako_chan_select_value_str. */
static MakoString mako_select_last_str = {NULL, 0};
static int64_t mako_select_rr_str = 0;

static inline MakoString mako_chan_select_value_str(void) {
    return mako_str_clone(mako_select_last_str);
}

static inline int64_t mako_chan_str_selectn(
    MakoChanStr **chs, int64_t n, int64_t timeout_ms
) {
    if (n < 1 || n > MAKO_SELECT_MAX || chs == NULL) return -1;
    struct timeval start, now;
    mako_gettimeofday(&start, NULL);
    for (;;) {
        int64_t start_i = mako_select_rr_str % n;
        for (int64_t k = 0; k < n; k++) {
            int64_t i = (start_i + k) % n;
            if (chs[i] == NULL) continue;
            MakoString v = {NULL, 0};
            if (mako_chan_str_try_recv(chs[i], &v)) {
                free(mako_select_last_str.data);
                mako_select_last_str = v;
                mako_select_rr_str = (i + 1) % n;
                return i;
            }
        }
        if (timeout_ms >= 0) {
            mako_gettimeofday(&now, NULL);
            int64_t elapsed =
                (now.tv_sec - start.tv_sec) * 1000 +
                (now.tv_usec - start.tv_usec) / 1000;
            if (elapsed >= timeout_ms) {
                mako_rt_counter_inc(&mako_rt_channel_select_timeouts);
                return -1;
            }
        }
        struct timespec ts = {0, 2000000L};
        nanosleep(&ts, NULL);
    }
}

static inline int64_t mako_chan_str_select2(
    MakoChanStr *a, MakoChanStr *b, int64_t timeout_ms
) {
    MakoChanStr *chs[2] = {a, b};
    return mako_chan_str_selectn(chs, 2, timeout_ms);
}

/* ---- Pointer channels (opaque / heap-boxed struct handles) ----
 * Send/recv transfer ownership of a void* (caller allocates; receiver frees).
 * Used for chan[Struct] seed: send boxes a copy, recv unboxes.
 */
typedef struct {
    void **buf;
    size_t cap;
    size_t head;
    size_t tail;
    size_t count;
    bool closed;
    pthread_mutex_t mu;
    pthread_cond_t can_send;
    pthread_cond_t can_recv;
} MakoChanPtr;

static inline MakoChanPtr *mako_chan_ptr_new(int64_t capacity) {
    size_t cap = capacity < 1 ? 1 : (size_t)capacity;
    MakoChanPtr *c = (MakoChanPtr *)calloc(1, sizeof(MakoChanPtr));
    c->buf = (void **)calloc(cap, sizeof(void *));
    c->cap = cap;
    pthread_mutex_init(&c->mu, NULL);
    pthread_cond_init(&c->can_send, NULL);
    pthread_cond_init(&c->can_recv, NULL);
    return c;
}

static inline int64_t mako_chan_ptr_send(MakoChanPtr *c, void *v) {
    pthread_mutex_lock(&c->mu);
    while (c->count == c->cap && !c->closed) {
        pthread_cond_wait(&c->can_send, &c->mu);
    }
    if (c->closed) {
        pthread_mutex_unlock(&c->mu);
        return 0;
    }
    c->buf[c->tail] = v;
    c->tail = (c->tail + 1) % c->cap;
    c->count++;
    pthread_cond_signal(&c->can_recv);
    pthread_mutex_unlock(&c->mu);
    return 1;
}

static inline void *mako_chan_ptr_recv(MakoChanPtr *c) {
    pthread_mutex_lock(&c->mu);
    while (c->count == 0 && !c->closed) {
        pthread_cond_wait(&c->can_recv, &c->mu);
    }
    if (c->count == 0 && c->closed) {
        pthread_mutex_unlock(&c->mu);
        return NULL;
    }
    void *v = c->buf[c->head];
    c->buf[c->head] = NULL;
    c->head = (c->head + 1) % c->cap;
    c->count--;
    pthread_cond_signal(&c->can_send);
    pthread_mutex_unlock(&c->mu);
    return v;
}

static inline void mako_chan_ptr_close(MakoChanPtr *c) {
    if (!c) return;
    pthread_mutex_lock(&c->mu);
    c->closed = true;
    pthread_cond_broadcast(&c->can_send);
    pthread_cond_broadcast(&c->can_recv);
    pthread_mutex_unlock(&c->mu);
}

/* Nonblocking try-recv for pointer channels. */
static inline int64_t mako_chan_ptr_try_recv(MakoChanPtr *c, void **out) {
    if (!c) return 0;
    pthread_mutex_lock(&c->mu);
    if (c->count == 0) {
        pthread_mutex_unlock(&c->mu);
        return 0;
    }
    void *v = c->buf[c->head];
    c->buf[c->head] = NULL;
    c->head = (c->head + 1) % c->cap;
    c->count--;
    pthread_cond_signal(&c->can_send);
    pthread_mutex_unlock(&c->mu);
    if (out) *out = v;
    else free(v);
    return 1;
}

static void *mako_select_last_ptr = NULL;
static int64_t mako_select_rr_ptr = 0;

static inline void *mako_chan_select_value_ptr(void) {
    return mako_select_last_ptr;
}

static inline int64_t mako_chan_ptr_selectn(
    MakoChanPtr **chs, int64_t n, int64_t timeout_ms
) {
    if (n < 1 || n > MAKO_SELECT_MAX || chs == NULL) return -1;
    struct timeval start, now;
    mako_gettimeofday(&start, NULL);
    for (;;) {
        int64_t start_i = mako_select_rr_ptr % n;
        for (int64_t k = 0; k < n; k++) {
            int64_t i = (start_i + k) % n;
            if (chs[i] == NULL) continue;
            void *v = NULL;
            if (mako_chan_ptr_try_recv(chs[i], &v)) {
                /* previous last_ptr not freed here — arm must free after take */
                mako_select_last_ptr = v;
                mako_select_rr_ptr = (i + 1) % n;
                return i;
            }
        }
        if (timeout_ms >= 0) {
            mako_gettimeofday(&now, NULL);
            int64_t elapsed =
                (now.tv_sec - start.tv_sec) * 1000 +
                (now.tv_usec - start.tv_usec) / 1000;
            if (elapsed >= timeout_ms) {
                mako_rt_counter_inc(&mako_rt_channel_select_timeouts);
                return -1;
            }
        }
        struct timespec ts = {0, 2000000L};
        nanosleep(&ts, NULL);
    }
}

static inline int64_t mako_chan_ptr_select2(
    MakoChanPtr *a, MakoChanPtr *b, int64_t timeout_ms
) {
    MakoChanPtr *chs[2] = {a, b};
    return mako_chan_ptr_selectn(chs, 2, timeout_ms);
}


/* ---- Structured concurrency nursery ---- */
typedef void *(*MakoTaskFn)(void *);

typedef struct {
    pthread_t thread;
    MakoTaskFn fn;
    void *arg;
    void *result;
    bool joined;
    bool cancelled_start; /* set if cancel before/during — cooperative */
    volatile int done;    /* 1 when trampoline finished (for timed join) */
} MakoTask;

/* MakoNursery — structured concurrency scope (Mako `crew` block).
 * Owns a set of spawned tasks. On scope exit, cancel_join cancels all tasks
 * then joins them — no orphaned threads. Tasks observe cancellation via
 * mako_nursery_cancelled() and should exit cooperatively.
 */
typedef struct {
    MakoTask *tasks;
    size_t len;
    size_t cap;
    volatile bool cancelled;
} MakoNursery;

static inline MakoNursery mako_nursery_new(void) {
    MakoNursery n;
    n.tasks = NULL;
    n.len = 0;
    n.cap = 0;
    n.cancelled = false;
    return n;
}

static inline void mako_nursery_cancel(MakoNursery *n) {
    n->cancelled = true;
}

static inline int64_t mako_nursery_cancelled(MakoNursery *n) {
    return n->cancelled ? 1 : 0;
}

/* Trampoline so timed join can poll `done` without blocking forever. */
static void *mako_task_trampoline(void *arg) {
    MakoTask *t = (MakoTask *)arg;
    void *r = NULL;
    if (t->fn) r = t->fn(t->arg);
    t->result = r;
#if defined(__STDC_NO_ATOMICS__)
    t->done = 1;
#else
    __atomic_store_n(&t->done, 1, __ATOMIC_RELEASE);
#endif
    return r;
}

static inline MakoTask *mako_spawn(MakoNursery *n, MakoTaskFn fn, void *arg) {
    if (n->len == n->cap) {
        size_t nc = n->cap ? n->cap * 2 : 4;
        n->tasks = (MakoTask *)realloc(n->tasks, nc * sizeof(MakoTask));
        n->cap = nc;
    }
    MakoTask *t = &n->tasks[n->len++];
    t->fn = fn;
    t->arg = arg;
    t->result = NULL;
    t->joined = false;
    t->cancelled_start = n->cancelled;
    t->done = 0;
    mako_rt_counter_inc(&mako_rt_tasks_spawned);
    if (n->cancelled) {
        /* do not start new work after cancel */
        t->joined = true;
        t->done = 1;
        t->result = (void *)(intptr_t)0;
        return t;
    }
    pthread_create(&t->thread, NULL, mako_task_trampoline, t);
    return t;
}

static inline void *mako_await(MakoTask *t) {
    if (!t->joined) {
        pthread_join(t->thread, &t->result);
        t->joined = true;
        t->done = 1;
        mako_rt_counter_inc(&mako_rt_tasks_joined);
    }
    return t->result;
}

/* Timed join: returns 1 and writes *out on success; 0 on timeout (task still running).
 * Polls task->done then joins; does not block past timeout on unfinished work.
 */
static inline int64_t mako_await_timeout_ms(MakoTask *t, int64_t ms, int64_t *out) {
    if (t->joined) {
        if (out) *out = (int64_t)(intptr_t)t->result;
        return 1;
    }
    if (ms < 0) ms = 0;
    int64_t waited = 0;
    while (waited < ms) {
        int done =
#if defined(__STDC_NO_ATOMICS__)
            t->done;
#else
            __atomic_load_n(&t->done, __ATOMIC_ACQUIRE);
#endif
        if (done) {
            pthread_join(t->thread, &t->result);
            t->joined = true;
            mako_rt_counter_inc(&mako_rt_tasks_joined);
            if (out) *out = (int64_t)(intptr_t)t->result;
            return 1;
        }
        struct timespec step = {0, 2 * 1000000L}; /* 2ms */
        nanosleep(&step, NULL);
        waited += 2;
    }
    /* Final check after last sleep */
    int done =
#if defined(__STDC_NO_ATOMICS__)
        t->done;
#else
        __atomic_load_n(&t->done, __ATOMIC_ACQUIRE);
#endif
    if (done) {
        pthread_join(t->thread, &t->result);
        t->joined = true;
        mako_rt_counter_inc(&mako_rt_tasks_joined);
        if (out) *out = (int64_t)(intptr_t)t->result;
        return 1;
    }
    if (out) *out = 0;
    return 0; /* still running — caller may later join or cancel_join */
}

static inline void mako_nursery_join_all(MakoNursery *n) {
    for (size_t i = 0; i < n->len; i++) {
        mako_await(&n->tasks[i]);
    }
    free(n->tasks);
    n->tasks = NULL;
    n->len = n->cap = 0;
}

/* Cancel then join — tasks cannot outlive cancel policy (structured concurrency). */
static inline void mako_nursery_cancel_join(MakoNursery *n) {
    mako_nursery_cancel(n);
    mako_nursery_join_all(n);
}

/* Forward — defined later in this header (time helpers). */
static inline int64_t mako_now_ms(void);

/* Drain crew with timeout: cancel, then join each task (sleep-poll).
 * Returns number of tasks joined; always best-effort completes joins. */
static inline int64_t mako_nursery_drain(MakoNursery *n, int64_t timeout_ms) {
    if (!n) return 0;
    if (timeout_ms < 0) timeout_ms = 0;
    mako_nursery_cancel(n);
    int64_t start = mako_now_ms();
    int64_t joined = 0;
    for (size_t i = 0; i < n->len; i++) {
        int64_t left = timeout_ms - (mako_now_ms() - start);
        if (left < 0) left = 0;
        int64_t out = 0;
        if (mako_await_timeout_ms(&n->tasks[i], left, &out) == 0) {
            /* timed out — force join without further wait budget */
            mako_await(&n->tasks[i]);
        }
        joined++;
    }
    free(n->tasks);
    n->tasks = NULL;
    n->len = n->cap = 0;
    return joined;
}

/* Global: drain is per-nursery; this records last drain count for metrics. */
static int64_t mako_last_crew_drain_joined = 0;
static inline int64_t mako_crew_drain(MakoNursery *n, int64_t timeout_ms) {
    int64_t j = mako_nursery_drain(n, timeout_ms);
    mako_last_crew_drain_joined = j;
    return j;
}
static inline int64_t mako_crew_drain_joined(void) {
    return mako_last_crew_drain_joined;
}

/* ---- Parallel map over int arrays ---- */
typedef int64_t (*MakoMapFn)(int64_t);

typedef struct {
    const int64_t *in;
    int64_t *out;
    MakoMapFn fn;
    size_t start;
    size_t end;
} MakoParChunk;

static void *mako_par_worker(void *arg) {
    MakoParChunk *c = (MakoParChunk *)arg;
    for (size_t i = c->start; i < c->end; i++) {
        c->out[i] = c->fn(c->in[i]);
    }
    return NULL;
}

static inline size_t mako_par_nthreads(size_t len) {
    size_t nthreads = 4;
#if defined(_SC_NPROCESSORS_ONLN)
    long hw = sysconf(_SC_NPROCESSORS_ONLN);
    if (hw > 0) nthreads = (size_t)hw;
#endif
    if (nthreads < 1) nthreads = 1;
    if (nthreads > 64) nthreads = 64;
    if (len < nthreads) nthreads = len;
    /* Small inputs: stay single-threaded to avoid spawn overhead (speed bar). */
    if (len < 64) nthreads = 1;
    if (nthreads < 1) nthreads = 1;
    return nthreads;
}

static inline MakoIntArray mako_par_map_int(MakoIntArray in, MakoMapFn fn) {
    MakoIntArray out = mako_int_array_new(in.len);
    if (in.len == 0) return out;

    size_t nthreads = mako_par_nthreads(in.len);
    if (nthreads == 1) {
        for (size_t i = 0; i < in.len; i++) {
            out.data[i] = fn(in.data[i]);
        }
        return out;
    }

    pthread_t *threads = (pthread_t *)malloc(nthreads * sizeof(pthread_t));
    MakoParChunk *chunks = (MakoParChunk *)malloc(nthreads * sizeof(MakoParChunk));
    size_t chunk = (in.len + nthreads - 1) / nthreads;

    for (size_t t = 0; t < nthreads; t++) {
        size_t start = t * chunk;
        size_t end = start + chunk;
        if (end > in.len) end = in.len;
        chunks[t] = (MakoParChunk){in.data, out.data, fn, start, end};
        pthread_create(&threads[t], NULL, mako_par_worker, &chunks[t]);
    }
    for (size_t t = 0; t < nthreads; t++) {
        pthread_join(threads[t], NULL);
    }
    free(threads);
    free(chunks);
    return out;
}

/* ---- Parallel map over float arrays ---- */
typedef double (*MakoMapFnF64)(double);

typedef struct {
    const double *in;
    double *out;
    MakoMapFnF64 fn;
    size_t start;
    size_t end;
} MakoParChunkF64;

static void *mako_par_worker_f64(void *arg) {
    MakoParChunkF64 *c = (MakoParChunkF64 *)arg;
    for (size_t i = c->start; i < c->end; i++) {
        c->out[i] = c->fn(c->in[i]);
    }
    return NULL;
}

static inline MakoFloatArray mako_par_map_float(MakoFloatArray in, MakoMapFnF64 fn) {
    MakoFloatArray out = mako_float_array_new(in.len);
    if (in.len == 0) return out;

    size_t nthreads = mako_par_nthreads(in.len);
    if (nthreads == 1) {
        for (size_t i = 0; i < in.len; i++) {
            out.data[i] = fn(in.data[i]);
        }
        return out;
    }

    pthread_t *threads = (pthread_t *)malloc(nthreads * sizeof(pthread_t));
    MakoParChunkF64 *chunks = (MakoParChunkF64 *)malloc(nthreads * sizeof(MakoParChunkF64));
    size_t chunk = (in.len + nthreads - 1) / nthreads;

    for (size_t t = 0; t < nthreads; t++) {
        size_t start = t * chunk;
        size_t end = start + chunk;
        if (end > in.len) end = in.len;
        chunks[t] = (MakoParChunkF64){in.data, out.data, fn, start, end};
        pthread_create(&threads[t], NULL, mako_par_worker_f64, &chunks[t]);
    }
    for (size_t t = 0; t < nthreads; t++) {
        pthread_join(threads[t], NULL);
    }
    free(threads);
    free(chunks);
    return out;
}

/* ---- Parallel map over string arrays (clones; mapper owns/returns string) ---- */
typedef MakoString (*MakoMapFnStr)(MakoString);

typedef struct {
    const MakoString *in;
    MakoString *out;
    MakoMapFnStr fn;
    size_t start;
    size_t end;
} MakoParChunkStr;

static void *mako_par_worker_str(void *arg) {
    MakoParChunkStr *c = (MakoParChunkStr *)arg;
    for (size_t i = c->start; i < c->end; i++) {
        c->out[i] = c->fn(mako_str_clone(c->in[i]));
    }
    return NULL;
}

static inline MakoStrArray mako_par_map_str(MakoStrArray in, MakoMapFnStr fn) {
    MakoStrArray out = mako_str_array_new(in.len);
    if (in.len == 0) return out;

    size_t nthreads = mako_par_nthreads(in.len);
    /* String map: prefer fewer threads (alloc pressure) */
    if (nthreads > 8) nthreads = 8;
    if (in.len < 32) nthreads = 1;

    if (nthreads == 1) {
        for (size_t i = 0; i < in.len; i++) {
            out.data[i] = fn(mako_str_clone(in.data[i]));
        }
        return out;
    }

    pthread_t *threads = (pthread_t *)malloc(nthreads * sizeof(pthread_t));
    MakoParChunkStr *chunks = (MakoParChunkStr *)malloc(nthreads * sizeof(MakoParChunkStr));
    size_t chunk = (in.len + nthreads - 1) / nthreads;

    for (size_t t = 0; t < nthreads; t++) {
        size_t start = t * chunk;
        size_t end = start + chunk;
        if (end > in.len) end = in.len;
        chunks[t] = (MakoParChunkStr){in.data, out.data, fn, start, end};
        pthread_create(&threads[t], NULL, mako_par_worker_str, &chunks[t]);
    }
    for (size_t t = 0; t < nthreads; t++) {
        pthread_join(threads[t], NULL);
    }
    free(threads);
    free(chunks);
    return out;
}

/* Parallel map over POD struct / byte arrays (element size esz).
 * fn writes one output element from one input element (no alias). */
typedef void (*MakoMapFnBytes)(void *dst, const void *src);

typedef struct {
    const char *in;
    char *out;
    size_t esz;
    MakoMapFnBytes fn;
    size_t start;
    size_t end;
} MakoParChunkBytes;

static void *mako_par_worker_bytes(void *arg) {
    MakoParChunkBytes *c = (MakoParChunkBytes *)arg;
    for (size_t i = c->start; i < c->end; i++) {
        c->fn(c->out + i * c->esz, c->in + i * c->esz);
    }
    return NULL;
}

static inline void mako_par_map_bytes(
    const void *in, void *out, size_t n, size_t esz, MakoMapFnBytes fn
) {
    if (!in || !out || !fn || esz == 0 || n == 0) return;
    size_t nthreads = mako_par_nthreads(n);
    if (nthreads == 1) {
        for (size_t i = 0; i < n; i++) {
            fn((char *)out + i * esz, (const char *)in + i * esz);
        }
        return;
    }
    pthread_t *threads = (pthread_t *)malloc(nthreads * sizeof(pthread_t));
    MakoParChunkBytes *chunks =
        (MakoParChunkBytes *)malloc(nthreads * sizeof(MakoParChunkBytes));
    size_t chunk = (n + nthreads - 1) / nthreads;
    for (size_t t = 0; t < nthreads; t++) {
        size_t start = t * chunk;
        size_t end = start + chunk;
        if (end > n) end = n;
        chunks[t] = (MakoParChunkBytes){
            (const char *)in, (char *)out, esz, fn, start, end};
        pthread_create(&threads[t], NULL, mako_par_worker_bytes, &chunks[t]);
    }
    for (size_t t = 0; t < nthreads; t++) pthread_join(threads[t], NULL);
    free(threads);
    free(chunks);
}

/* float64 bits ↔ int64 for int-ring channels */
static inline int64_t mako_f64_to_bits(double d) {
    union { double d; int64_t i; } u;
    u.d = d;
    return u.i;
}
static inline double mako_bits_to_f64(int64_t i) {
    union { double d; int64_t i; } u;
    u.i = i;
    return u.d;
}

/* ---- Arena (bump region) — free all at once, no GC ----
 * Linear allocator: allocations bump a pointer forward, individual frees are
 * impossible. Call mako_arena_free() or mako_arena_reset() to release everything.
 * Ideal for request-scoped work: allocate many small objects, free them all when
 * the request is done. 8-byte aligned. Grows by doubling (starts at 4KB).
 * Thread-local — do not share across threads without external synchronization.
 */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} MakoArena;

static inline MakoArena mako_arena_new(void) {
    MakoArena a;
    a.cap = 4096;
    a.len = 0;
    a.buf = (char *)malloc(a.cap);
    return a;
}

static inline void *mako_arena_alloc(MakoArena *a, size_t n) {
    /* 8-byte align */
    size_t align = 8;
    size_t pad = (align - (a->len % align)) % align;
    size_t need = a->len + pad + n;
    if (need > a->cap) {
        size_t nc = a->cap ? a->cap : 4096;
        while (nc < need) nc *= 2;
        a->buf = (char *)realloc(a->buf, nc);
        a->cap = nc;
    }
    a->len += pad;
    void *p = a->buf + a->len;
    a->len += n;
    return p;
}

static inline void mako_arena_reset(MakoArena *a) {
    a->len = 0;
}

static inline void mako_arena_free(MakoArena *a) {
    free(a->buf);
    a->buf = NULL;
    a->len = a->cap = 0;
}

static inline MakoString mako_arena_text(MakoArena *a, MakoString s) {
    char *d = (char *)mako_arena_alloc(a, s.len + 1);
    if (s.len && s.data) memcpy(d, s.data, s.len);
    d[s.len] = 0;
    MakoString out = {d, s.len};
    return out;
}

/* Copy raw bytes into arena — no intermediate malloc (HTTP hot path). */
static inline MakoString mako_arena_text_n(MakoArena *a, const char *p, size_t n) {
    char *d = (char *)mako_arena_alloc(a, n + 1);
    if (n && p) memcpy(d, p, n);
    d[n] = 0;
    MakoString out = {d, n};
    return out;
}

static inline MakoString mako_arena_cstr(MakoArena *a, const char *s) {
    if (!s || !*s) return mako_arena_text_n(a, "", 0);
    size_t n = strlen(s);
    return mako_arena_text_n(a, s, n);
}

static inline MakoIntArray mako_arena_ints(MakoArena *a, int64_t n) {
    size_t count = n < 0 ? 0 : (size_t)n;
    int64_t *data = (int64_t *)mako_arena_alloc(a, (count ? count : 1) * sizeof(int64_t));
    memset(data, 0, count * sizeof(int64_t));
    MakoIntArray arr = {data, count, count ? count : 1};
    return arr;
}

static inline MakoIntArray mako_arena_int_array_make(MakoArena *a, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    size_t c = (size_t)(cap ? cap : 1);
    int64_t *data = (int64_t *)mako_arena_alloc(a, c * sizeof(int64_t));
    memset(data, 0, c * sizeof(int64_t));
    MakoIntArray arr = {data, (size_t)len, c};
    return arr;
}

static inline MakoByteArray mako_arena_byte_array_make(MakoArena *a, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    size_t c = (size_t)(cap ? cap : 1);
    uint8_t *data = (uint8_t *)mako_arena_alloc(a, c);
    memset(data, 0, c);
    MakoByteArray arr = {data, (size_t)len, c};
    return arr;
}

static inline MakoStrArray mako_arena_str_array_make(MakoArena *a, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    size_t c = (size_t)(cap ? cap : 1);
    MakoString *data = (MakoString *)mako_arena_alloc(a, c * sizeof(MakoString));
    memset(data, 0, c * sizeof(MakoString));
    MakoStrArray arr = {data, (size_t)len, c};
    return arr;
}

static inline MakoFloatArray mako_arena_float_array_make(MakoArena *a, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    size_t c = (size_t)(cap ? cap : 1);
    double *data = (double *)mako_arena_alloc(a, c * sizeof(double));
    memset(data, 0, c * sizeof(double));
    MakoFloatArray arr = {data, (size_t)len, c};
    return arr;
}

static inline int64_t mako_arena_stamp(MakoArena *a, int64_t v) {
    int64_t *p = (int64_t *)mako_arena_alloc(a, sizeof(int64_t));
    *p = v;
    return *p;
}




#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static int mako_argc_g = 0;
static char **mako_argv_g = NULL;

static inline void mako_set_args(int argc, char **argv) {
    mako_argc_g = argc;
    mako_argv_g = argv;
}

static inline int64_t mako_argc(void) {
    return (int64_t)mako_argc_g;
}

static inline MakoString mako_arg_get(int64_t i) {
    if (i < 0 || i >= mako_argc_g || !mako_argv_g) {
        return mako_str_from_cstr("");
    }
    return mako_str_from_cstr(mako_argv_g[i]);
}

static inline MakoStrArray mako_args(void) {
    MakoStrArray a = mako_str_array_make(mako_argc_g, mako_argc_g);
    for (int i = 0; i < mako_argc_g; i++) {
        a.data[i] = mako_str_from_cstr(mako_argv_g[i]);
    }
    a.len = (size_t)mako_argc_g;
    return a;
}

static inline int64_t mako_mkdir(MakoString path) {
    const char *p = path.data ? path.data : "";
#if defined(_WIN32)
    if (_mkdir(p) == 0) return 0;
#else
    if (mkdir(p, 0755) == 0) return 0;
#endif
    if (errno == EEXIST) return 0;
    return -1;
}

static inline bool mako_file_exists(MakoString path) {
    const char *p = path.data ? path.data : "";
    struct stat st;
    return stat(p, &st) == 0;
}


static inline MakoResultInt mako_parse_int(MakoString s) {
    const char *p = s.data ? s.data : "";
    char *end = NULL;
    errno = 0;
    long long v = strtoll(p, &end, 10);
    if (end == p || (end && *end != '\0') || errno == ERANGE) {
        return mako_err_int(mako_str_from_cstr("parse_int failed"));
    }
    return mako_ok_int((int64_t)v);
}

static inline double mako_parse_float(MakoString s) {
    const char *p = s.data ? s.data : "";
    char *end = NULL;
    errno = 0;
    double v = strtod(p, &end);
    if (end == p || (end && *end != '\0') || errno == ERANGE) {
        return 0.0;
    }
    return v;
}

static inline MakoString mako_base64_encode(MakoString s) {
    static const char *T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n = s.len;
    size_t out_len = 4 * ((n + 2) / 3);
    char *o = (char *)malloc(out_len + 1);
    size_t j = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned int v = (unsigned char)s.data[i] << 16;
        if (i + 1 < n) v |= (unsigned char)s.data[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned char)s.data[i + 2];
        o[j++] = T[(v >> 18) & 63];
        o[j++] = T[(v >> 12) & 63];
        o[j++] = (i + 1 < n) ? T[(v >> 6) & 63] : '=';
        o[j++] = (i + 2 < n) ? T[v & 63] : '=';
    }
    o[j] = 0;
    return (MakoString){o, j};
}

static inline int mako_b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static inline MakoString mako_base64_decode(MakoString s) {
    size_t n = s.len;
    size_t cap = n / 4 * 3 + 4;
    char *o = (char *)malloc(cap + 1);
    size_t j = 0;
    unsigned int buf = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s.data[i];
        if (c == '=' || c == '\n' || c == '\r') continue;
        int v = mako_b64_val(c);
        if (v < 0) continue;
        buf = (buf << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            o[j++] = (char)((buf >> bits) & 0xff);
        }
    }
    o[j] = 0;
    return (MakoString){o, j};
}

static int mako_cmp_i64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

static inline MakoIntArray mako_sort_ints(MakoIntArray a) {
    MakoIntArray out = mako_int_array_make((int64_t)a.len, (int64_t)a.len);
    if (a.len) memcpy(out.data, a.data, a.len * sizeof(int64_t));
    out.len = a.len;
    if (out.len > 1) qsort(out.data, out.len, sizeof(int64_t), mako_cmp_i64);
    return out;
}

static int mako_cmp_str(const void *a, const void *b) {
    const MakoString *x = (const MakoString *)a;
    const MakoString *y = (const MakoString *)b;
    size_t n = x->len < y->len ? x->len : y->len;
    int c = memcmp(x->data ? x->data : "", y->data ? y->data : "", n);
    if (c != 0) return c;
    return (x->len > y->len) - (x->len < y->len);
}

static inline MakoStrArray mako_sort_strings(MakoStrArray a) {
    MakoStrArray out = mako_str_array_make((int64_t)a.len, (int64_t)a.len);
    for (size_t i = 0; i < a.len; i++) {
        out.data[i] = mako_str_clone(a.data[i]);
    }
    out.len = a.len;
    if (out.len > 1) qsort(out.data, out.len, sizeof(MakoString), mako_cmp_str);
    return out;
}

/* Regex: `.` ; `*`/`+`/`?` ; `|` ; `[abc]`/`[a-z]`/`[^…]` ; `(…)` numbered captures ; `^`/`$`.
 * RE2-ish escapes: `\d` `\D` `\w` `\W` `\s` `\S` ; literal `\\` `\.` etc.
 * `regex_capture(pat, text, n)` returns group n (1-based); n=0 is full match. */
#define MAKO_RE_MAX_CAPS 9
static size_t mako_re_cap_off[MAKO_RE_MAX_CAPS];
static size_t mako_re_cap_len[MAKO_RE_MAX_CAPS];
static int mako_re_cap_set[MAKO_RE_MAX_CAPS];
static int mako_re_capture_on;
static int mako_re_alt_number; /* only top-level | numbering */
static int mako_re_group_seq;
static const char *mako_re_text_base;
static int mako_re_allow_prefix; /* 1 = pattern end succeeds without consuming all text */

static inline int mako_re_here(const char *pat, size_t plen, const char *text, size_t tlen);
static inline int mako_re_piece(const char *pat, size_t plen, const char *text, size_t tlen);

static inline void mako_re_caps_clear(void) {
    for (int i = 0; i < MAKO_RE_MAX_CAPS; i++) {
        mako_re_cap_set[i] = 0;
        mako_re_cap_off[i] = 0;
        mako_re_cap_len[i] = 0;
    }
    mako_re_group_seq = 1; /* 0 reserved for full match */
}

/* Count `(` opens in pat[0..end) skipping classes — for `|` group numbering. */
static inline int mako_re_count_opens(const char *pat, size_t end) {
    int n = 0;
    for (size_t i = 0; i < end; i++) {
        if (pat[i] == '[') {
            i++;
            while (i < end && pat[i] != ']') i++;
            continue;
        }
        if (pat[i] == '(') n++;
    }
    return n;
}

static inline int mako_re_class_match(const char *cls, size_t clen, char ch) {
    int neg = 0;
    size_t i = 0;
    if (clen > 0 && cls[0] == '^') {
        neg = 1;
        i = 1;
    }
    int hit = 0;
    unsigned char uch = (unsigned char)ch;
    while (i < clen) {
        /* RE2-ish POSIX / escape classes inside [] */
        if (i + 1 < clen && cls[i] == '\\') {
            int ok = 0;
            switch (cls[i + 1]) {
            case 'd': ok = (uch >= '0' && uch <= '9'); break;
            case 'D': ok = !(uch >= '0' && uch <= '9'); break;
            case 'w': ok = ((uch >= 'a' && uch <= 'z') || (uch >= 'A' && uch <= 'Z')
                            || (uch >= '0' && uch <= '9') || uch == '_'); break;
            case 'W': ok = !((uch >= 'a' && uch <= 'z') || (uch >= 'A' && uch <= 'Z')
                             || (uch >= '0' && uch <= '9') || uch == '_'); break;
            case 's': ok = (uch == ' ' || uch == '\t' || uch == '\n' || uch == '\r'
                            || uch == '\f' || uch == '\v'); break;
            case 'S': ok = !(uch == ' ' || uch == '\t' || uch == '\n' || uch == '\r'
                             || uch == '\f' || uch == '\v'); break;
            default: ok = (uch == (unsigned char)cls[i + 1]); break;
            }
            if (ok) { hit = 1; break; }
            i += 2;
            continue;
        }
        if (i + 1 < clen && cls[i] == '[' && cls[i + 1] == ':') {
            /* [:digit:] [:alpha:] [:alnum:] [:space:] [:lower:] [:upper:] [:punct:] */
            size_t j = i + 2;
            while (j + 1 < clen && !(cls[j] == ':' && cls[j + 1] == ']')) j++;
            if (j + 1 < clen) {
                size_t n = j - (i + 2);
                int ok = 0;
                if (n == 5 && memcmp(cls + i + 2, "digit", 5) == 0)
                    ok = (uch >= '0' && uch <= '9');
                else if (n == 5 && memcmp(cls + i + 2, "alpha", 5) == 0)
                    ok = ((uch >= 'a' && uch <= 'z') || (uch >= 'A' && uch <= 'Z'));
                else if (n == 5 && memcmp(cls + i + 2, "alnum", 5) == 0)
                    ok = ((uch >= 'a' && uch <= 'z') || (uch >= 'A' && uch <= 'Z')
                          || (uch >= '0' && uch <= '9'));
                else if (n == 5 && memcmp(cls + i + 2, "space", 5) == 0)
                    ok = (uch == ' ' || uch == '\t' || uch == '\n' || uch == '\r'
                          || uch == '\f' || uch == '\v');
                else if (n == 5 && memcmp(cls + i + 2, "lower", 5) == 0)
                    ok = (uch >= 'a' && uch <= 'z');
                else if (n == 5 && memcmp(cls + i + 2, "upper", 5) == 0)
                    ok = (uch >= 'A' && uch <= 'Z');
                else if (n == 5 && memcmp(cls + i + 2, "punct", 5) == 0)
                    ok = (uch >= 33 && uch <= 47) || (uch >= 58 && uch <= 64)
                         || (uch >= 91 && uch <= 96) || (uch >= 123 && uch <= 126);
                if (ok) { hit = 1; break; }
                i = j + 2;
                continue;
            }
        }
        if (i + 2 < clen && cls[i + 1] == '-') {
            char lo = cls[i], hi = cls[i + 2];
            if (ch >= lo && ch <= hi) { hit = 1; break; }
            i += 3;
            continue;
        }
        if (cls[i] == ch) { hit = 1; break; }
        i++;
    }
    return neg ? !hit : hit;
}

static inline uint32_t mako_re_utf8_decode(const char *text, size_t tlen, size_t *consumed) {
    *consumed = 0;
    if (tlen == 0 || !text) return 0;
    unsigned char c0 = (unsigned char)text[0];
    if (c0 < 0x80) {
        *consumed = 1;
        return (uint32_t)c0;
    }
    if ((c0 & 0xE0) == 0xC0 && tlen >= 2) {
        unsigned char c1 = (unsigned char)text[1];
        if ((c1 & 0xC0) == 0x80) {
            uint32_t cp = ((uint32_t)(c0 & 0x1F) << 6) | (uint32_t)(c1 & 0x3F);
            if (cp >= 0x80) {
                *consumed = 2;
                return cp;
            }
        }
    }
    if ((c0 & 0xF0) == 0xE0 && tlen >= 3) {
        unsigned char c1 = (unsigned char)text[1];
        unsigned char c2 = (unsigned char)text[2];
        if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80) {
            uint32_t cp = ((uint32_t)(c0 & 0x0F) << 12)
                        | ((uint32_t)(c1 & 0x3F) << 6)
                        | (uint32_t)(c2 & 0x3F);
            if (cp >= 0x800 && !(cp >= 0xD800 && cp <= 0xDFFF)) {
                *consumed = 3;
                return cp;
            }
        }
    }
    if ((c0 & 0xF8) == 0xF0 && tlen >= 4) {
        unsigned char c1 = (unsigned char)text[1];
        unsigned char c2 = (unsigned char)text[2];
        unsigned char c3 = (unsigned char)text[3];
        if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
            uint32_t cp = ((uint32_t)(c0 & 0x07) << 18)
                        | ((uint32_t)(c1 & 0x3F) << 12)
                        | ((uint32_t)(c2 & 0x3F) << 6)
                        | (uint32_t)(c3 & 0x3F);
            if (cp >= 0x10000 && cp <= 0x10FFFF) {
                *consumed = 4;
                return cp;
            }
        }
    }
    *consumed = 1;
    return (uint32_t)c0;
}

static inline int mako_re_unicode_is_letter(uint32_t cp) {
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return 1;
    if (cp >= 0x00C0 && cp <= 0x02AF) return 1;     /* Latin-1/extended IPA */
    if (cp >= 0x0370 && cp <= 0x03FF) return 1;     /* Greek */
    if (cp >= 0x0400 && cp <= 0x052F) return 1;     /* Cyrillic */
    if (cp >= 0x0531 && cp <= 0x058F) return 1;     /* Armenian */
    if (cp >= 0x0590 && cp <= 0x05FF) return 1;     /* Hebrew */
    if (cp >= 0x0600 && cp <= 0x06FF) return 1;     /* Arabic */
    if (cp >= 0x0900 && cp <= 0x097F) return 1;     /* Devanagari */
    if (cp >= 0x3040 && cp <= 0x30FF) return 1;     /* Hiragana/Katakana */
    if (cp >= 0x3400 && cp <= 0x9FFF) return 1;     /* CJK */
    if (cp >= 0xAC00 && cp <= 0xD7AF) return 1;     /* Hangul */
    return 0;
}

static inline int mako_re_unicode_is_digit(uint32_t cp) {
    if (cp >= '0' && cp <= '9') return 1;
    if (cp >= 0x0660 && cp <= 0x0669) return 1;     /* Arabic-Indic */
    if (cp >= 0x06F0 && cp <= 0x06F9) return 1;     /* Extended Arabic-Indic */
    if (cp >= 0x0966 && cp <= 0x096F) return 1;     /* Devanagari */
    if (cp >= 0xFF10 && cp <= 0xFF19) return 1;     /* Fullwidth */
    return 0;
}

static inline int mako_re_unicode_is_space(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x00A0
        || (cp >= 0x2000 && cp <= 0x200B) || cp == 0x3000;
}
static inline int mako_re_unicode_is_punct(uint32_t cp) {
    if ((cp >= 0x21 && cp <= 0x2F) || (cp >= 0x3A && cp <= 0x40)
        || (cp >= 0x5B && cp <= 0x60) || (cp >= 0x7B && cp <= 0x7E))
        return 1;
    if (cp >= 0x2000 && cp <= 0x206F) return 1; /* general punctuation */
    if (cp >= 0x3000 && cp <= 0x303F) return 1; /* CJK symbols/punct */
    return 0;
}
static inline int mako_re_unicode_is_symbol(uint32_t cp) {
    if (cp == '$' || cp == '+' || cp == '<' || cp == '=' || cp == '>' || cp == '^'
        || cp == '`' || cp == '|' || cp == '~')
        return 1;
    if (cp >= 0x20A0 && cp <= 0x20CF) return 1; /* currency */
    if (cp >= 0x2200 && cp <= 0x22FF) return 1; /* math */
    return 0;
}
static inline int mako_re_unicode_prop_match(const char *name, size_t nlen, uint32_t cp) {
    if ((nlen == 1 && name[0] == 'L') || (nlen == 6 && memcmp(name, "Letter", 6) == 0)
        || (nlen == 2 && memcmp(name, "L&", 2) == 0) || (nlen == 2 && memcmp(name, "Lu", 2) == 0)
        || (nlen == 2 && memcmp(name, "Ll", 2) == 0) || (nlen == 2 && memcmp(name, "Lt", 2) == 0))
        return mako_re_unicode_is_letter(cp);
    if ((nlen == 1 && name[0] == 'N') || (nlen == 6 && memcmp(name, "Number", 6) == 0)
        || (nlen == 2 && memcmp(name, "Nd", 2) == 0)
        || (nlen == 14 && memcmp(name, "Decimal_Number", 14) == 0))
        return mako_re_unicode_is_digit(cp);
    if ((nlen == 1 && name[0] == 'Z') || (nlen == 9 && memcmp(name, "Separator", 9) == 0)
        || (nlen == 2 && memcmp(name, "Zs", 2) == 0))
        return mako_re_unicode_is_space(cp);
    if ((nlen == 1 && name[0] == 'P') || (nlen == 10 && memcmp(name, "Punctuation", 10) == 0)
        || (nlen == 2 && (memcmp(name, "Po", 2) == 0 || memcmp(name, "Pd", 2) == 0)))
        return mako_re_unicode_is_punct(cp);
    if ((nlen == 1 && name[0] == 'S') || (nlen == 6 && memcmp(name, "Symbol", 6) == 0)
        || (nlen == 2 && memcmp(name, "Sc", 2) == 0))
        return mako_re_unicode_is_symbol(cp);
    if (nlen == 5 && memcmp(name, "Greek", 5) == 0)
        return (cp >= 0x0370 && cp <= 0x03FF);
    if (nlen == 8 && memcmp(name, "Cyrillic", 8) == 0)
        return (cp >= 0x0400 && cp <= 0x052F);
    if (nlen == 5 && memcmp(name, "Latin", 5) == 0)
        return (cp <= 0x02AF && mako_re_unicode_is_letter(cp));
    if (nlen == 6 && memcmp(name, "Arabic", 6) == 0)
        return (cp >= 0x0600 && cp <= 0x06FF);
    if (nlen == 6 && memcmp(name, "Hebrew", 6) == 0)
        return (cp >= 0x0590 && cp <= 0x05FF);
    if (nlen == 3 && memcmp(name, "Han", 3) == 0)
        return (cp >= 0x3400 && cp <= 0x9FFF);
    if (nlen == 2 && memcmp(name, "Nl", 2) == 0) /* letter numbers */
        return (cp >= 0x16EE && cp <= 0x16F0) || (cp >= 0x2160 && cp <= 0x2188);
    if (nlen == 2 && memcmp(name, "No", 2) == 0) /* other numbers */
        return (cp >= 0x00B2 && cp <= 0x00B3) || cp == 0x00B9
            || (cp >= 0x2070 && cp <= 0x2079);
    if (nlen == 1 && name[0] == 'M') /* mark category seed */
        return (cp >= 0x0300 && cp <= 0x036F);
    if (nlen == 8 && memcmp(name, "Hiragana", 8) == 0)
        return (cp >= 0x3040 && cp <= 0x309F);
    if (nlen == 8 && memcmp(name, "Katakana", 8) == 0)
        return (cp >= 0x30A0 && cp <= 0x30FF);
    if (nlen == 6 && memcmp(name, "Hangul", 6) == 0)
        return (cp >= 0xAC00 && cp <= 0xD7AF);
    if (nlen == 4 && memcmp(name, "Thai", 4) == 0)
        return (cp >= 0x0E00 && cp <= 0x0E7F);
    if (nlen == 10 && memcmp(name, "Devanagari", 10) == 0)
        return (cp >= 0x0900 && cp <= 0x097F);
    return 0;
}

/* Find matching `)` for group starting at pat[0]=='('; returns index of `)` or plen. */
static inline size_t mako_re_group_end(const char *pat, size_t plen) {
    size_t depth = 0;
    for (size_t i = 0; i < plen; i++) {
        if (pat[i] == '[') {
            i++;
            while (i < plen && pat[i] != ']') i++;
            continue;
        }
        if (pat[i] == '(') depth++;
        else if (pat[i] == ')') {
            if (depth == 0) return i; /* shouldn't */
            depth--;
            if (depth == 0) return i;
        }
    }
    return plen;
}

/* Match atom against a prefix of text; *consumed = chars of text matched (0+). */
static inline int mako_re_atom_ex(const char *pat, size_t plen, const char *text, size_t tlen,
                                 size_t *alen, size_t *consumed, int *matched) {
    *matched = 0;
    *consumed = 0;
    *alen = 0;
    if (plen == 0) return 0;
    if (pat[0] == '(') {
        size_t end = mako_re_group_end(pat, plen);
        if (end >= plen) { *alen = plen; return 0; }
        *alen = end + 1;
        const char *body = pat + 1;
        size_t blen = end - 1;
        int noncap = 0;
        if (blen >= 2 && body[0] == '?' && body[1] == ':') {
            noncap = 1;
            body += 2;
            blen -= 2;
        }
        int gidx = -1;
        if (mako_re_capture_on && !noncap) {
            gidx = mako_re_group_seq++;
            if (gidx >= MAKO_RE_MAX_CAPS) gidx = -1;
        }
        int saved_seq = mako_re_group_seq;
        for (size_t n = 0; n <= tlen; n++) {
            mako_re_group_seq = saved_seq;
            char *buf = (char *)malloc(blen + 2);
            memcpy(buf, body, blen);
            buf[blen] = '$';
            int saved_alt = mako_re_alt_number;
            mako_re_alt_number = 0;
            int body_ok = mako_re_here(buf, blen + 1, text, n);
            mako_re_alt_number = saved_alt;
            if (body_ok) {
                free(buf);
                *matched = 1;
                *consumed = n;
                if (gidx >= 0 && mako_re_text_base) {
                    mako_re_cap_off[gidx] = (size_t)(text - mako_re_text_base);
                    mako_re_cap_len[gidx] = n;
                    mako_re_cap_set[gidx] = 1;
                }
                return 1;
            }
            free(buf);
        }
        mako_re_group_seq = saved_seq;
        return 1; /* atom parsed; no match */
    }
    if (pat[0] == '[') {
        size_t j = 1;
        while (j < plen) {
            if (pat[j] == '\\' && j + 1 < plen) { j += 2; continue; }
            if (j + 1 < plen && pat[j] == '[' && pat[j + 1] == ':') {
                j += 2;
                while (j + 1 < plen && !(pat[j] == ':' && pat[j + 1] == ']')) j++;
                if (j + 1 < plen) j += 2;
                continue;
            }
            if (pat[j] == ']') break;
            j++;
        }
        if (j >= plen) { *alen = plen; return 0; }
        *alen = j + 1;
        if (tlen == 0) return 1;
        *matched = mako_re_class_match(pat + 1, j - 1, text[0]);
        *consumed = *matched ? 1 : 0;
        return 1;
    }
    if (pat[0] == '\\' && plen >= 2) {
        if (pat[1] == 'Q') {
            size_t j = 2;
            while (j + 1 < plen) {
                if (pat[j] == '\\' && pat[j + 1] == 'E') { j += 2; break; }
                j++;
            }
            *alen = j;
            size_t lit_start = 2;
            size_t lit_end = (j >= 4 && pat[j - 2] == '\\' && pat[j - 1] == 'E') ? j - 2 : j;
            size_t lit_len = lit_end > lit_start ? lit_end - lit_start : 0;
            if (tlen < lit_len) { *matched = 0; *consumed = 0; return 1; }
            *matched = (lit_len == 0) || (memcmp(text, pat + lit_start, lit_len) == 0);
            *consumed = *matched ? lit_len : 0;
            return 1;
        }
        *alen = 2;
        if (tlen == 0) return 1;
        unsigned char ch = (unsigned char)text[0];
        int ok = 0;
        size_t override_consumed = 0;
        switch (pat[1]) {
        case 'd': ok = (ch >= '0' && ch <= '9'); break;
        case 'D': ok = !(ch >= '0' && ch <= '9'); break;
        case 'w': ok = ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                        || (ch >= '0' && ch <= '9') || ch == '_'); break;
        case 'W': ok = !((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                         || (ch >= '0' && ch <= '9') || ch == '_'); break;
        case 's': ok = (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v'); break;
        case 'S': ok = !(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v'); break;
        case 'b': case 'B': case 'A': case 'z': case 'G':
            /* zero-width — parsed here but matched in mako_re_piece */
            *matched = 1; *consumed = 0; return 1;
        case 'n': ok = (ch == '\n'); break;
        case 't': ok = (ch == '\t'); break;
        case 'r': ok = (ch == '\r'); break;
        case 'f': ok = (ch == '\f'); break;
        case 'v': ok = (ch == '\v'); break;
        case 'x': {
            if (plen < 4) { *alen = plen; return 0; }
            *alen = 4;
            int hi = -1, lo = -1;
            char a = pat[2], b = pat[3];
            if (a >= '0' && a <= '9') hi = a - '0';
            else if (a >= 'a' && a <= 'f') hi = a - 'a' + 10;
            else if (a >= 'A' && a <= 'F') hi = a - 'A' + 10;
            if (b >= '0' && b <= '9') lo = b - '0';
            else if (b >= 'a' && b <= 'f') lo = b - 'a' + 10;
            else if (b >= 'A' && b <= 'F') lo = b - 'A' + 10;
            if (hi < 0 || lo < 0) { *matched = 0; *consumed = 0; return 1; }
            ok = (ch == (unsigned char)((hi << 4) | lo));
            break;
        }
        case '1': case '2': case '3': case '4': case '5':
        case '6': case '7': case '8': case '9': {
            /* Backreference \1..\9 against captured groups */
            int g = pat[1] - '0';
            if (!mako_re_text_base || g >= MAKO_RE_MAX_CAPS || !mako_re_cap_set[g]) {
                *matched = 0; *consumed = 0; return 1;
            }
            size_t cl = mako_re_cap_len[g];
            const char *cp = mako_re_text_base + mako_re_cap_off[g];
            if (tlen < cl) { *matched = 0; *consumed = 0; return 1; }
            *matched = (cl == 0) || (memcmp(text, cp, cl) == 0);
            *consumed = *matched ? cl : 0;
            return 1;
        }
        case 'p': {
            if (plen < 5 || pat[2] != '{') { *alen = 2; ok = 0; break; }
            size_t j = 3;
            while (j < plen && pat[j] != '}') j++;
            if (j >= plen) { *alen = plen; return 0; }
            *alen = j + 1;
            size_t cp_len = 0;
            uint32_t cp = mako_re_utf8_decode(text, tlen, &cp_len);
            ok = mako_re_unicode_prop_match(pat + 3, j - 3, cp);
            if (ok) override_consumed = cp_len;
            break;
        }
        default: ok = (ch == (unsigned char)pat[1]); break;
        }
        *matched = ok;
        *consumed = ok ? (override_consumed ? override_consumed : 1) : 0;
        return 1;
    }
    *alen = 1;
    if (tlen == 0) return 1;
    if (pat[0] == '.') { *matched = 1; *consumed = 1; return 1; }
    *matched = (pat[0] == text[0]);
    *consumed = *matched ? 1 : 0;
    return 1;
}

static inline int mako_re_atom(const char *pat, size_t plen, const char *text, size_t tlen, size_t *alen, int *matched) {
    size_t consumed = 0;
    int ok = mako_re_atom_ex(pat, plen, text, tlen, alen, &consumed, matched);
    if (ok && *matched && consumed != 1 && pat[0] != '(') {
        /* single-char atoms */
    }
    return ok;
}

static inline int mako_re_atom_once(const char *atom, size_t alen, const char *text, size_t tlen, size_t *consumed) {
    int matched = 0;
    size_t a2 = 0;
    if (!mako_re_atom_ex(atom, alen, text, tlen, &a2, consumed, &matched) || !matched) return 0;
    return 1;
}

static inline int mako_re_star_atom(const char *atom, size_t alen, const char *pat, size_t plen, const char *text, size_t tlen) {
    /* greedy: collect max matches then backtrack */
    size_t pos[64];
    size_t npos = 0;
    pos[npos++] = 0;
    size_t i = 0;
    while (npos < 64) {
        size_t c = 0;
        if (!mako_re_atom_once(atom, alen, text + i, tlen - i, &c) || c == 0) break;
        i += c;
        pos[npos++] = i;
    }
    while (npos > 0) {
        npos--;
        if (mako_re_here(pat, plen, text + pos[npos], tlen - pos[npos])) return 1;
    }
    return 0;
}

static inline int mako_re_plus_atom(const char *atom, size_t alen, const char *pat, size_t plen, const char *text, size_t tlen) {
    size_t c = 0;
    if (!mako_re_atom_once(atom, alen, text, tlen, &c) || c == 0) return 0;
    return mako_re_star_atom(atom, alen, pat, plen, text + c, tlen - c);
}

static inline int mako_re_opt_atom(const char *atom, size_t alen, const char *pat, size_t plen, const char *text, size_t tlen) {
    size_t c = 0;
    if (mako_re_atom_once(atom, alen, text, tlen, &c) && c > 0) {
        if (mako_re_here(pat, plen, text + c, tlen - c)) return 1;
    }
    return mako_re_here(pat, plen, text, tlen);
}

/* Match a single alternative (no top-level `|`). Backtracks into `(…)` lengths. */
static inline int mako_re_is_word(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
static inline int mako_re_word_bound(const char *text, size_t tlen, size_t ti) {
    int left = (ti > 0) && mako_re_is_word((unsigned char)text[ti - 1]);
    int right = (ti < tlen) && mako_re_is_word((unsigned char)text[ti]);
    return left != right;
}

static inline int mako_re_piece(const char *pat, size_t plen, const char *text, size_t tlen) {
    size_t pi = 0, ti = 0;
    while (pi < plen) {
        if (pat[pi] == '$' && pi + 1 == plen) return ti == tlen;
        /* RE2-ish zero-width: \b \B \A \z */
        if (pat[pi] == '\\' && pi + 1 < plen) {
            char esc = pat[pi + 1];
            if (esc == 'b' || esc == 'B' || esc == 'A' || esc == 'z' || esc == 'G') {
                int ok = 0;
                if (esc == 'A' || esc == 'G') ok = (ti == 0);
                else if (esc == 'z') ok = (ti == tlen);
                else if (esc == 'b') ok = mako_re_word_bound(text, tlen, ti);
                else ok = !mako_re_word_bound(text, tlen, ti);
                if (!ok) return 0;
                pi += 2;
                continue;
            }
        }

        /* Simple zero-width lookahead: (?=...) and (?!...). */
        if (pat[pi] == '(' && pi + 2 < plen && pat[pi + 1] == '?' &&
            (pat[pi + 2] == '=' || pat[pi + 2] == '!')) {
            size_t end = mako_re_group_end(pat + pi, plen - pi);
            if (end >= plen - pi) return 0;
            const char *body = pat + pi + 3;
            size_t blen = end - 3;
            int saved_allow = mako_re_allow_prefix;
            int saved_seq = mako_re_group_seq;
            mako_re_allow_prefix = 1;
            int ok = mako_re_here(body, blen, text + ti, tlen - ti);
            mako_re_allow_prefix = saved_allow;
            mako_re_group_seq = saved_seq;
            if (pat[pi + 2] == '!') ok = !ok;
            if (!ok) return 0;
            pi += end + 1;
            continue;
        }

        /* Special-case unquantified group: try body lengths longest-first with rest. */
        if (pat[pi] == '(') {
            size_t end = mako_re_group_end(pat + pi, plen - pi);
            if (end >= plen - pi) return 0;
            size_t alen = end + 1;
            const char *body = pat + pi + 1;
            size_t blen = end - 1;
            int noncap = 0;
            if (blen >= 2 && body[0] == '?' && body[1] == ':') {
                noncap = 1;
                body += 2;
                blen -= 2;
            }
            int gidx = -1;
            int saved_seq = mako_re_group_seq;
            if (mako_re_capture_on && !noncap) {
                gidx = mako_re_group_seq++;
                if (gidx >= MAKO_RE_MAX_CAPS) gidx = -1;
            }
            if (pi + alen < plen && (pat[pi + alen] == '*' || pat[pi + alen] == '+' || pat[pi + alen] == '?' || pat[pi + alen] == '{')) {
                /* quantified group — fall through to atom quantifier path */
                mako_re_group_seq = saved_seq;
            } else {
                const char *rest = pat + pi + alen;
                size_t rlen = plen - pi - alen;
                for (size_t n = tlen - ti + 1; n-- > 0; ) {
                    mako_re_group_seq = saved_seq + ((mako_re_capture_on && !noncap) ? 1 : 0);
                    char *buf = (char *)malloc(blen + 2);
                    memcpy(buf, body, blen);
                    buf[blen] = '$';
                    int saved_alt = mako_re_alt_number;
                    mako_re_alt_number = 0;
                    int body_ok = mako_re_here(buf, blen + 1, text + ti, n);
                    mako_re_alt_number = saved_alt;
                    if (!body_ok) { free(buf); continue; }
                    /* Publish capture before matching rest so backrefs (\1) work. */
                    int saved_set = 0;
                    size_t saved_off = 0, saved_len = 0;
                    if (gidx >= 0 && mako_re_text_base) {
                        saved_set = mako_re_cap_set[gidx];
                        saved_off = mako_re_cap_off[gidx];
                        saved_len = mako_re_cap_len[gidx];
                        mako_re_cap_off[gidx] = (size_t)((text + ti) - mako_re_text_base);
                        mako_re_cap_len[gidx] = n;
                        mako_re_cap_set[gidx] = 1;
                    }
                    if (mako_re_piece(rest, rlen, text + ti + n, tlen - ti - n)) {
                        free(buf);
                        return 1;
                    }
                    if (gidx >= 0) {
                        mako_re_cap_set[gidx] = saved_set;
                        mako_re_cap_off[gidx] = saved_off;
                        mako_re_cap_len[gidx] = saved_len;
                    }
                    free(buf);
                }
                mako_re_group_seq = saved_seq;
                return 0;
            }
        }

        size_t alen = 0, consumed = 0;
        int matched = 0;
        if (!mako_re_atom_ex(pat + pi, plen - pi, text + ti, tlen - ti, &alen, &consumed, &matched)) {
            if (alen == 0) return 0;
        }
        /* {n} / {n,m} / {n,} quantifiers */
        if (pi + alen < plen && pat[pi + alen] == '{') {
            size_t qi = pi + alen + 1;
            int nmin = 0, nmax = -1;
            int saw = 0;
            while (qi < plen && pat[qi] >= '0' && pat[qi] <= '9') {
                nmin = nmin * 10 + (pat[qi] - '0');
                qi++;
                saw = 1;
            }
            int ok_brace = 0;
            if (saw && qi < plen && pat[qi] == '}') {
                nmax = nmin;
                qi++;
                ok_brace = 1;
            } else if (saw && qi < plen && pat[qi] == ',') {
                qi++;
                if (qi < plen && pat[qi] == '}') {
                    nmax = 64;
                    qi++;
                    ok_brace = 1;
                } else {
                    nmax = 0;
                    int saw2 = 0;
                    while (qi < plen && pat[qi] >= '0' && pat[qi] <= '9') {
                        nmax = nmax * 10 + (pat[qi] - '0');
                        qi++;
                        saw2 = 1;
                    }
                    if (saw2 && qi < plen && pat[qi] == '}') {
                        qi++;
                        ok_brace = 1;
                    }
                }
            }
            if (ok_brace) {
                const char *rest = pat + qi;
                size_t rlen = plen - qi;
                if (nmax > 64) nmax = 64;
                if (nmax < nmin) nmax = nmin;
                for (int cnt = nmax; cnt >= nmin; cnt--) {
                    size_t pos = ti;
                    int okc = 1;
                    for (int k = 0; k < cnt; k++) {
                        size_t c = 0;
                        if (!mako_re_atom_once(pat + pi, alen, text + pos, tlen - pos, &c) || c == 0) {
                            okc = 0;
                            break;
                        }
                        pos += c;
                    }
                    if (okc && mako_re_piece(rest, rlen, text + pos, tlen - pos)) return 1;
                }
                return 0;
            }
        }
        if (pi + alen < plen && (pat[pi + alen] == '*' || pat[pi + alen] == '+' || pat[pi + alen] == '?')) {
            char q = pat[pi + alen];
            const char *rest = pat + pi + alen + 1;
            size_t rlen = plen - pi - alen - 1;
            if (q == '*') return mako_re_star_atom(pat + pi, alen, rest, rlen, text + ti, tlen - ti);
            if (q == '+') return mako_re_plus_atom(pat + pi, alen, rest, rlen, text + ti, tlen - ti);
            return mako_re_opt_atom(pat + pi, alen, rest, rlen, text + ti, tlen - ti);
        }
        if (!matched) return 0;
        pi += alen;
        ti += consumed;
    }
    return mako_re_allow_prefix ? 1 : (ti == tlen);
}

static inline int mako_re_here(const char *pat, size_t plen, const char *text, size_t tlen) {
    /* Split on top-level `|` (not inside `[]` or `(…)`). */
    size_t start = 0;
    size_t depth = 0;
    for (size_t i = 0; i < plen; i++) {
        if (pat[i] == '[') {
            i++;
            while (i < plen && pat[i] != ']') i++;
            continue;
        }
        if (pat[i] == '(') { depth++; continue; }
        if (pat[i] == ')' && depth > 0) { depth--; continue; }
        if (pat[i] == '|' && depth == 0) {
            int saved = mako_re_group_seq;
            if (mako_re_capture_on && mako_re_alt_number) {
                mako_re_group_seq = 1 + mako_re_count_opens(pat, start);
            }
            if (mako_re_piece(pat + start, i - start, text, tlen)) return 1;
            mako_re_group_seq = saved;
            start = i + 1;
        }
    }
    {
        int saved = mako_re_group_seq;
        if (mako_re_capture_on && mako_re_alt_number) {
            mako_re_group_seq = 1 + mako_re_count_opens(pat, start);
        }
        int ok = mako_re_piece(pat + start, plen - start, text, tlen);
        if (!ok) mako_re_group_seq = saved;
        return ok;
    }
}

static inline bool mako_regex_match(MakoString pat, MakoString text) {
    const char *p = pat.data ? pat.data : "";
    size_t plen = pat.len;
    const char *t = text.data ? text.data : "";
    size_t tlen = text.len;
    /* Enable captures so backrefs (\1..\9) work during match. */
    int saved_cap = mako_re_capture_on;
    const char *saved_base = mako_re_text_base;
    mako_re_caps_clear();
    mako_re_capture_on = 1;
    mako_re_text_base = t;
    if (plen > 0 && p[0] == '^') {
        p++;
        plen--;
    }
    int has_dollar = plen > 0 && p[plen - 1] == '$';
    int ok_any = 0;
    if (has_dollar) {
        ok_any = mako_re_here(p, plen, t, tlen) != 0;
    } else {
        /* Append `$` to each top-level alternative so `a|b` means `a$|b$`. */
        size_t start = 0;
        size_t depth = 0;
        for (size_t i = 0; i <= plen; i++) {
            if (i < plen && p[i] == '[') {
                i++;
                while (i < plen && p[i] != ']') i++;
                continue;
            }
            if (i < plen && p[i] == '(') { depth++; continue; }
            if (i < plen && p[i] == ')' && depth > 0) { depth--; continue; }
            if (i == plen || (p[i] == '|' && depth == 0)) {
                size_t n = i - start;
                char *buf = (char *)malloc(n + 2);
                memcpy(buf, p + start, n);
                buf[n] = '$';
                mako_re_caps_clear();
                mako_re_group_seq = 1;
                int ok = mako_re_piece(buf, n + 1, t, tlen);
                free(buf);
                if (ok) { ok_any = 1; break; }
                start = i + 1;
            }
        }
    }
    if (ok_any) {
        mako_re_cap_off[0] = 0;
        mako_re_cap_len[0] = tlen;
        mako_re_cap_set[0] = 1;
    }
    mako_re_capture_on = saved_cap;
    mako_re_text_base = saved_base;
    return ok_any != 0;
}

static inline MakoString mako_regex_find(MakoString pat, MakoString text) {
    if (!pat.data || pat.len == 0) return mako_str_from_cstr("");
    const char *p = pat.data;
    size_t plen = pat.len;
    const char *t = text.data ? text.data : "";
    size_t tlen = text.len;
    int from_start = 0;
    if (plen > 0 && p[0] == '^') {
        from_start = 1;
        p++;
        plen--;
    }
    int has_dollar = plen > 0 && p[plen - 1] == '$';
    size_t body = has_dollar ? plen - 1 : plen;
    for (size_t i = 0; i <= tlen; i++) {
        if (from_start && i != 0) break;
        if (has_dollar) {
            if (mako_re_here(p, plen, t + i, tlen - i)) {
                size_t n = tlen - i;
                char *d = (char *)malloc(n + 1);
                memcpy(d, t + i, n);
                d[n] = 0;
                return (MakoString){d, n};
            }
            continue;
        }
        /* Prefix match against full remaining suffix so \b sees real neighbors. */
        mako_re_allow_prefix = 1;
        mako_re_text_base = t; /* absolute base for diagnostics; bounds use local text */
        /* Try longest-first by scanning n; evaluate pattern+$ against window BUT
         * for trailing word-boundary, check char after window in full string. */
        for (size_t n = tlen - i + 1; n-- > 0; ) {
            char *buf = (char *)malloc(body + 2);
            memcpy(buf, p, body);
            buf[body] = '$';
            /* Match against exact window of length n */
            mako_re_allow_prefix = 0;
            int ok = mako_re_here(buf, body + 1, t + i, n);
            free(buf);
            if (!ok) continue;
            /* If pattern contains \b/\B, verify boundaries in full-string context.
             * Re-check: run prefix match on full suffix and require consume==n. */
            mako_re_allow_prefix = 1;
            /* Simulate: match body as prefix of t+i .. tlen; measure by finding
             * if body+$ matches only when we also check next char for \b. */
            /* Extra check for false \b at artificial end: if next char in full
             * string is a word char and pattern ends with \b, reject. */
            int ends_b = 0, ends_B = 0;
            if (body >= 2 && p[body - 2] == '\\' && p[body - 1] == 'b') ends_b = 1;
            if (body >= 2 && p[body - 2] == '\\' && p[body - 1] == 'B') ends_B = 1;
            if (ends_b || ends_B) {
                int left = (n > 0) && mako_re_is_word((unsigned char)t[i + n - 1]);
                int right = (i + n < tlen) && mako_re_is_word((unsigned char)t[i + n]);
                int bound = left != right;
                if (ends_b && !bound) continue;
                if (ends_B && bound) continue;
            }
            /* leading \b similarly against full string */
            int starts_b = (body >= 2 && p[0] == '\\' && p[1] == 'b');
            int starts_B = (body >= 2 && p[0] == '\\' && p[1] == 'B');
            if (starts_b || starts_B) {
                int left = (i > 0) && mako_re_is_word((unsigned char)t[i - 1]);
                int right = (i < tlen) && mako_re_is_word((unsigned char)t[i]);
                int bound = left != right;
                if (starts_b && !bound) continue;
                if (starts_B && bound) continue;
            }
            char *d = (char *)malloc(n + 1);
            memcpy(d, t + i, n);
            d[n] = 0;
            mako_re_allow_prefix = 0;
            return (MakoString){d, n};
        }
        mako_re_allow_prefix = 0;
    }
    mako_re_allow_prefix = 0;
    return mako_str_from_cstr("");
}


static inline int mako_regex_match_caps(MakoString pat, MakoString text) {
    mako_re_caps_clear();
    mako_re_capture_on = 1;
    mako_re_alt_number = 1;
    const char *t = text.data ? text.data : "";
    mako_re_text_base = t;
    int ok = mako_regex_match(pat, text) ? 1 : 0;
    if (ok) {
        mako_re_cap_off[0] = 0;
        mako_re_cap_len[0] = text.len;
        mako_re_cap_set[0] = 1;
    }
    mako_re_capture_on = 0;
    mako_re_alt_number = 0;
    mako_re_text_base = NULL;
    return ok;
}

/* Leftmost-longest window with captures. Prefer full-string match first. */
static inline int mako_regex_find_caps(MakoString pat, MakoString text) {
    if (!pat.data) return 0;
    /* Prefer whole-string match when possible (stable captures). */
    if (mako_regex_match_caps(pat, text)) return 1;

    const char *p0 = pat.data;
    size_t plen0 = pat.len;
    const char *t = text.data ? text.data : "";
    size_t tlen = text.len;
    const char *p = p0;
    size_t plen = plen0;
    int from_start = 0;
    if (plen > 0 && p[0] == '^') {
        from_start = 1;
        p++;
        plen--;
    }
    int has_dollar = plen > 0 && p[plen - 1] == '$';
    size_t body = has_dollar ? plen - 1 : plen;

    for (size_t i = 0; i <= tlen; i++) {
        if (from_start && i != 0) break;
        /* longest first */
        for (size_t n = tlen - i + 1; n-- > 0; ) {
            if (has_dollar && n != tlen - i) continue;
            char *pbuf = (char *)malloc(body + 2);
            memcpy(pbuf, p, body);
            pbuf[body] = '$';
            mako_re_caps_clear();
            mako_re_capture_on = 1;
            mako_re_alt_number = 1;
            mako_re_text_base = t + i;
            int ok = mako_re_here(pbuf, body + 1, t + i, n);
            mako_re_capture_on = 0;
            mako_re_alt_number = 0;
            mako_re_text_base = NULL;
            free(pbuf);
            if (ok) {
                mako_re_cap_off[0] = i;
                mako_re_cap_len[0] = n;
                mako_re_cap_set[0] = 1;
                for (int g = 1; g < MAKO_RE_MAX_CAPS; g++) {
                    if (mako_re_cap_set[g]) mako_re_cap_off[g] += i;
                }
                return 1;
            }
            if (has_dollar) break;
        }
    }
    return 0;
}

static inline MakoString mako_regex_capture(MakoString pat, MakoString text, int64_t n) {
    if (n < 0 || n >= MAKO_RE_MAX_CAPS) return mako_str_from_cstr("");
    if (!mako_regex_find_caps(pat, text)) return mako_str_from_cstr("");
    int idx = (int)n;
    if (!mako_re_cap_set[idx]) return mako_str_from_cstr("");
    size_t off = mako_re_cap_off[idx];
    size_t len = mako_re_cap_len[idx];
    const char *t = text.data ? text.data : "";
    if (off + len > text.len) return mako_str_from_cstr("");
    char *d = (char *)malloc(len + 1);
    memcpy(d, t + off, len);
    d[len] = 0;
    return (MakoString){d, len};
}

static inline int64_t mako_remove_file(MakoString path) {
    const char *p = path.data ? path.data : "";
#if defined(_WIN32)
    return _unlink(p) == 0 ? 0 : -1;
#else
    return unlink(p) == 0 ? 0 : -1;
#endif
}

static inline MakoString mako_read_file(MakoString path) {
    const char *p = path.data ? path.data : "";
    FILE *f = fopen(p, "rb");
    if (!f) {
        return mako_str_from_cstr("");
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return mako_str_from_cstr("");
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return mako_str_from_cstr("");
    }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        mako_abort("read_file OOM");
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    MakoString s = {buf, n};
    return s;
}

static inline int64_t mako_write_file(MakoString path, MakoString contents) {
    const char *p = path.data ? path.data : "";
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    size_t n = contents.len;
    size_t w = n ? fwrite(contents.data, 1, n, f) : 0;
    fclose(f);
    return (w == n) ? 0 : -1;
}

/* Append bytes (create if missing). Used by append-only logs / mini engines. */
static inline int64_t mako_append_file(MakoString path, MakoString contents) {
    const char *p = path.data ? path.data : "";
    FILE *f = fopen(p, "ab");
    if (!f) return -1;
    size_t n = contents.len;
    size_t w = n ? fwrite(contents.data, 1, n, f) : 0;
    fclose(f);
    return (w == n) ? 0 : -1;
}

static inline MakoString mako_env_get(MakoString key) {
    const char *k = key.data ? key.data : "";
    const char *v = getenv(k);
    return mako_str_from_cstr(v ? v : "");
}

static inline int64_t mako_env_set(MakoString key, MakoString val) {
    const char *k = key.data ? key.data : "";
    const char *v = val.data ? val.data : "";
#if defined(__wasi__)
    /* WASI preview1: no setenv in wasi-libc — soft-fail. */
    (void)k;
    (void)v;
    return -1;
#else
    return mako_setenv(k, v);
#endif
}

static inline int64_t mako_now_ms(void) {
    struct timeval tv;
    mako_gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
}

/* Monotonic nanoseconds for microbenches (CLOCK_MONOTONIC). */
static inline int64_t mako_now_ns(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    }
#endif
    struct timeval tv;
    mako_gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000000LL + (int64_t)tv.tv_usec * 1000LL;
}

/* Prevent LTO/DCE from erasing bench work (Rust black_box equivalent). */
static inline int64_t mako_black_box_i64(int64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : "+r"(x));
#else
    volatile int64_t y = x;
    x = y;
#endif
    return x;
}

static inline MakoString mako_path_join(MakoString a, MakoString b) {
    size_t al = a.len;
    size_t bl = b.len;
    int need_sep = 1;
    if (al == 0) {
        return mako_str_from_cstr(b.data ? b.data : "");
    }
    if (bl == 0) {
        return mako_str_from_cstr(a.data ? a.data : "");
    }
    if (a.data[al - 1] == '/') need_sep = 0;
    if (b.data[0] == '/') need_sep = 0;
    size_t n = al + bl + (need_sep ? 1 : 0);
    char *d = (char *)malloc(n + 1);
    if (!d) mako_abort("path_join OOM");
    memcpy(d, a.data, al);
    size_t o = al;
    if (need_sep) d[o++] = '/';
    memcpy(d + o, b.data, bl);
    d[n] = '\0';
    MakoString s = {d, n};
    return s;
}

static inline void mako_exit(int64_t code) {
    exit((int)code);
}

/* mako_sleep_ms — defined in mako_platform.h (Unix nanosleep / Windows Sleep) */

/* ---- Actors (mailbox = owned channel; state lives in the actor loop) ---- */
typedef MakoChan MakoActor;

static inline MakoActor *mako_actor_spawn(int64_t mailbox_cap) {
    return mako_chan_new(mailbox_cap < 1 ? 8 : mailbox_cap);
}

static inline int64_t mako_actor_send(MakoActor *a, int64_t msg) {
    return mako_chan_send(a, msg);
}

static inline int64_t mako_actor_recv(MakoActor *a) {
    return mako_chan_recv(a);
}

static inline void mako_actor_stop(MakoActor *a) {
    mako_chan_close(a);
}

/* ---- Go-like testing (setjmp so one failure doesn't abort the process) ---- */
#if defined(__wasi__)
/* wasi-libc setjmp needs Wasm EH; soft stubs for wasm32-wasip1 hello builds. */
typedef int jmp_buf[1];
static inline int setjmp(jmp_buf env) {
    (void)env;
    return 0;
}
static inline void longjmp(jmp_buf env, int val) {
    (void)env;
    (void)val;
    abort();
}
#else
#include <setjmp.h>
#endif

static jmp_buf mako_test_jmp;
static volatile int mako_test_failed = 0;
static const char *mako_test_fail_msg = NULL;
static const char *mako_test_current = NULL;
static char mako_test_sub[128];
static int mako_test_sub_failed = 0;
static int mako_test_had_sub = 0;

static inline void mako_test_begin(const char *name) {
    mako_test_failed = 0;
    mako_test_fail_msg = NULL;
    mako_test_current = name;
    mako_test_sub[0] = 0;
    mako_test_sub_failed = 0;
    mako_test_had_sub = 0;
}

static inline void mako_t_run_finish_prev(void) {
    if (!mako_test_had_sub || mako_test_sub[0] == 0) return;
    if (mako_test_sub_failed) {
        /* already printed FAIL for this sub */
    } else {
        printf("--- PASS: %s/%s\n", mako_test_current ? mako_test_current : "?", mako_test_sub);
        fflush(stdout);
    }
    mako_test_sub_failed = 0;
}

/* Nested subtests: push current name, start child. Finish with t_run_pop. */
static inline void mako_t_run_push(MakoString name) {
    /* Finish previous sibling at this nesting level first */
    mako_t_run_finish_prev();
    /* Encode nesting as Parent/Child in mako_test_sub */
    char parent[128];
    size_t plen = 0;
    if (mako_test_had_sub && mako_test_sub[0]) {
        plen = strlen(mako_test_sub);
        if (plen >= sizeof(parent)) plen = sizeof(parent) - 1;
        memcpy(parent, mako_test_sub, plen);
        parent[plen] = 0;
    } else {
        parent[0] = 0;
    }
    size_t n = name.len;
    char child[64];
    if (n >= sizeof(child)) n = sizeof(child) - 1;
    if (name.data && n > 0) {
        memcpy(child, name.data, n);
        child[n] = 0;
    } else {
        child[0] = 0;
    }
    if (parent[0]) {
        snprintf(mako_test_sub, sizeof(mako_test_sub), "%s/%s", parent, child);
    } else {
        snprintf(mako_test_sub, sizeof(mako_test_sub), "%s", child);
    }
    mako_test_had_sub = 1;
    mako_test_sub_failed = 0;
}

static inline void mako_t_run(MakoString name) {
    mako_t_run_finish_prev();
    size_t n = name.len < sizeof(mako_test_sub) - 1 ? name.len : sizeof(mako_test_sub) - 1;
    if (name.data && n > 0) {
        memcpy(mako_test_sub, name.data, n);
        mako_test_sub[n] = 0;
    } else {
        mako_test_sub[0] = 0;
    }
    mako_test_had_sub = 1;
    mako_test_sub_failed = 0;
}

/* Nested: t_run_nested("child") appends under current sub path */
static inline void mako_t_run_nested(MakoString name) {
    mako_t_run_push(name);
}

static inline void mako_fail(MakoString msg) {
    mako_test_failed = 1;
    mako_test_fail_msg = msg.data ? msg.data : "(fail)";
    if (mako_test_had_sub && mako_test_sub[0]) {
        mako_test_sub_failed = 1;
        fprintf(
            stderr,
            "--- FAIL: %s/%s\n    %s\n",
            mako_test_current ? mako_test_current : "?",
            mako_test_sub,
            mako_test_fail_msg
        );
        fflush(stderr);
        return; /* continue parent TestXxx (other subtests) */
    }
    longjmp(mako_test_jmp, 1);
}

static inline void mako_assert(int64_t cond) {
    if (!cond) {
        mako_fail(mako_str_from_cstr("assert failed"));
    }
}

static inline void mako_assert_eq(int64_t a, int64_t b) {
    if (a != b) {
        char buf[160];
        snprintf(buf, sizeof(buf), "assert_eq failed: got %lld, want %lld",
                 (long long)a, (long long)b);
        mako_fail(mako_str_from_cstr(buf));
    }
}

static inline void mako_assert_eq_at(const char *file, int line, int64_t a, int64_t b) {
    if (a != b) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "assert_eq failed at %s:%d: got %lld, want %lld",
                 file ? file : "?", line, (long long)a, (long long)b);
        mako_fail(mako_str_from_cstr(buf));
    }
}

static inline void mako_assert_eq_str(MakoString a, MakoString b) {
    if (!mako_str_eq(a, b)) {
        char buf[320];
        snprintf(
            buf,
            sizeof(buf),
            "assert_eq_str failed: got \"%.*s\", want \"%.*s\"",
            (int)a.len,
            a.data ? a.data : "",
            (int)b.len,
            b.data ? b.data : ""
        );
        mako_fail(mako_str_from_cstr(buf));
    }
}

/* Run one TestXxx; returns 0 on pass, 1 on fail. Continues to next test after fail. */
/* On a native fault (SIGABRT/SIGSEGV/SIGBUS/SIGFPE) inside a test, report which
 * test was running and a backtrace before dying, so a crash points at a place
 * instead of just "killed by signal N". */
static void mako_test_crash_handler(int sig) {
    const char *nm = mako_test_current ? mako_test_current : "(none)";
    const char *sn =
        sig == SIGSEGV ? "SIGSEGV" :
        sig == SIGABRT ? "SIGABRT" :
        sig == SIGFPE  ? "SIGFPE"  :
#if defined(SIGBUS)
        sig == SIGBUS  ? "SIGBUS"  :
#endif
        "signal";
    fprintf(stderr, "\n--- CRASH: %s during test '%s'\n", sn, nm);
#if defined(__GLIBC__) || defined(__APPLE__)
    void *frames[32];
    int n = backtrace(frames, 32);
    backtrace_symbols_fd(frames, n, 2 /* stderr */);
#endif
    fflush(stderr);
    signal(sig, SIG_DFL);
    raise(sig);
}

static inline void mako_test_install_crash_handler(void) {
#if defined(_WIN32)
    signal(SIGSEGV, mako_test_crash_handler);
    signal(SIGABRT, mako_test_crash_handler);
    signal(SIGFPE, mako_test_crash_handler);
#else
    /* Run the handler on a dedicated stack (SA_ONSTACK) so even a
     * stack-overflow SIGSEGV — which leaves no room on the normal stack — can
     * still report which test crashed. */
    static char alt_stack[64 * 1024];
    stack_t ss;
    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof(alt_stack);
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = mako_test_crash_handler;
    sa.sa_flags = SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
#endif
}

static inline int mako_test_run(const char *name, void (*fn)(void)) {
    mako_test_begin(name);
    if (setjmp(mako_test_jmp) == 0) {
        fn();
        mako_t_run_finish_prev();
        if (mako_test_failed) {
            fprintf(stderr, "--- FAIL: %s\n", name);
            fflush(stderr);
            return 1;
        }
        printf("--- PASS: %s\n", name);
        fflush(stdout);
        return 0;
    }
    fprintf(
        stderr,
        "--- FAIL: %s\n    %s\n",
        name,
        mako_test_fail_msg ? mako_test_fail_msg : "failed"
    );
    fflush(stderr);
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_RT_H */
