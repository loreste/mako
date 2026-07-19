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

/* ---- SST shell (full impl later): needed complete for LSM compact ---- */
typedef struct MakoSst {
    int64_t *keys;
    int64_t *vals;
    int64_t n;
    char path[512];
} MakoSst;

static inline int64_t mako_sst_get(MakoSst *s, int64_t key);
static inline int64_t mako_sst_free(MakoSst *s);
static inline MakoSst *mako_sst_build(MakoString path, int64_t *keys, int64_t *vals, int64_t n);

/* ---- LSM seed: memtable + L0 run + multi-level SST (L1..L3) ---- */
#define MAKO_LSM_SST_LEVELS 3
#define MAKO_LSM_COMPACT_MAX 4096

typedef struct {
    MakoHIndex *mem;
    MakoWal *run; /* L0: append-only flush log */
    MakoSst *levels[MAKO_LSM_SST_LEVELS]; /* [0]=L1 (newest SST) … [2]=L3 */
    int64_t flushes;
    int64_t compactions;
} MakoLsm;

static inline void mako_lsm_kv_put(
    int64_t *keys, int64_t *vals, int *n, int maxn, int64_t k, int64_t v
) {
    for (int i = 0; i < *n; i++) {
        if (keys[i] == k) {
            vals[i] = v;
            return;
        }
    }
    if (*n < maxn) {
        keys[*n] = k;
        vals[*n] = v;
        (*n)++;
    }
}

static inline void mako_lsm_seed_from_sst(
    MakoSst *s, int64_t *keys, int64_t *vals, int *n, int maxn
) {
    if (!s || !s->keys) return;
    for (int64_t i = 0; i < s->n && *n < maxn; i++) {
        mako_lsm_kv_put(keys, vals, n, maxn, s->keys[i], s->vals[i]);
    }
}

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
    /* L0 run: last write wins */
    if (l->run) {
        int64_t off = 0;
        int64_t found = -1;
        for (;;) {
            MakoString rec = mako_wal_read_at(l->run, off);
            if (rec.len == 0) break;
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
        if (found != -1) return found;
    }
    /* SST levels: lower index is newer */
    for (int i = 0; i < MAKO_LSM_SST_LEVELS; i++) {
        if (l->levels[i]) {
            int64_t g = mako_sst_get(l->levels[i], key);
            if (g != -1) return g;
        }
    }
    return -1;
}

static inline int64_t mako_lsm_flush(MakoLsm *l) {
    if (!l || !l->mem || !l->run) return -1;
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
    for (size_t i = 0; i < l->mem->cap; i++) l->mem->keys[i] = MAKO_HINDEX_EMPTY;
    l->mem->len = 0;
    l->flushes++;
    return 0;
}

/* Compact L0 run (+ optional L1 SST) into a new L1 SST; truncate run. */
static inline int64_t mako_lsm_compact(MakoLsm *l, MakoString sst_path) {
    if (!l || !l->run || !sst_path.data) return -1;
    int64_t keys[MAKO_LSM_COMPACT_MAX];
    int64_t vals[MAKO_LSM_COMPACT_MAX];
    int n = 0;
    mako_lsm_seed_from_sst(l->levels[0], keys, vals, &n, MAKO_LSM_COMPACT_MAX);
    int64_t off = 0;
    for (;;) {
        MakoString rec = mako_wal_read_at(l->run, off);
        if (rec.len == 0) break;
        if (rec.data && rec.len > 2 && rec.data[0] == 'P') {
            long long k = 0, val = 0;
            if (sscanf(rec.data, "P,%lld,%lld", &k, &val) == 2) {
                mako_lsm_kv_put(keys, vals, &n, MAKO_LSM_COMPACT_MAX, (int64_t)k, (int64_t)val);
            }
        }
        off = mako_wal_next_off();
        mako_str_free(rec);
        if (off < 0) break;
    }
    MakoSst *ns = mako_sst_build(sst_path, keys, vals, n);
    if (!ns) return -1;
    if (l->levels[0]) mako_sst_free(l->levels[0]);
    l->levels[0] = ns;
#if !defined(_WIN32)
    if (l->run && l->run->fd >= 0) {
        if (ftruncate(l->run->fd, 0) != 0) return -1;
        lseek(l->run->fd, 0, SEEK_SET);
    }
#endif
    l->compactions++;
    return n;
}

/* Promote / merge newest SST level into the next deeper level (L1→L2→L3). */
static inline int64_t mako_lsm_compact_down(MakoLsm *l, MakoString sst_path) {
    if (!l || !sst_path.data) return -1;
    int src = -1;
    for (int i = 0; i < MAKO_LSM_SST_LEVELS - 1; i++) {
        if (l->levels[i]) {
            src = i;
            break;
        }
    }
    if (src < 0) return -1;
    int dst = src + 1;
    int64_t keys[MAKO_LSM_COMPACT_MAX];
    int64_t vals[MAKO_LSM_COMPACT_MAX];
    int n = 0;
    /* Deeper level first (older), then overlay newer src. */
    mako_lsm_seed_from_sst(l->levels[dst], keys, vals, &n, MAKO_LSM_COMPACT_MAX);
    mako_lsm_seed_from_sst(l->levels[src], keys, vals, &n, MAKO_LSM_COMPACT_MAX);
    MakoSst *ns = mako_sst_build(sst_path, keys, vals, n);
    if (!ns) return -1;
    if (l->levels[dst]) mako_sst_free(l->levels[dst]);
    if (l->levels[src]) mako_sst_free(l->levels[src]);
    l->levels[src] = NULL;
    l->levels[dst] = ns;
    l->compactions++;
    return n;
}

/* Number of non-empty SST levels (0..3). */
static inline int64_t mako_lsm_sst_levels(MakoLsm *l) {
    if (!l) return 0;
    int64_t c = 0;
    for (int i = 0; i < MAKO_LSM_SST_LEVELS; i++)
        if (l->levels[i]) c++;
    return c;
}

/* Key count at SST level (1=L1 … 3=L3); 0 if empty/invalid. */
static inline int64_t mako_lsm_level_len(MakoLsm *l, int64_t level) {
    if (!l || level < 1 || level > MAKO_LSM_SST_LEVELS) return 0;
    MakoSst *s = l->levels[(int)level - 1];
    return s ? s->n : 0;
}

static inline int64_t mako_lsm_flushes(MakoLsm *l) {
    return l ? l->flushes : 0;
}

static inline int64_t mako_lsm_compactions(MakoLsm *l) {
    return l ? l->compactions : 0;
}

static inline int64_t mako_lsm_free(MakoLsm *l) {
    if (!l) return 0;
    mako_hindex_free(l->mem);
    for (int i = 0; i < MAKO_LSM_SST_LEVELS; i++) {
        if (l->levels[i]) mako_sst_free(l->levels[i]);
    }
    free(l);
    return 0;
}

/* ---- Crash recovery: replay WAL into a store ---- */
static inline int64_t mako_store_recover_wal(MakoStore *s, MakoWal *w) {
    if (!s || !w) return -1;
    int64_t off = 0;
    int64_t applied = 0;
    for (;;) {
        MakoString rec = mako_wal_read_at(w, off);
        if (rec.len == 0) break;
        if (rec.data && rec.len > 2) {
            if (rec.data[0] == 'P') {
                long long k = 0, v = 0;
                if (sscanf(rec.data, "P,%lld,%lld", &k, &v) == 2) {
                    if (mako_store_put(s, (int64_t)k, (int64_t)v) == 0) applied++;
                }
            } else if (rec.data[0] == 'D') {
                long long k = 0;
                if (sscanf(rec.data, "D,%lld", &k) == 1) {
                    if (mako_store_del(s, (int64_t)k) == 0) applied++;
                }
            }
        }
        off = mako_wal_next_off();
        mako_str_free(rec);
        if (off < 0) break;
    }
    return applied;
}

/* ---- Hot reload seed: file mtime watch ---- */
static inline int64_t mako_file_mtime_ns(MakoString path) {
    if (!path.data || path.len == 0 || path.len >= 4096) return -1;
#if defined(_WIN32)
    return -1;
#else
    char buf[4096];
    memcpy(buf, path.data, path.len);
    buf[path.len] = 0;
    struct stat st;
    if (stat(buf, &st) != 0) return -1;
    /* _POSIX_C_SOURCE (set by mako_rt.h) exposes st_mtime + st_mtimensec on
     * Darwin; Linux POSIX.1-2008 uses st_mtim. */
#if defined(__linux__)
    return (int64_t)st.st_mtim.tv_sec * 1000000000LL + (int64_t)st.st_mtim.tv_nsec;
#elif defined(__APPLE__)
    return (int64_t)st.st_mtime * 1000000000LL + (int64_t)st.st_mtimensec;
#else
    return (int64_t)st.st_mtime * 1000000000LL;
#endif
#endif
}

#define MAKO_HOT_WATCH_MAX 8
typedef struct {
    char path[512];
    int64_t mtime_ns;
    int used;
} MakoHotWatch;

static MakoHotWatch mako_hot_watches[MAKO_HOT_WATCH_MAX];

static inline int64_t mako_hot_reload_watch(MakoString path) {
    if (!path.data || path.len == 0 || path.len >= 511) return -1;
    int64_t mt = mako_file_mtime_ns(path);
    if (mt < 0) return -1;
    for (int i = 0; i < MAKO_HOT_WATCH_MAX; i++) {
        if (mako_hot_watches[i].used
            && strncmp(mako_hot_watches[i].path, path.data, path.len) == 0
            && mako_hot_watches[i].path[path.len] == 0) {
            mako_hot_watches[i].mtime_ns = mt;
            return i;
        }
    }
    for (int i = 0; i < MAKO_HOT_WATCH_MAX; i++) {
        if (!mako_hot_watches[i].used) {
            memcpy(mako_hot_watches[i].path, path.data, path.len);
            mako_hot_watches[i].path[path.len] = 0;
            mako_hot_watches[i].mtime_ns = mt;
            mako_hot_watches[i].used = 1;
            return i;
        }
    }
    return -1;
}

/* 1 if file mtime changed since last watch/poll; updates stored mtime. */
static inline int64_t mako_hot_reload_changed(MakoString path) {
    if (!path.data || path.len == 0) return 0;
    int64_t mt = mako_file_mtime_ns(path);
    if (mt < 0) return 0;
    for (int i = 0; i < MAKO_HOT_WATCH_MAX; i++) {
        if (mako_hot_watches[i].used
            && strncmp(mako_hot_watches[i].path, path.data, path.len) == 0
            && mako_hot_watches[i].path[path.len] == 0) {
            if (mt != mako_hot_watches[i].mtime_ns) {
                mako_hot_watches[i].mtime_ns = mt;
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

static inline int64_t mako_hot_reload_unwatch(MakoString path) {
    if (!path.data || path.len == 0) return 0;
    for (int i = 0; i < MAKO_HOT_WATCH_MAX; i++) {
        if (mako_hot_watches[i].used
            && strncmp(mako_hot_watches[i].path, path.data, path.len) == 0
            && mako_hot_watches[i].path[path.len] == 0) {
            mako_hot_watches[i].used = 0;
            mako_hot_watches[i].path[0] = 0;
            return 1;
        }
    }
    return 0;
}

static inline int64_t mako_hot_reload_watch_count(void) {
    int n = 0;
    for (int i = 0; i < MAKO_HOT_WATCH_MAX; i++)
        if (mako_hot_watches[i].used) n++;
    return n;
}

/* Soft "code swap" counter — product hot-reload would bump on dylib replace. */
static int64_t mako_hot_reload_swaps = 0;

static inline int64_t mako_hot_reload_note_swap(void) {
    mako_hot_reload_swaps++;
    return mako_hot_reload_swaps;
}

static inline int64_t mako_hot_reload_swap_count(void) {
    return mako_hot_reload_swaps;
}

/* Combined stamp of all watched mtimes (xor of ns) for change detection. */
static inline int64_t mako_hot_reload_stamp(void) {
    uint64_t h = 0;
    for (int i = 0; i < MAKO_HOT_WATCH_MAX; i++) {
        if (!mako_hot_watches[i].used) continue;
        h ^= (uint64_t)mako_hot_watches[i].mtime_ns;
        h = (h << 7) | (h >> 57);
    }
    return (int64_t)h;
}

static inline MakoString mako_hot_reload_status_json(void) {
    char buf[384];
    int n = snprintf(
        buf, sizeof(buf),
        "{\"schema\":\"mako.hot_reload.v1\",\"watches\":%" PRId64
        ",\"swaps\":%" PRId64 ",\"stamp\":%" PRId64 "}",
        mako_hot_reload_watch_count(),
        mako_hot_reload_swaps,
        mako_hot_reload_stamp()
    );
    if (n < 0) return mako_str_from_cstr("{}");
    return mako_str_from_cstr(buf);
}

/* ---- Client prediction service seed (multiplayer netcode depth) ---- */
typedef struct {
    int64_t tick;
    int64_t state;
    int64_t last_auth;
    int64_t pending_input;
    int live;
} MakoPredict;

static inline MakoPredict *mako_predict_new(int64_t initial) {
    MakoPredict *p = (MakoPredict *)calloc(1, sizeof(MakoPredict));
    if (!p) return NULL;
    p->state = initial;
    p->last_auth = initial;
    p->live = 1;
    return p;
}

static inline int64_t mako_predict_tick(MakoPredict *p) {
    return p ? p->tick : -1;
}

static inline int64_t mako_predict_state(MakoPredict *p) {
    return p ? p->state : 0;
}

/* Apply local input for next tick (client prediction). */
static inline int64_t mako_predict_input(MakoPredict *p, int64_t delta) {
    if (!p || !p->live) return -1;
    p->pending_input = delta;
    p->state = mako_snap_predict(p->state, delta);
    p->tick++;
    return p->state;
}

/* Server auth arrives for some past/current tick; snap to auth + re-apply pending. */
static inline int64_t mako_predict_reconcile(MakoPredict *p, int64_t auth_state) {
    if (!p || !p->live) return -1;
    p->last_auth = auth_state;
    p->state = mako_snap_reconcile(p->state, auth_state);
    /* Optionally re-apply last pending input after reconcile. */
    if (p->pending_input != 0) {
        p->state = mako_snap_predict(p->state, p->pending_input);
    }
    return p->state;
}

static inline int64_t mako_predict_free(MakoPredict *p) {
    free(p);
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

/* Drop versions with ts < min_ts that are superseded (not the latest for key). */
static inline int64_t mako_mvcc_gc(MakoMvcc *m, int64_t min_ts) {
    if (!m || min_ts <= 0) return 0;
    int dropped = 0;
    for (int i = 0; i < m->n; i++) {
        if (!m->vers[i].live || m->vers[i].ts >= min_ts) continue;
        int64_t key = m->vers[i].key;
        int64_t ts = m->vers[i].ts;
        int has_newer = 0;
        for (int j = 0; j < m->n; j++) {
            if (m->vers[j].live && m->vers[j].key == key && m->vers[j].ts > ts) {
                has_newer = 1;
                break;
            }
        }
        if (has_newer) {
            m->vers[i].live = 0;
            dropped++;
        }
    }
    return dropped;
}

/* Collect live versions count. */
static inline int64_t mako_mvcc_live(MakoMvcc *m) {
    if (!m) return 0;
    int64_t n = 0;
    for (int i = 0; i < m->n; i++)
        if (m->vers[i].live) n++;
    return n;
}

/* ---- On-disk B-tree snapshot (sorted KV file) ---- */
#define MAKO_BTREE_SAVE_MAX 4096

static inline void mako_btree_collect(MakoBNode *n, int64_t *keys, int64_t *vals, int *out_n, int maxn) {
    if (!n || *out_n >= maxn) return;
    if (n->leaf) {
        for (int i = 0; i < n->n && *out_n < maxn; i++) {
            keys[*out_n] = n->keys[i];
            vals[*out_n] = n->vals[i];
            (*out_n)++;
        }
        return;
    }
    for (int i = 0; i < n->n; i++) {
        mako_btree_collect(n->kids[i], keys, vals, out_n, maxn);
        if (*out_n < maxn) {
            keys[*out_n] = n->keys[i];
            vals[*out_n] = n->vals[i];
            (*out_n)++;
        }
    }
    mako_btree_collect(n->kids[n->n], keys, vals, out_n, maxn);
}

/* FNV-1a over raw bytes (for btree snapshot checksum). */
static inline uint64_t mako_fnv1a64(const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* Save v2: magic "MBT2" | count | checksum | count*(key,val). Empty path → -1. */
#define MAKO_BTREE_SAVE_MAGIC 0x3254424DLL /* "MBT2" le */
static inline int64_t mako_btree_save(MakoBTree *t, MakoString path) {
    if (!t || !path.data || path.len == 0 || path.len >= 500) return -1;
#if defined(_WIN32)
    return -1;
#else
    int64_t keys[MAKO_BTREE_SAVE_MAX];
    int64_t vals[MAKO_BTREE_SAVE_MAX];
    int n = 0;
    mako_btree_collect(t->root, keys, vals, &n, MAKO_BTREE_SAVE_MAX);
    char pbuf[512];
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    int fd = open(pbuf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    int64_t magic = (int64_t)MAKO_BTREE_SAVE_MAGIC;
    int64_t count = n;
    uint64_t csum = mako_fnv1a64(keys, (size_t)n * sizeof(int64_t));
    csum ^= mako_fnv1a64(vals, (size_t)n * sizeof(int64_t));
    csum ^= (uint64_t)count * 0x9e3779b97f4a7c15ULL;
    int64_t checksum = (int64_t)csum;
    if (write(fd, &magic, sizeof(magic)) != (ssize_t)sizeof(magic)
        || write(fd, &count, sizeof(count)) != (ssize_t)sizeof(count)
        || write(fd, &checksum, sizeof(checksum)) != (ssize_t)sizeof(checksum)) {
        close(fd);
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (write(fd, &keys[i], sizeof(int64_t)) != (ssize_t)sizeof(int64_t)
            || write(fd, &vals[i], sizeof(int64_t)) != (ssize_t)sizeof(int64_t)) {
            close(fd);
            return -1;
        }
    }
    close(fd);
    return count;
#endif
}

/* Load v2 (magic+checksum) or legacy v1 (count only, no magic). Missing file → NULL. */
static inline MakoBTree *mako_btree_load(MakoString path) {
    if (!path.data || path.len == 0 || path.len >= 500) return NULL;
#if defined(_WIN32)
    return NULL;
#else
    char pbuf[512];
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    int fd = open(pbuf, O_RDONLY);
    if (fd < 0) return NULL;
    int64_t first = 0;
    if (read(fd, &first, sizeof(first)) != (ssize_t)sizeof(first)) {
        close(fd);
        return NULL;
    }
    int64_t count = 0;
    int is_v2 = 0;
    if (first == (int64_t)MAKO_BTREE_SAVE_MAGIC) {
        is_v2 = 1;
        int64_t checksum = 0;
        if (read(fd, &count, sizeof(count)) != (ssize_t)sizeof(count)
            || read(fd, &checksum, sizeof(checksum)) != (ssize_t)sizeof(checksum)
            || count < 0 || count > MAKO_BTREE_SAVE_MAX) {
            close(fd);
            return NULL;
        }
        int64_t keys[MAKO_BTREE_SAVE_MAX];
        int64_t vals[MAKO_BTREE_SAVE_MAX];
        for (int64_t i = 0; i < count; i++) {
            if (read(fd, &keys[i], sizeof(int64_t)) != (ssize_t)sizeof(int64_t)
                || read(fd, &vals[i], sizeof(int64_t)) != (ssize_t)sizeof(int64_t)) {
                close(fd);
                return NULL;
            }
        }
        close(fd);
        uint64_t csum = mako_fnv1a64(keys, (size_t)count * sizeof(int64_t));
        csum ^= mako_fnv1a64(vals, (size_t)count * sizeof(int64_t));
        csum ^= (uint64_t)count * 0x9e3779b97f4a7c15ULL;
        if ((int64_t)csum != checksum) return NULL;
        MakoBTree *t = mako_btree_new();
        if (!t) return NULL;
        for (int64_t i = 0; i < count; i++) (void)mako_btree_put(t, keys[i], vals[i]);
        t->count = count;
        return t;
    }
    /* Legacy v1: first word is count */
    (void)is_v2;
    count = first;
    if (count < 0 || count > MAKO_BTREE_SAVE_MAX) {
        close(fd);
        return NULL;
    }
    MakoBTree *t = mako_btree_new();
    if (!t) {
        close(fd);
        return NULL;
    }
    for (int64_t i = 0; i < count; i++) {
        int64_t k = 0, v = 0;
        if (read(fd, &k, sizeof(k)) != (ssize_t)sizeof(k)
            || read(fd, &v, sizeof(v)) != (ssize_t)sizeof(v)) {
            mako_btree_free(t);
            close(fd);
            return NULL;
        }
        (void)mako_btree_put(t, k, v);
    }
    close(fd);
    t->count = count;
    return t;
#endif
}

/* ---- SST: sorted immutable run (binary file for binary search) ---- */
/* Build SST from in-memory pairs (caller provides sorted or we sort). */
static inline int mako_sst_cmp_i64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

typedef struct {
    int64_t k, v;
} MakoSstPair;

static inline int mako_sst_cmp_pair(const void *a, const void *b) {
    int64_t x = ((const MakoSstPair *)a)->k;
    int64_t y = ((const MakoSstPair *)b)->k;
    return (x > y) - (x < y);
}

static inline MakoSst *mako_sst_build(MakoString path, int64_t *keys, int64_t *vals, int64_t n) {
    if (!path.data || path.len == 0 || path.len >= 500 || n < 0) return NULL;
    MakoSst *s = (MakoSst *)calloc(1, sizeof(MakoSst));
    if (!s) return NULL;
    memcpy(s->path, path.data, path.len);
    s->path[path.len] = 0;
    s->n = n;
    if (n == 0) {
        s->keys = NULL;
        s->vals = NULL;
        return s;
    }
    MakoSstPair *pairs = (MakoSstPair *)malloc((size_t)n * sizeof(MakoSstPair));
    if (!pairs) {
        free(s);
        return NULL;
    }
    for (int64_t i = 0; i < n; i++) {
        pairs[i].k = keys[i];
        pairs[i].v = vals[i];
    }
    qsort(pairs, (size_t)n, sizeof(MakoSstPair), mako_sst_cmp_pair);
    s->keys = (int64_t *)malloc((size_t)n * sizeof(int64_t));
    s->vals = (int64_t *)malloc((size_t)n * sizeof(int64_t));
    if (!s->keys || !s->vals) {
        free(pairs);
        free(s->keys);
        free(s->vals);
        free(s);
        return NULL;
    }
    for (int64_t i = 0; i < n; i++) {
        s->keys[i] = pairs[i].k;
        s->vals[i] = pairs[i].v;
    }
    free(pairs);
#if !defined(_WIN32)
    int fd = open(s->path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, &n, sizeof(n));
        write(fd, s->keys, (size_t)n * sizeof(int64_t));
        write(fd, s->vals, (size_t)n * sizeof(int64_t));
        close(fd);
    }
#endif
    return s;
}

/* Convenience: 4 fixed pairs for Mako without arrays. */
static inline MakoSst *mako_sst_build4(
    MakoString path,
    int64_t k0, int64_t v0, int64_t k1, int64_t v1,
    int64_t k2, int64_t v2, int64_t k3, int64_t v3
) {
    int64_t keys[4] = {k0, k1, k2, k3};
    int64_t vals[4] = {v0, v1, v2, v3};
    return mako_sst_build(path, keys, vals, 4);
}

/* Convenience: 8 fixed pairs (N≠4 product path). */
static inline MakoSst *mako_sst_build8(
    MakoString path,
    int64_t k0, int64_t v0, int64_t k1, int64_t v1,
    int64_t k2, int64_t v2, int64_t k3, int64_t v3,
    int64_t k4, int64_t v4, int64_t k5, int64_t v5,
    int64_t k6, int64_t v6, int64_t k7, int64_t v7
) {
    int64_t keys[8] = {k0, k1, k2, k3, k4, k5, k6, k7};
    int64_t vals[8] = {v0, v1, v2, v3, v4, v5, v6, v7};
    return mako_sst_build(path, keys, vals, 8);
}

/* Build SST from parallel key/val arrays (n pairs). n<=0 → empty SST. */
static inline MakoSst *mako_sst_build_n(
    MakoString path, int64_t n,
    int64_t k0, int64_t v0, int64_t k1, int64_t v1,
    int64_t k2, int64_t v2, int64_t k3, int64_t v3,
    int64_t k4, int64_t v4, int64_t k5, int64_t v5,
    int64_t k6, int64_t v6, int64_t k7, int64_t v7
) {
    if (n <= 0) return mako_sst_build(path, NULL, NULL, 0);
    if (n > 8) n = 8;
    int64_t keys[8] = {k0, k1, k2, k3, k4, k5, k6, k7};
    int64_t vals[8] = {v0, v1, v2, v3, v4, v5, v6, v7};
    return mako_sst_build(path, keys, vals, n);
}

static inline int64_t mako_sst_get(MakoSst *s, int64_t key) {
    if (!s || !s->keys || s->n <= 0) return -1;
    int64_t lo = 0, hi = s->n - 1;
    while (lo <= hi) {
        int64_t mid = lo + (hi - lo) / 2;
        int64_t k = s->keys[mid];
        if (k == key) return s->vals[mid];
        if (k < key) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static inline int64_t mako_sst_len(MakoSst *s) {
    return s ? s->n : 0;
}

static inline int64_t mako_sst_free(MakoSst *s) {
    if (!s) return 0;
    free(s->keys);
    free(s->vals);
    free(s);
    return 0;
}

/* ---- Page cache (LRU of fixed slots) ---- */
#define MAKO_PCACHE_SLOTS 16

typedef struct {
    int64_t page_id;
    MakoPage *page;
    int64_t last_use;
    int used;
} MakoPCacheSlot;

typedef struct {
    MakoPCacheSlot slots[MAKO_PCACHE_SLOTS];
    int64_t clock;
    int64_t hits;
    int64_t misses;
} MakoPageCache;

static inline MakoPageCache *mako_pcache_new(void) {
    return (MakoPageCache *)calloc(1, sizeof(MakoPageCache));
}

static inline MakoPage *mako_pcache_get(MakoPageCache *c, int64_t page_id) {
    if (!c || page_id < 0) return NULL;
    for (int i = 0; i < MAKO_PCACHE_SLOTS; i++) {
        if (c->slots[i].used && c->slots[i].page_id == page_id) {
            c->slots[i].last_use = ++c->clock;
            c->hits++;
            return c->slots[i].page;
        }
    }
    c->misses++;
    /* allocate new page into LRU victim */
    int vic = 0;
    int64_t oldest = c->slots[0].used ? c->slots[0].last_use : -1;
    for (int i = 0; i < MAKO_PCACHE_SLOTS; i++) {
        if (!c->slots[i].used) {
            vic = i;
            oldest = -1;
            break;
        }
        if (c->slots[i].last_use < oldest) {
            oldest = c->slots[i].last_use;
            vic = i;
        }
    }
    if (c->slots[vic].used && c->slots[vic].page) {
        mako_page_free(c->slots[vic].page);
    }
    MakoPage *p = mako_page_alloc(4096);
    c->slots[vic].page = p;
    c->slots[vic].page_id = page_id;
    c->slots[vic].last_use = ++c->clock;
    c->slots[vic].used = 1;
    return p;
}

static inline int64_t mako_pcache_hits(MakoPageCache *c) {
    return c ? c->hits : 0;
}

static inline int64_t mako_pcache_misses(MakoPageCache *c) {
    return c ? c->misses : 0;
}

static inline int64_t mako_pcache_free(MakoPageCache *c) {
    if (!c) return 0;
    for (int i = 0; i < MAKO_PCACHE_SLOTS; i++) {
        if (c->slots[i].used && c->slots[i].page) mako_page_free(c->slots[i].page);
    }
    free(c);
    return 0;
}

/* ---- Page-backed B-tree seed: nodes live in MakoPage slots ---- */
#define MAKO_PBT_MAX_KEYS 7
#define MAKO_PBT_MAX_PAGES 64
#define MAKO_PBT_PAGE_BYTES 4096
/* Layout in page data as int64_t[]:
 * [0] leaf (1/0), [1] n,
 * [2 .. 2+MAX) keys, [2+MAX .. 2+2*MAX) vals,
 * [2+2*MAX .. 2+2*MAX+MAX+1) child page ids (-1 empty).
 */
#define MAKO_PBT_OFF_LEAF 0
#define MAKO_PBT_OFF_N 1
#define MAKO_PBT_OFF_KEYS 2
#define MAKO_PBT_OFF_VALS (2 + MAKO_PBT_MAX_KEYS)
#define MAKO_PBT_OFF_KIDS (2 + 2 * MAKO_PBT_MAX_KEYS)

typedef struct {
    MakoPage *pages[MAKO_PBT_MAX_PAGES];
    int64_t root; /* page id */
    int64_t npages;
    int64_t count;
} MakoPageBTree;

static inline int64_t *mako_pbt_raw(MakoPage *p) {
    return p && p->data ? (int64_t *)(void *)p->data : NULL;
}

static inline int64_t mako_pbt_alloc_page(MakoPageBTree *t) {
    if (!t || t->npages >= MAKO_PBT_MAX_PAGES) return -1;
    int64_t id = t->npages;
    MakoPage *p = mako_page_alloc(MAKO_PBT_PAGE_BYTES);
    if (!p) return -1;
    t->pages[id] = p;
    t->npages++;
    int64_t *r = mako_pbt_raw(p);
    if (!r) return -1;
    r[MAKO_PBT_OFF_LEAF] = 1;
    r[MAKO_PBT_OFF_N] = 0;
    for (int i = 0; i <= MAKO_PBT_MAX_KEYS; i++) r[MAKO_PBT_OFF_KIDS + i] = -1;
    return id;
}

static inline MakoPageBTree *mako_pbtree_new(void) {
    MakoPageBTree *t = (MakoPageBTree *)calloc(1, sizeof(MakoPageBTree));
    if (!t) return NULL;
    int64_t root = mako_pbt_alloc_page(t);
    if (root < 0) {
        free(t);
        return NULL;
    }
    t->root = root;
    return t;
}

static inline int64_t mako_pbtree_get_node(MakoPageBTree *t, int64_t pid, int64_t key) {
    if (!t || pid < 0 || pid >= t->npages) return -1;
    int64_t *r = mako_pbt_raw(t->pages[pid]);
    if (!r) return -1;
    int n = (int)r[MAKO_PBT_OFF_N];
    int i = 0;
    while (i < n && key > r[MAKO_PBT_OFF_KEYS + i]) i++;
    if (i < n && key == r[MAKO_PBT_OFF_KEYS + i]) return r[MAKO_PBT_OFF_VALS + i];
    if (r[MAKO_PBT_OFF_LEAF]) return -1;
    return mako_pbtree_get_node(t, r[MAKO_PBT_OFF_KIDS + i], key);
}

static inline int64_t mako_pbtree_get(MakoPageBTree *t, int64_t key) {
    return t ? mako_pbtree_get_node(t, t->root, key) : -1;
}

static inline void mako_pbt_split_child(MakoPageBTree *t, int64_t pid, int i) {
    int64_t *parent = mako_pbt_raw(t->pages[pid]);
    int64_t yid = parent[MAKO_PBT_OFF_KIDS + i];
    int64_t *y = mako_pbt_raw(t->pages[yid]);
    int64_t zid = mako_pbt_alloc_page(t);
    if (zid < 0) return;
    int64_t *z = mako_pbt_raw(t->pages[zid]);
    int mid = MAKO_PBT_MAX_KEYS / 2;
    z[MAKO_PBT_OFF_LEAF] = y[MAKO_PBT_OFF_LEAF];
    z[MAKO_PBT_OFF_N] = MAKO_PBT_MAX_KEYS - mid - 1;
    for (int j = 0; j < (int)z[MAKO_PBT_OFF_N]; j++) {
        z[MAKO_PBT_OFF_KEYS + j] = y[MAKO_PBT_OFF_KEYS + j + mid + 1];
        z[MAKO_PBT_OFF_VALS + j] = y[MAKO_PBT_OFF_VALS + j + mid + 1];
    }
    if (!y[MAKO_PBT_OFF_LEAF]) {
        for (int j = 0; j <= (int)z[MAKO_PBT_OFF_N]; j++)
            z[MAKO_PBT_OFF_KIDS + j] = y[MAKO_PBT_OFF_KIDS + j + mid + 1];
    }
    y[MAKO_PBT_OFF_N] = mid;
    int pn = (int)parent[MAKO_PBT_OFF_N];
    for (int j = pn; j >= i + 1; j--)
        parent[MAKO_PBT_OFF_KIDS + j + 1] = parent[MAKO_PBT_OFF_KIDS + j];
    parent[MAKO_PBT_OFF_KIDS + i + 1] = zid;
    for (int j = pn - 1; j >= i; j--) {
        parent[MAKO_PBT_OFF_KEYS + j + 1] = parent[MAKO_PBT_OFF_KEYS + j];
        parent[MAKO_PBT_OFF_VALS + j + 1] = parent[MAKO_PBT_OFF_VALS + j];
    }
    parent[MAKO_PBT_OFF_KEYS + i] = y[MAKO_PBT_OFF_KEYS + mid];
    parent[MAKO_PBT_OFF_VALS + i] = y[MAKO_PBT_OFF_VALS + mid];
    parent[MAKO_PBT_OFF_N] = pn + 1;
}

static inline void mako_pbt_insert_nonfull(MakoPageBTree *t, int64_t pid, int64_t key, int64_t val) {
    int64_t *r = mako_pbt_raw(t->pages[pid]);
    int i = (int)r[MAKO_PBT_OFF_N] - 1;
    if (r[MAKO_PBT_OFF_LEAF]) {
        while (i >= 0 && key < r[MAKO_PBT_OFF_KEYS + i]) {
            r[MAKO_PBT_OFF_KEYS + i + 1] = r[MAKO_PBT_OFF_KEYS + i];
            r[MAKO_PBT_OFF_VALS + i + 1] = r[MAKO_PBT_OFF_VALS + i];
            i--;
        }
        if (i >= 0 && r[MAKO_PBT_OFF_KEYS + i] == key) {
            r[MAKO_PBT_OFF_VALS + i] = val;
            return;
        }
        r[MAKO_PBT_OFF_KEYS + i + 1] = key;
        r[MAKO_PBT_OFF_VALS + i + 1] = val;
        r[MAKO_PBT_OFF_N]++;
    } else {
        while (i >= 0 && key < r[MAKO_PBT_OFF_KEYS + i]) i--;
        i++;
        if (i < (int)r[MAKO_PBT_OFF_N] && r[MAKO_PBT_OFF_KEYS + i] == key) {
            r[MAKO_PBT_OFF_VALS + i] = val;
            return;
        }
        int64_t cid = r[MAKO_PBT_OFF_KIDS + i];
        int64_t *ch = mako_pbt_raw(t->pages[cid]);
        if (ch && ch[MAKO_PBT_OFF_N] == MAKO_PBT_MAX_KEYS) {
            mako_pbt_split_child(t, pid, i);
            r = mako_pbt_raw(t->pages[pid]);
            if (key > r[MAKO_PBT_OFF_KEYS + i]) i++;
            else if (key == r[MAKO_PBT_OFF_KEYS + i]) {
                r[MAKO_PBT_OFF_VALS + i] = val;
                return;
            }
        }
        mako_pbt_insert_nonfull(t, r[MAKO_PBT_OFF_KIDS + i], key, val);
    }
}

static inline int64_t mako_pbtree_put(MakoPageBTree *t, int64_t key, int64_t val) {
    if (!t) return -1;
    int64_t *root = mako_pbt_raw(t->pages[t->root]);
    if (!root) return -1;
    /* Count only new keys; in-place updates keep len stable. */
    int is_new = (mako_pbtree_get(t, key) == -1) ? 1 : 0;
    if (root[MAKO_PBT_OFF_N] == MAKO_PBT_MAX_KEYS) {
        int64_t old = t->root;
        int64_t sid = mako_pbt_alloc_page(t);
        if (sid < 0) return -1;
        int64_t *s = mako_pbt_raw(t->pages[sid]);
        s[MAKO_PBT_OFF_LEAF] = 0;
        s[MAKO_PBT_OFF_N] = 0;
        s[MAKO_PBT_OFF_KIDS + 0] = old;
        t->root = sid;
        mako_pbt_split_child(t, sid, 0);
        mako_pbt_insert_nonfull(t, sid, key, val);
    } else {
        mako_pbt_insert_nonfull(t, t->root, key, val);
    }
    if (is_new) t->count++;
    return 0;
}

static inline int64_t mako_pbtree_len(MakoPageBTree *t) {
    return t ? t->count : 0;
}

static inline int64_t mako_pbtree_pages(MakoPageBTree *t) {
    return t ? t->npages : 0;
}

static inline int64_t mako_pbtree_free(MakoPageBTree *t) {
    if (!t) return 0;
    for (int64_t i = 0; i < t->npages; i++) {
        if (t->pages[i]) mako_page_free(t->pages[i]);
    }
    free(t);
    return 0;
}

/* ---- Bloom filter seed (int64 keys, fixed bitset) ---- */
#define MAKO_BLOOM_BITS 2048
#define MAKO_BLOOM_WORDS (MAKO_BLOOM_BITS / 64)
#define MAKO_BLOOM_HASHES 4

typedef struct {
    uint64_t bits[MAKO_BLOOM_WORDS];
    int64_t n;
} MakoBloom;

static inline uint64_t mako_bloom_mix(uint64_t x, uint64_t seed) {
    x ^= seed;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static inline MakoBloom *mako_bloom_new(void) {
    return (MakoBloom *)calloc(1, sizeof(MakoBloom));
}

static inline int64_t mako_bloom_add(MakoBloom *b, int64_t key) {
    if (!b) return -1;
    for (int h = 0; h < MAKO_BLOOM_HASHES; h++) {
        uint64_t x = mako_bloom_mix((uint64_t)key, (uint64_t)(h + 1) * 0x9e3779b97f4a7c15ULL);
        size_t bit = (size_t)(x % (uint64_t)MAKO_BLOOM_BITS);
        b->bits[bit / 64] |= (uint64_t)1 << (bit % 64);
    }
    b->n++;
    return 0;
}

/* 1 = maybe present, 0 = definitely absent. */
static inline int64_t mako_bloom_maybe(MakoBloom *b, int64_t key) {
    if (!b) return 0;
    for (int h = 0; h < MAKO_BLOOM_HASHES; h++) {
        uint64_t x = mako_bloom_mix((uint64_t)key, (uint64_t)(h + 1) * 0x9e3779b97f4a7c15ULL);
        size_t bit = (size_t)(x % (uint64_t)MAKO_BLOOM_BITS);
        if ((b->bits[bit / 64] & ((uint64_t)1 << (bit % 64))) == 0) return 0;
    }
    return 1;
}

static inline int64_t mako_bloom_len(MakoBloom *b) {
    return b ? b->n : 0;
}

/* Clear bits and key count; keep the allocation (rebuild without free/new). */
static inline int64_t mako_bloom_clear(MakoBloom *b) {
    if (!b) return -1;
    memset(b->bits, 0, sizeof(b->bits));
    b->n = 0;
    return 0;
}

static inline int64_t mako_bloom_free(MakoBloom *b) {
    free(b);
    return 0;
}

/* FNV-1a 64-bit (string → domain int key). Collisions inflate false positives only for bloom. */
static inline int64_t mako_str_hash64(MakoString s) {
    uint64_t h = 14695981039346656037ULL;
    if (s.data) {
        for (size_t i = 0; i < s.len; i++) {
            h ^= (uint64_t)(unsigned char)s.data[i];
            h *= 1099511628211ULL;
        }
    }
    /* Keep non-negative for range seeds that treat keys as signed. */
    if ((int64_t)h < 0) return (int64_t)(~h);
    return (int64_t)h;
}

static inline int64_t mako_bloom_add_str(MakoBloom *b, MakoString key) {
    return mako_bloom_add(b, mako_str_hash64(key));
}

static inline int64_t mako_bloom_maybe_str(MakoBloom *b, MakoString key) {
    return mako_bloom_maybe(b, mako_str_hash64(key));
}

/* ---- Ordered range scan (TLS small + heap grow; inclusive lo..hi) ---- */
#define MAKO_RANGE_TLS 128
#define MAKO_RANGE_HEAP_MAX 65536
static __thread int64_t mako_range_keys_tls[MAKO_RANGE_TLS];
static __thread int64_t mako_range_vals_tls[MAKO_RANGE_TLS];
static __thread int64_t *mako_range_keys_dyn;
static __thread int64_t *mako_range_vals_dyn;
static __thread int64_t mako_range_capacity;
static __thread int64_t mako_range_n;
static __thread int64_t mako_range_i; /* iterator cursor */

static inline int64_t *mako_range_keys_ptr(void) {
    return mako_range_keys_dyn ? mako_range_keys_dyn : mako_range_keys_tls;
}
static inline int64_t *mako_range_vals_ptr(void) {
    return mako_range_vals_dyn ? mako_range_vals_dyn : mako_range_vals_tls;
}

static inline void mako_range_clear(void) {
    mako_range_n = 0;
    mako_range_i = 0;
    if (!mako_range_capacity) mako_range_capacity = MAKO_RANGE_TLS;
}

static inline int mako_range_ensure(int64_t need) {
    if (need <= mako_range_capacity) return 0;
    if (need > MAKO_RANGE_HEAP_MAX) return -1;
    int64_t ncap = mako_range_capacity < MAKO_RANGE_TLS ? MAKO_RANGE_TLS : mako_range_capacity;
    while (ncap < need) {
        ncap *= 2;
        if (ncap > MAKO_RANGE_HEAP_MAX) ncap = MAKO_RANGE_HEAP_MAX;
    }
    int64_t *nk = (int64_t *)realloc(mako_range_keys_dyn, (size_t)ncap * sizeof(int64_t));
    int64_t *nv = (int64_t *)realloc(mako_range_vals_dyn, (size_t)ncap * sizeof(int64_t));
    if (!nk || !nv) {
        free(nk);
        free(nv);
        return -1;
    }
    if (!mako_range_keys_dyn && mako_range_n > 0) {
        memcpy(nk, mako_range_keys_tls, (size_t)mako_range_n * sizeof(int64_t));
        memcpy(nv, mako_range_vals_tls, (size_t)mako_range_n * sizeof(int64_t));
    }
    mako_range_keys_dyn = nk;
    mako_range_vals_dyn = nv;
    mako_range_capacity = ncap;
    return 0;
}

static inline void mako_range_push(int64_t k, int64_t v) {
    if (mako_range_n >= MAKO_RANGE_HEAP_MAX) return;
    if (mako_range_n >= mako_range_capacity || (mako_range_n >= MAKO_RANGE_TLS && !mako_range_keys_dyn)) {
        if (mako_range_ensure(mako_range_n + 1) != 0) return;
    }
    mako_range_keys_ptr()[mako_range_n] = k;
    mako_range_vals_ptr()[mako_range_n] = v;
    mako_range_n++;
}

static inline void mako_btree_range_node(MakoBNode *n, int64_t lo, int64_t hi) {
    if (!n || mako_range_n >= MAKO_RANGE_HEAP_MAX) return;
    if (n->leaf) {
        for (int i = 0; i < n->n; i++) {
            if (n->keys[i] < lo) continue;
            if (n->keys[i] > hi) break;
            mako_range_push(n->keys[i], n->vals[i]);
        }
        return;
    }
    for (int i = 0; i < n->n; i++) {
        mako_btree_range_node(n->kids[i], lo, hi);
        if (mako_range_n >= MAKO_RANGE_HEAP_MAX) return;
        if (n->keys[i] >= lo && n->keys[i] <= hi) mako_range_push(n->keys[i], n->vals[i]);
        if (n->keys[i] > hi) return;
    }
    mako_btree_range_node(n->kids[n->n], lo, hi);
}

/* Populate range buffer; return count of matches (cap MAKO_RANGE_HEAP_MAX). */
static inline int64_t mako_btree_range(MakoBTree *t, int64_t lo, int64_t hi) {
    mako_range_clear();
    if (!t || !t->root || lo > hi) return 0;
    mako_btree_range_node(t->root, lo, hi);
    return mako_range_n;
}

static inline int64_t mako_sst_range(MakoSst *s, int64_t lo, int64_t hi) {
    mako_range_clear();
    if (!s || !s->keys || s->n <= 0 || lo > hi) return 0;
    int64_t i = 0, j = s->n;
    while (i < j) {
        int64_t m = i + (j - i) / 2;
        if (s->keys[m] < lo) i = m + 1;
        else j = m;
    }
    for (; i < s->n && s->keys[i] <= hi; i++) {
        mako_range_push(s->keys[i], s->vals[i]);
        if (mako_range_n >= MAKO_RANGE_HEAP_MAX) break;
    }
    return mako_range_n;
}

static inline int64_t mako_range_len(void) {
    return mako_range_n;
}

static inline int64_t mako_range_cap(void) {
    return MAKO_RANGE_HEAP_MAX;
}

static inline int64_t mako_range_key_at(int64_t i) {
    if (i < 0 || i >= mako_range_n) return -1;
    return mako_range_keys_ptr()[i];
}

static inline int64_t mako_range_val_at(int64_t i) {
    if (i < 0 || i >= mako_range_n) return -1;
    return mako_range_vals_ptr()[i];
}

/* Iterator over last range result: rewind then next/key/val. */
static inline int64_t mako_range_rewind(void) {
    mako_range_i = 0;
    return mako_range_n;
}

/* 1 = advanced, 0 = exhausted. After next, range_key/range_val read current. */
static inline int64_t mako_range_next(void) {
    if (mako_range_i >= mako_range_n) return 0;
    mako_range_i++;
    return 1;
}

static inline int64_t mako_range_key(void) {
    if (mako_range_i <= 0 || mako_range_i > mako_range_n) return -1;
    return mako_range_keys_ptr()[mako_range_i - 1];
}

static inline int64_t mako_range_val(void) {
    if (mako_range_i <= 0 || mako_range_i > mako_range_n) return -1;
    return mako_range_vals_ptr()[mako_range_i - 1];
}

/* Collect all values for an exact key into the range buffer (multi-value / first). */
static inline int64_t mako_btree_get_all(MakoBTree *t, int64_t key) {
    return mako_btree_range(t, key, key);
}

/* String-key helpers: hash domain key (collision risk on btree; safe for bloom). */
static inline int64_t mako_btree_put_str(MakoBTree *t, MakoString key, int64_t val) {
    return mako_btree_put(t, mako_str_hash64(key), val);
}
static inline int64_t mako_btree_get_str(MakoBTree *t, MakoString key) {
    return mako_btree_get(t, mako_str_hash64(key));
}
static inline int64_t mako_btree_range_str(MakoBTree *t, MakoString lo, MakoString hi) {
    int64_t a = mako_str_hash64(lo), b = mako_str_hash64(hi);
    if (a > b) {
        int64_t tmp = a;
        a = b;
        b = tmp;
    }
    return mako_btree_range(t, a, b);
}

/* ---- Disk page manager seed (fixed 4 KiB pages, file-backed) ---- */
#define MAKO_PMAN_PAGE 4096
#define MAKO_PMAN_MAGIC 0x4D4B504DLL /* "MKPM" */
#define MAKO_PMAN_SLOTS (MAKO_PMAN_PAGE / 8)

typedef struct {
    char path[512];
    int fd;
    int64_t npages; /* includes superblock page 0 */
    int64_t reads;
    int64_t writes;
} MakoPageMan;

static inline int64_t mako_pman_write_super(MakoPageMan *pm) {
    if (!pm || pm->fd < 0) return -1;
#if defined(_WIN32)
    return -1;
#else
    int64_t hdr[MAKO_PMAN_SLOTS];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = MAKO_PMAN_MAGIC;
    hdr[1] = MAKO_PMAN_PAGE;
    hdr[2] = pm->npages;
    if (lseek(pm->fd, 0, SEEK_SET) < 0) return -1;
    ssize_t w = write(pm->fd, hdr, MAKO_PMAN_PAGE);
    return (w == MAKO_PMAN_PAGE) ? 0 : -1;
#endif
}

static inline MakoPageMan *mako_pman_open(MakoString path) {
#if defined(_WIN32)
    (void)path;
    return NULL;
#else
    if (!path.data || path.len == 0 || path.len >= 511) return NULL;
    MakoPageMan *pm = (MakoPageMan *)calloc(1, sizeof(MakoPageMan));
    if (!pm) return NULL;
    memcpy(pm->path, path.data, path.len);
    pm->path[path.len] = 0;
    pm->fd = open(pm->path, O_RDWR | O_CREAT, 0644);
    if (pm->fd < 0) {
        free(pm);
        return NULL;
    }
    off_t sz = lseek(pm->fd, 0, SEEK_END);
    if (sz < (off_t)MAKO_PMAN_PAGE) {
        pm->npages = 1;
        if (mako_pman_write_super(pm) != 0) {
            close(pm->fd);
            free(pm);
            return NULL;
        }
    } else {
        int64_t hdr[MAKO_PMAN_SLOTS];
        memset(hdr, 0, sizeof(hdr));
        if (lseek(pm->fd, 0, SEEK_SET) < 0 || read(pm->fd, hdr, MAKO_PMAN_PAGE) != MAKO_PMAN_PAGE
            || hdr[0] != MAKO_PMAN_MAGIC) {
            close(pm->fd);
            free(pm);
            return NULL;
        }
        pm->npages = hdr[2] > 0 ? hdr[2] : (int64_t)(sz / MAKO_PMAN_PAGE);
        if (pm->npages < 1) pm->npages = 1;
    }
    return pm;
#endif
}

/* Allocate a new page; returns page id (>= 1). Page 0 is superblock. */
static inline int64_t mako_pman_alloc(MakoPageMan *pm) {
#if defined(_WIN32)
    (void)pm;
    return -1;
#else
    if (!pm || pm->fd < 0) return -1;
    int64_t id = pm->npages;
    char zero[MAKO_PMAN_PAGE];
    memset(zero, 0, sizeof(zero));
    if (lseek(pm->fd, (off_t)id * MAKO_PMAN_PAGE, SEEK_SET) < 0) return -1;
    if (write(pm->fd, zero, MAKO_PMAN_PAGE) != MAKO_PMAN_PAGE) return -1;
    pm->npages = id + 1;
    pm->writes++;
    (void)mako_pman_write_super(pm);
    return id;
#endif
}

/* Read/write int64 slots within a page (0 .. slots-1). */
static inline int64_t mako_pman_set(MakoPageMan *pm, int64_t page_id, int64_t slot, int64_t val) {
#if defined(_WIN32)
    (void)pm;
    (void)page_id;
    (void)slot;
    (void)val;
    return -1;
#else
    if (!pm || pm->fd < 0 || page_id < 1 || page_id >= pm->npages) return -1;
    if (slot < 0 || slot >= MAKO_PMAN_SLOTS) return -1;
    off_t off = (off_t)page_id * MAKO_PMAN_PAGE + (off_t)slot * 8;
    if (lseek(pm->fd, off, SEEK_SET) < 0) return -1;
    if (write(pm->fd, &val, sizeof(val)) != (ssize_t)sizeof(val)) return -1;
    pm->writes++;
    return 0;
#endif
}

static inline int64_t mako_pman_get(MakoPageMan *pm, int64_t page_id, int64_t slot) {
#if defined(_WIN32)
    (void)pm;
    (void)page_id;
    (void)slot;
    return -1;
#else
    if (!pm || pm->fd < 0 || page_id < 1 || page_id >= pm->npages) return -1;
    if (slot < 0 || slot >= MAKO_PMAN_SLOTS) return -1;
    int64_t val = 0;
    off_t off = (off_t)page_id * MAKO_PMAN_PAGE + (off_t)slot * 8;
    if (lseek(pm->fd, off, SEEK_SET) < 0) return -1;
    if (read(pm->fd, &val, sizeof(val)) != (ssize_t)sizeof(val)) return -1;
    pm->reads++;
    return val;
#endif
}

static inline int64_t mako_pman_sync(MakoPageMan *pm) {
#if defined(_WIN32)
    (void)pm;
    return -1;
#else
    if (!pm || pm->fd < 0) return -1;
    if (mako_pman_write_super(pm) != 0) return -1;
    return fsync(pm->fd) == 0 ? 0 : -1;
#endif
}

static inline int64_t mako_pman_pages(MakoPageMan *pm) {
    return pm ? pm->npages : 0;
}

static inline int64_t mako_pman_reads(MakoPageMan *pm) {
    return pm ? pm->reads : 0;
}

static inline int64_t mako_pman_writes(MakoPageMan *pm) {
    return pm ? pm->writes : 0;
}

static inline int64_t mako_pman_close(MakoPageMan *pm) {
#if defined(_WIN32)
    free(pm);
    return 0;
#else
    if (!pm) return 0;
    if (pm->fd >= 0) {
        (void)mako_pman_write_super(pm);
        close(pm->fd);
    }
    free(pm);
    return 0;
#endif
}

/* Write raw page bytes (up to 4 KiB). Shorter data is zero-padded. */
static inline int64_t mako_pman_write_page(MakoPageMan *pm, int64_t page_id, MakoString data) {
#if defined(_WIN32)
    (void)pm;
    (void)page_id;
    (void)data;
    return -1;
#else
    if (!pm || pm->fd < 0 || page_id < 1 || page_id >= pm->npages) return -1;
    char buf[MAKO_PMAN_PAGE];
    memset(buf, 0, sizeof(buf));
    size_t n = data.data && data.len > 0 ? data.len : 0;
    if (n > MAKO_PMAN_PAGE) n = MAKO_PMAN_PAGE;
    if (n && data.data) memcpy(buf, data.data, n);
    if (lseek(pm->fd, (off_t)page_id * MAKO_PMAN_PAGE, SEEK_SET) < 0) return -1;
    if (write(pm->fd, buf, MAKO_PMAN_PAGE) != MAKO_PMAN_PAGE) return -1;
    pm->writes++;
    return (int64_t)n;
#endif
}

/* Read full page as string (always 4096 bytes on success). */
static inline MakoString mako_pman_read_page(MakoPageMan *pm, int64_t page_id) {
    MakoString out = {NULL, 0};
#if defined(_WIN32)
    (void)pm;
    (void)page_id;
    return out;
#else
    if (!pm || pm->fd < 0 || page_id < 1 || page_id >= pm->npages) return out;
    char *buf = (char *)malloc(MAKO_PMAN_PAGE);
    if (!buf) return out;
    if (lseek(pm->fd, (off_t)page_id * MAKO_PMAN_PAGE, SEEK_SET) < 0) {
        free(buf);
        return out;
    }
    if (read(pm->fd, buf, MAKO_PMAN_PAGE) != MAKO_PMAN_PAGE) {
        free(buf);
        return out;
    }
    pm->reads++;
    out.data = buf;
    out.len = MAKO_PMAN_PAGE;
    return out;
#endif
}

/* ---- Multi-value ordered map (sorted pairs; duplicate keys allowed) ---- */
typedef struct {
    int64_t *keys;
    int64_t *vals;
    int64_t n;
    int64_t cap;
} MakoMultiMap;

static inline MakoMultiMap *mako_multimap_new(void) {
    MakoMultiMap *m = (MakoMultiMap *)calloc(1, sizeof(MakoMultiMap));
    return m;
}

static inline int mako_multimap_grow(MakoMultiMap *m, int64_t need) {
    if (!m) return -1;
    if (need <= m->cap) return 0;
    int64_t ncap = m->cap ? m->cap * 2 : 16;
    while (ncap < need) ncap *= 2;
    int64_t *nk = (int64_t *)realloc(m->keys, (size_t)ncap * sizeof(int64_t));
    int64_t *nv = (int64_t *)realloc(m->vals, (size_t)ncap * sizeof(int64_t));
    if (!nk || !nv) {
        free(nk);
        free(nv);
        return -1;
    }
    m->keys = nk;
    m->vals = nv;
    m->cap = ncap;
    return 0;
}

/* Insert (key,val) keeping sorted order; duplicates allowed (multi-value). */
static inline int64_t mako_multimap_put(MakoMultiMap *m, int64_t key, int64_t val) {
    if (!m || mako_multimap_grow(m, m->n + 1) != 0) return -1;
    int64_t i = m->n;
    while (i > 0 && m->keys[i - 1] > key) {
        m->keys[i] = m->keys[i - 1];
        m->vals[i] = m->vals[i - 1];
        i--;
    }
    m->keys[i] = key;
    m->vals[i] = val;
    m->n++;
    return 0;
}

static inline int64_t mako_multimap_len(MakoMultiMap *m) {
    return m ? m->n : 0;
}

/* First value for key, or -1. */
static inline int64_t mako_multimap_get(MakoMultiMap *m, int64_t key) {
    if (!m || m->n <= 0) return -1;
    int64_t lo = 0, hi = m->n;
    while (lo < hi) {
        int64_t mid = lo + (hi - lo) / 2;
        if (m->keys[mid] < key) lo = mid + 1;
        else hi = mid;
    }
    if (lo < m->n && m->keys[lo] == key) return m->vals[lo];
    return -1;
}

/* All values for key → range buffer. */
static inline int64_t mako_multimap_get_all(MakoMultiMap *m, int64_t key) {
    mako_range_clear();
    if (!m || m->n <= 0) return 0;
    int64_t lo = 0, hi = m->n;
    while (lo < hi) {
        int64_t mid = lo + (hi - lo) / 2;
        if (m->keys[mid] < key) lo = mid + 1;
        else hi = mid;
    }
    for (; lo < m->n && m->keys[lo] == key; lo++) {
        mako_range_push(m->keys[lo], m->vals[lo]);
        if (mako_range_n >= MAKO_RANGE_HEAP_MAX) break;
    }
    return mako_range_n;
}

static inline int64_t mako_multimap_range(MakoMultiMap *m, int64_t lo_k, int64_t hi_k) {
    mako_range_clear();
    if (!m || m->n <= 0 || lo_k > hi_k) return 0;
    int64_t lo = 0, hi = m->n;
    while (lo < hi) {
        int64_t mid = lo + (hi - lo) / 2;
        if (m->keys[mid] < lo_k) lo = mid + 1;
        else hi = mid;
    }
    for (; lo < m->n && m->keys[lo] <= hi_k; lo++) {
        mako_range_push(m->keys[lo], m->vals[lo]);
        if (mako_range_n >= MAKO_RANGE_HEAP_MAX) break;
    }
    return mako_range_n;
}

static inline int64_t mako_multimap_free(MakoMultiMap *m) {
    if (!m) return 0;
    free(m->keys);
    free(m->vals);
    free(m);
    return 0;
}

/* ---- Process-local domain handle registry (int slots → opaque ptrs) ---- */
#define MAKO_DOMREG_MAX 256
#define MAKO_DOMREG_BLOOM 1
#define MAKO_DOMREG_BTREE 2
#define MAKO_DOMREG_PMAN 3
#define MAKO_DOMREG_SST 4
#define MAKO_DOMREG_MULTIMAP 5
static void *mako_domreg_ptr[MAKO_DOMREG_MAX];
static int8_t mako_domreg_kind[MAKO_DOMREG_MAX];

static inline int64_t mako_domain_reg_put(void *p, int8_t kind) {
    if (!p) return -1;
    for (int i = 1; i < MAKO_DOMREG_MAX; i++) {
        if (!mako_domreg_ptr[i]) {
            mako_domreg_ptr[i] = p;
            mako_domreg_kind[i] = kind;
            return i;
        }
    }
    return -1;
}

static inline void *mako_domain_reg_get(int64_t id, int8_t kind) {
    if (id <= 0 || id >= MAKO_DOMREG_MAX) return NULL;
    if (mako_domreg_kind[id] != kind) return NULL;
    return mako_domreg_ptr[id];
}

static inline int64_t mako_domain_reg_del(int64_t id) {
    if (id <= 0 || id >= MAKO_DOMREG_MAX) return -1;
    mako_domreg_ptr[id] = NULL;
    mako_domreg_kind[id] = 0;
    return 0;
}

static inline int64_t mako_domain_reg_put_bloom(MakoBloom *b) {
    return mako_domain_reg_put((void *)b, MAKO_DOMREG_BLOOM);
}
static inline MakoBloom *mako_domain_reg_get_bloom(int64_t id) {
    return (MakoBloom *)mako_domain_reg_get(id, MAKO_DOMREG_BLOOM);
}
static inline int64_t mako_domain_reg_put_btree(MakoBTree *t) {
    return mako_domain_reg_put((void *)t, MAKO_DOMREG_BTREE);
}
static inline MakoBTree *mako_domain_reg_get_btree(int64_t id) {
    return (MakoBTree *)mako_domain_reg_get(id, MAKO_DOMREG_BTREE);
}
static inline int64_t mako_domain_reg_put_pman(MakoPageMan *pm) {
    return mako_domain_reg_put((void *)pm, MAKO_DOMREG_PMAN);
}
static inline MakoPageMan *mako_domain_reg_get_pman(int64_t id) {
    return (MakoPageMan *)mako_domain_reg_get(id, MAKO_DOMREG_PMAN);
}

/* ---- Portable SIMD-ish seed: scalar loop (autovec-friendly) ---- */
static inline int64_t mako_simd_dot_i64_4(
    int64_t a0, int64_t a1, int64_t a2, int64_t a3,
    int64_t b0, int64_t b1, int64_t b2, int64_t b3
) {
    return a0 * b0 + a1 * b1 + a2 * b2 + a3 * b3;
}

static inline int64_t mako_simd_sum_i64_4(int64_t a0, int64_t a1, int64_t a2, int64_t a3) {
    return a0 + a1 + a2 + a3;
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
    uint32_t *pixels; /* soft framebuffer ARGB seed (host only) */
    int64_t npix;
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
    win->npix = win->w * win->h;
    if (win->npix > 0 && win->npix <= 4096 * 4096) {
        win->pixels = (uint32_t *)calloc((size_t)win->npix, sizeof(uint32_t));
    }
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
    free(w->pixels);
    free(w);
    return 0;
}

/* Event poll seed (no real windowing backend): always 0 = no events. */
static inline int64_t mako_gfx_poll(MakoGfxWindow *w) {
    (void)w;
    return 0;
}

/* Backend name for soft window seed. */
static inline MakoString mako_gfx_backend_name(void) {
    return mako_str_from_cstr("soft");
}

static inline int64_t mako_gfx_window_pixels(MakoGfxWindow *w) {
    return w ? w->npix : 0;
}

/* Fill soft framebuffer with ARGB color (0xAARRGGBB). */
static inline int64_t mako_gfx_window_fill(MakoGfxWindow *w, int64_t argb) {
    if (!w || !w->pixels) return -1;
    uint32_t c = (uint32_t)argb;
    for (int64_t i = 0; i < w->npix; i++) w->pixels[i] = c;
    return 0;
}

static inline int64_t mako_gfx_window_set_pixel(MakoGfxWindow *w, int64_t x, int64_t y, int64_t argb) {
    if (!w || !w->pixels || x < 0 || y < 0 || x >= w->w || y >= w->h) return -1;
    w->pixels[y * w->w + x] = (uint32_t)argb;
    return 0;
}

static inline int64_t mako_gfx_window_get_pixel(MakoGfxWindow *w, int64_t x, int64_t y) {
    if (!w || !w->pixels || x < 0 || y < 0 || x >= w->w || y >= w->h) return -1;
    return (int64_t)w->pixels[y * w->w + x];
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
    /* Soft source-line BPs registered via debug_line_bp_set */
    (void)mako_debug_check_line_bps(mako_debug_file, line);
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

/* OTLP convenience export (domain after http+trace includes). */
static inline int64_t mako_otlp_export_traces_json(MakoString url, int64_t timeout_ms) {
    MakoString body = mako_trace_export_otlp_json();
    MakoString ct = mako_str_from_cstr("application/json");
    int64_t st = mako_otlp_http_export(url, body, ct, timeout_ms);
    mako_str_free(body);
    mako_str_free(ct);
    return st;
}

static inline int64_t mako_otlp_export_traces_pb(MakoString url, int64_t timeout_ms) {
    MakoString body = mako_trace_export_otlp_pb();
    MakoString ct = mako_str_from_cstr("application/x-protobuf");
    int64_t st = mako_otlp_http_export(url, body, ct, timeout_ms);
    mako_str_free(body);
    mako_str_free(ct);
    return st;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_DOMAIN_H */
