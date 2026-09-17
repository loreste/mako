#ifndef MAKO_RUNTIME_METRICS
#define MAKO_RUNTIME_METRICS 0
#endif
#ifndef MAKO_UNICODE17
#define MAKO_UNICODE17 0
#endif
#include "mako_rt.h"
#define MAKO_OVERFLOW_MODE 0
#define MAKO_SAFE_DEFAULT 1
#include "mako_overflow.h"
#ifndef MAKO_WASI
#include "mako_uuid.h"
#include "mako_net.h"
#include "mako_proxy.h"
#include "mako_http.h"
#include "mako_trace.h"
#include "mako_log.h"
#include "mako_std.h"
#include "mako_stdlib.h"
#include "mako_leak.h"
#include "mako_shutdown.h"
#include "mako_tls.h"
#include "mako_dtls.h"
#include "mako_llm.h"
#include "mako_sip.h"
#include "mako_nghttp2.h"
#include "mako_quiche.h"
#include "mako_ws.h"
#include "mako_db.h"
#include "mako_cmap.h"
#include "mako_dio.h"
#include "mako_domain.h"
#include "mako_sctp.h"
#include "mako_timer.h"
#include "mako_peer.h"
#include "mako_diameter.h"
#include "mako_evloop.h"
#include "mako_game.h"
#include "mako_gpu.h"
#include "mako_model.h"
#include "mako_tok.h"
#include "mako_mail.h"
#include "mako_template.h"
#include "mako_fmt.h"
#include "mako_cloud.h"
#include "mako_httpengine.h"
#include "mako_pqc.h"
#include "mako_errtrace.h"
#if MAKO_UNICODE17
#include "mako_unicode17.h"
#endif
#endif /* MAKO_WASI */

struct MakoArr_QueryState;
typedef struct MakoArr_QueryState MakoArr_QueryState;
typedef struct TokenChoice {
    MakoString value;
} TokenChoice;
static inline bool mako_eq_TokenChoice(TokenChoice a, TokenChoice b) {
    return mako_str_eq(a.value, b.value);
}
static inline uint64_t mako_hash_TokenChoice(TokenChoice k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= mako_hash_bytes(k.value.data, k.value.len); h *= 1099511628211ULL;
    return h;
}
typedef struct MakoArr_TokenChoice {
    TokenChoice *data;
    size_t len;
    size_t cap;
} MakoArr_TokenChoice;
static inline MakoArr_TokenChoice mako_arr_TokenChoice_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_TokenChoice a;
    a.data = (TokenChoice *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(TokenChoice));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_TokenChoice_free(MakoArr_TokenChoice a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
        for (size_t i = 0; i < a.len; i++) {
            mako_str_free(a.data[i].value);
        }
    }
    mako_rc_release(a.data);
}
static inline MakoArr_TokenChoice mako_arr_TokenChoice_clone(MakoArr_TokenChoice a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_TokenChoice_len(MakoArr_TokenChoice a) { return (int64_t)a.len; }
static inline int64_t mako_arr_TokenChoice_cap(MakoArr_TokenChoice a) { return (int64_t)a.cap; }
static inline TokenChoice mako_arr_TokenChoice_get(MakoArr_TokenChoice a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline TokenChoice* mako_arr_TokenChoice_get_ptr(MakoArr_TokenChoice a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return &a.data[i];
}
static inline void mako_arr_TokenChoice_set(MakoArr_TokenChoice a, int64_t i, TokenChoice v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    TokenChoice old = a.data[i];
    a.data[i] = v;
    if (old.value.data != v.value.data) { mako_str_free(old.value); }
}
static inline MakoArr_TokenChoice mako_arr_TokenChoice_append(MakoArr_TokenChoice s, TokenChoice v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        TokenChoice *nd = (TokenChoice *)mako_rc_alloc(ncap * sizeof(TokenChoice));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(TokenChoice));
        for (size_t i = 0; i < s.len; i++) {
            nd[i].value = mako_str_clone(nd[i].value);
        }
        if (!mako_rc_shared(s.data)) {
            for (size_t i = 0; i < s.len; i++) {
                mako_str_free(s.data[i].value);
            }
        }
        mako_rc_release(s.data);
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len] = v;
    s.len++;
    return s;
}
static inline MakoArr_TokenChoice mako_arr_TokenChoice_arena_append(MakoArena *arena, MakoArr_TokenChoice s, TokenChoice v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        TokenChoice *nd = (TokenChoice *)mako_arena_alloc(arena, ncap * sizeof(TokenChoice));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(TokenChoice));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_TokenChoice mako_arr_TokenChoice_of(const TokenChoice *vals, size_t n) {
    MakoArr_TokenChoice a = mako_arr_TokenChoice_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(TokenChoice));
    return a;
}
static inline MakoArr_TokenChoice mako_arr_TokenChoice_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_TokenChoice a;
    a.data = (TokenChoice *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(TokenChoice));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(TokenChoice));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_TokenChoice {
    MakoArr_TokenChoice *data;
    size_t len;
    size_t cap;
} MakoArr_arr_TokenChoice;
static inline MakoArr_arr_TokenChoice mako_arr_arr_TokenChoice_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_TokenChoice a;
    a.data = (MakoArr_TokenChoice *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_TokenChoice));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_TokenChoice_free(MakoArr_arr_TokenChoice a) {
    if (!(a.cap > 0 && a.data)) return;
    if (mako_rc_release_last(a.data)) {
        free((char *)a.data - MAKO_RC_HEADER);
    }
}
static inline MakoArr_arr_TokenChoice mako_arr_arr_TokenChoice_clone(MakoArr_arr_TokenChoice a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_arr_TokenChoice_len(MakoArr_arr_TokenChoice a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_TokenChoice_cap(MakoArr_arr_TokenChoice a) { return (int64_t)a.cap; }
static inline MakoArr_TokenChoice mako_arr_arr_TokenChoice_get(MakoArr_arr_TokenChoice a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_TokenChoice_set(MakoArr_arr_TokenChoice a, int64_t i, MakoArr_TokenChoice v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    MakoArr_TokenChoice old = a.data[i];
    a.data[i] = v;
    if (old.data != v.data) { mako_arr_TokenChoice_free(old); }
    if (old.data == v.data && old.cap > 0 && v.cap > 0 && old.data && mako_rc_shared(old.data)) mako_rc_release(old.data);
}
static inline MakoArr_arr_TokenChoice mako_arr_arr_TokenChoice_append(MakoArr_arr_TokenChoice s, MakoArr_TokenChoice v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_TokenChoice *nd = (MakoArr_TokenChoice *)mako_rc_alloc(ncap * sizeof(MakoArr_TokenChoice));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_TokenChoice));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_TokenChoice mako_arr_arr_TokenChoice_of(const MakoArr_TokenChoice *vals, size_t n) {
    MakoArr_arr_TokenChoice a = mako_arr_arr_TokenChoice_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_TokenChoice));
    return a;
}
static inline MakoArr_arr_TokenChoice mako_arr_arr_TokenChoice_slice_expr(MakoArr_arr_TokenChoice s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_TokenChoice out;
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

typedef struct QueryState {
    MakoIntArray values;
    MakoStrArray labels;
} QueryState;
static inline bool mako_eq_QueryState(QueryState a, QueryState b) {
    return ((a.values.data == b.values.data) && (a.values.len == b.values.len)) && ((a.labels.data == b.labels.data) && (a.labels.len == b.labels.len));
}
static inline uint64_t mako_hash_QueryState(QueryState k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= (uint64_t)(uintptr_t)k.values.data; h *= 1099511628211ULL; h ^= (uint64_t)k.values.len; h *= 1099511628211ULL;
    h ^= (uint64_t)(uintptr_t)k.labels.data; h *= 1099511628211ULL; h ^= (uint64_t)k.labels.len; h *= 1099511628211ULL;
    return h;
}
typedef struct MakoArr_QueryState {
    QueryState *data;
    size_t len;
    size_t cap;
} MakoArr_QueryState;
static inline MakoArr_QueryState mako_arr_QueryState_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_QueryState a;
    a.data = (QueryState *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(QueryState));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_QueryState_free(MakoArr_QueryState a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
        for (size_t i = 0; i < a.len; i++) {
            mako_int_array_free(a.data[i].values);
            mako_str_array_free(a.data[i].labels);
        }
    }
    mako_rc_release(a.data);
}
static inline MakoArr_QueryState mako_arr_QueryState_clone(MakoArr_QueryState a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_QueryState_len(MakoArr_QueryState a) { return (int64_t)a.len; }
static inline int64_t mako_arr_QueryState_cap(MakoArr_QueryState a) { return (int64_t)a.cap; }
static inline QueryState mako_arr_QueryState_get(MakoArr_QueryState a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline QueryState* mako_arr_QueryState_get_ptr(MakoArr_QueryState a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return &a.data[i];
}
static inline void mako_arr_QueryState_set(MakoArr_QueryState a, int64_t i, QueryState v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    QueryState old = a.data[i];
    a.data[i] = v;
    if (old.values.data != v.values.data) { mako_int_array_free(old.values); }
    if (old.values.data == v.values.data && old.values.cap > 0 && v.values.cap > 0 && old.values.data && mako_rc_shared(old.values.data)) mako_rc_release(old.values.data);
    if (old.labels.data != v.labels.data) { mako_str_array_free(old.labels); }
    if (old.labels.data == v.labels.data && old.labels.cap > 0 && v.labels.cap > 0 && old.labels.data && mako_rc_shared(old.labels.data)) mako_rc_release(old.labels.data);
}
static inline MakoArr_QueryState mako_arr_QueryState_append(MakoArr_QueryState s, QueryState v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        QueryState *nd = (QueryState *)mako_rc_alloc(ncap * sizeof(QueryState));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(QueryState));
        for (size_t i = 0; i < s.len; i++) {
            nd[i].values = mako_int_array_clone(nd[i].values);
            nd[i].labels = mako_str_array_clone(nd[i].labels);
        }
        if (!mako_rc_shared(s.data)) {
            for (size_t i = 0; i < s.len; i++) {
                mako_int_array_free(s.data[i].values);
                mako_str_array_free(s.data[i].labels);
            }
        }
        mako_rc_release(s.data);
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len] = v;
    s.len++;
    return s;
}
static inline MakoArr_QueryState mako_arr_QueryState_arena_append(MakoArena *arena, MakoArr_QueryState s, QueryState v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        QueryState *nd = (QueryState *)mako_arena_alloc(arena, ncap * sizeof(QueryState));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(QueryState));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_QueryState mako_arr_QueryState_of(const QueryState *vals, size_t n) {
    MakoArr_QueryState a = mako_arr_QueryState_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(QueryState));
    return a;
}
static inline MakoArr_QueryState mako_arr_QueryState_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_QueryState a;
    a.data = (QueryState *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(QueryState));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(QueryState));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_QueryState {
    MakoArr_QueryState *data;
    size_t len;
    size_t cap;
} MakoArr_arr_QueryState;
static inline MakoArr_arr_QueryState mako_arr_arr_QueryState_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_QueryState a;
    a.data = (MakoArr_QueryState *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_QueryState));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_QueryState_free(MakoArr_arr_QueryState a) {
    if (!(a.cap > 0 && a.data)) return;
    if (mako_rc_release_last(a.data)) {
        free((char *)a.data - MAKO_RC_HEADER);
    }
}
static inline MakoArr_arr_QueryState mako_arr_arr_QueryState_clone(MakoArr_arr_QueryState a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_arr_QueryState_len(MakoArr_arr_QueryState a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_QueryState_cap(MakoArr_arr_QueryState a) { return (int64_t)a.cap; }
static inline MakoArr_QueryState mako_arr_arr_QueryState_get(MakoArr_arr_QueryState a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_QueryState_set(MakoArr_arr_QueryState a, int64_t i, MakoArr_QueryState v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    MakoArr_QueryState old = a.data[i];
    a.data[i] = v;
    if (old.data != v.data) { mako_arr_QueryState_free(old); }
    if (old.data == v.data && old.cap > 0 && v.cap > 0 && old.data && mako_rc_shared(old.data)) mako_rc_release(old.data);
}
static inline MakoArr_arr_QueryState mako_arr_arr_QueryState_append(MakoArr_arr_QueryState s, MakoArr_QueryState v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_QueryState *nd = (MakoArr_QueryState *)mako_rc_alloc(ncap * sizeof(MakoArr_QueryState));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_QueryState));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_QueryState mako_arr_arr_QueryState_of(const MakoArr_QueryState *vals, size_t n) {
    MakoArr_arr_QueryState a = mako_arr_arr_QueryState_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_QueryState));
    return a;
}
static inline MakoArr_arr_QueryState mako_arr_arr_QueryState_slice_expr(MakoArr_arr_QueryState s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_QueryState out;
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
static inline QueryState mako_map_value_QueryState_clone(QueryState value) {
QueryState __mako_cloned_0 = value;
MakoIntArray __mako_cloned_1 = mako_int_array_clone(__mako_cloned_0.values);
__mako_cloned_0.values = __mako_cloned_1;
MakoStrArray __mako_cloned_2 = mako_str_array_clone(__mako_cloned_0.labels);
__mako_cloned_0.labels = __mako_cloned_2;
    return __mako_cloned_0;
}
static inline void mako_map_value_QueryState_drop(QueryState value) {
    (void)value;
    mako_int_array_free(value.values);
    mako_str_array_free(value.labels);
}
typedef struct {
    uint8_t *state;
    int64_t *keys;
    QueryState *vals;
    size_t cap;
    size_t len;
} MakoMapI_QueryState;
static inline MakoMapI_QueryState mako_map_i_QueryState_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapI_QueryState m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (int64_t *)malloc(cap * sizeof(int64_t));
    m.vals = (QueryState *)calloc(cap, sizeof(QueryState));
    if (!m.keys || !m.vals) { fprintf(stderr, "mako: OOM in map_i_QueryState_new\n"); abort(); }
    m.cap = cap; m.len = 0; return m;
}
static inline void mako_map_i_QueryState_rehash(MakoMapI_QueryState *m, size_t ncap);
static inline void mako_map_i_QueryState_set(MakoMapI_QueryState *m, int64_t key, QueryState val) {
    if ((m->len + 1) * 4 >= m->cap * 3) mako_map_i_QueryState_rehash(m, m->cap * 2);
    uint64_t h = mako_hash_i64(key);
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
            m->len++; return;
        }
        if (st == MAKO_MAP_TOMB) { if (first_tomb == (size_t)-1) first_tomb = i; }
        else if (m->keys[i] == key) {
            QueryState old = m->vals[i];
            m->vals[i] = val;
            if (old.values.data != val.values.data) { mako_int_array_free(old.values); }
            if (old.values.data == val.values.data && old.values.cap > 0 && val.values.cap > 0 && old.values.data && mako_rc_shared(old.values.data)) mako_rc_release(old.values.data);
            if (old.labels.data != val.labels.data) { mako_str_array_free(old.labels); }
            if (old.labels.data == val.labels.data && old.labels.cap > 0 && val.labels.cap > 0 && old.labels.data && mako_rc_shared(old.labels.data)) mako_rc_release(old.labels.data);
            return;
        }
        i = (i + 1) & mask;
    }
}
static inline void mako_map_i_QueryState_rehash(MakoMapI_QueryState *m, size_t ncap) {
    uint8_t *ostate = m->state;
    int64_t *okeys = m->keys;
    QueryState *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapI_QueryState n = mako_map_i_QueryState_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        int64_t key = okeys[i];
        QueryState val = ovals[i];
        uint64_t h = mako_hash_i64(key);
        size_t j = (size_t)(h & (n.cap - 1));
        while (n.state[j] == MAKO_MAP_FULL) j = (j + 1) & (n.cap - 1);
        n.state[j] = MAKO_MAP_FULL;
        n.keys[j] = key; n.vals[j] = val; n.len++;
    }
    free(ostate); free(okeys); free(ovals); *m = n;
}
static inline QueryState mako_map_i_QueryState_get(MakoMapI_QueryState *m, int64_t key) {
    QueryState z; memset(&z, 0, sizeof(z));
    if (!m) return z;
    uint64_t h = mako_hash_i64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return z;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}
static inline bool mako_map_i_QueryState_has(MakoMapI_QueryState *m, int64_t key) {
    if (!m) return false;
    uint64_t h = mako_hash_i64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return true;
        i = (i + 1) & (m->cap - 1);
    }
}
static inline void mako_map_i_QueryState_delete(MakoMapI_QueryState *m, int64_t key) {
    if (!m) return;
    uint64_t h = mako_hash_i64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) {
            mako_map_value_QueryState_drop(m->vals[i]); m->state[i] = MAKO_MAP_TOMB; m->len--; return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}
static inline int64_t mako_map_i_QueryState_len(MakoMapI_QueryState *m) { return m ? (int64_t)m->len : 0; }
static inline MakoMapI_QueryState *mako_map_i_QueryState_make(int64_t hint) {
    MakoMapI_QueryState *m = (MakoMapI_QueryState *)malloc(sizeof(MakoMapI_QueryState));
    *m = mako_map_i_QueryState_new(hint > 0 ? (size_t)hint : 0); return m;
}
static inline void mako_map_i_QueryState_free(MakoMapI_QueryState *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) mako_map_value_QueryState_drop(m->vals[i]);
    }
    free(m->state); free(m->keys); free(m->vals); free(m);
}
typedef struct {
    uint8_t *state;
    MakoString *keys;
    QueryState *vals;
    size_t cap;
    size_t len;
} MakoMapS_QueryState;
static inline MakoMapS_QueryState mako_map_s_QueryState_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapS_QueryState m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (MakoString *)calloc(cap, sizeof(MakoString));
    m.vals = (QueryState *)calloc(cap, sizeof(QueryState));
    if (!m.keys || !m.vals) { fprintf(stderr, "mako: OOM in map_s_QueryState_new\n"); abort(); }
    m.cap = cap; m.len = 0; return m;
}
static inline void mako_map_s_QueryState_rehash(MakoMapS_QueryState *m, size_t ncap);
static inline void mako_map_s_QueryState_set_take(MakoMapS_QueryState *m, MakoString key, QueryState val) {
    if ((m->len + 1) * 4 >= m->cap * 3) mako_map_s_QueryState_rehash(m, m->cap * 2);
    uint64_t h = mako_hash_bytes(key.data, key.len);
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
            m->len++; return;
        }
        if (st == MAKO_MAP_TOMB) { if (first_tomb == (size_t)-1) first_tomb = i; }
        else if (m->keys[i].len == key.len && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            mako_str_free(key);
            QueryState old = m->vals[i];
            m->vals[i] = val;
            if (old.values.data != val.values.data) { mako_int_array_free(old.values); }
            if (old.values.data == val.values.data && old.values.cap > 0 && val.values.cap > 0 && old.values.data && mako_rc_shared(old.values.data)) mako_rc_release(old.values.data);
            if (old.labels.data != val.labels.data) { mako_str_array_free(old.labels); }
            if (old.labels.data == val.labels.data && old.labels.cap > 0 && val.labels.cap > 0 && old.labels.data && mako_rc_shared(old.labels.data)) mako_rc_release(old.labels.data);
            return;
        }
        i = (i + 1) & mask;
    }
}
static inline void mako_map_s_QueryState_set(MakoMapS_QueryState *m, MakoString key, QueryState val) {
    mako_map_s_QueryState_set_take(m, mako_str_clone(key), val);
}
static inline void mako_map_s_QueryState_rehash(MakoMapS_QueryState *m, size_t ncap) {
    uint8_t *ostate = m->state;
    MakoString *okeys = m->keys;
    QueryState *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapS_QueryState n = mako_map_s_QueryState_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        MakoString key = okeys[i];
        QueryState val = ovals[i];
        uint64_t h = mako_hash_bytes(key.data, key.len);
        size_t j = (size_t)(h & (n.cap - 1));
        while (n.state[j] == MAKO_MAP_FULL) j = (j + 1) & (n.cap - 1);
        n.state[j] = MAKO_MAP_FULL;
        n.keys[j] = key; n.vals[j] = val; n.len++;
    }
    free(ostate); free(okeys); free(ovals); *m = n;
}
static inline QueryState mako_map_s_QueryState_get(MakoMapS_QueryState *m, MakoString key) {
    QueryState z; memset(&z, 0, sizeof(z));
    if (!m) return z;
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return z;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i].len == key.len && memcmp(m->keys[i].data, key.data, key.len) == 0) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}
static inline bool mako_map_s_QueryState_has(MakoMapS_QueryState *m, MakoString key) {
    if (!m) return false;
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i].len == key.len && memcmp(m->keys[i].data, key.data, key.len) == 0) return true;
        i = (i + 1) & (m->cap - 1);
    }
}
static inline void mako_map_s_QueryState_delete(MakoMapS_QueryState *m, MakoString key) {
    if (!m) return;
    uint64_t h = mako_hash_bytes(key.data, key.len);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i].len == key.len && memcmp(m->keys[i].data, key.data, key.len) == 0) {
            mako_str_free(m->keys[i]); m->keys[i].data = NULL; m->keys[i].len = 0;
            mako_map_value_QueryState_drop(m->vals[i]); m->state[i] = MAKO_MAP_TOMB; m->len--; return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}
static inline int64_t mako_map_s_QueryState_len(MakoMapS_QueryState *m) { return m ? (int64_t)m->len : 0; }
static inline MakoMapS_QueryState *mako_map_s_QueryState_make(int64_t hint) {
    MakoMapS_QueryState *m = (MakoMapS_QueryState *)malloc(sizeof(MakoMapS_QueryState));
    *m = mako_map_s_QueryState_new(hint > 0 ? (size_t)hint : 0); return m;
}
static inline void mako_map_s_QueryState_free(MakoMapS_QueryState *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) mako_map_value_QueryState_drop(m->vals[i]);
    }
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) { mako_str_free(m->keys[i]); m->keys[i].data = NULL; m->keys[i].len = 0; }
    }
    free(m->state); free(m->keys); free(m->vals); free(m);
}
static inline MakoIntArray mako_maps_keys_i_QueryState(MakoMapI_QueryState *m) {
    MakoIntArray out = mako_int_array_make(0, m ? (int64_t)m->len : 0);
    if (!m) return out;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] != MAKO_MAP_FULL) continue;
        if (out.len >= out.cap) {
            size_t ncap = out.cap ? out.cap * 2 : 8;
            out.data = (int64_t *)realloc(out.data, ncap * sizeof(int64_t)); out.cap = ncap;
        }
        out.data[out.len++] = m->keys[i];
    } return out;
}
static inline MakoArr_QueryState mako_maps_values_i_QueryState(MakoMapI_QueryState *m) {
    MakoArr_QueryState out = mako_arr_QueryState_make(0, m ? (int64_t)m->len : 0);
    if (!m) return out;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL)
            out = mako_arr_QueryState_append(out, mako_map_value_QueryState_clone(m->vals[i]));
    } return out;
}
static inline void mako_maps_clear_i_QueryState(MakoMapI_QueryState *m) {
    if (!m) return; for (size_t i = 0; i < m->cap; i++) if (m->state[i] == MAKO_MAP_FULL) mako_map_value_QueryState_drop(m->vals[i]); memset(m->state, 0, m->cap); m->len = 0;
}
static inline MakoMapI_QueryState *mako_maps_clone_i_QueryState(MakoMapI_QueryState *m) {
    MakoMapI_QueryState *n = mako_map_i_QueryState_make(m ? (int64_t)m->len : 0);
    if (!m) return n;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL)
            mako_map_i_QueryState_set(n, m->keys[i], mako_map_value_QueryState_clone(m->vals[i]));
    } return n;
}
static inline int64_t mako_maps_equal_i_QueryState(MakoMapI_QueryState *a, MakoMapI_QueryState *b) {
    if (!a && !b) return 1; if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; i++) {
        if (a->state[i] != MAKO_MAP_FULL) continue;
        if (!mako_map_i_QueryState_has(b, a->keys[i])) return 0;
        QueryState av = a->vals[i], bv = mako_map_i_QueryState_get(b, a->keys[i]);
        if (!mako_eq_QueryState(av, bv)) return 0;
    } return 1;
}
static inline void mako_maps_copy_i_QueryState(MakoMapI_QueryState *dst, MakoMapI_QueryState *src) {
    if (!dst || !src) return;
    for (size_t i = 0; i < src->cap; i++) {
        if (src->state[i] == MAKO_MAP_FULL)
            mako_map_i_QueryState_set(dst, src->keys[i], mako_map_value_QueryState_clone(src->vals[i]));
    }
}
static inline MakoStrArray mako_maps_keys_s_QueryState(MakoMapS_QueryState *m) {
    MakoStrArray out = mako_str_array_make(0, m ? (int64_t)m->len : 0);
    if (!m) return out;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) out = mako_str_array_append(out, m->keys[i]);
    } return out;
}
static inline MakoArr_QueryState mako_maps_values_s_QueryState(MakoMapS_QueryState *m) {
    MakoArr_QueryState out = mako_arr_QueryState_make(0, m ? (int64_t)m->len : 0);
    if (!m) return out;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL)
            out = mako_arr_QueryState_append(out, mako_map_value_QueryState_clone(m->vals[i]));
    } return out;
}
static inline void mako_maps_clear_s_QueryState(MakoMapS_QueryState *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) {
            mako_map_value_QueryState_drop(m->vals[i]); mako_str_free(m->keys[i]); m->keys[i].data = NULL; m->keys[i].len = 0;
        } m->state[i] = MAKO_MAP_EMPTY;
    } m->len = 0;
}
static inline MakoMapS_QueryState *mako_maps_clone_s_QueryState(MakoMapS_QueryState *m) {
    MakoMapS_QueryState *n = mako_map_s_QueryState_make(m ? (int64_t)m->len : 0);
    if (!m) return n;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL)
            mako_map_s_QueryState_set(n, m->keys[i], mako_map_value_QueryState_clone(m->vals[i]));
    } return n;
}
static inline int64_t mako_maps_equal_s_QueryState(MakoMapS_QueryState *a, MakoMapS_QueryState *b) {
    if (!a && !b) return 1; if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; i++) {
        if (a->state[i] != MAKO_MAP_FULL) continue;
        if (!mako_map_s_QueryState_has(b, a->keys[i])) return 0;
        QueryState av = a->vals[i], bv = mako_map_s_QueryState_get(b, a->keys[i]);
        if (!mako_eq_QueryState(av, bv)) return 0;
    } return 1;
}
static inline void mako_maps_copy_s_QueryState(MakoMapS_QueryState *dst, MakoMapS_QueryState *src) {
    if (!dst || !src) return;
    for (size_t i = 0; i < src->cap; i++) {
        if (src->state[i] == MAKO_MAP_FULL)
            mako_map_s_QueryState_set(dst, src->keys[i], mako_map_value_QueryState_clone(src->vals[i]));
    }
}
typedef struct {
    uint8_t *state;
    double *keys;
    QueryState *vals;
    size_t cap;
    size_t len;
} MakoMapF_QueryState;
static inline MakoMapF_QueryState mako_map_f_QueryState_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapF_QueryState m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (double *)malloc(cap * sizeof(double));
    m.vals = (QueryState *)calloc(cap, sizeof(QueryState));
    if (!m.keys || !m.vals) { fprintf(stderr, "mako: OOM in map_f_QueryState_new\n"); abort(); }
    m.cap = cap; m.len = 0; return m;
}
static inline void mako_map_f_QueryState_rehash(MakoMapF_QueryState *m, size_t ncap);
static inline void mako_map_f_QueryState_set(MakoMapF_QueryState *m, double key, QueryState val) {
    key = mako_f64_key_norm(key);
    if ((m->len + 1) * 4 >= m->cap * 3) mako_map_f_QueryState_rehash(m, m->cap * 2);
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
            m->len++; return;
        }
        if (st == MAKO_MAP_TOMB) { if (first_tomb == (size_t)-1) first_tomb = i; }
        else if (mako_f64_key_eq(m->keys[i], key)) {
            QueryState old = m->vals[i];
            m->vals[i] = val;
            if (old.values.data != val.values.data) { mako_int_array_free(old.values); }
            if (old.values.data == val.values.data && old.values.cap > 0 && val.values.cap > 0 && old.values.data && mako_rc_shared(old.values.data)) mako_rc_release(old.values.data);
            if (old.labels.data != val.labels.data) { mako_str_array_free(old.labels); }
            if (old.labels.data == val.labels.data && old.labels.cap > 0 && val.labels.cap > 0 && old.labels.data && mako_rc_shared(old.labels.data)) mako_rc_release(old.labels.data);
            return;
        }
        i = (i + 1) & mask;
    }
}
static inline void mako_map_f_QueryState_rehash(MakoMapF_QueryState *m, size_t ncap) {
    uint8_t *ostate = m->state;
    double *okeys = m->keys;
    QueryState *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapF_QueryState n = mako_map_f_QueryState_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        double key = okeys[i];
        QueryState val = ovals[i];
        uint64_t h = mako_hash_f64(key);
        size_t j = (size_t)(h & (n.cap - 1));
        while (n.state[j] == MAKO_MAP_FULL) j = (j + 1) & (n.cap - 1);
        n.state[j] = MAKO_MAP_FULL;
        n.keys[j] = key; n.vals[j] = val; n.len++;
    }
    free(ostate); free(okeys); free(ovals); *m = n;
}
static inline QueryState mako_map_f_QueryState_get(MakoMapF_QueryState *m, double key) {
    QueryState z; memset(&z, 0, sizeof(z));
    if (!m) return z;
    key = mako_f64_key_norm(key);
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return z;
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key)) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}
static inline bool mako_map_f_QueryState_has(MakoMapF_QueryState *m, double key) {
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
static inline void mako_map_f_QueryState_delete(MakoMapF_QueryState *m, double key) {
    if (!m) return;
    key = mako_f64_key_norm(key);
    uint64_t h = mako_hash_f64(key);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && mako_f64_key_eq(m->keys[i], key)) {
            mako_map_value_QueryState_drop(m->vals[i]); m->state[i] = MAKO_MAP_TOMB; m->len--; return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}
static inline int64_t mako_map_f_QueryState_len(MakoMapF_QueryState *m) { return m ? (int64_t)m->len : 0; }
static inline MakoMapF_QueryState *mako_map_f_QueryState_make(int64_t hint) {
    MakoMapF_QueryState *m = (MakoMapF_QueryState *)malloc(sizeof(MakoMapF_QueryState));
    *m = mako_map_f_QueryState_new(hint > 0 ? (size_t)hint : 0); return m;
}
static inline void mako_map_f_QueryState_free(MakoMapF_QueryState *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) mako_map_value_QueryState_drop(m->vals[i]);
    }
    free(m->state); free(m->keys); free(m->vals); free(m);
}
static inline MakoFloatArray mako_maps_keys_f_QueryState(MakoMapF_QueryState *m) {
    MakoFloatArray out = mako_float_array_make(0, m ? (int64_t)m->len : 0);
    if (!m) return out;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) out = mako_float_array_append(out, m->keys[i]);
    } return out;
}
static inline MakoArr_QueryState mako_maps_values_f_QueryState(MakoMapF_QueryState *m) {
    MakoArr_QueryState out = mako_arr_QueryState_make(0, m ? (int64_t)m->len : 0);
    if (!m) return out;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL)
            out = mako_arr_QueryState_append(out, mako_map_value_QueryState_clone(m->vals[i]));
    } return out;
}
static inline void mako_maps_clear_f_QueryState(MakoMapF_QueryState *m) {
    if (!m) return; for (size_t i = 0; i < m->cap; i++) if (m->state[i] == MAKO_MAP_FULL) mako_map_value_QueryState_drop(m->vals[i]); memset(m->state, 0, m->cap); m->len = 0;
}
static inline MakoMapF_QueryState *mako_maps_clone_f_QueryState(MakoMapF_QueryState *m) {
    MakoMapF_QueryState *n = mako_map_f_QueryState_make(m ? (int64_t)m->len : 0);
    if (!m) return n;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL)
            mako_map_f_QueryState_set(n, m->keys[i], mako_map_value_QueryState_clone(m->vals[i]));
    } return n;
}
static inline int64_t mako_maps_equal_f_QueryState(MakoMapF_QueryState *a, MakoMapF_QueryState *b) {
    if (!a && !b) return 1; if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; i++) {
        if (a->state[i] != MAKO_MAP_FULL) continue;
        if (!mako_map_f_QueryState_has(b, a->keys[i])) return 0;
        QueryState av = a->vals[i], bv = mako_map_f_QueryState_get(b, a->keys[i]);
        if (!mako_eq_QueryState(av, bv)) return 0;
    } return 1;
}
static inline void mako_maps_copy_f_QueryState(MakoMapF_QueryState *dst, MakoMapF_QueryState *src) {
    if (!dst || !src) return;
    for (size_t i = 0; i < src->cap; i++) {
        if (src->state[i] == MAKO_MAP_FULL)
            mako_map_f_QueryState_set(dst, src->keys[i], mako_map_value_QueryState_clone(src->vals[i]));
    }
}
typedef struct {
    uint8_t *state;
    bool *keys;
    QueryState *vals;
    size_t cap;
    size_t len;
} MakoMapB_QueryState;
static inline MakoMapB_QueryState mako_map_b_QueryState_new(size_t hint) {
    size_t cap = 8;
    size_t need = hint ? (hint * 4 / 3 + 1) : 8;
    while (cap < need) cap *= 2;
    MakoMapB_QueryState m;
    m.state = (uint8_t *)calloc(cap, 1);
    m.keys = (bool *)malloc(cap * sizeof(bool));
    m.vals = (QueryState *)calloc(cap, sizeof(QueryState));
    if (!m.keys || !m.vals) { fprintf(stderr, "mako: OOM in map_b_QueryState_new\n"); abort(); }
    m.cap = cap; m.len = 0; return m;
}
static inline void mako_map_b_QueryState_rehash(MakoMapB_QueryState *m, size_t ncap);
static inline void mako_map_b_QueryState_set(MakoMapB_QueryState *m, bool key, QueryState val) {
    if ((m->len + 1) * 4 >= m->cap * 3) mako_map_b_QueryState_rehash(m, m->cap * 2);
    uint64_t h = mako_hash_i64(key ? 1 : 0);
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
            m->len++; return;
        }
        if (st == MAKO_MAP_TOMB) { if (first_tomb == (size_t)-1) first_tomb = i; }
        else if (m->keys[i] == key) {
            QueryState old = m->vals[i];
            m->vals[i] = val;
            if (old.values.data != val.values.data) { mako_int_array_free(old.values); }
            if (old.values.data == val.values.data && old.values.cap > 0 && val.values.cap > 0 && old.values.data && mako_rc_shared(old.values.data)) mako_rc_release(old.values.data);
            if (old.labels.data != val.labels.data) { mako_str_array_free(old.labels); }
            if (old.labels.data == val.labels.data && old.labels.cap > 0 && val.labels.cap > 0 && old.labels.data && mako_rc_shared(old.labels.data)) mako_rc_release(old.labels.data);
            return;
        }
        i = (i + 1) & mask;
    }
}
static inline void mako_map_b_QueryState_rehash(MakoMapB_QueryState *m, size_t ncap) {
    uint8_t *ostate = m->state;
    bool *okeys = m->keys;
    QueryState *ovals = m->vals;
    size_t ocap = m->cap;
    MakoMapB_QueryState n = mako_map_b_QueryState_new(ncap / 2);
    for (size_t i = 0; i < ocap; i++) {
        if (ostate[i] != MAKO_MAP_FULL) continue;
        bool key = okeys[i];
        QueryState val = ovals[i];
        uint64_t h = mako_hash_i64(key ? 1 : 0);
        size_t j = (size_t)(h & (n.cap - 1));
        while (n.state[j] == MAKO_MAP_FULL) j = (j + 1) & (n.cap - 1);
        n.state[j] = MAKO_MAP_FULL;
        n.keys[j] = key; n.vals[j] = val; n.len++;
    }
    free(ostate); free(okeys); free(ovals); *m = n;
}
static inline QueryState mako_map_b_QueryState_get(MakoMapB_QueryState *m, bool key) {
    QueryState z; memset(&z, 0, sizeof(z)); if (!m) return z;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return z;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
}
static inline bool mako_map_b_QueryState_has(MakoMapB_QueryState *m, bool key) {
    if (!m) return false;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return false;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) return true;
        i = (i + 1) & (m->cap - 1);
    }
}
static inline void mako_map_b_QueryState_delete(MakoMapB_QueryState *m, bool key) {
    if (!m) return;
    uint64_t h = mako_hash_i64(key ? 1 : 0);
    size_t i = (size_t)(h & (m->cap - 1));
    for (;;) {
        if (m->state[i] == MAKO_MAP_EMPTY) return;
        if (m->state[i] == MAKO_MAP_FULL && m->keys[i] == key) {
            mako_map_value_QueryState_drop(m->vals[i]); m->state[i] = MAKO_MAP_TOMB; m->len--; return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}
static inline int64_t mako_map_b_QueryState_len(MakoMapB_QueryState *m) { return m ? (int64_t)m->len : 0; }
static inline MakoMapB_QueryState *mako_map_b_QueryState_make(int64_t hint) {
    MakoMapB_QueryState *m = (MakoMapB_QueryState *)malloc(sizeof(MakoMapB_QueryState));
    *m = mako_map_b_QueryState_new(hint > 0 ? (size_t)hint : 0); return m;
}
static inline void mako_map_b_QueryState_free(MakoMapB_QueryState *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) mako_map_value_QueryState_drop(m->vals[i]);
    }
    free(m->state); free(m->keys); free(m->vals); free(m);
}
static inline MakoBoolArray mako_maps_keys_b_QueryState(MakoMapB_QueryState *m) {
    MakoBoolArray out = mako_bool_array_make(0, m ? (int64_t)m->len : 0);
    if (!m) return out;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) out = mako_bool_array_append(out, m->keys[i]);
    } return out;
}
static inline MakoArr_QueryState mako_maps_values_b_QueryState(MakoMapB_QueryState *m) {
    MakoArr_QueryState out = mako_arr_QueryState_make(0, m ? (int64_t)m->len : 0);
    if (!m) return out;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) out = mako_arr_QueryState_append(out, mako_map_value_QueryState_clone(m->vals[i]));
    } return out;
}
static inline void mako_maps_clear_b_QueryState(MakoMapB_QueryState *m) {
    if (!m) return; for (size_t i = 0; i < m->cap; i++) if (m->state[i] == MAKO_MAP_FULL) mako_map_value_QueryState_drop(m->vals[i]); memset(m->state, 0, m->cap); m->len = 0;
}
static inline MakoMapB_QueryState *mako_maps_clone_b_QueryState(MakoMapB_QueryState *m) {
    MakoMapB_QueryState *n = mako_map_b_QueryState_make(m ? (int64_t)m->len : 0);
    if (!m) return n;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL)
            mako_map_b_QueryState_set(n, m->keys[i], mako_map_value_QueryState_clone(m->vals[i]));
    } return n;
}
static inline int64_t mako_maps_equal_b_QueryState(MakoMapB_QueryState *a, MakoMapB_QueryState *b) {
    if (!a && !b) return 1; if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; i++) {
        if (a->state[i] != MAKO_MAP_FULL) continue;
        if (!mako_map_b_QueryState_has(b, a->keys[i])) return 0;
        QueryState av = a->vals[i], bv = mako_map_b_QueryState_get(b, a->keys[i]);
        if (!mako_eq_QueryState(av, bv)) return 0;
    } return 1;
}
static inline void mako_maps_copy_b_QueryState(MakoMapB_QueryState *dst, MakoMapB_QueryState *src) {
    if (!dst || !src) return;
    for (size_t i = 0; i < src->cap; i++) {
        if (src->state[i] == MAKO_MAP_FULL)
            mako_map_b_QueryState_set(dst, src->keys[i], mako_map_value_QueryState_clone(src->vals[i]));
    }
}

typedef struct NestedQueryState {
    QueryState state;
} NestedQueryState;
static inline bool mako_eq_NestedQueryState(NestedQueryState a, NestedQueryState b) {
    return mako_eq_QueryState(a.state, b.state);
}
static inline uint64_t mako_hash_NestedQueryState(NestedQueryState k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= mako_hash_QueryState(k.state); h *= 1099511628211ULL;
    return h;
}
typedef struct MakoArr_NestedQueryState {
    NestedQueryState *data;
    size_t len;
    size_t cap;
} MakoArr_NestedQueryState;
static inline MakoArr_NestedQueryState mako_arr_NestedQueryState_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_NestedQueryState a;
    a.data = (NestedQueryState *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(NestedQueryState));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_NestedQueryState_free(MakoArr_NestedQueryState a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
        for (size_t i = 0; i < a.len; i++) {
            mako_int_array_free(a.data[i].state.values);
            mako_str_array_free(a.data[i].state.labels);
        }
    }
    mako_rc_release(a.data);
}
static inline MakoArr_NestedQueryState mako_arr_NestedQueryState_clone(MakoArr_NestedQueryState a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_NestedQueryState_len(MakoArr_NestedQueryState a) { return (int64_t)a.len; }
static inline int64_t mako_arr_NestedQueryState_cap(MakoArr_NestedQueryState a) { return (int64_t)a.cap; }
static inline NestedQueryState mako_arr_NestedQueryState_get(MakoArr_NestedQueryState a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline NestedQueryState* mako_arr_NestedQueryState_get_ptr(MakoArr_NestedQueryState a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return &a.data[i];
}
static inline void mako_arr_NestedQueryState_set(MakoArr_NestedQueryState a, int64_t i, NestedQueryState v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    NestedQueryState old = a.data[i];
    a.data[i] = v;
    if (old.state.values.data != v.state.values.data) { mako_int_array_free(old.state.values); }
    if (old.state.values.data == v.state.values.data && old.state.values.cap > 0 && v.state.values.cap > 0 && old.state.values.data && mako_rc_shared(old.state.values.data)) mako_rc_release(old.state.values.data);
    if (old.state.labels.data != v.state.labels.data) { mako_str_array_free(old.state.labels); }
    if (old.state.labels.data == v.state.labels.data && old.state.labels.cap > 0 && v.state.labels.cap > 0 && old.state.labels.data && mako_rc_shared(old.state.labels.data)) mako_rc_release(old.state.labels.data);
}
static inline MakoArr_NestedQueryState mako_arr_NestedQueryState_append(MakoArr_NestedQueryState s, NestedQueryState v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        NestedQueryState *nd = (NestedQueryState *)mako_rc_alloc(ncap * sizeof(NestedQueryState));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(NestedQueryState));
        for (size_t i = 0; i < s.len; i++) {
            nd[i].state.values = mako_int_array_clone(nd[i].state.values);
            nd[i].state.labels = mako_str_array_clone(nd[i].state.labels);
        }
        if (!mako_rc_shared(s.data)) {
            for (size_t i = 0; i < s.len; i++) {
                mako_int_array_free(s.data[i].state.values);
                mako_str_array_free(s.data[i].state.labels);
            }
        }
        mako_rc_release(s.data);
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len] = v;
    s.len++;
    return s;
}
static inline MakoArr_NestedQueryState mako_arr_NestedQueryState_arena_append(MakoArena *arena, MakoArr_NestedQueryState s, NestedQueryState v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        NestedQueryState *nd = (NestedQueryState *)mako_arena_alloc(arena, ncap * sizeof(NestedQueryState));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(NestedQueryState));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_NestedQueryState mako_arr_NestedQueryState_of(const NestedQueryState *vals, size_t n) {
    MakoArr_NestedQueryState a = mako_arr_NestedQueryState_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(NestedQueryState));
    return a;
}
static inline MakoArr_NestedQueryState mako_arr_NestedQueryState_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_NestedQueryState a;
    a.data = (NestedQueryState *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(NestedQueryState));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(NestedQueryState));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_NestedQueryState {
    MakoArr_NestedQueryState *data;
    size_t len;
    size_t cap;
} MakoArr_arr_NestedQueryState;
static inline MakoArr_arr_NestedQueryState mako_arr_arr_NestedQueryState_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_NestedQueryState a;
    a.data = (MakoArr_NestedQueryState *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_NestedQueryState));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_NestedQueryState_free(MakoArr_arr_NestedQueryState a) {
    if (!(a.cap > 0 && a.data)) return;
    if (mako_rc_release_last(a.data)) {
        free((char *)a.data - MAKO_RC_HEADER);
    }
}
static inline MakoArr_arr_NestedQueryState mako_arr_arr_NestedQueryState_clone(MakoArr_arr_NestedQueryState a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_arr_NestedQueryState_len(MakoArr_arr_NestedQueryState a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_NestedQueryState_cap(MakoArr_arr_NestedQueryState a) { return (int64_t)a.cap; }
static inline MakoArr_NestedQueryState mako_arr_arr_NestedQueryState_get(MakoArr_arr_NestedQueryState a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_NestedQueryState_set(MakoArr_arr_NestedQueryState a, int64_t i, MakoArr_NestedQueryState v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    MakoArr_NestedQueryState old = a.data[i];
    a.data[i] = v;
    if (old.data != v.data) { mako_arr_NestedQueryState_free(old); }
    if (old.data == v.data && old.cap > 0 && v.cap > 0 && old.data && mako_rc_shared(old.data)) mako_rc_release(old.data);
}
static inline MakoArr_arr_NestedQueryState mako_arr_arr_NestedQueryState_append(MakoArr_arr_NestedQueryState s, MakoArr_NestedQueryState v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_NestedQueryState *nd = (MakoArr_NestedQueryState *)mako_rc_alloc(ncap * sizeof(MakoArr_NestedQueryState));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_NestedQueryState));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_NestedQueryState mako_arr_arr_NestedQueryState_of(const MakoArr_NestedQueryState *vals, size_t n) {
    MakoArr_arr_NestedQueryState a = mako_arr_arr_NestedQueryState_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_NestedQueryState));
    return a;
}
static inline MakoArr_arr_NestedQueryState mako_arr_arr_NestedQueryState_slice_expr(MakoArr_arr_NestedQueryState s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_NestedQueryState out;
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

typedef struct QueryBatch {
    MakoArr_QueryState states;
} QueryBatch;
static inline bool mako_eq_QueryBatch(QueryBatch a, QueryBatch b) {
    return ((a.states.data == b.states.data) && (a.states.len == b.states.len));
}
static inline uint64_t mako_hash_QueryBatch(QueryBatch k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= (uint64_t)(uintptr_t)k.states.data; h *= 1099511628211ULL; h ^= (uint64_t)k.states.len; h *= 1099511628211ULL;
    return h;
}
typedef struct MakoArr_QueryBatch {
    QueryBatch *data;
    size_t len;
    size_t cap;
} MakoArr_QueryBatch;
static inline MakoArr_QueryBatch mako_arr_QueryBatch_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_QueryBatch a;
    a.data = (QueryBatch *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(QueryBatch));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_QueryBatch_free(MakoArr_QueryBatch a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
        for (size_t i = 0; i < a.len; i++) {
            mako_arr_QueryState_free(a.data[i].states);
        }
    }
    mako_rc_release(a.data);
}
static inline MakoArr_QueryBatch mako_arr_QueryBatch_clone(MakoArr_QueryBatch a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_QueryBatch_len(MakoArr_QueryBatch a) { return (int64_t)a.len; }
static inline int64_t mako_arr_QueryBatch_cap(MakoArr_QueryBatch a) { return (int64_t)a.cap; }
static inline QueryBatch mako_arr_QueryBatch_get(MakoArr_QueryBatch a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline QueryBatch* mako_arr_QueryBatch_get_ptr(MakoArr_QueryBatch a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return &a.data[i];
}
static inline void mako_arr_QueryBatch_set(MakoArr_QueryBatch a, int64_t i, QueryBatch v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    QueryBatch old = a.data[i];
    a.data[i] = v;
    if (old.states.data != v.states.data) { mako_arr_QueryState_free(old.states); }
    if (old.states.data == v.states.data && old.states.cap > 0 && v.states.cap > 0 && old.states.data && mako_rc_shared(old.states.data)) mako_rc_release(old.states.data);
}
static inline MakoArr_QueryBatch mako_arr_QueryBatch_append(MakoArr_QueryBatch s, QueryBatch v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        QueryBatch *nd = (QueryBatch *)mako_rc_alloc(ncap * sizeof(QueryBatch));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(QueryBatch));
        for (size_t i = 0; i < s.len; i++) {
            nd[i].states = mako_arr_QueryState_clone(nd[i].states);
        }
        if (!mako_rc_shared(s.data)) {
            for (size_t i = 0; i < s.len; i++) {
                mako_arr_QueryState_free(s.data[i].states);
            }
        }
        mako_rc_release(s.data);
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len] = v;
    s.len++;
    return s;
}
static inline MakoArr_QueryBatch mako_arr_QueryBatch_arena_append(MakoArena *arena, MakoArr_QueryBatch s, QueryBatch v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        QueryBatch *nd = (QueryBatch *)mako_arena_alloc(arena, ncap * sizeof(QueryBatch));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(QueryBatch));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_QueryBatch mako_arr_QueryBatch_of(const QueryBatch *vals, size_t n) {
    MakoArr_QueryBatch a = mako_arr_QueryBatch_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(QueryBatch));
    return a;
}
static inline MakoArr_QueryBatch mako_arr_QueryBatch_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_QueryBatch a;
    a.data = (QueryBatch *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(QueryBatch));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(QueryBatch));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_QueryBatch {
    MakoArr_QueryBatch *data;
    size_t len;
    size_t cap;
} MakoArr_arr_QueryBatch;
static inline MakoArr_arr_QueryBatch mako_arr_arr_QueryBatch_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_QueryBatch a;
    a.data = (MakoArr_QueryBatch *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_QueryBatch));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_QueryBatch_free(MakoArr_arr_QueryBatch a) {
    if (!(a.cap > 0 && a.data)) return;
    if (mako_rc_release_last(a.data)) {
        free((char *)a.data - MAKO_RC_HEADER);
    }
}
static inline MakoArr_arr_QueryBatch mako_arr_arr_QueryBatch_clone(MakoArr_arr_QueryBatch a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_arr_QueryBatch_len(MakoArr_arr_QueryBatch a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_QueryBatch_cap(MakoArr_arr_QueryBatch a) { return (int64_t)a.cap; }
static inline MakoArr_QueryBatch mako_arr_arr_QueryBatch_get(MakoArr_arr_QueryBatch a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_QueryBatch_set(MakoArr_arr_QueryBatch a, int64_t i, MakoArr_QueryBatch v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    MakoArr_QueryBatch old = a.data[i];
    a.data[i] = v;
    if (old.data != v.data) { mako_arr_QueryBatch_free(old); }
    if (old.data == v.data && old.cap > 0 && v.cap > 0 && old.data && mako_rc_shared(old.data)) mako_rc_release(old.data);
}
static inline MakoArr_arr_QueryBatch mako_arr_arr_QueryBatch_append(MakoArr_arr_QueryBatch s, MakoArr_QueryBatch v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_QueryBatch *nd = (MakoArr_QueryBatch *)mako_rc_alloc(ncap * sizeof(MakoArr_QueryBatch));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_QueryBatch));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_QueryBatch mako_arr_arr_QueryBatch_of(const MakoArr_QueryBatch *vals, size_t n) {
    MakoArr_arr_QueryBatch a = mako_arr_arr_QueryBatch_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_QueryBatch));
    return a;
}
static inline MakoArr_arr_QueryBatch mako_arr_arr_QueryBatch_slice_expr(MakoArr_arr_QueryBatch s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_QueryBatch out;
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

typedef struct SessionHandles {
    MakoChan* numbers;
    MakoChanStr* texts;
    MakoChanPtr* objects;
} SessionHandles;
static inline bool mako_eq_SessionHandles(SessionHandles a, SessionHandles b) {
    return (a.numbers == b.numbers) && (a.texts == b.texts) && (a.objects == b.objects);
}
static inline uint64_t mako_hash_SessionHandles(SessionHandles k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= (uint64_t)(uintptr_t)k.numbers; h *= 1099511628211ULL;
    h ^= (uint64_t)(uintptr_t)k.texts; h *= 1099511628211ULL;
    h ^= (uint64_t)(uintptr_t)k.objects; h *= 1099511628211ULL;
    return h;
}
typedef struct MakoArr_SessionHandles {
    SessionHandles *data;
    size_t len;
    size_t cap;
} MakoArr_SessionHandles;
static inline MakoArr_SessionHandles mako_arr_SessionHandles_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_SessionHandles a;
    a.data = (SessionHandles *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(SessionHandles));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_SessionHandles_free(MakoArr_SessionHandles a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
        for (size_t i = 0; i < a.len; i++) {
            mako_chan_free(a.data[i].numbers);
            mako_chan_str_free(a.data[i].texts);
            mako_chan_ptr_free(a.data[i].objects);
        }
    }
    mako_rc_release(a.data);
}
static inline MakoArr_SessionHandles mako_arr_SessionHandles_clone(MakoArr_SessionHandles a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_SessionHandles_len(MakoArr_SessionHandles a) { return (int64_t)a.len; }
static inline int64_t mako_arr_SessionHandles_cap(MakoArr_SessionHandles a) { return (int64_t)a.cap; }
static inline SessionHandles mako_arr_SessionHandles_get(MakoArr_SessionHandles a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline SessionHandles* mako_arr_SessionHandles_get_ptr(MakoArr_SessionHandles a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return &a.data[i];
}
static inline void mako_arr_SessionHandles_set(MakoArr_SessionHandles a, int64_t i, SessionHandles v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    SessionHandles old = a.data[i];
    a.data[i] = v;
    if (old.numbers != v.numbers) { mako_chan_free(old.numbers); }
    if (old.numbers == v.numbers && old.numbers) mako_chan_free(old.numbers);
    if (old.texts != v.texts) { mako_chan_str_free(old.texts); }
    if (old.texts == v.texts && old.texts) mako_chan_str_free(old.texts);
    if (old.objects != v.objects) { mako_chan_ptr_free(old.objects); }
    if (old.objects == v.objects && old.objects) mako_chan_ptr_free(old.objects);
}
static inline MakoArr_SessionHandles mako_arr_SessionHandles_append(MakoArr_SessionHandles s, SessionHandles v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        SessionHandles *nd = (SessionHandles *)mako_rc_alloc(ncap * sizeof(SessionHandles));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(SessionHandles));
        for (size_t i = 0; i < s.len; i++) {
            nd[i].numbers = mako_chan_clone(nd[i].numbers);
            nd[i].texts = mako_chan_str_clone(nd[i].texts);
            nd[i].objects = mako_chan_ptr_clone(nd[i].objects);
        }
        if (!mako_rc_shared(s.data)) {
            for (size_t i = 0; i < s.len; i++) {
                mako_chan_free(s.data[i].numbers);
                mako_chan_str_free(s.data[i].texts);
                mako_chan_ptr_free(s.data[i].objects);
            }
        }
        mako_rc_release(s.data);
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len] = v;
    s.len++;
    return s;
}
static inline MakoArr_SessionHandles mako_arr_SessionHandles_arena_append(MakoArena *arena, MakoArr_SessionHandles s, SessionHandles v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        SessionHandles *nd = (SessionHandles *)mako_arena_alloc(arena, ncap * sizeof(SessionHandles));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(SessionHandles));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_SessionHandles mako_arr_SessionHandles_of(const SessionHandles *vals, size_t n) {
    MakoArr_SessionHandles a = mako_arr_SessionHandles_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(SessionHandles));
    return a;
}
static inline MakoArr_SessionHandles mako_arr_SessionHandles_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_SessionHandles a;
    a.data = (SessionHandles *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(SessionHandles));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(SessionHandles));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_SessionHandles {
    MakoArr_SessionHandles *data;
    size_t len;
    size_t cap;
} MakoArr_arr_SessionHandles;
static inline MakoArr_arr_SessionHandles mako_arr_arr_SessionHandles_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_SessionHandles a;
    a.data = (MakoArr_SessionHandles *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_SessionHandles));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_SessionHandles_free(MakoArr_arr_SessionHandles a) {
    if (!(a.cap > 0 && a.data)) return;
    if (mako_rc_release_last(a.data)) {
        free((char *)a.data - MAKO_RC_HEADER);
    }
}
static inline MakoArr_arr_SessionHandles mako_arr_arr_SessionHandles_clone(MakoArr_arr_SessionHandles a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_arr_SessionHandles_len(MakoArr_arr_SessionHandles a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_SessionHandles_cap(MakoArr_arr_SessionHandles a) { return (int64_t)a.cap; }
static inline MakoArr_SessionHandles mako_arr_arr_SessionHandles_get(MakoArr_arr_SessionHandles a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_SessionHandles_set(MakoArr_arr_SessionHandles a, int64_t i, MakoArr_SessionHandles v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    MakoArr_SessionHandles old = a.data[i];
    a.data[i] = v;
    if (old.data != v.data) { mako_arr_SessionHandles_free(old); }
    if (old.data == v.data && old.cap > 0 && v.cap > 0 && old.data && mako_rc_shared(old.data)) mako_rc_release(old.data);
}
static inline MakoArr_arr_SessionHandles mako_arr_arr_SessionHandles_append(MakoArr_arr_SessionHandles s, MakoArr_SessionHandles v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_SessionHandles *nd = (MakoArr_SessionHandles *)mako_rc_alloc(ncap * sizeof(MakoArr_SessionHandles));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_SessionHandles));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_SessionHandles mako_arr_arr_SessionHandles_of(const MakoArr_SessionHandles *vals, size_t n) {
    MakoArr_arr_SessionHandles a = mako_arr_arr_SessionHandles_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_SessionHandles));
    return a;
}
static inline MakoArr_arr_SessionHandles mako_arr_arr_SessionHandles_slice_expr(MakoArr_arr_SessionHandles s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_SessionHandles out;
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

static void __attribute__((constructor)) __mako_reflect_reg_TokenChoice(void) {
    (void)mako_reflect_register_type("TokenChoice", "value:string");
}

static void __attribute__((constructor)) __mako_reflect_reg_QueryState(void) {
    (void)mako_reflect_register_type("QueryState", "values:[]int,labels:[]string");
}

static void __attribute__((constructor)) __mako_reflect_reg_NestedQueryState(void) {
    (void)mako_reflect_register_type("NestedQueryState", "state:QueryState");
}

static void __attribute__((constructor)) __mako_reflect_reg_QueryBatch(void) {
    (void)mako_reflect_register_type("QueryBatch", "states:[]QueryState");
}

static void __attribute__((constructor)) __mako_reflect_reg_SessionHandles(void) {
    (void)mako_reflect_register_type("SessionHandles", "numbers:chan<int>,texts:chan<string>,objects:chan<TokenChoice>");
}

#line 1 "examples/testing/byte_conversion_cleanup_test.mko"
int64_t scan_page(MakoString page, int64_t stop);
static inline MakoByteArray page_bytes(MakoString page);
static inline int64_t inspect_view(MakoString page);
static inline MakoString receive_token(MakoChanPtr* messages);
TokenChoice choose_token(MakoString text);
static inline int64_t borrowed_text_length(MakoString text);
MakoString response_text(MakoString text);
static inline QueryState retain_query_state(QueryState *state);
static inline MakoArr_QueryState query_state_values(MakoMapS_QueryState* records);
static inline MakoIntArray retain_values(MakoIntArray values);
QueryState replace_retained_field(QueryState *state);
void TestRetainedFieldCleanup(void);
static inline MakoMapS_QueryState* clone_query_states(MakoMapS_QueryState* states);
static inline MakoMapI_QueryState* clone_integer_states(MakoMapI_QueryState* states);
static inline MakoMapF_QueryState* clone_float_states(MakoMapF_QueryState* states);
static inline MakoMapB_QueryState* clone_boolean_states(MakoMapB_QueryState* states);
void TestContainerReplacementCleanup(void);
void TestQueryTemporaryCleanup(void);
void TestByteConversionCleanup(void);
void TestNestedFieldCleanup(void);
void TestStructArrayReplacementCleanup(void);
static inline SessionHandles retain_session_handles(SessionHandles *handles);
void TestChannelFieldReplacementCleanup(void);
void TestPageWriteTemporaryCleanup(void);
void TestSelfResliceCleanup(void);
static inline QueryState fresh_query_state(int64_t n);
static inline int64_t inspect_query_state(QueryState *state);
void TestBorrowedStructArgumentCleanup(void);
void TestProcessTemporaryCleanup(void);
void TestQueuedStructChannelCleanup(void);
void mako_main(void);

/*__MAKO_HELPERS__*/
static void __mako_channel_payload_drop_391(void *box) {
    QueryState *payload = (QueryState*)box;
    mako_int_array_free(((*payload)).values);
    mako_str_array_free(((*payload)).labels);
}
static void __mako_channel_payload_drop_315(void *box) {
    TokenChoice *payload = (TokenChoice*)box;
    mako_str_free(((*payload)).value);
}
static void __mako_channel_payload_drop_143(void *box) {
    TokenChoice *payload = (TokenChoice*)box;
    mako_str_free(((*payload)).value);
}

#line 1 "<mako-codegen>"
int64_t scan_page(MakoString page, int64_t stop) {
    mako_trace_enter("scan_page", "examples/testing/byte_conversion_cleanup_test.mko", 0);
#line 3 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoByteArray __mako_by_3 = mako_bytes_from_string(page);
    MakoByteArray raw = __mako_by_3;
#line 4 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t i = 0;
#line 5 "examples/testing/byte_conversion_cleanup_test.mko"
    while (1) {
        if (!((i < mako_byte_array_len(raw)))) break;
#line 6 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_idx_4 = i;
        if ((mako_byte_get(raw, __mako_idx_4) == stop)) {
            mako_byte_array_free(raw);
            mako_trace_exit("scan_page");
            return i;
        }
#line 7 "examples/testing/byte_conversion_cleanup_test.mko"
        i = mako_wrap_add_i64(i, 1);
    }
#line 9 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_retv_5 = mako_byte_array_len(raw);
    mako_byte_array_free(raw);
    mako_trace_exit("scan_page");
    return __mako_retv_5;
}

#line 1 "<mako-codegen>"
static inline MakoByteArray page_bytes(MakoString page) {
#line 13 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoByteArray __mako_by_6 = mako_bytes_from_string(page);
    MakoByteArray raw = __mako_by_6;
#line 14 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoByteArray __mako_view_7 = raw;
    MakoByteArray __mako_own_8 = __mako_view_7.cap > 0 ? __mako_view_7 : mako_byte_array_to_owned(__mako_view_7);
    mako_trace_exit("page_bytes");
    return __mako_own_8;
}

#line 1 "<mako-codegen>"
static inline int64_t inspect_view(MakoString page) {
#line 18 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoByteArray __mako_ab_9 = mako_as_bytes(page);
    MakoByteArray view = __mako_ab_9;
#line 19 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_retv_10 = mako_byte_array_len(view);
    mako_trace_exit("inspect_view");
    return __mako_retv_10;
}

#line 1 "<mako-codegen>"
static inline MakoString receive_token(MakoChanPtr* messages) {
#line 25 "examples/testing/byte_conversion_cleanup_test.mko"
    TokenChoice *__mako_pp_12 = (TokenChoice*)mako_chan_ptr_recv(messages);
    TokenChoice __mako_rv_11;
    if (__mako_pp_12) { __mako_rv_11 = *__mako_pp_12; mako_box_free(__mako_pp_12, sizeof(TokenChoice)); } else { memset(&__mako_rv_11, 0, sizeof(__mako_rv_11)); }
    TokenChoice message = __mako_rv_11;
#line 26 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoString __mako_cloned_13 = mako_str_clone(message.value);
    mako_str_free(message.value);
    mako_trace_exit("receive_token");
    return __mako_cloned_13;
}

#line 1 "<mako-codegen>"
TokenChoice choose_token(MakoString text) {
    mako_trace_enter("choose_token", "examples/testing/byte_conversion_cleanup_test.mko", 26);
#line 30 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoStrArray __mako_sf_14 = mako_str_fields(text);
    MakoStrArray fields = __mako_sf_14;
#line 31 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_idx_15 = 1;
    MakoString __mako_sg_16 = mako_str_array_get(fields, __mako_idx_15);
    MakoString __mako_cloned_17 = mako_str_clone(__mako_sg_16);
    MakoString chosen = __mako_cloned_17;
#line 32 "examples/testing/byte_conversion_cleanup_test.mko"
    TokenChoice __mako_st_18;
    memset(&__mako_st_18, 0, sizeof(__mako_st_18));
    MakoString __mako_cloned_19 = mako_str_clone(chosen);
    __mako_st_18.value = __mako_cloned_19;
    mako_str_free(chosen);
    mako_str_array_free(fields);
    mako_trace_exit("choose_token");
    return __mako_st_18;
}

#line 1 "<mako-codegen>"
static inline int64_t borrowed_text_length(MakoString text) {
#line 35 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_retv_20 = mako_str_len(text);
    mako_trace_exit("borrowed_text_length");
    return __mako_retv_20;
}

#line 1 "<mako-codegen>"
MakoString response_text(MakoString text) {
    mako_trace_enter("response_text", "examples/testing/byte_conversion_cleanup_test.mko", 35);
#line 38 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoStrBuilder *__mako_sb_21 = mako_str_builder_new();
    MakoStrBuilder* output = __mako_sb_21;
#line 39 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoString __mako_stu_22 = mako_str_to_upper(text);
    MakoString __mako_sa_23 = __mako_stu_22;
    mako_str_builder_write(output, __mako_sa_23);
#line 40 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoString __mako_srp_24 = mako_str_repeat(mako_str_view("!", 1), 3);
    MakoString __mako_sa_25 = __mako_srp_24;
    mako_str_builder_write_slice(output, __mako_sa_25, 0, 1);
#line 41 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoString __mako_s_26 = mako_str_builder_string(output);
    mako_str_free(__mako_sa_25);
    mako_str_free(__mako_sa_23);
    mako_str_builder_free(output);
    mako_trace_exit("response_text");
    return __mako_s_26;
}

#line 1 "<mako-codegen>"
static inline QueryState retain_query_state(QueryState *state) {
#line 46 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_cloned_27 = (*state);
    MakoIntArray __mako_cloned_28 = mako_int_array_clone(__mako_cloned_27.values);
    __mako_cloned_27.values = __mako_cloned_28;
    MakoStrArray __mako_cloned_29 = mako_str_array_clone(__mako_cloned_27.labels);
    __mako_cloned_27.labels = __mako_cloned_29;
    mako_trace_exit("retain_query_state");
    return __mako_cloned_27;
}

#line 1 "<mako-codegen>"
static inline MakoArr_QueryState query_state_values(MakoMapS_QueryState* records) {
#line 49 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoArr_QueryState __mako_mv_30 = mako_maps_values_s_QueryState(records);
    mako_trace_exit("query_state_values");
    return __mako_mv_30;
}

#line 1 "<mako-codegen>"
static inline MakoIntArray retain_values(MakoIntArray values) {
#line 52 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoIntArray __mako_cloned_31 = mako_int_array_clone(values);
    MakoIntArray __mako_view_32 = __mako_cloned_31;
    MakoIntArray __mako_own_33 = __mako_view_32.cap > 0 ? __mako_view_32 : mako_int_array_to_owned(__mako_view_32);
    mako_trace_exit("retain_values");
    return __mako_own_33;
}

#line 1 "<mako-codegen>"
QueryState replace_retained_field(QueryState *state) {
    mako_trace_enter("replace_retained_field", "examples/testing/byte_conversion_cleanup_test.mko", 52);
#line 55 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState* result = state;
#line 56 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoIntArray __mako_r_34 = retain_values(state->values);
    MakoIntArray retained = __mako_r_34;
#line 57 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoIntArray __mako_ident_move_35 = retained;
    memset(&retained, 0, sizeof(retained));
    MakoIntArray __mako_view_36 = __mako_ident_move_35;
    MakoIntArray __mako_own_37 = __mako_view_36.cap > 0 ? __mako_view_36 : mako_int_array_to_owned(__mako_view_36);
    MakoIntArray __mako_old_own_38 = result->values;
    result->values = __mako_own_37;
    if (__mako_old_own_38.data != result->values.data) mako_int_array_free(__mako_old_own_38);
if (__mako_old_own_38.data == result->values.data && __mako_old_own_38.cap > 0 && result->values.cap > 0 && __mako_old_own_38.data && mako_rc_shared(__mako_old_own_38.data)) mako_rc_release(__mako_old_own_38.data);
#line 58 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_cloned_39 = (*result);
    MakoIntArray __mako_cloned_40 = mako_int_array_clone(__mako_cloned_39.values);
    __mako_cloned_39.values = __mako_cloned_40;
    MakoStrArray __mako_cloned_41 = mako_str_array_clone(__mako_cloned_39.labels);
    __mako_cloned_39.labels = __mako_cloned_41;
    mako_trace_exit("replace_retained_field");
    return __mako_cloned_39;
}

#line 1 "<mako-codegen>"
void TestRetainedFieldCleanup(void) {
    mako_trace_enter("TestRetainedFieldCleanup", "examples/testing/byte_conversion_cleanup_test.mko", 58);
#line 62 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoArr_QueryState __mako_mk_42 = mako_arr_QueryState_make(1, 1);
    MakoArr_QueryState items = __mako_mk_42;
#line 63 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_st_43;
    memset(&__mako_st_43, 0, sizeof(__mako_st_43));
    MakoIntArray __mako_mk_44 = mako_int_array_make(0, 0);
    MakoIntArray __mako_view_45 = __mako_mk_44;
    MakoIntArray __mako_own_46 = __mako_view_45.cap > 0 ? __mako_view_45 : mako_int_array_to_owned(__mako_view_45);
    __mako_st_43.values = __mako_own_46;
    MakoStrArray __mako_mk_47 = mako_str_array_make(0, 0);
    __mako_st_43.labels = __mako_mk_47;
    int64_t __mako_iass_48 = 0;
    mako_arr_QueryState_set(items, __mako_iass_48, __mako_st_43);
#line 64 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t i = 0;
#line 65 "examples/testing/byte_conversion_cleanup_test.mko"
    while (1) {
        if (!((i < 1000))) break;
#line 66 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoIntArray __mako_mk_49 = mako_int_array_make(1000, 1000);
        MakoIntArray packed = __mako_mk_49;
#line 67 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoIntArray __mako_ident_move_50 = packed;
        memset(&packed, 0, sizeof(packed));
        MakoIntArray __mako_view_51 = __mako_ident_move_50;
        MakoIntArray __mako_own_52 = __mako_view_51.cap > 0 ? __mako_view_51 : mako_int_array_to_owned(__mako_view_51);
        int64_t __mako_ifield_53 = 0;
        MakoIntArray __mako_old_own_54 = mako_arr_QueryState_get_ptr(items, __mako_ifield_53)->values;
        mako_arr_QueryState_get_ptr(items, __mako_ifield_53)->values = __mako_own_52;
        if (__mako_old_own_54.data != mako_arr_QueryState_get_ptr(items, __mako_ifield_53)->values.data) mako_int_array_free(__mako_old_own_54);
if (__mako_old_own_54.data == mako_arr_QueryState_get_ptr(items, __mako_ifield_53)->values.data && __mako_old_own_54.cap > 0 && mako_arr_QueryState_get_ptr(items, __mako_ifield_53)->values.cap > 0 && __mako_old_own_54.data && mako_rc_shared(__mako_old_own_54.data)) mako_rc_release(__mako_old_own_54.data);
#line 68 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_iarg_55 = 0;
        QueryState __mako_r_56 = replace_retained_field(mako_arr_QueryState_get_ptr(items, __mako_iarg_55));
        int64_t __mako_iass_57 = 0;
        mako_arr_QueryState_set(items, __mako_iass_57, __mako_r_56);
#line 69 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_idx_58 = 0;
        QueryState __mako_sg_59 = mako_arr_QueryState_get(items, __mako_idx_58);
        MakoIntArray __mako_r_60 = retain_values(__mako_sg_59.values);
        MakoIntArray retained = __mako_r_60;
#line 70 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoIntArray __mako_cloned_61 = mako_int_array_clone(retained);
        MakoIntArray __mako_view_62 = __mako_cloned_61;
        MakoIntArray __mako_own_63 = __mako_view_62.cap > 0 ? __mako_view_62 : mako_int_array_to_owned(__mako_view_62);
        int64_t __mako_ifield_64 = 0;
        MakoIntArray __mako_old_own_65 = mako_arr_QueryState_get_ptr(items, __mako_ifield_64)->values;
        mako_arr_QueryState_get_ptr(items, __mako_ifield_64)->values = __mako_own_63;
        if (__mako_old_own_65.data != mako_arr_QueryState_get_ptr(items, __mako_ifield_64)->values.data) mako_int_array_free(__mako_old_own_65);
if (__mako_old_own_65.data == mako_arr_QueryState_get_ptr(items, __mako_ifield_64)->values.data && __mako_old_own_65.cap > 0 && mako_arr_QueryState_get_ptr(items, __mako_ifield_64)->values.cap > 0 && __mako_old_own_65.data && mako_rc_shared(__mako_old_own_65.data)) mako_rc_release(__mako_old_own_65.data);
#line 71 "examples/testing/byte_conversion_cleanup_test.mko"
        mako_assert_eq(mako_array_len(retained), 1000);
#line 72 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_idx_66 = 0;
        QueryState __mako_sg_67 = mako_arr_QueryState_get(items, __mako_idx_66);
        MakoIntArray __mako_ap_68 = mako_slice_append(__mako_sg_67.values, 7);
        MakoIntArray __mako_view_69 = __mako_ap_68;
        MakoIntArray __mako_own_70 = __mako_view_69.cap > 0 ? __mako_view_69 : mako_int_array_to_owned(__mako_view_69);
        int64_t __mako_ifield_71 = 0;
        MakoIntArray __mako_old_own_72 = mako_arr_QueryState_get_ptr(items, __mako_ifield_71)->values;
        mako_arr_QueryState_get_ptr(items, __mako_ifield_71)->values = __mako_own_70;
        if (__mako_old_own_72.data != mako_arr_QueryState_get_ptr(items, __mako_ifield_71)->values.data && __mako_old_own_72.cap > 0 && __mako_old_own_72.data) mako_rc_release(__mako_old_own_72.data);
#line 73 "examples/testing/byte_conversion_cleanup_test.mko"
        mako_assert_eq(mako_array_len(retained), 1000);
#line 74 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_idx_73 = 0;
        QueryState __mako_sg_74 = mako_arr_QueryState_get(items, __mako_idx_73);
        int64_t __mako_idx_75 = 1000;
        MAKO_BOUNDS_CHECK(__mako_idx_75 < 0 || (size_t)__mako_idx_75 >= __mako_sg_74.values.len, "index out of bounds (slices are 0..len-1)");
        mako_assert_eq(__mako_sg_74.values.data[__mako_idx_75], 7);
#line 75 "examples/testing/byte_conversion_cleanup_test.mko"
        i = mako_wrap_add_i64(i, 1);
        mako_int_array_free(retained);
    }
    mako_arr_QueryState_free(items);
    mako_trace_exit("TestRetainedFieldCleanup");
}

#line 1 "<mako-codegen>"
static inline MakoMapS_QueryState* clone_query_states(MakoMapS_QueryState* states) {
#line 79 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoMapS_QueryState *__mako_mc_76 = mako_maps_clone_s_QueryState(states);
    mako_trace_exit("clone_query_states");
    return __mako_mc_76;
}

#line 1 "<mako-codegen>"
static inline MakoMapI_QueryState* clone_integer_states(MakoMapI_QueryState* states) {
#line 80 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoMapI_QueryState *__mako_mc_77 = mako_maps_clone_i_QueryState(states);
    mako_trace_exit("clone_integer_states");
    return __mako_mc_77;
}

#line 1 "<mako-codegen>"
static inline MakoMapF_QueryState* clone_float_states(MakoMapF_QueryState* states) {
#line 81 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoMapF_QueryState *__mako_mc_78 = mako_maps_clone_f_QueryState(states);
    mako_trace_exit("clone_float_states");
    return __mako_mc_78;
}

#line 1 "<mako-codegen>"
static inline MakoMapB_QueryState* clone_boolean_states(MakoMapB_QueryState* states) {
#line 82 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoMapB_QueryState *__mako_mc_79 = mako_maps_clone_b_QueryState(states);
    mako_trace_exit("clone_boolean_states");
    return __mako_mc_79;
}

#line 1 "<mako-codegen>"
void TestContainerReplacementCleanup(void) {
    mako_trace_enter("TestContainerReplacementCleanup", "examples/testing/byte_conversion_cleanup_test.mko", 82);
#line 85 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoArr_QueryState __mako_mk_80 = mako_arr_QueryState_make(1, 1);
    MakoArr_QueryState items = __mako_mk_80;
#line 86 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_st_81;
    memset(&__mako_st_81, 0, sizeof(__mako_st_81));
    int64_t __mako_lit_83[] = { 7, 8 };
    MakoIntArray __mako_arr_82 = mako_int_array_view(__mako_lit_83, 2);
    MakoIntArray __mako_view_84 = __mako_arr_82;
    MakoIntArray __mako_own_85 = __mako_view_84.cap > 0 ? __mako_view_84 : mako_int_array_to_owned(__mako_view_84);
    __mako_st_81.values = __mako_own_85;
    MakoString __mako_srp_86 = mako_str_repeat(mako_str_view("label", 5), 4);
    MakoString __mako_elit_88[] = { __mako_srp_86 };
    MakoStrArray __mako_earr_87 = mako_str_array_of(__mako_elit_88, 1);
    mako_str_free(__mako_srp_86);
    __mako_st_81.labels = __mako_earr_87;
    int64_t __mako_iass_89 = 0;
    mako_arr_QueryState_set(items, __mako_iass_89, __mako_st_81);
#line 87 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_iarg_90 = 0;
    QueryState __mako_r_91 = retain_query_state(mako_arr_QueryState_get_ptr(items, __mako_iarg_90));
    QueryState snapshot = __mako_r_91;
#line 88 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoMapS_QueryState *__mako_mk_92 = mako_map_s_QueryState_make(0);
    MakoMapS_QueryState* records = __mako_mk_92;
#line 89 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_r_93 = retain_query_state(&snapshot);
    mako_map_s_QueryState_set(records, mako_str_view("state", 5), __mako_r_93);
#line 90 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t i = 0;
#line 91 "examples/testing/byte_conversion_cleanup_test.mko"
    while (1) {
        if (!((i < 1000))) break;
#line 92 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_iarg_94 = 0;
        QueryState __mako_r_95 = retain_query_state(mako_arr_QueryState_get_ptr(items, __mako_iarg_94));
        int64_t __mako_iass_96 = 0;
        mako_arr_QueryState_set(items, __mako_iass_96, __mako_r_95);
#line 93 "examples/testing/byte_conversion_cleanup_test.mko"
        QueryState __mako_mg_97 = mako_map_s_QueryState_get(records, mako_str_view("state", 5));
        QueryState __mako_r_98 = retain_query_state(&__mako_mg_97);
        mako_map_s_QueryState_set(records, mako_str_view("state", 5), __mako_r_98);
#line 94 "examples/testing/byte_conversion_cleanup_test.mko"
        i = mako_wrap_add_i64(i, 1);
    }
#line 96 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_st_99;
    memset(&__mako_st_99, 0, sizeof(__mako_st_99));
    int64_t __mako_lit_101[] = { 9 };
    MakoIntArray __mako_arr_100 = mako_int_array_view(__mako_lit_101, 1);
    MakoIntArray __mako_view_102 = __mako_arr_100;
    MakoIntArray __mako_own_103 = __mako_view_102.cap > 0 ? __mako_view_102 : mako_int_array_to_owned(__mako_view_102);
    __mako_st_99.values = __mako_own_103;
    MakoString __mako_slit_105[] = { mako_str_view("replacement", 11) };
    MakoStrArray __mako_sarr_104 = mako_str_array_of(__mako_slit_105, 1);
    __mako_st_99.labels = __mako_sarr_104;
    int64_t __mako_iass_106 = 0;
    mako_arr_QueryState_set(items, __mako_iass_106, __mako_st_99);
#line 97 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_idx_107 = 0;
    MAKO_BOUNDS_CHECK(__mako_idx_107 < 0 || (size_t)__mako_idx_107 >= snapshot.values.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(snapshot.values.data[__mako_idx_107], 7);
#line 98 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_mg_108 = mako_map_s_QueryState_get(records, mako_str_view("state", 5));
    int64_t __mako_idx_109 = 1;
    MAKO_BOUNDS_CHECK(__mako_idx_109 < 0 || (size_t)__mako_idx_109 >= __mako_mg_108.values.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(__mako_mg_108.values.data[__mako_idx_109], 8);
#line 99 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_idx_110 = 0;
    MakoString __mako_sg_111 = mako_str_array_get(snapshot.labels, __mako_idx_110);
    mako_assert_eq(mako_str_len(__mako_sg_111), 20);
#line 100 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_mg_112 = mako_map_s_QueryState_get(records, mako_str_view("state", 5));
    int64_t __mako_idx_113 = 0;
    MakoString __mako_sg_114 = mako_str_array_get(__mako_mg_112.labels, __mako_idx_113);
    mako_assert_eq(mako_str_len(__mako_sg_114), 20);
#line 101 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoMapS_QueryState* __mako_r_115 = clone_query_states(records);
    MakoMapS_QueryState* cloned = __mako_r_115;
#line 102 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoArr_QueryState __mako_r_116 = query_state_values(records);
    MakoArr_QueryState listed = __mako_r_116;
#line 103 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_maps_clear_s_QueryState(records);
#line 104 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_mg_117 = mako_map_s_QueryState_get(cloned, mako_str_view("state", 5));
    int64_t __mako_idx_118 = 0;
    MAKO_BOUNDS_CHECK(__mako_idx_118 < 0 || (size_t)__mako_idx_118 >= __mako_mg_117.values.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(__mako_mg_117.values.data[__mako_idx_118], 7);
#line 105 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_idx_119 = 0;
    QueryState __mako_sg_120 = mako_arr_QueryState_get(listed, __mako_idx_119);
    int64_t __mako_idx_121 = 0;
    MakoString __mako_sg_122 = mako_str_array_get(__mako_sg_120.labels, __mako_idx_121);
    mako_assert_eq(mako_str_len(__mako_sg_122), 20);
#line 106 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_maps_copy_s_QueryState(records, cloned);
#line 107 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_map_s_QueryState_delete(cloned, mako_str_view("state", 5));
#line 108 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_mg_123 = mako_map_s_QueryState_get(records, mako_str_view("state", 5));
    int64_t __mako_idx_124 = 1;
    MAKO_BOUNDS_CHECK(__mako_idx_124 < 0 || (size_t)__mako_idx_124 >= __mako_mg_123.values.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(__mako_mg_123.values.data[__mako_idx_124], 8);
#line 109 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_idx_125 = 0;
    MakoString __mako_sg_126 = mako_str_array_get(snapshot.labels, __mako_idx_125);
    mako_assert_eq(mako_str_len(__mako_sg_126), 20);
#line 110 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoMapI_QueryState *__mako_mk_127 = mako_map_i_QueryState_make(0);
    MakoMapI_QueryState* integers = __mako_mk_127;
#line 111 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoMapF_QueryState *__mako_mk_128 = mako_map_f_QueryState_make(0);
    MakoMapF_QueryState* floats = __mako_mk_128;
#line 112 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoMapB_QueryState *__mako_mk_129 = mako_map_b_QueryState_make(0);
    MakoMapB_QueryState* flags = __mako_mk_129;
#line 113 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_r_130 = retain_query_state(&snapshot);
    mako_map_i_QueryState_set(integers, 1, __mako_r_130);
#line 114 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_r_131 = retain_query_state(&snapshot);
    mako_map_f_QueryState_set(floats, 1.5, __mako_r_131);
#line 115 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_r_132 = retain_query_state(&snapshot);
    mako_map_b_QueryState_set(flags, true, __mako_r_132);
#line 116 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoMapI_QueryState* __mako_r_133 = clone_integer_states(integers);
    MakoMapI_QueryState* saved_integers = __mako_r_133;
#line 117 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoMapF_QueryState* __mako_r_134 = clone_float_states(floats);
    MakoMapF_QueryState* saved_floats = __mako_r_134;
#line 118 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoMapB_QueryState* __mako_r_135 = clone_boolean_states(flags);
    MakoMapB_QueryState* saved_flags = __mako_r_135;
#line 119 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_maps_clear_i_QueryState(integers);
#line 120 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_maps_clear_f_QueryState(floats);
#line 121 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_maps_clear_b_QueryState(flags);
#line 122 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_maps_copy_i_QueryState(integers, saved_integers);
#line 123 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_maps_copy_f_QueryState(floats, saved_floats);
#line 124 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_maps_copy_b_QueryState(flags, saved_flags);
#line 125 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_map_i_QueryState_delete(saved_integers, 1);
#line 126 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_map_f_QueryState_delete(saved_floats, 1.5);
#line 127 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_map_b_QueryState_delete(saved_flags, true);
#line 128 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_mg_136 = mako_map_i_QueryState_get(integers, 1);
    int64_t __mako_idx_137 = 0;
    MAKO_BOUNDS_CHECK(__mako_idx_137 < 0 || (size_t)__mako_idx_137 >= __mako_mg_136.values.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(__mako_mg_136.values.data[__mako_idx_137], 7);
#line 129 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_mg_138 = mako_map_f_QueryState_get(floats, 1.5);
    int64_t __mako_idx_139 = 0;
    MAKO_BOUNDS_CHECK(__mako_idx_139 < 0 || (size_t)__mako_idx_139 >= __mako_mg_138.values.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(__mako_mg_138.values.data[__mako_idx_139], 7);
#line 130 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_mg_140 = mako_map_b_QueryState_get(flags, true);
    int64_t __mako_idx_141 = 0;
    MAKO_BOUNDS_CHECK(__mako_idx_141 < 0 || (size_t)__mako_idx_141 >= __mako_mg_140.values.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(__mako_mg_140.values.data[__mako_idx_141], 7);
    mako_map_b_QueryState_free(saved_flags);
    mako_map_f_QueryState_free(saved_floats);
    mako_map_i_QueryState_free(saved_integers);
    mako_map_b_QueryState_free(flags);
    mako_map_f_QueryState_free(floats);
    mako_map_i_QueryState_free(integers);
    mako_arr_QueryState_free(listed);
    mako_map_s_QueryState_free(cloned);
    mako_map_s_QueryState_free(records);
    mako_int_array_free(snapshot.values);
    mako_str_array_free(snapshot.labels);
    mako_arr_QueryState_free(items);
    mako_trace_exit("TestContainerReplacementCleanup");
}

#line 1 "<mako-codegen>"
void TestQueryTemporaryCleanup(void) {
    mako_trace_enter("TestQueryTemporaryCleanup", "examples/testing/byte_conversion_cleanup_test.mko", 130);
#line 134 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoChanPtr *__mako_ch_142 = mako_chan_ptr_new_owned(1, __mako_channel_payload_drop_143);
    MakoChanPtr* messages = __mako_ch_142;
#line 135 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoString __mako_slit_145[] = { mako_str_view("hello", 5), mako_str_view("hé", 3) };
    MakoStrArray __mako_sarr_144 = mako_str_array_of(__mako_slit_145, 2);
    MakoStrArray parts = __mako_sarr_144;
#line 136 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t i = 0;
#line 137 "examples/testing/byte_conversion_cleanup_test.mko"
    while (1) {
        if (!((i < 1000))) break;
#line 138 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_srp_146 = mako_str_repeat(mako_str_view(" value ", 7), 3);
        MakoString trimmed = __mako_srp_146;
#line 139 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_trs_147 = mako_str_trim_space(trimmed);
        MakoString __mako_old_148 = trimmed;
        trimmed = __mako_trs_147;
        if (__mako_old_148.data != trimmed.data) mako_str_free(__mako_old_148);
#line 140 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_tr_149 = mako_str_trim(trimmed, mako_str_view("e", 1));
        MakoString __mako_old_150 = trimmed;
        trimmed = __mako_tr_149;
        if (__mako_old_150.data != trimmed.data) mako_str_free(__mako_old_150);
#line 141 "examples/testing/byte_conversion_cleanup_test.mko"
        mako_assert_eq(mako_str_len(trimmed), 18);
#line 142 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_srp_151 = mako_str_repeat(mako_str_view("temporary", 9), 3);
        MakoString __mako_sa_152 = __mako_srp_151;
        int64_t __mako_r_153 = borrowed_text_length(__mako_sa_152);
        mako_assert_eq(__mako_r_153, 27);
#line 143 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_srp_154 = mako_str_repeat(mako_str_view("values", 6), 1);
        MakoString __mako_sa_155 = __mako_srp_154;
        mako_assert_eq(mako_str_slice_ci_index(mako_str_view("select VALUES", 13), 0, 13, __mako_sa_155), 7);
#line 144 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_srp_156 = mako_str_repeat(mako_str_view("left:right", 10), 1);
        MakoString __mako_sa_157 = __mako_srp_156;
        MakoStrArray __mako_scut_158 = mako_str_cut(__mako_sa_157, mako_str_view(":", 1));
        MakoStrArray cut = __mako_scut_158;
#line 145 "examples/testing/byte_conversion_cleanup_test.mko"
        mako_assert_eq(mako_str_array_len(cut), 2);
#line 146 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_srp_159 = mako_str_repeat(mako_str_view("/issue64-missing-directory/file", 31), 1);
        MakoString __mako_sa_160 = __mako_srp_159;
        int64_t __mako_fo_161 = mako_file_open(__mako_sa_160, 0, 0);
        mako_assert_eq(__mako_fo_161, (-1));
#line 147 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_srp_162 = mako_str_repeat(mako_str_view("data", 4), 3);
        MakoString __mako_sa_163 = __mako_srp_162;
        int64_t __mako_wf_164 = mako_write_file(mako_str_view("/issue64-missing-directory/file", 31), __mako_sa_163);
        mako_assert_eq(__mako_wf_164, (-1));
#line 148 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_srp_165 = mako_str_repeat(mako_str_view("data", 4), 3);
        MakoString __mako_sa_166 = __mako_srp_165;
        int64_t __mako_af_167 = mako_append_file(mako_str_view("/issue64-missing-directory/file", 31), __mako_sa_166);
        mako_assert_eq(__mako_af_167, (-1));
#line 149 "examples/testing/byte_conversion_cleanup_test.mko"
        TokenChoice __mako_r_168 = choose_token(mako_str_view("select value from table", 23));
        TokenChoice choice = __mako_r_168;
#line 150 "examples/testing/byte_conversion_cleanup_test.mko"
        mako_assert(mako_str_eq(choice.value, mako_str_view("value", 5)));
#line 151 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_r_169 = response_text(mako_str_view("hello", 5));
        MakoString response = __mako_r_169;
#line 152 "examples/testing/byte_conversion_cleanup_test.mko"
        mako_assert(mako_str_eq(response, mako_str_view("HELLO!", 6)));
#line 153 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_srp_170 = mako_str_repeat(mako_str_view("hé", 3), 3);
        MakoString __mako_sa_171 = __mako_srp_170;
        mako_assert_eq(mako_str_len(__mako_sa_171), 9);
#line 154 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_idx_172 = 1;
        MakoString __mako_sg_173 = mako_str_array_get(parts, __mako_idx_172);
        mako_assert_eq(mako_str_len(__mako_sg_173), 3);
#line 155 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_srp_174 = mako_str_repeat(mako_str_view("hé", 3), 3);
        MakoString __mako_sa_175 = __mako_srp_174;
        MakoByteArray __mako_by_176 = mako_bytes_from_string(__mako_sa_175);
        int64_t __mako_idx_177 = 1;
        mako_assert_eq(mako_byte_get(__mako_by_176, __mako_idx_177), 195);
#line 156 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_idx_178 = 0;
        MakoString __mako_sg_179 = mako_str_array_get(parts, __mako_idx_178);
        MakoByteArray __mako_by_180 = mako_bytes_from_string(__mako_sg_179);
        int64_t __mako_idx_181 = 4;
        mako_assert_eq(mako_byte_get(__mako_by_180, __mako_idx_181), 111);
#line 157 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_stu_182 = mako_str_to_upper(mako_str_view("hello", 5));
        MakoString __mako_sa_183 = __mako_stu_182;
        MakoString __mako_srp_184 = mako_str_repeat(mako_str_view("L", 1), 1);
        MakoString __mako_sa_185 = __mako_srp_184;
        mako_assert_eq(mako_str_index(__mako_sa_183, __mako_sa_185), 2);
#line 158 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_stu_186 = mako_str_to_upper(mako_str_view("hello", 5));
        MakoString __mako_sa_187 = __mako_stu_186;
        mako_assert_eq(mako_str_last_index(__mako_sa_187, mako_str_view("L", 1)), 3);
#line 159 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_r_188 = response_text(mako_str_view("hello", 5));
        MakoString __mako_sa_189 = __mako_r_188;
        int64_t __mako_tw_190 = mako_tcp_write((-1), __mako_sa_189);
        mako_assert_eq(__mako_tw_190, (-1));
#line 160 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_r_191 = response_text(mako_str_view("hello", 5));
        MakoString __mako_sa_192 = __mako_r_191;
        int64_t __mako_twa_193 = mako_tcp_write_all((-1), __mako_sa_192);
        mako_assert_eq(__mako_twa_193, (-1));
#line 161 "examples/testing/byte_conversion_cleanup_test.mko"
        TokenChoice __mako_st_194;
        memset(&__mako_st_194, 0, sizeof(__mako_st_194));
        MakoString __mako_srp_195 = mako_str_repeat(mako_str_view("reply", 5), 4);
        __mako_st_194.value = __mako_srp_195;
        TokenChoice *__mako_sbox_197 = (TokenChoice*)mako_box_alloc(sizeof(TokenChoice));
        TokenChoice __mako_cloned_198 = __mako_st_194;
        MakoString __mako_cloned_199 = mako_str_clone(__mako_cloned_198.value);
        __mako_cloned_198.value = __mako_cloned_199;
        *__mako_sbox_197 = __mako_cloned_198;
        bool __mako_ok_196 = mako_chan_ptr_send(messages, __mako_sbox_197) != 0;
        mako_str_free(__mako_st_194.value);
        if (!__mako_ok_196) {
            mako_str_free(__mako_sbox_197->value);
            mako_box_free(__mako_sbox_197, sizeof(TokenChoice));
        }
        (void)(__mako_ok_196);
#line 162 "examples/testing/byte_conversion_cleanup_test.mko"
        TokenChoice __mako_st_200;
        memset(&__mako_st_200, 0, sizeof(__mako_st_200));
        MakoString __mako_srp_201 = mako_str_repeat(mako_str_view("full", 4), 4);
        __mako_st_200.value = __mako_srp_201;
        TokenChoice *__mako_sbox_203 = (TokenChoice*)mako_box_alloc(sizeof(TokenChoice));
        TokenChoice __mako_cloned_204 = __mako_st_200;
        MakoString __mako_cloned_205 = mako_str_clone(__mako_cloned_204.value);
        __mako_cloned_204.value = __mako_cloned_205;
        *__mako_sbox_203 = __mako_cloned_204;
        int64_t __mako_sto_202 = mako_chan_ptr_try_send(messages, __mako_sbox_203);
        mako_str_free(__mako_st_200.value);
        if (__mako_sto_202 != 1) {
            mako_str_free(__mako_sbox_203->value);
            mako_box_free(__mako_sbox_203, sizeof(TokenChoice));
        }
        mako_assert_eq(__mako_sto_202, 0);
#line 163 "examples/testing/byte_conversion_cleanup_test.mko"
        TokenChoice __mako_st_206;
        memset(&__mako_st_206, 0, sizeof(__mako_st_206));
        MakoString __mako_srp_207 = mako_str_repeat(mako_str_view("timeout", 7), 4);
        __mako_st_206.value = __mako_srp_207;
        TokenChoice *__mako_sbox_209 = (TokenChoice*)mako_box_alloc(sizeof(TokenChoice));
        TokenChoice __mako_cloned_210 = __mako_st_206;
        MakoString __mako_cloned_211 = mako_str_clone(__mako_cloned_210.value);
        __mako_cloned_210.value = __mako_cloned_211;
        *__mako_sbox_209 = __mako_cloned_210;
        int64_t __mako_sto_208 = mako_chan_ptr_send_timeout(messages, __mako_sbox_209, 0);
        mako_str_free(__mako_st_206.value);
        if (__mako_sto_208 != 1) {
            mako_str_free(__mako_sbox_209->value);
            mako_box_free(__mako_sbox_209, sizeof(TokenChoice));
        }
        mako_assert_eq(__mako_sto_208, 0);
#line 164 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_r_212 = receive_token(messages);
        MakoString reply = __mako_r_212;
#line 165 "examples/testing/byte_conversion_cleanup_test.mko"
        mako_assert_eq(mako_str_len(reply), 20);
#line 166 "examples/testing/byte_conversion_cleanup_test.mko"
        i = mako_wrap_add_i64(i, 1);
        mako_str_free(reply);
        mako_str_free(__mako_sa_192);
        mako_str_free(__mako_sa_189);
        mako_str_free(__mako_sa_187);
        mako_str_free(__mako_sa_185);
        mako_str_free(__mako_sa_183);
        mako_byte_array_free(__mako_by_180);
        mako_byte_array_free(__mako_by_176);
        mako_str_free(__mako_sa_175);
        mako_str_free(__mako_sa_171);
        mako_str_free(response);
        mako_str_free(choice.value);
        mako_str_free(__mako_sa_166);
        mako_str_free(__mako_sa_163);
        mako_str_free(__mako_sa_160);
        mako_str_array_free(cut);
        mako_str_free(__mako_sa_157);
        mako_str_free(__mako_sa_155);
        mako_str_free(__mako_sa_152);
        mako_str_free(trimmed);
    }
#line 168 "examples/testing/byte_conversion_cleanup_test.mko"
    TokenChoice __mako_st_213;
    memset(&__mako_st_213, 0, sizeof(__mako_st_213));
    __mako_st_213.value = mako_str_from_cstr("still owned by caller");
    TokenChoice named = __mako_st_213;
#line 169 "examples/testing/byte_conversion_cleanup_test.mko"
    TokenChoice *__mako_sbox_215 = (TokenChoice*)mako_box_alloc(sizeof(TokenChoice));
    TokenChoice __mako_cloned_216 = named;
    MakoString __mako_cloned_217 = mako_str_clone(__mako_cloned_216.value);
    __mako_cloned_216.value = __mako_cloned_217;
    *__mako_sbox_215 = __mako_cloned_216;
    bool __mako_ok_214 = mako_chan_ptr_send(messages, __mako_sbox_215) != 0;
    if (!__mako_ok_214) {
        mako_str_free(__mako_sbox_215->value);
        mako_box_free(__mako_sbox_215, sizeof(TokenChoice));
    }
    (void)(__mako_ok_214);
#line 170 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoString __mako_r_218 = receive_token(messages);
    MakoString received = __mako_r_218;
#line 171 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_assert(mako_str_eq(received, named.value));
#line 172 "examples/testing/byte_conversion_cleanup_test.mko"
    mako_chan_ptr_close(messages);
#line 173 "examples/testing/byte_conversion_cleanup_test.mko"
    TokenChoice __mako_st_219;
    memset(&__mako_st_219, 0, sizeof(__mako_st_219));
    MakoString __mako_srp_220 = mako_str_repeat(mako_str_view("closed", 6), 4);
    __mako_st_219.value = __mako_srp_220;
    TokenChoice *__mako_sbox_222 = (TokenChoice*)mako_box_alloc(sizeof(TokenChoice));
    TokenChoice __mako_cloned_223 = __mako_st_219;
    MakoString __mako_cloned_224 = mako_str_clone(__mako_cloned_223.value);
    __mako_cloned_223.value = __mako_cloned_224;
    *__mako_sbox_222 = __mako_cloned_223;
    bool __mako_ok_221 = mako_chan_ptr_send(messages, __mako_sbox_222) != 0;
    mako_str_free(__mako_st_219.value);
    if (!__mako_ok_221) {
        mako_str_free(__mako_sbox_222->value);
        mako_box_free(__mako_sbox_222, sizeof(TokenChoice));
    }
    (void)(__mako_ok_221);
#line 174 "examples/testing/byte_conversion_cleanup_test.mko"
    TokenChoice __mako_st_225;
    memset(&__mako_st_225, 0, sizeof(__mako_st_225));
    MakoString __mako_srp_226 = mako_str_repeat(mako_str_view("closed", 6), 4);
    __mako_st_225.value = __mako_srp_226;
    TokenChoice *__mako_sbox_228 = (TokenChoice*)mako_box_alloc(sizeof(TokenChoice));
    TokenChoice __mako_cloned_229 = __mako_st_225;
    MakoString __mako_cloned_230 = mako_str_clone(__mako_cloned_229.value);
    __mako_cloned_229.value = __mako_cloned_230;
    *__mako_sbox_228 = __mako_cloned_229;
    int64_t __mako_sto_227 = mako_chan_ptr_try_send(messages, __mako_sbox_228);
    mako_str_free(__mako_st_225.value);
    if (__mako_sto_227 != 1) {
        mako_str_free(__mako_sbox_228->value);
        mako_box_free(__mako_sbox_228, sizeof(TokenChoice));
    }
    (void)(__mako_sto_227);
#line 175 "examples/testing/byte_conversion_cleanup_test.mko"
    TokenChoice __mako_st_231;
    memset(&__mako_st_231, 0, sizeof(__mako_st_231));
    MakoString __mako_srp_232 = mako_str_repeat(mako_str_view("closed", 6), 4);
    __mako_st_231.value = __mako_srp_232;
    TokenChoice *__mako_sbox_234 = (TokenChoice*)mako_box_alloc(sizeof(TokenChoice));
    TokenChoice __mako_cloned_235 = __mako_st_231;
    MakoString __mako_cloned_236 = mako_str_clone(__mako_cloned_235.value);
    __mako_cloned_235.value = __mako_cloned_236;
    *__mako_sbox_234 = __mako_cloned_235;
    int64_t __mako_sto_233 = mako_chan_ptr_send_timeout(messages, __mako_sbox_234, 0);
    mako_str_free(__mako_st_231.value);
    if (__mako_sto_233 != 1) {
        mako_str_free(__mako_sbox_234->value);
        mako_box_free(__mako_sbox_234, sizeof(TokenChoice));
    }
    (void)(__mako_sto_233);
    mako_str_free(received);
    mako_str_free(named.value);
    mako_str_array_free(parts);
    mako_chan_ptr_free(messages);
    mako_trace_exit("TestQueryTemporaryCleanup");
}

#line 1 "<mako-codegen>"
void TestByteConversionCleanup(void) {
    mako_trace_enter("TestByteConversionCleanup", "examples/testing/byte_conversion_cleanup_test.mko", 175);
#line 179 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoString __mako_srp_237 = mako_str_repeat(mako_str_view("hello\n", 6), 256);
    MakoString page = __mako_srp_237;
#line 180 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t i = 0;
#line 181 "examples/testing/byte_conversion_cleanup_test.mko"
    while (1) {
        if (!((i < 1000))) break;
#line 182 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_r_238 = scan_page(page, 10);
        mako_assert_eq(__mako_r_238, 5);
#line 183 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_r_239 = scan_page(page, 0);
        mako_assert_eq(__mako_r_239, mako_str_len(page));
#line 184 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_r_240 = inspect_view(page);
        mako_assert_eq(__mako_r_240, mako_str_len(page));
#line 185 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoByteArray __mako_r_241 = page_bytes(page);
        MakoByteArray copied = __mako_r_241;
#line 186 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_idx_242 = 0;
        mako_assert_eq(mako_byte_get(copied, __mako_idx_242), 104);
#line 187 "examples/testing/byte_conversion_cleanup_test.mko"
        mako_assert_eq(mako_byte_array_len(copied), mako_str_len(page));
#line 188 "examples/testing/byte_conversion_cleanup_test.mko"
        i = mako_wrap_add_i64(i, 1);
        mako_byte_array_free(copied);
    }
    mako_str_free(page);
    mako_trace_exit("TestByteConversionCleanup");
}

#line 1 "<mako-codegen>"
void TestNestedFieldCleanup(void) {
    mako_trace_enter("TestNestedFieldCleanup", "examples/testing/byte_conversion_cleanup_test.mko", 188);
#line 195 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoArr_NestedQueryState __mako_mk_243 = mako_arr_NestedQueryState_make(1, 1);
    MakoArr_NestedQueryState indexed = __mako_mk_243;
#line 196 "examples/testing/byte_conversion_cleanup_test.mko"
    NestedQueryState __mako_st_244;
    memset(&__mako_st_244, 0, sizeof(__mako_st_244));
    QueryState __mako_st_245;
    memset(&__mako_st_245, 0, sizeof(__mako_st_245));
    int64_t __mako_lit_247[] = { 0 };
    MakoIntArray __mako_arr_246 = mako_int_array_view(__mako_lit_247, 1);
    MakoIntArray __mako_view_248 = __mako_arr_246;
    MakoIntArray __mako_own_249 = __mako_view_248.cap > 0 ? __mako_view_248 : mako_int_array_to_owned(__mako_view_248);
    __mako_st_245.values = __mako_own_249;
    MakoString __mako_slit_251[] = { mako_str_view("initial", 7) };
    MakoStrArray __mako_sarr_250 = mako_str_array_of(__mako_slit_251, 1);
    __mako_st_245.labels = __mako_sarr_250;
    __mako_st_244.state = __mako_st_245;
    int64_t __mako_iass_252 = 0;
    mako_arr_NestedQueryState_set(indexed, __mako_iass_252, __mako_st_244);
#line 197 "examples/testing/byte_conversion_cleanup_test.mko"
    NestedQueryState __mako_st_253;
    memset(&__mako_st_253, 0, sizeof(__mako_st_253));
    QueryState __mako_st_254;
    memset(&__mako_st_254, 0, sizeof(__mako_st_254));
    int64_t __mako_lit_256[] = { 1 };
    MakoIntArray __mako_arr_255 = mako_int_array_view(__mako_lit_256, 1);
    MakoIntArray __mako_view_257 = __mako_arr_255;
    MakoIntArray __mako_own_258 = __mako_view_257.cap > 0 ? __mako_view_257 : mako_int_array_to_owned(__mako_view_257);
    __mako_st_254.values = __mako_own_258;
    MakoString __mako_slit_260[] = { mako_str_view("seed", 4) };
    MakoStrArray __mako_sarr_259 = mako_str_array_of(__mako_slit_260, 1);
    __mako_st_254.labels = __mako_sarr_259;
    __mako_st_253.state = __mako_st_254;
    NestedQueryState outer = __mako_st_253;
#line 198 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t i = 0;
#line 199 "examples/testing/byte_conversion_cleanup_test.mko"
    while (1) {
        if (!((i < 1000))) break;
#line 200 "examples/testing/byte_conversion_cleanup_test.mko"
        QueryState __mako_st_261;
        memset(&__mako_st_261, 0, sizeof(__mako_st_261));
        int64_t __mako_lit_263[] = { i };
        MakoIntArray __mako_arr_262 = mako_int_array_view(__mako_lit_263, 1);
        MakoIntArray __mako_view_264 = __mako_arr_262;
        MakoIntArray __mako_own_265 = __mako_view_264.cap > 0 ? __mako_view_264 : mako_int_array_to_owned(__mako_view_264);
        __mako_st_261.values = __mako_own_265;
        MakoString __mako_srp_266 = mako_str_repeat(mako_str_view("indexed", 7), 3);
        MakoString __mako_elit_268[] = { __mako_srp_266 };
        MakoStrArray __mako_earr_267 = mako_str_array_of(__mako_elit_268, 1);
        mako_str_free(__mako_srp_266);
        __mako_st_261.labels = __mako_earr_267;
        int64_t __mako_ifield_269 = 0;
        QueryState __mako_old_st_270 = mako_arr_NestedQueryState_get_ptr(indexed, __mako_ifield_269)->state;
        mako_arr_NestedQueryState_get_ptr(indexed, __mako_ifield_269)->state = __mako_st_261;
        if (__mako_old_st_270.values.data != mako_arr_NestedQueryState_get_ptr(indexed, __mako_ifield_269)->state.values.data) { mako_int_array_free(__mako_old_st_270.values); }
if (__mako_old_st_270.values.data == mako_arr_NestedQueryState_get_ptr(indexed, __mako_ifield_269)->state.values.data && __mako_old_st_270.values.cap > 0 && mako_arr_NestedQueryState_get_ptr(indexed, __mako_ifield_269)->state.values.cap > 0 && __mako_old_st_270.values.data && mako_rc_shared(__mako_old_st_270.values.data)) mako_rc_release(__mako_old_st_270.values.data);
        if (__mako_old_st_270.labels.data != mako_arr_NestedQueryState_get_ptr(indexed, __mako_ifield_269)->state.labels.data) { mako_str_array_free(__mako_old_st_270.labels); }
if (__mako_old_st_270.labels.data == mako_arr_NestedQueryState_get_ptr(indexed, __mako_ifield_269)->state.labels.data && __mako_old_st_270.labels.cap > 0 && mako_arr_NestedQueryState_get_ptr(indexed, __mako_ifield_269)->state.labels.cap > 0 && __mako_old_st_270.labels.data && mako_rc_shared(__mako_old_st_270.labels.data)) mako_rc_release(__mako_old_st_270.labels.data);
#line 201 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_idx_271 = 0;
        NestedQueryState __mako_sg_272 = mako_arr_NestedQueryState_get(indexed, __mako_idx_271);
        int64_t __mako_idx_273 = 0;
        MakoString __mako_sg_274 = mako_str_array_get(__mako_sg_272.state.labels, __mako_idx_273);
        mako_assert_eq(mako_str_len(__mako_sg_274), 21);
#line 202 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_srp_275 = mako_str_repeat(mako_str_view("entry", 5), 3);
        MakoString __mako_sa_276 = __mako_srp_275;
        MakoStrArray __mako_ap_277 = mako_str_array_append(outer.state.labels, __mako_sa_276);
        mako_str_free(__mako_sa_276);
        MakoStrArray __mako_old_own_278 = outer.state.labels;
        outer.state.labels = __mako_ap_277;
        if (__mako_old_own_278.data != outer.state.labels.data && __mako_old_own_278.cap > 0 && __mako_old_own_278.data) mako_rc_release(__mako_old_own_278.data);
#line 203 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoIntArray __mako_r_279 = retain_values(outer.state.values);
        MakoIntArray retained = __mako_r_279;
#line 204 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoIntArray __mako_ident_move_280 = retained;
        memset(&retained, 0, sizeof(retained));
        MakoIntArray __mako_view_281 = __mako_ident_move_280;
        MakoIntArray __mako_own_282 = __mako_view_281.cap > 0 ? __mako_view_281 : mako_int_array_to_owned(__mako_view_281);
        MakoIntArray __mako_old_own_283 = outer.state.values;
        outer.state.values = __mako_own_282;
        if (__mako_old_own_283.data != outer.state.values.data) mako_int_array_free(__mako_old_own_283);
if (__mako_old_own_283.data == outer.state.values.data && __mako_old_own_283.cap > 0 && outer.state.values.cap > 0 && __mako_old_own_283.data && mako_rc_shared(__mako_old_own_283.data)) mako_rc_release(__mako_old_own_283.data);
#line 205 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_srp_284 = mako_str_repeat(mako_str_view("replacement", 11), 2);
        MakoString __mako_elit_286[] = { __mako_srp_284 };
        MakoStrArray __mako_earr_285 = mako_str_array_of(__mako_elit_286, 1);
        mako_str_free(__mako_srp_284);
        MakoStrArray __mako_old_own_287 = outer.state.labels;
        outer.state.labels = __mako_earr_285;
        if (__mako_old_own_287.data != outer.state.labels.data) mako_str_array_free(__mako_old_own_287);
#line 206 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_idx_288 = 0;
        MakoString __mako_sg_289 = mako_str_array_get(outer.state.labels, __mako_idx_288);
        mako_assert_eq(mako_str_len(__mako_sg_289), 22);
#line 207 "examples/testing/byte_conversion_cleanup_test.mko"
        i = mako_wrap_add_i64(i, 1);
    }
    mako_int_array_free(outer.state.values);
    mako_str_array_free(outer.state.labels);
    mako_arr_NestedQueryState_free(indexed);
    mako_trace_exit("TestNestedFieldCleanup");
}

#line 1 "<mako-codegen>"
void TestStructArrayReplacementCleanup(void) {
    mako_trace_enter("TestStructArrayReplacementCleanup", "examples/testing/byte_conversion_cleanup_test.mko", 207);
#line 214 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryBatch __mako_st_290;
    memset(&__mako_st_290, 0, sizeof(__mako_st_290));
    MakoArr_QueryState __mako_mk_291 = mako_arr_QueryState_make(0, 1);
    __mako_st_290.states = __mako_mk_291;
    QueryBatch batch = __mako_st_290;
#line 215 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t i = 0;
#line 216 "examples/testing/byte_conversion_cleanup_test.mko"
    while (1) {
        if (!((i < 1000))) break;
#line 217 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoArr_QueryState __mako_mk_292 = mako_arr_QueryState_make(0, 1);
        MakoArr_QueryState fresh = __mako_mk_292;
#line 218 "examples/testing/byte_conversion_cleanup_test.mko"
        QueryState __mako_st_293;
        memset(&__mako_st_293, 0, sizeof(__mako_st_293));
        int64_t __mako_lit_295[] = { i };
        MakoIntArray __mako_arr_294 = mako_int_array_view(__mako_lit_295, 1);
        MakoIntArray __mako_view_296 = __mako_arr_294;
        MakoIntArray __mako_own_297 = __mako_view_296.cap > 0 ? __mako_view_296 : mako_int_array_to_owned(__mako_view_296);
        __mako_st_293.values = __mako_own_297;
        MakoString __mako_srp_298 = mako_str_repeat(mako_str_view("payload", 7), 3);
        MakoString __mako_elit_300[] = { __mako_srp_298 };
        MakoStrArray __mako_earr_299 = mako_str_array_of(__mako_elit_300, 1);
        mako_str_free(__mako_srp_298);
        __mako_st_293.labels = __mako_earr_299;
        MakoArr_QueryState __mako_ap_301 = mako_arr_QueryState_append(fresh, __mako_st_293);
        fresh = __mako_ap_301;
#line 219 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoArr_QueryState __mako_cloned_302 = mako_arr_QueryState_clone(fresh);
        MakoArr_QueryState __mako_old_own_303 = batch.states;
        batch.states = __mako_cloned_302;
        if (__mako_old_own_303.data != batch.states.data) mako_arr_QueryState_free(__mako_old_own_303);
if (__mako_old_own_303.data == batch.states.data && __mako_old_own_303.cap > 0 && batch.states.cap > 0 && __mako_old_own_303.data && mako_rc_shared(__mako_old_own_303.data)) mako_rc_release(__mako_old_own_303.data);
#line 220 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_idx_304 = 0;
        QueryState __mako_sg_305 = mako_arr_QueryState_get(batch.states, __mako_idx_304);
        int64_t __mako_idx_306 = 0;
        MAKO_BOUNDS_CHECK(__mako_idx_306 < 0 || (size_t)__mako_idx_306 >= __mako_sg_305.values.len, "index out of bounds (slices are 0..len-1)");
        mako_assert_eq(__mako_sg_305.values.data[__mako_idx_306], i);
#line 221 "examples/testing/byte_conversion_cleanup_test.mko"
        i = mako_wrap_add_i64(i, 1);
        mako_arr_QueryState_free(fresh);
    }
    mako_arr_QueryState_free(batch.states);
    mako_trace_exit("TestStructArrayReplacementCleanup");
}

#line 1 "<mako-codegen>"
static inline SessionHandles retain_session_handles(SessionHandles *handles) {
#line 226 "examples/testing/byte_conversion_cleanup_test.mko"
    SessionHandles __mako_cloned_307 = (*handles);
    MakoChan* __mako_cloned_308 = mako_chan_clone(__mako_cloned_307.numbers);
    __mako_cloned_307.numbers = __mako_cloned_308;
    MakoChanStr* __mako_cloned_309 = mako_chan_str_clone(__mako_cloned_307.texts);
    __mako_cloned_307.texts = __mako_cloned_309;
    MakoChanPtr* __mako_cloned_310 = mako_chan_ptr_clone(__mako_cloned_307.objects);
    __mako_cloned_307.objects = __mako_cloned_310;
    mako_trace_exit("retain_session_handles");
    return __mako_cloned_307;
}

#line 1 "<mako-codegen>"
void TestChannelFieldReplacementCleanup(void) {
    mako_trace_enter("TestChannelFieldReplacementCleanup", "examples/testing/byte_conversion_cleanup_test.mko", 226);
#line 229 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t i = 0;
#line 230 "examples/testing/byte_conversion_cleanup_test.mko"
    while (1) {
        if (!((i < 1000))) break;
#line 231 "examples/testing/byte_conversion_cleanup_test.mko"
        SessionHandles __mako_st_311;
        memset(&__mako_st_311, 0, sizeof(__mako_st_311));
        MakoChan *__mako_ch_312 = mako_chan_new(1);
        __mako_st_311.numbers = __mako_ch_312;
        MakoChanStr *__mako_ch_313 = mako_chan_str_new(1);
        __mako_st_311.texts = __mako_ch_313;
        MakoChanPtr *__mako_ch_314 = mako_chan_ptr_new_owned(1, __mako_channel_payload_drop_315);
        __mako_st_311.objects = __mako_ch_314;
        SessionHandles handles = __mako_st_311;
#line 232 "examples/testing/byte_conversion_cleanup_test.mko"
        SessionHandles __mako_r_316 = retain_session_handles(&handles);
        SessionHandles retained = __mako_r_316;
#line 233 "examples/testing/byte_conversion_cleanup_test.mko"
        SessionHandles __mako_r_317 = retain_session_handles(&handles);
        SessionHandles __mako_old_st_318 = handles;
        handles = __mako_r_317;
        if (__mako_old_st_318.numbers != handles.numbers) { mako_chan_free(__mako_old_st_318.numbers); }
if (__mako_old_st_318.numbers == handles.numbers && __mako_old_st_318.numbers) mako_chan_free(__mako_old_st_318.numbers);
        if (__mako_old_st_318.texts != handles.texts) { mako_chan_str_free(__mako_old_st_318.texts); }
if (__mako_old_st_318.texts == handles.texts && __mako_old_st_318.texts) mako_chan_str_free(__mako_old_st_318.texts);
        if (__mako_old_st_318.objects != handles.objects) { mako_chan_ptr_free(__mako_old_st_318.objects); }
if (__mako_old_st_318.objects == handles.objects && __mako_old_st_318.objects) mako_chan_ptr_free(__mako_old_st_318.objects);
#line 234 "examples/testing/byte_conversion_cleanup_test.mko"
        bool __mako_ok_319 = mako_chan_send(handles.numbers, i) != 0;
        (void)(__mako_ok_319);
#line 235 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t __mako_rv_320 = mako_chan_recv(retained.numbers);
        mako_assert_eq(__mako_rv_320, i);
#line 236 "examples/testing/byte_conversion_cleanup_test.mko"
        bool __mako_ok_321 = mako_chan_str_send(handles.texts, mako_str_view("text", 4)) != 0;
        (void)(__mako_ok_321);
#line 237 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_rv_322 = mako_chan_str_recv(retained.texts);
        MakoString __mako_len_string_323 = __mako_rv_322;
        mako_assert_eq(mako_str_len(__mako_len_string_323), 4);
#line 238 "examples/testing/byte_conversion_cleanup_test.mko"
        TokenChoice __mako_st_324;
        memset(&__mako_st_324, 0, sizeof(__mako_st_324));
        __mako_st_324.value = mako_str_from_cstr("object");
        TokenChoice *__mako_sbox_326 = (TokenChoice*)mako_box_alloc(sizeof(TokenChoice));
        TokenChoice __mako_cloned_327 = __mako_st_324;
        MakoString __mako_cloned_328 = mako_str_clone(__mako_cloned_327.value);
        __mako_cloned_327.value = __mako_cloned_328;
        *__mako_sbox_326 = __mako_cloned_327;
        bool __mako_ok_325 = mako_chan_ptr_send(handles.objects, __mako_sbox_326) != 0;
        mako_str_free(__mako_st_324.value);
        if (!__mako_ok_325) {
            mako_str_free(__mako_sbox_326->value);
            mako_box_free(__mako_sbox_326, sizeof(TokenChoice));
        }
        (void)(__mako_ok_325);
#line 239 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_r_329 = receive_token(retained.objects);
        MakoString __mako_len_string_330 = __mako_r_329;
        mako_assert_eq(mako_str_len(__mako_len_string_330), 6);
#line 240 "examples/testing/byte_conversion_cleanup_test.mko"
        i = mako_wrap_add_i64(i, 1);
        mako_str_free(__mako_len_string_330);
        mako_str_free(__mako_len_string_323);
        mako_chan_free(retained.numbers);
        mako_chan_str_free(retained.texts);
        mako_chan_ptr_free(retained.objects);
        mako_chan_free(handles.numbers);
        mako_chan_str_free(handles.texts);
        mako_chan_ptr_free(handles.objects);
    }
    mako_trace_exit("TestChannelFieldReplacementCleanup");
}

#line 1 "<mako-codegen>"
void TestPageWriteTemporaryCleanup(void) {
    mako_trace_enter("TestPageWriteTemporaryCleanup", "examples/testing/byte_conversion_cleanup_test.mko", 240);
#line 246 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoPageMan *__mako_pm_331 = mako_pman_open(mako_str_from_cstr(""));
    MakoPageMan* pages = __mako_pm_331;
#line 247 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t i = 0;
#line 248 "examples/testing/byte_conversion_cleanup_test.mko"
    while (1) {
        if (!((i < 1000))) break;
#line 249 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_srp_332 = mako_str_repeat(mako_str_view("page", 4), 8);
        MakoString __mako_sa_333 = __mako_srp_332;
        mako_assert_eq(mako_pman_write_page(pages, 0, __mako_sa_333), (-1));
#line 250 "examples/testing/byte_conversion_cleanup_test.mko"
        i = mako_wrap_add_i64(i, 1);
        mako_str_free(__mako_sa_333);
    }
#line 252 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoString __mako_srp_334 = mako_str_repeat(mako_str_view("mako_issue64_", 13), 1);
    MakoString __mako_sa_335 = __mako_srp_334;
    MakoString __mako_tf_336 = mako_temp_file(__mako_sa_335);
    MakoString path = __mako_tf_336;
#line 253 "examples/testing/byte_conversion_cleanup_test.mko"
    bool __mako_fe_337 = mako_file_exists(path);
    mako_assert(__mako_fe_337);
#line 254 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_rm_338 = mako_remove_file(path);
    mako_assert_eq(__mako_rm_338, 0);
    mako_str_free(path);
    mako_str_free(__mako_sa_335);
    mako_trace_exit("TestPageWriteTemporaryCleanup");
}

#line 1 "<mako-codegen>"
void TestSelfResliceCleanup(void) {
    mako_trace_enter("TestSelfResliceCleanup", "examples/testing/byte_conversion_cleanup_test.mko", 254);
#line 258 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoIntArray __mako_mk_339 = mako_int_array_make(0, 64);
    MakoIntArray slots = __mako_mk_339;
#line 259 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t i = 0;
#line 260 "examples/testing/byte_conversion_cleanup_test.mko"
    while (1) {
        if (!((i < 1000))) break;
#line 261 "examples/testing/byte_conversion_cleanup_test.mko"
        int64_t j = 0;
#line 262 "examples/testing/byte_conversion_cleanup_test.mko"
        while (1) {
            if (!((j < 64))) break;
#line 263 "examples/testing/byte_conversion_cleanup_test.mko"
            MakoIntArray __mako_ap_340 = mako_slice_append(slots, j);
            void *__mako_old_data_341 = slots.data;
            size_t __mako_old_cap_342 = slots.cap;
            slots = __mako_ap_340;
            if (__mako_old_data_341 != slots.data && __mako_old_cap_342 > 0 && __mako_old_data_341) mako_rc_release(__mako_old_data_341);
#line 264 "examples/testing/byte_conversion_cleanup_test.mko"
            j = mako_wrap_add_i64(j, 1);
        }
#line 266 "examples/testing/byte_conversion_cleanup_test.mko"
        while (1) {
            if (!((mako_array_len(slots) > 0))) break;
#line 267 "examples/testing/byte_conversion_cleanup_test.mko"
            MakoIntArray __mako_sl_343 = mako_slice_expr(slots, 0, mako_wrap_sub_i64(mako_array_len(slots), 1), 0, 0);
            MakoIntArray __mako_reassign_view_344 = __mako_sl_343;
            if (__mako_reassign_view_344.data == slots.data && slots.cap > 0) __mako_reassign_view_344.cap = slots.cap;
            MakoIntArray __mako_view_345 = __mako_reassign_view_344;
            MakoIntArray __mako_own_346 = __mako_view_345.cap > 0 ? __mako_view_345 : mako_int_array_to_owned(__mako_view_345);
            MakoIntArray __mako_old_347 = slots;
            slots = __mako_own_346;
            if (__mako_old_347.data != slots.data) mako_int_array_free(__mako_old_347);
        }
#line 269 "examples/testing/byte_conversion_cleanup_test.mko"
        mako_assert_eq(mako_array_len(slots), 0);
#line 270 "examples/testing/byte_conversion_cleanup_test.mko"
        i = mako_wrap_add_i64(i, 1);
    }
#line 272 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_lit_349[] = { 1, 2, 3 };
    MakoIntArray __mako_arr_348 = mako_int_array_view(__mako_lit_349, 3);
    MakoIntArray values = __mako_arr_348;
#line 273 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoIntArray __mako_r_350 = retain_values(values);
    MakoIntArray retained = __mako_r_350;
#line 274 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoIntArray __mako_sl_351 = mako_slice_expr(values, 1, 3, 0, 0);
    MakoIntArray __mako_reassign_view_352 = __mako_sl_351;
    if (__mako_reassign_view_352.data == values.data && values.cap > 0) __mako_reassign_view_352.cap = values.cap;
    MakoIntArray __mako_view_353 = __mako_reassign_view_352;
    MakoIntArray __mako_own_354 = __mako_view_353.cap > 0 ? __mako_view_353 : mako_int_array_to_owned(__mako_view_353);
    MakoIntArray __mako_old_355 = values;
    values = __mako_own_354;
    if (__mako_old_355.data != values.data) mako_int_array_free(__mako_old_355);
#line 275 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_idx_356 = 0;
    MAKO_BOUNDS_CHECK(__mako_idx_356 < 0 || (size_t)__mako_idx_356 >= values.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(values.data[__mako_idx_356], 2);
#line 276 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_idx_357 = 0;
    MAKO_BOUNDS_CHECK(__mako_idx_357 < 0 || (size_t)__mako_idx_357 >= retained.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(retained.data[__mako_idx_357], 1);
#line 277 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoByteArray __mako_by_358 = mako_bytes_from_string(mako_str_view("abc", 3));
    MakoByteArray data = __mako_by_358;
#line 278 "examples/testing/byte_conversion_cleanup_test.mko"
    MakoByteArray __mako_sl_359 = mako_byte_slice_expr(data, 1, 3, 0, 0);
    MakoByteArray __mako_reassign_view_360 = __mako_sl_359;
    if (__mako_reassign_view_360.data == data.data && data.cap > 0) __mako_reassign_view_360.cap = data.cap;
    MakoByteArray __mako_view_361 = __mako_reassign_view_360;
    MakoByteArray __mako_own_362 = __mako_view_361.cap > 0 ? __mako_view_361 : mako_byte_array_to_owned(__mako_view_361);
    MakoByteArray __mako_old_363 = data;
    data = __mako_own_362;
    if (__mako_old_363.data != data.data) mako_byte_array_free(__mako_old_363);
#line 279 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_idx_364 = 0;
    mako_assert_eq(mako_byte_get(data, __mako_idx_364), 98);
    mako_byte_array_free(data);
    mako_int_array_free(retained);
    mako_int_array_free(values);
    mako_int_array_free(slots);
    mako_trace_exit("TestSelfResliceCleanup");
}

#line 1 "<mako-codegen>"
static inline QueryState fresh_query_state(int64_t n) {
#line 283 "examples/testing/byte_conversion_cleanup_test.mko"
    QueryState __mako_st_365;
    memset(&__mako_st_365, 0, sizeof(__mako_st_365));
    int64_t __mako_lit_367[] = { n };
    MakoIntArray __mako_arr_366 = mako_int_array_view(__mako_lit_367, 1);
    MakoIntArray __mako_view_368 = __mako_arr_366;
    MakoIntArray __mako_own_369 = __mako_view_368.cap > 0 ? __mako_view_368 : mako_int_array_to_owned(__mako_view_368);
    __mako_st_365.values = __mako_own_369;
    MakoString __mako_srp_370 = mako_str_repeat(mako_str_view("temporary", 9), 3);
    MakoString __mako_elit_372[] = { __mako_srp_370 };
    MakoStrArray __mako_earr_371 = mako_str_array_of(__mako_elit_372, 1);
    mako_str_free(__mako_srp_370);
    __mako_st_365.labels = __mako_earr_371;
    mako_trace_exit("fresh_query_state");
    return __mako_st_365;
}

#line 1 "<mako-codegen>"
static inline int64_t inspect_query_state(QueryState *state) {
#line 285 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t __mako_idx_373 = 0;
    MakoString __mako_sg_374 = mako_str_array_get(state->labels, __mako_idx_373);
    int64_t __mako_retv_375 = mako_str_len(__mako_sg_374);
    mako_trace_exit("inspect_query_state");
    return __mako_retv_375;
}

#line 1 "<mako-codegen>"
void TestBorrowedStructArgumentCleanup(void) {
    mako_trace_enter("TestBorrowedStructArgumentCleanup", "examples/testing/byte_conversion_cleanup_test.mko", 285);
#line 288 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t i = 0;
#line 289 "examples/testing/byte_conversion_cleanup_test.mko"
    while (1) {
        if (!((i < 1000))) break;
#line 290 "examples/testing/byte_conversion_cleanup_test.mko"
        QueryState __mako_r_376 = fresh_query_state(i);
        int64_t __mako_r_377 = inspect_query_state(&__mako_r_376);
        mako_assert_eq(__mako_r_377, 27);
#line 291 "examples/testing/byte_conversion_cleanup_test.mko"
        QueryState __mako_st_378;
        memset(&__mako_st_378, 0, sizeof(__mako_st_378));
        int64_t __mako_lit_380[] = { i };
        MakoIntArray __mako_arr_379 = mako_int_array_view(__mako_lit_380, 1);
        MakoIntArray __mako_view_381 = __mako_arr_379;
        MakoIntArray __mako_own_382 = __mako_view_381.cap > 0 ? __mako_view_381 : mako_int_array_to_owned(__mako_view_381);
        __mako_st_378.values = __mako_own_382;
        MakoString __mako_srp_383 = mako_str_repeat(mako_str_view("literal", 7), 3);
        MakoString __mako_elit_385[] = { __mako_srp_383 };
        MakoStrArray __mako_earr_384 = mako_str_array_of(__mako_elit_385, 1);
        mako_str_free(__mako_srp_383);
        __mako_st_378.labels = __mako_earr_384;
        int64_t __mako_r_386 = inspect_query_state(&__mako_st_378);
        mako_assert_eq(__mako_r_386, 21);
#line 292 "examples/testing/byte_conversion_cleanup_test.mko"
        i = mako_wrap_add_i64(i, 1);
        mako_int_array_free(__mako_st_378.values);
        mako_str_array_free(__mako_st_378.labels);
        mako_int_array_free(__mako_r_376.values);
        mako_str_array_free(__mako_r_376.labels);
    }
    mako_trace_exit("TestBorrowedStructArgumentCleanup");
}

#line 1 "<mako-codegen>"
void TestProcessTemporaryCleanup(void) {
    mako_trace_enter("TestProcessTemporaryCleanup", "examples/testing/byte_conversion_cleanup_test.mko", 292);
#line 297 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t i = 0;
#line 298 "examples/testing/byte_conversion_cleanup_test.mko"
    while (1) {
        if (!((i < 1000))) break;
#line 299 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoStrArray __mako_av_387 = mako_args();
        MakoStrArray argv = __mako_av_387;
#line 300 "examples/testing/byte_conversion_cleanup_test.mko"
        mako_assert((mako_str_array_len(argv) > 0));
#line 301 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoString __mako_srp_388 = mako_str_repeat(mako_str_view("PIPE", 4), 1);
        MakoString __mako_sa_389 = __mako_srp_388;
        int64_t ignored = mako_signal_ignore(__mako_sa_389);
#line 302 "examples/testing/byte_conversion_cleanup_test.mko"
        i = mako_wrap_add_i64(i, 1);
        mako_str_free(__mako_sa_389);
        mako_str_array_free(argv);
    }
    mako_trace_exit("TestProcessTemporaryCleanup");
}

#line 1 "<mako-codegen>"
void TestQueuedStructChannelCleanup(void) {
    mako_trace_enter("TestQueuedStructChannelCleanup", "examples/testing/byte_conversion_cleanup_test.mko", 302);
#line 307 "examples/testing/byte_conversion_cleanup_test.mko"
    int64_t i = 0;
#line 308 "examples/testing/byte_conversion_cleanup_test.mko"
    while (1) {
        if (!((i < 1000))) break;
#line 309 "examples/testing/byte_conversion_cleanup_test.mko"
        MakoChanPtr *__mako_ch_390 = mako_chan_ptr_new_owned(2, __mako_channel_payload_drop_391);
        MakoChanPtr* pending = __mako_ch_390;
#line 310 "examples/testing/byte_conversion_cleanup_test.mko"
        QueryState __mako_r_392 = fresh_query_state(i);
        QueryState *__mako_sbox_394 = (QueryState*)mako_box_alloc(sizeof(QueryState));
        QueryState __mako_cloned_395 = __mako_r_392;
        MakoIntArray __mako_cloned_396 = mako_int_array_clone(__mako_cloned_395.values);
        __mako_cloned_395.values = __mako_cloned_396;
        MakoStrArray __mako_cloned_397 = mako_str_array_clone(__mako_cloned_395.labels);
        __mako_cloned_395.labels = __mako_cloned_397;
        *__mako_sbox_394 = __mako_cloned_395;
        bool __mako_ok_393 = mako_chan_ptr_send(pending, __mako_sbox_394) != 0;
        mako_int_array_free(__mako_r_392.values);
        mako_str_array_free(__mako_r_392.labels);
        if (!__mako_ok_393) {
            mako_int_array_free(__mako_sbox_394->values);
            mako_str_array_free(__mako_sbox_394->labels);
            mako_box_free(__mako_sbox_394, sizeof(QueryState));
        }
        __mako_ok_393;
#line 311 "examples/testing/byte_conversion_cleanup_test.mko"
        QueryState __mako_r_398 = fresh_query_state(mako_wrap_add_i64(i, 1));
        QueryState *__mako_sbox_400 = (QueryState*)mako_box_alloc(sizeof(QueryState));
        QueryState __mako_cloned_401 = __mako_r_398;
        MakoIntArray __mako_cloned_402 = mako_int_array_clone(__mako_cloned_401.values);
        __mako_cloned_401.values = __mako_cloned_402;
        MakoStrArray __mako_cloned_403 = mako_str_array_clone(__mako_cloned_401.labels);
        __mako_cloned_401.labels = __mako_cloned_403;
        *__mako_sbox_400 = __mako_cloned_401;
        bool __mako_ok_399 = mako_chan_ptr_send(pending, __mako_sbox_400) != 0;
        mako_int_array_free(__mako_r_398.values);
        mako_str_array_free(__mako_r_398.labels);
        if (!__mako_ok_399) {
            mako_int_array_free(__mako_sbox_400->values);
            mako_str_array_free(__mako_sbox_400->labels);
            mako_box_free(__mako_sbox_400, sizeof(QueryState));
        }
        __mako_ok_399;
#line 312 "examples/testing/byte_conversion_cleanup_test.mko"
        mako_chan_ptr_close(pending);
#line 313 "examples/testing/byte_conversion_cleanup_test.mko"
        i = mako_wrap_add_i64(i, 1);
        mako_chan_ptr_free(pending);
    }
    mako_trace_exit("TestQueuedStructChannelCleanup");
}

#line 1 "<mako-codegen>"
void mako_main(void) {
    mako_trace_enter("main", "examples/testing/byte_conversion_cleanup_test.mko", 313);
#line 318 "examples/testing/byte_conversion_cleanup_test.mko"
    TestQueuedStructChannelCleanup();
#line 319 "examples/testing/byte_conversion_cleanup_test.mko"
    TestProcessTemporaryCleanup();
#line 320 "examples/testing/byte_conversion_cleanup_test.mko"
    TestByteConversionCleanup();
#line 321 "examples/testing/byte_conversion_cleanup_test.mko"
    TestQueryTemporaryCleanup();
#line 322 "examples/testing/byte_conversion_cleanup_test.mko"
    TestContainerReplacementCleanup();
#line 323 "examples/testing/byte_conversion_cleanup_test.mko"
    TestRetainedFieldCleanup();
#line 324 "examples/testing/byte_conversion_cleanup_test.mko"
    TestNestedFieldCleanup();
#line 325 "examples/testing/byte_conversion_cleanup_test.mko"
    TestStructArrayReplacementCleanup();
#line 326 "examples/testing/byte_conversion_cleanup_test.mko"
    TestChannelFieldReplacementCleanup();
#line 327 "examples/testing/byte_conversion_cleanup_test.mko"
    TestPageWriteTemporaryCleanup();
#line 328 "examples/testing/byte_conversion_cleanup_test.mko"
    TestBorrowedStructArgumentCleanup();
#line 329 "examples/testing/byte_conversion_cleanup_test.mko"
    TestSelfResliceCleanup();
    mako_trace_exit("main");
}

#line 1 "<mako-codegen>"

int main(int argc, char **argv) {
    mako_set_args(argc, argv);
    mako_main();
    return 0;
}
