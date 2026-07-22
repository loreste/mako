// Bridge from shared-IR pointer ABI into the header-only mako runtime.
// Compiled into libmako_native_runtime.a and linked by the native backends.
//
// String args: borrowed MakoNativeString* → temporary MakoString view (not freed).
// String returns: owned MakoString → transferred into MakoNativeString* header.

#include "mako_rt.h"
#include "mako_net.h"
#include "mako_http.h"
#include "mako_stdlib.h"
#include "mako_std.h"
#include "mako_fmt.h"
#include "mako_uuid.h"
#include "mako_ws.h"
#include "mako_db.h"
#include "mako_evloop.h"
#include "mako_mail.h"
#include "mako_cmap.h"
#include "mako_template.h"
#include "mako_sip.h"
// Real TLS / GPU / H3 (gated by build.rs feature defines).
// mako_log.h redefines symbols already in mako_stdlib.h — use stdlib log only.
#include "mako_tls.h"
#include "mako_gpu.h"
#include "mako_model.h"
#include "mako_quiche.h"
#include "mako_overflow.h"
#include "mako_cloud.h"
#include "mako_llm.h"
#include "mako_proxy.h"
#include "mako_trace.h"
/* domain after trace — domain OTLP helpers call mako_trace_export_*. */
#include "mako_domain.h"
#include "mako_shutdown.h"
#include "mako_tok.h"
#include "mako_game.h"
#include "mako_leak.h"

// Forward decls matching native_runtime.c (same archive).
typedef struct {
    const char *data;
    size_t len;
} MakoNativeString;

MakoNativeString *mako_native_string_literal_ptr(const char *data, size_t len);
void mako_native_string_drop_ptr(MakoNativeString *value);

static MakoString bridge_borrow_str(const MakoNativeString *s) {
    if (!s || !s->data) {
        return mako_str_empty;
    }
    MakoString m;
    m.data = (char *)(uintptr_t)s->data;
    /* Strip immortal/static high bit from native string lens. */
    m.len = s->len & ~((size_t)1 << (sizeof(size_t) * 8 - 1));
    return m;
}

static MakoNativeString *bridge_take_str(MakoString s) {
    if (mako_str_is_empty_singleton(s) || !s.data) {
        return mako_native_string_literal_ptr("", 0);
    }
    // Transfer ownership of s.data into a native header.
    MakoNativeString *out = (MakoNativeString *)malloc(sizeof(*out));
    if (!out) abort();
    out->data = s.data;
    out->len = s.len;
    return out;
}

/* ---- SqlDB key registry (meta-key → full MakoSqlDB) ------------------------ */
#define MAKO_NATIVE_SQL_DB_MAX 64
static MakoSqlDB mako_native_sql_dbs[MAKO_NATIVE_SQL_DB_MAX];
static int64_t mako_native_sql_db_keys[MAKO_NATIVE_SQL_DB_MAX];

static int64_t mako_native_sql_register(MakoSqlDB db) {
    int64_t key = mako_sql_meta_key(db);
    if (!key) return 0;
    for (int i = 0; i < MAKO_NATIVE_SQL_DB_MAX; i++) {
        if (mako_native_sql_db_keys[i] == key || mako_native_sql_db_keys[i] == 0) {
            mako_native_sql_db_keys[i] = key;
            mako_native_sql_dbs[i] = db;
            return key;
        }
    }
    /* table full: overwrite slot 0 */
    mako_native_sql_db_keys[0] = key;
    mako_native_sql_dbs[0] = db;
    return key;
}

static MakoSqlDB mako_native_sql_db_from_key(int64_t key) {
    MakoSqlDB empty;
    memset(&empty, 0, sizeof(empty));
    if (!key) return empty;
    for (int i = 0; i < MAKO_NATIVE_SQL_DB_MAX; i++) {
        if (mako_native_sql_db_keys[i] == key) return mako_native_sql_dbs[i];
    }
    /* fallback: treat key as sqlite handle (common path) */
    empty.driver = 1;
    empty.sqlite = (void *)(intptr_t)key;
    return empty;
}

/* ---- slog (standalone; mako_log.h clashes with stdlib log) ----------------- */
static int mako_native_slog_min_level = 1; /* info */
static int mako_native_slog_json_mode = 0;
static char mako_native_slog_service_buf[96];
static FILE *mako_native_slog_out_fp = NULL;

static int mako_native_slog_level_num(MakoNativeString *level) {
    MakoString s = bridge_borrow_str(level);
    if (!s.data || s.len == 0) return 1;
    if (s.len >= 5 && memcmp(s.data, "debug", 5) == 0) return 0;
    if (s.len >= 4 && memcmp(s.data, "info", 4) == 0) return 1;
    if (s.len >= 4 && memcmp(s.data, "warn", 4) == 0) return 2;
    if (s.len >= 5 && memcmp(s.data, "error", 5) == 0) return 3;
    return 1;
}

static const char *mako_native_slog_level_tag(int n) {
    if (n <= 0) return "debug";
    if (n == 2) return "warn";
    if (n >= 3) return "error";
    return "info";
}

static FILE *mako_native_slog_fp(void) {
    return mako_native_slog_out_fp ? mako_native_slog_out_fp : stderr;
}

static void mako_native_slog_json_escape_write(FILE *fp, const char *p, size_t n) {
    if (!p) return;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c == '"' || c == '\\') {
            fputc('\\', fp);
            fputc((char)c, fp);
        } else if (c == '\n') {
            fputs("\\n", fp);
        } else if (c == '\r') {
            fputs("\\r", fp);
        } else if (c == '\t') {
            fputs("\\t", fp);
        } else if (c < 0x20) {
            fprintf(fp, "\\u%04x", c);
        } else {
            fputc((char)c, fp);
        }
    }
}

static void mako_native_slog_emit_fields(
    MakoNativeString *level, MakoNativeString *msg,
    MakoNativeString *k1, MakoNativeString *v1,
    MakoNativeString *k2, MakoNativeString *v2,
    MakoNativeString *k3, MakoNativeString *v3,
    int64_t unused
) {
    (void)unused;
    int n = mako_native_slog_level_num(level);
    if (n < mako_native_slog_min_level) return;
    FILE *fp = mako_native_slog_fp();
    const char *tag = mako_native_slog_level_tag(n);
    MakoString m = bridge_borrow_str(msg);
    if (mako_native_slog_json_mode) {
        fputc('{', fp);
        fputs("\"level\":\"", fp);
        fputs(tag, fp);
        fputs("\"", fp);
        if (mako_native_slog_service_buf[0]) {
            fputs(",\"service\":\"", fp);
            mako_native_slog_json_escape_write(
                fp, mako_native_slog_service_buf, strlen(mako_native_slog_service_buf));
            fputc('"', fp);
        }
        fputs(",\"msg\":\"", fp);
        mako_native_slog_json_escape_write(fp, m.data, m.len);
        fputc('"', fp);
        MakoNativeString *ks[3] = {k1, k2, k3};
        MakoNativeString *vs[3] = {v1, v2, v3};
        for (int i = 0; i < 3; i++) {
            if (!ks[i]) continue;
            MakoString kk = bridge_borrow_str(ks[i]);
            MakoString vv = bridge_borrow_str(vs[i]);
            fputc(',', fp);
            fputc('"', fp);
            mako_native_slog_json_escape_write(fp, kk.data, kk.len);
            fputs("\":\"", fp);
            mako_native_slog_json_escape_write(fp, vv.data, vv.len);
            fputc('"', fp);
        }
        fputs("}\n", fp);
    } else {
        fputs(tag, fp);
        if (mako_native_slog_service_buf[0]) {
            fprintf(fp, " service=%s", mako_native_slog_service_buf);
        }
        fputc(' ', fp);
        if (m.data && m.len) fwrite(m.data, 1, m.len, fp);
        MakoNativeString *ks[3] = {k1, k2, k3};
        MakoNativeString *vs[3] = {v1, v2, v3};
        for (int i = 0; i < 3; i++) {
            if (!ks[i]) continue;
            MakoString kk = bridge_borrow_str(ks[i]);
            MakoString vv = bridge_borrow_str(vs[i]);
            fputc(' ', fp);
            if (kk.data && kk.len) fwrite(kk.data, 1, kk.len, fp);
            fputc('=', fp);
            if (vv.data && vv.len) fwrite(vv.data, 1, vv.len, fp);
        }
        fputc('\n', fp);
    }
}

static void mako_native_slog_emit(const char *lvl, MakoNativeString *msg) {
    FILE *fp = mako_native_slog_fp();
    if (mako_native_slog_json_mode) {
        fputs("{\"level\":\"", fp);
        fputs(lvl, fp);
        fputs("\"", fp);
        if (mako_native_slog_service_buf[0]) {
            fputs(",\"service\":\"", fp);
            mako_native_slog_json_escape_write(
                fp, mako_native_slog_service_buf, strlen(mako_native_slog_service_buf));
            fputc('"', fp);
        }
        fputs(",\"msg\":\"", fp);
        MakoString m = bridge_borrow_str(msg);
        mako_native_slog_json_escape_write(fp, m.data, m.len);
        fputs("\"}\n", fp);
    } else {
        fputs(lvl, fp);
        fputc(' ', fp);
        MakoString m = bridge_borrow_str(msg);
        if (m.data && m.len) fwrite(m.data, 1, (size_t)m.len, fp);
        fputc('\n', fp);
    }
}

// ---- net / tcp --------------------------------------------------------------

int64_t mako_native_tcp_listen(int64_t port) {
    return mako_tcp_listen(port);
}

int64_t mako_native_tcp_listen_addr_ptr(MakoNativeString *host, int64_t port) {
    return mako_tcp_listen_addr(bridge_borrow_str(host), port);
}

int64_t mako_native_tcp_connect_ptr(MakoNativeString *host, int64_t port) {
    return mako_tcp_connect(bridge_borrow_str(host), port);
}

int64_t mako_native_tcp_accept(int64_t fd) {
    return mako_tcp_accept(fd);
}

int64_t mako_native_tcp_accept_nb(int64_t fd) {
    return mako_tcp_accept_nb(fd);
}

int64_t mako_native_tcp_close(int64_t fd) {
    return mako_tcp_close(fd);
}

int64_t mako_native_tcp_write_ptr(int64_t fd, MakoNativeString *s) {
    return mako_tcp_write(fd, bridge_borrow_str(s));
}

// ---- http -------------------------------------------------------------------

int64_t mako_native_http_bind(int64_t port) {
    return mako_http_bind(port);
}

int64_t mako_native_http_accept(int64_t listen_fd) {
    return mako_http_accept(listen_fd);
}

int64_t mako_native_http_echo(int64_t port) {
    return mako_http_echo(port);
}

int64_t mako_native_http_serve_ptr(int64_t port, MakoNativeString *body) {
    return mako_http_serve(port, bridge_borrow_str(body));
}

MakoNativeString *mako_native_http_get_ptr(MakoNativeString *url) {
    return bridge_take_str(mako_http_get(bridge_borrow_str(url)));
}

// ---- fmt --------------------------------------------------------------------

int64_t mako_native_fmt_println_ptr(MakoNativeString *s) {
    return mako_fmt_println1(bridge_borrow_str(s));
}

int64_t mako_native_fmt_println2_ptr(MakoNativeString *a, MakoNativeString *b) {
    return mako_fmt_println2(bridge_borrow_str(a), bridge_borrow_str(b));
}

int64_t mako_native_fmt_printf2_ptr(MakoNativeString *fmt, MakoNativeString *a,
                                   MakoNativeString *b) {
    return mako_fmt_printf2(bridge_borrow_str(fmt), bridge_borrow_str(a),
                           bridge_borrow_str(b));
}

int64_t mako_native_fmt_eprintln_ptr(MakoNativeString *s) {
    return mako_fmt_eprintln1(bridge_borrow_str(s));
}

MakoNativeString *mako_native_fmt_sprintf2_ptr(MakoNativeString *fmt, MakoNativeString *a,
                                              MakoNativeString *b) {
    return bridge_take_str(
        mako_fmt_sprintf2(bridge_borrow_str(fmt), bridge_borrow_str(a), bridge_borrow_str(b)));
}

MakoNativeString *mako_native_fmt_errorf_ptr(MakoNativeString *fmt, MakoNativeString *a) {
    return bridge_take_str(mako_fmt_errorf1(bridge_borrow_str(fmt), bridge_borrow_str(a)));
}

int64_t mako_native_print_raw_ptr(MakoNativeString *s) {
    MakoString m = bridge_borrow_str(s);
    if (m.data && m.len) {
        fwrite(m.data, 1, m.len, stdout);
    }
    return 0;
}

// ---- channels (int) ---------------------------------------------------------

MakoChan *mako_native_chan_new(int64_t capacity) {
    return mako_chan_new(capacity);
}

int64_t mako_native_chan_send(MakoChan *c, int64_t v) {
    return mako_chan_send(c, v);
}

int64_t mako_native_chan_recv(MakoChan *c) {
    return mako_chan_recv(c);
}

int64_t mako_native_chan_try_send(MakoChan *c, int64_t v) {
    return mako_chan_try_send(c, v);
}

int64_t mako_native_chan_send_timeout(MakoChan *c, int64_t v, int64_t timeout_ms) {
    return mako_chan_send_timeout(c, v, timeout_ms);
}

int64_t mako_native_chan_close(MakoChan *c) {
    mako_chan_close(c);
    return 0;
}

void mako_native_chan_drop(MakoChan *c) {
    if (!c) return;
    mako_chan_close(c);
    pthread_mutex_destroy(&c->mu);
    pthread_cond_destroy(&c->can_send);
    pthread_cond_destroy(&c->can_recv);
    free(c->buf);
    free(c);
}

// ---- uuid / log -------------------------------------------------------------

MakoNativeString *mako_native_uuid_v4(void) {
    return bridge_take_str(mako_uuid_string(mako_uuid_v4()));
}

void mako_native_log_info_ptr(MakoNativeString *msg) {
    MakoString m = bridge_borrow_str(msg);
    // Match mako_log_info_strong: level + message line on stderr.
    fputs("INFO ", stderr);
    if (m.data && m.len) fwrite(m.data, 1, m.len, stderr);
    fputc('\n', stderr);
}

// ---- arena ------------------------------------------------------------------

MakoArena *mako_native_arena_new(void) {
    MakoArena *a = (MakoArena *)malloc(sizeof(*a));
    if (!a) abort();
    *a = mako_arena_new();
    return a;
}

void mako_native_arena_free(MakoArena *a) {
    if (!a) return;
    mako_arena_free(a);
    free(a);
}

MakoNativeString *mako_native_arena_text_ptr(MakoArena *a, MakoNativeString *s) {
    if (!a) return mako_native_string_literal_ptr("", 0);
    MakoString m = mako_arena_text(a, bridge_borrow_str(s));
    // Arena owns the buffer; clone into a native owned string for IR ownership.
    return mako_native_string_literal_ptr(m.data ? m.data : "", m.len);
}

int64_t mako_native_arena_stamp(MakoArena *a, int64_t v) {
    if (!a) return 0;
    return mako_arena_stamp(a, v);
}

// Forward decls from native_runtime.c
typedef struct {
    int64_t *data;
    size_t len;
    size_t cap;
    int64_t owned;
} MakoNativeIntSlice;
MakoNativeIntSlice *mako_native_int_slice_make_ptr(size_t len, size_t cap);
typedef struct {
    double *data;
    size_t len;
    size_t cap;
    int64_t owned;
} MakoNativeFloatSlice;
MakoNativeFloatSlice *mako_native_float_slice_make_ptr(int64_t len, int64_t cap);

MakoNativeIntSlice *mako_native_arena_ints(MakoArena *a, int64_t n) {
    if (!a || n < 0) n = 0;
    MakoIntArray arr = mako_arena_ints(a, n);
    // Clone into an owned native slice (arena memory dies with the arena).
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr((size_t)n, (size_t)n);
    if (arr.data && n > 0) {
        memcpy(out->data, arr.data, (size_t)n * sizeof(int64_t));
    }
    return out;
}

// ---- nursery / crew ---------------------------------------------------------

MakoNursery *mako_native_nursery_new(void) {
    MakoNursery *n = (MakoNursery *)malloc(sizeof(*n));
    if (!n) abort();
    *n = mako_nursery_new();
    return n;
}

void mako_native_nursery_cancel_join(MakoNursery *n) {
    if (!n) return;
    mako_nursery_cancel_join(n);
    free(n);
}

// ---- kick pack + spawn (threaded) ------------------------------------------

void *mako_native_pack_new(int64_t n) {
    if (n < 0) n = 0;
    int64_t *p = (int64_t *)calloc((size_t)n, sizeof(int64_t));
    if (!p && n > 0) abort();
    return p;
}

/* Sequential mut capture cells: shared int64 heap cell for outer+closure. */
int64_t mako_native_i64_cell_new(int64_t v) {
    int64_t *p = (int64_t *)malloc(sizeof(int64_t));
    if (!p) abort();
    *p = v;
    return (int64_t)(intptr_t)p;
}

int64_t mako_native_i64_cell_load(int64_t cell) {
    if (!cell) return 0;
    return *(int64_t *)(intptr_t)cell;
}

void mako_native_i64_cell_store(int64_t cell, int64_t v) {
    if (!cell) return;
    *(int64_t *)(intptr_t)cell = v;
}

void mako_native_i64_cell_free(int64_t cell) {
    free((void *)(intptr_t)cell);
}

void mako_native_pack_set(void *pack, int64_t i, int64_t v) {
    if (!pack || i < 0) return;
    ((int64_t *)pack)[i] = v;
}

int64_t mako_native_pack_get(void *pack, int64_t i) {
    if (!pack || i < 0) return 0;
    return ((int64_t *)pack)[i];
}

// Entry for IR trampolines: body is int64_t (*)(void *pack), returns result as void*.
typedef int64_t (*MakoNativeKickBody)(void *pack);

typedef struct {
    MakoNativeKickBody body;
    void *pack;
} MakoNativeSpawnWrap;

static void *mako_native_spawn_entry(void *arg) {
    MakoNativeSpawnWrap *w = (MakoNativeSpawnWrap *)arg;
    int64_t r = w->body ? w->body(w->pack) : 0;
    free(w->pack);
    free(w);
    return (void *)(intptr_t)r;
}

// Returns MakoTask* (as Task handle for IR).
MakoTask *mako_native_spawn_i64(MakoNursery *n, void *body_fn, void *pack) {
    if (!n) abort();
    MakoNativeSpawnWrap *w = (MakoNativeSpawnWrap *)malloc(sizeof(*w));
    if (!w) abort();
    w->body = (MakoNativeKickBody)body_fn;
    w->pack = pack;
    return mako_spawn(n, mako_native_spawn_entry, w);
}

MakoTask *mako_native_detach_spawn_i64(void *body_fn, void *pack) {
    MakoNativeSpawnWrap *w = (MakoNativeSpawnWrap *)malloc(sizeof(*w));
    if (!w) abort();
    w->body = (MakoNativeKickBody)body_fn;
    w->pack = pack;
    return mako_detach_spawn(mako_native_spawn_entry, w);
}
void mako_native_detached_join_all(void) {
    mako_detached_join_all();
}


int64_t mako_native_kick_identity(void *pack) {
    return pack ? *(int64_t *)pack : 0;
}

// Sequential fallback: spawn identity body on a process-wide nursery.
MakoTask *mako_native_task_from_i64(int64_t v) {
    static MakoNursery seq_n;
    static int seq_init = 0;
    if (!seq_init) {
        seq_n = mako_nursery_new();
        seq_init = 1;
    }
    int64_t *pack = (int64_t *)malloc(sizeof(int64_t));
    if (!pack) abort();
    *pack = v;
    MakoNativeSpawnWrap *w = (MakoNativeSpawnWrap *)malloc(sizeof(*w));
    if (!w) abort();
    w->body = mako_native_kick_identity;
    w->pack = pack;
    return mako_spawn(&seq_n, mako_native_spawn_entry, w);
}

int64_t mako_native_task_join_i64(MakoTask *t) {
    if (!t) return 0;
    // Nursery owns the task allocation; await only (no free).
    void *r = mako_await(t);
    return (int64_t)(intptr_t)r;
}

// 1 = joined (value in *out_slot via pack[0]), 0 = timeout.
int64_t mako_native_task_join_timeout_i64(MakoTask *t, int64_t ms, int64_t pack) {
    if (!t) return 0;
    int64_t out = 0;
    int64_t ok = mako_await_timeout_ms(t, ms, &out);
    if (ok) {
        // pack is intptr to mako pack; store joined i64 at index 0.
        mako_native_pack_set((void *)(uintptr_t)pack, 0, out);
        return 1;
    }
    return 0;
}

int64_t mako_native_f64_to_bits(double v) {
    union { double d; int64_t i; } u;
    u.d = v;
    return u.i;
}

double mako_native_bits_to_f64(int64_t i) {
    union { double d; int64_t i; } u;
    u.i = i;
    return u.d;
}

void mako_native_task_drop(MakoTask *t) {
    // Nursery cancel/join frees tasks registered on it. A Task local that
    // was joined is still owned by the nursery — do not free here.
    (void)t;
}

// ---- select among int channels ---------------------------------------------

int64_t mako_native_chan_select1(MakoChan *a, int64_t ms) {
    MakoChan *chs[1] = {a};
    return mako_chan_selectn(chs, 1, ms);
}

int64_t mako_native_chan_select2(MakoChan *a, MakoChan *b, int64_t ms) {
    return mako_chan_select2(a, b, ms);
}

int64_t mako_native_chan_select3(MakoChan *a, MakoChan *b, MakoChan *c, int64_t ms) {
    return mako_chan_select3(a, b, c, ms);
}

int64_t mako_native_chan_select4(MakoChan *a, MakoChan *b, MakoChan *c, MakoChan *d,
                                 int64_t ms) {
    return mako_chan_select4(a, b, c, d, ms);
}

int64_t mako_native_chan_select_value(void) {
    return mako_chan_select_value();
}

// ---- http path/method, json, regex ----------------------------------------

MakoNativeString *mako_native_http_path_ptr(int64_t conn) {
    return bridge_take_str(mako_http_path(conn));
}

MakoNativeString *mako_native_http_method_ptr(int64_t conn) {
    return bridge_take_str(mako_http_method(conn));
}

MakoNativeString *mako_native_json_array_ints3(int64_t a, int64_t b, int64_t c) {
    return bridge_take_str(mako_json_array_ints3(a, b, c));
}

MakoNativeString *mako_native_json_object_ptr(MakoNativeString *k, MakoNativeString *v) {
    return bridge_take_str(mako_json_object_str(bridge_borrow_str(k), bridge_borrow_str(v)));
}

MakoNativeString *mako_native_json_merge_ptr(MakoNativeString *a, MakoNativeString *b) {
    return bridge_take_str(mako_json_merge(bridge_borrow_str(a), bridge_borrow_str(b)));
}

MakoNativeString *mako_native_json_nest_ptr(MakoNativeString *k, MakoNativeString *v) {
    return bridge_take_str(mako_json_nest(bridge_borrow_str(k), bridge_borrow_str(v)));
}

int64_t mako_native_json_array_len_ptr(MakoNativeString *j) {
    return mako_json_array_len(bridge_borrow_str(j));
}

int64_t mako_native_json_array_get_int_ptr(MakoNativeString *j, int64_t i) {
    return mako_json_array_get_int(bridge_borrow_str(j), i);
}

MakoNativeString *mako_native_json_array_get_string_ptr(MakoNativeString *j, int64_t i) {
    return bridge_take_str(mako_json_array_get_string(bridge_borrow_str(j), i));
}

MakoNativeString *mako_native_json_array_push_int_ptr(MakoNativeString *j, int64_t v) {
    return bridge_take_str(mako_json_array_push_int(bridge_borrow_str(j), v));
}

MakoNativeString *mako_native_json_array_strings2_ptr(MakoNativeString *a, MakoNativeString *b) {
    return bridge_take_str(
        mako_json_array_strings2(bridge_borrow_str(a), bridge_borrow_str(b)));
}

int64_t mako_native_regex_match_ptr(MakoNativeString *pat, MakoNativeString *text) {
    return mako_regex_match(bridge_borrow_str(pat), bridge_borrow_str(text)) ? 1 : 0;
}

MakoNativeString *mako_native_regex_find_ptr(MakoNativeString *pat, MakoNativeString *text) {
    return bridge_take_str(mako_regex_find(bridge_borrow_str(pat), bridge_borrow_str(text)));
}

// ---- misc availability stubs / simple helpers ------------------------------

int64_t mako_native_h3_server_available(void) {
    return mako_h3_server_available();
}

int64_t mako_native_llm_https_available(void) {
    return 0;
}

int64_t mako_native_http2_detect_ptr(MakoNativeString *s) {
    return mako_http2_detect(bridge_borrow_str(s)) ? 1 : 0;
}

MakoNativeString *mako_native_http2_empty_settings(void) {
    return bridge_take_str(mako_http2_empty_settings());
}

// ---- ptr slice ([]struct) ---------------------------------------------------

typedef struct {
    void **data;
    size_t len;
    size_t cap;
    int64_t owned;
} MakoNativePtrSlice;

MakoNativePtrSlice *mako_native_ptr_slice_make(int64_t len, int64_t cap) {
    if (cap < len) cap = len;
    if (cap < 0) cap = 0;
    MakoNativePtrSlice *s = (MakoNativePtrSlice *)calloc(1, sizeof(*s));
    if (!s) abort();
    if (cap > 0) {
        s->data = (void **)calloc((size_t)cap, sizeof(void *));
        if (!s->data) abort();
    }
    s->len = (size_t)len;
    s->cap = (size_t)cap;
    s->owned = 1;
    return s;
}

int64_t mako_native_ptr_slice_len(const MakoNativePtrSlice *s) {
    return s ? (int64_t)s->len : 0;
}

void *mako_native_ptr_slice_get(const MakoNativePtrSlice *s, int64_t i) {
    if (!s || i < 0 || (size_t)i >= s->len) abort();
    return s->data[i];
}

void mako_native_ptr_slice_set(MakoNativePtrSlice *s, int64_t i, void *elem) {
    if (!s || i < 0 || (size_t)i >= s->len) abort();
    s->data[i] = elem;
}

MakoNativePtrSlice *mako_native_ptr_slice_append(MakoNativePtrSlice *s, void *elem) {
    if (!s) abort();
    if (s->owned && s->len < s->cap) {
        s->data[s->len++] = elem;
        return s;
    }
    size_t ncap = s->cap ? s->cap * 2 : 4;
    if (ncap < s->len + 1) ncap = s->len + 1;
    MakoNativePtrSlice *out = mako_native_ptr_slice_make((int64_t)s->len + 1, (int64_t)ncap);
    for (size_t i = 0; i < s->len; ++i) out->data[i] = s->data[i];
    out->data[s->len] = elem;
    // Steal elements from s; clear so drop doesn't free them.
    if (s->owned) {
        free(s->data);
        s->data = NULL;
        s->len = s->cap = 0;
        free(s);
    }
    return out;
}

void mako_native_ptr_slice_drop(MakoNativePtrSlice *s) {
    /* Shell-only drop: free the pointer array + header, not the elements.
     *
     * Elements may be:
     * - shared handles (chan/opaque/task) that must not be free()'d
     * - nested slice/map headers that need typed drops (IR emits those first
     *   when required; free(header) alone would leak their data arrays)
     * - struct blocks owned by StructSlice — those use
     *   mako_native_ptr_slice_drop_free_elems instead.
     *
     * Blind free(elem) here corrupted channels stored in []chan[T] /
     * [][]chan[T] and double-freed with mako_native_chan_drop. */
    if (!s) return;
    if (s->owned && s->data) {
        free(s->data);
    }
    free(s);
}

void mako_native_ptr_slice_drop_free_elems(MakoNativePtrSlice *s) {
    /* Drop for []Struct / pointer arrays of flat calloc blocks. */
    if (!s) return;
    if (s->owned && s->data) {
        for (size_t i = 0; i < s->len; ++i) free(s->data[i]);
        free(s->data);
    }
    free(s);
}

MakoNativePtrSlice *mako_native_ptr_slice_clone(const MakoNativePtrSlice *s) {
    if (!s) return NULL;
    MakoNativePtrSlice *out = mako_native_ptr_slice_make((int64_t)s->len, (int64_t)s->len);
    // Shallow: elements must already be independently owned clones (IR clones
    // each struct before append/set). For clone of the slice, deep-copy bytes
    // is not possible without layout — duplicate pointers only when unowned
    // views; for owned, allocate fresh copies of each block is unknown size.
    // Contract: IR always deep-clones elements before storing; clone of slice
    // re-clones by reusing same pointers only if we treat elements as shared.
    // Safer: for Point-like (caller must not mutate), share pointers + bump not
    // available. Deep clone: not supported at C level; IR emits element clones.
    for (size_t i = 0; i < s->len; ++i) out->data[i] = s->data[i];
    // Mark as unowned view so drop doesn't free shared elements? No — that
    // double-frees on drop of both. For clone, IR uses StructClone per element.
    // This helper is only used when IR passes pre-cloned element pointers.
    return out;
}

/* Deep clone pointer-slice of POD heap blocks (elem_bytes each). */
MakoNativePtrSlice *mako_native_ptr_slice_clone_deep(const MakoNativePtrSlice *s, int64_t elem_bytes) {
    if (!s) return NULL;
    if (elem_bytes <= 0) elem_bytes = 8;
    MakoNativePtrSlice *out = mako_native_ptr_slice_make((int64_t)s->len, (int64_t)s->len);
    for (size_t i = 0; i < s->len; ++i) {
        void *e = s->data[i];
        if (!e) { out->data[i] = NULL; continue; }
        void *c = calloc(1, (size_t)elem_bytes);
        if (!c) abort();
        memcpy(c, e, (size_t)elem_bytes);
        out->data[i] = c;
    }
    return out;
}

/* Subslice of pointer array [low:high); owned shallow copy of pointers. */
MakoNativePtrSlice *mako_native_ptr_slice_slice(const MakoNativePtrSlice *s, int64_t low, int64_t high, int64_t max) {
    (void)max;
    if (!s) return mako_native_ptr_slice_make(0, 0);
    int64_t n = (int64_t)s->len;
    if (low < 0) low = 0;
    if (high < 0 || high > n) high = n;
    if (low > high) low = high;
    int64_t len = high - low;
    MakoNativePtrSlice *out = mako_native_ptr_slice_make(len, len);
    for (int64_t i = 0; i < len; ++i) out->data[i] = s->data[low + i];
    return out;
}

// ---- select N ---------------------------------------------------------------

int64_t mako_native_chan_selectn(void *chs_pack, int64_t n, int64_t ms) {
    if (n <= 0 || n > 16) return -1;
    MakoChan *chs[16];
    for (int64_t i = 0; i < n; ++i)
        chs[i] = (MakoChan *)(intptr_t)mako_native_pack_get(chs_pack, i);
    return mako_chan_selectn(chs, (int)n, ms);
}

// ---- bulk interop wrappers --------------------------------------------------

int64_t mako_native_http_respond_ptr(int64_t conn, int64_t status, MakoNativeString *body) {
    return mako_http_respond(conn, status, bridge_borrow_str(body));
}

MakoNativeString *mako_native_http_header_ptr(int64_t conn, MakoNativeString *name) {
    return bridge_take_str(mako_http_header(conn, bridge_borrow_str(name)));
}

int64_t mako_native_ws_echo_once(int64_t port) {
    return mako_ws_echo_once(port);
}

int64_t mako_native_sqlite_query_int_ptr(MakoNativeString *path, MakoNativeString *sql) {
    return mako_sqlite_query_int(bridge_borrow_str(path), bridge_borrow_str(sql));
}

int64_t mako_native_nb_listen(int64_t port) {
    return mako_nb_listen(port);
}

int64_t mako_native_io_poll2(int64_t a, int64_t b, int64_t ms) {
    return mako_io_poll2(a, b, ms);
}

int64_t mako_native_io_kq_poll2(int64_t a, int64_t b, int64_t ms) {
    return mako_io_kq_poll2(a, b, ms);
}

int64_t mako_native_io_poll4(int64_t a, int64_t b, int64_t c, int64_t d, int64_t ms) {
    // Fallback: poll first two if no poll4 in net.h
    return mako_io_poll4(a, b, c, d, ms);
}

int64_t mako_native_io_wait(int64_t fd, int64_t ms) {
    return mako_io_wait(fd, ms);
}

int64_t mako_native_io_native_poll2(int64_t a, int64_t b, int64_t ms) {
    return mako_io_poll2(a, b, ms);
}

void mako_native_metric_inc(int64_t id) {
    mako_metric_inc(id);
}

void mako_native_log_warn_ptr(MakoNativeString *msg) {
    MakoString m = bridge_borrow_str(msg);
    fputs("WARN ", stderr);
    if (m.data && m.len) fwrite(m.data, 1, m.len, stderr);
    fputc('\n', stderr);
}

MakoNativeString *mako_native_json_i_ptr(MakoNativeString *k, int64_t v) {
    return bridge_take_str(mako_json_i(bridge_borrow_str(k), v));
}

int64_t mako_native_json_has_ptr(MakoNativeString *j, MakoNativeString *k) {
    // Presence check via path/get — use has_string with empty expect as weak has.
    MakoString js = bridge_borrow_str(j);
    MakoString key = bridge_borrow_str(k);
    // mako_json_get_int returns 0 if missing; check via get_string non-empty path
    MakoString s = mako_json_get_string(js, key);
    int64_t ok = (s.data && s.len > 0) ? 1 : 0;
    mako_str_free(s);
    return ok;
}

MakoNativeString *mako_native_json_path_string_ptr(MakoNativeString *j, MakoNativeString *k1,
                                                  MakoNativeString *k2) {
    return bridge_take_str(mako_json_path_string(
        bridge_borrow_str(j), bridge_borrow_str(k1), bridge_borrow_str(k2)));
}

MakoNativeString *mako_native_json_array_push_string_ptr(MakoNativeString *arr,
                                                        MakoNativeString *s) {
    return bridge_take_str(
        mako_json_array_push_string(bridge_borrow_str(arr), bridge_borrow_str(s)));
}

MakoNativeString *mako_native_pb_encode_varint(int64_t v) {
    return bridge_take_str(mako_pb_encode_varint(v));
}

MakoNativeString *mako_native_pb_encode_key(int64_t field, int64_t wire) {
    return bridge_take_str(mako_pb_encode_key(field, wire));
}

MakoNativeString *mako_native_pb_encode_simple_ptr(MakoNativeString *name, int64_t id) {
    return bridge_take_str(mako_pb_encode_simple(bridge_borrow_str(name), id));
}

// uuid demos often pass the string from uuid_v4(); accept *MakoNativeString.
MakoNativeString *mako_native_uuid_string_ptr(MakoNativeString *s) {
    if (!s) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_str_clone(bridge_borrow_str(s)));
}

// uuid_v4 already returns string; uuid_string on Uuid type uses opaque.

int64_t mako_native_mail_msg_new(void) {
    return mako_mail_msg_new();
}

int64_t mako_native_smtp_mock_serve_once(void) {
    return mako_smtp_mock_serve_once();
}

// TLS — real OpenSSL path when MAKO_HAS_OPENSSL (header stubs otherwise).
int64_t mako_native_tls_serve_once(int64_t port) {
    (void)port;
    return -1; /* use 4-arg form */
}
int64_t mako_native_tls_serve_n(int64_t port, int64_t n) {
    (void)port; (void)n;
    return -1; /* use 5-arg form */
}
int64_t mako_native_tls_serve_h2_routes(int64_t port) {
    (void)port;
    return -1; /* use 6-arg form */
}
MakoNativeString *mako_native_tls_get_ptr(MakoNativeString *url) {
    (void)url;
    return mako_native_string_literal_ptr("", 0);
}
MakoNativeString *mako_native_tls_get_insecure_ptr(MakoNativeString *url) {
    (void)url;
    return mako_native_string_literal_ptr("", 0);
}

int64_t mako_native_redis_mock_once(int64_t port) {
    return mako_redis_mock_once(port);
}

MakoNativeString *mako_native_regex_capture_ptr(MakoNativeString *pat, MakoNativeString *text,
                                               int64_t group) {
    return bridge_take_str(
        mako_regex_capture(bridge_borrow_str(pat), bridge_borrow_str(text), group));
}

int64_t mako_native_http2_settings_len(void) {
    return 0;
}

/* IR maps http2_detect → mako_native_http2_detect with Str arg as pointer. */
int64_t mako_native_http2_detect(MakoNativeString *s) {
    return mako_http2_detect(bridge_borrow_str(s)) ? 1 : 0;
}

MakoNativeString *mako_native_llm_api_key(void) {
    return mako_native_string_literal_ptr("", 0);
}

int64_t mako_native_gpu_device_open(void) {
    return mako_gpu_device_open();
}

int64_t mako_native_h3_server_new(int64_t port) {
    (void)port;
    return -1; /* use cert/key form */
}

/* mako_native_grpc_http2_unary_ptr defined later with full arity */

MakoNativeString *mako_native_sip_branch(void) {
    return bridge_take_str(mako_sip_branch());
}

// str_builder / tmpl_data_new defined later with real APIs

// nursery cancel / cancelled
void mako_native_nursery_cancel(MakoNursery *n) {
    if (n) mako_nursery_cancel(n);
}

int64_t mako_native_nursery_cancelled(MakoNursery *n) {
    return n ? mako_nursery_cancelled(n) : 0;
}

// Sequential int map for fan
MakoNativeIntSlice *mako_native_int_map_apply(MakoNativeIntSlice *src, void *fn_ptr) {
    typedef int64_t (*MapFn)(int64_t);
    MapFn fn = (MapFn)fn_ptr;
    int64_t n = src ? (int64_t)src->len : 0;
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr((size_t)n, (size_t)n);
    for (int64_t i = 0; i < n; ++i)
        out->data[i] = fn(src->data[i]);
    return out;
}

void mako_native_opaque_drop(void *p) {
    free(p);
}

// parse_int → try: returns 1 and writes *out on success, else 0.
int64_t mako_native_parse_int_try_ptr(MakoNativeString *s, int64_t *out) {
    MakoResultInt r = mako_parse_int(bridge_borrow_str(s));
    if (r.ok) {
        if (out) *out = r.value;
        // free err string if any
        return 1;
    }
    mako_str_free(r.err);
    if (out) *out = 0;
    return 0;
}

/* parse_int_base / hex / bin / oct / auto: base 0 = auto (0x/0b/0o). */
int64_t mako_native_parse_int_base_try_ptr(MakoNativeString *s, int64_t base, int64_t *out) {
    MakoResultInt r = mako_parse_int_base(bridge_borrow_str(s), base);
    if (r.ok) {
        if (out) *out = r.value;
        return 1;
    }
    mako_str_free(r.err);
    if (out) *out = 0;
    return 0;
}

/* parse_bool → try: 1 and *out=0/1 on success, else 0. */
int64_t mako_native_parse_bool_try_ptr(MakoNativeString *s, int64_t *out) {
    MakoResultInt r = mako_parse_bool(bridge_borrow_str(s));
    if (r.ok) {
        if (out) *out = r.value;
        return 1;
    }
    mako_str_free(r.err);
    if (out) *out = 0;
    return 0;
}

int64_t mako_native_chan_recv_ok(MakoChan *c, int64_t *out) {
    return mako_chan_recv_ok(c, out);
}

double mako_native_parse_float_ptr(MakoNativeString *s) {
    return mako_parse_float(bridge_borrow_str(s));
}

void mako_native_pack_free(void *pack) {
    free(pack);
}


int64_t mako_native_str_has_prefix_ptr(MakoNativeString *s, MakoNativeString *pref) {
    return mako_str_has_prefix(bridge_borrow_str(s), bridge_borrow_str(pref));
}

void mako_native_metric_add(int64_t id, int64_t delta) {
    mako_metric_add(id, delta);
}

MakoNativeString *mako_native_sqlite_query_text_ptr(MakoNativeString *path, MakoNativeString *sql) {
    return bridge_take_str(mako_sqlite_query_text(bridge_borrow_str(path), bridge_borrow_str(sql)));
}

MakoNativeString *mako_native_json_get_string_ptr(MakoNativeString *j, MakoNativeString *k) {
    return bridge_take_str(mako_json_get_string(bridge_borrow_str(j), bridge_borrow_str(k)));
}

int64_t mako_native_json_path_int_ptr(MakoNativeString *j, MakoNativeString *k1, MakoNativeString *k2) {
    return mako_json_path_int(bridge_borrow_str(j), bridge_borrow_str(k1), bridge_borrow_str(k2));
}

/* Native runtime map slot helpers (defined in native_runtime.c). */
int64_t mako_native_map_ss_cap_ptr(const void *m);
int64_t mako_native_map_ss_slot_full_ptr(const void *m, int64_t i);
MakoNativeString *mako_native_map_ss_slot_key_ptr(const void *m, int64_t i);
MakoNativeString *mako_native_map_ss_slot_val_ptr(const void *m, int64_t i);

/* Build JSON object from native MapSS (pointer ABI), not C MakoMapSS. */
MakoNativeString *mako_native_json_object_from_map_ss(void *m) {
    if (!m) {
        return bridge_take_str(mako_str_from_cstr("{}"));
    }
    int64_t cap = mako_native_map_ss_cap_ptr(m);
    MakoMapSS cm = mako_map_ss_new(8);
    for (int64_t i = 0; i < cap; i++) {
        if (!mako_native_map_ss_slot_full_ptr(m, i)) continue;
        MakoNativeString *k = mako_native_map_ss_slot_key_ptr(m, i);
        MakoNativeString *v = mako_native_map_ss_slot_val_ptr(m, i);
        if (k && v) {
            mako_map_ss_set(&cm, bridge_borrow_str(k), bridge_borrow_str(v));
        }
        if (k) mako_native_string_drop_ptr(k);
        if (v) mako_native_string_drop_ptr(v);
    }
    MakoString j = mako_json_object_from_map_ss(&cm);
    for (size_t i = 0; i < cm.cap; i++) {
        if (cm.state[i] == MAKO_MAP_FULL) {
            mako_str_free(cm.keys[i]);
            mako_str_free(cm.vals[i]);
        }
    }
    free(cm.state);
    free(cm.keys);
    free(cm.vals);
    return bridge_take_str(j);
}

int64_t mako_native_http_respond_ct_ptr(int64_t conn, int64_t status, MakoNativeString *ct,
                                       MakoNativeString *body) {
    return mako_http_respond_ct(conn, status, bridge_borrow_str(ct), bridge_borrow_str(body));
}

int64_t mako_native_http_keepalive(int64_t conn) {
    return mako_http_keepalive(conn);
}

int64_t mako_native_http_close(int64_t conn) {
    return mako_http_close(conn);
}

int64_t mako_native_http_close_listener(int64_t fd) {
    return mako_http_close_listener(fd);
}

int64_t mako_native_pb_decode_varint_ptr(MakoNativeString *s) {
    return mako_pb_decode_varint(bridge_borrow_str(s));
}

int64_t mako_native_pb_key_field_ptr(MakoNativeString *s) {
    return mako_pb_key_field(bridge_borrow_str(s));
}

MakoNativeString *mako_native_pb_simple_name_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_pb_simple_name(bridge_borrow_str(s)));
}

void mako_native_log_error_ptr(MakoNativeString *msg) {
    MakoString m = bridge_borrow_str(msg);
    fputs("ERROR ", stderr);
    if (m.data && m.len) fwrite(m.data, 1, m.len, stderr);
    fputc('\n', stderr);
}

MakoNativeString *mako_native_redis_ping_ptr(MakoNativeString *host, int64_t port) {
    return bridge_take_str(mako_redis_ping(bridge_borrow_str(host), port));
}

int64_t mako_native_mail_msg_set_from_ptr(int64_t h, MakoNativeString *from) {
    return mako_mail_msg_set_from(h, bridge_borrow_str(from));
}


// ---- second-wave interop (uuid / metrics / builder / tmpl / mail / pb / crypto) ----

int64_t mako_native_uuid_parse_ok_ptr(MakoNativeString *s) {
    return mako_uuid_parse_ok(bridge_borrow_str(s)) ? 1 : 0;
}

MakoNativeString *mako_native_uuid_parse_ptr(MakoNativeString *s) {
    bool ok = false;
    MakoUuid u = mako_uuid_parse(bridge_borrow_str(s), &ok);
    if (!ok) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_uuid_string(u));
}

int64_t mako_native_uuid_eq_ptr(MakoNativeString *a, MakoNativeString *b) {
    bool oka = false, okb = false;
    MakoString sa = bridge_borrow_str(a);
    MakoString sb = bridge_borrow_str(b);
    MakoUuid ua = mako_uuid_parse(sa, &oka);
    if (!oka) ua = mako_ulid_parse(sa, &oka);
    MakoUuid ub = mako_uuid_parse(sb, &okb);
    if (!okb) ub = mako_ulid_parse(sb, &okb);
    if (!oka || !okb) return 0;
    return mako_uuid_eq(ua, ub) ? 1 : 0;
}

int64_t mako_native_uuid_is_nil_ptr(MakoNativeString *s) {
    bool ok = false;
    MakoUuid u = mako_uuid_parse(bridge_borrow_str(s), &ok);
    /* Failed parse / empty string is treated as nil (matches C backend). */
    if (!ok) return 1;
    return mako_uuid_is_nil(u) ? 1 : 0;
}

MakoNativeString *mako_native_uuid_nil(void) {
    return bridge_take_str(mako_uuid_string(mako_uuid_nil()));
}

int64_t mako_native_uuid_check_ptr(MakoNativeString *s) {
    return mako_uuid_parse_ok(bridge_borrow_str(s)) ? 1 : 0;
}

int64_t mako_native_str_has_suffix_ptr(MakoNativeString *s, MakoNativeString *suf) {
    return mako_str_has_suffix(bridge_borrow_str(s), bridge_borrow_str(suf));
}

MakoNativeString *mako_native_http_body_ptr(int64_t conn) {
    return bridge_take_str(mako_http_body(conn));
}

int64_t mako_native_http_next(int64_t conn) {
    return mako_http_next(conn);
}

int64_t mako_native_pb_varint_len_ptr(MakoNativeString *s) {
    return mako_pb_varint_len(bridge_borrow_str(s));
}

int64_t mako_native_pb_key_wire_ptr(MakoNativeString *s) {
    return mako_pb_key_wire(bridge_borrow_str(s));
}

int64_t mako_native_pb_simple_id_ptr(MakoNativeString *s) {
    return mako_pb_simple_id(bridge_borrow_str(s));
}

int64_t mako_native_json_has_string_ptr(MakoNativeString *j, MakoNativeString *k,
                                       MakoNativeString *expect) {
    return mako_json_has_string(bridge_borrow_str(j), bridge_borrow_str(k),
                               bridge_borrow_str(expect));
}

int64_t mako_native_json_get_int_ptr(MakoNativeString *j, MakoNativeString *k) {
    return mako_json_get_int(bridge_borrow_str(j), bridge_borrow_str(k));
}

MakoNativeString *mako_native_json_get_object_ptr(MakoNativeString *j, MakoNativeString *k) {
    return bridge_take_str(mako_json_get_object(bridge_borrow_str(j), bridge_borrow_str(k)));
}

int64_t mako_native_metric_get(int64_t id) { return mako_metric_get(id); }
void mako_native_gauge_set(int64_t id, int64_t v) { mako_gauge_set(id, v); }
void mako_native_gauge_add(int64_t id, int64_t d) { mako_gauge_add(id, d); }
int64_t mako_native_gauge_get(int64_t id) { return mako_gauge_get(id); }
void mako_native_hist_observe(int64_t id, int64_t v) { mako_hist_observe(id, v); }
int64_t mako_native_hist_count(int64_t id) { return mako_hist_count(id); }
int64_t mako_native_hist_sum(int64_t id) { return mako_hist_sum(id); }
int64_t mako_native_hist_avg(int64_t id) { return mako_hist_avg(id); }

MakoNativeString *mako_native_metrics_export(void) {
    return bridge_take_str(mako_metrics_export());
}

MakoNativeString *mako_native_sha256_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_sha256_hex(bridge_borrow_str(s)));
}

MakoNativeString *mako_native_hmac_sha256_ptr(MakoNativeString *k, MakoNativeString *m) {
    return bridge_take_str(mako_hmac_sha256_hex(bridge_borrow_str(k), bridge_borrow_str(m)));
}

MakoNativeString *mako_native_bin_encode_int(int64_t v) {
    return bridge_take_str(mako_bin_encode_int(v));
}

int64_t mako_native_safe_add(int64_t a, int64_t b) {
    return mako_safe_add(a, b);
}

void *mako_native_str_builder(void) {
    return mako_str_builder_new();
}

void mako_native_builder_write_ptr(void *b, MakoNativeString *s) {
    if (b) mako_str_builder_write((MakoStrBuilder *)b, bridge_borrow_str(s));
}

void mako_native_builder_write_byte(void *b, int64_t v) {
    if (b) mako_str_builder_write_byte((MakoStrBuilder *)b, v);
}

int64_t mako_native_builder_len(void *b) {
    return b ? (int64_t)((MakoStrBuilder *)b)->len : 0;
}

MakoNativeString *mako_native_builder_string(void *b) {
    if (!b) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_str_builder_string((MakoStrBuilder *)b));
}

int64_t mako_native_tmpl_data_new(void) {
    return mako_tmpl_data_new();
}

int64_t mako_native_tmpl_data_set_ptr(int64_t h, MakoNativeString *k, MakoNativeString *v) {
    return mako_tmpl_data_set(h, bridge_borrow_str(k), bridge_borrow_str(v));
}

int64_t mako_native_tmpl_data_set_list_ptr(int64_t h, MakoNativeString *k, MakoNativeString *csv) {
    return mako_tmpl_data_set_list(h, bridge_borrow_str(k), bridge_borrow_str(csv));
}

MakoNativeString *mako_native_tmpl_text_ptr(MakoNativeString *src, int64_t h) {
    return bridge_take_str(mako_tmpl_text(bridge_borrow_str(src), h));
}

MakoNativeString *mako_native_tmpl_html_ptr(MakoNativeString *src, int64_t h) {
    return bridge_take_str(mako_tmpl_html(bridge_borrow_str(src), h));
}

int64_t mako_native_tmpl_data_free(int64_t h) {
    return mako_tmpl_data_free(h);
}

int64_t mako_native_mail_msg_add_to_ptr(int64_t h, MakoNativeString *a) {
    return mako_mail_msg_add_to(h, bridge_borrow_str(a));
}

int64_t mako_native_mail_msg_set_subject_ptr(int64_t h, MakoNativeString *s) {
    return mako_mail_msg_set_subject(h, bridge_borrow_str(s));
}

int64_t mako_native_mail_msg_set_text_ptr(int64_t h, MakoNativeString *s) {
    return mako_mail_msg_set_text(h, bridge_borrow_str(s));
}

int64_t mako_native_mail_msg_set_html_ptr(int64_t h, MakoNativeString *s) {
    return mako_mail_msg_set_html(h, bridge_borrow_str(s));
}

int64_t mako_native_mail_msg_attach_ptr(int64_t h, MakoNativeString *name,
                                        MakoNativeString *ct, MakoNativeString *data) {
    return mako_mail_msg_attach(h, bridge_borrow_str(name), bridge_borrow_str(ct),
                                bridge_borrow_str(data));
}

MakoNativeString *mako_native_mail_msg_build(int64_t h) {
    return bridge_take_str(mako_mail_msg_build(h));
}

int64_t mako_native_mail_msg_free(int64_t h) {
    return mako_mail_msg_free(h);
}

int64_t mako_native_smtp_send_msg_ptr(MakoNativeString *host, int64_t port,
                                      MakoNativeString *user, MakoNativeString *pass,
                                      int64_t msg, int64_t tls) {
    return mako_smtp_send_msg(bridge_borrow_str(host), port, bridge_borrow_str(user),
                              bridge_borrow_str(pass), msg, tls);
}

int64_t mako_native_hpack_decoded_count_ptr(MakoNativeString *s) {
    (void)s;
    return 0;
}

int64_t mako_native_evloop_new(void) {
    /* Opaque handle as pointer cast for IR I64 ABI. */
    return (int64_t)(intptr_t)mako_evloop_new();
}

MakoNativeString *mako_native_sip_tag(void) {
    return bridge_take_str(mako_sip_tag());
}

MakoNativeString *mako_native_llm_content_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_llm_content(bridge_borrow_str(s)));
}

MakoNativeString *mako_native_gpu_device_backend(int64_t d) {
    return bridge_take_str(mako_gpu_device_backend(d));
}

int64_t mako_native_actor_pack(int64_t tag, int64_t payload) {
    return mako_actor_pack(tag, payload);
}

int64_t mako_native_str_index_ptr(MakoNativeString *hay, MakoNativeString *needle) {
    return mako_str_index(bridge_borrow_str(hay), bridge_borrow_str(needle));
}

int64_t mako_native_actor_msg_tag(int64_t m) {
    return mako_actor_msg_tag(m);
}

int64_t mako_native_actor_msg_payload(int64_t m) {
    return mako_actor_msg_payload(m);
}

int64_t mako_native_str_last_index_ptr(MakoNativeString *hay, MakoNativeString *needle) {
    return mako_str_last_index(bridge_borrow_str(hay), bridge_borrow_str(needle));
}

void mako_native_actor_stop(MakoChan *a) {
    if (a) mako_actor_stop(a);
}

MakoNativeIntSlice *mako_native_slice_ints(MakoNativeIntSlice *s, int64_t lo, int64_t hi) {
    if (!s) return mako_native_int_slice_make_ptr(0, 0);
    if (lo < 0) lo = 0;
    if (hi < lo) hi = lo;
    if ((size_t)hi > s->len) hi = (int64_t)s->len;
    if ((size_t)lo > s->len) lo = (int64_t)s->len;
    int64_t n = hi - lo;
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr((size_t)n, (size_t)n);
    for (int64_t i = 0; i < n; ++i) out->data[i] = s->data[lo + i];
    return out;
}

int64_t mako_native_slice_get(MakoNativeIntSlice *s, int64_t i) {
    if (!s || i < 0 || (size_t)i >= s->len) abort();
    return s->data[i];
}

int64_t mako_native_slice_len(MakoNativeIntSlice *s) {
    return s ? (int64_t)s->len : 0;
}

int64_t mako_native_tls_serve_once_ptr(int64_t port, MakoNativeString *cert,
                                       MakoNativeString *key, MakoNativeString *body) {
    return mako_tls_serve_once(port, bridge_borrow_str(cert), bridge_borrow_str(key),
                               bridge_borrow_str(body));
}

int64_t mako_native_tls_serve_n_ptr(int64_t port, MakoNativeString *cert,
                                    MakoNativeString *key, MakoNativeString *body, int64_t n) {
    return mako_tls_serve_n(port, bridge_borrow_str(cert), bridge_borrow_str(key),
                            bridge_borrow_str(body), n);
}

int64_t mako_native_tls_serve_h2_routes_ptr(int64_t port, MakoNativeString *cert,
                                            MakoNativeString *key, MakoNativeString *body,
                                            MakoNativeString *health, int64_t n) {
    return mako_tls_serve_h2_routes(port, bridge_borrow_str(cert), bridge_borrow_str(key),
                                    bridge_borrow_str(body), bridge_borrow_str(health), n);
}

MakoNativeString *mako_native_tls_get_full_ptr(MakoNativeString *host, int64_t port,
                                              MakoNativeString *path, MakoNativeString *ca) {
    return bridge_take_str(mako_tls_get(bridge_borrow_str(host), port,
                                        bridge_borrow_str(path), bridge_borrow_str(ca)));
}

MakoNativeString *mako_native_tls_get_insecure_full_ptr(MakoNativeString *host, int64_t port,
                                                       MakoNativeString *path) {
    return bridge_take_str(mako_tls_get_insecure(bridge_borrow_str(host), port,
                                                 bridge_borrow_str(path)));
}

int64_t mako_native_http2_settings_len_ptr(MakoNativeString *s) {
    return mako_http2_settings_len(bridge_borrow_str(s));
}

int64_t mako_native_hpack_decoded_count(void) {
    return mako_hpack_decoded_count();
}

MakoNativeString *mako_native_hpack_decoded_name(int64_t i) {
    return bridge_take_str(mako_hpack_decoded_name(i));
}

MakoNativeString *mako_native_hpack_decoded_value(int64_t i) {
    return bridge_take_str(mako_hpack_decoded_value(i));
}

MakoNativeString *mako_native_grpc_http2_unary_ptr(int64_t stream, MakoNativeString *name, int64_t id) {
    return bridge_take_str(mako_grpc_http2_unary(stream, bridge_borrow_str(name), id));
}

MakoNativeString *mako_native_grpc_http2_unary_payload_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_grpc_http2_unary_payload(bridge_borrow_str(s)));
}

MakoNativeString *mako_native_grpc_http2_unary_response_ptr(int64_t stream, MakoNativeString *name, int64_t id) {
    return bridge_take_str(mako_grpc_http2_unary_response(stream, bridge_borrow_str(name), id));
}

int64_t mako_native_grpc_http2_response_status_ptr(MakoNativeString *s) {
    return mako_grpc_http2_response_status(bridge_borrow_str(s));
}

MakoNativeString *mako_native_grpc_unary_name_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_grpc_unary_name(bridge_borrow_str(s)));
}

int64_t mako_native_grpc_unary_id_ptr(MakoNativeString *s) {
    return mako_grpc_unary_id(bridge_borrow_str(s));
}

MakoNativeString *mako_native_str_trim_ptr(MakoNativeString *s, MakoNativeString *cut) {
    return bridge_take_str(mako_str_trim(bridge_borrow_str(s), bridge_borrow_str(cut)));
}

MakoNativeString *mako_native_str_trim_space_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_str_trim_space(bridge_borrow_str(s)));
}

MakoNativeString *mako_native_sip_call_id_new_ptr(MakoNativeString *host) {
    return bridge_take_str(mako_sip_call_id_new(bridge_borrow_str(host)));
}

MakoNativeString *mako_native_gpu_device_name(int64_t d) {
    return bridge_take_str(mako_gpu_device_name(d));
}

MakoNativeString *mako_native_llm_redact_key_ptr(MakoNativeString *k) {
    /* Redact without full llm.h: show first 4 chars + "…". */
    MakoString s = bridge_borrow_str(k);
    if (!s.data || s.len == 0) return mako_native_string_literal_ptr("", 0);
    size_t n = s.len < 4 ? s.len : 4;
    char buf[16];
    size_t i = 0;
    for (; i < n && i < sizeof(buf) - 4; ++i) buf[i] = s.data[i];
    buf[i++] = '.'; buf[i++] = '.'; buf[i++] = '.'; buf[i] = 0;
    return bridge_take_str(mako_str_from_cstr(buf));
}

int64_t mako_native_evloop_add(int64_t el, int64_t fd, int64_t flags) {
    return mako_evloop_add((MakoEvLoop *)(intptr_t)el, fd, flags);
}

int64_t mako_native_evloop_wait(int64_t el, int64_t ms) {
    return mako_evloop_wait((MakoEvLoop *)(intptr_t)el, ms);
}

int64_t mako_native_evloop_event_fd(int64_t el, int64_t i) {
    return mako_evloop_event_fd((MakoEvLoop *)(intptr_t)el, i);
}

int64_t mako_native_smtp_mock_start(int64_t port) {
    return mako_smtp_mock_start(port);
}


MakoNativeString *mako_native_str_to_lower_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_str_to_lower(bridge_borrow_str(s)));
}
MakoNativeString *mako_native_str_to_upper_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_str_to_upper(bridge_borrow_str(s)));
}
MakoNativeString *mako_native_str_repeat_ptr(MakoNativeString *s, int64_t n) {
    return bridge_take_str(mako_str_repeat(bridge_borrow_str(s), n));
}
MakoNativeString *mako_native_str_replace_ptr(MakoNativeString *s, MakoNativeString *oldv,
                                             MakoNativeString *newv) {
    return bridge_take_str(mako_str_replace(bridge_borrow_str(s), bridge_borrow_str(oldv),
                                            bridge_borrow_str(newv)));
}
int64_t mako_native_str_count_ptr(MakoNativeString *s, MakoNativeString *sub) {
    return mako_str_count(bridge_borrow_str(s), bridge_borrow_str(sub));
}
int64_t mako_native_mail_msg_add_cc_ptr(int64_t h, MakoNativeString *a) {
    return mako_mail_msg_add_cc(h, bridge_borrow_str(a));
}
MakoNativeString *mako_native_llm_base_url(void) {
    return mako_native_string_literal_ptr("https://api.x.ai/v1", 19);
}
MakoNativeString *mako_native_llm_default_model(void) {
    return mako_native_string_literal_ptr("grok-3", 6);
}
int64_t mako_native_nb_accept(int64_t fd) { return mako_nb_accept(fd); }
int64_t mako_native_sip_cseq_new(void) { return mako_sip_cseq_new(); }
int64_t mako_native_model_new(int64_t dev) {
    return mako_model_new(dev);
}
int64_t mako_native_h3_server_new_certs(MakoNativeString *cert, MakoNativeString *key) {
    return mako_h3_server_new(bridge_borrow_str(cert), bridge_borrow_str(key));
}

/* Must match MakoNativeStringSlice in native_runtime.c: data is MakoNativeString**. */
typedef struct {
    MakoNativeString **data;
    size_t len;
    size_t cap;
    int64_t owned;
} MakoNativeStrSliceHdr;

static MakoNativeStrSliceHdr *mako_native_empty_str_slice(void) {
    MakoNativeStrSliceHdr *h = (MakoNativeStrSliceHdr *)calloc(1, sizeof(*h));
    if (!h) abort();
    h->cap = 1;
    h->owned = 1;
    h->data = (MakoNativeString **)calloc(1, sizeof(MakoNativeString *));
    if (!h->data) abort();
    return h;
}

static MakoNativeStrSliceHdr *mako_native_str_array_to_slice(MakoStrArray a) {
    MakoNativeStrSliceHdr *h = (MakoNativeStrSliceHdr *)calloc(1, sizeof(*h));
    if (!h) abort();
    h->len = a.len;
    h->cap = a.len ? a.len : 1;
    h->owned = 1;
    h->data = (MakoNativeString **)calloc(h->cap, sizeof(MakoNativeString *));
    if (!h->data) abort();
    for (size_t i = 0; i < a.len; ++i) {
        MakoNativeString *p = bridge_take_str(a.data[i]);
        h->data[i] = p ? p : mako_native_string_literal_ptr("", 0);
    }
    free(a.data);
    return h;
}

MakoNativeStrSliceHdr *mako_native_str_split_ptr(MakoNativeString *s, MakoNativeString *sep) {
    return mako_native_str_array_to_slice(
        mako_str_split(bridge_borrow_str(s), bridge_borrow_str(sep)));
}

MakoNativeStrSliceHdr *mako_native_str_fields_ptr(MakoNativeString *s) {
    return mako_native_str_array_to_slice(mako_str_fields(bridge_borrow_str(s)));
}

MakoNativeStrSliceHdr *mako_native_str_cut_ptr(MakoNativeString *s, MakoNativeString *sep) {
    /* Cut once at first separator (not full split). */
    return mako_native_str_array_to_slice(
        mako_str_cut(bridge_borrow_str(s), bridge_borrow_str(sep)));
}

MakoNativeString *mako_native_str_join_ptr(MakoNativeStrSliceHdr *parts, MakoNativeString *sep) {
    if (!parts || parts->len == 0) return mako_native_string_literal_ptr("", 0);
    MakoStrArray a;
    a.data = (MakoString *)calloc(parts->len, sizeof(MakoString));
    if (!a.data) abort();
    a.len = parts->len;
    for (size_t i = 0; i < parts->len; ++i) {
        MakoNativeString *el = parts->data ? parts->data[i] : NULL;
        if (el) {
            a.data[i].data = (char *)el->data;
            a.data[i].len = el->len;
        } else {
            a.data[i].data = (char *)"";
            a.data[i].len = 0;
        }
    }
    MakoString out = mako_str_join(a, bridge_borrow_str(sep));
    free(a.data);
    return bridge_take_str(out);
}

int64_t mako_native_mail_msg_add_header_ptr(int64_t h, MakoNativeString *n, MakoNativeString *v) {
    return mako_mail_msg_add_header(h, bridge_borrow_str(n), bridge_borrow_str(v));
}
int64_t mako_native_mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
MakoNativeString *mako_native_nb_read_ptr(int64_t fd) {
    return bridge_take_str(mako_nb_read(fd));
}
MakoNativeString *mako_native_sip_headers_append_ptr(MakoNativeString *h, MakoNativeString *k,
                                                    MakoNativeString *v) {
    return bridge_take_str(mako_sip_headers_append(bridge_borrow_str(h), bridge_borrow_str(k),
                                                   bridge_borrow_str(v)));
}
int64_t mako_native_h3_server_bind(int64_t h, MakoNativeString *addr, int64_t port) {
    return mako_h3_server_bind(h, bridge_borrow_str(addr), port);
}
/* model_set_f32 real implementation is mako_native_model_set_f32_ptr below */
int64_t mako_native_http2_conn_new(void) {
    MakoHttp2Conn *c = mako_http2_conn_new();
    return c ? (int64_t)(intptr_t)c : -1;
}

/* Interface fat pointer: { tag, data* } */
typedef struct {
    int64_t tag;
    void *data;
} MakoNativeIface;

void *mako_native_iface_box(int64_t tag, void *data) {
    MakoNativeIface *b = (MakoNativeIface *)malloc(sizeof(MakoNativeIface));
    if (!b) abort();
    b->tag = tag;
    b->data = data; /* takes ownership of struct heap block */
    return b;
}

int64_t mako_native_iface_tag(void *box) {
    return box ? ((MakoNativeIface *)box)->tag : 0;
}

void *mako_native_iface_data(void *box) {
    return box ? ((MakoNativeIface *)box)->data : NULL;
}

/* opaque_drop already frees; iface box free frees header only if data freed separately.
 * For owned iface drop: free data via struct drop then free box — IR uses opaque_drop
 * which free(box) only. Leak of struct data — acceptable seed; improve later. */

int64_t mako_native_http2_conn_use(int64_t c) {
    return mako_http2_conn_use((MakoHttp2Conn *)(intptr_t)c);
}
int64_t mako_native_http2_conn_set_server(int64_t v) {
    return mako_http2_conn_set_server(v);
}
MakoNativeString *mako_native_http2_conn_pump_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_http2_conn_pump(bridge_borrow_str(s)));
}
int64_t mako_native_http2_conn_header_stream(void) {
    return mako_http2_conn_header_stream();
}
MakoNativeString *mako_native_http2_conn_header_block(int64_t s) {
    return bridge_take_str(mako_http2_conn_header_block(s));
}
int64_t mako_native_http2_conn_free(int64_t c) {
    mako_http2_conn_free((MakoHttp2Conn *)(intptr_t)c);
    return 0;
}
int64_t mako_native_hpack_decode_block_ptr(MakoNativeString *s) {
    return mako_hpack_decode_block(bridge_borrow_str(s));
}
MakoNativeString *mako_native_http2_response_ptr(int64_t stream, int64_t status, MakoNativeString *body) {
    return bridge_take_str(
        mako_http2_response(stream, status, bridge_borrow_str(body)));
}
int64_t mako_native_tls_server_available(void) {
    return mako_tls_server_available();
}
int64_t mako_native_tls_server_new_ptr(MakoNativeString *c, MakoNativeString *k) {
    /* Opaque handle: store pointer as i64 for IR ABI. */
    void *srv = mako_tls_server_new(bridge_borrow_str(c), bridge_borrow_str(k));
    return (int64_t)(intptr_t)srv;
}
int64_t mako_native_tls_accept(int64_t srv, int64_t fd) {
    void *conn = mako_tls_accept((void *)(intptr_t)srv, fd);
    return (int64_t)(intptr_t)conn;
}
MakoNativeString *mako_native_tls_read(int64_t conn, int64_t n) {
    return bridge_take_str(mako_tls_read((void *)(intptr_t)conn, n));
}
int64_t mako_native_tls_write_ptr(int64_t conn, MakoNativeString *s) {
    return mako_tls_write((void *)(intptr_t)conn, bridge_borrow_str(s));
}
int64_t mako_native_tls_conn_close(int64_t c) {
    return mako_tls_conn_close((void *)(intptr_t)c);
}

int64_t mako_native_h3_server_close(int64_t h) {
    return mako_h3_server_close(h);
}
int64_t mako_native_h3_server_poll(int64_t h, int64_t ms) {
    return mako_h3_server_poll(h, ms);
}
int64_t mako_native_h3_response_ptr(int64_t h, int64_t sid, int64_t st,
                                   MakoNativeString *ct, MakoNativeString *body) {
    return mako_h3_response(h, sid, st, bridge_borrow_str(ct), bridge_borrow_str(body));
}
int64_t mako_native_h3_stream_write_ptr(int64_t h, int64_t sid, MakoNativeString *s) {
    return mako_h3_stream_write(h, sid, bridge_borrow_str(s));
}

MakoNativeString *mako_native_llm_system_user_ptr(MakoNativeString *model, MakoNativeString *sys,
                                                 MakoNativeString *user) {
    return bridge_take_str(mako_llm_system_user(
        bridge_borrow_str(model), bridge_borrow_str(sys), bridge_borrow_str(user)));
}
MakoNativeString *mako_native_llm_chat_retry_ptr(MakoNativeString *base, MakoNativeString *key,
                                                MakoNativeString *body, int64_t ms, int64_t n) {
    (void)base; (void)key; (void)body; (void)ms; (void)n;
    return mako_native_string_literal_ptr("", 0);
}
int64_t mako_native_elapsed_ns(int64_t t0) {
    return mako_native_mono_ns() - t0;
}
int64_t mako_native_llm_is_error_ptr(MakoNativeString *s) {
    return mako_llm_is_error(bridge_borrow_str(s));
}
MakoNativeString *mako_native_llm_error_message_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_llm_error_message(bridge_borrow_str(s)));
}
int64_t mako_native_llm_last_status(void) { return 0; }
int64_t mako_native_llm_usage_prompt_tokens_ptr(MakoNativeString *s) {
    return mako_llm_usage_prompt_tokens(bridge_borrow_str(s));
}
int64_t mako_native_llm_usage_completion_tokens_ptr(MakoNativeString *s) {
    return mako_llm_usage_completion_tokens(bridge_borrow_str(s));
}

int64_t mako_native_smtp_new_ptr(MakoNativeString *host, int64_t port) {
    return mako_smtp_new(bridge_borrow_str(host), port);
}
int64_t mako_native_smtp_connect(int64_t c) { return mako_smtp_connect(c) ? 1 : 0; }
int64_t mako_native_smtp_ehlo_ptr(int64_t c, MakoNativeString *h) {
    return mako_smtp_ehlo(c, bridge_borrow_str(h)) ? 1 : 0;
}
int64_t mako_native_smtp_send_built(int64_t c, int64_t msg) {
    return mako_smtp_send_built(c, msg);
}
int64_t mako_native_smtp_quit(int64_t c) { return mako_smtp_quit(c); }
int64_t mako_native_smtp_close(int64_t c) { return mako_smtp_close(c); }
int64_t mako_native_smtp_mock_stop(void) {
    return mako_smtp_mock_stop();
}
MakoNativeString *mako_native_smtp_last_reply(int64_t c) {
    return bridge_take_str(mako_smtp_last_reply(c));
}
MakoNativeString *mako_native_smtp_mock_last_from(void) {
    return bridge_take_str(mako_smtp_mock_last_from());
}
MakoNativeString *mako_native_smtp_mock_last_rcpt(void) {
    return bridge_take_str(mako_smtp_mock_last_rcpt());
}
MakoNativeString *mako_native_smtp_mock_last_message(void) {
    return bridge_take_str(mako_smtp_mock_last_message());
}

/* Convert native float-slice header → MakoFloatArray view (borrowed data). */
static MakoFloatArray bridge_float_slice(const MakoNativeFloatSlice *s) {
    MakoFloatArray a;
    if (!s || !s->data) {
        a.data = NULL;
        a.len = 0;
        a.cap = 0;
        return a;
    }
    a.data = s->data;
    a.len = s->len;
    a.cap = s->cap;
    return a;
}

int64_t mako_native_model_set_f32_ptr(int64_t m, MakoNativeString *name, void *slice,
                                     int64_t a, int64_t b, int64_t c, int64_t d) {
    MakoFloatArray vals = bridge_float_slice((const MakoNativeFloatSlice *)slice);
    return mako_model_set_f32(m, bridge_borrow_str(name), vals, a, b, c, d);
}
int64_t mako_native_model_linear_f32(int64_t m, int64_t out, int64_t in,
                                     MakoNativeString *w, MakoNativeString *b,
                                     int64_t a, int64_t bb, int64_t c, int64_t d) {
    return mako_model_linear_f32(m, out, in, bridge_borrow_str(w), bridge_borrow_str(b),
                                 a, bb, c, d);
}
int64_t mako_native_model_save_ptr(int64_t m, MakoNativeString *path) {
    return mako_model_save(m, bridge_borrow_str(path));
}
int64_t mako_native_model_free(int64_t m) { return mako_model_free(m); }
int64_t mako_native_gpu_device_close(int64_t d) { return mako_gpu_device_close(d); }
int64_t mako_native_gpu_buf_new(int64_t d, int64_t n) { return mako_gpu_buf_new(d, n); }
int64_t mako_native_gpu_upload_f32(int64_t buf, void *slice) {
    MakoFloatArray vals = bridge_float_slice((const MakoNativeFloatSlice *)slice);
    return mako_gpu_upload_f32(buf, vals);
}
void *mako_native_gpu_download_f32(int64_t buf) {
    MakoFloatArray vals = mako_gpu_download_f32(buf);
    extern MakoNativeFloatSlice *mako_native_float_slice_make_ptr(int64_t len, int64_t cap);
    MakoNativeFloatSlice *out = mako_native_float_slice_make_ptr((int64_t)vals.len, (int64_t)vals.len);
    if (vals.data && vals.len && out && out->data) {
        memcpy(out->data, vals.data, vals.len * sizeof(double));
    }
    free(vals.data);
    return out;
}
int64_t mako_native_gpu_relu_f32(int64_t a, int64_t b) {
    return mako_gpu_relu_f32(a, b);
}

int64_t mako_native_evloop_del(int64_t el, int64_t fd) {
    return mako_evloop_del((MakoEvLoop *)(intptr_t)el, fd);
}
int64_t mako_native_nb_write_ptr(int64_t fd, MakoNativeString *s) {
    return mako_nb_write(fd, bridge_borrow_str(s));
}
int64_t mako_native_nb_close(int64_t fd) { return mako_nb_close(fd); }
/* int64_t mako_native_nb_listen(int64_t port) { return mako_nb_listen(port); } */

MakoNativeString *mako_native_sip_via_value_ptr(MakoNativeString *tr, MakoNativeString *host,
                                               int64_t port, MakoNativeString *branch) {
    return bridge_take_str(mako_sip_via_value(bridge_borrow_str(tr), bridge_borrow_str(host),
                                              port, bridge_borrow_str(branch)));
}
MakoNativeString *mako_native_sip_from_value_ptr(MakoNativeString *d, MakoNativeString *u,
                                                MakoNativeString *t) {
    return bridge_take_str(mako_sip_from_value(bridge_borrow_str(d), bridge_borrow_str(u),
                                               bridge_borrow_str(t)));
}
MakoNativeString *mako_native_sip_to_value_ptr(MakoNativeString *d, MakoNativeString *u,
                                              MakoNativeString *t) {
    return bridge_take_str(mako_sip_to_value(bridge_borrow_str(d), bridge_borrow_str(u),
                                            bridge_borrow_str(t)));
}
MakoNativeString *mako_native_sip_cseq_value_ptr(int64_t seq, MakoNativeString *m) {
    return bridge_take_str(mako_sip_cseq_value(seq, bridge_borrow_str(m)));
}
MakoNativeString *mako_native_sip_contact_value_ptr(MakoNativeString *u) {
    return bridge_take_str(mako_sip_contact_value(bridge_borrow_str(u)));
}
MakoNativeString *mako_native_sip_uri_build_ptr(MakoNativeString *user, MakoNativeString *host, int64_t port) {
    return bridge_take_str(mako_sip_uri_build(bridge_borrow_str(user), bridge_borrow_str(host), port));
}
MakoNativeString *mako_native_sip_request_ptr(MakoNativeString *method, MakoNativeString *uri,
                                             MakoNativeString *h, MakoNativeString *body) {
    return bridge_take_str(mako_sip_request(bridge_borrow_str(method), bridge_borrow_str(uri),
                                           bridge_borrow_str(h), bridge_borrow_str(body)));
}
MakoNativeString *mako_native_sip_method_ptr(MakoNativeString *m) {
    return bridge_take_str(mako_sip_method(bridge_borrow_str(m)));
}
MakoNativeString *mako_native_sip_request_uri_ptr(MakoNativeString *m) {
    return bridge_take_str(mako_sip_request_uri(bridge_borrow_str(m)));
}
MakoNativeString *mako_native_sip_body_ptr(MakoNativeString *m) {
    return bridge_take_str(mako_sip_body(bridge_borrow_str(m)));
}
MakoNativeString *mako_native_sip_reply_ptr(MakoNativeString *req, int64_t code, MakoNativeString *reason,
                                           MakoNativeString *extra, MakoNativeString *body) {
    return bridge_take_str(mako_sip_reply(bridge_borrow_str(req), code, bridge_borrow_str(reason),
                                         bridge_borrow_str(extra), bridge_borrow_str(body)));
}
int64_t mako_native_sip_status_code_ptr(MakoNativeString *m) {
    return mako_sip_status_code(bridge_borrow_str(m));
}
MakoNativeString *mako_native_sdp_build_audio_ptr(MakoNativeString *a, MakoNativeString *b,
                                                 MakoNativeString *c, int64_t port, MakoNativeString *d) {
    return bridge_take_str(mako_sdp_build_audio(bridge_borrow_str(a), bridge_borrow_str(b),
                                               bridge_borrow_str(c), port, bridge_borrow_str(d)));
}
int64_t mako_native_sdp_media_port_ptr(MakoNativeString *s, int64_t i) {
    return mako_sdp_media_port(bridge_borrow_str(s), i);
}
MakoNativeString *mako_native_sdp_connection_addr_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_sdp_connection_addr(bridge_borrow_str(s)));
}
MakoNativeString *mako_native_rtp_pack_ptr(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e,
                                          MakoNativeString *payload) {
    return bridge_take_str(mako_rtp_pack(a,b,c,d,e, bridge_borrow_str(payload)));
}
int64_t mako_native_rtp_seq_ptr(MakoNativeString *p) {
    return mako_rtp_seq(bridge_borrow_str(p));
}
int64_t mako_native_rtp_ssrc_ptr(MakoNativeString *p) {
    return mako_rtp_ssrc(bridge_borrow_str(p));
}

void *mako_native_cmap_new(void) { return mako_cmap_new(); }
void mako_native_cmap_set_ptr(void *m, MakoNativeString *k, MakoNativeString *v) {
    mako_cmap_set((MakoCMap *)m, bridge_borrow_str(k), bridge_borrow_str(v));
}
MakoNativeString *mako_native_cmap_get_ptr(void *m, MakoNativeString *k) {
    return bridge_take_str(mako_cmap_get((MakoCMap *)m, bridge_borrow_str(k)));
}
int64_t mako_native_sql_open_sqlite_ptr(MakoNativeString *path) {
    MakoSqlDB db = mako_sql_open_sqlite(bridge_borrow_str(path));
    return mako_native_sql_register(db);
}
int64_t mako_native_sql_exec_plain_ptr(int64_t db, MakoNativeString *sql) {
    return mako_sql_exec_plain(mako_native_sql_db_from_key(db), bridge_borrow_str(sql));
}
int64_t mako_native_sql_exec_str4_ptr(int64_t db, MakoNativeString *sql,
                                     MakoNativeString *a, MakoNativeString *b,
                                     MakoNativeString *c, MakoNativeString *d) {
    return mako_sql_exec_str4(
        mako_native_sql_db_from_key(db),
        bridge_borrow_str(sql),
        bridge_borrow_str(a), bridge_borrow_str(b),
        bridge_borrow_str(c), bridge_borrow_str(d));
}
MakoNativeString *mako_native_sql_query_str_ptr(int64_t db, MakoNativeString *sql, MakoNativeString *p) {
    return bridge_take_str(mako_sql_query_str(
        mako_native_sql_db_from_key(db), bridge_borrow_str(sql), bridge_borrow_str(p)));
}

MakoNativeString *mako_native_http_forward_ptr(MakoNativeString *host, int64_t port,
                                               MakoNativeString *method, MakoNativeString *path,
                                               MakoNativeString *body) {
    return bridge_take_str(mako_http_forward(
        bridge_borrow_str(host), port, bridge_borrow_str(method),
        bridge_borrow_str(path), bridge_borrow_str(body)));
}

int64_t mako_native_h3_accept_stream(int64_t h) {
    return mako_h3_accept_stream(h);
}

int64_t mako_native_sql_close(int64_t db) {
    MakoSqlDB d = mako_native_sql_db_from_key(db);
    int64_t rc = mako_sql_close(d);
    for (int i = 0; i < MAKO_NATIVE_SQL_DB_MAX; i++) {
        if (mako_native_sql_db_keys[i] == db) {
            mako_native_sql_db_keys[i] = 0;
            memset(&mako_native_sql_dbs[i], 0, sizeof(mako_native_sql_dbs[i]));
            break;
        }
    }
    return rc;
}
MakoNativeString *mako_native_h3_stream_method(int64_t h, int64_t sid) {
    return bridge_take_str(mako_h3_stream_method(h, sid));
}
MakoNativeString *mako_native_h3_stream_path(int64_t h, int64_t sid) {
    return bridge_take_str(mako_h3_stream_path(h, sid));
}
MakoNativeString *mako_native_h3_stream_body(int64_t h, int64_t sid) {
    return bridge_take_str(mako_h3_stream_body(h, sid));
}

int64_t mako_native_quiche_available(void) {
    return mako_quiche_available();
}
MakoNativeString *mako_native_quiche_version(void) {
    return bridge_take_str(mako_quiche_version());
}
MakoNativeString *mako_native_quiche_handshake_ptr(MakoNativeString *host, int64_t port,
                                                   MakoNativeString *sni, int64_t verify) {
    return bridge_take_str(mako_quiche_handshake(
        bridge_borrow_str(host), port, bridge_borrow_str(sni), verify));
}
MakoNativeString *mako_native_quiche_h3_get_ptr(MakoNativeString *host, int64_t port,
                                                MakoNativeString *path, MakoNativeString *sni,
                                                int64_t verify) {
    return bridge_take_str(mako_quiche_h3_get(
        bridge_borrow_str(host), port, bridge_borrow_str(path),
        bridge_borrow_str(sni), verify));
}
MakoNativeString *mako_native_quiche_h3_post_ptr(MakoNativeString *host, int64_t port,
                                                 MakoNativeString *path, MakoNativeString *body,
                                                 MakoNativeString *sni, int64_t verify) {
    return bridge_take_str(mako_quiche_h3_post(
        bridge_borrow_str(host), port, bridge_borrow_str(path),
        bridge_borrow_str(body), bridge_borrow_str(sni), verify));
}
MakoNativeString *mako_native_quiche_h3_get_two_ptr(MakoNativeString *host, int64_t port,
                                                    MakoNativeString *p1, MakoNativeString *p2,
                                                    MakoNativeString *sni, int64_t verify) {
    return bridge_take_str(mako_quiche_h3_get_two(
        bridge_borrow_str(host), port, bridge_borrow_str(p1), bridge_borrow_str(p2),
        bridge_borrow_str(sni), verify));
}

// Abort-based asserts for the native backend. The C test harness uses setjmp
// + mako_fail; native harness main just calls TestXxx() and relies on abort.
void mako_native_assert_eq_str_ptr(MakoNativeString *a, MakoNativeString *b) {
    MakoString sa = bridge_borrow_str(a);
    MakoString sb = bridge_borrow_str(b);
    if (!mako_str_eq(sa, sb)) {
        fprintf(stderr,
                "assert_eq_str failed: got \"%.*s\", want \"%.*s\"\n",
                (int)sa.len, sa.data ? sa.data : "",
                (int)sb.len, sb.data ? sb.data : "");
        fflush(stderr);
        abort();
    }
}

// ---- High-frequency stdlib helpers used by examples/testing -----------------

MakoNativeString *mako_native_env_get_or_ptr(MakoNativeString *name, MakoNativeString *def) {
    /* mako_env_get_or may return the borrowed default; always produce owned native. */
    MakoString n = bridge_borrow_str(name);
    char nbuf[512];
    if (!n.data || n.len >= sizeof(nbuf)) {
        return bridge_take_str(mako_str_clone(bridge_borrow_str(def)));
    }
    memcpy(nbuf, n.data, n.len);
    nbuf[n.len] = 0;
    const char *val = getenv(nbuf);
    if (!val || val[0] == 0) {
        return bridge_take_str(mako_str_clone(bridge_borrow_str(def)));
    }
    return bridge_take_str(mako_str_from_cstr(val));
}

MakoNativeString *mako_native_path_clean_ptr(MakoNativeString *path) {
    return bridge_take_str(mako_path_clean(bridge_borrow_str(path)));
}

MakoNativeString *mako_native_sha1_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_sha1_hex(bridge_borrow_str(s)));
}

int64_t mako_native_would_overflow_add(int64_t a, int64_t b) {
    return mako_would_overflow_add(a, b);
}

int64_t mako_native_would_overflow_sub(int64_t a, int64_t b) {
    return mako_would_overflow_sub(a, b);
}

int64_t mako_native_would_overflow_mul(int64_t a, int64_t b) {
    return mako_would_overflow_mul(a, b);
}

int64_t mako_native_chan_cap(int64_t ch) {
    return mako_chan_cap((MakoChan *)(uintptr_t)ch);
}

int64_t mako_native_chan_len(int64_t ch) {
    return mako_chan_len((MakoChan *)(uintptr_t)ch);
}

// Opaque i64 handle from mako_native_sql_open_*; non-zero means open.
int64_t mako_native_sql_ok(int64_t db) {
    if (!db) return 0;
    return mako_sql_ok(mako_native_sql_db_from_key(db));
}

void mako_native_time_sleep_ms(int64_t ms) {
    mako_sleep_ms(ms);
}

// ---- auto bulk stdlib bridges (examples/testing corpus) ----

int64_t mako_native_aead_available(void) {
    return (int64_t)mako_aead_available();
}

int64_t mako_native_alloc_track_reset(void) {
    return (int64_t)mako_alloc_track_reset();
}

int64_t mako_native_append_file_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_append_file(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

MakoNativeString *mako_native_auth_bearer_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_auth_bearer(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_avro_encode_long_ptr(int64_t a0) {
    return bridge_take_str(mako_avro_encode_long(a0));
}

MakoNativeString *mako_native_binary_put_u16be_ptr(int64_t a0) {
    return bridge_take_str(mako_binary_put_u16be(a0));
}

int64_t mako_native_checked_add(int64_t a0, int64_t a1) {
    return (int64_t)mako_checked_add(a0, a1);
}

int64_t mako_native_conn_pool_slot_ptr(MakoNativeString *a0, int64_t a1) {
    return (int64_t)mako_conn_pool_slot(bridge_borrow_str(a0), a1);
}

int64_t mako_native_const_eq_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_const_eq(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

MakoNativeString *mako_native_cookie_get_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_cookie_get(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

MakoNativeString *mako_native_dap_initialize_response_ptr(int64_t a0) {
    return bridge_take_str(mako_dap_initialize_response(a0));
}

int64_t mako_native_ecs_world_new(int64_t a0) {
    return (int64_t)mako_ecs_world_new(a0);
}

int64_t mako_native_elapsed_ms(int64_t a0) {
    return (int64_t)mako_elapsed_ms(a0);
}

MakoNativeString *mako_native_exec_output_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_exec_output(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_fmt_sprintf_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_fmt_sprintf1(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

MakoNativeString *mako_native_format_bool_ptr(int64_t a0) {
    return bridge_take_str(mako_format_bool(a0));
}

MakoNativeString *mako_native_fsm_rule_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_fsm_rule(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_fx_from_int(int64_t a0, int64_t a1) {
    return (int64_t)mako_fx_from_int(a0, a1);
}

int64_t mako_native_game_fixed_steps(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_game_fixed_steps(a0, a1, a2);
}

int64_t mako_native_gpu_available(void) {
    return (int64_t)mako_gpu_available();
}

int64_t mako_native_gpu_set_prefer_host(int64_t a0) {
    return (int64_t)mako_gpu_set_prefer_host(a0);
}

MakoNativeString *mako_native_graphql_field_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_graphql_field(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_grpc_encode_message_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_grpc_encode_message(bridge_borrow_str(a0)));
}

int64_t mako_native_h3_server_fd(int64_t a0) {
    return (int64_t)mako_h3_server_fd(a0);
}

MakoNativeString *mako_native_hex_decode_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_hex_decode(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_hex_encode_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_hex_encode(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_hpack_encode_indexed_ptr(int64_t a0) {
    return bridge_take_str(mako_hpack_encode_indexed(a0));
}

MakoNativeString *mako_native_hpack_encode_literal_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_hpack_encode_literal(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

MakoNativeString *mako_native_http2_client_preface_ptr(void) {
    return bridge_take_str(mako_http2_client_preface());
}

int64_t mako_native_http2_conn_recv_ptr(MakoNativeString *a0) {
    return (int64_t)mako_http2_conn_recv(bridge_borrow_str(a0));
}

int64_t mako_native_http2_conn_reset(void) {
    mako_http2_conn_reset();
    return 0;
}

MakoNativeString *mako_native_http2_goaway_frame_ptr(int64_t a0, int64_t a1) {
    return bridge_take_str(mako_http2_goaway_frame(a0, a1));
}

MakoNativeString *mako_native_http2_headers_frame_ptr(int64_t a0, MakoNativeString *a1, int64_t a2) {
    return bridge_take_str(mako_http2_headers_frame(a0, bridge_borrow_str(a1), a2));
}

MakoNativeString *mako_native_http2_response_ct_ptr(int64_t a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3) {
    return bridge_take_str(mako_http2_response_ct(a0, a1, bridge_borrow_str(a2), bridge_borrow_str(a3)));
}

MakoNativeString *mako_native_http_health_json_ptr(MakoNativeString *a0, int64_t a1) {
    return bridge_take_str(mako_http_health_json(bridge_borrow_str(a0), a1));
}

int64_t mako_native_http_last_status(void) {
    return (int64_t)mako_http_last_status();
}

int64_t mako_native_http_respond_json_ptr(int64_t a0, int64_t a1, MakoNativeString *a2) {
    return (int64_t)mako_http_respond_json(a0, a1, bridge_borrow_str(a2));
}

int64_t mako_native_http_shutdown_reset(void) {
    return (int64_t)mako_http_shutdown_reset();
}

int64_t mako_native_https_last_status(void) {
    return (int64_t)mako_https_last_status();
}

int64_t mako_native_io_backoff_ms(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_io_backoff_ms(a0, a1, a2);
}

int64_t mako_native_job_schedule_ptr(MakoNativeString *a0, int64_t a1) {
    return (int64_t)mako_job_schedule(bridge_borrow_str(a0), a1);
}

MakoNativeString *mako_native_jpeg_encode_gray_ptr(int64_t a0, int64_t a1, MakoNativeString *a2) {
    return bridge_take_str(mako_jpeg_encode_gray(a0, a1, bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_jpeg_encode_gray_huff_ptr(int64_t a0, int64_t a1, MakoNativeString *a2) {
    return bridge_take_str(mako_jpeg_encode_gray_huff(a0, a1, bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_jpeg_encode_gray_jfif_ptr(int64_t a0, int64_t a1, MakoNativeString *a2) {
    return bridge_take_str(mako_jpeg_encode_gray_jfif(a0, a1, bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_llm_message_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_llm_message(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

MakoNativeString *mako_native_mail_parse_address_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_mail_parse_address(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_mongo_connect_url_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_mongo_connect_url(bridge_borrow_str(a0)));
}

int64_t mako_native_mono_res_ns(void) {
    return (int64_t)mako_mono_res_ns();
}

MakoNativeString *mako_native_msgpack_encode_int_ptr(int64_t a0) {
    return bridge_take_str(mako_msgpack_encode_int(a0));
}

MakoNativeString *mako_native_multipart_boundary_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_multipart_boundary(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_openapi_route_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_openapi_route(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_parse_ip_ok_ptr(MakoNativeString *a0) {
    return (int64_t)mako_parse_ip_ok(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_path_base_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_path_base(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_pb_encode_bytes_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_pb_encode_bytes(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_pb_encode_nested_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, int64_t a3) {
    return bridge_take_str(mako_pb_encode_nested(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), a3));
}

int64_t mako_native_pb_zigzag_encode(int64_t a0) {
    return (int64_t)mako_pb_zigzag_encode(a0);
}

int64_t mako_native_plugin_max_slots(void) {
    return (int64_t)mako_plugin_max_slots();
}

int64_t mako_native_profile_sample_clear(void) {
    return (int64_t)mako_profile_sample_clear();
}

MakoNativeString *mako_native_quic_hkdf_expand_label_hex_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2) {
    return bridge_take_str(mako_quic_hkdf_expand_label_hex(bridge_borrow_str(a0), bridge_borrow_str(a1), a2));
}

int64_t mako_native_quiche_start_server_ptr(int64_t a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3, MakoNativeString *a4) {
    return (int64_t)mako_quiche_start_server(a0, bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3), bridge_borrow_str(a4));
}

int64_t mako_native_rate_allow_ptr(MakoNativeString *a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_rate_allow(bridge_borrow_str(a0), a1, a2);
}

int64_t mako_native_redis_mock_kv(int64_t a0, int64_t a1) {
    return (int64_t)mako_redis_mock_kv(a0, a1);
}

MakoNativeString *mako_native_reflect_type_schema_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_reflect_type_schema(bridge_borrow_str(a0)));
}

/* Register a named struct schema for reflect_type_schema / num_fields.
 * name and schema must be immortal C strings (data-section literals). */
int64_t mako_native_reflect_register_type(const char *name, const char *schema) {
    return (int64_t)mako_reflect_register_type(name, schema);
}

int64_t mako_native_remove_all_ptr(MakoNativeString *a0) {
    return (int64_t)mako_remove_all(bridge_borrow_str(a0));
}

int64_t mako_native_reqctx_new(void) {
    return (int64_t)mako_reqctx_new();
}

int64_t mako_native_ring_new(int64_t a0) {
    return (int64_t)mako_ring_new(a0);
}

int64_t mako_native_router_new(void) {
    return (int64_t)mako_router_new();
}

int64_t mako_native_runtime_stats_reset(void) {
    mako_runtime_stats_reset();
    return 0;
}

int64_t mako_native_sched_set_workers(int64_t a0) {
    mako_sched_set_workers(a0);
    return 0;
}

MakoNativeString *mako_native_sha512_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_sha512_hex(bridge_borrow_str(a0)));
}

int64_t mako_native_signal_watch_ptr(MakoNativeString *a0) {
    return (int64_t)mako_signal_watch(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_sip_md5_hex_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_sip_md5_hex(bridge_borrow_str(a0)));
}

int64_t mako_native_sip_ok_ptr(MakoNativeString *a0) {
    return (int64_t)mako_sip_ok(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_slog_redact_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_slog_redact(bridge_borrow_str(a0)));
}

int64_t mako_native_sql_check_typed_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    return (int64_t)mako_sql_check_typed(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3));
}

MakoNativeString *mako_native_sse_event_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_sse_event(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

int64_t mako_native_str_slice_eq_ptr(MakoNativeString *a0, int64_t a1, int64_t a2, MakoNativeString *a3) {
    return (int64_t)mako_str_slice_eq(bridge_borrow_str(a0), a1, a2, bridge_borrow_str(a3));
}

int64_t mako_native_syscall_available(void) {
    return (int64_t)mako_syscall_available();
}

int64_t mako_native_t_run_ptr(MakoNativeString *a0) {
    mako_t_run(bridge_borrow_str(a0));
    return 0;
}

int64_t mako_native_tcp_listen_backlog_ptr(MakoNativeString *a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_tcp_listen_backlog(bridge_borrow_str(a0), a1, a2);
}

MakoNativeString *mako_native_tcp_local_addr_ptr(int64_t a0) {
    return bridge_take_str(mako_tcp_local_addr(a0));
}

int64_t mako_native_tcp_pool_open_ptr(MakoNativeString *a0, int64_t a1, int64_t a2, int64_t a3) {
    return (int64_t)mako_tcp_pool_open(bridge_borrow_str(a0), a1, a2, a3);
}

int64_t mako_native_tcp_set_he_delay_ms(int64_t a0) {
    mako_tcp_set_he_delay_ms(a0);
    return 0;
}

int64_t mako_native_time_date(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    return (int64_t)mako_time_date(a0, a1, a2, a3, a4, a5, a6);
}

MakoNativeString *mako_native_tls_aead_seal_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    return bridge_take_str(mako_tls_aead_seal(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3)));
}

int64_t mako_native_tls_client_available(void) {
    return (int64_t)mako_tls_client_available();
}

MakoNativeString *mako_native_tls_client_hello_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_tls_client_hello(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_tls_handshake_ok_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) {
    return bridge_take_str(mako_tls_handshake_ok(bridge_borrow_str(a0), a1, bridge_borrow_str(a2)));
}

int64_t mako_native_tmpl_new_ptr(MakoNativeString *a0) {
    return (int64_t)mako_tmpl_new(bridge_borrow_str(a0));
}

int64_t mako_native_trace_clear(void) {
    return (int64_t)mako_trace_clear();
}

MakoNativeString *mako_native_trace_id_ptr(void) {
    return bridge_take_str(mako_trace_id());
}

int64_t mako_native_udp_bind(int64_t a0) {
    return (int64_t)mako_udp_bind(a0);
}

MakoNativeString *mako_native_url_scheme_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_url_scheme(bridge_borrow_str(a0)));
}

int64_t mako_native_utf8_rune_error(void) {
    return (int64_t)mako_utf8_rune_error();
}

int64_t mako_native_validate_required_ptr(MakoNativeString *a0) {
    return (int64_t)mako_validate_required(bridge_borrow_str(a0));
}

int64_t mako_native_watch_available(void) {
    return (int64_t)mako_watch_available();
}

MakoNativeString *mako_native_ws_accept_key_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_ws_accept_key(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_yaml_get_string_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_yaml_get_string(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

int64_t mako_native_zip_write_file_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return (int64_t)mako_zip_write_file(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2));
}

// ---- bulk bridges round 2 ----

int64_t mako_native_auth_check_bearer_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_auth_check_bearer(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

int64_t mako_native_avro_decode_long_ptr(MakoNativeString *a0) {
    return (int64_t)mako_avro_decode_long(bridge_borrow_str(a0));
}

int64_t mako_native_binary_u16be_ptr(MakoNativeString *a0) {
    return (int64_t)mako_binary_u16be(bridge_borrow_str(a0));
}

int64_t mako_native_checked_sub(int64_t a0, int64_t a1) {
    return (int64_t)mako_checked_sub(a0, a1);
}

int64_t mako_native_conn_pool_next(int64_t a0) {
    return (int64_t)mako_conn_pool_next(a0);
}

MakoNativeString *mako_native_cookie_make_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2) {
    return bridge_take_str(mako_cookie_make(bridge_borrow_str(a0), bridge_borrow_str(a1), a2));
}

int64_t mako_native_crypto_eq_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_crypto_eq(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

MakoNativeString *mako_native_dap_stopped_event_ptr(MakoNativeString *a0, int64_t a1) {
    return bridge_take_str(mako_dap_stopped_event(bridge_borrow_str(a0), a1));
}

int64_t mako_native_dns_ip_family_ptr(MakoNativeString *a0) {
    return (int64_t)mako_dns_ip_family(bridge_borrow_str(a0));
}

int64_t mako_native_ecs_spawn(int64_t a0) {
    return (int64_t)mako_ecs_spawn(a0);
}

MakoNativeString *mako_native_fmt_sprintf3_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    return bridge_take_str(mako_fmt_sprintf3(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3)));
}

MakoNativeString *mako_native_fmt_sprintf_d_ptr(MakoNativeString *a0, int64_t a1) {
    /* Full int verbs (%d %x %X %b %o …), not the stdlib %d-only helper. */
    return bridge_take_str(mako_fmt_sprintf_d_full(bridge_borrow_str(a0), a1));
}

int64_t mako_native_frame_alloc_new(int64_t a0) {
    return (int64_t)mako_frame_alloc_new(a0);
}

int64_t mako_native_fx_to_int(int64_t a0, int64_t a1) {
    return (int64_t)mako_fx_to_int(a0, a1);
}

int64_t mako_native_game_fixed_remainder(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_game_fixed_remainder(a0, a1, a2);
}

MakoNativeString *mako_native_gpu_backend_ptr(void) {
    return bridge_take_str(mako_gpu_backend());
}

int64_t mako_native_gpu_gelu_f32(int64_t a0, int64_t a1) {
    return (int64_t)mako_gpu_gelu_f32(a0, a1);
}

MakoNativeString *mako_native_graphql_arg_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_graphql_arg(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

int64_t mako_native_grpc_message_len_ptr(MakoNativeString *a0) {
    return (int64_t)mako_grpc_message_len(bridge_borrow_str(a0));
}

int64_t mako_native_hpack_decode_clear(void) {
    mako_hpack_decode_clear();
    return 0;
}

int64_t mako_native_hpack_decode_indexed_ptr(MakoNativeString *a0) {
    return (int64_t)mako_hpack_decode_indexed(bridge_borrow_str(a0));
}

int64_t mako_native_http2_conn_is_server(void) {
    return (int64_t)mako_http2_conn_is_server();
}

MakoNativeString *mako_native_http2_continuation_frame_ptr(int64_t a0, MakoNativeString *a1, int64_t a2) {
    return bridge_take_str(mako_http2_continuation_frame(a0, bridge_borrow_str(a1), a2));
}

int64_t mako_native_http2_frame_type_ptr(MakoNativeString *a0) {
    return (int64_t)mako_http2_frame_type(bridge_borrow_str(a0));
}

int64_t mako_native_http2_is_goaway_ptr(MakoNativeString *a0) {
    return (int64_t)mako_http2_is_goaway(bridge_borrow_str(a0));
}

int64_t mako_native_http2_window_conn(void) {
    return (int64_t)mako_http2_window_conn();
}

MakoNativeString *mako_native_http_post_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_http_post(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

MakoNativeString *mako_native_http_request_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, int64_t a3) {
    return bridge_take_str(mako_http_request(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), a3));
}

int64_t mako_native_http_shutdown_requested(void) {
    return (int64_t)mako_http_shutdown_requested();
}

int64_t mako_native_io_should_pause(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_io_should_pause(a0, a1, a2);
}

int64_t mako_native_job_due_ptr(MakoNativeString *a0) {
    return (int64_t)mako_job_due(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_has_app8_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_has_app8(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_has_eoi_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_has_eoi(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_has_soi_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_has_soi(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_jpeg_huff_block_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_jpeg_huff_block(bridge_borrow_str(a0)));
}

int64_t mako_native_jpeg_is_baseline_gray_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_is_baseline_gray(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_is_jfif_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_is_jfif(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_is_mako_raw_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_is_mako_raw(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_jfif_density_units_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_jfif_density_units(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_jfif_major_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_jfif_major(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_roundtrip_ok_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_roundtrip_ok(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_sof0_components_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_sof0_components(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_sof0_precision_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_sof0_precision(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_sof0_quant_table_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_sof0_quant_table(bridge_borrow_str(a0));
}

int64_t mako_native_jwt_verify_jwks_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_jwt_verify_jwks(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

int64_t mako_native_jwt_verify_rs256_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_jwt_verify_rs256(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

int64_t mako_native_leak_mark(void) {
    return (int64_t)mako_leak_mark();
}

MakoNativeString *mako_native_llm_messages_append_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_llm_messages_append(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

int64_t mako_native_mail_address_ok_ptr(MakoNativeString *a0) {
    return (int64_t)mako_mail_address_ok(bridge_borrow_str(a0));
}

int64_t mako_native_mkdir_all_ptr(MakoNativeString *a0) {
    return (int64_t)mako_mkdir_all(bridge_borrow_str(a0));
}

int64_t mako_native_model_tensor_count(int64_t a0) {
    return (int64_t)mako_model_tensor_count(a0);
}

MakoNativeString *mako_native_mongo_find_one_request_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_mongo_find_one_request(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

int64_t mako_native_mono_overhead_ns(void) {
    return (int64_t)mako_mono_overhead_ns();
}

int64_t mako_native_msgpack_decode_int_ptr(MakoNativeString *a0) {
    return (int64_t)mako_msgpack_decode_int(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_multipart_form_value_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_multipart_form_value(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_openapi_doc_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_openapi_doc(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_path_dir_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_path_dir(bridge_borrow_str(a0)));
}

int64_t mako_native_pb_bytes_len_ptr(MakoNativeString *a0) {
    return (int64_t)mako_pb_bytes_len(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_pb_nested_inner_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_pb_nested_inner(bridge_borrow_str(a0)));
}

int64_t mako_native_pb_zigzag_decode(int64_t a0) {
    return (int64_t)mako_pb_zigzag_decode(a0);
}

MakoNativeString *mako_native_pbkdf2_sha256_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2, int64_t a3) {
    return bridge_take_str(mako_pbkdf2_sha256(bridge_borrow_str(a0), bridge_borrow_str(a1), a2, a3));
}

int64_t mako_native_plugin_abi_version(void) {
    return (int64_t)mako_plugin_abi_version();
}

int64_t mako_native_plugin_open_ptr(MakoNativeString *a0) {
    return (int64_t)mako_plugin_open(bridge_borrow_str(a0));
}

int64_t mako_native_profile_sample_count(void) {
    return (int64_t)mako_profile_sample_count();
}

MakoNativeString *mako_native_quic_hkdf_expand_label_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2) {
    return bridge_take_str(mako_quic_hkdf_expand_label(bridge_borrow_str(a0), bridge_borrow_str(a1), a2));
}

int64_t mako_native_quiche_stop_server(int64_t a0) {
    return (int64_t)mako_quiche_stop_server(a0);
}

MakoNativeString *mako_native_random_bytes_ptr(int64_t a0) {
    return bridge_take_str(mako_random_bytes(a0));
}

int64_t mako_native_rate_remaining_ptr(MakoNativeString *a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_rate_remaining(bridge_borrow_str(a0), a1, a2);
}

MakoNativeString *mako_native_redis_set_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3) {
    return bridge_take_str(mako_redis_set(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3)));
}

int64_t mako_native_reflect_struct_num_fields_ptr(MakoNativeString *a0) {
    return (int64_t)mako_reflect_struct_num_fields(bridge_borrow_str(a0));
}

int64_t mako_native_reqctx_count(int64_t a0) {
    return (int64_t)mako_reqctx_count(a0);
}

int64_t mako_native_ring_cap(int64_t a0) {
    return (int64_t)mako_ring_cap(a0);
}

int64_t mako_native_router_count(int64_t a0) {
    return (int64_t)mako_router_count(a0);
}

MakoNativeString *mako_native_runtime_stats_json_ptr(void) {
    return bridge_take_str(mako_runtime_stats_json());
}

int64_t mako_native_sched_workers(void) {
    return (int64_t)mako_sched_workers();
}

MakoNativeString *mako_native_sg_gather3_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_sg_gather3(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_signal_ignore_ptr(MakoNativeString *a0) {
    return (int64_t)mako_signal_ignore(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_sip_digest_response_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3, MakoNativeString *a4, MakoNativeString *a5) {
    return bridge_take_str(mako_sip_digest_response(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3), bridge_borrow_str(a4), bridge_borrow_str(a5)));
}

int64_t mako_native_sip_is_request_ptr(MakoNativeString *a0) {
    return (int64_t)mako_sip_is_request(bridge_borrow_str(a0));
}


MakoNativeString *mako_native_sse_retry_ptr(int64_t a0) {
    return bridge_take_str(mako_sse_retry(a0));
}

int64_t mako_native_str_slice_ci_eq_ptr(MakoNativeString *a0, int64_t a1, int64_t a2, MakoNativeString *a3) {
    return (int64_t)mako_str_slice_ci_eq(bridge_borrow_str(a0), a1, a2, bridge_borrow_str(a3));
}

int64_t mako_native_t_run_nested_ptr(MakoNativeString *a0) {
    mako_t_run_nested(bridge_borrow_str(a0));
    return 0;
}

int64_t mako_native_tcp_get_he_delay_ms(void) {
    return (int64_t)mako_tcp_get_he_delay_ms();
}

int64_t mako_native_tcp_keepalive(int64_t a0, int64_t a1, int64_t a2, int64_t a3) {
    return (int64_t)mako_tcp_keepalive(a0, a1, a2, a3);
}

MakoNativeString *mako_native_tcp_peer_addr_ptr(int64_t a0) {
    return bridge_take_str(mako_tcp_peer_addr(a0));
}

int64_t mako_native_tcp_pool_idle(int64_t a0) {
    return (int64_t)mako_tcp_pool_idle(a0);
}

MakoNativeString *mako_native_tls_aead_open_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    return bridge_take_str(mako_tls_aead_open(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3)));
}

int64_t mako_native_tls_client_hello_legacy_version_ptr(MakoNativeString *a0) {
    return (int64_t)mako_tls_client_hello_legacy_version(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_tls_handshake_version_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) {
    return bridge_take_str(mako_tls_handshake_version(bridge_borrow_str(a0), a1, bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_tmpl_execute_ptr(int64_t a0, int64_t a1) {
    return bridge_take_str(mako_tmpl_execute(a0, a1));
}

MakoNativeString *mako_native_toml_get_string_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_toml_get_string(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

MakoNativeString *mako_native_trace_export_otlp_json_ptr(void) {
    return bridge_take_str(mako_trace_export_otlp_json());
}

int64_t mako_native_trace_set_ptr(MakoNativeString *a0) {
    return (int64_t)mako_trace_set(bridge_borrow_str(a0));
}

int64_t mako_native_udp_local_port(int64_t a0) {
    return (int64_t)mako_udp_local_port(a0);
}

MakoNativeString *mako_native_url_host_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_url_host(bridge_borrow_str(a0)));
}

int64_t mako_native_utf8_rune_self(void) {
    return (int64_t)mako_utf8_rune_self();
}

int64_t mako_native_validate_min_len_ptr(MakoNativeString *a0, int64_t a1) {
    return (int64_t)mako_validate_min_len(bridge_borrow_str(a0), a1);
}

MakoNativeString *mako_native_ws_client_request_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_ws_client_request(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_yaml_get_int_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_yaml_get_int(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

MakoNativeString *mako_native_zip_first_name_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_zip_first_name(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_zip_read_file_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_zip_read_file(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

// ---- bulk bridges round 3 (path/random/time/fmt) ----

MakoNativeString *mako_native_path_ext_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_path_ext(bridge_borrow_str(a0)));
}

int64_t mako_native_random_int(int64_t a0, int64_t a1) {
    return (int64_t)mako_random_int(a0, a1);
}

int64_t mako_native_wall_ms(void) {
    return (int64_t)mako_wall_ms();
}

MakoNativeString *mako_native_fmt_sprint_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_fmt_sprint1(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_fmt_sprint2_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_fmt_sprint2(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

MakoNativeString *mako_native_fmt_sprint3_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(
        mako_fmt_sprint3(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_fmt_sprintln_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_fmt_sprintln1(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_fmt_sprintln2_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_fmt_sprintln2(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

MakoNativeString *mako_native_fmt_errorf2_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(
        mako_fmt_errorf2(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_fmt_print_ptr(MakoNativeString *a0) {
    return (int64_t)mako_fmt_print1(bridge_borrow_str(a0));
}

int64_t mako_native_fmt_printf_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_fmt_printf1(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

int64_t mako_native_fmt_eprint_ptr(MakoNativeString *a0) {
    return (int64_t)mako_fmt_eprint1(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_fmt_sprintf_f_ptr(MakoNativeString *a0, double a1, int64_t a2) {
    return bridge_take_str(mako_fmt_sprintf_f(bridge_borrow_str(a0), a1, a2));
}

MakoNativeString *mako_native_fmt_sprintf_dd_ptr(MakoNativeString *fmt, int64_t a0, int64_t a1) {
    return bridge_take_str(mako_fmt_sprintf_dd(bridge_borrow_str(fmt), a0, a1));
}

int64_t mako_native_path_is_abs_ptr(MakoNativeString *a0) {
    return (int64_t)mako_path_is_abs(bridge_borrow_str(a0));
}

int64_t mako_native_time_unix(void) {
    return (int64_t)mako_time_unix();
}

int64_t mako_native_abs(int64_t a0) {
    return (int64_t)mako_abs(a0);
}

int64_t mako_native_min(int64_t a0, int64_t a1) {
    return (int64_t)mako_min(a0, a1);
}

int64_t mako_native_max(int64_t a0, int64_t a1) {
    return (int64_t)mako_max(a0, a1);
}

int64_t mako_native_clamp(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_clamp(a0, a1, a2);
}

double mako_native_math_sqrt(double a0) {
    return mako_math_sqrt(a0);
}

double mako_native_math_floor(double a0) {
    return mako_math_floor(a0);
}

MakoNativeString *mako_native_getcwd_ptr(void) {
    return bridge_take_str(mako_getcwd());
}

int64_t mako_native_is_dir_ptr(MakoNativeString *a0) {
    return (int64_t)mako_is_dir(bridge_borrow_str(a0));
}

int64_t mako_native_eprint_ptr(MakoNativeString *a0) {
    mako_eprint_raw(bridge_borrow_str(a0));
    return 0;
}

int64_t mako_native_eprintln_ptr(MakoNativeString *a0) {
    mako_eprintln_str(bridge_borrow_str(a0));
    return 0;
}

int64_t mako_native_mono_us(void) {
    return (int64_t)mako_mono_us();
}

int64_t mako_native_mono_ms(void) {
    return (int64_t)mako_mono_ms();
}

void mako_native_sleep_us(int64_t a0) {
    mako_sleep_us(a0);
}

int64_t mako_native_elapsed_us(int64_t a0) {
    return (int64_t)mako_elapsed_us(a0);
}

int64_t mako_native_elapsed_mono_ms(int64_t a0) {
    return (int64_t)mako_elapsed_mono_ms(a0);
}

int64_t mako_native_deadline_ms(int64_t a0) {
    return (int64_t)mako_deadline_ms(a0);
}

int64_t mako_native_deadline_expired(int64_t a0) {
    return (int64_t)mako_deadline_expired(a0);
}

int64_t mako_native_deadline_remaining_ns(int64_t a0) {
    return (int64_t)mako_deadline_remaining_ns(a0);
}

MakoNativeString *mako_native_format_float_ptr(double v, int64_t prec) {
    return bridge_take_str(mako_format_float(v, prec));
}

/* Cranelift []string header: MakoNativeString **data (see native_runtime.c). */
typedef struct {
    MakoNativeString **data;
    size_t len;
    size_t cap;
    int64_t owned;
} MakoNativeStringSlicePtr;

MakoNativeStringSlicePtr *mako_native_read_dir_ptr(MakoNativeString *path) {
    MakoStrArray a = mako_read_dir(bridge_borrow_str(path));
    MakoNativeStringSlicePtr *h =
        (MakoNativeStringSlicePtr *)calloc(1, sizeof(*h));
    if (!h) abort();
    h->len = a.len;
    h->cap = a.len ? a.len : 1;
    h->owned = 1;
    h->data = (MakoNativeString **)calloc(h->cap, sizeof(MakoNativeString *));
    if (!h->data) abort();
    for (size_t i = 0; i < a.len; ++i) {
        /* Take ownership of each MakoString into a native header. */
        h->data[i] = bridge_take_str(a.data[i]);
        if (!h->data[i]) {
            h->data[i] = mako_native_string_literal_ptr("", 0);
        }
    }
    free(a.data);
    return h;
}

int64_t mako_native_deadline_ns(int64_t a0) {
    return (int64_t)mako_deadline_ns(a0);
}

void mako_native_sleep_until_ns(int64_t a0) {
    mako_sleep_until_ns(a0);
}

MakoNativeString *mako_native_format_int_hex_ptr(int64_t a0) {
    return bridge_take_str(mako_format_int_hex(a0));
}

MakoNativeString *mako_native_format_int_hex_upper_ptr(int64_t a0) {
    return bridge_take_str(mako_format_int_hex_upper(a0));
}

MakoNativeString *mako_native_format_int_hex_prefix_ptr(int64_t a0) {
    return bridge_take_str(mako_format_int_hex_prefix(a0));
}

MakoNativeString *mako_native_time_format_ptr(int64_t a0) {
    return bridge_take_str(mako_time_format_rfc3339(a0));
}

int64_t mako_native_mutex_new(void) {
    return (int64_t)(intptr_t)mako_mutex_new();
}

void mako_native_mutex_lock(int64_t m) {
    mako_mutex_lock((MakoMutex *)(intptr_t)m);
}

void mako_native_mutex_unlock(int64_t m) {
    mako_mutex_unlock((MakoMutex *)(intptr_t)m);
}

void mako_native_spin_until_ns(int64_t a0) {
    mako_spin_until_ns(a0);
}

MakoNativeString *mako_native_format_int_bin_ptr(int64_t a0) {
    return bridge_take_str(mako_format_int_bin(a0));
}

MakoNativeString *mako_native_format_int_oct_ptr(int64_t a0) {
    return bridge_take_str(mako_format_int_oct(a0));
}

MakoNativeString *mako_native_format_int_dec_ptr(int64_t a0) {
    return bridge_take_str(mako_format_int_dec(a0));
}

MakoNativeString *mako_native_format_int_base_ptr(int64_t a0, int64_t a1) {
    return bridge_take_str(mako_format_int_base(a0, a1));
}

MakoNativeString *mako_native_format_int_hex_pad_ptr(int64_t a0, int64_t a1) {
    return bridge_take_str(mako_format_int_hex_pad(a0, a1));
}

MakoNativeString *mako_native_format_pad_ptr(MakoNativeString *s, int64_t width, int64_t zero) {
    return bridge_take_str(mako_format_pad(bridge_borrow_str(s), width, (int)zero));
}

int64_t mako_native_wall_us(void) {
    return (int64_t)mako_wall_us();
}

int64_t mako_native_wall_ns(void) {
    return (int64_t)mako_wall_ns();
}

// ---- bulk bridges round 4 ----

MakoNativeString *mako_native_fmt_sprintf4_ptr(
    MakoNativeString *fmt, MakoNativeString *a0, MakoNativeString *a1,
    MakoNativeString *a2, MakoNativeString *a3
) {
    return bridge_take_str(mako_fmt_sprintf4(
        bridge_borrow_str(fmt), bridge_borrow_str(a0), bridge_borrow_str(a1),
        bridge_borrow_str(a2), bridge_borrow_str(a3)));
}

MakoNativeString *mako_native_argon2id_hash_ptr(MakoNativeString *pw) {
    return bridge_take_str(mako_argon2id_hash(bridge_borrow_str(pw)));
}

int64_t mako_native_argon2id_verify_ptr(MakoNativeString *phc, MakoNativeString *pw) {
    return (int64_t)mako_argon2id_verify(bridge_borrow_str(phc), bridge_borrow_str(pw));
}

int64_t mako_native_fmt_print2_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_fmt_print2(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

int64_t mako_native_btree_put(int64_t t, int64_t key, int64_t val) {
    return (int64_t)mako_btree_put((MakoBTree *)(intptr_t)t, key, val);
}

int64_t mako_native_btree_get(int64_t t, int64_t key) {
    return (int64_t)mako_btree_get((MakoBTree *)(intptr_t)t, key);
}

int64_t mako_native_btree_free(int64_t t) {
    return (int64_t)mako_btree_free((MakoBTree *)(intptr_t)t);
}

int64_t mako_native_btree_range(int64_t t, int64_t lo, int64_t hi) {
    return (int64_t)mako_btree_range((MakoBTree *)(intptr_t)t, lo, hi);
}

int64_t mako_native_pbtree_new(void) {
    return (int64_t)(intptr_t)mako_pbtree_new();
}

int64_t mako_native_pbtree_put(int64_t t, int64_t key, int64_t val) {
    return (int64_t)mako_pbtree_put((MakoPageBTree *)(intptr_t)t, key, val);
}

int64_t mako_native_pbtree_get(int64_t t, int64_t key) {
    return (int64_t)mako_pbtree_get((MakoPageBTree *)(intptr_t)t, key);
}

int64_t mako_native_pbtree_len(int64_t t) {
    return (int64_t)mako_pbtree_len((MakoPageBTree *)(intptr_t)t);
}

int64_t mako_native_pbtree_pages(int64_t t) {
    return (int64_t)mako_pbtree_pages((MakoPageBTree *)(intptr_t)t);
}

int64_t mako_native_pbtree_free(int64_t t) {
    return (int64_t)mako_pbtree_free((MakoPageBTree *)(intptr_t)t);
}

// ---- WAL / LSM / bloom / page manager ----

int64_t mako_native_wal_open_ptr(MakoNativeString *path) {
    return (int64_t)(intptr_t)mako_wal_open(bridge_borrow_str(path));
}

int64_t mako_native_wal_close(int64_t w) {
    return (int64_t)mako_wal_close((MakoWal *)(intptr_t)w);
}

int64_t mako_native_lsm_new(int64_t mem_cap) {
    return (int64_t)(intptr_t)mako_lsm_new(mem_cap);
}

int64_t mako_native_lsm_free(int64_t l) {
    return (int64_t)mako_lsm_free((MakoLsm *)(intptr_t)l);
}

int64_t mako_native_lsm_attach_run(int64_t l, int64_t w) {
    return (int64_t)mako_lsm_attach_run((MakoLsm *)(intptr_t)l, (MakoWal *)(intptr_t)w);
}

int64_t mako_native_lsm_put(int64_t l, int64_t key, int64_t val) {
    return (int64_t)mako_lsm_put((MakoLsm *)(intptr_t)l, key, val);
}

int64_t mako_native_lsm_get(int64_t l, int64_t key) {
    return (int64_t)mako_lsm_get((MakoLsm *)(intptr_t)l, key);
}

int64_t mako_native_lsm_flush(int64_t l) {
    return (int64_t)mako_lsm_flush((MakoLsm *)(intptr_t)l);
}

int64_t mako_native_lsm_flushes(int64_t l) {
    return (int64_t)mako_lsm_flushes((MakoLsm *)(intptr_t)l);
}

int64_t mako_native_lsm_compact_ptr(int64_t l, MakoNativeString *sst) {
    return (int64_t)mako_lsm_compact((MakoLsm *)(intptr_t)l, bridge_borrow_str(sst));
}

int64_t mako_native_lsm_compact_down_ptr(int64_t l, MakoNativeString *sst) {
    return (int64_t)mako_lsm_compact_down((MakoLsm *)(intptr_t)l, bridge_borrow_str(sst));
}

int64_t mako_native_lsm_compactions(int64_t l) {
    return (int64_t)mako_lsm_compactions((MakoLsm *)(intptr_t)l);
}

int64_t mako_native_lsm_sst_levels(int64_t l) {
    return (int64_t)mako_lsm_sst_levels((MakoLsm *)(intptr_t)l);
}

int64_t mako_native_lsm_level_len(int64_t l, int64_t level) {
    return (int64_t)mako_lsm_level_len((MakoLsm *)(intptr_t)l, level);
}

int64_t mako_native_bloom_new(void) {
    return (int64_t)(intptr_t)mako_bloom_new();
}

int64_t mako_native_bloom_add(int64_t b, int64_t key) {
    return (int64_t)mako_bloom_add((MakoBloom *)(intptr_t)b, key);
}

int64_t mako_native_bloom_len(int64_t b) {
    return (int64_t)mako_bloom_len((MakoBloom *)(intptr_t)b);
}

int64_t mako_native_bloom_maybe(int64_t b, int64_t key) {
    return (int64_t)mako_bloom_maybe((MakoBloom *)(intptr_t)b, key);
}

int64_t mako_native_bloom_clear(int64_t b) {
    return (int64_t)mako_bloom_clear((MakoBloom *)(intptr_t)b);
}

int64_t mako_native_bloom_free(int64_t b) {
    return (int64_t)mako_bloom_free((MakoBloom *)(intptr_t)b);
}

int64_t mako_native_pman_open_ptr(MakoNativeString *path) {
    return (int64_t)(intptr_t)mako_pman_open(bridge_borrow_str(path));
}

int64_t mako_native_pman_alloc(int64_t pm) {
    return (int64_t)mako_pman_alloc((MakoPageMan *)(intptr_t)pm);
}

int64_t mako_native_pman_set(int64_t pm, int64_t page_id, int64_t slot, int64_t val) {
    return (int64_t)mako_pman_set((MakoPageMan *)(intptr_t)pm, page_id, slot, val);
}

int64_t mako_native_pman_get(int64_t pm, int64_t page_id, int64_t slot) {
    return (int64_t)mako_pman_get((MakoPageMan *)(intptr_t)pm, page_id, slot);
}

int64_t mako_native_pman_close(int64_t pm) {
    return (int64_t)mako_pman_close((MakoPageMan *)(intptr_t)pm);
}

int64_t mako_native_argon2_available(void) {
    return (int64_t)mako_argon2_available();
}

int64_t mako_native_fmt_printf3_ptr(
    MakoNativeString *fmt, MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2
) {
    return (int64_t)mako_fmt_printf3(
        bridge_borrow_str(fmt), bridge_borrow_str(a0), bridge_borrow_str(a1),
        bridge_borrow_str(a2));
}

int64_t mako_native_yaml_has_ptr(MakoNativeString *doc, MakoNativeString *key) {
    return (int64_t)mako_yaml_has(bridge_borrow_str(doc), bridge_borrow_str(key));
}

int64_t mako_native_chan_str_try_send(void *c, MakoNativeString *s) {
    return (int64_t)mako_chan_str_try_send((MakoChanStr *)c, bridge_borrow_str(s));
}

int64_t mako_native_cmap_has_ptr(void *m, MakoNativeString *k) {
    return (int64_t)mako_cmap_has((MakoCMap *)m, bridge_borrow_str(k));
}

int64_t mako_native_cmap_del_ptr(void *m, MakoNativeString *k) {
    return (int64_t)mako_cmap_del((MakoCMap *)m, bridge_borrow_str(k));
}

int64_t mako_native_cmap_len(void *m) {
    return (int64_t)mako_cmap_len((MakoCMap *)m);
}

int64_t mako_native_cmap_incr_ptr(void *m, MakoNativeString *k, int64_t delta) {
    return (int64_t)mako_cmap_incr((MakoCMap *)m, bridge_borrow_str(k), delta);
}

int64_t mako_native_btree_new(void) {
    return (int64_t)(intptr_t)mako_btree_new();
}

int64_t mako_native_sql_open_postgres_ptr(MakoNativeString *url) {
    MakoSqlDB db = mako_sql_open_postgres(bridge_borrow_str(url));
    return mako_native_sql_register(db);
}

/* []byte pointer ABI (native_runtime.c). */
typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    int64_t owned;
} MakoNativeByteSlice;
MakoNativeByteSlice *mako_native_byte_slice_make_ptr(int64_t len, int64_t cap);

MakoNativeByteSlice *mako_native_buf_get_ptr(int64_t need) {
    int64_t cap = need > 0 ? need : 64;
    return mako_native_byte_slice_make_ptr(0, cap);
}

void mako_native_buf_put_ptr(MakoNativeByteSlice *b) {
    if (!b) return;
    if (b->owned && b->data) free(b->data);
    free(b);
}

MakoNativeByteSlice *mako_native_as_bytes_ptr(MakoNativeString *s) {
    /* Borrowed view of string bytes (cap=0, owned=0). Header is still heap. */
    MakoNativeByteSlice *out = (MakoNativeByteSlice *)calloc(1, sizeof(*out));
    if (!out) abort();
    out->data = (uint8_t *)(uintptr_t)(s && s->data ? s->data : "");
    out->len = s ? (s->len & ~((size_t)1 << (sizeof(size_t) * 8 - 1))) : 0;
    out->cap = 0;
    out->owned = 0;
    return out;
}

MakoNativeString *mako_native_bytes_as_str_ptr(MakoNativeByteSlice *b) {
    const char *p = (const char *)(b && b->data ? b->data : "");
    size_t n = b ? b->len : 0;
    return mako_native_string_literal_ptr(p, n);
}

int64_t mako_native_bytes_is_view_ptr(MakoNativeByteSlice *b) {
    return (b && b->cap == 0) ? 1 : 0;
}

MakoNativeByteSlice *mako_native_bytes_view_ptr(MakoNativeString *s, int64_t lo, int64_t hi) {
    size_t n = s ? (s->len & ~((size_t)1 << (sizeof(size_t) * 8 - 1))) : 0;
    if (lo < 0) lo = 0;
    if (hi < lo) hi = lo;
    if ((size_t)hi > n) hi = (int64_t)n;
    if ((size_t)lo > n) lo = (int64_t)n;
    MakoNativeByteSlice *out = (MakoNativeByteSlice *)calloc(1, sizeof(*out));
    if (!out) abort();
    const char *base = s && s->data ? s->data : "";
    out->data = (uint8_t *)(uintptr_t)(base + lo);
    out->len = (size_t)(hi - lo);
    out->cap = 0;
    out->owned = 0;
    return out;
}

int64_t mako_native_chan_str_send_take(void *c, MakoNativeString *s) {
    /* Native string channels reuse MakoChan with the string header pointer as
     * the i64 payload (see ChanS send/recv in native_ir). Take ownership of `s`
     * on success; free it if the send fails so the call always consumes. */
    if (!s) {
        return mako_chan_send((MakoChan *)c, 0);
    }
    int64_t ok = mako_chan_send((MakoChan *)c, (int64_t)(intptr_t)s);
    if (!ok) {
        mako_native_string_drop_ptr(s);
    }
    return ok;
}

int64_t mako_native_chan_str_try_send_take(void *c, MakoNativeString *s) {
    if (!s) {
        return mako_chan_try_send((MakoChan *)c, 0);
    }
    int64_t ok = mako_chan_try_send((MakoChan *)c, (int64_t)(intptr_t)s);
    if (!ok) {
        mako_native_string_drop_ptr(s);
    }
    return ok;
}

MakoNativeIntSlice *mako_native_list_new_int_ptr(void) {
    return mako_native_int_slice_make_ptr(0, 8);
}

int64_t mako_native_yaml_get_bool_ptr(MakoNativeString *doc, MakoNativeString *key) {
    return (int64_t)mako_yaml_get_bool(bridge_borrow_str(doc), bridge_borrow_str(key));
}

int64_t mako_native_toml_get_int_ptr(MakoNativeString *doc, MakoNativeString *key) {
    return (int64_t)mako_toml_get_int(bridge_borrow_str(doc), bridge_borrow_str(key));
}

int64_t mako_native_validate_max_len_ptr(MakoNativeString *s, int64_t max_len) {
    return (int64_t)mako_validate_max_len(bridge_borrow_str(s), max_len);
}

int64_t mako_native_utf8_max_rune(void) {
    return (int64_t)mako_utf8_max_rune();
}

MakoNativeString *mako_native_url_path_ptr(MakoNativeString *u) {
    return bridge_take_str(mako_url_path(bridge_borrow_str(u)));
}

int64_t mako_native_zip_deflate_available(void) {
    return (int64_t)mako_zip_deflate_available();
}
// ---- auto domain-track bridges ----
int64_t mako_native_range_val_at(int64_t a0) {
    return (int64_t)mako_range_val_at(a0);
}

int64_t mako_native_range_key_at(int64_t a0) {
    return (int64_t)mako_range_key_at(a0);
}

int64_t mako_native_range_len(void) {
    return (int64_t)mako_range_len();
}

int64_t mako_native_range_cap(void) {
    return (int64_t)mako_range_cap();
}

int64_t mako_native_range_next(void) {
    return (int64_t)mako_range_next();
}

int64_t mako_native_range_rewind(void) {
    return (int64_t)mako_range_rewind();
}

int64_t mako_native_btree_put_str_ptr(int64_t a0, MakoNativeString *a1, int64_t a2) {
    return (int64_t)mako_btree_put_str((void *)(intptr_t)a0, bridge_borrow_str(a1), a2);
}

int64_t mako_native_btree_get_str_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_btree_get_str((void *)(intptr_t)a0, bridge_borrow_str(a1));
}

int64_t mako_native_btree_save_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_btree_save((void *)(intptr_t)a0, bridge_borrow_str(a1));
}

int64_t mako_native_btree_load_ptr(MakoNativeString *a0) {
    return (int64_t)(intptr_t)mako_btree_load(bridge_borrow_str(a0));
}

int64_t mako_native_bloom_add_str_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_bloom_add_str((void *)(intptr_t)a0, bridge_borrow_str(a1));
}

int64_t mako_native_bloom_maybe_str_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_bloom_maybe_str((void *)(intptr_t)a0, bridge_borrow_str(a1));
}

int64_t mako_native_domain_reg_del(int64_t a0) {
    return (int64_t)mako_domain_reg_del(a0);
}

int64_t mako_native_domain_reg_put_bloom(int64_t a0) {
    return (int64_t)mako_domain_reg_put_bloom((void *)(intptr_t)a0);
}

int64_t mako_native_domain_reg_put_btree(int64_t a0) {
    return (int64_t)mako_domain_reg_put_btree((void *)(intptr_t)a0);
}

int64_t mako_native_domain_reg_put_pman(int64_t a0) {
    return (int64_t)mako_domain_reg_put_pman((void *)(intptr_t)a0);
}

int64_t mako_native_domain_reg_get_bloom(int64_t a0) {
    return (int64_t)(intptr_t)mako_domain_reg_get_bloom(a0);
}

int64_t mako_native_domain_reg_get_btree(int64_t a0) {
    return (int64_t)(intptr_t)mako_domain_reg_get_btree(a0);
}

int64_t mako_native_domain_reg_get_pman(int64_t a0) {
    return (int64_t)(intptr_t)mako_domain_reg_get_pman(a0);
}

int64_t mako_native_pman_pages(int64_t a0) {
    return (int64_t)mako_pman_pages((void *)(intptr_t)a0);
}

int64_t mako_native_pman_reads(int64_t a0) {
    return (int64_t)mako_pman_reads((void *)(intptr_t)a0);
}

int64_t mako_native_pman_writes(int64_t a0) {
    return (int64_t)mako_pman_writes((void *)(intptr_t)a0);
}

int64_t mako_native_pman_sync(int64_t a0) {
    return (int64_t)mako_pman_sync((void *)(intptr_t)a0);
}

MakoNativeString *mako_native_pman_read_page_ptr(int64_t a0, int64_t a1) {
    return bridge_take_str(mako_pman_read_page((void *)(intptr_t)a0, a1));
}

int64_t mako_native_pman_write_page_ptr(int64_t a0, int64_t a1, MakoNativeString *a2) {
    return (int64_t)mako_pman_write_page((void *)(intptr_t)a0, a1, bridge_borrow_str(a2));
}

int64_t mako_native_hot_reload_watch_ptr(MakoNativeString *a0) {
    return (int64_t)mako_hot_reload_watch(bridge_borrow_str(a0));
}

int64_t mako_native_hot_reload_unwatch_ptr(MakoNativeString *a0) {
    return (int64_t)mako_hot_reload_unwatch(bridge_borrow_str(a0));
}

int64_t mako_native_hot_reload_changed_ptr(MakoNativeString *a0) {
    return (int64_t)mako_hot_reload_changed(bridge_borrow_str(a0));
}

int64_t mako_native_file_mtime_ns_ptr(MakoNativeString *a0) {
    return (int64_t)mako_file_mtime_ns(bridge_borrow_str(a0));
}

int64_t mako_native_mvcc_new(void) {
    return (int64_t)(intptr_t)mako_mvcc_new();
}

int64_t mako_native_mvcc_free(int64_t a0) {
    return (int64_t)mako_mvcc_free((void *)(intptr_t)a0);
}

int64_t mako_native_mvcc_begin(int64_t a0) {
    return (int64_t)mako_mvcc_begin((void *)(intptr_t)a0);
}

int64_t mako_native_mvcc_put(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_mvcc_put((void *)(intptr_t)a0, a1, a2);
}

int64_t mako_native_mvcc_get(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_mvcc_get((void *)(intptr_t)a0, a1, a2);
}

int64_t mako_native_mvcc_versions(int64_t a0) {
    return (int64_t)mako_mvcc_versions((void *)(intptr_t)a0);
}

int64_t mako_native_multimap_new(void) {
    return (int64_t)(intptr_t)mako_multimap_new();
}

int64_t mako_native_multimap_free(int64_t a0) {
    return (int64_t)mako_multimap_free((void *)(intptr_t)a0);
}

int64_t mako_native_multimap_put(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_multimap_put((void *)(intptr_t)a0, a1, a2);
}

int64_t mako_native_multimap_get(int64_t a0, int64_t a1) {
    return (int64_t)mako_multimap_get((void *)(intptr_t)a0, a1);
}

int64_t mako_native_multimap_get_all(int64_t a0, int64_t a1) {
    return (int64_t)mako_multimap_get_all((void *)(intptr_t)a0, a1);
}

int64_t mako_native_multimap_len(int64_t a0) {
    return (int64_t)mako_multimap_len((void *)(intptr_t)a0);
}

int64_t mako_native_multimap_range(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_multimap_range((void *)(intptr_t)a0, a1, a2);
}

int64_t mako_native_rollback_new(int64_t a0) {
    return (int64_t)(intptr_t)mako_rollback_new(a0);
}

int64_t mako_native_rollback_free(int64_t a0) {
    return (int64_t)mako_rollback_free((void *)(intptr_t)a0);
}

int64_t mako_native_rollback_push_ptr(int64_t a0, int64_t a1, MakoNativeString *a2) {
    return (int64_t)mako_rollback_push((void *)(intptr_t)a0, a1, bridge_borrow_str(a2));
}

int64_t mako_native_rollback_get(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_rollback_get((void *)(intptr_t)a0, a1, a2);
}

int64_t mako_native_rollback_len(int64_t a0) {
    return (int64_t)mako_rollback_len((void *)(intptr_t)a0);
}

int64_t mako_native_rollback_restore_slot0(int64_t a0, int64_t a1) {
    return (int64_t)mako_rollback_restore_slot0((void *)(intptr_t)a0, a1);
}

int64_t mako_native_store_new(int64_t a0) {
    return (int64_t)(intptr_t)mako_store_new(a0);
}

int64_t mako_native_store_free(int64_t a0) {
    return (int64_t)mako_store_free((void *)(intptr_t)a0);
}

int64_t mako_native_store_attach_wal(int64_t a0, int64_t a1) {
    return (int64_t)mako_store_attach_wal((void *)(intptr_t)a0, (void *)(intptr_t)a1);
}

int64_t mako_native_store_begin(int64_t a0) {
    return (int64_t)mako_store_begin((void *)(intptr_t)a0);
}

int64_t mako_native_store_put(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_store_put((void *)(intptr_t)a0, a1, a2);
}

int64_t mako_native_store_get(int64_t a0, int64_t a1) {
    return (int64_t)mako_store_get((void *)(intptr_t)a0, a1);
}

int64_t mako_native_store_commit(int64_t a0) {
    return (int64_t)mako_store_commit((void *)(intptr_t)a0);
}

int64_t mako_native_store_recover_wal(int64_t a0, int64_t a1) {
    return (int64_t)mako_store_recover_wal((void *)(intptr_t)a0, (void *)(intptr_t)a1);
}

int64_t mako_native_sst_free(int64_t a0) {
    return (int64_t)mako_sst_free((void *)(intptr_t)a0);
}

int64_t mako_native_sst_get(int64_t a0, int64_t a1) {
    return (int64_t)mako_sst_get((void *)(intptr_t)a0, a1);
}

int64_t mako_native_sst_len(int64_t a0) {
    return (int64_t)mako_sst_len((void *)(intptr_t)a0);
}

int64_t mako_native_sst_range(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_sst_range((void *)(intptr_t)a0, a1, a2);
}

int64_t mako_native_gfx_window_open_ptr(int64_t a0, int64_t a1, MakoNativeString *a2) {
    return (int64_t)(intptr_t)mako_gfx_window_open(a0, a1, bridge_borrow_str(a2));
}

int64_t mako_native_gfx_window_close(int64_t a0) {
    return (int64_t)mako_gfx_window_close((void *)(intptr_t)a0);
}

int64_t mako_native_gfx_window_width(int64_t a0) {
    return (int64_t)mako_gfx_window_width((void *)(intptr_t)a0);
}

int64_t mako_native_gfx_window_height(int64_t a0) {
    return (int64_t)mako_gfx_window_height((void *)(intptr_t)a0);
}

int64_t mako_native_gfx_shader_compile_ptr(MakoNativeString *a0) {
    return (int64_t)mako_gfx_shader_compile(bridge_borrow_str(a0));
}

int64_t mako_native_audio_mix(int64_t a0, int64_t a1) {
    return (int64_t)mako_audio_mix(a0, a1);
}

int64_t mako_native_physics_step_x(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_physics_step_x(a0, a1, a2);
}

int64_t mako_native_physics_step_v(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_physics_step_v(a0, a1, a2);
}

int64_t mako_native_ai_rope_cos(int64_t a0) {
    return (int64_t)mako_ai_rope_cos(a0);
}

int64_t mako_native_ai_rope_apply_x(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_ai_rope_apply_x(a0, a1, a2);
}

int64_t mako_native_kv_cache_new(int64_t a0) {
    return (int64_t)(intptr_t)mako_kv_cache_new(a0);
}

int64_t mako_native_kv_cache_free(int64_t a0) {
    return (int64_t)mako_kv_cache_free((void *)(intptr_t)a0);
}

int64_t mako_native_kv_cache_append(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_kv_cache_append((void *)(intptr_t)a0, a1, a2);
}

int64_t mako_native_kv_cache_get_k(int64_t a0, int64_t a1) {
    return (int64_t)mako_kv_cache_get_k((void *)(intptr_t)a0, a1);
}

int64_t mako_native_kv_cache_get_v(int64_t a0, int64_t a1) {
    return (int64_t)mako_kv_cache_get_v((void *)(intptr_t)a0, a1);
}

int64_t mako_native_kv_cache_len(int64_t a0) {
    return (int64_t)mako_kv_cache_len((void *)(intptr_t)a0);
}

int64_t mako_native_f32_to_f16_bits(int64_t a0) {
    return (int64_t)mako_f32_to_f16_bits(a0);
}

int64_t mako_native_debug_set_loc_ptr(MakoNativeString *a0, int64_t a1) {
    return (int64_t)mako_debug_set_loc(bridge_borrow_str(a0), a1);
}

MakoNativeString *mako_native_debug_frame_json_ptr(void) {
    return bridge_take_str(mako_debug_frame_json());
}

MakoNativeString *mako_native_snap_encode4_ptr(int64_t a0, int64_t a1, int64_t a2, int64_t a3) {
    return bridge_take_str(mako_snap_encode4(a0, a1, a2, a3));
}


// ---- SST / gemm / debug / builder ----

int64_t mako_native_sst_build4_ptr(
    MakoNativeString *path,
    int64_t k0, int64_t v0, int64_t k1, int64_t v1,
    int64_t k2, int64_t v2, int64_t k3, int64_t v3
) {
    return (int64_t)(intptr_t)mako_sst_build4(
        bridge_borrow_str(path), k0, v0, k1, v1, k2, v2, k3, v3);
}

int64_t mako_native_sst_build8_ptr(
    MakoNativeString *path,
    int64_t k0, int64_t v0, int64_t k1, int64_t v1,
    int64_t k2, int64_t v2, int64_t k3, int64_t v3,
    int64_t k4, int64_t v4, int64_t k5, int64_t v5,
    int64_t k6, int64_t v6, int64_t k7, int64_t v7
) {
    return (int64_t)(intptr_t)mako_sst_build8(
        bridge_borrow_str(path), k0, v0, k1, v1, k2, v2, k3, v3,
        k4, v4, k5, v5, k6, v6, k7, v7);
}

int64_t mako_native_sst_build_n_ptr(
    MakoNativeString *path, int64_t n,
    int64_t k0, int64_t v0, int64_t k1, int64_t v1,
    int64_t k2, int64_t v2, int64_t k3, int64_t v3,
    int64_t k4, int64_t v4, int64_t k5, int64_t v5,
    int64_t k6, int64_t v6, int64_t k7, int64_t v7
) {
    return (int64_t)(intptr_t)mako_sst_build_n(
        bridge_borrow_str(path), n, k0, v0, k1, v1, k2, v2, k3, v3,
        k4, v4, k5, v5, k6, v6, k7, v7);
}

int64_t mako_native_gemm2x2(
    int64_t a00, int64_t a01, int64_t a10, int64_t a11,
    int64_t b00, int64_t b01, int64_t b10, int64_t b11
) {
    return (int64_t)mako_gemm2x2(a00, a01, a10, a11, b00, b01, b10, b11);
}

int64_t mako_native_gemm_c01(void) { return (int64_t)mako_gemm_c01_get(); }
int64_t mako_native_gemm_c10(void) { return (int64_t)mako_gemm_c10_get(); }
int64_t mako_native_gemm_c11(void) { return (int64_t)mako_gemm_c11_get(); }

MakoNativeString *mako_native_debug_file_ptr(void) {
    return bridge_take_str(mako_debug_file_get());
}

int64_t mako_native_debug_line(void) {
    return (int64_t)mako_debug_line_get();
}

void mako_native_builder_write_slice_ptr(
    int64_t b, MakoNativeString *s, int64_t off, int64_t len
) {
    mako_str_builder_write_slice(
        (MakoStrBuilder *)(intptr_t)b, bridge_borrow_str(s), off, len);
}

int64_t mako_native_str_hash64_ptr(MakoNativeString *s) {
    return (int64_t)mako_str_hash64(bridge_borrow_str(s));
}

int64_t mako_native_str_slice_ci_index_ptr(
    MakoNativeString *s, int64_t off, int64_t len, MakoNativeString *needle
) {
    return (int64_t)mako_str_slice_ci_index(
        bridge_borrow_str(s), off, len, bridge_borrow_str(needle));
}

int64_t mako_native_str_slice_ci_starts_ptr(
    MakoNativeString *s, int64_t off, MakoNativeString *prefix
) {
    return (int64_t)mako_str_slice_ci_starts(
        bridge_borrow_str(s), off, bridge_borrow_str(prefix));
}

int64_t mako_native_file_open_ptr(MakoNativeString *path, int64_t mode, int64_t flags) {
    return (int64_t)mako_file_open(bridge_borrow_str(path), mode, flags);
}

int64_t mako_native_file_close(int64_t fd) {
    return (int64_t)mako_file_close(fd);
}

int64_t mako_native_file_append2_ptr(int64_t fd, MakoNativeString *a, MakoNativeString *b) {
    return (int64_t)mako_file_append2(fd, bridge_borrow_str(a), bridge_borrow_str(b));
}

int64_t mako_native_file_append3_ptr(
    int64_t fd, MakoNativeString *a, MakoNativeString *b, MakoNativeString *c
) {
    return (int64_t)mako_file_append3(
        fd, bridge_borrow_str(a), bridge_borrow_str(b), bridge_borrow_str(c)
    );
}

int64_t mako_native_file_seek(int64_t fd, int64_t offset, int64_t whence) {
    return (int64_t)mako_file_seek(fd, offset, whence);
}

MakoNativeString *mako_native_file_read_exact_ptr(int64_t fd, int64_t count) {
    return bridge_take_str(mako_file_read_exact(fd, count));
}

int64_t mako_native_fdatasync(int64_t fd) {
    return (int64_t)mako_fdatasync(fd);
}

int64_t mako_native_fallocate(int64_t fd, int64_t size) {
    return (int64_t)mako_fallocate(fd, size);
}

/* ---- simple stdlib / goext bridges ---- */
int64_t mako_native_fmt_eprintf_ptr(MakoNativeString *fmt, MakoNativeString *a0) {
    return (int64_t)mako_fmt_eprintf1(bridge_borrow_str(fmt), bridge_borrow_str(a0));
}

int64_t mako_native_is_file_ptr(MakoNativeString *path) {
    return (int64_t)mako_is_file(bridge_borrow_str(path));
}

MakoNativeString *mako_native_session_id_new_ptr(void) {
    return bridge_take_str(mako_session_id_new());
}

int64_t mako_native_png_available(void) {
    return (int64_t)mako_png_available();
}

MakoNativeString *mako_native_binary_put_u32be_ptr(int64_t v) {
    return bridge_take_str(mako_binary_put_u32be(v));
}

MakoNativeString *mako_native_bcrypt_hash_ptr(MakoNativeString *password, int64_t cost) {
    return bridge_take_str(mako_bcrypt_hash(bridge_borrow_str(password), cost));
}

int64_t mako_native_bcrypt_verify_ptr(MakoNativeString *hash, MakoNativeString *password) {
    return (int64_t)mako_bcrypt_verify(bridge_borrow_str(hash), bridge_borrow_str(password));
}

int64_t mako_native_bcrypt_available(void) {
    return (int64_t)mako_bcrypt_available();
}

/* Non-owning view: same header pointer; caller must not free via this alias. */
MakoNativeString *mako_native_str_as_view_ptr(MakoNativeString *s) {
    return s;
}

int64_t mako_native_dns_is_loopback_ptr(MakoNativeString *ip) {
    return (int64_t)mako_dns_is_loopback(bridge_borrow_str(ip));
}

/* ---- list[int] aliases (IntSlice pointer ABI) ---- */
MakoNativeIntSlice *mako_native_int_slice_append_ptr(MakoNativeIntSlice *value, int64_t element);
int64_t mako_native_int_slice_len_ptr(const MakoNativeIntSlice *value);
int64_t mako_native_int_slice_get_ptr(const MakoNativeIntSlice *value, int64_t index);
typedef struct {
    MakoNativeString **data;
    size_t len;
    size_t cap;
    int64_t owned;
} MakoNativeStringSlice;
MakoNativeStringSlice *mako_native_string_slice_clone_ptr(const MakoNativeStringSlice *value);

MakoNativeIntSlice *mako_native_list_push_int_ptr(MakoNativeIntSlice *a, int64_t v) {
    return mako_native_int_slice_append_ptr(a, v);
}

int64_t mako_native_list_len_int_ptr(MakoNativeIntSlice *a) {
    return mako_native_int_slice_len_ptr(a);
}

int64_t mako_native_list_get_int_ptr(MakoNativeIntSlice *a, int64_t i) {
    return mako_native_int_slice_get_ptr(a, i);
}

static int64_t mako_native_list_popped_int_tls = 0;

MakoNativeIntSlice *mako_native_list_pop_int_ptr(MakoNativeIntSlice *a) {
    if (!a || a->len == 0) {
        mako_native_list_popped_int_tls = 0;
        return a ? a : mako_native_int_slice_make_ptr(0, 0);
    }
    mako_native_list_popped_int_tls = a->data[a->len - 1];
    a->len--;
    return a;
}

int64_t mako_native_list_popped_int(void) {
    return mako_native_list_popped_int_tls;
}

int64_t mako_native_stack_peek_int_ptr(MakoNativeIntSlice *a) {
    if (!a || a->len == 0) return 0;
    return a->data[a->len - 1];
}

MakoNativeString *mako_native_stack_peek_str_ptr(MakoNativeStringSlice *a) {
    if (!a || a->len == 0) {
        return bridge_take_str(mako_str_from_cstr(""));
    }
    /* Clone last element so the caller owns a stable header. */
    MakoString borrowed = bridge_borrow_str(a->data[a->len - 1]);
    return bridge_take_str(mako_str_clone(borrowed));
}

MakoNativeIntSlice *mako_native_list_clear_int_ptr(MakoNativeIntSlice *a) {
    if (a) a->len = 0;
    return a ? a : mako_native_int_slice_make_ptr(0, 0);
}

int64_t mako_native_list_sum_int_ptr(MakoNativeIntSlice *a) {
    if (!a) return 0;
    int64_t s = 0;
    for (size_t i = 0; i < a->len; i++) s += a->data[i];
    return s;
}

/* list_insert / list_remove — simple in-place rebuild */
MakoNativeIntSlice *mako_native_list_insert_int_ptr(
    MakoNativeIntSlice *a, int64_t idx, int64_t v
) {
    if (!a) a = mako_native_int_slice_make_ptr(0, 8);
    size_t n = a->len;
    size_t at = (idx < 0) ? 0 : ((size_t)idx > n ? n : (size_t)idx);
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr((int64_t)(n + 1), (int64_t)(n + 1));
    for (size_t i = 0; i < at; i++) out->data[i] = a->data[i];
    out->data[at] = v;
    for (size_t i = at; i < n; i++) out->data[i + 1] = a->data[i];
    out->len = n + 1;
    return out;
}

MakoNativeIntSlice *mako_native_list_remove_int_ptr(MakoNativeIntSlice *a, int64_t idx) {
    if (!a || a->len == 0) return a ? a : mako_native_int_slice_make_ptr(0, 0);
    size_t n = a->len;
    if (idx < 0 || (size_t)idx >= n) return a;
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr((int64_t)(n - 1), (int64_t)(n - 1));
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (i == (size_t)idx) continue;
        out->data[j++] = a->data[i];
    }
    out->len = n - 1;
    return out;
}

/* strings_copy → str slice clone */
MakoNativeStringSlice *mako_native_strings_copy_ptr(MakoNativeStringSlice *a) {
    return mako_native_string_slice_clone_ptr(a);
}

/* ---- hmac / csrf / rwmutex ---- */
MakoNativeString *mako_native_hmac_sha256_raw_ptr(MakoNativeString *k, MakoNativeString *m) {
    return bridge_take_str(mako_hmac_sha256_raw(bridge_borrow_str(k), bridge_borrow_str(m)));
}

MakoNativeString *mako_native_csrf_token_ptr(void) {
    return bridge_take_str(mako_csrf_token());
}

int64_t mako_native_rwmutex_new(void) {
    return (int64_t)(intptr_t)mako_rwmutex_new();
}

void mako_native_rwmutex_rlock(int64_t m) {
    mako_rwmutex_rlock((MakoRWMutex *)(intptr_t)m);
}

void mako_native_rwmutex_runlock(int64_t m) {
    mako_rwmutex_runlock((MakoRWMutex *)(intptr_t)m);
}

void mako_native_rwmutex_lock(int64_t m) {
    mako_rwmutex_lock((MakoRWMutex *)(intptr_t)m);
}

void mako_native_rwmutex_unlock(int64_t m) {
    mako_rwmutex_unlock((MakoRWMutex *)(intptr_t)m);
}

/* ---- list[string] aliases (StringSlice pointer ABI) ---- */
MakoNativeString *mako_native_string_clone_ptr(const MakoNativeString *value);
MakoNativeStringSlice *mako_native_string_slice_make_ptr(size_t len, size_t cap);
MakoNativeStringSlice *mako_native_string_slice_append_ptr(
    MakoNativeStringSlice *value, MakoNativeString *element
);
int64_t mako_native_string_slice_len_ptr(const MakoNativeStringSlice *value);
MakoNativeString *mako_native_string_slice_get_ptr(
    const MakoNativeStringSlice *value, int64_t index
);

MakoNativeStringSlice *mako_native_list_new_str_ptr(void) {
    return mako_native_string_slice_make_ptr(0, 8);
}

MakoNativeStringSlice *mako_native_list_push_str_ptr(
    MakoNativeStringSlice *a, MakoNativeString *v
) {
    if (!a) a = mako_native_string_slice_make_ptr(0, 8);
    return mako_native_string_slice_append_ptr(a, v);
}

int64_t mako_native_list_len_str_ptr(MakoNativeStringSlice *a) {
    return mako_native_string_slice_len_ptr(a);
}

MakoNativeString *mako_native_list_get_str_ptr(MakoNativeStringSlice *a, int64_t i) {
    return mako_native_string_slice_get_ptr(a, i);
}

static MakoNativeString *mako_native_list_popped_str_tls = NULL;

MakoNativeStringSlice *mako_native_list_pop_str_ptr(MakoNativeStringSlice *a) {
    if (!a || a->len == 0) {
        if (mako_native_list_popped_str_tls) {
            mako_native_string_drop_ptr(mako_native_list_popped_str_tls);
            mako_native_list_popped_str_tls = NULL;
        }
        mako_native_list_popped_str_tls = mako_native_string_literal_ptr("", 0);
        return a ? a : mako_native_string_slice_make_ptr(0, 0);
    }
    if (mako_native_list_popped_str_tls) {
        mako_native_string_drop_ptr(mako_native_list_popped_str_tls);
    }
    /* Take ownership of last element (no clone). */
    mako_native_list_popped_str_tls = a->data[a->len - 1];
    a->data[a->len - 1] = NULL;
    a->len--;
    return a;
}

MakoNativeString *mako_native_list_popped_str_ptr(void) {
    if (!mako_native_list_popped_str_tls) {
        return mako_native_string_literal_ptr("", 0);
    }
    MakoNativeString *out = mako_native_list_popped_str_tls;
    mako_native_list_popped_str_tls = NULL;
    return out;
}

MakoNativeStringSlice *mako_native_list_clear_str_ptr(MakoNativeStringSlice *a) {
    if (!a) return mako_native_string_slice_make_ptr(0, 0);
    for (size_t i = 0; i < a->len; i++) {
        mako_native_string_drop_ptr(a->data[i]);
        a->data[i] = NULL;
    }
    a->len = 0;
    return a;
}

MakoNativeStringSlice *mako_native_list_insert_str_ptr(
    MakoNativeStringSlice *a, int64_t idx, MakoNativeString *v
) {
    if (!a) a = mako_native_string_slice_make_ptr(0, 8);
    size_t n = a->len;
    size_t at = (idx < 0) ? 0 : ((size_t)idx > n ? n : (size_t)idx);
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(n + 1, n + 1);
    for (size_t i = 0; i < at; i++)
        out->data[i] = mako_native_string_clone_ptr(a->data[i]);
    out->data[at] = mako_native_string_clone_ptr(v);
    for (size_t i = at; i < n; i++)
        out->data[i + 1] = mako_native_string_clone_ptr(a->data[i]);
    out->len = n + 1;
    return out;
}

MakoNativeStringSlice *mako_native_list_remove_str_ptr(
    MakoNativeStringSlice *a, int64_t idx
) {
    if (!a || a->len == 0) return a ? a : mako_native_string_slice_make_ptr(0, 0);
    size_t n = a->len;
    if (idx < 0 || (size_t)idx >= n) return a;
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(n - 1, n - 1);
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (i == (size_t)idx) continue;
        out->data[j++] = mako_native_string_clone_ptr(a->data[i]);
    }
    out->len = n - 1;
    return out;
}

int64_t mako_native_strings_index_ptr(MakoNativeStringSlice *a, MakoNativeString *s) {
    if (!a || !s) return -1;
    size_t sl = s->data ? (s->len & ~((size_t)1 << (sizeof(size_t) * 8 - 1))) : 0;
    for (size_t i = 0; i < a->len; i++) {
        MakoNativeString *e = a->data[i];
        size_t el = e && e->data ? (e->len & ~((size_t)1 << (sizeof(size_t) * 8 - 1))) : 0;
        if (e && el == sl && (sl == 0 || memcmp(e->data, s->data, sl) == 0))
            return (int64_t)i;
    }
    return -1;
}

MakoNativeString *mako_native_string_clone_ptr(const MakoNativeString *value);

static int64_t mako_native_queue_popped_int_tls = 0;

MakoNativeIntSlice *mako_native_queue_pop_int_ptr(MakoNativeIntSlice *a) {
    if (!a || a->len == 0) {
        mako_native_queue_popped_int_tls = 0;
        return a ? a : mako_native_int_slice_make_ptr(0, 0);
    }
    mako_native_queue_popped_int_tls = a->data[0];
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr((int64_t)(a->len - 1), (int64_t)(a->len - 1));
    for (size_t i = 1; i < a->len; i++) out->data[i - 1] = a->data[i];
    out->len = a->len - 1;
    return out;
}

int64_t mako_native_queue_popped_int(void) {
    return mako_native_queue_popped_int_tls;
}

/* ---- slices_reverse / unique ---- */
MakoNativeStringSlice *mako_native_slices_reverse_strs_ptr(MakoNativeStringSlice *a) {
    size_t n = a ? a->len : 0;
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(n, n);
    if (!a || !n) return out;
    for (size_t i = 0; i < n; i++)
        out->data[i] = mako_native_string_clone_ptr(a->data[n - 1 - i]);
    out->len = n;
    return out;
}

MakoNativeStringSlice *mako_native_slices_unique_strs_ptr(MakoNativeStringSlice *a) {
    /* sort + unique via C stdlib path: convert to MakoStrArray is heavy;
     * simple O(n^2) unique preserving first occurrence. */
    size_t n = a ? a->len : 0;
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(n, n);
    if (!a || !n) return out;
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        MakoNativeString *s = a->data[i];
        int seen = 0;
        for (size_t k = 0; k < j; k++) {
            MakoNativeString *t = out->data[k];
            size_t sl = s && s->data ? (s->len & ~((size_t)1 << (sizeof(size_t) * 8 - 1))) : 0;
            size_t tl = t && t->data ? (t->len & ~((size_t)1 << (sizeof(size_t) * 8 - 1))) : 0;
            if (s && t && sl == tl && (sl == 0 || memcmp(s->data, t->data, sl) == 0)) {
                seen = 1;
                break;
            }
        }
        if (!seen)
            out->data[j++] = mako_native_string_clone_ptr(s);
    }
    out->len = j;
    return out;
}

MakoNativeIntSlice *mako_native_slices_reverse_ints_ptr(MakoNativeIntSlice *a) {
    size_t n = a ? a->len : 0;
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr(n, n);
    if (!a || !n) return out;
    for (size_t i = 0; i < n; i++) out->data[i] = a->data[n - 1 - i];
    out->len = n;
    return out;
}

MakoNativeIntSlice *mako_native_slices_unique_ints_ptr(MakoNativeIntSlice *a) {
    /* Match C runtime: sort then unique so order is ascending (tests expect u[0]==1). */
    size_t n = a ? a->len : 0;
    if (!a || !n) return mako_native_int_slice_make_ptr(0, 0);
    MakoIntArray arr = mako_int_array_view(a->data, a->len);
    MakoIntArray uniq = mako_slices_unique_ints(arr);
    size_t j = uniq.len;
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr(j, j ? j : 1);
    for (size_t i = 0; i < j; i++) out->data[i] = uniq.data[i];
    free(uniq.data);
    out->len = j;
    return out;
}

/* ---- high-yield corpus bridges ---- */
typedef struct MakoNativeMapSI MakoNativeMapSI;
void mako_native_map_si_set_ptr(MakoNativeMapSI *m, MakoNativeString *key, int64_t val);

MakoNativeString *mako_native_sha256_raw_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_sha256_raw(bridge_borrow_str(s)));
}

int64_t mako_native_list_min_int_ptr(MakoNativeIntSlice *a) {
    if (!a || a->len == 0) return 0;
    int64_t m = a->data[0];
    for (size_t i = 1; i < a->len; i++) if (a->data[i] < m) m = a->data[i];
    return m;
}

int64_t mako_native_list_max_int_ptr(MakoNativeIntSlice *a) {
    if (!a || a->len == 0) return 0;
    int64_t m = a->data[0];
    for (size_t i = 1; i < a->len; i++) if (a->data[i] > m) m = a->data[i];
    return m;
}

int64_t mako_native_str_slice_contains_ptr(
    MakoNativeString *s, int64_t off, int64_t len, MakoNativeString *needle
) {
    return mako_str_slice_contains(
        bridge_borrow_str(s), off, len, bridge_borrow_str(needle));
}

int64_t mako_native_wait_group_new(void) {
    return (int64_t)(intptr_t)mako_wait_group_new();
}

void mako_native_wait_group_add(int64_t wg, int64_t delta) {
    mako_wait_group_add((MakoWaitGroup *)(intptr_t)wg, delta);
}

void mako_native_wait_group_done(int64_t wg) {
    mako_wait_group_done((MakoWaitGroup *)(intptr_t)wg);
}

void mako_native_wait_group_wait(int64_t wg) {
    mako_wait_group_wait((MakoWaitGroup *)(intptr_t)wg);
}

int64_t mako_native_atomic_new(int64_t init) {
    return (int64_t)(intptr_t)mako_atomic_new(init);
}

int64_t mako_native_atomic_add(int64_t p, int64_t delta) {
    return mako_atomic_add((MakoAtomicInt *)(intptr_t)p, delta);
}

int64_t mako_native_atomic_load(int64_t p) {
    return mako_atomic_load((MakoAtomicInt *)(intptr_t)p);
}

void mako_native_atomic_store(int64_t p, int64_t v) {
    mako_atomic_store((MakoAtomicInt *)(intptr_t)p, v);
}

int64_t mako_native_atomic_cas(int64_t p, int64_t oldv, int64_t newv) {
    return mako_atomic_cas((MakoAtomicInt *)(intptr_t)p, oldv, newv);
}

void mako_native_map_si_set_take_ptr(MakoNativeMapSI *m, MakoNativeString *key, int64_t val) {
    /* set clones key into map; consume caller's key. */
    mako_native_map_si_set_ptr(m, key, val);
    if (key) mako_native_string_drop_ptr(key);
}

int64_t mako_native_syscall_getpid(void) {
    return (int64_t)mako_syscall_getpid();
}

int64_t mako_native_path_size_ptr(MakoNativeString *p) {
    return (int64_t)mako_path_size(bridge_borrow_str(p));
}

MakoNativeString *mako_native_url_query_ptr(MakoNativeString *u) {
    return bridge_take_str(mako_url_query(bridge_borrow_str(u)));
}

int64_t mako_native_ring_len(int64_t id) {
    return (int64_t)mako_ring_len(id);
}

int64_t mako_native_signal_fired_ptr(MakoNativeString *name) {
    return (int64_t)mako_signal_fired(bridge_borrow_str(name));
}

int64_t mako_native_checked_mul(int64_t a, int64_t b) {
    return mako_checked_mul(a, b);
}

/* ---- corpus bridges batch 2 ---- */
MakoNativeIntSlice *mako_native_list_concat_int_ptr(MakoNativeIntSlice *a, MakoNativeIntSlice *b) {
    size_t na = a ? a->len : 0, nb = b ? b->len : 0;
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr(na + nb, na + nb);
    if (a && na) memcpy(out->data, a->data, na * sizeof(int64_t));
    if (b && nb) memcpy(out->data + na, b->data, nb * sizeof(int64_t));
    out->len = na + nb;
    return out;
}

MakoNativeString *mako_native_xor_bytes_ptr(MakoNativeString *a, MakoNativeString *b) {
    return bridge_take_str(mako_xor_bytes(bridge_borrow_str(a), bridge_borrow_str(b)));
}

int64_t mako_native_str_slice_index_ptr(
    MakoNativeString *s, int64_t off, int64_t len, MakoNativeString *needle
) {
    return mako_str_slice_index(bridge_borrow_str(s), off, len, bridge_borrow_str(needle));
}

typedef struct MakoNativeMapSS MakoNativeMapSS;
void mako_native_map_ss_set_ptr(MakoNativeMapSS *m, MakoNativeString *k, MakoNativeString *v);

void mako_native_map_ss_set_take_ptr(
    MakoNativeMapSS *m, MakoNativeString *k, MakoNativeString *v
) {
    mako_native_map_ss_set_ptr(m, k, v);
    if (k) mako_native_string_drop_ptr(k);
    if (v) mako_native_string_drop_ptr(v);
}

MakoNativeString *mako_native_url_query_escape_ptr(MakoNativeString *s) {
    return bridge_take_str(mako_url_query_escape(bridge_borrow_str(s)));
}

/* ---- auto bulk bridges (generated) ---- */

MakoNativeString *mako_native_aes_ctr_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_aes_ctr(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_aes_gcm_seal_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    return bridge_take_str(mako_aes_gcm_seal(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3)));
}

MakoNativeString *mako_native_auth_basic_header_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_auth_basic_header(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

int64_t mako_native_avro_long_len_ptr(MakoNativeString *a0) {
    return (int64_t)mako_avro_long_len(bridge_borrow_str(a0));
}

int64_t mako_native_binary_u32be_ptr(MakoNativeString *a0) {
    return (int64_t)mako_binary_u32be(bridge_borrow_str(a0));
}

int64_t mako_native_cache_put_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2) {
    return (int64_t)mako_cache_put(bridge_borrow_str(a0), bridge_borrow_str(a1), a2);
}

MakoNativeString *mako_native_cassandra_connect_url_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_cassandra_connect_url(bridge_borrow_str(a0)));
}

int64_t mako_native_chan_str_select2(int64_t a0, int64_t a1, int64_t a2) {
    /* Native string channels reuse MakoChan with string headers as i64 payloads. */
    return mako_chan_select2((MakoChan *)(intptr_t)a0, (MakoChan *)(intptr_t)a1, a2);
}

int64_t mako_native_csrf_check_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_csrf_check(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

MakoNativeStringSlice *mako_native_csv_split_line_ptr(MakoNativeString *a0) {
    MakoStrArray a = mako_csv_split_line(bridge_borrow_str(a0));
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(a.len, a.len ? a.len : 1);
    for (size_t i = 0; i < a.len; i++) out->data[i] = bridge_take_str(a.data[i]);
    out->len = a.len;
    free(a.data);
    return out;
}

MakoNativeString *mako_native_dap_threads_response_ptr(int64_t a0) {
    return bridge_take_str(mako_dap_threads_response(a0));
}

int64_t mako_native_dns_is_private_ptr(MakoNativeString *a0) {
    return (int64_t)mako_dns_is_private(bridge_borrow_str(a0));
}

int64_t mako_native_ecs_alive(int64_t a0, int64_t a1) {
    return (int64_t)mako_ecs_alive(a0, a1);
}

int64_t mako_native_file_mtime_ptr(MakoNativeString *a0) {
    return (int64_t)mako_file_mtime(bridge_borrow_str(a0));
}

MakoNativeStringSlice *mako_native_filepath_walk_ptr(MakoNativeString *a0) {
    MakoStrArray a = mako_filepath_walk(bridge_borrow_str(a0));
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(a.len, a.len ? a.len : 1);
    for (size_t i = 0; i < a.len; i++) out->data[i] = bridge_take_str(a.data[i]);
    out->len = a.len;
    free(a.data);
    return out;
}

int64_t mako_native_frame_cap(int64_t a0) {
    return (int64_t)mako_frame_cap(a0);
}

MakoNativeString *mako_native_fsm_transition_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_fsm_transition(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_fx_mul(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_fx_mul(a0, a1, a2);
}

int64_t mako_native_game_alpha(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_game_alpha(a0, a1, a2);
}

MakoNativeString *mako_native_game_udp_recv_ptr(int64_t a0) {
    return bridge_take_str(mako_game_udp_recv((MakoGameUDP *)(intptr_t)a0));
}

MakoNativeString *mako_native_gif_encode_rgb_ptr(int64_t a0, int64_t a1, MakoNativeString *a2) {
    return bridge_take_str(mako_gif_encode_rgb(a0, a1, bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_gpu_device_vendor_ptr(int64_t a0) {
    return bridge_take_str(mako_gpu_device_vendor(a0));
}

int64_t mako_native_gpu_silu_f32(int64_t a0, int64_t a1) {
    return (int64_t)mako_gpu_silu_f32(a0, a1);
}

int64_t mako_native_graphql_is_mutation_ptr(MakoNativeString *a0) {
    return (int64_t)mako_graphql_is_mutation(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_grpc_message_payload_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_grpc_message_payload(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_hpack_static_name_ptr(int64_t a0) {
    return bridge_take_str(mako_hpack_static_name(a0));
}

MakoNativeString *mako_native_http_post_timeout_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2) {
    return bridge_take_str(mako_http_post_timeout(bridge_borrow_str(a0), bridge_borrow_str(a1), a2));
}

int64_t mako_native_http_shutdown_ready(void) {
    return (int64_t)mako_http_shutdown_ready();
}

MakoNativeString *mako_native_http2_concat_frames_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_http2_concat_frames(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

MakoNativeString *mako_native_http2_frame_payload_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_http2_frame_payload(bridge_borrow_str(a0)));
}

int64_t mako_native_http2_is_ping_ptr(MakoNativeString *a0) {
    return (int64_t)mako_http2_is_ping(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_http2_settings_ack_ptr(void) {
    return bridge_take_str(mako_http2_settings_ack());
}

int64_t mako_native_http2_window_blocked(int64_t a0) {
    return (int64_t)mako_http2_window_blocked(a0);
}

int64_t mako_native_job_delay_ms_ptr(MakoNativeString *a0) {
    return (int64_t)mako_job_delay_ms(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_app7_length_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_app7_length(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_app8_length_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_app8_length(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_jpeg_decode_gray_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_jpeg_decode_gray(bridge_borrow_str(a0)));
}

int64_t mako_native_jpeg_has_app9_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_has_app9(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_is_mako_complete_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_is_mako_complete(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_jfif_minor_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_jfif_minor(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_jfif_thumb_width_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_jfif_thumb_width(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_jfif_x_density_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_jfif_x_density(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_sof0_matches_app7_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_sof0_matches_app7(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_sof0_width_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_sof0_width(bridge_borrow_str(a0));
}

int64_t mako_native_jwt_verify_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_jwt_verify(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

MakoNativeString *mako_native_lb_pick2_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_lb_pick2(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_leak_bytes_since(int64_t a0) {
    return (int64_t)mako_leak_bytes_since(a0);
}

int64_t mako_native_leak_scope_enter(void) {
    return (int64_t)mako_leak_scope_enter();
}

int64_t mako_native_list_eq_int(MakoNativeIntSlice *a0, MakoNativeIntSlice *a1) {
    return (int64_t)mako_list_eq_int(((MakoIntArray){.data = a0 ? a0->data : NULL, .len = a0 ? a0->len : 0, .cap = a0 ? a0->cap : 0}), ((MakoIntArray){.data = a1 ? a1->data : NULL, .len = a1 ? a1->len : 0, .cap = a1 ? a1->cap : 0}));
}

MakoNativeString *mako_native_llm_chat_body_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2) {
    return bridge_take_str(mako_llm_chat_body(bridge_borrow_str(a0), bridge_borrow_str(a1), a2));
}

MakoNativeString *mako_native_mail_simple_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    return bridge_take_str(mako_mail_simple(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3)));
}

int64_t mako_native_model_tensor_elems_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_model_tensor_elems(a0, bridge_borrow_str(a1));
}

MakoNativeString *mako_native_msgpack_int_hex_ptr(int64_t a0) {
    return bridge_take_str(mako_msgpack_int_hex(a0));
}

int64_t mako_native_msgpack_is_nil_ptr(MakoNativeString *a0) {
    return (int64_t)mako_msgpack_is_nil(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_multipart_file_name_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_multipart_file_name(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_pb_encode_field_varint_ptr(int64_t a0, int64_t a1) {
    return bridge_take_str(mako_pb_encode_field_varint(a0, a1));
}

MakoNativeString *mako_native_pb_encode_repeated_varint_ptr(int64_t a0, int64_t a1, int64_t a2) {
    return bridge_take_str(mako_pb_encode_repeated_varint(a0, a1, a2));
}

MakoNativeString *mako_native_pb_encode_sint_ptr(int64_t a0) {
    return bridge_take_str(mako_pb_encode_sint(a0));
}

int64_t mako_native_plugin_alive(int64_t a0) {
    return (int64_t)mako_plugin_alive(a0);
}

MakoNativeString *mako_native_plugin_api_version_ptr(void) {
    return bridge_take_str(mako_plugin_api_version());
}

MakoNativeString *mako_native_png_encode_gray_ptr(int64_t a0, int64_t a1, MakoNativeString *a2) {
    return bridge_take_str(mako_png_encode_gray(a0, a1, bridge_borrow_str(a2)));
}

int64_t mako_native_profile_sample_once_ptr(MakoNativeString *a0) {
    return (int64_t)mako_profile_sample_once(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_quic_initial_client_secret_hex_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_quic_initial_client_secret_hex(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_redis_get_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) {
    return bridge_take_str(mako_redis_get(bridge_borrow_str(a0), a1, bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_reflect_struct_field_name_ptr(MakoNativeString *a0, int64_t a1) {
    return bridge_take_str(mako_reflect_struct_field_name(bridge_borrow_str(a0), a1));
}

int64_t mako_native_reqctx_has_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_reqctx_has(a0, bridge_borrow_str(a1));
}

int64_t mako_native_ring_pop(int64_t a0) {
    return (int64_t)mako_ring_pop(a0);
}

int64_t mako_native_router_add_ptr(int64_t a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    return (int64_t)mako_router_add(a0, bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3));
}

MakoNativeString *mako_native_rpc_frame_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_rpc_frame(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

MakoNativeString *mako_native_sip_digest_response_ha1_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    return bridge_take_str(mako_sip_digest_response_ha1(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3)));
}

int64_t mako_native_sip_is_response_ptr(MakoNativeString *a0) {
    return (int64_t)mako_sip_is_response(bridge_borrow_str(a0));
}

int64_t mako_native_sock_error(int64_t a0) {
    return (int64_t)mako_sock_error(a0);
}

int64_t mako_native_str_at_eq_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) {
    return (int64_t)mako_str_at_eq(bridge_borrow_str(a0), a1, bridge_borrow_str(a2));
}

int64_t mako_native_syscall_getppid(void) {
    return (int64_t)mako_syscall_getppid();
}

int64_t mako_native_tcp_connect_timeout_ptr(MakoNativeString *a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_tcp_connect_timeout(bridge_borrow_str(a0), a1, a2);
}

int64_t mako_native_tcp_pool_open_count(int64_t a0) {
    return (int64_t)mako_tcp_pool_open_count(a0);
}

int64_t mako_native_tcp_set_timeout(int64_t a0, int64_t a1) {
    return (int64_t)mako_tcp_set_timeout(a0, a1);
}

int64_t mako_native_time_year(int64_t a0) {
    return (int64_t)mako_time_year(a0);
}

MakoNativeString *mako_native_tls_client_hello_random_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_tls_client_hello_random(bridge_borrow_str(a0)));
}

int64_t mako_native_tls_conn_fd(int64_t a0) {
    return (int64_t)mako_tls_conn_fd((void *)(intptr_t)a0);
}

MakoNativeString *mako_native_tls_post_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3, MakoNativeString *a4) {
    return bridge_take_str(mako_tls_post(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3), bridge_borrow_str(a4)));
}

MakoNativeString *mako_native_tls_record_appdata_seal_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_tls_record_appdata_seal(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_tls_server_free(int64_t a0) {
    return (int64_t)mako_tls_server_free((void *)(intptr_t)a0);
}

int64_t mako_native_tmpl_free(int64_t a0) {
    return (int64_t)mako_tmpl_free(a0);
}

int64_t mako_native_trace_begin_ptr(MakoNativeString *a0) {
    return (int64_t)mako_trace_begin(bridge_borrow_str(a0));
}

int64_t mako_native_udp_send_to_ptr(int64_t a0, MakoNativeString *a1, int64_t a2, MakoNativeString *a3) {
    return (int64_t)mako_udp_send_to(a0, bridge_borrow_str(a1), a2, bridge_borrow_str(a3));
}

int64_t mako_native_unix_socket_pair(void) {
    return (int64_t)mako_unix_socket_pair();
}

int64_t mako_native_utf8_utf_max(void) {
    return (int64_t)mako_utf8_utf_max();
}

int64_t mako_native_validate_int_range(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_validate_int_range(a0, a1, a2);
}

int64_t mako_native_ws_upgrade_request_ok_ptr(MakoNativeString *a0) {
    return (int64_t)mako_ws_upgrade_request_ok(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_yaml_pair_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_yaml_pair(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

/* round 1 */
MakoNativeString *mako_native_aes_gcm_open_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    return bridge_take_str(mako_aes_gcm_open(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3)));
}

int64_t mako_native_atomic_write_file_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_atomic_write_file(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

int64_t mako_native_auth_check_basic_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return (int64_t)mako_auth_check_basic(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2));
}

int64_t mako_native_avro_decode_bool_ptr(MakoNativeString *a0) {
    return (int64_t)mako_avro_decode_bool(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_binary_put_u64be_ptr(int64_t a0) {
    return bridge_take_str(mako_binary_put_u64be(a0));
}

int64_t mako_native_cache_has_ptr(MakoNativeString *a0) {
    return (int64_t)mako_cache_has(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_cassandra_select_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_cassandra_select(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_cbor_int_hex_ptr(int64_t a0) {
    return bridge_take_str(mako_cbor_int_hex(a0));
}

MakoNativeString *mako_native_dap_request_command_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_dap_request_command(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_dns_normalize_host_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_dns_normalize_host(bridge_borrow_str(a0)));
}

int64_t mako_native_ecs_add(int64_t a0, int64_t a1, int64_t a2, int64_t a3) {
    return (int64_t)mako_ecs_add(a0, a1, a2, a3);
}

MakoNativeStringSlice *mako_native_filepath_walk_n_ptr(MakoNativeString *a0, int64_t a1) {
    MakoStrArray a=mako_filepath_walk_n(bridge_borrow_str(a0), a1);
    MakoNativeStringSlice *out=mako_native_string_slice_make_ptr(a.len,a.len?a.len:1);
    for(size_t i=0;i<a.len;i++) out->data[i]=bridge_take_str(a.data[i]);
    out->len=a.len;
    free(a.data);
    return out;
}

int64_t mako_native_frame_used(int64_t a0) {
    return (int64_t)mako_frame_used(a0);
}

int64_t mako_native_fsm_is_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_fsm_is(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

int64_t mako_native_fx_div(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_fx_div(a0, a1, a2);
}

int64_t mako_native_game_frame_budget_ok(int64_t a0, int64_t a1) {
    return (int64_t)mako_game_frame_budget_ok(a0, a1);
}

MakoNativeString *mako_native_gif_encode_rgb_lzw_ptr(int64_t a0, int64_t a1, MakoNativeString *a2) {
    return bridge_take_str(mako_gif_encode_rgb_lzw(a0, a1, bridge_borrow_str(a2)));
}

int64_t mako_native_gif_width_ptr(MakoNativeString *a0) {
    return (int64_t)mako_gif_width(bridge_borrow_str(a0));
}

int64_t mako_native_gpu_device_is_gpu(int64_t a0) {
    return (int64_t)mako_gpu_device_is_gpu(a0);
}

int64_t mako_native_gpu_layernorm_f32(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, double a6) {
    return (int64_t)mako_gpu_layernorm_f32(a0, a1, a2, a3, a4, a5, a6);
}

MakoNativeString *mako_native_graphql_data_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_graphql_data(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

MakoNativeString *mako_native_grpc_unary_request_ptr(MakoNativeString *a0, int64_t a1) {
    return bridge_take_str(mako_grpc_unary_request(bridge_borrow_str(a0), a1));
}

MakoNativeString *mako_native_hpack_static_value_ptr(int64_t a0) {
    return bridge_take_str(mako_hpack_static_value(a0));
}

int64_t mako_native_http_shutdown_remaining(void) {
    return (int64_t)mako_http_shutdown_remaining();
}

MakoNativeString *mako_native_http2_frame_at_ptr(MakoNativeString *a0, int64_t a1) {
    return bridge_take_str(mako_http2_frame_at(bridge_borrow_str(a0), a1));
}

int64_t mako_native_http2_frame_stream_ptr(MakoNativeString *a0) {
    return (int64_t)mako_http2_frame_stream(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_http2_header_block_ptr(MakoNativeString *a0, int64_t a1) {
    return bridge_take_str(mako_http2_header_block(bridge_borrow_str(a0), a1));
}

int64_t mako_native_http2_is_settings_ack_ptr(MakoNativeString *a0) {
    return (int64_t)mako_http2_is_settings_ack(bridge_borrow_str(a0));
}

int64_t mako_native_http2_stream_apply_ptr(MakoNativeString *a0) {
    return (int64_t)mako_http2_stream_apply(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_https_get_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2) {
    return bridge_take_str(mako_https_get(bridge_borrow_str(a0), bridge_borrow_str(a1), a2));
}

int64_t mako_native_io_set_nonblocking(int64_t a0, int64_t a1) {
    return (int64_t)mako_io_set_nonblocking(a0, a1);
}

int64_t mako_native_job_cancel_ptr(MakoNativeString *a0) {
    return (int64_t)mako_job_cancel(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_app7_payload_len_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_app7_payload_len(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_app9_length_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_app9_length(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_is_mako_dct_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_is_mako_dct(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_jfif_thumb_height_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_jfif_thumb_height(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_jfif_y_density_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_jfif_y_density(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_sof0_height_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_sof0_height(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_sof0_sampling_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_sof0_sampling(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_width_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_width(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_lb_pick3_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    return bridge_take_str(mako_lb_pick3(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3)));
}

int64_t mako_native_leak_detected(int64_t a0) {
    return (int64_t)mako_leak_detected(a0);
}

int64_t mako_native_leak_scope_exit(void) {
    return (int64_t)mako_leak_scope_exit();
}

MakoNativeIntSlice *mako_native_list_fill_int_ptr(int64_t a0, int64_t a1) {
    MakoIntArray arr=mako_list_fill_int(a0, a1);
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t));
    out->len=arr.len;
    if(arr.cap) free(arr.data);
    return out;
}

MakoNativeString *mako_native_llm_body_with_tools_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_llm_body_with_tools(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

MakoNativeString *mako_native_mail_header_get_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_mail_header_get(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

int64_t mako_native_model_tensor_ndim_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_model_tensor_ndim(a0, bridge_borrow_str(a1));
}

MakoNativeString *mako_native_msgpack_encode_nil_ptr(void) {
    return bridge_take_str(mako_msgpack_encode_nil());
}

MakoNativeString *mako_native_multipart_file_content_type_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_multipart_file_content_type(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_pb_decode_sint_ptr(MakoNativeString *a0) {
    return (int64_t)mako_pb_decode_sint(bridge_borrow_str(a0));
}

int64_t mako_native_pb_repeated_count_ptr(MakoNativeString *a0, int64_t a1) {
    return (int64_t)mako_pb_repeated_count(bridge_borrow_str(a0), a1);
}

int64_t mako_native_plugin_count(void) {
    return (int64_t)mako_plugin_count();
}

MakoNativeString *mako_native_plugin_name_ptr(int64_t a0) {
    return bridge_take_str(mako_plugin_name(a0));
}

int64_t mako_native_png_width_ptr(MakoNativeString *a0) {
    return (int64_t)mako_png_width(bridge_borrow_str(a0));
}

int64_t mako_native_profile_sample_len(void) {
    return (int64_t)mako_profile_sample_len();
}

MakoNativeString *mako_native_quic_initial_client_key_hex_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_quic_initial_client_key_hex(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_redis_exists_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) {
    return bridge_take_str(mako_redis_exists(bridge_borrow_str(a0), a1, bridge_borrow_str(a2)));
}

int64_t mako_native_ring_push(int64_t a0, int64_t a1) {
    return (int64_t)mako_ring_push(a0, a1);
}

int64_t mako_native_router_group_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_router_group(a0, bridge_borrow_str(a1));
}

MakoNativeString *mako_native_rpc_method_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_rpc_method(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_sip_version_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_sip_version(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_sip_www_authenticate_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_sip_www_authenticate(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

int64_t mako_native_str_byte_at_ptr(MakoNativeString *a0, int64_t a1) {
    return (int64_t)mako_str_byte_at(bridge_borrow_str(a0), a1);
}

int64_t mako_native_syscall_getuid(void) {
    return (int64_t)mako_syscall_getuid();
}

int64_t mako_native_tcp_pool_acquire(int64_t a0) {
    return (int64_t)mako_tcp_pool_acquire(a0);
}

MakoNativeString *mako_native_tcp_read_ptr(int64_t a0) {
    return bridge_take_str(mako_tcp_read(a0));
}

int64_t mako_native_tcp_write_all_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_tcp_write_all(a0, bridge_borrow_str(a1));
}

int64_t mako_native_time_month(int64_t a0) {
    return (int64_t)mako_time_month(a0);
}

int64_t mako_native_tls_client_hello_has_aes128_gcm_ptr(MakoNativeString *a0) {
    return (int64_t)mako_tls_client_hello_has_aes128_gcm(bridge_borrow_str(a0));
}

int64_t mako_native_tls_record_type_ptr(MakoNativeString *a0) {
    return (int64_t)mako_tls_record_type(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_tmpl_html_execute_ptr(int64_t a0, int64_t a1) {
    return bridge_take_str(mako_tmpl_html_execute(a0, a1));
}

MakoNativeString *mako_native_trace_span_id_ptr(void) {
    return bridge_take_str(mako_trace_span_id());
}

MakoNativeString *mako_native_udp_recv_ptr(int64_t a0, int64_t a1) {
    return bridge_take_str(mako_udp_recv(a0, a1));
}

int64_t mako_native_unix_socket_pair_peer(void) {
    return (int64_t)mako_unix_socket_pair_peer();
}

int64_t mako_native_utf8_valid_ptr(MakoNativeString *a0) {
    return (int64_t)mako_utf8_valid(bridge_borrow_str(a0));
}

int64_t mako_native_validate_email_ptr(MakoNativeString *a0) {
    return (int64_t)mako_validate_email(bridge_borrow_str(a0));
}

int64_t mako_native_ws_client_accept_ok_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_ws_client_accept_ok(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

MakoNativeString *mako_native_yaml_pair_int_ptr(MakoNativeString *a0, int64_t a1) {
    return bridge_take_str(mako_yaml_pair_int(bridge_borrow_str(a0), a1));
}

/* round 2 */
int64_t mako_native_auth_role_has_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_auth_role_has(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

MakoNativeString *mako_native_avro_encode_bool_ptr(int64_t a0) {
    return bridge_take_str(mako_avro_encode_bool(a0));
}

MakoNativeString *mako_native_avro_long_hex_ptr(int64_t a0) {
    return bridge_take_str(mako_avro_long_hex(a0));
}

int64_t mako_native_binary_u64be_ptr(MakoNativeString *a0) {
    return (int64_t)mako_binary_u64be(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_cache_get_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_cache_get(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_clickhouse_connect_url_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_clickhouse_connect_url(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_dap_handle_request_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_dap_handle_request(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_dns_join_host_port_ptr(MakoNativeString *a0, int64_t a1) {
    return bridge_take_str(mako_dns_join_host_port(bridge_borrow_str(a0), a1));
}

int64_t mako_native_ecs_has(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_ecs_has(a0, a1, a2);
}

MakoNativeString *mako_native_ffi_abi_name_ptr(void) {
    return bridge_take_str(mako_ffi_abi_name());
}

int64_t mako_native_fsm_can_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return (int64_t)mako_fsm_can(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2));
}

MakoNativeString *mako_native_gif_decode_rgb_lzw_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_gif_decode_rgb_lzw(bridge_borrow_str(a0)));
}

int64_t mako_native_gif_height_ptr(MakoNativeString *a0) {
    return (int64_t)mako_gif_height(bridge_borrow_str(a0));
}

int64_t mako_native_gpu_attention_f32(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5) {
    return (int64_t)mako_gpu_attention_f32(a0, a1, a2, a3, a4, a5);
}

int64_t mako_native_gpu_buf_cap(int64_t a0) {
    return (int64_t)mako_gpu_buf_cap(a0);
}

MakoNativeString *mako_native_graphql_error_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_graphql_error(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_grpc_content_type_ptr(void) {
    return bridge_take_str(mako_grpc_content_type());
}

MakoNativeString *mako_native_hpack_literal_name_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_hpack_literal_name(bridge_borrow_str(a0)));
}

int64_t mako_native_http_shutdown_expired(void) {
    return (int64_t)mako_http_shutdown_expired();
}

int64_t mako_native_http2_conn_initial_window(void) {
    return (int64_t)mako_http2_conn_initial_window();
}

int64_t mako_native_http2_frame_len_ptr(MakoNativeString *a0) {
    return (int64_t)mako_http2_frame_len(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_http2_server_preface_ptr(void) {
    return bridge_take_str(mako_http2_server_preface());
}

int64_t mako_native_http2_window_of(int64_t a0) {
    return (int64_t)mako_http2_window_of(a0);
}

MakoNativeString *mako_native_https_last_header_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_https_last_header(bridge_borrow_str(a0)));
}

int64_t mako_native_io_read_ready(int64_t a0, int64_t a1) {
    return (int64_t)mako_io_read_ready(a0, a1);
}

int64_t mako_native_jpeg_app7_len_matches_payload_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_app7_len_matches_payload(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_jpeg_encode_gray_dct_ptr(int64_t a0, int64_t a1, MakoNativeString *a2) {
    return bridge_take_str(mako_jpeg_encode_gray_dct(a0, a1, bridge_borrow_str(a2)));
}

int64_t mako_native_jpeg_height_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_height(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_is_mako_huff_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_is_mako_huff(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_is_mako_jfif_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_is_mako_jfif(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_sof0_component_id_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_sof0_component_id(bridge_borrow_str(a0));
}

int64_t mako_native_leak_assert_clear(int64_t a0) {
    return (int64_t)mako_leak_assert_clear(a0);
}

int64_t mako_native_leak_assert_scope(void) {
    return (int64_t)mako_leak_assert_scope();
}

MakoNativeIntSlice *mako_native_list_range_int_ptr(int64_t a0, int64_t a1) {
    MakoIntArray arr=mako_list_range_int(a0, a1);
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t));
    out->len=arr.len;
    if(arr.cap) free(arr.data);
    return out;
}

MakoNativeString *mako_native_llm_finish_reason_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_llm_finish_reason(bridge_borrow_str(a0)));
}

int64_t mako_native_mail_msg_add_bcc_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_mail_msg_add_bcc(a0, bridge_borrow_str(a1));
}

int64_t mako_native_model_tensor_dim_ptr(int64_t a0, MakoNativeString *a1, int64_t a2) {
    return (int64_t)mako_model_tensor_dim(a0, bridge_borrow_str(a1), a2);
}

int64_t mako_native_msgpack_decode_bool_ptr(MakoNativeString *a0) {
    return (int64_t)mako_msgpack_decode_bool(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_multipart_file_value_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_multipart_file_value(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_pb_repeated_at_ptr(MakoNativeString *a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_pb_repeated_at(bridge_borrow_str(a0), a1, a2);
}

MakoNativeString *mako_native_plugin_version_ptr(int64_t a0) {
    return bridge_take_str(mako_plugin_version(a0));
}

int64_t mako_native_png_height_ptr(MakoNativeString *a0) {
    return (int64_t)mako_png_height(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_profile_samples_json_ptr(void) {
    return bridge_take_str(mako_profile_samples_json());
}

MakoNativeString *mako_native_quic_initial_client_iv_hex_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_quic_initial_client_iv_hex(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_redis_del_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) {
    return bridge_take_str(mako_redis_del(bridge_borrow_str(a0), a1, bridge_borrow_str(a2)));
}

int64_t mako_native_rename_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_rename(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

int64_t mako_native_ring_peek(int64_t a0) {
    return (int64_t)mako_ring_peek(a0);
}

MakoNativeString *mako_native_rpc_payload_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_rpc_payload(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_seal_at_rest_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_seal_at_rest(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_sip_method_is_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_sip_method_is(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

MakoNativeString *mako_native_sip_proxy_authenticate_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_sip_proxy_authenticate(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

int64_t mako_native_syscall_geteuid(void) {
    return (int64_t)mako_syscall_geteuid();
}

int64_t mako_native_tcp_pool_release(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_tcp_pool_release(a0, a1, a2);
}

MakoNativeString *mako_native_tcp_read_n_ptr(int64_t a0, int64_t a1) {
    return bridge_take_str(mako_tcp_read_n(a0, a1));
}

MakoNativeString *mako_native_template_execute_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_template_execute(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_time_day(int64_t a0) {
    return (int64_t)mako_time_day(a0);
}

int64_t mako_native_tls_record_version_ptr(MakoNativeString *a0) {
    return (int64_t)mako_tls_record_version(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_tls_server_hello_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_tls_server_hello(bridge_borrow_str(a0)));
}

int64_t mako_native_trace_end(void) {
    return (int64_t)mako_trace_end();
}

int64_t mako_native_udp_close(int64_t a0) {
    return (int64_t)mako_udp_close(a0);
}

int64_t mako_native_utf8_rune_len(int64_t a0) {
    return (int64_t)mako_utf8_rune_len(a0);
}

MakoNativeString *mako_native_yaml_pair_bool_ptr(MakoNativeString *a0, int64_t a1) {
    return bridge_take_str(mako_yaml_pair_bool(bridge_borrow_str(a0), a1));
}

/* round 3 */
int64_t mako_native_alloc_track_alloc(int64_t a0) {
    return (int64_t)mako_alloc_track_alloc(a0);
}

int64_t mako_native_authz_allow_role_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_authz_allow_role(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

MakoNativeString *mako_native_avro_encode_string_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_avro_encode_string(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_clickhouse_select_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_clickhouse_select(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_copy_file_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_copy_file(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

int64_t mako_native_dap_request_seq_ptr(MakoNativeString *a0) {
    return (int64_t)mako_dap_request_seq(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_dns_split_host_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_dns_split_host(bridge_borrow_str(a0)));
}

int64_t mako_native_ecs_get(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_ecs_get(a0, a1, a2);
}

MakoNativeString *mako_native_gif_decode_rgb_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_gif_decode_rgb(bridge_borrow_str(a0)));
}

int64_t mako_native_gpu_buf_len(int64_t a0) {
    return (int64_t)mako_gpu_buf_len(a0);
}

MakoNativeString *mako_native_graphql_request_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_graphql_request(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_grpc_status_trailer_ptr(int64_t a0) {
    return bridge_take_str(mako_grpc_status_trailer(a0));
}

MakoNativeString *mako_native_hpack_literal_value_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_hpack_literal_value(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_html_template_execute_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_html_template_execute(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_html_template_nested_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2, MakoNativeString *a3, MakoNativeString *a4) {
    return bridge_take_str(mako_html_template_nested(bridge_borrow_str(a0), bridge_borrow_str(a1), a2, bridge_borrow_str(a3), bridge_borrow_str(a4)));
}

MakoNativeString *mako_native_html_template_range_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_html_template_range(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_http_active_connections(void) {
    return (int64_t)mako_http_active_connections();
}

MakoNativeString *mako_native_http_compress_if_accepted_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_http_compress_if_accepted(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

int64_t mako_native_http2_conn_max_frame_size(void) {
    return (int64_t)mako_http2_conn_max_frame_size();
}

int64_t mako_native_http2_conn_preface_received(void) {
    return (int64_t)mako_http2_conn_preface_received();
}

int64_t mako_native_http2_frame_flags_ptr(MakoNativeString *a0) {
    return (int64_t)mako_http2_frame_flags(bridge_borrow_str(a0));
}

int64_t mako_native_http2_goaway_last_stream_ptr(MakoNativeString *a0) {
    return (int64_t)mako_http2_goaway_last_stream(bridge_borrow_str(a0));
}

int64_t mako_native_http2_window_consume(int64_t a0, int64_t a1) {
    return (int64_t)mako_http2_window_consume(a0, a1);
}

int64_t mako_native_io_write_ready(int64_t a0, int64_t a1) {
    return (int64_t)mako_io_write_ready(a0, a1);
}

int64_t mako_native_jpeg_has_app7_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_has_app7(bridge_borrow_str(a0));
}

int64_t mako_native_jpeg_jfif_app0_length_ptr(MakoNativeString *a0) {
    return (int64_t)mako_jpeg_jfif_app0_length(bridge_borrow_str(a0));
}

int64_t mako_native_lfq_new(int64_t a0) {
    return (int64_t)mako_lfq_new(a0);
}

int64_t mako_native_list_binary_search_int(MakoNativeIntSlice *a0, int64_t a1) {
    return (int64_t)mako_list_binary_search_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
}

int64_t mako_native_llm_usage_total_tokens_ptr(MakoNativeString *a0) {
    return (int64_t)mako_llm_usage_total_tokens(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_mail_msg_envelope_from_ptr(int64_t a0) {
    return bridge_take_str(mako_mail_msg_envelope_from(a0));
}

int64_t mako_native_model_load_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_model_load(a0, bridge_borrow_str(a1));
}

int64_t mako_native_model_load_gguf_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_model_load_gguf(a0, bridge_borrow_str(a1));
}

MakoNativeString *mako_native_msgpack_encode_bool_ptr(int64_t a0) {
    return bridge_take_str(mako_msgpack_encode_bool(a0));
}

int64_t mako_native_multipart_file_size_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return (int64_t)mako_multipart_file_size(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2));
}

MakoNativeString *mako_native_oidc_discovery_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2) {
    return bridge_take_str(mako_oidc_discovery(bridge_borrow_str(a0), bridge_borrow_str(a1), a2));
}

MakoNativeString *mako_native_open_at_rest_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_open_at_rest(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_plugin_kind_ptr(int64_t a0) {
    return bridge_take_str(mako_plugin_kind(a0));
}

int64_t mako_native_plugin_last_error(void) {
    return (int64_t)mako_plugin_last_error();
}

MakoNativeString *mako_native_png_decode_gray_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_png_decode_gray(bridge_borrow_str(a0)));
}

int64_t mako_native_profile_sample_start(int64_t a0) {
    return (int64_t)mako_profile_sample_start(a0);
}

MakoNativeString *mako_native_quic_initial_client_key_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_quic_initial_client_key(bridge_borrow_str(a0)));
}

int64_t mako_native_reflect_type_count(void) {
    return (int64_t)mako_reflect_type_count();
}

MakoNativeIntSlice *mako_native_set_union_int_ptr(MakoNativeIntSlice *a0, MakoNativeIntSlice *a1) {
    MakoIntArray arr=mako_set_union_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), ((MakoIntArray){.data=a1?a1->data:NULL,.len=a1?a1->len:0,.cap=a1?a1->cap:0}));
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t));
    out->len=arr.len;
    if(arr.cap) free(arr.data);
    return out;
}

MakoNativeString *mako_native_sg_gather2_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_sg_gather2(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

int64_t mako_native_sip_first_message_len_ptr(MakoNativeString *a0) {
    return (int64_t)mako_sip_first_message_len(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_sip_header_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_sip_header(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

int64_t mako_native_syscall_getgid(void) {
    return (int64_t)mako_syscall_getgid();
}

int64_t mako_native_tcp_pool_close(int64_t a0) {
    return (int64_t)mako_tcp_pool_close(a0);
}

int64_t mako_native_tcp_shutdown(int64_t a0, int64_t a1) {
    return (int64_t)mako_tcp_shutdown(a0, a1);
}

int64_t mako_native_time_hour(int64_t a0) {
    return (int64_t)mako_time_hour(a0);
}

int64_t mako_native_tls_record_len_ptr(MakoNativeString *a0) {
    return (int64_t)mako_tls_record_len(bridge_borrow_str(a0));
}

int64_t mako_native_tls_serve_once_h2_ptr(int64_t a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    return (int64_t)mako_tls_serve_once_h2(a0, bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3));
}

MakoNativeString *mako_native_tls_server_hello_random_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_tls_server_hello_random(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_trace_current_ptr(void) {
    return bridge_take_str(mako_trace_current());
}

int64_t mako_native_trace_export_otlp_pb_len(void) {
    return (int64_t)mako_trace_export_otlp_pb_len();
}

int64_t mako_native_udp_bind_addr_ptr(MakoNativeString *a0, int64_t a1) {
    return (int64_t)mako_udp_bind_addr(bridge_borrow_str(a0), a1);
}

int64_t mako_native_unix_write_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_unix_write(a0, bridge_borrow_str(a1));
}

int64_t mako_native_utf8_valid_rune(int64_t a0) {
    return (int64_t)mako_utf8_valid_rune(a0);
}

MakoNativeString *mako_native_yaml_merge_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return bridge_take_str(mako_yaml_merge(bridge_borrow_str(a0), bridge_borrow_str(a1)));
}

/* round 4 */
int64_t mako_native_auth_session_cookie_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return (int64_t)mako_auth_session_cookie(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2));
}

MakoNativeString *mako_native_avro_decode_string_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_avro_decode_string(bridge_borrow_str(a0)));
}

int64_t mako_native_dns_split_port_ptr(MakoNativeString *a0) {
    return (int64_t)mako_dns_split_port(bridge_borrow_str(a0));
}

int64_t mako_native_ecs_query_count(int64_t a0, int64_t a1) {
    return (int64_t)mako_ecs_query_count(a0, a1);
}

MakoNativeString *mako_native_elastic_connect_url_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_elastic_connect_url(bridge_borrow_str(a0)));
}

int64_t mako_native_gpu_buf_write_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_gpu_buf_write(a0, bridge_borrow_str(a1));
}

int64_t mako_native_grpc_status_code_ptr(MakoNativeString *a0) {
    return (int64_t)mako_grpc_status_code(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_hpack_huffman_encode_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_hpack_huffman_encode(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_html_template_with_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_html_template_with(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_http_content_encoding_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_http_content_encoding(bridge_borrow_str(a0)));
}

int64_t mako_native_http_shutdown_drain_conn(int64_t a0) {
    return (int64_t)mako_http_shutdown_drain_conn(a0);
}

int64_t mako_native_http2_conn_header_table_size(void) {
    return (int64_t)mako_http2_conn_header_table_size();
}

int64_t mako_native_http2_conn_settings_exchanged(void) {
    return (int64_t)mako_http2_conn_settings_exchanged();
}

int64_t mako_native_http2_goaway_error_ptr(MakoNativeString *a0) {
    return (int64_t)mako_http2_goaway_error(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_http2_window_update_frame_ptr(int64_t a0, int64_t a1) {
    return bridge_take_str(mako_http2_window_update_frame(a0, a1));
}

int64_t mako_native_install_graceful_shutdown(int64_t a0) {
    return (int64_t)mako_install_graceful_shutdown(a0);
}

int64_t mako_native_io_try_write_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_io_try_write(a0, bridge_borrow_str(a1));
}

MakoNativeString *mako_native_leak_report_json_ptr(int64_t a0) {
    return bridge_take_str(mako_leak_report_json(a0));
}

int64_t mako_native_lfq_try_push(int64_t a0, int64_t a1) {
    return (int64_t)mako_lfq_try_push(a0, a1);
}

MakoNativeIntSlice *mako_native_list_take_int_ptr(MakoNativeIntSlice *a0, int64_t a1) {
    MakoIntArray arr=mako_list_take_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t));
    out->len=arr.len;
    if(arr.cap) free(arr.data);
    return out;
}

int64_t mako_native_llm_tool_call_count_ptr(MakoNativeString *a0) {
    return (int64_t)mako_llm_tool_call_count(bridge_borrow_str(a0));
}

int64_t mako_native_mail_msg_rcpt_count(int64_t a0) {
    return (int64_t)mako_mail_msg_rcpt_count(a0);
}

int64_t mako_native_model_load_safetensors_ptr(int64_t a0, MakoNativeString *a1) {
    return (int64_t)mako_model_load_safetensors(a0, bridge_borrow_str(a1));
}

MakoNativeString *mako_native_msgpack_encode_string_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_msgpack_encode_string(bridge_borrow_str(a0)));
}

int64_t mako_native_multipart_file_allowed_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, int64_t a3, MakoNativeString *a4) {
    return (int64_t)mako_multipart_file_allowed(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), a3, bridge_borrow_str(a4));
}

MakoNativeString *mako_native_oidc_token_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, int64_t a3) {
    return bridge_take_str(mako_oidc_token(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), a3));
}

MakoNativeString *mako_native_plugin_last_error_str_ptr(void) {
    return bridge_take_str(mako_plugin_last_error_str());
}

int64_t mako_native_plugin_plugin_abi(int64_t a0) {
    return (int64_t)mako_plugin_plugin_abi(a0);
}

int64_t mako_native_profile_sample_stop(void) {
    return (int64_t)mako_profile_sample_stop();
}

MakoNativeString *mako_native_profile_samples_pprof_text_ptr(void) {
    return bridge_take_str(mako_profile_samples_pprof_text());
}

int64_t mako_native_quic_detect_ptr(MakoNativeString *a0) {
    return (int64_t)mako_quic_detect(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_quic_initial_client_iv_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_quic_initial_client_iv(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_reflect_struct_field_type_ptr(MakoNativeString *a0, int64_t a1) {
    return bridge_take_str(mako_reflect_struct_field_type(bridge_borrow_str(a0), a1));
}

MakoNativeString *mako_native_reflect_type_of_int_ptr(int64_t a0) {
    return bridge_take_str(mako_reflect_type_of_int(a0));
}

int64_t mako_native_rmdir_ptr(MakoNativeString *a0) {
    return (int64_t)mako_rmdir(bridge_borrow_str(a0));
}

MakoNativeString *mako_native_scram_gs2_header_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_scram_gs2_header(bridge_borrow_str(a0)));
}

int64_t mako_native_set_has_int(MakoNativeIntSlice *a0, int64_t a1) {
    return (int64_t)mako_set_has_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
}

int64_t mako_native_sip_header_count_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_sip_header_count(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

MakoNativeString *mako_native_sip_strip_via_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_sip_strip_via(bridge_borrow_str(a0)));
}

int64_t mako_native_syscall_getegid(void) {
    return (int64_t)mako_syscall_getegid();
}

int64_t mako_native_tcp_linger(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)mako_tcp_linger(a0, a1, a2);
}

int64_t mako_native_time_minute(int64_t a0) {
    return (int64_t)mako_time_minute(a0);
}

MakoNativeString *mako_native_tls_certificate_ptr(MakoNativeString *a0) {
    return bridge_take_str(mako_tls_certificate(bridge_borrow_str(a0)));
}

MakoNativeString *mako_native_tls_h2_settings_exchange_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) {
    return bridge_take_str(mako_tls_h2_settings_exchange(bridge_borrow_str(a0), a1, bridge_borrow_str(a2)));
}

MakoNativeString *mako_native_tls_record_appdata_open_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    return bridge_take_str(mako_tls_record_appdata_open(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
}

int64_t mako_native_tok_new(void) {
    return (int64_t)mako_tok_new();
}

MakoNativeString *mako_native_trace_export_otlp_pb_ptr(void) {
    return bridge_take_str(mako_trace_export_otlp_pb());
}

MakoNativeString *mako_native_udp_last_sender_host_ptr(void) {
    return bridge_take_str(mako_udp_last_sender_host());
}

MakoNativeString *mako_native_unix_read_ptr(int64_t a0, int64_t a1) {
    return bridge_take_str(mako_unix_read(a0, a1));
}

MakoNativeString *mako_native_utf8_encode_rune_ptr(int64_t a0) {
    return bridge_take_str(mako_utf8_encode_rune(a0));
}

MakoNativeStringSlice *mako_native_yaml_keys_ptr(MakoNativeString *a0) {
    MakoStrArray a=mako_yaml_keys(bridge_borrow_str(a0));
    MakoNativeStringSlice *out=mako_native_string_slice_make_ptr(a.len,a.len?a.len:1);
    for(size_t i=0;i<a.len;i++) out->data[i]=bridge_take_str(a.data[i]);
    out->len=a.len;
    free(a.data);
    return out;
}
int64_t mako_native_alloc_track_free(int64_t a0) {
    int64_t ret=(int64_t)mako_alloc_track_free(a0);
    return ret;
}

MakoNativeString *mako_native_auth_token_sign_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_auth_token_sign(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

MakoNativeString *mako_native_avro_encode_array_long_ptr(MakoNativeIntSlice *a0) {
    MakoNativeString *ret = bridge_take_str(mako_avro_encode_array_long(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0})));
    return ret;
}

MakoNativeString *mako_native_csv_join_row_ptr(MakoNativeStringSlice *a0) {
    MakoStrArray sa0; sa0.len = a0?a0->len:0; sa0.data = (MakoString*)calloc(sa0.len?sa0.len:1,sizeof(MakoString));
    for(size_t j=0;j<sa0.len;j++){ MakoNativeString *p=a0->data[j]; sa0.data[j]=bridge_borrow_str(p); }
    MakoNativeString *ret = bridge_take_str(mako_csv_join_row(sa0));
    free(sa0.data);
    return ret;
}

int64_t mako_native_dns_lookup_count_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_dns_lookup_count(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_ecs_query_first(int64_t a0, int64_t a1) {
    int64_t ret=(int64_t)mako_ecs_query_first(a0, a1);
    return ret;
}

MakoNativeString *mako_native_elastic_search_request_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_elastic_search_request(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

MakoNativeString *mako_native_gob_encode_strs_ptr(MakoNativeStringSlice *a0) {
    MakoStrArray sa0; sa0.len = a0?a0->len:0; sa0.data = (MakoString*)calloc(sa0.len?sa0.len:1,sizeof(MakoString));
    for(size_t j=0;j<sa0.len;j++){ MakoNativeString *p=a0->data[j]; sa0.data[j]=bridge_borrow_str(p); }
    MakoNativeString *ret = bridge_take_str(mako_gob_encode_strs(sa0));
    free(sa0.data);
    return ret;
}

MakoNativeString *mako_native_gpu_buf_read_ptr(int64_t a0, int64_t a1) {
    MakoNativeString *ret = bridge_take_str(mako_gpu_buf_read(a0, a1));
    return ret;
}

int64_t mako_native_grpc_default_max_message(void) {
    int64_t ret=(int64_t)mako_grpc_default_max_message();
    return ret;
}

int64_t mako_native_gzip_available(void) {
    int64_t ret=(int64_t)mako_gzip_available();
    return ret;
}

MakoNativeString *mako_native_hpack_huffman_decode_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_hpack_huffman_decode(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_http_shutdown_begin(int64_t a0) {
    int64_t ret=(int64_t)mako_http_shutdown_begin(a0);
    return ret;
}

int64_t mako_native_http2_conn_closing(void) {
    int64_t ret=(int64_t)mako_http2_conn_closing();
    return ret;
}

int64_t mako_native_http2_conn_enable_push(void) {
    int64_t ret=(int64_t)mako_http2_conn_enable_push();
    return ret;
}

MakoNativeString *mako_native_http2_ping_frame_ptr(MakoNativeString *a0, int64_t a1) {
    MakoNativeString *ret = bridge_take_str(mako_http2_ping_frame(bridge_borrow_str(a0), a1));
    return ret;
}

int64_t mako_native_http2_recv_window_of(int64_t a0) {
    int64_t ret=(int64_t)mako_http2_recv_window_of(a0);
    return ret;
}

int64_t mako_native_lfq_len(int64_t a0) {
    int64_t ret=(int64_t)mako_lfq_len(a0);
    return ret;
}

MakoNativeIntSlice *mako_native_list_drop_int_ptr(MakoNativeIntSlice *a0, int64_t a1) {
    MakoIntArray arr=mako_list_drop_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t));
    out->len=arr.len;
    if(arr.cap) free(arr.data);
    return out;
}

MakoNativeString *mako_native_llm_tool_call_name_ptr(MakoNativeString *a0, int64_t a1) {
    MakoNativeString *ret = bridge_take_str(mako_llm_tool_call_name(bridge_borrow_str(a0), a1));
    return ret;
}

MakoNativeString *mako_native_mail_msg_rcpt_at_ptr(int64_t a0, int64_t a1) {
    MakoNativeString *ret = bridge_take_str(mako_mail_msg_rcpt_at(a0, a1));
    return ret;
}

MakoNativeString *mako_native_msgpack_decode_string_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_msgpack_decode_string(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_otlp_export_traces_json_ptr(MakoNativeString *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_otlp_export_traces_json(bridge_borrow_str(a0), a1);
    return ret;
}

MakoNativeString *mako_native_plugin_path_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_plugin_path(a0));
    return ret;
}

int64_t mako_native_profile_sample_thread_count(void) {
    int64_t ret=(int64_t)mako_profile_sample_thread_count();
    return ret;
}

int64_t mako_native_profile_sample_wall_ns(void) {
    int64_t ret=(int64_t)mako_profile_sample_wall_ns();
    return ret;
}

MakoNativeString *mako_native_quic_initial_protect_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3) {
    MakoNativeString *ret = bridge_take_str(mako_quic_initial_protect(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3)));
    return ret;
}

int64_t mako_native_quic_long_header_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_quic_long_header(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_reflect_struct_has_field_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    int64_t ret=(int64_t)mako_reflect_struct_has_field(bridge_borrow_str(a0), bridge_borrow_str(a1));
    return ret;
}

MakoNativeString *mako_native_reflect_type_of_string_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_reflect_type_of_string(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_scram_cbind_b64_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_scram_cbind_b64(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

MakoNativeIntSlice *mako_native_set_intersect_int_ptr(MakoNativeIntSlice *a0, MakoNativeIntSlice *a1) {
    MakoIntArray arr=mako_set_intersect_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), ((MakoIntArray){.data=a1?a1->data:NULL,.len=a1?a1->len:0,.cap=a1?a1->cap:0}));
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t));
    out->len=arr.len;
    if(arr.cap) free(arr.data);
    return out;
}

int64_t mako_native_signal_on_term(void) {
    int64_t ret=(int64_t)mako_signal_on_term();
    return ret;
}

MakoNativeString *mako_native_sip_via_branch_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_sip_via_branch(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_syscall_hostname_ptr(void) {
    MakoNativeString *ret = bridge_take_str(mako_syscall_hostname());
    return ret;
}

MakoNativeString *mako_native_temp_file_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_temp_file(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_time_second(int64_t a0) {
    int64_t ret=(int64_t)mako_time_second(a0);
    return ret;
}

MakoNativeString *mako_native_tls_certificate_der_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_tls_certificate_der(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_tls_h2_get_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3) {
    MakoNativeString *ret = bridge_take_str(mako_tls_h2_get(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3)));
    return ret;
}

int64_t mako_native_tls_record_seq_reset(void) {
    int64_t ret=(int64_t)mako_tls_record_seq_reset();
    return ret;
}

int64_t mako_native_tok_set_ptr(int64_t a0, MakoNativeString *a1, int64_t a2) {
    int64_t ret=(int64_t)mako_tok_set(a0, bridge_borrow_str(a1), a2);
    return ret;
}

MakoNativeString *mako_native_udp_recv_from_ptr(int64_t a0, int64_t a1) {
    MakoNativeString *ret = bridge_take_str(mako_udp_recv_from(a0, a1));
    return ret;
}

int64_t mako_native_unix_close(int64_t a0) {
    int64_t ret=(int64_t)mako_unix_close(a0);
    return ret;
}

MakoNativeStringSlice *mako_native_yaml_get_list_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoStrArray a=mako_yaml_get_list(bridge_borrow_str(a0), bridge_borrow_str(a1));
    MakoNativeStringSlice *out=mako_native_string_slice_make_ptr(a.len,a.len?a.len:1);
    for(size_t i=0;i<a.len;i++) out->data[i]=bridge_take_str(a.data[i]);
    out->len=a.len;
    free(a.data);
    return out;
}
MakoNativeString *mako_native_auth_token_subject_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_auth_token_subject(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeIntSlice *mako_native_avro_decode_array_long_ptr(MakoNativeString *a0) {
    MakoIntArray arr=mako_avro_decode_array_long(bridge_borrow_str(a0));
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t)); out->len=arr.len; if(arr.cap) free(arr.data); return out;
}

MakoNativeString *mako_native_dns_lookup_all_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_dns_lookup_all(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_ecs_archetype(int64_t a0, int64_t a1) {
    int64_t ret=(int64_t)mako_ecs_archetype(a0, a1);
    return ret;
}

int64_t mako_native_gfx_poll(int64_t a0) {
    int64_t ret=(int64_t)mako_gfx_poll((MakoGfxWindow *)(intptr_t)a0);
    return ret;
}

MakoNativeStringSlice *mako_native_gob_decode_strs_ptr(MakoNativeString *a0) {
    MakoStrArray a=mako_gob_decode_strs(bridge_borrow_str(a0));
    MakoNativeStringSlice *out=mako_native_string_slice_make_ptr(a.len,a.len?a.len:1);
    for(size_t i=0;i<a.len;i++) out->data[i]=bridge_take_str(a.data[i]); out->len=a.len; free(a.data); return out;
}

MakoNativeString *mako_native_gob_encode_string_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_gob_encode_string(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_gpu_buf_free(int64_t a0) {
    int64_t ret=(int64_t)mako_gpu_buf_free(a0);
    return ret;
}

int64_t mako_native_grpc_message_within_limit_ptr(MakoNativeString *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_grpc_message_within_limit(bridge_borrow_str(a0), a1);
    return ret;
}

MakoNativeString *mako_native_gzip_decompress_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_gzip_decompress(bridge_borrow_str(a0)));
    return ret;
}

void mako_native_hpack_dyn_clear(void) {
    mako_hpack_dyn_clear();
}

int64_t mako_native_http_shutdown_deadline(void) {
    int64_t ret=(int64_t)mako_http_shutdown_deadline();
    return ret;
}

int64_t mako_native_http2_conn_settings_ack_needed(void) {
    int64_t ret=(int64_t)mako_http2_conn_settings_ack_needed();
    return ret;
}

int64_t mako_native_http2_conn_unacked(void) {
    int64_t ret=(int64_t)mako_http2_conn_unacked();
    return ret;
}

int64_t mako_native_http2_is_window_update_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_http2_is_window_update(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_http2_recv_window_conn(void) {
    int64_t ret=(int64_t)mako_http2_recv_window_conn();
    return ret;
}

int64_t mako_native_lfq_try_pop(int64_t a0) {
    int64_t ret=(int64_t)mako_lfq_try_pop(a0);
    return ret;
}

MakoNativeIntSlice *mako_native_list_zip_int_ptr(MakoNativeIntSlice *a0, MakoNativeIntSlice *a1) {
    MakoIntArray arr=mako_list_zip_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), ((MakoIntArray){.data=a1?a1->data:NULL,.len=a1?a1->len:0,.cap=a1?a1->cap:0}));
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t)); out->len=arr.len; if(arr.cap) free(arr.data); return out;
}

MakoNativeString *mako_native_llm_tool_call_args_ptr(MakoNativeString *a0, int64_t a1) {
    MakoNativeString *ret = bridge_take_str(mako_llm_tool_call_args(bridge_borrow_str(a0), a1));
    return ret;
}

MakoNativeString *mako_native_msgpack_encode_array_int_ptr(MakoNativeIntSlice *a0) {
    MakoNativeString *ret = bridge_take_str(mako_msgpack_encode_array_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0})));
    return ret;
}

int64_t mako_native_otlp_export_traces_pb_ptr(MakoNativeString *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_otlp_export_traces_pb(bridge_borrow_str(a0), a1);
    return ret;
}

int64_t mako_native_plugin_close(int64_t a0) {
    int64_t ret=(int64_t)mako_plugin_close(a0);
    return ret;
}

int64_t mako_native_plugin_log_count(void) {
    int64_t ret=(int64_t)mako_plugin_log_count();
    return ret;
}

int64_t mako_native_profile_sample_cpu_us(void) {
    int64_t ret=(int64_t)mako_profile_sample_cpu_us();
    return ret;
}

MakoNativeString *mako_native_quic_initial_unprotect_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3) {
    MakoNativeString *ret = bridge_take_str(mako_quic_initial_unprotect(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3)));
    return ret;
}

int64_t mako_native_quic_short_header_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_quic_short_header(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_realpath_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_realpath(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_reflect_kind_of_int_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_reflect_kind_of_int(a0));
    return ret;
}

MakoNativeString *mako_native_scram_client_final_without_proof_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_scram_client_final_without_proof(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

MakoNativeIntSlice *mako_native_set_diff_int_ptr(MakoNativeIntSlice *a0, MakoNativeIntSlice *a1) {
    MakoIntArray arr=mako_set_diff_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), ((MakoIntArray){.data=a1?a1->data:NULL,.len=a1?a1->len:0,.cap=a1?a1->cap:0}));
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t)); out->len=arr.len; if(arr.cap) free(arr.data); return out;
}

int64_t mako_native_shutdown_requested(void) {
    int64_t ret=(int64_t)mako_shutdown_requested();
    return ret;
}

MakoNativeString *mako_native_sip_addr_tag_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_sip_addr_tag(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_smtp_auth_plain_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_smtp_auth_plain(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

MakoNativeString *mako_native_syscall_uname_sysname_ptr(void) {
    MakoNativeString *ret = bridge_take_str(mako_syscall_uname_sysname());
    return ret;
}

int64_t mako_native_time_millisecond(int64_t a0) {
    int64_t ret=(int64_t)mako_time_millisecond(a0);
    return ret;
}

MakoNativeString *mako_native_tls_certificate_verify_ptr(int64_t a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_tls_certificate_verify(a0, bridge_borrow_str(a1)));
    return ret;
}

MakoNativeString *mako_native_tls_h2_post_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3, MakoNativeString *a4) {
    MakoNativeString *ret = bridge_take_str(mako_tls_h2_post(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3), bridge_borrow_str(a4)));
    return ret;
}

int64_t mako_native_tls_record_write_seq(void) {
    int64_t ret=(int64_t)mako_tls_record_write_seq();
    return ret;
}

int64_t mako_native_tok_id_ptr(int64_t a0, MakoNativeString *a1) {
    int64_t ret=(int64_t)mako_tok_id(a0, bridge_borrow_str(a1));
    return ret;
}

int64_t mako_native_toml_get_bool_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    int64_t ret=(int64_t)mako_toml_get_bool(bridge_borrow_str(a0), bridge_borrow_str(a1));
    return ret;
}

int64_t mako_native_udp_last_sender_port(void) {
    int64_t ret=(int64_t)mako_udp_last_sender_port();
    return ret;
}

int64_t mako_native_utf8_decode_size_ptr(MakoNativeString *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_utf8_decode_size(bridge_borrow_str(a0), a1);
    return ret;
}

MakoNativeString *mako_native_xml_escape_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_xml_escape(bridge_borrow_str(a0)));
    return ret;
}
int64_t mako_native_auth_token_check_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    int64_t ret=(int64_t)mako_auth_token_check(bridge_borrow_str(a0), bridge_borrow_str(a1));
    return ret;
}

int64_t mako_native_ecs_system_add(int64_t a0, int64_t a1, int64_t a2) {
    int64_t ret=(int64_t)mako_ecs_system_add(a0, a1, a2);
    return ret;
}

MakoNativeString *mako_native_gfx_backend_name_ptr(void) {
    MakoNativeString *ret = bridge_take_str(mako_gfx_backend_name());
    return ret;
}

MakoNativeString *mako_native_gob_decode_string_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_gob_decode_string(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_gpu_add_f32(int64_t a0, int64_t a1, int64_t a2) {
    int64_t ret=(int64_t)mako_gpu_add_f32(a0, a1, a2);
    return ret;
}

int64_t mako_native_graphql_is_query_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_graphql_is_query(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_grpc_http2_unary_response_status_ptr(int64_t a0, MakoNativeString *a1, int64_t a2, int64_t a3) {
    MakoNativeString *ret = bridge_take_str(mako_grpc_http2_unary_response_status(a0, bridge_borrow_str(a1), a2, a3));
    return ret;
}

MakoNativeIntSlice *mako_native_heap_push_int_ptr(MakoNativeIntSlice *a0, int64_t a1) {
    MakoIntArray arr=mako_heap_push_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t)); out->len=arr.len; if(arr.cap) free(arr.data); return out;
}

int64_t mako_native_hpack_dyn_insert_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    int64_t ret=(int64_t)mako_hpack_dyn_insert(bridge_borrow_str(a0), bridge_borrow_str(a1));
    return ret;
}

MakoNativeString *mako_native_html_escape_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_html_escape(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_http2_conn_auto_settings_ack_ptr(void) {
    MakoNativeString *ret = bridge_take_str(mako_http2_conn_auto_settings_ack());
    return ret;
}

MakoNativeString *mako_native_http2_data_frame_ptr(int64_t a0, MakoNativeString *a1, int64_t a2) {
    MakoNativeString *ret = bridge_take_str(mako_http2_data_frame(a0, bridge_borrow_str(a1), a2));
    return ret;
}

MakoNativeString *mako_native_http2_settings_max_concurrent_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_http2_settings_max_concurrent(a0));
    return ret;
}

int64_t mako_native_http2_window_update_increment_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_http2_window_update_increment(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_list_find_int(MakoNativeIntSlice *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_list_find_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
    return ret;
}

MakoNativeString *mako_native_llm_sse_data_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_llm_sse_data(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_lookup_host_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_lookup_host(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_metrics_export_otlp_json_ptr(void) {
    MakoNativeString *ret = bridge_take_str(mako_metrics_export_otlp_json());
    return ret;
}

MakoNativeIntSlice *mako_native_msgpack_decode_array_int_ptr(MakoNativeString *a0) {
    MakoIntArray arr=mako_msgpack_decode_array_int(bridge_borrow_str(a0));
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t)); out->len=arr.len; if(arr.cap) free(arr.data); return out;
}

MakoNativeString *mako_native_plugin_call_ptr(int64_t a0, MakoNativeString *a1, MakoNativeString *a2) {
    MakoNativeString *ret = bridge_take_str(mako_plugin_call(a0, bridge_borrow_str(a1), bridge_borrow_str(a2)));
    return ret;
}

MakoNativeString *mako_native_plugin_last_log_ptr(void) {
    MakoNativeString *ret = bridge_take_str(mako_plugin_last_log());
    return ret;
}

MakoNativeString *mako_native_profile_http_route_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_profile_http_route(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_quic_initial_client_hp_hex_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_quic_initial_client_hp_hex(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_quic_long_type_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_quic_long_type(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_reflect_value_string_int_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_reflect_value_string_int(a0));
    return ret;
}

MakoNativeString *mako_native_scram_tls_unique_cbind_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_scram_tls_unique_cbind((void *)(intptr_t)a0));
    return ret;
}

MakoNativeString *mako_native_sg_slice_ptr(MakoNativeString *a0, int64_t a1, int64_t a2) {
    MakoNativeString *ret = bridge_take_str(mako_sg_slice(bridge_borrow_str(a0), a1, a2));
    return ret;
}

int64_t mako_native_should_stop_accepting(void) {
    int64_t ret=(int64_t)mako_should_stop_accepting();
    return ret;
}

int64_t mako_native_sip_content_length_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_sip_content_length(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_smtp_send_soft_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) {
    int64_t ret=(int64_t)mako_smtp_send_soft(bridge_borrow_str(a0), a1, bridge_borrow_str(a2));
    return ret;
}

MakoNativeString *mako_native_syscall_uname_machine_ptr(void) {
    MakoNativeString *ret = bridge_take_str(mako_syscall_uname_machine());
    return ret;
}

MakoNativeString *mako_native_temp_dir_ptr(void) {
    MakoNativeString *ret = bridge_take_str(mako_temp_dir());
    return ret;
}

int64_t mako_native_time_weekday(int64_t a0) {
    int64_t ret=(int64_t)mako_time_weekday(a0);
    return ret;
}

int64_t mako_native_tls_certificate_verify_scheme_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_tls_certificate_verify_scheme(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_tls_record_read_seq(void) {
    int64_t ret=(int64_t)mako_tls_record_read_seq();
    return ret;
}

int64_t mako_native_tls_serve_h2_n_ptr(int64_t a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3, int64_t a4) {
    int64_t ret=(int64_t)mako_tls_serve_h2_n(a0, bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3), a4);
    return ret;
}

MakoNativeString *mako_native_tok_token_ptr(int64_t a0, int64_t a1) {
    MakoNativeString *ret = bridge_take_str(mako_tok_token(a0, a1));
    return ret;
}

double mako_native_toml_get_float_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return mako_toml_get_float(bridge_borrow_str(a0), bridge_borrow_str(a1));
}
/* kept for any stale callers that expected int64 bits of the old cast path */
int64_t mako_native_toml_get_float_bits_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    double d = mako_toml_get_float(bridge_borrow_str(a0), bridge_borrow_str(a1));
    int64_t ret;
    memcpy(&ret, &d, sizeof(ret));
    return ret;
}

MakoNativeString *mako_native_udp_last_sender_ptr(void) {
    MakoNativeString *ret = bridge_take_str(mako_udp_last_sender());
    return ret;
}

int64_t mako_native_utf8_full_rune_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_utf8_full_rune(bridge_borrow_str(a0));
    return ret;
}
MakoNativeString *mako_native_bytes_to_hex_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_bytes_to_hex(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_dns_lookup_ipv4_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_dns_lookup_ipv4(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_ecs_set(int64_t a0, int64_t a1, int64_t a2, int64_t a3) {
    int64_t ret=(int64_t)mako_ecs_set(a0, a1, a2, a3);
    return ret;
}

MakoNativeString *mako_native_gob_encode_int_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_gob_encode_int(a0));
    return ret;
}

int64_t mako_native_gpu_metal_ok(void) {
    int64_t ret=(int64_t)mako_gpu_metal_ok();
    return ret;
}

int64_t mako_native_gpu_mul_f32(int64_t a0, int64_t a1, int64_t a2) {
    int64_t ret=(int64_t)mako_gpu_mul_f32(a0, a1, a2);
    return ret;
}

MakoNativeString *mako_native_graphql_operation_name_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_graphql_operation_name(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_grpc_http2_stream_data_ptr(int64_t a0, MakoNativeString *a1, int64_t a2, int64_t a3) {
    MakoNativeString *ret = bridge_take_str(mako_grpc_http2_stream_data(a0, bridge_borrow_str(a1), a2, a3));
    return ret;
}

int64_t mako_native_heap_peek_int(MakoNativeIntSlice *a0) {
    int64_t ret=(int64_t)mako_heap_peek_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}));
    return ret;
}

int64_t mako_native_hpack_dyn_len(void) {
    int64_t ret=(int64_t)mako_hpack_dyn_len();
    return ret;
}

int64_t mako_native_http2_conn_max_concurrent(void) {
    int64_t ret=(int64_t)mako_http2_conn_max_concurrent();
    return ret;
}

int64_t mako_native_http2_conn_send_settings(void) {
    int64_t ret=(int64_t)mako_http2_conn_send_settings();
    return ret;
}

MakoNativeString *mako_native_http2_rst_stream_frame_ptr(int64_t a0, int64_t a1) {
    MakoNativeString *ret = bridge_take_str(mako_http2_rst_stream_frame(a0, a1));
    return ret;
}

int64_t mako_native_list_count_int(MakoNativeIntSlice *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_list_count_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
    return ret;
}

MakoNativeString *mako_native_llm_sse_delta_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_llm_sse_delta(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_plugin_call1_ptr(int64_t a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_plugin_call1(a0, bridge_borrow_str(a1)));
    return ret;
}

int64_t mako_native_plugin_close_all(void) {
    int64_t ret=(int64_t)mako_plugin_close_all();
    return ret;
}

MakoNativeString *mako_native_profile_snapshot_json_ptr(void) {
    MakoNativeString *ret = bridge_take_str(mako_profile_snapshot_json());
    return ret;
}

MakoNativeString *mako_native_quic_initial_hp_mask_hex_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_quic_initial_hp_mask_hex(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

int64_t mako_native_quic_is_retry_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_quic_is_retry(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_reflect_value_string_str_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_reflect_value_string_str(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_scram_plus_client_final_bare_ptr(int64_t a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_scram_plus_client_final_bare((void *)(intptr_t)a0, bridge_borrow_str(a1)));
    return ret;
}

int64_t mako_native_server_shutdown_begin(int64_t a0) {
    int64_t ret=(int64_t)mako_server_shutdown_begin(a0);
    return ret;
}

int64_t mako_native_sip_msg_complete_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_sip_msg_complete(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_smtp_send_dialog_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3, MakoNativeString *a4) {
    int64_t ret=(int64_t)mako_smtp_send_dialog(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3), bridge_borrow_str(a4));
    return ret;
}

int64_t mako_native_symlink_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    int64_t ret=(int64_t)mako_symlink(bridge_borrow_str(a0), bridge_borrow_str(a1));
    return ret;
}

MakoNativeString *mako_native_syscall_uname_json_ptr(void) {
    MakoNativeString *ret = bridge_take_str(mako_syscall_uname_json());
    return ret;
}

int64_t mako_native_time_yearday(int64_t a0) {
    int64_t ret=(int64_t)mako_time_yearday(a0);
    return ret;
}

MakoNativeString *mako_native_tls_certificate_verify_sig_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_tls_certificate_verify_sig(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_tls_h2_get_twice_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3) {
    MakoNativeString *ret = bridge_take_str(mako_tls_h2_get_twice(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3)));
    return ret;
}

MakoNativeString *mako_native_tls_record_appdata_seal_seq_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    MakoNativeString *ret = bridge_take_str(mako_tls_record_appdata_seal_seq(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
    return ret;
}

MakoNativeIntSlice *mako_native_tok_encode_ptr(int64_t a0, MakoNativeString *a1) {
    MakoIntArray arr=mako_tok_encode(a0, bridge_borrow_str(a1));
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t)); out->len=arr.len; if(arr.cap) free(arr.data); return out;
}

int64_t mako_native_utf8_rune_start(int64_t a0) {
    int64_t ret=(int64_t)mako_utf8_rune_start(a0);
    return ret;
}

MakoNativeString *mako_native_xml_tag_text_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_xml_tag_text(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}
MakoNativeString *mako_native_chacha20_poly1305_seal_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    MakoNativeString *ret = bridge_take_str(mako_chacha20_poly1305_seal(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3)));
    return ret;
}

MakoNativeString *mako_native_dns_lookup_ipv6_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_dns_lookup_ipv6(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_ecs_remove(int64_t a0, int64_t a1, int64_t a2) {
    int64_t ret=(int64_t)mako_ecs_remove(a0, a1, a2);
    return ret;
}

int64_t mako_native_gob_decode_int_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_gob_decode_int(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_gpu_cuda_ok(void) {
    int64_t ret=(int64_t)mako_gpu_cuda_ok();
    return ret;
}

int64_t mako_native_gpu_scale_f32(int64_t a0, int64_t a1, double a2) {
    int64_t ret=(int64_t)mako_gpu_scale_f32(a0, a1, a2);
    return ret;
}

int64_t mako_native_graphql_has_field_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    int64_t ret=(int64_t)mako_graphql_has_field(bridge_borrow_str(a0), bridge_borrow_str(a1));
    return ret;
}

int64_t mako_native_grpc_http2_stream_data_count_ptr(MakoNativeString *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_grpc_http2_stream_data_count(bridge_borrow_str(a0), a1);
    return ret;
}

MakoNativeIntSlice *mako_native_heap_pop_int_ptr(MakoNativeIntSlice *a0) {
    MakoIntArray arr=mako_heap_pop_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}));
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t)); out->len=arr.len; if(arr.cap) free(arr.data); return out;
}

MakoNativeString *mako_native_hex_to_bytes_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_hex_to_bytes(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_hot_reload_plugin_watch_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_hot_reload_plugin_watch(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_hpack_dyn_name_ptr(void) {
    MakoNativeString *ret = bridge_take_str(mako_hpack_dyn_name());
    return ret;
}

int64_t mako_native_http2_conn_goaway_last(void) {
    int64_t ret=(int64_t)mako_http2_conn_goaway_last();
    return ret;
}

int64_t mako_native_http2_is_rst_stream_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_http2_is_rst_stream(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_http2_stream_body_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_http2_stream_body(a0));
    return ret;
}

int64_t mako_native_list_any_eq_int(MakoNativeIntSlice *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_list_any_eq_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
    return ret;
}

MakoNativeString *mako_native_llm_stream_append_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_llm_stream_append(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

MakoNativeString *mako_native_plugin_info_json_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_plugin_info_json(a0));
    return ret;
}

int64_t mako_native_process_rss_bytes(void) {
    int64_t ret=(int64_t)mako_process_rss_bytes();
    return ret;
}

MakoNativeString *mako_native_quic_initial_hp_mask_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_quic_initial_hp_mask(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

int64_t mako_native_quic_is_version_negotiation_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_quic_is_version_negotiation(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_readlink_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_readlink(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_reflect_len_string_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_reflect_len_string(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_server_drain(int64_t a0) {
    int64_t ret=(int64_t)mako_server_drain(a0);
    return ret;
}

int64_t mako_native_sip_msg_needed_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_sip_msg_needed(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_smtp_send_auth_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3, MakoNativeString *a4, MakoNativeString *a5, MakoNativeString *a6) {
    int64_t ret=(int64_t)mako_smtp_send_auth(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3), bridge_borrow_str(a4), bridge_borrow_str(a5), bridge_borrow_str(a6));
    return ret;
}

int64_t mako_native_syscall_pagesize(void) {
    int64_t ret=(int64_t)mako_syscall_pagesize();
    return ret;
}

int64_t mako_native_time_parse_rfc3339_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_time_parse_rfc3339(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_tls_finished_verify_data_hex_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_tls_finished_verify_data_hex(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

MakoNativeString *mako_native_tls_h2_mux_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) {
    MakoNativeString *ret = bridge_take_str(mako_tls_h2_mux(bridge_borrow_str(a0), a1, bridge_borrow_str(a2)));
    return ret;
}

MakoNativeString *mako_native_tls_record_appdata_open_seq_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    MakoNativeString *ret = bridge_take_str(mako_tls_record_appdata_open_seq(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
    return ret;
}

MakoNativeString *mako_native_tok_decode_ptr(int64_t a0, MakoNativeIntSlice *a1) {
    MakoNativeString *ret = bridge_take_str(mako_tok_decode(a0, ((MakoIntArray){.data=a1?a1->data:NULL,.len=a1?a1->len:0,.cap=a1?a1->cap:0})));
    return ret;
}

int64_t mako_native_utf8_decode_last_rune_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_utf8_decode_last_rune(bridge_borrow_str(a0));
    return ret;
}
MakoNativeString *mako_native_cbor_encode_int_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_cbor_encode_int(a0));
    return ret;
}

MakoNativeString *mako_native_chacha20_poly1305_open_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    MakoNativeString *ret = bridge_take_str(mako_chacha20_poly1305_open(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3)));
    return ret;
}

int64_t mako_native_ecs_despawn(int64_t a0, int64_t a1) {
    int64_t ret=(int64_t)mako_ecs_despawn(a0, a1);
    return ret;
}

int64_t mako_native_gpu_fill_f32(int64_t a0, int64_t a1, double a2) {
    int64_t ret=(int64_t)mako_gpu_fill_f32(a0, a1, a2);
    return ret;
}

int64_t mako_native_gpu_vulkan_ok(void) {
    int64_t ret=(int64_t)mako_gpu_vulkan_ok();
    return ret;
}

MakoNativeString *mako_native_graphql_request_vars_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_graphql_request_vars(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

MakoNativeString *mako_native_grpc_http2_stream_two_ptr(int64_t a0, MakoNativeString *a1, int64_t a2, MakoNativeString *a3, int64_t a4) {
    MakoNativeString *ret = bridge_take_str(mako_grpc_http2_stream_two(a0, bridge_borrow_str(a1), a2, bridge_borrow_str(a3), a4));
    return ret;
}

int64_t mako_native_heap_popped_int(void) {
    int64_t ret=(int64_t)mako_heap_popped_int();
    return ret;
}

MakoNativeString *mako_native_hpack_dyn_name_at_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_hpack_dyn_name_at(a0));
    return ret;
}

int64_t mako_native_http2_conn_send_goaway(void) {
    int64_t ret=(int64_t)mako_http2_conn_send_goaway();
    return ret;
}

int64_t mako_native_http2_rst_stream_error_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_http2_rst_stream_error(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_http2_stream_body_done(int64_t a0) {
    int64_t ret=(int64_t)mako_http2_stream_body_done(a0);
    return ret;
}

MakoNativeString *mako_native_httptest_get_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_httptest_get(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_list_all_eq_int(MakoNativeIntSlice *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_list_all_eq_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
    return ret;
}

MakoNativeString *mako_native_llm_json_extract_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_llm_json_extract(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_plugin_find_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_plugin_find(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_process_cpu_user_us(void) {
    int64_t ret=(int64_t)mako_process_cpu_user_us();
    return ret;
}

int64_t mako_native_pwrite_ptr(int64_t a0, MakoNativeString *a1, int64_t a2) {
    int64_t ret=(int64_t)mako_pwrite(a0, bridge_borrow_str(a1), a2);
    return ret;
}

MakoNativeString *mako_native_quic_header_protect_apply_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) {
    MakoNativeString *ret = bridge_take_str(mako_quic_header_protect_apply(bridge_borrow_str(a0), a1, bridge_borrow_str(a2)));
    return ret;
}

int64_t mako_native_quic_version_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_quic_version(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_regex_valid_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_regex_valid(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_register_listener(int64_t a0) {
    int64_t ret=(int64_t)mako_register_listener(a0);
    return ret;
}

int64_t mako_native_sdp_ok_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_sdp_ok(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_smtp_set_timeout_ms(int64_t a0, int64_t a1) {
    int64_t ret=(int64_t)mako_smtp_set_timeout_ms(a0, a1);
    return ret;
}

int64_t mako_native_syscall_ncpu(void) {
    int64_t ret=(int64_t)mako_syscall_ncpu();
    return ret;
}

int64_t mako_native_time_parse_date_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_time_parse_date(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_tls_serve_grpc_once_ptr(int64_t a0, MakoNativeString *a1, MakoNativeString *a2) {
    int64_t ret=(int64_t)mako_tls_serve_grpc_once(a0, bridge_borrow_str(a1), bridge_borrow_str(a2));
    return ret;
}

int64_t mako_native_tls_transcript_reset(void) {
    int64_t ret=(int64_t)mako_tls_transcript_reset();
    return ret;
}

int64_t mako_native_tok_load_json_ptr(int64_t a0, MakoNativeString *a1) {
    int64_t ret=(int64_t)mako_tok_load_json(a0, bridge_borrow_str(a1));
    return ret;
}

int64_t mako_native_utf8_decode_last_size(void) {
    int64_t ret=(int64_t)mako_utf8_decode_last_size();
    return ret;
}
int64_t mako_native_cbor_decode_int_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_cbor_decode_int(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_close_listeners(void) {
    int64_t ret=(int64_t)mako_close_listeners();
    return ret;
}

int64_t mako_native_file_size(int64_t a0) {
    int64_t ret=(int64_t)mako_file_size(a0);
    return ret;
}

int64_t mako_native_gpu_f32_count(int64_t a0) {
    int64_t ret=(int64_t)mako_gpu_f32_count(a0);
    return ret;
}

MakoNativeString *mako_native_graphql_data2_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    MakoNativeString *ret = bridge_take_str(mako_graphql_data2(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3)));
    return ret;
}

MakoNativeString *mako_native_grpc_http2_client_stream_flow_ptr(int64_t a0, MakoNativeString *a1, int64_t a2, MakoNativeString *a3, int64_t a4, MakoNativeString *a5, int64_t a6, int64_t a7) {
    MakoNativeString *ret = bridge_take_str(mako_grpc_http2_client_stream_flow(a0, bridge_borrow_str(a1), a2, bridge_borrow_str(a3), a4, bridge_borrow_str(a5), a6, a7));
    return ret;
}

MakoNativeString *mako_native_hpack_dyn_value_at_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_hpack_dyn_value_at(a0));
    return ret;
}

MakoNativeString *mako_native_http2_conn_goaway_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_http2_conn_goaway(a0));
    return ret;
}

int64_t mako_native_http2_is_priority_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_http2_is_priority(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_http2_stream_state_of(int64_t a0) {
    int64_t ret=(int64_t)mako_http2_stream_state_of(a0);
    return ret;
}

int64_t mako_native_httptest_status(void) {
    int64_t ret=(int64_t)mako_httptest_status();
    return ret;
}

MakoNativeStringSlice *mako_native_list_concat_str_ptr(MakoNativeStringSlice *a0, MakoNativeStringSlice *a1) {
    /* Own independent string clones — do not share views into a0/a1. */
    size_t n0 = a0 ? a0->len : 0;
    size_t n1 = a1 ? a1->len : 0;
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(n0 + n1, n0 + n1 ? n0 + n1 : 1);
    if (!out) abort();
    for (size_t j = 0; j < n0; j++) {
        MakoString v = bridge_borrow_str(a0->data[j]);
        out->data[j] = bridge_take_str(mako_str_clone(v));
    }
    for (size_t j = 0; j < n1; j++) {
        MakoString v = bridge_borrow_str(a1->data[j]);
        out->data[n0 + j] = bridge_take_str(mako_str_clone(v));
    }
    out->len = n0 + n1;
    return out;
}

MakoNativeIntSlice *mako_native_list_take_while_lt_int_ptr(MakoNativeIntSlice *a0, int64_t a1) {
    MakoIntArray arr=mako_list_take_while_lt_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t)); out->len=arr.len; if(arr.cap) free(arr.data); return out;
}

int64_t mako_native_llm_estimate_tokens_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_llm_estimate_tokens(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_path_file_size_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_path_file_size(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_plugin_reload(int64_t a0) {
    int64_t ret=(int64_t)mako_plugin_reload(a0);
    return ret;
}

int64_t mako_native_quic_dcid_len_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_quic_dcid_len(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_quic_header_protect_remove_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) {
    MakoNativeString *ret = bridge_take_str(mako_quic_header_protect_remove(bridge_borrow_str(a0), a1, bridge_borrow_str(a2)));
    return ret;
}

MakoNativeString *mako_native_regex_quote_meta_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_regex_quote_meta(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_sdp_version_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_sdp_version(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_seal_file_at_rest_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    int64_t ret=(int64_t)mako_seal_file_at_rest(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3));
    return ret;
}

int64_t mako_native_smtp_starttls_available(void) {
    int64_t ret=(int64_t)mako_smtp_starttls_available();
    return ret;
}

MakoNativeString *mako_native_snap_diff_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret = bridge_take_str(mako_snap_diff(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

MakoNativeString *mako_native_stack_trace_ptr(void) {
    MakoNativeString *ret = bridge_take_str(mako_stack_trace());
    return ret;
}

int64_t mako_native_syscall_getrlimit_nofile(void) {
    int64_t ret=(int64_t)mako_syscall_getrlimit_nofile();
    return ret;
}

MakoNativeString *mako_native_time_format_date_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_time_format_date(a0));
    return ret;
}

MakoNativeString *mako_native_tls_grpc_unary_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3, int64_t a4, MakoNativeString *a5) {
    MakoNativeString *ret = bridge_take_str(mako_tls_grpc_unary(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3), a4, bridge_borrow_str(a5)));
    return ret;
}

int64_t mako_native_tls_transcript_len(void) {
    int64_t ret=(int64_t)mako_tls_transcript_len();
    return ret;
}

int64_t mako_native_tok_size(int64_t a0) {
    int64_t ret=(int64_t)mako_tok_size(a0);
    return ret;
}

int64_t mako_native_unicode_is_letter(int64_t a0) {
    int64_t ret=(int64_t)mako_unicode_is_letter(a0);
    return ret;
}
int64_t mako_native_cbor_type_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_cbor_type(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_crash_report_install_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_crash_report_install(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_gpu_opencl_ok(void) {
    int64_t ret=(int64_t)mako_gpu_opencl_ok();
    return ret;
}

MakoNativeString *mako_native_http2_priority_frame_ptr(int64_t a0, int64_t a1, int64_t a2, int64_t a3) {
    MakoNativeString *ret = bridge_take_str(mako_http2_priority_frame(a0, a1, a2, a3));
    return ret;
}

int64_t mako_native_http2_stream_half_closed_remote(int64_t a0) {
    int64_t ret=(int64_t)mako_http2_stream_half_closed_remote(a0);
    return ret;
}

void mako_native_http2_stream_reset(void) {
    mako_http2_stream_reset();
}

MakoNativeString *mako_native_httptest_header_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_httptest_header(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeIntSlice *mako_native_list_map_add_int_ptr(MakoNativeIntSlice *a0, int64_t a1) {
    MakoIntArray arr=mako_list_map_add_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t)); out->len=arr.len; if(arr.cap) free(arr.data); return out;
}

int64_t mako_native_llm_retry_delay_ms(int64_t a0, int64_t a1, int64_t a2) {
    int64_t ret=(int64_t)mako_llm_retry_delay_ms(a0, a1, a2);
    return ret;
}

MakoNativeString *mako_native_open_file_at_rest_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    MakoNativeString *ret = bridge_take_str(mako_open_file_at_rest(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
    return ret;
}

int64_t mako_native_pem_count_blocks_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_pem_count_blocks(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_plugin_manifest_artifact_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_plugin_manifest_artifact(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_pread_ptr(int64_t a0, int64_t a1, int64_t a2) {
    MakoNativeString *ret = bridge_take_str(mako_pread(a0, a1, a2));
    return ret;
}

MakoNativeString *mako_native_quic_dcid_ptr(MakoNativeString *a0) {
    MakoNativeString *ret = bridge_take_str(mako_quic_dcid(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_quic_initial_packet_protect_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, int64_t a3, MakoNativeString *a4) {
    MakoNativeString *ret = bridge_take_str(mako_quic_initial_packet_protect(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), a3, bridge_borrow_str(a4)));
    return ret;
}

int64_t mako_native_sdp_media_count_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_sdp_media_count(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_snap_get_ptr(MakoNativeString *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_snap_get(bridge_borrow_str(a0), a1);
    return ret;
}

int64_t mako_native_syscall_pipe(void) {
    int64_t ret=(int64_t)mako_syscall_pipe();
    return ret;
}

MakoNativeString *mako_native_time_format_clock_ptr(int64_t a0) {
    MakoNativeString *ret = bridge_take_str(mako_time_format_clock(a0));
    return ret;
}

int64_t mako_native_time_offset_named_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_time_offset_named(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_tls_serve_grpc_stream_ptr(int64_t a0, MakoNativeString *a1, MakoNativeString *a2) {
    int64_t ret=(int64_t)mako_tls_serve_grpc_stream(a0, bridge_borrow_str(a1), bridge_borrow_str(a2));
    return ret;
}

int64_t mako_native_tls_transcript_append_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_tls_transcript_append(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_tok_free(int64_t a0) {
    int64_t ret=(int64_t)mako_tok_free(a0);
    return ret;
}

int64_t mako_native_unicode_is_digit(int64_t a0) {
    int64_t ret=(int64_t)mako_unicode_is_digit(a0);
    return ret;
}
int64_t mako_native_cbor_is_null_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_cbor_is_null(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_fsync(int64_t a0) {
    int64_t ret=(int64_t)mako_fsync(a0);
    return ret;
}

MakoNativeIntSlice *mako_native_list_map_mul_int_ptr(MakoNativeIntSlice *a0, int64_t a1) {
    MakoIntArray arr=mako_list_map_mul_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
    MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
    if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t)); out->len=arr.len; if(arr.cap) free(arr.data); return out;
}

int64_t mako_native_llm_should_retry_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_llm_should_retry(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_pem_has_block_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    int64_t ret=(int64_t)mako_pem_has_block(bridge_borrow_str(a0), bridge_borrow_str(a1));
    return ret;
}

MakoNativeString *mako_native_plugin_manifest_lib_path_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_plugin_manifest_lib_path(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_quic_scid_len_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_quic_scid_len(bridge_borrow_str(a0));
    return ret;
}

MakoNativeStringSlice *mako_native_regex_find_all_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2) {
    MakoStrArray a=mako_regex_find_all(bridge_borrow_str(a0), bridge_borrow_str(a1), a2);
    MakoNativeStringSlice *out=mako_native_string_slice_make_ptr(a.len,a.len?a.len:1);
    for(size_t i=0;i<a.len;i++) out->data[i]=bridge_take_str(a.data[i]); out->len=a.len; free(a.data); return out;
}

MakoNativeString *mako_native_sdp_media_type_ptr(MakoNativeString *a0, int64_t a1) {
    MakoNativeString *ret=bridge_take_str(mako_sdp_media_type(bridge_borrow_str(a0), a1));
    return ret;
}

MakoNativeString *mako_native_snap_apply_delta_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret=bridge_take_str(mako_snap_apply_delta(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

int64_t mako_native_syscall_pipe_read_fd(void) {
    int64_t ret=(int64_t)mako_syscall_pipe_read_fd();
    return ret;
}

MakoNativeString *mako_native_time_format_local_ptr(int64_t a0) {
    MakoNativeString *ret=bridge_take_str(mako_time_format_local(a0));
    return ret;
}

MakoNativeString *mako_native_time_format_offset_ptr(int64_t a0, int64_t a1) {
    MakoNativeString *ret=bridge_take_str(mako_time_format_offset(a0, a1));
    return ret;
}

MakoNativeString *mako_native_tls_transcript_finished_hex_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_tls_transcript_finished_hex(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_unicode_is_space(int64_t a0) {
    int64_t ret=(int64_t)mako_unicode_is_space(a0);
    return ret;
}

int64_t mako_native_http2_priority_dep_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_http2_priority_dep(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_http2_stream_apply_local_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_http2_stream_apply_local(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_gpu_matmul_f32(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5) {
    int64_t ret=(int64_t)mako_gpu_matmul_f32(a0, a1, a2, a3, a4, a5);
    return ret;
}

int64_t mako_native_gpu_mha_f32(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    int64_t ret=(int64_t)mako_gpu_mha_f32(a0, a1, a2, a3, a4, a5, a6);
    return ret;
}

/* ---- residual handles ---- */
int64_t mako_native_watch_new(void) {
    return (int64_t)(intptr_t)mako_watch_new();
}

int64_t mako_native_zip_create(void) {
    return (int64_t)(intptr_t)mako_zip_create();
}

int64_t mako_native_buf_reader_from_string_ptr(MakoNativeString *s) {
    return (int64_t)(intptr_t)mako_buf_reader_from_string(bridge_borrow_str(s));
}

int64_t mako_native_limits_new(int64_t a0, int64_t a1, int64_t a2) {
    return (int64_t)(intptr_t)mako_limits_new(a0, a1, a2);
}

int64_t mako_native_http_header_ok_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    return (int64_t)mako_http_header_pair_ok(bridge_borrow_str(a0), bridge_borrow_str(a1));
}

MakoNativeString *mako_native_mime_type_ptr(MakoNativeString *path) {
    return bridge_take_str(mako_mime_type_by_ext(bridge_borrow_str(path)));
}

int64_t mako_native_watch_add_ptr(int64_t wp, MakoNativeString *path) {
    return mako_watch_add((void *)(intptr_t)wp, bridge_borrow_str(path));
}

int64_t mako_native_zip_add_ptr(int64_t z, MakoNativeString *name, MakoNativeString *data) {
    return mako_zip_add((MakoZipWriter *)(intptr_t)z, bridge_borrow_str(name), bridge_borrow_str(data));
}

MakoNativeString *mako_native_buf_read_line_ptr(int64_t r) {
    return bridge_take_str(mako_buf_read_line((MakoBufReader *)(intptr_t)r));
}

int64_t mako_native_limits_try_mem(int64_t L, int64_t n) {
    return mako_limits_try_mem((MakoLimits *)(intptr_t)L, n);
}

int64_t mako_native_tar_write_file_ptr(MakoNativeString *tar, MakoNativeString *name, MakoNativeString *data) {
    return mako_tar_write_file(bridge_borrow_str(tar), bridge_borrow_str(name), bridge_borrow_str(data));
}
MakoNativeString *mako_native_watch_poll_ptr(int64_t a0, int64_t a1) {
    MakoNativeString *ret=bridge_take_str(mako_watch_poll((void*)(intptr_t)a0, a1));
    return ret;
}

int64_t mako_native_watch_close(int64_t a0) {
    int64_t ret=(int64_t)mako_watch_close((void*)(intptr_t)a0);
    return ret;
}

int64_t mako_native_buf_reader_close(int64_t a0) {
    int64_t ret=(int64_t)mako_buf_reader_close((MakoBufReader*)(intptr_t)a0);
    return ret;
}

MakoNativeString *mako_native_buf_read_ptr(int64_t a0, int64_t a1) {
    MakoNativeString *ret=bridge_take_str(mako_buf_read((MakoBufReader*)(intptr_t)a0, a1));
    return ret;
}

int64_t mako_native_buf_flush(int64_t a0) {
    int64_t ret=(int64_t)mako_buf_flush((MakoBufWriter*)(intptr_t)a0);
    return ret;
}

int64_t mako_native_buf_write_ptr(int64_t a0, MakoNativeString *a1) {
    int64_t ret=(int64_t)mako_buf_write((MakoBufWriter*)(intptr_t)a0, bridge_borrow_str(a1));
    return ret;
}

int64_t mako_native_buf_write_byte(int64_t a0, int64_t a1) {
    int64_t ret=(int64_t)mako_buf_write_byte((MakoBufWriter*)(intptr_t)a0, a1);
    return ret;
}

int64_t mako_native_buf_writer_close(int64_t a0) {
    int64_t ret=(int64_t)mako_buf_writer_close((MakoBufWriter*)(intptr_t)a0);
    return ret;
}

int64_t mako_native_zip_write_to_ptr(int64_t a0, MakoNativeString *a1) {
    int64_t ret=(int64_t)mako_zip_write_to((MakoZipWriter*)(intptr_t)a0, bridge_borrow_str(a1));
    return ret;
}

void mako_native_zip_close(int64_t a0) {
    mako_zip_close((MakoZipWriter*)(intptr_t)a0);
}

MakoNativeStringSlice *mako_native_zip_list_ptr(MakoNativeString *a0) {
    MakoStrArray a=mako_zip_list(bridge_borrow_str(a0));
 MakoNativeStringSlice *out=mako_native_string_slice_make_ptr(a.len,a.len?a.len:1);
 for(size_t i=0;i<a.len;i++) out->data[i]=bridge_take_str(a.data[i]); out->len=a.len; free(a.data); return out;
}

int64_t mako_native_limits_mem_used(int64_t a0) {
    int64_t ret=(int64_t)mako_limits_mem_used((MakoLimits*)(intptr_t)a0);
    return ret;
}

int64_t mako_native_limits_check_time(int64_t a0) {
    int64_t ret=(int64_t)mako_limits_check_time((MakoLimits*)(intptr_t)a0);
    return ret;
}

int64_t mako_native_limits_free(int64_t a0) {
    int64_t ret=(int64_t)mako_limits_free((MakoLimits*)(intptr_t)a0);
    return ret;
}

int64_t mako_native_limits_open_conns(int64_t a0) {
    int64_t ret=(int64_t)mako_limits_open_conns((MakoLimits*)(intptr_t)a0);
    return ret;
}

int64_t mako_native_limits_release_conn(int64_t a0) {
    int64_t ret=(int64_t)mako_limits_release_conn((MakoLimits*)(intptr_t)a0);
    return ret;
}

int64_t mako_native_limits_release_mem(int64_t a0, int64_t a1) {
    int64_t ret=(int64_t)mako_limits_release_mem((MakoLimits*)(intptr_t)a0, a1);
    return ret;
}

int64_t mako_native_limits_try_conn(int64_t a0) {
    int64_t ret=(int64_t)mako_limits_try_conn((MakoLimits*)(intptr_t)a0);
    return ret;
}

MakoNativeString *mako_native_tar_first_name_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_tar_first_name(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_base32_encode_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_base32_encode(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_gzip_compress_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_gzip_compress(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_rand_intn(int64_t a0) {
    int64_t ret=(int64_t)mako_rand_intn(a0);
    return ret;
}

void mako_native_rand_seed(int64_t a0) {
    mako_rand_seed(a0);
}

int64_t mako_native_bytes_buffer_len(int64_t a0) {
    int64_t ret=(int64_t)mako_bytes_buffer_len((MakoBytesBuffer*)(intptr_t)a0);
    return ret;
}

void mako_native_bytes_buffer_reset(int64_t a0) {
    mako_bytes_buffer_reset((MakoBytesBuffer*)(intptr_t)a0);
}

MakoNativeString *mako_native_bytes_buffer_string_ptr(int64_t a0) {
    MakoNativeString *ret=bridge_take_str(mako_bytes_buffer_string((MakoBytesBuffer*)(intptr_t)a0));
    return ret;
}

void mako_native_bytes_buffer_write_ptr(int64_t a0, MakoNativeString *a1) {
    mako_bytes_buffer_write((MakoBytesBuffer*)(intptr_t)a0, bridge_borrow_str(a1));
}

int64_t mako_native_context_expired(int64_t a0) {
    int64_t ret=(int64_t)mako_context_expired(a0);
    return ret;
}

int64_t mako_native_flag_int_ptr(MakoNativeString *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_flag_int(bridge_borrow_str(a0), a1);
    return ret;
}

MakoNativeString *mako_native_flag_string_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret=bridge_take_str(mako_flag_string(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

int64_t mako_native_session_cancelled_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_session_cancelled(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_session_cancel_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_session_cancel(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_session_cancel_clear_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_session_cancel_clear(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_session_cancel_token_ptr(void) {
    MakoNativeString *ret=bridge_take_str(mako_session_cancel_token());
    return ret;
}

int64_t mako_native_binary_u16le_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_binary_u16le(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_binary_u32le_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_binary_u32le(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_binary_u64le_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_binary_u64le(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_binary_put_u16le_ptr(int64_t a0) {
    MakoNativeString *ret=bridge_take_str(mako_binary_put_u16le(a0));
    return ret;
}

MakoNativeString *mako_native_binary_put_u32le_ptr(int64_t a0) {
    MakoNativeString *ret=bridge_take_str(mako_binary_put_u32le(a0));
    return ret;
}

MakoNativeString *mako_native_binary_put_u64le_ptr(int64_t a0) {
    MakoNativeString *ret=bridge_take_str(mako_binary_put_u64le(a0));
    return ret;
}

int64_t mako_native_jpeg_dct_dc_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_jpeg_dct_dc(bridge_borrow_str(a0));
    return ret;
}

/* Native MapSS layout (must match native_runtime.c). */
#ifndef MAKO_NMAP_FULL
enum { MAKO_NMAP_EMPTY_B = 0, MAKO_NMAP_FULL_B = 1, MAKO_NMAP_TOMB_B = 2 };
#endif
typedef struct MakoNativeMapSS {
    uint8_t *state;
    MakoNativeString **keys;
    MakoNativeString **vals;
    size_t cap;
    size_t len;
} MakoNativeMapSS;

MakoNativeString *mako_native_gob_encode_map_ss_ptr(MakoNativeMapSS *nm) {
    /* Convert native MapSS → C MakoMapSS then encode (layouts differ). */
    MakoMapSS *cm = mako_map_ss_make(nm ? (int64_t)nm->len : 0);
    if (nm && nm->state && nm->keys && nm->vals) {
        for (size_t i = 0; i < nm->cap; i++) {
            if (nm->state[i] != MAKO_NMAP_FULL_B) continue;
            MakoNativeString *k = nm->keys[i];
            MakoNativeString *v = nm->vals[i];
            if (!k || !v) continue;
            mako_map_ss_set(cm, bridge_borrow_str(k), bridge_borrow_str(v));
        }
    }
    MakoString enc = mako_gob_encode_map_ss(cm);
    mako_map_ss_free(cm);
    return bridge_take_str(enc);
}

MakoNativeString *mako_native_html_template_if_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2, MakoNativeString *a3) {
    MakoNativeString *ret=bridge_take_str(mako_html_template_if(bridge_borrow_str(a0), bridge_borrow_str(a1), a2, bridge_borrow_str(a3)));
    return ret;
}

MakoNativeString *mako_native_html_template_execute2_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3, MakoNativeString *a4) {
    MakoNativeString *ret=bridge_take_str(mako_html_template_execute2(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3), bridge_borrow_str(a4)));
    return ret;
}

MakoNativeString *mako_native_smtp_format_message_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3) {
    MakoNativeString *ret=bridge_take_str(mako_smtp_format_message(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3)));
    return ret;
}
MakoNativeString *mako_native_cbor_encode_null_ptr(void) {
    MakoNativeString *ret=bridge_take_str(mako_cbor_encode_null());
    return ret;
}

int64_t mako_native_file_truncate(int64_t a0, int64_t a1) {
    int64_t ret=(int64_t)mako_file_truncate(a0, a1);
    return ret;
}

int64_t mako_native_gpu_bias_add_f32(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4) {
    int64_t ret=(int64_t)mako_gpu_bias_add_f32(a0, a1, a2, a3, a4);
    return ret;
}

int64_t mako_native_http2_conn_header_assembling(void) {
    int64_t ret=(int64_t)mako_http2_conn_header_assembling();
    return ret;
}

int64_t mako_native_http2_priority_weight_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_http2_priority_weight(bridge_borrow_str(a0));
    return ret;
}

MakoNativeIntSlice *mako_native_list_filter_lt_int_ptr(MakoNativeIntSlice *a0, int64_t a1) {
    MakoIntArray arr=mako_list_filter_lt_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
 MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
 if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t)); out->len=arr.len; if(arr.cap) free(arr.data); return out;
}

MakoNativeString *mako_native_llm_body_force_stream_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_llm_body_force_stream(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_model_tensor_buf_ptr(int64_t a0, MakoNativeString *a1) {
    int64_t ret=(int64_t)mako_model_tensor_buf(a0, bridge_borrow_str(a1));
    return ret;
}

int64_t mako_native_netcode_lag_comp_tick(int64_t a0, int64_t a1, int64_t a2) {
    int64_t ret=(int64_t)mako_netcode_lag_comp_tick(a0, a1, a2);
    return ret;
}

MakoNativeString *mako_native_pem_extract_block_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret=bridge_take_str(mako_pem_extract_block(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

MakoNativeString *mako_native_quic_initial_packet_unprotect_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2, int64_t a3) {
    MakoNativeString *ret=bridge_take_str(mako_quic_initial_packet_unprotect(bridge_borrow_str(a0), bridge_borrow_str(a1), a2, a3));
    return ret;
}

MakoNativeString *mako_native_quic_scid_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_quic_scid(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_regex_replace_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    MakoNativeString *ret=bridge_take_str(mako_regex_replace(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
    return ret;
}

MakoNativeString *mako_native_sdp_media_proto_ptr(MakoNativeString *a0, int64_t a1) {
    MakoNativeString *ret=bridge_take_str(mako_sdp_media_proto(bridge_borrow_str(a0), a1));
    return ret;
}

int64_t mako_native_syscall_pipe_write_fd(void) {
    int64_t ret=(int64_t)mako_syscall_pipe_write_fd();
    return ret;
}

int64_t mako_native_time_add_ms(int64_t a0, int64_t a1) {
    int64_t ret=(int64_t)mako_time_add_ms(a0, a1);
    return ret;
}

int64_t mako_native_time_in_offset(int64_t a0, int64_t a1) {
    int64_t ret=(int64_t)mako_time_in_offset(a0, a1);
    return ret;
}

MakoNativeString *mako_native_tls_client_handshake_traffic_secret_hex_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret=bridge_take_str(mako_tls_client_handshake_traffic_secret_hex(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

MakoNativeString *mako_native_tls_grpc_stream_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3, int64_t a4, MakoNativeString *a5, int64_t a6, MakoNativeString *a7) {
    MakoNativeString *ret=bridge_take_str(mako_tls_grpc_stream(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3), a4, bridge_borrow_str(a5), a6, bridge_borrow_str(a7)));
    return ret;
}

int64_t mako_native_unicode_is_punct(int64_t a0) {
    int64_t ret=(int64_t)mako_unicode_is_punct(a0);
    return ret;
}
int64_t mako_native_cbor_decode_bool_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_cbor_decode_bool(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_duration_hours(int64_t a0) {
    int64_t ret=(int64_t)mako_duration_hours(a0);
    return ret;
}

int64_t mako_native_gpu_saxpy_f32(int64_t a0, int64_t a1, int64_t a2, double a3) {
    int64_t ret=(int64_t)mako_gpu_saxpy_f32(a0, a1, a2, a3);
    return ret;
}

int64_t mako_native_http2_conn_active_streams(void) {
    int64_t ret=(int64_t)mako_http2_conn_active_streams();
    return ret;
}

int64_t mako_native_http2_priority_exclusive_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_http2_priority_exclusive(bridge_borrow_str(a0));
    return ret;
}

MakoNativeIntSlice *mako_native_list_filter_gt_int_ptr(MakoNativeIntSlice *a0, int64_t a1) {
    MakoIntArray arr=mako_list_filter_gt_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
 MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
 if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t)); out->len=arr.len; if(arr.cap) free(arr.data); return out;
}

MakoNativeString *mako_native_llm_embed_body_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret=bridge_take_str(mako_llm_embed_body(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

int64_t mako_native_netcode_interp(int64_t a0, int64_t a1, int64_t a2) {
    int64_t ret=(int64_t)mako_netcode_interp(a0, a1, a2);
    return ret;
}

MakoNativeString *mako_native_pem_load_file_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_pem_load_file(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_quic_crypto_payload_ptr(MakoNativeString *a0, int64_t a1, int64_t a2) {
    MakoNativeString *ret=bridge_take_str(mako_quic_crypto_payload(bridge_borrow_str(a0), a1, a2));
    return ret;
}

int64_t mako_native_quic_payload_offset_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_quic_payload_offset(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_regex_replace_all_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    MakoNativeString *ret=bridge_take_str(mako_regex_replace_all(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
    return ret;
}

MakoNativeString *mako_native_sdp_attr_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret=bridge_take_str(mako_sdp_attr(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

int64_t mako_native_syscall_write_ptr(int64_t a0, MakoNativeString *a1) {
    int64_t ret=(int64_t)mako_syscall_write(a0, bridge_borrow_str(a1));
    return ret;
}

int64_t mako_native_tls_serve_h2_wu_ptr(int64_t a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3, int64_t a4) {
    int64_t ret=(int64_t)mako_tls_serve_h2_wu(a0, bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3), a4);
    return ret;
}

MakoNativeString *mako_native_tls_server_handshake_traffic_secret_hex_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret=bridge_take_str(mako_tls_server_handshake_traffic_secret_hex(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

int64_t mako_native_tok_load_bpe_ptr(int64_t a0, MakoNativeString *a1, MakoNativeString *a2) {
    int64_t ret=(int64_t)mako_tok_load_bpe(a0, bridge_borrow_str(a1), bridge_borrow_str(a2));
    return ret;
}

int64_t mako_native_unicode_is_symbol(int64_t a0) {
    int64_t ret=(int64_t)mako_unicode_is_symbol(a0);
    return ret;
}
MakoNativeString *mako_native_cbor_encode_bool_ptr(int64_t a0) {
    MakoNativeString *ret=bridge_take_str(mako_cbor_encode_bool(a0));
    return ret;
}

double mako_native_gpu_sum_f32(int64_t a0) {
    double ret = mako_gpu_sum_f32(a0);
    return ret;
}

int64_t mako_native_gpu_softmax_rows_f32(
    int64_t out_h, int64_t a_h, int64_t rows, int64_t cols
) {
    return mako_gpu_softmax_rows_f32(out_h, a_h, rows, cols);
}

int64_t mako_native_hot_reload_watch_count(void) {
    int64_t ret=(int64_t)mako_hot_reload_watch_count();
    return ret;
}

int64_t mako_native_http2_priority_apply_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_http2_priority_apply(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_list_fold_add_int(MakoNativeIntSlice *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_list_fold_add_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
    return ret;
}

int64_t mako_native_llm_embedding_dim_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_llm_embedding_dim(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_metrics_export_prom_ptr(void) {
    MakoNativeString *ret=bridge_take_str(mako_metrics_export_prom());
    return ret;
}

MakoNativeString *mako_native_quic_payload_crypto_data_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_quic_payload_crypto_data(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_quic_spin_bit_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_quic_spin_bit(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_sip_reason_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_sip_reason(bridge_borrow_str(a0)));
    return ret;
}

MakoNativeString *mako_native_syscall_read_ptr(int64_t a0, int64_t a1) {
    MakoNativeString *ret=bridge_take_str(mako_syscall_read(a0, a1));
    return ret;
}

int64_t mako_native_time_sub_ms(int64_t a0, int64_t a1) {
    int64_t ret=(int64_t)mako_time_sub_ms(a0, a1);
    return ret;
}

MakoNativeString *mako_native_tls_derive_secret_hex_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) {
    MakoNativeString *ret=bridge_take_str(mako_tls_derive_secret_hex(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)));
    return ret;
}

MakoNativeString *mako_native_tls_h2_window_get_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3) {
    MakoNativeString *ret=bridge_take_str(mako_tls_h2_window_get(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3)));
    return ret;
}

int64_t mako_native_tok_merge_count(int64_t a0) {
    int64_t ret=(int64_t)mako_tok_merge_count(a0);
    return ret;
}

int64_t mako_native_unicode_is_control(int64_t a0) {
    int64_t ret=(int64_t)mako_unicode_is_control(a0);
    return ret;
}
MakoNativeString *mako_native_cbor_encode_string_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_cbor_encode_string(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_duration_days(int64_t a0) {
    int64_t ret=(int64_t)mako_duration_days(a0);
    return ret;
}

int64_t mako_native_hot_reload_stamp(void) {
    int64_t ret=(int64_t)mako_hot_reload_stamp();
    return ret;
}

int64_t mako_native_http2_stream_priority_dep(int64_t a0) {
    int64_t ret=(int64_t)mako_http2_stream_priority_dep(a0);
    return ret;
}

int64_t mako_native_list_fold_mul_int(MakoNativeIntSlice *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_list_fold_mul_int(((MakoIntArray){.data=a0?a0->data:NULL,.len=a0?a0->len:0,.cap=a0?a0->cap:0}), a1);
    return ret;
}

MakoNativeString *mako_native_llm_embedding_json_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_llm_embedding_json(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_quic_key_phase_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_quic_key_phase(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_quic_payload_crypto_data_len_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_quic_payload_crypto_data_len(bridge_borrow_str(a0));
    return ret;
}

MakoNativeString *mako_native_sip_uri_scheme_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_sip_uri_scheme(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_syscall_close(int64_t a0) {
    int64_t ret=(int64_t)mako_syscall_close(a0);
    return ret;
}

MakoNativeString *mako_native_tls_client_application_traffic_secret_hex_ptr(MakoNativeString *a0, MakoNativeString *a1) {
    MakoNativeString *ret=bridge_take_str(mako_tls_client_application_traffic_secret_hex(bridge_borrow_str(a0), bridge_borrow_str(a1)));
    return ret;
}

MakoNativeIntSlice *mako_native_tok_encode_bpe_ptr(int64_t a0, MakoNativeString *a1) {
    MakoIntArray arr=mako_tok_encode_bpe(a0, bridge_borrow_str(a1));
 MakoNativeIntSlice *out=mako_native_int_slice_make_ptr(arr.len,arr.len?arr.len:1);
 if(arr.data&&arr.len) memcpy(out->data,arr.data,arr.len*sizeof(int64_t)); out->len=arr.len; if(arr.cap) free(arr.data); return out;
}

MakoNativeString *mako_native_trace_export_json_ptr(void) {
    MakoNativeString *ret=bridge_take_str(mako_trace_export_json());
    return ret;
}

int64_t mako_native_unicode_is_print(int64_t a0) {
    int64_t ret=(int64_t)mako_unicode_is_print(a0);
    return ret;
}
MakoNativeString *mako_native_cbor_decode_string_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_cbor_decode_string(bridge_borrow_str(a0)));
    return ret;
}


/* ---- POD-as-opaque wrappers ---- */
#include "mako_security.h"

int64_t mako_native_secret_from_str_ptr(MakoNativeString *s) {
    MakoSecret sec = mako_secret_from_str(bridge_borrow_str(s));
    MakoSecret *box = (MakoSecret *)malloc(sizeof(MakoSecret));
    if (!box) abort();
    *box = sec;
    return (int64_t)(intptr_t)box;
}

int64_t mako_native_secret_len(int64_t h) {
    if (!h) return 0;
    return mako_secret_len(*(MakoSecret *)(intptr_t)h);
}

int64_t mako_native_secret_eq_str_ptr(int64_t h, MakoNativeString *s) {
    if (!h) return 0;
    return mako_secret_eq_str(*(MakoSecret *)(intptr_t)h, bridge_borrow_str(s));
}

void mako_native_secret_drop(int64_t h) {
    if (!h) return;
    MakoSecret *box = (MakoSecret *)(intptr_t)h;
    mako_secret_drop(box);
    free(box);
}

int64_t mako_native_reflect_value_new_ptr(MakoNativeString *schema) {
    return (int64_t)(intptr_t)mako_reflect_value_new(bridge_borrow_str(schema));
}

MakoNativeString *mako_native_reflect_value_get_ptr(int64_t v, MakoNativeString *field) {
    return bridge_take_str(mako_reflect_value_get((MakoReflectValue *)(intptr_t)v, bridge_borrow_str(field)));
}

int64_t mako_native_reflect_value_set_ptr(int64_t v, MakoNativeString *field, MakoNativeString *val) {
    return mako_reflect_value_set((MakoReflectValue *)(intptr_t)v, bridge_borrow_str(field), bridge_borrow_str(val));
}

int64_t mako_native_reflect_value_num_fields(int64_t v) {
    return mako_reflect_value_num_fields((MakoReflectValue *)(intptr_t)v);
}

/* Sql via meta-key registry → full MakoSqlDB. */
int64_t mako_native_sql_begin(int64_t key) {
    return mako_sql_begin(mako_native_sql_db_from_key(key));
}

int64_t mako_native_sql_last_insert_id(int64_t key) {
    return mako_sql_last_insert_id(mako_native_sql_db_from_key(key));
}

int64_t mako_native_sql_query_int_ptr(int64_t key, MakoNativeString *sql, MakoNativeIntSlice *args) {
    MakoIntArray a = mako_int_array_empty();
    if (args && args->data && args->len) a = mako_int_array_view(args->data, args->len);
    return mako_sql_query_int(mako_native_sql_db_from_key(key), bridge_borrow_str(sql), a);
}


/* ch.recv_timeout(ms) helper: returns pack [ok, value] where ok: 1=got,0=timeout,-1=closed */
int64_t mako_native_chan_recv_timeout(void *c, int64_t timeout_ms, int64_t *out) {
    return mako_chan_recv_timeout((MakoChan *)c, out, timeout_ms);
}

int64_t mako_native_chan_try_recv(void *c) {
    int64_t v = 0;
    if (!mako_chan_try_recv((MakoChan *)c, &v)) return 0;
    return v;
}

/* ---- climb batch: remaining high-yield ---- */
MakoNativeString *mako_native_hmac_sha1_hex_ptr(MakoNativeString *k, MakoNativeString *m) {
    return bridge_take_str(mako_hmac_sha1_hex(bridge_borrow_str(k), bridge_borrow_str(m)));
}
MakoNativeString *mako_native_hmac_sha1_raw_ptr(MakoNativeString *k, MakoNativeString *m) {
    return bridge_take_str(mako_hmac_sha1_raw(bridge_borrow_str(k), bridge_borrow_str(m)));
}
int64_t mako_native_unicode_is_upper(int64_t r) { return mako_unicode_is_upper(r); }
int64_t mako_native_time_before(int64_t a, int64_t b) { return mako_time_before(a, b); }
int64_t mako_native_syscall_errno(void) { return mako_syscall_errno(); }
int64_t mako_native_ws_accept(int64_t fd) { return mako_ws_accept(fd); }

static MakoNativeString *mako_native_queue_popped_str_tls = NULL;

MakoNativeStringSlice *mako_native_queue_pop_str_ptr(MakoNativeStringSlice *a) {
    /* Pop front (queue), store value in TLS for queue_popped_str. */
    if (mako_native_queue_popped_str_tls) {
        mako_native_string_drop_ptr(mako_native_queue_popped_str_tls);
        mako_native_queue_popped_str_tls = NULL;
    }
    if (!a || a->len == 0) {
        mako_native_queue_popped_str_tls = mako_native_string_literal_ptr("", 0);
        return a ? a : mako_native_string_slice_make_ptr(0, 0);
    }
    mako_native_queue_popped_str_tls = mako_native_string_clone_ptr(a->data[0]);
    size_t n = a->len - 1;
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(n, n);
    for (size_t i = 0; i < n; i++) out->data[i] = mako_native_string_clone_ptr(a->data[i + 1]);
    out->len = n;
    return out;
}

MakoNativeString *mako_native_queue_popped_str(void) {
    if (!mako_native_queue_popped_str_tls)
        return mako_native_string_literal_ptr("", 0);
    /* Caller takes ownership of a clone so repeated reads stay valid. */
    return mako_native_string_clone_ptr(mako_native_queue_popped_str_tls);
}

int64_t mako_native_sql_commit(int64_t key) {
    return mako_sql_commit(mako_native_sql_db_from_key(key));
}
int64_t mako_native_sql_exec_ptr(int64_t key, MakoNativeString *sql, MakoNativeIntSlice *args) {
    MakoIntArray a = mako_int_array_empty();
    if (args && args->data && args->len) a = mako_int_array_view(args->data, args->len);
    return mako_sql_exec(mako_native_sql_db_from_key(key), bridge_borrow_str(sql), a);
}
int64_t mako_native_sql_rows_affected(int64_t key) {
    return mako_sql_rows_affected(mako_native_sql_db_from_key(key));
}
int64_t mako_native_sql_migration_applied(int64_t key, int64_t ver) {
    return mako_sql_migration_applied(mako_native_sql_db_from_key(key), ver);
}
MakoNativeString *mako_native_sql_query_str2_ptr(
    int64_t key, MakoNativeString *sql, MakoNativeString *p1, MakoNativeString *p2
) {
    return bridge_take_str(mako_sql_query_str2(
        mako_native_sql_db_from_key(key),
        bridge_borrow_str(sql), bridge_borrow_str(p1), bridge_borrow_str(p2)));
}
int64_t mako_native_sql_query_rows_ptr(int64_t key, MakoNativeString *sql, MakoNativeIntSlice *args) {
    MakoIntArray a = mako_int_array_empty();
    if (args && args->data && args->len) a = mako_int_array_view(args->data, args->len);
    return mako_sql_query_rows(mako_native_sql_db_from_key(key), bridge_borrow_str(sql), a);
}

int64_t mako_native_reqctx_get_value_exists(int64_t id, MakoNativeString *key) {
    /* reqctx_has-style: use get_value non-empty? prefer has if exists */
    return mako_reqctx_has(id, bridge_borrow_str(key));
}
MakoNativeString *mako_native_reqctx_get_value_ptr(int64_t id, MakoNativeString *key) {
    return bridge_take_str(mako_reqctx_get_value(id, bridge_borrow_str(key)));
}

int64_t mako_native_reflect_value_of_type_ptr(MakoNativeString *name) {
    return (int64_t)(intptr_t)mako_reflect_value_of_type(bridge_borrow_str(name));
}
int64_t mako_native_reflect_value_clone(int64_t v) {
    return (int64_t)(intptr_t)mako_reflect_value_clone((MakoReflectValue *)(intptr_t)v);
}
int64_t mako_native_reflect_value_set_at_ptr(int64_t v, int64_t idx, MakoNativeString *val) {
    return mako_reflect_value_set_at((MakoReflectValue *)(intptr_t)v, idx, bridge_borrow_str(val));
}

/* http_request_parse → boxed opaque (arena-free shallow copy of method/path/body strings) */
typedef struct {
    MakoNativeString *method;
    MakoNativeString *path;
    MakoNativeString *body;
} MakoNativeHttpRequest;
int64_t mako_native_http_request_parse_ptr(MakoNativeString *raw) {
    MakoHttpRequest r = mako_http_request_parse(bridge_borrow_str(raw));
    MakoNativeHttpRequest *box = (MakoNativeHttpRequest *)calloc(1, sizeof(*box));
    if (!box) abort();
    box->method = bridge_take_str(mako_http_request_method(r));
    box->path = bridge_take_str(mako_http_request_path(r));
    box->body = bridge_take_str(mako_http_request_body(r));
    return (int64_t)(intptr_t)box;
}
MakoNativeString *mako_native_http_request_method_ptr(int64_t h) {
    MakoNativeHttpRequest *b = (MakoNativeHttpRequest *)(intptr_t)h;
    if (!b || !b->method) return mako_native_string_literal_ptr("", 0);
    return mako_native_string_clone_ptr(b->method);
}
MakoNativeString *mako_native_http_request_path_ptr(int64_t h) {
    MakoNativeHttpRequest *b = (MakoNativeHttpRequest *)(intptr_t)h;
    if (!b || !b->path) return mako_native_string_literal_ptr("", 0);
    return mako_native_string_clone_ptr(b->path);
}
MakoNativeString *mako_native_http_request_body_ptr(int64_t h) {
    MakoNativeHttpRequest *b = (MakoNativeHttpRequest *)(intptr_t)h;
    if (!b || !b->body) return mako_native_string_literal_ptr("", 0);
    return mako_native_string_clone_ptr(b->body);
}

int64_t mako_native_nursery_drain(void *n, int64_t timeout_ms) {
    return mako_nursery_drain((MakoNursery *)n, timeout_ms);
}


static MakoHttpRequest mako_native_http_request_from_box(int64_t req) {
    MakoHttpRequest r = mako_http_request_empty();
    MakoNativeHttpRequest *b = (MakoNativeHttpRequest *)(intptr_t)req;
    if (!b) return r;
    if (b->method) r.method = bridge_borrow_str(b->method);
    if (b->path) r.path = bridge_borrow_str(b->path);
    if (b->body) r.body = bridge_borrow_str(b->body);
    return r;
}

int64_t mako_native_http_route_match_ptr(int64_t req, MakoNativeString *method, MakoNativeString *pattern) {
    MakoHttpRequest r = mako_native_http_request_from_box(req);
    return mako_http_route_match(r, bridge_borrow_str(method), bridge_borrow_str(pattern)) ? 1 : 0;
}

MakoNativeString *mako_native_hkdf_sha256_ptr(
    MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, int64_t a3
) {
    return bridge_take_str(mako_hkdf_sha256(
        bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), a3));
}

int64_t mako_native_unsafe_index_ptr(MakoNativeString *s, int64_t i) {
    if (!s || i < 0 || (size_t)i >= s->len) return -1;
    return (unsigned char)s->data[i];
}

int64_t mako_native_reqctx_set_ptr(int64_t id, MakoNativeString *key, MakoNativeString *val) {
    return mako_reqctx_set(id, bridge_borrow_str(key), bridge_borrow_str(val));
}

int64_t mako_native_unsafe_index_ints_ptr(MakoNativeIntSlice *a, int64_t i) {
    if (!a || i < 0 || (size_t)i >= a->len) return 0;
    return a->data[i];
}

MakoNativeString *mako_native_http_route_param_ptr(
    int64_t req, MakoNativeString *pattern, MakoNativeString *name
) {
    MakoHttpRequest r = mako_native_http_request_from_box(req);
    return bridge_take_str(
        mako_http_route_param(r, bridge_borrow_str(pattern), bridge_borrow_str(name)));
}

int64_t mako_native_tls_client_new_insecure(void) {
    return (int64_t)(intptr_t)mako_tls_client_new_insecure();
}

int64_t mako_native_middleware_require_context_ptr(int64_t ctx, MakoNativeString *key) {
    return mako_middleware_require_context(ctx, bridge_borrow_str(key));
}

MakoNativeString *mako_native_router_match_ptr(int64_t router, int64_t req) {
    MakoNativeHttpRequest *b = (MakoNativeHttpRequest *)(intptr_t)req;
    MakoHttpRequest r = mako_http_request_empty();
    if (b) {
        if (b->method) r.method = bridge_borrow_str(b->method);
        if (b->path) r.path = bridge_borrow_str(b->path);
        if (b->body) r.body = bridge_borrow_str(b->body);
    }
    return bridge_take_str(mako_router_match(router, r));
}

MakoNativeString *mako_native_router_match_path_ptr(int64_t router, MakoNativeString *method, MakoNativeString *path) {
    return bridge_take_str(mako_router_match_path(router, bridge_borrow_str(method), bridge_borrow_str(path)));
}
MakoNativeString *mako_native_sip_uri_host_ptr(MakoNativeString *a0) {
    MakoNativeString *ret=bridge_take_str(mako_sip_uri_host(bridge_borrow_str(a0)));
    return ret;
}

int64_t mako_native_quic_is_ack_ptr(MakoNativeString *a0) {
    int64_t ret=(int64_t)mako_quic_is_ack(bridge_borrow_str(a0));
    return ret;
}

int64_t mako_native_quic_vn_version_at_ptr(MakoNativeString *a0, int64_t a1) {
    int64_t ret=(int64_t)mako_quic_vn_version_at(bridge_borrow_str(a0), a1);
    return ret;
}

int64_t mako_native_tls_server_reload_ptr(int64_t a0, MakoNativeString *a1, MakoNativeString *a2) {
    int64_t ret=(int64_t)mako_tls_server_reload((void*)(intptr_t)a0, bridge_borrow_str(a1), bridge_borrow_str(a2));
    return ret;
}

int64_t mako_native_tls_hs_reset(void) {
    int64_t ret=(int64_t)mako_tls_hs_reset();
    return ret;
}

int64_t mako_native_http2_stream_priority_exclusive(int64_t a0) {
    int64_t ret=(int64_t)mako_http2_stream_priority_exclusive(a0);
    return ret;
}

int64_t mako_native_hot_reload_swap_count(void) {
    int64_t ret=(int64_t)mako_hot_reload_swap_count();
    return ret;
}

int64_t mako_native_tls_client_free(int64_t c) {
    return mako_tls_client_free((void *)(intptr_t)c);
}

int64_t mako_native_middleware_allow_methods_ptr(int64_t req, MakoNativeString *methods) {
    MakoNativeHttpRequest *b = (MakoNativeHttpRequest *)(intptr_t)req;
    MakoHttpRequest r = mako_http_request_empty();
    if (b) {
        if (b->method) r.method = bridge_borrow_str(b->method);
        if (b->path) r.path = bridge_borrow_str(b->path);
    }
    return mako_middleware_allow_methods(r, bridge_borrow_str(methods));
}

MakoNativeString *mako_native_router_param_ptr(int64_t router, int64_t req, MakoNativeString *name) {
    MakoNativeHttpRequest *b = (MakoNativeHttpRequest *)(intptr_t)req;
    MakoHttpRequest r = mako_http_request_empty();
    if (b) {
        if (b->method) r.method = bridge_borrow_str(b->method);
        if (b->path) r.path = bridge_borrow_str(b->path);
    }
    return bridge_take_str(mako_router_param(router, r, bridge_borrow_str(name)));
}

int64_t mako_native_tls_client_new_ptr(MakoNativeString *ca) {
    return (int64_t)(intptr_t)mako_tls_client_new(bridge_borrow_str(ca));
}

int64_t mako_native_middleware_next_ptr(int64_t ctx, MakoNativeString *name) {
    return mako_middleware_next(ctx, bridge_borrow_str(name));
}

int64_t mako_native_middleware_ran_ptr(int64_t ctx, MakoNativeString *name) {
    return mako_middleware_ran(ctx, bridge_borrow_str(name));
}

MakoNativeString *mako_native_middleware_trace_ptr(int64_t ctx) {
    return bridge_take_str(mako_middleware_trace(ctx));
}


/* ABI: tls_connect(ctx, fd, host) — matches mako_tls_connect / typecheck. */
int64_t mako_native_tls_connect_ptr(int64_t client, int64_t fd, MakoNativeString *host) {
    return (int64_t)(intptr_t)mako_tls_connect(
        (void *)(intptr_t)client, fd, bridge_borrow_str(host));
}

/* ---- bulk simple runtime bridges (native IR climb) ---- */

int64_t mako_native_time_equal(int64_t a, int64_t b) { return mako_time_equal(a, b); }
int64_t mako_native_unicode_is_lower(int64_t r) { return mako_unicode_is_lower(r); }
MakoNativeString *mako_native_syscall_errno_str(void) {
    return bridge_take_str(mako_syscall_errno_str());
}
int64_t mako_native_crash_report_installed(void) { return mako_crash_report_installed_p(); }
int64_t mako_native_http2_stream_id(void) { return mako_http2_stream_id(); }
int64_t mako_native_http2_stream_priority_child_count(int64_t p) {
    return mako_http2_stream_priority_child_count(p);
}
int64_t mako_native_http2_stream_body_len(int64_t sid) {
    return mako_http2_stream_body_len_of(sid);
}
int64_t mako_native_quic_ack_largest_ptr(MakoNativeString *s) {
    return mako_quic_ack_largest(bridge_borrow_str(s));
}
int64_t mako_native_quic_vn_select_ptr(MakoNativeString *s, int64_t preferred) {
    return mako_quic_vn_select(bridge_borrow_str(s), preferred);
}
int64_t mako_native_sip_uri_port_ptr(MakoNativeString *s) {
    return mako_sip_uri_port(bridge_borrow_str(s));
}
int64_t mako_native_frame_alloc(int64_t id, int64_t bytes) {
    return mako_frame_alloc(id, bytes);
}
MakoNativeString *mako_native_ws_recv(int64_t fd, int64_t max_bytes) {
    return bridge_take_str(mako_ws_recv(fd, max_bytes));
}
int64_t mako_native_uuid_version_ptr(MakoNativeString *s) {
    bool ok = false;
    MakoUuid u = mako_uuid_parse(bridge_borrow_str(s), &ok);
    if (!ok) return 0;
    return mako_uuid_version(u);
}
int64_t mako_native_tls_hs_state(void) { return mako_tls_hs_state_get(); }
MakoNativeString *mako_native_tls_peer_cn(int64_t conn) {
    return bridge_take_str(mako_tls_peer_cn((void *)(intptr_t)conn));
}
int64_t mako_native_tls_make_csr_ptr(
    MakoNativeString *csr, MakoNativeString *key, MakoNativeString *cn, int64_t bits
) {
    return mako_tls_make_csr(
        bridge_borrow_str(csr), bridge_borrow_str(key), bridge_borrow_str(cn), bits);
}
int64_t mako_native_tls_server_sni_add_ptr(
    int64_t srv, MakoNativeString *host, MakoNativeString *cert, MakoNativeString *key
) {
    return mako_tls_server_sni_add(
        (void *)(intptr_t)srv,
        bridge_borrow_str(host),
        bridge_borrow_str(cert),
        bridge_borrow_str(key));
}
int64_t mako_native_nghttp2_available(void) {
#if defined(MAKO_HAS_NGHTTP2) && MAKO_HAS_NGHTTP2
    return 1;
#else
    return 0;
#endif
}
int64_t mako_native_sql_rows_ok(int64_t h) { return mako_sql_rows_ok(h); }
/* SqlDB is a meta-key handle in native IR (see sql_open_* bridges). */
int64_t mako_native_sql_rollback(int64_t db) {
    return mako_sql_rollback(mako_native_sql_db_from_key(db));
}
int64_t mako_native_sql_migrate_ptr(int64_t db, int64_t ver, MakoNativeString *up) {
    return mako_sql_migrate(mako_native_sql_db_from_key(db), ver, bridge_borrow_str(up));
}
int64_t mako_native_slog_is_json(void) { return mako_native_slog_json_mode ? 1 : 0; }
void mako_native_slog_with2_ptr(
    MakoNativeString *level, MakoNativeString *msg, MakoNativeString *k1,
    MakoNativeString *v1, MakoNativeString *k2
) {
    /* Legacy 5-arg form; IR uses 6-arg with2_6. */
    mako_native_slog_emit_fields(level, msg, k1, v1, k2, NULL, NULL, NULL, 0);
}
MakoNativeString *mako_native_chan_select_value_str(void) {
    /* Payload is an owned MakoNativeString* stored as i64 on the channel. */
    int64_t v = mako_chan_select_value();
    if (!v) return mako_native_string_literal_ptr("", 0);
    return (MakoNativeString *)(intptr_t)v;
}
MakoNativeString *mako_native_hot_reload_status_json(void) {
    return bridge_take_str(mako_hot_reload_status_json());
}
int64_t mako_native_buf_writer_new_ptr(MakoNativeString *path) {
    return (int64_t)(intptr_t)mako_buf_writer_new(bridge_borrow_str(path));
}
int64_t mako_native_mmap_create_ptr(MakoNativeString *path, int64_t size) {
    return (int64_t)(intptr_t)mako_mmap_create(bridge_borrow_str(path), size);
}
int64_t mako_native_page_alloc(int64_t size) {
    return (int64_t)(intptr_t)mako_page_alloc(size);
}
int64_t mako_native_pcache_new(void) {
    return (int64_t)(intptr_t)mako_pcache_new();
}
int64_t mako_native_hindex_new(int64_t cap) {
    return (int64_t)(intptr_t)mako_hindex_new(cap);
}
int64_t mako_native_pg_connect_ptr(MakoNativeString *url) {
    MakoPgConn c = mako_pg_connect(bridge_borrow_str(url));
    return c.handle;
}
int64_t mako_native_mysql_connect_ptr(MakoNativeString *url) {
    MakoMysqlConn c = mako_mysql_connect(bridge_borrow_str(url));
    return c.handle;
}
int64_t mako_native_smtp_send_starttls_ptr(
    MakoNativeString *host, int64_t port, MakoNativeString *user, MakoNativeString *pass,
    MakoNativeString *from, MakoNativeString *to, MakoNativeString *msg
) {
    return mako_smtp_send_starttls(
        bridge_borrow_str(host), port, bridge_borrow_str(user), bridge_borrow_str(pass),
        bridge_borrow_str(from), bridge_borrow_str(to), bridge_borrow_str(msg));
}
int64_t mako_native_evloop_shutdown(int64_t el) {
    return mako_evloop_shutdown((MakoEvLoop *)(intptr_t)el);
}
int64_t mako_native_fn_has_env(int64_t f) {
    if (!f) return 0;
    MakoFn *p = (MakoFn *)(intptr_t)f;
    return p->env ? 1 : 0;
}
MakoNativeString *mako_native_cbor_encode_array_int_ptr(MakoNativeIntSlice *a) {
    MakoIntArray arr = mako_int_array_empty();
    if (a && a->data && a->len) {
        arr = mako_int_array_view(a->data, a->len);
    }
    return bridge_take_str(mako_cbor_encode_array_int(arr));
}
int64_t mako_native_reflect_value_equal(int64_t a, int64_t b) {
    return mako_reflect_value_equal(
        (MakoReflectValue *)(intptr_t)a, (MakoReflectValue *)(intptr_t)b);
}
MakoNativeString *mako_native_reflect_value_field_at(int64_t v, int64_t idx) {
    return bridge_take_str(
        mako_reflect_value_field_at((MakoReflectValue *)(intptr_t)v, idx));
}
int64_t mako_native_hot_reload_plugin_handle(void) {
    return mako_hot_reload_plugin_handle_id();
}

/* ---- next unknown-call wave ---- */

int64_t mako_native_uuid_variant_ptr(MakoNativeString *s) {
    bool ok = false;
    MakoUuid u = mako_uuid_parse(bridge_borrow_str(s), &ok);
    if (!ok) return 0;
    return mako_uuid_variant(u);
}
int64_t mako_native_unicode_to_lower(int64_t r) { return mako_unicode_to_lower(r); }
int64_t mako_native_time_trunc_day(int64_t ms) { return mako_time_trunc_day(ms); }
int64_t mako_native_ws_send_text_ptr(int64_t fd, MakoNativeString *msg) {
    return mako_ws_send_text_msg(fd, bridge_borrow_str(msg));
}
int64_t mako_native_sql_rows_cols(int64_t h) { return mako_sql_rows_cols(h); }
int64_t mako_native_sql_prepare_ptr(int64_t db, MakoNativeString *sql) {
    return mako_sql_prepare(mako_native_sql_db_from_key(db), bridge_borrow_str(sql));
}
void mako_native_slog_set_service_ptr(MakoNativeString *name) {
    MakoString s = bridge_borrow_str(name);
    size_t n = s.len;
    if (n >= sizeof(mako_native_slog_service_buf)) n = sizeof(mako_native_slog_service_buf) - 1;
    if (n && s.data) memcpy(mako_native_slog_service_buf, s.data, n);
    mako_native_slog_service_buf[n] = 0;
}
void mako_native_slog_with2_6_ptr(
    MakoNativeString *level, MakoNativeString *msg, MakoNativeString *k1,
    MakoNativeString *v1, MakoNativeString *k2, MakoNativeString *v2
) {
    mako_native_slog_emit_fields(level, msg, k1, v1, k2, v2, NULL, NULL, 0);
}
MakoNativeString *mako_native_sip_dialog_id_ptr(
    MakoNativeString *a, MakoNativeString *b, MakoNativeString *c
) {
    return bridge_take_str(mako_sip_dialog_id(
        bridge_borrow_str(a), bridge_borrow_str(b), bridge_borrow_str(c)));
}
MakoNativeString *mako_native_reflect_value_schema(int64_t v) {
    return bridge_take_str(mako_reflect_value_schema((MakoReflectValue *)(intptr_t)v));
}
int64_t mako_native_reflect_value_from_2_ptr(
    MakoNativeString *schema, MakoNativeString *a, MakoNativeString *b
) {
    return (int64_t)(intptr_t)mako_reflect_value_from_2(
        bridge_borrow_str(schema), bridge_borrow_str(a), bridge_borrow_str(b));
}
int64_t mako_native_quic_has_crypto_ptr(MakoNativeString *s) {
    return mako_quic_has_crypto(bridge_borrow_str(s)) ? 1 : 0;
}
int64_t mako_native_quic_ack_delay_ptr(MakoNativeString *s) {
    return mako_quic_ack_delay(bridge_borrow_str(s));
}
int64_t mako_native_predict_new(int64_t initial) {
    return (int64_t)(intptr_t)mako_predict_new(initial);
}
int64_t mako_native_pg_ok(int64_t h) {
    MakoPgConn c = { .handle = h };
    return mako_pg_ok(c);
}
int64_t mako_native_mysql_ok(int64_t h) {
    MakoMysqlConn c = { .handle = h };
    return mako_mysql_ok(c);
}
int64_t mako_native_pcache_get(int64_t c, int64_t page_id) {
    return (int64_t)(intptr_t)mako_pcache_get((MakoPageCache *)(intptr_t)c, page_id);
}
int64_t mako_native_page_size(int64_t p) {
    return mako_page_size((MakoPage *)(intptr_t)p);
}
int64_t mako_native_mmap_size(int64_t m) {
    return mako_mmap_size((MakoMMap *)(intptr_t)m);
}
MakoNativeString *mako_native_nghttp2_get_ptr(
    MakoNativeString *host, int64_t port, MakoNativeString *path, MakoNativeString *ca
) {
    (void)host; (void)port; (void)path; (void)ca;
    return mako_native_string_literal_ptr("", 0);
}
int64_t mako_native_http2_stream_half_closed_local(int64_t sid) {
    return mako_http2_stream_half_closed_local(sid);
}
int64_t mako_native_http2_schedule_next(void) { return mako_http2_schedule_next(); }
int64_t mako_native_hot_reload_plugin_poll(void) { return mako_hot_reload_plugin_poll(); }
int64_t mako_native_hindex_put(int64_t h, int64_t key, int64_t val) {
    return mako_hindex_put((MakoHIndex *)(intptr_t)h, key, val);
}
int64_t mako_native_frame_reset(int64_t id) { return mako_frame_reset(id); }
int64_t mako_native_fn_drop(int64_t f) {
    if (!f) return 0;
    MakoFn *p = (MakoFn *)(intptr_t)f;
    /* Match mako_fn_drop: free capture env, keep fat box so has_env still works. */
    if (p->env) {
        if (p->drop_env) {
            p->drop_env(p->env);
        } else {
            free(p->env);
        }
        p->env = NULL;
    }
    p->drop_env = NULL;
    return 0;
}
int64_t mako_native_tls_hs_is_app(void) { return mako_tls_hs_is_app(); }
int64_t mako_native_tls_server_new_mtls_ptr(
    MakoNativeString *cert, MakoNativeString *key, MakoNativeString *ca
) {
    return (int64_t)(intptr_t)mako_tls_server_new_mtls(
        bridge_borrow_str(cert), bridge_borrow_str(key), bridge_borrow_str(ca));
}
int64_t mako_native_tls_server_sni_update_ptr(
    int64_t srv, MakoNativeString *host, MakoNativeString *cert, MakoNativeString *key
) {
    return mako_tls_server_sni_update(
        (void *)(intptr_t)srv,
        bridge_borrow_str(host),
        bridge_borrow_str(cert),
        bridge_borrow_str(key));
}
int64_t mako_native_syscall_isatty(int64_t fd) { return mako_syscall_isatty(fd); }
int64_t mako_native_buf_reader_new_ptr(MakoNativeString *path) {
    return (int64_t)(intptr_t)mako_buf_reader_new(bridge_borrow_str(path));
}
int64_t mako_native_bytes_buffer(void) {
    return (int64_t)(intptr_t)mako_bytes_buffer_new();
}
int64_t mako_native_det_rng_next(int64_t seed) {
    return mako_deterministic_rng_next(seed);
}
int64_t mako_native_game_udp_bind(int64_t port) {
    MakoGameUDP *u = mako_game_udp_bind_addr(mako_str_from_cstr("0.0.0.0"), port);
    return u ? (int64_t)(intptr_t)u : 0;
}
MakoNativeIntSlice *mako_native_cbor_decode_array_int_ptr(MakoNativeString *bin) {
    MakoIntArray a = mako_cbor_decode_array_int(bridge_borrow_str(bin));
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr(a.len, a.len ? a.len : 0);
    if (a.data && a.len) {
        memcpy(out->data, a.data, a.len * sizeof(int64_t));
        out->len = a.len;
    }
    mako_int_array_free(a);
    return out;
}
MakoNativeMapSS *mako_native_map_ss_make_ptr(int64_t hint);
MakoNativeMapSS *mako_native_gob_decode_map_ss_ptr(MakoNativeString *g) {
    MakoMapSS *cm = mako_gob_decode_map_ss(bridge_borrow_str(g));
    MakoNativeMapSS *out = mako_native_map_ss_make_ptr(cm ? (int64_t)cm->len : 0);
    if (cm) {
        for (size_t i = 0; i < cm->cap; i++) {
            if (cm->state[i] != MAKO_MAP_FULL) continue;
            MakoNativeString *k = bridge_take_str(mako_str_clone(cm->keys[i]));
            MakoNativeString *v = bridge_take_str(mako_str_clone(cm->vals[i]));
            mako_native_map_ss_set_ptr(out, k, v);
            mako_native_string_drop_ptr(k);
            mako_native_string_drop_ptr(v);
        }
    }
    return out;
}


/* ---- wave3 simple chains ---- */
int64_t mako_native_debug_break_reset(void) { return mako_debug_break_reset(); }
int64_t mako_native_game_udp_fd(int64_t u) { return mako_game_udp_fd((MakoGameUDP *)(intptr_t)u); }
MakoNativeString *mako_native_gob_encode_struct(int64_t v) {
    return bridge_take_str(mako_gob_encode_struct((MakoReflectValue *)(intptr_t)v));
}
int64_t mako_native_hindex_get(int64_t h, int64_t key) {
    return mako_hindex_get((MakoHIndex *)(intptr_t)h, key);
}
int64_t mako_native_hot_reload_plugin_close(void) { return mako_hot_reload_plugin_close(); }
MakoNativeString *mako_native_http2_push_promise_frame_ptr(
    int64_t a0, int64_t a1, MakoNativeString *a2, int64_t a3
) {
    return bridge_take_str(mako_http2_push_promise_frame(a0, a1, bridge_borrow_str(a2), a3));
}
int64_t mako_native_mmap_write_ptr(int64_t m, int64_t o, MakoNativeString *d) {
    return mako_mmap_write((MakoMMap *)(intptr_t)m, o, bridge_borrow_str(d));
}
int64_t mako_native_mysql_close(int64_t h) {
    MakoMysqlConn c = { .handle = h };
    return mako_mysql_close(c);
}
MakoNativeString *mako_native_nghttp2_post_ptr(
    MakoNativeString *host, int64_t port, MakoNativeString *path,
    MakoNativeString *body, MakoNativeString *ca
) {
    (void)host; (void)port; (void)path; (void)body; (void)ca;
    return mako_native_string_literal_ptr("", 0);
}
int64_t mako_native_obj_pool_new(int64_t cap) { return mako_obj_pool_new(cap); }
int64_t mako_native_page_write_ptr(int64_t p, int64_t o, MakoNativeString *d) {
    return mako_page_write((MakoPage *)(intptr_t)p, o, bridge_borrow_str(d));
}
int64_t mako_native_pg_exec_ptr(int64_t h, MakoNativeString *sql) {
    MakoPgConn c = { .handle = h };
    return mako_pg_exec(c, bridge_borrow_str(sql));
}
int64_t mako_native_predict_state(int64_t p) {
    return mako_predict_state((MakoPredict *)(intptr_t)p);
}
int64_t mako_native_quic_ack_range_count_ptr(MakoNativeString *s) {
    return mako_quic_ack_range_count(bridge_borrow_str(s));
}
int64_t mako_native_quic_crypto_offset_ptr(MakoNativeString *s) {
    return mako_quic_crypto_offset(bridge_borrow_str(s));
}
MakoNativeString *mako_native_sip_txn_key_ptr(MakoNativeString *a, MakoNativeString *b) {
    return bridge_take_str(mako_sip_txn_key(bridge_borrow_str(a), bridge_borrow_str(b)));
}
void mako_native_slog_with3_ptr(
    MakoNativeString *level, MakoNativeString *msg, MakoNativeString *k1, MakoNativeString *v1,
    MakoNativeString *k2, MakoNativeString *v2, MakoNativeString *k3, MakoNativeString *v3
) {
    mako_native_slog_emit_fields(level, msg, k1, v1, k2, v2, k3, v3, 0);
}
int64_t mako_native_sql_rows_next(int64_t h) { return mako_sql_rows_next(h); }
int64_t mako_native_sql_stmt_query_int_ptr(int64_t stmt, MakoNativeIntSlice *args) {
    MakoIntArray a = mako_int_array_empty();
    if (args && args->data && args->len) a = mako_int_array_view(args->data, args->len);
    return mako_sql_stmt_query_int(stmt, a);
}
int64_t mako_native_syscall_umask(int64_t mask) { return mako_syscall_umask(mask); }
int64_t mako_native_time_trunc_hour(int64_t ms) { return mako_time_trunc_hour(ms); }
int64_t mako_native_tls_hs_advance_ptr(MakoNativeString *msg) {
    return mako_tls_hs_advance(bridge_borrow_str(msg));
}
int64_t mako_native_tls_server_sni_remove_ptr(int64_t srv, MakoNativeString *host) {
    return mako_tls_server_sni_remove((void *)(intptr_t)srv, bridge_borrow_str(host));
}
int64_t mako_native_unicode_to_upper(int64_t r) { return mako_unicode_to_upper(r); }
MakoNativeString *mako_native_uuid_string_upper_ptr(MakoNativeString *s) {
    bool ok = false;
    MakoUuid u = mako_uuid_parse(bridge_borrow_str(s), &ok);
    if (!ok) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_uuid_string_upper(u));
}
int64_t mako_native_ws_send_binary_ptr(int64_t fd, MakoNativeString *msg) {
    return mako_ws_send_binary_msg(fd, bridge_borrow_str(msg));
}
/* Signature: det_rng_range(seed, max) → [0, max). */
int64_t mako_native_det_rng_range(int64_t seed, int64_t max) {
    return mako_deterministic_rng_range(seed, max);
}
int64_t mako_native_context_with_timeout(int64_t ms) {
    return mako_context_with_timeout_ms(ms);
}

/* ---- wave4 auto simple bridges ---- */
int64_t mako_native_debug_bp(int64_t a0) { return (int64_t)mako_debug_bp(a0); }
int64_t mako_native_debug_bp_disable(int64_t a0) { return (int64_t)mako_debug_bp_disable(a0); }
int64_t mako_native_debug_bp_enable(int64_t a0) { return (int64_t)mako_debug_bp_enable(a0); }
int64_t mako_native_debug_break_ptr(MakoNativeString *a0) { return (int64_t)mako_debug_break(bridge_borrow_str(a0)); }
int64_t mako_native_debug_break_hits(void) { return (int64_t)mako_debug_break_hits(); }
int64_t mako_native_debug_current_task(void) { return (int64_t)mako_debug_current_task(); }
int64_t mako_native_debug_frame_depth(void) { return (int64_t)mako_debug_frame_depth(); }
MakoNativeString *mako_native_debug_frames_json_ptr(void) { return bridge_take_str(mako_debug_frames_json()); }
int64_t mako_native_debug_get_int_ptr(MakoNativeString *a0) { return (int64_t)mako_debug_get_int(bridge_borrow_str(a0)); }
int64_t mako_native_debug_line_bp_clear(int64_t a0) { return (int64_t)mako_debug_line_bp_clear(a0); }
int64_t mako_native_debug_line_bp_hits(int64_t a0) { return (int64_t)mako_debug_line_bp_hits(a0); }
int64_t mako_native_debug_line_bp_set_ptr(MakoNativeString *a0, int64_t a1) { return (int64_t)mako_debug_line_bp_set(bridge_borrow_str(a0), a1); }
MakoNativeString *mako_native_debug_locals_json_ptr(void) { return bridge_take_str(mako_debug_locals_json()); }
int64_t mako_native_debug_pop_frame(void) { return (int64_t)mako_debug_pop_frame(); }
int64_t mako_native_debug_push_frame_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) { return (int64_t)mako_debug_push_frame(bridge_borrow_str(a0), a1, bridge_borrow_str(a2)); }
int64_t mako_native_debug_set_current_task(int64_t a0) { return (int64_t)mako_debug_set_current_task(a0); }
int64_t mako_native_debug_set_int_ptr(MakoNativeString *a0, int64_t a1) { return (int64_t)mako_debug_set_int(bridge_borrow_str(a0), a1); }
MakoNativeString *mako_native_debug_snapshot_json_ptr(void) { return bridge_take_str(mako_debug_snapshot_json()); }
int64_t mako_native_debug_trap_enable(int64_t a0) { return (int64_t)mako_debug_trap_enable(a0); }
int64_t mako_native_duration_minutes(int64_t a0) { return (int64_t)mako_duration_minutes(a0); }
int64_t mako_native_duration_seconds(int64_t a0) { return (int64_t)mako_duration_seconds(a0); }
MakoNativeString *mako_native_duration_string_ptr(int64_t a0) { return bridge_take_str(mako_duration_string(a0)); }
int64_t mako_native_duration_to_hours(int64_t a0) { return (int64_t)mako_duration_to_hours(a0); }
int64_t mako_native_duration_to_minutes(int64_t a0) { return (int64_t)mako_duration_to_minutes(a0); }
int64_t mako_native_duration_to_seconds(int64_t a0) { return (int64_t)mako_duration_to_seconds(a0); }
void mako_native_game_udp_close(int64_t a0) { mako_game_udp_close((void*)(intptr_t)a0); }
int64_t mako_native_game_udp_send_to_ptr(int64_t a0, MakoNativeString *a1, int64_t a2, MakoNativeString *a3) { return (int64_t)mako_game_udp_send_to((void*)(intptr_t)a0, bridge_borrow_str(a1), a2, bridge_borrow_str(a3)); }
MakoNativeString *mako_native_game_udp_sender_addr_ptr(int64_t a0) { return bridge_take_str(mako_game_udp_sender_addr((void*)(intptr_t)a0)); }
int64_t mako_native_gfx_window_fill(int64_t a0, int64_t a1) { return (int64_t)mako_gfx_window_fill((void*)(intptr_t)a0, a1); }
int64_t mako_native_gfx_window_get_pixel(int64_t a0, int64_t a1, int64_t a2) { return (int64_t)mako_gfx_window_get_pixel((void*)(intptr_t)a0, a1, a2); }
int64_t mako_native_gfx_window_pixels(int64_t a0) { return (int64_t)mako_gfx_window_pixels((void*)(intptr_t)a0); }
int64_t mako_native_gfx_window_set_pixel(int64_t a0, int64_t a1, int64_t a2, int64_t a3) { return (int64_t)mako_gfx_window_set_pixel((void*)(intptr_t)a0, a1, a2, a3); }
MakoNativeString *mako_native_h3_stream_read_ptr(int64_t a0, int64_t a1) { return bridge_take_str(mako_h3_stream_read(a0, a1)); }
int64_t mako_native_hindex_del(int64_t a0, int64_t a1) { return (int64_t)mako_hindex_del((void*)(intptr_t)a0, a1); }
int64_t mako_native_hindex_free(int64_t a0) { return (int64_t)mako_hindex_free((void*)(intptr_t)a0); }
int64_t mako_native_hindex_len(int64_t a0) { return (int64_t)mako_hindex_len((void*)(intptr_t)a0); }
int64_t mako_native_http2_is_push_promise_ptr(MakoNativeString *a0) { return (int64_t)mako_http2_is_push_promise(bridge_borrow_str(a0)); }
int64_t mako_native_http2_next_ready_stream(void) { return (int64_t)mako_http2_next_ready_stream(); }
int64_t mako_native_http2_push_promise_stream_ptr(MakoNativeString *a0) { return (int64_t)mako_http2_push_promise_stream(bridge_borrow_str(a0)); }
int64_t mako_native_http2_ready_streams(void) { return (int64_t)mako_http2_ready_streams(); }
int64_t mako_native_http2_stream_take(int64_t a0) { return (int64_t)mako_http2_stream_take(a0); }
MakoNativeString *mako_native_http_decode_chunked_ptr(MakoNativeString *a0) { return bridge_take_str(mako_http_decode_chunked(bridge_borrow_str(a0))); }
int64_t mako_native_jpeg_has_sof0_ptr(MakoNativeString *a0) { return (int64_t)mako_jpeg_has_sof0(bridge_borrow_str(a0)); }
void mako_native_log_debug_ptr(MakoNativeString *a0) { mako_log_debug(bridge_borrow_str(a0)); }
int64_t mako_native_mmap_close(int64_t a0) { return (int64_t)mako_mmap_close((void*)(intptr_t)a0); }
MakoNativeString *mako_native_mmap_read_ptr(int64_t a0, int64_t a1, int64_t a2) { return bridge_take_str(mako_mmap_read((void*)(intptr_t)a0, a1, a2)); }
int64_t mako_native_mmap_sync(int64_t a0, int64_t a1) { return (int64_t)mako_mmap_sync((void*)(intptr_t)a0, a1); }
int64_t mako_native_mvcc_gc(int64_t a0, int64_t a1) { return (int64_t)mako_mvcc_gc((void*)(intptr_t)a0, a1); }
int64_t mako_native_mvcc_live(int64_t a0) { return (int64_t)mako_mvcc_live((void*)(intptr_t)a0); }
MakoNativeString *mako_native_mysql_connect_url_ptr(MakoNativeString *a0) { return bridge_take_str(mako_mysql_connect_url(bridge_borrow_str(a0))); }
MakoNativeString *mako_native_mysql_driver_name_ptr(MakoNativeString *a0) { return bridge_take_str(mako_mysql_driver_name(bridge_borrow_str(a0))); }
int64_t mako_native_mysql_is_mariadb_ptr(MakoNativeString *a0) { return (int64_t)mako_mysql_is_mariadb(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_nghttp2_get_two_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3, MakoNativeString *a4) {
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    return mako_native_string_literal_ptr("", 0);
}
int64_t mako_native_obj_acquire(int64_t a0) { return (int64_t)mako_obj_acquire(a0); }
int64_t mako_native_obj_available(int64_t a0) { return (int64_t)mako_obj_available(a0); }
int64_t mako_native_obj_pool_cap(int64_t a0) { return (int64_t)mako_obj_pool_cap(a0); }
int64_t mako_native_obj_release(int64_t a0, int64_t a1) { return (int64_t)mako_obj_release(a0, a1); }
int64_t mako_native_page_free(int64_t a0) { return (int64_t)mako_page_free((void*)(intptr_t)a0); }
MakoNativeString *mako_native_page_read_ptr(int64_t a0, int64_t a1, int64_t a2) { return bridge_take_str(mako_page_read((void*)(intptr_t)a0, a1, a2)); }
int64_t mako_native_pcache_free(int64_t a0) { return (int64_t)mako_pcache_free((void*)(intptr_t)a0); }
int64_t mako_native_pcache_hits(int64_t a0) { return (int64_t)mako_pcache_hits((void*)(intptr_t)a0); }
int64_t mako_native_pcache_misses(int64_t a0) { return (int64_t)mako_pcache_misses((void*)(intptr_t)a0); }
MakoNativeString *mako_native_pg_connect_url_ptr(MakoNativeString *a0) { return bridge_take_str(mako_pg_connect_url(bridge_borrow_str(a0))); }
int64_t mako_native_predict_free(int64_t a0) { return (int64_t)mako_predict_free((void*)(intptr_t)a0); }
int64_t mako_native_predict_input(int64_t a0, int64_t a1) { return (int64_t)mako_predict_input((void*)(intptr_t)a0, a1); }
int64_t mako_native_predict_reconcile(int64_t a0, int64_t a1) { return (int64_t)mako_predict_reconcile((void*)(intptr_t)a0, a1); }
int64_t mako_native_predict_tick(int64_t a0) { return (int64_t)mako_predict_tick((void*)(intptr_t)a0); }
int64_t mako_native_quic_ack_first_range_ptr(MakoNativeString *a0) { return (int64_t)mako_quic_ack_first_range(bridge_borrow_str(a0)); }
int64_t mako_native_quic_ack_smallest_ptr(MakoNativeString *a0) { return (int64_t)mako_quic_ack_smallest(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_quic_crypto_data_ptr(MakoNativeString *a0) { return bridge_take_str(mako_quic_crypto_data(bridge_borrow_str(a0))); }
int64_t mako_native_quic_crypto_data_len_ptr(MakoNativeString *a0) { return (int64_t)mako_quic_crypto_data_len(bridge_borrow_str(a0)); }
int64_t mako_native_quic_crypto_data_offset_ptr(MakoNativeString *a0) { return (int64_t)mako_quic_crypto_data_offset(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_quic_crypto_frame_ptr(MakoNativeString *a0, int64_t a1) { return bridge_take_str(mako_quic_crypto_frame(bridge_borrow_str(a0), a1)); }
int64_t mako_native_quic_is_stream_ptr(MakoNativeString *a0) { return (int64_t)mako_quic_is_stream(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_quic_stream_data_ptr(MakoNativeString *a0) { return bridge_take_str(mako_quic_stream_data(bridge_borrow_str(a0))); }
int64_t mako_native_quic_stream_data_len_ptr(MakoNativeString *a0) { return (int64_t)mako_quic_stream_data_len(bridge_borrow_str(a0)); }
int64_t mako_native_quic_stream_fin_ptr(MakoNativeString *a0) { return (int64_t)mako_quic_stream_fin(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_quic_stream_frame_ptr(int64_t a0, int64_t a1, MakoNativeString *a2, int64_t a3) { return bridge_take_str(mako_quic_stream_frame(a0, a1, bridge_borrow_str(a2), a3)); }
int64_t mako_native_quic_stream_id_of_ptr(MakoNativeString *a0) { return (int64_t)mako_quic_stream_id_of(bridge_borrow_str(a0)); }
int64_t mako_native_quic_stream_offset_ptr(MakoNativeString *a0) { return (int64_t)mako_quic_stream_offset(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_redis_connect_url_ptr(MakoNativeString *a0) { return bridge_take_str(mako_redis_connect_url(bridge_borrow_str(a0))); }
MakoNativeString *mako_native_replay_append_ptr(MakoNativeString *a0, int64_t a1, int64_t a2) { return bridge_take_str(mako_replay_append(bridge_borrow_str(a0), a1, a2)); }
int64_t mako_native_replay_input_ptr(MakoNativeString *a0, int64_t a1) { return (int64_t)mako_replay_input(bridge_borrow_str(a0), a1); }
int64_t mako_native_rtp_header_len(void) { return (int64_t)mako_rtp_header_len(); }
int64_t mako_native_rtp_marker_ptr(MakoNativeString *a0) { return (int64_t)mako_rtp_marker(bridge_borrow_str(a0)); }
int64_t mako_native_rtp_parse_ok_ptr(MakoNativeString *a0) { return (int64_t)mako_rtp_parse_ok(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_rtp_payload_ptr(MakoNativeString *a0) { return bridge_take_str(mako_rtp_payload(bridge_borrow_str(a0))); }
int64_t mako_native_rtp_payload_type_ptr(MakoNativeString *a0) { return (int64_t)mako_rtp_payload_type(bridge_borrow_str(a0)); }
int64_t mako_native_rtp_timestamp_ptr(MakoNativeString *a0) { return (int64_t)mako_rtp_timestamp(bridge_borrow_str(a0)); }
int64_t mako_native_rtp_version_ptr(MakoNativeString *a0) { return (int64_t)mako_rtp_version(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_sdp_append_line_ptr(MakoNativeString *a0, MakoNativeString *a1) { return bridge_take_str(mako_sdp_append_line(bridge_borrow_str(a0), bridge_borrow_str(a1))); }
MakoNativeString *mako_native_sdp_attr_candidate_ptr(int64_t a0, int64_t a1, MakoNativeString *a2, int64_t a3, MakoNativeString *a4, int64_t a5, MakoNativeString *a6) { return bridge_take_str(mako_sdp_attr_candidate(a0, a1, bridge_borrow_str(a2), a3, bridge_borrow_str(a4), a5, bridge_borrow_str(a6))); }
MakoNativeString *mako_native_sdp_attr_fmtp_ptr(int64_t a0, MakoNativeString *a1) { return bridge_take_str(mako_sdp_attr_fmtp(a0, bridge_borrow_str(a1))); }
MakoNativeString *mako_native_sdp_attr_rtpmap_ptr(int64_t a0, MakoNativeString *a1, int64_t a2) { return bridge_take_str(mako_sdp_attr_rtpmap(a0, bridge_borrow_str(a1), a2)); }
MakoNativeString *mako_native_sdp_build_av_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, int64_t a3, MakoNativeString *a4, int64_t a5, MakoNativeString *a6) { return bridge_take_str(mako_sdp_build_av(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), a3, bridge_borrow_str(a4), a5, bridge_borrow_str(a6))); }
MakoNativeString *mako_native_sdp_connection_ptr(MakoNativeString *a0) { return bridge_take_str(mako_sdp_connection(bridge_borrow_str(a0))); }
int64_t mako_native_sdp_connection_is_ip6_ptr(MakoNativeString *a0) { return (int64_t)mako_sdp_connection_is_ip6(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_sdp_media_connection_addr_ptr(MakoNativeString *a0, int64_t a1) { return bridge_take_str(mako_sdp_media_connection_addr(bridge_borrow_str(a0), a1)); }
MakoNativeString *mako_native_sdp_media_direction_ptr(MakoNativeString *a0, int64_t a1) { return bridge_take_str(mako_sdp_media_direction(bridge_borrow_str(a0), a1)); }
MakoNativeString *mako_native_sdp_media_formats_ptr(MakoNativeString *a0, int64_t a1) { return bridge_take_str(mako_sdp_media_formats(bridge_borrow_str(a0), a1)); }
MakoNativeString *mako_native_sdp_origin_addr_ptr(MakoNativeString *a0) { return bridge_take_str(mako_sdp_origin_addr(bridge_borrow_str(a0))); }
MakoNativeString *mako_native_sdp_replace_connection_addr_ptr(MakoNativeString *a0, MakoNativeString *a1) { return bridge_take_str(mako_sdp_replace_connection_addr(bridge_borrow_str(a0), bridge_borrow_str(a1))); }
MakoNativeString *mako_native_sdp_replace_media_port_ptr(MakoNativeString *a0, int64_t a1, int64_t a2) { return bridge_take_str(mako_sdp_replace_media_port(bridge_borrow_str(a0), a1, a2)); }
MakoNativeString *mako_native_sdp_set_media_direction_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) { return bridge_take_str(mako_sdp_set_media_direction(bridge_borrow_str(a0), a1, bridge_borrow_str(a2))); }
MakoNativeString *mako_native_sdp_timing_ptr(MakoNativeString *a0) { return bridge_take_str(mako_sdp_timing(bridge_borrow_str(a0))); }
int64_t mako_native_simd_dot_i64_4(int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6, int64_t a7) { return (int64_t)mako_simd_dot_i64_4(a0, a1, a2, a3, a4, a5, a6, a7); }
int64_t mako_native_simd_sum_i64_4(int64_t a0, int64_t a1, int64_t a2, int64_t a3) { return (int64_t)mako_simd_sum_i64_4(a0, a1, a2, a3); }
MakoNativeString *mako_native_sip_auth_param_ptr(MakoNativeString *a0, MakoNativeString *a1) { return bridge_take_str(mako_sip_auth_param(bridge_borrow_str(a0), bridge_borrow_str(a1))); }
MakoNativeString *mako_native_sip_authorization_digest_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2, MakoNativeString *a3, MakoNativeString *a4) { return bridge_take_str(mako_sip_authorization_digest(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2), bridge_borrow_str(a3), bridge_borrow_str(a4))); }
int64_t mako_native_sip_body_view_ptr(MakoNativeString *a0) { return (int64_t)mako_sip_body_view(bridge_borrow_str(a0)); }
int64_t mako_native_sip_header_ci_eq_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) { return (int64_t)mako_sip_header_ci_eq(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)); }
int64_t mako_native_sip_header_contains_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) { return (int64_t)mako_sip_header_contains(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)); }
int64_t mako_native_sip_header_eq_ptr(MakoNativeString *a0, MakoNativeString *a1, MakoNativeString *a2) { return (int64_t)mako_sip_header_eq(bridge_borrow_str(a0), bridge_borrow_str(a1), bridge_borrow_str(a2)); }
int64_t mako_native_sip_header_view_ptr(MakoNativeString *a0, MakoNativeString *a1) { return (int64_t)mako_sip_header_view(bridge_borrow_str(a0), bridge_borrow_str(a1)); }
MakoNativeString *mako_native_sip_insert_via_ptr(MakoNativeString *a0, MakoNativeString *a1) { return bridge_take_str(mako_sip_insert_via(bridge_borrow_str(a0), bridge_borrow_str(a1))); }
int64_t mako_native_sip_method_eq_ptr(MakoNativeString *a0, MakoNativeString *a1) { return (int64_t)mako_sip_method_eq(bridge_borrow_str(a0), bridge_borrow_str(a1)); }
int64_t mako_native_sip_method_view_ptr(MakoNativeString *a0) { return (int64_t)mako_sip_method_view(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_sip_msg_fix_top_via_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2) { return bridge_take_str(mako_sip_msg_fix_top_via(bridge_borrow_str(a0), bridge_borrow_str(a1), a2)); }
MakoNativeString *mako_native_sip_msg_response_host_ptr(MakoNativeString *a0) { return bridge_take_str(mako_sip_msg_response_host(bridge_borrow_str(a0))); }
int64_t mako_native_sip_msg_response_port_ptr(MakoNativeString *a0) { return (int64_t)mako_sip_msg_response_port(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_sip_record_route_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2) { return bridge_take_str(mako_sip_record_route(bridge_borrow_str(a0), a1, bridge_borrow_str(a2))); }
MakoNativeString *mako_native_sip_reply_with_to_tag_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3, MakoNativeString *a4, MakoNativeString *a5) { return bridge_take_str(mako_sip_reply_with_to_tag(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3), bridge_borrow_str(a4), bridge_borrow_str(a5))); }
int64_t mako_native_sip_udp_bind_ptr(MakoNativeString *a0, int64_t a1) { return (int64_t)mako_sip_udp_bind(bridge_borrow_str(a0), a1); }
MakoNativeString *mako_native_sip_udp_recv_ptr(int64_t a0, int64_t a1) { return bridge_take_str(mako_sip_udp_recv(a0, a1)); }
int64_t mako_native_sip_udp_send_ptr(int64_t a0, MakoNativeString *a1, int64_t a2, MakoNativeString *a3) { return (int64_t)mako_sip_udp_send(a0, bridge_borrow_str(a1), a2, bridge_borrow_str(a3)); }
MakoNativeString *mako_native_sip_via_fix_source_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2) { return bridge_take_str(mako_sip_via_fix_source(bridge_borrow_str(a0), bridge_borrow_str(a1), a2)); }
int64_t mako_native_sip_via_has_rport_ptr(MakoNativeString *a0) { return (int64_t)mako_sip_via_has_rport(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_sip_via_host_ptr(MakoNativeString *a0) { return bridge_take_str(mako_sip_via_host(bridge_borrow_str(a0))); }
MakoNativeString *mako_native_sip_via_maddr_ptr(MakoNativeString *a0) { return bridge_take_str(mako_sip_via_maddr(bridge_borrow_str(a0))); }
int64_t mako_native_sip_via_port_ptr(MakoNativeString *a0) { return (int64_t)mako_sip_via_port(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_sip_via_received_ptr(MakoNativeString *a0) { return bridge_take_str(mako_sip_via_received(bridge_borrow_str(a0))); }
MakoNativeString *mako_native_sip_via_response_addr_ptr(MakoNativeString *a0) { return bridge_take_str(mako_sip_via_response_addr(bridge_borrow_str(a0))); }
MakoNativeString *mako_native_sip_via_response_host_ptr(MakoNativeString *a0) { return bridge_take_str(mako_sip_via_response_host(bridge_borrow_str(a0))); }
int64_t mako_native_sip_via_response_port_ptr(MakoNativeString *a0) { return (int64_t)mako_sip_via_response_port(bridge_borrow_str(a0)); }
int64_t mako_native_sip_via_rport_ptr(MakoNativeString *a0) { return (int64_t)mako_sip_via_rport(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_sip_via_transport_ptr(MakoNativeString *a0) { return bridge_take_str(mako_sip_via_transport(bridge_borrow_str(a0))); }
MakoNativeString *mako_native_sip_via_value_nat_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2, MakoNativeString *a3, MakoNativeString *a4, int64_t a5) { return bridge_take_str(mako_sip_via_value_nat(bridge_borrow_str(a0), bridge_borrow_str(a1), a2, bridge_borrow_str(a3), bridge_borrow_str(a4), a5)); }
MakoNativeString *mako_native_sip_via_value_rport_ptr(MakoNativeString *a0, MakoNativeString *a1, int64_t a2, MakoNativeString *a3) { return bridge_take_str(mako_sip_via_value_rport(bridge_borrow_str(a0), bridge_borrow_str(a1), a2, bridge_borrow_str(a3))); }
int64_t mako_native_sip_view_ci_eq_ptr(MakoNativeString *a0) { return (int64_t)mako_sip_view_ci_eq(bridge_borrow_str(a0)); }
int64_t mako_native_sip_view_contains_ptr(MakoNativeString *a0) { return (int64_t)mako_sip_view_contains(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_sip_view_copy_ptr(void) { return bridge_take_str(mako_sip_view_copy()); }
int64_t mako_native_sip_view_eq_ptr(MakoNativeString *a0) { return (int64_t)mako_sip_view_eq(bridge_borrow_str(a0)); }
int64_t mako_native_sip_view_len(void) { return (int64_t)mako_sip_view_len(); }
int64_t mako_native_sip_view_offset(void) { return (int64_t)mako_sip_view_offset(); }
void mako_native_slog_flush(void) {
    FILE *fp = mako_native_slog_out_fp ? mako_native_slog_out_fp : stderr;
    fflush(fp);
}
int64_t mako_native_slog_set_output_ptr(MakoNativeString *a0) {
    MakoString path = bridge_borrow_str(a0);
    if (mako_native_slog_out_fp && mako_native_slog_out_fp != stderr) {
        fclose(mako_native_slog_out_fp);
        mako_native_slog_out_fp = NULL;
    }
    if (!path.data || path.len == 0) {
        mako_native_slog_out_fp = NULL; /* stderr */
        return 1;
    }
    char buf[512];
    size_t n = path.len < sizeof(buf) - 1 ? path.len : sizeof(buf) - 1;
    memcpy(buf, path.data, n);
    buf[n] = 0;
    FILE *f = fopen(buf, "a");
    if (!f) return 0;
    mako_native_slog_out_fp = f;
    return 1;
}
void mako_native_slog_with_int_ptr(
    MakoNativeString *level, MakoNativeString *msg, MakoNativeString *key, int64_t val
) {
    int n = mako_native_slog_level_num(level);
    if (n < mako_native_slog_min_level) return;
    char num[32];
    snprintf(num, sizeof(num), "%lld", (long long)val);
    MakoNativeString vhdr = { .data = num, .len = strlen(num) };
    mako_native_slog_emit_fields(level, msg, key, &vhdr, NULL, NULL, NULL, NULL, 0);
}
int64_t mako_native_snap_count_ptr(MakoNativeString *a0) { return (int64_t)mako_snap_count(bridge_borrow_str(a0)); }
MakoNativeString *mako_native_snap_encode2_ptr(int64_t a0, int64_t a1) { return bridge_take_str(mako_snap_encode2(a0, a1)); }
int64_t mako_native_snap_predict(int64_t a0, int64_t a1) { return (int64_t)mako_snap_predict(a0, a1); }
int64_t mako_native_snap_reconcile(int64_t a0, int64_t a1) { return (int64_t)mako_snap_reconcile(a0, a1); }
int64_t mako_native_sql_rows_close(int64_t a0) { return (int64_t)mako_sql_rows_close(a0); }
int64_t mako_native_sql_rows_int(int64_t a0, int64_t a1) { return (int64_t)mako_sql_rows_int(a0, a1); }
MakoNativeString *mako_native_sql_rows_str_ptr(int64_t a0, int64_t a1) { return bridge_take_str(mako_sql_rows_str(a0, a1)); }
int64_t mako_native_sql_stmt_close(int64_t a0) { return (int64_t)mako_sql_stmt_close(a0); }
int64_t mako_native_store_len(int64_t a0) { return (int64_t)mako_store_len((void*)(intptr_t)a0); }
int64_t mako_native_store_rollback(int64_t a0) { return (int64_t)mako_store_rollback((void*)(intptr_t)a0); }
int64_t mako_native_str_eq_ptr(MakoNativeString *a0, MakoNativeString *a1) { return (int64_t)mako_str_eq(bridge_borrow_str(a0), bridge_borrow_str(a1)); }
int64_t mako_native_syscall_dup(int64_t a0) { return (int64_t)mako_syscall_dup(a0); }
int64_t mako_native_task_done(int64_t a0) { return (int64_t)mako_task_done((void*)(intptr_t)a0); }
int64_t mako_native_task_id(int64_t a0) { return (int64_t)mako_task_id((void*)(intptr_t)a0); }
int64_t mako_native_task_joined(int64_t a0) { return (int64_t)mako_task_joined((void*)(intptr_t)a0); }
MakoNativeString *mako_native_tasks_inspect_json_ptr(void) { return bridge_take_str(mako_tasks_inspect_json()); }
int64_t mako_native_tcp_accept4(int64_t a0) { return (int64_t)mako_tcp_accept4(a0); }
int64_t mako_native_tcp_connect_check(int64_t a0) { return (int64_t)mako_tcp_connect_check(a0); }
int64_t mako_native_tcp_connect_nb_ptr(MakoNativeString *a0, int64_t a1) { return (int64_t)mako_tcp_connect_nb(bridge_borrow_str(a0), a1); }
int64_t mako_native_tcp_connect_wait(int64_t a0, int64_t a1) { return (int64_t)mako_tcp_connect_wait(a0, a1); }
int64_t mako_native_tcp_fd_copy(int64_t a0, int64_t a1, int64_t a2) { return (int64_t)mako_tcp_fd_copy(a0, a1, a2); }
int64_t mako_native_tcp_listen_reuseport_ptr(MakoNativeString *a0, int64_t a1, int64_t a2) { return (int64_t)mako_tcp_listen_reuseport(bridge_borrow_str(a0), a1, a2); }
int64_t mako_native_tcp_nodelay(int64_t a0) { return (int64_t)mako_tcp_nodelay(a0); }
int64_t mako_native_tcp_reuseport(int64_t a0) { return (int64_t)mako_tcp_reuseport(a0); }
int64_t mako_native_tcp_set_recv_buf(int64_t a0, int64_t a1) { return (int64_t)mako_tcp_set_recv_buf(a0, a1); }
int64_t mako_native_tcp_set_send_buf(int64_t a0, int64_t a1) { return (int64_t)mako_tcp_set_send_buf(a0, a1); }
int64_t mako_native_tcp_splice(int64_t a0, int64_t a1, int64_t a2) { return (int64_t)mako_tcp_splice(a0, a1, a2); }
int64_t mako_native_time_local_offset_sec(void) { return (int64_t)mako_time_local_offset_sec(); }
int64_t mako_native_time_since_ms(int64_t a0) { return (int64_t)mako_time_since_ms(a0); }
int64_t mako_native_time_until_ms(int64_t a0) { return (int64_t)mako_time_until_ms(a0); }
MakoNativeString *mako_native_tls_encrypted_extensions_ptr(void) { return bridge_take_str(mako_tls_encrypted_extensions()); }
MakoNativeString *mako_native_tls_finished_ptr(MakoNativeString *a0) { return bridge_take_str(mako_tls_finished(bridge_borrow_str(a0))); }
int64_t mako_native_tls_handshake_step(int64_t a0) { return (int64_t)mako_tls_handshake_step((void*)(intptr_t)a0); }
int64_t mako_native_tls_hs_session_certificate_ptr(MakoNativeString *a0) { return (int64_t)mako_tls_hs_session_certificate(bridge_borrow_str(a0)); }
int64_t mako_native_tls_hs_session_certificate_verify_ptr(int64_t a0, MakoNativeString *a1) { return (int64_t)mako_tls_hs_session_certificate_verify(a0, bridge_borrow_str(a1)); }
int64_t mako_native_tls_hs_session_client_hello_ptr(MakoNativeString *a0) { return (int64_t)mako_tls_hs_session_client_hello(bridge_borrow_str(a0)); }
int64_t mako_native_tls_hs_session_encrypted_extensions(void) { return (int64_t)mako_tls_hs_session_encrypted_extensions(); }
int64_t mako_native_tls_hs_session_finished_ptr(MakoNativeString *a0, MakoNativeString *a1) { return (int64_t)mako_tls_hs_session_finished(bridge_borrow_str(a0), bridge_borrow_str(a1)); }
MakoNativeString *mako_native_tls_hs_session_finished_hex_ptr(MakoNativeString *a0) { return bridge_take_str(mako_tls_hs_session_finished_hex(bridge_borrow_str(a0))); }
int64_t mako_native_tls_hs_session_reset(void) { return (int64_t)mako_tls_hs_session_reset(); }
int64_t mako_native_tls_hs_session_server_hello_ptr(MakoNativeString *a0) { return (int64_t)mako_tls_hs_session_server_hello(bridge_borrow_str(a0)); }
int64_t mako_native_tls_is_init_finished(int64_t a0) { return (int64_t)mako_tls_is_init_finished((void*)(intptr_t)a0); }
int64_t mako_native_tls_want_read(int64_t a0) { return (int64_t)mako_tls_want_read((void*)(intptr_t)a0); }
int64_t mako_native_ulid_parse_ok_ptr(MakoNativeString *a0) { return (int64_t)mako_ulid_parse_ok(bridge_borrow_str(a0)); }
int64_t mako_native_unicode_is_ptr(MakoNativeString *a0, int64_t a1) { return (int64_t)mako_unicode_is(bridge_borrow_str(a0), a1); }
int64_t mako_native_unicode_simple_fold(int64_t a0) { return (int64_t)mako_unicode_simple_fold(a0); }
int64_t mako_native_unicode_to_title(int64_t a0) { return (int64_t)mako_unicode_to_title(a0); }
int64_t mako_native_wal_append_ptr(int64_t a0, MakoNativeString *a1) { return (int64_t)mako_wal_append((void*)(intptr_t)a0, bridge_borrow_str(a1)); }
int64_t mako_native_wal_next_off(void) { return (int64_t)mako_wal_next_off(); }
MakoNativeString *mako_native_wal_read_at_ptr(int64_t a0, int64_t a1) { return bridge_take_str(mako_wal_read_at((void*)(intptr_t)a0, a1)); }
int64_t mako_native_wal_size(int64_t a0) { return (int64_t)mako_wal_size((void*)(intptr_t)a0); }
int64_t mako_native_wal_sync(int64_t a0) { return (int64_t)mako_wal_sync((void*)(intptr_t)a0); }
int64_t mako_native_ws_client_connect_ptr(MakoNativeString *a0, int64_t a1, MakoNativeString *a2, MakoNativeString *a3) { return (int64_t)mako_ws_client_connect(bridge_borrow_str(a0), a1, bridge_borrow_str(a2), bridge_borrow_str(a3)); }
MakoNativeString *mako_native_ws_client_recv_ptr(int64_t a0, int64_t a1) { return bridge_take_str(mako_ws_client_recv(a0, a1)); }
int64_t mako_native_ws_client_send_binary_ptr(int64_t a0, MakoNativeString *a1) { return (int64_t)mako_ws_client_send_binary(a0, bridge_borrow_str(a1)); }
int64_t mako_native_ws_client_send_close_ptr(int64_t a0, int64_t a1, MakoNativeString *a2) { return (int64_t)mako_ws_client_send_close(a0, a1, bridge_borrow_str(a2)); }
int64_t mako_native_ws_client_send_ping_ptr(int64_t a0, MakoNativeString *a1) { return (int64_t)mako_ws_client_send_ping(a0, bridge_borrow_str(a1)); }
int64_t mako_native_ws_client_send_text_ptr(int64_t a0, MakoNativeString *a1) { return (int64_t)mako_ws_client_send_text(a0, bridge_borrow_str(a1)); }
int64_t mako_native_ws_close(int64_t a0) { return (int64_t)mako_ws_close(a0); }
int64_t mako_native_ws_last_close_code(void) { return (int64_t)mako_ws_last_close_code(); }
int64_t mako_native_ws_last_fin(void) { return (int64_t)mako_ws_last_fin(); }
int64_t mako_native_ws_last_status(void) { return (int64_t)mako_ws_last_status(); }
int64_t mako_native_ws_send_close_ptr(int64_t a0, int64_t a1, MakoNativeString *a2) { return (int64_t)mako_ws_send_close(a0, a1, bridge_borrow_str(a2)); }
int64_t mako_native_ws_send_ping(int64_t a0, int64_t a1, int64_t a2) { return (int64_t)mako_ws_send_ping(a0, (void*)(intptr_t)a1, a2); }
int64_t mako_native_ws_send_pong(int64_t a0, int64_t a1, int64_t a2) { return (int64_t)mako_ws_send_pong(a0, (void*)(intptr_t)a1, a2); }

/* ---- wave5 residual simple ---- */
int64_t mako_native_ws_send_ping_ptr(int64_t fd, MakoNativeString *msg) {
    MakoString m = bridge_borrow_str(msg);
    return (int64_t)mako_ws_send_ping((int)fd, m.data ? m.data : "", m.len);
}
MakoNativeString *mako_native_uuid_urn_ptr(MakoNativeString *s) {
    bool ok = false;
    MakoUuid u = mako_uuid_parse(bridge_borrow_str(s), &ok);
    if (!ok) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_uuid_urn(u));
}
void mako_native_sleep_ns(int64_t ns) { mako_sleep_ns(ns); }
int64_t mako_native_mmap_open_ptr(MakoNativeString *path, int64_t mode) {
    return (int64_t)(intptr_t)mako_mmap_open(bridge_borrow_str(path), mode);
}
int64_t mako_native_sql_query_rows_str_ptr(int64_t db, MakoNativeString *sql, MakoNativeString *p1) {
    return mako_sql_query_rows_str(
        mako_native_sql_db_from_key(db), bridge_borrow_str(sql), bridge_borrow_str(p1));
}
int64_t mako_native_sql_stmt_exec_ptr(int64_t stmt, MakoNativeIntSlice *args) {
    MakoIntArray a = mako_int_array_empty();
    if (args && args->data && args->len) a = mako_int_array_view(args->data, args->len);
    return mako_sql_stmt_exec(stmt, a);
}
int64_t mako_native_pg_exec_row_count_ptr(int64_t h, MakoNativeString *sql) {
    MakoPgConn c = { .handle = h };
    return mako_pg_exec_row_count(c, bridge_borrow_str(sql));
}
int64_t mako_native_redis_connect_ptr(MakoNativeString *url) {
    MakoRedisConn c = mako_redis_connect(bridge_borrow_str(url));
    return c.handle;
}
int64_t mako_native_gob_decode_struct_ptr(MakoNativeString *g) {
    return (int64_t)(intptr_t)mako_gob_decode_struct(bridge_borrow_str(g));
}

int64_t mako_native_ws_send_pong_ptr(int64_t fd, MakoNativeString *msg) {
    return mako_ws_send_pong_msg(fd, bridge_borrow_str(msg));
}
MakoNativeString *mako_native_uuid_bytes_ptr(MakoNativeString *s) {
    bool ok = false;
    MakoUuid u = mako_uuid_parse(bridge_borrow_str(s), &ok);
    if (!ok) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_uuid_bytes(u));
}
int64_t mako_native_redis_ok(int64_t h) {
    MakoRedisConn c = { .handle = h };
    return mako_redis_ok(c);
}
int64_t mako_native_pg_close(int64_t h) {
    MakoPgConn c = { .handle = h };
    return mako_pg_close(c);
}

/* try_recv with out pack: 1=got (pack[0]=val), 0=empty */
int64_t mako_native_chan_try_recv_out(void *c, int64_t pack) {
    int64_t v = 0;
    if (!mako_chan_try_recv((MakoChan *)c, &v)) return 0;
    mako_native_pack_set((void *)(uintptr_t)pack, 0, v);
    return 1;
}

int64_t mako_native_task_join_deadline_i64(MakoTask *t, int64_t deadline_ns, int64_t pack) {
    if (!t) return 0;
    int64_t out = 0;
    int64_t ok = mako_await_deadline_ns(t, deadline_ns, &out);
    if (ok) {
        mako_native_pack_set((void *)(uintptr_t)pack, 0, out);
        return 1;
    }
    return 0;
}

void mako_native_nursery_note_err_ptr(MakoNursery *n, MakoNativeString *msg) {
    if (!n) return;
    mako_nursery_note_err(n, bridge_borrow_str(msg));
}

int64_t mako_native_nursery_err_count(MakoNursery *n) {
    return mako_nursery_err_count(n);
}

MakoNativeString *mako_native_nursery_first_err(MakoNursery *n) {
    return bridge_take_str(mako_nursery_first_err(n));
}

void mako_native_nursery_join_pending(MakoNursery *n) {
    if (n) mako_nursery_join_pending(n);
}

int64_t mako_native_nursery_ok(MakoNursery *n) {
    return mako_nursery_ok(n);
}
int64_t mako_native_deadline_remaining_ms(int64_t d) {
    return mako_deadline_remaining_ms(d);
}

/* Fat fn pointer: heap MakoFn { code, env, drop }. */
int64_t mako_native_fn_box(int64_t code, int64_t env) {
    MakoFn *p = (MakoFn *)calloc(1, sizeof(MakoFn));
    if (!p) abort();
    p->fn = (void *)(intptr_t)code;
    p->env = (void *)(intptr_t)env;
    p->drop_env = NULL; /* pack_free not needed for i64 packs here; leak env pack is ok for tests */
    return (int64_t)(intptr_t)p;
}

int64_t mako_native_fn_call0(int64_t fbox) {
    if (!fbox) return 0;
    MakoFn *f = (MakoFn *)(intptr_t)fbox;
    if (f->env)
        return ((int64_t (*)(void *))f->fn)(f->env);
    return ((int64_t (*)(void))f->fn)();
}
int64_t mako_native_fn_call1(int64_t fbox, int64_t a0) {
    if (!fbox) return 0;
    MakoFn *f = (MakoFn *)(intptr_t)fbox;
    if (f->env)
        return ((int64_t (*)(void *, int64_t))f->fn)(f->env, a0);
    return ((int64_t (*)(int64_t))f->fn)(a0);
}
int64_t mako_native_fn_call2(int64_t fbox, int64_t a0, int64_t a1) {
    if (!fbox) return 0;
    MakoFn *f = (MakoFn *)(intptr_t)fbox;
    if (f->env)
        return ((int64_t (*)(void *, int64_t, int64_t))f->fn)(f->env, a0, a1);
    return ((int64_t (*)(int64_t, int64_t))f->fn)(a0, a1);
}
int64_t mako_native_fn_call3(int64_t fbox, int64_t a0, int64_t a1, int64_t a2) {
    if (!fbox) return 0;
    MakoFn *f = (MakoFn *)(intptr_t)fbox;
    if (f->env)
        return ((int64_t (*)(void *, int64_t, int64_t, int64_t))f->fn)(f->env, a0, a1, a2);
    return ((int64_t (*)(int64_t, int64_t, int64_t))f->fn)(a0, a1, a2);
}

/* ---- residual unknowns ---- */
int64_t mako_native_duration_ms(int64_t n) { return mako_duration_ms(n); }
int64_t mako_native_debug_trap_enabled(void) {
    return mako_debug_trap_enabled ? 1 : 0;
}
int64_t mako_native_context_remaining(int64_t deadline_ms) {
    return mako_context_remaining_ms(deadline_ms);
}
int64_t mako_native_alloc_live_bytes(void) {
    return mako_alloc_track_live_bytes();
}
int64_t mako_native_ws_last_opcode(void) {
    return mako_ws_last_frame_opcode();
}
MakoNativeString *mako_native_uuid_from_bytes_ptr(MakoNativeString *s) {
    bool ok = false;
    MakoUuid u = mako_uuid_from_bytes(bridge_borrow_str(s), &ok);
    if (!ok) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_uuid_string(u));
}
MakoNativeString *mako_native_redis_conn_ping(int64_t h) {
    MakoRedisConn c = { .handle = h };
    return bridge_take_str(mako_redis_conn_ping(c));
}
MakoNativeIntSlice *mako_native_sql_query_col_int_ptr(int64_t db, MakoNativeString *sql, int64_t max_rows) {
    MakoIntArray a = mako_sql_query_col_int(
        mako_native_sql_db_from_key(db), bridge_borrow_str(sql), max_rows);
    size_t n = a.len;
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr(n, n ? n : 1);
    for (size_t i = 0; i < n; i++) out->data[i] = a.data[i];
    free(a.data);
    return out;
}
int64_t mako_native_uuid_cmp_ptr(MakoNativeString *a, MakoNativeString *b) {
    bool oka=false, okb=false;
    MakoUuid ua = mako_uuid_parse(bridge_borrow_str(a), &oka);
    MakoUuid ub = mako_uuid_parse(bridge_borrow_str(b), &okb);
    if (!oka || !okb) return oka==okb ? 0 : (oka ? 1 : -1);
    return mako_uuid_eq(ua, ub) ? 0 : 1;
}
MakoNativeString *mako_native_uuid_v7(void) {
    return bridge_take_str(mako_uuid_string(mako_uuid_v7()));
}
int64_t mako_native_redis_close(int64_t h) {
    MakoRedisConn c = { .handle = h };
    return mako_redis_close(c);
}
MakoNativeStringSlice *mako_native_sql_query_col_str_ptr(int64_t db, MakoNativeString *sql, int64_t max_rows) {
    MakoStrArray a = mako_sql_query_col_str(
        mako_native_sql_db_from_key(db), bridge_borrow_str(sql), max_rows);
    size_t n = a.len;
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(n, n ? n : 1);
    for (size_t i = 0; i < n; i++) {
        out->data[i] = bridge_take_str(a.data[i]);
    }
    free(a.data);
    return out;
}

/* ---- http_parse / HttpParsed bag (heap POD for Opaque handle ABI) ---- */
int64_t mako_native_http_parse_ptr(MakoNativeString *raw) {
    MakoHttpParsed *p = (MakoHttpParsed *)calloc(1, sizeof(MakoHttpParsed));
    if (!p) return 0;
    *p = mako_http_parse(bridge_borrow_str(raw));
    return (int64_t)(intptr_t)p;
}
int64_t mako_native_http_parsed_ok(int64_t h) {
    MakoHttpParsed *p = (MakoHttpParsed *)(intptr_t)h;
    return p ? mako_http_parsed_ok(*p) : 0;
}
int64_t mako_native_http_parsed_content_length(int64_t h) {
    MakoHttpParsed *p = (MakoHttpParsed *)(intptr_t)h;
    return p ? mako_http_parsed_content_length(*p) : -1;
}
int64_t mako_native_http_parsed_chunked(int64_t h) {
    MakoHttpParsed *p = (MakoHttpParsed *)(intptr_t)h;
    return p ? mako_http_parsed_chunked(*p) : 0;
}
MakoNativeString *mako_native_http_parsed_method_ptr(int64_t h) {
    MakoHttpParsed *p = (MakoHttpParsed *)(intptr_t)h;
    if (!p) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_http_parsed_method(*p));
}
MakoNativeString *mako_native_http_parsed_path_ptr(int64_t h) {
    MakoHttpParsed *p = (MakoHttpParsed *)(intptr_t)h;
    if (!p) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_http_parsed_path(*p));
}
MakoNativeString *mako_native_http_parsed_host_ptr(int64_t h) {
    MakoHttpParsed *p = (MakoHttpParsed *)(intptr_t)h;
    if (!p) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_http_parsed_host(*p));
}
MakoNativeString *mako_native_http_parsed_headers_ptr(int64_t h) {
    MakoHttpParsed *p = (MakoHttpParsed *)(intptr_t)h;
    if (!p) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_http_parsed_headers(*p));
}
MakoNativeString *mako_native_http_parsed_body_ptr(int64_t h) {
    MakoHttpParsed *p = (MakoHttpParsed *)(intptr_t)h;
    if (!p) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_http_parsed_body(*p));
}
MakoNativeString *mako_native_http_parsed_header_ptr(int64_t h, MakoNativeString *name) {
    MakoHttpParsed *p = (MakoHttpParsed *)(intptr_t)h;
    if (!p) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_http_parsed_header(*p, bridge_borrow_str(name)));
}

/* ULID timestamp extraction (uuid string → ms). */
int64_t mako_native_ulid_timestamp_ms_ptr(MakoNativeString *s) {
    bool ok = false;
    MakoUuid u = mako_ulid_parse(bridge_borrow_str(s), &ok);
    if (!ok) {
        /* Also accept uuid-string form used by tests. */
        u = mako_uuid_parse(bridge_borrow_str(s), &ok);
        if (!ok) return 0;
        /* UUID bytes: first 6 bytes of ULID time for v7; for ULID parse path above. */
        return mako_ulid_timestamp_ms(u);
    }
    return mako_ulid_timestamp_ms(u);
}

/* Reflect bag with explicit leaf capacity (schema registry may not know POD types). */
int64_t mako_native_reflect_value_new_n_ptr(MakoNativeString *schema, int64_t n) {
    MakoReflectValue *v = mako_reflect_value_new(bridge_borrow_str(schema));
    if (!v) return 0;
    if (n < 0) n = 0;
    if (n > 32) n = 32;
    if ((int)n > v->n) {
        for (int i = v->n; i < (int)n; i++) {
            v->values[i] = mako_str_from_cstr("");
        }
        v->n = (int)n;
    } else if ((int)n >= 0 && (int)n < v->n) {
        /* Keep schema size if larger; tests only need leaves. */
    }
    /* Always honor requested leaf count when schema is unknown (n_reg == 0). */
    if (v->n == 0 && n > 0) {
        v->n = (int)n;
        for (int i = 0; i < v->n; i++) v->values[i] = mako_str_from_cstr("");
    }
    /* If schema reported fewer fields than our POD leaf walk needs, grow. */
    if ((int)n > v->n) {
        for (int i = v->n; i < (int)n; i++) v->values[i] = mako_str_from_cstr("");
        v->n = (int)n;
    }
    return (int64_t)(intptr_t)v;
}

int64_t mako_native_toml_has_ptr(MakoNativeString *doc, MakoNativeString *key) {
    return mako_toml_has(bridge_borrow_str(doc), bridge_borrow_str(key));
}
MakoNativeString *mako_native_ulid_new(void) {
    return bridge_take_str(mako_ulid_string(mako_ulid_new()));
}
MakoNativeString *mako_native_ulid_string_ptr(MakoNativeString *s) {
    /* Uuid is stored as canonical string in native IR. */
    return bridge_take_str(mako_str_clone(bridge_borrow_str(s)));
}
MakoNativeString *mako_native_ulid_parse_ptr(MakoNativeString *s) {
    bool ok = false;
    MakoUuid u = mako_ulid_parse(bridge_borrow_str(s), &ok);
    if (!ok) return mako_native_string_literal_ptr("", 0);
    return bridge_take_str(mako_ulid_string(u));
}
MakoNativeString *mako_native_uuid_ns_dns(void) {
    return bridge_take_str(mako_uuid_string(mako_uuid_ns_dns()));
}
MakoNativeString *mako_native_uuid_ns_url(void) {
    return bridge_take_str(mako_uuid_string(mako_uuid_ns_url()));
}
MakoNativeString *mako_native_uuid_ns_oid(void) {
    return bridge_take_str(mako_uuid_string(mako_uuid_ns_oid()));
}
MakoNativeString *mako_native_uuid_ns_x500(void) {
    return bridge_take_str(mako_uuid_string(mako_uuid_ns_x500()));
}

int64_t mako_native_toml_get_int_in_ptr(MakoNativeString *doc, MakoNativeString *sec, MakoNativeString *key) {
    return mako_toml_get_int_in(bridge_borrow_str(doc), bridge_borrow_str(sec), bridge_borrow_str(key));
}
MakoNativeString *mako_native_toml_get_in_ptr(MakoNativeString *doc, MakoNativeString *sec, MakoNativeString *key) {
    return bridge_take_str(mako_toml_get_in(bridge_borrow_str(doc), bridge_borrow_str(sec), bridge_borrow_str(key)));
}
MakoNativeStringSlice *mako_native_toml_keys_ptr(MakoNativeString *doc) {
    MakoStrArray a = mako_toml_keys(bridge_borrow_str(doc));
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(a.len, a.len ? a.len : 1);
    for (size_t i = 0; i < a.len; i++) out->data[i] = bridge_take_str(a.data[i]);
    out->len = a.len;
    free(a.data);
    return out;
}
MakoNativeString *mako_native_toml_pair_ptr(MakoNativeString *k, MakoNativeString *v) {
    return bridge_take_str(mako_toml_pair(bridge_borrow_str(k), bridge_borrow_str(v)));
}
MakoNativeString *mako_native_toml_pair_int_ptr(MakoNativeString *k, int64_t v) {
    return bridge_take_str(mako_toml_pair_int(bridge_borrow_str(k), v));
}
MakoNativeString *mako_native_toml_pair_bool_ptr(MakoNativeString *k, int64_t v) {
    return bridge_take_str(mako_toml_pair_bool(bridge_borrow_str(k), v));
}
MakoNativeString *mako_native_toml_section_ptr(MakoNativeString *n) {
    return bridge_take_str(mako_toml_section(bridge_borrow_str(n)));
}
MakoNativeString *mako_native_toml_merge_ptr(MakoNativeString *a, MakoNativeString *b) {
    return bridge_take_str(mako_toml_merge(bridge_borrow_str(a), bridge_borrow_str(b)));
}
MakoNativeString *mako_native_uuid_v5_ptr(MakoNativeString *ns, MakoNativeString *name) {
    bool ok = false;
    MakoUuid nsu = mako_uuid_parse(bridge_borrow_str(ns), &ok);
    if (!ok) nsu = mako_uuid_ns_dns();
    return bridge_take_str(mako_uuid_string(mako_uuid_v5(nsu, bridge_borrow_str(name))));
}
MakoNativeString *mako_native_uuid_v3_ptr(MakoNativeString *ns, MakoNativeString *name) {
    /* No dedicated v3 helper; use v5 (SHA-1 name-based) as fallback. */
    bool ok = false;
    MakoUuid nsu = mako_uuid_parse(bridge_borrow_str(ns), &ok);
    if (!ok) nsu = mako_uuid_ns_dns();
    return bridge_take_str(mako_uuid_string(mako_uuid_v5(nsu, bridge_borrow_str(name))));
}
int64_t mako_native_alloc_high_bytes(void) {
    return mako_alloc_track_high_bytes();
}

/* Optimizer barrier for microbenches (must not be fully inlinable/DCE'd). */
int64_t mako_native_black_box_i64(int64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("" : "+r"(x) : : "memory");
#else
    volatile int64_t y = x;
    return y;
#endif
    return x;
}

/* f-string format specs — printf-compatible subset. */
static void mako_fmt_parse(const char *sp, size_t sl,
    int *left, int *sign_plus, int *sign_space, int *alt, int *zero,
    int *width, int *prec, char *kind) {
    *left = *sign_plus = *sign_space = *alt = *zero = 0;
    *width = 0; *prec = -1; *kind = 'd';
    size_t i = 0;
    while (i < sl) {
        char c = sp[i];
        if (c == '-') { *left = 1; i++; continue; }
        if (c == '+') { *sign_plus = 1; i++; continue; }
        if (c == ' ') { *sign_space = 1; i++; continue; }
        if (c == '#') { *alt = 1; i++; continue; }
        if (c == '0') { *zero = 1; i++; continue; }
        break;
    }
    while (i < sl && sp[i] >= '0' && sp[i] <= '9') {
        *width = *width * 10 + (sp[i] - '0');
        i++;
    }
    if (i < sl && sp[i] == '.') {
        i++; *prec = 0;
        while (i < sl && sp[i] >= '0' && sp[i] <= '9') {
            *prec = *prec * 10 + (sp[i] - '0');
            i++;
        }
    }
    if (i < sl) {
        char c = sp[i];
        if (c=='x'||c=='X'||c=='o'||c=='b'||c=='d'||c=='f'||c=='e'||c=='E'||c=='s')
            *kind = c;
    }
}

MakoNativeString *mako_native_format_spec_int_ptr(int64_t n, MakoNativeString *spec) {
    const char *sp = (spec && spec->data) ? spec->data : "";
    size_t sl = spec ? (spec->len & ~((size_t)1 << (sizeof(size_t)*8-1))) : 0;
    int left, sign_plus, sign_space, alt, zero, width, prec; char kind;
    mako_fmt_parse(sp, sl, &left, &sign_plus, &sign_space, &alt, &zero, &width, &prec, &kind);
    char body[128];
    if (kind == 'x' || kind == 'X') {
        snprintf(body, sizeof(body), (kind=='X') ? "%llX" : "%llx", (unsigned long long)n);
        if (alt) {
            char tmp[140];
            snprintf(tmp, sizeof(tmp), (kind=='X') ? "0X%s" : "0x%s", body);
            memcpy(body, tmp, strlen(tmp)+1);
        }
    } else if (kind == 'o') {
        snprintf(body, sizeof(body), "%llo", (unsigned long long)n);
        if (alt && body[0] != '0') {
            char tmp[140];
            snprintf(tmp, sizeof(tmp), "0%s", body);
            memcpy(body, tmp, strlen(tmp)+1);
        }
    } else if (kind == 'b') {
        unsigned long long u = (unsigned long long)n;
        char rev[80]; int r = 0;
        if (u == 0) rev[r++] = '0';
        while (u) { rev[r++] = (char)('0' + (u & 1ull)); u >>= 1; }
        int k = 0;
        if (alt) { body[k++] = '0'; body[k++] = 'b'; }
        while (r--) body[k++] = rev[r];
        body[k] = 0;
    } else {
        /* d with optional + / space */
        if (sign_plus && n >= 0) snprintf(body, sizeof(body), "+%lld", (long long)n);
        else if (sign_space && n >= 0) snprintf(body, sizeof(body), " %lld", (long long)n);
        else snprintf(body, sizeof(body), "%lld", (long long)n);
    }
    size_t bl = strlen(body);
    if (width <= (int)bl) return mako_native_string_literal_ptr(body, bl);
    char *outb = (char *)malloc((size_t)width + 1);
    if (!outb) abort();
    int pad = width - (int)bl;
    if (left) {
        memcpy(outb, body, bl);
        memset(outb + bl, ' ', (size_t)pad);
    } else if (zero && !left && kind != 'b') {
        /* zero pad after sign/prefix */
        int pref = 0;
        if (body[0]=='+'||body[0]=='-'||body[0]==' ') pref = 1;
        if (alt && (kind=='x'||kind=='X') && bl>=2) pref = 2;
        memcpy(outb, body, (size_t)pref);
        memset(outb + pref, '0', (size_t)pad);
        memcpy(outb + pref + pad, body + pref, bl - (size_t)pref);
    } else {
        memset(outb, ' ', (size_t)pad);
        memcpy(outb + pad, body, bl);
    }
    outb[width] = 0;
    MakoNativeString *ret = mako_native_string_literal_ptr(outb, (size_t)width);
    free(outb);
    return ret;
}

MakoNativeString *mako_native_format_spec_float_ptr(double x, MakoNativeString *spec) {
    const char *sp = (spec && spec->data) ? spec->data : "";
    size_t sl = spec ? (spec->len & ~((size_t)1 << (sizeof(size_t)*8-1))) : 0;
    int left, sign_plus, sign_space, alt, zero, width, prec; char kind;
    mako_fmt_parse(sp, sl, &left, &sign_plus, &sign_space, &alt, &zero, &width, &prec, &kind);
    if (prec < 0) prec = 6;
    if (kind != 'e' && kind != 'E' && kind != 'f') kind = 'f';
    char fmt[32];
    if (sign_plus) snprintf(fmt, sizeof(fmt), "%%+.%d%c", prec, kind);
    else snprintf(fmt, sizeof(fmt), "%%.%d%c", prec, kind);
    char body[128];
    snprintf(body, sizeof(body), fmt, x);
    size_t bl = strlen(body);
    if (width <= (int)bl) return mako_native_string_literal_ptr(body, bl);
    char *outb = (char *)malloc((size_t)width + 1);
    if (!outb) abort();
    int pad = width - (int)bl;
    if (left) { memcpy(outb, body, bl); memset(outb+bl, ' ', (size_t)pad); }
    else { memset(outb, ' ', (size_t)pad); memcpy(outb+pad, body, bl); }
    outb[width] = 0;
    MakoNativeString *ret = mako_native_string_literal_ptr(outb, (size_t)width);
    free(outb);
    return ret;
}

MakoNativeString *mako_native_format_spec_str_ptr(MakoNativeString *s, MakoNativeString *spec) {
    const char *sp = (spec && spec->data) ? spec->data : "";
    size_t sl = spec ? (spec->len & ~((size_t)1 << (sizeof(size_t)*8-1))) : 0;
    int left = 0, width = 0;
    size_t i = 0;
    if (i < sl && (sp[i]=='<' || sp[i]=='-')) { left = 1; i++; }
    if (i < sl && sp[i]=='>') { left = 0; i++; }
    while (i < sl && sp[i] >= '0' && sp[i] <= '9') {
        width = width * 10 + (sp[i] - '0');
        i++;
    }
    size_t n = s ? (s->len & ~((size_t)1 << (sizeof(size_t)*8-1))) : 0;
    const char *d = (s && s->data) ? s->data : "";
    if (width <= (int)n) return mako_native_string_literal_ptr(d, n);
    char *buf = (char *)malloc((size_t)width + 1);
    if (!buf) abort();
    int pad = width - (int)n;
    if (left) { memcpy(buf, d, n); memset(buf+n, ' ', (size_t)pad); }
    else { memset(buf, ' ', (size_t)pad); memcpy(buf+pad, d, n); }
    buf[width] = 0;
    MakoNativeString *out = mako_native_string_literal_ptr(buf, (size_t)width);
    free(buf);
    return out;
}

MakoNativeString *mako_native_float_to_string_ptr(double x) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", x);
    return mako_native_string_literal_ptr(buf, strlen(buf));
}


/* ---- language gap closures (native IR builtins) ---- */
MakoNativeString *mako_native_jpeg_encode_gray_baseline_ptr(
    int64_t w, int64_t h, MakoNativeString *pixels
) {
    return bridge_take_str(mako_jpeg_encode_gray_baseline(w, h, bridge_borrow_str(pixels)));
}
int64_t mako_native_jpeg_is_baseline_huff_ptr(MakoNativeString *s) {
    return (int64_t)mako_jpeg_is_baseline_huff(bridge_borrow_str(s));
}
int64_t mako_native_reflect_value_from_2_int_ptr(
    MakoNativeString *schema, int64_t a, int64_t b
) {
    return (int64_t)(intptr_t)mako_reflect_value_from_2_int(
        bridge_borrow_str(schema), a, b);
}
MakoNativeString *mako_native_alloc_report_json_ptr(void) {
    return bridge_take_str(mako_alloc_track_report_json());
}

/* proxy edge / pool — heap result handles as Opaque (i64) */
int64_t mako_native_http_proxy_raw_ptr(
    int64_t client_fd, int64_t backend_fd, MakoNativeString *raw, int64_t timeout_ms
) {
    MakoProxyIoResult *h = (MakoProxyIoResult *)malloc(sizeof(*h));
    if (!h) abort();
    *h = mako_http_proxy_raw(
        client_fd, backend_fd, bridge_borrow_str(raw), timeout_ms);
    return (int64_t)(intptr_t)h;
}
int64_t mako_native_proxy_io_ok(int64_t h) {
    MakoProxyIoResult *r = (MakoProxyIoResult *)(intptr_t)h;
    return r ? r->ok : 0;
}
int64_t mako_native_proxy_io_bytes_written(int64_t h) {
    MakoProxyIoResult *r = (MakoProxyIoResult *)(intptr_t)h;
    return r ? r->bytes_written : 0;
}
int64_t mako_native_proxy_io_bytes_read(int64_t h) {
    MakoProxyIoResult *r = (MakoProxyIoResult *)(intptr_t)h;
    return r ? r->bytes_read : 0;
}
int64_t mako_native_http_forward_full_ptr(
    MakoNativeString *host, int64_t port,
    MakoNativeString *method, MakoNativeString *path,
    MakoNativeString *headers, MakoNativeString *body, int64_t timeout_ms
) {
    MakoHttpForwardResult *h = (MakoHttpForwardResult *)malloc(sizeof(*h));
    if (!h) abort();
    *h = mako_http_forward_full(
        bridge_borrow_str(host), port,
        bridge_borrow_str(method), bridge_borrow_str(path),
        bridge_borrow_str(headers), bridge_borrow_str(body), timeout_ms);
    return (int64_t)(intptr_t)h;
}
int64_t mako_native_http_forward_ok(int64_t h) {
    MakoHttpForwardResult *r = (MakoHttpForwardResult *)(intptr_t)h;
    return r ? r->ok : 0;
}
int64_t mako_native_http_forward_status(int64_t h) {
    MakoHttpForwardResult *r = (MakoHttpForwardResult *)(intptr_t)h;
    return r ? r->status : 0;
}
int64_t mako_native_http_forward_body_len(int64_t h) {
    MakoHttpForwardResult *r = (MakoHttpForwardResult *)(intptr_t)h;
    return r ? r->body_len : 0;
}
int64_t mako_native_http_forward_total_bytes(int64_t h) {
    MakoHttpForwardResult *r = (MakoHttpForwardResult *)(intptr_t)h;
    return r ? r->total_bytes : 0;
}

int64_t mako_native_http_forward_fd_ptr(
    int64_t fd,
    MakoNativeString *method, MakoNativeString *path, MakoNativeString *host,
    MakoNativeString *headers, MakoNativeString *body, int64_t timeout_ms
) {
    MakoHttpForwardResult *h = (MakoHttpForwardResult *)malloc(sizeof(*h));
    if (!h) abort();
    *h = mako_http_forward_fd(
        fd,
        bridge_borrow_str(method), bridge_borrow_str(path), bridge_borrow_str(host),
        bridge_borrow_str(headers), bridge_borrow_str(body), timeout_ms);
    return (int64_t)(intptr_t)h;
}
int64_t mako_native_tls_accept_start(int64_t ctx, int64_t fd) {
    return (int64_t)(intptr_t)mako_tls_accept_start((void *)(intptr_t)ctx, fd);
}

/* ---- missing bridge symbols (native IR builtins) -------------------------- */

int64_t mako_native_hot_reload_note_swap(void) {
    return mako_hot_reload_note_swap();
}

int64_t mako_native_http2_stream_priority_weight(int64_t stream) {
    return mako_http2_stream_priority_weight(stream);
}

int64_t mako_native_http2_stream_state(void) {
    return mako_http2_stream_state();
}

MakoNativeString *mako_native_quic_ack_frame_ptr(int64_t largest, int64_t delay, int64_t first_range) {
    return bridge_take_str(mako_quic_ack_frame(largest, delay, first_range));
}

int64_t mako_native_quic_vn_version_count_ptr(MakoNativeString *s) {
    return mako_quic_vn_version_count(bridge_borrow_str(s));
}

MakoNativeString *mako_native_sip_uri_user_ptr(MakoNativeString *uri) {
    return bridge_take_str(mako_sip_uri_user(bridge_borrow_str(uri)));
}

/* slog entry points (state/helpers defined near top of file). */
void mako_native_slog_debug_ptr(MakoNativeString *msg) {
    if (mako_native_slog_min_level <= 0) mako_native_slog_emit("debug", msg);
}
void mako_native_slog_info_ptr(MakoNativeString *msg) {
    if (mako_native_slog_min_level <= 1) mako_native_slog_emit("info", msg);
}
void mako_native_slog_warn_ptr(MakoNativeString *msg) {
    if (mako_native_slog_min_level <= 2) mako_native_slog_emit("warn", msg);
}
void mako_native_slog_error_ptr(MakoNativeString *msg) {
    if (mako_native_slog_min_level <= 3) mako_native_slog_emit("error", msg);
}

int64_t mako_native_slog_set_level_ptr(MakoNativeString *level) {
    mako_native_slog_min_level = mako_native_slog_level_num(level);
    return 0;
}

int64_t mako_native_slog_get_level(void) {
    return (int64_t)mako_native_slog_min_level;
}

void mako_native_slog_set_json(int64_t on) {
    mako_native_slog_json_mode = on ? 1 : 0;
}

void mako_native_slog_with_ptr(
    MakoNativeString *level, MakoNativeString *msg, MakoNativeString *k, MakoNativeString *v
) {
    mako_native_slog_emit_fields(level, msg, k, v, NULL, NULL, NULL, NULL, 0);
}

int64_t mako_native_slog_with_redacted_ptr(
    MakoNativeString *level, MakoNativeString *msg, MakoNativeString *key
) {
    MakoNativeString red = { .data = "[REDACTED]", .len = 10 };
    mako_native_slog_emit_fields(level, msg, key, &red, NULL, NULL, NULL, NULL, 0);
    return 0;
}

int64_t mako_native_syscall_access_ptr(MakoNativeString *path, int64_t mode) {
    return mako_syscall_access(bridge_borrow_str(path), mode);
}

int64_t mako_native_time_after(int64_t a, int64_t b) {
    return mako_time_after(a, b);
}

int64_t mako_native_tls_make_self_signed_ptr(
    MakoNativeString *cert, MakoNativeString *key, MakoNativeString *cn, int64_t days
) {
    return mako_tls_make_self_signed(
        bridge_borrow_str(cert), bridge_borrow_str(key), bridge_borrow_str(cn), days
    );
}

MakoNativeString *mako_native_tls_server_application_traffic_secret_hex_ptr(
    MakoNativeString *master, MakoNativeString *transcript
) {
    return bridge_take_str(mako_tls_server_application_traffic_secret_hex(
        bridge_borrow_str(master), bridge_borrow_str(transcript)
    ));
}

int64_t mako_native_unicode_is_graphic(int64_t r) {
    return mako_unicode_is_graphic(r);
}
