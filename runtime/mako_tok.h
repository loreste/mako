/* Vocabulary + BPE tokenizer — for local models you author or load.
 *
 * Supports:
 *   - JSON vocab {"token": id, ...}
 *   - line vocab (token per line, id = line index)
 *   - longest-match encode
 *   - BPE: merges file + vocab, iterative pair merge encode
 *   - decode id sequence → string
 *
 * Pair with model_* / gpu_* for text models; remote chat stays on llm_*.
 * Not full SentencePiece / tiktoken (byte-level unicode map later).
 */
#ifndef MAKO_TOK_H
#define MAKO_TOK_H

#include "mako_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAKO_TOK_MAX 4
#define MAKO_TOK_VOCAB_MAX 8192
#define MAKO_TOK_TOKEN_MAX 64
#define MAKO_TOK_MERGES_MAX 4096
#define MAKO_TOK_BPE_MAX_PIECES 512

typedef struct {
    char token[MAKO_TOK_TOKEN_MAX];
    int64_t id;
} MakoTokEntry;

typedef struct {
    char left[MAKO_TOK_TOKEN_MAX];
    char right[MAKO_TOK_TOKEN_MAX];
    int rank;
} MakoBpeMerge;

typedef struct {
    int live;
    int n;
    int64_t unk_id;
    MakoTokEntry entries[MAKO_TOK_VOCAB_MAX];
    /* reverse: id → index in entries, -1 if missing */
    int id_index[MAKO_TOK_VOCAB_MAX];
    int max_id;
    /* BPE merges (rank = load order) */
    int n_merges;
    MakoBpeMerge merges[MAKO_TOK_MERGES_MAX];
} MakoTok;

static MakoTok mako_toks[MAKO_TOK_MAX];

static inline MakoTok *mako_tok_ref(int64_t h) {
    if (h < 1 || h > MAKO_TOK_MAX) return NULL;
    MakoTok *t = &mako_toks[h - 1];
    return t->live ? t : NULL;
}

static inline int64_t mako_tok_new(void) {
    for (int i = 0; i < MAKO_TOK_MAX; i++) {
        if (!mako_toks[i].live) {
            memset(&mako_toks[i], 0, sizeof(MakoTok));
            mako_toks[i].live = 1;
            mako_toks[i].unk_id = 0;
            for (int j = 0; j < MAKO_TOK_VOCAB_MAX; j++) mako_toks[i].id_index[j] = -1;
            return (int64_t)(i + 1);
        }
    }
    return -1;
}

static inline int64_t mako_tok_free(int64_t h) {
    MakoTok *t = mako_tok_ref(h);
    if (!t) return 0;
    memset(t, 0, sizeof(MakoTok));
    return 1;
}

static inline void mako_tok_reindex(MakoTok *t) {
    for (int j = 0; j < MAKO_TOK_VOCAB_MAX; j++) t->id_index[j] = -1;
    t->max_id = 0;
    for (int i = 0; i < t->n; i++) {
        int64_t id = t->entries[i].id;
        if (id >= 0 && id < MAKO_TOK_VOCAB_MAX) {
            t->id_index[id] = i;
            if ((int)id > t->max_id) t->max_id = (int)id;
        }
    }
}

static inline int64_t mako_tok_set(int64_t h, MakoString token, int64_t id) {
    MakoTok *t = mako_tok_ref(h);
    if (!t || !token.data || token.len == 0 || token.len >= MAKO_TOK_TOKEN_MAX) return 0;
    if (id < 0 || id >= MAKO_TOK_VOCAB_MAX) return 0;
    char key[MAKO_TOK_TOKEN_MAX];
    memcpy(key, token.data, token.len);
    key[token.len] = 0;
    for (int i = 0; i < t->n; i++) {
        if (strcmp(t->entries[i].token, key) == 0) {
            t->entries[i].id = id;
            mako_tok_reindex(t);
            return 1;
        }
    }
    if (t->n >= MAKO_TOK_VOCAB_MAX) return 0;
    snprintf(t->entries[t->n].token, sizeof(t->entries[t->n].token), "%s", key);
    t->entries[t->n].id = id;
    t->n++;
    if (strcmp(key, "<unk>") == 0 || strcmp(key, "[UNK]") == 0) t->unk_id = id;
    mako_tok_reindex(t);
    return 1;
}

static inline int64_t mako_tok_size(int64_t h) {
    MakoTok *t = mako_tok_ref(h);
    return t ? (int64_t)t->n : -1;
}

static inline int64_t mako_tok_id(int64_t h, MakoString token) {
    MakoTok *t = mako_tok_ref(h);
    if (!t || !token.data) return -1;
    char key[MAKO_TOK_TOKEN_MAX];
    size_t n = token.len < sizeof(key) - 1 ? token.len : sizeof(key) - 1;
    memcpy(key, token.data, n);
    key[n] = 0;
    for (int i = 0; i < t->n; i++) {
        if (strcmp(t->entries[i].token, key) == 0) return t->entries[i].id;
    }
    return t->unk_id;
}

static inline MakoString mako_tok_token(int64_t h, int64_t id) {
    MakoTok *t = mako_tok_ref(h);
    if (!t || id < 0 || id >= MAKO_TOK_VOCAB_MAX) return mako_str_from_cstr("");
    int idx = t->id_index[id];
    if (idx < 0) return mako_str_from_cstr("");
    return mako_str_from_cstr(t->entries[idx].token);
}

/* Load JSON object: "tok": id, ... (minimal scanner). */
static inline int64_t mako_tok_load_json(int64_t h, MakoString path) {
    MakoTok *t = mako_tok_ref(h);
    if (!t || !path.data) return 0;
    char pth[1024];
    size_t pl = path.len < sizeof(pth) - 1 ? path.len : sizeof(pth) - 1;
    memcpy(pth, path.data, pl);
    pth[pl] = 0;
    FILE *f = fopen(pth, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long sz = ftell(f);
    if (sz <= 0 || sz > 8 * 1024 * 1024) {
        fclose(f);
        return 0;
    }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return 0;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return 0;
    }
    buf[sz] = 0;
    fclose(f);
    const char *p = buf;
    const char *end = buf + sz;
    while (p < end && *p != '{') p++;
    if (p >= end) {
        free(buf);
        return 0;
    }
    p++;
    int loaded = 0;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ','))
            p++;
        if (p < end && *p == '}') break;
        if (p >= end || *p != '"') break;
        p++;
        char tok[MAKO_TOK_TOKEN_MAX];
        size_t tn = 0;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) p++;
            if (tn + 1 < sizeof(tok)) tok[tn++] = *p;
            p++;
        }
        tok[tn] = 0;
        if (p >= end || *p != '"') break;
        p++;
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p >= end || *p != ':') break;
        p++;
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        int neg = 0;
        if (p < end && *p == '-') {
            neg = 1;
            p++;
        }
        if (p >= end || !isdigit((unsigned char)*p)) break;
        int64_t id = 0;
        while (p < end && isdigit((unsigned char)*p)) {
            id = id * 10 + (*p - '0');
            p++;
        }
        if (neg) id = -id;
        MakoString ts = mako_str_from_cstr(tok);
        if (mako_tok_set(h, ts, id)) loaded++;
        free(ts.data);
    }
    free(buf);
    return loaded > 0 ? 1 : 0;
}

/* One token per line; id = 0-based line index. */
static inline int64_t mako_tok_load_lines(int64_t h, MakoString path) {
    MakoTok *t = mako_tok_ref(h);
    if (!t || !path.data) return 0;
    char pth[1024];
    size_t pl = path.len < sizeof(pth) - 1 ? path.len : sizeof(pth) - 1;
    memcpy(pth, path.data, pl);
    pth[pl] = 0;
    FILE *f = fopen(pth, "r");
    if (!f) return 0;
    char line[MAKO_TOK_TOKEN_MAX + 8];
    int64_t id = 0;
    int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (n == 0) continue;
        MakoString ts = mako_str_from_cstr(line);
        if (mako_tok_set(h, ts, id)) loaded++;
        free(ts.data);
        id++;
    }
    fclose(f);
    return loaded > 0 ? 1 : 0;
}

/* Longest-match encode left-to-right. Returns []int of token ids. */
static inline MakoIntArray mako_tok_encode(int64_t h, MakoString text) {
    MakoTok *t = mako_tok_ref(h);
    MakoIntArray out = mako_int_array_make(0, 16);
    if (!t || !text.data || text.len == 0) return out;
    size_t i = 0;
    while (i < text.len) {
        int best_len = 0;
        int64_t best_id = t->unk_id;
        for (int e = 0; e < t->n; e++) {
            size_t tl = strlen(t->entries[e].token);
            if (tl == 0 || tl > text.len - i) continue;
            if (tl > (size_t)best_len && memcmp(text.data + i, t->entries[e].token, tl) == 0) {
                best_len = (int)tl;
                best_id = t->entries[e].id;
            }
        }
        if (best_len == 0) {
            /* single byte / char as unk and advance 1 */
            out = mako_slice_append(out, t->unk_id);
            i++;
        } else {
            out = mako_slice_append(out, best_id);
            i += (size_t)best_len;
        }
    }
    return out;
}

static inline MakoString mako_tok_decode(int64_t h, MakoIntArray ids) {
    MakoTok *t = mako_tok_ref(h);
    if (!t || ids.len == 0) return mako_str_from_cstr("");
    size_t cap = ids.len * MAKO_TOK_TOKEN_MAX + 1;
    char *buf = (char *)malloc(cap);
    if (!buf) mako_abort("tok decode OOM");
    size_t n = 0;
    for (size_t i = 0; i < ids.len; i++) {
        int64_t id = ids.data[i];
        const char *tok = "";
        if (id >= 0 && id < MAKO_TOK_VOCAB_MAX && t->id_index[id] >= 0)
            tok = t->entries[t->id_index[id]].token;
        size_t tl = strlen(tok);
        if (n + tl >= cap) break;
        memcpy(buf + n, tok, tl);
        n += tl;
    }
    buf[n] = 0;
    return (MakoString){buf, n};
}

/* Load BPE merges file: optional #version line, then "left right" pairs. */
static inline int64_t mako_tok_load_merges(int64_t h, MakoString path) {
    MakoTok *t = mako_tok_ref(h);
    if (!t || !path.data) return 0;
    char pth[1024];
    size_t pl = path.len < sizeof(pth) - 1 ? path.len : sizeof(pth) - 1;
    memcpy(pth, path.data, pl);
    pth[pl] = 0;
    FILE *f = fopen(pth, "r");
    if (!f) return 0;
    char line[2 * MAKO_TOK_TOKEN_MAX + 8];
    int rank = 0;
    int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (n == 0 || line[0] == '#') continue;
        char *sp = strchr(line, ' ');
        if (!sp) continue;
        *sp = 0;
        const char *left = line;
        const char *right = sp + 1;
        while (*right == ' ') right++;
        if (!*left || !*right) continue;
        if (t->n_merges >= MAKO_TOK_MERGES_MAX) break;
        if (strlen(left) >= MAKO_TOK_TOKEN_MAX || strlen(right) >= MAKO_TOK_TOKEN_MAX) continue;
        snprintf(t->merges[t->n_merges].left, sizeof(t->merges[0].left), "%s", left);
        snprintf(t->merges[t->n_merges].right, sizeof(t->merges[0].right), "%s", right);
        t->merges[t->n_merges].rank = rank++;
        t->n_merges++;
        loaded++;
    }
    fclose(f);
    return loaded > 0 ? 1 : 0;
}

/* Load vocab JSON + merges (BPE setup). */
static inline int64_t mako_tok_load_bpe(
    int64_t h, MakoString vocab_path, MakoString merges_path
) {
    if (!mako_tok_load_json(h, vocab_path)) return 0;
    if (!mako_tok_load_merges(h, merges_path)) return 0;
    return 1;
}

static inline int mako_tok_merge_rank(MakoTok *t, const char *a, const char *b) {
    for (int i = 0; i < t->n_merges; i++) {
        if (strcmp(t->merges[i].left, a) == 0 && strcmp(t->merges[i].right, b) == 0)
            return t->merges[i].rank;
    }
    return -1;
}

/* BPE encode: start from single characters, apply merges by rank, map to ids. */
static inline MakoIntArray mako_tok_encode_bpe(int64_t h, MakoString text) {
    MakoTok *t = mako_tok_ref(h);
    MakoIntArray out = mako_int_array_make(0, 16);
    if (!t || !text.data || text.len == 0) return out;
    if (t->n_merges == 0) {
        /* fall back to longest-match */
        return mako_tok_encode(h, text);
    }

    char pieces[MAKO_TOK_BPE_MAX_PIECES][MAKO_TOK_TOKEN_MAX];
    int np = 0;
    for (size_t i = 0; i < text.len && np < MAKO_TOK_BPE_MAX_PIECES; i++) {
        pieces[np][0] = text.data[i];
        pieces[np][1] = 0;
        np++;
    }

    for (;;) {
        int best_rank = 0x7fffffff;
        int best_i = -1;
        for (int i = 0; i + 1 < np; i++) {
            int r = mako_tok_merge_rank(t, pieces[i], pieces[i + 1]);
            if (r >= 0 && r < best_rank) {
                best_rank = r;
                best_i = i;
            }
        }
        if (best_i < 0) break;
        /* merge pieces[best_i] + pieces[best_i+1] */
        char merged[MAKO_TOK_TOKEN_MAX];
        size_t la = strlen(pieces[best_i]);
        size_t lb = strlen(pieces[best_i + 1]);
        if (la + lb >= MAKO_TOK_TOKEN_MAX) break;
        memcpy(merged, pieces[best_i], la);
        memcpy(merged + la, pieces[best_i + 1], lb + 1);
        snprintf(pieces[best_i], sizeof(pieces[0]), "%s", merged);
        for (int j = best_i + 1; j + 1 < np; j++) {
            memcpy(pieces[j], pieces[j + 1], MAKO_TOK_TOKEN_MAX);
        }
        np--;
    }

    for (int i = 0; i < np; i++) {
        int64_t id = t->unk_id;
        for (int e = 0; e < t->n; e++) {
            if (strcmp(t->entries[e].token, pieces[i]) == 0) {
                id = t->entries[e].id;
                break;
            }
        }
        out = mako_slice_append(out, id);
    }
    return out;
}

static inline int64_t mako_tok_merge_count(int64_t h) {
    MakoTok *t = mako_tok_ref(h);
    return t ? (int64_t)t->n_merges : -1;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_TOK_H */
