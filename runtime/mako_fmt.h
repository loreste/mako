/* fmt / print — Go-inspired formatting and output.
 *
 * String verbs: %% %s %v %t %q %x %X (byte hex of string) %f %g
 * Int verbs (fmt_sprintf_d / format_*): %d %i %v %b %o %x %X
 *   optional: flags (+ - # 0), width (e.g. %08x, %#x → 0x…)
 *
 * Builtins: format_int_{hex,bin,oct,base}, parse_int_*, fmt_sprintf*
 * Packs: std/fmt · std/print · std/strconv
 */
#ifndef MAKO_FMT_H
#define MAKO_FMT_H

#include "mako_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void mako_fmt_append(
    char **buf, size_t *len, size_t *cap, const char *s, size_t n
) {
    if (*len + n + 1 > *cap) {
        size_t nc = *cap ? *cap * 2 : 64;
        while (nc < *len + n + 1) nc *= 2;
        char *nb = (char *)realloc(*buf, nc);
        if (!nb) return;
        *buf = nb;
        *cap = nc;
    }
    if (n && s) memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = 0;
}

static inline void mako_fmt_appends(char **buf, size_t *len, size_t *cap, const char *s) {
    mako_fmt_append(buf, len, cap, s, s ? strlen(s) : 0);
}

/* ---- Integer base format / parse (decimal, hex, bin, oct, base 2–36) ---- */

static inline MakoString mako_format_uint_base(uint64_t n, int base, int upper) {
    if (base < 2 || base > 36) base = 10;
    char tmp[72];
    int i = 0;
    if (n == 0) tmp[i++] = '0';
    else {
        while (n > 0 && i < 70) {
            int d = (int)(n % (uint64_t)base);
            n /= (uint64_t)base;
            tmp[i++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + (d - 10));
        }
    }
    /* reverse */
    for (int a = 0, b = i - 1; a < b; a++, b--) {
        char t = tmp[a];
        tmp[a] = tmp[b];
        tmp[b] = t;
    }
    tmp[i] = 0;
    return mako_str_from_cstr(tmp);
}

static inline MakoString mako_format_int_base(int64_t n, int64_t base) {
    int b = (int)base;
    if (b < 2 || b > 36) b = 10;
    if (n < 0 && b == 10) {
        uint64_t u = (uint64_t)(-(n + 1)) + 1; /* careful with INT64_MIN */
        if (n == INT64_MIN) {
            MakoString body = mako_format_uint_base((uint64_t)INT64_MAX + 1, b, 0);
            size_t nlen = body.len + 1;
            char *d = (char *)malloc(nlen + 1);
            d[0] = '-';
            memcpy(d + 1, body.data, body.len);
            d[nlen] = 0;
            mako_str_free(body);
            return (MakoString){d, nlen};
        }
        MakoString body = mako_format_uint_base((uint64_t)(-n), b, 0);
        size_t nlen = body.len + 1;
        char *d = (char *)malloc(nlen + 1);
        d[0] = '-';
        memcpy(d + 1, body.data, body.len);
        d[nlen] = 0;
        mako_str_free(body);
        return (MakoString){d, nlen};
    }
    return mako_format_uint_base((uint64_t)n, b, 0);
}

static inline MakoString mako_format_int_hex(int64_t n) {
    return mako_format_uint_base((uint64_t)n, 16, 0);
}
static inline MakoString mako_format_int_hex_upper(int64_t n) {
    return mako_format_uint_base((uint64_t)n, 16, 1);
}
static inline MakoString mako_format_int_bin(int64_t n) {
    return mako_format_uint_base((uint64_t)n, 2, 0);
}
static inline MakoString mako_format_int_oct(int64_t n) {
    return mako_format_uint_base((uint64_t)n, 8, 0);
}
static inline MakoString mako_format_int_dec(int64_t n) {
    return mako_format_int_base(n, 10);
}

/* Zero-pad / width: pad left with '0' or ' ' to at least width chars. */
static inline MakoString mako_format_pad(MakoString s, int64_t width, int zero) {
    if (width <= 0 || (int64_t)s.len >= width) return mako_str_clone(s);
    size_t w = (size_t)width;
    char *d = (char *)malloc(w + 1);
    size_t pad = w - s.len;
    char pc = zero ? '0' : ' ';
    /* keep leading '-' for zero pad after sign */
    size_t o = 0;
    if (zero && s.len > 0 && s.data[0] == '-' && pad > 0) {
        d[o++] = '-';
        memset(d + o, '0', pad);
        o += pad;
        if (s.len > 1) memcpy(d + o, s.data + 1, s.len - 1);
        o += s.len - 1;
    } else {
        memset(d, pc, pad);
        if (s.len) memcpy(d + pad, s.data, s.len);
        o = w;
    }
    d[o] = 0;
    return (MakoString){d, o};
}

static inline MakoString mako_format_int_hex_pad(int64_t n, int64_t width) {
    MakoString h = mako_format_int_hex(n);
    MakoString out = mako_format_pad(h, width, 1);
    mako_str_free(h);
    return out;
}

static inline MakoString mako_format_int_hex_prefix(int64_t n) {
    MakoString h = mako_format_int_hex(n);
    size_t nlen = h.len + 2;
    char *d = (char *)malloc(nlen + 1);
    d[0] = '0';
    d[1] = 'x';
    if (h.len) memcpy(d + 2, h.data, h.len);
    d[nlen] = 0;
    mako_str_free(h);
    return (MakoString){d, nlen};
}

static inline int mako_digit_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

static inline MakoResultInt mako_parse_int_base(MakoString s, int64_t base) {
    int b = (int)base;
    /* base 0 = auto-detect prefix (0x/0b/0o); else 2..36 */
    if (b != 0 && (b < 2 || b > 36)) {
        return mako_err_int(mako_str_from_cstr("parse_int_base: base must be 0 or 2..36"));
    }
    const char *p = s.data ? s.data : "";
    size_t n = s.len;
    size_t i = 0;
    while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
    int neg = 0;
    if (i < n && (p[i] == '-' || p[i] == '+')) {
        neg = p[i] == '-';
        i++;
    }
    /* optional 0x / 0b / 0o prefixes when base is 0 or matching */
    int auto_base = (b == 0);
    if (auto_base) b = 10;
    if (i + 1 < n && p[i] == '0') {
        char x = p[i + 1];
        if ((x == 'x' || x == 'X') && (auto_base || base == 16)) {
            b = 16;
            i += 2;
        } else if ((x == 'b' || x == 'B') && (auto_base || base == 2)) {
            b = 2;
            i += 2;
        } else if ((x == 'o' || x == 'O') && (auto_base || base == 8)) {
            b = 8;
            i += 2;
        } else if (auto_base && x >= '0' && x <= '7') {
            b = 8; /* leading 0 → octal like Go ParseInt base 0 */
            /* keep the 0 as digit */
        }
    }
    if (i >= n) return mako_err_int(mako_str_from_cstr("parse_int_base: empty"));
    uint64_t u = 0;
    int any = 0;
    for (; i < n; i++) {
        int d = mako_digit_val(p[i]);
        if (d < 0 || d >= b) break;
        any = 1;
        u = u * (uint64_t)b + (uint64_t)d;
    }
    while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
    if (!any || i != n) return mako_err_int(mako_str_from_cstr("parse_int_base: invalid"));
    int64_t v = neg ? -(int64_t)u : (int64_t)u;
    return mako_ok_int(v);
}

static inline MakoResultInt mako_parse_int_hex(MakoString s) {
    return mako_parse_int_base(s, 16);
}
static inline MakoResultInt mako_parse_int_bin(MakoString s) {
    return mako_parse_int_base(s, 2);
}
static inline MakoResultInt mako_parse_int_oct(MakoString s) {
    return mako_parse_int_base(s, 8);
}
static inline MakoResultInt mako_parse_int_auto(MakoString s) {
    return mako_parse_int_base(s, 0); /* 0x / 0b / 0o / decimal */
}

/* Format int with verb + optional flags/width: returns owned string for that verb. */
static inline MakoString mako_fmt_format_int_verb(
    int64_t n, char verb, int sharp, int zero, int width, int plus
) {
    MakoString body;
    int upper = 0;
    if (verb == 'X') {
        upper = 1;
        verb = 'x';
    }
    switch (verb) {
    case 'b':
        body = mako_format_int_bin(n);
        break;
    case 'o':
        body = mako_format_int_oct(n);
        break;
    case 'x':
        body = upper ? mako_format_int_hex_upper(n) : mako_format_int_hex(n);
        break;
    case 'd':
    case 'i':
    case 'v':
    default:
        body = mako_format_int_dec(n);
        break;
    }
    /* + for positive decimal */
    if (plus && (verb == 'd' || verb == 'i' || verb == 'v') && n >= 0) {
        size_t nlen = body.len + 1;
        char *d = (char *)malloc(nlen + 1);
        d[0] = '+';
        if (body.len) memcpy(d + 1, body.data, body.len);
        d[nlen] = 0;
        mako_str_free(body);
        body = (MakoString){d, nlen};
    }
    /* # prefix */
    if (sharp) {
        const char *pre = NULL;
        if (verb == 'x') pre = upper ? "0X" : "0x";
        else if (verb == 'o' && (body.len == 0 || body.data[0] != '0')) pre = "0";
        else if (verb == 'b') pre = "0b";
        if (pre) {
            size_t pl = strlen(pre);
            size_t nlen = body.len + pl;
            char *d = (char *)malloc(nlen + 1);
            memcpy(d, pre, pl);
            if (body.len) memcpy(d + pl, body.data, body.len);
            d[nlen] = 0;
            mako_str_free(body);
            body = (MakoString){d, nlen};
        }
    }
    if (width > 0) {
        MakoString padded = mako_format_pad(body, width, zero);
        mako_str_free(body);
        return padded;
    }
    return body;
}

/* Quote string like Go %q (minimal: double-quoted, escape \ " and controls). */
static inline void mako_fmt_append_quoted(
    char **buf, size_t *len, size_t *cap, const char *s, size_t n
) {
    mako_fmt_appends(buf, len, cap, "\"");
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\' || c == '"') {
            char esc[2] = {'\\', (char)c};
            mako_fmt_append(buf, len, cap, esc, 2);
        } else if (c == '\n') {
            mako_fmt_append(buf, len, cap, "\\n", 2);
        } else if (c == '\r') {
            mako_fmt_append(buf, len, cap, "\\r", 2);
        } else if (c == '\t') {
            mako_fmt_append(buf, len, cap, "\\t", 2);
        } else if (c < 32 || c == 127) {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "\\x%02x", c);
            mako_fmt_appends(buf, len, cap, tmp);
        } else {
            mako_fmt_append(buf, len, cap, (const char *)&c, 1);
        }
    }
    mako_fmt_appends(buf, len, cap, "\"");
}

static inline void mako_fmt_append_hex(
    char **buf, size_t *len, size_t *cap, const char *s, size_t n, int upper
) {
    static const char *lo = "0123456789abcdef";
    static const char *hi = "0123456789ABCDEF";
    const char *T = upper ? hi : lo;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        char pair[2] = {T[c >> 4], T[c & 15]};
        mako_fmt_append(buf, len, cap, pair, 2);
    }
}

/* Parse flags/width after '%'; advances *pi to verb char index. */
static inline char mako_fmt_parse_spec(
    const char *p, size_t n, size_t *pi, int *sharp, int *zero, int *width, int *plus, int *minus
) {
    size_t i = *pi;
    *sharp = *zero = *width = *plus = *minus = 0;
    if (i >= n || p[i] != '%') return 0;
    i++;
    while (i < n) {
        char c = p[i];
        if (c == '#') {
            *sharp = 1;
            i++;
        } else if (c == '0') {
            *zero = 1;
            i++;
        } else if (c == '+') {
            *plus = 1;
            i++;
        } else if (c == '-') {
            *minus = 1;
            i++;
        } else if (c == ' ') {
            i++;
        } else
            break;
    }
    if (i < n && p[i] >= '1' && p[i] <= '9') {
        int w = 0;
        while (i < n && p[i] >= '0' && p[i] <= '9') {
            w = w * 10 + (p[i] - '0');
            i++;
        }
        *width = w;
    }
    if (i >= n) {
        *pi = i;
        return 0;
    }
    char verb = p[i];
    *pi = i;
    return verb;
}

/* Core: format fmt using args[0..nargs) as successive verb values (strings). */
static inline MakoString mako_fmt_sprintf_args(
    MakoString fmt, MakoString *args, int nargs
) {
    const char *p = fmt.data ? fmt.data : "";
    size_t n = fmt.len;
    char *out = NULL;
    size_t len = 0, cap = 0;
    int ai = 0;
    size_t i = 0;
    while (i < n) {
        if (p[i] != '%' || i + 1 >= n) {
            mako_fmt_append(&out, &len, &cap, p + i, 1);
            i++;
            continue;
        }
        size_t si = i;
        int sharp = 0, zero = 0, width = 0, plus = 0, minus = 0;
        char v = mako_fmt_parse_spec(p, n, &si, &sharp, &zero, &width, &plus, &minus);
        if (v == '%') {
            mako_fmt_append(&out, &len, &cap, "%", 1);
            i = si + 1;
            continue;
        }
        if (!v) {
            mako_fmt_append(&out, &len, &cap, p + i, 1);
            i++;
            continue;
        }
        const char *arg = "";
        size_t alen = 0;
        if (ai < nargs && args) {
            arg = args[ai].data ? args[ai].data : "";
            alen = args[ai].len;
            ai++;
        }
        if (v == 's' || v == 'v' || v == 'd' || v == 'i' || v == 't' || v == 'g' || v == 'f'
            || v == 'b' || v == 'o') {
            /* string args already preformatted for int bases when caller used format_* */
            if (width > 0) {
                MakoString tmp = {(char *)arg, alen};
                MakoString pad = mako_format_pad(tmp, width, zero && !minus);
                mako_fmt_append(&out, &len, &cap, pad.data, pad.len);
                mako_str_free(pad);
            } else {
                mako_fmt_append(&out, &len, &cap, arg, alen);
            }
            i = si + 1;
        } else if (v == 'q') {
            mako_fmt_append_quoted(&out, &len, &cap, arg, alen);
            i = si + 1;
        } else if (v == 'x' || v == 'X') {
            /* byte-hex of string payload */
            mako_fmt_append_hex(&out, &len, &cap, arg, alen, v == 'X');
            i = si + 1;
        } else {
            mako_fmt_append(&out, &len, &cap, p + i, 1);
            i++;
        }
        (void)sharp;
        (void)plus;
    }
    if (!out) return mako_str_from_cstr("");
    return (MakoString){out, len};
}

static inline MakoString mako_fmt_sprintf1(MakoString fmt, MakoString a0) {
    MakoString args[1] = {a0};
    return mako_fmt_sprintf_args(fmt, args, 1);
}

static inline MakoString mako_fmt_sprintf2(MakoString fmt, MakoString a0, MakoString a1) {
    MakoString args[2] = {a0, a1};
    return mako_fmt_sprintf_args(fmt, args, 2);
}

static inline MakoString mako_fmt_sprintf3(
    MakoString fmt, MakoString a0, MakoString a1, MakoString a2
) {
    MakoString args[3] = {a0, a1, a2};
    return mako_fmt_sprintf_args(fmt, args, 3);
}

static inline MakoString mako_fmt_sprintf4(
    MakoString fmt, MakoString a0, MakoString a1, MakoString a2, MakoString a3
) {
    MakoString args[4] = {a0, a1, a2, a3};
    return mako_fmt_sprintf_args(fmt, args, 4);
}

/* Sprint: join args with spaces (Go Sprint). */
static inline MakoString mako_fmt_sprint1(MakoString a0) {
    return mako_str_clone(a0);
}

static inline MakoString mako_fmt_sprint2(MakoString a0, MakoString a1) {
    size_t n = a0.len + 1 + a1.len;
    char *d = (char *)malloc(n + 1);
    size_t o = 0;
    if (a0.len && a0.data) {
        memcpy(d, a0.data, a0.len);
        o = a0.len;
    }
    d[o++] = ' ';
    if (a1.len && a1.data) {
        memcpy(d + o, a1.data, a1.len);
        o += a1.len;
    }
    d[o] = 0;
    return (MakoString){d, o};
}

static inline MakoString mako_fmt_sprint3(MakoString a0, MakoString a1, MakoString a2) {
    MakoString ab = mako_fmt_sprint2(a0, a1);
    MakoString out = mako_fmt_sprint2(ab, a2);
    mako_str_free(ab);
    return out;
}

static inline MakoString mako_fmt_sprint4(
    MakoString a0, MakoString a1, MakoString a2, MakoString a3
) {
    MakoString ab = mako_fmt_sprint3(a0, a1, a2);
    MakoString out = mako_fmt_sprint2(ab, a3);
    mako_str_free(ab);
    return out;
}

static inline MakoString mako_fmt_sprintln1(MakoString a0) {
    size_t n = a0.len + 1;
    char *d = (char *)malloc(n + 1);
    if (a0.len && a0.data) memcpy(d, a0.data, a0.len);
    d[a0.len] = '\n';
    d[a0.len + 1] = 0;
    return (MakoString){d, a0.len + 1};
}

static inline MakoString mako_fmt_sprintln2(MakoString a0, MakoString a1) {
    MakoString s = mako_fmt_sprint2(a0, a1);
    MakoString out = mako_fmt_sprintln1(s);
    mako_str_free(s);
    return out;
}

/* Output: no trailing newline unless *ln. */
static inline void mako_print_raw(MakoString s) {
    if (s.len && s.data) fwrite(s.data, 1, s.len, stdout);
    fflush(stdout);
}

static inline void mako_eprint_raw(MakoString s) {
    if (s.len && s.data) fwrite(s.data, 1, s.len, stderr);
    fflush(stderr);
}

static inline void mako_eprintln_str(MakoString s) {
    mako_eprint_raw(s);
    fputc('\n', stderr);
    fflush(stderr);
}

static inline int64_t mako_fmt_print1(MakoString a0) {
    mako_print_raw(a0);
    return 0;
}

static inline int64_t mako_fmt_print2(MakoString a0, MakoString a1) {
    mako_print_raw(a0);
    fputc(' ', stdout);
    mako_print_raw(a1);
    fflush(stdout);
    return 0;
}

static inline int64_t mako_fmt_println1(MakoString a0) {
    mako_print_raw(a0);
    fputc('\n', stdout);
    fflush(stdout);
    return 0;
}

static inline int64_t mako_fmt_println2(MakoString a0, MakoString a1) {
    mako_fmt_print2(a0, a1);
    fputc('\n', stdout);
    fflush(stdout);
    return 0;
}

static inline int64_t mako_fmt_printf1(MakoString fmt, MakoString a0) {
    MakoString s = mako_fmt_sprintf1(fmt, a0);
    mako_print_raw(s);
    mako_str_free(s);
    return 0;
}

static inline int64_t mako_fmt_printf2(MakoString fmt, MakoString a0, MakoString a1) {
    MakoString s = mako_fmt_sprintf2(fmt, a0, a1);
    mako_print_raw(s);
    mako_str_free(s);
    return 0;
}

static inline int64_t mako_fmt_printf3(
    MakoString fmt, MakoString a0, MakoString a1, MakoString a2
) {
    MakoString s = mako_fmt_sprintf3(fmt, a0, a1, a2);
    mako_print_raw(s);
    mako_str_free(s);
    return 0;
}

static inline int64_t mako_fmt_eprint1(MakoString a0) {
    mako_eprint_raw(a0);
    return 0;
}

static inline int64_t mako_fmt_eprintln1(MakoString a0) {
    mako_eprintln_str(a0);
    return 0;
}

static inline int64_t mako_fmt_eprintf1(MakoString fmt, MakoString a0) {
    MakoString s = mako_fmt_sprintf1(fmt, a0);
    mako_eprint_raw(s);
    mako_str_free(s);
    return 0;
}

/* Errorf-style: return formatted string (caller uses as error message). */
static inline MakoString mako_fmt_errorf1(MakoString fmt, MakoString a0) {
    return mako_fmt_sprintf1(fmt, a0);
}

static inline MakoString mako_fmt_errorf2(MakoString fmt, MakoString a0, MakoString a1) {
    return mako_fmt_sprintf2(fmt, a0, a1);
}

/* Upgrade legacy fmt_sprintf_s to use engine (all verbs). */
static inline MakoString mako_fmt_sprintf_s_full(MakoString fmt, MakoString arg) {
    return mako_fmt_sprintf1(fmt, arg);
}

/* Int-aware sprintf: first integer verb (%d %x %X %b %o %v %i) gets `arg`. */
static inline MakoString mako_fmt_sprintf_d_full(MakoString fmt, int64_t arg) {
    const char *p = fmt.data ? fmt.data : "";
    size_t n = fmt.len;
    char *out = NULL;
    size_t len = 0, cap = 0;
    int used = 0;
    size_t i = 0;
    while (i < n) {
        if (p[i] != '%' || i + 1 >= n) {
            mako_fmt_append(&out, &len, &cap, p + i, 1);
            i++;
            continue;
        }
        size_t si = i;
        int sharp = 0, zero = 0, width = 0, plus = 0, minus = 0;
        char v = mako_fmt_parse_spec(p, n, &si, &sharp, &zero, &width, &plus, &minus);
        if (v == '%') {
            mako_fmt_append(&out, &len, &cap, "%", 1);
            i = si + 1;
            continue;
        }
        if (!v) {
            mako_fmt_append(&out, &len, &cap, p + i, 1);
            i++;
            continue;
        }
        int is_int_verb =
            (v == 'd' || v == 'i' || v == 'v' || v == 'b' || v == 'o' || v == 'x' || v == 'X');
        if (is_int_verb && !used) {
            MakoString body =
                mako_fmt_format_int_verb(arg, v, sharp, zero && !minus, width, plus);
            mako_fmt_append(&out, &len, &cap, body.data, body.len);
            mako_str_free(body);
            used = 1;
            i = si + 1;
        } else if (is_int_verb) {
            /* no more int args — leave empty */
            i = si + 1;
        } else {
            /* non-int verb without string arg: skip */
            i = si + 1;
        }
        (void)minus;
    }
    if (!out) return mako_str_from_cstr("");
    return (MakoString){out, len};
}

/* Two int args for formats like "%d %x". */
static inline MakoString mako_fmt_sprintf_dd(MakoString fmt, int64_t a0, int64_t a1) {
    const char *p = fmt.data ? fmt.data : "";
    size_t n = fmt.len;
    char *out = NULL;
    size_t len = 0, cap = 0;
    int64_t args[2] = {a0, a1};
    int ai = 0;
    size_t i = 0;
    while (i < n) {
        if (p[i] != '%' || i + 1 >= n) {
            mako_fmt_append(&out, &len, &cap, p + i, 1);
            i++;
            continue;
        }
        size_t si = i;
        int sharp = 0, zero = 0, width = 0, plus = 0, minus = 0;
        char v = mako_fmt_parse_spec(p, n, &si, &sharp, &zero, &width, &plus, &minus);
        if (v == '%') {
            mako_fmt_append(&out, &len, &cap, "%", 1);
            i = si + 1;
            continue;
        }
        if (!v) {
            mako_fmt_append(&out, &len, &cap, p + i, 1);
            i++;
            continue;
        }
        int is_int_verb =
            (v == 'd' || v == 'i' || v == 'v' || v == 'b' || v == 'o' || v == 'x' || v == 'X');
        if (is_int_verb && ai < 2) {
            MakoString body =
                mako_fmt_format_int_verb(args[ai++], v, sharp, zero && !minus, width, plus);
            mako_fmt_append(&out, &len, &cap, body.data, body.len);
            mako_str_free(body);
            i = si + 1;
        } else {
            i = si + 1;
        }
        (void)minus;
    }
    if (!out) return mako_str_from_cstr("");
    return (MakoString){out, len};
}

static inline MakoString mako_fmt_sprintf_f(MakoString fmt, double arg, int64_t prec) {
    MakoString as = mako_format_float(arg, prec);
    MakoString out = mako_fmt_sprintf1(fmt, as);
    mako_str_free(as);
    return out;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_FMT_H */
