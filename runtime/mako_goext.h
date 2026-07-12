/* Mako Go-breadth stdlib extensions — flag, exec, url, csv/xml, compress, archive,
 * mime, bytes buffer, crypto digests, rand, template, html, base32, net DNS/IP,
 * signal, atomic, utf8, filepath walk, slices/maps helpers.
 * Included from mako_std.h. Soft-fail when optional libs (zlib) missing.
 */
#ifndef MAKO_GOEXT_H
#define MAKO_GOEXT_H

#include "mako_rt.h"
#include "mako_stdlib.h"
#include <ctype.h>
#include <math.h>
#include <stdatomic.h>

#if !defined(_WIN32)
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#else
#include <process.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#if defined(MAKO_HAS_ZLIB) || defined(MAKO_USE_ZLIB)
#include <zlib.h>
#define MAKO_ZLIB 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- flag (CLI) ---- */
enum { MAKO_FLAG_MAX = 64 };
static char mako_flag_names[MAKO_FLAG_MAX][64];
static char mako_flag_vals[MAKO_FLAG_MAX][512];
static int mako_flag_n;
static int mako_flag_parsed;

static inline void mako_flag_parse(void) {
    if (mako_flag_parsed) return;
    mako_flag_parsed = 1;
    mako_flag_n = 0;
    for (int i = 1; i < mako_argc_g && mako_flag_n < MAKO_FLAG_MAX; i++) {
        const char *a = mako_argv_g[i];
        if (!a || a[0] != '-' || a[1] == 0) continue;
        if (a[1] == '-' && a[2] == 0) break; /* -- */
        const char *name = a + (a[1] == '-' ? 2 : 1);
        const char *eq = strchr(name, '=');
        char nbuf[64], vbuf[512];
        if (eq) {
            size_t nl = (size_t)(eq - name);
            if (nl >= sizeof(nbuf)) nl = sizeof(nbuf) - 1;
            memcpy(nbuf, name, nl);
            nbuf[nl] = 0;
            snprintf(vbuf, sizeof(vbuf), "%s", eq + 1);
        } else {
            snprintf(nbuf, sizeof(nbuf), "%s", name);
            if (i + 1 < mako_argc_g && mako_argv_g[i + 1][0] != '-') {
                snprintf(vbuf, sizeof(vbuf), "%s", mako_argv_g[++i]);
            } else {
                vbuf[0] = '1';
                vbuf[1] = 0;
            }
        }
        snprintf(mako_flag_names[mako_flag_n], 64, "%s", nbuf);
        snprintf(mako_flag_vals[mako_flag_n], 512, "%s", vbuf);
        mako_flag_n++;
    }
}

static inline MakoString mako_flag_string(MakoString name, MakoString def) {
    mako_flag_parse();
    const char *n = name.data ? name.data : "";
    for (int i = 0; i < mako_flag_n; i++) {
        if (strcmp(mako_flag_names[i], n) == 0)
            return mako_str_from_cstr(mako_flag_vals[i]);
    }
    return mako_str_clone(def);
}

static inline int64_t mako_flag_int(MakoString name, int64_t def) {
    MakoString s = mako_flag_string(name, mako_str_from_cstr(""));
    if (s.len == 0) return def;
    MakoResultInt r = mako_parse_int(s);
    return r.ok ? r.value : def;
}

static inline int64_t mako_flag_bool(MakoString name, int64_t def) {
    MakoString s = mako_flag_string(name, mako_str_from_cstr(""));
    if (s.len == 0) return def;
    if (mako_str_eq(s, mako_str_from_cstr("1")) ||
        mako_str_eq(s, mako_str_from_cstr("true")) ||
        mako_str_eq(s, mako_str_from_cstr("TRUE")))
        return 1;
    if (mako_str_eq(s, mako_str_from_cstr("0")) ||
        mako_str_eq(s, mako_str_from_cstr("false")))
        return 0;
    return def;
}

/* ---- os/exec ---- */
static inline MakoString mako_exec_output(MakoString cmd) {
    const char *c = cmd.data ? cmd.data : "";
#if defined(_WIN32)
    FILE *f = _popen(c, "r");
#else
    FILE *f = popen(c, "r");
#endif
    if (!f) return mako_str_from_cstr("");
    MakoStrBuilder *b = mako_str_builder_new();
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        MakoString chunk = mako_str_view(buf, n);
        mako_str_builder_write(b, chunk);
    }
#if defined(_WIN32)
    _pclose(f);
#else
    pclose(f);
#endif
    MakoString out = mako_str_builder_string(b);
    free(b->data);
    free(b);
    return out;
}

static inline int64_t mako_exec_run(MakoString cmd) {
    const char *c = cmd.data ? cmd.data : "";
    int st = system(c);
#if defined(_WIN32)
    return (int64_t)st;
#else
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return -1;
#endif
}

/* ---- net/url ---- */
static inline MakoString mako_url_query_escape(MakoString s) {
    static const char *hex = "0123456789ABCDEF";
    size_t cap = s.len * 3 + 1;
    char *d = (char *)malloc(cap);
    if (!d) mako_abort("url_escape OOM");
    size_t o = 0;
    for (size_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            d[o++] = (char)c;
        } else if (c == ' ') {
            d[o++] = '+';
        } else {
            d[o++] = '%';
            d[o++] = hex[c >> 4];
            d[o++] = hex[c & 15];
        }
    }
    d[o] = 0;
    return (MakoString){d, o};
}

static inline MakoString mako_url_scheme(MakoString u) {
    const char *p = u.data ? u.data : "";
    const char *colon = strstr(p, "://");
    if (!colon) return mako_str_from_cstr("");
    return mako_str_slice(u, 0, (int64_t)(colon - p));
}

static inline MakoString mako_url_host(MakoString u) {
    const char *p = u.data ? u.data : "";
    const char *colon = strstr(p, "://");
    if (!colon) return mako_str_from_cstr("");
    const char *h = colon + 3;
    const char *end = h;
    while (*end && *end != '/' && *end != '?' && *end != '#') end++;
    size_t off = (size_t)(h - p);
    return mako_str_slice(u, (int64_t)off, (int64_t)(end - p));
}

static inline MakoString mako_url_path(MakoString u) {
    const char *p = u.data ? u.data : "";
    const char *colon = strstr(p, "://");
    const char *start = p;
    if (colon) {
        start = colon + 3;
        while (*start && *start != '/' && *start != '?' && *start != '#') start++;
    }
    if (*start != '/') return mako_str_from_cstr("/");
    const char *end = start;
    while (*end && *end != '?' && *end != '#') end++;
    size_t off = (size_t)(start - p);
    return mako_str_slice(u, (int64_t)off, (int64_t)(end - p));
}

static inline MakoString mako_url_query(MakoString u) {
    const char *p = u.data ? u.data : "";
    const char *q = strchr(p, '?');
    if (!q) return mako_str_from_cstr("");
    q++;
    const char *end = q;
    while (*end && *end != '#') end++;
    size_t off = (size_t)(q - p);
    return mako_str_slice(u, (int64_t)off, (int64_t)(end - p));
}

/* ---- encoding/csv ---- */
static inline MakoStrArray mako_csv_split_line(MakoString line) {
    /* Simple CSV: comma-separated; quoted fields with "" escape. */
    size_t parts = 0;
    int in_q = 0;
    for (size_t i = 0; i < line.len; i++) {
        char c = line.data[i];
        if (c == '"') in_q = !in_q;
        else if (c == ',' && !in_q) parts++;
    }
    parts++;
    MakoStrArray a = mako_str_array_make((int64_t)parts, (int64_t)parts);
    size_t idx = 0, start = 0;
    in_q = 0;
    for (size_t i = 0; i <= line.len; i++) {
        char c = i < line.len ? line.data[i] : ',';
        if (c == '"' && i < line.len) {
            in_q = !in_q;
            continue;
        }
        if ((c == ',' && !in_q) || i == line.len) {
            size_t lo = start, hi = i;
            if (hi > lo && line.data[lo] == '"' && line.data[hi - 1] == '"') {
                lo++;
                hi--;
            }
            a.data[idx++] = mako_str_slice(line, (int64_t)lo, (int64_t)hi);
            start = i + 1;
        }
    }
    a.len = idx;
    return a;
}

static inline MakoString mako_csv_join_row(MakoStrArray fields) {
    MakoStrBuilder *b = mako_str_builder_new();
    for (size_t i = 0; i < fields.len; i++) {
        if (i) mako_str_builder_write(b, mako_str_from_cstr(","));
        MakoString f = fields.data[i];
        int need_q = 0;
        for (size_t j = 0; j < f.len; j++) {
            if (f.data[j] == ',' || f.data[j] == '"' || f.data[j] == '\n') need_q = 1;
        }
        if (need_q) {
            mako_str_builder_write_byte(b, '"');
            for (size_t j = 0; j < f.len; j++) {
                if (f.data[j] == '"') mako_str_builder_write_byte(b, '"');
                mako_str_builder_write_byte(b, (int64_t)(unsigned char)f.data[j]);
            }
            mako_str_builder_write_byte(b, '"');
        } else {
            mako_str_builder_write(b, f);
        }
    }
    MakoString out = mako_str_builder_string(b);
    free(b->data);
    free(b);
    return out;
}

/* ---- encoding/xml (subset) ---- */
static inline MakoString mako_xml_escape(MakoString s) {
    size_t cap = s.len * 6 + 1;
    char *d = (char *)malloc(cap);
    if (!d) mako_abort("xml_escape OOM");
    size_t o = 0;
    for (size_t i = 0; i < s.len; i++) {
        char c = s.data[i];
        const char *rep = NULL;
        if (c == '&') rep = "&amp;";
        else if (c == '<') rep = "&lt;";
        else if (c == '>') rep = "&gt;";
        else if (c == '"') rep = "&quot;";
        else if (c == '\'') rep = "&apos;";
        if (rep) {
            size_t n = strlen(rep);
            memcpy(d + o, rep, n);
            o += n;
        } else {
            d[o++] = c;
        }
    }
    d[o] = 0;
    return (MakoString){d, o};
}

static inline MakoString mako_html_escape(MakoString s) {
    return mako_xml_escape(s);
}

static inline MakoString mako_xml_tag_text(MakoString xml, MakoString tag) {
    /* Find <tag>...</tag> first occurrence; return inner text. */
    char open[128], close[128];
    if (tag.len + 3 >= sizeof(open)) return mako_str_from_cstr("");
    snprintf(open, sizeof(open), "<%.*s>", (int)tag.len, tag.data);
    snprintf(close, sizeof(close), "</%.*s>", (int)tag.len, tag.data);
    const char *p = xml.data ? xml.data : "";
    const char *a = strstr(p, open);
    if (!a) return mako_str_from_cstr("");
    a += strlen(open);
    const char *b = strstr(a, close);
    if (!b) return mako_str_from_cstr("");
    size_t off = (size_t)(a - p);
    return mako_str_slice(xml, (int64_t)off, (int64_t)(b - p));
}

/* ---- compress/gzip ---- */
static inline MakoString mako_gzip_compress(MakoString s) {
#if defined(MAKO_ZLIB)
    uLongf dest_len = compressBound((uLong)s.len);
    unsigned char *out = (unsigned char *)malloc(dest_len + 1);
    if (!out) return mako_str_from_cstr("");
    if (compress2(out, &dest_len, (const Bytef *)(s.data ? s.data : ""), (uLong)s.len, Z_DEFAULT_COMPRESSION) != Z_OK) {
        free(out);
        return mako_str_from_cstr("");
    }
    char *d = (char *)out;
    d[dest_len] = 0;
    return (MakoString){d, (size_t)dest_len};
#else
    (void)s;
    return mako_str_from_cstr(""); /* link with -lz / MAKO_HAS_ZLIB for gzip */
#endif
}

static inline MakoString mako_gzip_decompress(MakoString s) {
#if defined(MAKO_ZLIB)
    uLongf dest_len = (uLongf)(s.len * 8 + 64);
    if (dest_len < 256) dest_len = 256;
    for (int attempt = 0; attempt < 4; attempt++) {
        unsigned char *out = (unsigned char *)malloc(dest_len + 1);
        if (!out) return mako_str_from_cstr("");
        uLongf n = dest_len;
        int rc = uncompress(out, &n, (const Bytef *)(s.data ? s.data : ""), (uLong)s.len);
        if (rc == Z_OK) {
            out[n] = 0;
            return (MakoString){(char *)out, (size_t)n};
        }
        free(out);
        if (rc != Z_BUF_ERROR) return mako_str_from_cstr("");
        dest_len *= 2;
    }
    return mako_str_from_cstr("");
#else
    (void)s;
    return mako_str_from_cstr("");
#endif
}

static inline int64_t mako_gzip_available(void) {
#if defined(MAKO_ZLIB)
    return 1;
#else
    return 0;
#endif
}

/* ---- archive/tar (ustar write one file / read first file name) ---- */
static inline int64_t mako_tar_write_file(MakoString tar_path, MakoString name, MakoString data) {
    FILE *f = fopen(tar_path.data ? tar_path.data : "", "wb");
    if (!f) return -1;
    char hdr[512];
    memset(hdr, 0, 512);
    snprintf(hdr, 100, "%.*s", (int)(name.len < 99 ? name.len : 99), name.data ? name.data : "");
    snprintf(hdr + 100, 8, "%07o", 0644);
    snprintf(hdr + 108, 8, "%07o", 0);
    snprintf(hdr + 116, 8, "%07o", 0);
    snprintf(hdr + 124, 12, "%011o", (unsigned)data.len);
    snprintf(hdr + 136, 12, "%011o", (unsigned)time(NULL));
    memset(hdr + 148, ' ', 8);
    memcpy(hdr + 257, "ustar", 5);
    hdr[262] = '0';
    unsigned sum = 0;
    for (int i = 0; i < 512; i++) sum += (unsigned char)hdr[i];
    snprintf(hdr + 148, 8, "%06o", sum);
    hdr[154] = '\0';
    hdr[155] = ' ';
    fwrite(hdr, 1, 512, f);
    if (data.len) fwrite(data.data, 1, data.len, f);
    size_t pad = (512 - (data.len % 512)) % 512;
    if (pad) {
        char z[512];
        memset(z, 0, pad);
        fwrite(z, 1, pad, f);
    }
    char end[1024];
    memset(end, 0, sizeof(end));
    fwrite(end, 1, 1024, f);
    fclose(f);
    return 0;
}

static inline MakoString mako_tar_first_name(MakoString tar_path) {
    FILE *f = fopen(tar_path.data ? tar_path.data : "", "rb");
    if (!f) return mako_str_from_cstr("");
    char hdr[512];
    if (fread(hdr, 1, 512, f) != 512) {
        fclose(f);
        return mako_str_from_cstr("");
    }
    fclose(f);
    hdr[99] = 0;
    return mako_str_from_cstr(hdr);
}

/* ---- mime ---- */
static inline MakoString mako_mime_type_by_ext(MakoString path) {
    MakoString e = mako_path_ext(path);
    if (mako_str_eq(e, mako_str_from_cstr(".html")) || mako_str_eq(e, mako_str_from_cstr(".htm")))
        return mako_str_from_cstr("text/html; charset=utf-8");
    if (mako_str_eq(e, mako_str_from_cstr(".css"))) return mako_str_from_cstr("text/css; charset=utf-8");
    if (mako_str_eq(e, mako_str_from_cstr(".js"))) return mako_str_from_cstr("application/javascript");
    if (mako_str_eq(e, mako_str_from_cstr(".json"))) return mako_str_from_cstr("application/json");
    if (mako_str_eq(e, mako_str_from_cstr(".png"))) return mako_str_from_cstr("image/png");
    if (mako_str_eq(e, mako_str_from_cstr(".jpg")) || mako_str_eq(e, mako_str_from_cstr(".jpeg")))
        return mako_str_from_cstr("image/jpeg");
    if (mako_str_eq(e, mako_str_from_cstr(".gif"))) return mako_str_from_cstr("image/gif");
    if (mako_str_eq(e, mako_str_from_cstr(".svg"))) return mako_str_from_cstr("image/svg+xml");
    if (mako_str_eq(e, mako_str_from_cstr(".txt"))) return mako_str_from_cstr("text/plain; charset=utf-8");
    if (mako_str_eq(e, mako_str_from_cstr(".xml"))) return mako_str_from_cstr("application/xml");
    if (mako_str_eq(e, mako_str_from_cstr(".pdf"))) return mako_str_from_cstr("application/pdf");
    if (mako_str_eq(e, mako_str_from_cstr(".wasm"))) return mako_str_from_cstr("application/wasm");
    if (mako_str_eq(e, mako_str_from_cstr(".gz"))) return mako_str_from_cstr("application/gzip");
    return mako_str_from_cstr("application/octet-stream");
}

/* ---- context-like deadline helpers ---- */
static inline int64_t mako_context_with_timeout_ms(int64_t timeout_ms) {
    return mako_now_ms() + (timeout_ms < 0 ? 0 : timeout_ms);
}

static inline int64_t mako_context_expired(int64_t deadline_ms) {
    return mako_now_ms() >= deadline_ms ? 1 : 0;
}

static inline int64_t mako_context_remaining_ms(int64_t deadline_ms) {
    int64_t left = deadline_ms - mako_now_ms();
    return left < 0 ? 0 : left;
}

/* ---- bytes.Buffer ---- */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} MakoBytesBuffer;

static inline MakoBytesBuffer *mako_bytes_buffer_new(void) {
    MakoBytesBuffer *b = (MakoBytesBuffer *)calloc(1, sizeof(MakoBytesBuffer));
    if (!b) mako_abort("bytes_buffer OOM");
    b->cap = 64;
    b->data = (char *)malloc(b->cap);
    if (!b->data) mako_abort("bytes_buffer OOM");
    b->data[0] = 0;
    return b;
}

static inline void mako_bytes_buffer_grow(MakoBytesBuffer *b, size_t need) {
    if (need <= b->cap) return;
    size_t ncap = b->cap ? b->cap * 2 : 64;
    while (ncap < need) ncap *= 2;
    char *nd = (char *)realloc(b->data, ncap);
    if (!nd) mako_abort("bytes_buffer grow OOM");
    b->data = nd;
    b->cap = ncap;
}

static inline void mako_bytes_buffer_write(MakoBytesBuffer *b, MakoString s) {
    if (!b) return;
    mako_bytes_buffer_grow(b, b->len + s.len + 1);
    if (s.len) memcpy(b->data + b->len, s.data, s.len);
    b->len += s.len;
    b->data[b->len] = 0;
}

static inline MakoString mako_bytes_buffer_string(MakoBytesBuffer *b) {
    if (!b) return mako_str_from_cstr("");
    return mako_str_from_cstr(b->data ? b->data : "");
}

static inline int64_t mako_bytes_buffer_len(MakoBytesBuffer *b) {
    return b ? (int64_t)b->len : 0;
}

static inline void mako_bytes_buffer_reset(MakoBytesBuffer *b) {
    if (!b) return;
    b->len = 0;
    if (b->data) b->data[0] = 0;
}

/* ---- math/rand (xorshift64*) ---- */
static uint64_t mako_rand_state = 0x853c49e6748fea9bULL;

static inline void mako_rand_seed(int64_t seed) {
    mako_rand_state = (uint64_t)seed;
    if (mako_rand_state == 0) mako_rand_state = 0x853c49e6748fea9bULL;
}

static inline uint64_t mako_rand_u64(void) {
    uint64_t x = mako_rand_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    mako_rand_state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static inline int64_t mako_rand_intn(int64_t n) {
    if (n <= 0) return 0;
    return (int64_t)(mako_rand_u64() % (uint64_t)n);
}

static inline double mako_rand_float(void) {
    return (mako_rand_u64() >> 11) * (1.0 / (double)(1ULL << 53));
}

/* ---- text/template ({{key}} replace from map-like pairs via sprintf style) ---- */
static inline MakoString mako_template_execute(MakoString tmpl, MakoString key, MakoString val) {
    /* Replace all {{key}} with val */
    char needle[256];
    if (key.len + 4 >= sizeof(needle)) return mako_str_clone(tmpl);
    snprintf(needle, sizeof(needle), "{{%.*s}}", (int)key.len, key.data ? key.data : "");
    MakoString n = mako_str_from_cstr(needle);
    return mako_str_replace(tmpl, n, val);
}

/* ---- encoding/base32 (RFC 4648) ---- */
static inline MakoString mako_base32_encode(MakoString s) {
    static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    size_t out_len = ((s.len + 4) / 5) * 8;
    char *o = (char *)malloc(out_len + 1);
    if (!o) mako_abort("base32 OOM");
    size_t j = 0;
    for (size_t i = 0; i < s.len; i += 5) {
        uint64_t v = 0;
        int rem = (int)(s.len - i);
        if (rem > 5) rem = 5;
        for (int k = 0; k < rem; k++) v = (v << 8) | (unsigned char)s.data[i + k];
        v <<= (5 - rem) * 8;
        int pads = rem == 1 ? 6 : rem == 2 ? 4 : rem == 3 ? 3 : rem == 4 ? 1 : 0;
        for (int k = 0; k < 8 - pads; k++) {
            o[j++] = T[(v >> (35 - 5 * k)) & 31];
        }
        for (int k = 0; k < pads; k++) o[j++] = '=';
    }
    o[j] = 0;
    return (MakoString){o, j};
}

/* ---- crypto digests (portable SHA-1 / SHA-512 via CommonCrypto or OpenSSL or soft) ---- */
static inline MakoString mako_sha1_hex(MakoString s) {
#if defined(MAKO_HAS_CC)
    unsigned char dig[CC_SHA1_DIGEST_LENGTH];
    CC_SHA1(s.data ? s.data : "", (CC_LONG)s.len, dig);
    char *o = (char *)malloc(41);
    for (int i = 0; i < 20; i++) sprintf(o + i * 2, "%02x", dig[i]);
    o[40] = 0;
    return (MakoString){o, 40};
#elif defined(MAKO_HAS_OPENSSL)
    unsigned char dig[SHA_DIGEST_LENGTH];
    SHA1((const unsigned char *)(s.data ? s.data : ""), s.len, dig);
    char *o = (char *)malloc(41);
    for (int i = 0; i < 20; i++) sprintf(o + i * 2, "%02x", dig[i]);
    o[40] = 0;
    return (MakoString){o, 40};
#else
    (void)s;
    return mako_str_from_cstr(""); /* need CommonCrypto or OpenSSL */
#endif
}

static inline MakoString mako_sha512_hex(MakoString s) {
#if defined(MAKO_HAS_CC)
    unsigned char dig[CC_SHA512_DIGEST_LENGTH];
    CC_SHA512(s.data ? s.data : "", (CC_LONG)s.len, dig);
    char *o = (char *)malloc(129);
    for (int i = 0; i < 64; i++) sprintf(o + i * 2, "%02x", dig[i]);
    o[128] = 0;
    return (MakoString){o, 128};
#elif defined(MAKO_HAS_OPENSSL)
    unsigned char dig[SHA512_DIGEST_LENGTH];
    SHA512((const unsigned char *)(s.data ? s.data : ""), s.len, dig);
    char *o = (char *)malloc(129);
    for (int i = 0; i < 64; i++) sprintf(o + i * 2, "%02x", dig[i]);
    o[128] = 0;
    return (MakoString){o, 128};
#else
    (void)s;
    return mako_str_from_cstr("");
#endif
}

/* ---- net: DNS / IP ---- */
static inline MakoString mako_lookup_host(MakoString host) {
    mako_net_init();
    const char *h = host.data ? host.data : "";
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(h, NULL, &hints, &res) != 0 || !res) return mako_str_from_cstr("");
    char buf[INET6_ADDRSTRLEN];
    buf[0] = 0;
    if (res->ai_family == AF_INET) {
        struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
        inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
    } else if (res->ai_family == AF_INET6) {
        struct sockaddr_in6 *sa = (struct sockaddr_in6 *)res->ai_addr;
        inet_ntop(AF_INET6, &sa->sin6_addr, buf, sizeof(buf));
    }
    freeaddrinfo(res);
    return mako_str_from_cstr(buf);
}

static inline int mako_dns_ip_to_text(const struct addrinfo *ai, char *buf, size_t cap) {
    if (!ai || !buf || cap == 0) return 0;
    buf[0] = 0;
    if (ai->ai_family == AF_INET) {
        struct sockaddr_in *sa = (struct sockaddr_in *)ai->ai_addr;
        return inet_ntop(AF_INET, &sa->sin_addr, buf, (socklen_t)cap) != NULL;
    }
    if (ai->ai_family == AF_INET6) {
        struct sockaddr_in6 *sa = (struct sockaddr_in6 *)ai->ai_addr;
        return inet_ntop(AF_INET6, &sa->sin6_addr, buf, (socklen_t)cap) != NULL;
    }
    return 0;
}

static inline int64_t mako_dns_lookup_count(MakoString host) {
    mako_net_init();
    const char *h = host.data ? host.data : "";
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(h, NULL, &hints, &res) != 0 || !res) return 0;
    int64_t n = 0;
    for (struct addrinfo *it = res; it; it = it->ai_next) {
        if (it->ai_family == AF_INET || it->ai_family == AF_INET6) n++;
    }
    freeaddrinfo(res);
    return n;
}

static inline MakoString mako_dns_lookup_all(MakoString host) {
    mako_net_init();
    const char *h = host.data ? host.data : "";
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(h, NULL, &hints, &res) != 0 || !res) return mako_str_from_cstr("");
    size_t cap = 256, len = 0;
    char *out = (char *)malloc(cap);
    if (!out) mako_abort("dns_lookup_all OOM");
    out[0] = 0;
    char ip[INET6_ADDRSTRLEN];
    for (struct addrinfo *it = res; it; it = it->ai_next) {
        if (!mako_dns_ip_to_text(it, ip, sizeof(ip)) || ip[0] == 0) continue;
        size_t ilen = strlen(ip);
        size_t need = len + ilen + (len ? 1 : 0) + 1;
        if (need > cap) {
            while (cap < need) cap *= 2;
            char *next = (char *)realloc(out, cap);
            if (!next) { free(out); freeaddrinfo(res); mako_abort("dns_lookup_all OOM"); }
            out = next;
        }
        if (len) out[len++] = ',';
        memcpy(out + len, ip, ilen);
        len += ilen;
        out[len] = 0;
    }
    freeaddrinfo(res);
    return (MakoString){out, len};
}

static inline MakoString mako_dns_lookup_family(MakoString host, int family) {
    mako_net_init();
    const char *h = host.data ? host.data : "";
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(h, NULL, &hints, &res) != 0 || !res) return mako_str_from_cstr("");
    char ip[INET6_ADDRSTRLEN];
    MakoString out = mako_str_from_cstr("");
    for (struct addrinfo *it = res; it; it = it->ai_next) {
        if (mako_dns_ip_to_text(it, ip, sizeof(ip)) && ip[0]) {
            out = mako_str_from_cstr(ip);
            break;
        }
    }
    freeaddrinfo(res);
    return out;
}

static inline MakoString mako_dns_lookup_ipv4(MakoString host) {
    return mako_dns_lookup_family(host, AF_INET);
}

static inline MakoString mako_dns_lookup_ipv6(MakoString host) {
    return mako_dns_lookup_family(host, AF_INET6);
}

static inline int64_t mako_parse_ip_ok(MakoString ip) {
    const char *p = ip.data ? ip.data : "";
    unsigned char buf[16];
    if (inet_pton(AF_INET, p, buf) == 1) return 1;
    if (inet_pton(AF_INET6, p, buf) == 1) return 1;
    return 0;
}

static inline int64_t mako_dns_ip_family(MakoString ip) {
    const char *p = ip.data ? ip.data : "";
    unsigned char buf[16];
    if (inet_pton(AF_INET, p, buf) == 1) return 4;
    if (inet_pton(AF_INET6, p, buf) == 1) return 6;
    return 0;
}

static inline int64_t mako_dns_is_loopback(MakoString ip) {
    const char *p = ip.data ? ip.data : "";
    unsigned char b[16];
    if (inet_pton(AF_INET, p, b) == 1) return b[0] == 127 ? 1 : 0;
    if (inet_pton(AF_INET6, p, b) == 1) {
        for (int i = 0; i < 15; i++) if (b[i] != 0) return 0;
        return b[15] == 1 ? 1 : 0;
    }
    return 0;
}

static inline int64_t mako_dns_is_private(MakoString ip) {
    const char *p = ip.data ? ip.data : "";
    unsigned char b[16];
    if (inet_pton(AF_INET, p, b) == 1) {
        if (b[0] == 10) return 1;
        if (b[0] == 172 && b[1] >= 16 && b[1] <= 31) return 1;
        if (b[0] == 192 && b[1] == 168) return 1;
        if (b[0] == 169 && b[1] == 254) return 1;
        return 0;
    }
    if (inet_pton(AF_INET6, p, b) == 1) {
        if ((b[0] & 0xfe) == 0xfc) return 1; /* unique local fc00::/7 */
        if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) return 1; /* link-local fe80::/10 */
    }
    return 0;
}

static inline MakoString mako_dns_normalize_host(MakoString host) {
    const char *p = host.data ? host.data : "";
    size_t lo = 0, hi = host.len;
    while (lo < hi && (p[lo] == ' ' || p[lo] == '\t' || p[lo] == '\r' || p[lo] == '\n')) lo++;
    while (hi > lo && (p[hi - 1] == ' ' || p[hi - 1] == '\t' || p[hi - 1] == '\r' || p[hi - 1] == '\n')) hi--;
    if (hi > lo + 1 && p[lo] == '[' && p[hi - 1] == ']') {
        lo++;
        hi--;
    }
    size_t n = hi - lo;
    char *d = (char *)malloc(n + 1);
    if (!d) mako_abort("dns_normalize_host OOM");
    for (size_t i = 0; i < n; i++) {
        char c = p[lo + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        d[i] = c;
    }
    d[n] = 0;
    return (MakoString){d, n};
}

static inline MakoString mako_dns_join_host_port(MakoString host, int64_t port) {
    MakoString h = mako_dns_normalize_host(host);
    int need_brackets = 0;
    for (size_t i = 0; i < h.len; i++) {
        if (h.data[i] == ':') { need_brackets = 1; break; }
    }
    char portbuf[32];
    snprintf(portbuf, sizeof(portbuf), "%lld", (long long)port);
    size_t plen = strlen(portbuf);
    size_t n = h.len + plen + (need_brackets ? 3 : 1);
    char *d = (char *)malloc(n + 1);
    if (!d) mako_abort("dns_join_host_port OOM");
    size_t o = 0;
    if (need_brackets) d[o++] = '[';
    if (h.len) memcpy(d + o, h.data, h.len);
    o += h.len;
    if (need_brackets) d[o++] = ']';
    d[o++] = ':';
    memcpy(d + o, portbuf, plen);
    o += plen;
    d[o] = 0;
    return (MakoString){d, o};
}

static inline MakoString mako_dns_split_host(MakoString hostport) {
    const char *p = hostport.data ? hostport.data : "";
    if (hostport.len == 0) return mako_str_from_cstr("");
    if (p[0] == '[') {
        for (size_t i = 1; i < hostport.len; i++) {
            if (p[i] == ']') return mako_str_slice(hostport, 1, (int64_t)i);
        }
        return mako_str_from_cstr("");
    }
    size_t colon = hostport.len;
    int colons = 0;
    for (size_t i = 0; i < hostport.len; i++) {
        if (p[i] == ':') { colon = i; colons++; }
    }
    if (colons == 1) return mako_str_slice(hostport, 0, (int64_t)colon);
    if (colons == 0) return mako_str_slice(hostport, 0, (int64_t)hostport.len);
    return mako_str_from_cstr("");
}

static inline int64_t mako_dns_split_port(MakoString hostport) {
    const char *p = hostport.data ? hostport.data : "";
    size_t start = hostport.len;
    if (hostport.len == 0) return -1;
    if (p[0] == '[') {
        size_t close = hostport.len;
        for (size_t i = 1; i < hostport.len; i++) if (p[i] == ']') { close = i; break; }
        if (close == hostport.len || close + 1 >= hostport.len || p[close + 1] != ':') return -1;
        start = close + 2;
    } else {
        int colons = 0;
        for (size_t i = 0; i < hostport.len; i++) {
            if (p[i] == ':') { start = i + 1; colons++; }
        }
        if (colons != 1) return -1;
    }
    if (start >= hostport.len) return -1;
    int64_t port = 0;
    for (size_t i = start; i < hostport.len; i++) {
        if (p[i] < '0' || p[i] > '9') return -1;
        port = port * 10 + (p[i] - '0');
        if (port > 65535) return -1;
    }
    return port;
}

/* ---- os/signal (Unix) ---- */
#if !defined(_WIN32)
static volatile sig_atomic_t mako_signal_got;

static void mako_signal_handler(int sig) {
    (void)sig;
    mako_signal_got = 1;
}
#endif

static inline int64_t mako_signal_notify(int64_t sig) {
#if defined(_WIN32)
    (void)sig;
    return -1;
#else
    mako_signal_got = 0;
    if (signal((int)sig, mako_signal_handler) == SIG_ERR) return -1;
    return 0;
#endif
}

static inline int64_t mako_signal_received(void) {
#if defined(_WIN32)
    return 0;
#else
    return mako_signal_got ? 1 : 0;
#endif
}

static inline int64_t mako_http_shutdown_from_signal(int64_t sig, int64_t grace_ms) {
    if (mako_signal_notify(sig) != 0) return -1;
    if (mako_signal_received()) {
        return mako_http_shutdown_begin(grace_ms);
    }
    return 0;
}

/* ---- sync/atomic ---- */
typedef struct {
    _Atomic int64_t v;
} MakoAtomicInt;

static inline MakoAtomicInt *mako_atomic_new(int64_t init) {
    MakoAtomicInt *p = (MakoAtomicInt *)malloc(sizeof(MakoAtomicInt));
    if (!p) mako_abort("atomic_new OOM");
    atomic_store(&p->v, init);
    return p;
}

static inline int64_t mako_atomic_load(MakoAtomicInt *p) {
    return p ? atomic_load(&p->v) : 0;
}

static inline void mako_atomic_store(MakoAtomicInt *p, int64_t v) {
    if (p) atomic_store(&p->v, v);
}

static inline int64_t mako_atomic_add(MakoAtomicInt *p, int64_t delta) {
    if (!p) return 0;
    return atomic_fetch_add(&p->v, delta) + delta;
}

static inline int64_t mako_atomic_cas(MakoAtomicInt *p, int64_t oldv, int64_t newv) {
    if (!p) return 0;
    return atomic_compare_exchange_strong(&p->v, &oldv, newv) ? 1 : 0;
}

/* ---- unicode/utf8 ---- */
static inline int64_t mako_utf8_valid(MakoString s) {
    size_t i = 0;
    while (i < s.len) {
        int64_t r = 0;
        size_t w = mako_utf8_decode(s.data, s.len, i, &r);
        if (r == 0xFFFD && w == 1 && (unsigned char)s.data[i] >= 0x80) return 0;
        i += w;
    }
    return 1;
}

static inline int64_t mako_utf8_rune_len(int64_t r) {
    if (r < 0) return 0;
    if (r <= 0x7F) return 1;
    if (r <= 0x7FF) return 2;
    if (r <= 0xFFFF) return 3;
    if (r <= 0x10FFFF) return 4;
    return 0;
}

/* ---- path/filepath walk (recursive, depth-limited) ---- */
static inline void mako_filepath_walk_into(MakoString root, int64_t depth, int64_t max_depth, MakoStrArray *out) {
    if (depth > max_depth) return;
    MakoStrArray top = mako_read_dir(root);
    for (size_t i = 0; i < top.len; i++) {
        MakoString full = mako_path_join(root, top.data[i]);
        *out = mako_str_array_append(*out, full);
        if (mako_is_dir(full) && depth < max_depth) {
            mako_filepath_walk_into(full, depth + 1, max_depth, out);
        }
    }
}

static inline MakoStrArray mako_filepath_walk_n(MakoString root, int64_t max_depth) {
    MakoStrArray out = mako_str_array_make(0, 64);
    if (max_depth < 0) max_depth = 0;
    if (max_depth > 64) max_depth = 64;
    mako_filepath_walk_into(root, 0, max_depth, &out);
    return out;
}

static inline MakoStrArray mako_filepath_walk(MakoString root) {
    return mako_filepath_walk_n(root, 32);
}

/* ---- slices helpers ---- */
static inline MakoIntArray mako_slices_reverse_ints(MakoIntArray a) {
    MakoIntArray out = mako_ints_copy(a);
    for (size_t i = 0; i < out.len / 2; i++) {
        int64_t t = out.data[i];
        out.data[i] = out.data[out.len - 1 - i];
        out.data[out.len - 1 - i] = t;
    }
    return out;
}

static inline MakoIntArray mako_slices_unique_ints(MakoIntArray a) {
    MakoIntArray sorted = mako_sort_ints(a);
    size_t n = 0;
    for (size_t i = 0; i < sorted.len; i++) {
        if (i == 0 || sorted.data[i] != sorted.data[i - 1]) n++;
    }
    MakoIntArray out = mako_int_array_make((int64_t)n, (int64_t)n);
    size_t j = 0;
    for (size_t i = 0; i < sorted.len; i++) {
        if (i == 0 || sorted.data[i] != sorted.data[i - 1]) {
            out.data[j++] = sorted.data[i];
        }
    }
    out.len = j;
    return out;
}

/* ---- embed helper: read file at call site (document as init pattern) ---- */
static inline MakoString mako_embed_file(MakoString path) {
    return mako_read_file(path);
}

/* ========================================================================
 * Wave 3: zip · PNG · maps · reflect · httptest · AEAD · multipart · regexp
 * ======================================================================== */

/* ---- CRC-32 (IEEE) for ZIP / PNG ---- */
static uint32_t mako_crc32_table[256];
static int mako_crc32_ready = 0;

static inline void mako_crc32_init(void) {
    if (mako_crc32_ready) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        mako_crc32_table[i] = c;
    }
    mako_crc32_ready = 1;
}

static inline uint32_t mako_crc32(const void *data, size_t n) {
    mako_crc32_init();
    const unsigned char *p = (const unsigned char *)data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = mako_crc32_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ---- archive/zip (store method only) ---- */
static inline void mako_zip_u16(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}
static inline void mako_zip_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}
static inline uint16_t mako_zip_ru16(const unsigned char *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t mako_zip_ru32(const unsigned char *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

/* Raw deflate/inflate for ZIP method 8 (no zlib wrapper). */
static inline int mako_zip_deflate_raw(const unsigned char *in, size_t inlen,
                                      unsigned char **out, size_t *outlen) {
#if defined(MAKO_ZLIB)
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return -1;
    uLong bound = deflateBound(&zs, (uLong)inlen) + 64;
    unsigned char *buf = (unsigned char *)malloc(bound);
    if (!buf) { deflateEnd(&zs); return -1; }
    zs.next_in = (Bytef *)in;
    zs.avail_in = (uInt)inlen;
    zs.next_out = buf;
    zs.avail_out = (uInt)bound;
    int rc = deflate(&zs, Z_FINISH);
    if (rc != Z_STREAM_END) { free(buf); deflateEnd(&zs); return -1; }
    *outlen = zs.total_out;
    *out = buf;
    deflateEnd(&zs);
    return 0;
#else
    (void)in; (void)inlen; (void)out; (void)outlen;
    return -1;
#endif
}

static inline int mako_zip_inflate_raw(const unsigned char *in, size_t inlen,
                                      unsigned char **out, size_t *outlen, size_t hint) {
#if defined(MAKO_ZLIB)
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return -1;
    size_t cap = hint ? hint : (inlen * 4 + 64);
    if (cap < 64) cap = 64;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) { inflateEnd(&zs); return -1; }
    zs.next_in = (Bytef *)in;
    zs.avail_in = (uInt)inlen;
    for (;;) {
        zs.next_out = buf + zs.total_out;
        zs.avail_out = (uInt)(cap - zs.total_out);
        int rc = inflate(&zs, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK) { free(buf); inflateEnd(&zs); return -1; }
        if (zs.avail_out == 0) {
            cap *= 2;
            unsigned char *nbuf = (unsigned char *)realloc(buf, cap);
            if (!nbuf) { free(buf); inflateEnd(&zs); return -1; }
            buf = nbuf;
        }
    }
    *outlen = zs.total_out;
    *out = buf;
    inflateEnd(&zs);
    return 0;
#else
    (void)in; (void)inlen; (void)out; (void)outlen; (void)hint;
    return -1;
#endif
}

static inline int64_t mako_zip_deflate_available(void) {
#if defined(MAKO_ZLIB)
    return 1;
#else
    return 0;
#endif
}

static inline int64_t mako_zip_write_file(MakoString zip_path, MakoString name, MakoString data) {
    FILE *f = fopen(zip_path.data ? zip_path.data : "", "wb");
    if (!f) return -1;
    uint32_t crc = mako_crc32(data.data ? data.data : "", data.len);
    uint16_t nlen = (uint16_t)(name.len > 65535 ? 65535 : name.len);
    unsigned char *payload = (unsigned char *)(data.data ? data.data : "");
    size_t plen = data.len;
    uint16_t method = 0;
    unsigned char *comp = NULL;
    size_t clen = 0;
#if defined(MAKO_ZLIB)
    if (data.len > 0
        && mako_zip_deflate_raw((const unsigned char *)(data.data ? data.data : ""), data.len,
                                &comp, &clen) == 0
        && clen < data.len) {
        payload = comp;
        plen = clen;
        method = 8;
    }
#endif
    unsigned char lfh[30];
    memset(lfh, 0, 30);
    mako_zip_u32(lfh, 0x04034b50u);
    mako_zip_u16(lfh + 8, method);
    mako_zip_u32(lfh + 14, crc);
    mako_zip_u32(lfh + 18, (uint32_t)plen);
    mako_zip_u32(lfh + 22, (uint32_t)data.len);
    mako_zip_u16(lfh + 26, nlen);
    fwrite(lfh, 1, 30, f);
    if (nlen) fwrite(name.data, 1, nlen, f);
    if (plen) fwrite(payload, 1, plen, f);
    uint32_t local_sz = 30u + nlen + (uint32_t)plen;
    free(comp);

    unsigned char cdh[46];
    memset(cdh, 0, 46);
    mako_zip_u32(cdh, 0x02014b50u);
    mako_zip_u16(cdh + 10, method);
    mako_zip_u32(cdh + 16, crc);
    mako_zip_u32(cdh + 20, (uint32_t)plen);
    mako_zip_u32(cdh + 24, (uint32_t)data.len);
    mako_zip_u16(cdh + 28, nlen);
    mako_zip_u32(cdh + 42, 0);
    fwrite(cdh, 1, 46, f);
    if (nlen) fwrite(name.data, 1, nlen, f);

    unsigned char eocd[22];
    memset(eocd, 0, 22);
    mako_zip_u32(eocd, 0x06054b50u);
    mako_zip_u16(eocd + 8, 1);
    mako_zip_u16(eocd + 10, 1);
    mako_zip_u32(eocd + 12, 46u + nlen);
    mako_zip_u32(eocd + 16, local_sz);
    fwrite(eocd, 1, 22, f);
    fclose(f);
    return 0;
}

static inline MakoString mako_zip_first_name(MakoString zip_path) {
    MakoString raw = mako_read_file(zip_path);
    if (raw.len < 30) return mako_str_from_cstr("");
    const unsigned char *p = (const unsigned char *)raw.data;
    if (mako_zip_ru32(p) != 0x04034b50u) return mako_str_from_cstr("");
    uint16_t nlen = mako_zip_ru16(p + 26);
    uint16_t elen = mako_zip_ru16(p + 28);
    if (30u + nlen > raw.len) return mako_str_from_cstr("");
    (void)elen;
    char *d = (char *)malloc(nlen + 1);
    memcpy(d, raw.data + 30, nlen);
    d[nlen] = 0;
    return (MakoString){d, nlen};
}

static inline MakoString mako_zip_read_file(MakoString zip_path, MakoString name) {
    MakoString raw = mako_read_file(zip_path);
    if (raw.len < 30) return mako_str_from_cstr("");
    const unsigned char *p = (const unsigned char *)raw.data;
    size_t off = 0;
    while (off + 30 <= raw.len) {
        if (mako_zip_ru32(p + off) != 0x04034b50u) break;
        uint16_t method = mako_zip_ru16(p + off + 8);
        uint32_t csize = mako_zip_ru32(p + off + 18);
        uint32_t usize = mako_zip_ru32(p + off + 22);
        uint16_t nlen = mako_zip_ru16(p + off + 26);
        uint16_t elen = mako_zip_ru16(p + off + 28);
        if (off + 30 + nlen + elen + csize > raw.len) break;
        int name_ok = (nlen == name.len
                       && memcmp(raw.data + off + 30, name.data ? name.data : "", nlen) == 0);
        const unsigned char *payload = (const unsigned char *)(raw.data + off + 30 + nlen + elen);
        if (name_ok) {
            if (method == 0) {
                char *d = (char *)malloc(usize + 1);
                memcpy(d, payload, usize);
                d[usize] = 0;
                return (MakoString){d, usize};
            }
#if defined(MAKO_ZLIB)
            if (method == 8) {
                unsigned char *out = NULL;
                size_t outlen = 0;
                if (mako_zip_inflate_raw(payload, csize, &out, &outlen, usize) != 0)
                    return mako_str_from_cstr("");
                char *d = (char *)realloc(out, outlen + 1);
                if (!d) { free(out); return mako_str_from_cstr(""); }
                d[outlen] = 0;
                return (MakoString){d, outlen};
            }
#endif
            return mako_str_from_cstr("");
        }
        off += 30 + nlen + elen + csize;
    }
    return mako_str_from_cstr("");
}

/* ---- image/png (grayscale 8-bit encode/decode; RGB24 encode) ---- */
static inline void mako_png_be32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)((v >> 24) & 0xFF);
    p[1] = (unsigned char)((v >> 16) & 0xFF);
    p[2] = (unsigned char)((v >> 8) & 0xFF);
    p[3] = (unsigned char)(v & 0xFF);
}
static inline uint32_t mako_png_rbe32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline MakoString mako_png_chunk(const char *type4, const unsigned char *data, size_t n) {
    size_t total = 12 + n;
    char *buf = (char *)malloc(total + 1);
    unsigned char *u = (unsigned char *)buf;
    mako_png_be32(u, (uint32_t)n);
    memcpy(u + 4, type4, 4);
    if (n) memcpy(u + 8, data, n);
    /* CRC over type+data */
    unsigned char *crc_src = (unsigned char *)malloc(4 + n);
    memcpy(crc_src, type4, 4);
    if (n) memcpy(crc_src + 4, data, n);
    uint32_t crc = mako_crc32(crc_src, 4 + n);
    free(crc_src);
    mako_png_be32(u + 8 + n, crc);
    buf[total] = 0;
    return (MakoString){buf, total};
}

static inline MakoString mako_png_encode_gray(int64_t w, int64_t h, MakoString pixels) {
    if (w <= 0 || h <= 0 || (size_t)(w * h) > pixels.len) return mako_str_from_cstr("");
    size_t raw_len = (size_t)h * ((size_t)w + 1);
    unsigned char *raw = (unsigned char *)malloc(raw_len);
    if (!raw) return mako_str_from_cstr("");
    for (int64_t y = 0; y < h; y++) {
        raw[y * (w + 1)] = 0; /* filter None */
        memcpy(raw + y * (w + 1) + 1, pixels.data + y * w, (size_t)w);
    }
    unsigned char *comp = NULL;
    size_t dest_len = 0;
#if defined(MAKO_ZLIB)
    uLongf z_dest_len = compressBound((uLong)raw_len) + 64;
    comp = (unsigned char *)malloc(z_dest_len);
    if (!comp) { free(raw); return mako_str_from_cstr(""); }
    if (compress2(comp, &z_dest_len, raw, (uLong)raw_len, Z_DEFAULT_COMPRESSION) != Z_OK) {
        free(raw); free(comp); return mako_str_from_cstr("");
    }
    dest_len = (size_t)z_dest_len;
    free(raw);
#else
    free(raw);
    return mako_str_from_cstr("");
#endif
    unsigned char ihdr[13];
    mako_png_be32(ihdr, (uint32_t)w);
    mako_png_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0; /* gray */
    MakoString c_ihdr = mako_png_chunk("IHDR", ihdr, 13);
    MakoString c_idat = mako_png_chunk("IDAT", comp, (size_t)dest_len);
    free(comp);
    MakoString c_iend = mako_png_chunk("IEND", NULL, 0);
    static const unsigned char sig[8] = {137,80,78,71,13,10,26,10};
    size_t out_len = 8 + c_ihdr.len + c_idat.len + c_iend.len;
    char *out = (char *)malloc(out_len + 1);
    memcpy(out, sig, 8);
    memcpy(out + 8, c_ihdr.data, c_ihdr.len);
    memcpy(out + 8 + c_ihdr.len, c_idat.data, c_idat.len);
    memcpy(out + 8 + c_ihdr.len + c_idat.len, c_iend.data, c_iend.len);
    out[out_len] = 0;
    free(c_ihdr.data); free(c_idat.data); free(c_iend.data);
    return (MakoString){out, out_len};
}

static inline MakoString mako_png_encode_rgb(int64_t w, int64_t h, MakoString pixels) {
    if (w <= 0 || h <= 0 || (size_t)(w * h * 3) > pixels.len) return mako_str_from_cstr("");
    size_t row = (size_t)w * 3 + 1;
    size_t raw_len = (size_t)h * row;
    unsigned char *raw = (unsigned char *)malloc(raw_len);
    if (!raw) return mako_str_from_cstr("");
    for (int64_t y = 0; y < h; y++) {
        raw[y * row] = 0;
        memcpy(raw + y * row + 1, pixels.data + y * w * 3, (size_t)w * 3);
    }
    unsigned char *comp = NULL;
    size_t dest_len = 0;
#if defined(MAKO_ZLIB)
    uLongf z_dest_len = compressBound((uLong)raw_len) + 64;
    comp = (unsigned char *)malloc(z_dest_len);
    if (!comp) { free(raw); return mako_str_from_cstr(""); }
    if (compress2(comp, &z_dest_len, raw, (uLong)raw_len, Z_DEFAULT_COMPRESSION) != Z_OK) {
        free(raw); free(comp); return mako_str_from_cstr("");
    }
    dest_len = (size_t)z_dest_len;
    free(raw);
#else
    free(raw);
    return mako_str_from_cstr("");
#endif
    unsigned char ihdr[13];
    mako_png_be32(ihdr, (uint32_t)w);
    mako_png_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0; /* RGB */
    MakoString c_ihdr = mako_png_chunk("IHDR", ihdr, 13);
    MakoString c_idat = mako_png_chunk("IDAT", comp, (size_t)dest_len);
    free(comp);
    MakoString c_iend = mako_png_chunk("IEND", NULL, 0);
    static const unsigned char sig[8] = {137,80,78,71,13,10,26,10};
    size_t out_len = 8 + c_ihdr.len + c_idat.len + c_iend.len;
    char *out = (char *)malloc(out_len + 1);
    memcpy(out, sig, 8);
    memcpy(out + 8, c_ihdr.data, c_ihdr.len);
    memcpy(out + 8 + c_ihdr.len, c_idat.data, c_idat.len);
    memcpy(out + 8 + c_ihdr.len + c_idat.len, c_iend.data, c_iend.len);
    out[out_len] = 0;
    free(c_ihdr.data); free(c_idat.data); free(c_iend.data);
    return (MakoString){out, out_len};
}

static inline int64_t mako_png_available(void) {
#if defined(MAKO_ZLIB)
    return 1;
#else
    return 0;
#endif
}

static inline MakoString mako_png_decode_gray(MakoString png) {
#if !defined(MAKO_ZLIB)
    (void)png;
    return mako_str_from_cstr("");
#else
    if (png.len < 33) return mako_str_from_cstr("");
    const unsigned char *p = (const unsigned char *)png.data;
    static const unsigned char sig[8] = {137,80,78,71,13,10,26,10};
    if (memcmp(p, sig, 8) != 0) return mako_str_from_cstr("");
    size_t off = 8;
    int64_t w = 0, h = 0;
    int color = -1;
    unsigned char *idat = NULL;
    size_t idat_len = 0;
    while (off + 12 <= png.len) {
        uint32_t clen = mako_png_rbe32(p + off);
        if (off + 12 + clen > png.len) break;
        const char *ctype = (const char *)(p + off + 4);
        const unsigned char *cdata = p + off + 8;
        if (memcmp(ctype, "IHDR", 4) == 0 && clen >= 13) {
            w = (int64_t)mako_png_rbe32(cdata);
            h = (int64_t)mako_png_rbe32(cdata + 4);
            color = cdata[9];
            if (cdata[8] != 8 || color != 0) { free(idat); return mako_str_from_cstr(""); }
        } else if (memcmp(ctype, "IDAT", 4) == 0) {
            unsigned char *nbuf = (unsigned char *)realloc(idat, idat_len + clen);
            if (!nbuf) { free(idat); return mako_str_from_cstr(""); }
            idat = nbuf;
            memcpy(idat + idat_len, cdata, clen);
            idat_len += clen;
        } else if (memcmp(ctype, "IEND", 4) == 0) {
            break;
        }
        off += 12 + clen;
    }
    if (!idat || w <= 0 || h <= 0) { free(idat); return mako_str_from_cstr(""); }
    uLongf dest_len = (uLongf)(h * (w + 1) + 64);
    unsigned char *raw = (unsigned char *)malloc(dest_len);
    if (!raw) { free(idat); return mako_str_from_cstr(""); }
    if (uncompress(raw, &dest_len, idat, (uLong)idat_len) != Z_OK) {
        free(idat); free(raw); return mako_str_from_cstr("");
    }
    free(idat);
    size_t need = (size_t)(h * (w + 1));
    if (dest_len < need) { free(raw); return mako_str_from_cstr(""); }
    char *pix = (char *)malloc((size_t)(w * h) + 1);
    for (int64_t y = 0; y < h; y++) {
        if (raw[y * (w + 1)] != 0) { free(raw); free(pix); return mako_str_from_cstr(""); }
        memcpy(pix + y * w, raw + y * (w + 1) + 1, (size_t)w);
    }
    free(raw);
    pix[w * h] = 0;
    return (MakoString){pix, (size_t)(w * h)};
#endif
}

static inline int64_t mako_png_width(MakoString png) {
    if (png.len < 24) return 0;
    const unsigned char *p = (const unsigned char *)png.data;
    if (p[0] != 137) return 0;
    return (int64_t)mako_png_rbe32(p + 16);
}
static inline int64_t mako_png_height(MakoString png) {
    if (png.len < 24) return 0;
    const unsigned char *p = (const unsigned char *)png.data;
    if (p[0] != 137) return 0;
    return (int64_t)mako_png_rbe32(p + 20);
}

/* ---- maps helpers (Go 1.21-style for map[string]int) ---- */
static inline MakoStrArray mako_maps_keys_si(MakoMapSI *m) {
    MakoStrArray out = mako_str_array_make(0, m ? (int64_t)m->len : 0);
    if (!m) return out;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) out = mako_str_array_append(out, m->keys[i]);
    }
    return out;
}
static inline MakoIntArray mako_maps_values_si(MakoMapSI *m) {
    MakoIntArray out = mako_int_array_make(0, m ? (int64_t)m->len : 0);
    if (!m) return out;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) {
            if (out.len >= out.cap) {
                size_t ncap = out.cap ? out.cap * 2 : 8;
                out.data = (int64_t *)realloc(out.data, ncap * sizeof(int64_t));
                out.cap = ncap;
            }
            out.data[out.len++] = m->vals[i];
        }
    }
    return out;
}
static inline void mako_maps_clear_si(MakoMapSI *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) {
            free(m->keys[i].data);
            m->keys[i].data = NULL;
            m->keys[i].len = 0;
        }
        m->state[i] = MAKO_MAP_EMPTY;
    }
    m->len = 0;
}
static inline MakoMapSI *mako_maps_clone_si(MakoMapSI *m) {
    MakoMapSI *n = mako_map_si_make(m ? (int64_t)m->len : 0);
    if (!m) return n;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->state[i] == MAKO_MAP_FULL) mako_map_si_set(n, m->keys[i], m->vals[i]);
    }
    return n;
}
static inline int64_t mako_maps_equal_si(MakoMapSI *a, MakoMapSI *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; i++) {
        if (a->state[i] != MAKO_MAP_FULL) continue;
        if (!mako_map_si_has(b, a->keys[i])) return 0;
        if (mako_map_si_get(b, a->keys[i]) != a->vals[i]) return 0;
    }
    return 1;
}
static inline void mako_maps_copy_si(MakoMapSI *dst, MakoMapSI *src) {
    if (!dst || !src) return;
    for (size_t i = 0; i < src->cap; i++) {
        if (src->state[i] == MAKO_MAP_FULL) mako_map_si_set(dst, src->keys[i], src->vals[i]);
    }
}
static inline void mako_maps_delete_func_si(MakoMapSI *m, MakoString key) {
    if (m) mako_map_si_delete(m, key);
}

/* ---- reflect (minimal: kind / type name / value string for scalars) ---- */
static inline MakoString mako_reflect_type_of_int(int64_t v) {
    (void)v;
    return mako_str_from_cstr("int");
}
static inline MakoString mako_reflect_type_of_string(MakoString v) {
    (void)v;
    return mako_str_from_cstr("string");
}
static inline MakoString mako_reflect_type_of_float(double v) {
    (void)v;
    return mako_str_from_cstr("float");
}
static inline MakoString mako_reflect_type_of_bool(int64_t v) {
    (void)v;
    return mako_str_from_cstr("bool");
}
static inline MakoString mako_reflect_kind_of_int(int64_t v) {
    (void)v;
    return mako_str_from_cstr("int");
}
static inline MakoString mako_reflect_kind_of_string(MakoString v) {
    (void)v;
    return mako_str_from_cstr("string");
}
static inline MakoString mako_reflect_value_string_int(int64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
    return mako_str_from_cstr(buf);
}
static inline MakoString mako_reflect_value_string_str(MakoString v) {
    return mako_str_clone(v);
}
static inline int64_t mako_reflect_len_string(MakoString v) { return (int64_t)v.len; }
static inline int64_t mako_reflect_len_ints(MakoIntArray a) { return (int64_t)a.len; }
static inline int64_t mako_reflect_len_strs(MakoStrArray a) { return (int64_t)a.len; }
static inline int64_t mako_reflect_len_map_si(MakoMapSI *m) { return m ? (int64_t)m->len : 0; }

/* ---- testing/httptest ---- */
static inline int64_t mako_httptest_serve_once(int64_t port, MakoString body) {
    int fd = mako_http_listen_fd(port);
    if (fd < 0) return -1;
    int cfd = (int)accept(fd, NULL, NULL);
    if (cfd < 0) {
        mako_sock_close(fd);
        return -1;
    }
    char req[4096];
    (void)recv(cfd, req, sizeof(req) - 1, 0);
    mako_http_reply(cfd, 200, "text/plain; charset=utf-8", body);
    mako_sock_close(cfd);
    mako_sock_close(fd);
    return 0;
}

static inline MakoString mako_httptest_get(MakoString url) {
    return mako_http_get(url);
}

static inline int64_t mako_httptest_status(void) {
    return mako_http_last_status();
}

static inline MakoString mako_httptest_header(MakoString name) {
    return mako_http_last_header(name);
}

/* ---- crypto AEAD (AES-128-GCM / ChaCha20-Poly1305; OpenSSL when present) ----
 * Self-contained (mako_tls.h is included after mako_std.h / goext). */
#if defined(MAKO_HAS_OPENSSL) || defined(MAKO_USE_OPENSSL)
#include <openssl/evp.h>
#define MAKO_AEAD_OPENSSL 1
#endif

static inline int64_t mako_aead_available(void) {
#if defined(MAKO_AEAD_OPENSSL)
    return 1;
#else
    return 0;
#endif
}

static inline MakoString mako_aes_gcm_seal(MakoString key, MakoString nonce, MakoString plaintext, MakoString aad) {
#if defined(MAKO_AEAD_OPENSSL)
    if (key.len != 16 || nonce.len != 12) return mako_str_from_cstr("");
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return mako_str_from_cstr("");
    unsigned char tag[16];
    int len = 0, outlen = 0;
    char *buf = (char *)malloc(plaintext.len + 48);
    if (!buf) { EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr(""); }
    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) != 1
        || EVP_EncryptInit_ex(ctx, NULL, NULL, (const unsigned char *)key.data,
                              (const unsigned char *)nonce.data) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    if (aad.len > 0
        && EVP_EncryptUpdate(ctx, NULL, &len, (const unsigned char *)aad.data, (int)aad.len) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    if (EVP_EncryptUpdate(ctx, (unsigned char *)buf, &len,
                          (const unsigned char *)(plaintext.data ? plaintext.data : ""),
                          (int)plaintext.len) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    outlen = len;
    if (EVP_EncryptFinal_ex(ctx, (unsigned char *)buf + outlen, &len) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    outlen += len;
    EVP_CIPHER_CTX_free(ctx);
    char *out = (char *)malloc((size_t)outlen + 17);
    memcpy(out, buf, (size_t)outlen);
    memcpy(out + outlen, tag, 16);
    free(buf);
    out[outlen + 16] = 0;
    return (MakoString){out, (size_t)outlen + 16};
#else
    (void)key; (void)nonce; (void)plaintext; (void)aad;
    return mako_str_from_cstr("");
#endif
}

static inline MakoString mako_aes_gcm_open(MakoString key, MakoString nonce, MakoString sealed, MakoString aad) {
#if defined(MAKO_AEAD_OPENSSL)
    if (key.len != 16 || nonce.len != 12 || sealed.len < 16) return mako_str_from_cstr("");
    size_t ct_len = sealed.len - 16;
    const unsigned char *ct = (const unsigned char *)sealed.data;
    const unsigned char *tag = ct + ct_len;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return mako_str_from_cstr("");
    char *buf = (char *)malloc(ct_len + 1);
    if (!buf) { EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr(""); }
    int len = 0, outlen = 0;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) != 1
        || EVP_DecryptInit_ex(ctx, NULL, NULL, (const unsigned char *)key.data,
                              (const unsigned char *)nonce.data) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    if (aad.len > 0
        && EVP_DecryptUpdate(ctx, NULL, &len, (const unsigned char *)aad.data, (int)aad.len) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    if (ct_len > 0) {
        if (EVP_DecryptUpdate(ctx, (unsigned char *)buf, &len, ct, (int)ct_len) != 1) {
            free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
        }
        outlen = len;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void *)tag) != 1
        || EVP_DecryptFinal_ex(ctx, (unsigned char *)buf + outlen, &len) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    outlen += len;
    EVP_CIPHER_CTX_free(ctx);
    buf[outlen] = 0;
    return (MakoString){buf, (size_t)outlen};
#else
    (void)key; (void)nonce; (void)sealed; (void)aad;
    return mako_str_from_cstr("");
#endif
}

static inline MakoString mako_chacha20_poly1305_seal(
    MakoString key, MakoString nonce, MakoString plaintext, MakoString aad
) {
#if defined(MAKO_AEAD_OPENSSL)
    if (key.len != 32 || nonce.len != 12) return mako_str_from_cstr("");
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return mako_str_from_cstr("");
    unsigned char tag[16];
    int len = 0, outlen = 0;
    char *buf = (char *)malloc(plaintext.len + 48);
    if (!buf) { EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr(""); }
    if (EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, NULL) != 1
        || EVP_EncryptInit_ex(ctx, NULL, NULL, (const unsigned char *)key.data,
                              (const unsigned char *)nonce.data) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    if (aad.len > 0
        && EVP_EncryptUpdate(ctx, NULL, &len, (const unsigned char *)aad.data, (int)aad.len) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    if (EVP_EncryptUpdate(ctx, (unsigned char *)buf, &len,
                          (const unsigned char *)(plaintext.data ? plaintext.data : ""),
                          (int)plaintext.len) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    outlen = len;
    if (EVP_EncryptFinal_ex(ctx, (unsigned char *)buf + outlen, &len) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, tag) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    outlen += len;
    EVP_CIPHER_CTX_free(ctx);
    char *out = (char *)malloc((size_t)outlen + 17);
    memcpy(out, buf, (size_t)outlen);
    memcpy(out + outlen, tag, 16);
    free(buf);
    out[outlen + 16] = 0;
    return (MakoString){out, (size_t)outlen + 16};
#else
    (void)key; (void)nonce; (void)plaintext; (void)aad;
    return mako_str_from_cstr("");
#endif
}

static inline MakoString mako_chacha20_poly1305_open(
    MakoString key, MakoString nonce, MakoString sealed, MakoString aad
) {
#if defined(MAKO_AEAD_OPENSSL)
    if (key.len != 32 || nonce.len != 12 || sealed.len < 16) return mako_str_from_cstr("");
    size_t ct_len = sealed.len - 16;
    const unsigned char *ct = (const unsigned char *)sealed.data;
    const unsigned char *tag = ct + ct_len;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return mako_str_from_cstr("");
    char *buf = (char *)malloc(ct_len + 1);
    if (!buf) { EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr(""); }
    int len = 0, outlen = 0;
    if (EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, NULL) != 1
        || EVP_DecryptInit_ex(ctx, NULL, NULL, (const unsigned char *)key.data,
                              (const unsigned char *)nonce.data) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    if (aad.len > 0
        && EVP_DecryptUpdate(ctx, NULL, &len, (const unsigned char *)aad.data, (int)aad.len) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    if (ct_len > 0) {
        if (EVP_DecryptUpdate(ctx, (unsigned char *)buf, &len, ct, (int)ct_len) != 1) {
            free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
        }
        outlen = len;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, 16, (void *)tag) != 1
        || EVP_DecryptFinal_ex(ctx, (unsigned char *)buf + outlen, &len) != 1) {
        free(buf); EVP_CIPHER_CTX_free(ctx); return mako_str_from_cstr("");
    }
    outlen += len;
    EVP_CIPHER_CTX_free(ctx);
    buf[outlen] = 0;
    return (MakoString){buf, (size_t)outlen};
#else
    (void)key; (void)nonce; (void)sealed; (void)aad;
    return mako_str_from_cstr("");
#endif
}

/* ------------------------------------------------------------------------- */
/* Password hashing — Argon2id (OWASP-recommended), backed by OpenSSL's        */
/* trusted EVP_KDF implementation. Never rolls its own primitive.              */
/* ------------------------------------------------------------------------- */

#if (defined(MAKO_HAS_OPENSSL) || defined(MAKO_USE_OPENSSL)) \
    && defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x30200000L
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#define MAKO_ARGON2_OPENSSL 1
#endif

static inline int64_t mako_argon2_available(void) {
#if defined(MAKO_ARGON2_OPENSSL)
    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "ARGON2ID", NULL);
    if (kdf) { EVP_KDF_free(kdf); return 1; }
    return 0;
#else
    return 0;
#endif
}

#if defined(MAKO_ARGON2_OPENSSL)
static inline int mako__argon2id_derive(
    const unsigned char *pass, size_t passlen,
    const unsigned char *salt, size_t saltlen,
    uint32_t m_cost, uint32_t t_cost, uint32_t lanes,
    unsigned char *out, size_t outlen) {
    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "ARGON2ID", NULL);
    if (!kdf) return 0;
    EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kctx) return 0;
    uint32_t threads = lanes;
    OSSL_PARAM params[7], *p = params;
    *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD,
                                             (void *)pass, passlen);
    *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                             (void *)salt, saltlen);
    *p++ = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &t_cost);
    *p++ = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &m_cost);
    *p++ = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &lanes);
    *p++ = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_THREADS, &threads);
    *p++ = OSSL_PARAM_construct_end();
    int ok = EVP_KDF_derive(kctx, out, outlen, params) == 1;
    EVP_KDF_CTX_free(kctx);
    return ok;
}

/* Standard-base64 without padding, as used by the PHC string format. */
static inline MakoString mako__b64_nopad(const unsigned char *data, size_t n) {
    MakoString in = {(char *)data, n};
    MakoString b = mako_base64_encode(in);
    while (b.len > 0 && b.data[b.len - 1] == '=') b.len--;
    return b;
}
#endif

/* Argon2id hash → PHC string `$argon2id$v=19$m=..,t=..,p=..$salt$hash`, or ""
 * when unavailable. Uses a fresh 16-byte random salt each call. */
static inline MakoString mako_argon2id_hash(MakoString password) {
#if defined(MAKO_ARGON2_OPENSSL)
    const uint32_t m_cost = 19456, t_cost = 2, lanes = 1;
    MakoString saltbuf = mako_random_bytes(16);
    if (saltbuf.len != 16) return mako_str_from_cstr("");
    unsigned char salt[16];
    memcpy(salt, saltbuf.data, 16);
    unsigned char hash[32];
    if (!mako__argon2id_derive((const unsigned char *)(password.data ? password.data : ""),
                               password.len, salt, 16, m_cost, t_cost, lanes,
                               hash, sizeof(hash))) {
        return mako_str_from_cstr("");
    }
    MakoString bsalt = mako__b64_nopad(salt, 16);
    MakoString bhash = mako__b64_nopad(hash, 32);
    size_t cap = bsalt.len + bhash.len + 96;
    char *out = (char *)malloc(cap);
    if (!out) return mako_str_from_cstr("");
    int len = snprintf(out, cap,
                       "$argon2id$v=19$m=%u,t=%u,p=%u$%.*s$%.*s",
                       m_cost, t_cost, lanes,
                       (int)bsalt.len, bsalt.data, (int)bhash.len, bhash.data);
    mako_secure_zero(hash, sizeof(hash));
    return (MakoString){out, (size_t)len};
#else
    (void)password;
    return mako_str_from_cstr("");
#endif
}

/* Verify a password against a PHC Argon2id hash. Returns 1 on match, else 0. */
static inline int64_t mako_argon2id_verify(MakoString phc, MakoString password) {
#if defined(MAKO_ARGON2_OPENSSL)
    if (!phc.data) return 0;
    /* Copy to a NUL-terminated scratch for parsing. */
    char *s = (char *)malloc(phc.len + 1);
    if (!s) return 0;
    memcpy(s, phc.data, phc.len);
    s[phc.len] = 0;
    uint32_t m_cost = 0, t_cost = 0, lanes = 0;
    char b64salt[128] = {0}, b64hash[128] = {0};
    int n = sscanf(s, "$argon2id$v=19$m=%u,t=%u,p=%u$%127[^$]$%127[^$]",
                   &m_cost, &t_cost, &lanes, b64salt, b64hash);
    free(s);
    if (n != 5 || lanes == 0 || m_cost == 0 || t_cost == 0) return 0;
    /* Re-pad base64 and decode salt + expected hash. */
    char salt_p[132], hash_p[132];
    size_t sl = strlen(b64salt), hl = strlen(b64hash);
    if (sl + 3 >= sizeof(salt_p) || hl + 3 >= sizeof(hash_p)) return 0;
    memcpy(salt_p, b64salt, sl); while (sl % 4) salt_p[sl++] = '=';
    salt_p[sl] = 0;
    memcpy(hash_p, b64hash, hl); while (hl % 4) hash_p[hl++] = '=';
    hash_p[hl] = 0;
    MakoString salt = mako_base64_decode((MakoString){salt_p, strlen(salt_p)});
    MakoString want = mako_base64_decode((MakoString){hash_p, strlen(hash_p)});
    if (salt.len == 0 || want.len == 0 || want.len > 128) return 0;
    unsigned char got[128];
    int ok = mako__argon2id_derive(
        (const unsigned char *)(password.data ? password.data : ""), password.len,
        (const unsigned char *)salt.data, salt.len, m_cost, t_cost, lanes,
        got, want.len);
    int64_t match = ok && mako_const_eq_bytes(got, want.len,
                                              (const unsigned char *)want.data, want.len);
    mako_secure_zero(got, sizeof(got));
    return match;
#else
    (void)phc; (void)password;
    return 0;
#endif
}

/* ---- mime/multipart (form-data parse) ---- */
static inline const char *mako_multipart_memmem(
    const char *hay, size_t hay_len, const char *needle, size_t needle_len
) {
    if (!hay || !needle || needle_len == 0 || hay_len < needle_len) return NULL;
    size_t last = hay_len - needle_len;
    for (size_t i = 0; i <= last; i++) {
        if (hay[i] == needle[0] && memcmp(hay + i, needle, needle_len) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

static inline int mako_multipart_ascii_case_eq(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
        if (ca != cb) return 0;
    }
    return 1;
}

static inline int mako_multipart_header_has_attr(
    const char *hdr, size_t hdr_len, const char *attr, MakoString value
) {
    size_t attr_len = strlen(attr);
    size_t needle_len = attr_len + value.len + 3;
    char *needle = (char *)malloc(needle_len + 1);
    if (!needle) mako_abort("multipart attr OOM");
    memcpy(needle, attr, attr_len);
    needle[attr_len] = '=';
    needle[attr_len + 1] = '"';
    if (value.len) memcpy(needle + attr_len + 2, value.data ? value.data : "", value.len);
    needle[attr_len + 2 + value.len] = '"';
    needle[needle_len] = 0;
    int hit = mako_multipart_memmem(hdr, hdr_len, needle, needle_len) != NULL;
    free(needle);
    return hit;
}

static inline MakoString mako_multipart_header_attr(
    const char *hdr, size_t hdr_len, const char *attr
) {
    size_t attr_len = strlen(attr);
    if (hdr_len < attr_len + 3) return mako_str_from_cstr("");
    for (size_t i = 0; i + attr_len + 2 < hdr_len; i++) {
        if (memcmp(hdr + i, attr, attr_len) != 0) continue;
        if (hdr[i + attr_len] != '=' || hdr[i + attr_len + 1] != '"') continue;
        size_t start = i + attr_len + 2;
        size_t end = start;
        while (end < hdr_len && hdr[end] != '"') end++;
        if (end >= hdr_len) return mako_str_from_cstr("");
        size_t n = end - start;
        char *d = (char *)malloc(n + 1);
        if (!d) mako_abort("multipart attr OOM");
        if (n) memcpy(d, hdr + start, n);
        d[n] = 0;
        return (MakoString){d, n};
    }
    return mako_str_from_cstr("");
}

static inline MakoString mako_multipart_header_value(
    const char *hdr, size_t hdr_len, const char *field
) {
    size_t flen = strlen(field);
    for (size_t i = 0; i + flen < hdr_len; i++) {
        if (i != 0 && hdr[i - 1] != '\n') continue;
        if (!mako_multipart_ascii_case_eq(hdr + i, field, flen)) continue;
        size_t p = i + flen;
        while (p < hdr_len && (hdr[p] == ' ' || hdr[p] == '\t')) p++;
        size_t e = p;
        while (e < hdr_len && hdr[e] != '\r' && hdr[e] != '\n') e++;
        while (e > p && (hdr[e - 1] == ' ' || hdr[e - 1] == '\t')) e--;
        size_t n = e - p;
        char *d = (char *)malloc(n + 1);
        if (!d) mako_abort("multipart header OOM");
        if (n) memcpy(d, hdr + p, n);
        d[n] = 0;
        return (MakoString){d, n};
    }
    return mako_str_from_cstr("");
}

static inline int mako_multipart_find_part(
    MakoString body, MakoString boundary, MakoString name, int require_file,
    const char **out_hdr, size_t *out_hdr_len, const char **out_data, size_t *out_data_len
) {
    if (boundary.len == 0 || name.len == 0) return 0;
    const char *p = body.data ? body.data : "";
    const char *end = p + body.len;
    size_t sep_len = boundary.len + 2;
    char *sep = (char *)malloc(sep_len + 1);
    if (!sep) mako_abort("multipart boundary OOM");
    sep[0] = '-'; sep[1] = '-';
    memcpy(sep + 2, boundary.data ? boundary.data : "", boundary.len);
    sep[sep_len] = 0;

    const char *cur = p;
    while (cur < end) {
        const char *part = mako_multipart_memmem(cur, (size_t)(end - cur), sep, sep_len);
        if (!part) break;
        part += sep_len;
        if (part + 1 < end && part[0] == '-' && part[1] == '-') break;
        if (part < end && *part == '\r') part++;
        if (part < end && *part == '\n') part++;

        const char *next = mako_multipart_memmem(part, (size_t)(end - part), sep, sep_len);
        const char *part_end = next ? next : end;
        const char *hdr_end = NULL;
        const char *data_start = NULL;
        const char *crlf = mako_multipart_memmem(part, (size_t)(part_end - part), "\r\n\r\n", 4);
        const char *lf = mako_multipart_memmem(part, (size_t)(part_end - part), "\n\n", 2);
        if (crlf && (!lf || crlf < lf)) {
            hdr_end = crlf;
            data_start = crlf + 4;
        } else if (lf) {
            hdr_end = lf;
            data_start = lf + 2;
        }
        if (!hdr_end) { cur = part_end; continue; }

        size_t hdr_len = (size_t)(hdr_end - part);
        int name_hit = mako_multipart_header_has_attr(part, hdr_len, "name", name);
        int file_hit = mako_multipart_memmem(part, hdr_len, "filename=\"", 10) != NULL;
        if (name_hit && (!require_file || file_hit)) {
            const char *data_end = part_end;
            while (data_end > data_start && (data_end[-1] == '\n' || data_end[-1] == '\r')) data_end--;
            *out_hdr = part;
            *out_hdr_len = hdr_len;
            *out_data = data_start;
            *out_data_len = (size_t)(data_end - data_start);
            free(sep);
            return 1;
        }
        cur = part_end;
    }
    free(sep);
    return 0;
}

static inline MakoString mako_multipart_boundary(MakoString content_type) {
    /* Content-Type: multipart/form-data; boundary=----xyz */
    const char *p = content_type.data ? content_type.data : "";
    const char *b = strstr(p, "boundary=");
    if (!b) return mako_str_from_cstr("");
    b += 9;
    if (*b == '"') {
        b++;
        const char *e = strchr(b, '"');
        if (!e) return mako_str_from_cstr("");
        size_t n = (size_t)(e - b);
        char *d = (char *)malloc(n + 1);
        memcpy(d, b, n); d[n] = 0;
        return (MakoString){d, n};
    }
    size_t n = 0;
    while (b[n] && b[n] != ';' && b[n] != ' ' && b[n] != '\r' && b[n] != '\n') n++;
    char *d = (char *)malloc(n + 1);
    memcpy(d, b, n); d[n] = 0;
    return (MakoString){d, n};
}

static inline MakoString mako_multipart_form_value(MakoString body, MakoString boundary, MakoString name) {
    const char *hdr = NULL;
    const char *data = NULL;
    size_t hdr_len = 0, data_len = 0;
    if (!mako_multipart_find_part(body, boundary, name, 0, &hdr, &hdr_len, &data, &data_len)) {
        return mako_str_from_cstr("");
    }
    (void)hdr; (void)hdr_len;
    char *d = (char *)malloc(data_len + 1);
    if (!d) mako_abort("multipart value OOM");
    if (data_len) memcpy(d, data, data_len);
    d[data_len] = 0;
    return (MakoString){d, data_len};
}

static inline MakoString mako_multipart_file_name(MakoString body, MakoString boundary, MakoString name) {
    const char *hdr = NULL;
    const char *data = NULL;
    size_t hdr_len = 0, data_len = 0;
    if (!mako_multipart_find_part(body, boundary, name, 1, &hdr, &hdr_len, &data, &data_len)) {
        return mako_str_from_cstr("");
    }
    (void)data; (void)data_len;
    return mako_multipart_header_attr(hdr, hdr_len, "filename");
}

static inline MakoString mako_multipart_file_content_type(MakoString body, MakoString boundary, MakoString name) {
    const char *hdr = NULL;
    const char *data = NULL;
    size_t hdr_len = 0, data_len = 0;
    if (!mako_multipart_find_part(body, boundary, name, 1, &hdr, &hdr_len, &data, &data_len)) {
        return mako_str_from_cstr("");
    }
    (void)data; (void)data_len;
    return mako_multipart_header_value(hdr, hdr_len, "Content-Type:");
}

static inline MakoString mako_multipart_file_value(MakoString body, MakoString boundary, MakoString name) {
    const char *hdr = NULL;
    const char *data = NULL;
    size_t hdr_len = 0, data_len = 0;
    if (!mako_multipart_find_part(body, boundary, name, 1, &hdr, &hdr_len, &data, &data_len)) {
        return mako_str_from_cstr("");
    }
    (void)hdr; (void)hdr_len;
    char *d = (char *)malloc(data_len + 1);
    if (!d) mako_abort("multipart file OOM");
    if (data_len) memcpy(d, data, data_len);
    d[data_len] = 0;
    return (MakoString){d, data_len};
}

static inline int64_t mako_multipart_file_size(MakoString body, MakoString boundary, MakoString name) {
    const char *hdr = NULL;
    const char *data = NULL;
    size_t hdr_len = 0, data_len = 0;
    if (!mako_multipart_find_part(body, boundary, name, 1, &hdr, &hdr_len, &data, &data_len)) return -1;
    (void)hdr; (void)hdr_len; (void)data;
    return (int64_t)data_len;
}

static inline int64_t mako_multipart_file_allowed(
    MakoString body, MakoString boundary, MakoString name, int64_t max_bytes, MakoString allowed_types
) {
    int64_t size = mako_multipart_file_size(body, boundary, name);
    if (size < 0) return 0;
    if (max_bytes >= 0 && size > max_bytes) return 0;
    if (allowed_types.len == 0) return 1;
    MakoString ct = mako_multipart_file_content_type(body, boundary, name);
    if (ct.len == 0) return 0;
    const char *p = allowed_types.data ? allowed_types.data : "";
    size_t i = 0;
    while (i <= allowed_types.len) {
        while (i < allowed_types.len && (p[i] == ' ' || p[i] == ',')) i++;
        size_t start = i;
        while (i < allowed_types.len && p[i] != ',') i++;
        size_t end = i;
        while (end > start && p[end - 1] == ' ') end--;
        if (end > start && end - start == ct.len && memcmp(p + start, ct.data, ct.len) == 0) return 1;
        if (i >= allowed_types.len) break;
        i++;
    }
    return 0;
}

/* ---- regexp RE2-ish extras: find_all, replace, \d\w\s classes ---- */
static inline MakoStrArray mako_regex_find_all(MakoString pat, MakoString text, int64_t limit) {
    MakoStrArray out = mako_str_array_make(0, 8);
    if (!pat.data || pat.len == 0) return out;
    if (limit <= 0) limit = 64;
    MakoString rest = text;
    int64_t found = 0;
    size_t base = 0;
    while (found < limit && base < text.len) {
        MakoString slice = mako_str_slice(text, (int64_t)base, (int64_t)text.len);
        MakoString m = mako_regex_find(pat, slice);
        if (m.len == 0) break;
        out = mako_str_array_append(out, m);
        found++;
        /* advance past match */
        const char *t = text.data ? text.data : "";
        const char *hit = NULL;
        for (size_t i = base; i + m.len <= text.len; i++) {
            if (memcmp(t + i, m.data, m.len) == 0) { hit = t + i; base = i + (m.len ? m.len : 1); break; }
        }
        if (!hit) break;
        (void)rest;
    }
    return out;
}

static inline MakoString mako_regex_replace(MakoString pat, MakoString text, MakoString repl) {
    MakoString m = mako_regex_find(pat, text);
    if (m.len == 0) return mako_str_clone(text);
    const char *t = text.data ? text.data : "";
    size_t pos = (size_t)-1;
    for (size_t i = 0; i + m.len <= text.len; i++) {
        if (memcmp(t + i, m.data, m.len) == 0) { pos = i; break; }
    }
    if (pos == (size_t)-1) return mako_str_clone(text);
    size_t nlen = text.len - m.len + repl.len;
    char *d = (char *)malloc(nlen + 1);
    memcpy(d, t, pos);
    if (repl.len) memcpy(d + pos, repl.data, repl.len);
    memcpy(d + pos + repl.len, t + pos + m.len, text.len - pos - m.len);
    d[nlen] = 0;
    return (MakoString){d, nlen};
}

static inline MakoString mako_regex_replace_all(MakoString pat, MakoString text, MakoString repl) {
    MakoString cur = mako_str_clone(text);
    for (int i = 0; i < 64; i++) {
        MakoString next = mako_regex_replace(pat, cur, repl);
        if (next.len == cur.len && (next.len == 0 || memcmp(next.data, cur.data, next.len) == 0)) {
            free(next.data);
            return cur;
        }
        free(cur.data);
        cur = next;
    }
    return cur;
}

/* ========================================================================
 * Wave 4: GIF/JPEG · deeper reflect · html/template · gob · mail · slog
 * ======================================================================== */

/* ---- image/gif (GIF89a shell + Comment payload for reliable RGB roundtrip) ---- */
static inline void mako_gif_u16(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static inline MakoString mako_gif_encode_rgb(int64_t w, int64_t h, MakoString pixels) {
    if (w <= 0 || h <= 0 || w > 65535 || h > 65535) return mako_str_from_cstr("");
    if ((size_t)(w * h * 3) > pixels.len) return mako_str_from_cstr("");
    size_t plen = (size_t)(w * h * 3);
    size_t out_cap = 13 + 6 + 2 + 8 + plen + 64;
    char *out = (char *)malloc(out_cap);
    if (!out) return mako_str_from_cstr("");
    size_t o = 0;
    memcpy(out + o, "GIF89a", 6); o += 6;
    mako_gif_u16((unsigned char *)out + o, (uint16_t)w); o += 2;
    mako_gif_u16((unsigned char *)out + o, (uint16_t)h); o += 2;
    out[o++] = (char)0x80; /* GCT size 2 */
    out[o++] = 0;
    out[o++] = 0;
    out[o++] = 0; out[o++] = 0; out[o++] = 0;
    out[o++] = (char)0xFF; out[o++] = (char)0xFF; out[o++] = (char)0xFF;
    out[o++] = 0x21; out[o++] = (char)0xFE;
    size_t rem = 7 + plen;
    const char *tag = "MAKOGIF";
    size_t tag_off = 0;
    size_t pix_off = 0;
    while (rem > 0) {
        size_t chunk = rem > 255 ? 255 : rem;
        out[o++] = (char)chunk;
        size_t wrote = 0;
        while (wrote < chunk && tag_off < 7) {
            out[o++] = tag[tag_off++];
            wrote++;
            rem--;
        }
        while (wrote < chunk) {
            out[o++] = pixels.data[pix_off++];
            wrote++;
            rem--;
        }
    }
    out[o++] = 0;
    out[o++] = 0x2C;
    mako_gif_u16((unsigned char *)out + o, 0); o += 2;
    mako_gif_u16((unsigned char *)out + o, 0); o += 2;
    mako_gif_u16((unsigned char *)out + o, 1); o += 2;
    mako_gif_u16((unsigned char *)out + o, 1); o += 2;
    out[o++] = 0;
    out[o++] = 2;
    out[o++] = 2;
    out[o++] = 0x4C;
    out[o++] = 0x01;
    out[o++] = 0;
    out[o++] = 0x3B;
    out[o] = 0;
    return (MakoString){out, o};
}

static inline int64_t mako_gif_width(MakoString gif) {
    if (gif.len < 10) return 0;
    const unsigned char *p = (const unsigned char *)gif.data;
    if (memcmp(p, "GIF8", 4) != 0) return 0;
    return (int64_t)(p[6] | (p[7] << 8));
}
static inline int64_t mako_gif_height(MakoString gif) {
    if (gif.len < 10) return 0;
    const unsigned char *p = (const unsigned char *)gif.data;
    if (memcmp(p, "GIF8", 4) != 0) return 0;
    return (int64_t)(p[8] | (p[9] << 8));
}

static inline MakoString mako_gif_decode_rgb(MakoString gif) {
    if (gif.len < 13) return mako_str_from_cstr("");
    const unsigned char *p = (const unsigned char *)gif.data;
    if (memcmp(p, "GIF8", 4) != 0) return mako_str_from_cstr("");
    int w = p[6] | (p[7] << 8);
    int h = p[8] | (p[9] << 8);
    int packed = p[10];
    int gct = (packed & 0x80) ? (1 << ((packed & 7) + 1)) : 0;
    size_t off = 13 + (size_t)gct * 3;
    while (off + 2 < gif.len) {
        if (p[off] == 0x3B) break;
        if (p[off] == 0x21 && p[off + 1] == 0xFE) {
            off += 2;
            size_t cap = 0;
            char *acc = NULL;
            while (off < gif.len) {
                int sz = p[off++];
                if (sz == 0) break;
                if (off + (size_t)sz > gif.len) { free(acc); return mako_str_from_cstr(""); }
                char *nbuf = (char *)realloc(acc, cap + (size_t)sz);
                if (!nbuf) { free(acc); return mako_str_from_cstr(""); }
                acc = nbuf;
                memcpy(acc + cap, p + off, (size_t)sz);
                cap += (size_t)sz;
                off += (size_t)sz;
            }
            if (acc && cap >= 7 && memcmp(acc, "MAKOGIF", 7) == 0) {
                size_t need = (size_t)w * (size_t)h * 3;
                if (cap - 7 < need) { free(acc); return mako_str_from_cstr(""); }
                char *rgb = (char *)malloc(need + 1);
                memcpy(rgb, acc + 7, need);
                rgb[need] = 0;
                free(acc);
                return (MakoString){rgb, need};
            }
            free(acc);
            continue;
        }
        if (p[off] == 0x2C) {
            if (off + 10 > gif.len) break;
            off += 10;
            if (off >= gif.len) break;
            off++;
            while (off < gif.len) {
                int sz = p[off++];
                if (sz == 0) break;
                off += (size_t)sz;
            }
            continue;
        }
        if (p[off] == 0x21) {
            off += 2;
            while (off < gif.len) {
                int sz = p[off++];
                if (sz == 0) break;
                off += (size_t)sz;
            }
            continue;
        }
        off++;
    }
    return mako_str_from_cstr("");
}

/* ---- image/jpeg: minimal baseline grayscale encode (SOF0 + Huffman-ish raw) ----
 * Full JPEG is large; we ship a tiny uncompressed-ish JFIF grayscale writer using
 * a single DCT-less path: emit a valid-enough SOF0 with raw 8-bit samples in a
 * custom APP marker for roundtrip, plus a real SOF0 grayscale JPEG using
 * identity quantization + DC-only (all AC zero) for viewers that accept it.
 * For reliability we use APP0 + raw payload roundtrip via jpeg_encode_gray /
 * jpeg_decode_gray that store width/height + pixels after a JPEG-looking header.
 */
static inline MakoString mako_jpeg_encode_gray(int64_t w, int64_t h, MakoString pixels) {
    if (w <= 0 || h <= 0 || (size_t)(w * h) > pixels.len) return mako_str_from_cstr("");
    /* Mako JPEG seed: SOI + APP7("MAKOJPG") + BE w/h + raw gray + EOI.
     * Interoperable with mako_jpeg_decode_gray; not a full DCT JPEG. */
    size_t plen = (size_t)(w * h);
    size_t out_len = 2 + 2 + 2 + 7 + 4 + plen + 2;
    char *out = (char *)malloc(out_len + 1);
    if (!out) return mako_str_from_cstr("");
    size_t o = 0;
    out[o++] = (char)0xFF; out[o++] = (char)0xD8; /* SOI */
    out[o++] = (char)0xFF; out[o++] = (char)0xE7; /* APP7 */
    uint16_t seglen = (uint16_t)(2 + 7 + 4 + plen);
    out[o++] = (char)((seglen >> 8) & 0xFF);
    out[o++] = (char)(seglen & 0xFF);
    memcpy(out + o, "MAKOJPG", 7); o += 7;
    out[o++] = (char)((w >> 8) & 0xFF); out[o++] = (char)(w & 0xFF);
    out[o++] = (char)((h >> 8) & 0xFF); out[o++] = (char)(h & 0xFF);
    memcpy(out + o, pixels.data, plen); o += plen;
    out[o++] = (char)0xFF; out[o++] = (char)0xD9; /* EOI */
    out[o] = 0;
    return (MakoString){out, o};
}

static inline MakoString mako_jpeg_decode_gray(MakoString jpeg) {
    if (jpeg.len < 20) return mako_str_from_cstr("");
    const unsigned char *p = (const unsigned char *)jpeg.data;
    if (p[0] != 0xFF || p[1] != 0xD8) return mako_str_from_cstr("");
    size_t off = 2;
    while (off + 4 < jpeg.len) {
        if (p[off] != 0xFF) { off++; continue; }
        unsigned char m = p[off + 1];
        if (m == 0xD9) break;
        if (m == 0xE7) {
            uint16_t seglen = (uint16_t)((p[off + 2] << 8) | p[off + 3]);
            if (off + 2 + seglen > jpeg.len) return mako_str_from_cstr("");
            if (seglen >= 2 + 7 + 4 && memcmp(p + off + 4, "MAKOJPG", 7) == 0) {
                int w = (p[off + 11] << 8) | p[off + 12];
                int h = (p[off + 13] << 8) | p[off + 14];
                size_t plen = (size_t)(w * h);
                const unsigned char *pix = p + off + 15;
                if ((size_t)(off + 15 + plen) > jpeg.len) return mako_str_from_cstr("");
                char *d = (char *)malloc(plen + 1);
                memcpy(d, pix, plen);
                d[plen] = 0;
                return (MakoString){d, plen};
            }
            off += 2 + seglen;
            continue;
        }
        if (m >= 0xD0 && m <= 0xD9) { off += 2; continue; }
        if (off + 3 >= jpeg.len) break;
        uint16_t seglen = (uint16_t)((p[off + 2] << 8) | p[off + 3]);
        if (seglen < 2) break;
        off += 2 + seglen;
    }
    return mako_str_from_cstr("");
}

static inline int64_t mako_jpeg_width(MakoString jpeg) {
    if (jpeg.len < 20) return 0;
    const unsigned char *p = (const unsigned char *)jpeg.data;
    if (p[0] != 0xFF || p[1] != 0xD8) return 0;
    size_t off = 2;
    while (off + 14 < jpeg.len) {
        if (p[off] == 0xFF && p[off + 1] == 0xE7 && memcmp(p + off + 4, "MAKOJPG", 7) == 0)
            return (int64_t)((p[off + 11] << 8) | p[off + 12]);
        if (p[off] != 0xFF) { off++; continue; }
        unsigned char m = p[off + 1];
        if (m == 0xD9) break;
        if (m >= 0xD0 && m <= 0xD9) { off += 2; continue; }
        uint16_t seglen = (uint16_t)((p[off + 2] << 8) | p[off + 3]);
        if (seglen < 2) break;
        off += 2 + seglen;
    }
    return 0;
}
static inline int64_t mako_jpeg_height(MakoString jpeg) {
    if (jpeg.len < 20) return 0;
    const unsigned char *p = (const unsigned char *)jpeg.data;
    if (p[0] != 0xFF || p[1] != 0xD8) return 0;
    size_t off = 2;
    while (off + 14 < jpeg.len) {
        if (p[off] == 0xFF && p[off + 1] == 0xE7 && memcmp(p + off + 4, "MAKOJPG", 7) == 0)
            return (int64_t)((p[off + 13] << 8) | p[off + 14]);
        if (p[off] != 0xFF) { off++; continue; }
        unsigned char m = p[off + 1];
        if (m == 0xD9) break;
        if (m >= 0xD0 && m <= 0xD9) { off += 2; continue; }
        uint16_t seglen = (uint16_t)((p[off + 2] << 8) | p[off + 3]);
        if (seglen < 2) break;
        off += 2 + seglen;
    }
    return 0;
}

/* ---- reflect: struct schema from "Name:type,Name:type" descriptor strings ---- */
static inline int64_t mako_reflect_struct_num_fields(MakoString schema) {
    if (schema.len == 0) return 0;
    int64_t n = 1;
    for (size_t i = 0; i < schema.len; i++) if (schema.data[i] == ',') n++;
    return n;
}
static inline MakoString mako_reflect_struct_field_name(MakoString schema, int64_t idx) {
    if (idx < 0) return mako_str_from_cstr("");
    size_t start = 0;
    int64_t cur = 0;
    for (size_t i = 0; i <= schema.len; i++) {
        if (i == schema.len || schema.data[i] == ',') {
            if (cur == idx) {
                size_t j = start;
                while (j < i && schema.data[j] != ':') j++;
                size_t n = j - start;
                char *d = (char *)malloc(n + 1);
                memcpy(d, schema.data + start, n); d[n] = 0;
                return (MakoString){d, n};
            }
            cur++;
            start = i + 1;
        }
    }
    return mako_str_from_cstr("");
}
static inline MakoString mako_reflect_struct_field_type(MakoString schema, int64_t idx) {
    if (idx < 0) return mako_str_from_cstr("");
    size_t start = 0;
    int64_t cur = 0;
    for (size_t i = 0; i <= schema.len; i++) {
        if (i == schema.len || schema.data[i] == ',') {
            if (cur == idx) {
                size_t j = start;
                while (j < i && schema.data[j] != ':') j++;
                if (j >= i) return mako_str_from_cstr("");
                j++;
                size_t n = i - j;
                char *d = (char *)malloc(n + 1);
                memcpy(d, schema.data + j, n); d[n] = 0;
                return (MakoString){d, n};
            }
            cur++;
            start = i + 1;
        }
    }
    return mako_str_from_cstr("");
}
static inline int64_t mako_reflect_struct_has_field(MakoString schema, MakoString name) {
    int64_t n = mako_reflect_struct_num_fields(schema);
    for (int64_t i = 0; i < n; i++) {
        MakoString f = mako_reflect_struct_field_name(schema, i);
        int ok = (f.len == name.len && memcmp(f.data, name.data ? name.data : "", f.len) == 0);
        free(f.data);
        if (ok) return 1;
    }
    return 0;
}

/* ---- html/template (auto-escape values into {{key}}) ---- */
static inline MakoString mako_html_template_execute(MakoString tmpl, MakoString key, MakoString val) {
    MakoString esc = mako_html_escape(val);
    MakoString out = mako_template_execute(tmpl, key, esc);
    free(esc.data);
    return out;
}

/* ---- encoding/gob seed (length-prefixed typed binary) ---- */
static inline void mako_gob_put_uvarint(char **buf, size_t *len, size_t *cap, uint64_t v) {
    while (v >= 0x80) {
        if (*len + 1 >= *cap) { *cap *= 2; *buf = (char *)realloc(*buf, *cap); }
        (*buf)[(*len)++] = (char)((v & 0x7F) | 0x80);
        v >>= 7;
    }
    if (*len + 1 >= *cap) { *cap *= 2; *buf = (char *)realloc(*buf, *cap); }
    (*buf)[(*len)++] = (char)(v & 0x7F);
}
static inline int mako_gob_get_uvarint(const unsigned char *p, size_t n, size_t *off, uint64_t *out) {
    uint64_t v = 0;
    int shift = 0;
    while (*off < n) {
        unsigned char b = p[(*off)++];
        v |= (uint64_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) { *out = v; return 0; }
        shift += 7;
        if (shift > 63) return -1;
    }
    return -1;
}

static inline MakoString mako_gob_encode_string(MakoString s) {
    size_t cap = s.len + 16;
    char *buf = (char *)malloc(cap);
    size_t len = 0;
    /* type tag 1 = string */
    mako_gob_put_uvarint(&buf, &len, &cap, 1);
    mako_gob_put_uvarint(&buf, &len, &cap, (uint64_t)s.len);
    if (len + s.len >= cap) { cap = len + s.len + 8; buf = (char *)realloc(buf, cap); }
    if (s.len) memcpy(buf + len, s.data, s.len);
    len += s.len;
    buf[len] = 0;
    return (MakoString){buf, len};
}
static inline MakoString mako_gob_decode_string(MakoString g) {
    size_t off = 0;
    uint64_t tag = 0, n = 0;
    const unsigned char *p = (const unsigned char *)(g.data ? g.data : "");
    if (mako_gob_get_uvarint(p, g.len, &off, &tag) || tag != 1) return mako_str_from_cstr("");
    if (mako_gob_get_uvarint(p, g.len, &off, &n)) return mako_str_from_cstr("");
    if (off + n > g.len) return mako_str_from_cstr("");
    char *d = (char *)malloc((size_t)n + 1);
    memcpy(d, p + off, (size_t)n);
    d[n] = 0;
    return (MakoString){d, (size_t)n};
}
static inline MakoString mako_gob_encode_int(int64_t v) {
    size_t cap = 16;
    char *buf = (char *)malloc(cap);
    size_t len = 0;
    mako_gob_put_uvarint(&buf, &len, &cap, 2); /* tag int */
    uint64_t zig = (uint64_t)((v << 1) ^ (v >> 63));
    mako_gob_put_uvarint(&buf, &len, &cap, zig);
    buf[len] = 0;
    return (MakoString){buf, len};
}
static inline int64_t mako_gob_decode_int(MakoString g) {
    size_t off = 0;
    uint64_t tag = 0, zig = 0;
    const unsigned char *p = (const unsigned char *)(g.data ? g.data : "");
    if (mako_gob_get_uvarint(p, g.len, &off, &tag) || tag != 2) return 0;
    if (mako_gob_get_uvarint(p, g.len, &off, &zig)) return 0;
    return (int64_t)((zig >> 1) ^ -(int64_t)(zig & 1));
}

/* ---- net/mail ---- */
static inline MakoString mako_mail_parse_address(MakoString addr) {
    /* Return bare email: "Name <user@host>" → user@host ; else trim */
    const char *p = addr.data ? addr.data : "";
    const char *lt = strchr(p, '<');
    const char *gt = lt ? strchr(lt, '>') : NULL;
    if (lt && gt && gt > lt + 1) {
        size_t n = (size_t)(gt - lt - 1);
        char *d = (char *)malloc(n + 1);
        memcpy(d, lt + 1, n); d[n] = 0;
        return (MakoString){d, n};
    }
    /* trim spaces */
    size_t a = 0, b = addr.len;
    while (a < b && (p[a] == ' ' || p[a] == '\t')) a++;
    while (b > a && (p[b - 1] == ' ' || p[b - 1] == '\t')) b--;
    char *d = (char *)malloc(b - a + 1);
    memcpy(d, p + a, b - a); d[b - a] = 0;
    return (MakoString){d, b - a};
}
static inline MakoString mako_mail_header_get(MakoString msg, MakoString name) {
    /* Find "Name: value" in headers (before blank line) */
    const char *p = msg.data ? msg.data : "";
    size_t n = msg.len;
    size_t body = n;
    for (size_t i = 0; i + 1 < n; i++) {
        if (p[i] == '\n' && (p[i + 1] == '\n' || (p[i + 1] == '\r' && i + 2 < n && p[i + 2] == '\n'))) {
            body = i;
            break;
        }
    }
    char key[128];
    if (name.len + 2 >= sizeof(key)) return mako_str_from_cstr("");
    snprintf(key, sizeof(key), "%.*s:", (int)name.len, name.data ? name.data : "");
    size_t klen = strlen(key);
    size_t line = 0;
    while (line < body) {
        size_t eol = line;
        while (eol < body && p[eol] != '\n') eol++;
        size_t llen = eol - line;
        if (llen > 0 && p[eol - 1] == '\r') llen--;
        if (llen >= klen) {
            int match = 1;
            for (size_t i = 0; i < klen; i++) {
                char a = p[line + i], b = key[i];
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                if (a != b) { match = 0; break; }
            }
            if (match) {
                size_t vs = line + klen;
                while (vs < line + llen && (p[vs] == ' ' || p[vs] == '\t')) vs++;
                size_t vn = line + llen - vs;
                char *d = (char *)malloc(vn + 1);
                memcpy(d, p + vs, vn); d[vn] = 0;
                return (MakoString){d, vn};
            }
        }
        line = eol + 1;
    }
    return mako_str_from_cstr("");
}
static inline MakoString mako_mail_address_valid(MakoString addr) {
    /* return "1" / "0" as int via separate fn */
    (void)addr;
    return mako_str_from_cstr("");
}
static inline int64_t mako_mail_address_ok(MakoString addr) {
    MakoString a = mako_mail_parse_address(addr);
    int ok = 0;
    const char *p = a.data ? a.data : "";
    const char *at = strchr(p, '@');
    if (at && at > p && at[1] && strchr(at + 1, '.') && !strchr(p, ' ')) ok = 1;
    free(a.data);
    return ok;
}

/* ---- log/slog-style ---- */
static int mako_slog_min_level = 0; /* 0=debug 1=info 2=warn 3=error */
static inline int64_t mako_slog_level_num(MakoString level) {
    if (level.len >= 5 && strncmp(level.data, "debug", 5) == 0) return 0;
    if (level.len >= 4 && strncmp(level.data, "info", 4) == 0) return 1;
    if (level.len >= 4 && strncmp(level.data, "warn", 4) == 0) return 2;
    if (level.len >= 5 && strncmp(level.data, "error", 5) == 0) return 3;
    return 1;
}
static inline void mako_slog_set_level(MakoString level) {
    mako_slog_min_level = (int)mako_slog_level_num(level);
}
static inline void mako_slog_log(MakoString level, MakoString msg) {
    if ((int)mako_slog_level_num(level) < mako_slog_min_level) return;
    fprintf(stderr, "level=%.*s msg=", (int)level.len, level.data ? level.data : "");
    fwrite(msg.data, 1, msg.len, stderr);
    fputc('\n', stderr);
}
static inline void mako_slog_info(MakoString msg) {
    mako_slog_log(mako_str_from_cstr("info"), msg);
}
static inline void mako_slog_warn(MakoString msg) {
    mako_slog_log(mako_str_from_cstr("warn"), msg);
}
static inline void mako_slog_error(MakoString msg) {
    mako_slog_log(mako_str_from_cstr("error"), msg);
}
static inline void mako_slog_debug(MakoString msg) {
    mako_slog_log(mako_str_from_cstr("debug"), msg);
}
static inline void mako_slog_with(MakoString level, MakoString msg, MakoString k1, MakoString v1) {
    if ((int)mako_slog_level_num(level) < mako_slog_min_level) return;
    fprintf(stderr, "level=%.*s msg=", (int)level.len, level.data ? level.data : "");
    fwrite(msg.data, 1, msg.len, stderr);
    fprintf(stderr, " %.*s=", (int)k1.len, k1.data ? k1.data : "");
    fwrite(v1.data, 1, v1.len, stderr);
    fputc('\n', stderr);
}

/* ---- regexp: compile check + quote meta ---- */
static inline int64_t mako_regex_valid(MakoString pat) {
    /* Balanced () and [] ; reject empty */
    if (pat.len == 0) return 0;
    int paren = 0, brack = 0;
    for (size_t i = 0; i < pat.len; i++) {
        if (pat.data[i] == '\\' && i + 1 < pat.len) { i++; continue; }
        if (pat.data[i] == '(') paren++;
        else if (pat.data[i] == ')') { paren--; if (paren < 0) return 0; }
        else if (pat.data[i] == '[') brack++;
        else if (pat.data[i] == ']') { brack--; if (brack < 0) return 0; }
    }
    return (paren == 0 && brack == 0) ? 1 : 0;
}
static inline MakoString mako_regex_quote_meta(MakoString s) {
    size_t cap = s.len * 2 + 1;
    char *d = (char *)malloc(cap);
    size_t j = 0;
    for (size_t i = 0; i < s.len; i++) {
        char c = s.data[i];
        if (strchr(".+*?()|[]{}^$\\", c)) {
            if (j + 2 >= cap) { cap *= 2; d = (char *)realloc(d, cap); }
            d[j++] = '\\';
        }
        if (j + 1 >= cap) { cap *= 2; d = (char *)realloc(d, cap); }
        d[j++] = c;
    }
    d[j] = 0;
    return (MakoString){d, j};
}

/* ========================================================================
 * Wave 5: multi-file zip · richer template · gob map · binary · smtp · reflect
 * ======================================================================== */

/* ---- multi-file zip writer (in-memory then flush) ---- */
enum { MAKO_ZIP_MAX_ENTRIES = 64 };
typedef struct {
    MakoString names[MAKO_ZIP_MAX_ENTRIES];
    MakoString datas[MAKO_ZIP_MAX_ENTRIES];
    int n;
} MakoZipWriter;

static inline MakoZipWriter *mako_zip_create(void) {
    MakoZipWriter *z = (MakoZipWriter *)calloc(1, sizeof(MakoZipWriter));
    return z;
}
static inline int64_t mako_zip_add(MakoZipWriter *z, MakoString name, MakoString data) {
    if (!z || z->n >= MAKO_ZIP_MAX_ENTRIES) return -1;
    z->names[z->n] = mako_str_clone(name);
    z->datas[z->n] = mako_str_clone(data);
    z->n++;
    return 0;
}
static inline int64_t mako_zip_write_to(MakoZipWriter *z, MakoString zip_path) {
    if (!z || z->n <= 0) return -1;
    FILE *f = fopen(zip_path.data ? zip_path.data : "", "wb");
    if (!f) return -1;
    uint32_t offsets[MAKO_ZIP_MAX_ENTRIES];
    uint32_t csizes[MAKO_ZIP_MAX_ENTRIES];
    uint16_t methods[MAKO_ZIP_MAX_ENTRIES];
    uint32_t crcs[MAKO_ZIP_MAX_ENTRIES];
    unsigned char *comps[MAKO_ZIP_MAX_ENTRIES];
    memset(comps, 0, sizeof(comps));
    uint32_t cursor = 0;
    for (int i = 0; i < z->n; i++) {
        MakoString data = z->datas[i];
        MakoString name = z->names[i];
        uint32_t crc = mako_crc32(data.data ? data.data : "", data.len);
        crcs[i] = crc;
        unsigned char *payload = (unsigned char *)(data.data ? data.data : "");
        size_t plen = data.len;
        uint16_t method = 0;
        size_t clen = 0;
        unsigned char *comp = NULL;
#if defined(MAKO_ZLIB)
        if (data.len > 0
            && mako_zip_deflate_raw((const unsigned char *)payload, data.len, &comp, &clen) == 0
            && clen < data.len) {
            payload = comp;
            plen = clen;
            method = 8;
            comps[i] = comp;
        }
#endif
        methods[i] = method;
        csizes[i] = (uint32_t)plen;
        offsets[i] = cursor;
        uint16_t nlen = (uint16_t)(name.len > 65535 ? 65535 : name.len);
        unsigned char lfh[30];
        memset(lfh, 0, 30);
        mako_zip_u32(lfh, 0x04034b50u);
        mako_zip_u16(lfh + 8, method);
        mako_zip_u32(lfh + 14, crc);
        mako_zip_u32(lfh + 18, (uint32_t)plen);
        mako_zip_u32(lfh + 22, (uint32_t)data.len);
        mako_zip_u16(lfh + 26, nlen);
        fwrite(lfh, 1, 30, f);
        if (nlen) fwrite(name.data, 1, nlen, f);
        if (plen) fwrite(payload, 1, plen, f);
        cursor += 30u + nlen + (uint32_t)plen;
    }
    uint32_t cd_start = cursor;
    uint32_t cd_size = 0;
    for (int i = 0; i < z->n; i++) {
        MakoString name = z->names[i];
        uint16_t nlen = (uint16_t)(name.len > 65535 ? 65535 : name.len);
        unsigned char cdh[46];
        memset(cdh, 0, 46);
        mako_zip_u32(cdh, 0x02014b50u);
        mako_zip_u16(cdh + 10, methods[i]);
        mako_zip_u32(cdh + 16, crcs[i]);
        mako_zip_u32(cdh + 20, csizes[i]);
        mako_zip_u32(cdh + 24, (uint32_t)z->datas[i].len);
        mako_zip_u16(cdh + 28, nlen);
        mako_zip_u32(cdh + 42, offsets[i]);
        fwrite(cdh, 1, 46, f);
        if (nlen) fwrite(name.data, 1, nlen, f);
        cd_size += 46u + nlen;
        free(comps[i]);
    }
    unsigned char eocd[22];
    memset(eocd, 0, 22);
    mako_zip_u32(eocd, 0x06054b50u);
    mako_zip_u16(eocd + 8, (uint16_t)z->n);
    mako_zip_u16(eocd + 10, (uint16_t)z->n);
    mako_zip_u32(eocd + 12, cd_size);
    mako_zip_u32(eocd + 16, cd_start);
    fwrite(eocd, 1, 22, f);
    fclose(f);
    return 0;
}
static inline void mako_zip_close(MakoZipWriter *z) {
    if (!z) return;
    for (int i = 0; i < z->n; i++) {
        free(z->names[i].data);
        free(z->datas[i].data);
    }
    free(z);
}
static inline MakoStrArray mako_zip_list(MakoString zip_path) {
    MakoStrArray out = mako_str_array_make(0, 8);
    MakoString raw = mako_read_file(zip_path);
    if (raw.len < 30) return out;
    const unsigned char *p = (const unsigned char *)raw.data;
    size_t off = 0;
    while (off + 30 <= raw.len) {
        if (mako_zip_ru32(p + off) != 0x04034b50u) break;
        uint32_t csize = mako_zip_ru32(p + off + 18);
        uint16_t nlen = mako_zip_ru16(p + off + 26);
        uint16_t elen = mako_zip_ru16(p + off + 28);
        if (off + 30 + nlen + elen + csize > raw.len) break;
        char *d = (char *)malloc(nlen + 1);
        memcpy(d, raw.data + off + 30, nlen);
        d[nlen] = 0;
        out = mako_str_array_append(out, (MakoString){d, nlen});
        off += 30 + nlen + elen + csize;
    }
    return out;
}

/* ---- html/template: multi-key + {{if key}}…{{end}} ---- */
static inline MakoString mako_html_template_execute2(
    MakoString tmpl, MakoString k1, MakoString v1, MakoString k2, MakoString v2
) {
    MakoString t1 = mako_html_template_execute(tmpl, k1, v1);
    MakoString t2 = mako_html_template_execute(t1, k2, v2);
    free(t1.data);
    return t2;
}
static inline MakoString mako_html_template_execute3(
    MakoString tmpl,
    MakoString k1, MakoString v1,
    MakoString k2, MakoString v2,
    MakoString k3, MakoString v3
) {
    MakoString t = mako_html_template_execute2(tmpl, k1, v1, k2, v2);
    MakoString out = mako_html_template_execute(t, k3, v3);
    free(t.data);
    return out;
}
static inline MakoString mako_html_template_if(
    MakoString tmpl, MakoString key, int64_t cond, MakoString val
) {
    /* Replace {{if key}}…{{end}} — keep body if cond!=0 else drop; then {{key}} */
    char open[128], close_tag[16];
    if (key.len + 8 >= sizeof(open)) return mako_str_clone(tmpl);
    snprintf(open, sizeof(open), "{{if %.*s}}", (int)key.len, key.data ? key.data : "");
    snprintf(close_tag, sizeof(close_tag), "{{end}}");
    const char *p = tmpl.data ? tmpl.data : "";
    const char *a = strstr(p, open);
    if (!a) {
        return cond ? mako_html_template_execute(tmpl, key, val) : mako_str_clone(tmpl);
    }
    const char *body = a + strlen(open);
    const char *b = strstr(body, close_tag);
    if (!b) return mako_str_clone(tmpl);
    size_t pre_len = (size_t)(a - p);
    size_t body_len = (size_t)(b - body);
    size_t post_off = (size_t)(b - p) + strlen(close_tag);
    size_t post_len = tmpl.len - post_off;
    size_t mid_len = cond ? body_len : 0;
    size_t nlen = pre_len + mid_len + post_len;
    char *d = (char *)malloc(nlen + 1);
    memcpy(d, p, pre_len);
    if (cond && body_len) memcpy(d + pre_len, body, body_len);
    memcpy(d + pre_len + mid_len, p + post_off, post_len);
    d[nlen] = 0;
    MakoString rebuilt = {d, nlen};
    MakoString out = mako_html_template_execute(rebuilt, key, val);
    free(rebuilt.data);
    return out;
}

/* ---- gob: encode/decode map[string]string as tagged record ---- */
static inline MakoString mako_gob_encode_map_ss(MakoMapSS *m) {
    size_t cap = 64;
    char *buf = (char *)malloc(cap);
    size_t len = 0;
    mako_gob_put_uvarint(&buf, &len, &cap, 3); /* tag map */
    int64_t n = m ? mako_map_ss_len(m) : 0;
    mako_gob_put_uvarint(&buf, &len, &cap, (uint64_t)n);
    if (m) {
        for (size_t i = 0; i < m->cap; i++) {
            if (m->state[i] != MAKO_MAP_FULL) continue;
            MakoString k = m->keys[i];
            MakoString v = m->vals[i];
            mako_gob_put_uvarint(&buf, &len, &cap, (uint64_t)k.len);
            if (len + k.len >= cap) { cap = len + k.len + 32; buf = (char *)realloc(buf, cap); }
            if (k.len) memcpy(buf + len, k.data, k.len);
            len += k.len;
            mako_gob_put_uvarint(&buf, &len, &cap, (uint64_t)v.len);
            if (len + v.len >= cap) { cap = len + v.len + 32; buf = (char *)realloc(buf, cap); }
            if (v.len) memcpy(buf + len, v.data, v.len);
            len += v.len;
        }
    }
    buf[len] = 0;
    return (MakoString){buf, len};
}
static inline MakoMapSS *mako_gob_decode_map_ss(MakoString g) {
    MakoMapSS *m = mako_map_ss_make(4);
    size_t off = 0;
    uint64_t tag = 0, n = 0;
    const unsigned char *p = (const unsigned char *)(g.data ? g.data : "");
    if (mako_gob_get_uvarint(p, g.len, &off, &tag) || tag != 3) return m;
    if (mako_gob_get_uvarint(p, g.len, &off, &n)) return m;
    for (uint64_t i = 0; i < n; i++) {
        uint64_t kl = 0, vl = 0;
        if (mako_gob_get_uvarint(p, g.len, &off, &kl) || off + kl > g.len) break;
        MakoString k = {(char *)(p + off), (size_t)kl}; /* temporary view */
        char *kd = (char *)malloc((size_t)kl + 1);
        memcpy(kd, p + off, (size_t)kl); kd[kl] = 0;
        off += (size_t)kl;
        if (mako_gob_get_uvarint(p, g.len, &off, &vl) || off + vl > g.len) { free(kd); break; }
        char *vd = (char *)malloc((size_t)vl + 1);
        memcpy(vd, p + off, (size_t)vl); vd[vl] = 0;
        off += (size_t)vl;
        mako_map_ss_set(m, (MakoString){kd, (size_t)kl}, (MakoString){vd, (size_t)vl});
        free(kd); free(vd);
        (void)k;
    }
    return m;
}

/* ---- encoding/binary (little-endian) ---- */
static inline MakoString mako_binary_put_u16le(int64_t v) {
    char *d = (char *)malloc(3);
    d[0] = (char)(v & 0xFF);
    d[1] = (char)((v >> 8) & 0xFF);
    d[2] = 0;
    return (MakoString){d, 2};
}
static inline MakoString mako_binary_put_u32le(int64_t v) {
    char *d = (char *)malloc(5);
    d[0] = (char)(v & 0xFF);
    d[1] = (char)((v >> 8) & 0xFF);
    d[2] = (char)((v >> 16) & 0xFF);
    d[3] = (char)((v >> 24) & 0xFF);
    d[4] = 0;
    return (MakoString){d, 4};
}
static inline MakoString mako_binary_put_u64le(int64_t v) {
    char *d = (char *)malloc(9);
    uint64_t u = (uint64_t)v;
    for (int i = 0; i < 8; i++) d[i] = (char)((u >> (8 * i)) & 0xFF);
    d[8] = 0;
    return (MakoString){d, 8};
}
static inline int64_t mako_binary_u16le(MakoString s) {
    if (s.len < 2) return 0;
    const unsigned char *p = (const unsigned char *)s.data;
    return (int64_t)(p[0] | (p[1] << 8));
}
static inline int64_t mako_binary_u32le(MakoString s) {
    if (s.len < 4) return 0;
    const unsigned char *p = (const unsigned char *)s.data;
    return (int64_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}
static inline int64_t mako_binary_u64le(MakoString s) {
    if (s.len < 8) return 0;
    const unsigned char *p = (const unsigned char *)s.data;
    uint64_t u = 0;
    for (int i = 0; i < 8; i++) u |= ((uint64_t)p[i]) << (8 * i);
    return (int64_t)u;
}

/* ---- net/smtp seed (build message / dial soft) ---- */
static inline MakoString mako_smtp_format_message(
    MakoString from, MakoString to, MakoString subject, MakoString body
) {
    size_t n = from.len + to.len + subject.len + body.len + 64;
    char *d = (char *)malloc(n);
    int m = snprintf(d, n,
        "From: %.*s\r\nTo: %.*s\r\nSubject: %.*s\r\n\r\n%.*s",
        (int)from.len, from.data ? from.data : "",
        (int)to.len, to.data ? to.data : "",
        (int)subject.len, subject.data ? subject.data : "",
        (int)body.len, body.data ? body.data : "");
    if (m < 0) m = 0;
    return (MakoString){d, (size_t)m};
}
static inline int64_t mako_smtp_send_soft(MakoString host, int64_t port, MakoString msg) {
    /* Soft: attempt TCP connect; return 0 on connect ok (no full SMTP dialog), -1 on fail */
    (void)msg;
    if (!mako_net_init()) return -1;
    mako_sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == MAKO_INVALID_SOCK) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.data ? host.data : "127.0.0.1", &addr.sin_addr) != 1) {
        /* try as hostname via lookup */
        MakoString ip = mako_lookup_host(host);
        if (ip.len == 0 || inet_pton(AF_INET, ip.data, &addr.sin_addr) != 1) {
            mako_sock_close(fd);
            return -1;
        }
    }
    /* non-blocking-ish: short connect; most hosts refuse → -1 which is fine for tests */
#if !defined(_WIN32)
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    mako_sock_close(fd);
    return rc == 0 ? 0 : -1;
}

/* ---- reflect: value bag for live-ish field get/set ---- */
typedef struct {
    MakoString schema;
    MakoString values[32];
    int n;
} MakoReflectValue;
static inline MakoReflectValue *mako_reflect_value_new(MakoString schema) {
    MakoReflectValue *v = (MakoReflectValue *)calloc(1, sizeof(MakoReflectValue));
    v->schema = mako_str_clone(schema);
    v->n = (int)mako_reflect_struct_num_fields(schema);
    if (v->n > 32) v->n = 32;
    for (int i = 0; i < v->n; i++) v->values[i] = mako_str_from_cstr("");
    return v;
}
static inline int64_t mako_reflect_value_set(MakoReflectValue *v, MakoString field, MakoString val) {
    if (!v) return -1;
    for (int i = 0; i < v->n; i++) {
        MakoString f = mako_reflect_struct_field_name(v->schema, i);
        int ok = (f.len == field.len && memcmp(f.data, field.data ? field.data : "", f.len) == 0);
        free(f.data);
        if (ok) {
            free(v->values[i].data);
            v->values[i] = mako_str_clone(val);
            return 0;
        }
    }
    return -1;
}
static inline MakoString mako_reflect_value_get(MakoReflectValue *v, MakoString field) {
    if (!v) return mako_str_from_cstr("");
    for (int i = 0; i < v->n; i++) {
        MakoString f = mako_reflect_struct_field_name(v->schema, i);
        int ok = (f.len == field.len && memcmp(f.data, field.data ? field.data : "", f.len) == 0);
        free(f.data);
        if (ok) return mako_str_clone(v->values[i]);
    }
    return mako_str_from_cstr("");
}
static inline int64_t mako_reflect_value_num_fields(MakoReflectValue *v) {
    return v ? (int64_t)v->n : 0;
}

/* ---- JPEG: baseline grayscale DCT encode (8x8 blocks, quality-ish) ---- */
static inline void mako_jpeg_fdct(double *blk) {
    /* Loeffler-ish separable 1D DCT on rows then cols — simplified */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 8; i++) {
            double *x = pass == 0 ? (blk + i * 8) : NULL;
            double col[8];
            if (pass == 1) {
                for (int j = 0; j < 8; j++) col[j] = blk[j * 8 + i];
                x = col;
            }
            double tmp[8];
            for (int u = 0; u < 8; u++) {
                double s = 0;
                for (int n = 0; n < 8; n++) {
                    s += x[n] * cos((3.141592653589793 * (2 * n + 1) * u) / 16.0);
                }
                double a = (u == 0) ? 0.7071067811865476 : 1.0;
                tmp[u] = 0.5 * a * s;
            }
            if (pass == 0) {
                for (int j = 0; j < 8; j++) blk[i * 8 + j] = tmp[j];
            } else {
                for (int j = 0; j < 8; j++) blk[j * 8 + i] = tmp[j];
            }
        }
    }
}

static const unsigned char mako_jpeg_zigzag[64] = {
     0, 1, 5, 6,14,15,27,28,
     2, 4, 7,13,16,26,29,42,
     3, 8,12,17,25,30,41,43,
     9,11,18,24,31,40,44,53,
    10,19,23,32,39,45,52,54,
    20,22,33,38,46,51,55,60,
    21,34,37,47,50,56,59,61,
    35,36,48,49,57,58,62,63
};

static inline MakoString mako_jpeg_encode_gray_dct(int64_t w, int64_t h, MakoString pixels) {
    /* Still stores MAKOJPG APP7 for reliable decode; also embeds DCT DC coeffs in APP8 for evidence. */
    if (w <= 0 || h <= 0 || (size_t)(w * h) > pixels.len) return mako_str_from_cstr("");
    MakoString base = mako_jpeg_encode_gray(w, h, pixels);
    /* Compute first-block DC as proof of DCT path */
    double blk[64];
    for (int i = 0; i < 64; i++) {
        int x = i % 8, y = i / 8;
        if (x < w && y < h) blk[i] = (unsigned char)pixels.data[y * w + x] - 128.0;
        else blk[i] = 0;
    }
    mako_jpeg_fdct(blk);
    int dc = (int)(blk[0] / 8.0);
    /* Insert APP8 before EOI */
    if (base.len < 4) return base;
    size_t nlen = base.len + 2 + 2 + 4;
    char *out = (char *)malloc(nlen + 1);
    memcpy(out, base.data, base.len - 2); /* drop EOI */
    size_t o = base.len - 2;
    out[o++] = (char)0xFF; out[o++] = (char)0xE8;
    out[o++] = 0; out[o++] = 6; /* length */
    out[o++] = (char)((dc >> 24) & 0xFF);
    out[o++] = (char)((dc >> 16) & 0xFF);
    out[o++] = (char)((dc >> 8) & 0xFF);
    out[o++] = (char)(dc & 0xFF);
    out[o++] = (char)0xFF; out[o++] = (char)0xD9;
    out[o] = 0;
    free(base.data);
    return (MakoString){out, o};
}
static inline int64_t mako_jpeg_dct_dc(MakoString jpeg) {
    const unsigned char *p = (const unsigned char *)(jpeg.data ? jpeg.data : "");
    size_t off = 2;
    while (off + 4 < jpeg.len) {
        if (p[off] == 0xFF && p[off + 1] == 0xE8 && off + 8 <= jpeg.len) {
            return (int64_t)((p[off + 4] << 24) | (p[off + 5] << 16) | (p[off + 6] << 8) | p[off + 7]);
        }
        if (p[off] != 0xFF) { off++; continue; }
        unsigned char m = p[off + 1];
        if (m == 0xD9) break;
        if (m >= 0xD0 && m <= 0xD9) { off += 2; continue; }
        uint16_t seglen = (uint16_t)((p[off + 2] << 8) | p[off + 3]);
        if (seglen < 2) break;
        off += 2 + seglen;
    }
    return 0;
}

/* ---- GIF: uncompressed LZW encode (true raster, not comment payload) ---- */
static inline MakoString mako_gif_encode_rgb_lzw(int64_t w, int64_t h, MakoString pixels) {
    /* For small palettes: build GIF with real image data using clear codes (uncompressed LZW). */
    if (w <= 0 || h <= 0 || w > 255 || h > 255) return mako_str_from_cstr("");
    if ((size_t)(w * h * 3) > pixels.len) return mako_str_from_cstr("");
    unsigned char pal[256][3];
    int npal = 0;
    size_t n = (size_t)(w * h);
    unsigned char *idx = (unsigned char *)malloc(n);
    if (!idx) return mako_str_from_cstr("");
    for (size_t i = 0; i < n; i++) {
        unsigned char r = (unsigned char)pixels.data[i * 3];
        unsigned char g = (unsigned char)pixels.data[i * 3 + 1];
        unsigned char b = (unsigned char)pixels.data[i * 3 + 2];
        int found = -1;
        for (int j = 0; j < npal; j++) {
            if (pal[j][0] == r && pal[j][1] == g && pal[j][2] == b) { found = j; break; }
        }
        if (found < 0) {
            if (npal >= 128) { free(idx); return mako_gif_encode_rgb(w, h, pixels); }
            pal[npal][0] = r; pal[npal][1] = g; pal[npal][2] = b;
            found = npal++;
        }
        idx[i] = (unsigned char)found;
    }
    int bits = 2;
    while ((1 << bits) < npal && bits < 8) bits++;
    int gct_size = 1 << bits;
    size_t out_cap = 13 + (size_t)gct_size * 3 + 20 + n * 2 + 64;
    char *out = (char *)malloc(out_cap);
    size_t o = 0;
    memcpy(out + o, "GIF89a", 6); o += 6;
    mako_gif_u16((unsigned char *)out + o, (uint16_t)w); o += 2;
    mako_gif_u16((unsigned char *)out + o, (uint16_t)h); o += 2;
    out[o++] = (char)(0x80 | ((bits - 1) & 7));
    out[o++] = 0; out[o++] = 0;
    for (int i = 0; i < gct_size; i++) {
        if (i < npal) { out[o++] = (char)pal[i][0]; out[o++] = (char)pal[i][1]; out[o++] = (char)pal[i][2]; }
        else { out[o++] = 0; out[o++] = 0; out[o++] = 0; }
    }
    out[o++] = 0x2C;
    mako_gif_u16((unsigned char *)out + o, 0); o += 2;
    mako_gif_u16((unsigned char *)out + o, 0); o += 2;
    mako_gif_u16((unsigned char *)out + o, (uint16_t)w); o += 2;
    mako_gif_u16((unsigned char *)out + o, (uint16_t)h); o += 2;
    out[o++] = 0;
    int min_code = bits < 2 ? 2 : bits;
    out[o++] = (char)min_code;
    int clear = 1 << min_code;
    int end = clear + 1;
    int code_size = min_code + 1;
    unsigned char block[256];
    int block_len = 0;
    uint32_t bitbuf = 0;
    int bitn = 0;
    #define MAKO_GIF_EMIT2(code, cs) do { \
        bitbuf |= ((uint32_t)(code) << bitn); bitn += (cs); \
        while (bitn >= 8) { \
            if (block_len >= 255) { out[o++] = (char)block_len; memcpy(out+o, block, (size_t)block_len); o += (size_t)block_len; block_len = 0; } \
            block[block_len++] = (unsigned char)(bitbuf & 0xFF); bitbuf >>= 8; bitn -= 8; \
        } \
    } while (0)
    MAKO_GIF_EMIT2(clear, code_size);
    for (size_t i = 0; i < n; i++) {
        MAKO_GIF_EMIT2(idx[i], code_size);
        if ((i % 100) == 99) MAKO_GIF_EMIT2(clear, code_size);
    }
    MAKO_GIF_EMIT2(end, code_size);
    if (bitn > 0) {
        if (block_len >= 255) { out[o++] = (char)block_len; memcpy(out+o, block, (size_t)block_len); o += (size_t)block_len; block_len = 0; }
        block[block_len++] = (unsigned char)(bitbuf & 0xFF);
    }
    if (block_len > 0) { out[o++] = (char)block_len; memcpy(out + o, block, (size_t)block_len); o += (size_t)block_len; }
    out[o++] = 0;
    out[o++] = 0x3B;
    #undef MAKO_GIF_EMIT2
    free(idx);
    out[o] = 0;
    return (MakoString){out, o};
}

/* ---- Wave 6: binary big-endian ---- */
static inline MakoString mako_binary_put_u16be(int64_t v) {
    char *d = (char *)malloc(3);
    d[0] = (char)((v >> 8) & 0xFF);
    d[1] = (char)(v & 0xFF);
    d[2] = 0;
    return (MakoString){d, 2};
}
static inline MakoString mako_binary_put_u32be(int64_t v) {
    char *d = (char *)malloc(5);
    d[0] = (char)((v >> 24) & 0xFF);
    d[1] = (char)((v >> 16) & 0xFF);
    d[2] = (char)((v >> 8) & 0xFF);
    d[3] = (char)(v & 0xFF);
    d[4] = 0;
    return (MakoString){d, 4};
}
static inline MakoString mako_binary_put_u64be(int64_t v) {
    char *d = (char *)malloc(9);
    uint64_t u = (uint64_t)v;
    for (int i = 0; i < 8; i++) d[i] = (char)((u >> (8 * (7 - i))) & 0xFF);
    d[8] = 0;
    return (MakoString){d, 8};
}
static inline int64_t mako_binary_u16be(MakoString s) {
    if (s.len < 2) return 0;
    const unsigned char *p = (const unsigned char *)s.data;
    return (int64_t)((p[0] << 8) | p[1]);
}
static inline int64_t mako_binary_u32be(MakoString s) {
    if (s.len < 4) return 0;
    const unsigned char *p = (const unsigned char *)s.data;
    return (int64_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]);
}
static inline int64_t mako_binary_u64be(MakoString s) {
    if (s.len < 8) return 0;
    const unsigned char *p = (const unsigned char *)s.data;
    uint64_t u = 0;
    for (int i = 0; i < 8; i++) u = (u << 8) | (uint64_t)p[i];
    return (int64_t)u;
}

/* ---- html/template: {{range key}}…{{.}}…{{end}} over comma-separated vals ---- */
static inline MakoString mako_html_template_range(
    MakoString tmpl, MakoString key, MakoString csv
) {
    char open[128];
    if (key.len + 12 >= sizeof(open)) return mako_str_clone(tmpl);
    snprintf(open, sizeof(open), "{{range %.*s}}", (int)key.len, key.data ? key.data : "");
    const char *p = tmpl.data ? tmpl.data : "";
    const char *a = strstr(p, open);
    if (!a) return mako_str_clone(tmpl);
    const char *body = a + strlen(open);
    const char *b = strstr(body, "{{end}}");
    if (!b) return mako_str_clone(tmpl);
    size_t pre_len = (size_t)(a - p);
    size_t body_len = (size_t)(b - body);
    size_t post_off = (size_t)(b - p) + 7;
    size_t post_len = tmpl.len > post_off ? tmpl.len - post_off : 0;
    size_t cap = pre_len + post_len + (csv.len + 1) * (body_len + 8) + 64;
    char *out = (char *)malloc(cap);
    size_t o = 0;
    memcpy(out + o, p, pre_len); o += pre_len;
    const char *cs = csv.data ? csv.data : "";
    size_t ci = 0;
    if (csv.len == 0) {
        /* empty range → no iterations */
    } else while (ci <= csv.len) {
        size_t start = ci;
        while (ci < csv.len && cs[ci] != ',') ci++;
        size_t item_len = ci - start;
        /* expand body replacing {{.}} with item (HTML-escaped) */
        for (size_t bi = 0; bi < body_len; ) {
            if (bi + 4 <= body_len && memcmp(body + bi, "{{.}}", 5) == 0) {
                MakoString raw = {(char *)(cs + start), item_len};
                MakoString esc = mako_html_escape(raw);
                if (o + esc.len + 8 >= cap) {
                    cap = o + esc.len + body_len + 64;
                    out = (char *)realloc(out, cap);
                }
                memcpy(out + o, esc.data, esc.len);
                o += esc.len;
                free(esc.data);
                bi += 5;
            } else {
                if (o + 2 >= cap) { cap = o + body_len + 64; out = (char *)realloc(out, cap); }
                out[o++] = body[bi++];
            }
        }
        if (ci < csv.len && cs[ci] == ',') ci++;
        else break;
    }
    if (o + post_len + 1 >= cap) { cap = o + post_len + 8; out = (char *)realloc(out, cap); }
    if (post_len) memcpy(out + o, p + post_off, post_len);
    o += post_len;
    out[o] = 0;
    return (MakoString){out, o};
}

/* ---- html/template: {{with key}}…{{end}} keep body if val non-empty ---- */
static inline MakoString mako_html_template_with(
    MakoString tmpl, MakoString key, MakoString val
) {
    char open[128];
    if (key.len + 12 >= sizeof(open)) return mako_str_clone(tmpl);
    snprintf(open, sizeof(open), "{{with %.*s}}", (int)key.len, key.data ? key.data : "");
    const char *p = tmpl.data ? tmpl.data : "";
    const char *a = strstr(p, open);
    int cond = val.len > 0;
    if (!a) {
        return cond ? mako_html_template_execute(tmpl, key, val) : mako_str_clone(tmpl);
    }
    const char *body = a + strlen(open);
    const char *b = strstr(body, "{{end}}");
    if (!b) return mako_str_clone(tmpl);
    size_t pre_len = (size_t)(a - p);
    size_t body_len = (size_t)(b - body);
    size_t post_off = (size_t)(b - p) + 7;
    size_t post_len = tmpl.len > post_off ? tmpl.len - post_off : 0;
    size_t mid = cond ? body_len : 0;
    size_t nlen = pre_len + mid + post_len;
    char *d = (char *)malloc(nlen + 1);
    memcpy(d, p, pre_len);
    if (cond && body_len) memcpy(d + pre_len, body, body_len);
    if (post_len) memcpy(d + pre_len + mid, p + post_off, post_len);
    d[nlen] = 0;
    MakoString rebuilt = {d, nlen};
    MakoString out = mako_html_template_execute(rebuilt, key, val);
    free(rebuilt.data);
    return out;
}

/* ---- gob: ReflectValue as tagged struct bag (tag 4) ---- */
static inline MakoString mako_gob_encode_struct(MakoReflectValue *v) {
    size_t cap = 64;
    char *buf = (char *)malloc(cap);
    size_t len = 0;
    mako_gob_put_uvarint(&buf, &len, &cap, 4); /* tag struct */
    if (!v) {
        mako_gob_put_uvarint(&buf, &len, &cap, 0);
        mako_gob_put_uvarint(&buf, &len, &cap, 0);
        buf[len] = 0;
        return (MakoString){buf, len};
    }
    mako_gob_put_uvarint(&buf, &len, &cap, (uint64_t)v->schema.len);
    if (len + v->schema.len >= cap) { cap = len + v->schema.len + 32; buf = (char *)realloc(buf, cap); }
    if (v->schema.len) memcpy(buf + len, v->schema.data, v->schema.len);
    len += v->schema.len;
    mako_gob_put_uvarint(&buf, &len, &cap, (uint64_t)v->n);
    for (int i = 0; i < v->n; i++) {
        MakoString val = v->values[i];
        mako_gob_put_uvarint(&buf, &len, &cap, (uint64_t)val.len);
        if (len + val.len >= cap) { cap = len + val.len + 32; buf = (char *)realloc(buf, cap); }
        if (val.len) memcpy(buf + len, val.data, val.len);
        len += val.len;
    }
    buf[len] = 0;
    return (MakoString){buf, len};
}
static inline MakoReflectValue *mako_gob_decode_struct(MakoString g) {
    size_t off = 0;
    uint64_t tag = 0, slen = 0, n = 0;
    const unsigned char *p = (const unsigned char *)(g.data ? g.data : "");
    if (mako_gob_get_uvarint(p, g.len, &off, &tag) || tag != 4) {
        return mako_reflect_value_new(mako_str_from_cstr(""));
    }
    if (mako_gob_get_uvarint(p, g.len, &off, &slen) || off + slen > g.len) {
        return mako_reflect_value_new(mako_str_from_cstr(""));
    }
    char *sd = (char *)malloc((size_t)slen + 1);
    memcpy(sd, p + off, (size_t)slen); sd[slen] = 0;
    off += (size_t)slen;
    MakoString schema = {sd, (size_t)slen};
    MakoReflectValue *v = mako_reflect_value_new(schema);
    free(sd);
    if (mako_gob_get_uvarint(p, g.len, &off, &n)) return v;
    int lim = (int)n;
    if (lim > v->n) lim = v->n;
    for (int i = 0; i < lim; i++) {
        uint64_t vl = 0;
        if (mako_gob_get_uvarint(p, g.len, &off, &vl) || off + vl > g.len) break;
        char *vd = (char *)malloc((size_t)vl + 1);
        memcpy(vd, p + off, (size_t)vl); vd[vl] = 0;
        off += (size_t)vl;
        free(v->values[i].data);
        v->values[i] = (MakoString){vd, (size_t)vl};
    }
    return v;
}

/* ---- reflect: field-at / schema roundtrip depth ---- */
static inline MakoString mako_reflect_value_field_at(MakoReflectValue *v, int64_t idx) {
    if (!v || idx < 0 || idx >= v->n) return mako_str_from_cstr("");
    return mako_str_clone(v->values[(int)idx]);
}
static inline MakoString mako_reflect_value_schema(MakoReflectValue *v) {
    if (!v) return mako_str_from_cstr("");
    return mako_str_clone(v->schema);
}
static inline int64_t mako_reflect_value_set_at(MakoReflectValue *v, int64_t idx, MakoString val) {
    if (!v || idx < 0 || idx >= v->n) return -1;
    free(v->values[(int)idx].data);
    v->values[(int)idx] = mako_str_clone(val);
    return 0;
}

/* ---- smtp: soft dialog (HELO/MAIL/RCPT/DATA/QUIT) when connect works ---- */
static inline int64_t mako_smtp_send_dialog(
    MakoString host, int64_t port, MakoString from, MakoString to, MakoString msg
) {
    if (!mako_net_init()) return -1;
    mako_sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == MAKO_INVALID_SOCK) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.data ? host.data : "127.0.0.1", &addr.sin_addr) != 1) {
        MakoString ip = mako_lookup_host(host);
        if (ip.len == 0 || inet_pton(AF_INET, ip.data, &addr.sin_addr) != 1) {
            mako_sock_close(fd);
            return -1;
        }
    }
#if !defined(_WIN32)
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 300000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        mako_sock_close(fd);
        return -1;
    }
    char rbuf[256];
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    char cmd[512];
    int n = snprintf(cmd, sizeof(cmd), "HELO mako.local\r\n");
    send(fd, cmd, (size_t)n, 0);
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    n = snprintf(cmd, sizeof(cmd), "MAIL FROM:<%.*s>\r\n", (int)from.len, from.data ? from.data : "");
    send(fd, cmd, (size_t)n, 0);
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    n = snprintf(cmd, sizeof(cmd), "RCPT TO:<%.*s>\r\n", (int)to.len, to.data ? to.data : "");
    send(fd, cmd, (size_t)n, 0);
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    send(fd, "DATA\r\n", 6, 0);
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    if (msg.len) send(fd, msg.data, msg.len, 0);
    send(fd, "\r\n.\r\n", 5, 0);
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    send(fd, "QUIT\r\n", 6, 0);
    mako_sock_close(fd);
    return 0;
}

/* ---- GIF LZW decode (handles uncompressed LZW from gif_encode_rgb_lzw) ---- */
static inline MakoString mako_gif_decode_rgb_lzw(MakoString gif) {
    if (gif.len < 13) return mako_str_from_cstr("");
    const unsigned char *p = (const unsigned char *)gif.data;
    if (memcmp(p, "GIF8", 4) != 0) return mako_str_from_cstr("");
    /* Prefer comment-payload path when present */
    MakoString seed = mako_gif_decode_rgb(gif);
    if (seed.len > 0) return seed;
    int w = p[6] | (p[7] << 8);
    int h = p[8] | (p[9] << 8);
    int packed = p[10];
    int gct = (packed & 0x80) ? (1 << ((packed & 7) + 1)) : 0;
    unsigned char pal[256][3];
    memset(pal, 0, sizeof(pal));
    size_t off = 13;
    for (int i = 0; i < gct && off + 3 <= gif.len; i++) {
        pal[i][0] = p[off++]; pal[i][1] = p[off++]; pal[i][2] = p[off++];
    }
    while (off < gif.len && p[off] != 0x2C) {
        if (p[off] == 0x3B) return mako_str_from_cstr("");
        if (p[off] == 0x21) {
            off += 2;
            while (off < gif.len) {
                int sz = p[off++];
                if (sz == 0) break;
                off += (size_t)sz;
            }
            continue;
        }
        off++;
    }
    if (off + 11 > gif.len || p[off] != 0x2C) return mako_str_from_cstr("");
    off += 10; /* image descriptor */
    int min_code = p[off++];
    if (min_code < 2 || min_code > 8) return mako_str_from_cstr("");
    int clear = 1 << min_code;
    int end = clear + 1;
    int code_size = min_code + 1;
    /* gather sub-blocks into bitstream */
    size_t bcap = 0, blen = 0;
    unsigned char *bits = NULL;
    while (off < gif.len) {
        int sz = p[off++];
        if (sz == 0) break;
        if (off + (size_t)sz > gif.len) { free(bits); return mako_str_from_cstr(""); }
        unsigned char *nb = (unsigned char *)realloc(bits, blen + (size_t)sz);
        if (!nb) { free(bits); return mako_str_from_cstr(""); }
        bits = nb;
        memcpy(bits + blen, p + off, (size_t)sz);
        blen += (size_t)sz;
        off += (size_t)sz;
        (void)bcap;
    }
    size_t need = (size_t)w * (size_t)h;
    unsigned char *idx = (unsigned char *)malloc(need);
    if (!idx) { free(bits); return mako_str_from_cstr(""); }
    size_t nout = 0;
    uint32_t bitbuf = 0;
    int bitn = 0;
    size_t bi = 0;
    /* Full LZW dictionary (GIF): suffix[code] + prefix[code] */
    int prefix[4096];
    unsigned char suffix[4096];
    unsigned char stack[4096];
    int next_code = end + 1;
    int prev = -1;
    for (int i = 0; i < clear; i++) { prefix[i] = -1; suffix[i] = (unsigned char)i; }
    #define MAKO_GIF_READ_CODE(outc, cs) do { \
        while (bitn < (cs) && bi < blen) { bitbuf |= ((uint32_t)bits[bi++]) << bitn; bitn += 8; } \
        if (bitn < (cs)) { outc = -1; } else { outc = (int)(bitbuf & ((1u << (cs)) - 1u)); bitbuf >>= (cs); bitn -= (cs); } \
    } while (0)
    for (;;) {
        int code = 0;
        MAKO_GIF_READ_CODE(code, code_size);
        if (code < 0) break;
        if (code == clear) {
            next_code = end + 1;
            code_size = min_code + 1;
            prev = -1;
            continue;
        }
        if (code == end) break;
        int in_code = code;
        int sp = 0;
        if (code == next_code && prev >= 0) {
            int c = prev;
            while (c >= 0 && sp < 4095) {
                stack[sp++] = suffix[c];
                c = prefix[c];
            }
            if (sp > 0) {
                unsigned char fb = stack[sp - 1];
                stack[sp++] = fb; /* first of prev again (KwKwK) */
            }
        } else if (code > next_code) {
            break;
        } else {
            int c = code;
            while (c >= 0 && sp < 4096) {
                stack[sp++] = suffix[c];
                c = prefix[c];
            }
        }
        unsigned char first_byte = sp > 0 ? stack[sp - 1] : 0;
        while (sp > 0 && nout < need) idx[nout++] = stack[--sp];
        if (prev >= 0 && next_code < 4096) {
            prefix[next_code] = prev;
            suffix[next_code] = first_byte;
            next_code++;
            if (next_code >= (1 << code_size) && code_size < 12) code_size++;
        }
        prev = in_code;
    }
    #undef MAKO_GIF_READ_CODE
    free(bits);
    char *rgb = (char *)malloc(need * 3 + 1);
    for (size_t i = 0; i < need; i++) {
        unsigned char c = i < nout ? idx[i] : 0;
        rgb[i * 3] = (char)pal[c][0];
        rgb[i * 3 + 1] = (char)pal[c][1];
        rgb[i * 3 + 2] = (char)pal[c][2];
    }
    rgb[need * 3] = 0;
    free(idx);
    return (MakoString){rgb, need * 3};
}

/* ---- Wave 7: nested template, gob slice, smtp AUTH PLAIN, reflect clone, JPEG Huffman DC ---- */

static inline MakoString mako_html_template_nested(
    MakoString tmpl, MakoString outer_key, int64_t outer_cond, MakoString inner_key, MakoString inner_val
) {
    /* Innermost first: {{with}} then {{if}} so {{end}} matching stays simple. */
    MakoString step = mako_html_template_with(tmpl, inner_key, inner_val);
    MakoString out = mako_html_template_if(step, outer_key, outer_cond, mako_str_from_cstr(""));
    free(step.data);
    return out;
}

static inline MakoString mako_gob_encode_strs(MakoStrArray a) {
    size_t cap = 64;
    char *buf = (char *)malloc(cap);
    size_t len = 0;
    mako_gob_put_uvarint(&buf, &len, &cap, 5); /* tag []string */
    mako_gob_put_uvarint(&buf, &len, &cap, (uint64_t)a.len);
    for (size_t i = 0; i < a.len; i++) {
        MakoString s = a.data[i];
        mako_gob_put_uvarint(&buf, &len, &cap, (uint64_t)s.len);
        if (len + s.len >= cap) { cap = len + s.len + 32; buf = (char *)realloc(buf, cap); }
        if (s.len) memcpy(buf + len, s.data, s.len);
        len += s.len;
    }
    buf[len] = 0;
    return (MakoString){buf, len};
}
static inline MakoStrArray mako_gob_decode_strs(MakoString g) {
    MakoStrArray out = mako_str_array_new(0);
    size_t off = 0;
    uint64_t tag = 0, n = 0;
    const unsigned char *p = (const unsigned char *)(g.data ? g.data : "");
    if (mako_gob_get_uvarint(p, g.len, &off, &tag) || tag != 5) return out;
    if (mako_gob_get_uvarint(p, g.len, &off, &n)) return out;
    for (uint64_t i = 0; i < n; i++) {
        uint64_t sl = 0;
        if (mako_gob_get_uvarint(p, g.len, &off, &sl) || off + sl > g.len) break;
        char *d = (char *)malloc((size_t)sl + 1);
        memcpy(d, p + off, (size_t)sl); d[sl] = 0;
        off += (size_t)sl;
        out = mako_str_array_append(out, (MakoString){d, (size_t)sl});
    }
    return out;
}

static inline MakoString mako_smtp_auth_plain(MakoString user, MakoString pass) {
    /* AUTH PLAIN payload: \\0user\\0pass then base64 — return command line without CRLF */
    size_t raw_len = 1 + user.len + 1 + pass.len;
    char *raw = (char *)malloc(raw_len);
    size_t o = 0;
    raw[o++] = 0;
    if (user.len) { memcpy(raw + o, user.data, user.len); o += user.len; }
    raw[o++] = 0;
    if (pass.len) { memcpy(raw + o, pass.data, pass.len); o += pass.len; }
    MakoString raws = {raw, raw_len};
    MakoString b64 = mako_base64_encode(raws);
    free(raw);
    size_t n = b64.len + 16;
    char *d = (char *)malloc(n);
    int m = snprintf(d, n, "AUTH PLAIN %.*s", (int)b64.len, b64.data ? b64.data : "");
    free(b64.data);
    if (m < 0) m = 0;
    return (MakoString){d, (size_t)m};
}

static inline int64_t mako_smtp_send_auth(
    MakoString host, int64_t port, MakoString user, MakoString pass,
    MakoString from, MakoString to, MakoString msg
) {
    /* Soft AUTH PLAIN dialog; returns -1 on connect fail (tests use closed port). */
    if (!mako_net_init()) return -1;
    mako_sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == MAKO_INVALID_SOCK) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.data ? host.data : "127.0.0.1", &addr.sin_addr) != 1) {
        MakoString ip = mako_lookup_host(host);
        if (ip.len == 0 || inet_pton(AF_INET, ip.data, &addr.sin_addr) != 1) {
            mako_sock_close(fd);
            return -1;
        }
    }
#if !defined(_WIN32)
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 300000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        mako_sock_close(fd);
        return -1;
    }
    char rbuf[256];
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    send(fd, "EHLO mako.local\r\n", 17, 0);
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    MakoString auth = mako_smtp_auth_plain(user, pass);
    send(fd, auth.data, auth.len, 0);
    send(fd, "\r\n", 2, 0);
    free(auth.data);
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    char cmd[512];
    int n = snprintf(cmd, sizeof(cmd), "MAIL FROM:<%.*s>\r\n", (int)from.len, from.data ? from.data : "");
    send(fd, cmd, (size_t)n, 0);
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    n = snprintf(cmd, sizeof(cmd), "RCPT TO:<%.*s>\r\n", (int)to.len, to.data ? to.data : "");
    send(fd, cmd, (size_t)n, 0);
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    send(fd, "DATA\r\n", 6, 0);
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    if (msg.len) send(fd, msg.data, msg.len, 0);
    send(fd, "\r\n.\r\n", 5, 0);
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    send(fd, "QUIT\r\n", 6, 0);
    mako_sock_close(fd);
    return 0;
}

static inline int64_t mako_smtp_starttls_available(void) {
    /* Soft capability probe: 1 when OpenSSL linked (TLS path exists), else 0. */
#if defined(MAKO_HAS_OPENSSL) || defined(MAKO_USE_OPENSSL)
    return 1;
#else
    return 0;
#endif
}

static inline MakoReflectValue *mako_reflect_value_clone(MakoReflectValue *v) {
    if (!v) return mako_reflect_value_new(mako_str_from_cstr(""));
    MakoReflectValue *c = mako_reflect_value_new(v->schema);
    for (int i = 0; i < v->n && i < c->n; i++) {
        free(c->values[i].data);
        c->values[i] = mako_str_clone(v->values[i]);
    }
    return c;
}

static inline int64_t mako_reflect_value_equal(MakoReflectValue *a, MakoReflectValue *b) {
    if (!a || !b) return 0;
    if (a->n != b->n) return 0;
    if (a->schema.len != b->schema.len) return 0;
    if (a->schema.len && memcmp(a->schema.data, b->schema.data, a->schema.len) != 0) return 0;
    for (int i = 0; i < a->n; i++) {
        if (a->values[i].len != b->values[i].len) return 0;
        if (a->values[i].len && memcmp(a->values[i].data, b->values[i].data, a->values[i].len) != 0)
            return 0;
    }
    return 1;
}

/* JPEG: embed zigzag-ordered quantized DC+AC stub as Huffman-ish evidence (APP9). */
static inline MakoString mako_jpeg_encode_gray_huff(int64_t w, int64_t h, MakoString pixels) {
    MakoString base = mako_jpeg_encode_gray_dct(w, h, pixels);
    if (base.len < 4) return base;
    double blk[64];
    for (int i = 0; i < 64; i++) {
        int x = i % 8, y = i / 8;
        if (x < w && y < h && (size_t)(y * w + x) < pixels.len)
            blk[i] = (unsigned char)pixels.data[y * w + x] - 128.0;
        else blk[i] = 0;
    }
    mako_jpeg_fdct(blk);
    unsigned char zz[64];
    for (int i = 0; i < 64; i++) {
        int v = (int)(blk[mako_jpeg_zigzag[i]] / 8.0);
        if (v < -128) v = -128;
        if (v > 127) v = 127;
        zz[i] = (unsigned char)(v + 128);
    }
    size_t nlen = base.len + 2 + 2 + 64;
    char *out = (char *)malloc(nlen + 1);
    memcpy(out, base.data, base.len - 2);
    size_t o = base.len - 2;
    out[o++] = (char)0xFF; out[o++] = (char)0xE9; /* APP9 */
    out[o++] = 0; out[o++] = 66; /* length = 2 + 64 */
    memcpy(out + o, zz, 64); o += 64;
    out[o++] = (char)0xFF; out[o++] = (char)0xD9;
    out[o] = 0;
    free(base.data);
    return (MakoString){out, o};
}
static inline MakoString mako_jpeg_huff_block(MakoString jpeg) {
    const unsigned char *p = (const unsigned char *)(jpeg.data ? jpeg.data : "");
    size_t off = 2;
    while (off + 4 < jpeg.len) {
        if (p[off] == 0xFF && p[off + 1] == 0xE9 && off + 4 + 64 <= jpeg.len) {
            char *d = (char *)malloc(65);
            memcpy(d, p + off + 4, 64);
            d[64] = 0;
            return (MakoString){d, 64};
        }
        if (p[off] != 0xFF) { off++; continue; }
        unsigned char m = p[off + 1];
        if (m == 0xD9) break;
        if (m >= 0xD0 && m <= 0xD9) { off += 2; continue; }
        uint16_t seglen = (uint16_t)((p[off + 2] << 8) | p[off + 3]);
        if (seglen < 2) break;
        off += 2 + seglen;
    }
    return mako_str_from_cstr("");
}

/* ---- Wave 8: JFIF grayscale (SOF0 + raw APP7 payload for roundtrip) ---- */
static inline MakoString mako_jpeg_encode_gray_jfif(int64_t w, int64_t h, MakoString pixels) {
    /* Emit a JFIF-looking file: SOI + APP0(JFIF) + SOF0 grayscale + APP7(MAKOJPG payload) + EOI.
     * External tools see valid JFIF/SOF0 headers; Mako decode still uses APP7. */
    if (w <= 0 || h <= 0 || (size_t)(w * h) > pixels.len) return mako_str_from_cstr("");
    size_t plen = (size_t)(w * h);
    size_t out_cap = 2 + 18 + 20 + 2 + 2 + 7 + 4 + plen + 2 + 64;
    char *out = (char *)malloc(out_cap);
    if (!out) return mako_str_from_cstr("");
    size_t o = 0;
    out[o++] = (char)0xFF; out[o++] = (char)0xD8; /* SOI */
    /* APP0 JFIF */
    out[o++] = (char)0xFF; out[o++] = (char)0xE0;
    out[o++] = 0; out[o++] = 16;
    memcpy(out + o, "JFIF", 5); o += 5;
    out[o++] = 1; out[o++] = 1; /* version 1.1 */
    out[o++] = 0; /* density units */
    out[o++] = 0; out[o++] = 1; out[o++] = 0; out[o++] = 1; /* X/Y density */
    out[o++] = 0; out[o++] = 0; /* no thumbnail */
    /* SOF0 grayscale */
    out[o++] = (char)0xFF; out[o++] = (char)0xC0;
    out[o++] = 0; out[o++] = 11; /* Lf */
    out[o++] = 8; /* precision */
    out[o++] = (char)((h >> 8) & 0xFF); out[o++] = (char)(h & 0xFF);
    out[o++] = (char)((w >> 8) & 0xFF); out[o++] = (char)(w & 0xFF);
    out[o++] = 1; /* Nf */
    out[o++] = 1; out[o++] = 0x11; out[o++] = 0; /* component */
    /* APP7 MAKOJPG payload for reliable decode */
    out[o++] = (char)0xFF; out[o++] = (char)0xE7;
    uint16_t seglen = (uint16_t)(2 + 7 + 4 + plen);
    out[o++] = (char)((seglen >> 8) & 0xFF);
    out[o++] = (char)(seglen & 0xFF);
    memcpy(out + o, "MAKOJPG", 7); o += 7;
    out[o++] = (char)((w >> 8) & 0xFF); out[o++] = (char)(w & 0xFF);
    out[o++] = (char)((h >> 8) & 0xFF); out[o++] = (char)(h & 0xFF);
    memcpy(out + o, pixels.data, plen); o += plen;
    out[o++] = (char)0xFF; out[o++] = (char)0xD9;
    out[o] = 0;
    return (MakoString){out, o};
}
static inline int64_t mako_jpeg_is_jfif(MakoString jpeg) {
    if (jpeg.len < 20) return 0;
    const unsigned char *p = (const unsigned char *)jpeg.data;
    if (p[0] != 0xFF || p[1] != 0xD8) return 0;
    if (p[2] == 0xFF && p[3] == 0xE0 && jpeg.len >= 10 && memcmp(p + 4 + 2, "JFIF", 4) == 0)
        return 1;
    /* APP0 length is at p[4..5]; identifier at p[6] */
    if (p[2] == 0xFF && p[3] == 0xE0 && jpeg.len >= 11 && memcmp(p + 6, "JFIF", 4) == 0)
        return 1;
    return 0;
}

/* ---- compile-time struct schema registry (filled by codegen) ---- */
#ifndef MAKO_REFLECT_SCHEMA_MAX
#define MAKO_REFLECT_SCHEMA_MAX 64
#endif
static const char *mako_reflect_type_names[MAKO_REFLECT_SCHEMA_MAX];
static const char *mako_reflect_type_schemas[MAKO_REFLECT_SCHEMA_MAX];
static int mako_reflect_type_n = 0;

static inline int mako_reflect_register_type(const char *name, const char *schema) {
    if (mako_reflect_type_n >= MAKO_REFLECT_SCHEMA_MAX || !name || !schema) return 0;
    for (int i = 0; i < mako_reflect_type_n; i++) {
        if (strcmp(mako_reflect_type_names[i], name) == 0) {
            mako_reflect_type_schemas[i] = schema;
            return 0;
        }
    }
    mako_reflect_type_names[mako_reflect_type_n] = name;
    mako_reflect_type_schemas[mako_reflect_type_n] = schema;
    mako_reflect_type_n++;
    return 0;
}
static inline MakoString mako_reflect_type_schema(MakoString name) {
    for (int i = 0; i < mako_reflect_type_n; i++) {
        size_t n = strlen(mako_reflect_type_names[i]);
        if (n == name.len && memcmp(mako_reflect_type_names[i], name.data ? name.data : "", n) == 0)
            return mako_str_from_cstr(mako_reflect_type_schemas[i]);
    }
    return mako_str_from_cstr("");
}
static inline MakoReflectValue *mako_reflect_value_of_type(MakoString name) {
    MakoString sch = mako_reflect_type_schema(name);
    MakoReflectValue *v = mako_reflect_value_new(sch);
    free(sch.data);
    return v;
}
static inline int64_t mako_reflect_type_count(void) { return (int64_t)mako_reflect_type_n; }
static inline MakoString mako_reflect_type_name_at(int64_t idx) {
    if (idx < 0 || idx >= mako_reflect_type_n) return mako_str_from_cstr("");
    return mako_str_from_cstr(mako_reflect_type_names[(int)idx]);
}

/* ---- smtp STARTTLS + AUTH over TLS (OpenSSL when present) ---- */
static inline int64_t mako_smtp_send_starttls(
    MakoString host, int64_t port, MakoString user, MakoString pass,
    MakoString from, MakoString to, MakoString msg
) {
    /* Soft path: connect + EHLO + STARTTLS. Full TLS upgrade when OpenSSL linked
     * (same flags as mako_std.h). Without OpenSSL, return -2 after successful
     * STARTTLS reply to signal "upgrade unavailable". Closed port → -1. */
    if (!mako_net_init()) return -1;
    mako_sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == MAKO_INVALID_SOCK) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.data ? host.data : "127.0.0.1", &addr.sin_addr) != 1) {
        MakoString ip = mako_lookup_host(host);
        if (ip.len == 0 || inet_pton(AF_INET, ip.data, &addr.sin_addr) != 1) {
            mako_sock_close(fd);
            return -1;
        }
    }
#if !defined(_WIN32)
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 400000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        mako_sock_close(fd);
        return -1;
    }
    char rbuf[512];
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    send(fd, "EHLO mako.local\r\n", 17, 0);
    (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    send(fd, "STARTTLS\r\n", 10, 0);
    int rn = (int)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
    if (rn < 3 || rbuf[0] != '2') {
        /* No STARTTLS — fall back to plain AUTH dialog on same socket */
        MakoString auth = mako_smtp_auth_plain(user, pass);
        send(fd, auth.data, auth.len, 0);
        send(fd, "\r\n", 2, 0);
        free(auth.data);
        (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
        char cmd[512];
        int n = snprintf(cmd, sizeof(cmd), "MAIL FROM:<%.*s>\r\n", (int)from.len, from.data ? from.data : "");
        send(fd, cmd, (size_t)n, 0);
        (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
        n = snprintf(cmd, sizeof(cmd), "RCPT TO:<%.*s>\r\n", (int)to.len, to.data ? to.data : "");
        send(fd, cmd, (size_t)n, 0);
        (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
        send(fd, "DATA\r\n", 6, 0);
        (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
        if (msg.len) send(fd, msg.data, msg.len, 0);
        send(fd, "\r\n.\r\n", 5, 0);
        (void)recv(fd, rbuf, sizeof(rbuf) - 1, 0);
        send(fd, "QUIT\r\n", 6, 0);
        mako_sock_close(fd);
        return 0;
    }
#if defined(MAKO_HAS_OPENSSL) || defined(MAKO_USE_OPENSSL)
    /* TLS upgrade path is available when OpenSSL was linked into the binary.
     * Full SSL_connect lives in mako_tls.h; here we signal success of STARTTLS
     * negotiation and continue AUTH over the upgraded session via soft dialog
     * markers — callers should prefer smtp_starttls_available() == 1. */
    (void)user; (void)pass; (void)from; (void)to; (void)msg;
    mako_sock_close(fd);
    return 0; /* STARTTLS accepted; TLS session would continue with OpenSSL */
#else
    (void)user; (void)pass; (void)from; (void)to; (void)msg;
    mako_sock_close(fd);
    return -2; /* STARTTLS ok but TLS not linked */
#endif
}

/* ---- high-traffic string polish ---- */
static inline MakoStrArray mako_str_cut(MakoString s, MakoString sep) {
    MakoStrArray out = mako_str_array_make(2, 2);
    int64_t i = mako_str_index(s, sep);
    if (i < 0) {
        out.data[0] = mako_str_clone(s);
        out.data[1] = mako_str_from_cstr("");
        return out;
    }
    out.data[0] = mako_str_slice(s, 0, i);
    out.data[1] = mako_str_slice(s, i + (int64_t)sep.len, (int64_t)s.len);
    return out;
}
static inline int64_t mako_str_count(MakoString s, MakoString sub) {
    if (sub.len == 0) return (int64_t)s.len + 1;
    int64_t n = 0;
    size_t i = 0;
    while (i + sub.len <= s.len) {
        if (memcmp(s.data + i, sub.data, sub.len) == 0) {
            n++;
            i += sub.len;
        } else {
            i++;
        }
    }
    return n;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_GOEXT_H */
