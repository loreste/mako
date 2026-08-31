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
#endif /* MAKO_WASI */

#line 1 "examples/testing/unicode17_normalization_test.mko"
MakoIntArray append_one_memory(MakoIntArray xs, int64_t value);
void mako_main(void);

/*__MAKO_HELPERS__*/

#line 1 "<mako-codegen>"
MakoIntArray append_one_memory(MakoIntArray xs, int64_t value) {
#line 6 "examples/testing/unicode17_normalization_test.mko"
    MakoIntArray ap_0 = mako_slice_append(xs, value);
    MakoIntArray own_1 = mako_int_array_to_owned(ap_0);
    return own_1;
}

#line 1 "<mako-codegen>"
void mako_main(void) {
#line 10 "examples/testing/unicode17_normalization_test.mko"
    MakoIntArray arr_2 = mako_int_array_empty();
    MakoIntArray xs = arr_2;
#line 11 "examples/testing/unicode17_normalization_test.mko"
    int64_t i = 0;
#line 12 "examples/testing/unicode17_normalization_test.mko"
    while (1) {
        if (!((i < 128))) break;
#line 13 "examples/testing/unicode17_normalization_test.mko"
        MakoIntArray r_3 = append_one_memory(xs, i);
        void *old_data_4 = xs.data;
        size_t old_cap_5 = xs.cap;
        xs = r_3;
        if (old_data_4 != xs.data && old_cap_5 > 0 && old_data_4) mako_rc_release(old_data_4);
#line 14 "examples/testing/unicode17_normalization_test.mko"
        i = mako_wrap_add_i64(i, 1);
    }
#line 16 "examples/testing/unicode17_normalization_test.mko"
    mako_assert_eq(mako_array_len(xs), 128);
#line 17 "examples/testing/unicode17_normalization_test.mko"
    int64_t idx_6 = 0;
    MAKO_BOUNDS_CHECK(idx_6 < 0 || (size_t)idx_6 >= xs.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(xs.data[idx_6], 0);
#line 18 "examples/testing/unicode17_normalization_test.mko"
    int64_t idx_7 = 127;
    MAKO_BOUNDS_CHECK(idx_7 < 0 || (size_t)idx_7 >= xs.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(xs.data[idx_7], 127);
    mako_int_array_free(xs);
}

#line 1 "<mako-codegen>"

int main(int argc, char **argv) {
    mako_set_args(argc, argv);
    mako_main();
    return 0;
}
