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

typedef struct Logger_State {
    int64_t count;
} Logger_State;
static inline bool mako_eq_Logger_State(Logger_State a, Logger_State b) {
    return (a.count == b.count);
}
static inline uint64_t mako_hash_Logger_State(Logger_State k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= mako_hash_i64((int64_t)k.count); h *= 1099511628211ULL;
    return h;
}
typedef struct MakoArr_Logger_State {
    Logger_State *data;
    size_t len;
    size_t cap;
} MakoArr_Logger_State;
static inline MakoArr_Logger_State mako_arr_Logger_State_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Logger_State a;
    a.data = (Logger_State *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(Logger_State));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_Logger_State_free(MakoArr_Logger_State a) {
    if (!(a.cap > 0 && a.data)) return;
    mako_rc_release(a.data);
}
static inline MakoArr_Logger_State mako_arr_Logger_State_clone(MakoArr_Logger_State a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_Logger_State_len(MakoArr_Logger_State a) { return (int64_t)a.len; }
static inline int64_t mako_arr_Logger_State_cap(MakoArr_Logger_State a) { return (int64_t)a.cap; }
static inline Logger_State mako_arr_Logger_State_get(MakoArr_Logger_State a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline Logger_State* mako_arr_Logger_State_get_ptr(MakoArr_Logger_State a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return &a.data[i];
}
static inline void mako_arr_Logger_State_set(MakoArr_Logger_State a, int64_t i, Logger_State v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    Logger_State old = a.data[i];
    a.data[i] = v;
}
static inline MakoArr_Logger_State mako_arr_Logger_State_append(MakoArr_Logger_State s, Logger_State v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Logger_State *nd = (Logger_State *)mako_rc_alloc(ncap * sizeof(Logger_State));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Logger_State));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len] = v;
    s.len++;
    return s;
}
static inline MakoArr_Logger_State mako_arr_Logger_State_arena_append(MakoArena *arena, MakoArr_Logger_State s, Logger_State v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Logger_State *nd = (Logger_State *)mako_arena_alloc(arena, ncap * sizeof(Logger_State));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Logger_State));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_Logger_State mako_arr_Logger_State_of(const Logger_State *vals, size_t n) {
    MakoArr_Logger_State a = mako_arr_Logger_State_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(Logger_State));
    return a;
}
static inline MakoArr_Logger_State mako_arr_Logger_State_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Logger_State a;
    a.data = (Logger_State *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(Logger_State));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(Logger_State));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_Logger_State {
    MakoArr_Logger_State *data;
    size_t len;
    size_t cap;
} MakoArr_arr_Logger_State;
static inline MakoArr_arr_Logger_State mako_arr_arr_Logger_State_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_Logger_State a;
    a.data = (MakoArr_Logger_State *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_Logger_State));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_Logger_State_free(MakoArr_arr_Logger_State a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
    }
    mako_rc_release(a.data);
}
static inline MakoArr_arr_Logger_State mako_arr_arr_Logger_State_clone(MakoArr_arr_Logger_State a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_arr_Logger_State_len(MakoArr_arr_Logger_State a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_Logger_State_cap(MakoArr_arr_Logger_State a) { return (int64_t)a.cap; }
static inline MakoArr_Logger_State mako_arr_arr_Logger_State_get(MakoArr_arr_Logger_State a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_Logger_State_set(MakoArr_arr_Logger_State a, int64_t i, MakoArr_Logger_State v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    MakoArr_Logger_State old = a.data[i];
    a.data[i] = v;
    if (old.data != v.data) { mako_arr_Logger_State_free(old); }
}
static inline MakoArr_arr_Logger_State mako_arr_arr_Logger_State_append(MakoArr_arr_Logger_State s, MakoArr_Logger_State v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_Logger_State *nd = (MakoArr_Logger_State *)mako_rc_alloc(ncap * sizeof(MakoArr_Logger_State));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_Logger_State));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_Logger_State mako_arr_arr_Logger_State_of(const MakoArr_Logger_State *vals, size_t n) {
    MakoArr_arr_Logger_State a = mako_arr_arr_Logger_State_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_Logger_State));
    return a;
}
static inline MakoArr_arr_Logger_State mako_arr_arr_Logger_State_slice_expr(MakoArr_arr_Logger_State s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_Logger_State out;
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

typedef struct Logger_Log_Env {
    MakoString msg;
} Logger_Log_Env;
static inline bool mako_eq_Logger_Log_Env(Logger_Log_Env a, Logger_Log_Env b) {
    return mako_str_eq(a.msg, b.msg);
}
static inline uint64_t mako_hash_Logger_Log_Env(Logger_Log_Env k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= mako_hash_bytes(k.msg.data, k.msg.len); h *= 1099511628211ULL;
    return h;
}
typedef struct MakoArr_Logger_Log_Env {
    Logger_Log_Env *data;
    size_t len;
    size_t cap;
} MakoArr_Logger_Log_Env;
static inline MakoArr_Logger_Log_Env mako_arr_Logger_Log_Env_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Logger_Log_Env a;
    a.data = (Logger_Log_Env *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(Logger_Log_Env));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_Logger_Log_Env_free(MakoArr_Logger_Log_Env a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
        for (size_t i = 0; i < a.len; i++) {
            mako_str_free(a.data[i].msg);
        }
    }
    mako_rc_release(a.data);
}
static inline MakoArr_Logger_Log_Env mako_arr_Logger_Log_Env_clone(MakoArr_Logger_Log_Env a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_Logger_Log_Env_len(MakoArr_Logger_Log_Env a) { return (int64_t)a.len; }
static inline int64_t mako_arr_Logger_Log_Env_cap(MakoArr_Logger_Log_Env a) { return (int64_t)a.cap; }
static inline Logger_Log_Env mako_arr_Logger_Log_Env_get(MakoArr_Logger_Log_Env a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline Logger_Log_Env* mako_arr_Logger_Log_Env_get_ptr(MakoArr_Logger_Log_Env a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return &a.data[i];
}
static inline void mako_arr_Logger_Log_Env_set(MakoArr_Logger_Log_Env a, int64_t i, Logger_Log_Env v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    Logger_Log_Env old = a.data[i];
    a.data[i] = v;
    if (old.msg.data != v.msg.data) { mako_str_free(old.msg); }
}
static inline MakoArr_Logger_Log_Env mako_arr_Logger_Log_Env_append(MakoArr_Logger_Log_Env s, Logger_Log_Env v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Logger_Log_Env *nd = (Logger_Log_Env *)mako_rc_alloc(ncap * sizeof(Logger_Log_Env));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Logger_Log_Env));
        for (size_t i = 0; i < s.len; i++) {
            nd[i].msg = mako_str_clone(nd[i].msg);
        }
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len] = v;
    s.len++;
    return s;
}
static inline MakoArr_Logger_Log_Env mako_arr_Logger_Log_Env_arena_append(MakoArena *arena, MakoArr_Logger_Log_Env s, Logger_Log_Env v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Logger_Log_Env *nd = (Logger_Log_Env *)mako_arena_alloc(arena, ncap * sizeof(Logger_Log_Env));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Logger_Log_Env));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_Logger_Log_Env mako_arr_Logger_Log_Env_of(const Logger_Log_Env *vals, size_t n) {
    MakoArr_Logger_Log_Env a = mako_arr_Logger_Log_Env_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(Logger_Log_Env));
    return a;
}
static inline MakoArr_Logger_Log_Env mako_arr_Logger_Log_Env_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Logger_Log_Env a;
    a.data = (Logger_Log_Env *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(Logger_Log_Env));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(Logger_Log_Env));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_Logger_Log_Env {
    MakoArr_Logger_Log_Env *data;
    size_t len;
    size_t cap;
} MakoArr_arr_Logger_Log_Env;
static inline MakoArr_arr_Logger_Log_Env mako_arr_arr_Logger_Log_Env_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_Logger_Log_Env a;
    a.data = (MakoArr_Logger_Log_Env *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_Logger_Log_Env));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_Logger_Log_Env_free(MakoArr_arr_Logger_Log_Env a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
    }
    mako_rc_release(a.data);
}
static inline MakoArr_arr_Logger_Log_Env mako_arr_arr_Logger_Log_Env_clone(MakoArr_arr_Logger_Log_Env a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_arr_Logger_Log_Env_len(MakoArr_arr_Logger_Log_Env a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_Logger_Log_Env_cap(MakoArr_arr_Logger_Log_Env a) { return (int64_t)a.cap; }
static inline MakoArr_Logger_Log_Env mako_arr_arr_Logger_Log_Env_get(MakoArr_arr_Logger_Log_Env a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_Logger_Log_Env_set(MakoArr_arr_Logger_Log_Env a, int64_t i, MakoArr_Logger_Log_Env v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    MakoArr_Logger_Log_Env old = a.data[i];
    a.data[i] = v;
    if (old.data != v.data) { mako_arr_Logger_Log_Env_free(old); }
}
static inline MakoArr_arr_Logger_Log_Env mako_arr_arr_Logger_Log_Env_append(MakoArr_arr_Logger_Log_Env s, MakoArr_Logger_Log_Env v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_Logger_Log_Env *nd = (MakoArr_Logger_Log_Env *)mako_rc_alloc(ncap * sizeof(MakoArr_Logger_Log_Env));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_Logger_Log_Env));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_Logger_Log_Env mako_arr_arr_Logger_Log_Env_of(const MakoArr_Logger_Log_Env *vals, size_t n) {
    MakoArr_arr_Logger_Log_Env a = mako_arr_arr_Logger_Log_Env_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_Logger_Log_Env));
    return a;
}
static inline MakoArr_arr_Logger_Log_Env mako_arr_arr_Logger_Log_Env_slice_expr(MakoArr_arr_Logger_Log_Env s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_Logger_Log_Env out;
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

typedef struct Calculator_State {
    int64_t result;
} Calculator_State;
static inline bool mako_eq_Calculator_State(Calculator_State a, Calculator_State b) {
    return (a.result == b.result);
}
static inline uint64_t mako_hash_Calculator_State(Calculator_State k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= mako_hash_i64((int64_t)k.result); h *= 1099511628211ULL;
    return h;
}
typedef struct MakoArr_Calculator_State {
    Calculator_State *data;
    size_t len;
    size_t cap;
} MakoArr_Calculator_State;
static inline MakoArr_Calculator_State mako_arr_Calculator_State_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Calculator_State a;
    a.data = (Calculator_State *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(Calculator_State));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_Calculator_State_free(MakoArr_Calculator_State a) {
    if (!(a.cap > 0 && a.data)) return;
    mako_rc_release(a.data);
}
static inline MakoArr_Calculator_State mako_arr_Calculator_State_clone(MakoArr_Calculator_State a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_Calculator_State_len(MakoArr_Calculator_State a) { return (int64_t)a.len; }
static inline int64_t mako_arr_Calculator_State_cap(MakoArr_Calculator_State a) { return (int64_t)a.cap; }
static inline Calculator_State mako_arr_Calculator_State_get(MakoArr_Calculator_State a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline Calculator_State* mako_arr_Calculator_State_get_ptr(MakoArr_Calculator_State a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return &a.data[i];
}
static inline void mako_arr_Calculator_State_set(MakoArr_Calculator_State a, int64_t i, Calculator_State v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    Calculator_State old = a.data[i];
    a.data[i] = v;
}
static inline MakoArr_Calculator_State mako_arr_Calculator_State_append(MakoArr_Calculator_State s, Calculator_State v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Calculator_State *nd = (Calculator_State *)mako_rc_alloc(ncap * sizeof(Calculator_State));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Calculator_State));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len] = v;
    s.len++;
    return s;
}
static inline MakoArr_Calculator_State mako_arr_Calculator_State_arena_append(MakoArena *arena, MakoArr_Calculator_State s, Calculator_State v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Calculator_State *nd = (Calculator_State *)mako_arena_alloc(arena, ncap * sizeof(Calculator_State));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Calculator_State));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_Calculator_State mako_arr_Calculator_State_of(const Calculator_State *vals, size_t n) {
    MakoArr_Calculator_State a = mako_arr_Calculator_State_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(Calculator_State));
    return a;
}
static inline MakoArr_Calculator_State mako_arr_Calculator_State_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Calculator_State a;
    a.data = (Calculator_State *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(Calculator_State));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(Calculator_State));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_Calculator_State {
    MakoArr_Calculator_State *data;
    size_t len;
    size_t cap;
} MakoArr_arr_Calculator_State;
static inline MakoArr_arr_Calculator_State mako_arr_arr_Calculator_State_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_Calculator_State a;
    a.data = (MakoArr_Calculator_State *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_Calculator_State));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_Calculator_State_free(MakoArr_arr_Calculator_State a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
    }
    mako_rc_release(a.data);
}
static inline MakoArr_arr_Calculator_State mako_arr_arr_Calculator_State_clone(MakoArr_arr_Calculator_State a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_arr_Calculator_State_len(MakoArr_arr_Calculator_State a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_Calculator_State_cap(MakoArr_arr_Calculator_State a) { return (int64_t)a.cap; }
static inline MakoArr_Calculator_State mako_arr_arr_Calculator_State_get(MakoArr_arr_Calculator_State a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_Calculator_State_set(MakoArr_arr_Calculator_State a, int64_t i, MakoArr_Calculator_State v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    MakoArr_Calculator_State old = a.data[i];
    a.data[i] = v;
    if (old.data != v.data) { mako_arr_Calculator_State_free(old); }
}
static inline MakoArr_arr_Calculator_State mako_arr_arr_Calculator_State_append(MakoArr_arr_Calculator_State s, MakoArr_Calculator_State v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_Calculator_State *nd = (MakoArr_Calculator_State *)mako_rc_alloc(ncap * sizeof(MakoArr_Calculator_State));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_Calculator_State));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_Calculator_State mako_arr_arr_Calculator_State_of(const MakoArr_Calculator_State *vals, size_t n) {
    MakoArr_arr_Calculator_State a = mako_arr_arr_Calculator_State_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_Calculator_State));
    return a;
}
static inline MakoArr_arr_Calculator_State mako_arr_arr_Calculator_State_slice_expr(MakoArr_arr_Calculator_State s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_Calculator_State out;
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

typedef struct Calculator_Add_Env {
    int64_t a;
    int64_t b;
} Calculator_Add_Env;
static inline bool mako_eq_Calculator_Add_Env(Calculator_Add_Env a, Calculator_Add_Env b) {
    return (a.a == b.a) && (a.b == b.b);
}
static inline uint64_t mako_hash_Calculator_Add_Env(Calculator_Add_Env k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= mako_hash_i64((int64_t)k.a); h *= 1099511628211ULL;
    h ^= mako_hash_i64((int64_t)k.b); h *= 1099511628211ULL;
    return h;
}
typedef struct MakoArr_Calculator_Add_Env {
    Calculator_Add_Env *data;
    size_t len;
    size_t cap;
} MakoArr_Calculator_Add_Env;
static inline MakoArr_Calculator_Add_Env mako_arr_Calculator_Add_Env_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Calculator_Add_Env a;
    a.data = (Calculator_Add_Env *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(Calculator_Add_Env));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_Calculator_Add_Env_free(MakoArr_Calculator_Add_Env a) {
    if (!(a.cap > 0 && a.data)) return;
    mako_rc_release(a.data);
}
static inline MakoArr_Calculator_Add_Env mako_arr_Calculator_Add_Env_clone(MakoArr_Calculator_Add_Env a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_Calculator_Add_Env_len(MakoArr_Calculator_Add_Env a) { return (int64_t)a.len; }
static inline int64_t mako_arr_Calculator_Add_Env_cap(MakoArr_Calculator_Add_Env a) { return (int64_t)a.cap; }
static inline Calculator_Add_Env mako_arr_Calculator_Add_Env_get(MakoArr_Calculator_Add_Env a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline Calculator_Add_Env* mako_arr_Calculator_Add_Env_get_ptr(MakoArr_Calculator_Add_Env a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return &a.data[i];
}
static inline void mako_arr_Calculator_Add_Env_set(MakoArr_Calculator_Add_Env a, int64_t i, Calculator_Add_Env v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    Calculator_Add_Env old = a.data[i];
    a.data[i] = v;
}
static inline MakoArr_Calculator_Add_Env mako_arr_Calculator_Add_Env_append(MakoArr_Calculator_Add_Env s, Calculator_Add_Env v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Calculator_Add_Env *nd = (Calculator_Add_Env *)mako_rc_alloc(ncap * sizeof(Calculator_Add_Env));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Calculator_Add_Env));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len] = v;
    s.len++;
    return s;
}
static inline MakoArr_Calculator_Add_Env mako_arr_Calculator_Add_Env_arena_append(MakoArena *arena, MakoArr_Calculator_Add_Env s, Calculator_Add_Env v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Calculator_Add_Env *nd = (Calculator_Add_Env *)mako_arena_alloc(arena, ncap * sizeof(Calculator_Add_Env));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Calculator_Add_Env));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_Calculator_Add_Env mako_arr_Calculator_Add_Env_of(const Calculator_Add_Env *vals, size_t n) {
    MakoArr_Calculator_Add_Env a = mako_arr_Calculator_Add_Env_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(Calculator_Add_Env));
    return a;
}
static inline MakoArr_Calculator_Add_Env mako_arr_Calculator_Add_Env_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Calculator_Add_Env a;
    a.data = (Calculator_Add_Env *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(Calculator_Add_Env));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(Calculator_Add_Env));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_Calculator_Add_Env {
    MakoArr_Calculator_Add_Env *data;
    size_t len;
    size_t cap;
} MakoArr_arr_Calculator_Add_Env;
static inline MakoArr_arr_Calculator_Add_Env mako_arr_arr_Calculator_Add_Env_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_Calculator_Add_Env a;
    a.data = (MakoArr_Calculator_Add_Env *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_Calculator_Add_Env));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_Calculator_Add_Env_free(MakoArr_arr_Calculator_Add_Env a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
    }
    mako_rc_release(a.data);
}
static inline MakoArr_arr_Calculator_Add_Env mako_arr_arr_Calculator_Add_Env_clone(MakoArr_arr_Calculator_Add_Env a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_arr_Calculator_Add_Env_len(MakoArr_arr_Calculator_Add_Env a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_Calculator_Add_Env_cap(MakoArr_arr_Calculator_Add_Env a) { return (int64_t)a.cap; }
static inline MakoArr_Calculator_Add_Env mako_arr_arr_Calculator_Add_Env_get(MakoArr_arr_Calculator_Add_Env a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_Calculator_Add_Env_set(MakoArr_arr_Calculator_Add_Env a, int64_t i, MakoArr_Calculator_Add_Env v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    MakoArr_Calculator_Add_Env old = a.data[i];
    a.data[i] = v;
    if (old.data != v.data) { mako_arr_Calculator_Add_Env_free(old); }
}
static inline MakoArr_arr_Calculator_Add_Env mako_arr_arr_Calculator_Add_Env_append(MakoArr_arr_Calculator_Add_Env s, MakoArr_Calculator_Add_Env v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_Calculator_Add_Env *nd = (MakoArr_Calculator_Add_Env *)mako_rc_alloc(ncap * sizeof(MakoArr_Calculator_Add_Env));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_Calculator_Add_Env));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_Calculator_Add_Env mako_arr_arr_Calculator_Add_Env_of(const MakoArr_Calculator_Add_Env *vals, size_t n) {
    MakoArr_arr_Calculator_Add_Env a = mako_arr_arr_Calculator_Add_Env_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_Calculator_Add_Env));
    return a;
}
static inline MakoArr_arr_Calculator_Add_Env mako_arr_arr_Calculator_Add_Env_slice_expr(MakoArr_arr_Calculator_Add_Env s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_Calculator_Add_Env out;
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

typedef struct Calculator_MulAdd_Env {
    int64_t x;
    int64_t y;
    int64_t z;
} Calculator_MulAdd_Env;
static inline bool mako_eq_Calculator_MulAdd_Env(Calculator_MulAdd_Env a, Calculator_MulAdd_Env b) {
    return (a.x == b.x) && (a.y == b.y) && (a.z == b.z);
}
static inline uint64_t mako_hash_Calculator_MulAdd_Env(Calculator_MulAdd_Env k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= mako_hash_i64((int64_t)k.x); h *= 1099511628211ULL;
    h ^= mako_hash_i64((int64_t)k.y); h *= 1099511628211ULL;
    h ^= mako_hash_i64((int64_t)k.z); h *= 1099511628211ULL;
    return h;
}
typedef struct MakoArr_Calculator_MulAdd_Env {
    Calculator_MulAdd_Env *data;
    size_t len;
    size_t cap;
} MakoArr_Calculator_MulAdd_Env;
static inline MakoArr_Calculator_MulAdd_Env mako_arr_Calculator_MulAdd_Env_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Calculator_MulAdd_Env a;
    a.data = (Calculator_MulAdd_Env *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(Calculator_MulAdd_Env));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_Calculator_MulAdd_Env_free(MakoArr_Calculator_MulAdd_Env a) {
    if (!(a.cap > 0 && a.data)) return;
    mako_rc_release(a.data);
}
static inline MakoArr_Calculator_MulAdd_Env mako_arr_Calculator_MulAdd_Env_clone(MakoArr_Calculator_MulAdd_Env a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_Calculator_MulAdd_Env_len(MakoArr_Calculator_MulAdd_Env a) { return (int64_t)a.len; }
static inline int64_t mako_arr_Calculator_MulAdd_Env_cap(MakoArr_Calculator_MulAdd_Env a) { return (int64_t)a.cap; }
static inline Calculator_MulAdd_Env mako_arr_Calculator_MulAdd_Env_get(MakoArr_Calculator_MulAdd_Env a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline Calculator_MulAdd_Env* mako_arr_Calculator_MulAdd_Env_get_ptr(MakoArr_Calculator_MulAdd_Env a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return &a.data[i];
}
static inline void mako_arr_Calculator_MulAdd_Env_set(MakoArr_Calculator_MulAdd_Env a, int64_t i, Calculator_MulAdd_Env v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    Calculator_MulAdd_Env old = a.data[i];
    a.data[i] = v;
}
static inline MakoArr_Calculator_MulAdd_Env mako_arr_Calculator_MulAdd_Env_append(MakoArr_Calculator_MulAdd_Env s, Calculator_MulAdd_Env v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Calculator_MulAdd_Env *nd = (Calculator_MulAdd_Env *)mako_rc_alloc(ncap * sizeof(Calculator_MulAdd_Env));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Calculator_MulAdd_Env));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len] = v;
    s.len++;
    return s;
}
static inline MakoArr_Calculator_MulAdd_Env mako_arr_Calculator_MulAdd_Env_arena_append(MakoArena *arena, MakoArr_Calculator_MulAdd_Env s, Calculator_MulAdd_Env v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Calculator_MulAdd_Env *nd = (Calculator_MulAdd_Env *)mako_arena_alloc(arena, ncap * sizeof(Calculator_MulAdd_Env));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Calculator_MulAdd_Env));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_Calculator_MulAdd_Env mako_arr_Calculator_MulAdd_Env_of(const Calculator_MulAdd_Env *vals, size_t n) {
    MakoArr_Calculator_MulAdd_Env a = mako_arr_Calculator_MulAdd_Env_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(Calculator_MulAdd_Env));
    return a;
}
static inline MakoArr_Calculator_MulAdd_Env mako_arr_Calculator_MulAdd_Env_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Calculator_MulAdd_Env a;
    a.data = (Calculator_MulAdd_Env *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(Calculator_MulAdd_Env));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(Calculator_MulAdd_Env));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_Calculator_MulAdd_Env {
    MakoArr_Calculator_MulAdd_Env *data;
    size_t len;
    size_t cap;
} MakoArr_arr_Calculator_MulAdd_Env;
static inline MakoArr_arr_Calculator_MulAdd_Env mako_arr_arr_Calculator_MulAdd_Env_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_Calculator_MulAdd_Env a;
    a.data = (MakoArr_Calculator_MulAdd_Env *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_Calculator_MulAdd_Env));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_Calculator_MulAdd_Env_free(MakoArr_arr_Calculator_MulAdd_Env a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
    }
    mako_rc_release(a.data);
}
static inline MakoArr_arr_Calculator_MulAdd_Env mako_arr_arr_Calculator_MulAdd_Env_clone(MakoArr_arr_Calculator_MulAdd_Env a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_arr_Calculator_MulAdd_Env_len(MakoArr_arr_Calculator_MulAdd_Env a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_Calculator_MulAdd_Env_cap(MakoArr_arr_Calculator_MulAdd_Env a) { return (int64_t)a.cap; }
static inline MakoArr_Calculator_MulAdd_Env mako_arr_arr_Calculator_MulAdd_Env_get(MakoArr_arr_Calculator_MulAdd_Env a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_Calculator_MulAdd_Env_set(MakoArr_arr_Calculator_MulAdd_Env a, int64_t i, MakoArr_Calculator_MulAdd_Env v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    MakoArr_Calculator_MulAdd_Env old = a.data[i];
    a.data[i] = v;
    if (old.data != v.data) { mako_arr_Calculator_MulAdd_Env_free(old); }
}
static inline MakoArr_arr_Calculator_MulAdd_Env mako_arr_arr_Calculator_MulAdd_Env_append(MakoArr_arr_Calculator_MulAdd_Env s, MakoArr_Calculator_MulAdd_Env v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_Calculator_MulAdd_Env *nd = (MakoArr_Calculator_MulAdd_Env *)mako_rc_alloc(ncap * sizeof(MakoArr_Calculator_MulAdd_Env));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_Calculator_MulAdd_Env));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_Calculator_MulAdd_Env mako_arr_arr_Calculator_MulAdd_Env_of(const MakoArr_Calculator_MulAdd_Env *vals, size_t n) {
    MakoArr_arr_Calculator_MulAdd_Env a = mako_arr_arr_Calculator_MulAdd_Env_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_Calculator_MulAdd_Env));
    return a;
}
static inline MakoArr_arr_Calculator_MulAdd_Env mako_arr_arr_Calculator_MulAdd_Env_slice_expr(MakoArr_arr_Calculator_MulAdd_Env s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_Calculator_MulAdd_Env out;
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

static void __attribute__((constructor)) __mako_reflect_reg_Logger_State(void) {
    (void)mako_reflect_register_type("Logger_State", "count:int");
}

static void __attribute__((constructor)) __mako_reflect_reg_Logger_Log_Env(void) {
    (void)mako_reflect_register_type("Logger_Log_Env", "msg:string");
}

static void __attribute__((constructor)) __mako_reflect_reg_Calculator_State(void) {
    (void)mako_reflect_register_type("Calculator_State", "result:int");
}

static void __attribute__((constructor)) __mako_reflect_reg_Calculator_Add_Env(void) {
    (void)mako_reflect_register_type("Calculator_Add_Env", "a:int,b:int");
}

static void __attribute__((constructor)) __mako_reflect_reg_Calculator_MulAdd_Env(void) {
    (void)mako_reflect_register_type("Calculator_MulAdd_Env", "x:int,y:int,z:int");
}

#line 1 "examples/testing/actor_typed_payload_test.mko"
void TestActorStringPayload(void);
void TestActorMultiIntPayload(void);
void TestActorTriplePayload(void);
void mako_main(void);
int64_t Logger_Log(MakoString msg);
int64_t Logger_Bye(void);
MakoChan* Logger_spawn(void);
bool Logger_send(MakoChan* __mbox, int64_t __tag);
int64_t Logger_loop(MakoChan* __mbox);
int64_t Calculator_Add(int64_t a, int64_t b);
int64_t Calculator_MulAdd(int64_t x, int64_t y, int64_t z);
int64_t Calculator_Bye(void);
MakoChan* Calculator_spawn(void);
bool Calculator_send(MakoChan* __mbox, int64_t __tag);
int64_t Calculator_loop(MakoChan* __mbox);

/*__MAKO_HELPERS__*/
static _Atomic int64_t __mako_kick_id_24 = 0;
__attribute__((noinline,optnone)) void *__kick_Calculator_loop_23(void *arg) {
atomic_store_explicit(&__mako_kick_id_24, 24, memory_order_relaxed);
intptr_t *a = (intptr_t*)arg;
MakoChan* p0 = (MakoChan*)a[0];
free(a);
int64_t __r = (int64_t)Calculator_loop(p0);
mako_chan_free(p0);
return (void*)(intptr_t)__r;
}
static _Atomic int64_t __mako_kick_id_15 = 0;
__attribute__((noinline,optnone)) void *__kick_Calculator_loop_14(void *arg) {
atomic_store_explicit(&__mako_kick_id_15, 15, memory_order_relaxed);
intptr_t *a = (intptr_t*)arg;
MakoChan* p0 = (MakoChan*)a[0];
free(a);
int64_t __r = (int64_t)Calculator_loop(p0);
mako_chan_free(p0);
return (void*)(intptr_t)__r;
}
static _Atomic int64_t __mako_kick_id_4 = 0;
__attribute__((noinline,optnone)) void *__kick_Logger_loop_3(void *arg) {
atomic_store_explicit(&__mako_kick_id_4, 4, memory_order_relaxed);
intptr_t *a = (intptr_t*)arg;
MakoChan* p0 = (MakoChan*)a[0];
free(a);
int64_t __r = (int64_t)Logger_loop(p0);
mako_chan_free(p0);
return (void*)(intptr_t)__r;
}

#line 1 "<mako-codegen>"
void TestActorStringPayload(void) {
    mako_trace_enter("TestActorStringPayload", "examples/testing/actor_typed_payload_test.mko", 0);
#line 29 "examples/testing/actor_typed_payload_test.mko"
    MakoChan* __mako_r_0 = Logger_spawn();
    MakoChan* l = __mako_r_0;
#line 30 "examples/testing/actor_typed_payload_test.mko"
    MakoNursery t = mako_nursery_new();
#line 31 "examples/testing/actor_typed_payload_test.mko"
    intptr_t *__mako_arg_2 = (intptr_t*)malloc(sizeof(intptr_t) * 1);
    __mako_arg_2[0] = (intptr_t)mako_chan_clone(l);
    MakoTask *__mako_job_1 = mako_spawn(&t, __kick_Logger_loop_3, __mako_arg_2);
    MakoTask* j = __mako_job_1;
#line 32 "examples/testing/actor_typed_payload_test.mko"
    int64_t __mako_r_4 = Logger_Log(mako_str_view("hello", 5));
    bool __mako_r_5 = Logger_send(l, __mako_r_4);
    mako_assert(__mako_r_5);
#line 33 "examples/testing/actor_typed_payload_test.mko"
    int64_t __mako_r_6 = Logger_Log(mako_str_view("world", 5));
    bool __mako_r_7 = Logger_send(l, __mako_r_6);
    mako_assert(__mako_r_7);
#line 34 "examples/testing/actor_typed_payload_test.mko"
    int64_t __mako_r_8 = Logger_Bye();
    bool __mako_r_9 = Logger_send(l, __mako_r_8);
    mako_assert(__mako_r_9);
#line 35 "examples/testing/actor_typed_payload_test.mko"
    int64_t __mako_jn_10 = (int64_t)(intptr_t)mako_await(j);
    int64_t final_count = __mako_jn_10;
#line 36 "examples/testing/actor_typed_payload_test.mko"
    mako_assert_eq(final_count, 2);
    mako_nursery_cancel_join(&t);
    mako_chan_free(l);
    mako_trace_exit("TestActorStringPayload");
}

#line 1 "<mako-codegen>"
void TestActorMultiIntPayload(void) {
    mako_trace_enter("TestActorMultiIntPayload", "examples/testing/actor_typed_payload_test.mko", 36);
#line 41 "examples/testing/actor_typed_payload_test.mko"
    MakoChan* __mako_r_11 = Calculator_spawn();
    MakoChan* c = __mako_r_11;
#line 42 "examples/testing/actor_typed_payload_test.mko"
    MakoNursery t = mako_nursery_new();
#line 43 "examples/testing/actor_typed_payload_test.mko"
    intptr_t *__mako_arg_13 = (intptr_t*)malloc(sizeof(intptr_t) * 1);
    __mako_arg_13[0] = (intptr_t)mako_chan_clone(c);
    MakoTask *__mako_job_12 = mako_spawn(&t, __kick_Calculator_loop_14, __mako_arg_13);
    MakoTask* j = __mako_job_12;
#line 44 "examples/testing/actor_typed_payload_test.mko"
    int64_t __mako_r_15 = Calculator_Add(10, 20);
    bool __mako_r_16 = Calculator_send(c, __mako_r_15);
    (void)(__mako_r_16);
#line 45 "examples/testing/actor_typed_payload_test.mko"
    int64_t __mako_r_17 = Calculator_Bye();
    bool __mako_r_18 = Calculator_send(c, __mako_r_17);
    (void)(__mako_r_18);
#line 46 "examples/testing/actor_typed_payload_test.mko"
    int64_t __mako_jn_19 = (int64_t)(intptr_t)mako_await(j);
    int64_t result = __mako_jn_19;
#line 47 "examples/testing/actor_typed_payload_test.mko"
    mako_assert_eq(result, 30);
    mako_nursery_cancel_join(&t);
    mako_chan_free(c);
    mako_trace_exit("TestActorMultiIntPayload");
}

#line 1 "<mako-codegen>"
void TestActorTriplePayload(void) {
    mako_trace_enter("TestActorTriplePayload", "examples/testing/actor_typed_payload_test.mko", 47);
#line 52 "examples/testing/actor_typed_payload_test.mko"
    MakoChan* __mako_r_20 = Calculator_spawn();
    MakoChan* c = __mako_r_20;
#line 53 "examples/testing/actor_typed_payload_test.mko"
    MakoNursery t = mako_nursery_new();
#line 54 "examples/testing/actor_typed_payload_test.mko"
    intptr_t *__mako_arg_22 = (intptr_t*)malloc(sizeof(intptr_t) * 1);
    __mako_arg_22[0] = (intptr_t)mako_chan_clone(c);
    MakoTask *__mako_job_21 = mako_spawn(&t, __kick_Calculator_loop_23, __mako_arg_22);
    MakoTask* j = __mako_job_21;
#line 55 "examples/testing/actor_typed_payload_test.mko"
    int64_t __mako_r_24 = Calculator_MulAdd(3, 4, 5);
    bool __mako_r_25 = Calculator_send(c, __mako_r_24);
    (void)(__mako_r_25);
#line 56 "examples/testing/actor_typed_payload_test.mko"
    int64_t __mako_r_26 = Calculator_Bye();
    bool __mako_r_27 = Calculator_send(c, __mako_r_26);
    (void)(__mako_r_27);
#line 57 "examples/testing/actor_typed_payload_test.mko"
    int64_t __mako_jn_28 = (int64_t)(intptr_t)mako_await(j);
    int64_t result = __mako_jn_28;
#line 58 "examples/testing/actor_typed_payload_test.mko"
    mako_assert_eq(result, 17);
    mako_nursery_cancel_join(&t);
    mako_chan_free(c);
    mako_trace_exit("TestActorTriplePayload");
}

#line 1 "<mako-codegen>"
void mako_main(void) {
    mako_trace_enter("main", "examples/testing/actor_typed_payload_test.mko", 58);
#line 63 "examples/testing/actor_typed_payload_test.mko"
    TestActorStringPayload();
#line 64 "examples/testing/actor_typed_payload_test.mko"
    TestActorMultiIntPayload();
#line 65 "examples/testing/actor_typed_payload_test.mko"
    TestActorTriplePayload();
    mako_trace_exit("main");
}

#line 1 "<mako-codegen>"
int64_t Logger_Log(MakoString msg) {
    mako_trace_enter("Logger_Log", "examples/testing/actor_typed_payload_test.mko", 65);
    Logger_Log_Env __mako_st_32;
    memset(&__mako_st_32, 0, sizeof(__mako_st_32));
    MakoString __mako_cloned_33 = mako_str_clone(msg);
    __mako_st_32.msg = __mako_cloned_33;
    Logger_Log_Env *__mako_abox_34 = (Logger_Log_Env*)malloc(sizeof(Logger_Log_Env));
    *__mako_abox_34 = __mako_st_32;
    int64_t __mako_retv_35 = mako_actor_pack(1, (int64_t)(intptr_t)__mako_abox_34);
    mako_trace_exit("Logger_Log");
    return __mako_retv_35;
}

#line 1 "<mako-codegen>"
int64_t Logger_Bye(void) {
    mako_trace_enter("Logger_Bye", "examples/testing/actor_typed_payload_test.mko", 65);
    int64_t __mako_retv_36 = mako_actor_pack(2, 0);
    mako_trace_exit("Logger_Bye");
    return __mako_retv_36;
}

#line 1 "<mako-codegen>"
MakoChan* Logger_spawn(void) {
    mako_trace_enter("Logger_spawn", "examples/testing/actor_typed_payload_test.mko", 65);
    MakoActor *__mako_act_37 = mako_actor_spawn(16);
    mako_trace_exit("Logger_spawn");
    return __mako_act_37;
}

#line 1 "<mako-codegen>"
bool Logger_send(MakoChan* __mbox, int64_t __tag) {
    mako_trace_enter("Logger_send", "examples/testing/actor_typed_payload_test.mko", 65);
    bool __mako_as_38 = mako_actor_send(__mbox, __tag) != 0;
    mako_trace_exit("Logger_send");
    return __mako_as_38;
}

#line 1 "<mako-codegen>"
int64_t Logger_loop(MakoChan* __mbox) {
    mako_trace_enter("Logger_loop", "examples/testing/actor_typed_payload_test.mko", 65);
    int64_t __run = 1;
    Logger_State __mako_st_39;
    memset(&__mako_st_39, 0, sizeof(__mako_st_39));
    __mako_st_39.count = 0;
    Logger_State __st = __mako_st_39;
    while (1) {
        if (!((__run == 1))) break;
        int64_t __mako_ar_40 = mako_actor_recv(__mbox);
        int64_t __m = __mako_ar_40;
        int64_t __tag = mako_actor_msg_tag(__m);
        int64_t __pl = mako_actor_msg_payload(__m);
        if ((__tag == 1)) {
            Logger_Log_Env *__mako_envp_41 = (Logger_Log_Env*)(intptr_t)(__pl & 0x0000ffffffffffffLL);
            Logger_Log_Env __env = *__mako_envp_41;
            free(__mako_envp_41);
            int64_t msg = __env.msg;
            __st.count = mako_wrap_add_i64(__st.count, 1);
            mako_str_free(__env.msg);
        }
        if ((__tag == 2)) {
            (void)(0);
            __run = 0;
        }
    }
    mako_actor_stop(__mbox);
    int64_t __mako_retv_42 = __st.count;
    mako_trace_exit("Logger_loop");
    return __mako_retv_42;
}

#line 1 "<mako-codegen>"
int64_t Calculator_Add(int64_t a, int64_t b) {
    mako_trace_enter("Calculator_Add", "examples/testing/actor_typed_payload_test.mko", 65);
    Calculator_Add_Env __mako_st_43;
    memset(&__mako_st_43, 0, sizeof(__mako_st_43));
    __mako_st_43.a = a;
    __mako_st_43.b = b;
    Calculator_Add_Env *__mako_abox_44 = (Calculator_Add_Env*)malloc(sizeof(Calculator_Add_Env));
    *__mako_abox_44 = __mako_st_43;
    int64_t __mako_retv_45 = mako_actor_pack(1, (int64_t)(intptr_t)__mako_abox_44);
    mako_trace_exit("Calculator_Add");
    return __mako_retv_45;
}

#line 1 "<mako-codegen>"
int64_t Calculator_MulAdd(int64_t x, int64_t y, int64_t z) {
    mako_trace_enter("Calculator_MulAdd", "examples/testing/actor_typed_payload_test.mko", 65);
    Calculator_MulAdd_Env __mako_st_46;
    memset(&__mako_st_46, 0, sizeof(__mako_st_46));
    __mako_st_46.x = x;
    __mako_st_46.y = y;
    __mako_st_46.z = z;
    Calculator_MulAdd_Env *__mako_abox_47 = (Calculator_MulAdd_Env*)malloc(sizeof(Calculator_MulAdd_Env));
    *__mako_abox_47 = __mako_st_46;
    int64_t __mako_retv_48 = mako_actor_pack(2, (int64_t)(intptr_t)__mako_abox_47);
    mako_trace_exit("Calculator_MulAdd");
    return __mako_retv_48;
}

#line 1 "<mako-codegen>"
int64_t Calculator_Bye(void) {
    mako_trace_enter("Calculator_Bye", "examples/testing/actor_typed_payload_test.mko", 65);
    int64_t __mako_retv_49 = mako_actor_pack(3, 0);
    mako_trace_exit("Calculator_Bye");
    return __mako_retv_49;
}

#line 1 "<mako-codegen>"
MakoChan* Calculator_spawn(void) {
    mako_trace_enter("Calculator_spawn", "examples/testing/actor_typed_payload_test.mko", 65);
    MakoActor *__mako_act_50 = mako_actor_spawn(16);
    mako_trace_exit("Calculator_spawn");
    return __mako_act_50;
}

#line 1 "<mako-codegen>"
bool Calculator_send(MakoChan* __mbox, int64_t __tag) {
    mako_trace_enter("Calculator_send", "examples/testing/actor_typed_payload_test.mko", 65);
    bool __mako_as_51 = mako_actor_send(__mbox, __tag) != 0;
    mako_trace_exit("Calculator_send");
    return __mako_as_51;
}

#line 1 "<mako-codegen>"
int64_t Calculator_loop(MakoChan* __mbox) {
    mako_trace_enter("Calculator_loop", "examples/testing/actor_typed_payload_test.mko", 65);
    int64_t __run = 1;
    Calculator_State __mako_st_52;
    memset(&__mako_st_52, 0, sizeof(__mako_st_52));
    __mako_st_52.result = 0;
    Calculator_State __st = __mako_st_52;
    while (1) {
        if (!((__run == 1))) break;
        int64_t __mako_ar_53 = mako_actor_recv(__mbox);
        int64_t __m = __mako_ar_53;
        int64_t __tag = mako_actor_msg_tag(__m);
        int64_t __pl = mako_actor_msg_payload(__m);
        if ((__tag == 1)) {
            Calculator_Add_Env *__mako_envp_54 = (Calculator_Add_Env*)(intptr_t)(__pl & 0x0000ffffffffffffLL);
            Calculator_Add_Env __env = *__mako_envp_54;
            free(__mako_envp_54);
            int64_t a = __env.a;
            int64_t b = __env.b;
            __st.result = mako_wrap_add_i64(a, b);
        }
        if ((__tag == 2)) {
            Calculator_MulAdd_Env *__mako_envp_55 = (Calculator_MulAdd_Env*)(intptr_t)(__pl & 0x0000ffffffffffffLL);
            Calculator_MulAdd_Env __env = *__mako_envp_55;
            free(__mako_envp_55);
            int64_t x = __env.x;
            int64_t y = __env.y;
            int64_t z = __env.z;
            __st.result = mako_wrap_add_i64(mako_wrap_mul_i64(x, y), z);
        }
        if ((__tag == 3)) {
            (void)(0);
            __run = 0;
        }
    }
    mako_actor_stop(__mbox);
    mako_trace_exit("Calculator_loop");
    return 0;
}

#line 1 "<mako-codegen>"

int main(int argc, char **argv) {
    mako_set_args(argc, argv);
    mako_main();
    return 0;
}
