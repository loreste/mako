/* Mako templates — Go-inspired text/html templates.
 *
 * Syntax (subset of Go text/template, Mako-native surface):
 *   {{.key}}              interpolate data key (also {{key}})
 *   {{.}}                 current range/with value
 *   {{if .key}}…{{else}}…{{end}}
 *   {{range .key}}…{{end}}   list values (set via tmpl_data_set_list)
 *   {{with .key}}…{{end}}
 *   {{define "name"}}…{{end}}
 *   {{template "name"}}
 *   {{/* comment *\/}}
 *   {{printf "%s" .key}}  limited: %s and one arg
 *   {{len .key}} {{upper .key}} {{lower .key}} {{html .key}}
 *
 * HTML mode (tmpl_html_execute): auto-escapes interpolated values.
 * Packs: std/text/template · std/html/template
 */
#ifndef MAKO_TEMPLATE_H
#define MAKO_TEMPLATE_H

#include "mako_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAKO_TMPL_MAX_DATA 16
#define MAKO_TMPL_MAX_KEYS 64
#define MAKO_TMPL_MAX_LIST 128
#define MAKO_TMPL_MAX_DEF 32
#define MAKO_TMPL_MAX_T 16
#define MAKO_TMPL_KEY 64
#define MAKO_TMPL_STACK 16

/* ---- Data bag ---- */

typedef struct {
    char key[MAKO_TMPL_KEY];
    int is_list;
    char *value; /* scalar owned */
    char *items[MAKO_TMPL_MAX_LIST];
    int n_items;
} MakoTmplKV;

typedef struct {
    int live;
    int n;
    MakoTmplKV kv[MAKO_TMPL_MAX_KEYS];
} MakoTmplData;

static MakoTmplData mako_tmpl_data_tab[MAKO_TMPL_MAX_DATA];

static inline MakoTmplData *mako_tmpl_data_ref(int64_t h) {
    if (h < 1 || h > MAKO_TMPL_MAX_DATA) return NULL;
    MakoTmplData *d = &mako_tmpl_data_tab[h - 1];
    return d->live ? d : NULL;
}

static inline int64_t mako_tmpl_data_new(void) {
    for (int i = 0; i < MAKO_TMPL_MAX_DATA; i++) {
        if (!mako_tmpl_data_tab[i].live) {
            memset(&mako_tmpl_data_tab[i], 0, sizeof(MakoTmplData));
            mako_tmpl_data_tab[i].live = 1;
            return (int64_t)(i + 1);
        }
    }
    return -1;
}

static inline void mako_tmpl_kv_clear(MakoTmplKV *e) {
    free(e->value);
    e->value = NULL;
    for (int i = 0; i < e->n_items; i++) {
        free(e->items[i]);
        e->items[i] = NULL;
    }
    e->n_items = 0;
    e->is_list = 0;
    e->key[0] = 0;
}

static inline int64_t mako_tmpl_data_free(int64_t h) {
    MakoTmplData *d = mako_tmpl_data_ref(h);
    if (!d) return 0;
    for (int i = 0; i < d->n; i++) mako_tmpl_kv_clear(&d->kv[i]);
    memset(d, 0, sizeof(MakoTmplData));
    return 1;
}

static inline MakoTmplKV *mako_tmpl_data_find(MakoTmplData *d, const char *key) {
    for (int i = 0; i < d->n; i++) {
        if (strcmp(d->kv[i].key, key) == 0) return &d->kv[i];
    }
    return NULL;
}

static inline MakoTmplKV *mako_tmpl_data_ensure(MakoTmplData *d, const char *key) {
    MakoTmplKV *e = mako_tmpl_data_find(d, key);
    if (e) {
        mako_tmpl_kv_clear(e);
        snprintf(e->key, sizeof(e->key), "%s", key);
        return e;
    }
    if (d->n >= MAKO_TMPL_MAX_KEYS) return NULL;
    e = &d->kv[d->n++];
    memset(e, 0, sizeof(*e));
    snprintf(e->key, sizeof(e->key), "%s", key);
    return e;
}

static inline void mako_tmpl_key_from_str(char *out, size_t cap, MakoString s) {
    size_t n = s.len < cap - 1 ? s.len : cap - 1;
    if (n && s.data) memcpy(out, s.data, n);
    out[n] = 0;
    /* strip leading . for Go-style .Name */
    if (out[0] == '.') {
        memmove(out, out + 1, strlen(out));
    }
}

static inline int64_t mako_tmpl_data_set(int64_t h, MakoString key, MakoString val) {
    MakoTmplData *d = mako_tmpl_data_ref(h);
    if (!d) return 0;
    char k[MAKO_TMPL_KEY];
    mako_tmpl_key_from_str(k, sizeof(k), key);
    if (!k[0]) return 0;
    MakoTmplKV *e = mako_tmpl_data_ensure(d, k);
    if (!e) return 0;
    e->is_list = 0;
    e->value = (char *)malloc(val.len + 1);
    if (!e->value) return 0;
    if (val.len && val.data) memcpy(e->value, val.data, val.len);
    e->value[val.len] = 0;
    return 1;
}

/* List as CSV: "a,b,c" or newline-separated. */
static inline int64_t mako_tmpl_data_set_list(int64_t h, MakoString key, MakoString csv) {
    MakoTmplData *d = mako_tmpl_data_ref(h);
    if (!d) return 0;
    char k[MAKO_TMPL_KEY];
    mako_tmpl_key_from_str(k, sizeof(k), key);
    if (!k[0]) return 0;
    MakoTmplKV *e = mako_tmpl_data_ensure(d, k);
    if (!e) return 0;
    e->is_list = 1;
    const char *p = csv.data ? csv.data : "";
    size_t left = csv.len;
    while (left > 0 && e->n_items < MAKO_TMPL_MAX_LIST) {
        size_t i = 0;
        while (i < left && p[i] != ',' && p[i] != '\n' && p[i] != '\r') i++;
        /* trim spaces */
        size_t a = 0, b = i;
        while (a < b && (p[a] == ' ' || p[a] == '\t')) a++;
        while (b > a && (p[b - 1] == ' ' || p[b - 1] == '\t')) b--;
        char *item = (char *)malloc(b - a + 1);
        if (!item) break;
        if (b > a) memcpy(item, p + a, b - a);
        item[b - a] = 0;
        e->items[e->n_items++] = item;
        if (i >= left) break;
        p += i + 1;
        left -= i + 1;
        if (left && p[-1] == '\r' && p[0] == '\n') {
            p++;
            left--;
        }
    }
    return 1;
}

static inline int64_t mako_tmpl_data_set_int(int64_t h, MakoString key, int64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
    MakoString vs = mako_str_from_cstr(buf);
    int64_t rc = mako_tmpl_data_set(h, key, vs);
    free(vs.data);
    return rc;
}

/* ---- Parsed template (source + defines) ---- */

typedef struct {
    char name[MAKO_TMPL_KEY];
    char *body;
    size_t body_len;
} MakoTmplDef;

typedef struct {
    int live;
    char *src;
    size_t src_len;
    int n_def;
    MakoTmplDef defs[MAKO_TMPL_MAX_DEF];
} MakoTmpl;

static MakoTmpl mako_tmpl_tab[MAKO_TMPL_MAX_T];

static inline MakoTmpl *mako_tmpl_ref(int64_t h) {
    if (h < 1 || h > MAKO_TMPL_MAX_T) return NULL;
    MakoTmpl *t = &mako_tmpl_tab[h - 1];
    return t->live ? t : NULL;
}

static inline void mako_tmpl_clear(MakoTmpl *t) {
    free(t->src);
    for (int i = 0; i < t->n_def; i++) free(t->defs[i].body);
    memset(t, 0, sizeof(*t));
}

/* Extract {{define "name"}}…{{end}} into defs; leave remainder as main src. */
static inline int mako_tmpl_extract_defines(MakoTmpl *t) {
    char *src = t->src;
    size_t len = t->src_len;
    char *out = (char *)malloc(len + 1);
    if (!out) return 0;
    size_t o = 0;
    size_t i = 0;
    while (i < len) {
        if (i + 10 <= len && strncmp(src + i, "{{define", 8) == 0) {
            size_t j = i + 8;
            while (j < len && (src[j] == ' ' || src[j] == '\t')) j++;
            char q = 0;
            if (j < len && (src[j] == '"' || src[j] == '\'')) q = src[j++];
            char name[MAKO_TMPL_KEY];
            size_t ni = 0;
            while (j < len && src[j] != q && src[j] != '}' && ni + 1 < sizeof(name))
                name[ni++] = src[j++];
            name[ni] = 0;
            if (q && j < len && src[j] == q) j++;
            while (j < len && src[j] != '}') j++;
            if (j + 1 < len && src[j] == '}' && src[j + 1] == '}') j += 2;
            else {
                out[o++] = src[i++];
                continue;
            }
            /* find matching {{end}} with nesting */
            size_t body_start = j;
            int depth = 1;
            size_t k = j;
            while (k + 1 < len && depth > 0) {
                if (src[k] == '{' && src[k + 1] == '{') {
                    size_t m = k + 2;
                    while (m < len && (src[m] == ' ' || src[m] == '\t')) m++;
                    if (m + 2 <= len && strncmp(src + m, "if", 2) == 0
                        && (m + 2 == len || !isalnum((unsigned char)src[m + 2])))
                        depth++;
                    else if (m + 5 <= len && strncmp(src + m, "range", 5) == 0)
                        depth++;
                    else if (m + 4 <= len && strncmp(src + m, "with", 4) == 0)
                        depth++;
                    else if (m + 6 <= len && strncmp(src + m, "define", 6) == 0)
                        depth++;
                    else if (m + 3 <= len && strncmp(src + m, "end", 3) == 0
                             && (m + 3 >= len || !isalnum((unsigned char)src[m + 3]))) {
                        depth--;
                        if (depth == 0) {
                            size_t body_len = k - body_start;
                            if (t->n_def < MAKO_TMPL_MAX_DEF) {
                                MakoTmplDef *def = &t->defs[t->n_def++];
                                snprintf(def->name, sizeof(def->name), "%s", name);
                                def->body = (char *)malloc(body_len + 1);
                                if (def->body) {
                                    memcpy(def->body, src + body_start, body_len);
                                    def->body[body_len] = 0;
                                    def->body_len = body_len;
                                }
                            }
                            /* skip {{end}} */
                            while (k + 1 < len && !(src[k] == '}' && src[k + 1] == '}')) k++;
                            if (k + 1 < len) k += 2;
                            i = k;
                            break;
                        }
                    }
                }
                k++;
            }
            if (depth != 0) {
                out[o++] = src[i++];
            }
            continue;
        }
        out[o++] = src[i++];
    }
    out[o] = 0;
    free(t->src);
    t->src = out;
    t->src_len = o;
    return 1;
}

static inline int64_t mako_tmpl_new(MakoString source) {
    for (int i = 0; i < MAKO_TMPL_MAX_T; i++) {
        if (!mako_tmpl_tab[i].live) {
            MakoTmpl *t = &mako_tmpl_tab[i];
            memset(t, 0, sizeof(*t));
            t->live = 1;
            t->src = (char *)malloc(source.len + 1);
            if (!t->src) {
                t->live = 0;
                return -1;
            }
            if (source.len && source.data) memcpy(t->src, source.data, source.len);
            t->src[source.len] = 0;
            t->src_len = source.len;
            mako_tmpl_extract_defines(t);
            return (int64_t)(i + 1);
        }
    }
    return -1;
}

static inline int64_t mako_tmpl_free(int64_t h) {
    MakoTmpl *t = mako_tmpl_ref(h);
    if (!t) return 0;
    mako_tmpl_clear(t);
    return 1;
}

/* ---- Execute ---- */

typedef struct {
    MakoTmplData *data;
    const char *dot; /* current . value */
    int html_escape;
    MakoTmpl *tmpl;
    char *out;
    size_t out_len, out_cap;
} MakoTmplCtx;

static inline void mako_tmpl_out_append(MakoTmplCtx *c, const char *s, size_t n) {
    if (c->out_len + n + 1 > c->out_cap) {
        size_t nc = c->out_cap ? c->out_cap * 2 : 256;
        while (nc < c->out_len + n + 1) nc *= 2;
        char *nb = (char *)realloc(c->out, nc);
        if (!nb) return;
        c->out = nb;
        c->out_cap = nc;
    }
    if (n) memcpy(c->out + c->out_len, s, n);
    c->out_len += n;
    c->out[c->out_len] = 0;
}

static inline void mako_tmpl_out_appends(MakoTmplCtx *c, const char *s) {
    mako_tmpl_out_append(c, s, strlen(s));
}

static inline MakoString mako_tmpl_html_escape_cstr(const char *s) {
    MakoString in = mako_str_from_cstr(s ? s : "");
    MakoString out = mako_html_escape(in);
    free(in.data);
    return out;
}

static inline const char *mako_tmpl_lookup(MakoTmplCtx *c, const char *key) {
    if (!key || !key[0] || strcmp(key, ".") == 0) return c->dot ? c->dot : "";
    if (key[0] == '.') key++;
    if (!c->data) return "";
    MakoTmplKV *e = mako_tmpl_data_find(c->data, key);
    if (!e) return "";
    if (e->is_list) return e->n_items ? (e->items[0] ? e->items[0] : "") : "";
    return e->value ? e->value : "";
}

static inline int mako_tmpl_truthy(const char *v) {
    if (!v || !v[0]) return 0;
    if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0 || strcmp(v, "False") == 0) return 0;
    return 1;
}

static inline void mako_tmpl_emit_value(MakoTmplCtx *c, const char *v) {
    if (!v) v = "";
    if (c->html_escape) {
        MakoString esc = mako_tmpl_html_escape_cstr(v);
        mako_tmpl_out_append(c, esc.data, esc.len);
        free(esc.data);
    } else {
        mako_tmpl_out_appends(c, v);
    }
}

/* trim spaces from action inner */
static inline void mako_tmpl_trim(const char **s, size_t *n) {
    while (*n && ((*s)[0] == ' ' || (*s)[0] == '\t' || (*s)[0] == '\n' || (*s)[0] == '\r')) {
        (*s)++;
        (*n)--;
    }
    while (*n
           && ((*s)[*n - 1] == ' ' || (*s)[*n - 1] == '\t' || (*s)[*n - 1] == '\n'
               || (*s)[*n - 1] == '\r'))
        (*n)--;
}

static int mako_tmpl_exec_body(MakoTmplCtx *c, const char *src, size_t len);

/* Find matching {{end}} from position after an opener; returns index of {{end}} and length of end tag. */
static inline int mako_tmpl_find_end(
    const char *src, size_t len, size_t start, size_t *end_pos, size_t *end_tag_len
) {
    int depth = 1;
    size_t i = start;
    while (i + 1 < len) {
        if (src[i] == '{' && src[i + 1] == '{') {
            size_t j = i + 2;
            while (j < len && (src[j] == ' ' || src[j] == '\t')) j++;
            if (j + 3 <= len && strncmp(src + j, "end", 3) == 0
                && (j + 3 >= len || !isalnum((unsigned char)src[j + 3]))) {
                depth--;
                size_t k = j + 3;
                while (k + 1 < len && !(src[k] == '}' && src[k + 1] == '}')) k++;
                if (k + 1 >= len) return 0;
                if (depth == 0) {
                    *end_pos = i;
                    *end_tag_len = (k + 2) - i;
                    return 1;
                }
                i = k + 2;
                continue;
            }
            if ((j + 2 <= len && strncmp(src + j, "if", 2) == 0
                 && (j + 2 >= len || !isalnum((unsigned char)src[j + 2])))
                || (j + 5 <= len && strncmp(src + j, "range", 5) == 0)
                || (j + 4 <= len && strncmp(src + j, "with", 4) == 0)
                || (j + 6 <= len && strncmp(src + j, "define", 6) == 0)) {
                depth++;
            }
        }
        i++;
    }
    return 0;
}

/* Find {{else}} at depth 1 within [start, end). */
static inline int mako_tmpl_find_else(
    const char *src, size_t start, size_t end, size_t *else_pos, size_t *else_tag_len
) {
    int depth = 1;
    size_t i = start;
    while (i + 1 < end) {
        if (src[i] == '{' && src[i + 1] == '{') {
            size_t j = i + 2;
            while (j < end && (src[j] == ' ' || src[j] == '\t')) j++;
            if (j + 3 <= end && strncmp(src + j, "end", 3) == 0) {
                depth--;
                size_t k = j + 3;
                while (k + 1 < end && !(src[k] == '}' && src[k + 1] == '}')) k++;
                i = k + 2;
                continue;
            }
            if (depth == 1 && j + 4 <= end && strncmp(src + j, "else", 4) == 0
                && (j + 4 >= end || !isalnum((unsigned char)src[j + 4]))) {
                size_t k = j + 4;
                while (k + 1 < end && !(src[k] == '}' && src[k + 1] == '}')) k++;
                if (k + 1 >= end) return 0;
                *else_pos = i;
                *else_tag_len = (k + 2) - i;
                return 1;
            }
            if ((j + 2 <= end && strncmp(src + j, "if", 2) == 0)
                || (j + 5 <= end && strncmp(src + j, "range", 5) == 0)
                || (j + 4 <= end && strncmp(src + j, "with", 4) == 0))
                depth++;
        }
        i++;
    }
    return 0;
}

static inline int mako_tmpl_exec_action(MakoTmplCtx *c, const char *act, size_t alen) {
    mako_tmpl_trim(&act, &alen);
    if (alen == 0) return 1;
    /* comment */
    if (alen >= 4 && act[0] == '/' && act[1] == '*') return 1;

    /* pipeline-ish: word + args */
    char buf[256];
    if (alen >= sizeof(buf)) alen = sizeof(buf) - 1;
    memcpy(buf, act, alen);
    buf[alen] = 0;

    /* template "name" */
    if (strncmp(buf, "template", 8) == 0 && (buf[8] == ' ' || buf[8] == '\t' || buf[8] == '"')) {
        char *p = buf + 8;
        while (*p == ' ' || *p == '\t') p++;
        char q = (*p == '"' || *p == '\'') ? *p++ : 0;
        char name[MAKO_TMPL_KEY];
        size_t ni = 0;
        while (*p && *p != q && *p != ' ' && ni + 1 < sizeof(name)) name[ni++] = *p++;
        name[ni] = 0;
        if (!c->tmpl) return 1;
        for (int i = 0; i < c->tmpl->n_def; i++) {
            if (strcmp(c->tmpl->defs[i].name, name) == 0) {
                mako_tmpl_exec_body(c, c->tmpl->defs[i].body, c->tmpl->defs[i].body_len);
                return 1;
            }
        }
        return 1;
    }

    /* len / upper / lower / html / printf */
    if (strncmp(buf, "len ", 4) == 0 || strcmp(buf, "len") == 0) {
        const char *k = buf[3] == ' ' ? buf + 4 : ".";
        while (*k == ' ') k++;
        const char *v = mako_tmpl_lookup(c, k);
        char num[32];
        snprintf(num, sizeof(num), "%d", (int)strlen(v));
        mako_tmpl_out_appends(c, num);
        return 1;
    }
    if (strncmp(buf, "upper ", 6) == 0) {
        const char *v = mako_tmpl_lookup(c, buf + 6);
        for (const char *p = v; *p; p++) {
            char ch = *p;
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
            mako_tmpl_out_append(c, &ch, 1);
        }
        return 1;
    }
    if (strncmp(buf, "lower ", 6) == 0) {
        const char *v = mako_tmpl_lookup(c, buf + 6);
        for (const char *p = v; *p; p++) {
            char ch = *p;
            if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
            mako_tmpl_out_append(c, &ch, 1);
        }
        return 1;
    }
    if (strncmp(buf, "html ", 5) == 0) {
        const char *v = mako_tmpl_lookup(c, buf + 5);
        MakoString esc = mako_tmpl_html_escape_cstr(v);
        mako_tmpl_out_append(c, esc.data, esc.len);
        free(esc.data);
        return 1;
    }
    if (strncmp(buf, "printf ", 7) == 0) {
        /* printf "%s" .key  or printf .key */
        char *p = buf + 7;
        while (*p == ' ') p++;
        if (*p == '"') {
            p++;
            char fmt[64];
            size_t fi = 0;
            while (*p && *p != '"' && fi + 1 < sizeof(fmt)) fmt[fi++] = *p++;
            fmt[fi] = 0;
            if (*p == '"') p++;
            while (*p == ' ') p++;
            const char *v = mako_tmpl_lookup(c, p[0] ? p : ".");
            if (strcmp(fmt, "%s") == 0) mako_tmpl_emit_value(c, v);
            else {
                char tmp[512];
                snprintf(tmp, sizeof(tmp), fmt, v);
                mako_tmpl_emit_value(c, tmp);
            }
        } else {
            mako_tmpl_emit_value(c, mako_tmpl_lookup(c, p));
        }
        return 1;
    }

    /* plain field */
    mako_tmpl_emit_value(c, mako_tmpl_lookup(c, buf));
    return 1;
}

static int mako_tmpl_exec_body(MakoTmplCtx *c, const char *src, size_t len) {
    size_t i = 0;
    while (i < len) {
        if (i + 1 < len && src[i] == '{' && src[i + 1] == '{') {
            size_t j = i + 2;
            while (j + 1 < len && !(src[j] == '}' && src[j + 1] == '}')) j++;
            if (j + 1 >= len) {
                mako_tmpl_out_append(c, src + i, len - i);
                break;
            }
            const char *act = src + i + 2;
            size_t alen = j - (i + 2);
            mako_tmpl_trim(&act, &alen);

            /* skip comments */
            if (alen >= 2 && act[0] == '/' && act[1] == '*') {
                i = j + 2;
                continue;
            }

            /* control structures */
            if (alen >= 2 && strncmp(act, "if", 2) == 0
                && (alen == 2 || act[2] == ' ' || act[2] == '\t' || act[2] == '.')) {
                const char *key = act + 2;
                size_t kn = alen - 2;
                mako_tmpl_trim(&key, &kn);
                char kbuf[MAKO_TMPL_KEY];
                size_t knc = kn < sizeof(kbuf) - 1 ? kn : sizeof(kbuf) - 1;
                memcpy(kbuf, key, knc);
                kbuf[knc] = 0;
                size_t end_pos = 0, end_len = 0;
                if (!mako_tmpl_find_end(src, len, j + 2, &end_pos, &end_len)) {
                    i = j + 2;
                    continue;
                }
                size_t body_start = j + 2;
                size_t else_pos = 0, else_len = 0;
                int has_else =
                    mako_tmpl_find_else(src, body_start, end_pos, &else_pos, &else_len);
                int truth = mako_tmpl_truthy(mako_tmpl_lookup(c, kbuf));
                if (truth) {
                    size_t be = has_else ? else_pos : end_pos;
                    mako_tmpl_exec_body(c, src + body_start, be - body_start);
                } else if (has_else) {
                    size_t es = else_pos + else_len;
                    mako_tmpl_exec_body(c, src + es, end_pos - es);
                }
                i = end_pos + end_len;
                continue;
            }
            if (alen >= 5 && strncmp(act, "range", 5) == 0) {
                const char *key = act + 5;
                size_t kn = alen - 5;
                mako_tmpl_trim(&key, &kn);
                char kbuf[MAKO_TMPL_KEY];
                size_t knc = kn < sizeof(kbuf) - 1 ? kn : sizeof(kbuf) - 1;
                memcpy(kbuf, key, knc);
                kbuf[knc] = 0;
                if (kbuf[0] == '.') memmove(kbuf, kbuf + 1, strlen(kbuf));
                size_t end_pos = 0, end_len = 0;
                if (!mako_tmpl_find_end(src, len, j + 2, &end_pos, &end_len)) {
                    i = j + 2;
                    continue;
                }
                size_t body_start = j + 2;
                size_t body_len = end_pos - body_start;
                MakoTmplKV *e = c->data ? mako_tmpl_data_find(c->data, kbuf) : NULL;
                const char *saved = c->dot;
                if (e && e->is_list) {
                    for (int li = 0; li < e->n_items; li++) {
                        c->dot = e->items[li] ? e->items[li] : "";
                        mako_tmpl_exec_body(c, src + body_start, body_len);
                    }
                } else if (e && e->value && e->value[0]) {
                    /* range over single non-empty as one iteration */
                    c->dot = e->value;
                    mako_tmpl_exec_body(c, src + body_start, body_len);
                }
                c->dot = saved;
                i = end_pos + end_len;
                continue;
            }
            if (alen >= 4 && strncmp(act, "with", 4) == 0) {
                const char *key = act + 4;
                size_t kn = alen - 4;
                mako_tmpl_trim(&key, &kn);
                char kbuf[MAKO_TMPL_KEY];
                size_t knc = kn < sizeof(kbuf) - 1 ? kn : sizeof(kbuf) - 1;
                memcpy(kbuf, key, knc);
                kbuf[knc] = 0;
                size_t end_pos = 0, end_len = 0;
                if (!mako_tmpl_find_end(src, len, j + 2, &end_pos, &end_len)) {
                    i = j + 2;
                    continue;
                }
                const char *v = mako_tmpl_lookup(c, kbuf);
                if (mako_tmpl_truthy(v)) {
                    const char *saved = c->dot;
                    c->dot = v;
                    mako_tmpl_exec_body(c, src + j + 2, end_pos - (j + 2));
                    c->dot = saved;
                }
                i = end_pos + end_len;
                continue;
            }
            if (alen >= 3 && strncmp(act, "end", 3) == 0) {
                /* stray end — skip */
                i = j + 2;
                continue;
            }
            if (alen >= 4 && strncmp(act, "else", 4) == 0) {
                i = j + 2;
                continue;
            }
            if (alen >= 6 && strncmp(act, "define", 6) == 0) {
                /* defines already extracted */
                size_t end_pos = 0, end_len = 0;
                if (mako_tmpl_find_end(src, len, j + 2, &end_pos, &end_len))
                    i = end_pos + end_len;
                else
                    i = j + 2;
                continue;
            }

            mako_tmpl_exec_action(c, act, alen);
            i = j + 2;
            continue;
        }
        /* copy literal until next {{ */
        size_t start = i;
        while (i < len && !(i + 1 < len && src[i] == '{' && src[i + 1] == '{')) i++;
        mako_tmpl_out_append(c, src + start, i - start);
    }
    return 1;
}

static inline MakoString mako_tmpl_execute_mode(int64_t th, int64_t dh, int html) {
    MakoTmpl *t = mako_tmpl_ref(th);
    MakoTmplData *d = mako_tmpl_data_ref(dh);
    if (!t) return mako_str_from_cstr("");
    MakoTmplCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.data = d;
    ctx.dot = "";
    ctx.html_escape = html;
    ctx.tmpl = t;
    ctx.out_cap = t->src_len + 64;
    ctx.out = (char *)malloc(ctx.out_cap);
    if (!ctx.out) return mako_str_from_cstr("");
    ctx.out[0] = 0;
    ctx.out_len = 0;
    mako_tmpl_exec_body(&ctx, t->src, t->src_len);
    return (MakoString){ctx.out, ctx.out_len};
}

static inline MakoString mako_tmpl_execute(int64_t th, int64_t dh) {
    return mako_tmpl_execute_mode(th, dh, 0);
}

static inline MakoString mako_tmpl_html_execute(int64_t th, int64_t dh) {
    return mako_tmpl_execute_mode(th, dh, 1);
}

/* One-shot helpers: parse + data from pairs (compat / short scripts). */
static inline MakoString mako_tmpl_exec_map(MakoString source, int64_t data_h, int html) {
    int64_t t = mako_tmpl_new(source);
    if (t < 0) return mako_str_from_cstr("");
    MakoString out = mako_tmpl_execute_mode(t, data_h, html);
    mako_tmpl_free(t);
    return out;
}

static inline MakoString mako_tmpl_text(MakoString source, int64_t data_h) {
    return mako_tmpl_exec_map(source, data_h, 0);
}

static inline MakoString mako_tmpl_html(MakoString source, int64_t data_h) {
    return mako_tmpl_exec_map(source, data_h, 1);
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_TEMPLATE_H */
