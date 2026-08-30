#ifndef MAKO_UNICODE17_H
#define MAKO_UNICODE17_H

#include "mako_rt.h"

#if !MAKO_UNICODE17
#error "mako_unicode17.h requires MAKO_UNICODE17=1"
#endif

typedef struct {
    uint32_t *data;
    size_t len;
    size_t cap;
} MakoUnicodeScalars;

static inline void mako_u17_scalars_reserve(MakoUnicodeScalars *v, size_t need) {
    if (need <= v->cap) return;
    if (need > SIZE_MAX / sizeof(uint32_t)) mako_abort("unicode scalar buffer overflow");
    size_t cap = v->cap ? v->cap : 16;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) { cap = need; break; }
        cap *= 2;
    }
    uint32_t *next = (uint32_t *)realloc(v->data, cap * sizeof(uint32_t));
    if (!next) mako_abort("unicode scalar buffer OOM");
    v->data = next;
    v->cap = cap;
}

static inline void mako_u17_scalars_push(MakoUnicodeScalars *v, uint32_t cp) {
    if (v->len == SIZE_MAX) mako_abort("unicode scalar length overflow");
    mako_u17_scalars_reserve(v, v->len + 1);
    v->data[v->len++] = cp;
}

static inline const MakoUnicodeDecomposition *mako_u17_find_decomposition(uint32_t cp) {
    size_t lo = 0, hi = sizeof(mako_u17_decompositions) / sizeof(mako_u17_decompositions[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < mako_u17_decompositions[mid].cp) hi = mid;
        else if (cp > mako_u17_decompositions[mid].cp) lo = mid + 1;
        else return &mako_u17_decompositions[mid];
    }
    return NULL;
}

static inline uint8_t mako_u17_combining_class(uint32_t cp) {
    size_t lo = 0, hi = sizeof(mako_u17_combining_map) / sizeof(mako_u17_combining_map[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < mako_u17_combining_map[mid].key) hi = mid;
        else if (cp > mako_u17_combining_map[mid].key) lo = mid + 1;
        else return (uint8_t)mako_u17_combining_map[mid].value;
    }
    return 0;
}

static inline void mako_u17_decompose_scalar(
    MakoUnicodeScalars *out, uint32_t cp, int compatibility, unsigned depth
) {
    enum { SBASE = 0xAC00, LBASE = 0x1100, VBASE = 0x1161, TBASE = 0x11A7,
           LCOUNT = 19, VCOUNT = 21, TCOUNT = 28, NCOUNT = VCOUNT * TCOUNT,
           SCOUNT = LCOUNT * NCOUNT };
    if (cp >= SBASE && cp < SBASE + SCOUNT) {
        uint32_t index = cp - SBASE;
        mako_u17_scalars_push(out, LBASE + index / NCOUNT);
        mako_u17_scalars_push(out, VBASE + (index % NCOUNT) / TCOUNT);
        if (index % TCOUNT) mako_u17_scalars_push(out, TBASE + index % TCOUNT);
        return;
    }
    if (depth >= 64) mako_abort("unicode decomposition depth exceeded");
    const MakoUnicodeDecomposition *entry = mako_u17_find_decomposition(cp);
    if (!entry || (entry->compatibility && !compatibility)) {
        mako_u17_scalars_push(out, cp);
        return;
    }
    size_t data_len = sizeof(mako_u17_decomposition_data) / sizeof(mako_u17_decomposition_data[0]);
    if (entry->offset > data_len || entry->len > data_len - entry->offset)
        mako_abort("unicode decomposition table corrupt");
    for (size_t i = 0; i < entry->len; i++)
        mako_u17_decompose_scalar(out, mako_u17_decomposition_data[entry->offset + i], compatibility, depth + 1);
}

static inline void mako_u17_canonical_order(MakoUnicodeScalars *v) {
    if (v->len < 2) return;
    if (v->len > SIZE_MAX / sizeof(uint32_t)) mako_abort("unicode reorder overflow");
    uint32_t *scratch = (uint32_t *)malloc(v->len * sizeof(uint32_t));
    if (!scratch) mako_abort("unicode reorder OOM");
    size_t start = 0;
    while (start < v->len) {
        size_t marks = start + 1;
        while (marks < v->len && mako_u17_combining_class(v->data[marks]) != 0) marks++;
        size_t first_mark = mako_u17_combining_class(v->data[start]) == 0 ? start + 1 : start;
        if (marks - first_mark > 1) {
            size_t counts[256] = {0};
            size_t offsets[256];
            for (size_t i = first_mark; i < marks; i++) counts[mako_u17_combining_class(v->data[i])]++;
            size_t offset = first_mark;
            for (size_t c = 0; c < 256; c++) { offsets[c] = offset; offset += counts[c]; }
            for (size_t i = first_mark; i < marks; i++) {
                uint8_t c = mako_u17_combining_class(v->data[i]);
                scratch[offsets[c]++] = v->data[i];
            }
            memcpy(v->data + first_mark, scratch + first_mark, (marks - first_mark) * sizeof(uint32_t));
        }
        start = marks;
    }
    free(scratch);
}

static inline uint32_t mako_u17_compose_pair(uint32_t first, uint32_t second) {
    enum { SBASE = 0xAC00, LBASE = 0x1100, VBASE = 0x1161, TBASE = 0x11A7,
           LCOUNT = 19, VCOUNT = 21, TCOUNT = 28, NCOUNT = VCOUNT * TCOUNT,
           SCOUNT = LCOUNT * NCOUNT };
    if (first >= LBASE && first < LBASE + LCOUNT && second >= VBASE && second < VBASE + VCOUNT)
        return SBASE + ((first - LBASE) * VCOUNT + (second - VBASE)) * TCOUNT;
    if (first >= SBASE && first < SBASE + SCOUNT && (first - SBASE) % TCOUNT == 0 &&
        second > TBASE && second < TBASE + TCOUNT)
        return first + second - TBASE;
    size_t lo = 0, hi = sizeof(mako_u17_compositions) / sizeof(mako_u17_compositions[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const MakoUnicodeComposition *entry = &mako_u17_compositions[mid];
        if (first < entry->first || (first == entry->first && second < entry->second)) hi = mid;
        else if (first > entry->first || (first == entry->first && second > entry->second)) lo = mid + 1;
        else return entry->composed;
    }
    return 0;
}

static inline void mako_u17_compose(MakoUnicodeScalars *v) {
    if (v->len < 2) return;
    size_t out = 1, starter = 0;
    uint8_t previous_class = 0;
    for (size_t i = 1; i < v->len; i++) {
        uint32_t cp = v->data[i];
        uint8_t current_class = mako_u17_combining_class(cp);
        uint32_t composed = (previous_class == 0 || previous_class < current_class)
            ? mako_u17_compose_pair(v->data[starter], cp) : 0;
        if (composed) {
            v->data[starter] = composed;
        } else {
            if (current_class == 0) starter = out;
            v->data[out++] = cp;
            previous_class = current_class;
        }
    }
    v->len = out;
}

static inline size_t mako_u17_utf8_size(uint32_t cp) {
    return cp <= 0x7F ? 1 : cp <= 0x7FF ? 2 : cp <= 0xFFFF ? 3 : 4;
}

static inline MakoString mako_unicode_normalize(MakoString input, int64_t form) {
    if (form < 0 || form > 3) mako_abort("unicode normalization form must be 0..3");
    MakoUnicodeScalars scalars = {0};
    for (size_t offset = 0; offset < input.len;) {
        int64_t decoded = 0;
        size_t width = mako_utf8_decode(input.data, input.len, offset, &decoded);
        if (width == 0 || width > input.len - offset) { width = 1; decoded = 0xFFFD; }
        mako_u17_decompose_scalar(&scalars, (uint32_t)decoded, form >= 2, 0);
        offset += width;
    }
    mako_u17_canonical_order(&scalars);
    if (form == 1 || form == 3) mako_u17_compose(&scalars);
    size_t bytes = 0;
    for (size_t i = 0; i < scalars.len; i++) {
        size_t width = mako_u17_utf8_size(scalars.data[i]);
        if (bytes > SIZE_MAX - width) mako_abort("unicode UTF-8 size overflow");
        bytes += width;
    }
    if (bytes == SIZE_MAX) mako_abort("unicode UTF-8 allocation overflow");
    char *data = (char *)malloc(bytes + 1);
    if (!data) mako_abort("unicode normalization OOM");
    size_t offset = 0;
    for (size_t i = 0; i < scalars.len; i++) {
        uint32_t cp = scalars.data[i];
        if (cp <= 0x7F) data[offset++] = (char)cp;
        else if (cp <= 0x7FF) {
            data[offset++] = (char)(0xC0 | (cp >> 6)); data[offset++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            data[offset++] = (char)(0xE0 | (cp >> 12)); data[offset++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            data[offset++] = (char)(0x80 | (cp & 0x3F));
        } else {
            data[offset++] = (char)(0xF0 | (cp >> 18)); data[offset++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            data[offset++] = (char)(0x80 | ((cp >> 6) & 0x3F)); data[offset++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    data[offset] = '\0';
    free(scalars.data);
    return (MakoString){data, bytes};
}

static inline MakoString mako_unicode_nfd(MakoString input) { return mako_unicode_normalize(input, 0); }
static inline MakoString mako_unicode_nfc(MakoString input) { return mako_unicode_normalize(input, 1); }
static inline MakoString mako_unicode_nfkd(MakoString input) { return mako_unicode_normalize(input, 2); }
static inline MakoString mako_unicode_nfkc(MakoString input) { return mako_unicode_normalize(input, 3); }

#endif
