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

#line 1 "examples/testing/shared_array_crew_test.mko"
static inline MakoIntArray append_one_memory(MakoIntArray xs, int64_t value);
void mako_main(void);

/*__MAKO_HELPERS__*/

#line 1 "<mako-codegen>"
static inline MakoIntArray append_one_memory(MakoIntArray xs, int64_t value) {
#line 6 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
    MakoIntArray __mako_ap_0 = mako_slice_append(xs, value);
    MakoIntArray __mako_view_1 = __mako_ap_0;
    MakoIntArray __mako_own_2 = __mako_view_1.cap > 0 ? __mako_view_1 : mako_int_array_to_owned(__mako_view_1);
    mako_trace_exit("append_one_memory");
    return __mako_own_2;
}

#line 1 "<mako-codegen>"
void mako_main(void) {
    mako_trace_enter("main", "/Users/loreste/mako/examples/testing/append_move_memory.mko", 6);
#line 10 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
    MakoIntArray __mako_arr_3 = mako_int_array_empty();
    MakoIntArray xs = __mako_arr_3;
#line 11 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
    int64_t i = 0;
#line 12 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
    while (1) {
        if (!((i < 128))) break;
#line 13 "/Users/loreste/mako/examples/testing/append_move_memory.mko"
        MakoIntArray __mako_r_4 = append_one_memory(xs, i);
        xs = __mako_r_4;
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

int main(int argc, char **argv) {
    mako_set_args(argc, argv);
    mako_main();
    return 0;
}
