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
#include <limits.h>
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
#include <sys/resource.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Branch hints for hot paths (no-ops on unknown compilers). */
#if defined(__GNUC__) || defined(__clang__)
#define MAKO_LIKELY(x) __builtin_expect(!!(x), 1)
#define MAKO_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define MAKO_LIKELY(x) (x)
#define MAKO_UNLIKELY(x) (x)
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
/* Lock / cond wait contention (channel mutex waits). */
static atomic_llong mako_rt_lock_waits = 0;
static atomic_llong mako_rt_lock_wait_ns = 0;

/* Time helpers defined later — needed by channel wait instrumentation. */
static inline int64_t mako_now_ms(void);
static inline int64_t mako_now_ns(void);

static inline void mako_rt_counter_inc(atomic_llong *counter) {
    atomic_fetch_add_explicit(counter, 1, memory_order_relaxed);
}

static inline void mako_rt_note_lock_wait(int64_t wait_ns) {
    if (wait_ns < 0) wait_ns = 0;
    atomic_fetch_add_explicit(&mako_rt_lock_waits, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&mako_rt_lock_wait_ns, wait_ns, memory_order_relaxed);
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
    atomic_store_explicit(&mako_rt_lock_waits, 0, memory_order_relaxed);
    atomic_store_explicit(&mako_rt_lock_wait_ns, 0, memory_order_relaxed);
}

/* ---- First-class function values (fat pointer: code + optional env) ----
 * Non-capturing: env == NULL, fn is ret (*)(args...).
 * Capturing: env is heap env struct, fn is ret (*)(void *env, args...).
 * drop_env frees the env (and nested string clones) when present.
 */
typedef struct {
    void *fn;
    void *env;
    void (*drop_env)(void *);
} MakoFn;

static inline MakoFn mako_fn_bare(void *fn) {
    MakoFn f;
    f.fn = fn;
    f.env = NULL;
    f.drop_env = NULL;
    return f;
}

static inline MakoFn mako_fn_closure(void *fn, void *env, void (*drop_env)(void *)) {
    MakoFn f;
    f.fn = fn;
    f.env = env;
    f.drop_env = drop_env;
    return f;
}

/* Free capture env (if any). Safe on bare fns. After drop, f is bare. */
static inline void mako_fn_drop(MakoFn *f) {
    if (!f) return;
    if (f->env) {
        if (f->drop_env) {
            f->drop_env(f->env);
        } else {
            free(f->env);
        }
        f->env = NULL;
    }
    f->drop_env = NULL;
    /* keep f->fn so a dropped-but-bare call path still works if env was null */
}

static inline int64_t mako_fn_has_env(MakoFn f) {
    return f.env != NULL ? 1 : 0;
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

/* Process-wide empty string — never free this buffer (see mako_str_free). */
static char mako_str_empty_byte = 0;
static const MakoString mako_str_empty = {&mako_str_empty_byte, 0};

static inline int mako_str_is_empty_singleton(MakoString s) {
    return s.data == &mako_str_empty_byte;
}

/* Free an owned string; no-op for the empty singleton. */
static inline void mako_str_free(MakoString s) {
    if (s.data && s.data != &mako_str_empty_byte) free(s.data);
}

/* Create an owned MakoString by copying a C string. NULL/empty → shared empty. */
static inline MakoString mako_str_from_cstr(const char *s) {
    if (!s || !s[0]) {
        return mako_str_empty;
    }
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
    char *d = (char *)malloc(512);
    if (!d) {
        fprintf(stderr, "mako: OOM in runtime_stats_json\n");
        abort();
    }
    int n = snprintf(
        d,
        512,
        "{\"tasks_spawned\":%lld,\"tasks_joined\":%lld,\"channels_created\":%lld,"
        "\"channel_sends\":%lld,\"channel_try_send_drops\":%lld,\"channel_recvs\":%lld,"
        "\"channel_select_timeouts\":%lld,\"channel_peak_depth\":%lld,"
        "\"lock_waits\":%lld,\"lock_wait_ns\":%lld}",
        atomic_load_explicit(&mako_rt_tasks_spawned, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_tasks_joined, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channels_created, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channel_sends, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channel_try_send_drops, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channel_recvs, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channel_select_timeouts, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channel_peak_depth, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_lock_waits, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_lock_wait_ns, memory_order_relaxed)
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
    /* Empty fast paths without calling mako_str_clone (defined later). */
    if (MAKO_UNLIKELY(a.len == 0 && b.len == 0)) return mako_str_empty;
    if (MAKO_UNLIKELY(a.len == 0)) {
        char *d = (char *)malloc(b.len + 1);
        if (MAKO_UNLIKELY(!d)) abort();
        memcpy(d, b.data, b.len);
        d[b.len] = 0;
        return (MakoString){d, b.len};
    }
    if (MAKO_UNLIKELY(b.len == 0)) {
        char *d = (char *)malloc(a.len + 1);
        if (MAKO_UNLIKELY(!d)) abort();
        memcpy(d, a.data, a.len);
        d[a.len] = 0;
        return (MakoString){d, a.len};
    }
    char *d = (char *)malloc(a.len + b.len + 1);
    if (MAKO_UNLIKELY(!d)) {
        fprintf(stderr, "mako: OOM in str_concat\n");
        abort();
    }
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


/* ---- Go-like []bool ---- */
typedef struct {
    bool *data;
    size_t len;
    size_t cap;
} MakoBoolArray;

static inline MakoBoolArray mako_bool_array_new(size_t n) {
    MakoBoolArray a;
    a.data = (bool *)calloc(n ? n : 1, sizeof(bool));
    a.len = n;
    a.cap = n ? n : 1;
    return a;
}

static inline MakoBoolArray mako_bool_array_of(const bool *vals, size_t n) {
    MakoBoolArray a = mako_bool_array_new(n);
    if (n) memcpy(a.data, vals, n * sizeof(bool));
    return a;
}

static inline MakoBoolArray mako_bool_array_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoBoolArray a;
    a.data = (bool *)calloc((size_t)(cap ? cap : 1), sizeof(bool));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}

static inline int64_t mako_bool_array_len(MakoBoolArray a) { return (int64_t)a.len; }
static inline int64_t mako_bool_array_cap(MakoBoolArray a) { return (int64_t)a.cap; }

static inline bool mako_bool_array_get(MakoBoolArray a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("bool slice index out of bounds");
    return a.data[i];
}

static inline void mako_bool_array_set(MakoBoolArray a, int64_t i, bool v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("bool slice index out of bounds");
    a.data[i] = v;
}

static inline MakoBoolArray mako_bool_array_append(MakoBoolArray s, bool v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        bool *nd = (bool *)malloc(ncap * sizeof(bool));
        if (!nd) mako_abort("append: out of memory");
        if (s.len) memcpy(nd, s.data, s.len * sizeof(bool));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}

static inline MakoBoolArray mako_bool_array_slice_expr(
    MakoBoolArray s, int64_t low, int64_t high, int64_t max, int has_max
) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoBoolArray out;
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

static inline int64_t mako_bool_array_copy(MakoBoolArray dst, MakoBoolArray src) {
    size_t n = dst.len < src.len ? dst.len : src.len;
    if (n) memmove(dst.data, src.data, n * sizeof(bool));
    return (int64_t)n;
}

/* [](MakoIntArray) — nested slice */
typedef struct {
    MakoIntArray *data;
    size_t len;
    size_t cap;
} MakoArr_arr_int;
static inline MakoArr_arr_int mako_arr_arr_int_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_int a;
    a.data = (MakoIntArray *)calloc((size_t)(cap ? cap : 1), sizeof(MakoIntArray));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline int64_t mako_arr_arr_int_len(MakoArr_arr_int a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_int_cap(MakoArr_arr_int a) { return (int64_t)a.cap; }
static inline MakoIntArray mako_arr_arr_int_get(MakoArr_arr_int a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_int_set(MakoArr_arr_int a, int64_t i, MakoIntArray v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    a.data[i] = v;
}
static inline MakoArr_arr_int mako_arr_arr_int_append(MakoArr_arr_int s, MakoIntArray v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoIntArray *nd = (MakoIntArray *)realloc(s.data, ncap * sizeof(MakoIntArray));
        if (!nd) mako_abort("append: out of memory");
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_arr_int mako_arr_arr_int_of(const MakoIntArray *vals, size_t n) {
    MakoArr_arr_int a = mako_arr_arr_int_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoIntArray));
    return a;
}
static inline MakoArr_arr_int mako_arr_arr_int_slice_expr(MakoArr_arr_int s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_int out;
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
static inline int64_t mako_arr_arr_int_copy(MakoArr_arr_int dst, MakoArr_arr_int src) {
    size_t n = dst.len < src.len ? dst.len : src.len;
    if (n) memmove(dst.data, src.data, n * sizeof(MakoIntArray));
    return (int64_t)n;
}

/* [](MakoStrArray) — nested slice */
typedef struct {
    MakoStrArray *data;
    size_t len;
    size_t cap;
} MakoArr_arr_string;
static inline MakoArr_arr_string mako_arr_arr_string_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_string a;
    a.data = (MakoStrArray *)calloc((size_t)(cap ? cap : 1), sizeof(MakoStrArray));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline int64_t mako_arr_arr_string_len(MakoArr_arr_string a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_string_cap(MakoArr_arr_string a) { return (int64_t)a.cap; }
static inline MakoStrArray mako_arr_arr_string_get(MakoArr_arr_string a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_string_set(MakoArr_arr_string a, int64_t i, MakoStrArray v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    a.data[i] = v;
}
static inline MakoArr_arr_string mako_arr_arr_string_append(MakoArr_arr_string s, MakoStrArray v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoStrArray *nd = (MakoStrArray *)realloc(s.data, ncap * sizeof(MakoStrArray));
        if (!nd) mako_abort("append: out of memory");
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_arr_string mako_arr_arr_string_of(const MakoStrArray *vals, size_t n) {
    MakoArr_arr_string a = mako_arr_arr_string_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoStrArray));
    return a;
}
static inline MakoArr_arr_string mako_arr_arr_string_slice_expr(MakoArr_arr_string s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_string out;
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
static inline int64_t mako_arr_arr_string_copy(MakoArr_arr_string dst, MakoArr_arr_string src) {
    size_t n = dst.len < src.len ? dst.len : src.len;
    if (n) memmove(dst.data, src.data, n * sizeof(MakoStrArray));
    return (int64_t)n;
}

/* [](MakoFloatArray) — nested slice */
typedef struct {
    MakoFloatArray *data;
    size_t len;
    size_t cap;
} MakoArr_arr_float;
static inline MakoArr_arr_float mako_arr_arr_float_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_float a;
    a.data = (MakoFloatArray *)calloc((size_t)(cap ? cap : 1), sizeof(MakoFloatArray));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline int64_t mako_arr_arr_float_len(MakoArr_arr_float a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_float_cap(MakoArr_arr_float a) { return (int64_t)a.cap; }
static inline MakoFloatArray mako_arr_arr_float_get(MakoArr_arr_float a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_float_set(MakoArr_arr_float a, int64_t i, MakoFloatArray v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    a.data[i] = v;
}
static inline MakoArr_arr_float mako_arr_arr_float_append(MakoArr_arr_float s, MakoFloatArray v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoFloatArray *nd = (MakoFloatArray *)realloc(s.data, ncap * sizeof(MakoFloatArray));
        if (!nd) mako_abort("append: out of memory");
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_arr_float mako_arr_arr_float_of(const MakoFloatArray *vals, size_t n) {
    MakoArr_arr_float a = mako_arr_arr_float_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoFloatArray));
    return a;
}
static inline MakoArr_arr_float mako_arr_arr_float_slice_expr(MakoArr_arr_float s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_float out;
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
static inline int64_t mako_arr_arr_float_copy(MakoArr_arr_float dst, MakoArr_arr_float src) {
    size_t n = dst.len < src.len ? dst.len : src.len;
    if (n) memmove(dst.data, src.data, n * sizeof(MakoFloatArray));
    return (int64_t)n;
}

/* [](MakoBoolArray) — nested slice */
typedef struct {
    MakoBoolArray *data;
    size_t len;
    size_t cap;
} MakoArr_arr_bool;
static inline MakoArr_arr_bool mako_arr_arr_bool_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_bool a;
    a.data = (MakoBoolArray *)calloc((size_t)(cap ? cap : 1), sizeof(MakoBoolArray));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline int64_t mako_arr_arr_bool_len(MakoArr_arr_bool a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_bool_cap(MakoArr_arr_bool a) { return (int64_t)a.cap; }
static inline MakoBoolArray mako_arr_arr_bool_get(MakoArr_arr_bool a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_bool_set(MakoArr_arr_bool a, int64_t i, MakoBoolArray v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    a.data[i] = v;
}
static inline MakoArr_arr_bool mako_arr_arr_bool_append(MakoArr_arr_bool s, MakoBoolArray v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoBoolArray *nd = (MakoBoolArray *)realloc(s.data, ncap * sizeof(MakoBoolArray));
        if (!nd) mako_abort("append: out of memory");
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_arr_bool mako_arr_arr_bool_of(const MakoBoolArray *vals, size_t n) {
    MakoArr_arr_bool a = mako_arr_arr_bool_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoBoolArray));
    return a;
}
static inline MakoArr_arr_bool mako_arr_arr_bool_slice_expr(MakoArr_arr_bool s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_bool out;
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
static inline int64_t mako_arr_arr_bool_copy(MakoArr_arr_bool dst, MakoArr_arr_bool src) {
    size_t n = dst.len < src.len ? dst.len : src.len;
    if (n) memmove(dst.data, src.data, n * sizeof(MakoBoolArray));
    return (int64_t)n;
}

/* [](MakoByteArray) — nested slice */
typedef struct {
    MakoByteArray *data;
    size_t len;
    size_t cap;
} MakoArr_arr_byte;
static inline MakoArr_arr_byte mako_arr_arr_byte_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_byte a;
    a.data = (MakoByteArray *)calloc((size_t)(cap ? cap : 1), sizeof(MakoByteArray));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline int64_t mako_arr_arr_byte_len(MakoArr_arr_byte a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_byte_cap(MakoArr_arr_byte a) { return (int64_t)a.cap; }
static inline MakoByteArray mako_arr_arr_byte_get(MakoArr_arr_byte a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_byte_set(MakoArr_arr_byte a, int64_t i, MakoByteArray v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    a.data[i] = v;
}
static inline MakoArr_arr_byte mako_arr_arr_byte_append(MakoArr_arr_byte s, MakoByteArray v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoByteArray *nd = (MakoByteArray *)realloc(s.data, ncap * sizeof(MakoByteArray));
        if (!nd) mako_abort("append: out of memory");
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_arr_byte mako_arr_arr_byte_of(const MakoByteArray *vals, size_t n) {
    MakoArr_arr_byte a = mako_arr_arr_byte_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoByteArray));
    return a;
}
static inline MakoArr_arr_byte mako_arr_arr_byte_slice_expr(MakoArr_arr_byte s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_byte out;
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
static inline int64_t mako_arr_arr_byte_copy(MakoArr_arr_byte dst, MakoArr_arr_byte src) {
    size_t n = dst.len < src.len ? dst.len : src.len;
    if (n) memmove(dst.data, src.data, n * sizeof(MakoByteArray));
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

/* ---- Option[T] (same payload slots as Result Ok: int / string / float / ptr) ---- */
typedef struct {
    bool some;
    int64_t value;   /* int family, or pointer bits for boxed / map payloads */
    MakoString ok_s; /* Option[string] */
    double ok_f;     /* Option[float] */
} MakoOptionInt;

static inline MakoOptionInt mako_some_int(int64_t v) {
    MakoOptionInt o;
    memset(&o, 0, sizeof(o));
    o.some = true;
    o.value = v;
    return o;
}

static inline MakoOptionInt mako_some_str(MakoString s) {
    MakoOptionInt o;
    memset(&o, 0, sizeof(o));
    o.some = true;
    o.ok_s = s;
    return o;
}

static inline MakoOptionInt mako_some_float_opt(double v) {
    MakoOptionInt o;
    memset(&o, 0, sizeof(o));
    o.some = true;
    o.ok_f = v;
    return o;
}

static inline MakoOptionInt mako_some_ptr(void *p) {
    MakoOptionInt o;
    memset(&o, 0, sizeof(o));
    o.some = true;
    o.value = (int64_t)(intptr_t)p;
    return o;
}

static inline void *mako_option_some_ptr(MakoOptionInt o) {
    return o.some ? (void *)(intptr_t)o.value : NULL;
}

static inline MakoOptionInt mako_none_int(void) {
    MakoOptionInt o;
    memset(&o, 0, sizeof(o));
    o.some = false;
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

/* ---- String regions (core language; no malloc) ----
 * Compare/search within s[off .. off+len) without allocating a substring.
 * Bounds-safe: out-of-range returns 0 / -1. Use for any text scan that
 * would otherwise allocate s[i:j] only to compare or search.
 */

static inline int mako_str_ci_eq_bytes(const char *a, size_t al, const char *b, size_t bl) {
    if (al != bl) return 0;
    for (size_t i = 0; i < al; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return 1;
}

/* Case-sensitive equality of s[off:off+len] vs other. */
static inline int64_t mako_str_slice_eq(MakoString s, int64_t off, int64_t len, MakoString other) {
    if (!s.data || off < 0 || len < 0) return 0;
    if ((size_t)off + (size_t)len > s.len) return 0;
    size_t ol = other.data ? other.len : 0;
    if ((size_t)len != ol) return 0;
    if (len == 0) return 1;
    if (!other.data) return 0;
    return memcmp(s.data + (size_t)off, other.data, (size_t)len) == 0 ? 1 : 0;
}

static inline int64_t mako_str_slice_ci_eq(MakoString s, int64_t off, int64_t len, MakoString other) {
    if (!s.data || off < 0 || len < 0) return 0;
    if ((size_t)off + (size_t)len > s.len) return 0;
    size_t ol = other.data ? other.len : 0;
    return mako_str_ci_eq_bytes(
               s.data + (size_t)off, (size_t)len, other.data ? other.data : "", ol
           )
               ? 1
               : 0;
}

/* True if needle occurs inside s[off:off+len]. */
static inline int64_t mako_str_slice_contains(
    MakoString s, int64_t off, int64_t len, MakoString needle
) {
    if (!s.data || off < 0 || len < 0) return 0;
    if ((size_t)off + (size_t)len > s.len) return 0;
    if (!needle.data || needle.len == 0) return 1;
    if (needle.len > (size_t)len) return 0;
    const char *hay = s.data + (size_t)off;
    size_t lim = (size_t)len - needle.len;
    for (size_t i = 0; i <= lim; i++) {
        if (memcmp(hay + i, needle.data, needle.len) == 0) return 1;
    }
    return 0;
}

/* First index of needle in s[off:off+len], or -1. Relative to start of s (absolute offset). */
static inline int64_t mako_str_slice_index(MakoString s, int64_t off, int64_t len, MakoString needle) {
    if (!s.data || off < 0 || len < 0) return -1;
    if ((size_t)off + (size_t)len > s.len) return -1;
    if (!needle.data || needle.len == 0) return off;
    if (needle.len > (size_t)len) return -1;
    const char *hay = s.data + (size_t)off;
    size_t lim = (size_t)len - needle.len;
    for (size_t i = 0; i <= lim; i++) {
        if (memcmp(hay + i, needle.data, needle.len) == 0) return off + (int64_t)i;
    }
    return -1;
}

/* Byte at index, or -1 if OOB. */
static inline int64_t mako_str_byte_at(MakoString s, int64_t i) {
    if (!s.data || i < 0 || (size_t)i >= s.len) return -1;
    return (int64_t)(unsigned char)s.data[i];
}

/* Compare s[off..] prefix of length other.len with other (no alloc). */
static inline int64_t mako_str_at_eq(MakoString s, int64_t off, MakoString other) {
    size_t ol = other.data ? other.len : 0;
    return mako_str_slice_eq(s, off, (int64_t)ol, other);
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
    if (MAKO_UNLIKELY(!b)) mako_abort("str_builder: out of memory");
    b->data = (char *)malloc(64); /* start larger — f-strings / logs rarely tiny */
    if (MAKO_UNLIKELY(!b->data)) mako_abort("str_builder: out of memory");
    b->data[0] = 0;
    b->len = 0;
    b->cap = 64;
    return b;
}

static inline void mako_str_builder_grow(MakoStrBuilder *b, size_t need) {
    if (need <= b->cap) return;
    size_t ncap = b->cap ? b->cap * 2 : 64;
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

/* Zero-copy write of a C string literal / buffer (no intermediate MakoString). */
static inline void mako_str_builder_write_cstr(MakoStrBuilder *b, const char *s) {
    if (!s) return;
    size_t n = strlen(s);
    mako_str_builder_grow(b, b->len + n + 1);
    if (n) memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

/* Format int into the builder without a temporary heap string. */
static inline void mako_str_builder_write_i64(MakoStrBuilder *b, int64_t n) {
    char buf[32];
    int written = snprintf(buf, sizeof(buf), "%lld", (long long)n);
    if (written < 0) written = 0;
    mako_str_builder_grow(b, b->len + (size_t)written + 1);
    memcpy(b->data + b->len, buf, (size_t)written);
    b->len += (size_t)written;
    b->data[b->len] = 0;
}

/* f-string format specs for ints (printf-ish seed).
 * Flags: - left, + sign, ' ' space-sign, # alternate, 0 zero-pad.
 * Width digits, type: d / x / X / o / b (default d).
 * Examples: "02", "+d", "#x", "-5d", "08X".
 */
static inline void mako_str_builder_write_i64_spec(
    MakoStrBuilder *b, int64_t n, const char *spec
) {
    if (!spec || !spec[0]) {
        mako_str_builder_write_i64(b, n);
        return;
    }
    const char *p = spec;
    int left = 0, plus = 0, space = 0, alt = 0, zero_pad = 0;
    while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') {
        if (*p == '-') left = 1;
        else if (*p == '+') plus = 1;
        else if (*p == ' ') space = 1;
        else if (*p == '#') alt = 1;
        else if (*p == '0') zero_pad = 1;
        p++;
    }
    if (left) zero_pad = 0; /* - overrides 0 */
    int width = 0;
    while (*p >= '0' && *p <= '9') {
        width = width * 10 + (*p - '0');
        p++;
    }
    char type = (*p == 'd' || *p == 'x' || *p == 'X' || *p == 'o' || *p == 'b' || *p == 'i'
                 || *p == 'u')
                    ? *p
                    : 'd';
    if (type == 'i' || type == 'u') type = 'd';

    char num[96];
    int nlen = 0;
    char prefix[4];
    int plen = 0;
    prefix[0] = 0;

    if (type == 'x' || type == 'X') {
        unsigned long long u = (unsigned long long)n;
        const char *digits = (type == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
        if (u == 0) {
            num[nlen++] = '0';
        } else {
            char rev[72];
            int ri = 0;
            while (u) {
                rev[ri++] = digits[u & 15u];
                u >>= 4;
            }
            while (ri > 0) num[nlen++] = rev[--ri];
        }
        num[nlen] = 0;
        if (alt && n != 0) {
            prefix[plen++] = '0';
            prefix[plen++] = type; /* x or X */
            prefix[plen] = 0;
        }
    } else if (type == 'o') {
        unsigned long long u = (unsigned long long)n;
        if (u == 0) {
            num[nlen++] = '0';
        } else {
            char rev[80];
            int ri = 0;
            while (u) {
                rev[ri++] = (char)('0' + (u & 7u));
                u >>= 3;
            }
            while (ri > 0) num[nlen++] = rev[--ri];
        }
        num[nlen] = 0;
        if (alt && n != 0) {
            prefix[plen++] = '0';
            prefix[plen] = 0;
        }
    } else if (type == 'b') {
        uint64_t u = (uint64_t)n;
        if (u == 0) {
            num[nlen++] = '0';
        } else {
            char rev[66];
            int ri = 0;
            while (u) {
                rev[ri++] = (char)('0' + (u & 1u));
                u >>= 1;
            }
            while (ri > 0) num[nlen++] = rev[--ri];
        }
        num[nlen] = 0;
        if (alt && n != 0) {
            prefix[plen++] = '0';
            prefix[plen++] = 'b';
            prefix[plen] = 0;
        }
    } else {
        /* decimal with optional sign */
        int64_t v = n;
        if (v < 0) {
            prefix[plen++] = '-';
            prefix[plen] = 0;
            /* careful with INT64_MIN */
            uint64_t u = (v == INT64_MIN)
                             ? (uint64_t)1 << 63
                             : (uint64_t)(-v);
            if (u == 0) {
                num[nlen++] = '0';
            } else {
                char rev[24];
                int ri = 0;
                while (u) {
                    rev[ri++] = (char)('0' + (u % 10));
                    u /= 10;
                }
                while (ri > 0) num[nlen++] = rev[--ri];
            }
        } else {
            if (plus) {
                prefix[plen++] = '+';
                prefix[plen] = 0;
            } else if (space) {
                prefix[plen++] = ' ';
                prefix[plen] = 0;
            }
            uint64_t u = (uint64_t)v;
            if (u == 0) {
                num[nlen++] = '0';
            } else {
                char rev[24];
                int ri = 0;
                while (u) {
                    rev[ri++] = (char)('0' + (u % 10));
                    u /= 10;
                }
                while (ri > 0) num[nlen++] = rev[--ri];
            }
        }
        num[nlen] = 0;
    }

    int total = plen + nlen;
    int pad = (width > total) ? (width - total) : 0;
    char buf[160];
    int written = 0;
    if (left) {
        if (plen) {
            memcpy(buf + written, prefix, (size_t)plen);
            written += plen;
        }
        memcpy(buf + written, num, (size_t)nlen);
        written += nlen;
        memset(buf + written, ' ', (size_t)pad);
        written += pad;
    } else if (zero_pad && pad > 0) {
        if (plen) {
            memcpy(buf + written, prefix, (size_t)plen);
            written += plen;
        }
        memset(buf + written, '0', (size_t)pad);
        written += pad;
        memcpy(buf + written, num, (size_t)nlen);
        written += nlen;
    } else {
        memset(buf + written, ' ', (size_t)pad);
        written += pad;
        if (plen) {
            memcpy(buf + written, prefix, (size_t)plen);
            written += plen;
        }
        memcpy(buf + written, num, (size_t)nlen);
        written += nlen;
    }
    if (written < 0) written = 0;
    if ((size_t)written >= sizeof(buf)) written = (int)sizeof(buf) - 1;
    mako_str_builder_grow(b, b->len + (size_t)written + 1);
    memcpy(b->data + b->len, buf, (size_t)written);
    b->len += (size_t)written;
    b->data[b->len] = 0;
}

/* Float specs: flags (+ - space 0), width, .prec, type f/e/g (default f). */
static inline void mako_str_builder_write_f64_spec(
    MakoStrBuilder *b, double n, const char *spec
) {
    int left = 0, plus = 0, space = 0, zero_pad = 0;
    int width = 0;
    int prec = 6;
    int have_prec = 0;
    char type = 'f';
    if (spec && spec[0]) {
        const char *p = spec;
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '0') {
            if (*p == '-') left = 1;
            else if (*p == '+') plus = 1;
            else if (*p == ' ') space = 1;
            else if (*p == '0') zero_pad = 1;
            p++;
        }
        if (left) zero_pad = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }
        if (*p == '.') {
            p++;
            have_prec = 1;
            prec = 0;
            while (*p >= '0' && *p <= '9') {
                prec = prec * 10 + (*p - '0');
                p++;
            }
        }
        if (*p == 'f' || *p == 'F' || *p == 'e' || *p == 'E' || *p == 'g' || *p == 'G') {
            type = *p;
        } else if (!have_prec && width > 0 && *p == 0) {
            /* bare digits already consumed as width — keep prec 6 */
        }
        if (prec < 0) prec = 0;
        if (prec > 17) prec = 17;
    }
    char fmt[32];
    char flags[8];
    int fi = 0;
    if (left) flags[fi++] = '-';
    if (plus) flags[fi++] = '+';
    else if (space) flags[fi++] = ' ';
    if (zero_pad) flags[fi++] = '0';
    flags[fi] = 0;
    if (width > 0) {
        snprintf(fmt, sizeof(fmt), "%%%s%d.%d%c", flags, width, prec, type);
    } else {
        snprintf(fmt, sizeof(fmt), "%%%s.%d%c", flags, prec, type);
    }
    char buf[160];
    int written = snprintf(buf, sizeof(buf), fmt, n);
    if (written < 0) written = 0;
    if ((size_t)written >= sizeof(buf)) written = (int)sizeof(buf) - 1;
    mako_str_builder_grow(b, b->len + (size_t)written + 1);
    memcpy(b->data + b->len, buf, (size_t)written);
    b->len += (size_t)written;
    b->data[b->len] = 0;
}

/* String width pad: flags -/< left, > right (default), 0 ignored; width digits. */
static inline void mako_str_builder_write_str_spec(
    MakoStrBuilder *b, MakoString s, const char *spec
) {
    if (!spec || !spec[0]) {
        mako_str_builder_write(b, s);
        return;
    }
    int left_align = 0;
    const char *p = spec;
    while (*p == '<' || *p == '>' || *p == '-' || *p == '0') {
        if (*p == '<' || *p == '-') left_align = 1;
        else if (*p == '>') left_align = 0;
        p++;
    }
    int width = 0;
    while (*p >= '0' && *p <= '9') {
        width = width * 10 + (*p - '0');
        p++;
    }
    size_t slen = s.len;
    if (width <= 0 || (size_t)width <= slen) {
        mako_str_builder_write(b, s);
        return;
    }
    size_t pad = (size_t)width - slen;
    mako_str_builder_grow(b, b->len + (size_t)width + 1);
    if (left_align) {
        if (slen && s.data) memcpy(b->data + b->len, s.data, slen);
        b->len += slen;
        memset(b->data + b->len, ' ', pad);
        b->len += pad;
    } else {
        memset(b->data + b->len, ' ', pad);
        b->len += pad;
        if (slen && s.data) memcpy(b->data + b->len, s.data, slen);
        b->len += slen;
    }
    b->data[b->len] = 0;
}

static inline void mako_str_builder_write_byte(MakoStrBuilder *b, int64_t v) {
    if (v < 0 || v > 255) mako_abort("str_builder write_byte: out of range 0..255");
    mako_str_builder_grow(b, b->len + 2);
    b->data[b->len++] = (char)(uint8_t)v;
    b->data[b->len] = 0;
}

/* Copy-out (legacy): keeps builder reusable. Prefer finish on hot paths. */
static inline MakoString mako_str_builder_string(MakoStrBuilder *b) {
    char *d = (char *)malloc(b->len + 1);
    if (MAKO_UNLIKELY(!d)) mako_abort("str_builder: out of memory");
    if (b->len) memcpy(d, b->data, b->len);
    d[b->len] = 0;
    MakoString out = {d, b->len};
    return out;
}

/* Steal buffer as MakoString and free the builder shell — one heap string, no copy. */
static inline MakoString mako_str_builder_finish(MakoStrBuilder *b) {
    if (MAKO_UNLIKELY(!b)) return mako_str_empty;
    /* Ensure NUL-terminated owned buffer. */
    mako_str_builder_grow(b, b->len + 1);
    b->data[b->len] = 0;
    MakoString out = {b->data, b->len};
    free(b);
    return out;
}

static inline void mako_str_builder_free(MakoStrBuilder *b) {
    if (!b) return;
    free(b->data);
    free(b);
}

static inline int64_t mako_str_builder_len(MakoStrBuilder *b) {
    return (int64_t)b->len;
}

/* ---- Small-block freelist for chan[Struct]/tuple] heap boxes (speed/memory) ---- */
enum { MAKO_BOX_BINS = 6 };
/* bin i covers size up to 16 << i  (16, 32, 64, 128, 256, 512) */
static void *mako_box_freelist[MAKO_BOX_BINS];
static pthread_mutex_t mako_box_mu = PTHREAD_MUTEX_INITIALIZER;

static inline int mako_box_bin(size_t sz) {
    size_t lim = 16;
    for (int i = 0; i < MAKO_BOX_BINS; i++) {
        if (sz <= lim) return i;
        lim <<= 1;
    }
    return -1;
}

static inline size_t mako_box_bin_size(int bin) {
    return (size_t)16 << bin;
}

/* Allocate a heap box for POD channel payloads; freelist when size ≤ 512. */
static inline void *mako_box_alloc(size_t sz) {
    int bin = mako_box_bin(sz);
    if (bin >= 0) {
        pthread_mutex_lock(&mako_box_mu);
        void *p = mako_box_freelist[bin];
        if (p) {
            mako_box_freelist[bin] = *(void **)p;
            pthread_mutex_unlock(&mako_box_mu);
            return p;
        }
        pthread_mutex_unlock(&mako_box_mu);
        return malloc(mako_box_bin_size(bin));
    }
    return malloc(sz);
}

static inline void mako_box_free(void *p, size_t sz) {
    if (!p) return;
    int bin = mako_box_bin(sz);
    if (bin >= 0) {
        pthread_mutex_lock(&mako_box_mu);
        *(void **)p = mako_box_freelist[bin];
        mako_box_freelist[bin] = p;
        pthread_mutex_unlock(&mako_box_mu);
        return;
    }
    free(p);
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
    if (!p || n == 0) return h;
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

/* Float map keys: +0/-0 same key; all NaNs share one key. */
static inline double mako_f64_key_norm(double x) {
    if (x == 0.0) return 0.0; /* +0 and -0 */
    return x;
}
static inline uint64_t mako_f64_bits(double x) {
    uint64_t u;
    memcpy(&u, &x, sizeof u);
    return u;
}
static inline uint64_t mako_hash_f64(double x) {
    x = mako_f64_key_norm(x);
    if (x != x) {
        /* Canonical quiet NaN bits as key */
        return mako_hash_i64((int64_t)0x7ff8000000000000ULL);
    }
    return mako_hash_i64((int64_t)mako_f64_bits(x));
}
static inline bool mako_f64_key_eq(double a, double b) {
    if (a == 0.0 && b == 0.0) return true;
    if (a != a && b != b) return true; /* both NaN */
    return a == b;
}

/* Structural eq/hash for Option/Result bags (struct fields, map keys). */
static inline bool mako_eq_option_int(MakoOptionInt a, MakoOptionInt b) {
    if (a.some != b.some) return false;
    if (!a.some) return true;
    return a.value == b.value && mako_str_eq(a.ok_s, b.ok_s) && a.ok_f == b.ok_f;
}

static inline uint64_t mako_hash_option_int(MakoOptionInt k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= (uint64_t)(k.some ? 1 : 0);
    h *= 1099511628211ULL;
    if (!k.some) return h;
    h ^= mako_hash_i64(k.value);
    h *= 1099511628211ULL;
    h ^= mako_hash_bytes(k.ok_s.data ? k.ok_s.data : "", k.ok_s.data ? k.ok_s.len : 0);
    h *= 1099511628211ULL;
    h ^= mako_hash_f64(k.ok_f);
    h *= 1099511628211ULL;
    return h;
}

static inline bool mako_eq_result_int(MakoResultInt a, MakoResultInt b) {
    if (a.ok != b.ok) return false;
    if (a.ok) {
        return a.value == b.value && mako_str_eq(a.ok_s, b.ok_s) && a.ok_f == b.ok_f;
    }
    if (a.err_kind != b.err_kind) return false;
    if (a.err_kind == 0) {
        return mako_str_eq(a.err, b.err);
    }
    return a.err_tag == b.err_tag && a.err_i0 == b.err_i0 && a.err_i1 == b.err_i1
        && a.err_i2 == b.err_i2 && mako_str_eq(a.err_s0, b.err_s0)
        && mako_str_eq(a.err_s1, b.err_s1);
}

static inline uint64_t mako_hash_result_int(MakoResultInt k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= (uint64_t)(k.ok ? 1 : 0);
    h *= 1099511628211ULL;
    if (k.ok) {
        h ^= mako_hash_i64(k.value);
        h *= 1099511628211ULL;
        h ^= mako_hash_bytes(k.ok_s.data ? k.ok_s.data : "", k.ok_s.data ? k.ok_s.len : 0);
        h *= 1099511628211ULL;
        h ^= mako_hash_f64(k.ok_f);
        h *= 1099511628211ULL;
        return h;
    }
    h ^= (uint64_t)(uint32_t)k.err_kind;
    h *= 1099511628211ULL;
    if (k.err_kind == 0) {
        h ^= mako_hash_bytes(k.err.data ? k.err.data : "", k.err.data ? k.err.len : 0);
        h *= 1099511628211ULL;
        return h;
    }
    h ^= (uint64_t)(uint32_t)k.err_tag;
    h *= 1099511628211ULL;
    h ^= mako_hash_i64(k.err_i0);
    h *= 1099511628211ULL;
    h ^= mako_hash_i64(k.err_i1);
    h *= 1099511628211ULL;
    h ^= mako_hash_i64(k.err_i2);
    h *= 1099511628211ULL;
    h ^= mako_hash_bytes(k.err_s0.data ? k.err_s0.data : "", k.err_s0.data ? k.err_s0.len : 0);
    h *= 1099511628211ULL;
    h ^= mako_hash_bytes(k.err_s1.data ? k.err_s1.data : "", k.err_s1.data ? k.err_s1.len : 0);
    h *= 1099511628211ULL;
    return h;
}

static inline bool mako_eq_result_float(MakoResultFloat a, MakoResultFloat b) {
    if (a.ok != b.ok) return false;
    if (a.ok) return a.value == b.value;
    return mako_str_eq(a.err, b.err);
}

static inline uint64_t mako_hash_result_float(MakoResultFloat k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= (uint64_t)(k.ok ? 1 : 0);
    h *= 1099511628211ULL;
    if (k.ok) {
        h ^= mako_hash_f64(k.value);
        h *= 1099511628211ULL;
        return h;
    }
    h ^= mako_hash_bytes(k.err.data ? k.err.data : "", k.err.data ? k.err.len : 0);
    h *= 1099511628211ULL;
    return h;
}

static inline MakoString mako_str_clone(MakoString s) {
    if (MAKO_UNLIKELY(s.len == 0)) {
        return mako_str_empty;
    }
    char *d = (char *)malloc(s.len + 1);
    if (MAKO_UNLIKELY(!d)) {
        fprintf(stderr, "mako: OOM in str_clone\n");
        abort();
    }
    memcpy(d, s.data, s.len);
    d[s.len] = 0;
    MakoString out = {d, s.len};
    return out;
}

static inline MakoMapSI mako_map_si_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
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

/* Insert/update with *owned* key move (no clone). Caller must not free/use key after. */
static inline void mako_map_si_set_take(MakoMapSI *m, MakoString key, int64_t val) {
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) {
        mako_map_si_rehash(m, m->cap * 2);
    }
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (MAKO_LIKELY(st == MAKO_MAP_EMPTY)) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key; /* take ownership */
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (st == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (m->keys[i].len == key.len
                   && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            /* Key already present — drop the taken key, keep stored key. */
            mako_str_free(key);
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & mask;
    }
}

/* Default: clone key (safe; key still usable after). Hot paths use set_take. */
static inline void mako_map_si_set(MakoMapSI *m, MakoString key, int64_t val) {
    mako_map_si_set_take(m, mako_str_clone(key), val);
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
            mako_str_free(m->keys[i]);
            m->keys[i].data = NULL;
            m->keys[i].len = 0;
            m->state[i] = MAKO_MAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline int64_t mako_map_si_len(MakoMapSI *m) { return m ? (int64_t)m->len : 0; }

static inline MakoMapSI *mako_map_si_make(int64_t hint) {
    MakoMapSI *m = (MakoMapSI *)malloc(sizeof(MakoMapSI));
    *m = mako_map_si_new(hint > 0 ? (size_t)hint : 0);
    return m;
}

static inline MakoMapII mako_map_ii_new(size_t hint) {
    size_t cap = 8;
    /* Pre-size so hint entries fit under ~75% load without immediate rehash. */
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
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
    /* Grow at ~75% load (was 70%) — fewer rehashes on sequential inserts. */
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) {
        mako_map_ii_rehash(m, m->cap * 2);
    }
    uint64_t h = mako_hash_i64(key);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (MAKO_LIKELY(st == MAKO_MAP_EMPTY)) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (st == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (MAKO_LIKELY(m->keys[i] == key)) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & mask;
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
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    for (;;) {
        uint8_t st = m->state[i];
        if (MAKO_LIKELY(st == MAKO_MAP_FULL)) {
            if (MAKO_LIKELY(m->keys[i] == key)) return m->vals[i];
        } else if (st == MAKO_MAP_EMPTY) {
            return 0;
        }
        i = (i + 1) & mask;
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

static inline int64_t mako_map_ii_len(MakoMapII *m) { return m ? (int64_t)m->len : 0; }

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

/* map[int]float */
typedef struct {
    uint8_t *state;
    int64_t *keys;
    double *vals;
    size_t cap;
    size_t len;
} MakoMapIF;

static inline MakoMapIF mako_map_if_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapIF m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (int64_t *)malloc(cap * sizeof(int64_t));
    m.vals = (double *)malloc(cap * sizeof(double));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_if_new\n");
        abort();
    }
    m.cap = cap;
    m.len = 0;
    return m;
}

static inline void mako_map_if_rehash(MakoMapIF *m, size_t ncap);

static inline void mako_map_if_set(MakoMapIF *m, int64_t key, double val) {
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) {
        mako_map_if_rehash(m, m->cap * 2);
    }
    uint64_t h = mako_hash_i64(key);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (MAKO_LIKELY(st == MAKO_MAP_EMPTY)) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (st == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (MAKO_LIKELY(m->keys[i] == key)) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & mask;
    }
}

static inline void mako_map_if_rehash(MakoMapIF *m, size_t ncap) {
    uint8_t *ostate = m->state;
    int64_t *okeys = m->keys;
    double *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapIF n = mako_map_if_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        int64_t key = okeys[i];
        double val = ovals[i];
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

static inline double mako_map_if_get(MakoMapIF *m, int64_t key) {
    if (!m) return 0.0;
    uint64_t h = mako_hash_i64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return 0.0;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}

static inline bool mako_map_if_has(MakoMapIF *m, int64_t key) {
    if (!m) return false;
    uint64_t h = mako_hash_i64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return true;
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_if_delete(MakoMapIF *m, int64_t key) {
    if (!m) return;
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

static inline int64_t mako_map_if_len(MakoMapIF *m) { return m ? (int64_t)m->len : 0; }

static inline MakoMapIF *mako_map_if_make(int64_t hint) {
    MakoMapIF *m = (MakoMapIF *)malloc(sizeof(MakoMapIF));
    *m = mako_map_if_new(hint > 0 ? (size_t)hint : 0);
    return m;
}

/* map[string]float */
typedef struct {
    uint8_t *state;
    MakoString *keys;
    double *vals;
    size_t cap;
    size_t len;
} MakoMapSF;

static inline MakoMapSF mako_map_sf_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapSF m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (MakoString *)calloc(cap, sizeof(MakoString));
    m.vals = (double *)malloc(cap * sizeof(double));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_sf_new\n");
        abort();
    }
    m.cap = cap;
    m.len = 0;
    return m;
}

static inline void mako_map_sf_rehash(MakoMapSF *m, size_t ncap);

static inline void mako_map_sf_set_take(MakoMapSF *m, MakoString key, double val) {
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) {
        mako_map_sf_rehash(m, m->cap * 2);
    }
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (MAKO_LIKELY(st == MAKO_MAP_EMPTY)) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (st == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (m->keys[i].len == key.len
                   && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            mako_str_free(key);
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & mask;
    }
}

static inline void mako_map_sf_set(MakoMapSF *m, MakoString key, double val) {
    mako_map_sf_set_take(m, mako_str_clone(key), val);
}

static inline void mako_map_sf_rehash(MakoMapSF *m, size_t ncap) {
    uint8_t *ostate = m->state;
    MakoString *okeys = m->keys;
    double *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapSF n = mako_map_sf_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        MakoString key = okeys[i];
        double val = ovals[i];
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

static inline double mako_map_sf_get(MakoMapSF *m, MakoString key) {
    if (!m) return 0.0;
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return 0.0;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i].len == key.len
            && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            return m->vals[i];
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline bool mako_map_sf_has(MakoMapSF *m, MakoString key) {
    if (!m) return false;
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

static inline void mako_map_sf_delete(MakoMapSF *m, MakoString key) {
    if (!m) return;
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i].len == key.len
            && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            mako_str_free(m->keys[i]);
            m->keys[i].data = NULL;
            m->keys[i].len = 0;
            m->state[i] = MAKO_MAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline int64_t mako_map_sf_len(MakoMapSF *m) { return m ? (int64_t)m->len : 0; }

static inline MakoMapSF *mako_map_sf_make(int64_t hint) {
    MakoMapSF *m = (MakoMapSF *)malloc(sizeof(MakoMapSF));
    *m = mako_map_sf_new(hint > 0 ? (size_t)hint : 0);
    return m;
}

/* map[float]int */
typedef struct {
    uint8_t *state;
    double *keys;
    int64_t *vals;
    size_t cap;
    size_t len;
} MakoMapFI;

static inline MakoMapFI mako_map_fi_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapFI m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (double *)malloc(cap * sizeof(double));
    m.vals = (int64_t *)malloc(cap * sizeof(int64_t));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_fi_new\n");
        abort();
    }
    m.cap = cap;
    m.len = 0;
    return m;
}
static inline void mako_map_fi_rehash(MakoMapFI *m, size_t ncap);
static inline void mako_map_fi_set(MakoMapFI *m, double key, int64_t val) {
    key = mako_f64_key_norm(key);
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) mako_map_fi_rehash(m, m->cap * 2);
    uint64_t h = mako_hash_f64(key);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (st == MAKO_MAP_EMPTY) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (st == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (mako_f64_key_eq(m->keys[i], key)) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & mask;
    }
}
static inline void mako_map_fi_rehash(MakoMapFI *m, size_t ncap) {
    uint8_t *ostate = m->state;
    double *okeys = m->keys;
    int64_t *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapFI n = mako_map_fi_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        double key = okeys[i];
        int64_t val = ovals[i];
        uint64_t h = mako_hash_f64(key);
        size_t j = (size_t)(h & (n.cap - 1));
        while (n.state[j] == MAKO_MAP_FULL) j = (j + 1) & (n.cap - 1);
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
static inline int64_t mako_map_fi_get(MakoMapFI *m, double key) {
    if (!m) return 0;
    key = mako_f64_key_norm(key);
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return 0;
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key)) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}
static inline bool mako_map_fi_has(MakoMapFI *m, double key) {
    if (!m) return false;
    key = mako_f64_key_norm(key);
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key)) return true;
        i = (i + 1) & (m->cap - 1);
    }
}
static inline void mako_map_fi_delete(MakoMapFI *m, double key) {
    if (!m) return;
    key = mako_f64_key_norm(key);
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key)) {
            m->state[i] = MAKO_MAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}
static inline int64_t mako_map_fi_len(MakoMapFI *m) { return m ? (int64_t)m->len : 0; }
static inline MakoMapFI *mako_map_fi_make(int64_t hint) {
    MakoMapFI *m = (MakoMapFI *)malloc(sizeof(MakoMapFI));
    *m = mako_map_fi_new(hint > 0 ? (size_t)hint : 0);
    return m;
}

/* map[float]string */
typedef struct {
    uint8_t *state;
    double *keys;
    MakoString *vals;
    size_t cap;
    size_t len;
} MakoMapFS;

static inline MakoMapFS mako_map_fs_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapFS m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (double *)malloc(cap * sizeof(double));
    m.vals = (MakoString *)calloc(cap, sizeof(MakoString));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_fs_new\n");
        abort();
    }
    m.cap = cap;
    m.len = 0;
    return m;
}
static inline void mako_map_fs_rehash(MakoMapFS *m, size_t ncap);
static inline void mako_map_fs_set_take(MakoMapFS *m, double key, MakoString val) {
    key = mako_f64_key_norm(key);
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) mako_map_fs_rehash(m, m->cap * 2);
    uint64_t h = mako_hash_f64(key);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (st == MAKO_MAP_EMPTY) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (st == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (mako_f64_key_eq(m->keys[i], key)) {
            mako_str_free(m->vals[i]);
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & mask;
    }
}
static inline void mako_map_fs_set(MakoMapFS *m, double key, MakoString val) {
    mako_map_fs_set_take(m, key, mako_str_clone(val));
}
static inline void mako_map_fs_rehash(MakoMapFS *m, size_t ncap) {
    uint8_t *ostate = m->state;
    double *okeys = m->keys;
    MakoString *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapFS n = mako_map_fs_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        double key = okeys[i];
        MakoString val = ovals[i];
        uint64_t h = mako_hash_f64(key);
        size_t j = (size_t)(h & (n.cap - 1));
        while (n.state[j] == MAKO_MAP_FULL) j = (j + 1) & (n.cap - 1);
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
static inline MakoString mako_map_fs_get(MakoMapFS *m, double key) {
    if (!m) return mako_str_from_cstr("");
    key = mako_f64_key_norm(key);
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return mako_str_from_cstr("");
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key))
            return mako_str_clone(m->vals[i]);
        i = (i + 1) & (m->cap - 1);
    }
}
static inline bool mako_map_fs_has(MakoMapFS *m, double key) {
    if (!m) return false;
    key = mako_f64_key_norm(key);
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key)) return true;
        i = (i + 1) & (m->cap - 1);
    }
}
static inline void mako_map_fs_delete(MakoMapFS *m, double key) {
    if (!m) return;
    key = mako_f64_key_norm(key);
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key)) {
            mako_str_free(m->vals[i]);
            m->vals[i].data = NULL;
            m->vals[i].len = 0;
            m->state[i] = MAKO_MAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}
static inline int64_t mako_map_fs_len(MakoMapFS *m) { return m ? (int64_t)m->len : 0; }
static inline MakoMapFS *mako_map_fs_make(int64_t hint) {
    MakoMapFS *m = (MakoMapFS *)malloc(sizeof(MakoMapFS));
    *m = mako_map_fs_new(hint > 0 ? (size_t)hint : 0);
    return m;
}

/* map[float]float */
typedef struct {
    uint8_t *state;
    double *keys;
    double *vals;
    size_t cap;
    size_t len;
} MakoMapFF;

static inline MakoMapFF mako_map_ff_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapFF m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (double *)malloc(cap * sizeof(double));
    m.vals = (double *)malloc(cap * sizeof(double));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_ff_new\n");
        abort();
    }
    m.cap = cap;
    m.len = 0;
    return m;
}
static inline void mako_map_ff_rehash(MakoMapFF *m, size_t ncap);
static inline void mako_map_ff_set(MakoMapFF *m, double key, double val) {
    key = mako_f64_key_norm(key);
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) mako_map_ff_rehash(m, m->cap * 2);
    uint64_t h = mako_hash_f64(key);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (st == MAKO_MAP_EMPTY) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (st == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (mako_f64_key_eq(m->keys[i], key)) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & mask;
    }
}
static inline void mako_map_ff_rehash(MakoMapFF *m, size_t ncap) {
    uint8_t *ostate = m->state;
    double *okeys = m->keys;
    double *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapFF n = mako_map_ff_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        double key = okeys[i];
        double val = ovals[i];
        uint64_t h = mako_hash_f64(key);
        size_t j = (size_t)(h & (n.cap - 1));
        while (n.state[j] == MAKO_MAP_FULL) j = (j + 1) & (n.cap - 1);
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
static inline double mako_map_ff_get(MakoMapFF *m, double key) {
    if (!m) return 0.0;
    key = mako_f64_key_norm(key);
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return 0.0;
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key)) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}
static inline bool mako_map_ff_has(MakoMapFF *m, double key) {
    if (!m) return false;
    key = mako_f64_key_norm(key);
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key)) return true;
        i = (i + 1) & (m->cap - 1);
    }
}
static inline void mako_map_ff_delete(MakoMapFF *m, double key) {
    if (!m) return;
    key = mako_f64_key_norm(key);
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key)) {
            m->state[i] = MAKO_MAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}
static inline int64_t mako_map_ff_len(MakoMapFF *m) { return m ? (int64_t)m->len : 0; }
static inline MakoMapFF *mako_map_ff_make(int64_t hint) {
    MakoMapFF *m = (MakoMapFF *)malloc(sizeof(MakoMapFF));
    *m = mako_map_ff_new(hint > 0 ? (size_t)hint : 0);
    return m;
}

/* map[int]bool */
typedef struct {
    uint8_t *state;
    int64_t *keys;
    bool *vals;
    size_t cap;
    size_t len;
} MakoMapIB;

static inline MakoMapIB mako_map_ib_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapIB m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (int64_t *)malloc(cap * sizeof(int64_t));
    m.vals = (bool *)malloc(cap * sizeof(bool));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_ib_new\n");
        abort();
    }
    m.cap = cap;
    m.len = 0;
    return m;
}

static inline void mako_map_ib_rehash(MakoMapIB *m, size_t ncap);

static inline void mako_map_ib_set(MakoMapIB *m, int64_t key, bool val) {
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) {
        mako_map_ib_rehash(m, m->cap * 2);
    }
    uint64_t h = mako_hash_i64(key);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (MAKO_LIKELY(st == MAKO_MAP_EMPTY)) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (st == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (MAKO_LIKELY(m->keys[i] == key)) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & mask;
    }
}

static inline void mako_map_ib_rehash(MakoMapIB *m, size_t ncap) {
    uint8_t *ostate = m->state;
    int64_t *okeys = m->keys;
    bool *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapIB n = mako_map_ib_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        int64_t key = okeys[i];
        bool val = ovals[i];
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

static inline bool mako_map_ib_get(MakoMapIB *m, int64_t key) {
    if (!m) return false;
    uint64_t h = mako_hash_i64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}

static inline bool mako_map_ib_has(MakoMapIB *m, int64_t key) {
    if (!m) return false;
    uint64_t h = mako_hash_i64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return true;
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_ib_delete(MakoMapIB *m, int64_t key) {
    if (!m) return;
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

static inline int64_t mako_map_ib_len(MakoMapIB *m) { return m ? (int64_t)m->len : 0; }

static inline MakoMapIB *mako_map_ib_make(int64_t hint) {
    MakoMapIB *m = (MakoMapIB *)malloc(sizeof(MakoMapIB));
    *m = mako_map_ib_new(hint > 0 ? (size_t)hint : 0);
    return m;
}

/* map[string]bool */
typedef struct {
    uint8_t *state;
    MakoString *keys;
    bool *vals;
    size_t cap;
    size_t len;
} MakoMapSB;

static inline MakoMapSB mako_map_sb_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapSB m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (MakoString *)calloc(cap, sizeof(MakoString));
    m.vals = (bool *)malloc(cap * sizeof(bool));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_sb_new\n");
        abort();
    }
    m.cap = cap;
    m.len = 0;
    return m;
}

static inline void mako_map_sb_rehash(MakoMapSB *m, size_t ncap);

static inline void mako_map_sb_set(MakoMapSB *m, MakoString key, bool val) {
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) {
        mako_map_sb_rehash(m, m->cap * 2);
    }
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (MAKO_LIKELY(st == MAKO_MAP_EMPTY)) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = mako_str_clone(key);
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (st == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (MAKO_LIKELY(mako_str_eq(m->keys[i], key))) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & mask;
    }
}

static inline void mako_map_sb_rehash(MakoMapSB *m, size_t ncap) {
    uint8_t *ostate = m->state;
    MakoString *okeys = m->keys;
    bool *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapSB n = mako_map_sb_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        MakoString key = okeys[i];
        bool val = ovals[i];
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

static inline bool mako_map_sb_get(MakoMapSB *m, MakoString key) {
    if (!m) return false;
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && mako_str_eq(m->keys[i], key)) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}

static inline bool mako_map_sb_has(MakoMapSB *m, MakoString key) {
    if (!m) return false;
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && mako_str_eq(m->keys[i], key)) return true;
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_sb_delete(MakoMapSB *m, MakoString key) {
    if (!m) return;
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && mako_str_eq(m->keys[i], key)) {
            mako_str_free(m->keys[i]);
            m->keys[i].data = NULL; m->keys[i].len = 0;
            m->state[i] = MAKO_MAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline int64_t mako_map_sb_len(MakoMapSB *m) { return m ? (int64_t)m->len : 0; }

static inline MakoMapSB *mako_map_sb_make(int64_t hint) {
    MakoMapSB *m = (MakoMapSB *)malloc(sizeof(MakoMapSB));
    *m = mako_map_sb_new(hint > 0 ? (size_t)hint : 0);
    return m;
}

/* map[float]bool */
typedef struct {
    uint8_t *state;
    double *keys;
    bool *vals;
    size_t cap;
    size_t len;
} MakoMapFB;

static inline MakoMapFB mako_map_fb_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapFB m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (double *)malloc(cap * sizeof(double));
    m.vals = (bool *)malloc(cap * sizeof(bool));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_fb_new\n");
        abort();
    }
    m.cap = cap;
    m.len = 0;
    return m;
}

static inline void mako_map_fb_rehash(MakoMapFB *m, size_t ncap);

static inline void mako_map_fb_set(MakoMapFB *m, double key, bool val) {
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) {
        mako_map_fb_rehash(m, m->cap * 2);
    }
    uint64_t h = mako_hash_f64(key);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (MAKO_LIKELY(st == MAKO_MAP_EMPTY)) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (st == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (MAKO_LIKELY(mako_f64_key_eq(m->keys[i], key))) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & mask;
    }
}

static inline void mako_map_fb_rehash(MakoMapFB *m, size_t ncap) {
    uint8_t *ostate = m->state;
    double *okeys = m->keys;
    bool *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapFB n = mako_map_fb_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        double key = okeys[i];
        bool val = ovals[i];
        uint64_t h = mako_hash_f64(key);
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

static inline bool mako_map_fb_get(MakoMapFB *m, double key) {
    if (!m) return false;
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key)) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}

static inline bool mako_map_fb_has(MakoMapFB *m, double key) {
    if (!m) return false;
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key)) return true;
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_fb_delete(MakoMapFB *m, double key) {
    if (!m) return;
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key)) {
            m->state[i] = MAKO_MAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline int64_t mako_map_fb_len(MakoMapFB *m) { return m ? (int64_t)m->len : 0; }

static inline MakoMapFB *mako_map_fb_make(int64_t hint) {
    MakoMapFB *m = (MakoMapFB *)malloc(sizeof(MakoMapFB));
    *m = mako_map_fb_new(hint > 0 ? (size_t)hint : 0);
    return m;
}

/* map[bool]int */
typedef struct {
    uint8_t *state;
    bool *keys;
    int64_t *vals;
    size_t cap;
    size_t len;
} MakoMapBI;

static inline MakoMapBI mako_map_bi_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapBI m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (bool *)malloc(cap * sizeof(bool));
    m.vals = (int64_t *)malloc(cap * sizeof(int64_t));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_bi_new\n");
        abort();
    }
    m.cap = cap; m.len = 0; return m;
}

static inline void mako_map_bi_rehash(MakoMapBI *m, size_t ncap);

static inline void mako_map_bi_set(MakoMapBI *m, bool key, int64_t val) {
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) mako_map_bi_rehash(m, m->cap * 2);
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (MAKO_LIKELY(st == MAKO_MAP_EMPTY)) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++; return;
        }
        if (st == MAKO_MAP_TOMB) { if (first_tomb == (size_t)-1) first_tomb = i; }
        else if (m->keys[i] == key) { m->vals[i] = val; return; }
        i = (i + 1) & mask;
    }
}

static inline void mako_map_bi_rehash(MakoMapBI *m, size_t ncap) {
    uint8_t *ostate = m->state;
    bool *okeys = m->keys;
    int64_t *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapBI n = mako_map_bi_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        bool key = okeys[i];
        int64_t val = ovals[i];
        uint64_t h = mako_hash_i64(key ? 1 : 0);
        size_t j = (size_t)(h & (n.cap - 1));
        while (n.state[j] == MAKO_MAP_FULL) j = (j + 1) & (n.cap - 1);
        n.state[j] = MAKO_MAP_FULL;
        n.keys[j] = key; n.vals[j] = val; n.len++;
    }
    free(ostate); free(okeys); free(ovals); *m = n;
}

static inline int64_t mako_map_bi_get(MakoMapBI *m, bool key) {
    if (!m) return 0;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return 0;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}

static inline bool mako_map_bi_has(MakoMapBI *m, bool key) {
    if (!m) return false;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return true;
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_bi_delete(MakoMapBI *m, bool key) {
    if (!m) return;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) {
            m->state[i] = MAKO_MAP_TOMB; m->len--; return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline int64_t mako_map_bi_len(MakoMapBI *m) { return m ? (int64_t)m->len : 0; }

static inline MakoMapBI *mako_map_bi_make(int64_t hint) {
    MakoMapBI *m = (MakoMapBI *)malloc(sizeof(MakoMapBI));
    *m = mako_map_bi_new(hint > 0 ? (size_t)hint : 0); return m;
}

/* map[bool]string */
typedef struct {
    uint8_t *state;
    bool *keys;
    MakoString *vals;
    size_t cap;
    size_t len;
} MakoMapBS;

static inline MakoMapBS mako_map_bs_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapBS m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (bool *)malloc(cap * sizeof(bool));
    m.vals = (MakoString *)calloc(cap, sizeof(MakoString));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_bs_new\n");
        abort();
    }
    m.cap = cap; m.len = 0; return m;
}

static inline void mako_map_bs_rehash(MakoMapBS *m, size_t ncap);

static inline void mako_map_bs_set(MakoMapBS *m, bool key, MakoString val) {
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) mako_map_bs_rehash(m, m->cap * 2);
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (MAKO_LIKELY(st == MAKO_MAP_EMPTY)) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = mako_str_clone(val);
            m->len++; return;
        }
        if (st == MAKO_MAP_TOMB) { if (first_tomb == (size_t)-1) first_tomb = i; }
        else if (m->keys[i] == key) {
            mako_str_free(m->vals[i]); m->vals[i] = mako_str_clone(val); return;
        }
        i = (i + 1) & mask;
    }
}

static inline void mako_map_bs_rehash(MakoMapBS *m, size_t ncap) {
    uint8_t *ostate = m->state;
    bool *okeys = m->keys;
    MakoString *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapBS n = mako_map_bs_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        bool key = okeys[i];
        MakoString val = ovals[i];
        uint64_t h = mako_hash_i64(key ? 1 : 0);
        size_t j = (size_t)(h & (n.cap - 1));
        while (n.state[j] == MAKO_MAP_FULL) j = (j + 1) & (n.cap - 1);
        n.state[j] = MAKO_MAP_FULL;
        n.keys[j] = key; n.vals[j] = val; n.len++;
    }
    free(ostate); free(okeys); free(ovals); *m = n;
}

static inline MakoString mako_map_bs_get(MakoMapBS *m, bool key) {
    if (!m) return mako_str_from_cstr("");
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return mako_str_from_cstr("");
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return mako_str_clone(m->vals[i]);
        i = (i + 1) & (m->cap - 1);
    }
}

static inline bool mako_map_bs_has(MakoMapBS *m, bool key) {
    if (!m) return false;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return true;
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_bs_delete(MakoMapBS *m, bool key) {
    if (!m) return;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) {
            mako_str_free(m->vals[i]); m->vals[i].data = NULL; m->vals[i].len = 0;
            m->state[i] = MAKO_MAP_TOMB; m->len--; return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline int64_t mako_map_bs_len(MakoMapBS *m) { return m ? (int64_t)m->len : 0; }

static inline MakoMapBS *mako_map_bs_make(int64_t hint) {
    MakoMapBS *m = (MakoMapBS *)malloc(sizeof(MakoMapBS));
    *m = mako_map_bs_new(hint > 0 ? (size_t)hint : 0); return m;
}

/* map[bool]float */
typedef struct {
    uint8_t *state;
    bool *keys;
    double *vals;
    size_t cap;
    size_t len;
} MakoMapBF;

static inline MakoMapBF mako_map_bf_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapBF m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (bool *)malloc(cap * sizeof(bool));
    m.vals = (double *)malloc(cap * sizeof(double));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_bf_new\n");
        abort();
    }
    m.cap = cap; m.len = 0; return m;
}

static inline void mako_map_bf_rehash(MakoMapBF *m, size_t ncap);

static inline void mako_map_bf_set(MakoMapBF *m, bool key, double val) {
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) mako_map_bf_rehash(m, m->cap * 2);
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (MAKO_LIKELY(st == MAKO_MAP_EMPTY)) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++; return;
        }
        if (st == MAKO_MAP_TOMB) { if (first_tomb == (size_t)-1) first_tomb = i; }
        else if (m->keys[i] == key) { m->vals[i] = val; return; }
        i = (i + 1) & mask;
    }
}

static inline void mako_map_bf_rehash(MakoMapBF *m, size_t ncap) {
    uint8_t *ostate = m->state;
    bool *okeys = m->keys;
    double *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapBF n = mako_map_bf_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        bool key = okeys[i];
        double val = ovals[i];
        uint64_t h = mako_hash_i64(key ? 1 : 0);
        size_t j = (size_t)(h & (n.cap - 1));
        while (n.state[j] == MAKO_MAP_FULL) j = (j + 1) & (n.cap - 1);
        n.state[j] = MAKO_MAP_FULL;
        n.keys[j] = key; n.vals[j] = val; n.len++;
    }
    free(ostate); free(okeys); free(ovals); *m = n;
}

static inline double mako_map_bf_get(MakoMapBF *m, bool key) {
    if (!m) return 0.0;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return 0.0;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}

static inline bool mako_map_bf_has(MakoMapBF *m, bool key) {
    if (!m) return false;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return true;
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_bf_delete(MakoMapBF *m, bool key) {
    if (!m) return;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) {
            m->state[i] = MAKO_MAP_TOMB; m->len--; return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline int64_t mako_map_bf_len(MakoMapBF *m) { return m ? (int64_t)m->len : 0; }

static inline MakoMapBF *mako_map_bf_make(int64_t hint) {
    MakoMapBF *m = (MakoMapBF *)malloc(sizeof(MakoMapBF));
    *m = mako_map_bf_new(hint > 0 ? (size_t)hint : 0); return m;
}

/* map[bool]bool */
typedef struct {
    uint8_t *state;
    bool *keys;
    bool *vals;
    size_t cap;
    size_t len;
} MakoMapBB;

static inline MakoMapBB mako_map_bb_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapBB m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (bool *)malloc(cap * sizeof(bool));
    m.vals = (bool *)malloc(cap * sizeof(bool));
    if (!m.keys || !m.vals) {
        fprintf(stderr, "mako: OOM in map_bb_new\n");
        abort();
    }
    m.cap = cap; m.len = 0; return m;
}

static inline void mako_map_bb_rehash(MakoMapBB *m, size_t ncap);

static inline void mako_map_bb_set(MakoMapBB *m, bool key, bool val) {
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) mako_map_bb_rehash(m, m->cap * 2);
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (MAKO_LIKELY(st == MAKO_MAP_EMPTY)) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++; return;
        }
        if (st == MAKO_MAP_TOMB) { if (first_tomb == (size_t)-1) first_tomb = i; }
        else if (m->keys[i] == key) { m->vals[i] = val; return; }
        i = (i + 1) & mask;
    }
}

static inline void mako_map_bb_rehash(MakoMapBB *m, size_t ncap) {
    uint8_t *ostate = m->state;
    bool *okeys = m->keys;
    bool *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapBB n = mako_map_bb_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        bool key = okeys[i];
        bool val = ovals[i];
        uint64_t h = mako_hash_i64(key ? 1 : 0);
        size_t j = (size_t)(h & (n.cap - 1));
        while (n.state[j] == MAKO_MAP_FULL) j = (j + 1) & (n.cap - 1);
        n.state[j] = MAKO_MAP_FULL;
        n.keys[j] = key; n.vals[j] = val; n.len++;
    }
    free(ostate); free(okeys); free(ovals); *m = n;
}

static inline bool mako_map_bb_get(MakoMapBB *m, bool key) {
    if (!m) return false;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}

static inline bool mako_map_bb_has(MakoMapBB *m, bool key) {
    if (!m) return false;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return true;
        i = (i + 1) & (m->cap - 1);
    }
}

static inline void mako_map_bb_delete(MakoMapBB *m, bool key) {
    if (!m) return;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) {
            m->state[i] = MAKO_MAP_TOMB; m->len--; return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

static inline int64_t mako_map_bb_len(MakoMapBB *m) { return m ? (int64_t)m->len : 0; }

static inline MakoMapBB *mako_map_bb_make(int64_t hint) {
    MakoMapBB *m = (MakoMapBB *)malloc(sizeof(MakoMapBB));
    *m = mako_map_bb_new(hint > 0 ? (size_t)hint : 0); return m;
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
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
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

/* Take ownership of key and val (no clone). */
static inline void mako_map_ss_set_take(MakoMapSS *m, MakoString key, MakoString val) {
    if (MAKO_UNLIKELY((m->len + 1) * 4 >= m->cap * 3)) {
        mako_map_ss_rehash(m, m->cap * 2);
    }
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t mask = m->cap - 1;
    size_t i = (size_t)(h & mask);
    size_t first_tomb = (size_t)-1;
    for (;;) {
        uint8_t st = m->state[i];
        if (MAKO_LIKELY(st == MAKO_MAP_EMPTY)) {
            size_t slot = (first_tomb != (size_t)-1) ? first_tomb : i;
            m->state[slot] = MAKO_MAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (st == MAKO_MAP_TOMB) {
            if (first_tomb == (size_t)-1) first_tomb = i;
        } else if (m->keys[i].len == key.len
                   && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            mako_str_free(key);
            mako_str_free(m->vals[i]);
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & mask;
    }
}

static inline void mako_map_ss_set(MakoMapSS *m, MakoString key, MakoString val) {
    mako_map_ss_set_take(m, mako_str_clone(key), mako_str_clone(val));
}

static inline void mako_map_ss_rehash(MakoMapSS *m, size_t ncap) {
    /* Move owned keys/vals — no clone/free churn. */
    uint8_t *ostate = m->state;
    MakoString *okeys = m->keys;
    MakoString *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapSS n = mako_map_ss_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        MakoString key = okeys[i];
        MakoString val = ovals[i];
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
            mako_str_free(m->keys[i]);
            mako_str_free(m->vals[i]);
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

static inline int64_t mako_map_ss_len(MakoMapSS *m) { return m ? (int64_t)m->len : 0; }

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
 * MAKO_BOUNDS_ALWAYS is set (`mako build --bounds always` or
 * `[profile.release] bounds_checks = "on"`). Default release is the speed path.
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
    if (MAKO_LIKELY(s.len < s.cap)) {
        s.data[s.len++] = v;
        return s;
    }
    size_t ncap = s.cap ? s.cap * 2 : 1;
    if (ncap < s.len + 1) ncap = s.len + 1;
    int64_t *nd = (int64_t *)malloc(ncap * sizeof(int64_t));
    if (MAKO_UNLIKELY(!nd)) mako_abort("append: out of memory");
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
        int64_t t0 = mako_now_ns();
        pthread_cond_wait(&c->can_send, &c->mu);
        mako_rt_note_lock_wait(mako_now_ns() - t0);
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
        int64_t t0 = mako_now_ns();
        pthread_cond_wait(&c->can_recv, &c->mu);
        mako_rt_note_lock_wait(mako_now_ns() - t0);
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

/* Timed send/recv (portable; short nanosleep slices — no busy-spin).
 * Returns: 1 success, 0 timeout, -1 closed (or closed empty on recv). */
static inline int64_t mako_chan_send_timeout(MakoChan *c, int64_t v, int64_t timeout_ms) {
    if (!c) return -1;
    struct timeval start, now;
    mako_gettimeofday(&start, NULL);
    for (;;) {
        if (mako_chan_try_send(c, v)) return 1;
        pthread_mutex_lock(&c->mu);
        int closed = c->closed ? 1 : 0;
        pthread_mutex_unlock(&c->mu);
        if (closed) return -1;
        if (timeout_ms >= 0) {
            mako_gettimeofday(&now, NULL);
            int64_t elapsed =
                (now.tv_sec - start.tv_sec) * 1000 +
                (now.tv_usec - start.tv_usec) / 1000;
            if (elapsed >= timeout_ms) {
                mako_rt_counter_inc(&mako_rt_channel_select_timeouts);
                return 0;
            }
        }
        struct timespec ts = {0, 2000000L}; /* 2ms */
        nanosleep(&ts, NULL);
    }
}

static inline int64_t mako_chan_recv_timeout(MakoChan *c, int64_t *out, int64_t timeout_ms) {
    if (!c) return -1;
    struct timeval start, now;
    mako_gettimeofday(&start, NULL);
    for (;;) {
        if (mako_chan_try_recv(c, out)) return 1;
        pthread_mutex_lock(&c->mu);
        int closed_empty = (c->count == 0 && c->closed) ? 1 : 0;
        pthread_mutex_unlock(&c->mu);
        if (closed_empty) return -1;
        if (timeout_ms >= 0) {
            mako_gettimeofday(&now, NULL);
            int64_t elapsed =
                (now.tv_sec - start.tv_sec) * 1000 +
                (now.tv_usec - start.tv_usec) / 1000;
            if (elapsed >= timeout_ms) {
                mako_rt_counter_inc(&mako_rt_channel_select_timeouts);
                return 0;
            }
        }
        struct timespec ts = {0, 2000000L};
        nanosleep(&ts, NULL);
    }
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

/* Move ownership of `v` into the channel (no clone). Caller must not free/use `v`. */
static inline int64_t mako_chan_str_send_take(MakoChanStr *c, MakoString v) {
    if (!c) {
        mako_str_free(v);
        return 0;
    }
    pthread_mutex_lock(&c->mu);
    while (c->count == c->cap && !c->closed) {
        pthread_cond_wait(&c->can_send, &c->mu);
    }
    if (c->closed) {
        pthread_mutex_unlock(&c->mu);
        mako_str_free(v);
        return 0;
    }
    c->buf[c->tail] = v; /* take */
    c->tail = (c->tail + 1) % c->cap;
    c->count++;
    pthread_cond_signal(&c->can_recv);
    pthread_mutex_unlock(&c->mu);
    return 1;
}

/* Default send: clone so caller retains `v`. Hot paths use send_take. */
static inline int64_t mako_chan_str_send(MakoChanStr *c, MakoString v) {
    return mako_chan_str_send_take(c, mako_str_clone(v));
}

/* Nonblocking try-send: 1 on queued, 0 if full/closed. Takes ownership on success;
 * on failure (full/closed) frees `v` so the call is always consuming. */
static inline int64_t mako_chan_str_try_send_take(MakoChanStr *c, MakoString v) {
    if (!c) {
        mako_str_free(v);
        return 0;
    }
    pthread_mutex_lock(&c->mu);
    if (c->closed || c->count == c->cap) {
        pthread_mutex_unlock(&c->mu);
        mako_str_free(v);
        return 0;
    }
    c->buf[c->tail] = v;
    c->tail = (c->tail + 1) % c->cap;
    c->count++;
    pthread_cond_signal(&c->can_recv);
    pthread_mutex_unlock(&c->mu);
    return 1;
}

static inline int64_t mako_chan_str_try_send(MakoChanStr *c, MakoString v) {
    /* Clone first so failure does not consume caller's string. */
    return mako_chan_str_try_send_take(c, mako_str_clone(v));
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
    else mako_str_free(v);
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
                mako_str_free(mako_select_last_str);
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
    int64_t debug_id;    /* registry id for task inspect (0 = unregistered) */
    int64_t parent_id;   /* async parent task id (0 = root / unknown) */
} MakoTask;

/* TLS: current task id for parent-link seed when spawning children. */
static __thread int64_t mako_debug_current_task_id = 0;

/* ---- Task inspect / debugger seed ---- */
#define MAKO_TASK_REG_MAX 64
typedef struct {
    MakoTask *task;
    int64_t id;
    int active;
} MakoTaskRegSlot;
static MakoTaskRegSlot mako_task_reg[MAKO_TASK_REG_MAX];
static atomic_llong mako_task_id_seq = 1;
static atomic_llong mako_debug_break_count = 0;
static int mako_debug_trap_enabled = 0;

static inline void mako_task_reg_add(MakoTask *t) {
    if (!t) return;
    int64_t id = (int64_t)atomic_fetch_add_explicit(&mako_task_id_seq, 1, memory_order_relaxed);
    t->debug_id = id;
    t->parent_id = mako_debug_current_task_id;
    for (int i = 0; i < MAKO_TASK_REG_MAX; i++) {
        if (!mako_task_reg[i].active) {
            mako_task_reg[i].task = t;
            mako_task_reg[i].id = id;
            mako_task_reg[i].active = 1;
            return;
        }
    }
}

static inline int64_t mako_debug_set_current_task(int64_t id) {
    mako_debug_current_task_id = id;
    return id;
}

static inline int64_t mako_debug_current_task(void) {
    return mako_debug_current_task_id;
}

static inline void mako_task_reg_remove(MakoTask *t) {
    if (!t || t->debug_id == 0) return;
    for (int i = 0; i < MAKO_TASK_REG_MAX; i++) {
        if (mako_task_reg[i].active && mako_task_reg[i].id == t->debug_id) {
            mako_task_reg[i].active = 0;
            mako_task_reg[i].task = NULL;
            return;
        }
    }
}

static inline int64_t mako_task_done(MakoTask *t) {
    if (!t) return 1;
#if defined(__STDC_NO_ATOMICS__)
    return t->done ? 1 : 0;
#else
    return __atomic_load_n(&t->done, __ATOMIC_ACQUIRE) ? 1 : 0;
#endif
}

static inline int64_t mako_task_joined(MakoTask *t) {
    return (t && t->joined) ? 1 : 0;
}

static inline int64_t mako_task_id(MakoTask *t) {
    return t ? t->debug_id : 0;
}

/* JSON snapshot of registered (recently active) tasks. */
static inline MakoString mako_tasks_inspect_json(void) {
    size_t cap = 2048;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        fprintf(stderr, "mako: OOM in tasks_inspect_json\n");
        abort();
    }
    size_t len = 0;
    int n = snprintf(
        buf, cap,
        "{\"schema\":\"mako.tasks_inspect.v1\",\"tasks_spawned\":%lld,"
        "\"tasks_joined\":%lld,\"tasks\":[",
        atomic_load_explicit(&mako_rt_tasks_spawned, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_tasks_joined, memory_order_relaxed)
    );
    if (n < 0) n = 0;
    len = (size_t)n;
    int first = 1;
    for (int i = 0; i < MAKO_TASK_REG_MAX; i++) {
        if (!mako_task_reg[i].active || !mako_task_reg[i].task) continue;
        MakoTask *t = mako_task_reg[i].task;
        if (len + 160 >= cap) {
            cap *= 2;
            char *next = (char *)realloc(buf, cap);
            if (!next) {
                free(buf);
                fprintf(stderr, "mako: OOM in tasks_inspect_json grow\n");
                abort();
            }
            buf = next;
        }
        n = snprintf(
            buf + len,
            cap > len ? cap - len : 0,
            "%s{\"id\":%" PRId64 ",\"parent\":%" PRId64 ",\"done\":%d,\"joined\":%d,\"cancelled\":%d}",
            first ? "" : ",",
            t->debug_id,
            t->parent_id,
            mako_task_done(t) ? 1 : 0,
            t->joined ? 1 : 0,
            t->cancelled_start ? 1 : 0
        );
        if (n > 0) len += (size_t)n;
        first = 0;
    }
    if (len + 4 >= cap) {
        cap = len + 8;
        char *next = (char *)realloc(buf, cap);
        if (!next) {
            free(buf);
            abort();
        }
        buf = next;
    }
    buf[len++] = ']';
    buf[len++] = '}';
    buf[len] = 0;
    return (MakoString){buf, len};
}

/* Soft breakpoint seed: records a hit and logs to stderr.
 * When debug_trap_enable(1), also raises SIGTRAP (real process BP seed). */
static inline int64_t mako_debug_break(MakoString label) {
    atomic_fetch_add_explicit(&mako_debug_break_count, 1, memory_order_relaxed);
    fprintf(
        stderr,
        "mako debug_break: %.*s\n",
        (int)(label.len > 200 ? 200 : label.len),
        label.data ? label.data : ""
    );
    fflush(stderr);
#if !defined(_WIN32)
    if (mako_debug_trap_enabled) {
        raise(SIGTRAP);
    }
#endif
    return 1;
}

static inline int64_t mako_debug_break_hits(void) {
    return atomic_load_explicit(&mako_debug_break_count, memory_order_relaxed);
}

static inline int64_t mako_debug_break_reset(void) {
    atomic_store_explicit(&mako_debug_break_count, 0, memory_order_relaxed);
    return 1;
}

/* ---- Debug locals registry (seed: named int slots for async inspect) ---- */
#define MAKO_DEBUG_LOCAL_MAX 32
typedef struct {
    char name[48];
    int64_t value;
    int used;
} MakoDebugLocal;
static MakoDebugLocal mako_debug_locals[MAKO_DEBUG_LOCAL_MAX];
static int mako_debug_bp_enabled[16];

static inline int64_t mako_debug_set_int(MakoString name, int64_t value) {
    if (!name.data || name.len == 0 || name.len >= 48) return 0;
    for (int i = 0; i < MAKO_DEBUG_LOCAL_MAX; i++) {
        if (mako_debug_locals[i].used
            && strncmp(mako_debug_locals[i].name, name.data, name.len) == 0
            && mako_debug_locals[i].name[name.len] == 0) {
            mako_debug_locals[i].value = value;
            return 1;
        }
    }
    for (int i = 0; i < MAKO_DEBUG_LOCAL_MAX; i++) {
        if (!mako_debug_locals[i].used) {
            memcpy(mako_debug_locals[i].name, name.data, name.len);
            mako_debug_locals[i].name[name.len] = 0;
            mako_debug_locals[i].value = value;
            mako_debug_locals[i].used = 1;
            return 1;
        }
    }
    return 0;
}

static inline int64_t mako_debug_get_int(MakoString name) {
    if (!name.data || name.len == 0) return 0;
    for (int i = 0; i < MAKO_DEBUG_LOCAL_MAX; i++) {
        if (mako_debug_locals[i].used
            && strncmp(mako_debug_locals[i].name, name.data, name.len) == 0
            && mako_debug_locals[i].name[name.len] == 0) {
            return mako_debug_locals[i].value;
        }
    }
    return 0;
}

static inline MakoString mako_debug_locals_json(void) {
    size_t cap = 1024;
    char *buf = (char *)malloc(cap);
    if (!buf) abort();
    size_t len = 0;
    int n = snprintf(buf, cap, "{\"schema\":\"mako.debug_locals.v1\",\"locals\":[");
    if (n > 0) len = (size_t)n;
    int first = 1;
    for (int i = 0; i < MAKO_DEBUG_LOCAL_MAX; i++) {
        if (!mako_debug_locals[i].used) continue;
        if (len + 96 >= cap) {
            cap *= 2;
            char *next = (char *)realloc(buf, cap);
            if (!next) {
                free(buf);
                abort();
            }
            buf = next;
        }
        n = snprintf(
            buf + len,
            cap - len,
            "%s{\"name\":\"%s\",\"value\":%" PRId64 "}",
            first ? "" : ",",
            mako_debug_locals[i].name,
            mako_debug_locals[i].value
        );
        if (n > 0) len += (size_t)n;
        first = 0;
    }
    if (len + 4 >= cap) {
        char *next = (char *)realloc(buf, len + 8);
        if (!next) {
            free(buf);
            abort();
        }
        buf = next;
    }
    buf[len++] = ']';
    buf[len++] = '}';
    buf[len] = 0;
    return (MakoString){buf, len};
}

/* Soft breakpoints 0..15: enable/disable; debug_bp(id) hits only if enabled. */
static inline int64_t mako_debug_bp_enable(int64_t id) {
    if (id < 0 || id >= 16) return 0;
    mako_debug_bp_enabled[id] = 1;
    return 1;
}

static inline int64_t mako_debug_bp_disable(int64_t id) {
    if (id < 0 || id >= 16) return 0;
    mako_debug_bp_enabled[id] = 0;
    return 1;
}

static inline int64_t mako_debug_bp(int64_t id) {
    if (id < 0 || id >= 16 || !mako_debug_bp_enabled[id]) return 0;
    char lab[32];
    snprintf(lab, sizeof(lab), "bp-%lld", (long long)id);
    return mako_debug_break(mako_str_from_cstr(lab));
}

/* Soft trap mode: when enabled, debug_break may raise SIGTRAP (off by default). */
static inline int64_t mako_debug_trap_enable(int64_t on) {
    mako_debug_trap_enabled = on ? 1 : 0;
    return mako_debug_trap_enabled;
}
static inline int64_t mako_debug_trap_enabled_p(void) {
    return mako_debug_trap_enabled ? 1 : 0;
}

/* Source-line soft breakpoints: hit when debug_set_loc matches file+line. */
#define MAKO_DEBUG_LINE_BP_MAX 16
typedef struct {
    char file[128];
    int64_t line;
    int used;
    int hits;
} MakoDebugLineBp;
static MakoDebugLineBp mako_debug_line_bps[MAKO_DEBUG_LINE_BP_MAX];

static inline int64_t mako_debug_line_bp_set(MakoString file, int64_t line) {
    if (!file.data || file.len == 0 || file.len >= 128 || line < 1) return -1;
    for (int i = 0; i < MAKO_DEBUG_LINE_BP_MAX; i++) {
        if (mako_debug_line_bps[i].used
            && mako_debug_line_bps[i].line == line
            && strncmp(mako_debug_line_bps[i].file, file.data, file.len) == 0
            && mako_debug_line_bps[i].file[file.len] == 0) {
            return i;
        }
    }
    for (int i = 0; i < MAKO_DEBUG_LINE_BP_MAX; i++) {
        if (!mako_debug_line_bps[i].used) {
            memcpy(mako_debug_line_bps[i].file, file.data, file.len);
            mako_debug_line_bps[i].file[file.len] = 0;
            mako_debug_line_bps[i].line = line;
            mako_debug_line_bps[i].used = 1;
            mako_debug_line_bps[i].hits = 0;
            return i;
        }
    }
    return -1;
}

static inline int64_t mako_debug_line_bp_clear(int64_t id) {
    if (id < 0 || id >= MAKO_DEBUG_LINE_BP_MAX) return 0;
    mako_debug_line_bps[id].used = 0;
    mako_debug_line_bps[id].hits = 0;
    return 1;
}

static inline int64_t mako_debug_line_bp_hits(int64_t id) {
    if (id < 0 || id >= MAKO_DEBUG_LINE_BP_MAX || !mako_debug_line_bps[id].used) return 0;
    return mako_debug_line_bps[id].hits;
}

/* Called from debug_set_loc after updating TLS frame. */
static inline int64_t mako_debug_check_line_bps(const char *file, int64_t line) {
    if (!file) return 0;
    int hit = 0;
    for (int i = 0; i < MAKO_DEBUG_LINE_BP_MAX; i++) {
        if (!mako_debug_line_bps[i].used) continue;
        if (mako_debug_line_bps[i].line == line
            && strcmp(mako_debug_line_bps[i].file, file) == 0) {
            mako_debug_line_bps[i].hits++;
            hit = 1;
            char lab[160];
            snprintf(lab, sizeof(lab), "line-bp:%s:%lld", file, (long long)line);
            (void)mako_debug_break(mako_str_from_cstr(lab));
        }
    }
    return hit ? 1 : 0;
}

/* Logical frame stack (async walk seed) — push/pop source frames. */
#define MAKO_DEBUG_FRAME_STACK 32
typedef struct {
    char file[128];
    int64_t line;
    char name[64];
} MakoDebugFrame;
static __thread MakoDebugFrame mako_debug_frames[MAKO_DEBUG_FRAME_STACK];
static __thread int mako_debug_frame_sp = 0;

static inline int64_t mako_debug_push_frame(MakoString file, int64_t line, MakoString name) {
    if (mako_debug_frame_sp >= MAKO_DEBUG_FRAME_STACK) return -1;
    MakoDebugFrame *f = &mako_debug_frames[mako_debug_frame_sp++];
    size_t n = file.data && file.len < 127 ? file.len : (file.data ? 127 : 0);
    if (file.data && n) memcpy(f->file, file.data, n);
    f->file[n] = 0;
    f->line = line;
    n = name.data && name.len < 63 ? name.len : (name.data ? 63 : 0);
    if (name.data && n) memcpy(f->name, name.data, n);
    f->name[n] = 0;
    return mako_debug_frame_sp;
}

static inline int64_t mako_debug_pop_frame(void) {
    if (mako_debug_frame_sp <= 0) return 0;
    mako_debug_frame_sp--;
    return mako_debug_frame_sp;
}

static inline int64_t mako_debug_frame_depth(void) {
    return mako_debug_frame_sp;
}

static inline MakoString mako_debug_frames_json(void) {
    size_t cap = 1024;
    char *buf = (char *)malloc(cap);
    if (!buf) abort();
    size_t len = 0;
    int n = snprintf(buf, cap, "{\"schema\":\"mako.debug_frames.v1\",\"depth\":%d,\"frames\":[",
                     mako_debug_frame_sp);
    if (n > 0) len = (size_t)n;
    for (int i = 0; i < mako_debug_frame_sp; i++) {
        if (len + 200 >= cap) {
            cap *= 2;
            char *next = (char *)realloc(buf, cap);
            if (!next) {
                free(buf);
                abort();
            }
            buf = next;
        }
        n = snprintf(
            buf + len, cap - len,
            "%s{\"i\":%d,\"file\":\"%s\",\"line\":%" PRId64 ",\"name\":\"%s\"}",
            i ? "," : "", i, mako_debug_frames[i].file, mako_debug_frames[i].line,
            mako_debug_frames[i].name
        );
        if (n > 0) len += (size_t)n;
    }
    if (len + 4 >= cap) {
        char *next = (char *)realloc(buf, len + 8);
        if (!next) {
            free(buf);
            abort();
        }
        buf = next;
    }
    buf[len++] = ']';
    buf[len++] = '}';
    buf[len] = 0;
    return (MakoString){buf, len};
}

/* Combined debugger snapshot for tools / exporters. */
static inline MakoString mako_debug_snapshot_json(void) {
    MakoString tasks = mako_tasks_inspect_json();
    MakoString locals = mako_debug_locals_json();
    MakoString frames = mako_debug_frames_json();
    size_t cap = tasks.len + locals.len + frames.len + 256;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        mako_str_free(tasks);
        mako_str_free(locals);
        mako_str_free(frames);
        abort();
    }
    int n = snprintf(
        buf, cap,
        "{\"schema\":\"mako.debug_snapshot.v1\",\"current_task\":%" PRId64
        ",\"break_hits\":%" PRId64 ",\"trap\":%d,\"tasks\":%.*s,\"locals\":%.*s,\"frames\":%.*s}",
        mako_debug_current_task_id,
        atomic_load_explicit(&mako_debug_break_count, memory_order_relaxed),
        mako_debug_trap_enabled,
        (int)tasks.len, tasks.data ? tasks.data : "{}",
        (int)locals.len, locals.data ? locals.data : "{}",
        (int)frames.len, frames.data ? frames.data : "{}"
    );
    mako_str_free(tasks);
    mako_str_free(locals);
    mako_str_free(frames);
    if (n < 0) {
        free(buf);
        return mako_str_from_cstr("{}");
    }
    return (MakoString){buf, (size_t)n};
}

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
    /* Structured child errors: first Err message from joined Result jobs. */
    char *first_err;   /* heap C string, or NULL */
    size_t first_err_len;
    int64_t err_count;
} MakoNursery;

static inline MakoNursery mako_nursery_new(void) {
    MakoNursery n;
    n.tasks = NULL;
    n.len = 0;
    n.cap = 0;
    n.cancelled = false;
    n.first_err = NULL;
    n.first_err_len = 0;
    n.err_count = 0;
    return n;
}

static inline void mako_nursery_cancel(MakoNursery *n) {
    n->cancelled = true;
}

static inline int64_t mako_nursery_cancelled(MakoNursery *n) {
    return n->cancelled ? 1 : 0;
}

/* Record a child task error (clones msg). Keeps only the first message. */
static inline void mako_nursery_note_err(MakoNursery *n, MakoString msg) {
    if (!n) return;
    n->err_count++;
    if (n->first_err) return;
    size_t len = msg.len;
    char *d = (char *)malloc(len + 1);
    if (!d) return;
    if (len && msg.data) memcpy(d, msg.data, len);
    d[len] = 0;
    n->first_err = d;
    n->first_err_len = len;
}

static inline int64_t mako_nursery_err_count(MakoNursery *n) {
    return n ? n->err_count : 0;
}

static inline MakoString mako_nursery_first_err(MakoNursery *n) {
    if (!n || !n->first_err) return mako_str_from_cstr("");
    return mako_str_from_cstr(n->first_err);
}

/* After joins: 1 if no child errors, 0 if any (caller reads first_err). */
static inline int64_t mako_nursery_ok(MakoNursery *n) {
    return (n && n->err_count == 0) ? 1 : 0;
}

/* Free first_err buffer (tasks free is separate). */
static inline void mako_nursery_clear_errs(MakoNursery *n) {
    if (!n) return;
    free(n->first_err);
    n->first_err = NULL;
    n->first_err_len = 0;
    n->err_count = 0;
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
    t->debug_id = 0;
    t->parent_id = 0;
    mako_rt_counter_inc(&mako_rt_tasks_spawned);
    if (n->cancelled) {
        /* do not start new work after cancel */
        t->joined = true;
        t->done = 1;
        t->result = (void *)(intptr_t)0;
        return t;
    }
    pthread_create(&t->thread, NULL, mako_task_trampoline, t);
    mako_task_reg_add(t);
    return t;
}

static inline void *mako_await(MakoTask *t) {
    if (!t->joined) {
        pthread_join(t->thread, &t->result);
        t->joined = true;
        t->done = 1;
        mako_rt_counter_inc(&mako_rt_tasks_joined);
        mako_task_reg_remove(t);
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
            mako_task_reg_remove(t);
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
        mako_task_reg_remove(t);
        if (out) *out = (int64_t)(intptr_t)t->result;
        return 1;
    }
    if (out) *out = 0;
    return 0; /* still running — caller may later join or cancel_join */
}

static inline void mako_nursery_join_pending(MakoNursery *n) {
    if (!n) return;
    for (size_t i = 0; i < n->len; i++) {
        if (!n->tasks[i].joined) {
            mako_await(&n->tasks[i]);
        }
    }
}

static inline void mako_nursery_join_all(MakoNursery *n) {
    for (size_t i = 0; i < n->len; i++) {
        mako_await(&n->tasks[i]);
    }
    free(n->first_err);
    n->first_err = NULL;
    n->first_err_len = 0;
    free(n->tasks);
    n->tasks = NULL;
    n->len = n->cap = 0;
}

/* ---- Detached tasks: process-scoped nursery joined on demand ---- */
static MakoNursery mako_detached_root;
static int mako_detached_inited = 0;
static pthread_mutex_t mako_detached_mu = PTHREAD_MUTEX_INITIALIZER;

static inline void mako_detached_init(void) {
    if (mako_detached_inited) return;
    mako_detached_root = mako_nursery_new();
    mako_detached_inited = 1;
}

static inline MakoTask *mako_detach_spawn(MakoTaskFn fn, void *arg) {
    pthread_mutex_lock(&mako_detached_mu);
    mako_detached_init();
    MakoTask *t = mako_spawn(&mako_detached_root, fn, arg);
    pthread_mutex_unlock(&mako_detached_mu);
    return t;
}

static inline void mako_detached_join_all(void) {
    pthread_mutex_lock(&mako_detached_mu);
    if (mako_detached_inited) {
        mako_nursery_join_pending(&mako_detached_root);
        mako_nursery_clear_errs(&mako_detached_root);
        free(mako_detached_root.tasks);
        mako_detached_root.tasks = NULL;
        mako_detached_root.len = mako_detached_root.cap = 0;
    }
    pthread_mutex_unlock(&mako_detached_mu);
}

/* Cancel then join — tasks cannot outlive cancel policy (structured concurrency). */
static inline void mako_nursery_cancel_join(MakoNursery *n) {
    mako_nursery_cancel(n);
    mako_nursery_join_all(n);
}

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
#include <dirent.h>
#include <fcntl.h>
#endif

/* Copy path into C buffer; reject empty, too-long, or embedded NUL. */
static inline int mako_fs_path_buf(MakoString path, char *buf, size_t cap) {
    if (!buf || cap < 2 || !path.data || path.len == 0 || path.len >= cap) return -1;
    for (size_t i = 0; i < path.len; i++) {
        if (path.data[i] == '\0') return -1;
    }
    memcpy(buf, path.data, path.len);
    buf[path.len] = 0;
    return 0;
}

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
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return -1;
#if defined(_WIN32)
    if (_mkdir(pbuf) == 0) return 0;
#else
    if (mkdir(pbuf, 0755) == 0) return 0;
#endif
    if (errno == EEXIST) return 0;
    return -1;
}

static inline bool mako_file_exists(MakoString path) {
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return false;
    struct stat st;
    return stat(pbuf, &st) == 0;
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
    /* Case-specific letter categories before coarse Letter. */
    if (nlen == 2 && memcmp(name, "Lu", 2) == 0)
        return (cp >= 'A' && cp <= 'Z') || (cp >= 0x00C0 && cp <= 0x00D6)
            || (cp >= 0x00D8 && cp <= 0x00DE);
    if (nlen == 2 && memcmp(name, "Ll", 2) == 0)
        return (cp >= 'a' && cp <= 'z') || (cp >= 0x00DF && cp <= 0x00F6)
            || (cp >= 0x00F8 && cp <= 0x00FF);
    if (nlen == 2 && memcmp(name, "Lt", 2) == 0)
        return 0; /* titlecase seed */
    if (nlen == 2 && memcmp(name, "Lo", 2) == 0)
        return (cp >= 0x3400 && cp <= 0x9FFF);
    if ((nlen == 1 && name[0] == 'L') || (nlen == 6 && memcmp(name, "Letter", 6) == 0)
        || (nlen == 2 && memcmp(name, "L&", 2) == 0))
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
    if (nlen == 5 && memcmp(name, "Tamil", 5) == 0)
        return (cp >= 0x0B80 && cp <= 0x0BFF);
    if (nlen == 8 && memcmp(name, "Armenian", 8) == 0)
        return (cp >= 0x0530 && cp <= 0x058F);
    if (nlen == 8 && memcmp(name, "Ethiopic", 8) == 0)
        return (cp >= 0x1200 && cp <= 0x137F);
    if (nlen == 8 && memcmp(name, "Georgian", 8) == 0)
        return (cp >= 0x10A0 && cp <= 0x10FF);
    if (nlen == 8 && memcmp(name, "Cherokee", 8) == 0)
        return (cp >= 0x13A0 && cp <= 0x13FF);
    if (nlen == 7 && memcmp(name, "Bengali", 7) == 0)
        return (cp >= 0x0980 && cp <= 0x09FF);
    if (nlen == 7 && memcmp(name, "Sinhala", 7) == 0)
        return (cp >= 0x0D80 && cp <= 0x0DFF);
    if (nlen == 7 && memcmp(name, "Myanmar", 7) == 0)
        return (cp >= 0x1000 && cp <= 0x109F);
    if (nlen == 5 && memcmp(name, "Khmer", 5) == 0)
        return (cp >= 0x1780 && cp <= 0x17FF);
    if (nlen == 7 && memcmp(name, "Tibetan", 7) == 0)
        return (cp >= 0x0F00 && cp <= 0x0FFF);
    if (nlen == 6 && memcmp(name, "Syriac", 6) == 0)
        return (cp >= 0x0700 && cp <= 0x074F);
    if (nlen == 6 && memcmp(name, "Coptic", 6) == 0)
        return (cp >= 0x2C80 && cp <= 0x2CFF) || (cp >= 0x03E2 && cp <= 0x03EF);
    if (nlen == 5 && memcmp(name, "Runic", 5) == 0)
        return (cp >= 0x16A0 && cp <= 0x16FF);
    if (nlen == 6 && memcmp(name, "Thaana", 6) == 0)
        return (cp >= 0x0780 && cp <= 0x07BF);
    if (nlen == 7 && memcmp(name, "Tagalog", 7) == 0)
        return (cp >= 0x1700 && cp <= 0x171F);
    if (nlen == 8 && memcmp(name, "Bopomofo", 8) == 0)
        return (cp >= 0x3100 && cp <= 0x312F);
    if (nlen == 7 && memcmp(name, "Braille", 7) == 0)
        return (cp >= 0x2800 && cp <= 0x28FF);
    if (nlen == 5 && memcmp(name, "Ogham", 5) == 0)
        return (cp >= 0x1680 && cp <= 0x169F);
    if (nlen == 6 && memcmp(name, "Gothic", 6) == 0)
        return (cp >= 0x10330 && cp <= 0x1034F);
    if (nlen == 7 && memcmp(name, "Deseret", 7) == 0)
        return (cp >= 0x10400 && cp <= 0x1044F);
    if (nlen == 10 && memcmp(name, "Phoenician", 10) == 0)
        return (cp >= 0x10900 && cp <= 0x1091F);
    /* BMP fallbacks for tests without astral UTF-8 decode */
    if (nlen == 8 && memcmp(name, "Canadian", 8) == 0)
        return (cp >= 0x1400 && cp <= 0x167F);
    if (nlen == 8 && memcmp(name, "Gujarati", 8) == 0)
        return (cp >= 0x0A80 && cp <= 0x0AFF);
    if (nlen == 7 && memcmp(name, "Kannada", 7) == 0)
        return (cp >= 0x0C80 && cp <= 0x0CFF);
    if (nlen == 9 && memcmp(name, "Malayalam", 9) == 0)
        return (cp >= 0x0D00 && cp <= 0x0D7F);
    if (nlen == 6 && memcmp(name, "Telugu", 6) == 0)
        return (cp >= 0x0C00 && cp <= 0x0C7F);
    if (nlen == 5 && memcmp(name, "Oriya", 5) == 0)
        return (cp >= 0x0B00 && cp <= 0x0B7F);
    if (nlen == 3 && memcmp(name, "Lao", 3) == 0)
        return (cp >= 0x0E80 && cp <= 0x0EFF);
    if (nlen == 8 && memcmp(name, "Balinese", 8) == 0)
        return (cp >= 0x1B00 && cp <= 0x1B7F);
    if (nlen == 8 && memcmp(name, "Javanese", 8) == 0)
        return (cp >= 0xA980 && cp <= 0xA9DF);
    if (nlen == 9 && memcmp(name, "Sundanese", 9) == 0)
        return (cp >= 0x1B80 && cp <= 0x1BBF);
    if (nlen == 8 && memcmp(name, "Buginese", 8) == 0)
        return (cp >= 0x1A00 && cp <= 0x1A1F);
    if (nlen == 4 && memcmp(name, "Cham", 4) == 0)
        return (cp >= 0xAA00 && cp <= 0xAA5F);
    if (nlen == 5 && memcmp(name, "Rejang", 5) == 0)
        return (cp >= 0xA930 && cp <= 0xA95F);
    if (nlen == 4 && memcmp(name, "Lisu", 4) == 0)
        return (cp >= 0xA4D0 && cp <= 0xA4FF);
    if (nlen == 3 && memcmp(name, "Nko", 3) == 0)
        return (cp >= 0x07C0 && cp <= 0x07FF);
    if (nlen == 8 && memcmp(name, "Tifinagh", 8) == 0)
        return (cp >= 0x2D30 && cp <= 0x2D7F);
    if (nlen == 9 && memcmp(name, "Samaritan", 9) == 0)
        return (cp >= 0x0800 && cp <= 0x083F);
    if (nlen == 7 && memcmp(name, "Mandaic", 7) == 0)
        return (cp >= 0x0840 && cp <= 0x085F);
    if (nlen == 10 && memcmp(name, "Saurashtra", 10) == 0)
        return (cp >= 0xA880 && cp <= 0xA8DF);
    if (nlen == 6 && memcmp(name, "Tai_Le", 6) == 0)
        return (cp >= 0x1950 && cp <= 0x197F);
    if (nlen == 8 && memcmp(name, "Kayah_Li", 8) == 0)
        return (cp >= 0xA900 && cp <= 0xA92F);
    if (nlen == 11 && memcmp(name, "New_Tai_Lue", 11) == 0)
        return (cp >= 0x1980 && cp <= 0x19DF);
    if (nlen == 8 && memcmp(name, "Ol_Chiki", 8) == 0)
        return (cp >= 0x1C50 && cp <= 0x1C7F);
    if (nlen == 5 && memcmp(name, "Limbu", 5) == 0)
        return (cp >= 0x1900 && cp <= 0x194F);
    if (nlen == 6 && memcmp(name, "Lepcha", 6) == 0)
        return (cp >= 0x1C00 && cp <= 0x1C4F);
    if (nlen == 5 && memcmp(name, "Batak", 5) == 0)
        return (cp >= 0x1BC0 && cp <= 0x1BFF);
    if (nlen == 8 && memcmp(name, "Tai_Tham", 8) == 0)
        return (cp >= 0x1A20 && cp <= 0x1AAF);
    if (nlen == 12 && memcmp(name, "Syloti_Nagri", 12) == 0)
        return (cp >= 0xA800 && cp <= 0xA82F);
    if (nlen == 3 && memcmp(name, "Vai", 3) == 0)
        return (cp >= 0xA500 && cp <= 0xA63F);
    if (nlen == 2 && memcmp(name, "Yi", 2) == 0)
        return (cp >= 0xA000 && cp <= 0xA48F);
    if (nlen == 10 && memcmp(name, "Glagolitic", 10) == 0)
        return (cp >= 0x2C00 && cp <= 0x2C5F);
    if (nlen == 12 && memcmp(name, "Meetei_Mayek", 12) == 0)
        return (cp >= 0xABC0 && cp <= 0xABFF);
    if (nlen == 8 && memcmp(name, "Phags_Pa", 8) == 0)
        return (cp >= 0xA840 && cp <= 0xA87F);
    if (nlen == 5 && memcmp(name, "Buhid", 5) == 0)
        return (cp >= 0x1740 && cp <= 0x175F);
    if (nlen == 7 && memcmp(name, "Hanunoo", 7) == 0)
        return (cp >= 0x1720 && cp <= 0x173F);
    if (nlen == 8 && memcmp(name, "Tagbanwa", 8) == 0)
        return (cp >= 0x1760 && cp <= 0x177F);
    if (nlen == 5 && memcmp(name, "Bamum", 5) == 0)
        return (cp >= 0xA6A0 && cp <= 0xA6FF);
    if (nlen == 9 && memcmp(name, "Mongolian", 9) == 0)
        return (cp >= 0x1800 && cp <= 0x18AF);
    if (nlen == 8 && memcmp(name, "Tai_Viet", 8) == 0)
        return (cp >= 0xAA80 && cp <= 0xAADF);
    if (nlen == 9 && memcmp(name, "Inherited", 9) == 0)
        return (cp >= 0x0300 && cp <= 0x036F); /* combining marks seed */
    if (nlen == 6 && memcmp(name, "Common", 6) == 0)
        return mako_re_unicode_is_digit(cp)
            || mako_re_unicode_is_space(cp)
            || (cp >= 0x2000 && cp <= 0x206F); /* digits/space/general punct seed */
    if (nlen == 2 && memcmp(name, "Mn", 2) == 0) /* nonspacing marks */
        return (cp >= 0x0300 && cp <= 0x036F);
    if (nlen == 2 && memcmp(name, "Mc", 2) == 0) /* spacing combining marks seed */
        return (cp >= 0x093E && cp <= 0x0940) || cp == 0x0903; /* Devanagari Mc */
    if (nlen == 2 && memcmp(name, "Sm", 2) == 0) /* math symbols seed */
        return (cp >= 0x2200 && cp <= 0x22FF) || cp == '+' || cp == '=' || cp == '|'
            || cp == '~' || cp == '<' || cp == '>';
    if (nlen == 2 && memcmp(name, "Sk", 2) == 0) /* modifier symbols seed */
        return cp == '^' || cp == '`' || (cp >= 0x02B0 && cp <= 0x02FF);
    if (nlen == 2 && memcmp(name, "Pc", 2) == 0) /* connector punctuation seed */
        return cp == '_' || (cp >= 0x203F && cp <= 0x2040);
    /* Extra PCRE/Unicode seeds (full property database still residual). */
    if ((nlen == 3 && memcmp(name, "Any", 3) == 0) || (nlen == 1 && name[0] == 'C'))
        return 1; /* \p{Any} / coarse Cn/C seed */
    if (nlen == 5 && memcmp(name, "ASCII", 5) == 0)
        return cp <= 0x7F;
    if (nlen == 8 && memcmp(name, "Assigned", 8) == 0)
        return cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
    if (nlen == 2 && memcmp(name, "Lu", 2) == 0) /* uppercase letters seed */
        return (cp >= 'A' && cp <= 'Z') || (cp >= 0x00C0 && cp <= 0x00D6)
            || (cp >= 0x00D8 && cp <= 0x00DE);
    if (nlen == 2 && memcmp(name, "Ll", 2) == 0) /* lowercase letters seed */
        return (cp >= 'a' && cp <= 'z') || (cp >= 0x00DF && cp <= 0x00F6)
            || (cp >= 0x00F8 && cp <= 0x00FF);
    if (nlen == 2 && memcmp(name, "Lt", 2) == 0) /* titlecase seed */
        return 0;
    if (nlen == 2 && memcmp(name, "Lo", 2) == 0) /* other letters: Han seed */
        return (cp >= 0x3400 && cp <= 0x9FFF);
    if (nlen == 2 && memcmp(name, "Ps", 2) == 0) /* open punctuation */
        return cp == '(' || cp == '[' || cp == '{' || cp == 0x201C;
    if (nlen == 2 && memcmp(name, "Pe", 2) == 0) /* close punctuation */
        return cp == ')' || cp == ']' || cp == '}' || cp == 0x201D;
    /* Additional scripts / blocks for UCD depth. */
    if (nlen == 8 && memcmp(name, "Cuneiform", 8) == 0)
        return (cp >= 0x12000 && cp <= 0x123FF);
    if (nlen == 8 && memcmp(name, "Egyptian", 8) == 0)
        return (cp >= 0x13000 && cp <= 0x1342F);
    if (nlen == 5 && memcmp(name, "Emoji", 5) == 0)
        return (cp >= 0x1F300 && cp <= 0x1F5FF) || (cp >= 0x1F600 && cp <= 0x1F64F)
            || (cp >= 0x2600 && cp <= 0x26FF);
    if (nlen == 5 && memcmp(name, "Math", 5) == 0)
        return (cp >= 0x2200 && cp <= 0x22FF) || cp == '+' || cp == '=' || cp == 0x00D7;
    if (nlen == 5 && memcmp(name, "Blank", 5) == 0)
        return cp == ' ' || cp == '\t';
    if (nlen == 5 && memcmp(name, "Space", 5) == 0)
        return mako_re_unicode_is_space(cp);
    if (nlen == 5 && memcmp(name, "Alnum", 5) == 0)
        return mako_re_unicode_is_letter(cp) || mako_re_unicode_is_digit(cp);
    if (nlen == 5 && memcmp(name, "Alpha", 5) == 0)
        return mako_re_unicode_is_letter(cp);
    if (nlen == 5 && memcmp(name, "Digit", 5) == 0)
        return mako_re_unicode_is_digit(cp);
    if (nlen == 5 && memcmp(name, "XDigit", 5) == 0)
        return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'f')
            || (cp >= 'A' && cp <= 'F');
    if (nlen == 5 && memcmp(name, "Cntrl", 5) == 0)
        return cp < 0x20 || cp == 0x7F;
    if (nlen == 5 && memcmp(name, "Graph", 5) == 0)
        return cp > 0x20 && cp != 0x7F;
    if (nlen == 5 && memcmp(name, "Print", 5) == 0)
        return cp >= 0x20 && cp != 0x7F;
    if (nlen == 5 && memcmp(name, "Punct", 5) == 0)
        return mako_re_unicode_is_punct(cp);
    if (nlen == 4 && memcmp(name, "Word", 4) == 0)
        return mako_re_unicode_is_letter(cp) || mako_re_unicode_is_digit(cp) || cp == '_';
    if (nlen == 9 && memcmp(name, "Whitespace", 9) == 0)
        return mako_re_unicode_is_space(cp);
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
        case 'p': case 'P': {
            /* \p{Prop} / \P{Prop} (negated) */
            if (plen < 5 || pat[2] != '{') { *alen = 2; ok = 0; break; }
            size_t j = 3;
            while (j < plen && pat[j] != '}') j++;
            if (j >= plen) { *alen = plen; return 0; }
            *alen = j + 1;
            size_t cp_len = 0;
            uint32_t cp = mako_re_utf8_decode(text, tlen, &cp_len);
            ok = mako_re_unicode_prop_match(pat + 3, j - 3, cp);
            if (pat[1] == 'P') ok = !ok;
            if (ok) override_consumed = cp_len;
            break;
        }
        case 'X': {
            /* \X — extended grapheme cluster seed: one UTF-8 code point */
            size_t cp_len = 0;
            (void)mako_re_utf8_decode(text, tlen, &cp_len);
            ok = cp_len > 0;
            if (ok) override_consumed = cp_len;
            break;
        }
        case 'h': /* horizontal whitespace */
            ok = (ch == ' ' || ch == '\t');
            break;
        case 'H':
            ok = !(ch == ' ' || ch == '\t');
            break;
        case 'R': {
            /* \R — any unicode newline seed */
            if (ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v') ok = 1;
            else if ((unsigned char)ch == 0xC2 && tlen >= 2
                     && (unsigned char)text[1] == 0x85) {
                /* U+0085 NEL */
                ok = 1; override_consumed = 2;
            } else ok = 0;
            break;
        }
        case 'N': {
            /* \N — any char except newline (PCRE-ish seed) */
            ok = (ch != '\n' && ch != '\r');
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
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return -1;
#if defined(_WIN32)
    return _unlink(pbuf) == 0 ? 0 : -1;
#else
    return unlink(pbuf) == 0 ? 0 : -1;
#endif
}

static inline MakoString mako_read_file(MakoString path) {
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) {
        return mako_str_from_cstr("");
    }
    FILE *f = fopen(pbuf, "rb");
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
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return -1;
    FILE *f = fopen(pbuf, "wb");
    if (!f) return -1;
    size_t n = contents.len;
    size_t w = n ? fwrite(contents.data, 1, n, f) : 0;
    fclose(f);
    return (w == n) ? 0 : -1;
}

/* Append bytes (create if missing). Used by append-only logs / mini engines. */
static inline int64_t mako_append_file(MakoString path, MakoString contents) {
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return -1;
    FILE *f = fopen(pbuf, "ab");
    if (!f) return -1;
    size_t n = contents.len;
    size_t w = n ? fwrite(contents.data, 1, n, f) : 0;
    fclose(f);
    return (w == n) ? 0 : -1;
}

/* ---- Production filesystem / storage helpers ---- */
/* mako_fs_path_buf is defined earlier (with sys/stat includes). */

/* Rename / move within the same filesystem. Returns 0 ok, -1 error. */
static inline int64_t mako_rename(MakoString old_path, MakoString new_path) {
    char a[4096], b[4096];
    if (mako_fs_path_buf(old_path, a, sizeof(a)) < 0) return -1;
    if (mako_fs_path_buf(new_path, b, sizeof(b)) < 0) return -1;
#if defined(_WIN32)
    return MoveFileExA(a, b, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
#else
    return rename(a, b) == 0 ? 0 : -1;
#endif
}

/* Create directory and all missing parents (like mkdir -p). Returns 0 ok, -1 error. */
static inline int64_t mako_mkdir_all(MakoString path) {
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return -1;
    size_t n = strlen(pbuf);
    if (n == 0) return -1;
    /* Walk components; skip leading slash. */
    for (size_t i = 1; i <= n; i++) {
        if (pbuf[i] != '/' && pbuf[i] != '\\' && pbuf[i] != 0) continue;
        char save = pbuf[i];
        pbuf[i] = 0;
        if (pbuf[0] != 0) {
#if defined(_WIN32)
            if (_mkdir(pbuf) != 0 && errno != EEXIST) {
                /* drive roots etc. */
                struct stat st;
                if (stat(pbuf, &st) != 0 || !(st.st_mode & _S_IFDIR)) {
                    pbuf[i] = save;
                    return -1;
                }
            }
#else
            if (mkdir(pbuf, 0755) != 0 && errno != EEXIST) {
                struct stat st;
                if (stat(pbuf, &st) != 0 || !S_ISDIR(st.st_mode)) {
                    pbuf[i] = save;
                    return -1;
                }
            }
#endif
        }
        pbuf[i] = save;
    }
    return 0;
}

/* Remove empty directory. */
static inline int64_t mako_rmdir(MakoString path) {
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return -1;
#if defined(_WIN32)
    return _rmdir(pbuf) == 0 ? 0 : -1;
#else
    return rmdir(pbuf) == 0 ? 0 : -1;
#endif
}

/* True if path is a regular file. */
static inline int64_t mako_is_file(MakoString path) {
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return 0;
    struct stat st;
    if (stat(pbuf, &st) != 0) return 0;
#if defined(_WIN32)
    return (st.st_mode & _S_IFREG) ? 1 : 0;
#else
    return S_ISREG(st.st_mode) ? 1 : 0;
#endif
}

/* File size by path (-1 if missing/error). */
static inline int64_t mako_path_size(MakoString path) {
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return -1;
    struct stat st;
    if (stat(pbuf, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

/* Modification time as Unix seconds (-1 if missing). */
static inline int64_t mako_file_mtime(MakoString path) {
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return -1;
    struct stat st;
    if (stat(pbuf, &st) != 0) return -1;
#if defined(_WIN32)
    return (int64_t)st.st_mtime;
#else
    return (int64_t)st.st_mtime;
#endif
}

/* chmod mode bits (e.g. 0644). Returns 0 ok. */
static inline int64_t mako_chmod(MakoString path, int64_t mode) {
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return -1;
#if defined(_WIN32)
    return _chmod(pbuf, (int)mode) == 0 ? 0 : -1;
#else
    return chmod(pbuf, (mode_t)mode) == 0 ? 0 : -1;
#endif
}

/* Copy file contents (src → dst). Overwrites dst. Returns 0 ok. */
static inline int64_t mako_copy_file(MakoString src, MakoString dst) {
    char a[4096], b[4096];
    if (mako_fs_path_buf(src, a, sizeof(a)) < 0) return -1;
    if (mako_fs_path_buf(dst, b, sizeof(b)) < 0) return -1;
    FILE *in = fopen(a, "rb");
    if (!in) return -1;
    FILE *out = fopen(b, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[65536];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    }
    if (ferror(in)) ok = 0;
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    return ok ? 0 : -1;
}

/* Atomic write: temp file in same dir + fsync + rename. Crash-safe for configs/logs. */
static inline int64_t mako_atomic_write_file(MakoString path, MakoString contents) {
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return -1;
    /* Build temp path: <path>.tmp.<pid> */
    char tbuf[4224];
#if defined(_WIN32)
    snprintf(tbuf, sizeof(tbuf), "%s.tmp.%u", pbuf, (unsigned)_getpid());
#else
    snprintf(tbuf, sizeof(tbuf), "%s.tmp.%ld", pbuf, (long)getpid());
#endif
    FILE *f = fopen(tbuf, "wb");
    if (!f) return -1;
    size_t n = contents.data ? contents.len : 0;
    size_t w = n ? fwrite(contents.data, 1, n, f) : 0;
    if (w != n) { fclose(f); remove(tbuf); return -1; }
    fflush(f);
#if !defined(_WIN32)
    int fd = fileno(f);
    if (fd >= 0) {
#if defined(__APPLE__)
#ifndef F_FULLFSYNC
#define F_FULLFSYNC 51
#endif
        (void)fcntl(fd, F_FULLFSYNC);
#else
        (void)fsync(fd);
#endif
    }
#endif
    if (fclose(f) != 0) { remove(tbuf); return -1; }
#if defined(_WIN32)
    if (!MoveFileExA(tbuf, pbuf, MOVEFILE_REPLACE_EXISTING)) {
        remove(tbuf);
        return -1;
    }
#else
    if (rename(tbuf, pbuf) != 0) {
        remove(tbuf);
        return -1;
    }
#endif
    return 0;
}

/* System temp directory path (e.g. /tmp or $TMPDIR). */
static inline MakoString mako_temp_dir(void) {
#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof(buf), buf);
    if (n == 0 || n >= sizeof(buf)) return mako_str_from_cstr(".");
    /* Strip trailing slash for consistency. */
    while (n > 1 && (buf[n - 1] == '\\' || buf[n - 1] == '/')) {
        buf[--n] = 0;
    }
    return mako_str_from_cstr(buf);
#else
    const char *t = getenv("TMPDIR");
    if (!t || !t[0]) t = getenv("TMP");
    if (!t || !t[0]) t = "/tmp";
    return mako_str_from_cstr(t);
#endif
}

/* Create a unique temp file path (empty file created). prefix may be empty. */
static inline MakoString mako_temp_file(MakoString prefix) {
    MakoString dir = mako_temp_dir();
    char pfx[256];
    size_t pl = prefix.data ? prefix.len : 0;
    if (pl >= sizeof(pfx)) pl = sizeof(pfx) - 1;
    if (pl && prefix.data) memcpy(pfx, prefix.data, pl);
    pfx[pl] = 0;
    if (pl == 0) { memcpy(pfx, "mako", 4); pfx[4] = 0; }

    char path[4224];
#if defined(_WIN32)
    snprintf(path, sizeof(path), "%s\\%s-%u-%u.tmp",
             dir.data ? dir.data : ".", pfx, (unsigned)_getpid(),
             (unsigned)(time(NULL) & 0xfffffff));
    mako_str_free(dir);
    FILE *f = fopen(path, "wb");
    if (!f) return mako_str_from_cstr("");
    fclose(f);
    return mako_str_from_cstr(path);
#else
    snprintf(path, sizeof(path), "%s/%s-XXXXXX", dir.data ? dir.data : "/tmp", pfx);
    mako_str_free(dir);
    int fd = mkstemp(path);
    if (fd < 0) return mako_str_from_cstr("");
    close(fd);
    return mako_str_from_cstr(path);
#endif
}

/* Symlink: create link_path → target. */
static inline int64_t mako_symlink(MakoString target, MakoString link_path) {
#if defined(_WIN32)
    (void)target; (void)link_path;
    return -1; /* needs CreateSymbolicLink privileges */
#else
    char t[4096], l[4096];
    if (mako_fs_path_buf(target, t, sizeof(t)) < 0) return -1;
    if (mako_fs_path_buf(link_path, l, sizeof(l)) < 0) return -1;
    return symlink(t, l) == 0 ? 0 : -1;
#endif
}

/* Read symlink contents (empty on error). */
static inline MakoString mako_readlink(MakoString path) {
#if defined(_WIN32)
    (void)path;
    return mako_str_from_cstr("");
#else
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return mako_str_from_cstr("");
    char out[4096];
    ssize_t n = readlink(pbuf, out, sizeof(out) - 1);
    if (n < 0) return mako_str_from_cstr("");
    out[n] = 0;
    return mako_str_from_cstr(out);
#endif
}

/* Resolve absolute path (realpath). Empty on error. */
static inline MakoString mako_realpath(MakoString path) {
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return mako_str_from_cstr("");
#if defined(_WIN32)
    char out[4096];
    DWORD n = GetFullPathNameA(pbuf, (DWORD)sizeof(out), out, NULL);
    if (n == 0 || n >= sizeof(out)) return mako_str_from_cstr("");
    return mako_str_from_cstr(out);
#else
    char *r = realpath(pbuf, NULL);
    if (!r) return mako_str_from_cstr("");
    MakoString s = mako_str_from_cstr(r);
    free(r);
    return s;
#endif
}

/* Recursive remove file or directory tree. Depth-limited. Returns 0 ok. */
static inline int64_t mako_remove_all_depth(const char *path, int depth) {
    if (!path || !path[0] || depth > 64) return -1;
    struct stat st;
#if defined(_WIN32)
    if (stat(path, &st) != 0) {
        return errno == ENOENT ? 0 : -1;
    }
    if (!(st.st_mode & _S_IFDIR)) {
        return _unlink(path) == 0 ? 0 : -1;
    }
    char pattern[4224];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            char child[4224];
            snprintf(child, sizeof(child), "%s\\%s", path, fd.cFileName);
            if (mako_remove_all_depth(child, depth + 1) != 0) {
                FindClose(h);
                return -1;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return _rmdir(path) == 0 ? 0 : -1;
#else
    if (lstat(path, &st) != 0) {
        return errno == ENOENT ? 0 : -1;
    }
    if (S_ISLNK(st.st_mode) || S_ISREG(st.st_mode)) {
        return unlink(path) == 0 ? 0 : -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path) == 0 ? 0 : -1;
    }
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char child[4224];
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        if (mako_remove_all_depth(child, depth + 1) != 0) {
            closedir(d);
            return -1;
        }
    }
    closedir(d);
    return rmdir(path) == 0 ? 0 : -1;
#endif
}

static inline int64_t mako_remove_all(MakoString path) {
    char pbuf[4096];
    if (mako_fs_path_buf(path, pbuf, sizeof(pbuf)) < 0) return -1;
    /* Refuse obviously dangerous roots. */
    if (strcmp(pbuf, "/") == 0 || strcmp(pbuf, ".") == 0 || strcmp(pbuf, "..") == 0)
        return -1;
    if (strcmp(pbuf, "") == 0) return -1;
    return mako_remove_all_depth(pbuf, 0);
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

/* ---- Clocks (low-latency) ----
 *
 * Wall (REALTIME): calendar time; can jump with NTP/slew. Use for logs/deadlines
 *                  that must match wall clocks.
 * Mono (MONOTONIC[_RAW]): steady; never goes backwards. Use for latency, budgets,
 *                         timeouts, benchmarks.
 *
 * Prefer mono_* for all elapsed-time / low-latency measurements.
 */

/* Wall-clock nanoseconds (CLOCK_REALTIME). */
static inline int64_t mako_wall_ns(void) {
    struct timespec ts;
#if defined(CLOCK_REALTIME)
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    }
#endif
    struct timeval tv;
    mako_gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000000LL + (int64_t)tv.tv_usec * 1000LL;
}

static inline int64_t mako_wall_ms(void) {
    return mako_wall_ns() / 1000000LL;
}

static inline int64_t mako_wall_us(void) {
    return mako_wall_ns() / 1000LL;
}

/* Monotonic nanoseconds. Prefer CLOCK_MONOTONIC_RAW (no NTP slew) when available. */
static inline int64_t mako_mono_ns(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    /* ns = counter * 1e9 / freq */
    return (int64_t)((now.QuadPart * 1000000000LL) / freq.QuadPart);
#else
    struct timespec ts;
#if defined(CLOCK_MONOTONIC_RAW)
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) == 0) {
        return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    }
#endif
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    }
#endif
    struct timeval tv;
    mako_gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000000LL + (int64_t)tv.tv_usec * 1000LL;
#endif
}

static inline int64_t mako_mono_us(void) {
    return mako_mono_ns() / 1000LL;
}

static inline int64_t mako_mono_ms(void) {
    return mako_mono_ns() / 1000000LL;
}

/* Clock resolution in ns (best-effort). */
static inline int64_t mako_mono_res_ns(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    if (freq.QuadPart <= 0) return 1000;
    return 1000000000LL / freq.QuadPart;
#else
    struct timespec ts;
#if defined(CLOCK_MONOTONIC_RAW)
    if (clock_getres(CLOCK_MONOTONIC_RAW, &ts) == 0) {
        return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    }
#endif
#if defined(CLOCK_MONOTONIC)
    if (clock_getres(CLOCK_MONOTONIC, &ts) == 0) {
        return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    }
#endif
    return 1000; /* 1 µs fallback */
#endif
}

/* Compat: now_ms = wall ms (logs, unix conversion). now_ns = mono ns (latency). */
static inline int64_t mako_now_ms(void) {
    return mako_wall_ms();
}

static inline int64_t mako_now_ns(void) {
    return mako_mono_ns();
}

/* ---- Stack traces (symbolized when execinfo available) ---- */
static inline MakoString mako_stack_trace(void) {
#if defined(__GLIBC__) || defined(__APPLE__)
    void *frames[48];
    int n = backtrace(frames, 48);
    if (n <= 0) return mako_str_from_cstr("");
    char **syms = backtrace_symbols(frames, n);
    if (!syms) return mako_str_from_cstr("");
    size_t cap = 2048;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        free(syms);
        fprintf(stderr, "mako: OOM in stack_trace\n");
        abort();
    }
    size_t len = 0;
    for (int i = 0; i < n; i++) {
        const char *s = syms[i] ? syms[i] : "?";
        size_t sl = strlen(s);
        if (len + sl + 2 >= cap) {
            cap *= 2;
            char *next = (char *)realloc(buf, cap);
            if (!next) {
                free(buf);
                free(syms);
                fprintf(stderr, "mako: OOM in stack_trace grow\n");
                abort();
            }
            buf = next;
        }
        memcpy(buf + len, s, sl);
        len += sl;
        buf[len++] = '\n';
    }
    buf[len] = 0;
    free(syms);
    return (MakoString){buf, len};
#else
    return mako_str_from_cstr("(stack_trace unavailable on this platform)\n");
#endif
}

/* ---- Crash report seed: write signal + stack to a path ---- */
static char mako_crash_report_path[512];
static int mako_crash_report_installed = 0;

static void mako_crash_report_handler(int sig) {
    const char *sn =
        sig == SIGSEGV ? "SIGSEGV" :
        sig == SIGABRT ? "SIGABRT" :
        sig == SIGFPE  ? "SIGFPE"  :
#if defined(SIGBUS)
        sig == SIGBUS  ? "SIGBUS"  :
#endif
        "signal";
    FILE *f = NULL;
    if (mako_crash_report_path[0]) {
        f = fopen(mako_crash_report_path, "w");
    }
    if (!f) f = stderr;
    fprintf(f, "mako crash report\n");
    fprintf(f, "signal: %s (%d)\n", sn, sig);
    fprintf(f, "time_ms: %" PRId64 "\n", mako_now_ms());
    fprintf(f, "stack:\n");
#if defined(__GLIBC__) || defined(__APPLE__)
    void *frames[48];
    int n = backtrace(frames, 48);
    if (f == stderr) {
        backtrace_symbols_fd(frames, n, 2);
    } else {
        char **syms = backtrace_symbols(frames, n);
        if (syms) {
            for (int i = 0; i < n; i++) {
                fprintf(f, "%s\n", syms[i] ? syms[i] : "?");
            }
            free(syms);
        }
    }
#else
    fprintf(f, "(no backtrace)\n");
#endif
    if (f != stderr) {
        fflush(f);
        fclose(f);
    } else {
        fflush(stderr);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

/* Install process crash reporter. path empty → stderr only. Returns 1. */
static inline int64_t mako_crash_report_install(MakoString path) {
    if (path.data && path.len > 0 && path.len < (int)sizeof(mako_crash_report_path)) {
        size_t n = path.len < sizeof(mako_crash_report_path) - 1
                       ? path.len
                       : sizeof(mako_crash_report_path) - 1;
        memcpy(mako_crash_report_path, path.data, n);
        mako_crash_report_path[n] = 0;
    } else {
        mako_crash_report_path[0] = 0;
    }
#if defined(_WIN32)
    signal(SIGSEGV, mako_crash_report_handler);
    signal(SIGABRT, mako_crash_report_handler);
    signal(SIGFPE, mako_crash_report_handler);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = mako_crash_report_handler;
    sa.sa_flags = SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
#if defined(SIGBUS)
    sigaction(SIGBUS, &sa, NULL);
#endif
    sigaction(SIGFPE, &sa, NULL);
#endif
    mako_crash_report_installed = 1;
    return 1;
}

static inline int64_t mako_crash_report_installed_p(void) {
    return mako_crash_report_installed ? 1 : 0;
}

/* RSS / CPU sample helpers for profile snapshots. */
static inline int64_t mako_process_rss_bytes(void) {
#if defined(_WIN32)
    return -1;
#elif defined(__APPLE__)
    /* Prefer mach task_info; ru_maxrss is hidden under strict POSIX macros. */
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count)
        == KERN_SUCCESS) {
        return (int64_t)info.resident_size;
    }
    return -1;
#else
    /* Linux: prefer /proc; ru_maxrss is kilobytes. */
    FILE *f = fopen("/proc/self/statm", "r");
    if (f) {
        unsigned long pages = 0, rss_pages = 0;
        if (fscanf(f, "%lu %lu", &pages, &rss_pages) >= 2) {
            fclose(f);
            long psz = sysconf(_SC_PAGESIZE);
            if (psz < 1) psz = 4096;
            return (int64_t)rss_pages * (int64_t)psz;
        }
        fclose(f);
    }
#if defined(RUSAGE_SELF)
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        return (int64_t)ru.ru_maxrss * 1024;
    }
#endif
    return -1;
#endif
}

static inline int64_t mako_process_cpu_user_us(void) {
#if defined(_WIN32)
    return -1;
#elif defined(RUSAGE_SELF)
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return -1;
    return (int64_t)ru.ru_utime.tv_sec * 1000000LL + (int64_t)ru.ru_utime.tv_usec;
#else
    return -1;
#endif
}

static inline int64_t mako_process_cpu_sys_us(void) {
#if defined(_WIN32)
    return -1;
#elif defined(RUSAGE_SELF)
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return -1;
    return (int64_t)ru.ru_stime.tv_sec * 1000000LL + (int64_t)ru.ru_stime.tv_usec;
#else
    return -1;
#endif
}

/* Elapsed since start tick (mono domain preferred). */
static inline int64_t mako_elapsed_ns(int64_t start_mono_ns) {
    int64_t n = mako_mono_ns() - start_mono_ns;
    return n < 0 ? 0 : n;
}

static inline int64_t mako_elapsed_us(int64_t start_mono_us) {
    int64_t n = mako_mono_us() - start_mono_us;
    return n < 0 ? 0 : n;
}

/* Wall-based elapsed (legacy elapsed_ms); can jump with NTP. Prefer elapsed_mono_ms. */
static inline int64_t mako_elapsed_ms(int64_t start_wall_ms) {
    int64_t n = mako_wall_ms() - start_wall_ms;
    return n < 0 ? 0 : n;
}

static inline int64_t mako_elapsed_mono_ms(int64_t start_mono_ms) {
    int64_t n = mako_mono_ms() - start_mono_ms;
    return n < 0 ? 0 : n;
}

/* Deadlines on the monotonic clock. */
static inline int64_t mako_deadline_ns(int64_t timeout_ns) {
    if (timeout_ns < 0) timeout_ns = 0;
    return mako_mono_ns() + timeout_ns;
}

static inline int64_t mako_deadline_ms(int64_t timeout_ms) {
    if (timeout_ms < 0) timeout_ms = 0;
    return mako_mono_ns() + timeout_ms * 1000000LL;
}

static inline int64_t mako_deadline_remaining_ns(int64_t deadline_mono_ns) {
    int64_t left = deadline_mono_ns - mako_mono_ns();
    return left < 0 ? 0 : left;
}

static inline int64_t mako_deadline_remaining_ms(int64_t deadline_mono_ns) {
    return mako_deadline_remaining_ns(deadline_mono_ns) / 1000000LL;
}

static inline int64_t mako_deadline_expired(int64_t deadline_mono_ns) {
    return mako_mono_ns() >= deadline_mono_ns ? 1 : 0;
}

/* Join until absolute mono-ns deadline (from deadline_ms / deadline_ns). */
static inline int64_t mako_await_deadline_ns(MakoTask *t, int64_t deadline_mono_ns, int64_t *out) {
    int64_t left_ms = mako_deadline_remaining_ms(deadline_mono_ns);
    return mako_await_timeout_ms(t, left_ms, out);
}

/* High-resolution sleep (nanosleep loop). For <~50µs, OS may oversleep —
 * use spin_until_ns for ultra-short waits. */
static inline void mako_sleep_ns(int64_t ns) {
    if (ns <= 0) return;
#if defined(_WIN32)
    /* Sleep(1) min granularity; for short sleeps, spin. */
    if (ns < 1000000LL) {
        int64_t until = mako_mono_ns() + ns;
        while (mako_mono_ns() < until) {
            YieldProcessor();
        }
        return;
    }
    DWORD ms = (DWORD)(ns / 1000000LL);
    if (ms == 0) ms = 1;
    Sleep(ms);
#else
    struct timespec req, rem;
    req.tv_sec = (time_t)(ns / 1000000000LL);
    req.tv_nsec = (long)(ns % 1000000000LL);
    while (nanosleep(&req, &rem) != 0) {
        if (errno != EINTR) break;
        req = rem;
    }
#endif
}

static inline void mako_sleep_us(int64_t us) {
    if (us <= 0) return;
    if (us > 1000000000LL) us = 1000000000LL; /* clamp ~1000s */
    mako_sleep_ns(us * 1000LL);
}

/* Busy-wait until mono deadline (lowest latency, burns CPU). */
static inline void mako_spin_until_ns(int64_t deadline_mono_ns) {
    while (mako_mono_ns() < deadline_mono_ns) {
#if defined(_WIN32)
        YieldProcessor();
#elif defined(__GNUC__) || defined(__clang__)
        __asm__ __volatile__("" ::: "memory");
#if defined(__x86_64__) || defined(__i386__)
        __asm__ __volatile__("pause");
#elif defined(__aarch64__)
        __asm__ __volatile__("yield");
#endif
#endif
    }
}

/* Hybrid wait: sleep until near deadline, then spin for the last few µs. */
static inline void mako_sleep_until_ns(int64_t deadline_mono_ns) {
    for (;;) {
        int64_t left = deadline_mono_ns - mako_mono_ns();
        if (left <= 0) return;
        if (left > 100000LL) { /* > 100 µs: sleep most of it */
            mako_sleep_ns(left - 50000LL);
            continue;
        }
        mako_spin_until_ns(deadline_mono_ns);
        return;
    }
}

/* Measure a no-op for calibration (returns ns of one mono_ns sample pair). */
static inline int64_t mako_mono_overhead_ns(void) {
    int64_t a = mako_mono_ns();
    int64_t b = mako_mono_ns();
    int64_t d = b - a;
    return d < 0 ? 0 : d;
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
    /* Closed sockets / proxy races: write must not kill the test process. */
    signal(SIGPIPE, SIG_IGN);
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
