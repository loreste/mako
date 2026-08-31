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

typedef struct Point {
    double x;
    double y;
} Point;
static inline bool mako_eq_Point(Point a, Point b) {
    return (a.x == b.x) && (a.y == b.y);
}
static inline uint64_t mako_hash_Point(Point k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= mako_hash_f64(k.x); h *= 1099511628211ULL;
    h ^= mako_hash_f64(k.y); h *= 1099511628211ULL;
    return h;
}
typedef struct MakoArr_Point {
    Point *data;
    size_t len;
    size_t cap;
} MakoArr_Point;
static inline MakoArr_Point mako_arr_Point_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Point a;
    a.data = (Point *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(Point));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_Point_free(MakoArr_Point a) { if (a.cap > 0 && a.data) mako_rc_release(a.data); }
static inline MakoArr_Point mako_arr_Point_clone(MakoArr_Point a) {
    if (a.len == 0) { MakoArr_Point e = {0}; return e; }
    MakoArr_Point out = mako_arr_Point_make((int64_t)a.len, (int64_t)a.len);
    memcpy(out.data, a.data, a.len * sizeof(a.data[0]));
    return out;
}
static inline int64_t mako_arr_Point_len(MakoArr_Point a) { return (int64_t)a.len; }
static inline int64_t mako_arr_Point_cap(MakoArr_Point a) { return (int64_t)a.cap; }
static inline Point mako_arr_Point_get(MakoArr_Point a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline Point* mako_arr_Point_get_ptr(MakoArr_Point a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return &a.data[i];
}
static inline void mako_arr_Point_set(MakoArr_Point a, int64_t i, Point v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    a.data[i] = v;
}
static inline MakoArr_Point mako_arr_Point_append(MakoArr_Point s, Point v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Point *nd = (Point *)mako_rc_alloc(ncap * sizeof(Point));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Point));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_Point mako_arr_Point_arena_append(MakoArena *arena, MakoArr_Point s, Point v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Point *nd = (Point *)mako_arena_alloc(arena, ncap * sizeof(Point));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Point));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_Point mako_arr_Point_of(const Point *vals, size_t n) {
    MakoArr_Point a = mako_arr_Point_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(Point));
    return a;
}
static inline MakoArr_Point mako_arr_Point_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Point a;
    a.data = (Point *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(Point));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(Point));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_Point {
    MakoArr_Point *data;
    size_t len;
    size_t cap;
} MakoArr_arr_Point;
static inline MakoArr_arr_Point mako_arr_arr_Point_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_Point a;
    a.data = (MakoArr_Point *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_Point));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_Point_free(MakoArr_arr_Point a) {
    if (!(a.cap > 0 && a.data)) return;
    mako_rc_release(a.data);
}
static inline int64_t mako_arr_arr_Point_len(MakoArr_arr_Point a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_Point_cap(MakoArr_arr_Point a) { return (int64_t)a.cap; }
static inline MakoArr_Point mako_arr_arr_Point_get(MakoArr_arr_Point a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_Point_set(MakoArr_arr_Point a, int64_t i, MakoArr_Point v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    a.data[i] = v;
}
static inline MakoArr_arr_Point mako_arr_arr_Point_append(MakoArr_arr_Point s, MakoArr_Point v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_Point *nd = (MakoArr_Point *)mako_rc_alloc(ncap * sizeof(MakoArr_Point));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_Point));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_Point mako_arr_arr_Point_of(const MakoArr_Point *vals, size_t n) {
    MakoArr_arr_Point a = mako_arr_arr_Point_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_Point));
    return a;
}
static inline MakoArr_arr_Point mako_arr_arr_Point_slice_expr(MakoArr_arr_Point s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_Point out;
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

typedef struct Config {
    MakoString name;
    int64_t port;
    double rate;
    bool verbose;
} Config;
static inline bool mako_eq_Config(Config a, Config b) {
    return mako_str_eq(a.name, b.name) && (a.port == b.port) && (a.rate == b.rate) && (a.verbose == b.verbose);
}
static inline uint64_t mako_hash_Config(Config k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= mako_hash_bytes(k.name.data, k.name.len); h *= 1099511628211ULL;
    h ^= mako_hash_i64((int64_t)k.port); h *= 1099511628211ULL;
    h ^= mako_hash_f64(k.rate); h *= 1099511628211ULL;
    h ^= (uint64_t)(k.verbose ? 1 : 0); h *= 1099511628211ULL;
    return h;
}
typedef struct MakoArr_Config {
    Config *data;
    size_t len;
    size_t cap;
} MakoArr_Config;
static inline MakoArr_Config mako_arr_Config_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Config a;
    a.data = (Config *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(Config));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_Config_free(MakoArr_Config a) { if (a.cap > 0 && a.data) mako_rc_release(a.data); }
static inline MakoArr_Config mako_arr_Config_clone(MakoArr_Config a) {
    if (a.len == 0) { MakoArr_Config e = {0}; return e; }
    MakoArr_Config out = mako_arr_Config_make((int64_t)a.len, (int64_t)a.len);
    memcpy(out.data, a.data, a.len * sizeof(a.data[0]));
    return out;
}
static inline int64_t mako_arr_Config_len(MakoArr_Config a) { return (int64_t)a.len; }
static inline int64_t mako_arr_Config_cap(MakoArr_Config a) { return (int64_t)a.cap; }
static inline Config mako_arr_Config_get(MakoArr_Config a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline Config* mako_arr_Config_get_ptr(MakoArr_Config a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return &a.data[i];
}
static inline void mako_arr_Config_set(MakoArr_Config a, int64_t i, Config v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    a.data[i] = v;
}
static inline MakoArr_Config mako_arr_Config_append(MakoArr_Config s, Config v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Config *nd = (Config *)mako_rc_alloc(ncap * sizeof(Config));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Config));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_Config mako_arr_Config_arena_append(MakoArena *arena, MakoArr_Config s, Config v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Config *nd = (Config *)mako_arena_alloc(arena, ncap * sizeof(Config));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Config));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_Config mako_arr_Config_of(const Config *vals, size_t n) {
    MakoArr_Config a = mako_arr_Config_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(Config));
    return a;
}
static inline MakoArr_Config mako_arr_Config_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Config a;
    a.data = (Config *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(Config));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(Config));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_Config {
    MakoArr_Config *data;
    size_t len;
    size_t cap;
} MakoArr_arr_Config;
static inline MakoArr_arr_Config mako_arr_arr_Config_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_Config a;
    a.data = (MakoArr_Config *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_Config));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_Config_free(MakoArr_arr_Config a) {
    if (!(a.cap > 0 && a.data)) return;
    mako_rc_release(a.data);
}
static inline int64_t mako_arr_arr_Config_len(MakoArr_arr_Config a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_Config_cap(MakoArr_arr_Config a) { return (int64_t)a.cap; }
static inline MakoArr_Config mako_arr_arr_Config_get(MakoArr_arr_Config a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_Config_set(MakoArr_arr_Config a, int64_t i, MakoArr_Config v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    a.data[i] = v;
}
static inline MakoArr_arr_Config mako_arr_arr_Config_append(MakoArr_arr_Config s, MakoArr_Config v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_Config *nd = (MakoArr_Config *)mako_rc_alloc(ncap * sizeof(MakoArr_Config));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_Config));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_Config mako_arr_arr_Config_of(const MakoArr_Config *vals, size_t n) {
    MakoArr_arr_Config a = mako_arr_arr_Config_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_Config));
    return a;
}
static inline MakoArr_arr_Config mako_arr_arr_Config_slice_expr(MakoArr_arr_Config s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_Config out;
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

static void __attribute__((constructor)) __mako_reflect_reg_Point(void) {
    (void)mako_reflect_register_type("Point", "x:float,y:float");
}

static void __attribute__((constructor)) __mako_reflect_reg_Config(void) {
    (void)mako_reflect_register_type("Config", "name:string,port:int,rate:float,verbose:bool");
}

#line 1 "examples/testing/json_marshal_test.mko"
void TestJsonFloat(void);
void TestJsonBool(void);
void TestConfigToJson(void);
void TestConfigFromJson(void);
void TestPointToJson(void);
void mako_main(void);
MakoString Point_to_json(double x, double y);
MakoString Config_to_json(MakoString name, int64_t port, double rate, bool verbose);
MakoString Config_name_from_json(MakoString j);
int64_t Config_port_from_json(MakoString j);

/*__MAKO_HELPERS__*/

#line 1 "<mako-codegen>"
void TestJsonFloat(void) {
#line 18 "examples/testing/json_marshal_test.mko"
    MakoString jf_0 = mako_json_f(mako_str_from_cstr("pi"), 3.14);
    MakoString j = jf_0;
#line 19 "examples/testing/json_marshal_test.mko"
    mako_assert(mako_str_contains(j, mako_str_view("3.14", 4)));
#line 20 "examples/testing/json_marshal_test.mko"
    double jgf_1 = mako_json_get_float(j, mako_str_from_cstr("pi"));
    double v = jgf_1;
#line 21 "examples/testing/json_marshal_test.mko"
    mako_assert((v > 3.13));
#line 22 "examples/testing/json_marshal_test.mko"
    mako_assert((v < 3.15));
    mako_str_free(j);
}

#line 1 "<mako-codegen>"
void TestJsonBool(void) {
#line 26 "examples/testing/json_marshal_test.mko"
    MakoString jb_2 = mako_json_b(mako_str_from_cstr("active"), (int64_t)true);
    MakoString j = jb_2;
#line 27 "examples/testing/json_marshal_test.mko"
    mako_assert(mako_str_contains(j, mako_str_view("true", 4)));
#line 28 "examples/testing/json_marshal_test.mko"
    int64_t v = mako_json_get_bool(j, mako_str_from_cstr("active"));
#line 29 "examples/testing/json_marshal_test.mko"
    mako_assert((v == 1));
#line 30 "examples/testing/json_marshal_test.mko"
    MakoString jb_3 = mako_json_b(mako_str_from_cstr("active"), (int64_t)false);
    MakoString j2 = jb_3;
#line 31 "examples/testing/json_marshal_test.mko"
    mako_assert(mako_str_contains(j2, mako_str_view("false", 5)));
#line 32 "examples/testing/json_marshal_test.mko"
    mako_assert((mako_json_get_bool(j2, mako_str_from_cstr("active")) == 0));
    mako_str_free(j2);
    mako_str_free(j);
}

#line 1 "<mako-codegen>"
void TestConfigToJson(void) {
#line 36 "examples/testing/json_marshal_test.mko"
    MakoString r_4 = Config_to_json(mako_str_view("app", 3), 8080, 1.5, true);
    MakoString j = r_4;
#line 37 "examples/testing/json_marshal_test.mko"
    mako_assert(mako_str_contains(j, mako_str_view("\"name\":\"app\"", 12)));
#line 38 "examples/testing/json_marshal_test.mko"
    mako_assert(mako_str_contains(j, mako_str_view("\"port\":8080", 11)));
#line 39 "examples/testing/json_marshal_test.mko"
    mako_assert(mako_str_contains(j, mako_str_view("1.5", 3)));
    mako_str_free(j);
}

#line 1 "<mako-codegen>"
void TestConfigFromJson(void) {
#line 43 "examples/testing/json_marshal_test.mko"
    MakoString r_5 = Config_to_json(mako_str_view("web", 3), 3000, 0.75, false);
    MakoString j = r_5;
#line 44 "examples/testing/json_marshal_test.mko"
    MakoString r_6 = Config_name_from_json(j);
    mako_assert_eq_str(r_6, mako_str_view("web", 3));
#line 45 "examples/testing/json_marshal_test.mko"
    int64_t r_7 = Config_port_from_json(j);
    mako_assert_eq(r_7, 3000);
    mako_str_free(j);
}

#line 1 "<mako-codegen>"
void TestPointToJson(void) {
#line 49 "examples/testing/json_marshal_test.mko"
    MakoString r_8 = Point_to_json(1.5, 2.5);
    MakoString j = r_8;
#line 50 "examples/testing/json_marshal_test.mko"
    mako_assert(mako_str_contains(j, mako_str_view("1.5", 3)));
#line 51 "examples/testing/json_marshal_test.mko"
    mako_assert(mako_str_contains(j, mako_str_view("2.5", 3)));
    mako_str_free(j);
}

#line 1 "<mako-codegen>"
void mako_main(void) {
#line 55 "examples/testing/json_marshal_test.mko"
    TestJsonFloat();
#line 56 "examples/testing/json_marshal_test.mko"
    TestJsonBool();
#line 57 "examples/testing/json_marshal_test.mko"
    TestConfigToJson();
#line 58 "examples/testing/json_marshal_test.mko"
    TestConfigFromJson();
#line 59 "examples/testing/json_marshal_test.mko"
    TestPointToJson();
#line 60 "examples/testing/json_marshal_test.mko"
    mako_print_str(mako_str_view("PASS\n", 5));
}

#line 1 "<mako-codegen>"
MakoString Point_to_json(double x, double y) {
    MakoString jf_14 = mako_json_f(mako_str_from_cstr("x"), x);
    MakoString jf_15 = mako_json_f(mako_str_from_cstr("y"), y);
    MakoString jm_16 = mako_json_merge(jf_14, jf_15);
    return jm_16;
}

#line 1 "<mako-codegen>"
MakoString Config_to_json(MakoString name, int64_t port, double rate, bool verbose) {
    MakoString jo_17 = mako_json_object_str(mako_str_from_cstr("name"), name);
    MakoString ji_18 = mako_json_i(mako_str_from_cstr("port"), port);
    MakoString jm_19 = mako_json_merge(jo_17, ji_18);
    MakoString jf_20 = mako_json_f(mako_str_from_cstr("rate"), rate);
    MakoString jm_21 = mako_json_merge(jm_19, jf_20);
    MakoString jb_22 = mako_json_b(mako_str_from_cstr("verbose"), (int64_t)verbose);
    MakoString jm_23 = mako_json_merge(jm_21, jb_22);
    return jm_23;
}

#line 1 "<mako-codegen>"
MakoString Config_name_from_json(MakoString j) {
    MakoString jgs_24 = mako_json_get_string(j, mako_str_from_cstr("name"));
    return jgs_24;
}

#line 1 "<mako-codegen>"
int64_t Config_port_from_json(MakoString j) {
    int64_t retv_25 = mako_json_get_int(j, mako_str_from_cstr("port"));
    return retv_25;
}

#line 1 "<mako-codegen>"

int main(int argc, char **argv) {
    mako_set_args(argc, argv);
    mako_main();
    return 0;
}
