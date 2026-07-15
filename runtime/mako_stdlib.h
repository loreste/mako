/* Mako standard library — strings, strconv, fmt, path/fs/os, math, collections, time, sync.
 * Included via mako_std.h. Cross-platform (Win/Mac/Linux); memory-safe (bounds + OOM abort).
 */
#ifndef MAKO_STDLIB_H
#define MAKO_STDLIB_H

#include "mako_rt.h"
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <sys/stat.h>
#include <fcntl.h>
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- strings ---- */

static inline int64_t mako_str_has_prefix(MakoString s, MakoString pref) {
    if (pref.len > s.len) return 0;
    if (pref.len == 0) return 1;
    return memcmp(s.data ? s.data : "", pref.data ? pref.data : "", pref.len) == 0 ? 1 : 0;
}

static inline int64_t mako_str_has_suffix(MakoString s, MakoString suf) {
    if (suf.len > s.len) return 0;
    if (suf.len == 0) return 1;
    const char *p = (s.data ? s.data : "") + (s.len - suf.len);
    return memcmp(p, suf.data ? suf.data : "", suf.len) == 0 ? 1 : 0;
}

static inline int64_t mako_str_index(MakoString hay, MakoString needle) {
    if (needle.len == 0) return 0;
    if (needle.len > hay.len) return -1;
    for (size_t i = 0; i + needle.len <= hay.len; i++) {
        if (memcmp(hay.data + i, needle.data, needle.len) == 0) return (int64_t)i;
    }
    return -1;
}

static inline int64_t mako_str_last_index(MakoString hay, MakoString needle) {
    if (needle.len == 0) return (int64_t)hay.len;
    if (needle.len > hay.len) return -1;
    for (size_t i = hay.len - needle.len + 1; i > 0; i--) {
        size_t off = i - 1;
        if (memcmp(hay.data + off, needle.data, needle.len) == 0) return (int64_t)off;
    }
    return -1;
}

static inline int mako_is_space_byte(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static inline MakoString mako_str_trim_space(MakoString s) {
    size_t lo = 0, hi = s.len;
    while (lo < hi && mako_is_space_byte((unsigned char)s.data[lo])) lo++;
    while (hi > lo && mako_is_space_byte((unsigned char)s.data[hi - 1])) hi--;
    return mako_str_slice(s, (int64_t)lo, (int64_t)hi);
}

static inline MakoString mako_str_trim_left(MakoString s, MakoString cutset) {
    size_t i = 0;
    while (i < s.len) {
        char c = s.data[i];
        int found = 0;
        for (size_t j = 0; j < cutset.len; j++) {
            if (cutset.data[j] == c) { found = 1; break; }
        }
        if (!found) break;
        i++;
    }
    return mako_str_slice(s, (int64_t)i, (int64_t)s.len);
}

static inline MakoString mako_str_trim_right(MakoString s, MakoString cutset) {
    size_t i = s.len;
    while (i > 0) {
        char c = s.data[i - 1];
        int found = 0;
        for (size_t j = 0; j < cutset.len; j++) {
            if (cutset.data[j] == c) { found = 1; break; }
        }
        if (!found) break;
        i--;
    }
    return mako_str_slice(s, 0, (int64_t)i);
}

static inline MakoString mako_str_trim(MakoString s, MakoString cutset) {
    return mako_str_trim_right(mako_str_trim_left(s, cutset), cutset);
}

static inline MakoString mako_str_to_lower(MakoString s) {
    char *d = (char *)malloc(s.len + 1);
    if (!d) mako_abort("str_to_lower OOM");
    for (size_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        d[i] = (char)tolower(c);
    }
    d[s.len] = 0;
    return (MakoString){d, s.len};
}

static inline MakoString mako_str_to_upper(MakoString s) {
    char *d = (char *)malloc(s.len + 1);
    if (!d) mako_abort("str_to_upper OOM");
    for (size_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        d[i] = (char)toupper(c);
    }
    d[s.len] = 0;
    return (MakoString){d, s.len};
}

static inline MakoString mako_str_repeat(MakoString s, int64_t n) {
    if (n <= 0 || s.len == 0) return mako_str_from_cstr("");
    if (n > 1000000) mako_abort("str_repeat: count too large");
    size_t total = (size_t)n * s.len;
    char *d = (char *)malloc(total + 1);
    if (!d) mako_abort("str_repeat OOM");
    for (int64_t i = 0; i < n; i++) {
        memcpy(d + (size_t)i * s.len, s.data, s.len);
    }
    d[total] = 0;
    return (MakoString){d, total};
}

static inline MakoString mako_str_replace(MakoString s, MakoString oldv, MakoString newv) {
    if (oldv.len == 0) return mako_str_clone(s);
    /* Count matches */
    size_t count = 0;
    size_t i = 0;
    while (i + oldv.len <= s.len) {
        if (memcmp(s.data + i, oldv.data, oldv.len) == 0) {
            count++;
            i += oldv.len;
        } else {
            i++;
        }
    }
    if (count == 0) return mako_str_clone(s);
    size_t out_len = s.len + count * newv.len - count * oldv.len;
    char *d = (char *)malloc(out_len + 1);
    if (!d) mako_abort("str_replace OOM");
    size_t o = 0;
    i = 0;
    while (i < s.len) {
        if (i + oldv.len <= s.len && memcmp(s.data + i, oldv.data, oldv.len) == 0) {
            if (newv.len) memcpy(d + o, newv.data, newv.len);
            o += newv.len;
            i += oldv.len;
        } else {
            d[o++] = s.data[i++];
        }
    }
    d[o] = 0;
    return (MakoString){d, o};
}

static inline MakoStrArray mako_str_split(MakoString s, MakoString sep) {
    if (sep.len == 0) {
        /* Split into individual bytes as 1-char strings */
        MakoStrArray a = mako_str_array_make((int64_t)s.len, (int64_t)s.len);
        for (size_t i = 0; i < s.len; i++) {
            char buf[2] = {s.data[i], 0};
            a.data[i] = mako_str_from_cstr(buf);
        }
        a.len = s.len;
        return a;
    }
    /* Count parts */
    size_t parts = 1;
    size_t i = 0;
    while (i + sep.len <= s.len) {
        if (memcmp(s.data + i, sep.data, sep.len) == 0) {
            parts++;
            i += sep.len;
        } else {
            i++;
        }
    }
    MakoStrArray a = mako_str_array_make((int64_t)parts, (int64_t)parts);
    size_t start = 0;
    size_t idx = 0;
    i = 0;
    while (i + sep.len <= s.len) {
        if (memcmp(s.data + i, sep.data, sep.len) == 0) {
            a.data[idx++] = mako_str_slice(s, (int64_t)start, (int64_t)i);
            i += sep.len;
            start = i;
        } else {
            i++;
        }
    }
    a.data[idx++] = mako_str_slice(s, (int64_t)start, (int64_t)s.len);
    a.len = idx;
    return a;
}

static inline MakoStrArray mako_str_fields(MakoString s) {
    size_t parts = 0;
    size_t i = 0;
    while (i < s.len) {
        while (i < s.len && mako_is_space_byte((unsigned char)s.data[i])) i++;
        if (i >= s.len) break;
        parts++;
        while (i < s.len && !mako_is_space_byte((unsigned char)s.data[i])) i++;
    }
    MakoStrArray a = mako_str_array_make((int64_t)parts, (int64_t)parts);
    size_t idx = 0;
    i = 0;
    while (i < s.len) {
        while (i < s.len && mako_is_space_byte((unsigned char)s.data[i])) i++;
        if (i >= s.len) break;
        size_t start = i;
        while (i < s.len && !mako_is_space_byte((unsigned char)s.data[i])) i++;
        a.data[idx++] = mako_str_slice(s, (int64_t)start, (int64_t)i);
    }
    a.len = idx;
    return a;
}

static inline MakoString mako_str_join(MakoStrArray parts, MakoString sep) {
    if (parts.len == 0) return mako_str_from_cstr("");
    size_t total = 0;
    for (size_t i = 0; i < parts.len; i++) total += parts.data[i].len;
    total += sep.len * (parts.len - 1);
    char *d = (char *)malloc(total + 1);
    if (!d) mako_abort("str_join OOM");
    size_t o = 0;
    for (size_t i = 0; i < parts.len; i++) {
        if (i > 0 && sep.len) {
            memcpy(d + o, sep.data, sep.len);
            o += sep.len;
        }
        if (parts.data[i].len) {
            memcpy(d + o, parts.data[i].data, parts.data[i].len);
            o += parts.data[i].len;
        }
    }
    d[o] = 0;
    return (MakoString){d, o};
}

/* ---- strconv / fmt ---- */

static inline MakoString mako_format_int(int64_t n) {
    return mako_int_to_string(n);
}

static inline MakoString mako_format_float(double v, int64_t prec) {
    char buf[128];
    int p = (int)prec;
    if (p < 0) p = 6;
    if (p > 17) p = 17;
    int written = snprintf(buf, sizeof(buf), "%.*f", p, v);
    if (written < 0) written = 0;
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_format_bool(int64_t v) {
    return mako_str_from_cstr(v ? "true" : "false");
}

static inline MakoResultInt mako_parse_bool(MakoString s) {
    if (mako_str_eq(s, mako_str_from_cstr("true")) ||
        mako_str_eq(s, mako_str_from_cstr("1")) ||
        mako_str_eq(s, mako_str_from_cstr("TRUE"))) {
        return mako_ok_int(1);
    }
    if (mako_str_eq(s, mako_str_from_cstr("false")) ||
        mako_str_eq(s, mako_str_from_cstr("0")) ||
        mako_str_eq(s, mako_str_from_cstr("FALSE"))) {
        return mako_ok_int(0);
    }
    return mako_err_int(mako_str_from_cstr("parse_bool failed"));
}

/* fmt_sprintf: replace first %s / %d / %v in fmt with arg (string form of int or string). */
static inline MakoString mako_fmt_sprintf_s(MakoString fmt, MakoString arg) {
    const char *p = fmt.data ? fmt.data : "";
    size_t n = fmt.len;
    for (size_t i = 0; i + 1 < n; i++) {
        if (p[i] == '%' && (p[i + 1] == 's' || p[i + 1] == 'v')) {
            size_t out_len = n - 2 + arg.len;
            char *d = (char *)malloc(out_len + 1);
            if (!d) mako_abort("fmt_sprintf OOM");
            memcpy(d, p, i);
            if (arg.len) memcpy(d + i, arg.data, arg.len);
            memcpy(d + i + arg.len, p + i + 2, n - i - 2);
            d[out_len] = 0;
            return (MakoString){d, out_len};
        }
    }
    return mako_str_clone(fmt);
}

static inline MakoString mako_fmt_sprintf_d(MakoString fmt, int64_t arg) {
    MakoString as = mako_int_to_string(arg);
    const char *p = fmt.data ? fmt.data : "";
    size_t n = fmt.len;
    for (size_t i = 0; i + 1 < n; i++) {
        if (p[i] == '%' && (p[i + 1] == 'd' || p[i + 1] == 'v' || p[i + 1] == 'i')) {
            size_t out_len = n - 2 + as.len;
            char *d = (char *)malloc(out_len + 1);
            if (!d) mako_abort("fmt_sprintf OOM");
            memcpy(d, p, i);
            if (as.len) memcpy(d + i, as.data, as.len);
            memcpy(d + i + as.len, p + i + 2, n - i - 2);
            d[out_len] = 0;
            return (MakoString){d, out_len};
        }
    }
    return mako_str_clone(fmt);
}

/* ---- encoding/hex ---- */

static inline MakoString mako_hex_encode(MakoString s) {
    static const char *hex = "0123456789abcdef";
    char *d = (char *)malloc(s.len * 2 + 1);
    if (!d) mako_abort("hex_encode OOM");
    for (size_t i = 0; i < s.len; i++) {
        unsigned char c = (unsigned char)s.data[i];
        d[i * 2] = hex[c >> 4];
        d[i * 2 + 1] = hex[c & 0xf];
    }
    d[s.len * 2] = 0;
    return (MakoString){d, s.len * 2};
}

/* ---- path / fs / os ---- */

#if defined(_WIN32)
#define MAKO_PATH_SEP '\\'
#else
#define MAKO_PATH_SEP '/'
#endif

static inline int mako_path_is_sep(char c) {
    return c == '/' || c == '\\';
}

static inline MakoString mako_path_clean(MakoString path) {
    /* Lexical clean: collapse separators, resolve . and .. (no symlink). */
    if (path.len == 0) return mako_str_from_cstr(".");
    const char *p = path.data ? path.data : "";
    int abs = mako_path_is_sep(p[0]);
    size_t i = 0;
    char drive[3] = {0, 0, 0};
#if defined(_WIN32)
    if (path.len >= 2 &&
        ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) &&
        p[1] == ':') {
        drive[0] = p[0];
        drive[1] = ':';
        i = 2;
        if (i < path.len && mako_path_is_sep(p[i])) {
            abs = 1;
            i++;
        }
    } else
#endif
    if (abs) {
        i = 1;
    }

    typedef struct { size_t off; size_t len; } Seg;
    Seg segs[256];
    size_t nseg = 0;
    while (i < path.len) {
        while (i < path.len && mako_path_is_sep(p[i])) i++;
        if (i >= path.len) break;
        size_t start = i;
        while (i < path.len && !mako_path_is_sep(p[i])) i++;
        size_t len = i - start;
        if (len == 1 && p[start] == '.') {
            continue;
        }
        if (len == 2 && p[start] == '.' && p[start + 1] == '.') {
            if (nseg > 0) {
                nseg--;
            } else if (!abs && !drive[0]) {
                segs[nseg].off = start;
                segs[nseg].len = len;
                nseg++;
            }
            continue;
        }
        if (nseg < 256) {
            segs[nseg].off = start;
            segs[nseg].len = len;
            nseg++;
        }
    }

    size_t cap = path.len + 4;
    char *buf = (char *)malloc(cap);
    if (!buf) mako_abort("path_clean OOM");
    size_t n = 0;
    if (drive[0]) {
        buf[n++] = drive[0];
        buf[n++] = ':';
    }
    if (abs) buf[n++] = '/';
    if (nseg == 0) {
        if (!abs && !drive[0]) {
            buf[0] = '.';
            buf[1] = 0;
            return (MakoString){buf, 1};
        }
        if (drive[0] && !abs) {
            buf[n] = 0;
            return (MakoString){buf, n};
        }
        buf[n] = 0;
        return (MakoString){buf, n ? n : 1};
    }
    for (size_t s = 0; s < nseg; s++) {
        if (s > 0 || abs || (drive[0] && n > 0)) {
            if (!(s == 0 && abs && n > 0 && buf[n - 1] == '/')) {
                if (!(s == 0 && abs)) buf[n++] = '/';
            }
        }
        memcpy(buf + n, p + segs[s].off, segs[s].len);
        n += segs[s].len;
    }
    buf[n] = 0;
    return (MakoString){buf, n};
}

static inline MakoString mako_path_base(MakoString path) {
    if (path.len == 0) return mako_str_from_cstr(".");
    size_t end = path.len;
    while (end > 0 && mako_path_is_sep(path.data[end - 1])) end--;
    if (end == 0) return mako_str_from_cstr("/");
    size_t start = end;
    while (start > 0 && !mako_path_is_sep(path.data[start - 1])) start--;
    return mako_str_slice(path, (int64_t)start, (int64_t)end);
}

static inline MakoString mako_path_dir(MakoString path) {
    if (path.len == 0) return mako_str_from_cstr(".");
    size_t end = path.len;
    while (end > 0 && mako_path_is_sep(path.data[end - 1])) end--;
    if (end == 0) return mako_str_from_cstr("/");
    size_t start = end;
    while (start > 0 && !mako_path_is_sep(path.data[start - 1])) start--;
    if (start == 0) return mako_str_from_cstr(".");
    while (start > 1 && mako_path_is_sep(path.data[start - 1])) start--;
    return mako_str_slice(path, 0, (int64_t)start);
}

static inline MakoString mako_path_ext(MakoString path) {
    MakoString base = mako_path_base(path);
    for (size_t i = base.len; i > 0; i--) {
        if (base.data[i - 1] == '.') {
            if (i == 1) return mako_str_from_cstr(""); /* hidden file .foo */
            return mako_str_slice(base, (int64_t)(i - 1), (int64_t)base.len);
        }
    }
    return mako_str_from_cstr("");
}

static inline int64_t mako_path_is_abs(MakoString path) {
    if (path.len == 0) return 0;
    if (mako_path_is_sep(path.data[0])) return 1;
#if defined(_WIN32)
    if (path.len >= 3 && path.data[1] == ':' && mako_path_is_sep(path.data[2])) return 1;
#endif
    return 0;
}

static inline MakoString mako_getcwd(void) {
    char buf[4096];
#if defined(_WIN32)
    if (!_getcwd(buf, (int)sizeof(buf))) return mako_str_from_cstr("");
#else
    if (!getcwd(buf, sizeof(buf))) return mako_str_from_cstr("");
#endif
    return mako_str_from_cstr(buf);
}

static inline int64_t mako_chdir(MakoString path) {
    const char *p = path.data ? path.data : "";
#if defined(_WIN32)
    return _chdir(p) == 0 ? 0 : -1;
#else
    return chdir(p) == 0 ? 0 : -1;
#endif
}

static inline int64_t mako_is_dir(MakoString path) {
    const char *p = path.data ? path.data : "";
    struct stat st;
    if (stat(p, &st) != 0) return 0;
#if defined(_WIN32)
    return (st.st_mode & _S_IFDIR) ? 1 : 0;
#else
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

static inline MakoStrArray mako_read_dir(MakoString path) {
    const char *p = path.data ? path.data : ".";
    MakoStrArray a = mako_str_array_make(0, 16);
#if defined(_WIN32)
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", p);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return a;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        a = mako_str_array_append(a, mako_str_from_cstr(fd.cFileName));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(p);
    if (!d) return a;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        a = mako_str_array_append(a, mako_str_from_cstr(ent->d_name));
    }
    closedir(d);
#endif
    return a;
}

/* ---- math ---- */

static inline int64_t mako_abs(int64_t x) {
    return x < 0 ? -x : x;
}

static inline int64_t mako_min(int64_t a, int64_t b) {
    return a < b ? a : b;
}

static inline int64_t mako_max(int64_t a, int64_t b) {
    return a > b ? a : b;
}

static inline int64_t mako_clamp(int64_t x, int64_t lo, int64_t hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline double mako_math_abs(double x) { return fabs(x); }
static inline double mako_math_sqrt(double x) { return sqrt(x); }
static inline double mako_math_pow(double x, double y) { return pow(x, y); }
static inline double mako_math_floor(double x) { return floor(x); }
static inline double mako_math_ceil(double x) { return ceil(x); }
static inline double mako_math_sin(double x) { return sin(x); }
static inline double mako_math_cos(double x) { return cos(x); }
static inline double mako_math_log(double x) { return log(x); }
static inline double mako_math_exp(double x) { return exp(x); }

/* ---- collections ---- */

static inline int64_t mako_ints_contains(MakoIntArray a, int64_t v) {
    for (size_t i = 0; i < a.len; i++) {
        if (a.data[i] == v) return 1;
    }
    return 0;
}

static inline int64_t mako_strings_contains(MakoStrArray a, MakoString v) {
    for (size_t i = 0; i < a.len; i++) {
        if (mako_str_eq(a.data[i], v)) return 1;
    }
    return 0;
}

static inline MakoIntArray mako_ints_copy(MakoIntArray a) {
    MakoIntArray out = mako_int_array_make((int64_t)a.len, (int64_t)a.len);
    if (a.len) memcpy(out.data, a.data, a.len * sizeof(int64_t));
    out.len = a.len;
    return out;
}

static inline int64_t mako_ints_index(MakoIntArray a, int64_t v) {
    for (size_t i = 0; i < a.len; i++) {
        if (a.data[i] == v) return (int64_t)i;
    }
    return -1;
}

/* ---- List[T] / richer collections (int + string specializations; List aliases []T) ---- */
static inline MakoIntArray mako_list_new_int(void) {
    return mako_int_array_make(0, 8);
}
static inline MakoStrArray mako_list_new_str(void) {
    return mako_str_array_make(0, 8);
}
static inline MakoIntArray mako_list_push_int(MakoIntArray a, int64_t v) {
    return mako_slice_append(a, v);
}
static inline MakoStrArray mako_list_push_str(MakoStrArray a, MakoString v) {
    return mako_str_array_append(a, v);
}
/* Pop last: drop_last returns shortened list; last_* peeks without remove. */
static int64_t mako_list_popped_int_tls = 0;
static MakoString mako_list_popped_str_tls;
static inline MakoIntArray mako_list_pop_int(MakoIntArray a) {
    if (a.len == 0) {
        mako_list_popped_int_tls = 0;
        return a;
    }
    mako_list_popped_int_tls = a.data[a.len - 1];
    a.len--;
    return a;
}
static inline int64_t mako_list_popped_int(void) { return mako_list_popped_int_tls; }
static inline MakoStrArray mako_list_pop_str(MakoStrArray a) {
    if (a.len == 0) {
        mako_list_popped_str_tls = mako_str_from_cstr("");
        return a;
    }
    mako_list_popped_str_tls = a.data[a.len - 1];
    a.len--;
    return a;
}
static inline MakoString mako_list_popped_str(void) { return mako_list_popped_str_tls; }
static inline int64_t mako_list_get_int(MakoIntArray a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) return 0;
    return a.data[i];
}
static inline MakoString mako_list_get_str(MakoStrArray a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) return mako_str_from_cstr("");
    return a.data[i];
}
static inline int64_t mako_list_len_int(MakoIntArray a) { return (int64_t)a.len; }
static inline int64_t mako_list_len_str(MakoStrArray a) { return (int64_t)a.len; }
static inline MakoIntArray mako_list_clear_int(MakoIntArray a) {
    a.len = 0;
    return a;
}
static inline MakoStrArray mako_list_clear_str(MakoStrArray a) {
    a.len = 0;
    return a;
}
/* Insert v at index i (clamped to [0,len]); returns new list. */
static inline MakoIntArray mako_list_insert_int(MakoIntArray a, int64_t i, int64_t v) {
    if (i < 0) i = 0;
    if ((size_t)i > a.len) i = (int64_t)a.len;
    MakoIntArray out = mako_int_array_make((int64_t)a.len + 1, (int64_t)a.len + 1);
    size_t j = 0;
    for (size_t k = 0; k < a.len; k++) {
        if ((int64_t)k == i) out.data[j++] = v;
        out.data[j++] = a.data[k];
    }
    if ((size_t)i == a.len) out.data[j++] = v;
    out.len = j;
    return out;
}
static inline MakoStrArray mako_list_insert_str(MakoStrArray a, int64_t i, MakoString v) {
    if (i < 0) i = 0;
    if ((size_t)i > a.len) i = (int64_t)a.len;
    MakoStrArray out = mako_str_array_make((int64_t)a.len + 1, (int64_t)a.len + 1);
    size_t j = 0;
    for (size_t k = 0; k < a.len; k++) {
        if ((int64_t)k == i) out.data[j++] = v;
        out.data[j++] = a.data[k];
    }
    if ((size_t)i == a.len) out.data[j++] = v;
    out.len = j;
    return out;
}
/* Remove index i; OOB → copy. */
static inline MakoIntArray mako_list_remove_int(MakoIntArray a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) return mako_ints_copy(a);
    MakoIntArray out = mako_int_array_make((int64_t)a.len - 1, (int64_t)a.len - 1);
    size_t j = 0;
    for (size_t k = 0; k < a.len; k++) {
        if ((int64_t)k == i) continue;
        out.data[j++] = a.data[k];
    }
    out.len = j;
    return out;
}
static inline MakoStrArray mako_list_remove_str(MakoStrArray a, int64_t i) {
    if (i < 0 || (size_t)i >= a.len) {
        MakoStrArray out = mako_str_array_make((int64_t)a.len, (int64_t)a.len);
        for (size_t k = 0; k < a.len; k++) out.data[k] = a.data[k];
        out.len = a.len;
        return out;
    }
    MakoStrArray out = mako_str_array_make((int64_t)a.len - 1, (int64_t)a.len - 1);
    size_t j = 0;
    for (size_t k = 0; k < a.len; k++) {
        if ((int64_t)k == i) continue;
        out.data[j++] = a.data[k];
    }
    out.len = j;
    return out;
}
/* Stack: push=append, peek last. */
static inline int64_t mako_stack_peek_int(MakoIntArray a) {
    if (a.len == 0) return 0;
    return a.data[a.len - 1];
}
static inline MakoString mako_stack_peek_str(MakoStrArray a) {
    if (a.len == 0) return mako_str_from_cstr("");
    return a.data[a.len - 1];
}
/* Queue: pop front — returns shortened list; value in queue_popped_*. */
static int64_t mako_queue_popped_int_tls = 0;
static MakoString mako_queue_popped_str_tls;
static inline MakoIntArray mako_queue_pop_int(MakoIntArray a) {
    if (a.len == 0) {
        mako_queue_popped_int_tls = 0;
        return a;
    }
    mako_queue_popped_int_tls = a.data[0];
    MakoIntArray out = mako_int_array_make((int64_t)a.len - 1, (int64_t)a.len - 1);
    for (size_t i = 1; i < a.len; i++) out.data[i - 1] = a.data[i];
    out.len = a.len - 1;
    return out;
}
static inline int64_t mako_queue_popped_int(void) { return mako_queue_popped_int_tls; }
static inline MakoStrArray mako_queue_pop_str(MakoStrArray a) {
    if (a.len == 0) {
        mako_queue_popped_str_tls = mako_str_from_cstr("");
        return a;
    }
    mako_queue_popped_str_tls = a.data[0];
    MakoStrArray out = mako_str_array_make((int64_t)a.len - 1, (int64_t)a.len - 1);
    for (size_t i = 1; i < a.len; i++) out.data[i - 1] = a.data[i];
    out.len = a.len - 1;
    return out;
}
static inline MakoString mako_queue_popped_str(void) { return mako_queue_popped_str_tls; }
/* Set-like uniqueness: ints already have slices_unique; strings here. */
static inline MakoStrArray mako_slices_unique_strs(MakoStrArray a) {
    MakoStrArray sorted = mako_sort_strings(a);
    size_t n = 0;
    for (size_t i = 0; i < sorted.len; i++) {
        if (i == 0 || !mako_str_eq(sorted.data[i], sorted.data[i - 1])) n++;
    }
    MakoStrArray out = mako_str_array_make((int64_t)n, (int64_t)n);
    size_t j = 0;
    for (size_t i = 0; i < sorted.len; i++) {
        if (i == 0 || !mako_str_eq(sorted.data[i], sorted.data[i - 1])) {
            out.data[j++] = sorted.data[i];
        }
    }
    out.len = j;
    return out;
}
static inline MakoStrArray mako_slices_reverse_strs(MakoStrArray a) {
    MakoStrArray out = mako_str_array_make((int64_t)a.len, (int64_t)a.len);
    for (size_t i = 0; i < a.len; i++) out.data[i] = a.data[a.len - 1 - i];
    out.len = a.len;
    return out;
}
static inline int64_t mako_strings_index(MakoStrArray a, MakoString v) {
    for (size_t i = 0; i < a.len; i++) {
        if (mako_str_eq(a.data[i], v)) return (int64_t)i;
    }
    return -1;
}
static inline MakoStrArray mako_strings_copy(MakoStrArray a) {
    MakoStrArray out = mako_str_array_make((int64_t)a.len, (int64_t)a.len);
    for (size_t i = 0; i < a.len; i++) out.data[i] = a.data[i];
    out.len = a.len;
    return out;
}

/* ---- richer collections: set / heap / stats / concat / binary search / ring ---- */
static inline int64_t mako_list_sum_int(MakoIntArray a) {
    int64_t s = 0;
    for (size_t i = 0; i < a.len; i++) s += a.data[i];
    return s;
}
static inline int64_t mako_list_min_int(MakoIntArray a) {
    if (a.len == 0) return 0;
    int64_t m = a.data[0];
    for (size_t i = 1; i < a.len; i++) if (a.data[i] < m) m = a.data[i];
    return m;
}
static inline int64_t mako_list_max_int(MakoIntArray a) {
    if (a.len == 0) return 0;
    int64_t m = a.data[0];
    for (size_t i = 1; i < a.len; i++) if (a.data[i] > m) m = a.data[i];
    return m;
}
static inline MakoIntArray mako_list_concat_int(MakoIntArray a, MakoIntArray b) {
    MakoIntArray out = mako_int_array_make((int64_t)(a.len + b.len), (int64_t)(a.len + b.len));
    for (size_t i = 0; i < a.len; i++) out.data[i] = a.data[i];
    for (size_t i = 0; i < b.len; i++) out.data[a.len + i] = b.data[i];
    out.len = a.len + b.len;
    return out;
}
static inline MakoStrArray mako_list_concat_str(MakoStrArray a, MakoStrArray b) {
    MakoStrArray out = mako_str_array_make((int64_t)(a.len + b.len), (int64_t)(a.len + b.len));
    for (size_t i = 0; i < a.len; i++) out.data[i] = a.data[i];
    for (size_t i = 0; i < b.len; i++) out.data[a.len + i] = b.data[i];
    out.len = a.len + b.len;
    return out;
}
/* Binary search on sorted asc []int; returns index or -1. */
static inline int64_t mako_list_binary_search_int(MakoIntArray a, int64_t v) {
    int64_t lo = 0, hi = (int64_t)a.len - 1;
    while (lo <= hi) {
        int64_t mid = lo + (hi - lo) / 2;
        if (a.data[mid] == v) return mid;
        if (a.data[mid] < v) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}
/* Set ops on sorted unique int arrays (call slices_unique first if needed). */
static inline MakoIntArray mako_set_union_int(MakoIntArray a, MakoIntArray b) {
    MakoIntArray out = mako_int_array_make(0, (int64_t)(a.len + b.len));
    size_t i = 0, j = 0;
    while (i < a.len && j < b.len) {
        if (a.data[i] < b.data[j]) {
            out = mako_slice_append(out, a.data[i++]);
        } else if (b.data[j] < a.data[i]) {
            out = mako_slice_append(out, b.data[j++]);
        } else {
            out = mako_slice_append(out, a.data[i]);
            i++;
            j++;
        }
    }
    while (i < a.len) out = mako_slice_append(out, a.data[i++]);
    while (j < b.len) out = mako_slice_append(out, b.data[j++]);
    return out;
}
static inline MakoIntArray mako_set_intersect_int(MakoIntArray a, MakoIntArray b) {
    MakoIntArray out = mako_int_array_make(0, (int64_t)(a.len < b.len ? a.len : b.len));
    size_t i = 0, j = 0;
    while (i < a.len && j < b.len) {
        if (a.data[i] < b.data[j]) i++;
        else if (b.data[j] < a.data[i]) j++;
        else {
            out = mako_slice_append(out, a.data[i]);
            i++;
            j++;
        }
    }
    return out;
}
static inline MakoIntArray mako_set_diff_int(MakoIntArray a, MakoIntArray b) {
    MakoIntArray out = mako_int_array_make(0, (int64_t)a.len);
    size_t i = 0, j = 0;
    while (i < a.len && j < b.len) {
        if (a.data[i] < b.data[j]) out = mako_slice_append(out, a.data[i++]);
        else if (b.data[j] < a.data[i]) j++;
        else {
            i++;
            j++;
        }
    }
    while (i < a.len) out = mako_slice_append(out, a.data[i++]);
    return out;
}
static inline int64_t mako_set_has_int(MakoIntArray a, int64_t v) {
    return mako_list_binary_search_int(a, v) >= 0 ? 1 : 0;
}
/* Min-heap over []int (array heap layout). */
static inline void mako_heap_sift_up(MakoIntArray *h, size_t i) {
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (h->data[p] <= h->data[i]) break;
        int64_t t = h->data[p];
        h->data[p] = h->data[i];
        h->data[i] = t;
        i = p;
    }
}
static inline void mako_heap_sift_down(MakoIntArray *h, size_t i) {
    for (;;) {
        size_t l = 2 * i + 1, r = 2 * i + 2, smallest = i;
        if (l < h->len && h->data[l] < h->data[smallest]) smallest = l;
        if (r < h->len && h->data[r] < h->data[smallest]) smallest = r;
        if (smallest == i) break;
        int64_t t = h->data[i];
        h->data[i] = h->data[smallest];
        h->data[smallest] = t;
        i = smallest;
    }
}
static inline MakoIntArray mako_heap_push_int(MakoIntArray h, int64_t v) {
    h = mako_slice_append(h, v);
    mako_heap_sift_up(&h, h.len - 1);
    return h;
}
static int64_t mako_heap_popped_tls = 0;
static inline MakoIntArray mako_heap_pop_int(MakoIntArray h) {
    if (h.len == 0) {
        mako_heap_popped_tls = 0;
        return h;
    }
    mako_heap_popped_tls = h.data[0];
    h.data[0] = h.data[h.len - 1];
    h.len--;
    if (h.len > 0) mako_heap_sift_down(&h, 0);
    return h;
}
static inline int64_t mako_heap_popped_int(void) { return mako_heap_popped_tls; }
static inline int64_t mako_heap_peek_int(MakoIntArray h) {
    return h.len ? h.data[0] : 0;
}
/* Ring buffer: use mako_ring_* in mako_std.h (product lock-free ring). */
/* list equality */
static inline int64_t mako_list_eq_int(MakoIntArray a, MakoIntArray b) {
    if (a.len != b.len) return 0;
    for (size_t i = 0; i < a.len; i++) if (a.data[i] != b.data[i]) return 0;
    return 1;
}
static inline MakoIntArray mako_list_fill_int(int64_t n, int64_t v) {
    if (n < 0) n = 0;
    if (n > 1 << 20) n = 1 << 20;
    MakoIntArray out = mako_int_array_make(n, n);
    for (int64_t i = 0; i < n; i++) out.data[i] = v;
    out.len = (size_t)n;
    return out;
}
static inline MakoIntArray mako_list_range_int(int64_t start, int64_t end) {
    /* half-open [start, end) */
    if (end < start) end = start;
    int64_t n = end - start;
    if (n > 1 << 20) n = 1 << 20;
    MakoIntArray out = mako_int_array_make(n, n);
    for (int64_t i = 0; i < n; i++) out.data[i] = start + i;
    out.len = (size_t)n;
    return out;
}

/* ---- time ---- */

static inline MakoString mako_time_format_rfc3339(int64_t unix_ms) {
    time_t sec = (time_t)(unix_ms / 1000);
    int ms = (int)(unix_ms % 1000);
    if (ms < 0) ms = -ms;
    struct tm tm;
#if defined(_WIN32)
    gmtime_s(&tm, &sec);
#else
    gmtime_r(&sec, &tm);
#endif
    char buf[64];
    int n = snprintf(
        buf,
        sizeof(buf),
        "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec,
        ms
    );
    if (n < 0) n = 0;
    return mako_str_from_cstr(buf);
}

static inline int64_t mako_time_unix(void) {
    return mako_now_ms() / 1000;
}

/* ---- full time package (calendar + duration + parse) ---- */
static inline void mako_time_gm(int64_t unix_ms, struct tm *out, int *out_ms) {
    time_t sec = (time_t)(unix_ms / 1000);
    int ms = (int)(unix_ms % 1000);
    if (ms < 0) {
        ms += 1000;
        sec -= 1;
    }
    if (out_ms) *out_ms = ms;
#if defined(_WIN32)
    gmtime_s(out, &sec);
#else
    gmtime_r(&sec, out);
#endif
}
static inline void mako_time_local(int64_t unix_ms, struct tm *out, int *out_ms) {
    time_t sec = (time_t)(unix_ms / 1000);
    int ms = (int)(unix_ms % 1000);
    if (ms < 0) {
        ms += 1000;
        sec -= 1;
    }
    if (out_ms) *out_ms = ms;
#if defined(_WIN32)
    localtime_s(out, &sec);
#else
    localtime_r(&sec, out);
#endif
}
static inline int64_t mako_time_year(int64_t unix_ms) {
    struct tm tm;
    mako_time_gm(unix_ms, &tm, NULL);
    return tm.tm_year + 1900;
}
static inline int64_t mako_time_month(int64_t unix_ms) {
    struct tm tm;
    mako_time_gm(unix_ms, &tm, NULL);
    return tm.tm_mon + 1;
}
static inline int64_t mako_time_day(int64_t unix_ms) {
    struct tm tm;
    mako_time_gm(unix_ms, &tm, NULL);
    return tm.tm_mday;
}
static inline int64_t mako_time_hour(int64_t unix_ms) {
    struct tm tm;
    mako_time_gm(unix_ms, &tm, NULL);
    return tm.tm_hour;
}
static inline int64_t mako_time_minute(int64_t unix_ms) {
    struct tm tm;
    mako_time_gm(unix_ms, &tm, NULL);
    return tm.tm_min;
}
static inline int64_t mako_time_second(int64_t unix_ms) {
    struct tm tm;
    mako_time_gm(unix_ms, &tm, NULL);
    return tm.tm_sec;
}
static inline int64_t mako_time_millisecond(int64_t unix_ms) {
    int ms = 0;
    struct tm tm;
    mako_time_gm(unix_ms, &tm, &ms);
    return ms;
}
static inline int64_t mako_time_weekday(int64_t unix_ms) {
    struct tm tm;
    mako_time_gm(unix_ms, &tm, NULL);
    return tm.tm_wday; /* 0=Sun … 6=Sat */
}
static inline int64_t mako_time_yearday(int64_t unix_ms) {
    struct tm tm;
    mako_time_gm(unix_ms, &tm, NULL);
    return tm.tm_yday + 1; /* 1..366 */
}
/* Portable UTC mktime (avoids glibc timegm feature macros / macOS C99 issues). */
static inline time_t mako_timegm_tm(struct tm *tm) {
#if defined(_WIN32)
    return _mkgmtime(tm);
#else
    /* Algorithm: days since 1970-01-01 from Y-M-D, then h/m/s. */
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon + 1;
    int d = tm->tm_mday;
    if (m <= 2) {
        y -= 1;
        m += 12;
    }
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m - 3) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + (int64_t)doe - 719468;
    return (time_t)(days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec);
#endif
}
/* Build UTC instant from calendar fields. Returns unix ms. */
static inline int64_t mako_time_date(int64_t year, int64_t month, int64_t day,
                                     int64_t hour, int64_t min, int64_t sec, int64_t ms) {
    if (month < 1 || month > 12 || day < 1 || day > 31) return -1;
    if (hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60) return -1;
    if (ms < 0 || ms > 999) return -1;
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = (int)year - 1900;
    tm.tm_mon = (int)month - 1;
    tm.tm_mday = (int)day;
    tm.tm_hour = (int)hour;
    tm.tm_min = (int)min;
    tm.tm_sec = (int)sec;
    tm.tm_isdst = 0;
    time_t t = mako_timegm_tm(&tm);
    if (t == (time_t)-1) return -1;
    return (int64_t)t * 1000 + ms;
}
static inline int64_t mako_time_add_ms(int64_t t, int64_t delta_ms) { return t + delta_ms; }
static inline int64_t mako_time_sub_ms(int64_t a, int64_t b) { return a - b; }
static inline int64_t mako_time_after(int64_t a, int64_t b) { return a > b ? 1 : 0; }
static inline int64_t mako_time_before(int64_t a, int64_t b) { return a < b ? 1 : 0; }
static inline int64_t mako_time_equal(int64_t a, int64_t b) { return a == b ? 1 : 0; }
static inline int64_t mako_time_since_ms(int64_t t) { return mako_now_ms() - t; }
static inline int64_t mako_time_until_ms(int64_t t) { return t - mako_now_ms(); }
static inline int64_t mako_time_trunc_day(int64_t unix_ms) {
    struct tm tm;
    mako_time_gm(unix_ms, &tm, NULL);
    return mako_time_date(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, 0, 0, 0, 0);
}
static inline int64_t mako_time_trunc_hour(int64_t unix_ms) {
    struct tm tm;
    mako_time_gm(unix_ms, &tm, NULL);
    return mako_time_date(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, 0, 0, 0);
}
/* Local UTC offset in seconds east of UTC (best-effort). */
static inline int64_t mako_time_local_offset_sec(void) {
    time_t now = time(NULL);
    struct tm local_tm, gm_tm;
#if defined(_WIN32)
    localtime_s(&local_tm, &now);
    gmtime_s(&gm_tm, &now);
#else
    localtime_r(&now, &local_tm);
    gmtime_r(&now, &gm_tm);
#endif
    time_t lt = mako_timegm_tm(&local_tm);
    time_t gt = mako_timegm_tm(&gm_tm);
    return (int64_t)(lt - gt);
}
static inline MakoString mako_time_format_local(int64_t unix_ms) {
    struct tm tm;
    int ms = 0;
    mako_time_local(unix_ms, &tm, &ms);
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    if (n < 0) n = 0;
    return mako_str_from_cstr(buf);
}
static inline MakoString mako_time_format_date(int64_t unix_ms) {
    struct tm tm;
    mako_time_gm(unix_ms, &tm, NULL);
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    if (n < 0) n = 0;
    return mako_str_from_cstr(buf);
}
static inline MakoString mako_time_format_clock(int64_t unix_ms) {
    struct tm tm;
    mako_time_gm(unix_ms, &tm, NULL);
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    if (n < 0) n = 0;
    return mako_str_from_cstr(buf);
}
/* Parse RFC3339 / RFC3339Nano-ish: YYYY-MM-DDTHH:MM:SS[.mmm][Z|±HH:MM] → unix ms. -1 fail. */
static inline int64_t mako_time_parse_rfc3339(MakoString s) {
    if (!s.data || s.len < 19) return -1;
    int Y = 0, Mo = 0, D = 0, h = 0, mi = 0, se = 0, ms = 0;
    char buf[64];
    size_t n = s.len < 63 ? s.len : 63;
    memcpy(buf, s.data, n);
    buf[n] = 0;
    char *p = buf;
    if (sscanf(p, "%d-%d-%d", &Y, &Mo, &D) != 3) return -1;
    while (*p && *p != 'T' && *p != 't' && *p != ' ') p++;
    if (!*p) return -1;
    p++;
    if (sscanf(p, "%d:%d:%d", &h, &mi, &se) != 3) return -1;
    while (*p && *p != '.' && *p != 'Z' && *p != 'z' && *p != '+' && *p != '-') p++;
    if (*p == '.') {
        p++;
        int frac = 0, digits = 0;
        while (*p >= '0' && *p <= '9' && digits < 3) {
            frac = frac * 10 + (*p - '0');
            p++;
            digits++;
        }
        while (digits < 3) {
            frac *= 10;
            digits++;
        }
        ms = frac;
        while (*p >= '0' && *p <= '9') p++; /* skip extra frac digits */
    }
    int off_sec = 0;
    if (*p == 'Z' || *p == 'z') {
        off_sec = 0;
    } else if (*p == '+' || *p == '-') {
        int sign = (*p == '-') ? -1 : 1;
        p++;
        int oh = 0, om = 0;
        if (sscanf(p, "%d:%d", &oh, &om) < 1) return -1;
        off_sec = sign * (oh * 3600 + om * 60);
    }
    int64_t utc = mako_time_date(Y, Mo, D, h, mi, se, ms);
    if (utc < 0) return -1;
    return utc - (int64_t)off_sec * 1000;
}
/* Parse YYYY-MM-DD as UTC midnight. */
static inline int64_t mako_time_parse_date(MakoString s) {
    if (!s.data || s.len < 10) return -1;
    int Y = 0, Mo = 0, D = 0;
    char buf[16];
    size_t n = s.len < 15 ? s.len : 15;
    memcpy(buf, s.data, n);
    buf[n] = 0;
    if (sscanf(buf, "%d-%d-%d", &Y, &Mo, &D) != 3) return -1;
    return mako_time_date(Y, Mo, D, 0, 0, 0, 0);
}
/* Duration helpers (all in milliseconds unless named). */
static inline int64_t mako_duration_ms(int64_t n) { return n; }
static inline int64_t mako_duration_us_as_ms(int64_t us) { return us / 1000; }
static inline int64_t mako_duration_seconds(int64_t n) { return n * 1000; }
static inline int64_t mako_duration_minutes(int64_t n) { return n * 60 * 1000; }
static inline int64_t mako_duration_hours(int64_t n) { return n * 3600 * 1000; }
static inline int64_t mako_duration_days(int64_t n) { return n * 86400 * 1000; }
static inline int64_t mako_duration_to_seconds(int64_t ms) { return ms / 1000; }
static inline int64_t mako_duration_to_minutes(int64_t ms) { return ms / 60000; }
static inline int64_t mako_duration_to_hours(int64_t ms) { return ms / 3600000; }
/* Named fixed offsets (not full IANA TZDB). Returns seconds east of UTC, or
 * INT64_MIN-ish sentinel: unknown → -999999999. */
static inline int64_t mako_time_offset_named(MakoString name) {
    if (!name.data || name.len == 0) return -999999999;
    /* normalize uppercase compare */
    char buf[16];
    size_t n = name.len < 15 ? name.len : 15;
    for (size_t i = 0; i < n; i++) {
        char c = name.data[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        buf[i] = c;
    }
    buf[n] = 0;
    if (strcmp(buf, "UTC") == 0 || strcmp(buf, "GMT") == 0 || strcmp(buf, "Z") == 0) return 0;
    if (strcmp(buf, "EST") == 0) return -5 * 3600;
    if (strcmp(buf, "EDT") == 0) return -4 * 3600;
    if (strcmp(buf, "CST") == 0) return -6 * 3600;
    if (strcmp(buf, "CDT") == 0) return -5 * 3600;
    if (strcmp(buf, "MST") == 0) return -7 * 3600;
    if (strcmp(buf, "MDT") == 0) return -6 * 3600;
    if (strcmp(buf, "PST") == 0) return -8 * 3600;
    if (strcmp(buf, "PDT") == 0) return -7 * 3600;
    if (strcmp(buf, "CET") == 0) return 1 * 3600;
    if (strcmp(buf, "CEST") == 0) return 2 * 3600;
    if (strcmp(buf, "JST") == 0) return 9 * 3600;
    if (strcmp(buf, "KST") == 0) return 9 * 3600;
    if (strcmp(buf, "IST") == 0) return 5 * 3600 + 30 * 60; /* India */
    if (strcmp(buf, "AEST") == 0) return 10 * 3600;
    if (strcmp(buf, "AEDT") == 0) return 11 * 3600;
    return -999999999;
}
/* Format RFC3339-like with numeric offset: 2020-01-02T03:04:05.000+05:30 */
static inline MakoString mako_time_format_offset(int64_t unix_ms, int64_t offset_sec) {
    int64_t local_ms = unix_ms + offset_sec * 1000;
    struct tm tm;
    int ms = 0;
    mako_time_gm(local_ms, &tm, &ms);
    int off = (int)offset_sec;
    char sign = '+';
    if (off < 0) {
        sign = '-';
        off = -off;
    }
    int oh = off / 3600;
    int om = (off % 3600) / 60;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03d%c%02d:%02d",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                     ms, sign, oh, om);
    if (n < 0) n = 0;
    return mako_str_from_cstr(buf);
}
static inline int64_t mako_time_in_offset(int64_t unix_ms, int64_t offset_sec) {
    return unix_ms + offset_sec * 1000;
}

static inline MakoString mako_duration_string(int64_t ms) {
    int64_t sign = ms < 0 ? -1 : 1;
    int64_t v = ms < 0 ? -ms : ms;
    int64_t h = v / 3600000;
    int64_t m = (v % 3600000) / 60000;
    int64_t s = (v % 60000) / 1000;
    int64_t milli = v % 1000;
    char buf[64];
    int n;
    if (h > 0)
        n = snprintf(buf, sizeof(buf), "%s%lldh%lldm%llds", sign < 0 ? "-" : "",
                     (long long)h, (long long)m, (long long)s);
    else if (m > 0)
        n = snprintf(buf, sizeof(buf), "%s%lldm%llds", sign < 0 ? "-" : "",
                     (long long)m, (long long)s);
    else if (s > 0 && milli == 0)
        n = snprintf(buf, sizeof(buf), "%s%llds", sign < 0 ? "-" : "", (long long)s);
    else if (s > 0)
        n = snprintf(buf, sizeof(buf), "%s%lld.%03llds", sign < 0 ? "-" : "",
                     (long long)s, (long long)milli);
    else
        n = snprintf(buf, sizeof(buf), "%s%lldms", sign < 0 ? "-" : "", (long long)v);
    if (n < 0) n = 0;
    return mako_str_from_cstr(buf);
}

/* ---- full syscall package (portable OS primitives) ---- */
static inline int64_t mako_syscall_available(void) {
#if defined(MAKO_WASI)
    return 0;
#else
    return 1;
#endif
}
static inline int64_t mako_syscall_getpid(void) {
#if defined(_WIN32)
    return (int64_t)GetCurrentProcessId();
#elif defined(MAKO_WASI)
    return 1;
#else
    return (int64_t)getpid();
#endif
}
static inline int64_t mako_syscall_getppid(void) {
#if defined(_WIN32) || defined(MAKO_WASI)
    return 0;
#else
    return (int64_t)getppid();
#endif
}
static inline int64_t mako_syscall_getuid(void) {
#if defined(_WIN32) || defined(MAKO_WASI)
    return 0;
#else
    return (int64_t)getuid();
#endif
}
static inline int64_t mako_syscall_geteuid(void) {
#if defined(_WIN32) || defined(MAKO_WASI)
    return 0;
#else
    return (int64_t)geteuid();
#endif
}
static inline int64_t mako_syscall_getgid(void) {
#if defined(_WIN32) || defined(MAKO_WASI)
    return 0;
#else
    return (int64_t)getgid();
#endif
}
static inline int64_t mako_syscall_getegid(void) {
#if defined(_WIN32) || defined(MAKO_WASI)
    return 0;
#else
    return (int64_t)getegid();
#endif
}
static inline MakoString mako_syscall_hostname(void) {
    char buf[256];
#if defined(_WIN32)
    DWORD n = (DWORD)sizeof(buf);
    if (!GetComputerNameA(buf, &n)) return mako_str_from_cstr("localhost");
    return mako_str_from_cstr(buf);
#elif defined(MAKO_WASI)
    return mako_str_from_cstr("wasi");
#else
    if (gethostname(buf, sizeof(buf)) != 0) return mako_str_from_cstr("localhost");
    buf[sizeof(buf) - 1] = 0;
    return mako_str_from_cstr(buf);
#endif
}
static inline MakoString mako_syscall_uname_sysname(void) {
#if defined(_WIN32)
    return mako_str_from_cstr("Windows");
#elif defined(MAKO_WASI)
    return mako_str_from_cstr("WASI");
#else
    struct utsname u;
    if (uname(&u) != 0) return mako_str_from_cstr("unknown");
    return mako_str_from_cstr(u.sysname);
#endif
}
static inline MakoString mako_syscall_uname_release(void) {
#if defined(_WIN32) || defined(MAKO_WASI)
    return mako_str_from_cstr("");
#else
    struct utsname u;
    if (uname(&u) != 0) return mako_str_from_cstr("");
    return mako_str_from_cstr(u.release);
#endif
}
static inline MakoString mako_syscall_uname_machine(void) {
#if defined(_WIN32)
#if defined(_M_X64) || defined(__x86_64__)
    return mako_str_from_cstr("x86_64");
#elif defined(_M_ARM64) || defined(__aarch64__)
    return mako_str_from_cstr("arm64");
#else
    return mako_str_from_cstr("unknown");
#endif
#elif defined(MAKO_WASI)
    return mako_str_from_cstr("wasm32");
#else
    struct utsname u;
    if (uname(&u) != 0) return mako_str_from_cstr("");
    return mako_str_from_cstr(u.machine);
#endif
}
static inline MakoString mako_syscall_uname_json(void) {
    MakoString sys = mako_syscall_uname_sysname();
    MakoString rel = mako_syscall_uname_release();
    MakoString mach = mako_syscall_uname_machine();
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
                     "{\"sysname\":\"%.*s\",\"release\":\"%.*s\",\"machine\":\"%.*s\"}",
                     (int)sys.len, sys.data ? sys.data : "",
                     (int)rel.len, rel.data ? rel.data : "",
                     (int)mach.len, mach.data ? mach.data : "");
    mako_str_free(sys);
    mako_str_free(rel);
    mako_str_free(mach);
    if (n < 0) n = 0;
    return mako_str_from_cstr(buf);
}
static inline int64_t mako_syscall_errno(void) {
#if defined(MAKO_WASI)
    return 0;
#else
    return (int64_t)errno;
#endif
}
static inline MakoString mako_syscall_errno_str(void) {
#if defined(MAKO_WASI)
    return mako_str_from_cstr("ok");
#else
    return mako_str_from_cstr(strerror(errno));
#endif
}
static inline int64_t mako_syscall_kill(int64_t pid, int64_t sig) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)pid;
    (void)sig;
    return -1;
#else
    return kill((pid_t)pid, (int)sig) == 0 ? 0 : -1;
#endif
}
static inline int64_t mako_syscall_umask(int64_t mask) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)mask;
    return 0;
#else
    mode_t old = umask((mode_t)mask);
    return (int64_t)old;
#endif
}
static inline int64_t mako_syscall_pagesize(void) {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int64_t)si.dwPageSize;
#elif defined(MAKO_WASI)
    return 65536;
#else
    long p = sysconf(_SC_PAGESIZE);
    return p > 0 ? (int64_t)p : 4096;
#endif
}
static inline int64_t mako_syscall_ncpu(void) {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int64_t)si.dwNumberOfProcessors;
#elif defined(MAKO_WASI)
    return 1;
#else
#if defined(_SC_NPROCESSORS_ONLN)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) return (int64_t)n;
#endif
    /* Fallback when the constant is masked by strict C99 feature macros. */
    return 1;
#endif
}
static inline int64_t mako_syscall_isatty(int64_t fd) {
#if defined(_WIN32)
    return _isatty((int)fd) ? 1 : 0;
#elif defined(MAKO_WASI)
    (void)fd;
    return 0;
#else
    return isatty((int)fd) ? 1 : 0;
#endif
}
static int64_t mako_syscall_pipe_r = -1;
static int64_t mako_syscall_pipe_w = -1;
static inline int64_t mako_syscall_pipe(void) {
#if defined(_WIN32) || defined(MAKO_WASI)
    mako_syscall_pipe_r = -1;
    mako_syscall_pipe_w = -1;
    return -1;
#else
    int fds[2];
    if (pipe(fds) != 0) {
        mako_syscall_pipe_r = -1;
        mako_syscall_pipe_w = -1;
        return -1;
    }
    mako_syscall_pipe_r = fds[0];
    mako_syscall_pipe_w = fds[1];
    return 0;
#endif
}
static inline int64_t mako_syscall_pipe_read_fd(void) { return mako_syscall_pipe_r; }
static inline int64_t mako_syscall_pipe_write_fd(void) { return mako_syscall_pipe_w; }
static inline int64_t mako_syscall_close(int64_t fd) {
    if (fd < 0) return -1;
#if defined(_WIN32)
    return _close((int)fd) == 0 ? 0 : -1;
#elif defined(MAKO_WASI)
    (void)fd;
    return -1;
#else
    return close((int)fd) == 0 ? 0 : -1;
#endif
}
static inline int64_t mako_syscall_dup(int64_t fd) {
#if defined(_WIN32)
    return (int64_t)_dup((int)fd);
#elif defined(MAKO_WASI)
    (void)fd;
    return -1;
#else
    return (int64_t)dup((int)fd);
#endif
}
static inline int64_t mako_syscall_dup2(int64_t oldfd, int64_t newfd) {
#if defined(_WIN32)
    return _dup2((int)oldfd, (int)newfd) == 0 ? newfd : -1;
#elif defined(MAKO_WASI)
    (void)oldfd;
    (void)newfd;
    return -1;
#else
    return dup2((int)oldfd, (int)newfd) >= 0 ? newfd : -1;
#endif
}
static inline int64_t mako_syscall_write(int64_t fd, MakoString data) {
    if (fd < 0 || !data.data) return -1;
#if defined(_WIN32)
    int n = _write((int)fd, data.data, (unsigned)data.len);
    return n >= 0 ? (int64_t)n : -1;
#elif defined(MAKO_WASI)
    (void)fd;
    (void)data;
    return -1;
#else
    ssize_t n = write((int)fd, data.data, data.len);
    return n >= 0 ? (int64_t)n : -1;
#endif
}
static inline MakoString mako_syscall_read(int64_t fd, int64_t maxn) {
    if (fd < 0 || maxn <= 0) return mako_str_from_cstr("");
    if (maxn > 1 << 20) maxn = 1 << 20;
    char *buf = (char *)malloc((size_t)maxn + 1);
    if (!buf) return mako_str_from_cstr("");
#if defined(_WIN32)
    int n = _read((int)fd, buf, (unsigned)maxn);
#elif defined(MAKO_WASI)
    int n = -1;
    (void)fd;
#else
    ssize_t n = read((int)fd, buf, (size_t)maxn);
#endif
    if (n <= 0) {
        free(buf);
        return mako_str_from_cstr("");
    }
    buf[n] = 0;
    MakoString out = {buf, (size_t)n};
    return out;
}
static inline int64_t mako_syscall_access(MakoString path, int64_t mode) {
    /* mode: 0=F_OK, 1=X_OK, 2=W_OK, 4=R_OK (unix values) */
    if (!path.data) return -1;
#if defined(_WIN32)
    int m = 0;
    if (mode & 4) m |= 04;
    if (mode & 2) m |= 02;
    return _access(path.data, m) == 0 ? 0 : -1;
#elif defined(MAKO_WASI)
    (void)path;
    (void)mode;
    return -1;
#else
    return access(path.data, (int)mode) == 0 ? 0 : -1;
#endif
}
static inline int64_t mako_syscall_chmod(MakoString path, int64_t mode) {
    if (!path.data) return -1;
#if defined(_WIN32)
    return _chmod(path.data, (int)mode) == 0 ? 0 : -1;
#elif defined(MAKO_WASI)
    (void)path;
    (void)mode;
    return -1;
#else
    return chmod(path.data, (mode_t)mode) == 0 ? 0 : -1;
#endif
}
static inline int64_t mako_syscall_symlink(MakoString target, MakoString linkpath) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)target;
    (void)linkpath;
    return -1;
#else
    if (!target.data || !linkpath.data) return -1;
    return symlink(target.data, linkpath.data) == 0 ? 0 : -1;
#endif
}
static inline MakoString mako_syscall_readlink(MakoString path) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)path;
    return mako_str_from_cstr("");
#else
    if (!path.data) return mako_str_from_cstr("");
    char buf[4096];
    ssize_t n = readlink(path.data, buf, sizeof(buf) - 1);
    if (n < 0) return mako_str_from_cstr("");
    buf[n] = 0;
    return mako_str_from_cstr(buf);
#endif
}
static inline int64_t mako_syscall_getrlimit_nofile(void) {
#if defined(_WIN32) || defined(MAKO_WASI)
    return -1;
#else
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) return -1;
    return (int64_t)rl.rlim_cur;
#endif
}
static inline int64_t mako_syscall_setrlimit_nofile(int64_t n) {
#if defined(_WIN32) || defined(MAKO_WASI)
    (void)n;
    return -1;
#else
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) return -1;
    rl.rlim_cur = (rlim_t)n;
    if (rl.rlim_cur > rl.rlim_max) rl.rlim_cur = rl.rlim_max;
    return setrlimit(RLIMIT_NOFILE, &rl) == 0 ? 0 : -1;
#endif
}

/* ---- sync (mutex) ---- */

typedef struct {
    pthread_mutex_t mu;
} MakoMutex;

static inline MakoMutex *mako_mutex_new(void) {
    MakoMutex *m = (MakoMutex *)malloc(sizeof(MakoMutex));
    if (!m) mako_abort("mutex_new OOM");
    pthread_mutex_init(&m->mu, NULL);
    return m;
}

static inline void mako_mutex_lock(MakoMutex *m) {
    if (m) pthread_mutex_lock(&m->mu);
}

static inline void mako_mutex_unlock(MakoMutex *m) {
    if (m) pthread_mutex_unlock(&m->mu);
}

/* ---- RWMutex (readers–writer; portable over pthread / Win CS shims) ---- */

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int64_t readers;      /* active readers */
    int64_t writer;       /* 1 if a writer holds the lock */
    int64_t write_waiters; /* writers waiting (prefer writers) */
} MakoRWMutex;

static inline MakoRWMutex *mako_rwmutex_new(void) {
    MakoRWMutex *m = (MakoRWMutex *)malloc(sizeof(MakoRWMutex));
    if (!m) mako_abort("rwmutex_new OOM");
    pthread_mutex_init(&m->mu, NULL);
    pthread_cond_init(&m->cv, NULL);
    m->readers = 0;
    m->writer = 0;
    m->write_waiters = 0;
    return m;
}

static inline void mako_rwmutex_rlock(MakoRWMutex *m) {
    if (!m) return;
    pthread_mutex_lock(&m->mu);
    while (m->writer || m->write_waiters > 0) {
        pthread_cond_wait(&m->cv, &m->mu);
    }
    m->readers++;
    pthread_mutex_unlock(&m->mu);
}

static inline void mako_rwmutex_runlock(MakoRWMutex *m) {
    if (!m) return;
    pthread_mutex_lock(&m->mu);
    if (m->readers > 0) m->readers--;
    if (m->readers == 0) pthread_cond_broadcast(&m->cv);
    pthread_mutex_unlock(&m->mu);
}

static inline void mako_rwmutex_lock(MakoRWMutex *m) {
    if (!m) return;
    pthread_mutex_lock(&m->mu);
    m->write_waiters++;
    while (m->writer || m->readers > 0) {
        pthread_cond_wait(&m->cv, &m->mu);
    }
    m->write_waiters--;
    m->writer = 1;
    pthread_mutex_unlock(&m->mu);
}

static inline void mako_rwmutex_unlock(MakoRWMutex *m) {
    if (!m) return;
    pthread_mutex_lock(&m->mu);
    m->writer = 0;
    pthread_cond_broadcast(&m->cv);
    pthread_mutex_unlock(&m->mu);
}

/* ---- log (structured-ish key=value) ---- */

static inline void mako_log_debug(MakoString msg) {
#if defined(MAKO_LOG_H)
    mako_slog_debug(msg);
#else
    fprintf(stderr, "[%lld debug] ", (long long)mako_now_ms());
    fwrite(msg.data, 1, msg.len, stderr);
    fputc('\n', stderr);
#endif
}

#if !defined(MAKO_LOG_H)
static inline void mako_log_kv(MakoString level, MakoString key, MakoString val) {
    fprintf(stderr, "[%lld ", (long long)mako_now_ms());
    fwrite(level.data, 1, level.len, stderr);
    fputc(']', stderr);
    fputc(' ', stderr);
    fwrite(key.data, 1, key.len, stderr);
    fputc('=', stderr);
    fwrite(val.data, 1, val.len, stderr);
    fputc('\n', stderr);
}
#endif

/* ---- crypto: random bytes (OS CSPRNG) ---- */

static inline MakoString mako_random_bytes(int64_t n) {
    if (n <= 0) return mako_str_from_cstr("");
    if (n > 1024 * 1024) mako_abort("random_bytes: too large");
    char *d = (char *)malloc((size_t)n + 1);
    if (!d) mako_abort("random_bytes OOM");
#if defined(__APPLE__)
    arc4random_buf(d, (size_t)n);
#elif defined(_WIN32)
    {
        /* CryptGenRandom via advapi32 is heavy; prefer BCrypt if available later.
         * Use RtlGenRandom (SystemFunction036) when present, else weak fallback. */
        HMODULE adv = LoadLibraryA("advapi32.dll");
        typedef BOOLEAN(APIENTRY * RtlGenRandomFn)(PVOID, ULONG);
        RtlGenRandomFn fn = NULL;
        if (adv) fn = (RtlGenRandomFn)GetProcAddress(adv, "SystemFunction036");
        if (fn && fn(d, (ULONG)n)) {
            /* ok */
        } else {
            for (int64_t i = 0; i < n; i++) d[i] = (char)(rand() & 0xff);
        }
        if (adv) FreeLibrary(adv);
    }
#else
    {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) mako_abort("random_bytes: cannot open /dev/urandom");
        size_t got = 0;
        while (got < (size_t)n) {
            ssize_t r = read(fd, d + got, (size_t)n - got);
            if (r <= 0) {
                close(fd);
                mako_abort("random_bytes: /dev/urandom read failed");
            }
            got += (size_t)r;
        }
        close(fd);
    }
#endif
    d[n] = 0;
    return (MakoString){d, (size_t)n};
}

static inline int64_t mako_random_int(int64_t lo, int64_t hi) {
    if (hi <= lo) return lo;
    MakoString b = mako_random_bytes(8);
    if (b.len < 8) return lo;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | (unsigned char)b.data[i];
    uint64_t span = (uint64_t)(hi - lo);
    return lo + (int64_t)(v % span);
}

/* ---- bufio: buffered reader / writer ---- */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    size_t pos;
    FILE *fp; /* optional file; NULL = string-backed */
    int owns_buf;
} MakoBufReader;

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    FILE *fp;
} MakoBufWriter;

static inline MakoBufReader *mako_buf_reader_new(MakoString path) {
    MakoBufReader *r = (MakoBufReader *)calloc(1, sizeof(MakoBufReader));
    if (!r) mako_abort("buf_reader_new OOM");
    const char *p = path.data ? path.data : "";
    r->fp = fopen(p, "rb");
    if (!r->fp) {
        free(r);
        return NULL;
    }
    r->cap = 4096;
    r->buf = (char *)malloc(r->cap);
    if (!r->buf) mako_abort("buf_reader_new OOM");
    r->owns_buf = 1;
    return r;
}

static inline MakoBufReader *mako_buf_reader_from_string(MakoString s) {
    MakoBufReader *r = (MakoBufReader *)calloc(1, sizeof(MakoBufReader));
    if (!r) mako_abort("buf_reader_from_string OOM");
    r->cap = s.len;
    r->len = s.len;
    r->buf = (char *)malloc(s.len + 1);
    if (!r->buf) mako_abort("buf_reader_from_string OOM");
    if (s.len) memcpy(r->buf, s.data, s.len);
    r->buf[s.len] = 0;
    r->owns_buf = 1;
    r->fp = NULL;
    return r;
}

static inline int mako_buf_reader_fill(MakoBufReader *r) {
    if (!r) return 0;
    if (r->pos < r->len) return 1;
    if (!r->fp) return 0;
    size_t n = fread(r->buf, 1, r->cap, r->fp);
    r->len = n;
    r->pos = 0;
    return n > 0;
}

static inline MakoString mako_buf_read_line(MakoBufReader *r) {
    if (!r) return mako_str_from_cstr("");
    MakoStrBuilder *b = mako_str_builder_new();
    for (;;) {
        if (r->pos >= r->len && !mako_buf_reader_fill(r)) break;
        if (r->pos >= r->len) break;
        char c = r->buf[r->pos++];
        if (c == '\n') break;
        if (c == '\r') {
            if (r->pos >= r->len) mako_buf_reader_fill(r);
            if (r->pos < r->len && r->buf[r->pos] == '\n') r->pos++;
            break;
        }
        mako_str_builder_write_byte(b, (int64_t)(unsigned char)c);
    }
    MakoString out = mako_str_builder_string(b);
    free(b->data);
    free(b);
    return out;
}

static inline MakoString mako_buf_read(MakoBufReader *r, int64_t n) {
    if (!r || n <= 0) return mako_str_from_cstr("");
    if (n > 1024 * 1024) n = 1024 * 1024;
    char *d = (char *)malloc((size_t)n + 1);
    if (!d) mako_abort("buf_read OOM");
    size_t got = 0;
    while (got < (size_t)n) {
        if (r->pos >= r->len && !mako_buf_reader_fill(r)) break;
        if (r->pos >= r->len) break;
        size_t avail = r->len - r->pos;
        size_t take = avail;
        if (take > (size_t)n - got) take = (size_t)n - got;
        memcpy(d + got, r->buf + r->pos, take);
        r->pos += take;
        got += take;
    }
    d[got] = 0;
    return (MakoString){d, got};
}

static inline int64_t mako_buf_reader_close(MakoBufReader *r) {
    if (!r) return 0;
    if (r->fp) fclose(r->fp);
    if (r->owns_buf && r->buf) free(r->buf);
    free(r);
    return 0;
}

static inline MakoBufWriter *mako_buf_writer_new(MakoString path) {
    MakoBufWriter *w = (MakoBufWriter *)calloc(1, sizeof(MakoBufWriter));
    if (!w) mako_abort("buf_writer_new OOM");
    const char *p = path.data ? path.data : "";
    w->fp = fopen(p, "wb");
    if (!w->fp) {
        free(w);
        return NULL;
    }
    w->cap = 4096;
    w->buf = (char *)malloc(w->cap);
    if (!w->buf) mako_abort("buf_writer_new OOM");
    return w;
}

static inline int64_t mako_buf_flush(MakoBufWriter *w) {
    if (!w || !w->fp) return -1;
    if (w->len) {
        if (fwrite(w->buf, 1, w->len, w->fp) != w->len) return -1;
        w->len = 0;
    }
    return fflush(w->fp) == 0 ? 0 : -1;
}

static inline int64_t mako_buf_write(MakoBufWriter *w, MakoString s) {
    if (!w || !w->fp) return -1;
    size_t i = 0;
    while (i < s.len) {
        if (w->len >= w->cap) {
            if (mako_buf_flush(w) < 0) return -1;
        }
        size_t space = w->cap - w->len;
        size_t take = s.len - i;
        if (take > space) take = space;
        memcpy(w->buf + w->len, s.data + i, take);
        w->len += take;
        i += take;
    }
    return (int64_t)s.len;
}

static inline int64_t mako_buf_write_byte(MakoBufWriter *w, int64_t v) {
    if (v < 0 || v > 255) return -1;
    char c = (char)(uint8_t)v;
    MakoString s = {&c, 1};
    /* write one byte without relying on null-terminated string */
    if (!w || !w->fp) return -1;
    if (w->len >= w->cap && mako_buf_flush(w) < 0) return -1;
    w->buf[w->len++] = c;
    (void)s;
    return 1;
}

static inline int64_t mako_buf_writer_close(MakoBufWriter *w) {
    if (!w) return 0;
    int64_t rc = mako_buf_flush(w);
    if (w->fp) fclose(w->fp);
    free(w->buf);
    free(w);
    return rc;
}

/* ---- WaitGroup (Go-like) ---- */

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int64_t count;
} MakoWaitGroup;

static inline MakoWaitGroup *mako_wait_group_new(void) {
    MakoWaitGroup *wg = (MakoWaitGroup *)malloc(sizeof(MakoWaitGroup));
    if (!wg) mako_abort("wait_group_new OOM");
    pthread_mutex_init(&wg->mu, NULL);
    pthread_cond_init(&wg->cv, NULL);
    wg->count = 0;
    return wg;
}

static inline void mako_wait_group_add(MakoWaitGroup *wg, int64_t delta) {
    if (!wg) return;
    pthread_mutex_lock(&wg->mu);
    wg->count += delta;
    if (wg->count < 0) wg->count = 0;
    if (wg->count == 0) pthread_cond_broadcast(&wg->cv);
    pthread_mutex_unlock(&wg->mu);
}

static inline void mako_wait_group_done(MakoWaitGroup *wg) {
    mako_wait_group_add(wg, -1);
}

static inline void mako_wait_group_wait(MakoWaitGroup *wg) {
    if (!wg) return;
    pthread_mutex_lock(&wg->mu);
    while (wg->count > 0) pthread_cond_wait(&wg->cv, &wg->mu);
    pthread_mutex_unlock(&wg->mu);
}

/* ------------------------------------------------------------------------- */
/* File-system watch — one abstraction over kqueue (macOS/BSD) and inotify     */
/* (Linux). Watch files or directories; `watch_poll` blocks up to a timeout    */
/* and returns the path that changed (or "" on timeout). Pairs with SIGHUP for */
/* config / servers-file reloads.                                              */
/* ------------------------------------------------------------------------- */

#define MAKO_MAX_WATCH 128
#define MAKO_WATCH_PATH 256

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/event.h>
#include <sys/time.h>
#define MAKO_WATCH_KQUEUE 1
#elif defined(__linux__)
#include <sys/inotify.h>
#include <poll.h>
#define MAKO_WATCH_INOTIFY 1
#endif

#if defined(MAKO_WATCH_KQUEUE) || defined(MAKO_WATCH_INOTIFY)
typedef struct {
    int fd; /* kqueue or inotify fd */
    int handles[MAKO_MAX_WATCH]; /* per-path open fd (kqueue) or watch descriptor (inotify) */
    char paths[MAKO_MAX_WATCH][MAKO_WATCH_PATH];
    int count;
} MakoWatcher;

static inline void *mako_watch_new(void) {
    MakoWatcher *w = (MakoWatcher *)calloc(1, sizeof(MakoWatcher));
    if (!w) return NULL;
#if defined(MAKO_WATCH_KQUEUE)
    w->fd = kqueue();
#else
    w->fd = inotify_init1(IN_NONBLOCK);
#endif
    if (w->fd < 0) { free(w); return NULL; }
    return w;
}

static inline int64_t mako_watch_add(void *wp, MakoString path) {
    MakoWatcher *w = (MakoWatcher *)wp;
    if (!w || w->count >= MAKO_MAX_WATCH || path.len == 0 || path.len >= MAKO_WATCH_PATH)
        return -1;
    char p[MAKO_WATCH_PATH];
    memcpy(p, path.data, path.len);
    p[path.len] = 0;
#if defined(MAKO_WATCH_KQUEUE)
#ifdef O_EVTONLY
    int h = open(p, O_EVTONLY);
#else
    int h = open(p, O_RDONLY);
#endif
    if (h < 0) return -1;
    struct kevent ev;
    EV_SET(&ev, h, EVFILT_VNODE, EV_ADD | EV_CLEAR,
           NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_EXTEND | NOTE_ATTRIB, 0, NULL);
    if (kevent(w->fd, &ev, 1, NULL, 0, NULL) < 0) { close(h); return -1; }
#else
    int h = inotify_add_watch(w->fd, p,
                              IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_TO |
                              IN_MOVED_FROM | IN_ATTRIB | IN_MOVE_SELF | IN_DELETE_SELF);
    if (h < 0) return -1;
#endif
    memcpy(w->paths[w->count], p, path.len + 1);
    w->handles[w->count] = h;
    w->count++;
    return 0;
}

/* Block up to `timeout_ms`; return the changed path, or "" on timeout. */
static inline MakoString mako_watch_poll(void *wp, int64_t timeout_ms) {
    MakoWatcher *w = (MakoWatcher *)wp;
    if (!w) return mako_str_from_cstr("");
#if defined(MAKO_WATCH_KQUEUE)
    struct kevent ev;
    struct timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
    int n = kevent(w->fd, NULL, 0, &ev, 1, &ts);
    if (n <= 0) return mako_str_from_cstr("");
    for (int i = 0; i < w->count; i++)
        if (w->handles[i] == (int)ev.ident) return mako_str_from_cstr(w->paths[i]);
#else
    struct pollfd pfd;
    pfd.fd = w->fd;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, (int)timeout_ms);
    if (pr <= 0) return mako_str_from_cstr("");
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len = read(w->fd, buf, sizeof(buf));
    if (len <= 0) return mako_str_from_cstr("");
    struct inotify_event *e = (struct inotify_event *)buf;
    for (int i = 0; i < w->count; i++)
        if (w->handles[i] == e->wd) return mako_str_from_cstr(w->paths[i]);
#endif
    return mako_str_from_cstr("");
}

static inline int64_t mako_watch_close(void *wp) {
    MakoWatcher *w = (MakoWatcher *)wp;
    if (!w) return -1;
#if defined(MAKO_WATCH_KQUEUE)
    for (int i = 0; i < w->count; i++) close(w->handles[i]);
#endif
    close(w->fd);
    free(w);
    return 0;
}

static inline int64_t mako_watch_available(void) { return 1; }
#else /* no file-watch backend */
static inline void *mako_watch_new(void) { return NULL; }
static inline int64_t mako_watch_add(void *wp, MakoString path) {
    (void)wp; (void)path; return -1;
}
static inline MakoString mako_watch_poll(void *wp, int64_t timeout_ms) {
    (void)wp; (void)timeout_ms; return mako_str_from_cstr("");
}
static inline int64_t mako_watch_close(void *wp) { (void)wp; return -1; }
static inline int64_t mako_watch_available(void) { return 0; }
#endif

#ifdef __cplusplus
}
#endif

#endif /* MAKO_STDLIB_H */
