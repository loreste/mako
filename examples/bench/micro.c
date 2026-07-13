#include "mako_rt.h"
#define MAKO_OVERFLOW_MODE 0
#include "mako_overflow.h"
#ifndef MAKO_WASI
#include "mako_uuid.h"
#include "mako_net.h"
#include "mako_proxy.h"
#include "mako_http.h"
#include "mako_trace.h"
#include "mako_log.h"
#include "mako_std.h"
#include "mako_leak.h"
#include "mako_shutdown.h"
#include "mako_tls.h"
#include "mako_llm.h"
#include "mako_sip.h"
#include "mako_nghttp2.h"
#include "mako_quiche.h"
#include "mako_ws.h"
#include "mako_db.h"
#include "mako_cmap.h"
#include "mako_dio.h"
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
#endif /* MAKO_WASI */

#line 1 "examples/bench/micro.mko"
int64_t fib(int64_t n);
int64_t bench_fib(void);
int64_t bench_slice(void);
int64_t bench_map(void);
void mako_main(void);

/*__MAKO_HELPERS__*/

int64_t fib(int64_t n) {
    if ((n < 2)) {
        return n;
    }
    int64_t r_0 = fib((n - 1));
    int64_t r_1 = fib((n - 2));
    return (r_0 + r_1);
}

int64_t bench_fib(void) {
    int64_t bb_2 = mako_black_box_i64(30);
    int64_t n = bb_2;
    int64_t acc = 0;
    int64_t i = 0;
    int64_t bb_3 = mako_black_box_i64(5);
    int64_t iters = bb_3;
    while (1) {
        if (!((i < iters))) break;
        int64_t r_4 = fib(n);
        acc = (acc + r_4);
        i = (i + 1);
    }
    int64_t bb_5 = mako_black_box_i64(acc);
    return bb_5;
}

int64_t bench_slice(void) {
    int64_t bb_6 = mako_black_box_i64(100000);
    int64_t n = bb_6;
    MakoIntArray mk_7 = mako_int_array_make(0, n);
    MakoIntArray a = mk_7;
    int64_t i = 0;
    while (1) {
        if (!((i < n))) break;
        MakoIntArray ap_8 = mako_slice_append(a, i);
        a = ap_8;
        i = (i + 1);
    }
    int64_t bb_9 = mako_black_box_i64(mako_array_len(a));
    return bb_9;
}

int64_t bench_map(void) {
    int64_t bb_10 = mako_black_box_i64(50000);
    int64_t n = bb_10;
    MakoMapII *mk_11 = mako_map_ii_make(n);
    MakoMapII* m = mk_11;
    int64_t i = 0;
    while (1) {
        if (!((i < n))) break;
        mako_map_ii_set(m, i, (i * 2));
        i = (i + 1);
    }
    int64_t sum = 0;
    i = 0;
    while (1) {
        if (!((i < n))) break;
        sum = (sum + mako_map_ii_get(m, i));
        i = (i + 1);
    }
    int64_t bb_12 = mako_black_box_i64(sum);
    return bb_12;
}

void mako_main(void) {
    int64_t r_13 = bench_fib();
    (void)(r_13);
    int64_t r_14 = bench_slice();
    (void)(r_14);
    int64_t r_15 = bench_map();
    (void)(r_15);
    int64_t nns_16 = mako_now_ns();
    int64_t t0 = nns_16;
    int64_t r_17 = bench_fib();
    int64_t f = r_17;
    int64_t nns_18 = mako_now_ns();
    int64_t t1 = nns_18;
    int64_t r_19 = bench_slice();
    int64_t s = r_19;
    int64_t nns_20 = mako_now_ns();
    int64_t t2 = nns_20;
    int64_t r_21 = bench_map();
    int64_t m = r_21;
    int64_t nns_22 = mako_now_ns();
    int64_t t3 = nns_22;
    mako_print_str(mako_str_from_cstr("lang"));
    mako_print_str(mako_str_from_cstr("mako"));
    mako_print_str(mako_str_from_cstr("fib30x5"));
    mako_print_int(f);
    mako_print_int((t1 - t0));
    mako_print_str(mako_str_from_cstr("slice100k"));
    mako_print_int(s);
    mako_print_int((t2 - t1));
    mako_print_str(mako_str_from_cstr("map50k"));
    mako_print_int(m);
    mako_print_int((t3 - t2));
}


int main(int argc, char **argv) {
    mako_set_args(argc, argv);
    mako_main();
    return 0;
}
