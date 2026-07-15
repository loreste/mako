/* Domain-track seeds: B-tree, LSM, MVCC, multiplayer rollback, graphics soft.
 * No SIPREC/WebRTC. Portable C; efficiency-minded small limits.
 */
#ifndef MAKO_DOMAIN_H
#define MAKO_DOMAIN_H

#include "mako_dio.h"
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- B-tree seed (in-memory ordered map, fanout 8) ---- */
#define MAKO_BTREE_ORDER 8
#define MAKO_BTREE_MAX_KEYS (MAKO_BTREE_ORDER - 1)

typedef struct MakoBNode {
    int leaf;
    int n;
    int64_t keys[MAKO_BTREE_MAX_KEYS];
    int64_t vals[MAKO_BTREE_MAX_KEYS];
    struct MakoBNode *kids[MAKO_BTREE_ORDER];
} MakoBNode;

typedef struct {
    MakoBNode *root;
    int64_t count;
} MakoBTree;

static inline MakoBNode *mako_bnode_new(int leaf) {
    MakoBNode *n = (MakoBNode *)calloc(1, sizeof(MakoBNode));
    if (n) n->leaf = leaf;
    return n;
}

static inline MakoBTree *mako_btree_new(void) {
    MakoBTree *t = (MakoBTree *)calloc(1, sizeof(MakoBTree));
    if (!t) return NULL;
    t->root = mako_bnode_new(1);
    if (!t->root) {
        free(t);
        return NULL;
    }
    return t;
}

static inline int64_t mako_btree_get_node(MakoBNode *n, int64_t key) {
    if (!n) return -1;
    int i = 0;
    while (i < n->n && key > n->keys[i]) i++;
    if (i < n->n && key == n->keys[i]) return n->vals[i];
    if (n->leaf) return -1;
    return mako_btree_get_node(n->kids[i], key);
}

static inline int64_t mako_btree_get(MakoBTree *t, int64_t key) {
    return t ? mako_btree_get_node(t->root, key) : -1;
}

static inline void mako_btree_split_child(MakoBNode *parent, int i) {
    MakoBNode *y = parent->kids[i];
    MakoBNode *z = mako_bnode_new(y->leaf);
    int mid = MAKO_BTREE_MAX_KEYS / 2;
    z->n = MAKO_BTREE_MAX_KEYS - mid - 1;
    for (int j = 0; j < z->n; j++) {
        z->keys[j] = y->keys[j + mid + 1];
        z->vals[j] = y->vals[j + mid + 1];
    }
    if (!y->leaf) {
        for (int j = 0; j <= z->n; j++) z->kids[j] = y->kids[j + mid + 1];
    }
    y->n = mid;
    for (int j = parent->n; j >= i + 1; j--) parent->kids[j + 1] = parent->kids[j];
    parent->kids[i + 1] = z;
    for (int j = parent->n - 1; j >= i; j--) {
        parent->keys[j + 1] = parent->keys[j];
        parent->vals[j + 1] = parent->vals[j];
    }
    parent->keys[i] = y->keys[mid];
    parent->vals[i] = y->vals[mid];
    parent->n++;
}

static inline void mako_btree_insert_nonfull(MakoBNode *n, int64_t key, int64_t val) {
    int i = n->n - 1;
    if (n->leaf) {
        while (i >= 0 && key < n->keys[i]) {
            n->keys[i + 1] = n->keys[i];
            n->vals[i + 1] = n->vals[i];
            i--;
        }
        if (i >= 0 && n->keys[i] == key) {
            n->vals[i] = val;
            return;
        }
        n->keys[i + 1] = key;
        n->vals[i + 1] = val;
        n->n++;
    } else {
        while (i >= 0 && key < n->keys[i]) i--;
        i++;
        if (i < n->n && n->keys[i] == key) {
            n->vals[i] = val;
            return;
        }
        if (n->kids[i]->n == MAKO_BTREE_MAX_KEYS) {
            mako_btree_split_child(n, i);
            if (key > n->keys[i]) i++;
            else if (key == n->keys[i]) {
                n->vals[i] = val;
                return;
            }
        }
        mako_btree_insert_nonfull(n->kids[i], key, val);
    }
}

static inline int64_t mako_btree_put(MakoBTree *t, int64_t key, int64_t val) {
    if (!t || !t->root) return -1;
    MakoBNode *r = t->root;
    if (r->n == MAKO_BTREE_MAX_KEYS) {
        MakoBNode *s = mako_bnode_new(0);
        t->root = s;
        s->kids[0] = r;
        mako_btree_split_child(s, 0);
        mako_btree_insert_nonfull(s, key, val);
    } else {
        mako_btree_insert_nonfull(r, key, val);
    }
    t->count++;
    return 0;
}

static inline int64_t mako_btree_len(MakoBTree *t) {
    return t ? t->count : 0;
}

static inline void mako_bnode_free(MakoBNode *n) {
    if (!n) return;
    if (!n->leaf) {
        for (int i = 0; i <= n->n; i++) mako_bnode_free(n->kids[i]);
    }
    free(n);
}

static inline int64_t mako_btree_free(MakoBTree *t) {
    if (!t) return 0;
    mako_bnode_free(t->root);
    free(t);
    return 0;
}

/* ---- LSM seed: memtable (hash) + sorted run file ---- */
typedef struct {
    MakoHIndex *mem;
    MakoWal *run; /* sorted-ish log of flushes; not fully sorted SST */
    int64_t flushes;
} MakoLsm;

static inline MakoLsm *mako_lsm_new(int64_t mem_cap) {
    MakoLsm *l = (MakoLsm *)calloc(1, sizeof(MakoLsm));
    if (!l) return NULL;
    l->mem = mako_hindex_new(mem_cap);
    if (!l->mem) {
        free(l);
        return NULL;
    }
    return l;
}

static inline int64_t mako_lsm_attach_run(MakoLsm *l, MakoWal *w) {
    if (!l) return -1;
    l->run = w;
    return 0;
}

static inline int64_t mako_lsm_put(MakoLsm *l, int64_t key, int64_t val) {
    return l ? mako_hindex_put(l->mem, key, val) : -1;
}

static inline int64_t mako_lsm_get(MakoLsm *l, int64_t key) {
    if (!l) return -1;
    int64_t v = mako_hindex_get(l->mem, key);
    if (v != -1) return v;
    /* Scan run WAL from end (last write wins) — O(n) seed */
    if (!l->run) return -1;
    int64_t off = 0;
    int64_t found = -1;
    for (;;) {
        MakoString rec = mako_wal_read_at(l->run, off);
        if (rec.len == 0) break;
        /* format "P,key,val" */
        if (rec.data && rec.len > 2 && rec.data[0] == 'P') {
            long long k = 0, val = 0;
            if (sscanf(rec.data, "P,%lld,%lld", &k, &val) == 2 && (int64_t)k == key) {
                found = (int64_t)val;
            }
        }
        off = mako_wal_next_off();
        mako_str_free(rec);
        if (off < 0) break;
    }
    return found;
}

static inline int64_t mako_lsm_flush(MakoLsm *l) {
    if (!l || !l->mem || !l->run) return -1;
    /* Dump all live entries as P,key,val */
    for (size_t i = 0; i < l->mem->cap; i++) {
        int64_t k = l->mem->keys[i];
        if (k == MAKO_HINDEX_EMPTY || k == MAKO_HINDEX_TOMB) continue;
        char buf[64];
        int n = snprintf(
            buf, sizeof(buf), "P,%lld,%lld", (long long)k, (long long)l->mem->vals[i]
        );
        if (n > 0) {
            MakoString rec = {buf, (size_t)n};
            if (mako_wal_append(l->run, rec) != 0) return -1;
        }
    }
    if (mako_wal_sync(l->run) != 0) return -1;
    /* clear memtable */
    for (size_t i = 0; i < l->mem->cap; i++) l->mem->keys[i] = MAKO_HINDEX_EMPTY;
    l->mem->len = 0;
    l->flushes++;
    return 0;
}

static inline int64_t mako_lsm_flushes(MakoLsm *l) {
    return l ? l->flushes : 0;
}

static inline int64_t mako_lsm_free(MakoLsm *l) {
    if (!l) return 0;
    mako_hindex_free(l->mem);
    free(l);
    return 0;
}

/* ---- MVCC seed: multi-version map (key, ts) -> val ---- */
#define MAKO_MVCC_MAX 512
typedef struct {
    int64_t key;
    int64_t ts;
    int64_t val;
    int live;
} MakoMvccVer;

typedef struct {
    MakoMvccVer vers[MAKO_MVCC_MAX];
    int64_t clock;
    int n;
} MakoMvcc;

static inline MakoMvcc *mako_mvcc_new(void) {
    MakoMvcc *m = (MakoMvcc *)calloc(1, sizeof(MakoMvcc));
    /* clock starts at 0; puts assign ++clock so first write is ts=1 */
    return m;
}

static inline int64_t mako_mvcc_begin(MakoMvcc *m) {
    if (!m) return -1;
    return m->clock; /* snapshot: versions with ts <= clock */
}

static inline int64_t mako_mvcc_put(MakoMvcc *m, int64_t key, int64_t val) {
    if (!m || m->n >= MAKO_MVCC_MAX) return -1;
    int64_t ts = ++m->clock;
    m->vers[m->n].key = key;
    m->vers[m->n].ts = ts;
    m->vers[m->n].val = val;
    m->vers[m->n].live = 1;
    m->n++;
    return ts;
}

/* Latest version with ts <= read_ts */
static inline int64_t mako_mvcc_get(MakoMvcc *m, int64_t read_ts, int64_t key) {
    if (!m) return -1;
    int64_t best_ts = -1;
    int64_t best_val = -1;
    for (int i = 0; i < m->n; i++) {
        if (!m->vers[i].live || m->vers[i].key != key) continue;
        if (m->vers[i].ts <= read_ts && m->vers[i].ts >= best_ts) {
            best_ts = m->vers[i].ts;
            best_val = m->vers[i].val;
        }
    }
    return best_val;
}

static inline int64_t mako_mvcc_versions(MakoMvcc *m) {
    return m ? m->n : 0;
}

static inline int64_t mako_mvcc_free(MakoMvcc *m) {
    free(m);
    return 0;
}

/* ---- Multiplayer rollback ring ---- */
#define MAKO_RB_MAX 64
typedef struct {
    int64_t frames[MAKO_RB_MAX][8]; /* up to 8 slots per frame */
    int64_t tick[MAKO_RB_MAX];
    int head;
    int count;
    int slots;
} MakoRollback;

static inline MakoRollback *mako_rollback_new(int64_t slots) {
    MakoRollback *r = (MakoRollback *)calloc(1, sizeof(MakoRollback));
    if (!r) return NULL;
    r->slots = (slots > 0 && slots <= 8) ? (int)slots : 4;
    return r;
}

static inline int64_t mako_rollback_push(MakoRollback *r, int64_t tick, MakoString snap) {
    if (!r) return -1;
    int idx = r->head;
    r->tick[idx] = tick;
    int n = (int)mako_snap_count(snap);
    if (n > r->slots) n = r->slots;
    for (int i = 0; i < r->slots; i++) {
        r->frames[idx][i] = (i < n) ? mako_snap_get(snap, i) : 0;
    }
    r->head = (r->head + 1) % MAKO_RB_MAX;
    if (r->count < MAKO_RB_MAX) r->count++;
    return 0;
}

static inline int64_t mako_rollback_find(MakoRollback *r, int64_t tick) {
    if (!r) return -1;
    for (int i = 0; i < r->count; i++) {
        int idx = (r->head - 1 - i + MAKO_RB_MAX * 2) % MAKO_RB_MAX;
        if (r->tick[idx] == tick) return idx;
    }
    return -1;
}

static inline int64_t mako_rollback_get(MakoRollback *r, int64_t tick, int64_t slot) {
    int idx = (int)mako_rollback_find(r, tick);
    if (idx < 0 || slot < 0 || slot >= r->slots) return 0;
    return r->frames[idx][slot];
}

static inline int64_t mako_rollback_restore_slot0(MakoRollback *r, int64_t tick) {
    return mako_rollback_get(r, tick, 0);
}

static inline int64_t mako_rollback_len(MakoRollback *r) {
    return r ? r->count : 0;
}

static inline int64_t mako_rollback_free(MakoRollback *r) {
    free(r);
    return 0;
}

/* ---- Graphics / audio / physics soft seeds (handles only) ---- */
typedef struct {
    int64_t w, h;
    int open;
    char title[64];
} MakoGfxWindow;

static inline MakoGfxWindow *mako_gfx_window_open(int64_t w, int64_t h, MakoString title) {
    MakoGfxWindow *win = (MakoGfxWindow *)calloc(1, sizeof(MakoGfxWindow));
    if (!win) return NULL;
    win->w = w > 0 ? w : 640;
    win->h = h > 0 ? h : 480;
    win->open = 1;
    size_t n = title.len < 63 ? title.len : 63;
    if (title.data && n) memcpy(win->title, title.data, n);
    win->title[n] = 0;
    return win;
}

static inline int64_t mako_gfx_window_width(MakoGfxWindow *w) {
    return w ? w->w : 0;
}

static inline int64_t mako_gfx_window_height(MakoGfxWindow *w) {
    return w ? w->h : 0;
}

static inline int64_t mako_gfx_window_close(MakoGfxWindow *w) {
    if (!w) return 0;
    w->open = 0;
    free(w);
    return 0;
}

/* Shader "compile" seed: hash source length as id */
static inline int64_t mako_gfx_shader_compile(MakoString src) {
    if (!src.data) return -1;
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < src.len; i++) {
        h ^= (unsigned char)src.data[i];
        h *= 1099511628211ULL;
    }
    return (int64_t)(h & 0x7fffffffLL);
}

/* Asset load seed: returns size or -1 */
static inline int64_t mako_gfx_asset_size(MakoString path) {
    return mako_path_file_size(path);
}

/* Audio mix seed: clamp sum of two int samples */
static inline int64_t mako_audio_mix(int64_t a, int64_t b) {
    int64_t s = a + b;
    if (s > 32767) return 32767;
    if (s < -32768) return -32768;
    return s;
}

/* Physics integrate seed: x' = x + v*dt; v' = v + a*dt (fixed-point milli) */
static inline int64_t mako_physics_step_x(int64_t x, int64_t v, int64_t dt_ms) {
    return x + (v * dt_ms) / 1000;
}

static inline int64_t mako_physics_step_v(int64_t v, int64_t a, int64_t dt_ms) {
    return v + (a * dt_ms) / 1000;
}

/* ---- GPU AI depth host seeds (work without OpenCL) ---- */
/* RoPE: rotate pairs of f32 in a host buffer encoded as float string? Use int deg seed.
 * rope_rotate(x, y, theta_milli) -> pack into high/low of int64 for test simplicity:
 * returns x' as int scaled.
 */
static inline int64_t mako_ai_rope_cos(int64_t theta_milli) {
    double t = (double)theta_milli / 1000.0;
    return (int64_t)(cos(t) * 1000.0);
}

static inline int64_t mako_ai_rope_sin(int64_t theta_milli) {
    double t = (double)theta_milli / 1000.0;
    return (int64_t)(sin(t) * 1000.0);
}

/* Apply 2D rotation: (x,y) in milli-units */
static inline int64_t mako_ai_rope_apply_x(int64_t x, int64_t y, int64_t theta_milli) {
    double t = (double)theta_milli / 1000.0;
    double c = cos(t), s = sin(t);
    return (int64_t)((double)x * c - (double)y * s);
}

static inline int64_t mako_ai_rope_apply_y(int64_t x, int64_t y, int64_t theta_milli) {
    double t = (double)theta_milli / 1000.0;
    double c = cos(t), s = sin(t);
    return (int64_t)((double)x * s + (double)y * c);
}

/* KV-cache seed: ring of int64 tokens */
#define MAKO_KV_MAX 128
typedef struct {
    int64_t k[MAKO_KV_MAX];
    int64_t v[MAKO_KV_MAX];
    int len;
    int cap;
} MakoKvCache;

static inline MakoKvCache *mako_kv_cache_new(int64_t cap) {
    MakoKvCache *c = (MakoKvCache *)calloc(1, sizeof(MakoKvCache));
    if (!c) return NULL;
    c->cap = (cap > 0 && cap <= MAKO_KV_MAX) ? (int)cap : MAKO_KV_MAX;
    return c;
}

static inline int64_t mako_kv_cache_append(MakoKvCache *c, int64_t k, int64_t v) {
    if (!c || c->len >= c->cap) return -1;
    c->k[c->len] = k;
    c->v[c->len] = v;
    c->len++;
    return c->len;
}

static inline int64_t mako_kv_cache_get_k(MakoKvCache *c, int64_t i) {
    if (!c || i < 0 || i >= c->len) return 0;
    return c->k[i];
}

static inline int64_t mako_kv_cache_get_v(MakoKvCache *c, int64_t i) {
    if (!c || i < 0 || i >= c->len) return 0;
    return c->v[i];
}

static inline int64_t mako_kv_cache_len(MakoKvCache *c) {
    return c ? c->len : 0;
}

static inline int64_t mako_kv_cache_free(MakoKvCache *c) {
    free(c);
    return 0;
}

/* Batched GEMM seed (host): C = A @ B for small int matrices encoded as flat int64.
 * gemm_i64(m,n,k, a00,a01,..., scaled) — only for tiny 2x2 tests:
 * gemm2x2(a00,a01,a10,a11, b00,b01,b10,b11) returns c00 in return, others via tls.
 */
static __thread int64_t mako_gemm_c01, mako_gemm_c10, mako_gemm_c11;

static inline int64_t mako_gemm2x2(
    int64_t a00, int64_t a01, int64_t a10, int64_t a11,
    int64_t b00, int64_t b01, int64_t b10, int64_t b11
) {
    int64_t c00 = a00 * b00 + a01 * b10;
    mako_gemm_c01 = a00 * b01 + a01 * b11;
    mako_gemm_c10 = a10 * b00 + a11 * b10;
    mako_gemm_c11 = a10 * b01 + a11 * b11;
    return c00;
}

static inline int64_t mako_gemm_c01_get(void) { return mako_gemm_c01; }
static inline int64_t mako_gemm_c10_get(void) { return mako_gemm_c10; }
static inline int64_t mako_gemm_c11_get(void) { return mako_gemm_c11; }

/* f16 bits seed: round-trip float bits as half (simple truncate) */
static inline int64_t mako_f32_to_f16_bits(int64_t f32_bits) {
    uint32_t f = (uint32_t)f32_bits;
    uint32_t sign = (f >> 16) & 0x8000;
    int32_t exp = (int32_t)((f >> 23) & 0xff) - 127 + 15;
    uint32_t man = f & 0x7fffff;
    if (exp <= 0) return (int64_t)sign;
    if (exp >= 31) return (int64_t)(sign | 0x7c00);
    return (int64_t)(sign | ((uint32_t)exp << 10) | (man >> 13));
}

/* ---- Debugger source frame seed ---- */
static __thread char mako_debug_file[256];
static __thread int64_t mako_debug_line;

static inline int64_t mako_debug_set_loc(MakoString file, int64_t line) {
    size_t n = file.len < 255 ? file.len : 255;
    if (file.data && n) memcpy(mako_debug_file, file.data, n);
    mako_debug_file[n] = 0;
    mako_debug_line = line;
    return 1;
}

static inline MakoString mako_debug_file_get(void) {
    return mako_str_from_cstr(mako_debug_file[0] ? mako_debug_file : "");
}

static inline int64_t mako_debug_line_get(void) {
    return mako_debug_line;
}

static inline MakoString mako_debug_frame_json(void) {
    char buf[384];
    int n = snprintf(
        buf, sizeof(buf),
        "{\"schema\":\"mako.debug_frame.v1\",\"file\":\"%s\",\"line\":%" PRId64 "}",
        mako_debug_file[0] ? mako_debug_file : "",
        mako_debug_line
    );
    if (n < 0) return mako_str_from_cstr("{}");
    return mako_str_from_cstr(buf);
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_DOMAIN_H */
