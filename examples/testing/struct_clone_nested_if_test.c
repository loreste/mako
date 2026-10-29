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

struct MakoArr_Route;
typedef struct MakoArr_Route MakoArr_Route;
typedef struct Route {
    MakoString path;
    MakoString method;
    MakoString backend;
    MakoString host;
    MakoString prefix;
    MakoString rewrite;
    MakoString name;
} Route;
static inline bool mako_eq_Route(Route a, Route b) {
    return mako_str_eq(a.path, b.path) && mako_str_eq(a.method, b.method) && mako_str_eq(a.backend, b.backend) && mako_str_eq(a.host, b.host) && mako_str_eq(a.prefix, b.prefix) && mako_str_eq(a.rewrite, b.rewrite) && mako_str_eq(a.name, b.name);
}
static inline uint64_t mako_hash_Route(Route k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= mako_hash_bytes(k.path.data, k.path.len); h *= 1099511628211ULL;
    h ^= mako_hash_bytes(k.method.data, k.method.len); h *= 1099511628211ULL;
    h ^= mako_hash_bytes(k.backend.data, k.backend.len); h *= 1099511628211ULL;
    h ^= mako_hash_bytes(k.host.data, k.host.len); h *= 1099511628211ULL;
    h ^= mako_hash_bytes(k.prefix.data, k.prefix.len); h *= 1099511628211ULL;
    h ^= mako_hash_bytes(k.rewrite.data, k.rewrite.len); h *= 1099511628211ULL;
    h ^= mako_hash_bytes(k.name.data, k.name.len); h *= 1099511628211ULL;
    return h;
}
static inline void mako_struct_Route_drop_fields(Route *p) {
    if (!p) return;
    mako_str_free(p->path);
    mako_str_free(p->method);
    mako_str_free(p->backend);
    mako_str_free(p->host);
    mako_str_free(p->prefix);
    mako_str_free(p->rewrite);
    mako_str_free(p->name);
}
typedef struct MakoArr_Route {
    Route *data;
    size_t len;
    size_t cap;
} MakoArr_Route;
static inline MakoArr_Route mako_arr_Route_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Route a;
    a.data = (Route *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(Route));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_Route_free(MakoArr_Route a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
        for (size_t i = 0; i < a.len; i++) {
            mako_str_free(a.data[i].path);
            mako_str_free(a.data[i].method);
            mako_str_free(a.data[i].backend);
            mako_str_free(a.data[i].host);
            mako_str_free(a.data[i].prefix);
            mako_str_free(a.data[i].rewrite);
            mako_str_free(a.data[i].name);
        }
    }
    mako_rc_release(a.data);
}
static inline MakoArr_Route mako_arr_Route_clone(MakoArr_Route a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_Route_len(MakoArr_Route a) { return (int64_t)a.len; }
static inline int64_t mako_arr_Route_cap(MakoArr_Route a) { return (int64_t)a.cap; }
static inline Route mako_arr_Route_get(MakoArr_Route a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline Route* mako_arr_Route_get_ptr(const MakoArr_Route *a, int64_t i) {
    if (MAKO_UNLIKELY(i < 0 || (size_t)i >= a->len)) mako_abort("struct slice index out of bounds");
    return &a->data[i];
}
static inline void mako_arr_Route_set(MakoArr_Route a, int64_t i, Route v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    Route old = a.data[i];
    a.data[i] = v;
    if (1) { mako_str_free(old.path); }
    if (1) { mako_str_free(old.method); }
    if (1) { mako_str_free(old.backend); }
    if (1) { mako_str_free(old.host); }
    if (1) { mako_str_free(old.prefix); }
    if (1) { mako_str_free(old.rewrite); }
    if (1) { mako_str_free(old.name); }
}
static inline MakoArr_Route mako_arr_Route_append(MakoArr_Route s, Route v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Route *nd = (Route *)mako_rc_alloc(ncap * sizeof(Route));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Route));
        for (size_t i = 0; i < s.len; i++) {
            nd[i].path = mako_str_clone(nd[i].path);
            nd[i].method = mako_str_clone(nd[i].method);
            nd[i].backend = mako_str_clone(nd[i].backend);
            nd[i].host = mako_str_clone(nd[i].host);
            nd[i].prefix = mako_str_clone(nd[i].prefix);
            nd[i].rewrite = mako_str_clone(nd[i].rewrite);
            nd[i].name = mako_str_clone(nd[i].name);
        }
        if (!mako_rc_shared(s.data)) {
            for (size_t i = 0; i < s.len; i++) {
                mako_str_free(s.data[i].path);
                mako_str_free(s.data[i].method);
                mako_str_free(s.data[i].backend);
                mako_str_free(s.data[i].host);
                mako_str_free(s.data[i].prefix);
                mako_str_free(s.data[i].rewrite);
                mako_str_free(s.data[i].name);
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
static inline MakoArr_Route mako_arr_Route_arena_append(MakoArena *arena, MakoArr_Route s, Route v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Route *nd = (Route *)mako_arena_alloc(arena, ncap * sizeof(Route));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Route));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_Route mako_arr_Route_of(const Route *vals, size_t n) {
    MakoArr_Route a = mako_arr_Route_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(Route));
    return a;
}
static inline MakoArr_Route mako_arr_Route_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Route a;
    a.data = (Route *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(Route));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(Route));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_Route {
    MakoArr_Route *data;
    size_t len;
    size_t cap;
} MakoArr_arr_Route;
static inline MakoArr_arr_Route mako_arr_arr_Route_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_Route a;
    a.data = (MakoArr_Route *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_Route));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_Route_free(MakoArr_arr_Route a) {
    if (!(a.cap > 0 && a.data)) return;
    if (mako_rc_release_last(a.data)) {
        free((char *)a.data - MAKO_RC_HEADER);
    }
}
static inline MakoArr_arr_Route mako_arr_arr_Route_clone(MakoArr_arr_Route a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_arr_Route_len(MakoArr_arr_Route a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_Route_cap(MakoArr_arr_Route a) { return (int64_t)a.cap; }
static inline MakoArr_Route mako_arr_arr_Route_get(MakoArr_arr_Route a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_Route_set(MakoArr_arr_Route a, int64_t i, MakoArr_Route v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    MakoArr_Route old = a.data[i];
    a.data[i] = v;
    if (old.data != v.data) { mako_arr_Route_free(old); }
}
static inline MakoArr_arr_Route mako_arr_arr_Route_append(MakoArr_arr_Route s, MakoArr_Route v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_Route *nd = (MakoArr_Route *)mako_rc_alloc(ncap * sizeof(MakoArr_Route));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_Route));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_Route mako_arr_arr_Route_of(const MakoArr_Route *vals, size_t n) {
    MakoArr_arr_Route a = mako_arr_arr_Route_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_Route));
    return a;
}
static inline MakoArr_arr_Route mako_arr_arr_Route_slice_expr(MakoArr_arr_Route s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_Route out;
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

typedef struct World {
    MakoString name;
    MakoString mode;
    MakoArr_Route routes;
    int64_t workers;
} World;
static inline bool mako_eq_World(World a, World b) {
    return mako_str_eq(a.name, b.name) && mako_str_eq(a.mode, b.mode) && ((a.routes.data == b.routes.data) && (a.routes.len == b.routes.len)) && (a.workers == b.workers);
}
static inline uint64_t mako_hash_World(World k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= mako_hash_bytes(k.name.data, k.name.len); h *= 1099511628211ULL;
    h ^= mako_hash_bytes(k.mode.data, k.mode.len); h *= 1099511628211ULL;
    h ^= (uint64_t)(uintptr_t)k.routes.data; h *= 1099511628211ULL; h ^= (uint64_t)k.routes.len; h *= 1099511628211ULL;
    h ^= mako_hash_i64((int64_t)k.workers); h *= 1099511628211ULL;
    return h;
}
static inline void mako_struct_World_drop_fields(World *p) {
    if (!p) return;
    mako_str_free(p->name);
    mako_str_free(p->mode);
    mako_arr_Route_free(p->routes);
}
typedef struct MakoArr_World {
    World *data;
    size_t len;
    size_t cap;
} MakoArr_World;
static inline MakoArr_World mako_arr_World_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_World a;
    a.data = (World *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(World));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_World_free(MakoArr_World a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
        for (size_t i = 0; i < a.len; i++) {
            mako_str_free(a.data[i].name);
            mako_str_free(a.data[i].mode);
            mako_arr_Route_free(a.data[i].routes);
        }
    }
    mako_rc_release(a.data);
}
static inline MakoArr_World mako_arr_World_clone(MakoArr_World a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_World_len(MakoArr_World a) { return (int64_t)a.len; }
static inline int64_t mako_arr_World_cap(MakoArr_World a) { return (int64_t)a.cap; }
static inline World mako_arr_World_get(MakoArr_World a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline World* mako_arr_World_get_ptr(const MakoArr_World *a, int64_t i) {
    if (MAKO_UNLIKELY(i < 0 || (size_t)i >= a->len)) mako_abort("struct slice index out of bounds");
    return &a->data[i];
}
static inline void mako_arr_World_set(MakoArr_World a, int64_t i, World v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    World old = a.data[i];
    a.data[i] = v;
    if (1) { mako_str_free(old.name); }
    if (1) { mako_str_free(old.mode); }
    if (old.routes.data != v.routes.data) { mako_arr_Route_free(old.routes); } else if (old.routes.cap > 0 && old.routes.data && mako_rc_shared(old.routes.data)) { mako_rc_release(old.routes.data); }
}
static inline MakoArr_World mako_arr_World_append(MakoArr_World s, World v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        World *nd = (World *)mako_rc_alloc(ncap * sizeof(World));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(World));
        for (size_t i = 0; i < s.len; i++) {
            nd[i].name = mako_str_clone(nd[i].name);
            nd[i].mode = mako_str_clone(nd[i].mode);
            nd[i].routes = mako_arr_Route_clone(nd[i].routes);
        }
        if (!mako_rc_shared(s.data)) {
            for (size_t i = 0; i < s.len; i++) {
                mako_str_free(s.data[i].name);
                mako_str_free(s.data[i].mode);
                mako_arr_Route_free(s.data[i].routes);
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
static inline MakoArr_World mako_arr_World_arena_append(MakoArena *arena, MakoArr_World s, World v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        World *nd = (World *)mako_arena_alloc(arena, ncap * sizeof(World));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(World));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_World mako_arr_World_of(const World *vals, size_t n) {
    MakoArr_World a = mako_arr_World_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(World));
    return a;
}
static inline MakoArr_World mako_arr_World_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_World a;
    a.data = (World *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(World));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(World));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_World {
    MakoArr_World *data;
    size_t len;
    size_t cap;
} MakoArr_arr_World;
static inline MakoArr_arr_World mako_arr_arr_World_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_World a;
    a.data = (MakoArr_World *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_World));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_World_free(MakoArr_arr_World a) {
    if (!(a.cap > 0 && a.data)) return;
    if (mako_rc_release_last(a.data)) {
        free((char *)a.data - MAKO_RC_HEADER);
    }
}
static inline MakoArr_arr_World mako_arr_arr_World_clone(MakoArr_arr_World a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_arr_World_len(MakoArr_arr_World a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_World_cap(MakoArr_arr_World a) { return (int64_t)a.cap; }
static inline MakoArr_World mako_arr_arr_World_get(MakoArr_arr_World a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_World_set(MakoArr_arr_World a, int64_t i, MakoArr_World v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    MakoArr_World old = a.data[i];
    a.data[i] = v;
    if (old.data != v.data) { mako_arr_World_free(old); }
}
static inline MakoArr_arr_World mako_arr_arr_World_append(MakoArr_arr_World s, MakoArr_World v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_World *nd = (MakoArr_World *)mako_rc_alloc(ncap * sizeof(MakoArr_World));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_World));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_World mako_arr_arr_World_of(const MakoArr_World *vals, size_t n) {
    MakoArr_arr_World a = mako_arr_arr_World_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_World));
    return a;
}
static inline MakoArr_arr_World mako_arr_arr_World_slice_expr(MakoArr_arr_World s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_World out;
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

static void __attribute__((constructor)) __mako_reflect_reg_Route(void) {
    (void)mako_reflect_register_type("Route", "path:string,method:string,backend:string,host:string,prefix:string,rewrite:string,name:string");
}

static void __attribute__((constructor)) __mako_reflect_reg_World(void) {
    (void)mako_reflect_register_type("World", "name:string,mode:string,routes:[]Route,workers:int");
}

#line 1 "examples/testing/struct_clone_nested_if_test.mko"
static inline Route route_new(MakoString path, MakoString method, MakoString backend);
static inline World parse_line(World w, MakoString line);
int64_t doctor_world(World *w);
void TestStructCloneNestedIf(void);
void TestRepeatedCloneCycle(void);
static inline int64_t count_routes(World *w);
void TestByValueCloneIntegrity(void);
void TestChainedIndexFieldAssign(void);
World mutate_world(World w);
void TestMutParamDeepClone(void);
void TestStressCloneDrop(void);
static inline int64_t add(int64_t a, int64_t b);
static inline int64_t mul(int64_t a, int64_t b);
static inline MakoIntArray append_one_memory(MakoIntArray xs, int64_t value);
void mako_main(void);
static inline int64_t double_it(int64_t n);

/*__MAKO_HELPERS__*/

#line 1 "<mako-codegen>"
static inline Route route_new(MakoString path, MakoString method, MakoString backend) {
#line 17 "examples/testing/struct_clone_nested_if_test.mko"
    Route __mako_st_0;
    MakoString __mako_cloned_1 = mako_str_clone(path);
    __mako_st_0.path = __mako_cloned_1;
    MakoString __mako_cloned_2 = mako_str_clone(method);
    __mako_st_0.method = __mako_cloned_2;
    MakoString __mako_cloned_3 = mako_str_clone(backend);
    __mako_st_0.backend = __mako_cloned_3;
    __mako_st_0.host = mako_str_from_cstr("");
    __mako_st_0.prefix = mako_str_from_cstr("");
    __mako_st_0.rewrite = mako_str_from_cstr("");
    __mako_st_0.name = mako_str_from_cstr("");
    mako_trace_exit("route_new");
    return __mako_st_0;
}

#line 1 "<mako-codegen>"
static inline World parse_line(World w, MakoString line) {
    w.name = mako_str_clone(w.name);
    w.mode = mako_str_clone(w.mode);
    w.routes = mako_arr_Route_clone(w.routes);
#line 36 "examples/testing/struct_clone_nested_if_test.mko"
    if (mako_str_eq(line, mako_str_view("route", 5))) {
#line 37 "examples/testing/struct_clone_nested_if_test.mko"
        Route __mako_r_4 = route_new(mako_str_view("/default", 8), mako_str_view("GET", 3), mako_str_view("app", 3));
        Route r = __mako_r_4;
#line 38 "examples/testing/struct_clone_nested_if_test.mko"
        Route __mako_cloned_6 = r;
        MakoString __mako_cloned_7 = mako_str_clone(__mako_cloned_6.path);
        __mako_cloned_6.path = __mako_cloned_7;
        MakoString __mako_cloned_8 = mako_str_clone(__mako_cloned_6.method);
        __mako_cloned_6.method = __mako_cloned_8;
        MakoString __mako_cloned_9 = mako_str_clone(__mako_cloned_6.backend);
        __mako_cloned_6.backend = __mako_cloned_9;
        MakoString __mako_cloned_10 = mako_str_clone(__mako_cloned_6.host);
        __mako_cloned_6.host = __mako_cloned_10;
        MakoString __mako_cloned_11 = mako_str_clone(__mako_cloned_6.prefix);
        __mako_cloned_6.prefix = __mako_cloned_11;
        MakoString __mako_cloned_12 = mako_str_clone(__mako_cloned_6.rewrite);
        __mako_cloned_6.rewrite = __mako_cloned_12;
        MakoString __mako_cloned_13 = mako_str_clone(__mako_cloned_6.name);
        __mako_cloned_6.name = __mako_cloned_13;
        MakoArr_Route __mako_ap_5 = mako_arr_Route_append(w.routes, __mako_cloned_6);
        w.routes = __mako_ap_5;
        mako_str_free(r.path);
        mako_str_free(r.method);
        mako_str_free(r.backend);
        mako_str_free(r.host);
        mako_str_free(r.prefix);
        mako_str_free(r.rewrite);
        mako_str_free(r.name);
    } else {
        if (mako_str_eq(line, mako_str_view("route2", 6))) {
#line 40 "examples/testing/struct_clone_nested_if_test.mko"
            Route __mako_r_14 = route_new(mako_str_view("/api", 4), mako_str_view("POST", 4), mako_str_view("api", 3));
            Route r = __mako_r_14;
#line 41 "examples/testing/struct_clone_nested_if_test.mko"
            Route __mako_cloned_16 = r;
            MakoString __mako_cloned_17 = mako_str_clone(__mako_cloned_16.path);
            __mako_cloned_16.path = __mako_cloned_17;
            MakoString __mako_cloned_18 = mako_str_clone(__mako_cloned_16.method);
            __mako_cloned_16.method = __mako_cloned_18;
            MakoString __mako_cloned_19 = mako_str_clone(__mako_cloned_16.backend);
            __mako_cloned_16.backend = __mako_cloned_19;
            MakoString __mako_cloned_20 = mako_str_clone(__mako_cloned_16.host);
            __mako_cloned_16.host = __mako_cloned_20;
            MakoString __mako_cloned_21 = mako_str_clone(__mako_cloned_16.prefix);
            __mako_cloned_16.prefix = __mako_cloned_21;
            MakoString __mako_cloned_22 = mako_str_clone(__mako_cloned_16.rewrite);
            __mako_cloned_16.rewrite = __mako_cloned_22;
            MakoString __mako_cloned_23 = mako_str_clone(__mako_cloned_16.name);
            __mako_cloned_16.name = __mako_cloned_23;
            MakoArr_Route __mako_ap_15 = mako_arr_Route_append(w.routes, __mako_cloned_16);
            w.routes = __mako_ap_15;
            mako_str_free(r.path);
            mako_str_free(r.method);
            mako_str_free(r.backend);
            mako_str_free(r.host);
            mako_str_free(r.prefix);
            mako_str_free(r.rewrite);
            mako_str_free(r.name);
        } else {
            if (mako_str_eq(line, mako_str_view("route3", 6))) {
#line 43 "examples/testing/struct_clone_nested_if_test.mko"
                Route __mako_r_24 = route_new(mako_str_view("/health", 7), mako_str_view("GET", 3), mako_str_view("health", 6));
                Route r = __mako_r_24;
#line 44 "examples/testing/struct_clone_nested_if_test.mko"
                Route __mako_cloned_26 = r;
                MakoString __mako_cloned_27 = mako_str_clone(__mako_cloned_26.path);
                __mako_cloned_26.path = __mako_cloned_27;
                MakoString __mako_cloned_28 = mako_str_clone(__mako_cloned_26.method);
                __mako_cloned_26.method = __mako_cloned_28;
                MakoString __mako_cloned_29 = mako_str_clone(__mako_cloned_26.backend);
                __mako_cloned_26.backend = __mako_cloned_29;
                MakoString __mako_cloned_30 = mako_str_clone(__mako_cloned_26.host);
                __mako_cloned_26.host = __mako_cloned_30;
                MakoString __mako_cloned_31 = mako_str_clone(__mako_cloned_26.prefix);
                __mako_cloned_26.prefix = __mako_cloned_31;
                MakoString __mako_cloned_32 = mako_str_clone(__mako_cloned_26.rewrite);
                __mako_cloned_26.rewrite = __mako_cloned_32;
                MakoString __mako_cloned_33 = mako_str_clone(__mako_cloned_26.name);
                __mako_cloned_26.name = __mako_cloned_33;
                MakoArr_Route __mako_ap_25 = mako_arr_Route_append(w.routes, __mako_cloned_26);
                w.routes = __mako_ap_25;
                mako_str_free(r.path);
                mako_str_free(r.method);
                mako_str_free(r.backend);
                mako_str_free(r.host);
                mako_str_free(r.prefix);
                mako_str_free(r.rewrite);
                mako_str_free(r.name);
            } else {
                if (mako_str_eq(line, mako_str_view("workers", 7))) {
#line 46 "examples/testing/struct_clone_nested_if_test.mko"
                    w.workers = 4;
                } else {
                    if (mako_str_eq(line, mako_str_view("mode", 4))) {
#line 48 "examples/testing/struct_clone_nested_if_test.mko"
                        MakoString __mako_old_own_34 = w.mode;
                        w.mode = mako_str_from_cstr("http");
                        if (1) mako_str_free(__mako_old_own_34);
                    } else {
                        if (mako_str_eq(line, mako_str_view("name", 4))) {
#line 50 "examples/testing/struct_clone_nested_if_test.mko"
                            MakoString __mako_old_own_35 = w.name;
                            w.name = mako_str_from_cstr("test");
                            if (1) mako_str_free(__mako_old_own_35);
                        }
                    }
                }
            }
        }
    }
#line 52 "examples/testing/struct_clone_nested_if_test.mko"
    mako_trace_exit("parse_line");
    return w;
}

#line 1 "<mako-codegen>"
int64_t doctor_world(World *w) {
    mako_trace_enter("doctor_world", "examples/testing/struct_clone_nested_if_test.mko", 52);
#line 57 "examples/testing/struct_clone_nested_if_test.mko"
    int64_t errors = 0;
#line 58 "examples/testing/struct_clone_nested_if_test.mko"
    if ((mako_str_len(w->name) == 0)) {
#line 59 "examples/testing/struct_clone_nested_if_test.mko"
        errors = mako_wrap_add_i64(errors, 1);
    }
#line 61 "examples/testing/struct_clone_nested_if_test.mko"
    if ((mako_arr_Route_len(w->routes) == 0)) {
#line 62 "examples/testing/struct_clone_nested_if_test.mko"
        errors = mako_wrap_add_i64(errors, 1);
    }
#line 64 "examples/testing/struct_clone_nested_if_test.mko"
    MakoArr_Route __mako_iter_36 = w->routes;
    for (size_t __mako_ri_37 = 0; __mako_ri_37 < __mako_iter_36.len; __mako_ri_37++) {
        int64_t i = (int64_t)__mako_ri_37;
        Route r = mako_arr_Route_get(__mako_iter_36, (int64_t)__mako_ri_37);
#line 65 "examples/testing/struct_clone_nested_if_test.mko"
        if ((mako_str_len(r.path) == 0)) {
#line 66 "examples/testing/struct_clone_nested_if_test.mko"
            errors = mako_wrap_add_i64(errors, 1);
        }
#line 68 "examples/testing/struct_clone_nested_if_test.mko"
        if ((mako_str_len(r.backend) == 0)) {
#line 69 "examples/testing/struct_clone_nested_if_test.mko"
            errors = mako_wrap_add_i64(errors, 1);
        }
    }
#line 72 "examples/testing/struct_clone_nested_if_test.mko"
    mako_trace_exit("doctor_world");
    return errors;
}

#line 1 "<mako-codegen>"
void TestStructCloneNestedIf(void) {
    mako_trace_enter("TestStructCloneNestedIf", "examples/testing/struct_clone_nested_if_test.mko", 72);
#line 76 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_st_38;
    __mako_st_38.name = mako_str_from_cstr("");
    __mako_st_38.mode = mako_str_from_cstr("");
    MakoArr_Route __mako_mk_39 = mako_arr_Route_make(0, 4);
    __mako_st_38.routes = __mako_mk_39;
    __mako_st_38.workers = 0;
    World w = __mako_st_38;
#line 82 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_40 = parse_line(w, mako_str_view("name", 4));
    World __mako_old_st_41 = w;
    w = __mako_r_40;
    if (1) { mako_str_free(__mako_old_st_41.name); }
    if (1) { mako_str_free(__mako_old_st_41.mode); }
    if (__mako_old_st_41.routes.data != w.routes.data) { mako_arr_Route_free(__mako_old_st_41.routes); }
#line 83 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_42 = parse_line(w, mako_str_view("mode", 4));
    World __mako_old_st_43 = w;
    w = __mako_r_42;
    if (1) { mako_str_free(__mako_old_st_43.name); }
    if (1) { mako_str_free(__mako_old_st_43.mode); }
    if (__mako_old_st_43.routes.data != w.routes.data) { mako_arr_Route_free(__mako_old_st_43.routes); }
#line 84 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_44 = parse_line(w, mako_str_view("route", 5));
    World __mako_old_st_45 = w;
    w = __mako_r_44;
    if (1) { mako_str_free(__mako_old_st_45.name); }
    if (1) { mako_str_free(__mako_old_st_45.mode); }
    if (__mako_old_st_45.routes.data != w.routes.data) { mako_arr_Route_free(__mako_old_st_45.routes); }
#line 85 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_46 = parse_line(w, mako_str_view("route2", 6));
    World __mako_old_st_47 = w;
    w = __mako_r_46;
    if (1) { mako_str_free(__mako_old_st_47.name); }
    if (1) { mako_str_free(__mako_old_st_47.mode); }
    if (__mako_old_st_47.routes.data != w.routes.data) { mako_arr_Route_free(__mako_old_st_47.routes); }
#line 86 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_48 = parse_line(w, mako_str_view("route3", 6));
    World __mako_old_st_49 = w;
    w = __mako_r_48;
    if (1) { mako_str_free(__mako_old_st_49.name); }
    if (1) { mako_str_free(__mako_old_st_49.mode); }
    if (__mako_old_st_49.routes.data != w.routes.data) { mako_arr_Route_free(__mako_old_st_49.routes); }
#line 87 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_50 = parse_line(w, mako_str_view("workers", 7));
    World __mako_old_st_51 = w;
    w = __mako_r_50;
    if (1) { mako_str_free(__mako_old_st_51.name); }
    if (1) { mako_str_free(__mako_old_st_51.mode); }
    if (__mako_old_st_51.routes.data != w.routes.data) { mako_arr_Route_free(__mako_old_st_51.routes); }
#line 90 "examples/testing/struct_clone_nested_if_test.mko"
    int64_t __mako_r_52 = doctor_world(&w);
    int64_t errors = __mako_r_52;
#line 91 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((errors == 0));
#line 92 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((mako_arr_Route_len(w.routes) == 3));
#line 93 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(0 < 0 || (size_t)0 >= (w.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_53 = &(w.routes).data[0];
    mako_assert(mako_str_eq(__mako_sp_53->path, mako_str_view("/default", 8)));
#line 94 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(1 < 0 || (size_t)1 >= (w.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_54 = &(w.routes).data[1];
    mako_assert(mako_str_eq(__mako_sp_54->path, mako_str_view("/api", 4)));
#line 95 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(2 < 0 || (size_t)2 >= (w.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_55 = &(w.routes).data[2];
    mako_assert(mako_str_eq(__mako_sp_55->path, mako_str_view("/health", 7)));
#line 96 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert(mako_str_eq(w.name, mako_str_view("test", 4)));
#line 97 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((w.workers == 4));
    mako_str_free(w.name);
    mako_str_free(w.mode);
    mako_arr_Route_free(w.routes);
    mako_trace_exit("TestStructCloneNestedIf");
}

#line 1 "<mako-codegen>"
void TestRepeatedCloneCycle(void) {
    mako_trace_enter("TestRepeatedCloneCycle", "examples/testing/struct_clone_nested_if_test.mko", 97);
#line 101 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_st_56;
    __mako_st_56.name = mako_str_from_cstr("base");
    __mako_st_56.mode = mako_str_from_cstr("tcp");
    MakoArr_Route __mako_mk_57 = mako_arr_Route_make(0, 4);
    __mako_st_56.routes = __mako_mk_57;
    __mako_st_56.workers = 1;
    World w = __mako_st_56;
#line 108 "examples/testing/struct_clone_nested_if_test.mko"
    for (int64_t i = 0; i < 20; i++) {
#line 109 "examples/testing/struct_clone_nested_if_test.mko"
        World __mako_r_58 = parse_line(w, mako_str_view("route", 5));
        World __mako_old_st_59 = w;
        w = __mako_r_58;
        if (1) { mako_str_free(__mako_old_st_59.name); }
        if (1) { mako_str_free(__mako_old_st_59.mode); }
        if (__mako_old_st_59.routes.data != w.routes.data) { mako_arr_Route_free(__mako_old_st_59.routes); }
#line 110 "examples/testing/struct_clone_nested_if_test.mko"
        int64_t __mako_r_60 = doctor_world(&w);
        int64_t e = __mako_r_60;
#line 111 "examples/testing/struct_clone_nested_if_test.mko"
        mako_assert((e == 0));
    }
#line 113 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((mako_arr_Route_len(w.routes) == 20));
    mako_str_free(w.name);
    mako_str_free(w.mode);
    mako_arr_Route_free(w.routes);
    mako_trace_exit("TestRepeatedCloneCycle");
}

#line 1 "<mako-codegen>"
static inline int64_t count_routes(World *w) {
#line 119 "examples/testing/struct_clone_nested_if_test.mko"
    int64_t __mako_retv_61 = mako_arr_Route_len(w->routes);
    mako_trace_exit("count_routes");
    return __mako_retv_61;
}

#line 1 "<mako-codegen>"
void TestByValueCloneIntegrity(void) {
    mako_trace_enter("TestByValueCloneIntegrity", "examples/testing/struct_clone_nested_if_test.mko", 119);
#line 123 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_st_62;
    __mako_st_62.name = mako_str_from_cstr("original");
    __mako_st_62.mode = mako_str_from_cstr("http");
    MakoArr_Route __mako_mk_63 = mako_arr_Route_make(0, 4);
    __mako_st_62.routes = __mako_mk_63;
    __mako_st_62.workers = 2;
    World orig = __mako_st_62;
#line 129 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_64 = parse_line(orig, mako_str_view("route", 5));
    World __mako_old_st_65 = orig;
    orig = __mako_r_64;
    if (1) { mako_str_free(__mako_old_st_65.name); }
    if (1) { mako_str_free(__mako_old_st_65.mode); }
    if (__mako_old_st_65.routes.data != orig.routes.data) { mako_arr_Route_free(__mako_old_st_65.routes); }
#line 130 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_66 = parse_line(orig, mako_str_view("route2", 6));
    World __mako_old_st_67 = orig;
    orig = __mako_r_66;
    if (1) { mako_str_free(__mako_old_st_67.name); }
    if (1) { mako_str_free(__mako_old_st_67.mode); }
    if (__mako_old_st_67.routes.data != orig.routes.data) { mako_arr_Route_free(__mako_old_st_67.routes); }
#line 133 "examples/testing/struct_clone_nested_if_test.mko"
    int64_t __mako_r_68 = count_routes(&orig);
    int64_t n = __mako_r_68;
#line 134 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((n == 2));
#line 135 "examples/testing/struct_clone_nested_if_test.mko"
    int64_t __mako_r_69 = doctor_world(&orig);
    int64_t e = __mako_r_69;
#line 136 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((e == 0));
#line 139 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert(mako_str_eq(orig.name, mako_str_view("original", 8)));
#line 140 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert(mako_str_eq(orig.mode, mako_str_view("http", 4)));
#line 141 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((orig.workers == 2));
#line 142 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((mako_arr_Route_len(orig.routes) == 2));
#line 143 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(0 < 0 || (size_t)0 >= (orig.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_70 = &(orig.routes).data[0];
    mako_assert(mako_str_eq(__mako_sp_70->path, mako_str_view("/default", 8)));
#line 144 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(1 < 0 || (size_t)1 >= (orig.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_71 = &(orig.routes).data[1];
    mako_assert(mako_str_eq(__mako_sp_71->path, mako_str_view("/api", 4)));
    mako_str_free(orig.name);
    mako_str_free(orig.mode);
    mako_arr_Route_free(orig.routes);
    mako_trace_exit("TestByValueCloneIntegrity");
}

#line 1 "<mako-codegen>"
void TestChainedIndexFieldAssign(void) {
    mako_trace_enter("TestChainedIndexFieldAssign", "examples/testing/struct_clone_nested_if_test.mko", 144);
#line 149 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_st_72;
    __mako_st_72.name = mako_str_from_cstr("test");
    __mako_st_72.mode = mako_str_from_cstr("http");
    MakoArr_Route __mako_mk_73 = mako_arr_Route_make(0, 4);
    __mako_st_72.routes = __mako_mk_73;
    __mako_st_72.workers = 1;
    World w = __mako_st_72;
#line 155 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_74 = parse_line(w, mako_str_view("route", 5));
    World __mako_old_st_75 = w;
    w = __mako_r_74;
    if (1) { mako_str_free(__mako_old_st_75.name); }
    if (1) { mako_str_free(__mako_old_st_75.mode); }
    if (__mako_old_st_75.routes.data != w.routes.data) { mako_arr_Route_free(__mako_old_st_75.routes); }
#line 156 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_76 = parse_line(w, mako_str_view("route2", 6));
    World __mako_old_st_77 = w;
    w = __mako_r_76;
    if (1) { mako_str_free(__mako_old_st_77.name); }
    if (1) { mako_str_free(__mako_old_st_77.mode); }
    if (__mako_old_st_77.routes.data != w.routes.data) { mako_arr_Route_free(__mako_old_st_77.routes); }
#line 157 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_78 = parse_line(w, mako_str_view("route3", 6));
    World __mako_old_st_79 = w;
    w = __mako_r_78;
    if (1) { mako_str_free(__mako_old_st_79.name); }
    if (1) { mako_str_free(__mako_old_st_79.mode); }
    if (__mako_old_st_79.routes.data != w.routes.data) { mako_arr_Route_free(__mako_old_st_79.routes); }
#line 159 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(0 < 0 || (size_t)0 >= (w.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_80 = &(w.routes).data[0];
    mako_assert(mako_str_eq(__mako_sp_80->path, mako_str_view("/default", 8)));
#line 160 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(1 < 0 || (size_t)1 >= (w.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_81 = &(w.routes).data[1];
    mako_assert(mako_str_eq(__mako_sp_81->path, mako_str_view("/api", 4)));
#line 161 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(2 < 0 || (size_t)2 >= (w.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_82 = &(w.routes).data[2];
    mako_assert(mako_str_eq(__mako_sp_82->path, mako_str_view("/health", 7)));
#line 164 "examples/testing/struct_clone_nested_if_test.mko"
    MakoString __mako_old_own_83 = __mako_sp_80->path;
    __mako_sp_80->path = mako_str_from_cstr("/changed");
    if (1) mako_str_free(__mako_old_own_83);
#line 165 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(1 < 0 || (size_t)1 >= (w.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_84 = &(w.routes).data[1];
    MakoString __mako_old_own_85 = __mako_sp_84->method;
    __mako_sp_84->method = mako_str_from_cstr("PUT");
    if (1) mako_str_free(__mako_old_own_85);
#line 166 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(2 < 0 || (size_t)2 >= (w.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_86 = &(w.routes).data[2];
    MakoString __mako_old_own_87 = __mako_sp_86->backend;
    __mako_sp_86->backend = mako_str_from_cstr("new-backend");
    if (1) mako_str_free(__mako_old_own_87);
#line 168 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(0 < 0 || (size_t)0 >= (w.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_88 = &(w.routes).data[0];
    mako_assert(mako_str_eq(__mako_sp_88->path, mako_str_view("/changed", 8)));
#line 169 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(1 < 0 || (size_t)1 >= (w.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_89 = &(w.routes).data[1];
    mako_assert(mako_str_eq(__mako_sp_89->method, mako_str_view("PUT", 3)));
#line 170 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(2 < 0 || (size_t)2 >= (w.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_90 = &(w.routes).data[2];
    mako_assert(mako_str_eq(__mako_sp_90->backend, mako_str_view("new-backend", 11)));
#line 173 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert(mako_str_eq(__mako_sp_88->method, mako_str_view("GET", 3)));
#line 174 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert(mako_str_eq(__mako_sp_89->path, mako_str_view("/api", 4)));
#line 175 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert(mako_str_eq(__mako_sp_90->path, mako_str_view("/health", 7)));
    mako_str_free(w.name);
    mako_str_free(w.mode);
    mako_arr_Route_free(w.routes);
    mako_trace_exit("TestChainedIndexFieldAssign");
}

#line 1 "<mako-codegen>"
World mutate_world(World w) {
    w.name = mako_str_clone(w.name);
    w.mode = mako_str_clone(w.mode);
    w.routes = mako_arr_Route_clone(w.routes);
    mako_trace_enter("mutate_world", "examples/testing/struct_clone_nested_if_test.mko", 175);
#line 180 "examples/testing/struct_clone_nested_if_test.mko"
    MakoString __mako_old_own_91 = w.name;
    w.name = mako_str_from_cstr("mutated");
    if (1) mako_str_free(__mako_old_own_91);
#line 181 "examples/testing/struct_clone_nested_if_test.mko"
    MakoString __mako_old_own_92 = w.mode;
    w.mode = mako_str_from_cstr("changed");
    if (1) mako_str_free(__mako_old_own_92);
#line 182 "examples/testing/struct_clone_nested_if_test.mko"
    w.workers = 999;
#line 183 "examples/testing/struct_clone_nested_if_test.mko"
    Route __mako_r_93 = route_new(mako_str_view("/new", 4), mako_str_view("DELETE", 6), mako_str_view("nowhere", 7));
    Route r = __mako_r_93;
#line 184 "examples/testing/struct_clone_nested_if_test.mko"
    Route __mako_ident_move_95 = r;
    memset(&r, 0, sizeof(r));
    MakoArr_Route __mako_ap_94 = mako_arr_Route_append(w.routes, __mako_ident_move_95);
    w.routes = __mako_ap_94;
#line 185 "examples/testing/struct_clone_nested_if_test.mko"
    mako_trace_exit("mutate_world");
    return w;
}

#line 1 "<mako-codegen>"
void TestMutParamDeepClone(void) {
    mako_trace_enter("TestMutParamDeepClone", "examples/testing/struct_clone_nested_if_test.mko", 185);
#line 189 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_st_96;
    __mako_st_96.name = mako_str_from_cstr("original");
    __mako_st_96.mode = mako_str_from_cstr("http");
    MakoArr_Route __mako_mk_97 = mako_arr_Route_make(0, 4);
    __mako_st_96.routes = __mako_mk_97;
    __mako_st_96.workers = 2;
    World orig = __mako_st_96;
#line 195 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_98 = parse_line(orig, mako_str_view("route", 5));
    World __mako_old_st_99 = orig;
    orig = __mako_r_98;
    if (1) { mako_str_free(__mako_old_st_99.name); }
    if (1) { mako_str_free(__mako_old_st_99.mode); }
    if (__mako_old_st_99.routes.data != orig.routes.data) { mako_arr_Route_free(__mako_old_st_99.routes); }
#line 196 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_100 = parse_line(orig, mako_str_view("route2", 6));
    World __mako_old_st_101 = orig;
    orig = __mako_r_100;
    if (1) { mako_str_free(__mako_old_st_101.name); }
    if (1) { mako_str_free(__mako_old_st_101.mode); }
    if (__mako_old_st_101.routes.data != orig.routes.data) { mako_arr_Route_free(__mako_old_st_101.routes); }
#line 198 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_r_102 = mutate_world(orig);
    World cloned = __mako_r_102;
#line 201 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert(mako_str_eq(orig.name, mako_str_view("original", 8)));
#line 202 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert(mako_str_eq(orig.mode, mako_str_view("http", 4)));
#line 203 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((orig.workers == 2));
#line 204 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((mako_arr_Route_len(orig.routes) == 2));
#line 205 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(0 < 0 || (size_t)0 >= (orig.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_103 = &(orig.routes).data[0];
    mako_assert(mako_str_eq(__mako_sp_103->path, mako_str_view("/default", 8)));
#line 206 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(1 < 0 || (size_t)1 >= (orig.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_104 = &(orig.routes).data[1];
    mako_assert(mako_str_eq(__mako_sp_104->path, mako_str_view("/api", 4)));
#line 209 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert(mako_str_eq(cloned.name, mako_str_view("mutated", 7)));
#line 210 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((cloned.workers == 999));
#line 211 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((mako_arr_Route_len(cloned.routes) == 3));
#line 212 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(2 < 0 || (size_t)2 >= (cloned.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_105 = &(cloned.routes).data[2];
    mako_assert(mako_str_eq(__mako_sp_105->path, mako_str_view("/new", 4)));
    mako_str_free(cloned.name);
    mako_str_free(cloned.mode);
    mako_arr_Route_free(cloned.routes);
    mako_str_free(orig.name);
    mako_str_free(orig.mode);
    mako_arr_Route_free(orig.routes);
    mako_trace_exit("TestMutParamDeepClone");
}

#line 1 "<mako-codegen>"
void TestStressCloneDrop(void) {
    mako_trace_enter("TestStressCloneDrop", "examples/testing/struct_clone_nested_if_test.mko", 212);
#line 217 "examples/testing/struct_clone_nested_if_test.mko"
    World __mako_st_106;
    __mako_st_106.name = mako_str_from_cstr("stress");
    __mako_st_106.mode = mako_str_from_cstr("tcp");
    MakoArr_Route __mako_mk_107 = mako_arr_Route_make(0, 4);
    __mako_st_106.routes = __mako_mk_107;
    __mako_st_106.workers = 0;
    World w = __mako_st_106;
#line 223 "examples/testing/struct_clone_nested_if_test.mko"
    for (int64_t i = 0; i < 5; i++) {
#line 224 "examples/testing/struct_clone_nested_if_test.mko"
        World __mako_r_108 = parse_line(w, mako_str_view("route", 5));
        World __mako_old_st_109 = w;
        w = __mako_r_108;
        if (1) { mako_str_free(__mako_old_st_109.name); }
        if (1) { mako_str_free(__mako_old_st_109.mode); }
        if (__mako_old_st_109.routes.data != w.routes.data) { mako_arr_Route_free(__mako_old_st_109.routes); }
#line 225 "examples/testing/struct_clone_nested_if_test.mko"
        World __mako_r_110 = parse_line(w, mako_str_view("route2", 6));
        World __mako_old_st_111 = w;
        w = __mako_r_110;
        if (1) { mako_str_free(__mako_old_st_111.name); }
        if (1) { mako_str_free(__mako_old_st_111.mode); }
        if (__mako_old_st_111.routes.data != w.routes.data) { mako_arr_Route_free(__mako_old_st_111.routes); }
#line 226 "examples/testing/struct_clone_nested_if_test.mko"
        World __mako_r_112 = parse_line(w, mako_str_view("route3", 6));
        World __mako_old_st_113 = w;
        w = __mako_r_112;
        if (1) { mako_str_free(__mako_old_st_113.name); }
        if (1) { mako_str_free(__mako_old_st_113.mode); }
        if (__mako_old_st_113.routes.data != w.routes.data) { mako_arr_Route_free(__mako_old_st_113.routes); }
    }
#line 228 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((mako_arr_Route_len(w.routes) == 15));
#line 231 "examples/testing/struct_clone_nested_if_test.mko"
    for (int64_t i = 0; i < 50; i++) {
#line 232 "examples/testing/struct_clone_nested_if_test.mko"
        int64_t __mako_r_114 = doctor_world(&w);
        int64_t e = __mako_r_114;
#line 233 "examples/testing/struct_clone_nested_if_test.mko"
        mako_assert((e == 0));
    }
#line 237 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert(mako_str_eq(w.name, mako_str_view("stress", 6)));
#line 238 "examples/testing/struct_clone_nested_if_test.mko"
    mako_assert((mako_arr_Route_len(w.routes) == 15));
#line 239 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(0 < 0 || (size_t)0 >= (w.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_115 = &(w.routes).data[0];
    mako_assert(mako_str_eq(__mako_sp_115->path, mako_str_view("/default", 8)));
#line 240 "examples/testing/struct_clone_nested_if_test.mko"
    MAKO_BOUNDS_CHECK(14 < 0 || (size_t)14 >= (w.routes).len, "struct slice index out of bounds");
    Route *__mako_sp_116 = &(w.routes).data[14];
    mako_assert(mako_str_eq(__mako_sp_116->path, mako_str_view("/health", 7)));
    mako_str_free(w.name);
    mako_str_free(w.mode);
    mako_arr_Route_free(w.routes);
    mako_trace_exit("TestStressCloneDrop");
}

#line 1 "<mako-codegen>"
static inline int64_t add(int64_t a, int64_t b) {
#line 4 "/Users/loreste/mako/examples/testing/add.mko"
    int64_t __mako_retv_117 = mako_wrap_add_i64(a, b);
    mako_trace_exit("add");
    return __mako_retv_117;
}

#line 1 "<mako-codegen>"
static inline int64_t mul(int64_t a, int64_t b) {
#line 8 "/Users/loreste/mako/examples/testing/add.mko"
    int64_t __mako_retv_118 = mako_wrap_mul_i64(a, b);
    mako_trace_exit("mul");
    return __mako_retv_118;
}

#line 1 "<mako-codegen>"
static inline MakoIntArray append_one_memory(MakoIntArray xs, int64_t value) {
#line 6 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
    MakoIntArray __mako_ap_119 = mako_slice_append(xs, value);
    MakoIntArray __mako_view_120 = __mako_ap_119;
    MakoIntArray __mako_own_121 = __mako_view_120.cap > 0 ? __mako_view_120 : mako_int_array_to_owned(__mako_view_120);
    mako_trace_exit("append_one_memory");
    return __mako_own_121;
}

#line 1 "<mako-codegen>"
void mako_main(void) {
    mako_trace_enter("main", "/Users/loreste/mako/examples/testing/append_move_memory.mko", 6);
#line 10 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
    MakoIntArray __mako_arr_122 = mako_int_array_empty();
    MakoIntArray xs = __mako_arr_122;
#line 11 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
    int64_t i = 0;
#line 12 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
    while (1) {
        if (!((i < 128))) break;
#line 13 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
        MakoIntArray __mako_r_123 = append_one_memory(xs, i);
        void *__mako_old_data_124 = xs.data;
        size_t __mako_old_cap_125 = xs.cap;
        xs = __mako_r_123;
        if (__mako_old_data_124 != xs.data && __mako_old_cap_125 > 0 && __mako_old_data_124) mako_rc_release(__mako_old_data_124);
#line 14 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
        i = mako_wrap_add_i64(i, 1);
    }
#line 16 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
    mako_assert_eq(mako_array_len(xs), 128);
#line 17 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
    MAKO_BOUNDS_CHECK(0 < 0 || (size_t)0 >= xs.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(xs.data[0], 0);
#line 18 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
    MAKO_BOUNDS_CHECK(127 < 0 || (size_t)127 >= xs.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(xs.data[127], 127);
    mako_int_array_free(xs);
    mako_trace_exit("main");
}

#line 1 "<mako-codegen>"
static inline int64_t double_it(int64_t n) {
#line 6 "/Users/loreste/mako/examples/testing/debug_probe.mko"
    int64_t __mako_retv_126 = mako_wrap_mul_i64(n, 2);
    mako_trace_exit("double_it");
    return __mako_retv_126;
}

#line 1 "<mako-codegen>"

int main(int argc, char **argv) {
    mako_set_args(argc, argv);
    mako_main();
    return 0;
}
