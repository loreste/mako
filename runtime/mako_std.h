/* Mako stdlib extras — JSON, crypto, log/metrics, RC share, slices, DB integrations */
#ifndef MAKO_STD_H
#define MAKO_STD_H

#include "mako_rt.h"
#include "mako_stdlib.h"
#include "mako_plugin.h"
#if !defined(_WIN32)
#include <dlfcn.h>
#include <sys/stat.h>
#endif

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonHMAC.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#define MAKO_HAS_CC 1
#elif defined(MAKO_USE_OPENSSL)
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#define MAKO_HAS_OPENSSL 1
#endif

#include "mako_goext.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Logging (strong path when mako_log.h included; else simple stderr) ---- */
static inline void mako_log_write_trace_field(void) {
#if defined(MAKO_TRACE_H)
    MakoString tid = mako_trace_current();
    if (tid.data && tid.len > 0) {
        fprintf(stderr, "trace=%.*s ", (int)tid.len, tid.data);
    }
    mako_str_free(tid);
#endif
}

static inline void mako_log_info(MakoString msg) {
#if defined(MAKO_LOG_H)
    mako_slog_info(msg);
#else
    fprintf(stderr, "[%lld info] ", (long long)mako_now_ms());
    mako_log_write_trace_field();
    fwrite(msg.data, 1, msg.len, stderr);
    fputc('\n', stderr);
#endif
}

static inline void mako_log_warn(MakoString msg) {
#if defined(MAKO_LOG_H)
    mako_slog_warn(msg);
#else
    fprintf(stderr, "[%lld warn] ", (long long)mako_now_ms());
    mako_log_write_trace_field();
    fwrite(msg.data, 1, msg.len, stderr);
    fputc('\n', stderr);
#endif
}

static inline void mako_log_error(MakoString msg) {
#if defined(MAKO_LOG_H)
    mako_slog_error(msg);
#else
    fprintf(stderr, "[%lld error] ", (long long)mako_now_ms());
    mako_log_write_trace_field();
    fwrite(msg.data, 1, msg.len, stderr);
    fputc('\n', stderr);
#endif
}

/* ---- Backend validation helpers ---- */
static inline int64_t mako_validate_required(MakoString value) {
    for (size_t i = 0; i < value.len; i++) {
        unsigned char c = (unsigned char)value.data[i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return 1;
    }
    return 0;
}

static inline int64_t mako_validate_min_len(MakoString value, int64_t min_len) {
    if (min_len < 0) min_len = 0;
    return value.len >= (size_t)min_len ? 1 : 0;
}

static inline int64_t mako_validate_max_len(MakoString value, int64_t max_len) {
    if (max_len < 0) return 0;
    return value.len <= (size_t)max_len ? 1 : 0;
}

static inline int64_t mako_validate_int_range(int64_t value, int64_t min, int64_t max) {
    return value >= min && value <= max ? 1 : 0;
}

static inline int64_t mako_validate_email(MakoString value) {
    if (value.len < 3) return 0;
    size_t at = value.len;
    size_t dot_after_at = value.len;
    for (size_t i = 0; i < value.len; i++) {
        char c = value.data[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return 0;
        if (c == '@') {
            if (at != value.len || i == 0) return 0;
            at = i;
        } else if (c == '.' && at != value.len && i > at + 1) {
            dot_after_at = i;
        }
    }
    return at != value.len && dot_after_at + 1 < value.len ? 1 : 0;
}

/* ---- Game loop / fixed timestep helpers ---- */
static inline int64_t mako_game_fixed_steps(int64_t elapsed_ms, int64_t step_ms, int64_t max_steps) {
    if (elapsed_ms <= 0 || step_ms <= 0 || max_steps <= 0) return 0;
    int64_t steps = elapsed_ms / step_ms;
    return steps > max_steps ? max_steps : steps;
}

static inline int64_t mako_game_fixed_remainder(int64_t elapsed_ms, int64_t step_ms, int64_t max_steps) {
    if (elapsed_ms <= 0 || step_ms <= 0 || max_steps <= 0) return elapsed_ms > 0 ? elapsed_ms : 0;
    int64_t steps = mako_game_fixed_steps(elapsed_ms, step_ms, max_steps);
    int64_t rem = elapsed_ms - steps * step_ms;
    if (rem < 0) return 0;
    return rem;
}

static inline int64_t mako_game_alpha(int64_t remainder_ms, int64_t step_ms, int64_t scale) {
    if (remainder_ms <= 0 || step_ms <= 0 || scale <= 0) return 0;
    if (remainder_ms >= step_ms) return scale;
    return (remainder_ms * scale) / step_ms;
}

static inline int64_t mako_game_frame_budget_ok(int64_t frame_start_ms, int64_t budget_ms) {
    if (budget_ms < 0) return 0;
    return mako_now_ms() - frame_start_ms <= budget_ms ? 1 : 0;
}

/* ---- Deterministic simulation helpers ---- */
static inline int64_t mako_fx_from_int(int64_t value, int64_t scale) {
    if (scale <= 0) return 0;
    return value * scale;
}

static inline int64_t mako_fx_to_int(int64_t value, int64_t scale) {
    if (scale <= 0) return 0;
    return value / scale;
}

static inline int64_t mako_fx_mul(int64_t a, int64_t b, int64_t scale) {
    if (scale <= 0) return 0;
#if defined(__SIZEOF_INT128__)
    return (int64_t)(((__int128)a * (__int128)b) / (__int128)scale);
#else
    return (a * b) / scale;
#endif
}

static inline int64_t mako_fx_div(int64_t a, int64_t b, int64_t scale) {
    if (scale <= 0 || b == 0) return 0;
#if defined(__SIZEOF_INT128__)
    return (int64_t)(((__int128)a * (__int128)scale) / (__int128)b);
#else
    return (a * scale) / b;
#endif
}

static inline int64_t mako_deterministic_rng_next(int64_t state) {
    uint64_t x = (uint64_t)state;
    if (x == 0) x = 0x9E3779B97F4A7C15ULL;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    return (int64_t)(x * 2685821657736338717ULL);
}

static inline int64_t mako_deterministic_rng_range(int64_t state, int64_t max) {
    if (max <= 0) return 0;
    uint64_t next = (uint64_t)mako_deterministic_rng_next(state);
    return (int64_t)(next % (uint64_t)max);
}

static inline MakoString mako_replay_append(MakoString stream, int64_t tick, int64_t input) {
    size_t cap = stream.len + 96;
    char *d = (char *)malloc(cap);
    if (!d) mako_abort("replay_append: out of memory");
    if (stream.len && stream.data) memcpy(d, stream.data, stream.len);
    int n = snprintf(
        d + stream.len,
        cap - stream.len,
        "%lld:%lld\n",
        (long long)tick,
        (long long)input
    );
    if (n < 0) n = 0;
    return (MakoString){d, stream.len + (size_t)n};
}

static inline int64_t mako_replay_input(MakoString stream, int64_t tick) {
    if (!stream.data || stream.len == 0) return 0;
    char *copy = (char *)malloc(stream.len + 1);
    if (!copy) mako_abort("replay_input: out of memory");
    memcpy(copy, stream.data, stream.len);
    copy[stream.len] = 0;

    int64_t found = 0;
    char *line = copy;
    while (*line) {
        char *next_line = strchr(line, '\n');
        if (next_line) *next_line = 0;

        char *end_tick = NULL;
        long long parsed_tick = strtoll(line, &end_tick, 10);
        if (end_tick && *end_tick == ':') {
            char *end_input = NULL;
            long long parsed_input = strtoll(end_tick + 1, &end_input, 10);
            if (end_input != end_tick + 1 && parsed_tick == (long long)tick) {
                found = (int64_t)parsed_input;
            }
        }

        if (!next_line) break;
        line = next_line + 1;
    }
    free(copy);
    return found;
}

/* ---- Ring buffers, SPSC queue seed, and scatter/gather helpers ---- */
#define MAKO_RING_MAX 64

typedef struct {
    int64_t *data;
    int64_t cap;
    atomic_llong head;
    atomic_llong tail;
} MakoRing;

static MakoRing mako_rings[MAKO_RING_MAX];
static atomic_llong mako_ring_next_id = 0;

static inline MakoRing *mako_ring_get(int64_t id) {
    if (id <= 0 || id > MAKO_RING_MAX) return NULL;
    MakoRing *r = &mako_rings[id - 1];
    if (!r->data || r->cap <= 0) return NULL;
    return r;
}

static inline int64_t mako_ring_new(int64_t cap) {
    if (cap <= 0) return 0;
    if (cap > 1048576) cap = 1048576;
    long long slot = atomic_fetch_add_explicit(&mako_ring_next_id, 1, memory_order_relaxed);
    if (slot < 0 || slot >= MAKO_RING_MAX) return 0;
    int64_t *data = (int64_t *)calloc((size_t)cap, sizeof(int64_t));
    if (!data) mako_abort("ring_new: out of memory");
    MakoRing *r = &mako_rings[slot];
    r->data = data;
    r->cap = cap;
    atomic_store_explicit(&r->head, 0, memory_order_relaxed);
    atomic_store_explicit(&r->tail, 0, memory_order_relaxed);
    return (int64_t)slot + 1;
}

static inline int64_t mako_ring_len(int64_t id) {
    MakoRing *r = mako_ring_get(id);
    if (!r) return 0;
    long long head = atomic_load_explicit(&r->head, memory_order_acquire);
    long long tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    long long len = tail - head;
    if (len < 0) return 0;
    return len > r->cap ? r->cap : (int64_t)len;
}

static inline int64_t mako_ring_cap(int64_t id) {
    MakoRing *r = mako_ring_get(id);
    return r ? r->cap : 0;
}

static inline int64_t mako_ring_push(int64_t id, int64_t value) {
    MakoRing *r = mako_ring_get(id);
    if (!r) return 0;
    long long tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    long long head = atomic_load_explicit(&r->head, memory_order_acquire);
    if (tail - head >= r->cap) return 0;
    r->data[tail % r->cap] = value;
    atomic_store_explicit(&r->tail, tail + 1, memory_order_release);
    return 1;
}

static inline int64_t mako_ring_pop(int64_t id) {
    MakoRing *r = mako_ring_get(id);
    if (!r) return 0;
    long long head = atomic_load_explicit(&r->head, memory_order_relaxed);
    long long tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (tail <= head) return 0;
    int64_t value = r->data[head % r->cap];
    atomic_store_explicit(&r->head, head + 1, memory_order_release);
    return value;
}

static inline int64_t mako_ring_peek(int64_t id) {
    MakoRing *r = mako_ring_get(id);
    if (!r) return 0;
    long long head = atomic_load_explicit(&r->head, memory_order_acquire);
    long long tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (tail <= head) return 0;
    return r->data[head % r->cap];
}

static inline int64_t mako_lfq_new(int64_t cap) {
    return mako_ring_new(cap);
}

static inline int64_t mako_lfq_try_push(int64_t id, int64_t value) {
    return mako_ring_push(id, value);
}

static inline int64_t mako_lfq_try_pop(int64_t id) {
    return mako_ring_pop(id);
}

static inline int64_t mako_lfq_len(int64_t id) {
    return mako_ring_len(id);
}

static inline MakoString mako_sg_gather2(MakoString a, MakoString b) {
    size_t len = a.len + b.len;
    char *d = (char *)malloc(len + 1);
    if (!d) mako_abort("sg_gather2: out of memory");
    if (a.len && a.data) memcpy(d, a.data, a.len);
    if (b.len && b.data) memcpy(d + a.len, b.data, b.len);
    d[len] = 0;
    return (MakoString){d, len};
}

static inline MakoString mako_sg_gather3(MakoString a, MakoString b, MakoString c) {
    size_t len = a.len + b.len + c.len;
    char *d = (char *)malloc(len + 1);
    if (!d) mako_abort("sg_gather3: out of memory");
    size_t off = 0;
    if (a.len && a.data) {
        memcpy(d + off, a.data, a.len);
        off += a.len;
    }
    if (b.len && b.data) {
        memcpy(d + off, b.data, b.len);
        off += b.len;
    }
    if (c.len && c.data) memcpy(d + off, c.data, c.len);
    d[len] = 0;
    return (MakoString){d, len};
}

static inline MakoString mako_sg_slice(MakoString s, int64_t offset, int64_t len) {
    if (offset < 0) offset = 0;
    if (len < 0) len = 0;
    if ((size_t)offset > s.len) offset = (int64_t)s.len;
    size_t end = (size_t)offset + (size_t)len;
    if (end > s.len) end = s.len;
    return mako_str_slice(s, offset, (int64_t)end);
}

/* ---- Finite-state-machine helpers for session systems ---- */
static inline MakoString mako_fsm_rule(MakoString from, MakoString event, MakoString to) {
    size_t len = from.len + event.len + to.len + 3;
    char *d = (char *)malloc(len + 1);
    if (!d) mako_abort("fsm_rule: out of memory");
    size_t off = 0;
    if (from.len && from.data) {
        memcpy(d + off, from.data, from.len);
        off += from.len;
    }
    d[off++] = ':';
    if (event.len && event.data) {
        memcpy(d + off, event.data, event.len);
        off += event.len;
    }
    d[off++] = '>';
    if (to.len && to.data) {
        memcpy(d + off, to.data, to.len);
        off += to.len;
    }
    d[off++] = ';';
    d[off] = 0;
    return (MakoString){d, off};
}

static inline int64_t mako_fsm_is(MakoString state, MakoString expected) {
    return mako_str_eq(state, expected);
}

static inline int64_t mako_fsm_match_rule(
    MakoString rules,
    size_t start,
    size_t end,
    MakoString current,
    MakoString event,
    size_t *to_start,
    size_t *to_len
) {
    const char *r = rules.data ? rules.data : "";
    size_t colon = end;
    size_t arrow = end;
    for (size_t i = start; i < end; i++) {
        if (r[i] == ':' && colon == end) colon = i;
        if (r[i] == '>' && colon != end) {
            arrow = i;
            break;
        }
    }
    if (colon == end || arrow == end || arrow < colon) return 0;
    size_t from_len = colon - start;
    size_t event_len = arrow - colon - 1;
    size_t next_len = end - arrow - 1;
    if (from_len != current.len || event_len != event.len) return 0;
    if (from_len && memcmp(r + start, current.data, from_len) != 0) return 0;
    if (event_len && memcmp(r + colon + 1, event.data, event_len) != 0) return 0;
    *to_start = arrow + 1;
    *to_len = next_len;
    return 1;
}

static inline int64_t mako_fsm_can(MakoString current, MakoString event, MakoString rules) {
    if (!rules.data || rules.len == 0) return 0;
    size_t start = 0;
    while (start <= rules.len) {
        size_t end = start;
        while (end < rules.len && rules.data[end] != ';') end++;
        if (end > start) {
            size_t to_start = 0;
            size_t to_len = 0;
            if (mako_fsm_match_rule(rules, start, end, current, event, &to_start, &to_len)) {
                return 1;
            }
        }
        if (end >= rules.len) break;
        start = end + 1;
    }
    return 0;
}

static inline MakoString mako_fsm_transition(MakoString current, MakoString event, MakoString rules) {
    if (!rules.data || rules.len == 0) return mako_str_slice(current, 0, (int64_t)current.len);
    size_t start = 0;
    while (start <= rules.len) {
        size_t end = start;
        while (end < rules.len && rules.data[end] != ';') end++;
        if (end > start) {
            size_t to_start = 0;
            size_t to_len = 0;
            if (mako_fsm_match_rule(rules, start, end, current, event, &to_start, &to_len)) {
                return mako_str_slice(rules, (int64_t)to_start, (int64_t)(to_start + to_len));
            }
        }
        if (end >= rules.len) break;
        start = end + 1;
    }
    return mako_str_slice(current, 0, (int64_t)current.len);
}

/* ---- Game frame allocators, object pools, and allocation tracking ---- */
#define MAKO_FRAME_ALLOC_MAX 64
#define MAKO_OBJECT_POOL_MAX 64

typedef struct {
    unsigned char *data;
    int64_t cap;
    int64_t used;
} MakoFrameAlloc;

typedef struct {
    int64_t cap;
    int64_t free_count;
    int64_t *free_stack;
    unsigned char *in_use;
} MakoObjectPool;

static MakoFrameAlloc mako_frame_allocs[MAKO_FRAME_ALLOC_MAX];
static MakoObjectPool mako_object_pools[MAKO_OBJECT_POOL_MAX];
static atomic_llong mako_frame_alloc_next_id = 0;
static atomic_llong mako_object_pool_next_id = 0;
static atomic_llong mako_alloc_track_live = 0;
static atomic_llong mako_alloc_track_high = 0;

static inline void mako_alloc_track_observe(long long live) {
    long long old = atomic_load_explicit(&mako_alloc_track_high, memory_order_relaxed);
    while (live > old
           && !atomic_compare_exchange_weak_explicit(
               &mako_alloc_track_high,
               &old,
               live,
               memory_order_relaxed,
               memory_order_relaxed
           )) {
    }
}

static inline int64_t mako_alloc_track_reset(void) {
    atomic_store_explicit(&mako_alloc_track_live, 0, memory_order_relaxed);
    atomic_store_explicit(&mako_alloc_track_high, 0, memory_order_relaxed);
    return 1;
}

static inline int64_t mako_alloc_track_alloc(int64_t bytes) {
    if (bytes <= 0) return atomic_load_explicit(&mako_alloc_track_live, memory_order_relaxed);
    long long live = atomic_fetch_add_explicit(&mako_alloc_track_live, bytes, memory_order_relaxed) + bytes;
    mako_alloc_track_observe(live);
    return live;
}

static inline int64_t mako_alloc_track_free(int64_t bytes) {
    if (bytes <= 0) return atomic_load_explicit(&mako_alloc_track_live, memory_order_relaxed);
    long long old = atomic_load_explicit(&mako_alloc_track_live, memory_order_relaxed);
    while (1) {
        long long next = old - bytes;
        if (next < 0) next = 0;
        if (atomic_compare_exchange_weak_explicit(
                &mako_alloc_track_live,
                &old,
                next,
                memory_order_relaxed,
                memory_order_relaxed
            )) {
            return next;
        }
    }
}

static inline int64_t mako_alloc_track_live_bytes(void) {
    return atomic_load_explicit(&mako_alloc_track_live, memory_order_relaxed);
}

static inline int64_t mako_alloc_track_high_bytes(void) {
    return atomic_load_explicit(&mako_alloc_track_high, memory_order_relaxed);
}

static inline MakoString mako_alloc_track_report_json(void) {
    char *d = (char *)malloc(96);
    if (!d) mako_abort("alloc_track_report_json: out of memory");
    int n = snprintf(
        d,
        96,
        "{\"live_bytes\":%lld,\"high_water_bytes\":%lld}",
        atomic_load_explicit(&mako_alloc_track_live, memory_order_relaxed),
        atomic_load_explicit(&mako_alloc_track_high, memory_order_relaxed)
    );
    if (n < 0) n = 0;
    return (MakoString){d, (size_t)n};
}

static inline int64_t mako_leak_mark(void) {
    return atomic_load_explicit(&mako_alloc_track_live, memory_order_relaxed);
}

static inline int64_t mako_leak_bytes_since(int64_t mark) {
    long long live = atomic_load_explicit(&mako_alloc_track_live, memory_order_relaxed);
    long long leaked = live - mark;
    return leaked > 0 ? leaked : 0;
}

static inline int64_t mako_leak_detected(int64_t mark) {
    return mako_leak_bytes_since(mark) > 0 ? 1 : 0;
}

static inline int64_t mako_leak_assert_clear(int64_t mark) {
    return mako_leak_detected(mark) ? 0 : 1;
}

static inline MakoString mako_leak_report_json(int64_t mark) {
    long long live = atomic_load_explicit(&mako_alloc_track_live, memory_order_relaxed);
    long long high = atomic_load_explicit(&mako_alloc_track_high, memory_order_relaxed);
    long long leaked = live - mark;
    if (leaked < 0) leaked = 0;
    char *d = (char *)malloc(144);
    if (!d) mako_abort("leak_report_json: out of memory");
    int n = snprintf(
        d,
        144,
        "{\"mark_bytes\":%lld,\"live_bytes\":%lld,\"leaked_bytes\":%lld,\"high_water_bytes\":%lld,\"leak_detected\":%lld}",
        (long long)mark,
        live,
        leaked,
        high,
        leaked > 0 ? 1LL : 0LL
    );
    if (n < 0) n = 0;
    return (MakoString){d, (size_t)n};
}

static inline MakoFrameAlloc *mako_frame_alloc_get(int64_t id) {
    if (id <= 0 || id > MAKO_FRAME_ALLOC_MAX) return NULL;
    MakoFrameAlloc *a = &mako_frame_allocs[id - 1];
    if (!a->data || a->cap <= 0) return NULL;
    return a;
}

static inline int64_t mako_frame_alloc_new(int64_t cap) {
    if (cap <= 0) return 0;
    if (cap > 1048576 * 64LL) cap = 1048576 * 64LL;
    long long slot = atomic_fetch_add_explicit(&mako_frame_alloc_next_id, 1, memory_order_relaxed);
    if (slot < 0 || slot >= MAKO_FRAME_ALLOC_MAX) return 0;
    unsigned char *data = (unsigned char *)malloc((size_t)cap);
    if (!data) mako_abort("frame_alloc_new: out of memory");
    MakoFrameAlloc *a = &mako_frame_allocs[slot];
    a->data = data;
    a->cap = cap;
    a->used = 0;
    mako_alloc_track_alloc(cap);
    return (int64_t)slot + 1;
}

static inline int64_t mako_frame_alloc(int64_t id, int64_t bytes) {
    MakoFrameAlloc *a = mako_frame_alloc_get(id);
    if (!a || bytes <= 0) return -1;
    if (a->used + bytes > a->cap) return -1;
    int64_t off = a->used;
    a->used += bytes;
    return off;
}

static inline int64_t mako_frame_reset(int64_t id) {
    MakoFrameAlloc *a = mako_frame_alloc_get(id);
    if (!a) return 0;
    a->used = 0;
    return 1;
}

static inline int64_t mako_frame_used(int64_t id) {
    MakoFrameAlloc *a = mako_frame_alloc_get(id);
    return a ? a->used : 0;
}

static inline int64_t mako_frame_cap(int64_t id) {
    MakoFrameAlloc *a = mako_frame_alloc_get(id);
    return a ? a->cap : 0;
}

static inline MakoObjectPool *mako_object_pool_get(int64_t id) {
    if (id <= 0 || id > MAKO_OBJECT_POOL_MAX) return NULL;
    MakoObjectPool *p = &mako_object_pools[id - 1];
    if (!p->free_stack || !p->in_use || p->cap <= 0) return NULL;
    return p;
}

static inline int64_t mako_obj_pool_new(int64_t cap) {
    if (cap <= 0) return 0;
    if (cap > 1048576) cap = 1048576;
    long long slot = atomic_fetch_add_explicit(&mako_object_pool_next_id, 1, memory_order_relaxed);
    if (slot < 0 || slot >= MAKO_OBJECT_POOL_MAX) return 0;
    int64_t *stack = (int64_t *)malloc((size_t)cap * sizeof(int64_t));
    unsigned char *in_use = (unsigned char *)calloc((size_t)cap + 1, 1);
    if (!stack || !in_use) mako_abort("obj_pool_new: out of memory");
    for (int64_t i = 0; i < cap; i++) stack[i] = cap - i;
    MakoObjectPool *p = &mako_object_pools[slot];
    p->cap = cap;
    p->free_count = cap;
    p->free_stack = stack;
    p->in_use = in_use;
    mako_alloc_track_alloc(cap * (int64_t)sizeof(int64_t) + cap + 1);
    return (int64_t)slot + 1;
}

static inline int64_t mako_obj_acquire(int64_t id) {
    MakoObjectPool *p = mako_object_pool_get(id);
    if (!p || p->free_count <= 0) return 0;
    int64_t obj = p->free_stack[--p->free_count];
    if (obj <= 0 || obj > p->cap) return 0;
    p->in_use[obj] = 1;
    return obj;
}

static inline int64_t mako_obj_release(int64_t id, int64_t obj) {
    MakoObjectPool *p = mako_object_pool_get(id);
    if (!p || obj <= 0 || obj > p->cap || !p->in_use[obj]) return 0;
    p->in_use[obj] = 0;
    p->free_stack[p->free_count++] = obj;
    return 1;
}

static inline int64_t mako_obj_available(int64_t id) {
    MakoObjectPool *p = mako_object_pool_get(id);
    return p ? p->free_count : 0;
}

static inline int64_t mako_obj_pool_cap(int64_t id) {
    MakoObjectPool *p = mako_object_pool_get(id);
    return p ? p->cap : 0;
}

/* ---- ECS seed: entities, components, systems, archetype/query basics ---- */
#define MAKO_ECS_WORLD_MAX 32
#define MAKO_ECS_COMPONENT_MAX 64

typedef struct {
    int64_t cap;
    int64_t next_entity;
    unsigned char *alive;
    unsigned char *present;
    int64_t *values;
} MakoEcsWorld;

static MakoEcsWorld mako_ecs_worlds[MAKO_ECS_WORLD_MAX];
static atomic_llong mako_ecs_world_next_id = 0;

static inline MakoEcsWorld *mako_ecs_world_get(int64_t id) {
    if (id <= 0 || id > MAKO_ECS_WORLD_MAX) return NULL;
    MakoEcsWorld *w = &mako_ecs_worlds[id - 1];
    if (!w->alive || !w->present || !w->values || w->cap <= 0) return NULL;
    return w;
}

static inline int64_t mako_ecs_index(MakoEcsWorld *w, int64_t entity, int64_t component) {
    if (!w || entity <= 0 || entity > w->cap) return -1;
    if (component <= 0 || component >= MAKO_ECS_COMPONENT_MAX) return -1;
    return entity * MAKO_ECS_COMPONENT_MAX + component;
}

static inline int64_t mako_ecs_world_new(int64_t cap) {
    if (cap <= 0) return 0;
    if (cap > 1048576) cap = 1048576;
    long long slot = atomic_fetch_add_explicit(&mako_ecs_world_next_id, 1, memory_order_relaxed);
    if (slot < 0 || slot >= MAKO_ECS_WORLD_MAX) return 0;
    size_t rows = (size_t)cap + 1;
    unsigned char *alive = (unsigned char *)calloc(rows, 1);
    unsigned char *present = (unsigned char *)calloc(rows * MAKO_ECS_COMPONENT_MAX, 1);
    int64_t *values = (int64_t *)calloc(rows * MAKO_ECS_COMPONENT_MAX, sizeof(int64_t));
    if (!alive || !present || !values) mako_abort("ecs_world_new: out of memory");
    MakoEcsWorld *w = &mako_ecs_worlds[slot];
    w->cap = cap;
    w->next_entity = 1;
    w->alive = alive;
    w->present = present;
    w->values = values;
    mako_alloc_track_alloc(
        (int64_t)rows
        + (int64_t)(rows * MAKO_ECS_COMPONENT_MAX)
        + (int64_t)(rows * MAKO_ECS_COMPONENT_MAX * sizeof(int64_t))
    );
    return (int64_t)slot + 1;
}

static inline int64_t mako_ecs_spawn(int64_t world) {
    MakoEcsWorld *w = mako_ecs_world_get(world);
    if (!w) return 0;
    for (int64_t i = w->next_entity; i <= w->cap; i++) {
        if (!w->alive[i]) {
            w->alive[i] = 1;
            w->next_entity = i + 1;
            return i;
        }
    }
    for (int64_t i = 1; i < w->next_entity && i <= w->cap; i++) {
        if (!w->alive[i]) {
            w->alive[i] = 1;
            w->next_entity = i + 1;
            return i;
        }
    }
    return 0;
}

static inline int64_t mako_ecs_alive(int64_t world, int64_t entity) {
    MakoEcsWorld *w = mako_ecs_world_get(world);
    if (!w || entity <= 0 || entity > w->cap) return 0;
    return w->alive[entity] ? 1 : 0;
}

static inline int64_t mako_ecs_despawn(int64_t world, int64_t entity) {
    MakoEcsWorld *w = mako_ecs_world_get(world);
    if (!w || entity <= 0 || entity > w->cap || !w->alive[entity]) return 0;
    w->alive[entity] = 0;
    memset(
        w->present + (size_t)entity * MAKO_ECS_COMPONENT_MAX,
        0,
        MAKO_ECS_COMPONENT_MAX
    );
    memset(
        w->values + (size_t)entity * MAKO_ECS_COMPONENT_MAX,
        0,
        MAKO_ECS_COMPONENT_MAX * sizeof(int64_t)
    );
    if (entity < w->next_entity) w->next_entity = entity;
    return 1;
}

static inline int64_t mako_ecs_add(int64_t world, int64_t entity, int64_t component, int64_t value) {
    MakoEcsWorld *w = mako_ecs_world_get(world);
    int64_t idx = mako_ecs_index(w, entity, component);
    if (idx < 0 || !w->alive[entity]) return 0;
    w->present[idx] = 1;
    w->values[idx] = value;
    return 1;
}

static inline int64_t mako_ecs_set(int64_t world, int64_t entity, int64_t component, int64_t value) {
    MakoEcsWorld *w = mako_ecs_world_get(world);
    int64_t idx = mako_ecs_index(w, entity, component);
    if (idx < 0 || !w->alive[entity] || !w->present[idx]) return 0;
    w->values[idx] = value;
    return 1;
}

static inline int64_t mako_ecs_has(int64_t world, int64_t entity, int64_t component) {
    MakoEcsWorld *w = mako_ecs_world_get(world);
    int64_t idx = mako_ecs_index(w, entity, component);
    if (idx < 0 || !w->alive[entity]) return 0;
    return w->present[idx] ? 1 : 0;
}

static inline int64_t mako_ecs_get(int64_t world, int64_t entity, int64_t component) {
    MakoEcsWorld *w = mako_ecs_world_get(world);
    int64_t idx = mako_ecs_index(w, entity, component);
    if (idx < 0 || !w->alive[entity] || !w->present[idx]) return 0;
    return w->values[idx];
}

static inline int64_t mako_ecs_remove(int64_t world, int64_t entity, int64_t component) {
    MakoEcsWorld *w = mako_ecs_world_get(world);
    int64_t idx = mako_ecs_index(w, entity, component);
    if (idx < 0 || !w->alive[entity] || !w->present[idx]) return 0;
    w->present[idx] = 0;
    w->values[idx] = 0;
    return 1;
}

static inline int64_t mako_ecs_query_count(int64_t world, int64_t component) {
    MakoEcsWorld *w = mako_ecs_world_get(world);
    if (!w || component <= 0 || component >= MAKO_ECS_COMPONENT_MAX) return 0;
    int64_t count = 0;
    for (int64_t entity = 1; entity <= w->cap; entity++) {
        int64_t idx = entity * MAKO_ECS_COMPONENT_MAX + component;
        if (w->alive[entity] && w->present[idx]) count++;
    }
    return count;
}

static inline int64_t mako_ecs_query_first(int64_t world, int64_t component) {
    MakoEcsWorld *w = mako_ecs_world_get(world);
    if (!w || component <= 0 || component >= MAKO_ECS_COMPONENT_MAX) return 0;
    for (int64_t entity = 1; entity <= w->cap; entity++) {
        int64_t idx = entity * MAKO_ECS_COMPONENT_MAX + component;
        if (w->alive[entity] && w->present[idx]) return entity;
    }
    return 0;
}

static inline int64_t mako_ecs_archetype(int64_t world, int64_t entity) {
    MakoEcsWorld *w = mako_ecs_world_get(world);
    if (!w || entity <= 0 || entity > w->cap || !w->alive[entity]) return 0;
    uint64_t mask = 0;
    for (int64_t component = 1; component < MAKO_ECS_COMPONENT_MAX && component < 63; component++) {
        int64_t idx = entity * MAKO_ECS_COMPONENT_MAX + component;
        if (w->present[idx]) mask |= (uint64_t)1 << (uint64_t)component;
    }
    return (int64_t)mask;
}

static inline int64_t mako_ecs_system_add(int64_t world, int64_t component, int64_t delta) {
    MakoEcsWorld *w = mako_ecs_world_get(world);
    if (!w || component <= 0 || component >= MAKO_ECS_COMPONENT_MAX) return 0;
    int64_t count = 0;
    for (int64_t entity = 1; entity <= w->cap; entity++) {
        int64_t idx = entity * MAKO_ECS_COMPONENT_MAX + component;
        if (w->alive[entity] && w->present[idx]) {
            w->values[idx] += delta;
            count++;
        }
    }
    return count;
}

/* ---- Cookies, sessions, CSRF ---- */
static inline MakoString mako_cookie_get(MakoString header, MakoString name) {
    const char *h = header.data ? header.data : "";
    size_t pos = 0;
    while (pos < header.len) {
        while (pos < header.len && (h[pos] == ' ' || h[pos] == ';')) pos++;
        size_t key_start = pos;
        while (pos < header.len && h[pos] != '=' && h[pos] != ';') pos++;
        size_t key_len = pos - key_start;
        if (pos < header.len && h[pos] == '=') {
            pos++;
            size_t val_start = pos;
            while (pos < header.len && h[pos] != ';') pos++;
            size_t val_len = pos - val_start;
            if (key_len == name.len && memcmp(h + key_start, name.data, name.len) == 0) {
                char *d = (char *)malloc(val_len + 1);
                if (!d) mako_abort("cookie_get: out of memory");
                if (val_len) memcpy(d, h + val_start, val_len);
                d[val_len] = 0;
                return (MakoString){d, val_len};
            }
        }
        while (pos < header.len && h[pos] != ';') pos++;
    }
    return mako_str_from_cstr("");
}

static inline MakoString mako_cookie_make(MakoString name, MakoString value, int64_t max_age) {
    if (!mako_http_header_name_ok(name.data ? name.data : "", name.len)) {
        mako_abort("cookie_make: invalid cookie name");
    }
    if (!mako_http_header_value_ok(value.data ? value.data : "", value.len)) {
        mako_abort("cookie_make: invalid cookie value");
    }
    for (size_t i = 0; i < value.len; i++) {
        unsigned char c = (unsigned char)value.data[i];
        if (c == ';' || c == ',') mako_abort("cookie_make: invalid cookie delimiter");
    }
    size_t cap = name.len + value.len + 96;
    char *d = (char *)malloc(cap);
    if (!d) mako_abort("cookie_make: out of memory");
    int n = snprintf(
        d,
        cap,
        "%.*s=%.*s; Max-Age=%lld; Path=/; HttpOnly; SameSite=Lax",
        (int)name.len,
        name.data ? name.data : "",
        (int)value.len,
        value.data ? value.data : "",
        (long long)max_age
    );
    if (n < 0) n = 0;
    return (MakoString){d, (size_t)n};
}

static inline MakoString mako_session_id_new(void) {
    MakoString raw = mako_random_bytes(16);
    char *d = (char *)malloc(raw.len * 2 + 1);
    if (!d) mako_abort("session_id_new: out of memory");
    for (size_t i = 0; i < raw.len; i++) {
        snprintf(d + i * 2, 3, "%02x", (unsigned char)raw.data[i]);
    }
    d[raw.len * 2] = 0;
    MakoString out = {d, raw.len * 2};
    mako_str_free(raw);
    return out;
}

/* ---- Configurable memory / time / connection limits ---- */
typedef struct {
    int64_t mem_budget;     /* max bytes (0 = unlimited) */
    int64_t mem_used;
    int64_t time_budget_ms; /* wall clock from start_ms (0 = unlimited) */
    int64_t start_ms;
    int64_t max_conns;      /* max concurrent connections (0 = unlimited) */
    int64_t open_conns;
} MakoLimits;

static inline MakoLimits *mako_limits_new(int64_t mem_budget, int64_t time_budget_ms,
                                          int64_t max_conns) {
    MakoLimits *L = (MakoLimits *)calloc(1, sizeof(MakoLimits));
    if (!L) mako_abort("limits_new: out of memory");
    L->mem_budget = mem_budget < 0 ? 0 : mem_budget;
    L->time_budget_ms = time_budget_ms < 0 ? 0 : time_budget_ms;
    L->max_conns = max_conns < 0 ? 0 : max_conns;
    L->start_ms = mako_now_ms();
    return L;
}

static inline int64_t mako_limits_free(MakoLimits *L) {
    free(L);
    return 0;
}

static inline int64_t mako_limits_mem_used(MakoLimits *L) {
    return L ? L->mem_used : 0;
}

static inline int64_t mako_limits_open_conns(MakoLimits *L) {
    return L ? L->open_conns : 0;
}

/* Try to charge `n` bytes against the budget. Returns 1 if allowed, 0 if denied. */
static inline int64_t mako_limits_try_mem(MakoLimits *L, int64_t n) {
    if (!L || n < 0) return 0;
    if (L->mem_budget > 0 && L->mem_used + n > L->mem_budget) return 0;
    L->mem_used += n;
    return 1;
}

static inline int64_t mako_limits_release_mem(MakoLimits *L, int64_t n) {
    if (!L || n < 0) return 0;
    if (n > L->mem_used) L->mem_used = 0;
    else L->mem_used -= n;
    return 1;
}

/* 1 if still within time budget (or unlimited), else 0. */
static inline int64_t mako_limits_check_time(MakoLimits *L) {
    if (!L) return 0;
    if (L->time_budget_ms <= 0) return 1;
    return (mako_now_ms() - L->start_ms) <= L->time_budget_ms ? 1 : 0;
}

/* Try to open one connection slot. Returns 1 if allowed, 0 if at cap. */
static inline int64_t mako_limits_try_conn(MakoLimits *L) {
    if (!L) return 0;
    if (L->max_conns > 0 && L->open_conns >= L->max_conns) return 0;
    L->open_conns++;
    return 1;
}

static inline int64_t mako_limits_release_conn(MakoLimits *L) {
    if (!L) return 0;
    if (L->open_conns > 0) L->open_conns--;
    return 1;
}

/* ---- Remote session cancellation (process-local registry; share token over the wire) ---- */
#define MAKO_SESS_CANCEL_CAP 256
typedef struct {
    char token[40]; /* 32 hex + NUL */
    int cancelled;
    int used;
} MakoSessCancelSlot;

static MakoSessCancelSlot mako_sess_cancel_tab[MAKO_SESS_CANCEL_CAP];
/* Lazy-init: PTHREAD_MUTEX_INITIALIZER is not portable to CRITICAL_SECTION (Windows). */
static pthread_mutex_t mako_sess_cancel_mu;
static int mako_sess_cancel_mu_ready = 0;

static inline void mako_sess_cancel_mu_ensure(void) {
    if (mako_sess_cancel_mu_ready) return;
#if defined(_WIN32) || defined(_WIN64)
    static LONG once = 0;
    if (InterlockedCompareExchange(&once, 1, 0) == 0) {
        pthread_mutex_init(&mako_sess_cancel_mu, NULL);
        mako_sess_cancel_mu_ready = 1;
        InterlockedExchange(&once, 2);
    } else {
        while (InterlockedCompareExchange(&once, 2, 2) != 2)
            Sleep(0);
    }
#else
    static pthread_mutex_t boot = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&boot);
    if (!mako_sess_cancel_mu_ready) {
        pthread_mutex_init(&mako_sess_cancel_mu, NULL);
        mako_sess_cancel_mu_ready = 1;
    }
    pthread_mutex_unlock(&boot);
#endif
}

static inline int mako_sess_cancel_find(const char *tok, size_t n) {
    for (int i = 0; i < MAKO_SESS_CANCEL_CAP; i++) {
        if (!mako_sess_cancel_tab[i].used) continue;
        if (strlen(mako_sess_cancel_tab[i].token) == n
            && memcmp(mako_sess_cancel_tab[i].token, tok, n) == 0)
            return i;
    }
    return -1;
}

/* Mint a cancel token (hex session id) and register it as active (not cancelled). */
static inline MakoString mako_session_cancel_token(void) {
    MakoString t = mako_session_id_new();
    if (t.len == 0 || t.len >= 40) return t;
    mako_sess_cancel_mu_ensure();
    pthread_mutex_lock(&mako_sess_cancel_mu);
    int slot = -1;
    for (int i = 0; i < MAKO_SESS_CANCEL_CAP; i++) {
        if (!mako_sess_cancel_tab[i].used) {
            slot = i;
            break;
        }
    }
    if (slot >= 0) {
        memcpy(mako_sess_cancel_tab[slot].token, t.data, t.len);
        mako_sess_cancel_tab[slot].token[t.len] = 0;
        mako_sess_cancel_tab[slot].cancelled = 0;
        mako_sess_cancel_tab[slot].used = 1;
    }
    pthread_mutex_unlock(&mako_sess_cancel_mu);
    return t;
}

/* Mark token cancelled (remote peer or local). Returns 1 if found. */
static inline int64_t mako_session_cancel(MakoString token) {
    if (!token.data || token.len == 0 || token.len >= 40) return 0;
    mako_sess_cancel_mu_ensure();
    pthread_mutex_lock(&mako_sess_cancel_mu);
    int i = mako_sess_cancel_find(token.data, token.len);
    if (i < 0) {
        /* Register then cancel so remote cancel-before-register still works after mint. */
        for (int j = 0; j < MAKO_SESS_CANCEL_CAP; j++) {
            if (!mako_sess_cancel_tab[j].used) {
                memcpy(mako_sess_cancel_tab[j].token, token.data, token.len);
                mako_sess_cancel_tab[j].token[token.len] = 0;
                mako_sess_cancel_tab[j].cancelled = 1;
                mako_sess_cancel_tab[j].used = 1;
                pthread_mutex_unlock(&mako_sess_cancel_mu);
                return 1;
            }
        }
        pthread_mutex_unlock(&mako_sess_cancel_mu);
        return 0;
    }
    mako_sess_cancel_tab[i].cancelled = 1;
    pthread_mutex_unlock(&mako_sess_cancel_mu);
    return 1;
}

/* 1 if token is marked cancelled, else 0. */
static inline int64_t mako_session_cancelled(MakoString token) {
    if (!token.data || token.len == 0) return 0;
    mako_sess_cancel_mu_ensure();
    pthread_mutex_lock(&mako_sess_cancel_mu);
    int i = mako_sess_cancel_find(token.data, token.len);
    int64_t r = (i >= 0 && mako_sess_cancel_tab[i].cancelled) ? 1 : 0;
    pthread_mutex_unlock(&mako_sess_cancel_mu);
    return r;
}

static inline int64_t mako_session_cancel_clear(MakoString token) {
    if (!token.data || token.len == 0) return 0;
    mako_sess_cancel_mu_ensure();
    pthread_mutex_lock(&mako_sess_cancel_mu);
    int i = mako_sess_cancel_find(token.data, token.len);
    if (i >= 0) {
        mako_sess_cancel_tab[i].used = 0;
        mako_sess_cancel_tab[i].cancelled = 0;
        mako_sess_cancel_tab[i].token[0] = 0;
    }
    pthread_mutex_unlock(&mako_sess_cancel_mu);
    return i >= 0 ? 1 : 0;
}

/* ---- SCRAM channel binding helpers (RFC 5802 / 7677 gs2-header + cbind) ---- */
/* cbind_name empty or "n" → no binding ("n,,"). Otherwise "p=<name>,," (e.g. tls-unique). */
static inline MakoString mako_scram_gs2_header(MakoString cbind_name) {
    if (!cbind_name.data || cbind_name.len == 0
        || (cbind_name.len == 1 && cbind_name.data[0] == 'n')) {
        return mako_str_from_cstr("n,,");
    }
    size_t n = 3 + cbind_name.len + 2; /* p= + name + ,, */
    char *d = (char *)malloc(n + 1);
    if (!d) mako_abort("scram_gs2_header: OOM");
    d[0] = 'p';
    d[1] = '=';
    memcpy(d + 2, cbind_name.data, cbind_name.len);
    d[2 + cbind_name.len] = ',';
    d[3 + cbind_name.len] = ',';
    d[4 + cbind_name.len] = 0;
    return (MakoString){d, 4 + cbind_name.len};
}

/* c= attribute value: base64(gs2-header || cbind-data). cbind_data may be empty (y/n flags). */
static inline MakoString mako_scram_cbind_b64(MakoString gs2_header, MakoString cbind_data) {
    size_t n = gs2_header.len + (cbind_data.data ? cbind_data.len : 0);
    char *buf = (char *)malloc(n + 1);
    if (!buf) mako_abort("scram_cbind_b64: OOM");
    if (gs2_header.len && gs2_header.data)
        memcpy(buf, gs2_header.data, gs2_header.len);
    if (cbind_data.data && cbind_data.len)
        memcpy(buf + gs2_header.len, cbind_data.data, cbind_data.len);
    buf[n] = 0;
    MakoString raw = {buf, n};
    MakoString b64 = mako_base64_encode(raw);
    free(buf);
    return b64;
}

/* client-final-without-proof: "c=<cbind_b64>,r=<nonce>" */
static inline MakoString mako_scram_client_final_without_proof(MakoString cbind_b64,
                                                              MakoString nonce) {
    size_t n = 2 + cbind_b64.len + 3 + nonce.len;
    char *d = (char *)malloc(n + 1);
    if (!d) mako_abort("scram_client_final_wo_proof: OOM");
    int written = snprintf(d, n + 1, "c=%.*s,r=%.*s", (int)cbind_b64.len,
                           cbind_b64.data ? cbind_b64.data : "", (int)nonce.len,
                           nonce.data ? nonce.data : "");
    if (written < 0) written = 0;
    return (MakoString){d, (size_t)written};
}

/* ---- PEM helpers (string-level; no OpenSSL required) ---- */
/* Count -----BEGIN …----- blocks in a PEM blob. */
static inline int64_t mako_pem_count_blocks(MakoString pem) {
    if (!pem.data || pem.len < 11) return 0;
    int64_t n = 0;
    const char *p = pem.data;
    const char *end = pem.data + pem.len;
    while (p + 11 <= end) {
        if (memcmp(p, "-----BEGIN ", 11) == 0) n++;
        p++;
    }
    return n;
}

/* 1 if a block labeled `label` exists (e.g. "CERTIFICATE", "PRIVATE KEY"). */
static inline int64_t mako_pem_has_block(MakoString pem, MakoString label) {
    if (!pem.data || !label.data || label.len == 0) return 0;
    size_t need = 11 + label.len + 5; /* -----BEGIN + label + -----\n */
    if (pem.len < need) return 0;
    char needle[256];
    if (label.len + 20 >= sizeof(needle)) return 0;
    int n = snprintf(needle, sizeof(needle), "-----BEGIN %.*s-----",
                     (int)label.len, label.data);
    if (n <= 0) return 0;
    for (size_t i = 0; i + (size_t)n <= pem.len; i++) {
        if (memcmp(pem.data + i, needle, (size_t)n) == 0) return 1;
    }
    return 0;
}

/* Extract first full PEM block for `label` (BEGIN…END inclusive). Empty if missing. */
static inline MakoString mako_pem_extract_block(MakoString pem, MakoString label) {
    if (!pem.data || !label.data || label.len == 0) return mako_str_from_cstr("");
    char begin[256], endm[256];
    if (label.len + 24 >= sizeof(begin)) return mako_str_from_cstr("");
    int nb = snprintf(begin, sizeof(begin), "-----BEGIN %.*s-----",
                      (int)label.len, label.data);
    int ne = snprintf(endm, sizeof(endm), "-----END %.*s-----",
                      (int)label.len, label.data);
    if (nb <= 0 || ne <= 0) return mako_str_from_cstr("");
    const char *p = pem.data;
    const char *pend = pem.data + pem.len;
    const char *b = NULL;
    for (const char *q = p; q + nb <= pend; q++) {
        if (memcmp(q, begin, (size_t)nb) == 0) {
            b = q;
            break;
        }
    }
    if (!b) return mako_str_from_cstr("");
    const char *e = NULL;
    for (const char *q = b + nb; q + ne <= pend; q++) {
        if (memcmp(q, endm, (size_t)ne) == 0) {
            e = q + ne;
            /* include trailing newline if present */
            if (e < pend && (*e == '\n' || *e == '\r')) e++;
            if (e < pend && e[-1] == '\r' && *e == '\n') e++;
            break;
        }
    }
    if (!e || e <= b) return mako_str_from_cstr("");
    size_t n = (size_t)(e - b);
    char *d = (char *)malloc(n + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, b, n);
    d[n] = 0;
    return (MakoString){d, n};
}

/* Load a PEM file (read + require at least one BEGIN block). Empty on fail. */
static inline MakoString mako_pem_load_file(MakoString path) {
    MakoString raw = mako_read_file(path);
    if (!raw.data || raw.len == 0) return mako_str_from_cstr("");
    if (mako_pem_count_blocks(raw) == 0) {
        free(raw.data);
        return mako_str_from_cstr("");
    }
    return raw;
}

static inline MakoString mako_csrf_token(void) {
    return mako_session_id_new();
}

static inline int64_t mako_csrf_check(MakoString expected, MakoString submitted) {
    return mako_const_eq(expected, submitted);
}

static inline MakoString mako_hmac_sha256_hex(MakoString key, MakoString msg);

/* ---- Authentication and authorization helpers ---- */
static inline int mako_auth_prefix_eq(MakoString s, const char *prefix) {
    size_t n = strlen(prefix);
    return s.data && s.len >= n && memcmp(s.data, prefix, n) == 0;
}

static inline MakoString mako_auth_bearer(MakoString authorization) {
    const char *p = "Bearer ";
    size_t n = strlen(p);
    if (!mako_auth_prefix_eq(authorization, p)) return mako_str_from_cstr("");
    size_t start = n;
    while (start < authorization.len && authorization.data[start] == ' ') start++;
    size_t end = authorization.len;
    while (end > start && (authorization.data[end - 1] == ' ' || authorization.data[end - 1] == '\t')) end--;
    return mako_str_slice(authorization, (int64_t)start, (int64_t)end);
}

static inline int64_t mako_auth_check_bearer(MakoString authorization, MakoString expected_token) {
    MakoString got = mako_auth_bearer(authorization);
    return mako_const_eq(got, expected_token);
}

static inline MakoString mako_auth_basic_header(MakoString user, MakoString pass) {
    size_t raw_len = user.len + pass.len + 1;
    char *raw = (char *)malloc(raw_len + 1);
    if (!raw) mako_abort("auth_basic_header: out of memory");
    size_t pos = 0;
    if (user.len) memcpy(raw + pos, user.data, user.len);
    pos += user.len;
    raw[pos++] = ':';
    if (pass.len) memcpy(raw + pos, pass.data, pass.len);
    pos += pass.len;
    raw[pos] = 0;
    MakoString b64 = mako_base64_encode((MakoString){raw, raw_len});
    free(raw);
    size_t out_len = 6 + b64.len;
    char *out = (char *)malloc(out_len + 1);
    if (!out) mako_abort("auth_basic_header: out of memory");
    memcpy(out, "Basic ", 6);
    memcpy(out + 6, b64.data, b64.len);
    out[out_len] = 0;
    return (MakoString){out, out_len};
}

static inline int64_t mako_auth_check_basic(MakoString authorization, MakoString user, MakoString pass) {
    MakoString expected = mako_auth_basic_header(user, pass);
    return mako_const_eq(authorization, expected);
}

static inline int mako_auth_csv_contains(MakoString csv, MakoString item) {
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

static inline int64_t mako_auth_role_has(MakoString roles_csv, MakoString role) {
    return mako_auth_csv_contains(roles_csv, role) ? 1 : 0;
}

static inline int64_t mako_authz_allow_role(MakoString user_roles_csv, MakoString required_roles_csv) {
    if (!required_roles_csv.data || required_roles_csv.len == 0) return 1;
    size_t i = 0;
    while (i <= required_roles_csv.len) {
        while (i < required_roles_csv.len &&
               (required_roles_csv.data[i] == ' ' || required_roles_csv.data[i] == '\t' ||
                required_roles_csv.data[i] == ',')) i++;
        size_t start = i;
        while (i < required_roles_csv.len && required_roles_csv.data[i] != ',') i++;
        size_t end = i;
        while (end > start &&
               (required_roles_csv.data[end - 1] == ' ' || required_roles_csv.data[end - 1] == '\t')) end--;
        if (end > start) {
            MakoString role = {required_roles_csv.data + start, end - start};
            if (mako_auth_csv_contains(user_roles_csv, role)) return 1;
        }
        if (i >= required_roles_csv.len) break;
        i++;
    }
    return 0;
}

static inline MakoString mako_auth_token_sign(MakoString subject, MakoString secret) {
    MakoString sig = mako_hmac_sha256_hex(secret, subject);
    size_t out_len = subject.len + 1 + sig.len;
    char *out = (char *)malloc(out_len + 1);
    if (!out) mako_abort("auth_token_sign: out of memory");
    size_t pos = 0;
    if (subject.len) memcpy(out + pos, subject.data, subject.len);
    pos += subject.len;
    out[pos++] = '.';
    memcpy(out + pos, sig.data, sig.len);
    pos += sig.len;
    out[pos] = 0;
    return (MakoString){out, pos};
}

static inline MakoString mako_auth_token_subject(MakoString token) {
    if (!token.data) return mako_str_from_cstr("");
    for (size_t i = 0; i < token.len; i++) {
        if (token.data[i] == '.') return mako_str_slice(token, 0, (int64_t)i);
    }
    return mako_str_from_cstr("");
}

static inline int64_t mako_auth_token_check(MakoString token, MakoString secret) {
    MakoString subject = mako_auth_token_subject(token);
    if (!subject.data || subject.len == 0) return 0;
    MakoString expected = mako_auth_token_sign(subject, secret);
    return mako_const_eq(token, expected);
}

static inline int64_t mako_auth_session_cookie(MakoString cookie_header, MakoString cookie_name, MakoString expected) {
    MakoString got = mako_cookie_get(cookie_header, cookie_name);
    return mako_const_eq(got, expected);
}

/* ---- Backend rate limiting, compression negotiation, and TTL cache ---- */
#define MAKO_BACKEND_SLOTS 128

typedef struct {
    int used;
    uint64_t hash;
    char *key;
    size_t key_len;
    int64_t window_start_ms;
    int64_t count;
} MakoRateSlot;

typedef struct {
    int used;
    uint64_t hash;
    char *key;
    size_t key_len;
    char *value;
    size_t value_len;
    int64_t expires_ms;
} MakoCacheSlot;

typedef struct {
    int used;
    uint64_t hash;
    char *name;
    size_t name_len;
    int64_t due_ms;
} MakoJobSlot;

static MakoRateSlot mako_rate_slots[MAKO_BACKEND_SLOTS];
static MakoCacheSlot mako_cache_slots[MAKO_BACKEND_SLOTS];
static MakoJobSlot mako_job_slots[MAKO_BACKEND_SLOTS];

static inline char *mako_backend_dup(const char *p, size_t n) {
    char *d = (char *)malloc(n + 1);
    if (!d) mako_abort("backend helper: out of memory");
    if (n) memcpy(d, p ? p : "", n);
    d[n] = 0;
    return d;
}

static inline int mako_backend_key_eq(uint64_t hash, const char *key, size_t key_len, MakoString s) {
    return hash == mako_hash_bytes(s.data ? s.data : "", s.len)
        && key_len == s.len
        && (s.len == 0 || memcmp(key, s.data ? s.data : "", s.len) == 0);
}

static inline int64_t mako_rate_allow(MakoString key, int64_t limit, int64_t window_ms) {
    if (limit <= 0) return 0;
    if (window_ms <= 0) window_ms = 1000;
    const char *k = key.data ? key.data : "";
    uint64_t h = mako_hash_bytes(k, key.len);
    size_t start = (size_t)(h % MAKO_BACKEND_SLOTS);
    size_t victim = start;
    int64_t now = mako_now_ms();
    for (size_t probe = 0; probe < MAKO_BACKEND_SLOTS; probe++) {
        size_t idx = (start + probe) % MAKO_BACKEND_SLOTS;
        MakoRateSlot *s = &mako_rate_slots[idx];
        if (!s->used) {
            victim = idx;
            break;
        }
        if (mako_backend_key_eq(s->hash, s->key, s->key_len, key)) {
            if (now - s->window_start_ms >= window_ms) {
                s->window_start_ms = now;
                s->count = 0;
            }
            if (s->count >= limit) return 0;
            s->count++;
            return 1;
        }
        if (now - s->window_start_ms >= window_ms) victim = idx;
    }
    MakoRateSlot *s = &mako_rate_slots[victim];
    if (s->used) free(s->key);
    s->used = 1;
    s->hash = h;
    s->key = mako_backend_dup(k, key.len);
    s->key_len = key.len;
    s->window_start_ms = now;
    s->count = 1;
    return 1;
}

static inline int64_t mako_rate_remaining(MakoString key, int64_t limit, int64_t window_ms) {
    if (limit <= 0) return 0;
    if (window_ms <= 0) window_ms = 1000;
    uint64_t h = mako_hash_bytes(key.data ? key.data : "", key.len);
    int64_t now = mako_now_ms();
    for (size_t i = 0; i < MAKO_BACKEND_SLOTS; i++) {
        MakoRateSlot *s = &mako_rate_slots[i];
        if (s->used && mako_backend_key_eq(s->hash, s->key, s->key_len, key)) {
            if (now - s->window_start_ms >= window_ms) return limit;
            int64_t left = limit - s->count;
            return left > 0 ? left : 0;
        }
    }
    return limit;
}

static inline int mako_contains_gzip(MakoString accept_encoding) {
    const char *s = accept_encoding.data ? accept_encoding.data : "";
    for (size_t i = 0; i + 4 <= accept_encoding.len; i++) {
        char a = s[i], b = s[i + 1], c = s[i + 2], d = s[i + 3];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (d >= 'A' && d <= 'Z') d = (char)(d - 'A' + 'a');
        if (a == 'g' && b == 'z' && c == 'i' && d == 'p') return 1;
    }
    return 0;
}

static inline MakoString mako_http_content_encoding(MakoString accept_encoding) {
    if (mako_contains_gzip(accept_encoding) && mako_gzip_available() == 1) {
        return mako_str_from_cstr("gzip");
    }
    return mako_str_from_cstr("identity");
}

static inline MakoString mako_http_compress_if_accepted(MakoString body, MakoString accept_encoding) {
    if (mako_contains_gzip(accept_encoding) && mako_gzip_available() == 1) {
        return mako_gzip_compress(body);
    }
    char *d = mako_backend_dup(body.data ? body.data : "", body.len);
    return (MakoString){d, body.len};
}

static inline int64_t mako_cache_put(MakoString key, MakoString value, int64_t ttl_ms) {
    const char *k = key.data ? key.data : "";
    uint64_t h = mako_hash_bytes(k, key.len);
    size_t start = (size_t)(h % MAKO_BACKEND_SLOTS);
    size_t victim = start;
    int64_t now = mako_now_ms();
    for (size_t probe = 0; probe < MAKO_BACKEND_SLOTS; probe++) {
        size_t idx = (start + probe) % MAKO_BACKEND_SLOTS;
        MakoCacheSlot *s = &mako_cache_slots[idx];
        if (!s->used) {
            victim = idx;
            break;
        }
        if (s->expires_ms <= now) {
            victim = idx;
            break;
        }
        if (mako_backend_key_eq(s->hash, s->key, s->key_len, key)) {
            victim = idx;
            break;
        }
    }
    MakoCacheSlot *s = &mako_cache_slots[victim];
    if (s->used) {
        free(s->key);
        free(s->value);
    }
    if (ttl_ms <= 0) {
        s->used = 0;
        return 1;
    }
    s->used = 1;
    s->hash = h;
    s->key = mako_backend_dup(k, key.len);
    s->key_len = key.len;
    s->value = mako_backend_dup(value.data ? value.data : "", value.len);
    s->value_len = value.len;
    s->expires_ms = now + ttl_ms;
    return 1;
}

static inline MakoString mako_cache_get(MakoString key) {
    uint64_t h = mako_hash_bytes(key.data ? key.data : "", key.len);
    int64_t now = mako_now_ms();
    for (size_t i = 0; i < MAKO_BACKEND_SLOTS; i++) {
        MakoCacheSlot *s = &mako_cache_slots[i];
        if (!s->used || !mako_backend_key_eq(s->hash, s->key, s->key_len, key)) continue;
        if (s->expires_ms <= now) {
            free(s->key);
            free(s->value);
            s->used = 0;
            return mako_str_from_cstr("");
        }
        char *d = mako_backend_dup(s->value, s->value_len);
        return (MakoString){d, s->value_len};
    }
    return mako_str_from_cstr("");
}

static inline int64_t mako_cache_has(MakoString key) {
    MakoString value = mako_cache_get(key);
    int64_t ok = value.len > 0 ? 1 : 0;
    if (value.data) mako_str_free(value);
    return ok;
}

static inline int64_t mako_job_schedule(MakoString name, int64_t delay_ms) {
    if (delay_ms < 0) delay_ms = 0;
    const char *n = name.data ? name.data : "";
    uint64_t h = mako_hash_bytes(n, name.len);
    size_t start = (size_t)(h % MAKO_BACKEND_SLOTS);
    size_t victim = start;
    for (size_t probe = 0; probe < MAKO_BACKEND_SLOTS; probe++) {
        size_t idx = (start + probe) % MAKO_BACKEND_SLOTS;
        MakoJobSlot *s = &mako_job_slots[idx];
        if (!s->used) {
            victim = idx;
            break;
        }
        if (mako_backend_key_eq(s->hash, s->name, s->name_len, name)) {
            victim = idx;
            break;
        }
    }
    MakoJobSlot *s = &mako_job_slots[victim];
    if (s->used) free(s->name);
    s->used = 1;
    s->hash = h;
    s->name = mako_backend_dup(n, name.len);
    s->name_len = name.len;
    s->due_ms = mako_now_ms() + delay_ms;
    return s->due_ms;
}

static inline int64_t mako_job_due(MakoString name) {
    uint64_t h = mako_hash_bytes(name.data ? name.data : "", name.len);
    int64_t now = mako_now_ms();
    for (size_t i = 0; i < MAKO_BACKEND_SLOTS; i++) {
        MakoJobSlot *s = &mako_job_slots[i];
        if (s->used && mako_backend_key_eq(s->hash, s->name, s->name_len, name)) {
            return now >= s->due_ms ? 1 : 0;
        }
    }
    return 0;
}

static inline int64_t mako_job_delay_ms(MakoString name) {
    uint64_t h = mako_hash_bytes(name.data ? name.data : "", name.len);
    int64_t now = mako_now_ms();
    for (size_t i = 0; i < MAKO_BACKEND_SLOTS; i++) {
        MakoJobSlot *s = &mako_job_slots[i];
        if (s->used && mako_backend_key_eq(s->hash, s->name, s->name_len, name)) {
            int64_t left = s->due_ms - now;
            return left > 0 ? left : 0;
        }
    }
    return -1;
}

static inline int64_t mako_job_cancel(MakoString name) {
    uint64_t h = mako_hash_bytes(name.data ? name.data : "", name.len);
    for (size_t i = 0; i < MAKO_BACKEND_SLOTS; i++) {
        MakoJobSlot *s = &mako_job_slots[i];
        if (s->used && mako_backend_key_eq(s->hash, s->name, s->name_len, name)) {
            free(s->name);
            s->used = 0;
            return 1;
        }
    }
    return 0;
}

/* ---- Connection pooling and load-balancing primitives ---- */
static int64_t mako_conn_pool_rr = 0;

static inline int64_t mako_conn_pool_slot(MakoString key, int64_t pool_size) {
    if (pool_size <= 0) return -1;
    uint64_t h = mako_hash_bytes(key.data ? key.data : "", key.len);
    return (int64_t)(h % (uint64_t)pool_size);
}

static inline int64_t mako_conn_pool_next(int64_t pool_size) {
    if (pool_size <= 0) return -1;
    int64_t next = mako_conn_pool_rr++;
    if (mako_conn_pool_rr < 0) mako_conn_pool_rr = 0;
    if (next < 0) next = 0;
    return next % pool_size;
}

static inline MakoString mako_lb_pick2(MakoString a, MakoString b, MakoString key) {
    MakoString chosen = mako_conn_pool_slot(key, 2) == 0 ? a : b;
    char *d = mako_backend_dup(chosen.data ? chosen.data : "", chosen.len);
    return (MakoString){d, chosen.len};
}

static inline MakoString mako_lb_pick3(MakoString a, MakoString b, MakoString c, MakoString key) {
    int64_t slot = mako_conn_pool_slot(key, 3);
    MakoString chosen = slot == 0 ? a : (slot == 1 ? b : c);
    char *d = mako_backend_dup(chosen.data ? chosen.data : "", chosen.len);
    return (MakoString){d, chosen.len};
}

#if !defined(MAKO_LOG_H)
static inline MakoString mako_slog_redact(MakoString value) {
    (void)value;
    return mako_str_from_cstr("[REDACTED]");
}

static inline void mako_slog_with_redacted(MakoString level, MakoString msg, MakoString key) {
    mako_slog_with(level, msg, key, mako_str_from_cstr("[REDACTED]"));
}
#endif

/* ---- Metrics (process-local counters/gauges/histograms) ---- */
static int64_t mako_metric_counters[64];
static int64_t mako_metric_gauges[64];
static int64_t mako_metric_hist_count[64];
static int64_t mako_metric_hist_sum[64];

static inline void mako_metric_inc(int64_t id) {
    if (id >= 0 && id < 64) mako_metric_counters[id]++;
}

static inline void mako_metric_add(int64_t id, int64_t delta) {
    if (id >= 0 && id < 64) mako_metric_counters[id] += delta;
}

static inline int64_t mako_metric_get(int64_t id) {
    if (id < 0 || id >= 64) return 0;
    return mako_metric_counters[id];
}

static inline void mako_gauge_set(int64_t id, int64_t value) {
    if (id >= 0 && id < 64) mako_metric_gauges[id] = value;
}

static inline void mako_gauge_add(int64_t id, int64_t delta) {
    if (id >= 0 && id < 64) mako_metric_gauges[id] += delta;
}

static inline int64_t mako_gauge_get(int64_t id) {
    if (id < 0 || id >= 64) return 0;
    return mako_metric_gauges[id];
}

static inline void mako_hist_observe(int64_t id, int64_t value) {
    if (id >= 0 && id < 64) {
        mako_metric_hist_count[id]++;
        mako_metric_hist_sum[id] += value;
    }
}

static inline int64_t mako_hist_count(int64_t id) {
    if (id < 0 || id >= 64) return 0;
    return mako_metric_hist_count[id];
}

static inline int64_t mako_hist_sum(int64_t id) {
    if (id < 0 || id >= 64) return 0;
    return mako_metric_hist_sum[id];
}

static inline int64_t mako_hist_avg(int64_t id) {
    if (id < 0 || id >= 64 || mako_metric_hist_count[id] == 0) return 0;
    return mako_metric_hist_sum[id] / mako_metric_hist_count[id];
}

static inline MakoString mako_metrics_export(void) {
    size_t cap = 8192;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        fprintf(stderr, "mako: OOM in metrics_export\n");
        abort();
    }
    size_t len = 0;
    for (int i = 0; i < 64; i++) {
        if (mako_metric_counters[i] == 0 && mako_metric_gauges[i] == 0 && mako_metric_hist_count[i] == 0) {
            continue;
        }
        int n = snprintf(
            buf + len,
            cap > len ? cap - len : 0,
            "mako_counter_%d %" PRId64 "\n"
            "mako_gauge_%d %" PRId64 "\n"
            "mako_hist_count_%d %" PRId64 "\n"
            "mako_hist_sum_%d %" PRId64 "\n",
            i, mako_metric_counters[i],
            i, mako_metric_gauges[i],
            i, mako_metric_hist_count[i],
            i, mako_metric_hist_sum[i]
        );
        if (n < 0) break;
        len += (size_t)n;
        if (len + 256 >= cap) {
            cap *= 2;
            char *next = (char *)realloc(buf, cap);
            if (!next) {
                free(buf);
                fprintf(stderr, "mako: OOM in metrics_export grow\n");
                abort();
            }
            buf = next;
        }
    }
    buf[len] = 0;
    MakoString out = {buf, len};
    return out;
}

/* OTLP/HTTP JSON metrics export (seed) for non-zero counter/gauge/hist slots. */
static inline MakoString mako_metrics_export_otlp_json(void) {
    size_t cap = 8192;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        fprintf(stderr, "mako: OOM in metrics_export_otlp_json\n");
        abort();
    }
    size_t len = 0;
    int n = snprintf(
        buf, cap,
        "{\"resourceMetrics\":[{\"resource\":{\"attributes\":["
        "{\"key\":\"service.name\",\"value\":{\"stringValue\":\"mako\"}}"
        "]},\"scopeMetrics\":[{\"scope\":{\"name\":\"mako.metrics\",\"version\":\"0.1.4\"},"
        "\"metrics\":["
    );
    if (n < 0) n = 0;
    len = (size_t)n;
    int first = 1;
    for (int i = 0; i < 64; i++) {
        if (mako_metric_counters[i] == 0 && mako_metric_gauges[i] == 0
            && mako_metric_hist_count[i] == 0) {
            continue;
        }
        if (len + 700 >= cap) {
            cap *= 2;
            char *next = (char *)realloc(buf, cap);
            if (!next) {
                free(buf);
                fprintf(stderr, "mako: OOM in metrics_export_otlp_json grow\n");
                abort();
            }
            buf = next;
        }
        if (mako_metric_counters[i] != 0) {
            n = snprintf(
                buf + len, cap > len ? cap - len : 0,
                "%s{\"name\":\"mako_counter_%d\",\"sum\":{\"dataPoints\":[{"
                "\"asInt\":\"%" PRId64 "\"}],\"aggregationTemporality\":2,"
                "\"isMonotonic\":true}}",
                first ? "" : ",", i, mako_metric_counters[i]
            );
            if (n > 0) {
                len += (size_t)n;
                first = 0;
            }
        }
        if (mako_metric_gauges[i] != 0) {
            n = snprintf(
                buf + len, cap > len ? cap - len : 0,
                "%s{\"name\":\"mako_gauge_%d\",\"gauge\":{\"dataPoints\":[{"
                "\"asInt\":\"%" PRId64 "\"}]}}",
                first ? "" : ",", i, mako_metric_gauges[i]
            );
            if (n > 0) {
                len += (size_t)n;
                first = 0;
            }
        }
        if (mako_metric_hist_count[i] != 0) {
            n = snprintf(
                buf + len, cap > len ? cap - len : 0,
                "%s{\"name\":\"mako_hist_%d\",\"histogram\":{\"dataPoints\":[{"
                "\"count\":\"%" PRId64 "\",\"sum\":%f}],"
                "\"aggregationTemporality\":2}}",
                first ? "" : ",", i, mako_metric_hist_count[i],
                (double)mako_metric_hist_sum[i]
            );
            if (n > 0) {
                len += (size_t)n;
                first = 0;
            }
        }
    }
    const char *tail = "]}]}}";
    size_t tlen = strlen(tail);
    if (len + tlen + 1 >= cap) {
        cap = len + tlen + 8;
        char *next = (char *)realloc(buf, cap);
        if (!next) {
            free(buf);
            fprintf(stderr, "mako: OOM in metrics_export_otlp_json tail\n");
            abort();
        }
        buf = next;
    }
    memcpy(buf + len, tail, tlen + 1);
    len += tlen;
    return (MakoString){buf, len};
}

/* Combined profile snapshot: process RSS/CPU + alloc track + scheduler counters. */
static inline MakoString mako_profile_snapshot_json(void) {
    char *d = (char *)malloc(768);
    if (!d) {
        fprintf(stderr, "mako: OOM in profile_snapshot_json\n");
        abort();
    }
    int n = snprintf(
        d,
        768,
        "{\"schema\":\"mako.profile_snapshot.v1\","
        "\"now_ms\":%" PRId64 ",\"now_ns\":%" PRId64 ","
        "\"rss_bytes\":%" PRId64 ","
        "\"cpu_user_us\":%" PRId64 ",\"cpu_sys_us\":%" PRId64 ","
        "\"alloc_live\":%" PRId64 ",\"alloc_high\":%" PRId64 ","
        "\"tasks_spawned\":%lld,\"tasks_joined\":%lld,"
        "\"channels_created\":%lld,\"channel_sends\":%lld,"
        "\"channel_recvs\":%lld,\"channel_peak_depth\":%lld,"
        "\"lock_waits\":%lld,\"lock_wait_ns\":%lld}",
        mako_now_ms(),
        mako_now_ns(),
        mako_process_rss_bytes(),
        mako_process_cpu_user_us(),
        mako_process_cpu_sys_us(),
        mako_alloc_track_live_bytes(),
        mako_alloc_track_high_bytes(),
        atomic_load_explicit(&mako_rt_tasks_spawned, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_tasks_joined, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channels_created, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channel_sends, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channel_recvs, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_channel_peak_depth, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_lock_waits, memory_order_relaxed),
        atomic_load_explicit(&mako_rt_lock_wait_ns, memory_order_relaxed)
    );
    if (n < 0) n = 0;
    return (MakoString){d, (size_t)n};
}

/* ---- Sampling CPU profiler seed (cooperative + optional SIGPROF) ----
 * Not a continuous pprof product. Ring of stack snapshots + CPU us deltas.
 */
#define MAKO_PROF_SAMPLE_MAX 64
#define MAKO_PROF_STACK_CAP 384
#define MAKO_PROF_LABEL_CAP 48

typedef struct {
    int64_t ts_ns;
    int64_t cpu_user_us;
    int64_t tid; /* pthread / thread id seed for multi-thread samples */
    char label[MAKO_PROF_LABEL_CAP];
    char stack[MAKO_PROF_STACK_CAP];
    int used;
} MakoProfSample;

static MakoProfSample mako_prof_samples[MAKO_PROF_SAMPLE_MAX];
static int mako_prof_sample_head = 0;
static int mako_prof_sample_count = 0; /* total recorded (may exceed MAX) */
static int64_t mako_prof_start_cpu_us = 0;
static int64_t mako_prof_start_ns = 0;
static volatile int mako_prof_active = 0;
#if !defined(_WIN32) && !defined(MAKO_WASI)
static struct sigaction mako_prof_old_sa;
static int mako_prof_sa_saved = 0;
#endif

static inline int64_t mako_prof_current_tid(void) {
#if defined(_WIN32) || defined(MAKO_WASI)
    return 0;
#else
    return (int64_t)(uintptr_t)pthread_self();
#endif
}

static inline void mako_prof_store_sample(const char *label, const char *stack) {
    int i = mako_prof_sample_head % MAKO_PROF_SAMPLE_MAX;
    mako_prof_sample_head++;
    mako_prof_sample_count++;
    MakoProfSample *s = &mako_prof_samples[i];
    s->used = 1;
    s->ts_ns = mako_now_ns();
    s->cpu_user_us = mako_process_cpu_user_us();
    s->tid = mako_prof_current_tid();
    s->label[0] = 0;
    s->stack[0] = 0;
    if (label && label[0]) {
        size_t n = strlen(label);
        if (n >= MAKO_PROF_LABEL_CAP) n = MAKO_PROF_LABEL_CAP - 1;
        memcpy(s->label, label, n);
        s->label[n] = 0;
    }
    if (stack && stack[0]) {
        size_t n = strlen(stack);
        if (n >= MAKO_PROF_STACK_CAP) n = MAKO_PROF_STACK_CAP - 1;
        memcpy(s->stack, stack, n);
        s->stack[n] = 0;
        /* Collapse newlines for single-line JSON field. */
        for (size_t k = 0; k < n; k++) {
            if (s->stack[k] == '\n' || s->stack[k] == '\r' || s->stack[k] == '"')
                s->stack[k] = ' ';
        }
    }
}

static inline int64_t mako_profile_sample_clear(void) {
    memset(mako_prof_samples, 0, sizeof(mako_prof_samples));
    mako_prof_sample_head = 0;
    mako_prof_sample_count = 0;
    return 1;
}

/* Capture one stack sample now (cooperative; always available). */
static inline int64_t mako_profile_sample_once(MakoString label) {
    MakoString st = mako_stack_trace();
    char lab[MAKO_PROF_LABEL_CAP];
    lab[0] = 0;
    if (label.data && label.len > 0) {
        size_t n = label.len < MAKO_PROF_LABEL_CAP - 1 ? label.len : MAKO_PROF_LABEL_CAP - 1;
        memcpy(lab, label.data, n);
        lab[n] = 0;
    }
    mako_prof_store_sample(lab, st.data ? st.data : "");
    mako_str_free(st);
    return 1;
}

static inline int64_t mako_profile_sample_count(void) {
    return mako_prof_sample_count;
}

static inline int64_t mako_profile_sample_len(void) {
    int n = 0;
    for (int i = 0; i < MAKO_PROF_SAMPLE_MAX; i++)
        if (mako_prof_samples[i].used) n++;
    return n;
}

#if !defined(_WIN32) && !defined(MAKO_WASI) && (defined(__GLIBC__) || defined(__APPLE__))
static void mako_prof_sigprof_handler(int sig) {
    (void)sig;
    if (!mako_prof_active) return;
    void *frames[16];
    int n = backtrace(frames, 16);
    char **syms = backtrace_symbols(frames, n);
    char buf[MAKO_PROF_STACK_CAP];
    size_t len = 0;
    buf[0] = 0;
    if (syms) {
        for (int i = 0; i < n && len + 2 < sizeof(buf); i++) {
            size_t sl = strlen(syms[i]);
            if (len + sl + 2 >= sizeof(buf)) break;
            if (len) buf[len++] = ' ';
            memcpy(buf + len, syms[i], sl);
            len += sl;
            buf[len] = 0;
        }
        free(syms);
    }
    mako_prof_store_sample("sigprof", buf);
}
#endif

/* Start interval sampling. interval_ms: 1..1000 (POSIX SIGPROF); elsewhere no-op active flag. */
static inline int64_t mako_profile_sample_start(int64_t interval_ms) {
    if (interval_ms < 1) interval_ms = 10;
    if (interval_ms > 1000) interval_ms = 1000;
    mako_prof_active = 1;
    mako_prof_start_cpu_us = mako_process_cpu_user_us();
    mako_prof_start_ns = mako_now_ns();
#if !defined(_WIN32) && !defined(MAKO_WASI) && (defined(__GLIBC__) || defined(__APPLE__))
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = mako_prof_sigprof_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, &mako_prof_old_sa) == 0) mako_prof_sa_saved = 1;
    struct itimerval it;
    memset(&it, 0, sizeof(it));
    it.it_interval.tv_sec = (time_t)(interval_ms / 1000);
    it.it_interval.tv_usec = (suseconds_t)((interval_ms % 1000) * 1000);
    it.it_value = it.it_interval;
    if (setitimer(ITIMER_PROF, &it, NULL) != 0) {
        /* Fall back: still allow cooperative samples. */
    }
    return 1;
#else
    (void)interval_ms;
    return 1;
#endif
}

/* Stop sampling; return number of samples recorded since start (or total). */
static inline int64_t mako_profile_sample_stop(void) {
    mako_prof_active = 0;
#if !defined(_WIN32) && !defined(MAKO_WASI) && (defined(__GLIBC__) || defined(__APPLE__))
    struct itimerval it;
    memset(&it, 0, sizeof(it));
    setitimer(ITIMER_PROF, &it, NULL);
    if (mako_prof_sa_saved) {
        sigaction(SIGPROF, &mako_prof_old_sa, NULL);
        mako_prof_sa_saved = 0;
    }
#endif
    return mako_prof_sample_count;
}

/* CPU user us spent while sampling was active (approx). */
static inline int64_t mako_profile_sample_cpu_us(void) {
    int64_t now = mako_process_cpu_user_us();
    if (mako_prof_start_cpu_us <= 0) return 0;
    int64_t d = now - mako_prof_start_cpu_us;
    return d > 0 ? d : 0;
}

static inline int64_t mako_profile_sample_wall_ns(void) {
    if (mako_prof_start_ns <= 0) return 0;
    int64_t d = mako_now_ns() - mako_prof_start_ns;
    return d > 0 ? d : 0;
}

static inline MakoString mako_profile_samples_json(void) {
    size_t cap = 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) abort();
    size_t len = 0;
    int n = snprintf(
        buf, cap,
        "{\"schema\":\"mako.profile_samples.v1\",\"count\":%d,\"len\":%" PRId64
        ",\"cpu_user_us\":%" PRId64 ",\"wall_ns\":%" PRId64 ",\"active\":%d,\"samples\":[",
        mako_prof_sample_count,
        mako_profile_sample_len(),
        mako_profile_sample_cpu_us(),
        mako_profile_sample_wall_ns(),
        mako_prof_active
    );
    if (n > 0) len = (size_t)n;
    int first = 1;
    for (int i = 0; i < MAKO_PROF_SAMPLE_MAX; i++) {
        if (!mako_prof_samples[i].used) continue;
        MakoProfSample *s = &mako_prof_samples[i];
        if (len + MAKO_PROF_STACK_CAP + 128 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) {
                free(buf);
                abort();
            }
            buf = nb;
        }
        n = snprintf(
            buf + len, cap - len,
            "%s{\"ts_ns\":%" PRId64 ",\"cpu_user_us\":%" PRId64
            ",\"tid\":%" PRId64 ",\"label\":\"%s\",\"stack\":\"%s\"}",
            first ? "" : ",",
            s->ts_ns,
            s->cpu_user_us,
            s->tid,
            s->label,
            s->stack
        );
        if (n > 0) len += (size_t)n;
        first = 0;
    }
    if (len + 4 >= cap) {
        char *nb = (char *)realloc(buf, len + 8);
        if (!nb) {
            free(buf);
            abort();
        }
        buf = nb;
    }
    buf[len++] = ']';
    buf[len++] = '}';
    buf[len] = 0;
    return (MakoString){buf, len};
}

/* Folded-stack / pprof-text seed: "stack;frames label count" lines (not protobuf pprof). */
static inline MakoString mako_profile_samples_pprof_text(void) {
    size_t cap = 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) abort();
    size_t len = 0;
    int n = snprintf(buf, cap, "# mako.profile_pprof_text.v1 samples=%d\n", mako_prof_sample_count);
    if (n > 0) len = (size_t)n;
    for (int i = 0; i < MAKO_PROF_SAMPLE_MAX; i++) {
        if (!mako_prof_samples[i].used) continue;
        MakoProfSample *s = &mako_prof_samples[i];
        if (len + MAKO_PROF_STACK_CAP + 96 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) {
                free(buf);
                abort();
            }
            buf = nb;
        }
        /* spaces in stack already collapsed; emit stack|label tid count=1 */
        n = snprintf(
            buf + len, cap - len,
            "%s|%s tid=%" PRId64 " 1\n",
            s->stack[0] ? s->stack : "(no-stack)",
            s->label[0] ? s->label : "sample",
            s->tid
        );
        if (n > 0) len += (size_t)n;
    }
    buf[len] = 0;
    return (MakoString){buf, len};
}

/* Continuous-ish export: pprof-text body suitable for GET /debug/pprof/text. */
static inline MakoString mako_profile_pprof_http_body(void) {
    MakoString body = mako_profile_samples_pprof_text();
    size_t cap = body.len + 128;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        mako_str_free(body);
        abort();
    }
    int n = snprintf(
        buf, cap,
        "HTTP/1.0 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n%.*s",
        body.len, (int)body.len, body.data ? body.data : ""
    );
    mako_str_free(body);
    if (n < 0) {
        free(buf);
        return mako_str_from_cstr("");
    }
    return (MakoString){buf, (size_t)n};
}

/* Path router seed for a tiny profile exporter: returns body for known paths. */
static inline MakoString mako_profile_http_route(MakoString path) {
    if (!path.data) return mako_str_from_cstr("HTTP/1.0 404 Not Found\r\n\r\n");
    if (path.len >= 17 && memcmp(path.data, "/debug/pprof/text", 17) == 0) {
        return mako_profile_pprof_http_body();
    }
    if (path.len >= 17 && memcmp(path.data, "/debug/pprof/json", 17) == 0) {
        MakoString body = mako_profile_samples_json();
        size_t cap = body.len + 128;
        char *buf = (char *)malloc(cap);
        if (!buf) {
            mako_str_free(body);
            abort();
        }
        int n = snprintf(
            buf, cap,
            "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n%.*s",
            body.len, (int)body.len, body.data ? body.data : ""
        );
        mako_str_free(body);
        if (n < 0) {
            free(buf);
            return mako_str_from_cstr("");
        }
        return (MakoString){buf, (size_t)n};
    }
    if (path.len >= 14 && memcmp(path.data, "/debug/profile", 14) == 0) {
        MakoString body = mako_profile_snapshot_json();
        size_t cap = body.len + 128;
        char *buf = (char *)malloc(cap);
        if (!buf) {
            mako_str_free(body);
            abort();
        }
        int n = snprintf(
            buf, cap,
            "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n%.*s",
            body.len, (int)body.len, body.data ? body.data : ""
        );
        mako_str_free(body);
        if (n < 0) {
            free(buf);
            return mako_str_from_cstr("");
        }
        return (MakoString){buf, (size_t)n};
    }
    return mako_str_from_cstr(
        "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\n\r\n"
        "paths: /debug/pprof/text /debug/pprof/json /debug/profile\n"
    );
}

/* Distinct thread ids observed in the ring (continuous multi-thread seed). */
static inline int64_t mako_profile_sample_thread_count(void) {
    int64_t seen[MAKO_PROF_SAMPLE_MAX];
    int nseen = 0;
    for (int i = 0; i < MAKO_PROF_SAMPLE_MAX; i++) {
        if (!mako_prof_samples[i].used) continue;
        int64_t tid = mako_prof_samples[i].tid;
        int found = 0;
        for (int j = 0; j < nseen; j++) {
            if (seen[j] == tid) {
                found = 1;
                break;
            }
        }
        if (!found && nseen < MAKO_PROF_SAMPLE_MAX) seen[nseen++] = tid;
    }
    return nseen;
}

/* Prometheus text exposition (0.0.4-ish) for the same 64 slots. */
static inline MakoString mako_metrics_export_prom(void) {
    size_t cap = 8192;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        fprintf(stderr, "mako: OOM in metrics_export_prom\n");
        abort();
    }
    size_t len = 0;
    for (int i = 0; i < 64; i++) {
        if (mako_metric_counters[i] == 0 && mako_metric_gauges[i] == 0
            && mako_metric_hist_count[i] == 0) {
            continue;
        }
        int n = snprintf(
            buf + len,
            cap > len ? cap - len : 0,
            "# HELP mako_counter_%d Mako process counter slot %d\n"
            "# TYPE mako_counter_%d counter\n"
            "mako_counter_%d %" PRId64 "\n"
            "# HELP mako_gauge_%d Mako process gauge slot %d\n"
            "# TYPE mako_gauge_%d gauge\n"
            "mako_gauge_%d %" PRId64 "\n"
            "# HELP mako_hist_sum_%d Mako histogram sum slot %d\n"
            "# TYPE mako_hist_sum_%d counter\n"
            "mako_hist_sum_%d %" PRId64 "\n"
            "# HELP mako_hist_count_%d Mako histogram count slot %d\n"
            "# TYPE mako_hist_count_%d counter\n"
            "mako_hist_count_%d %" PRId64 "\n",
            i, i, i, i, mako_metric_counters[i],
            i, i, i, i, mako_metric_gauges[i],
            i, i, i, i, mako_metric_hist_sum[i],
            i, i, i, i, mako_metric_hist_count[i]
        );
        if (n < 0) break;
        len += (size_t)n;
        if (len + 512 >= cap) {
            cap *= 2;
            char *next = (char *)realloc(buf, cap);
            if (!next) {
                free(buf);
                fprintf(stderr, "mako: OOM in metrics_export_prom grow\n");
                abort();
            }
            buf = next;
        }
    }
    buf[len] = 0;
    return (MakoString){buf, len};
}

/* ---- JSON seed (escape + wrap object with one string field) ---- */
static inline MakoString mako_json_escape(MakoString s) {
    size_t cap = s.len * 2 + 8;
    char *d = (char *)malloc(cap);
    size_t j = 0;
    for (size_t i = 0; i < s.len; i++) {
        char c = s.data[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= cap) { cap *= 2; d = (char *)realloc(d, cap); }
            d[j++] = '\\';
            d[j++] = c;
        } else if (c == '\n') {
            if (j + 2 >= cap) { cap *= 2; d = (char *)realloc(d, cap); }
            d[j++] = '\\';
            d[j++] = 'n';
        } else {
            if (j + 1 >= cap) { cap *= 2; d = (char *)realloc(d, cap); }
            d[j++] = c;
        }
    }
    d[j] = 0;
    return (MakoString){d, j};
}

static inline MakoString mako_json_object_str(MakoString key, MakoString val) {
    MakoString ek = mako_json_escape(key);
    MakoString ev = mako_json_escape(val);
    size_t n = ek.len + ev.len + 16;
    char *d = (char *)malloc(n);
    int wrote = snprintf(d, n, "{\"%s\":\"%s\"}", ek.data, ev.data);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline MakoString mako_json_si(
    MakoString k1,
    MakoString v1,
    MakoString k2,
    int64_t v2
) {
    MakoString e1 = mako_json_escape(k1);
    MakoString ev = mako_json_escape(v1);
    MakoString e2 = mako_json_escape(k2);
    size_t n = e1.len + ev.len + e2.len + 48;
    char *d = (char *)malloc(n);
    int wrote = snprintf(
        d,
        n,
        "{\"%s\":\"%s\",\"%s\":%lld}",
        e1.data,
        ev.data,
        e2.data,
        (long long)v2
    );
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline MakoString mako_json_ss(
    MakoString k1,
    MakoString v1,
    MakoString k2,
    MakoString v2
) {
    MakoString e1 = mako_json_escape(k1);
    MakoString a = mako_json_escape(v1);
    MakoString e2 = mako_json_escape(k2);
    MakoString b = mako_json_escape(v2);
    size_t n = e1.len + a.len + e2.len + b.len + 32;
    char *d = (char *)malloc(n);
    int wrote = snprintf(
        d,
        n,
        "{\"%s\":\"%s\",\"%s\":\"%s\"}",
        e1.data,
        a.data,
        e2.data,
        b.data
    );
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline MakoString mako_json_i(MakoString key, int64_t val) {
    MakoString ek = mako_json_escape(key);
    size_t n = ek.len + 32;
    char *d = (char *)malloc(n);
    int wrote = snprintf(d, n, "{\"%s\":%lld}", ek.data, (long long)val);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

/* Parse: returns 1 if haystack contains "\"key\":\"value\"" substring match (seed). */
static inline int64_t mako_json_has_string(MakoString json, MakoString key, MakoString expect) {
    MakoString needle_key = mako_json_escape(key);
    MakoString needle_val = mako_json_escape(expect);
    size_t n = needle_key.len + needle_val.len + 8;
    char *pat = (char *)malloc(n);
    snprintf(pat, n, "\"%s\":\"%s\"", needle_key.data, needle_val.data);
    int found = strstr(json.data ? json.data : "", pat) != NULL;
    free(pat);
    return found ? 1 : 0;
}

/* Extract string value for "key":"..." (Partial — no full JSON parser). */
static inline MakoString mako_json_get_string(MakoString json, MakoString key) {
    MakoString ek = mako_json_escape(key);
    size_t n = ek.len + 8;
    char *pat = (char *)malloc(n);
    snprintf(pat, n, "\"%s\":\"", ek.data);
    const char *src = json.data ? json.data : "";
    const char *p = strstr(src, pat);
    free(pat);
    if (!p) return mako_str_from_cstr("");
    p += ek.len + 4; /* past "key":" */
    const char *end = p;
    while (*end && *end != '"') {
        if (*end == '\\' && end[1]) end += 2;
        else end++;
    }
    size_t len = (size_t)(end - p);
    char *d = (char *)malloc(len + 1);
    memcpy(d, p, len);
    d[len] = 0;
    return (MakoString){d, len};
}

/* Extract int value for "key":<digits> (Partial). */
static inline int64_t mako_json_get_int(MakoString json, MakoString key) {
    MakoString ek = mako_json_escape(key);
    size_t n = ek.len + 8;
    char *pat = (char *)malloc(n);
    snprintf(pat, n, "\"%s\":", ek.data);
    const char *src = json.data ? json.data : "";
    const char *p = strstr(src, pat);
    free(pat);
    if (!p) return 0;
    p += ek.len + 3; /* past "key": */
    while (*p == ' ' || *p == '\t') p++;
    return (int64_t)strtoll(p, NULL, 10);
}

/* Wrap an already-serialized JSON value under a key: {"key":INNER}. */
static inline MakoString mako_json_nest(MakoString key, MakoString inner) {
    MakoString ek = mako_json_escape(key);
    size_t n = ek.len + inner.len + 16;
    char *d = (char *)malloc(n);
    int wrote = snprintf(
        d,
        n,
        "{\"%s\":%s}",
        ek.data,
        inner.data ? inner.data : "null"
    );
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

/* Extract nested object/array value for "key":{...} or "key":[...] (brace-matched). */
static inline MakoString mako_json_get_object(MakoString json, MakoString key) {
    MakoString ek = mako_json_escape(key);
    size_t n = ek.len + 8;
    char *pat = (char *)malloc(n);
    snprintf(pat, n, "\"%s\":", ek.data);
    const char *src = json.data ? json.data : "";
    const char *p = strstr(src, pat);
    free(pat);
    if (!p) return mako_str_from_cstr("");
    p += ek.len + 3;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '{' && *p != '[') return mako_str_from_cstr("");
    char open = *p;
    char close = (open == '{') ? '}' : ']';
    int depth = 0;
    int in_str = 0;
    const char *start = p;
    for (const char *q = p; *q; q++) {
        if (in_str) {
            if (*q == '\\' && q[1]) {
                q++;
                continue;
            }
            if (*q == '"') in_str = 0;
            continue;
        }
        if (*q == '"') {
            in_str = 1;
            continue;
        }
        if (*q == open) depth++;
        else if (*q == close) {
            depth--;
            if (depth == 0) {
                size_t len = (size_t)(q - start + 1);
                char *d = (char *)malloc(len + 1);
                memcpy(d, start, len);
                d[len] = 0;
                return (MakoString){d, len};
            }
        }
    }
    return mako_str_from_cstr("");
}

/* Nested path: json["k1"]["k2"] as string (Partial). */
static inline MakoString mako_json_path_string(
    MakoString json,
    MakoString k1,
    MakoString k2
) {
    MakoString obj = mako_json_get_object(json, k1);
    MakoString out = mako_json_get_string(obj, k2);
    return out;
}

static inline int64_t mako_json_path_int(
    MakoString json,
    MakoString k1,
    MakoString k2
) {
    MakoString obj = mako_json_get_object(json, k1);
    return mako_json_get_int(obj, k2);
}

/* ---- JSON arrays (Partial — top-level [...] of ints/strings) ---- */

static inline int64_t mako_json_array_len(MakoString json) {
    const char *s = json.data ? json.data : "";
    while (*s == ' ' || *s == '\t') s++;
    if (*s != '[') return -1;
    s++;
    int64_t count = 0;
    int depth = 1;
    int in_str = 0;
    int saw = 0;
    for (; *s; s++) {
        if (in_str) {
            if (*s == '\\' && s[1]) {
                s++;
                continue;
            }
            if (*s == '"') in_str = 0;
            continue;
        }
        if (*s == '"') {
            in_str = 1;
            saw = 1;
            continue;
        }
        if (*s == '[' || *s == '{') {
            depth++;
            saw = 1;
            continue;
        }
        if (*s == ']' || *s == '}') {
            depth--;
            if (depth == 0) {
                if (saw) count++;
                return count;
            }
            continue;
        }
        if (*s == ',' && depth == 1) {
            count++;
            saw = 0;
            continue;
        }
        if (*s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') saw = 1;
    }
    return -1;
}

/* Return pointer/len of the idx-th top-level array element (no alloc). */
static inline int mako_json_array_elem(
    MakoString json,
    int64_t idx,
    const char **out,
    size_t *out_len
) {
    const char *s = json.data ? json.data : "";
    while (*s == ' ' || *s == '\t') s++;
    if (*s != '[') return 0;
    s++;
    int64_t cur = 0;
    int depth = 1;
    int in_str = 0;
    const char *elem = s;
    for (; *s; s++) {
        if (in_str) {
            if (*s == '\\' && s[1]) {
                s++;
                continue;
            }
            if (*s == '"') in_str = 0;
            continue;
        }
        if (*s == '"') {
            in_str = 1;
            continue;
        }
        if (*s == '[' || *s == '{') {
            depth++;
            continue;
        }
        if (*s == ']' || *s == '}') {
            depth--;
            if (depth == 0) {
                if (cur == idx) {
                    const char *e = s;
                    while (elem < e && (*elem == ' ' || *elem == '\t')) elem++;
                    while (e > elem && (e[-1] == ' ' || e[-1] == '\t')) e--;
                    *out = elem;
                    *out_len = (size_t)(e - elem);
                    return 1;
                }
                return 0;
            }
            continue;
        }
        if (*s == ',' && depth == 1) {
            if (cur == idx) {
                const char *e = s;
                while (elem < e && (*elem == ' ' || *elem == '\t')) elem++;
                while (e > elem && (e[-1] == ' ' || e[-1] == '\t')) e--;
                *out = elem;
                *out_len = (size_t)(e - elem);
                return 1;
            }
            cur++;
            elem = s + 1;
        }
    }
    return 0;
}

static inline int64_t mako_json_array_get_int(MakoString json, int64_t idx) {
    const char *p = NULL;
    size_t n = 0;
    if (!mako_json_array_elem(json, idx, &p, &n) || !p || n == 0) return 0;
    return (int64_t)strtoll(p, NULL, 10);
}

static inline MakoString mako_json_array_get_string(MakoString json, int64_t idx) {
    const char *p = NULL;
    size_t n = 0;
    if (!mako_json_array_elem(json, idx, &p, &n) || !p || n < 2 || p[0] != '"')
        return mako_str_from_cstr("");
    /* strip quotes */
    const char *start = p + 1;
    const char *end = p + n - 1;
    if (*end != '"') return mako_str_from_cstr("");
    size_t len = (size_t)(end - start);
    char *d = (char *)malloc(len + 1);
    memcpy(d, start, len);
    d[len] = 0;
    return (MakoString){d, len};
}

static inline MakoString mako_json_array_ints3(int64_t a, int64_t b, int64_t c) {
    char *d = (char *)malloc(96);
    int wrote = snprintf(d, 96, "[%lld,%lld,%lld]", (long long)a, (long long)b, (long long)c);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline MakoString mako_json_array_strings2(MakoString a, MakoString b) {
    MakoString ea = mako_json_escape(a);
    MakoString eb = mako_json_escape(b);
    size_t n = ea.len + eb.len + 16;
    char *d = (char *)malloc(n);
    int wrote = snprintf(d, n, "[\"%s\",\"%s\"]", ea.data, eb.data);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

/* Append an int to a JSON array string (rebuild). */
static inline MakoString mako_json_array_push_string(MakoString arr, MakoString v) {
    const char *s = arr.data ? arr.data : "[]";
    size_t al = arr.len ? arr.len : 2;
    if (al < 2 || s[0] != '[' || s[al - 1] != ']') return mako_str_from_cstr("[]");
    MakoString ev = mako_json_escape(v);
    int empty = 1;
    for (size_t i = 1; i + 1 < al; i++) {
        if (s[i] != ' ' && s[i] != '\t') { empty = 0; break; }
    }
    size_t n = al + ev.len + 8;
    char *d = (char *)malloc(n);
    int wrote;
    if (empty)
        wrote = snprintf(d, n, "[\"%s\"]", ev.data);
    else
        wrote = snprintf(d, n, "%.*s,\"%s\"]", (int)(al - 1), s, ev.data);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline MakoString mako_json_object_from_map_ss(MakoMapSS *m) {
    if (!m) return mako_str_from_cstr("{}");
    size_t cap = 64;
    char *d = (char *)malloc(cap);
    size_t len = 0;
    d[len++] = '{';
    int first = 1;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] != MAKO_MAP_FULL) continue;
        MakoString ek = mako_json_escape(m->keys[i]);
        MakoString ev = mako_json_escape(m->vals[i]);
        size_t need = len + ek.len + ev.len + 8;
        if (need + 1 > cap) {
            while (need + 1 > cap) cap *= 2;
            d = (char *)realloc(d, cap);
        }
        if (!first) d[len++] = ',';
        first = 0;
        d[len++] = '"';
        memcpy(d + len, ek.data, ek.len); len += ek.len;
        d[len++] = '"';
        d[len++] = ':';
        d[len++] = '"';
        memcpy(d + len, ev.data, ev.len); len += ev.len;
        d[len++] = '"';
    }
    d[len++] = '}';
    d[len] = 0;
    return (MakoString){d, len};
}

static inline MakoString mako_json_array_push_int(MakoString arr, int64_t v) {
    const char *s = arr.data ? arr.data : "[]";
    size_t al = arr.len ? arr.len : 2;
    if (al < 2 || s[0] != '[' || s[al - 1] != ']') return mako_str_from_cstr("[]");
    int empty = 1;
    for (size_t i = 1; i + 1 < al; i++) {
        if (s[i] != ' ' && s[i] != '\t') {
            empty = 0;
            break;
        }
    }
    size_t n = al + 32;
    char *d = (char *)malloc(n);
    int wrote;
    if (empty)
        wrote = snprintf(d, n, "[%lld]", (long long)v);
    else
        wrote = snprintf(d, n, "%.*s,%lld]", (int)(al - 1), s, (long long)v);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

/* Merge two JSON objects: strip outer braces and join. Partial — flat objects only. */
static inline MakoString mako_json_merge(MakoString a, MakoString b) {
    const char *ad = a.data ? a.data : "{}";
    const char *bd = b.data ? b.data : "{}";
    size_t al = a.len ? a.len : 2;
    size_t bl = b.len ? b.len : 2;
    /* expect { ... } */
    if (al < 2 || bl < 2 || ad[0] != '{' || bd[0] != '{')
        return mako_str_from_cstr("{}");
    size_t n = al + bl + 4;
    char *d = (char *)malloc(n);
    /* {"a":1} + {"b":2} -> {"a":1,"b":2} */
    size_t ai = al - 1; /* index of closing } */
    size_t bi = 1;      /* skip opening { of b */
    while (bi < bl && (bd[bi] == ' ' || bd[bi] == '\t')) bi++;
    int wrote = snprintf(d, n, "%.*s,%s", (int)ai, ad, bd + bi);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline MakoString mako_openapi_route(MakoString method, MakoString path, MakoString summary) {
    MakoString em = mako_json_escape(method);
    for (size_t i = 0; i < em.len; i++) {
        if (em.data[i] >= 'A' && em.data[i] <= 'Z') em.data[i] = (char)(em.data[i] - 'A' + 'a');
    }
    MakoString ep = mako_json_escape(path);
    MakoString es = mako_json_escape(summary);
    size_t n = em.len + ep.len + es.len + 128;
    char *d = (char *)malloc(n);
    if (!d) mako_abort("openapi_route: out of memory");
    int wrote = snprintf(
        d,
        n,
        "{\"%s\":{\"%s\":{\"summary\":\"%s\",\"responses\":{\"200\":{\"description\":\"OK\"}}}}}",
        ep.data,
        em.data,
        es.data
    );
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline MakoString mako_openapi_doc(MakoString title, MakoString version, MakoString paths) {
    MakoString et = mako_json_escape(title);
    MakoString ev = mako_json_escape(version);
    const char *p = paths.data && paths.len ? paths.data : "{}";
    size_t n = et.len + ev.len + paths.len + 128;
    char *d = (char *)malloc(n);
    if (!d) mako_abort("openapi_doc: out of memory");
    int wrote = snprintf(
        d,
        n,
        "{\"openapi\":\"3.1.0\",\"info\":{\"title\":\"%s\",\"version\":\"%s\"},\"paths\":%s}",
        et.data,
        ev.data,
        p
    );
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline int mako_gql_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static inline int mako_gql_is_ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static inline int mako_gql_is_ident_char(char c) {
    return mako_gql_is_ident_start(c) || (c >= '0' && c <= '9');
}

static inline size_t mako_gql_skip_ws(MakoString s, size_t i) {
    while (i < s.len && s.data && mako_gql_is_space(s.data[i])) i++;
    return i;
}

static inline size_t mako_gql_skip_ident(MakoString s, size_t i) {
    while (i < s.len && s.data && mako_gql_is_ident_char(s.data[i])) i++;
    return i;
}

static inline int mako_gql_ident_eq(MakoString s, size_t start, size_t end, MakoString want) {
    size_t n = end > start ? end - start : 0;
    return n == want.len && n > 0 && s.data && want.data && memcmp(s.data + start, want.data, n) == 0;
}

static inline int64_t mako_graphql_is_mutation(MakoString query) {
    size_t i = mako_gql_skip_ws(query, 0);
    if (i >= query.len || !query.data || !mako_gql_is_ident_start(query.data[i])) return 0;
    size_t end = mako_gql_skip_ident(query, i);
    return mako_gql_ident_eq(query, i, end, mako_str_from_cstr("mutation")) ? 1 : 0;
}

static inline MakoString mako_graphql_field(MakoString query) {
    if (!query.data || query.len == 0) return mako_str_from_cstr("");
    size_t i = 0;
    while (i < query.len && query.data[i] != '{') i++;
    if (i >= query.len) return mako_str_from_cstr("");
    i = mako_gql_skip_ws(query, i + 1);
    if (i >= query.len || !mako_gql_is_ident_start(query.data[i])) return mako_str_from_cstr("");
    size_t end = mako_gql_skip_ident(query, i);
    return mako_str_slice(query, (int64_t)i, (int64_t)end);
}

static inline MakoString mako_graphql_arg(MakoString query, MakoString name) {
    if (!query.data || query.len == 0 || !name.data || name.len == 0) return mako_str_from_cstr("");
    for (size_t i = 0; i + name.len < query.len; i++) {
        if (!mako_gql_is_ident_start(query.data[i])) continue;
        size_t end = mako_gql_skip_ident(query, i);
        if (!mako_gql_ident_eq(query, i, end, name)) {
            i = end;
            continue;
        }
        size_t p = mako_gql_skip_ws(query, end);
        if (p >= query.len || query.data[p] != ':') {
            i = end;
            continue;
        }
        p = mako_gql_skip_ws(query, p + 1);
        if (p >= query.len) return mako_str_from_cstr("");
        if (query.data[p] == '"') {
            size_t start = p + 1;
            p = start;
            while (p < query.len) {
                if (query.data[p] == '"' && (p == start || query.data[p - 1] != '\\')) {
                    return mako_str_slice(query, (int64_t)start, (int64_t)p);
                }
                p++;
            }
            return mako_str_from_cstr("");
        }
        size_t start = p;
        while (p < query.len) {
            char c = query.data[p];
            if (mako_gql_is_space(c) || c == ',' || c == ')' || c == '}' || c == ']') break;
            p++;
        }
        return mako_str_slice(query, (int64_t)start, (int64_t)p);
    }
    return mako_str_from_cstr("");
}

static inline MakoString mako_graphql_data(MakoString field, MakoString json) {
    MakoString ef = mako_json_escape(field);
    const char *payload = json.data && json.len ? json.data : "null";
    size_t payload_len = json.data && json.len ? json.len : 4;
    size_t n = ef.len + payload_len + 32;
    char *d = (char *)malloc(n);
    if (!d) mako_abort("graphql_data: out of memory");
    int wrote = snprintf(d, n, "{\"data\":{\"%s\":%.*s}}", ef.data, (int)payload_len, payload);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline MakoString mako_graphql_error(MakoString message) {
    MakoString em = mako_json_escape(message);
    size_t n = em.len + 40;
    char *d = (char *)malloc(n);
    if (!d) mako_abort("graphql_error: out of memory");
    int wrote = snprintf(d, n, "{\"errors\":[{\"message\":\"%s\"}]}", em.data);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline MakoString mako_graphql_request(MakoString query) {
    MakoString eq = mako_json_escape(query);
    size_t n = eq.len + 16;
    char *d = (char *)malloc(n);
    if (!d) mako_abort("graphql_request: out of memory");
    int wrote = snprintf(d, n, "{\"query\":\"%s\"}", eq.data);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline int64_t mako_graphql_is_query(MakoString query) {
    size_t i = mako_gql_skip_ws(query, 0);
    if (i >= query.len || !query.data) return 0;
    /* bare `{` selection set counts as query */
    if (query.data[i] == '{') return 1;
    if (!mako_gql_is_ident_start(query.data[i])) return 0;
    size_t end = mako_gql_skip_ident(query, i);
    return mako_gql_ident_eq(query, i, end, mako_str_from_cstr("query")) ? 1 : 0;
}

static inline MakoString mako_graphql_operation_name(MakoString query) {
    size_t i = mako_gql_skip_ws(query, 0);
    if (i >= query.len || !query.data) return mako_str_from_cstr("");
    if (!mako_gql_is_ident_start(query.data[i])) return mako_str_from_cstr("");
    size_t end = mako_gql_skip_ident(query, i);
    int is_op = mako_gql_ident_eq(query, i, end, mako_str_from_cstr("query"))
        || mako_gql_ident_eq(query, i, end, mako_str_from_cstr("mutation"))
        || mako_gql_ident_eq(query, i, end, mako_str_from_cstr("subscription"));
    if (!is_op) return mako_str_from_cstr("");
    i = mako_gql_skip_ws(query, end);
    if (i >= query.len || !mako_gql_is_ident_start(query.data[i])) return mako_str_from_cstr("");
    end = mako_gql_skip_ident(query, i);
    return mako_str_slice(query, (int64_t)i, (int64_t)end);
}

static inline MakoString mako_graphql_request_vars(MakoString query, MakoString vars_json) {
    MakoString eq = mako_json_escape(query);
    const char *vj = (vars_json.data && vars_json.len) ? vars_json.data : "{}";
    size_t vlen = (vars_json.data && vars_json.len) ? vars_json.len : 2;
    size_t n = eq.len + vlen + 32;
    char *d = (char *)malloc(n);
    if (!d) mako_abort("graphql_request_vars OOM");
    int wrote = snprintf(d, n, "{\"query\":\"%s\",\"variables\":%.*s}", eq.data, (int)vlen, vj);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline int64_t mako_graphql_has_field(MakoString query, MakoString name) {
    if (!name.data || name.len == 0) return 0;
    MakoString f = mako_graphql_field(query);
    int64_t ok = (f.len == name.len && f.data && memcmp(f.data, name.data, name.len) == 0) ? 1 : 0;
    /* also scan for name as identifier after `{` */
    if (!ok && query.data) {
        for (size_t i = 0; i + name.len <= query.len; i++) {
            if (memcmp(query.data + i, name.data, name.len) != 0) continue;
            int left_ok = (i == 0) || !mako_gql_is_ident_start(query.data[i - 1]);
            size_t after = i + name.len;
            int right_ok = (after >= query.len) || !mako_gql_is_ident_start(query.data[after]);
            if (left_ok && right_ok) {
                ok = 1;
                break;
            }
        }
    }
    return ok;
}

static inline MakoString mako_graphql_data2(MakoString f1, MakoString j1, MakoString f2, MakoString j2) {
    MakoString e1 = mako_json_escape(f1);
    MakoString e2 = mako_json_escape(f2);
    const char *p1 = j1.data && j1.len ? j1.data : "null";
    const char *p2 = j2.data && j2.len ? j2.data : "null";
    size_t n = e1.len + e2.len + j1.len + j2.len + 48;
    char *d = (char *)malloc(n);
    if (!d) mako_abort("graphql_data2 OOM");
    int wrote = snprintf(d, n, "{\"data\":{\"%s\":%.*s,\"%s\":%.*s}}", e1.data,
                         (int)(j1.data && j1.len ? j1.len : 4), p1, e2.data,
                         (int)(j2.data && j2.len ? j2.len : 4), p2);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline MakoString mako_sse_event(MakoString event, MakoString data) {
    size_t newline_count = 0;
    for (size_t i = 0; i < data.len; i++) {
        if (data.data[i] == '\n') newline_count++;
    }
    size_t n = event.len + data.len + 32 + (newline_count + 1) * 6;
    char *d = (char *)malloc(n);
    if (!d) mako_abort("sse_event: out of memory");
    size_t pos = 0;
    if (event.data && event.len) {
        memcpy(d + pos, "event: ", 7);
        pos += 7;
        for (size_t i = 0; i < event.len; i++) {
            char c = event.data[i];
            d[pos++] = (c == '\r' || c == '\n') ? ' ' : c;
        }
        d[pos++] = '\n';
    }
    size_t start = 0;
    for (size_t i = 0; i <= data.len; i++) {
        if (i == data.len || data.data[i] == '\n') {
            size_t end = i;
            if (end > start && data.data[end - 1] == '\r') end--;
            memcpy(d + pos, "data: ", 6);
            pos += 6;
            if (end > start) {
                memcpy(d + pos, data.data + start, end - start);
                pos += end - start;
            }
            d[pos++] = '\n';
            start = i + 1;
        }
    }
    d[pos++] = '\n';
    return (MakoString){d, pos};
}

static inline MakoString mako_sse_retry(int64_t millis) {
    char *d = (char *)malloc(48);
    if (!d) mako_abort("sse_retry: out of memory");
    if (millis < 0) millis = 0;
    int wrote = snprintf(d, 48, "retry: %lld\n\n", (long long)millis);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline MakoString mako_rpc_frame(MakoString method, MakoString payload) {
    MakoString em = mako_json_escape(method);
    const char *p = payload.data && payload.len ? payload.data : "{}";
    size_t n = em.len + payload.len + 32;
    char *d = (char *)malloc(n);
    if (!d) mako_abort("rpc_frame: out of memory");
    int wrote = snprintf(d, n, "{\"method\":\"%s\",\"payload\":%s}", em.data, p);
    if (wrote < 0) wrote = 0;
    return (MakoString){d, (size_t)wrote};
}

static inline MakoString mako_rpc_method(MakoString frame) {
    return mako_json_get_string(frame, mako_str_from_cstr("method"));
}

static inline MakoString mako_rpc_payload(MakoString frame) {
    return mako_json_get_object(frame, mako_str_from_cstr("payload"));
}

/* ---- Binary serialize int little-endian hex ---- */
static inline MakoString mako_bin_encode_int(int64_t v) {
    char *d = (char *)malloc(17);
    snprintf(d, 17, "%016llx", (unsigned long long)v);
    return (MakoString){d, 16};
}

static inline MakoString mako_trim_config_value(const char *p, size_t n) {
    while (n > 0 && (*p == ' ' || *p == '\t' || *p == '"' || *p == '\'')) {
        p++;
        n--;
    }
    while (n > 0) {
        char c = p[n - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\'') {
            n--;
        } else {
            break;
        }
    }
    char *d = (char *)malloc(n + 1);
    if (!d) mako_abort("config value: out of memory");
    if (n) memcpy(d, p, n);
    d[n] = 0;
    return (MakoString){d, n};
}

/* ---- YAML (flat key: value seed + list/bool/encode; not full YAML 1.2) ---- */
static inline MakoString mako_yaml_get_string(MakoString doc, MakoString key) {
    const char *p = doc.data ? doc.data : "";
    size_t pos = 0;
    while (pos < doc.len) {
        size_t line_start = pos;
        while (pos < doc.len && p[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < doc.len && p[pos] == '\n') pos++;
        size_t i = line_start;
        while (i < line_end && (p[i] == ' ' || p[i] == '\t')) i++;
        if (i < line_end && p[i] == '#') continue; /* comment */
        if (i + key.len < line_end && memcmp(p + i, key.data, key.len) == 0 && p[i + key.len] == ':') {
            return mako_trim_config_value(p + i + key.len + 1, line_end - (i + key.len + 1));
        }
    }
    return mako_str_from_cstr("");
}

static inline int64_t mako_yaml_has(MakoString doc, MakoString key) {
    MakoString v = mako_yaml_get_string(doc, key);
    int64_t ok = (v.data && v.len > 0) ? 1 : 0;
    /* empty value is still "present" if key: exists — re-scan */
    if (ok) {
        mako_str_free(v);
        return 1;
    }
    mako_str_free(v);
    const char *p = doc.data ? doc.data : "";
    size_t pos = 0;
    while (pos < doc.len) {
        size_t line_start = pos;
        while (pos < doc.len && p[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < doc.len && p[pos] == '\n') pos++;
        size_t i = line_start;
        while (i < line_end && (p[i] == ' ' || p[i] == '\t')) i++;
        if (i + key.len <= line_end && memcmp(p + i, key.data, key.len) == 0
            && (i + key.len == line_end || p[i + key.len] == ':' || p[i + key.len] == ' ')) {
            if (i + key.len < line_end && p[i + key.len] == ':') return 1;
        }
    }
    return 0;
}

static inline int64_t mako_yaml_get_int(MakoString doc, MakoString key) {
    MakoString s = mako_yaml_get_string(doc, key);
    if (!s.data || s.len == 0) {
        mako_str_free(s);
        return 0;
    }
    char buf[64];
    size_t n = s.len < sizeof(buf) - 1 ? s.len : sizeof(buf) - 1;
    memcpy(buf, s.data, n);
    buf[n] = 0;
    mako_str_free(s);
    return (int64_t)strtoll(buf, NULL, 10);
}

static inline int64_t mako_yaml_get_bool(MakoString doc, MakoString key) {
    MakoString s = mako_yaml_get_string(doc, key);
    if (!s.data || s.len == 0) {
        mako_str_free(s);
        return 0;
    }
    int64_t r = 0;
    if ((s.len == 4 && (memcmp(s.data, "true", 4) == 0 || memcmp(s.data, "True", 4) == 0
                        || memcmp(s.data, "TRUE", 4) == 0))
        || (s.len == 3 && (memcmp(s.data, "yes", 3) == 0 || memcmp(s.data, "Yes", 3) == 0))
        || (s.len == 1 && (s.data[0] == '1' || s.data[0] == 'y' || s.data[0] == 'Y')))
        r = 1;
    mako_str_free(s);
    return r;
}

/* Encode one key: value line (quotes if needed). */
static inline MakoString mako_yaml_escape(MakoString s) {
    int need = 0;
    for (size_t i = 0; i < s.len; i++) {
        char c = s.data[i];
        if (c == ':' || c == '#' || c == '"' || c == '\'' || c == '\n' || c == '\r'
            || (i == 0 && (c == ' ' || c == '\t'))) {
            need = 1;
            break;
        }
    }
    if (!need && s.len > 0) return mako_str_clone(s);
    /* quote + escape " and \ */
    size_t cap = s.len * 2 + 3;
    char *d = (char *)malloc(cap);
    if (!d) mako_abort("yaml_escape OOM");
    size_t j = 0;
    d[j++] = '"';
    for (size_t i = 0; i < s.len; i++) {
        char c = s.data[i];
        if (c == '"' || c == '\\') d[j++] = '\\';
        if (c == '\n') {
            d[j++] = '\\';
            d[j++] = 'n';
            continue;
        }
        d[j++] = c;
    }
    d[j++] = '"';
    d[j] = 0;
    return (MakoString){d, j};
}

static inline MakoString mako_yaml_pair(MakoString key, MakoString val) {
    MakoString ev = mako_yaml_escape(val);
    size_t el = ev.len;
    size_t n = key.len + 2 + el + 1;
    char *d = (char *)malloc(n + 1);
    if (!d) mako_abort("yaml_pair OOM");
    memcpy(d, key.data, key.len);
    d[key.len] = ':';
    d[key.len + 1] = ' ';
    memcpy(d + key.len + 2, ev.data, el);
    d[key.len + 2 + el] = '\n';
    d[key.len + 2 + el + 1] = 0;
    mako_str_free(ev);
    return (MakoString){d, key.len + 2 + el + 1};
}

static inline MakoString mako_yaml_pair_int(MakoString key, int64_t val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)val);
    return mako_yaml_pair(key, mako_str_from_cstr(buf));
}

static inline MakoString mako_yaml_pair_bool(MakoString key, int64_t val) {
    return mako_yaml_pair(key, mako_str_from_cstr(val ? "true" : "false"));
}

static inline MakoString mako_yaml_merge(MakoString a, MakoString b) {
    return mako_str_concat(a, b);
}

/* Sequence under key: lines "- item" after "key:" */
static inline MakoStrArray mako_yaml_get_list(MakoString doc, MakoString key) {
    MakoStrArray out = mako_str_array_make(0, 8);
    const char *p = doc.data ? doc.data : "";
    size_t pos = 0;
    int in = 0;
    while (pos < doc.len) {
        size_t line_start = pos;
        while (pos < doc.len && p[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < doc.len && p[pos] == '\n') pos++;
        size_t i = line_start;
        while (i < line_end && (p[i] == ' ' || p[i] == '\t')) i++;
        if (!in) {
            if (i + key.len < line_end && memcmp(p + i, key.data, key.len) == 0
                && p[i + key.len] == ':') {
                in = 1;
                /* inline list: key: [a, b] not supported; multi-line only */
            }
            continue;
        }
        /* stop at next top-level key (no indent dash) */
        if (i < line_end && p[i] != '-' && p[i] != ' ' && p[i] != '\t' && p[i] != '#') {
            /* another key at same level */
            if (p[i] != '\n') break;
        }
        size_t j = i;
        while (j < line_end && (p[j] == ' ' || p[j] == '\t')) j++;
        if (j < line_end && p[j] == '-') {
            j++;
            while (j < line_end && (p[j] == ' ' || p[j] == '\t')) j++;
            MakoString item = mako_trim_config_value(p + j, line_end - j);
            out = mako_str_array_append(out, item);
        } else if (j < line_end && p[j] != '#') {
            break;
        }
    }
    return out;
}

static inline MakoStrArray mako_yaml_keys(MakoString doc) {
    MakoStrArray out = mako_str_array_make(0, 16);
    const char *p = doc.data ? doc.data : "";
    size_t pos = 0;
    while (pos < doc.len) {
        size_t line_start = pos;
        while (pos < doc.len && p[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < doc.len && p[pos] == '\n') pos++;
        size_t i = line_start;
        while (i < line_end && (p[i] == ' ' || p[i] == '\t')) i++;
        if (i >= line_end || p[i] == '#' || p[i] == '-') continue;
        size_t k0 = i;
        while (i < line_end && p[i] != ':' && p[i] != ' ' && p[i] != '\t') i++;
        if (i < line_end && p[i] == ':' && i > k0) {
            char *d = (char *)malloc(i - k0 + 1);
            if (!d) continue;
            memcpy(d, p + k0, i - k0);
            d[i - k0] = 0;
            out = mako_str_array_append(out, (MakoString){d, i - k0});
        }
    }
    return out;
}

/* ---- TOML (flat + [section] keys; not full TOML 1.0) ---- */
static inline MakoString mako_toml_get_string(MakoString doc, MakoString key) {
    const char *p = doc.data ? doc.data : "";
    size_t pos = 0;
    while (pos < doc.len) {
        size_t line_start = pos;
        while (pos < doc.len && p[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < doc.len && p[pos] == '\n') pos++;
        size_t i = line_start;
        while (i < line_end && (p[i] == ' ' || p[i] == '\t')) i++;
        if (i < line_end && p[i] == '#') continue;
        if (i < line_end && p[i] == '[') continue; /* skip section headers in flat get */
        if (i + key.len <= line_end && memcmp(p + i, key.data, key.len) == 0) {
            size_t j = i + key.len;
            while (j < line_end && (p[j] == ' ' || p[j] == '\t')) j++;
            if (j < line_end && p[j] == '=') return mako_trim_config_value(p + j + 1, line_end - j - 1);
        }
    }
    return mako_str_from_cstr("");
}

static inline int64_t mako_toml_get_int(MakoString doc, MakoString key) {
    MakoString s = mako_toml_get_string(doc, key);
    if (!s.data || s.len == 0) {
        mako_str_free(s);
        return 0;
    }
    char buf[64];
    size_t n = s.len < sizeof(buf) - 1 ? s.len : sizeof(buf) - 1;
    memcpy(buf, s.data, n);
    buf[n] = 0;
    mako_str_free(s);
    return (int64_t)strtoll(buf, NULL, 10);
}

static inline int64_t mako_toml_has(MakoString doc, MakoString key) {
    MakoString s = mako_toml_get_string(doc, key);
    /* present if key= found even for empty — re-scan */
    const char *p = doc.data ? doc.data : "";
    size_t pos = 0;
    int found = 0;
    while (pos < doc.len) {
        size_t line_start = pos;
        while (pos < doc.len && p[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < doc.len && p[pos] == '\n') pos++;
        size_t i = line_start;
        while (i < line_end && (p[i] == ' ' || p[i] == '\t')) i++;
        if (i + key.len <= line_end && memcmp(p + i, key.data, key.len) == 0) {
            size_t j = i + key.len;
            while (j < line_end && (p[j] == ' ' || p[j] == '\t')) j++;
            if (j < line_end && p[j] == '=') {
                found = 1;
                break;
            }
        }
    }
    mako_str_free(s);
    return found;
}

static inline int64_t mako_toml_get_bool(MakoString doc, MakoString key) {
    MakoString s = mako_toml_get_string(doc, key);
    if (!s.data || s.len == 0) {
        mako_str_free(s);
        return 0;
    }
    int64_t r = 0;
    if (s.len == 4 && memcmp(s.data, "true", 4) == 0) r = 1;
    mako_str_free(s);
    return r;
}

static inline double mako_toml_get_float(MakoString doc, MakoString key) {
    MakoString s = mako_toml_get_string(doc, key);
    if (!s.data || s.len == 0) {
        mako_str_free(s);
        return 0.0;
    }
    char buf[64];
    size_t n = s.len < sizeof(buf) - 1 ? s.len : sizeof(buf) - 1;
    memcpy(buf, s.data, n);
    buf[n] = 0;
    mako_str_free(s);
    return strtod(buf, NULL);
}

/* Get key inside [section] ... until next [ */
static inline MakoString mako_toml_get_in(MakoString doc, MakoString section, MakoString key) {
    const char *p = doc.data ? doc.data : "";
    size_t pos = 0;
    int in_sec = 0;
    while (pos < doc.len) {
        size_t line_start = pos;
        while (pos < doc.len && p[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < doc.len && p[pos] == '\n') pos++;
        size_t i = line_start;
        while (i < line_end && (p[i] == ' ' || p[i] == '\t')) i++;
        if (i < line_end && p[i] == '[') {
            size_t j = i + 1;
            while (j < line_end && p[j] != ']') j++;
            size_t slen = j > i + 1 ? j - (i + 1) : 0;
            in_sec = (slen == section.len && memcmp(p + i + 1, section.data, slen) == 0) ? 1 : 0;
            continue;
        }
        if (!in_sec) continue;
        if (i < line_end && p[i] == '#') continue;
        if (i + key.len <= line_end && memcmp(p + i, key.data, key.len) == 0) {
            size_t j = i + key.len;
            while (j < line_end && (p[j] == ' ' || p[j] == '\t')) j++;
            if (j < line_end && p[j] == '=')
                return mako_trim_config_value(p + j + 1, line_end - j - 1);
        }
    }
    return mako_str_from_cstr("");
}

static inline int64_t mako_toml_get_int_in(MakoString doc, MakoString section, MakoString key) {
    MakoString s = mako_toml_get_in(doc, section, key);
    if (!s.data || s.len == 0) {
        mako_str_free(s);
        return 0;
    }
    char buf[64];
    size_t n = s.len < sizeof(buf) - 1 ? s.len : sizeof(buf) - 1;
    memcpy(buf, s.data, n);
    buf[n] = 0;
    mako_str_free(s);
    return (int64_t)strtoll(buf, NULL, 10);
}

static inline MakoString mako_toml_escape(MakoString s) {
    /* basic "..." string */
    size_t cap = s.len * 2 + 3;
    char *d = (char *)malloc(cap);
    if (!d) mako_abort("toml_escape OOM");
    size_t j = 0;
    d[j++] = '"';
    for (size_t i = 0; i < s.len; i++) {
        char c = s.data[i];
        if (c == '"' || c == '\\') d[j++] = '\\';
        if (c == '\n') {
            d[j++] = '\\';
            d[j++] = 'n';
            continue;
        }
        d[j++] = c;
    }
    d[j++] = '"';
    d[j] = 0;
    return (MakoString){d, j};
}

static inline MakoString mako_toml_pair(MakoString key, MakoString val) {
    MakoString ev = mako_toml_escape(val);
    size_t el = ev.len;
    size_t n = key.len + 3 + el + 1;
    char *d = (char *)malloc(n + 1);
    if (!d) mako_abort("toml_pair OOM");
    memcpy(d, key.data, key.len);
    d[key.len] = ' ';
    d[key.len + 1] = '=';
    d[key.len + 2] = ' ';
    memcpy(d + key.len + 3, ev.data, el);
    d[key.len + 3 + el] = '\n';
    d[key.len + 3 + el + 1] = 0;
    mako_str_free(ev);
    return (MakoString){d, key.len + 3 + el + 1};
}

static inline MakoString mako_toml_pair_int(MakoString key, int64_t val) {
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "%.*s = %lld\n", (int)key.len, key.data ? key.data : "",
                     (long long)val);
    if (n < 0) n = 0;
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_toml_pair_bool(MakoString key, int64_t val) {
    char buf[96];
    int n = snprintf(buf, sizeof(buf), "%.*s = %s\n", (int)key.len, key.data ? key.data : "",
                     val ? "true" : "false");
    if (n < 0) n = 0;
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_toml_section(MakoString name) {
    size_t n = name.len + 3;
    char *d = (char *)malloc(n + 1);
    if (!d) mako_abort("toml_section OOM");
    d[0] = '[';
    memcpy(d + 1, name.data, name.len);
    d[1 + name.len] = ']';
    d[2 + name.len] = '\n';
    d[3 + name.len] = 0;
    return (MakoString){d, 3 + name.len};
}

static inline MakoString mako_toml_merge(MakoString a, MakoString b) {
    return mako_str_concat(a, b);
}

static inline MakoStrArray mako_toml_keys(MakoString doc) {
    MakoStrArray out = mako_str_array_make(0, 16);
    const char *p = doc.data ? doc.data : "";
    size_t pos = 0;
    while (pos < doc.len) {
        size_t line_start = pos;
        while (pos < doc.len && p[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < doc.len && p[pos] == '\n') pos++;
        size_t i = line_start;
        while (i < line_end && (p[i] == ' ' || p[i] == '\t')) i++;
        if (i >= line_end || p[i] == '#' || p[i] == '[') continue;
        size_t k0 = i;
        while (i < line_end && p[i] != '=' && p[i] != ' ' && p[i] != '\t') i++;
        size_t k1 = i;
        while (i < line_end && (p[i] == ' ' || p[i] == '\t')) i++;
        if (i < line_end && p[i] == '=' && k1 > k0) {
            char *d = (char *)malloc(k1 - k0 + 1);
            if (!d) continue;
            memcpy(d, p + k0, k1 - k0);
            d[k1 - k0] = 0;
            out = mako_str_array_append(out, (MakoString){d, k1 - k0});
        }
    }
    return out;
}

/* ---- binary hex helpers (debug / tests) ---- */
static inline MakoString mako_bytes_to_hex(MakoString bin) {
    char *d = (char *)malloc(bin.len * 2 + 1);
    if (!d) mako_abort("bytes_to_hex OOM");
    for (size_t i = 0; i < bin.len; i++)
        sprintf(d + i * 2, "%02x", (unsigned char)bin.data[i]);
    d[bin.len * 2] = 0;
    return (MakoString){d, bin.len * 2};
}
static inline int mako_hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static inline MakoString mako_hex_to_bytes(MakoString hex) {
    size_t n = hex.len / 2;
    char *d = (char *)malloc(n + 1);
    if (!d) mako_abort("hex_to_bytes OOM");
    for (size_t i = 0; i < n; i++) {
        int hi = mako_hex_nibble(hex.data[i * 2]);
        int lo = mako_hex_nibble(hex.data[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            free(d);
            return mako_str_from_cstr("");
        }
        d[i] = (char)((hi << 4) | lo);
    }
    d[n] = 0;
    return (MakoString){d, n};
}

/* ---- MessagePack (subset: nil/bool/int/str/array[int]) ---- */
static inline void mako_mp_ensure(unsigned char **buf, size_t *cap, size_t need) {
    if (need <= *cap) return;
    size_t nc = *cap ? *cap * 2 : 64;
    while (nc < need) nc *= 2;
    unsigned char *nb = (unsigned char *)realloc(*buf, nc);
    if (!nb) mako_abort("msgpack OOM");
    *buf = nb;
    *cap = nc;
}
static inline void mako_mp_put(unsigned char **buf, size_t *cap, size_t *n, unsigned char b) {
    mako_mp_ensure(buf, cap, *n + 1);
    (*buf)[(*n)++] = b;
}
static inline void mako_mp_put_u16(unsigned char **buf, size_t *cap, size_t *n, uint16_t v) {
    mako_mp_put(buf, cap, n, (unsigned char)(v >> 8));
    mako_mp_put(buf, cap, n, (unsigned char)(v & 0xff));
}
static inline void mako_mp_put_u32(unsigned char **buf, size_t *cap, size_t *n, uint32_t v) {
    mako_mp_put(buf, cap, n, (unsigned char)(v >> 24));
    mako_mp_put(buf, cap, n, (unsigned char)((v >> 16) & 0xff));
    mako_mp_put(buf, cap, n, (unsigned char)((v >> 8) & 0xff));
    mako_mp_put(buf, cap, n, (unsigned char)(v & 0xff));
}
static inline void mako_mp_put_u64(unsigned char **buf, size_t *cap, size_t *n, uint64_t v) {
    for (int i = 7; i >= 0; i--) mako_mp_put(buf, cap, n, (unsigned char)((v >> (i * 8)) & 0xff));
}
static inline void mako_mp_encode_int_into(unsigned char **buf, size_t *cap, size_t *n, int64_t v) {
    if (v >= 0 && v <= 127) {
        mako_mp_put(buf, cap, n, (unsigned char)v);
    } else if (v >= -32 && v < 0) {
        mako_mp_put(buf, cap, n, (unsigned char)(0xe0 | (v & 0x1f)));
    } else if (v >= 0 && v <= 0xff) {
        mako_mp_put(buf, cap, n, 0xcc);
        mako_mp_put(buf, cap, n, (unsigned char)v);
    } else if (v >= 0 && v <= 0xffff) {
        mako_mp_put(buf, cap, n, 0xcd);
        mako_mp_put_u16(buf, cap, n, (uint16_t)v);
    } else if (v >= 0 && v <= 0xffffffffLL) {
        mako_mp_put(buf, cap, n, 0xce);
        mako_mp_put_u32(buf, cap, n, (uint32_t)v);
    } else if (v >= 0) {
        mako_mp_put(buf, cap, n, 0xcf);
        mako_mp_put_u64(buf, cap, n, (uint64_t)v);
    } else if (v >= -128) {
        mako_mp_put(buf, cap, n, 0xd0);
        mako_mp_put(buf, cap, n, (unsigned char)(int8_t)v);
    } else if (v >= -32768) {
        mako_mp_put(buf, cap, n, 0xd1);
        mako_mp_put_u16(buf, cap, n, (uint16_t)(int16_t)v);
    } else if (v >= (int64_t)INT32_MIN) {
        mako_mp_put(buf, cap, n, 0xd2);
        mako_mp_put_u32(buf, cap, n, (uint32_t)(int32_t)v);
    } else {
        mako_mp_put(buf, cap, n, 0xd3);
        mako_mp_put_u64(buf, cap, n, (uint64_t)v);
    }
}
static inline MakoString mako_msgpack_encode_int(int64_t v) {
    unsigned char *buf = NULL;
    size_t cap = 0, n = 0;
    mako_mp_encode_int_into(&buf, &cap, &n, v);
    return (MakoString){(char *)buf, n};
}
static inline MakoString mako_msgpack_encode_bool(int64_t v) {
    char *d = (char *)malloc(1);
    if (!d) mako_abort("mp bool OOM");
    d[0] = v ? (char)0xc3 : (char)0xc2;
    return (MakoString){d, 1};
}
static inline MakoString mako_msgpack_encode_nil(void) {
    char *d = (char *)malloc(1);
    if (!d) mako_abort("mp nil OOM");
    d[0] = (char)0xc0;
    return (MakoString){d, 1};
}
static inline MakoString mako_msgpack_encode_string(MakoString s) {
    unsigned char *buf = NULL;
    size_t cap = 0, n = 0;
    if (s.len <= 31) {
        mako_mp_put(&buf, &cap, &n, (unsigned char)(0xa0 | s.len));
    } else if (s.len <= 0xff) {
        mako_mp_put(&buf, &cap, &n, 0xd9);
        mako_mp_put(&buf, &cap, &n, (unsigned char)s.len);
    } else if (s.len <= 0xffff) {
        mako_mp_put(&buf, &cap, &n, 0xda);
        mako_mp_put_u16(&buf, &cap, &n, (uint16_t)s.len);
    } else {
        mako_mp_put(&buf, &cap, &n, 0xdb);
        mako_mp_put_u32(&buf, &cap, &n, (uint32_t)s.len);
    }
    mako_mp_ensure(&buf, &cap, n + s.len);
    if (s.len) memcpy(buf + n, s.data, s.len);
    n += s.len;
    return (MakoString){(char *)buf, n};
}
static inline MakoString mako_msgpack_encode_array_int(MakoIntArray a) {
    unsigned char *buf = NULL;
    size_t cap = 0, n = 0;
    if (a.len <= 15) {
        mako_mp_put(&buf, &cap, &n, (unsigned char)(0x90 | a.len));
    } else if (a.len <= 0xffff) {
        mako_mp_put(&buf, &cap, &n, 0xdc);
        mako_mp_put_u16(&buf, &cap, &n, (uint16_t)a.len);
    } else {
        mako_mp_put(&buf, &cap, &n, 0xdd);
        mako_mp_put_u32(&buf, &cap, &n, (uint32_t)a.len);
    }
    for (size_t i = 0; i < a.len; i++) mako_mp_encode_int_into(&buf, &cap, &n, a.data[i]);
    return (MakoString){(char *)buf, n};
}
/* Legacy hex form of int64 (big-endian int64 header d3). */
static inline MakoString mako_msgpack_int_hex(int64_t v) {
    MakoString raw = mako_msgpack_encode_int(v);
    MakoString h = mako_bytes_to_hex(raw);
    mako_str_free(raw);
    return h;
}
static inline int64_t mako_msgpack_decode_int(MakoString bin) {
    if (!bin.data || bin.len == 0) return 0;
    const unsigned char *p = (const unsigned char *)bin.data;
    size_t n = bin.len;
    unsigned char b = p[0];
    if (b <= 0x7f) return (int64_t)b;
    if ((b & 0xe0) == 0xe0) return (int64_t)(int8_t)b;
    if (b == 0xcc && n >= 2) return p[1];
    if (b == 0xcd && n >= 3) return ((int64_t)p[1] << 8) | p[2];
    if (b == 0xce && n >= 5)
        return ((int64_t)p[1] << 24) | ((int64_t)p[2] << 16) | ((int64_t)p[3] << 8) | p[4];
    if (b == 0xcf && n >= 9) {
        uint64_t u = 0;
        for (int i = 0; i < 8; i++) u = (u << 8) | p[1 + i];
        return (int64_t)u;
    }
    if (b == 0xd0 && n >= 2) return (int64_t)(int8_t)p[1];
    if (b == 0xd1 && n >= 3) return (int64_t)(int16_t)(((uint16_t)p[1] << 8) | p[2]);
    if (b == 0xd2 && n >= 5) {
        uint32_t u = ((uint32_t)p[1] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 8) | p[4];
        return (int64_t)(int32_t)u;
    }
    if (b == 0xd3 && n >= 9) {
        uint64_t u = 0;
        for (int i = 0; i < 8; i++) u = (u << 8) | p[1 + i];
        return (int64_t)u;
    }
    return 0;
}
static inline int64_t mako_msgpack_is_nil(MakoString bin) {
    return (bin.len >= 1 && (unsigned char)bin.data[0] == 0xc0) ? 1 : 0;
}
static inline int64_t mako_msgpack_decode_bool(MakoString bin) {
    if (bin.len < 1) return 0;
    unsigned char b = (unsigned char)bin.data[0];
    if (b == 0xc3) return 1;
    return 0;
}
static inline MakoString mako_msgpack_decode_string(MakoString bin) {
    if (!bin.data || bin.len == 0) return mako_str_from_cstr("");
    const unsigned char *p = (const unsigned char *)bin.data;
    size_t n = bin.len, off = 0, len = 0;
    unsigned char b = p[0];
    if ((b & 0xe0) == 0xa0) {
        len = b & 0x1f;
        off = 1;
    } else if (b == 0xd9 && n >= 2) {
        len = p[1];
        off = 2;
    } else if (b == 0xda && n >= 3) {
        len = ((size_t)p[1] << 8) | p[2];
        off = 3;
    } else if (b == 0xdb && n >= 5) {
        len = ((size_t)p[1] << 24) | ((size_t)p[2] << 16) | ((size_t)p[3] << 8) | p[4];
        off = 5;
    } else
        return mako_str_from_cstr("");
    if (off + len > n) return mako_str_from_cstr("");
    char *d = (char *)malloc(len + 1);
    if (!d) mako_abort("mp dec str OOM");
    if (len) memcpy(d, p + off, len);
    d[len] = 0;
    return (MakoString){d, len};
}
/* Decode array of ints; non-int elements skipped (best-effort). */
static inline MakoIntArray mako_msgpack_decode_array_int(MakoString bin) {
    MakoIntArray out = mako_int_array_make(0, 8);
    if (!bin.data || bin.len == 0) return out;
    const unsigned char *p = (const unsigned char *)bin.data;
    size_t n = bin.len, off = 0, alen = 0;
    unsigned char b = p[0];
    if ((b & 0xf0) == 0x90) {
        alen = b & 0x0f;
        off = 1;
    } else if (b == 0xdc && n >= 3) {
        alen = ((size_t)p[1] << 8) | p[2];
        off = 3;
    } else if (b == 0xdd && n >= 5) {
        alen = ((size_t)p[1] << 24) | ((size_t)p[2] << 16) | ((size_t)p[3] << 8) | p[4];
        off = 5;
    } else
        return out;
    for (size_t i = 0; i < alen && off < n; i++) {
        MakoString piece = {(char *)(uintptr_t)(p + off), n - off};
        /* size of one int encoding: re-encode and compare length via probe */
        int64_t v = mako_msgpack_decode_int(piece);
        MakoString enc = mako_msgpack_encode_int(v);
        /* verify prefix match; if not, break */
        if (enc.len > n - off || memcmp(enc.data, p + off, enc.len) != 0) {
            /* try fixint/negative only single byte */
            if (off < n) {
                unsigned char bb = p[off];
                if (bb <= 0x7f || (bb & 0xe0) == 0xe0) {
                    out = mako_slice_append(out, mako_msgpack_decode_int(
                        (MakoString){(char *)(uintptr_t)(p + off), 1}));
                    off += 1;
                    mako_str_free(enc);
                    continue;
                }
            }
            mako_str_free(enc);
            break;
        }
        out = mako_slice_append(out, v);
        off += enc.len;
        mako_str_free(enc);
    }
    return out;
}

/* ---- CBOR (subset: int/bool/null/text/array[int]) ---- */
static inline void mako_cbor_put_head(unsigned char **buf, size_t *cap, size_t *n, int major,
                                      uint64_t val) {
    unsigned char mt = (unsigned char)((major & 7) << 5);
    if (val < 24) {
        mako_mp_put(buf, cap, n, (unsigned char)(mt | val));
    } else if (val <= 0xff) {
        mako_mp_put(buf, cap, n, (unsigned char)(mt | 24));
        mako_mp_put(buf, cap, n, (unsigned char)val);
    } else if (val <= 0xffff) {
        mako_mp_put(buf, cap, n, (unsigned char)(mt | 25));
        mako_mp_put_u16(buf, cap, n, (uint16_t)val);
    } else if (val <= 0xffffffffULL) {
        mako_mp_put(buf, cap, n, (unsigned char)(mt | 26));
        mako_mp_put_u32(buf, cap, n, (uint32_t)val);
    } else {
        mako_mp_put(buf, cap, n, (unsigned char)(mt | 27));
        mako_mp_put_u64(buf, cap, n, val);
    }
}
static inline void mako_cbor_encode_int_into(unsigned char **buf, size_t *cap, size_t *n, int64_t v) {
    if (v >= 0)
        mako_cbor_put_head(buf, cap, n, 0, (uint64_t)v);
    else
        mako_cbor_put_head(buf, cap, n, 1, (uint64_t)(-1 - v));
}
static inline MakoString mako_cbor_encode_int(int64_t v) {
    unsigned char *buf = NULL;
    size_t cap = 0, n = 0;
    mako_cbor_encode_int_into(&buf, &cap, &n, v);
    return (MakoString){(char *)buf, n};
}
static inline MakoString mako_cbor_encode_bool(int64_t v) {
    char *d = (char *)malloc(1);
    if (!d) mako_abort("cbor bool OOM");
    d[0] = v ? (char)0xf5 : (char)0xf4;
    return (MakoString){d, 1};
}
static inline MakoString mako_cbor_encode_null(void) {
    char *d = (char *)malloc(1);
    if (!d) mako_abort("cbor null OOM");
    d[0] = (char)0xf6;
    return (MakoString){d, 1};
}
static inline MakoString mako_cbor_encode_string(MakoString s) {
    unsigned char *buf = NULL;
    size_t cap = 0, n = 0;
    mako_cbor_put_head(&buf, &cap, &n, 3, (uint64_t)s.len);
    mako_mp_ensure(&buf, &cap, n + s.len);
    if (s.len) memcpy(buf + n, s.data, s.len);
    n += s.len;
    return (MakoString){(char *)buf, n};
}
static inline MakoString mako_cbor_encode_array_int(MakoIntArray a) {
    unsigned char *buf = NULL;
    size_t cap = 0, n = 0;
    mako_cbor_put_head(&buf, &cap, &n, 4, (uint64_t)a.len);
    for (size_t i = 0; i < a.len; i++) mako_cbor_encode_int_into(&buf, &cap, &n, a.data[i]);
    return (MakoString){(char *)buf, n};
}
static inline MakoString mako_cbor_int_hex(int64_t v) {
    MakoString raw = mako_cbor_encode_int(v);
    MakoString h = mako_bytes_to_hex(raw);
    mako_str_free(raw);
    return h;
}
/* Read CBOR additional info at *off; advances *off past head; returns value. */
static inline int mako_cbor_read_head(const unsigned char *p, size_t n, size_t *off, int *major,
                                      uint64_t *val) {
    if (*off >= n) return -1;
    unsigned char b = p[(*off)++];
    *major = (b >> 5) & 7;
    unsigned char ai = b & 0x1f;
    if (ai < 24) {
        *val = ai;
        return 0;
    }
    if (ai == 24) {
        if (*off + 1 > n) return -1;
        *val = p[(*off)++];
        return 0;
    }
    if (ai == 25) {
        if (*off + 2 > n) return -1;
        *val = ((uint64_t)p[*off] << 8) | p[*off + 1];
        *off += 2;
        return 0;
    }
    if (ai == 26) {
        if (*off + 4 > n) return -1;
        *val = ((uint64_t)p[*off] << 24) | ((uint64_t)p[*off + 1] << 16) | ((uint64_t)p[*off + 2] << 8)
            | p[*off + 3];
        *off += 4;
        return 0;
    }
    if (ai == 27) {
        if (*off + 8 > n) return -1;
        *val = 0;
        for (int i = 0; i < 8; i++) *val = (*val << 8) | p[(*off)++];
        return 0;
    }
    if (ai == 20 || ai == 21 || ai == 22 || ai == 23) {
        *val = ai;
        return 0;
    }
    return -1;
}
static inline int64_t mako_cbor_type(MakoString bin) {
    if (!bin.data || bin.len == 0) return -1;
    return ((unsigned char)bin.data[0] >> 5) & 7;
}
static inline int64_t mako_cbor_decode_int(MakoString bin) {
    size_t off = 0;
    int major = 0;
    uint64_t val = 0;
    if (mako_cbor_read_head((const unsigned char *)bin.data, bin.len, &off, &major, &val) != 0)
        return 0;
    if (major == 0) return (int64_t)val;
    if (major == 1) return (int64_t)(-1 - (int64_t)val);
    return 0;
}
static inline int64_t mako_cbor_decode_bool(MakoString bin) {
    if (bin.len < 1) return 0;
    unsigned char b = (unsigned char)bin.data[0];
    if (b == 0xf5) return 1;
    return 0;
}
static inline int64_t mako_cbor_is_null(MakoString bin) {
    return (bin.len >= 1 && (unsigned char)bin.data[0] == 0xf6) ? 1 : 0;
}
static inline MakoString mako_cbor_decode_string(MakoString bin) {
    size_t off = 0;
    int major = 0;
    uint64_t val = 0;
    if (mako_cbor_read_head((const unsigned char *)bin.data, bin.len, &off, &major, &val) != 0
        || major != 3)
        return mako_str_from_cstr("");
    if (off + val > bin.len) return mako_str_from_cstr("");
    char *d = (char *)malloc((size_t)val + 1);
    if (!d) mako_abort("cbor str OOM");
    if (val) memcpy(d, bin.data + off, (size_t)val);
    d[val] = 0;
    return (MakoString){d, (size_t)val};
}
static inline MakoIntArray mako_cbor_decode_array_int(MakoString bin) {
    MakoIntArray out = mako_int_array_make(0, 8);
    size_t off = 0;
    int major = 0;
    uint64_t alen = 0;
    if (mako_cbor_read_head((const unsigned char *)bin.data, bin.len, &off, &major, &alen) != 0
        || major != 4)
        return out;
    for (uint64_t i = 0; i < alen && off < bin.len; i++) {
        size_t start = off;
        int m = 0;
        uint64_t v = 0;
        if (mako_cbor_read_head((const unsigned char *)bin.data, bin.len, &off, &m, &v) != 0) break;
        if (m == 0)
            out = mako_slice_append(out, (int64_t)v);
        else if (m == 1)
            out = mako_slice_append(out, (int64_t)(-1 - (int64_t)v));
        else {
            (void)start;
            break;
        }
    }
    return out;
}

/* ---- list combinators (int) ---- */
static inline MakoIntArray mako_list_take_int(MakoIntArray a, int64_t n) {
    if (n < 0) n = 0;
    if ((size_t)n > a.len) n = (int64_t)a.len;
    MakoIntArray out = mako_int_array_make(n, n);
    for (int64_t i = 0; i < n; i++) out.data[i] = a.data[i];
    out.len = (size_t)n;
    return out;
}
static inline MakoIntArray mako_list_drop_int(MakoIntArray a, int64_t n) {
    if (n < 0) n = 0;
    if ((size_t)n >= a.len) return mako_int_array_make(0, 0);
    size_t m = a.len - (size_t)n;
    MakoIntArray out = mako_int_array_make((int64_t)m, (int64_t)m);
    for (size_t i = 0; i < m; i++) out.data[i] = a.data[(size_t)n + i];
    out.len = m;
    return out;
}
static inline MakoIntArray mako_list_zip_int(MakoIntArray a, MakoIntArray b) {
    size_t m = a.len < b.len ? a.len : b.len;
    MakoIntArray out = mako_int_array_make((int64_t)(m * 2), (int64_t)(m * 2));
    for (size_t i = 0; i < m; i++) {
        out.data[i * 2] = a.data[i];
        out.data[i * 2 + 1] = b.data[i];
    }
    out.len = m * 2;
    return out;
}
static inline int64_t mako_list_find_int(MakoIntArray a, int64_t v) {
    return mako_ints_index(a, v);
}
static inline int64_t mako_list_count_int(MakoIntArray a, int64_t v) {
    int64_t c = 0;
    for (size_t i = 0; i < a.len; i++)
        if (a.data[i] == v) c++;
    return c;
}
static inline int64_t mako_list_any_eq_int(MakoIntArray a, int64_t v) {
    return mako_ints_contains(a, v) ? 1 : 0;
}
static inline int64_t mako_list_all_eq_int(MakoIntArray a, int64_t v) {
    if (a.len == 0) return 1;
    for (size_t i = 0; i < a.len; i++)
        if (a.data[i] != v) return 0;
    return 1;
}
static inline MakoIntArray mako_list_take_while_lt_int(MakoIntArray a, int64_t thr) {
    size_t n = 0;
    while (n < a.len && a.data[n] < thr) n++;
    return mako_list_take_int(a, (int64_t)n);
}
static inline MakoIntArray mako_list_map_add_int(MakoIntArray a, int64_t delta) {
    MakoIntArray out = mako_int_array_make((int64_t)a.len, (int64_t)a.len);
    for (size_t i = 0; i < a.len; i++) out.data[i] = a.data[i] + delta;
    out.len = a.len;
    return out;
}
static inline MakoIntArray mako_list_map_mul_int(MakoIntArray a, int64_t k) {
    MakoIntArray out = mako_int_array_make((int64_t)a.len, (int64_t)a.len);
    for (size_t i = 0; i < a.len; i++) out.data[i] = a.data[i] * k;
    out.len = a.len;
    return out;
}
static inline MakoIntArray mako_list_filter_lt_int(MakoIntArray a, int64_t thr) {
    MakoIntArray out = mako_int_array_make(0, (int64_t)a.len);
    for (size_t i = 0; i < a.len; i++)
        if (a.data[i] < thr) out = mako_slice_append(out, a.data[i]);
    return out;
}
static inline MakoIntArray mako_list_filter_gt_int(MakoIntArray a, int64_t thr) {
    MakoIntArray out = mako_int_array_make(0, (int64_t)a.len);
    for (size_t i = 0; i < a.len; i++)
        if (a.data[i] > thr) out = mako_slice_append(out, a.data[i]);
    return out;
}
static inline int64_t mako_list_fold_add_int(MakoIntArray a, int64_t init) {
    int64_t s = init;
    for (size_t i = 0; i < a.len; i++) s += a.data[i];
    return s;
}
static inline int64_t mako_list_fold_mul_int(MakoIntArray a, int64_t init) {
    int64_t s = init;
    for (size_t i = 0; i < a.len; i++) s *= a.data[i];
    return s;
}

/* ---- Avro binary (subset: null/bool/long/string/array[long]; zigzag long) ---- */
static inline MakoString mako_avro_encode_long(int64_t v) {
    uint64_t zz = ((uint64_t)v << 1) ^ (uint64_t)(v >> 63);
    char tmp[10];
    size_t n = 0;
    while (zz >= 0x80) {
        tmp[n++] = (char)((zz & 0x7f) | 0x80);
        zz >>= 7;
    }
    tmp[n++] = (char)zz;
    char *d = (char *)malloc(n + 1);
    if (!d) mako_abort("avro_encode_long OOM");
    memcpy(d, tmp, n);
    d[n] = 0;
    return (MakoString){d, n};
}
static inline int64_t mako_avro_decode_long(MakoString s) {
    if (!s.data || s.len == 0) return 0;
    uint64_t result = 0;
    int shift = 0;
    for (size_t i = 0; i < s.len && i < 10; i++) {
        unsigned char b = (unsigned char)s.data[i];
        result |= (uint64_t)(b & 0x7f) << shift;
        if ((b & 0x80) == 0) {
            /* zigzag decode */
            return (int64_t)((result >> 1) ^ (-(int64_t)(result & 1)));
        }
        shift += 7;
    }
    return 0;
}
static inline int64_t mako_avro_long_len(MakoString s) {
    if (!s.data || s.len == 0) return -1;
    for (size_t i = 0; i < s.len && i < 10; i++) {
        if (((unsigned char)s.data[i] & 0x80) == 0) return (int64_t)(i + 1);
    }
    return -1;
}
static inline MakoString mako_avro_encode_bool(int64_t v) {
    char *d = (char *)malloc(1);
    if (!d) mako_abort("avro bool OOM");
    d[0] = v ? 1 : 0;
    return (MakoString){d, 1};
}
static inline int64_t mako_avro_decode_bool(MakoString s) {
    if (!s.data || s.len == 0) return 0;
    return s.data[0] ? 1 : 0;
}
static inline MakoString mako_avro_encode_null(void) {
    /* Avro null is empty (union index often separate); seed = single 0 byte marker */
    char *d = (char *)malloc(1);
    if (!d) mako_abort("avro null OOM");
    d[0] = 0;
    return (MakoString){d, 1};
}
static inline MakoString mako_avro_encode_string(MakoString s) {
    MakoString lenv = mako_avro_encode_long((int64_t)s.len);
    MakoString out = mako_str_concat(lenv, s);
    mako_str_free(lenv);
    return out;
}
static inline MakoString mako_avro_decode_string(MakoString s) {
    int64_t ll = mako_avro_long_len(s);
    if (ll < 0) return mako_str_from_cstr("");
    int64_t n = mako_avro_decode_long(s);
    if (n < 0 || (int64_t)s.len < ll + n) return mako_str_from_cstr("");
    char *d = (char *)malloc((size_t)n + 1);
    if (!d) mako_abort("avro str OOM");
    if (n) memcpy(d, s.data + ll, (size_t)n);
    d[n] = 0;
    return (MakoString){d, (size_t)n};
}
/* Array of long: block count (zigzag long) then items, then 0. */
static inline MakoString mako_avro_encode_array_long(MakoIntArray a) {
    MakoString cnt = mako_avro_encode_long((int64_t)a.len);
    MakoString body = mako_str_from_cstr("");
    for (size_t i = 0; i < a.len; i++) {
        MakoString e = mako_avro_encode_long(a.data[i]);
        MakoString n = mako_str_concat(body, e);
        mako_str_free(body);
        mako_str_free(e);
        body = n;
    }
    MakoString zero = mako_avro_encode_long(0);
    MakoString mid = mako_str_concat(cnt, body);
    mako_str_free(cnt);
    mako_str_free(body);
    MakoString out = mako_str_concat(mid, zero);
    mako_str_free(mid);
    mako_str_free(zero);
    return out;
}
static inline MakoIntArray mako_avro_decode_array_long(MakoString s) {
    MakoIntArray out = mako_int_array_make(0, 8);
    if (!s.data || s.len == 0) return out;
    size_t off = 0;
    MakoString rest = {(char *)(s.data + off), s.len - off};
    int64_t ll = mako_avro_long_len(rest);
    if (ll < 0) return out;
    int64_t count = mako_avro_decode_long(rest);
    off += (size_t)ll;
    if (count < 0) count = -count; /* ignore block size form */
    for (int64_t i = 0; i < count && off < s.len; i++) {
        rest = (MakoString){(char *)(s.data + off), s.len - off};
        ll = mako_avro_long_len(rest);
        if (ll < 0) break;
        out = mako_slice_append(out, mako_avro_decode_long(rest));
        off += (size_t)ll;
    }
    return out;
}
static inline MakoString mako_avro_long_hex(int64_t v) {
    MakoString raw = mako_avro_encode_long(v);
    MakoString h = mako_bytes_to_hex(raw);
    mako_str_free(raw);
    return h;
}

/* ---- SHA-256 / HMAC ---- */
static inline MakoString mako_sha256_hex(MakoString s) {
    unsigned char dig[32];
#if defined(MAKO_HAS_CC)
    CC_SHA256(s.data, (CC_LONG)s.len, dig);
#elif defined(MAKO_HAS_OPENSSL)
    SHA256((const unsigned char *)s.data, s.len, dig);
#else
    /* Fallback: not cryptographic — FNV-ish fill for portability */
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < s.len; i++) {
        h ^= (unsigned char)s.data[i];
        h *= 0x100000001b3ULL;
    }
    for (int i = 0; i < 32; i++) dig[i] = (unsigned char)((h >> ((i % 8) * 8)) & 0xff);
#endif
    char *out = (char *)malloc(65);
    for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", dig[i]);
    out[64] = 0;
    return (MakoString){out, 64};
}

/* Raw SHA-256 → 32-byte binary. */
static inline MakoString mako_sha256_raw(MakoString s) {
    unsigned char dig[32];
#if defined(MAKO_HAS_CC)
    CC_SHA256(s.data ? s.data : "", (CC_LONG)s.len, dig);
#elif defined(MAKO_HAS_OPENSSL)
    SHA256((const unsigned char *)(s.data ? s.data : ""), s.len, dig);
#else
    {
        MakoString h = mako_sha256_hex(s);
        if (!h.data || h.len < 64) { mako_str_free(h); return mako_str_from_cstr(""); }
        char *d = (char *)malloc(33);
        if (!d) { mako_str_free(h); return mako_str_from_cstr(""); }
        for (int i = 0; i < 32; i++) {
            unsigned int v = 0;
            sscanf(h.data + i * 2, "%02x", &v);
            d[i] = (char)v;
        }
        d[32] = 0;
        mako_str_free(h);
        return (MakoString){d, 32};
    }
#endif
    {
        char *d = (char *)malloc(33);
        if (!d) return mako_str_from_cstr("");
        memcpy(d, dig, 32);
        d[32] = 0;
        return (MakoString){d, 32};
    }
}

/* Pairwise XOR of two equal-length byte strings (e.g. SCRAM ClientProof).
 * Returns "" when lengths differ. */
static inline MakoString mako_xor_bytes(MakoString a, MakoString b) {
    if (a.len != b.len) return mako_str_from_cstr("");
    char *d = (char *)malloc(a.len + 1);
    if (!d) return mako_str_from_cstr("");
    for (size_t i = 0; i < a.len; i++) d[i] = (char)(a.data[i] ^ b.data[i]);
    d[a.len] = 0;
    return (MakoString){d, a.len};
}

static inline MakoString mako_hmac_sha256_hex(MakoString key, MakoString msg) {
    unsigned char dig[32];
#if defined(MAKO_HAS_CC)
    CCHmac(kCCHmacAlgSHA256, key.data, key.len, msg.data, msg.len, dig);
#elif defined(MAKO_HAS_OPENSSL)
    unsigned int len = 32;
    HMAC(EVP_sha256(), key.data, (int)key.len, (const unsigned char *)msg.data, msg.len, dig, &len);
#else
    MakoString both = mako_str_concat(key, msg);
    MakoString h = mako_sha256_hex(both);
    return h;
#endif
    char *out = (char *)malloc(65);
    for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", dig[i]);
    out[64] = 0;
    return (MakoString){out, 64};
}

/* Raw HMAC-SHA256 → 32-byte binary (for HKDF). Empty on failure. */
static inline MakoString mako_hmac_sha256_raw(MakoString key, MakoString msg) {
    unsigned char dig[32];
#if defined(MAKO_HAS_CC)
    CCHmac(kCCHmacAlgSHA256, key.data, key.len, msg.data, msg.len, dig);
#elif defined(MAKO_HAS_OPENSSL)
    unsigned int len = 32;
    HMAC(EVP_sha256(), key.data, (int)key.len, (const unsigned char *)msg.data, msg.len, dig, &len);
#else
    {
        /* Non-crypto fallback: reuse hex digest bytes (demo only). */
        MakoString h = mako_hmac_sha256_hex(key, msg);
        if (!h.data || h.len < 64) { mako_str_free(h); return mako_str_from_cstr(""); }
        char *d = (char *)malloc(33);
        if (!d) { mako_str_free(h); return mako_str_from_cstr(""); }
        for (int i = 0; i < 32; i++) {
            unsigned int v = 0;
            sscanf(h.data + i * 2, "%02x", &v);
            d[i] = (char)v;
        }
        d[32] = 0;
        mako_str_free(h);
        return (MakoString){d, 32};
    }
#endif
    {
        char *d = (char *)malloc(33);
        if (!d) return mako_str_from_cstr("");
        memcpy(d, dig, 32);
        d[32] = 0;
        return (MakoString){d, 32};
    }
}

/* HKDF-SHA256 (RFC 5869) extract+expand — protocol keying for Mako apps.
 * Empty salt uses HashLen zeros. out_len capped at 255*32. */
static inline MakoString mako_hkdf_sha256(
    MakoString ikm, MakoString salt, MakoString info, int64_t out_len
) {
    if (out_len <= 0) return mako_str_from_cstr("");
    if (out_len > 255 * 32) out_len = 255 * 32;
    unsigned char zero_salt[32];
    memset(zero_salt, 0, sizeof(zero_salt));
    MakoString salt_s = salt;
    if (!salt.data || salt.len == 0) {
        salt_s = (MakoString){(char *)zero_salt, 32};
    }
    MakoString prk = mako_hmac_sha256_raw(salt_s, ikm);
    if (!prk.data || prk.len == 0) {
        mako_str_free(prk);
        return mako_str_from_cstr("");
    }
    size_t n = (size_t)out_len;
    size_t nblocks = (n + 31) / 32;
    char *out = (char *)malloc(n + 1);
    if (!out) {
        mako_str_free(prk);
        return mako_str_from_cstr("");
    }
    unsigned char t[32];
    size_t tlen = 0;
    size_t filled = 0;
    for (size_t i = 1; i <= nblocks; i++) {
        size_t msg_len = tlen + (info.data ? info.len : 0) + 1;
        unsigned char *msg = (unsigned char *)malloc(msg_len ? msg_len : 1);
        if (!msg) {
            free(out);
            mako_str_free(prk);
            return mako_str_from_cstr("");
        }
        size_t o = 0;
        if (tlen) {
            memcpy(msg, t, tlen);
            o = tlen;
        }
        if (info.data && info.len) {
            memcpy(msg + o, info.data, info.len);
            o += info.len;
        }
        msg[o++] = (unsigned char)i;
        MakoString block = mako_hmac_sha256_raw(prk, (MakoString){(char *)msg, o});
        free(msg);
        if (!block.data || block.len < 32) {
            mako_str_free(block);
            free(out);
            mako_str_free(prk);
            return mako_str_from_cstr("");
        }
        memcpy(t, block.data, 32);
        tlen = 32;
        size_t take = 32;
        if (filled + take > n) take = n - filled;
        memcpy(out + filled, block.data, take);
        filled += take;
        mako_str_free(block);
    }
    mako_str_free(prk);
    out[n] = 0;
    return (MakoString){out, n};
}

/* PBKDF2-HMAC-SHA256 → derived key of `dklen` bytes. A SCRAM-SHA-256 / password
 * KDF primitive, backed by the platform crypto library. Empty on failure or when
 * no crypto backend is available. */
static inline MakoString mako_pbkdf2_sha256(MakoString password, MakoString salt,
                                            int64_t iterations, int64_t dklen) {
    if (iterations <= 0 || dklen <= 0 || dklen > 1024) return mako_str_from_cstr("");
    unsigned char *out = (unsigned char *)malloc((size_t)dklen + 1);
    if (!out) return mako_str_from_cstr("");
    int ok = 0;
#if defined(MAKO_HAS_CC)
    ok = CCKeyDerivationPBKDF(kCCPBKDF2,
                              password.data ? password.data : "", password.len,
                              (const uint8_t *)(salt.data ? salt.data : ""), salt.len,
                              kCCPRFHmacAlgSHA256, (unsigned)iterations,
                              out, (size_t)dklen) == 0; /* kCCSuccess */
#elif defined(MAKO_HAS_OPENSSL)
    ok = PKCS5_PBKDF2_HMAC(password.data ? password.data : "", (int)password.len,
                           (const unsigned char *)(salt.data ? salt.data : ""), (int)salt.len,
                           (int)iterations, EVP_sha256(), (int)dklen, out) == 1;
#endif
    if (!ok) { free(out); return mako_str_from_cstr(""); }
    out[dklen] = 0;
    return (MakoString){(char *)out, (size_t)dklen};
}

/* HKDF-Expand-Label seed (RFC 8446 / QUIC initial secrets demo).
 * Not full QUIC packet protection — documents initial-secret derivation shape.
 * label is ASCII (e.g. "quic key"); out_len capped at 32. */
static inline MakoString mako_quic_hkdf_expand_label(
    MakoString secret, MakoString label, int64_t out_len
) {
    if (!secret.data || secret.len == 0 || !label.data || out_len <= 0 || out_len > 32)
        return mako_str_from_cstr("");
    /* HkdfLabel: uint16 length | uint8 label_len | "tls13 " + label | uint8 context_len=0 */
    size_t prefix = 6; /* "tls13 " */
    size_t lab_n = prefix + label.len;
    if (lab_n > 255) return mako_str_from_cstr("");
    size_t hklen = 2 + 1 + lab_n + 1;
    char *hk = (char *)malloc(hklen);
    if (!hk) return mako_str_from_cstr("");
    hk[0] = (char)((out_len >> 8) & 0xff);
    hk[1] = (char)(out_len & 0xff);
    hk[2] = (char)lab_n;
    memcpy(hk + 3, "tls13 ", 6);
    memcpy(hk + 3 + 6, label.data, label.len);
    hk[3 + lab_n] = 0; /* empty context */
    MakoString info = {hk, hklen};
    /* HKDF-Expand: T(1) = HMAC(secret, info || 0x01) */
    char *info1 = (char *)malloc(hklen + 1);
    if (!info1) { free(hk); return mako_str_from_cstr(""); }
    memcpy(info1, hk, hklen);
    info1[hklen] = 1;
    MakoString msg = {info1, hklen + 1};
    MakoString okm = mako_hmac_sha256_raw(secret, msg);
    free(hk);
    free(info1);
    (void)info;
    if (!okm.data || okm.len < (size_t)out_len) {
        mako_str_free(okm);
        return mako_str_from_cstr("");
    }
    if (okm.len == (size_t)out_len) return okm;
    MakoString clipped = mako_str_slice(okm, 0, out_len);
    mako_str_free(okm);
    return clipped;
}

/* Hex form of HKDF-Expand-Label for tests. */
static inline MakoString mako_quic_hkdf_expand_label_hex(
    MakoString secret, MakoString label, int64_t out_len
) {
    MakoString raw = mako_quic_hkdf_expand_label(secret, label, out_len);
    if (!raw.data || raw.len == 0) return mako_str_from_cstr("");
    char *out = (char *)malloc(raw.len * 2 + 1);
    if (!out) { mako_str_free(raw); return mako_str_from_cstr(""); }
    for (size_t i = 0; i < raw.len; i++)
        sprintf(out + i * 2, "%02x", (unsigned char)raw.data[i]);
    out[raw.len * 2] = 0;
    size_t n = raw.len * 2;
    mako_str_free(raw);
    return (MakoString){out, n};
}

/* RFC 9001 §5.2 initial salt (QUIC v1). */
static const unsigned char MAKO_QUIC_V1_INITIAL_SALT[20] = {
    0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17,
    0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a
};

/* HKDF-Extract(salt, ikm) = HMAC-SHA256(salt, ikm) → 32-byte PRK. */
static inline MakoString mako_quic_hkdf_extract(MakoString salt, MakoString ikm) {
    if (!ikm.data) return mako_str_from_cstr("");
    MakoString s = salt;
    char zeros[32];
    MakoString zero_salt = {zeros, 32};
    if (!salt.data || salt.len == 0) {
        memset(zeros, 0, 32);
        s = zero_salt;
    }
    return mako_hmac_sha256_raw(s, ikm);
}

/* Client initial secret from dest CID (RFC 9001 §5.2).
 * initial_secret = HKDF-Extract(v1_salt, dcid)
 * client_initial_secret = HKDF-Expand-Label(initial_secret, "client in", "", 32)
 * Demo only — not packet protection. */
static inline MakoString mako_quic_initial_client_secret(MakoString dcid) {
    if (!dcid.data || dcid.len == 0) return mako_str_from_cstr("");
    MakoString salt = {(char *)MAKO_QUIC_V1_INITIAL_SALT, 20};
    MakoString prk = mako_quic_hkdf_extract(salt, dcid);
    if (!prk.data || prk.len != 32) { mako_str_free(prk); return mako_str_from_cstr(""); }
    MakoString lab = mako_str_from_cstr("client in");
    MakoString out = mako_quic_hkdf_expand_label(prk, lab, 32);
    mako_str_free(prk);
    mako_str_free(lab);
    return out;
}

static inline MakoString mako_quic_initial_client_secret_hex(MakoString dcid) {
    MakoString raw = mako_quic_initial_client_secret(dcid);
    if (!raw.data || raw.len == 0) return mako_str_from_cstr("");
    char *out = (char *)malloc(raw.len * 2 + 1);
    if (!out) { mako_str_free(raw); return mako_str_from_cstr(""); }
    for (size_t i = 0; i < raw.len; i++)
        sprintf(out + i * 2, "%02x", (unsigned char)raw.data[i]);
    out[raw.len * 2] = 0;
    size_t n = raw.len * 2;
    mako_str_free(raw);
    return (MakoString){out, n};
}

/* Derive quic key (16) / quic iv (12) from client_initial_secret (RFC 9001 §5.1). */
static inline MakoString mako_quic_initial_client_key(MakoString dcid) {
    MakoString sec = mako_quic_initial_client_secret(dcid);
    if (!sec.data) return mako_str_from_cstr("");
    MakoString lab = mako_str_from_cstr("quic key");
    MakoString out = mako_quic_hkdf_expand_label(sec, lab, 16);
    mako_str_free(sec);
    mako_str_free(lab);
    return out;
}

static inline MakoString mako_quic_initial_client_iv(MakoString dcid) {
    MakoString sec = mako_quic_initial_client_secret(dcid);
    if (!sec.data) return mako_str_from_cstr("");
    MakoString lab = mako_str_from_cstr("quic iv");
    MakoString out = mako_quic_hkdf_expand_label(sec, lab, 12);
    mako_str_free(sec);
    mako_str_free(lab);
    return out;
}

/* Header protection key: HKDF-Expand-Label(..., "quic hp", "", 16). */
static inline MakoString mako_quic_initial_client_hp(MakoString dcid) {
    MakoString sec = mako_quic_initial_client_secret(dcid);
    if (!sec.data) return mako_str_from_cstr("");
    MakoString lab = mako_str_from_cstr("quic hp");
    MakoString out = mako_quic_hkdf_expand_label(sec, lab, 16);
    mako_str_free(sec);
    mako_str_free(lab);
    return out;
}

static inline MakoString mako_quic_initial_client_key_hex(MakoString dcid) {
    MakoString raw = mako_quic_initial_client_key(dcid);
    if (!raw.data || raw.len == 0) return mako_str_from_cstr("");
    char *out = (char *)malloc(raw.len * 2 + 1);
    if (!out) { mako_str_free(raw); return mako_str_from_cstr(""); }
    for (size_t i = 0; i < raw.len; i++)
        sprintf(out + i * 2, "%02x", (unsigned char)raw.data[i]);
    out[raw.len * 2] = 0;
    size_t n = raw.len * 2;
    mako_str_free(raw);
    return (MakoString){out, n};
}

static inline MakoString mako_quic_initial_client_iv_hex(MakoString dcid) {
    MakoString raw = mako_quic_initial_client_iv(dcid);
    if (!raw.data || raw.len == 0) return mako_str_from_cstr("");
    char *out = (char *)malloc(raw.len * 2 + 1);
    if (!out) { mako_str_free(raw); return mako_str_from_cstr(""); }
    for (size_t i = 0; i < raw.len; i++)
        sprintf(out + i * 2, "%02x", (unsigned char)raw.data[i]);
    out[raw.len * 2] = 0;
    size_t n = raw.len * 2;
    mako_str_free(raw);
    return (MakoString){out, n};
}

static inline MakoString mako_quic_initial_client_hp_hex(MakoString dcid) {
    MakoString raw = mako_quic_initial_client_hp(dcid);
    if (!raw.data || raw.len == 0) return mako_str_from_cstr("");
    char *out = (char *)malloc(raw.len * 2 + 1);
    if (!out) { mako_str_free(raw); return mako_str_from_cstr(""); }
    for (size_t i = 0; i < raw.len; i++)
        sprintf(out + i * 2, "%02x", (unsigned char)raw.data[i]);
    out[raw.len * 2] = 0;
    size_t n = raw.len * 2;
    mako_str_free(raw);
    return (MakoString){out, n};
}

/* Nonce = IV XOR left-padded packet number (RFC 9001 §5.3). */
static inline MakoString mako_quic_aead_nonce(MakoString iv, int64_t packet_number) {
    if (!iv.data || iv.len != 12 || packet_number < 0) return mako_str_from_cstr("");
    char *d = (char *)malloc(13);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, iv.data, 12);
    d[12] = 0;
    uint64_t pn = (uint64_t)packet_number & 0x3fffffffffffffffULL;
    for (int i = 0; i < 8; i++) {
        d[4 + i] ^= (char)((pn >> (56 - 8 * i)) & 0xff);
    }
    return (MakoString){d, 12};
}

/* Decode even-length hex string → binary (for demo vectors / DCID literals). */
static inline MakoString mako_hex_decode(MakoString hex) {
    if (!hex.data || hex.len == 0 || (hex.len % 2) != 0) return mako_str_from_cstr("");
    size_t n = hex.len / 2;
    char *d = (char *)malloc(n + 1);
    if (!d) return mako_str_from_cstr("");
    for (size_t i = 0; i < n; i++) {
        unsigned int v = 0;
        if (sscanf(hex.data + i * 2, "%02x", &v) != 1) {
            free(d);
            return mako_str_from_cstr("");
        }
        d[i] = (char)v;
    }
    d[n] = 0;
    return (MakoString){d, n};
}

/* ---- share / RC (cycle-free shared heap; atomic refcount for crew-safe clones) ---- */
typedef struct {
    int64_t *ptr;
    int64_t *refcount;
} MakoShareInt;

static inline MakoShareInt mako_share_int(int64_t v) {
    MakoShareInt s;
    s.ptr = (int64_t *)malloc(sizeof(int64_t));
    *s.ptr = v;
    s.refcount = (int64_t *)malloc(sizeof(int64_t));
    *s.refcount = 1;
    return s;
}

static inline MakoShareInt mako_share_clone(MakoShareInt s) {
    if (s.refcount) {
        /* Atomic so clone+drop is safe across crew tasks after explicit clone. */
        __atomic_add_fetch(s.refcount, (int64_t)1, __ATOMIC_SEQ_CST);
    }
    return s;
}

static inline int64_t mako_share_get(MakoShareInt s) {
    return s.ptr ? *s.ptr : 0;
}

static inline void mako_share_set(MakoShareInt s, int64_t v) {
    if (s.ptr) {
        __atomic_store_n(s.ptr, v, __ATOMIC_SEQ_CST);
    }
}

static inline void mako_share_drop(MakoShareInt s) {
    if (!s.refcount) return;
    if (__atomic_sub_fetch(s.refcount, (int64_t)1, __ATOMIC_SEQ_CST) <= 0) {
        free(s.ptr);
        free(s.refcount);
    }
}

/* ---- Zero-copy slice / view over int array (legacy helpers; prefer []int + a[i:j]) ---- */
typedef struct {
    int64_t *data;
    size_t len;
} MakoSlice;

static inline MakoSlice mako_slice_ints(MakoIntArray a, int64_t start, int64_t end) {
    MakoSlice s = {NULL, 0};
    if (start < 0) start = 0;
    if (end < start) end = start;
    if ((size_t)end > a.len) end = (int64_t)a.len;
    if ((size_t)start >= a.len) return s;
    s.data = a.data + (size_t)start;
    s.len = (size_t)(end - start);
    return s;
}

static inline int64_t mako_slice_len(MakoSlice s) { return (int64_t)s.len; }

static inline int64_t mako_slice_get(MakoSlice s, int64_t i) {
    if (i < 0 || (size_t)i >= s.len) {
        mako_abort("slice index out of bounds");
    }
    return s.data[i];
}

/* ---- Safe add with overflow check ---- */
static inline int64_t mako_safe_add(int64_t a, int64_t b) {
    int64_t r;
    if (__builtin_add_overflow(a, b, &r)) {
        mako_abort("integer overflow in safe_add");
    }
    return r;
}

/* ---- Plugin host loader (rich package over mako_plugin.h ABI) ---- */
#define MAKO_PLUGIN_HOST_MAX 16
typedef struct {
    void *handle;
    const MakoPluginVTable *vt;
    int used;
    char path[512];
} MakoPluginHostSlot;
static MakoPluginHostSlot mako_plugin_slots[MAKO_PLUGIN_HOST_MAX];
static int64_t mako_plugin_last_err = 0; /* 0=ok, 1=path, 2=dlopen, 3=entry, 4=abi, 5=full */
static char mako_plugin_log_buf[2048];
static int64_t mako_plugin_log_level_v = 0;
static int64_t mako_plugin_log_count_v = 0;
static void mako_plugin_host_log(int32_t level, MakoString message) {
    mako_plugin_log_level_v = level;
    mako_plugin_log_count_v++;
    size_t n = message.len < sizeof(mako_plugin_log_buf) - 1 ? message.len : sizeof(mako_plugin_log_buf) - 1;
    if (message.data && n) memcpy(mako_plugin_log_buf, message.data, n);
    mako_plugin_log_buf[n] = 0;
}

static inline int64_t mako_plugin_max_slots(void) { return MAKO_PLUGIN_HOST_MAX; }
static inline int64_t mako_plugin_abi_version(void) { return (int64_t)MAKO_PLUGIN_ABI_VERSION; }
static inline MakoString mako_plugin_api_version(void) {
    return mako_str_from_cstr(MAKO_PLUGIN_API_VERSION);
}
static inline int64_t mako_plugin_last_error(void) { return mako_plugin_last_err; }
static inline MakoString mako_plugin_last_error_str(void) {
    switch (mako_plugin_last_err) {
        case 0: return mako_str_from_cstr("ok");
        case 1: return mako_str_from_cstr("bad path");
        case 2: return mako_str_from_cstr("dlopen failed");
        case 3: return mako_str_from_cstr("missing mako_plugin_entry");
        case 4: return mako_str_from_cstr("abi mismatch");
        case 5: return mako_str_from_cstr("slot table full");
        case 6: return mako_str_from_cstr("unsupported platform");
        default: return mako_str_from_cstr("unknown");
    }
}

static inline int64_t mako_plugin_open(MakoString path) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)path;
    mako_plugin_last_err = 6;
    return -1;
#else
    mako_plugin_last_err = 0;
    if (!path.data || path.len == 0 || path.len >= 512) {
        mako_plugin_last_err = 1;
        return -1;
    }
    char pbuf[512];
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    void *h = dlopen(pbuf, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        mako_plugin_last_err = 2;
        return -1;
    }
    MakoPluginEntryFn entry = (MakoPluginEntryFn)dlsym(h, "mako_plugin_entry");
    if (!entry) {
        dlclose(h);
        mako_plugin_last_err = 3;
        return -1;
    }
    const MakoPluginVTable *vt = entry();
    if (!vt || !mako_plugin_abi_compatible(vt->abi_version)) {
        dlclose(h);
        mako_plugin_last_err = 4;
        return -1;
    }
    for (int i = 0; i < MAKO_PLUGIN_HOST_MAX; i++) {
        if (!mako_plugin_slots[i].used) {
            mako_plugin_slots[i].handle = h;
            mako_plugin_slots[i].vt = vt;
            mako_plugin_slots[i].used = 1;
            memcpy(mako_plugin_slots[i].path, pbuf, path.len + 1);
            if (vt->init) {
                MakoPluginHost host;
                host.abi_version = MAKO_PLUGIN_ABI_VERSION;
                host.log = mako_plugin_host_log;
                host.user_data = NULL;
                (void)vt->init(&host);
            }
            return i;
        }
    }
    dlclose(h);
    mako_plugin_last_err = 5;
    return -1;
#endif
}

static inline int64_t mako_plugin_alive(int64_t handle) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)handle;
    return 0;
#else
    if (handle < 0 || handle >= MAKO_PLUGIN_HOST_MAX) return 0;
    return mako_plugin_slots[handle].used ? 1 : 0;
#endif
}

static inline MakoString mako_plugin_path(int64_t handle) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)handle;
    return mako_str_from_cstr("");
#else
    if (handle < 0 || handle >= MAKO_PLUGIN_HOST_MAX || !mako_plugin_slots[handle].used)
        return mako_str_from_cstr("");
    return mako_str_from_cstr(mako_plugin_slots[handle].path);
#endif
}

static inline MakoString mako_plugin_name(int64_t handle) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)handle;
    return mako_str_from_cstr("");
#else
    if (handle < 0 || handle >= MAKO_PLUGIN_HOST_MAX || !mako_plugin_slots[handle].used)
        return mako_str_from_cstr("");
    const MakoPluginVTable *vt = mako_plugin_slots[handle].vt;
    if (!vt || !vt->info.name) return mako_str_from_cstr("");
    return mako_str_from_cstr(vt->info.name);
#endif
}

static inline MakoString mako_plugin_version(int64_t handle) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)handle;
    return mako_str_from_cstr("");
#else
    if (handle < 0 || handle >= MAKO_PLUGIN_HOST_MAX || !mako_plugin_slots[handle].used)
        return mako_str_from_cstr("");
    const MakoPluginVTable *vt = mako_plugin_slots[handle].vt;
    if (!vt || !vt->info.version) return mako_str_from_cstr("");
    return mako_str_from_cstr(vt->info.version);
#endif
}

static inline MakoString mako_plugin_kind(int64_t handle) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)handle;
    return mako_str_from_cstr("");
#else
    if (handle < 0 || handle >= MAKO_PLUGIN_HOST_MAX || !mako_plugin_slots[handle].used)
        return mako_str_from_cstr("");
    const MakoPluginVTable *vt = mako_plugin_slots[handle].vt;
    if (!vt || !vt->info.kind) return mako_str_from_cstr("native");
    return mako_str_from_cstr(vt->info.kind);
#endif
}

static inline int64_t mako_plugin_plugin_abi(int64_t handle) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)handle;
    return -1;
#else
    if (handle < 0 || handle >= MAKO_PLUGIN_HOST_MAX || !mako_plugin_slots[handle].used)
        return -1;
    const MakoPluginVTable *vt = mako_plugin_slots[handle].vt;
    return vt ? (int64_t)vt->abi_version : -1;
#endif
}

static inline int64_t mako_plugin_count(void) {
    int64_t n = 0;
    for (int i = 0; i < MAKO_PLUGIN_HOST_MAX; i++) {
        if (mako_plugin_slots[i].used) n++;
    }
    return n;
}

static inline MakoString mako_plugin_call(int64_t handle, MakoString op, MakoString payload) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)handle;
    (void)op;
    (void)payload;
    return mako_str_from_cstr("");
#else
    if (handle < 0 || handle >= MAKO_PLUGIN_HOST_MAX || !mako_plugin_slots[handle].used)
        return mako_str_from_cstr("");
    const MakoPluginVTable *vt = mako_plugin_slots[handle].vt;
    if (!vt || !vt->call) return mako_str_from_cstr("");
    MakoString out = vt->call(op, payload);
    /* If plugin provides free_string, copy into host-owned buffer and free. */
    if (vt->free_string && out.data) {
        MakoString copy = mako_str_clone(out);
        vt->free_string(out);
        return copy;
    }
    return out;
#endif
}

/* Call and report whether the plugin slot is alive (empty string may still be success). */
static inline int64_t mako_plugin_call_ok(int64_t handle) {
    return mako_plugin_alive(handle);
}

static inline int64_t mako_plugin_close(int64_t handle) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)handle;
    return 0;
#else
    if (handle < 0 || handle >= MAKO_PLUGIN_HOST_MAX || !mako_plugin_slots[handle].used)
        return 0;
    const MakoPluginVTable *vt = mako_plugin_slots[handle].vt;
    if (vt && vt->shutdown) vt->shutdown();
    if (mako_plugin_slots[handle].handle) dlclose(mako_plugin_slots[handle].handle);
    mako_plugin_slots[handle].handle = NULL;
    mako_plugin_slots[handle].vt = NULL;
    mako_plugin_slots[handle].used = 0;
    mako_plugin_slots[handle].path[0] = 0;
    return 1;
#endif
}

/* Close all open plugin slots (test / process teardown). */
static inline int64_t mako_plugin_close_all(void) {
    int64_t n = 0;
    for (int i = 0; i < MAKO_PLUGIN_HOST_MAX; i++) {
        if (mako_plugin_slots[i].used) {
            (void)mako_plugin_close(i);
            n++;
        }
    }
    return n;
}

/* ---- Plugin product surface: registry, log host, reload, manifest ---- */
static inline MakoString mako_plugin_last_log(void) {
    return mako_str_from_cstr(mako_plugin_log_buf);
}
static inline int64_t mako_plugin_last_log_level(void) { return mako_plugin_log_level_v; }
static inline int64_t mako_plugin_log_count(void) { return mako_plugin_log_count_v; }

/* Re-open path for handle (hot swap same slot path). */
static inline int64_t mako_plugin_reload(int64_t handle) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)handle;
    return -1;
#else
    if (handle < 0 || handle >= MAKO_PLUGIN_HOST_MAX || !mako_plugin_slots[handle].used)
        return -1;
    char path[512];
    memcpy(path, mako_plugin_slots[handle].path, sizeof(path));
    (void)mako_plugin_close(handle);
    MakoString p = mako_str_from_cstr(path);
    int64_t h = mako_plugin_open(p);
    mako_str_free(p);
    return h;
#endif
}

/* Find first open plugin whose info.name equals `name`. */
static inline int64_t mako_plugin_find(MakoString name) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)name;
    return -1;
#else
    if (!name.data || name.len == 0) return -1;
    for (int i = 0; i < MAKO_PLUGIN_HOST_MAX; i++) {
        if (!mako_plugin_slots[i].used) continue;
        const MakoPluginVTable *vt = mako_plugin_slots[i].vt;
        if (!vt || !vt->info.name) continue;
        size_t n = strlen(vt->info.name);
        if (n == name.len && memcmp(vt->info.name, name.data, n) == 0) return i;
    }
    return -1;
#endif
}

/* Parse artifact= from a minimal mako.plugin.toml (key = "value" lines). */
static inline MakoString mako_plugin_manifest_artifact(MakoString toml_path) {
    MakoString raw = mako_read_file(toml_path);
    if (!raw.data || raw.len == 0) return mako_str_from_cstr("");
    /* Look for artifact = "..." */
    const char *key = "artifact";
    for (size_t i = 0; i + 8 < raw.len; i++) {
        if (memcmp(raw.data + i, key, 8) != 0) continue;
        size_t j = i + 8;
        while (j < raw.len && (raw.data[j] == ' ' || raw.data[j] == '\t')) j++;
        if (j >= raw.len || raw.data[j] != '=') continue;
        j++;
        while (j < raw.len && (raw.data[j] == ' ' || raw.data[j] == '\t')) j++;
        if (j >= raw.len || raw.data[j] != '"') continue;
        j++;
        size_t start = j;
        while (j < raw.len && raw.data[j] != '"') j++;
        if (j >= raw.len) break;
        size_t n = j - start;
        char *d = (char *)malloc(n + 1);
        if (!d) {
            mako_str_free(raw);
            return mako_str_from_cstr("");
        }
        memcpy(d, raw.data + start, n);
        d[n] = 0;
        mako_str_free(raw);
        MakoString out = {d, n};
        return out;
    }
    mako_str_free(raw);
    return mako_str_from_cstr("");
}

/* Resolve relative artifact against directory of manifest path. */
static inline MakoString mako_plugin_manifest_lib_path(MakoString toml_path) {
    MakoString art = mako_plugin_manifest_artifact(toml_path);
    if (!art.data || art.len == 0) return art;
    /* If absolute, return as-is. */
    if (art.data[0] == '/') return art;
    /* dirname of toml */
    size_t slash = toml_path.len;
    while (slash > 0 && toml_path.data[slash - 1] != '/') slash--;
    MakoString dir = mako_str_view(toml_path.data, slash > 0 ? slash - 1 : 0);
    if (dir.len == 0) return art;
    MakoString joined = mako_path_join(dir, art);
    mako_str_free(art);
    return joined;
}

static inline int64_t mako_plugin_open_manifest(MakoString toml_path) {
    MakoString lib = mako_plugin_manifest_lib_path(toml_path);
    if (!lib.data || lib.len == 0) {
        mako_plugin_last_err = 1;
        return -1;
    }
    int64_t h = mako_plugin_open(lib);
    mako_str_free(lib);
    return h;
}

/* Call with automatic empty-payload convenience. */
static inline MakoString mako_plugin_call1(int64_t handle, MakoString op) {
    return mako_plugin_call(handle, op, mako_str_from_cstr(""));
}

/* Product info JSON for a handle. */
static inline MakoString mako_plugin_info_json(int64_t handle) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)handle;
    return mako_str_from_cstr("{}");
#else
    if (handle < 0 || handle >= MAKO_PLUGIN_HOST_MAX || !mako_plugin_slots[handle].used)
        return mako_str_from_cstr("{}");
    const MakoPluginVTable *vt = mako_plugin_slots[handle].vt;
    const char *name = (vt && vt->info.name) ? vt->info.name : "";
    const char *ver = (vt && vt->info.version) ? vt->info.version : "";
    const char *kind = (vt && vt->info.kind) ? vt->info.kind : "native";
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
                     "{\"handle\":%lld,\"name\":\"%s\",\"version\":\"%s\",\"kind\":\"%s\","
                     "\"abi\":%u,\"path\":\"%s\"}",
                     (long long)handle, name, ver, kind,
                     vt ? vt->abi_version : 0u, mako_plugin_slots[handle].path);
    if (n < 0) n = 0;
    return mako_str_from_cstr(buf);
#endif
}

/* Interop surface name seed (C sysv today; other ABIs Later). */
static inline MakoString mako_ffi_abi_name(void) {
#if defined(_WIN32)
    return mako_str_from_cstr("c-win");
#else
    return mako_str_from_cstr("c-sysv");
#endif
}

/* Live dylib hot-reload seed: re-open plugin when path mtime changes.
 * Uses plugin_open/close; soft swap counter lives in domain (optional call). */
static int64_t mako_hot_plugin_handle = -1;
static char mako_hot_plugin_path[512];
static int64_t mako_hot_plugin_mtime = -1;
static int64_t mako_hot_plugin_swaps = 0;

static inline int64_t mako_hot_reload_plugin_watch(MakoString path) {
    if (!path.data || path.len == 0 || path.len >= 511) return -1;
    memcpy(mako_hot_plugin_path, path.data, path.len);
    mako_hot_plugin_path[path.len] = 0;
#if !defined(_WIN32) && !defined(MAKO_WASI)
    {
        struct stat st;
        char pbuf[512];
        memcpy(pbuf, path.data, path.len);
        pbuf[path.len] = 0;
        mako_hot_plugin_mtime = (stat(pbuf, &st) == 0) ? (int64_t)st.st_mtime : -1;
    }
#else
    mako_hot_plugin_mtime = -1;
#endif
    if (mako_hot_plugin_handle >= 0) {
        (void)mako_plugin_close(mako_hot_plugin_handle);
        mako_hot_plugin_handle = -1;
    }
    mako_hot_plugin_handle = mako_plugin_open(path);
    return mako_hot_plugin_handle;
}

/* 1 if reloaded, 0 if unchanged, -1 on error/missing path. */
static inline int64_t mako_hot_reload_plugin_poll(void) {
    if (!mako_hot_plugin_path[0]) return -1;
#if defined(_WIN32) || defined(MAKO_WASI)
    return 0;
#else
    struct stat st;
    if (stat(mako_hot_plugin_path, &st) != 0) return -1;
    int64_t mt = (int64_t)st.st_mtime;
    if (mt == mako_hot_plugin_mtime) return 0;
    mako_hot_plugin_mtime = mt;
    if (mako_hot_plugin_handle >= 0) {
        (void)mako_plugin_close(mako_hot_plugin_handle);
        mako_hot_plugin_handle = -1;
    }
    MakoString p = mako_str_from_cstr(mako_hot_plugin_path);
    mako_hot_plugin_handle = mako_plugin_open(p);
    mako_str_free(p);
    mako_hot_plugin_swaps++;
    return mako_hot_plugin_handle >= 0 ? 1 : -1;
#endif
}

static inline MakoString mako_hot_reload_plugin_call(MakoString op, MakoString payload) {
    if (mako_hot_plugin_handle < 0) return mako_str_from_cstr("");
    return mako_plugin_call(mako_hot_plugin_handle, op, payload);
}

static inline int64_t mako_hot_reload_plugin_handle_id(void) {
    return mako_hot_plugin_handle;
}

static inline int64_t mako_hot_reload_plugin_swaps(void) {
    return mako_hot_plugin_swaps;
}

static inline int64_t mako_hot_reload_plugin_close(void) {
    if (mako_hot_plugin_handle >= 0) {
        (void)mako_plugin_close(mako_hot_plugin_handle);
        mako_hot_plugin_handle = -1;
    }
    mako_hot_plugin_path[0] = 0;
    mako_hot_plugin_mtime = -1;
    return 1;
}

/* ---- Plugin / dlopen ---- */
static inline int64_t mako_dlopen_probe(MakoString path) {
#if defined(_WIN32)
    (void)path;
    return 0;
#else
    void *h = dlopen(path.data, RTLD_LAZY);
    if (!h) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 0;
    }
    dlclose(h);
    return 1;
#endif
}

/* ---- DB compatibility handles and wire helpers.
 * Postgres lives in mako_db.h (libpq when MAKO_HAS_LIBPQ). ---- */
typedef struct { int64_t handle; } MakoMysqlConn;
typedef struct { int64_t handle; } MakoRedisConn;

/* Protobuf wire varint (Partial toward gRPC). Zigzag not included. */
static inline MakoString mako_pb_encode_varint(int64_t v) {
    uint64_t x = (uint64_t)v;
    char tmp[10];
    size_t n = 0;
    while (x >= 0x80) {
        tmp[n++] = (char)((x & 0x7f) | 0x80);
        x >>= 7;
    }
    tmp[n++] = (char)x;
    char *d = (char *)malloc(n + 1);
    memcpy(d, tmp, n);
    d[n] = 0;
    return (MakoString){d, n};
}

/* Decode first varint from bytes; returns value, or 0 on empty. Sets *ok=1 on success. */
static inline int64_t mako_pb_decode_varint(MakoString s) {
    if (!s.data || s.len == 0) return 0;
    uint64_t result = 0;
    int shift = 0;
    for (size_t i = 0; i < s.len && i < 10; i++) {
        unsigned char b = (unsigned char)s.data[i];
        result |= (uint64_t)(b & 0x7f) << shift;
        if ((b & 0x80) == 0) return (int64_t)result;
        shift += 7;
    }
    return 0; /* truncated / overflow → 0 */
}

static inline int64_t mako_pb_varint_len(MakoString s) {
    if (!s.data || s.len == 0) return -1;
    for (size_t i = 0; i < s.len && i < 10; i++) {
        if (((unsigned char)s.data[i] & 0x80) == 0) return (int64_t)(i + 1);
    }
    return -1;
}

/* Protobuf field key = (field_number << 3) | wire_type. Wire: 0=varint 1=64bit 2=len 5=32bit. */
static inline MakoString mako_pb_encode_key(int64_t field, int64_t wire) {
    if (field < 1 || field > 536870911 || wire < 0 || wire > 7) {
        return mako_pb_encode_varint(0);
    }
    return mako_pb_encode_varint((field << 3) | (wire & 7));
}

static inline int64_t mako_pb_key_field(MakoString s) {
    int64_t k = mako_pb_decode_varint(s);
    return k >> 3;
}

static inline int64_t mako_pb_key_wire(MakoString s) {
    int64_t k = mako_pb_decode_varint(s);
    return k & 7;
}

/* Zigzag for sint32/sint64 (Partial protobuf). */
static inline int64_t mako_pb_zigzag_encode(int64_t n) {
    return (int64_t)(((uint64_t)n << 1) ^ -(uint64_t)(n < 0));
}

static inline int64_t mako_pb_zigzag_decode(int64_t n) {
    return (n >> 1) ^ (-(n & 1));
}

static inline MakoString mako_pb_encode_sint(int64_t n) {
    return mako_pb_encode_varint(mako_pb_zigzag_encode(n));
}

static inline int64_t mako_pb_decode_sint(MakoString s) {
    return mako_pb_zigzag_decode(mako_pb_decode_varint(s));
}

/* Wire type 2: length-delimited payload = varint(len) || bytes. */
static inline MakoString mako_pb_encode_bytes(MakoString payload) {
    MakoString lenv = mako_pb_encode_varint((int64_t)(payload.data ? payload.len : 0));
    MakoString out = mako_str_concat(lenv, payload);
    mako_str_free(lenv);
    return out;
}

/* First varint = declared length; -1 if truncated/invalid. */
static inline int64_t mako_pb_bytes_len(MakoString s) {
    int64_t vl = mako_pb_varint_len(s);
    if (vl < 0) return -1;
    int64_t declared = mako_pb_decode_varint(s);
    if (declared < 0) return -1;
    if ((int64_t)s.len < vl + declared) return -1;
    return declared;
}

/* field_number + varint value (wire 0). */
static inline MakoString mako_pb_encode_field_varint(int64_t field, int64_t value) {
    MakoString key = mako_pb_encode_key(field, 0);
    MakoString val = mako_pb_encode_varint(value);
    MakoString out = mako_str_concat(key, val);
    mako_str_free(key);
    mako_str_free(val);
    return out;
}

/* Fixed simple message: field 1 = string (wire 2), field 2 = varint (wire 0). */
static inline MakoString mako_pb_encode_simple(MakoString name, int64_t id) {
    MakoString k1 = mako_pb_encode_key(1, 2);
    MakoString v1 = mako_pb_encode_bytes(name);
    MakoString f1 = mako_str_concat(k1, v1);
    mako_str_free(k1);
    mako_str_free(v1);
    MakoString f2 = mako_pb_encode_field_varint(2, id);
    MakoString out = mako_str_concat(f1, f2);
    mako_str_free(f1);
    mako_str_free(f2);
    return out;
}

/* Decode one field at *off; advances *off. Returns 1 on success. */
static inline int mako_pb_next_field(
    const unsigned char *p, size_t n, size_t *off,
    int64_t *out_field, int64_t *out_wire,
    int64_t *out_varint, size_t *out_bytes_off, size_t *out_bytes_len
) {
    if (!p || !off || *off >= n) return 0;
    MakoString rest = {(char *)(p + *off), n - *off};
    int64_t kl = mako_pb_varint_len(rest);
    if (kl < 0) return 0;
    int64_t key = mako_pb_decode_varint(rest);
    int64_t field = key >> 3;
    int64_t wire = key & 7;
    size_t i = *off + (size_t)kl;
    if (out_field) *out_field = field;
    if (out_wire) *out_wire = wire;
    if (out_varint) *out_varint = 0;
    if (out_bytes_off) *out_bytes_off = 0;
    if (out_bytes_len) *out_bytes_len = 0;
    if (wire == 0) {
        MakoString vr = {(char *)(p + i), n - i};
        int64_t vl = mako_pb_varint_len(vr);
        if (vl < 0) return 0;
        if (out_varint) *out_varint = mako_pb_decode_varint(vr);
        *off = i + (size_t)vl;
        return 1;
    }
    if (wire == 2) {
        MakoString lr = {(char *)(p + i), n - i};
        int64_t ll = mako_pb_varint_len(lr);
        if (ll < 0) return 0;
        int64_t blen = mako_pb_decode_varint(lr);
        if (blen < 0) return 0;
        size_t start = i + (size_t)ll;
        if (start + (size_t)blen > n) return 0;
        if (out_bytes_off) *out_bytes_off = start;
        if (out_bytes_len) *out_bytes_len = (size_t)blen;
        *off = start + (size_t)blen;
        return 1;
    }
    return 0; /* unsupported wire for this seed */
}

static inline MakoString mako_pb_simple_name(MakoString msg) {
    if (!msg.data) return (MakoString){NULL, 0};
    const unsigned char *p = (const unsigned char *)msg.data;
    size_t off = 0;
    while (off < msg.len) {
        int64_t field = 0, wire = 0, vint = 0;
        size_t bo = 0, bl = 0;
        if (!mako_pb_next_field(p, msg.len, &off, &field, &wire, &vint, &bo, &bl)) break;
        if (field == 1 && wire == 2) {
            return mako_str_slice(msg, (int64_t)bo, (int64_t)(bo + bl));
        }
        (void)vint;
    }
    return (MakoString){NULL, 0};
}

static inline int64_t mako_pb_simple_id(MakoString msg) {
    if (!msg.data) return 0;
    const unsigned char *p = (const unsigned char *)msg.data;
    size_t off = 0;
    while (off < msg.len) {
        int64_t field = 0, wire = 0, vint = 0;
        size_t bo = 0, bl = 0;
        if (!mako_pb_next_field(p, msg.len, &off, &field, &wire, &vint, &bo, &bl)) break;
        if (field == 2 && wire == 0) return vint;
        (void)bo; (void)bl;
    }
    return 0;
}

/* Nested: simple msg + field 3 = embedded simple (wire 2). */
static inline MakoString mako_pb_encode_nested(
    MakoString name, int64_t id, MakoString inner_name, int64_t inner_id
) {
    MakoString outer = mako_pb_encode_simple(name, id);
    MakoString inner = mako_pb_encode_simple(inner_name, inner_id);
    MakoString k3 = mako_pb_encode_key(3, 2);
    MakoString v3 = mako_pb_encode_bytes(inner);
    MakoString f3 = mako_str_concat(k3, v3);
    mako_str_free(k3);
    mako_str_free(v3);
    mako_str_free(inner);
    MakoString out = mako_str_concat(outer, f3);
    mako_str_free(outer);
    mako_str_free(f3);
    return out;
}

static inline MakoString mako_pb_nested_inner(MakoString msg) {
    if (!msg.data) return (MakoString){NULL, 0};
    const unsigned char *p = (const unsigned char *)msg.data;
    size_t off = 0;
    while (off < msg.len) {
        int64_t field = 0, wire = 0, vint = 0;
        size_t bo = 0, bl = 0;
        if (!mako_pb_next_field(p, msg.len, &off, &field, &wire, &vint, &bo, &bl)) break;
        if (field == 3 && wire == 2) {
            return mako_str_slice(msg, (int64_t)bo, (int64_t)(bo + bl));
        }
        (void)vint;
    }
    return (MakoString){NULL, 0};
}

/* Repeated field 4 = varint (wire 0), multiple occurrences. */
static inline MakoString mako_pb_encode_repeated_varint(int64_t a, int64_t b, int64_t c) {
    MakoString f1 = mako_pb_encode_field_varint(4, a);
    MakoString f2 = mako_pb_encode_field_varint(4, b);
    MakoString f3 = mako_pb_encode_field_varint(4, c);
    MakoString t = mako_str_concat(f1, f2);
    mako_str_free(f1);
    mako_str_free(f2);
    MakoString out = mako_str_concat(t, f3);
    mako_str_free(t);
    mako_str_free(f3);
    return out;
}

static inline int64_t mako_pb_repeated_count(MakoString msg, int64_t field) {
    if (!msg.data) return 0;
    const unsigned char *p = (const unsigned char *)msg.data;
    size_t off = 0;
    int64_t count = 0;
    while (off < msg.len) {
        int64_t f = 0, wire = 0, vint = 0;
        size_t bo = 0, bl = 0;
        if (!mako_pb_next_field(p, msg.len, &off, &f, &wire, &vint, &bo, &bl)) break;
        if (f == field && wire == 0) count++;
        (void)vint; (void)bo; (void)bl;
    }
    return count;
}

/* 0-based index into repeated varint field occurrences. */
static inline int64_t mako_pb_repeated_at(MakoString msg, int64_t field, int64_t index) {
    if (!msg.data || index < 0) return 0;
    const unsigned char *p = (const unsigned char *)msg.data;
    size_t off = 0;
    int64_t seen = 0;
    while (off < msg.len) {
        int64_t f = 0, wire = 0, vint = 0;
        size_t bo = 0, bl = 0;
        if (!mako_pb_next_field(p, msg.len, &off, &f, &wire, &vint, &bo, &bl)) break;
        if (f == field && wire == 0) {
            if (seen == index) return vint;
            seen++;
        }
        (void)bo; (void)bl;
    }
    return 0;
}

/* gRPC length-prefixed message: 1-byte compressed flag + 4-byte big-endian length + payload.
 * Limits: uncompressed only (flag=0), payload ≤ 16MiB. */
static inline MakoString mako_grpc_encode_message(MakoString payload) {
    size_t plen = payload.data ? payload.len : 0;
    if (plen > 0xFFFFFFu) return (MakoString){NULL, 0};
    size_t n = 5 + plen;
    char *d = (char *)malloc(n + 1);
    if (!d) return (MakoString){NULL, 0};
    d[0] = 0; /* uncompressed */
    d[1] = (char)((plen >> 24) & 0xff);
    d[2] = (char)((plen >> 16) & 0xff);
    d[3] = (char)((plen >> 8) & 0xff);
    d[4] = (char)(plen & 0xff);
    if (plen && payload.data) memcpy(d + 5, payload.data, plen);
    d[n] = 0;
    return (MakoString){d, n};
}

static inline int64_t mako_grpc_message_len(MakoString framed) {
    if (!framed.data || framed.len < 5) return -1;
    if ((unsigned char)framed.data[0] != 0) return -1; /* compressed not supported */
    uint32_t plen = ((uint32_t)(unsigned char)framed.data[1] << 24)
                  | ((uint32_t)(unsigned char)framed.data[2] << 16)
                  | ((uint32_t)(unsigned char)framed.data[3] << 8)
                  | ((uint32_t)(unsigned char)framed.data[4]);
    if (framed.len < 5 + plen) return -1;
    return (int64_t)plen;
}

/* Default gRPC max receive message size seed (4 MiB), matching common defaults. */
#define MAKO_GRPC_DEFAULT_MAX_MSG (4 * 1024 * 1024)

/* 1 if framed message length is within max_bytes (and parseable); 0 otherwise.
 * max_bytes <= 0 → use MAKO_GRPC_DEFAULT_MAX_MSG. */
static inline int64_t mako_grpc_message_within_limit(MakoString framed, int64_t max_bytes) {
    int64_t plen = mako_grpc_message_len(framed);
    if (plen < 0) return 0;
    int64_t lim = max_bytes > 0 ? max_bytes : (int64_t)MAKO_GRPC_DEFAULT_MAX_MSG;
    return plen <= lim ? 1 : 0;
}

static inline int64_t mako_grpc_default_max_message(void) {
    return (int64_t)MAKO_GRPC_DEFAULT_MAX_MSG;
}

static inline MakoString mako_grpc_message_payload(MakoString framed) {
    int64_t plen = mako_grpc_message_len(framed);
    if (plen < 0) return (MakoString){NULL, 0};
    return mako_str_slice(framed, 5, 5 + plen);
}

/* Unary gRPC request body: pb_encode_simple + length-prefix framing. */
static inline MakoString mako_grpc_unary_request(MakoString name, int64_t id) {
    MakoString pb = mako_pb_encode_simple(name, id);
    MakoString framed = mako_grpc_encode_message(pb);
    mako_str_free(pb);
    return framed;
}

/* Decode unary framed message → simple name (empty on failure). */
static inline MakoString mako_grpc_unary_name(MakoString framed) {
    MakoString payload = mako_grpc_message_payload(framed);
    if (!payload.data) return (MakoString){NULL, 0};
    MakoString name = mako_pb_simple_name(payload);
    mako_str_free(payload);
    return name;
}

static inline int64_t mako_grpc_unary_id(MakoString framed) {
    MakoString payload = mako_grpc_message_payload(framed);
    if (!payload.data) return 0;
    int64_t id = mako_pb_simple_id(payload);
    mako_str_free(payload);
    return id;
}

/* gRPC content-type header value. */
static inline MakoString mako_grpc_content_type(void) {
    return mako_str_from_cstr("application/grpc");
}

/* Minimal gRPC status trailer block (HTTP/2 trailers seed).
 * Format: "grpc-status: <code>\\r\\ngrpc-message: \\r\\n" */
static inline MakoString mako_grpc_status_trailer(int64_t code) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "grpc-status: %lld\r\ngrpc-message: \r\n", (long long)code);
    if (n < 0 || n >= (int)sizeof(buf)) return (MakoString){NULL, 0};
    return mako_str_from_cstr(buf);
}

/* Parse grpc-status from trailer bytes; -1 if missing/invalid. */
static inline int64_t mako_grpc_status_code(MakoString trailers) {
    if (!trailers.data) return -1;
    const char *p = trailers.data;
    size_t n = trailers.len;
    const char *key = "grpc-status:";
    size_t klen = 12;
    for (size_t i = 0; i + klen < n; i++) {
        int ok = 1;
        for (size_t j = 0; j < klen; j++) {
            char a = p[i + j];
            char b = key[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (a != b) { ok = 0; break; }
        }
        if (!ok) continue;
        size_t k = i + klen;
        while (k < n && (p[k] == ' ' || p[k] == '\t')) k++;
        if (k >= n) return -1;
        int64_t v = 0;
        int any = 0;
        while (k < n && p[k] >= '0' && p[k] <= '9') {
            v = v * 10 + (p[k] - '0');
            k++;
            any = 1;
        }
        return any ? v : -1;
    }
    return -1;
}

/* Assemble HTTP/2 unary gRPC request: HEADERS (content-type) + DATA (framed pb).
 * Stream id required. HEADERS without END_STREAM; DATA with END_STREAM.
 * Limits: single content-type literal header; no path/:method. */
static inline MakoString mako_grpc_http2_unary(
    int64_t stream, MakoString name, int64_t id
) {
    MakoString ct = mako_grpc_content_type();
    MakoString lit = mako_hpack_encode_literal(mako_str_from_cstr("content-type"), ct);
    mako_str_free(ct);
    MakoString hdrs = mako_http2_headers_frame(stream, lit, 0x4); /* END_HEADERS */
    mako_str_free(lit);
    MakoString body = mako_grpc_unary_request(name, id);
    MakoString data = mako_http2_data_frame(stream, body, 0x1); /* END_STREAM */
    mako_str_free(body);
    MakoString out = mako_http2_concat_frames(hdrs, data);
    mako_str_free(hdrs);
    mako_str_free(data);
    return out;
}

/* Extract gRPC framed payload from a unary HTTP/2 buffer (first DATA frame). */
static inline MakoString mako_grpc_http2_unary_payload(MakoString buf) {
    /* Find first DATA frame (type 0) */
    const unsigned char *p = (const unsigned char *)(buf.data ? buf.data : "");
    size_t n = buf.len;
    size_t off = 0;
    while (off + 9 <= n) {
        int64_t len = -1, typ = -1;
        if (!mako_http2_parse_frame(p + off, n - off, &len, &typ, NULL, NULL)) break;
        if (typ == 0) {
            MakoString frame = mako_str_slice(buf, (int64_t)off, (int64_t)(off + 9 + (size_t)len));
            MakoString payload = mako_http2_frame_payload(frame);
            mako_str_free(frame);
            return payload;
        }
        off += 9 + (size_t)len;
    }
    return (MakoString){NULL, 0};
}

/* Assemble unary gRPC response: HEADERS + DATA + trailer HEADERS (grpc-status).
 * Trailer carries END_STREAM. `status` is encoded as literal decimal in trailer. */
static inline MakoString mako_grpc_http2_unary_response_status(
    int64_t stream, MakoString name, int64_t id, int64_t status
) {
    MakoString ct = mako_grpc_content_type();
    MakoString lit = mako_hpack_encode_literal(mako_str_from_cstr("content-type"), ct);
    mako_str_free(ct);
    MakoString hdrs = mako_http2_headers_frame(stream, lit, 0x4); /* END_HEADERS */
    mako_str_free(lit);
    MakoString body = mako_grpc_unary_request(name, id); /* same wire as request msg */
    MakoString data = mako_http2_data_frame(stream, body, 0);
    mako_str_free(body);
    char scode[16];
    int sn = snprintf(scode, sizeof(scode), "%lld", (long long)status);
    if (sn < 0 || sn >= (int)sizeof(scode)) {
        mako_str_free(hdrs);
        mako_str_free(data);
        return (MakoString){NULL, 0};
    }
    MakoString tlit = mako_hpack_encode_literal(
        mako_str_from_cstr("grpc-status"), mako_str_from_cstr(scode)
    );
    MakoString th = mako_http2_headers_frame(stream, tlit, 0x5); /* END_HEADERS|END_STREAM */
    mako_str_free(tlit);
    MakoString mid = mako_http2_concat_frames(hdrs, data);
    mako_str_free(hdrs);
    mako_str_free(data);
    MakoString out = mako_http2_concat_frames(mid, th);
    mako_str_free(mid);
    mako_str_free(th);
    return out;
}

/* Assemble unary gRPC response with grpc-status 0 (compat). */
static inline MakoString mako_grpc_http2_unary_response(
    int64_t stream, MakoString name, int64_t id
) {
    return mako_grpc_http2_unary_response_status(stream, name, id, 0);
}

/* Parse response DATA payload from assembled unary response (HEADERS+DATA+trailers). */
static inline MakoString mako_grpc_http2_response_payload(MakoString buf) {
    return mako_grpc_http2_unary_payload(buf);
}

/* Extract grpc-status from trailer HEADERS after DATA in a multi-frame buffer.
 * Requires: HEADERS (no END_STREAM) … DATA … HEADERS with END_STREAM.
 * Decodes HPACK on the trailer block only. Returns -1 if missing/invalid. */
static inline int64_t mako_grpc_http2_response_status(MakoString buf) {
    if (!buf.data || buf.len < 9) return -1;
    const unsigned char *p = (const unsigned char *)buf.data;
    size_t n = buf.len;
    size_t off = 0;
    int saw_headers = 0;
    int saw_data = 0;
    int64_t status = -1;
    while (off + 9 <= n) {
        int64_t len = -1, typ = -1, flags = -1;
        if (!mako_http2_parse_frame(p + off, n - off, &len, &typ, &flags, NULL)) break;
        if (typ == 1) {
            if ((flags & 0x1) == 0) {
                saw_headers = 1; /* initial response headers */
            } else if (saw_headers && saw_data && len > 0) {
                /* Trailer HEADERS after DATA */
                MakoString frame = mako_str_slice(buf, (int64_t)off, (int64_t)(off + 9 + (size_t)len));
                MakoString pl = mako_http2_frame_payload(frame);
                mako_str_free(frame);
                if (mako_hpack_decode_block(pl) >= 0) {
                    int64_t cnt = mako_hpack_decoded_count();
                    for (int64_t i = 0; i < cnt; i++) {
                        MakoString nm = mako_hpack_decoded_name(i);
                        MakoString vl = mako_hpack_decoded_value(i);
                        if (nm.data && nm.len == 11
                            && memcmp(nm.data, "grpc-status", 11) == 0 && vl.data) {
                            int64_t v = 0;
                            int any = 0;
                            for (size_t k = 0; k < vl.len; k++) {
                                if (vl.data[k] < '0' || vl.data[k] > '9') { any = 0; break; }
                                v = v * 10 + (vl.data[k] - '0');
                                any = 1;
                            }
                            if (any) status = v;
                        }
                        mako_str_free(nm);
                        mako_str_free(vl);
                    }
                }
                mako_str_free(pl);
            }
        } else if (typ == 0) {
            saw_data = 1;
        }
        off += 9 + (size_t)len;
    }
    return status;
}

/* gRPC streaming seed: one DATA frame carrying a framed message.
 * end_stream=0 → more messages may follow; end_stream!=0 → END_STREAM flag. */
static inline MakoString mako_grpc_http2_stream_data(
    int64_t stream, MakoString name, int64_t id, int64_t end_stream
) {
    MakoString body = mako_grpc_unary_request(name, id);
    int64_t flags = end_stream ? 0x1 : 0;
    MakoString data = mako_http2_data_frame(stream, body, flags);
    mako_str_free(body);
    return data;
}

/* Two-message client stream: DATA (no END_STREAM) + DATA (END_STREAM). */
static inline MakoString mako_grpc_http2_stream_two(
    int64_t stream, MakoString name1, int64_t id1, MakoString name2, int64_t id2
) {
    MakoString a = mako_grpc_http2_stream_data(stream, name1, id1, 0);
    MakoString b = mako_grpc_http2_stream_data(stream, name2, id2, 1);
    MakoString out = mako_http2_concat_frames(a, b);
    mako_str_free(a);
    mako_str_free(b);
    return out;
}

/* Count DATA frames on `stream` in a buffer; optionally require last has END_STREAM.
 * Returns count, or -1 if no DATA / last missing END_STREAM when require_end!=0. */
static inline int64_t mako_grpc_http2_stream_data_count(MakoString buf, int64_t stream) {
    if (!buf.data || buf.len < 9 || stream <= 0) return -1;
    const unsigned char *p = (const unsigned char *)buf.data;
    size_t n = buf.len;
    size_t off = 0;
    int64_t count = 0;
    int last_es = 0;
    while (off + 9 <= n) {
        int64_t len = -1, typ = -1, flags = -1, sid = -1;
        if (!mako_http2_parse_frame(p + off, n - off, &len, &typ, &flags, &sid)) break;
        if (typ == 0 && sid == stream) {
            count++;
            last_es = ((flags & 0x1) != 0) ? 1 : 0;
        }
        off += 9 + (size_t)len;
    }
    if (count == 0) return -1;
    return last_es ? count : -count; /* negative if stream not ended */
}

/* One-flow seed: client two-DATA stream + server unary response (status after DATA).
 * Returns concat(stream_two, unary_response_status). Not a live transport. */
static inline MakoString mako_grpc_http2_client_stream_flow(
    int64_t stream,
    MakoString name1, int64_t id1,
    MakoString name2, int64_t id2,
    MakoString reply, int64_t rid,
    int64_t status
) {
    MakoString req = mako_grpc_http2_stream_two(stream, name1, id1, name2, id2);
    MakoString resp = mako_grpc_http2_unary_response_status(stream, reply, rid, status);
    MakoString out = mako_http2_concat_frames(req, resp);
    mako_str_free(req);
    mako_str_free(resp);
    return out;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_STD_H */
