/* Mako concurrent hashmap.
 *
 * Operations use a portable readers/writer gate: reads may run concurrently,
 * while writers exclude readers and other writers for the complete probe and
 * mutation.  The old lock-free-read claim was unsound because open-addressed
 * slots contain ordinary pointers that writers replace and free.  Copying a
 * value while holding the read side of this gate gives CMap a real lifetime
 * boundary without requiring hazard pointers or a tracing collector.
 */
#ifndef MAKO_CMAP_H
#define MAKO_CMAP_H

#include "mako_stdlib.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMAP_INIT_SHIFT 20    /* 1M slots initially; no resize races. */
#define CMAP_INIT_CAP (1u << CMAP_INIT_SHIFT)
#define CMAP_LOAD_MAX 75u
#define CMAP_EMPTY 0u
#define CMAP_TOMBSTONE 1u

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
    MakoRWMutex gate;
} MakoCMap;

static inline uint32_t cmap_hash(const char *key, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)key[i];
        h *= 16777619u;
    }
    /* Ensure hash != 0 and != 1 (reserved). */
    h |= 2u;
    return h;
}

static inline void cmap_validate_string(MakoString s, const char *what) {
    if (s.len > UINT32_MAX) mako_abort(what);
    if (s.len != 0 && !s.data) mako_abort(what);
}

static inline char *cmap_dup_bytes(const char *data, size_t len) {
    if (len == SIZE_MAX) mako_abort("cmap: string too large");
    char *copy = (char *)malloc(len + 1);
    if (!copy) mako_abort("cmap: out of memory");
    if (len) memcpy(copy, data, len);
    copy[len] = 0;
    return copy;
}

/* Return the matching slot, or UINT32_MAX.  If no match exists, `empty` is
 * the first empty slot encountered and `tombstone` is the first reusable
 * tombstone.  The caller must hold the write gate when using this helper. */
static inline uint32_t cmap_probe_locked(
    MakoCMap *m,
    MakoString key,
    uint32_t hash,
    uint32_t *empty,
    uint32_t *tombstone
) {
    uint32_t idx = hash & m->mask;
    const char *key_data = key.data ? key.data : "";
    *empty = UINT32_MAX;
    *tombstone = UINT32_MAX;

    for (uint32_t i = 0; i < m->cap; i++) {
        uint32_t pos = (idx + i) & m->mask;
        CMapSlot *s = &m->slots[pos];
        if (s->hash == CMAP_EMPTY) {
            *empty = pos;
            return UINT32_MAX;
        }
        if (s->hash == CMAP_TOMBSTONE) {
            if (*tombstone == UINT32_MAX) *tombstone = pos;
            continue;
        }
        if (s->hash == hash && s->key_len == (uint32_t)key.len &&
            memcmp(s->key, key_data, key.len) == 0) {
            return pos;
        }
    }
    return UINT32_MAX;
}

/* Rehash while the write gate is held.  Slot-owned key/value pointers move
 * without being copied; readers cannot observe the old table during this
 * operation. */
static inline void cmap_grow_locked(MakoCMap *m) {
    if (m->cap > UINT32_MAX / 2u) {
        mako_abort("cmap: capacity limit reached");
        return;
    }
    uint32_t new_cap = m->cap * 2u;
    CMapSlot *new_slots = (CMapSlot *)calloc(new_cap, sizeof(CMapSlot));
    if (!new_slots) {
        mako_abort("cmap: out of memory while growing");
        return;
    }

    uint32_t new_mask = new_cap - 1u;
    for (uint32_t i = 0; i < m->cap; i++) {
        CMapSlot *old = &m->slots[i];
        if (old->hash == CMAP_EMPTY || old->hash == CMAP_TOMBSTONE) continue;
        uint32_t pos = old->hash & new_mask;
        while (new_slots[pos].hash != CMAP_EMPTY) {
            pos = (pos + 1u) & new_mask;
        }
        new_slots[pos] = *old;
    }
    free(m->slots);
    m->slots = new_slots;
    m->cap = new_cap;
    m->mask = new_mask;
}

static inline void cmap_grow_if_needed_locked(MakoCMap *m) {
    uint64_t len = (uint64_t)atomic_load_explicit(&m->len, memory_order_relaxed);
    uint64_t next = len + 1u;
    uint64_t threshold = ((uint64_t)m->cap * CMAP_LOAD_MAX) / 100u;
    if (next >= threshold) cmap_grow_locked(m);
}

static inline MakoCMap *mako_cmap_new(void) {
    MakoCMap *m = (MakoCMap *)calloc(1, sizeof(MakoCMap));
    if (!m) {
        mako_abort("cmap_new: out of memory");
        return NULL;
    }

    m->cap = CMAP_INIT_CAP;
    m->mask = m->cap - 1;
    m->slots = (CMapSlot *)calloc(m->cap, sizeof(CMapSlot));
    if (!m->slots) {
        free(m);
        mako_abort("cmap_new: out of memory");
        return NULL;
    }

    if (pthread_mutex_init(&m->gate.mu, NULL) != 0) {
        free(m->slots);
        free(m);
        mako_abort("cmap_new: synchronization initialization failed");
        return NULL;
    }
    if (pthread_cond_init(&m->gate.cv, NULL) != 0) {
        pthread_mutex_destroy(&m->gate.mu);
        free(m->slots);
        free(m);
        mako_abort("cmap_new: synchronization initialization failed");
        return NULL;
    }
    m->gate.readers = 0;
    m->gate.writer = 0;
    m->gate.write_waiters = 0;
    atomic_store_explicit(&m->len, 0, memory_order_relaxed);
    return m;
}

static inline void mako_cmap_set(MakoCMap *m, MakoString key, MakoString val) {
    if (!m) mako_abort("cmap_set: null map");
    cmap_validate_string(key, "cmap_set: key is too large or invalid");
    cmap_validate_string(val, "cmap_set: value is too large or invalid");

    uint32_t hash = cmap_hash(key.data ? key.data : "", key.len);
    mako_rwmutex_lock(&m->gate);

    uint32_t empty;
    uint32_t tombstone;
    uint32_t found = cmap_probe_locked(m, key, hash, &empty, &tombstone);
    if (found != UINT32_MAX) {
        CMapSlot *s = &m->slots[found];
        if (s->val_len == (uint32_t)val.len) {
            if (val.len) memcpy(s->val, val.data, val.len);
        } else {
            char *new_val = cmap_dup_bytes(val.data ? val.data : "", val.len);
            free(s->val);
            s->val = new_val;
            s->val_len = (uint32_t)val.len;
        }
        mako_rwmutex_unlock(&m->gate);
        return;
    }

    cmap_grow_if_needed_locked(m);
    found = cmap_probe_locked(m, key, hash, &empty, &tombstone);

    uint32_t target = tombstone != UINT32_MAX ? tombstone : empty;
    if (target != UINT32_MAX) {
        /* Allocate before publishing the hash, so a failed allocation cannot
         * leave a partially initialized slot visible to a future operation. */
        char *new_key = cmap_dup_bytes(key.data ? key.data : "", key.len);
        char *new_val = cmap_dup_bytes(val.data ? val.data : "", val.len);
        CMapSlot *s = &m->slots[target];
        s->key_len = (uint32_t)key.len;
        s->val_len = (uint32_t)val.len;
        s->key = new_key;
        s->val = new_val;
        s->hash = hash;
        atomic_fetch_add_explicit(&m->len, 1, memory_order_relaxed);
    } else {
        mako_rwmutex_unlock(&m->gate);
        mako_abort("cmap_set: map capacity exhausted");
        return;
    }
    mako_rwmutex_unlock(&m->gate);
}

static inline MakoString mako_cmap_get(MakoCMap *m, MakoString key) {
    if (!m) mako_abort("cmap_get: null map");
    cmap_validate_string(key, "cmap_get: key is too large or invalid");

    uint32_t hash = cmap_hash(key.data ? key.data : "", key.len);
    mako_rwmutex_rlock(&m->gate);

    uint32_t empty;
    uint32_t tombstone;
    uint32_t found = cmap_probe_locked(m, key, hash, &empty, &tombstone);
    if (found == UINT32_MAX) {
        mako_rwmutex_runlock(&m->gate);
        return mako_str_from_cstr("");
    }

    CMapSlot *s = &m->slots[found];
    MakoString result = {
        cmap_dup_bytes(s->val, s->val_len),
        s->val_len,
    };
    mako_rwmutex_runlock(&m->gate);
    return result;
}

static inline int64_t mako_cmap_has(MakoCMap *m, MakoString key) {
    if (!m) mako_abort("cmap_has: null map");
    cmap_validate_string(key, "cmap_has: key is too large or invalid");

    uint32_t hash = cmap_hash(key.data ? key.data : "", key.len);
    mako_rwmutex_rlock(&m->gate);
    uint32_t empty;
    uint32_t tombstone;
    uint32_t found = cmap_probe_locked(m, key, hash, &empty, &tombstone);
    mako_rwmutex_runlock(&m->gate);
    return found == UINT32_MAX ? 0 : 1;
}

static inline int64_t mako_cmap_del(MakoCMap *m, MakoString key) {
    if (!m) mako_abort("cmap_del: null map");
    cmap_validate_string(key, "cmap_del: key is too large or invalid");

    uint32_t hash = cmap_hash(key.data ? key.data : "", key.len);
    mako_rwmutex_lock(&m->gate);
    uint32_t empty;
    uint32_t tombstone;
    uint32_t found = cmap_probe_locked(m, key, hash, &empty, &tombstone);
    if (found == UINT32_MAX) {
        mako_rwmutex_unlock(&m->gate);
        return 0;
    }

    CMapSlot *s = &m->slots[found];
    free(s->key);
    free(s->val);
    s->key = NULL;
    s->val = NULL;
    s->key_len = 0;
    s->val_len = 0;
    s->hash = CMAP_TOMBSTONE;
    atomic_fetch_sub_explicit(&m->len, 1, memory_order_relaxed);
    mako_rwmutex_unlock(&m->gate);
    return 1;
}

static inline int64_t mako_cmap_len(MakoCMap *m) {
    if (!m) mako_abort("cmap_len: null map");
    mako_rwmutex_rlock(&m->gate);
    int64_t len = atomic_load_explicit(&m->len, memory_order_relaxed);
    mako_rwmutex_runlock(&m->gate);
    return len;
}

/* Strict signed decimal parser.  It rejects partial parses and detects
 * overflow instead of invoking signed-integer undefined behavior. */
static inline int cmap_parse_i64(const CMapSlot *s, int64_t *out) {
    if (!s || !s->val || s->val_len == 0 || !out) return 0;
    size_t i = 0;
    int negative = s->val[0] == '-';
    if (negative) {
        i = 1;
        if (i == s->val_len) return 0;
    }

    uint64_t magnitude = 0;
    uint64_t limit = negative ? ((uint64_t)INT64_MAX + 1u) : (uint64_t)INT64_MAX;
    for (; i < s->val_len; i++) {
        unsigned char c = (unsigned char)s->val[i];
        if (c < '0' || c > '9') return 0;
        uint64_t digit = (uint64_t)(c - '0');
        if (magnitude > (limit - digit) / 10u) return 0;
        magnitude = magnitude * 10u + digit;
    }

    if (negative) {
        if (magnitude == (uint64_t)INT64_MAX + 1u) {
            *out = INT64_MIN;
        } else {
            *out = -(int64_t)magnitude;
        }
    } else {
        *out = (int64_t)magnitude;
    }
    return 1;
}

static inline int cmap_add_i64(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) ||
        (b < 0 && a < INT64_MIN - b)) return 0;
    *out = a + b;
    return 1;
}

/* Atomic increment: key must contain a strict signed decimal integer.
 * Returns INT64_MIN on parse or arithmetic overflow. */
static inline int64_t mako_cmap_incr(MakoCMap *m, MakoString key, int64_t delta) {
    if (!m) mako_abort("cmap_incr: null map");
    cmap_validate_string(key, "cmap_incr: key is too large or invalid");

    /* INT64_MIN is the error sentinel exposed by this integer-returning API;
     * reserve it rather than allowing a successful update to be ambiguous. */
    if (delta == INT64_MIN) return INT64_MIN;

    uint32_t hash = cmap_hash(key.data ? key.data : "", key.len);
    mako_rwmutex_lock(&m->gate);
    uint32_t empty;
    uint32_t tombstone;
    uint32_t found = cmap_probe_locked(m, key, hash, &empty, &tombstone);

    if (found == UINT32_MAX) {
        cmap_grow_if_needed_locked(m);
        found = cmap_probe_locked(m, key, hash, &empty, &tombstone);
        uint32_t target = tombstone != UINT32_MAX ? tombstone : empty;
        if (target == UINT32_MAX) {
            mako_rwmutex_unlock(&m->gate);
            return INT64_MIN;
        }
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "%lld", (long long)delta);
        if (n < 0 || (size_t)n >= sizeof(buf)) {
            mako_rwmutex_unlock(&m->gate);
            return INT64_MIN;
        }
        char *new_key = cmap_dup_bytes(key.data ? key.data : "", key.len);
        char *new_val = cmap_dup_bytes(buf, (size_t)n);
        CMapSlot *s = &m->slots[target];
        s->key_len = (uint32_t)key.len;
        s->val_len = (uint32_t)n;
        s->key = new_key;
        s->val = new_val;
        s->hash = hash;
        atomic_fetch_add_explicit(&m->len, 1, memory_order_relaxed);
        mako_rwmutex_unlock(&m->gate);
        return delta;
    }

    CMapSlot *s = &m->slots[found];
    int64_t current;
    int64_t next;
    if (!cmap_parse_i64(s, &current) || !cmap_add_i64(current, delta, &next) ||
        next == INT64_MIN) {
        mako_rwmutex_unlock(&m->gate);
        return INT64_MIN;
    }

    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lld", (long long)next);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        mako_rwmutex_unlock(&m->gate);
        return INT64_MIN;
    }
    char *new_val = cmap_dup_bytes(buf, (size_t)n);
    free(s->val);
    s->val = new_val;
    s->val_len = (uint32_t)n;
    mako_rwmutex_unlock(&m->gate);
    return next;
}

#endif /* MAKO_CMAP_H */
