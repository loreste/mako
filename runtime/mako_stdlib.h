/* Mako standard library — strings, strconv, fmt, path/fs/os, math, collections, time, sync.
 * Included via mako_std.h. Cross-platform (Win/Mac/Linux); memory-safe (bounds + OOM abort).
 */
#ifndef MAKO_STDLIB_H
#define MAKO_STDLIB_H

#include "mako_rt.h"
#include <ctype.h>
#include <math.h>
#include <limits.h>

#include <sys/stat.h>
#include <fcntl.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#include <dirent.h>
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
    fprintf(stderr, "[%lld debug] ", (long long)mako_now_ms());
    fwrite(msg.data, 1, msg.len, stderr);
    fputc('\n', stderr);
}

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
