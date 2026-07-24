/* General-purpose deadline timer heap (protocol-agnostic).
 *
 * Any subsystem can arm (deadline_ms, kind, id) timers and poll/run due work.
 * No invented defaults: max_timers and all deadlines are caller-supplied.
 * Absolute ceiling only for allocator safety.
 *
 * Kind and id are opaque to this module — interpretation is the caller's.
 *
 * Memory safety:
 * - Handles pack slot + generation (ABA after free/reuse).
 * - arm() returns a stable token (not a heap index); cancel scans by token.
 * - All mutations under a process mutex; calloc fails closed.
 */
#ifndef MAKO_TIMER_H
#define MAKO_TIMER_H

#include "mako_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAKO_TIMER_HEAP_SLOTS   16
#define MAKO_TIMER_HEAP_CEILING 131072

typedef struct {
    int used;
    int64_t deadline_ms;
    int64_t kind;
    int64_t id;
    int64_t token; /* stable cancel id; never 0 when used */
} MakoTimerNode;

typedef struct {
    int live;
    int max_n;
    int n;
    int64_t drops;
    uint32_t gen;
    int64_t next_token;
    MakoTimerNode *nodes;
} MakoTimerHeap;

static MakoTimerHeap mako_timer_heaps[MAKO_TIMER_HEAP_SLOTS];
static pthread_mutex_t mako_timer_heaps_mu = MAKO_MUTEX_INIT;

static inline int64_t mako_timer_pack_handle(int slot, uint32_t gen) {
    if (gen == 0) gen = 1;
    return (int64_t)(((uint32_t)(slot + 1) & 0xffffu)
                     | ((gen & 0xffffu) << 16));
}
static inline int mako_timer_handle_slot(int64_t h) {
    int id = (int)(h & 0xffff);
    if (id <= 0 || id > MAKO_TIMER_HEAP_SLOTS) return -1;
    return id - 1;
}
static inline uint32_t mako_timer_handle_gen(int64_t h) {
    return (uint32_t)((h >> 16) & 0xffffu);
}

/* Caller must hold mako_timer_heaps_mu. */
static inline int mako_timer_live_locked(int64_t handle, MakoTimerHeap **out) {
    int slot = mako_timer_handle_slot(handle);
    if (slot < 0) return 0;
    MakoTimerHeap *h = &mako_timer_heaps[slot];
    if (!h->live) return 0;
    if (mako_timer_handle_gen(handle) != h->gen) return 0;
    if (out) *out = h;
    return 1;
}

static inline void mako_timer_swap(MakoTimerNode *a, MakoTimerNode *b) {
    MakoTimerNode t = *a;
    *a = *b;
    *b = t;
}

static inline void mako_timer_up(MakoTimerHeap *h, int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h->nodes[p].deadline_ms <= h->nodes[i].deadline_ms) break;
        mako_timer_swap(&h->nodes[p], &h->nodes[i]);
        i = p;
    }
}

static inline void mako_timer_down(MakoTimerHeap *h, int i) {
    for (;;) {
        int l = 2 * i + 1, r = l + 1, s = i;
        if (l < h->n && h->nodes[l].deadline_ms < h->nodes[s].deadline_ms) s = l;
        if (r < h->n && h->nodes[r].deadline_ms < h->nodes[s].deadline_ms) s = r;
        if (s == i) break;
        mako_timer_swap(&h->nodes[s], &h->nodes[i]);
        i = s;
    }
}

static inline int64_t mako_timer_heap_new(int64_t max_timers) {
    if (max_timers <= 0 || max_timers > MAKO_TIMER_HEAP_CEILING) return -1;
    /* Overflow-safe: ceiling * sizeof(node) fits size_t on all targets. */
    size_t bytes = (size_t)max_timers * sizeof(MakoTimerNode);
    if (bytes / sizeof(MakoTimerNode) != (size_t)max_timers) return -1;

    pthread_mutex_lock(&mako_timer_heaps_mu);
    int slot = -1;
    for (int i = 0; i < MAKO_TIMER_HEAP_SLOTS; i++) {
        if (!mako_timer_heaps[i].live) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&mako_timer_heaps_mu);
        return -1;
    }
    MakoTimerHeap *h = &mako_timer_heaps[slot];
    uint32_t prev_gen = h->gen;
    MakoTimerNode *nodes = (MakoTimerNode *)calloc((size_t)max_timers, sizeof(MakoTimerNode));
    if (!nodes) {
        pthread_mutex_unlock(&mako_timer_heaps_mu);
        return -1;
    }
    memset(h, 0, sizeof(*h));
    h->nodes = nodes;
    h->live = 1;
    h->max_n = (int)max_timers;
    h->gen = (prev_gen + 1) & 0xffffu;
    if (h->gen == 0) h->gen = 1;
    h->next_token = 1;
    int64_t handle = mako_timer_pack_handle(slot, h->gen);
    pthread_mutex_unlock(&mako_timer_heaps_mu);
    return handle;
}

static inline int64_t mako_timer_heap_free(int64_t handle) {
    pthread_mutex_lock(&mako_timer_heaps_mu);
    MakoTimerHeap *h = NULL;
    if (!mako_timer_live_locked(handle, &h)) {
        pthread_mutex_unlock(&mako_timer_heaps_mu);
        return -1;
    }
    uint32_t gen = h->gen;
    free(h->nodes);
    memset(h, 0, sizeof(*h));
    h->gen = gen; /* live=0; next new() increments */
    pthread_mutex_unlock(&mako_timer_heaps_mu);
    return 0;
}

/* Arm a timer. Returns stable token (>0) or -1. Cancel by token, not index. */
static inline int64_t mako_timer_heap_arm(
    int64_t handle, int64_t deadline_ms, int64_t kind, int64_t id
) {
    pthread_mutex_lock(&mako_timer_heaps_mu);
    MakoTimerHeap *h = NULL;
    if (!mako_timer_live_locked(handle, &h)) {
        pthread_mutex_unlock(&mako_timer_heaps_mu);
        return -1;
    }
    if (h->n >= h->max_n) {
        h->drops++;
        pthread_mutex_unlock(&mako_timer_heaps_mu);
        return -1;
    }
    int64_t token = h->next_token++;
    if (token <= 0) {
        h->next_token = 1;
        token = 1;
    }
    int i = h->n++;
    h->nodes[i].used = 1;
    h->nodes[i].deadline_ms = deadline_ms;
    h->nodes[i].kind = kind;
    h->nodes[i].id = id;
    h->nodes[i].token = token;
    mako_timer_up(h, i);
    pthread_mutex_unlock(&mako_timer_heaps_mu);
    return token;
}

/* Lazy-cancel by stable token from arm(). Safe after heap reorders. */
static inline int64_t mako_timer_heap_cancel(int64_t handle, int64_t token) {
    if (token <= 0) return -1;
    pthread_mutex_lock(&mako_timer_heaps_mu);
    MakoTimerHeap *h = NULL;
    if (!mako_timer_live_locked(handle, &h)) {
        pthread_mutex_unlock(&mako_timer_heaps_mu);
        return -1;
    }
    for (int i = 0; i < h->n; i++) {
        if (h->nodes[i].used && h->nodes[i].token == token) {
            h->nodes[i].used = 0;
            pthread_mutex_unlock(&mako_timer_heaps_mu);
            return 0;
        }
    }
    pthread_mutex_unlock(&mako_timer_heaps_mu);
    return -1;
}

/* Next deadline still armed, or -1 if empty. */
static inline int64_t mako_timer_heap_next(int64_t handle) {
    pthread_mutex_lock(&mako_timer_heaps_mu);
    MakoTimerHeap *h = NULL;
    if (!mako_timer_live_locked(handle, &h)) {
        pthread_mutex_unlock(&mako_timer_heaps_mu);
        return -1;
    }
    while (h->n > 0 && !h->nodes[0].used) {
        h->nodes[0] = h->nodes[--h->n];
        if (h->n > 0) mako_timer_down(h, 0);
    }
    int64_t d = h->n > 0 ? h->nodes[0].deadline_ms : -1;
    pthread_mutex_unlock(&mako_timer_heaps_mu);
    return d;
}

/* Pop one due timer (deadline <= now_ms). Returns 1 and fills outs, or 0. */
static inline int64_t mako_timer_heap_pop_due(
    int64_t handle, int64_t now_ms, int64_t *kind_out, int64_t *id_out
) {
    pthread_mutex_lock(&mako_timer_heaps_mu);
    MakoTimerHeap *h = NULL;
    if (!mako_timer_live_locked(handle, &h)) {
        pthread_mutex_unlock(&mako_timer_heaps_mu);
        return 0;
    }
    while (h->n > 0 && !h->nodes[0].used) {
        h->nodes[0] = h->nodes[--h->n];
        if (h->n > 0) mako_timer_down(h, 0);
    }
    if (h->n <= 0 || h->nodes[0].deadline_ms > now_ms) {
        pthread_mutex_unlock(&mako_timer_heaps_mu);
        return 0;
    }
    if (kind_out) *kind_out = h->nodes[0].kind;
    if (id_out) *id_out = h->nodes[0].id;
    h->nodes[0] = h->nodes[--h->n];
    if (h->n > 0) mako_timer_down(h, 0);
    pthread_mutex_unlock(&mako_timer_heaps_mu);
    return 1;
}

/* Side channel for last pop (for languages without out-params). */
static __thread int64_t mako_timer_last_kind_v = 0;
static __thread int64_t mako_timer_last_id_v = 0;

static inline int64_t mako_timer_heap_pop_due1(int64_t handle, int64_t now_ms) {
    int64_t k = 0, id = 0;
    int64_t r = mako_timer_heap_pop_due(handle, now_ms, &k, &id);
    if (r) {
        mako_timer_last_kind_v = k;
        mako_timer_last_id_v = id;
    }
    return r;
}

static inline int64_t mako_timer_last_kind(void) {
    return mako_timer_last_kind_v;
}
static inline int64_t mako_timer_last_id(void) {
    return mako_timer_last_id_v;
}

static inline int64_t mako_timer_heap_count(int64_t handle) {
    pthread_mutex_lock(&mako_timer_heaps_mu);
    MakoTimerHeap *h = NULL;
    int n = -1;
    if (mako_timer_live_locked(handle, &h)) n = h->n;
    pthread_mutex_unlock(&mako_timer_heaps_mu);
    return n;
}

static inline int64_t mako_timer_heap_drops(int64_t handle) {
    pthread_mutex_lock(&mako_timer_heaps_mu);
    MakoTimerHeap *h = NULL;
    int64_t d = -1;
    if (mako_timer_live_locked(handle, &h)) d = h->drops;
    pthread_mutex_unlock(&mako_timer_heaps_mu);
    return d;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_TIMER_H */
