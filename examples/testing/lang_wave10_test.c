#include "mako_rt.h"
#ifndef MAKO_WASI
#include "mako_uuid.h"
#include "mako_http.h"
#include "mako_net.h"
#include "mako_std.h"
#include "mako_tls.h"
#include "mako_nghttp2.h"
#include "mako_quiche.h"
#include "mako_ws.h"
#include "mako_db.h"
#include "mako_cmap.h"
#include "mako_dio.h"
#include "mako_evloop.h"
#include "mako_game.h"
#include "mako_cloud.h"
#include "mako_httpengine.h"
#endif /* MAKO_WASI */

typedef struct {
    int64_t x;
    int64_t y;
} Pt;
typedef struct {
    Pt *data;
    size_t len;
    size_t cap;
} MakoArr_Pt;
static inline MakoArr_Pt mako_arr_Pt_make(int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Pt a;
    a.data = (Pt *)calloc((size_t)(cap ? cap : 1), sizeof(Pt));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}
static inline int64_t mako_arr_Pt_len(MakoArr_Pt a) { return (int64_t)a.len; }
static inline int64_t mako_arr_Pt_cap(MakoArr_Pt a) { return (int64_t)a.cap; }
static inline Pt mako_arr_Pt_get(MakoArr_Pt a, int64_t i) {
#ifndef NDEBUG
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
#endif
    return a.data[i];
}
static inline void mako_arr_Pt_set(MakoArr_Pt a, int64_t i, Pt v) {
#ifndef NDEBUG
    if (i < 0 || (size_t)i >= a.len) mako_abort("struct slice index out of bounds");
#endif
    a.data[i] = v;
}
static inline MakoArr_Pt mako_arr_Pt_append(MakoArr_Pt s, Pt v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Pt *nd = (Pt *)realloc(s.data, ncap * sizeof(Pt));
        if (!nd) mako_abort("append: out of memory");
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_Pt mako_arr_Pt_arena_append(MakoArena *arena, MakoArr_Pt s, Pt v) {
    if (s.len + 1 > s.cap) {
        size_t ncap = s.cap ? s.cap * 2 : 1;
        if (ncap < s.len + 1) ncap = s.len + 1;
        Pt *nd = (Pt *)mako_arena_alloc(arena, ncap * sizeof(Pt));
        if (s.len) memcpy(nd, s.data, s.len * sizeof(Pt));
        s.data = nd;
        s.cap = ncap;
    }
    s.data[s.len++] = v;
    return s;
}
static inline MakoArr_Pt mako_arr_Pt_of(const Pt *vals, size_t n) {
    MakoArr_Pt a = mako_arr_Pt_make((int64_t)n, (int64_t)n);
    if (n) memcpy(a.data, vals, n * sizeof(Pt));
    return a;
}
static inline MakoArr_Pt mako_arr_Pt_arena_make(MakoArena *arena, int64_t len, int64_t cap) {
    if (len < 0) len = 0;
    if (cap < len) cap = len;
    MakoArr_Pt a;
    a.data = (Pt *)mako_arena_alloc(arena, (size_t)(cap ? cap : 1) * sizeof(Pt));
    memset(a.data, 0, (size_t)(cap ? cap : 1) * sizeof(Pt));
    a.len = (size_t)len;
    a.cap = (size_t)(cap ? cap : 1);
    return a;
}

static void __attribute__((constructor)) __mako_reflect_reg_Pt(void) {
    (void)mako_reflect_register_type("Pt", "x:int,y:int");
}

typedef struct {
    int64_t _0;
    int64_t _1;
} MakoTup_int_int;

void TestGenerics(void);
void TestOnMethods(void);
void TestTuples(void);
void TestChanOpenInt(void);
void TestChanOpenString(void);
void TestChanNewCompat(void);
int64_t Pt_sum(Pt self);
int64_t identity__int(int64_t x);
MakoString identity__string(MakoString x);

/*__MAKO_HELPERS__*/

void TestGenerics(void) {
    int64_t r_0 = identity__int(7);
    mako_assert_eq(r_0, 7);
    MakoString r_1 = identity__string(mako_str_from_cstr("ok"));
    mako_assert_eq_str(r_1, mako_str_from_cstr("ok"));
}

void TestOnMethods(void) {
    Pt st_2;
    memset(&st_2, 0, sizeof(st_2));
    st_2.x = 1;
    st_2.y = 2;
    Pt p = st_2;
    int64_t em_3 = Pt_sum(p);
    mako_assert_eq(em_3, 3);
}

void TestTuples(void) {
    MakoTup_int_int tup_4;
    tup_4._0 = 4;
    tup_4._1 = 5;
    MakoTup_int_int t = tup_4;
    MakoTup_int_int scrut_6 = t;
    int64_t m_5 = 0; /* void match */
    if (1) {
        int64_t a = scrut_6._0;
        int64_t b = scrut_6._1;
        mako_assert_eq((a + b), 9);
    } else {
        fprintf(stderr, "non-exhaustive match\n"); abort();
    }
    m_5;
}

void TestChanOpenInt(void) {
    MakoChan *ch_7 = mako_chan_new(1);
    MakoChan* ch = ch_7;
    bool ok_8 = mako_chan_send(ch, 11) != 0;
    (void)(ok_8);
    int64_t rv_9 = mako_chan_recv(ch);
    mako_assert_eq(rv_9, 11);
    mako_chan_close(ch);
}

void TestChanOpenString(void) {
    MakoChanStr *ch_10 = mako_chan_str_new(1);
    MakoChanStr* cs = ch_10;
    bool ok_11 = mako_chan_str_send(cs, mako_str_from_cstr("wave10")) != 0;
    (void)(ok_11);
    MakoString rv_12 = mako_chan_str_recv(cs);
    mako_assert_eq_str(rv_12, mako_str_from_cstr("wave10"));
    mako_chan_str_close(cs);
}

void TestChanNewCompat(void) {
    MakoChan *ch_13 = mako_chan_new(1);
    MakoChan* ch = ch_13;
    bool ok_14 = mako_chan_send(ch, 3) != 0;
    (void)(ok_14);
    int64_t rv_15 = mako_chan_recv(ch);
    mako_assert_eq(rv_15, 3);
    mako_chan_close(ch);
}

int64_t Pt_sum(Pt self) {
    return (self.x + self.y);
}

int64_t identity__int(int64_t x) {
    return x;
}

MakoString identity__string(MakoString x) {
    return x;
}


int main(int argc, char **argv) {
    mako_set_args(argc, argv);
    mako_main();
    return 0;
}
