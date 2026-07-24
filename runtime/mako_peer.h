/* General-purpose peer + route table (protocol-agnostic).
 *
 * Tracks named peers (identity, host, port, transport, priority) and
 * opaque string-key routes → peer identity. No protocol logic.
 * Applications attach their own connection handles via
 * peer_table_set_conn / get_conn and interpret transport/state themselves.
 *
 * All sizes and timeouts are caller-supplied. Absolute ceilings only.
 *
 * Memory safety:
 * - Handles pack slot + generation (ABA after free/reuse).
 * - Fixed-size identity/host buffers; reject NUL/CR/LF in host; bounds on lens.
 * - calloc fails closed; free preserves generation.
 */
#ifndef MAKO_PEER_H
#define MAKO_PEER_H

#include "mako_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAKO_PEER_TABLE_SLOTS   16
#define MAKO_PEER_TABLE_CEILING 4096
#define MAKO_PEER_IDENT_MAX     256
#define MAKO_PEER_HOST_MAX      256
#define MAKO_PEER_ROUTE_KEY_MAX 256

typedef struct {
    int used;
    char identity[MAKO_PEER_IDENT_MAX];
    size_t identity_len;
    char host[MAKO_PEER_HOST_MAX];
    size_t host_len;
    int port;
    int transport; /* app-defined enum */
    int priority;
    int64_t conn;  /* opaque connection handle owned by app */
    int64_t state; /* app-defined */
    int64_t last_ms;
} MakoPeerEntry;

typedef struct {
    int used;
    char key[MAKO_PEER_ROUTE_KEY_MAX];
    size_t key_len;
    char peer_identity[MAKO_PEER_IDENT_MAX];
    size_t peer_identity_len;
} MakoPeerRoute;

typedef struct {
    int live;
    int max_peers;
    int max_routes;
    int64_t drops;
    uint32_t gen;
    MakoPeerEntry *peers;
    MakoPeerRoute *routes;
} MakoPeerTable;

static MakoPeerTable mako_peer_tables[MAKO_PEER_TABLE_SLOTS];
static pthread_mutex_t mako_peer_tables_mu = MAKO_MUTEX_INIT;

static inline int64_t mako_peer_pack_handle(int slot, uint32_t gen) {
    if (gen == 0) gen = 1;
    return (int64_t)(((uint32_t)(slot + 1) & 0xffffu)
                     | ((gen & 0xffffu) << 16));
}
static inline int mako_peer_handle_slot(int64_t h) {
    int id = (int)(h & 0xffff);
    if (id <= 0 || id > MAKO_PEER_TABLE_SLOTS) return -1;
    return id - 1;
}
static inline uint32_t mako_peer_handle_gen(int64_t h) {
    return (uint32_t)((h >> 16) & 0xffffu);
}

/* Caller must hold mako_peer_tables_mu. */
static inline int mako_peer_live_locked(int64_t handle, MakoPeerTable **out) {
    int slot = mako_peer_handle_slot(handle);
    if (slot < 0) return 0;
    MakoPeerTable *t = &mako_peer_tables[slot];
    if (!t->live) return 0;
    if (mako_peer_handle_gen(handle) != t->gen) return 0;
    if (out) *out = t;
    return 1;
}

static inline int64_t mako_peer_table_new(int64_t max_peers, int64_t max_routes) {
    if (max_peers <= 0 || max_routes <= 0) return -1;
    if (max_peers > MAKO_PEER_TABLE_CEILING || max_routes > MAKO_PEER_TABLE_CEILING) {
        return -1;
    }
    size_t pb = (size_t)max_peers * sizeof(MakoPeerEntry);
    size_t rb = (size_t)max_routes * sizeof(MakoPeerRoute);
    if (pb / sizeof(MakoPeerEntry) != (size_t)max_peers) return -1;
    if (rb / sizeof(MakoPeerRoute) != (size_t)max_routes) return -1;

    pthread_mutex_lock(&mako_peer_tables_mu);
    int slot = -1;
    for (int i = 0; i < MAKO_PEER_TABLE_SLOTS; i++) {
        if (!mako_peer_tables[i].live) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&mako_peer_tables_mu);
        return -1;
    }
    MakoPeerTable *t = &mako_peer_tables[slot];
    uint32_t prev_gen = t->gen;
    MakoPeerEntry *peers = (MakoPeerEntry *)calloc((size_t)max_peers, sizeof(MakoPeerEntry));
    MakoPeerRoute *routes = (MakoPeerRoute *)calloc((size_t)max_routes, sizeof(MakoPeerRoute));
    if (!peers || !routes) {
        free(peers);
        free(routes);
        pthread_mutex_unlock(&mako_peer_tables_mu);
        return -1;
    }
    memset(t, 0, sizeof(*t));
    t->peers = peers;
    t->routes = routes;
    t->live = 1;
    t->max_peers = (int)max_peers;
    t->max_routes = (int)max_routes;
    t->gen = (prev_gen + 1) & 0xffffu;
    if (t->gen == 0) t->gen = 1;
    int64_t handle = mako_peer_pack_handle(slot, t->gen);
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return handle;
}

static inline int64_t mako_peer_table_free(int64_t handle) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    if (!mako_peer_live_locked(handle, &t)) {
        pthread_mutex_unlock(&mako_peer_tables_mu);
        return -1;
    }
    uint32_t gen = t->gen;
    free(t->peers);
    free(t->routes);
    memset(t, 0, sizeof(*t));
    t->gen = gen;
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return 0;
}

static inline int64_t mako_peer_table_add(
    int64_t handle, MakoString identity, MakoString host, int64_t port,
    int64_t transport, int64_t priority
) {
    if (!identity.data || identity.len == 0 || identity.len >= MAKO_PEER_IDENT_MAX) return -1;
    if (!host.data || host.len == 0 || host.len >= MAKO_PEER_HOST_MAX) return -1;
    if (port <= 0 || port > 65535) return -1;
    for (size_t i = 0; i < identity.len; i++) {
        if (identity.data[i] == 0) return -1;
    }
    for (size_t i = 0; i < host.len; i++) {
        if (host.data[i] == 0 || host.data[i] == '\r' || host.data[i] == '\n') return -1;
    }
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    if (!mako_peer_live_locked(handle, &t)) {
        pthread_mutex_unlock(&mako_peer_tables_mu);
        return -1;
    }
    int idx = -1;
    for (int i = 0; i < t->max_peers; i++) {
        if (!t->peers[i].used) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        t->drops++;
        pthread_mutex_unlock(&mako_peer_tables_mu);
        return -1;
    }
    MakoPeerEntry *p = &t->peers[idx];
    memset(p, 0, sizeof(*p));
    p->used = 1;
    memcpy(p->identity, identity.data, identity.len);
    p->identity[identity.len] = 0;
    p->identity_len = identity.len;
    memcpy(p->host, host.data, host.len);
    p->host[host.len] = 0;
    p->host_len = host.len;
    p->port = (int)port;
    p->transport = (int)transport;
    p->priority = (int)priority;
    p->conn = -1;
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return (int64_t)idx;
}

static inline int64_t mako_peer_table_set_conn(int64_t handle, int64_t peer_id, int64_t conn) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    if (!mako_peer_live_locked(handle, &t)
        || peer_id < 0 || peer_id >= t->max_peers || !t->peers[peer_id].used) {
        pthread_mutex_unlock(&mako_peer_tables_mu);
        return -1;
    }
    t->peers[peer_id].conn = conn;
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return 0;
}

static inline int64_t mako_peer_table_get_conn(int64_t handle, int64_t peer_id) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    int64_t c = -1;
    if (mako_peer_live_locked(handle, &t)
        && peer_id >= 0 && peer_id < t->max_peers && t->peers[peer_id].used) {
        c = t->peers[peer_id].conn;
    }
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return c;
}

static inline int64_t mako_peer_table_set_state(int64_t handle, int64_t peer_id, int64_t state) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    if (!mako_peer_live_locked(handle, &t)
        || peer_id < 0 || peer_id >= t->max_peers || !t->peers[peer_id].used) {
        pthread_mutex_unlock(&mako_peer_tables_mu);
        return -1;
    }
    t->peers[peer_id].state = state;
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return 0;
}

static inline int64_t mako_peer_table_get_state(int64_t handle, int64_t peer_id) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    int64_t s = -1;
    if (mako_peer_live_locked(handle, &t)
        && peer_id >= 0 && peer_id < t->max_peers && t->peers[peer_id].used) {
        s = t->peers[peer_id].state;
    }
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return s;
}

static inline int64_t mako_peer_table_touch(int64_t handle, int64_t peer_id, int64_t now_ms) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    if (!mako_peer_live_locked(handle, &t)
        || peer_id < 0 || peer_id >= t->max_peers || !t->peers[peer_id].used) {
        pthread_mutex_unlock(&mako_peer_tables_mu);
        return -1;
    }
    t->peers[peer_id].last_ms = now_ms;
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return 0;
}

static inline int64_t mako_peer_table_last_ms(int64_t handle, int64_t peer_id) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    int64_t v = -1;
    if (mako_peer_live_locked(handle, &t)
        && peer_id >= 0 && peer_id < t->max_peers && t->peers[peer_id].used) {
        v = t->peers[peer_id].last_ms;
    }
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return v;
}

static inline int64_t mako_peer_table_route_add(
    int64_t handle, MakoString key, MakoString peer_identity
) {
    if (!peer_identity.data || peer_identity.len == 0
        || peer_identity.len >= MAKO_PEER_IDENT_MAX) {
        return -1;
    }
    if (key.len >= MAKO_PEER_ROUTE_KEY_MAX) return -1;
    /* key may be empty (default route); if present, reject embedded NUL. */
    if (key.data) {
        for (size_t i = 0; i < key.len; i++) {
            if (key.data[i] == 0) return -1;
        }
    } else if (key.len != 0) {
        return -1; /* len without data */
    }
    for (size_t i = 0; i < peer_identity.len; i++) {
        if (peer_identity.data[i] == 0) return -1;
    }
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    if (!mako_peer_live_locked(handle, &t)) {
        pthread_mutex_unlock(&mako_peer_tables_mu);
        return -1;
    }
    int idx = -1;
    for (int i = 0; i < t->max_routes; i++) {
        if (!t->routes[i].used) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        t->drops++;
        pthread_mutex_unlock(&mako_peer_tables_mu);
        return -1;
    }
    MakoPeerRoute *r = &t->routes[idx];
    memset(r, 0, sizeof(*r));
    r->used = 1;
    if (key.data && key.len) {
        memcpy(r->key, key.data, key.len);
        r->key[key.len] = 0;
        r->key_len = key.len;
    }
    memcpy(r->peer_identity, peer_identity.data, peer_identity.len);
    r->peer_identity[peer_identity.len] = 0;
    r->peer_identity_len = peer_identity.len;
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return (int64_t)idx;
}

/* Lookup peer_id by route key. Empty key or "*" is default route. */
static inline int64_t mako_peer_table_route_lookup(int64_t handle, MakoString key) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    if (!mako_peer_live_locked(handle, &t)) {
        pthread_mutex_unlock(&mako_peer_tables_mu);
        return -1;
    }
    const char *want = key.data ? key.data : "";
    size_t wlen = key.data ? key.len : 0;
    int found = -1, def = -1;
    for (int i = 0; i < t->max_routes; i++) {
        if (!t->routes[i].used) continue;
        int is_def = (t->routes[i].key_len == 0
                      || (t->routes[i].key_len == 1 && t->routes[i].key[0] == '*'));
        for (int p = 0; p < t->max_peers; p++) {
            if (!t->peers[p].used) continue;
            if (t->peers[p].identity_len == t->routes[i].peer_identity_len
                && memcmp(t->peers[p].identity, t->routes[i].peer_identity,
                          t->routes[i].peer_identity_len) == 0) {
                if (is_def) def = p;
                else if (t->routes[i].key_len == wlen
                         && memcmp(t->routes[i].key, want, wlen) == 0) {
                    found = p;
                }
                break;
            }
        }
        if (found >= 0) break;
    }
    int out = found >= 0 ? found : def;
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return (int64_t)out;
}

static inline int64_t mako_peer_table_count(int64_t handle) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    int n = -1;
    if (mako_peer_live_locked(handle, &t)) {
        n = 0;
        for (int i = 0; i < t->max_peers; i++) if (t->peers[i].used) n++;
    }
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return n;
}

static inline int64_t mako_peer_table_drops(int64_t handle) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    int64_t d = -1;
    if (mako_peer_live_locked(handle, &t)) d = t->drops;
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return d;
}

/* Host/port helpers for apps that need to open sockets themselves. */
static inline MakoString mako_peer_table_host(int64_t handle, int64_t peer_id) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    MakoString out = mako_str_from_cstr("");
    if (mako_peer_live_locked(handle, &t)
        && peer_id >= 0 && peer_id < t->max_peers && t->peers[peer_id].used) {
        /* Copy under lock so free can't free host buffer mid-copy. */
        out = mako_str_from_cstr(t->peers[peer_id].host);
    }
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return out;
}

static inline int64_t mako_peer_table_port(int64_t handle, int64_t peer_id) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    int64_t p = -1;
    if (mako_peer_live_locked(handle, &t)
        && peer_id >= 0 && peer_id < t->max_peers && t->peers[peer_id].used) {
        p = t->peers[peer_id].port;
    }
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return p;
}

static inline int64_t mako_peer_table_transport(int64_t handle, int64_t peer_id) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    int64_t tr = -1;
    if (mako_peer_live_locked(handle, &t)
        && peer_id >= 0 && peer_id < t->max_peers && t->peers[peer_id].used) {
        tr = t->peers[peer_id].transport;
    }
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return tr;
}

/* Capacity and slot occupancy — for scanning peers without protocol knowledge. */
static inline int64_t mako_peer_table_capacity(int64_t handle) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    int64_t c = -1;
    if (mako_peer_live_locked(handle, &t)) c = t->max_peers;
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return c;
}

static inline int64_t mako_peer_table_alive(int64_t handle, int64_t peer_id) {
    pthread_mutex_lock(&mako_peer_tables_mu);
    MakoPeerTable *t = NULL;
    int64_t a = 0;
    if (mako_peer_live_locked(handle, &t)
        && peer_id >= 0 && peer_id < t->max_peers && t->peers[peer_id].used) {
        a = 1;
    }
    pthread_mutex_unlock(&mako_peer_tables_mu);
    return a;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_PEER_H */
