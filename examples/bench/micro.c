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

typedef struct Pair {
    int64_t x;
    int64_t y;
} Pair;
static inline bool mako_eq_Pair(Pair a, Pair b) {
    return (a.x == b.x) && (a.y == b.y);
}
static inline uint64_t mako_hash_Pair(Pair k) {
    uint64_t h = 14695981039346656037ULL;
    h ^= mako_hash_i64((int64_t)k.x); h *= 1099511628211ULL;
    h ^= mako_hash_i64((int64_t)k.y); h *= 1099511628211ULL;
    return h;
}
typedef struct MakoArr_Pair {
    Pair *data;
    size_t len;
    size_t cap;
} MakoArr_Pair;
static inline MakoArr_Pair mako_arr_Pair_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Pair a;
    a.data = (Pair *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(Pair));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_Pair_free(MakoArr_Pair a) {
    if (!(a.cap > 0 && a.data)) return;
    mako_rc_release(a.data);
}
static inline MakoArr_Pair mako_arr_Pair_clone(MakoArr_Pair a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_Pair_len(MakoArr_Pair a) { return (int64_t)a.len; }
static inline int64_t mako_arr_Pair_cap(MakoArr_Pair a) { return (int64_t)a.cap; }
static inline Pair mako_arr_Pair_get(MakoArr_Pair a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return a.data[i];
}
static inline Pair* mako_arr_Pair_get_ptr(MakoArr_Pair a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    return &a.data[i];
}
static inline void mako_arr_Pair_set(MakoArr_Pair a, int64_t i, Pair v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
    Pair old = a.data[i];
    a.data[i] = v;
}
static inline MakoArr_Pair mako_arr_Pair_append(MakoArr_Pair s, Pair v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Pair *nd = (Pair *)mako_rc_alloc(ncap * sizeof(Pair));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Pair));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len] = v;
    s.len++;
    return s;
}
static inline MakoArr_Pair mako_arr_Pair_arena_append(MakoArena *arena, MakoArr_Pair s, Pair v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Pair *nd = (Pair *)mako_arena_alloc(arena, ncap * sizeof(Pair));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Pair));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_Pair mako_arr_Pair_of(const Pair *vals, size_t n) {
    MakoArr_Pair a = mako_arr_Pair_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(Pair));
    return a;
}
static inline MakoArr_Pair mako_arr_Pair_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Pair a;
    a.data = (Pair *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(Pair));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(Pair));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
typedef struct MakoArr_arr_Pair {
    MakoArr_Pair *data;
    size_t len;
    size_t cap;
} MakoArr_arr_Pair;
static inline MakoArr_arr_Pair mako_arr_arr_Pair_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_arr_Pair a;
    a.data = (MakoArr_Pair *)mako_rc_calloc((size_t)(cap ? cap : 1) * sizeof(MakoArr_Pair));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline void mako_arr_arr_Pair_free(MakoArr_arr_Pair a) {
    if (!(a.cap > 0 && a.data)) return;
    if (!mako_rc_shared(a.data)) {
    }
    mako_rc_release(a.data);
}
static inline MakoArr_arr_Pair mako_arr_arr_Pair_clone(MakoArr_arr_Pair a) {
    if (a.cap > 0 && a.data) mako_rc_retain(a.data);
    return a;
}
static inline int64_t mako_arr_arr_Pair_len(MakoArr_arr_Pair a) { return (int64_t)a.len; }
static inline int64_t mako_arr_arr_Pair_cap(MakoArr_arr_Pair a) { return (int64_t)a.cap; }
static inline MakoArr_Pair mako_arr_arr_Pair_get(MakoArr_arr_Pair a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    return a.data[i];
}
static inline void mako_arr_arr_Pair_set(MakoArr_arr_Pair a, int64_t i, MakoArr_Pair v) {
    if (i < 0 || (size_t)i >= a.len) mako_abort("nested slice index out of bounds");
    MakoArr_Pair old = a.data[i];
    a.data[i] = v;
    if (old.data != v.data) { mako_arr_Pair_free(old); }
}
static inline MakoArr_arr_Pair mako_arr_arr_Pair_append(MakoArr_arr_Pair s, MakoArr_Pair v) {
    if (s.len + 1 > s.cap || mako_rc_shared(s.data)) {
        size_t ncap = s.cap;
        if (s.len + 1 > s.cap) ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        MakoArr_Pair *nd = (MakoArr_Pair *)mako_rc_alloc(ncap * sizeof(MakoArr_Pair));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(MakoArr_Pair));
        s.data = nd; s.cap = ncap;
    }
    s.data[s.len++] = v; return s;
}
static inline MakoArr_arr_Pair mako_arr_arr_Pair_of(const MakoArr_Pair *vals, size_t n) {
    MakoArr_arr_Pair a = mako_arr_arr_Pair_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(MakoArr_Pair));
    return a;
}
static inline MakoArr_arr_Pair mako_arr_arr_Pair_slice_expr(MakoArr_arr_Pair s, int64_t low, int64_t high, int64_t max, int has_max) {
    int64_t len = (int64_t)s.len;
    int64_t cap = (int64_t)s.cap;
    if (low < 0) low = 0;
    if (high < 0) high = 0;
    if (low > len) low = len;
    if (high > len) high = len;
    if (high < low) high = low;
    MakoArr_arr_Pair out;
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

static void __attribute__((constructor)) __mako_reflect_reg_Pair(void) {
    (void)mako_reflect_register_type("Pair", "x:int,y:int");
}

#line 1 "examples/bench/micro.mko"
int64_t fib(int64_t n);
int64_t bench_fib(void);
int64_t bench_slice(void);
int64_t bench_struct(void);
int64_t bench_map(void);
int64_t bench_strings(void);
int64_t bench_channels(void);
void mako_main(void);

/*__MAKO_HELPERS__*/

#line 1 "<mako-codegen>"
int64_t fib(int64_t n) {
    mako_trace_enter("fib", "examples/bench/micro.mko", 0);
#line 11 "examples/bench/micro.mko"
    if ((n < 2)) {
#line 12 "examples/bench/micro.mko"
        mako_trace_exit("fib");
        return n;
    }
#line 14 "examples/bench/micro.mko"
    int64_t __mako_r_0 = fib(mako_wrap_sub_i64(n, 1));
    int64_t __mako_r_1 = fib(mako_wrap_sub_i64(n, 2));
    int64_t __mako_retv_2 = mako_wrap_add_i64(__mako_r_0, __mako_r_1);
    mako_trace_exit("fib");
    return __mako_retv_2;
}

#line 1 "<mako-codegen>"
int64_t bench_fib(void) {
    mako_trace_enter("bench_fib", "examples/bench/micro.mko", 14);
#line 18 "examples/bench/micro.mko"
    int64_t __mako_bb_3 = mako_black_box_i64(30);
    int64_t n = __mako_bb_3;
#line 19 "examples/bench/micro.mko"
    int64_t acc = 0;
#line 20 "examples/bench/micro.mko"
    int64_t i = 0;
#line 21 "examples/bench/micro.mko"
    int64_t __mako_bb_4 = mako_black_box_i64(5);
    int64_t iters = __mako_bb_4;
#line 22 "examples/bench/micro.mko"
    while (1) {
        if (!((i < iters))) break;
#line 23 "examples/bench/micro.mko"
        int64_t __mako_r_5 = fib(n);
        acc = mako_wrap_add_i64(acc, __mako_r_5);
#line 24 "examples/bench/micro.mko"
        i = mako_wrap_add_i64(i, 1);
    }
#line 26 "examples/bench/micro.mko"
    int64_t __mako_bb_6 = mako_black_box_i64(acc);
    mako_trace_exit("bench_fib");
    return __mako_bb_6;
}

#line 1 "<mako-codegen>"
int64_t bench_slice(void) {
    mako_trace_enter("bench_slice", "examples/bench/micro.mko", 26);
#line 30 "examples/bench/micro.mko"
    int64_t __mako_bb_7 = mako_black_box_i64(100000);
    int64_t n = __mako_bb_7;
#line 31 "examples/bench/micro.mko"
    MakoIntArray __mako_mk_8 = mako_int_array_make(0, n);
    MakoIntArray a = __mako_mk_8;
#line 32 "examples/bench/micro.mko"
    int64_t i = 0;
#line 33 "examples/bench/micro.mko"
    while (1) {
        if (!((i < n))) break;
#line 34 "examples/bench/micro.mko"
        MakoIntArray __mako_ap_9 = mako_slice_append(a, i);
        void *__mako_old_data_10 = a.data;
        size_t __mako_old_cap_11 = a.cap;
        a = __mako_ap_9;
        if (__mako_old_data_10 != a.data && __mako_old_cap_11 > 0 && __mako_old_data_10) mako_rc_release(__mako_old_data_10);
#line 35 "examples/bench/micro.mko"
        i = mako_wrap_add_i64(i, 1);
    }
#line 37 "examples/bench/micro.mko"
    int64_t __mako_bb_12 = mako_black_box_i64(mako_array_len(a));
    mako_int_array_free(a);
    mako_trace_exit("bench_slice");
    return __mako_bb_12;
}

#line 1 "<mako-codegen>"
int64_t bench_struct(void) {
    mako_trace_enter("bench_struct", "examples/bench/micro.mko", 37);
#line 41 "examples/bench/micro.mko"
    int64_t __mako_bb_13 = mako_black_box_i64(1000000);
    int64_t n = __mako_bb_13;
#line 42 "examples/bench/micro.mko"
    int64_t sum = 0;
#line 43 "examples/bench/micro.mko"
    int64_t i = 0;
#line 44 "examples/bench/micro.mko"
    while (1) {
        if (!((i < n))) break;
#line 45 "examples/bench/micro.mko"
        int64_t __mako_bb_14 = mako_black_box_i64(i);
        int64_t x = __mako_bb_14;
#line 46 "examples/bench/micro.mko"
        Pair __mako_st_15;
        memset(&__mako_st_15, 0, sizeof(__mako_st_15));
        __mako_st_15.x = x;
        __mako_st_15.y = mako_wrap_add_i64(x, 1);
        Pair p = __mako_st_15;
#line 47 "examples/bench/micro.mko"
        sum = mako_wrap_add_i64(mako_wrap_add_i64(sum, p.x), p.y);
#line 48 "examples/bench/micro.mko"
        i = mako_wrap_add_i64(i, 1);
    }
#line 50 "examples/bench/micro.mko"
    int64_t __mako_bb_16 = mako_black_box_i64(sum);
    mako_trace_exit("bench_struct");
    return __mako_bb_16;
}

#line 1 "<mako-codegen>"
int64_t bench_map(void) {
    mako_trace_enter("bench_map", "examples/bench/micro.mko", 50);
#line 54 "examples/bench/micro.mko"
    int64_t __mako_bb_17 = mako_black_box_i64(50000);
    int64_t n = __mako_bb_17;
#line 55 "examples/bench/micro.mko"
    MakoMapII *__mako_mk_18 = mako_map_ii_make(n);
    MakoMapII* m = __mako_mk_18;
#line 56 "examples/bench/micro.mko"
    int64_t i = 0;
#line 57 "examples/bench/micro.mko"
    while (1) {
        if (!((i < n))) break;
#line 58 "examples/bench/micro.mko"
        mako_map_ii_set(m, i, mako_wrap_mul_i64(i, 2));
#line 59 "examples/bench/micro.mko"
        i = mako_wrap_add_i64(i, 1);
    }
#line 61 "examples/bench/micro.mko"
    int64_t sum = 0;
#line 62 "examples/bench/micro.mko"
    i = 0;
#line 63 "examples/bench/micro.mko"
    while (1) {
        if (!((i < n))) break;
#line 64 "examples/bench/micro.mko"
        sum = mako_wrap_add_i64(sum, mako_map_ii_get(m, i));
#line 65 "examples/bench/micro.mko"
        i = mako_wrap_add_i64(i, 1);
    }
#line 67 "examples/bench/micro.mko"
    int64_t __mako_bb_19 = mako_black_box_i64(sum);
    mako_map_ii_free(m);
    mako_trace_exit("bench_map");
    return __mako_bb_19;
}

#line 1 "<mako-codegen>"
int64_t bench_strings(void) {
    mako_trace_enter("bench_strings", "examples/bench/micro.mko", 67);
#line 71 "examples/bench/micro.mko"
    int64_t __mako_bb_20 = mako_black_box_i64(20000);
    int64_t n = __mako_bb_20;
#line 72 "examples/bench/micro.mko"
    int64_t total = 0;
#line 73 "examples/bench/micro.mko"
    int64_t i = 0;
#line 74 "examples/bench/micro.mko"
    while (1) {
        if (!((i < n))) break;
#line 75 "examples/bench/micro.mko"
        MakoString __mako_cr_22 = mako_int_to_string(i);
        MakoString __mako_s_21 = mako_str_concat(mako_str_view("item-", 5), __mako_cr_22);
        mako_str_free(__mako_cr_22);
        MakoString s = __mako_s_21;
#line 76 "examples/bench/micro.mko"
        total = mako_wrap_add_i64(total, mako_str_len(s));
#line 77 "examples/bench/micro.mko"
        i = mako_wrap_add_i64(i, 1);
        mako_str_free(s);
    }
#line 79 "examples/bench/micro.mko"
    int64_t __mako_bb_23 = mako_black_box_i64(total);
    mako_trace_exit("bench_strings");
    return __mako_bb_23;
}

#line 1 "<mako-codegen>"
int64_t bench_channels(void) {
    mako_trace_enter("bench_channels", "examples/bench/micro.mko", 79);
#line 83 "examples/bench/micro.mko"
    int64_t __mako_bb_24 = mako_black_box_i64(50000);
    int64_t n = __mako_bb_24;
#line 84 "examples/bench/micro.mko"
    MakoChan *__mako_ch_25 = mako_chan_new(1);
    MakoChan* ch = __mako_ch_25;
#line 85 "examples/bench/micro.mko"
    int64_t sum = 0;
#line 86 "examples/bench/micro.mko"
    int64_t i = 0;
#line 87 "examples/bench/micro.mko"
    while (1) {
        if (!((i < n))) break;
#line 88 "examples/bench/micro.mko"
        int64_t __mako_bb_26 = mako_black_box_i64(i);
        bool __mako_ok_27 = mako_chan_send(ch, __mako_bb_26) != 0;
        (void)(__mako_ok_27);
#line 89 "examples/bench/micro.mko"
        int64_t __mako_rv_28 = mako_chan_recv(ch);
        sum = mako_wrap_add_i64(sum, __mako_rv_28);
#line 90 "examples/bench/micro.mko"
        i = mako_wrap_add_i64(i, 1);
    }
#line 92 "examples/bench/micro.mko"
    mako_chan_close(ch);
#line 93 "examples/bench/micro.mko"
    int64_t __mako_bb_29 = mako_black_box_i64(sum);
    mako_chan_free(ch);
    mako_trace_exit("bench_channels");
    return __mako_bb_29;
}

#line 1 "<mako-codegen>"
void mako_main(void) {
    mako_trace_enter("main", "examples/bench/micro.mko", 93);
#line 97 "examples/bench/micro.mko"
    int64_t __mako_r_30 = bench_fib();
    (void)(__mako_r_30);
#line 98 "examples/bench/micro.mko"
    int64_t __mako_r_31 = bench_struct();
    (void)(__mako_r_31);
#line 99 "examples/bench/micro.mko"
    int64_t __mako_r_32 = bench_slice();
    (void)(__mako_r_32);
#line 100 "examples/bench/micro.mko"
    int64_t __mako_r_33 = bench_map();
    (void)(__mako_r_33);
#line 101 "examples/bench/micro.mko"
    int64_t __mako_r_34 = bench_strings();
    (void)(__mako_r_34);
#line 102 "examples/bench/micro.mko"
    int64_t __mako_r_35 = bench_channels();
    (void)(__mako_r_35);
#line 104 "examples/bench/micro.mko"
    int64_t __mako_nns_36 = mako_now_ns();
    int64_t t0 = __mako_nns_36;
#line 105 "examples/bench/micro.mko"
    int64_t __mako_r_37 = bench_fib();
    int64_t f = __mako_r_37;
#line 106 "examples/bench/micro.mko"
    int64_t __mako_nns_38 = mako_now_ns();
    int64_t t1 = __mako_nns_38;
#line 107 "examples/bench/micro.mko"
    int64_t __mako_r_39 = bench_struct();
    int64_t st = __mako_r_39;
#line 108 "examples/bench/micro.mko"
    int64_t __mako_nns_40 = mako_now_ns();
    int64_t t2 = __mako_nns_40;
#line 109 "examples/bench/micro.mko"
    int64_t __mako_r_41 = bench_slice();
    int64_t s = __mako_r_41;
#line 110 "examples/bench/micro.mko"
    int64_t __mako_nns_42 = mako_now_ns();
    int64_t t3 = __mako_nns_42;
#line 111 "examples/bench/micro.mko"
    int64_t __mako_r_43 = bench_map();
    int64_t m = __mako_r_43;
#line 112 "examples/bench/micro.mko"
    int64_t __mako_nns_44 = mako_now_ns();
    int64_t t4 = __mako_nns_44;
#line 113 "examples/bench/micro.mko"
    int64_t __mako_r_45 = bench_strings();
    int64_t str = __mako_r_45;
#line 114 "examples/bench/micro.mko"
    int64_t __mako_nns_46 = mako_now_ns();
    int64_t t5 = __mako_nns_46;
#line 115 "examples/bench/micro.mko"
    int64_t __mako_r_47 = bench_channels();
    int64_t ch = __mako_r_47;
#line 116 "examples/bench/micro.mko"
    int64_t __mako_nns_48 = mako_now_ns();
    int64_t t6 = __mako_nns_48;
#line 118 "examples/bench/micro.mko"
    mako_print_str(mako_str_view("lang", 4));
#line 119 "examples/bench/micro.mko"
    mako_print_str(mako_str_view("mako", 4));
#line 120 "examples/bench/micro.mko"
    mako_print_str(mako_str_view("fib30x5", 7));
#line 121 "examples/bench/micro.mko"
    mako_print_int(f);
#line 122 "examples/bench/micro.mko"
    mako_print_int(mako_wrap_sub_i64(t1, t0));
#line 123 "examples/bench/micro.mko"
    mako_print_str(mako_str_view("struct1m", 8));
#line 124 "examples/bench/micro.mko"
    mako_print_int(st);
#line 125 "examples/bench/micro.mko"
    mako_print_int(mako_wrap_sub_i64(t2, t1));
#line 126 "examples/bench/micro.mko"
    mako_print_str(mako_str_view("slice100k", 9));
#line 127 "examples/bench/micro.mko"
    mako_print_int(s);
#line 128 "examples/bench/micro.mko"
    mako_print_int(mako_wrap_sub_i64(t3, t2));
#line 129 "examples/bench/micro.mko"
    mako_print_str(mako_str_view("map50k", 6));
#line 130 "examples/bench/micro.mko"
    mako_print_int(m);
#line 131 "examples/bench/micro.mko"
    mako_print_int(mako_wrap_sub_i64(t4, t3));
#line 132 "examples/bench/micro.mko"
    mako_print_str(mako_str_view("string20k", 9));
#line 133 "examples/bench/micro.mko"
    mako_print_int(str);
#line 134 "examples/bench/micro.mko"
    mako_print_int(mako_wrap_sub_i64(t5, t4));
#line 135 "examples/bench/micro.mko"
    mako_print_str(mako_str_view("chan50k", 7));
#line 136 "examples/bench/micro.mko"
    mako_print_int(ch);
#line 137 "examples/bench/micro.mko"
    mako_print_int(mako_wrap_sub_i64(t6, t5));
    mako_trace_exit("main");
}

#line 1 "<mako-codegen>"

int main(int argc, char **argv) {
    mako_set_args(argc, argv);
    mako_main();
    return 0;
}
