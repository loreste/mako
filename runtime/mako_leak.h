/* Per-scope leak detector — nestable enter/exit over alloc_track live bytes.
 * Include after mako_std.h (needs mako_alloc_track_live_bytes).
 */
#ifndef MAKO_LEAK_H
#define MAKO_LEAK_H

#ifndef MAKO_RT_H
#include "mako_rt.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MAKO_LEAK_SCOPE_MAX 64

typedef struct {
    int64_t mark;
} MakoLeakScopeFrame;

static __thread MakoLeakScopeFrame mako_leak_stack[MAKO_LEAK_SCOPE_MAX];
static __thread int mako_leak_depth = 0;
static __thread int64_t mako_leak_last_imbalance = 0;
static __thread int mako_leak_warn_count = 0;

static inline int64_t mako_leak_scope_enter(void) {
    if (mako_leak_depth >= MAKO_LEAK_SCOPE_MAX) return -1;
    int64_t live = mako_alloc_track_live_bytes();
    mako_leak_stack[mako_leak_depth].mark = live;
    mako_leak_depth++;
    return (int64_t)mako_leak_depth;
}

static inline int64_t mako_leak_scope_exit(void) {
    if (mako_leak_depth <= 0) return 0;
    mako_leak_depth--;
    int64_t mark = mako_leak_stack[mako_leak_depth].mark;
    int64_t live = mako_alloc_track_live_bytes();
    int64_t leaked = live - mark;
    if (leaked < 0) leaked = 0;
    mako_leak_last_imbalance = leaked;
    if (leaked > 0) {
        mako_leak_warn_count++;
        fprintf(stderr,
                "warning: leak_check scope exit: +%lld bytes still live "
                "(mark=%lld live=%lld)\n",
                (long long)leaked, (long long)mark, (long long)live);
    }
    return leaked;
}

/* Bytes leaked relative to current (innermost) scope mark, or live if none. */
static inline int64_t mako_leak_check(void) {
    int64_t live = mako_alloc_track_live_bytes();
    if (mako_leak_depth <= 0) return live;
    int64_t mark = mako_leak_stack[mako_leak_depth - 1].mark;
    int64_t leaked = live - mark;
    return leaked > 0 ? leaked : 0;
}

static inline int64_t mako_leak_last(void) { return mako_leak_last_imbalance; }
static inline int64_t mako_leak_warn_count_get(void) { return mako_leak_warn_count; }
static inline int64_t mako_leak_depth_get(void) { return mako_leak_depth; }

static inline int64_t mako_leak_assert_scope(void) {
    return mako_leak_check() == 0 ? 1 : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_LEAK_H */
