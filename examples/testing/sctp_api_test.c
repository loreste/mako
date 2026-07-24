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
#include "mako_domain.h"
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

#line 1 "examples/testing/sctp_api_test.mko"
MakoIntArray append_one_memory(MakoIntArray xs, int64_t value);
void mako_main(void);

/*__MAKO_HELPERS__*/

MakoIntArray append_one_memory(MakoIntArray xs, int64_t value) {
    MakoIntArray ap_0 = mako_slice_append(xs, value);
    MakoIntArray own_1 = mako_int_array_to_owned(ap_0);
    return own_1;
}

void mako_main(void) {
    MakoIntArray arr_2 = mako_int_array_empty();
    MakoIntArray xs = arr_2;
    int64_t i = 0;
    while (1) {
        if (!((i < 128))) break;
        MakoIntArray r_3 = append_one_memory(xs, i);
        xs = r_3;
        i = (i + 1);
    }
    mako_assert_eq(mako_array_len(xs), 128);
    int64_t idx_4 = 0;
    MAKO_BOUNDS_CHECK(idx_4 < 0 || (size_t)idx_4 >= xs.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(xs.data[idx_4], 0);
    int64_t idx_5 = 127;
    MAKO_BOUNDS_CHECK(idx_5 < 0 || (size_t)idx_5 >= xs.len, "index out of bounds (slices are 0..len-1)");
    mako_assert_eq(xs.data[idx_5], 127);
    mako_int_array_free(xs);
}


int main(int argc, char **argv) {
    mako_set_args(argc, argv);
    mako_main();
    return 0;
}
