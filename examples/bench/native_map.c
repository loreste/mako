#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Simple open-addressed map[int]int for the hand-C baseline. */
typedef struct {
    int64_t *keys;
    int64_t *vals;
    uint8_t *state; /* 0 empty, 1 full */
    size_t cap;
    size_t len;
} MapII;

static MapII map_make(size_t n) {
    size_t cap = 1;
    while (cap < n * 2) cap *= 2;
    if (cap < 16) cap = 16;
    MapII m = {0};
    m.keys = calloc(cap, sizeof(int64_t));
    m.vals = calloc(cap, sizeof(int64_t));
    m.state = calloc(cap, 1);
    m.cap = cap;
    if (!m.keys || !m.vals || !m.state) abort();
    return m;
}

static void map_set(MapII *m, int64_t k, int64_t v) {
    size_t i = (size_t)k & (m->cap - 1);
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == 0) {
            m->state[i] = 1;
            m->keys[i] = k;
            m->vals[i] = v;
            m->len++;
            return;
        }
        if (m->state[i] == 1 && m->keys[i] == k) {
            m->vals[i] = v;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
    abort();
}

static int64_t map_get(const MapII *m, int64_t k) {
    size_t i = (size_t)k & (m->cap - 1);
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == 0) return 0;
        if (m->state[i] == 1 && m->keys[i] == k) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
    return 0;
}

static void map_free(MapII *m) {
    free(m->keys);
    free(m->vals);
    free(m->state);
}

static int64_t map_checksum(int64_t n) {
    MapII m = map_make((size_t)n);
    for (int64_t i = 0; i < n; ++i) map_set(&m, i, i * 2);
    int64_t sum = 0;
    for (int64_t i = 0; i < n; ++i) sum += map_get(&m, i);
    map_free(&m);
    return sum;
}

int main(void) {
    printf("%lld\n", (long long)map_checksum(1000000));
    return 0;
}
