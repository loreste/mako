/* Mako concurrent hashmap — lock-free reads, per-stripe spinlock writes.
 * Designed for high-throughput key-value workloads (Redis-like). */
#ifndef MAKO_CMAP_H
#define MAKO_CMAP_H

#include "mako_rt.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>

#define CMAP_INIT_SHIFT 20    /* 1M entries initial */
#define CMAP_INIT_CAP (1 << CMAP_INIT_SHIFT)
#define CMAP_STRIPES 512
#define CMAP_LOAD_MAX 75      /* percent */
#define CMAP_EMPTY 0
#define CMAP_TOMBSTONE 1

typedef struct {
    uint32_t hash;        /* 0=empty, 1=tombstone, else valid */
    uint32_t key_len;
    uint32_t val_len;
    char *key;
    char *val;
} CMapSlot;

typedef struct {
    CMapSlot *slots;
    uint32_t cap;
    uint32_t mask;
    atomic_int_fast64_t len;
    atomic_flag locks[CMAP_STRIPES];
} MakoCMap;

static inline uint32_t cmap_hash(const char *key, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)key[i];
        h *= 16777619u;
    }
    /* Ensure hash != 0 and != 1 (reserved) */
    h |= 2;
    return h;
}

static inline uint32_t cmap_stripe(uint32_t h) {
    return h & (CMAP_STRIPES - 1);
}

static inline void cmap_lock(MakoCMap *m, uint32_t stripe) {
    while (atomic_flag_test_and_set_explicit(&m->locks[stripe], memory_order_acquire)) {
        /* spin — very short critical sections so this is optimal */
        #if defined(__aarch64__)
        __asm__ volatile("yield");
        #elif defined(__x86_64__)
        __asm__ volatile("pause");
        #endif
    }
}

static inline void cmap_unlock(MakoCMap *m, uint32_t stripe) {
    atomic_flag_clear_explicit(&m->locks[stripe], memory_order_release);
}

static inline MakoCMap *mako_cmap_new(void) {
    MakoCMap *m = (MakoCMap *)calloc(1, sizeof(MakoCMap));
    if (!m) return NULL;
    m->cap = CMAP_INIT_CAP;
    m->mask = m->cap - 1;
    m->slots = (CMapSlot *)calloc(m->cap, sizeof(CMapSlot));
    if (!m->slots) { free(m); return NULL; }
    atomic_store(&m->len, 0);
    for (int i = 0; i < CMAP_STRIPES; i++) {
        atomic_flag_clear(&m->locks[i]);
    }
    return m;
}

static inline void mako_cmap_set(MakoCMap *m, MakoString key, MakoString val) {
    uint32_t h = cmap_hash(key.data, key.len);
    uint32_t stripe = cmap_stripe(h);
    cmap_lock(m, stripe);

    uint32_t idx = h & m->mask;
    uint32_t first_tomb = UINT32_MAX;

    for (uint32_t i = 0; i < m->cap; i++) {
        uint32_t pos = (idx + i) & m->mask;
        CMapSlot *s = &m->slots[pos];

        if (s->hash == CMAP_EMPTY) {
            /* Insert at tombstone if we found one, else here */
            uint32_t target = (first_tomb != UINT32_MAX) ? first_tomb : pos;
            CMapSlot *t = &m->slots[target];
            t->hash = h;
            t->key_len = (uint32_t)key.len;
            t->val_len = (uint32_t)val.len;
            t->key = (char *)malloc(key.len + 1);
            memcpy(t->key, key.data, key.len);
            t->key[key.len] = 0;
            t->val = (char *)malloc(val.len + 1);
            memcpy(t->val, val.data, val.len);
            t->val[val.len] = 0;
            atomic_fetch_add(&m->len, 1);
            cmap_unlock(m, stripe);
            return;
        }

        if (s->hash == CMAP_TOMBSTONE) {
            if (first_tomb == UINT32_MAX) first_tomb = pos;
            continue;
        }

        /* Check if key matches — update in place */
        if (s->hash == h && s->key_len == (uint32_t)key.len &&
            memcmp(s->key, key.data, key.len) == 0) {
            /* Update value */
            if (s->val_len != (uint32_t)val.len) {
                free(s->val);
                s->val = (char *)malloc(val.len + 1);
            }
            memcpy(s->val, val.data, val.len);
            s->val[val.len] = 0;
            s->val_len = (uint32_t)val.len;
            cmap_unlock(m, stripe);
            return;
        }
    }

    /* Table full — use tombstone */
    if (first_tomb != UINT32_MAX) {
        CMapSlot *t = &m->slots[first_tomb];
        t->hash = h;
        t->key_len = (uint32_t)key.len;
        t->val_len = (uint32_t)val.len;
        t->key = (char *)malloc(key.len + 1);
        memcpy(t->key, key.data, key.len);
        t->key[key.len] = 0;
        t->val = (char *)malloc(val.len + 1);
        memcpy(t->val, val.data, val.len);
        t->val[val.len] = 0;
        atomic_fetch_add(&m->len, 1);
    }
    cmap_unlock(m, stripe);
}

static inline MakoString mako_cmap_get(MakoCMap *m, MakoString key) {
    uint32_t h = cmap_hash(key.data, key.len);
    uint32_t idx = h & m->mask;

    /* Lock-free read path — slots are stable (no resize) */
    for (uint32_t i = 0; i < m->cap; i++) {
        uint32_t pos = (idx + i) & m->mask;
        CMapSlot *s = &m->slots[pos];
        uint32_t sh = s->hash;

        if (sh == CMAP_EMPTY) {
            return mako_str_from_cstr("");
        }
        if (sh == CMAP_TOMBSTONE) continue;
        if (sh == h && s->key_len == (uint32_t)key.len &&
            memcmp(s->key, key.data, key.len) == 0) {
            /* Found — return copy of value */
            size_t vlen = s->val_len;
            char *copy = (char *)malloc(vlen + 1);
            if (!copy) return mako_str_from_cstr("");
            memcpy(copy, s->val, vlen);
            copy[vlen] = 0;
            return (MakoString){copy, vlen};
        }
    }
    return mako_str_from_cstr("");
}

static inline int64_t mako_cmap_has(MakoCMap *m, MakoString key) {
    uint32_t h = cmap_hash(key.data, key.len);
    uint32_t idx = h & m->mask;

    for (uint32_t i = 0; i < m->cap; i++) {
        uint32_t pos = (idx + i) & m->mask;
        CMapSlot *s = &m->slots[pos];
        uint32_t sh = s->hash;

        if (sh == CMAP_EMPTY) return 0;
        if (sh == CMAP_TOMBSTONE) continue;
        if (sh == h && s->key_len == (uint32_t)key.len &&
            memcmp(s->key, key.data, key.len) == 0) {
            return 1;
        }
    }
    return 0;
}

static inline int64_t mako_cmap_del(MakoCMap *m, MakoString key) {
    uint32_t h = cmap_hash(key.data, key.len);
    uint32_t stripe = cmap_stripe(h);
    cmap_lock(m, stripe);

    uint32_t idx = h & m->mask;
    for (uint32_t i = 0; i < m->cap; i++) {
        uint32_t pos = (idx + i) & m->mask;
        CMapSlot *s = &m->slots[pos];

        if (s->hash == CMAP_EMPTY) {
            cmap_unlock(m, stripe);
            return 0;
        }
        if (s->hash == CMAP_TOMBSTONE) continue;
        if (s->hash == h && s->key_len == (uint32_t)key.len &&
            memcmp(s->key, key.data, key.len) == 0) {
            free(s->key);
            free(s->val);
            s->key = NULL;
            s->val = NULL;
            s->key_len = 0;
            s->val_len = 0;
            s->hash = CMAP_TOMBSTONE;
            atomic_fetch_sub(&m->len, 1);
            cmap_unlock(m, stripe);
            return 1;
        }
    }
    cmap_unlock(m, stripe);
    return 0;
}

static inline int64_t mako_cmap_len(MakoCMap *m) {
    return (int64_t)atomic_load(&m->len);
}

/* Atomic increment: key must contain an integer string. Returns new value.
 * Returns INT64_MIN on parse error. */
static inline int64_t mako_cmap_incr(MakoCMap *m, MakoString key, int64_t delta) {
    uint32_t h = cmap_hash(key.data, key.len);
    uint32_t stripe = cmap_stripe(h);
    cmap_lock(m, stripe);

    uint32_t idx = h & m->mask;
    for (uint32_t i = 0; i < m->cap; i++) {
        uint32_t pos = (idx + i) & m->mask;
        CMapSlot *s = &m->slots[pos];

        if (s->hash == CMAP_EMPTY) {
            /* Key doesn't exist — treat as 0, set to delta */
            s->hash = h;
            s->key_len = (uint32_t)key.len;
            s->key = (char *)malloc(key.len + 1);
            memcpy(s->key, key.data, key.len);
            s->key[key.len] = 0;
            char buf[32];
            int n = snprintf(buf, sizeof(buf), "%lld", (long long)delta);
            s->val = (char *)malloc((size_t)n + 1);
            memcpy(s->val, buf, (size_t)n + 1);
            s->val_len = (uint32_t)n;
            atomic_fetch_add(&m->len, 1);
            cmap_unlock(m, stripe);
            return delta;
        }
        if (s->hash == CMAP_TOMBSTONE) continue;
        if (s->hash == h && s->key_len == (uint32_t)key.len &&
            memcmp(s->key, key.data, key.len) == 0) {
            /* Parse current value */
            int64_t cur = 0;
            int neg = 0;
            char *p = s->val;
            if (*p == '-') { neg = 1; p++; }
            while (*p >= '0' && *p <= '9') {
                cur = cur * 10 + (*p - '0');
                p++;
            }
            if (*p != 0) {
                /* Not a valid integer */
                cmap_unlock(m, stripe);
                return INT64_MIN;
            }
            if (neg) cur = -cur;
            cur += delta;
            /* Write back */
            char buf[32];
            int n = snprintf(buf, sizeof(buf), "%lld", (long long)cur);
            if ((uint32_t)n != s->val_len) {
                free(s->val);
                s->val = (char *)malloc((size_t)n + 1);
            }
            memcpy(s->val, buf, (size_t)n + 1);
            s->val_len = (uint32_t)n;
            cmap_unlock(m, stripe);
            return cur;
        }
    }
    cmap_unlock(m, stripe);
    return delta; /* shouldn't reach */
}

#endif /* MAKO_CMAP_H */
