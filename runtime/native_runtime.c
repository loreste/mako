#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

void mako_native_print_i64(int64_t value) {
    printf("%lld\n", (long long)value);
}

void mako_native_print_bool(int32_t value) {
    puts(value ? "true" : "false");
}

typedef struct {
    const char *data;
    size_t len;
} MakoNativeString;

/* High bit of len marks immortal/static payload (string literals). Safe for
 * any data alignment (unlike pointer low-bit tags). Clone shares; drop no-ops. */
#define MAKO_STR_LEN_STATIC ((size_t)1 << (sizeof(size_t) * 8 - 1))
static inline int mako_str_is_static_len(size_t len) {
    return (len & MAKO_STR_LEN_STATIC) != 0;
}
static inline size_t mako_str_raw_len(size_t len) {
    return len & ~MAKO_STR_LEN_STATIC;
}
/* Content equality that ignores the immortal/static high bit on len. */
static inline int mako_native_str_content_eq(const MakoNativeString *a,
                                            const MakoNativeString *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    size_t al = a->data ? mako_str_raw_len(a->len) : 0;
    size_t bl = b->data ? mako_str_raw_len(b->len) : 0;
    if (al != bl) return 0;
    if (al == 0) return 1;
    if (!a->data || !b->data) return a->data == b->data;
    return memcmp(a->data, b->data, al) == 0;
}

// Pointer ABI used by the Cranelift shared-IR backend. The pointed-to header
// owns its data and is always safe to release; literals are copied so their
// lifetime is independent of generated code stack/data sections.
MakoNativeString *mako_native_string_literal_ptr(const char *data, size_t len) {
    MakoNativeString *out = malloc(sizeof(*out));
    if (out == NULL) abort();
    out->data = malloc(len + 1);
    if (out->data == NULL) abort();
    memcpy((char *)out->data, data, len);
    ((char *)out->data)[len] = '\0';
    out->len = len;
    return out;
}

MakoNativeString *mako_native_string_clone_ptr(const MakoNativeString *value) {
    // Null-safe: inactive enum payload slots hold a null header, and the flat
    // recursive clone visits every slot regardless of the active variant.
    if (value == NULL) return NULL;
    if (mako_str_is_static_len(value->len)) {
        MakoNativeString *out = malloc(sizeof(*out));
        if (!out) abort();
        *out = *value;
        return out;
    }
    return mako_native_string_literal_ptr(value->data, mako_str_raw_len(value->len));
}

MakoNativeString *mako_native_string_concat_ptr(const MakoNativeString *left,
                                                const MakoNativeString *right) {
    MakoNativeString *out = malloc(sizeof(*out));
    if (out == NULL) abort();
    size_t ll = mako_str_raw_len(left->len);
    size_t rl = mako_str_raw_len(right->len);
    if (rl > SIZE_MAX - ll) abort();
    out->len = ll + rl;
    out->data = malloc(out->len + 1);
    if (out->data == NULL) abort();
    memcpy((char *)out->data, left->data, ll);
    memcpy((char *)out->data + ll, right->data, rl);
    ((char *)out->data)[out->len] = '\0';
    return out;
}

int32_t mako_native_string_equal_ptr(const MakoNativeString *left,
                                     const MakoNativeString *right) {
    size_t ll = mako_str_raw_len(left->len);
    size_t rl = mako_str_raw_len(right->len);
    return ll == rl && memcmp(left->data, right->data, ll) == 0;
}

void mako_native_string_print_ptr(const MakoNativeString *value) {
    fwrite(value->data, 1, mako_str_raw_len(value->len), stdout);
    fputc('\n', stdout);
}

void mako_native_string_drop_ptr(MakoNativeString *value) {
    if (value == NULL) return;
    if (!mako_str_is_static_len(value->len)) free((void *)value->data);
    free(value);
}

MakoNativeString mako_native_string_clone(MakoNativeString value) {
    // Null-safe (see the pointer-ABI clone): an inactive payload slot is
    // {NULL, 0} and must clone to {NULL, 0}, never allocate.
    if (value.data == NULL) return (MakoNativeString){NULL, 0};
    if (mako_str_is_static_len(value.len)) return value; /* share immortal */
    size_t n = mako_str_raw_len(value.len);
    char *data = malloc(n + 1);
    if (data == NULL) abort();
    memcpy(data, value.data, n);
    data[n] = '\0';
    return (MakoNativeString){data, n};
}

MakoNativeString mako_native_string_concat(MakoNativeString left,
                                           MakoNativeString right) {
    size_t ll = mako_str_raw_len(left.len);
    size_t rl = mako_str_raw_len(right.len);
    if (rl > SIZE_MAX - ll) abort();
    size_t len = ll + rl;
    char *data = malloc(len + 1);
    if (data == NULL) abort();
    memcpy(data, left.data, ll);
    memcpy(data + ll, right.data, rl);
    data[len] = '\0';
    return (MakoNativeString){data, len};
}

int32_t mako_native_string_equal(MakoNativeString left,
                                 MakoNativeString right) {
    size_t ll = mako_str_raw_len(left.len);
    size_t rl = mako_str_raw_len(right.len);
    return ll == rl && memcmp(left.data, right.data, ll) == 0;
}

void mako_native_string_print(MakoNativeString value) {
    fwrite(value.data, 1, mako_str_raw_len(value.len), stdout);
    fputc('\n', stdout);
}

void mako_native_string_drop(MakoNativeString value) {
    if (!value.data || mako_str_is_static_len(value.len)) return;
    free((void *)value.data);
}

typedef struct {
    int64_t *data;
    size_t len;
    size_t cap;
    int64_t owned;
} MakoNativeIntSlice;

static void mako_native_slice_bounds(MakoNativeIntSlice value, int64_t index) {
    if (index < 0 || (uint64_t)index >= value.len) abort();
}

void mako_native_int_slice_make(MakoNativeIntSlice *result, size_t len, size_t cap) {
    if (cap < len) abort();
    int64_t *data = cap ? calloc(cap, sizeof(*data)) : NULL;
    if (cap && data == NULL) abort();
    *result = (MakoNativeIntSlice){data, len, cap, 1};
}

void mako_native_int_slice_literal(MakoNativeIntSlice *result, const int64_t *values, size_t len) {
    mako_native_int_slice_make(result, len, len);
    if (len) memcpy(result->data, values, len * sizeof(*values));
}

int64_t mako_native_int_slice_len(int64_t *data, size_t len, size_t cap, int64_t owned) {
    (void)data; (void)cap; (void)owned;
    return (int64_t)len;
}

int64_t mako_native_int_slice_get(int64_t *data, size_t len, size_t cap, int64_t owned, int64_t index) {
    MakoNativeIntSlice value = {data, len, cap, owned};
    mako_native_slice_bounds(value, index);
    return value.data[index];
}

void mako_native_int_slice_set(int64_t *data, size_t len, size_t cap, int64_t owned,
                               int64_t index, int64_t element) {
    MakoNativeIntSlice value = {data, len, cap, owned};
    mako_native_slice_bounds(value, index);
    value.data[index] = element;
}

void mako_native_int_slice_append(MakoNativeIntSlice *result, int64_t *data, size_t len,
                                  size_t capacity, int64_t owned, int64_t element) {
    MakoNativeIntSlice value = {data, len, capacity, owned};
    if (value.owned && value.len < value.cap) {
        value.data[value.len] = element;
        value.len++;
        *result = value;
        return;
    }
    size_t cap = value.cap;
    if (cap < value.len + 1) cap = cap ? cap * 2 : 1;
    MakoNativeIntSlice out;
    mako_native_int_slice_make(&out, value.len + 1, cap);
    if (value.len) memcpy(out.data, value.data, value.len * sizeof(*value.data));
    out.data[value.len] = element;
    if (value.owned) free(value.data);
    *result = out;
}

void mako_native_int_slice_slice(MakoNativeIntSlice *result, int64_t *data, size_t len,
                                 size_t cap, int64_t owned, int64_t low, int64_t high,
                                 int64_t max) {
    MakoNativeIntSlice value = {data, len, cap, owned};
    int64_t length = (int64_t)value.len;
    if (low < 0) low = 0;
    if (high < 0 || high > length) high = length;
    if (low > length) low = length;
    if (high < low) high = low;
    if (max < 0 || max > length) max = length;
    if (max < high) max = high;
    *result = (MakoNativeIntSlice){value.data + low, (size_t)(high - low),
                                   (size_t)(max - low), 0};
}

void mako_native_int_slice_clone(MakoNativeIntSlice *result, int64_t *data, size_t len,
                                 size_t cap, int64_t owned) {
    MakoNativeIntSlice value = {data, len, cap, owned};
    MakoNativeIntSlice out;
    mako_native_int_slice_make(&out, value.len, value.len);
    if (value.len) memcpy(out.data, value.data, value.len * sizeof(*value.data));
    *result = out;
}

void mako_native_int_slice_drop(int64_t *data, size_t len, size_t cap, int64_t owned) {
    MakoNativeIntSlice value = {data, len, cap, owned};
    if (value.owned) free(value.data);
}

// Pointer ABI for Cranelift shared IR. Headers are heap allocated; every
// pointer returned here is an owned value and must be released with drop_ptr.
MakoNativeIntSlice *mako_native_int_slice_make_ptr(size_t len, size_t cap) {
    MakoNativeIntSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    mako_native_int_slice_make(out, len, cap);
    return out;
}

MakoNativeIntSlice *mako_native_int_slice_literal_ptr(const int64_t *values, size_t len) {
    MakoNativeIntSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    mako_native_int_slice_literal(out, values, len);
    return out;
}

int64_t mako_native_int_slice_len_ptr(const MakoNativeIntSlice *value) {
    return value ? (int64_t)value->len : 0;
}

int64_t mako_native_int_slice_get_ptr(const MakoNativeIntSlice *value, int64_t index) {
    return mako_native_int_slice_get(value->data, value->len, value->cap, value->owned, index);
}

void mako_native_int_slice_set_ptr(MakoNativeIntSlice *value, int64_t index, int64_t element) {
    mako_native_int_slice_set(value->data, value->len, value->cap, value->owned, index, element);
}

MakoNativeIntSlice *mako_native_int_slice_append_ptr(MakoNativeIntSlice *value, int64_t element) {
    MakoNativeIntSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    mako_native_int_slice_append(out, value->data, value->len, value->cap, value->owned, element);
    free(value);
    return out;
}

MakoNativeIntSlice *mako_native_int_slice_slice_ptr(const MakoNativeIntSlice *value,
                                                    int64_t low, int64_t high, int64_t max) {
    MakoNativeIntSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    mako_native_int_slice_slice(out, value->data, value->len, value->cap, value->owned,
                                low, high, max);
    return out;
}

MakoNativeIntSlice *mako_native_int_slice_clone_ptr(const MakoNativeIntSlice *value) {
    // Null-safe: inactive enum/struct payload slots hold a null header.
    if (!value) return NULL;
    MakoNativeIntSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    mako_native_int_slice_clone(out, value->data, value->len, value->cap, value->owned);
    return out;
}

void mako_native_int_slice_drop_ptr(MakoNativeIntSlice *value) {
    if (!value) return;
    mako_native_int_slice_drop(value->data, value->len, value->cap, value->owned);
    free(value);
}

int64_t mako_native_int_slice_cap_ptr(const MakoNativeIntSlice *value) {
    return value ? (int64_t)value->cap : 0;
}

// Go-like copy(dst, src): min(len(dst), len(src)) elements; returns count.
// Mutates dst in place; both must be borrowed (non-owned) locals.
int64_t mako_native_int_slice_copy_ptr(MakoNativeIntSlice *dst,
                                       const MakoNativeIntSlice *src) {
    if (!dst || !src || !dst->data || !src->data) return 0;
    size_t n = dst->len < src->len ? dst->len : src->len;
    if (n == 0) return 0;
    memmove(dst->data, src->data, n * sizeof(int64_t));
    return (int64_t)n;
}

// ---- []float (pointer ABI; double elements) ---------------------------------

typedef struct {
    double *data;
    size_t len;
    size_t cap;
    int64_t owned;
} MakoNativeFloatSlice;

static void mako_native_float_slice_bounds(MakoNativeFloatSlice value, int64_t index) {
    if (index < 0 || (uint64_t)index >= value.len) abort();
}

void mako_native_float_slice_make(MakoNativeFloatSlice *result, size_t len, size_t cap) {
    if (cap < len) abort();
    double *data = cap ? calloc(cap, sizeof(*data)) : NULL;
    if (cap && data == NULL) abort();
    *result = (MakoNativeFloatSlice){data, len, cap, 1};
}

MakoNativeFloatSlice *mako_native_float_slice_make_ptr(int64_t len, int64_t cap) {
    if (len < 0 || cap < 0) abort();
    if (cap < len) cap = len;
    MakoNativeFloatSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    mako_native_float_slice_make(out, (size_t)len, (size_t)cap);
    return out;
}

int64_t mako_native_float_slice_len_ptr(const MakoNativeFloatSlice *value) {
    return value ? (int64_t)value->len : 0;
}

int64_t mako_native_float_slice_cap_ptr(const MakoNativeFloatSlice *value) {
    return value ? (int64_t)value->cap : 0;
}

int64_t mako_native_float_slice_copy_ptr(MakoNativeFloatSlice *dst,
                                         const MakoNativeFloatSlice *src) {
    if (!dst || !src || !dst->data || !src->data) return 0;
    size_t n = dst->len < src->len ? dst->len : src->len;
    if (n == 0) return 0;
    memmove(dst->data, src->data, n * sizeof(double));
    return (int64_t)n;
}

double mako_native_float_slice_get_ptr(const MakoNativeFloatSlice *value, int64_t index) {
    if (!value) abort();
    mako_native_float_slice_bounds(*value, index);
    return value->data[index];
}

void mako_native_float_slice_set_ptr(MakoNativeFloatSlice *value, int64_t index, double element) {
    if (!value) abort();
    mako_native_float_slice_bounds(*value, index);
    value->data[index] = element;
}

MakoNativeFloatSlice *mako_native_float_slice_append_ptr(MakoNativeFloatSlice *value,
                                                        double element) {
    if (!value) abort();
    if (value->owned && value->len < value->cap) {
        value->data[value->len++] = element;
        return value;
    }
    size_t cap = value->cap;
    if (cap < value->len + 1) cap = cap ? cap * 2 : 1;
    MakoNativeFloatSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    mako_native_float_slice_make(out, value->len + 1, cap);
    if (value->len) memcpy(out->data, value->data, value->len * sizeof(double));
    out->data[value->len] = element;
    if (value->owned) free(value->data);
    free(value);
    return out;
}

MakoNativeFloatSlice *mako_native_float_slice_slice_ptr(const MakoNativeFloatSlice *value,
                                                       int64_t low, int64_t high,
                                                       int64_t max) {
    if (!value) abort();
    int64_t length = (int64_t)value->len;
    if (low < 0) low = 0;
    if (high < 0 || high > length) high = length;
    if (low > length) low = length;
    if (high < low) high = low;
    if (max < 0 || max > length) max = length;
    if (max < high) max = high;
    MakoNativeFloatSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    // Owned deep copy of the window (safe across drop of the parent).
    mako_native_float_slice_make(out, (size_t)(high - low), (size_t)(high - low));
    if (high > low) {
        memcpy(out->data, value->data + low, (size_t)(high - low) * sizeof(double));
    }
    return out;
}

MakoNativeFloatSlice *mako_native_float_slice_clone_ptr(const MakoNativeFloatSlice *value) {
    if (!value) return NULL;
    MakoNativeFloatSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    mako_native_float_slice_make(out, value->len, value->len);
    if (value->len) memcpy(out->data, value->data, value->len * sizeof(double));
    return out;
}

void mako_native_float_slice_drop_ptr(MakoNativeFloatSlice *value) {
    if (!value) return;
    if (value->owned) free(value->data);
    free(value);
}

void mako_native_print_f64(double value) {
    // Match C-backend print_float: enough digits, trailing newline.
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%g", value);
    if (n < 0) abort();
    fwrite(buf, 1, (size_t)n, stdout);
    fputc('\n', stdout);
}

// ---- []byte (uint8 pointer-ABI header) --------------------------------------

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    int64_t owned; // 1 = free data on drop; 0 = view
} MakoNativeByteSlice;

static void mako_native_byte_slice_bounds(MakoNativeByteSlice v, int64_t index) {
    if (index < 0 || (uint64_t)index >= v.len) abort();
}

void mako_native_byte_slice_make(MakoNativeByteSlice *result, size_t len, size_t cap) {
    if (cap < len) cap = len;
    uint8_t *data = cap ? calloc(cap, 1) : NULL;
    if (cap && !data) abort();
    *result = (MakoNativeByteSlice){data, len, cap, 1};
}

MakoNativeByteSlice *mako_native_byte_slice_make_ptr(int64_t len, int64_t cap) {
    if (len < 0 || cap < 0) abort();
    if (cap < len) cap = len;
    MakoNativeByteSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    mako_native_byte_slice_make(out, (size_t)len, (size_t)cap);
    return out;
}

int64_t mako_native_byte_slice_len_ptr(const MakoNativeByteSlice *v) {
    return v ? (int64_t)v->len : 0;
}

int64_t mako_native_byte_slice_cap_ptr(const MakoNativeByteSlice *v) {
    return v ? (int64_t)v->cap : 0;
}

int64_t mako_native_byte_slice_copy_ptr(MakoNativeByteSlice *dst,
                                        const MakoNativeByteSlice *src) {
    if (!dst || !src || !dst->data || !src->data) return 0;
    size_t n = dst->len < src->len ? dst->len : src->len;
    if (n == 0) return 0;
    memmove(dst->data, src->data, n);
    return (int64_t)n;
}

int64_t mako_native_byte_slice_get_ptr(const MakoNativeByteSlice *v, int64_t index) {
    if (!v) abort();
    mako_native_byte_slice_bounds(*v, index);
    return (int64_t)v->data[index];
}

void mako_native_byte_slice_set_ptr(MakoNativeByteSlice *v, int64_t index, int64_t element) {
    if (!v) abort();
    mako_native_byte_slice_bounds(*v, index);
    if (element < 0 || element > 255) abort();
    v->data[index] = (uint8_t)element;
}

MakoNativeByteSlice *mako_native_byte_slice_append_ptr(MakoNativeByteSlice *v, int64_t element) {
    if (!v) abort();
    if (element < 0 || element > 255) abort();
    if (v->owned && v->len < v->cap) {
        v->data[v->len++] = (uint8_t)element;
        return v;
    }
    size_t cap = v->cap ? v->cap * 2 : 1;
    if (cap < v->len + 1) cap = v->len + 1;
    MakoNativeByteSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    mako_native_byte_slice_make(out, v->len + 1, cap);
    if (v->len) memcpy(out->data, v->data, v->len);
    out->data[v->len] = (uint8_t)element;
    if (v->owned) free(v->data);
    free(v);
    return out;
}

MakoNativeByteSlice *mako_native_byte_slice_slice_ptr(const MakoNativeByteSlice *v,
                                                     int64_t low, int64_t high,
                                                     int64_t max) {
    (void)max;
    if (!v) abort();
    int64_t length = (int64_t)v->len;
    if (low < 0) low = 0;
    if (high < 0 || high > length) high = length;
    if (low > length) low = length;
    if (high < low) high = low;
    // Deep-copy the window so drop of parent is always safe.
    MakoNativeByteSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    mako_native_byte_slice_make(out, (size_t)(high - low), (size_t)(high - low));
    if (high > low) memcpy(out->data, v->data + low, (size_t)(high - low));
    return out;
}

MakoNativeByteSlice *mako_native_byte_slice_clone_ptr(const MakoNativeByteSlice *v) {
    if (!v) return NULL;
    MakoNativeByteSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    mako_native_byte_slice_make(out, v->len, v->len);
    if (v->len) memcpy(out->data, v->data, v->len);
    return out;
}

void mako_native_byte_slice_drop_ptr(MakoNativeByteSlice *v) {
    if (!v) return;
    if (v->owned) free(v->data);
    free(v);
}

MakoNativeByteSlice *mako_native_bytes_from_string_ptr(const MakoNativeString *s) {
    size_t n = s && s->data ? mako_str_raw_len(s->len) : 0;
    MakoNativeByteSlice *out = malloc(sizeof(*out));
    if (!out) abort();
    mako_native_byte_slice_make(out, n, n);
    if (n) memcpy(out->data, s->data, n);
    return out;
}

MakoNativeString *mako_native_string_from_bytes_ptr(const MakoNativeByteSlice *b) {
    size_t n = b && b->data ? b->len : 0;
    char *d = malloc(n + 1);
    if (!d) abort();
    if (n) memcpy(d, b->data, n);
    d[n] = '\0';
    MakoNativeString *out = malloc(sizeof(*out));
    if (!out) abort();
    out->data = d;
    out->len = n;
    return out;
}

// Value-ABI string return for LLVM (Cranelift keeps the pointer form).
MakoNativeString mako_native_string_from_bytes_val(const MakoNativeByteSlice *b) {
    MakoNativeString *p = mako_native_string_from_bytes_ptr(b);
    MakoNativeString out = *p;
    free(p);
    return out;
}

MakoNativeByteSlice *mako_native_bytes_from_string_val(MakoNativeString s) {
    return mako_native_bytes_from_string_ptr(&s);
}

int64_t mako_native_rune_count_ptr(const MakoNativeString *s) {
    if (!s || !s->data) return 0;
    // UTF-8 code points: count lead bytes.
    size_t slen = mako_str_raw_len(s->len);
    int64_t n = 0;
    for (size_t i = 0; i < slen; ) {
        unsigned char c = (unsigned char)s->data[i];
        size_t adv = 1;
        if ((c & 0x80) == 0) adv = 1;
        else if ((c & 0xe0) == 0xc0) adv = 2;
        else if ((c & 0xf0) == 0xe0) adv = 3;
        else if ((c & 0xf8) == 0xf0) adv = 4;
        if (i + adv > slen) adv = 1;
        i += adv;
        n++;
    }
    return n;
}

int64_t mako_native_rune_count(MakoNativeString s) {
    return mako_native_rune_count_ptr(&s);
}

static int mako_native_cmp_i64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

MakoNativeIntSlice *mako_native_sort_ints_ptr(const MakoNativeIntSlice *a) {
    if (!a) return mako_native_int_slice_make_ptr(0, 0);
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr((int64_t)a->len, (int64_t)a->len);
    if (a->len) memcpy(out->data, a->data, a->len * sizeof(int64_t));
    out->len = a->len;
    if (out->len > 1) qsort(out->data, out->len, sizeof(int64_t), mako_native_cmp_i64);
    return out;
}

int64_t mako_native_ints_contains_ptr(const MakoNativeIntSlice *a, int64_t v) {
    if (!a || !a->data) return 0;
    for (size_t i = 0; i < a->len; i++) {
        if (a->data[i] == v) return 1;
    }
    return 0;
}

int64_t mako_native_ints_index_ptr(const MakoNativeIntSlice *a, int64_t v) {
    if (!a || !a->data) return -1;
    for (size_t i = 0; i < a->len; i++) {
        if (a->data[i] == v) return (int64_t)i;
    }
    return -1;
}

MakoNativeIntSlice *mako_native_ints_copy_ptr(const MakoNativeIntSlice *a) {
    if (!a) return mako_native_int_slice_make_ptr(0, 0);
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr((int64_t)a->len, (int64_t)a->len);
    if (a->len && a->data) memcpy(out->data, a->data, a->len * sizeof(int64_t));
    out->len = a->len;
    return out;
}

/* Peel one `ctx: rest` layer from an error message (owned return). */
MakoNativeString *mako_native_error_peel_once_ptr(const MakoNativeString *s) {
    size_t slen = s && s->data ? mako_str_raw_len(s->len) : 0;
    if (!s || !s->data || slen < 3) {
        return mako_native_string_clone_ptr(s);
    }
    for (size_t i = 0; i + 1 < slen; i++) {
        if (s->data[i] == ':' && s->data[i + 1] == ' ') {
            return mako_native_string_literal_ptr(s->data + i + 2, slen - i - 2);
        }
    }
    return mako_native_string_clone_ptr(s);
}

MakoNativeString *mako_native_error_peel_root_ptr(const MakoNativeString *s) {
    MakoNativeString *cur = mako_native_string_clone_ptr(s);
    for (int guard = 0; guard < 64; guard++) {
        size_t clen = cur && cur->data ? mako_str_raw_len(cur->len) : 0;
        if (!cur || !cur->data || clen < 3) return cur;
        int found = 0;
        size_t pos = 0;
        for (size_t i = 0; i + 1 < clen; i++) {
            if (cur->data[i] == ':' && cur->data[i + 1] == ' ') {
                found = 1;
                pos = i;
                break;
            }
        }
        if (!found) return cur;
        MakoNativeString *next =
            mako_native_string_literal_ptr(cur->data + pos + 2, clen - pos - 2);
        mako_native_string_drop_ptr(cur);
        cur = next;
    }
    return cur;
}

MakoNativeString *mako_native_error_as_tag_ptr(const MakoNativeString *s) {
    size_t slen = s && s->data ? mako_str_raw_len(s->len) : 0;
    if (!s || !s->data || slen == 0) {
        return mako_native_string_literal_ptr("", 0);
    }
    for (size_t i = 0; i + 1 < slen; i++) {
        if (s->data[i] == ':' && s->data[i + 1] == ' ') {
            return mako_native_string_literal_ptr(s->data, i);
        }
    }
    return mako_native_string_literal_ptr("", 0);
}

int64_t mako_native_error_has_tag_ptr(const MakoNativeString *s, const MakoNativeString *tag) {
    if (!s || !s->data || !tag || !tag->data) return 0;
    size_t n = mako_str_raw_len(tag->len);
    size_t slen = mako_str_raw_len(s->len);
    if (slen < n + 2) return 0;
    if (memcmp(s->data, tag->data, n) != 0) return 0;
    return (s->data[n] == ':' && s->data[n + 1] == ' ') ? 1 : 0;
}

// ---- fs / args --------------------------------------------------------------

int64_t mako_native_mkdir_ptr(const MakoNativeString *path) {
    if (!path || !path->data) return -1;
#if defined(_WIN32)
    return _mkdir(path->data) == 0 || errno == EEXIST ? 0 : -1;
#else
    if (mkdir(path->data, 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
#endif
}

int64_t mako_native_file_exists_ptr(const MakoNativeString *path) {
    if (!path || !path->data) return 0;
    struct stat st;
    return stat(path->data, &st) == 0 ? 1 : 0;
}

int64_t mako_native_remove_file_ptr(const MakoNativeString *path) {
    if (!path || !path->data) return -1;
    return unlink(path->data) == 0 ? 0 : -1;
}

// Value-ABI aliases for LLVM.
int64_t mako_native_mkdir(MakoNativeString path) {
    return mako_native_mkdir_ptr(&path);
}
int64_t mako_native_file_exists(MakoNativeString path) {
    return mako_native_file_exists_ptr(&path);
}
int64_t mako_native_remove_file(MakoNativeString path) {
    return mako_native_remove_file_ptr(&path);
}

typedef struct {
    MakoNativeString **data;
    size_t len;
    size_t cap;
    int64_t owned;
} MakoNativeStringSlice;

static MakoNativeString *mako_native_string_clone_ptr_value(const MakoNativeString *value) {
    return mako_native_string_clone_ptr(value);
}

static void mako_native_string_slice_free_elements(MakoNativeStringSlice *slice) {
    if (!slice || !slice->data) return;
    if (slice->owned) {
        for (size_t i = 0; i < slice->len; ++i) {
            mako_native_string_drop_ptr(slice->data[i]);
        }
    }
    free(slice->data);
}

MakoNativeStringSlice *mako_native_string_slice_make_ptr(size_t len, size_t cap) {
    if (cap < len) abort();
    MakoNativeStringSlice *out = calloc(1, sizeof(*out));
    if (!out) abort();
    out->data = calloc(cap ? cap : 1, sizeof(*out->data));
    if (!out->data) abort();
    out->len = len; out->cap = cap ? cap : 1; out->owned = 1;
    return out;
}

MakoNativeStringSlice *mako_native_string_slice_literal_ptr(
    MakoNativeString **values, size_t len) {
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(len, len);
    for (size_t i = 0; i < len; ++i) out->data[i] = mako_native_string_clone_ptr_value(values[i]);
    return out;
}

// `args()` → owned []string of argv (weak argc/argv; empty when unset).
MakoNativeStringSlice *mako_native_args_ptr(void) {
    extern int mako_argc_g;
    extern char **mako_argv_g;
    int n = mako_argc_g > 0 ? mako_argc_g : 0;
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr((size_t)n, (size_t)n);
    for (int i = 0; i < n; i++) {
        const char *s = (mako_argv_g && mako_argv_g[i]) ? mako_argv_g[i] : "";
        out->data[i] = mako_native_string_literal_ptr(s, strlen(s));
    }
    return out;
}

static int mako_native_cmp_str_hdr(const void *a, const void *b) {
    const MakoNativeString *x = *(const MakoNativeString *const *)a;
    const MakoNativeString *y = *(const MakoNativeString *const *)b;
    size_t xl = x && x->data ? mako_str_raw_len(x->len) : 0;
    size_t yl = y && y->data ? mako_str_raw_len(y->len) : 0;
    size_t n = xl < yl ? xl : yl;
    const char *xd = x && x->data ? x->data : "";
    const char *yd = y && y->data ? y->data : "";
    int c = memcmp(xd, yd, n);
    if (c != 0) return c;
    return (xl > yl) - (xl < yl);
}

MakoNativeStringSlice *mako_native_sort_strings_ptr(const MakoNativeStringSlice *a) {
    size_t n = a ? a->len : 0;
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(n, n);
    for (size_t i = 0; i < n; i++) {
        out->data[i] = mako_native_string_clone_ptr(a->data[i]);
    }
    if (n > 1) qsort(out->data, n, sizeof(MakoNativeString *), mako_native_cmp_str_hdr);
    return out;
}

/* Pointer-ABI []string: data is MakoNativeString** (same as sort_strings / maps_keys). */
int64_t mako_native_strings_contains_ptr(const MakoNativeStringSlice *a,
                                         const MakoNativeString *v) {
    if (!a || !a->data || !v) return 0;
    size_t vlen = v->data ? mako_str_raw_len(v->len) : 0;
    const char *vdata = v->data ? v->data : "";
    for (size_t i = 0; i < a->len; i++) {
        const MakoNativeString *e = a->data[i];
        size_t elen = e && e->data ? mako_str_raw_len(e->len) : 0;
        const char *edata = e && e->data ? e->data : "";
        if (elen == vlen && (vlen == 0 || memcmp(edata, vdata, vlen) == 0)) {
            return 1;
        }
    }
    return 0;
}

// ---- string index / byte-slice (pointer ABI) --------------------------------

int64_t mako_native_str_get_ptr(const MakoNativeString *s, int64_t i) {
    size_t slen = s && s->data ? mako_str_raw_len(s->len) : 0;
    if (!s || !s->data || i < 0 || (uint64_t)i >= slen) abort();
    return (int64_t)(unsigned char)s->data[i];
}

MakoNativeString *mako_native_str_slice_ptr(const MakoNativeString *s, int64_t low,
                                           int64_t high) {
    size_t len = s && s->data ? mako_str_raw_len(s->len) : 0;
    if (low < 0) low = 0;
    if (high < low) high = low;
    if ((uint64_t)high > len) high = (int64_t)len;
    if ((uint64_t)low > len) low = (int64_t)len;
    size_t n = (size_t)(high - low);
    return mako_native_string_literal_ptr(
        (s && s->data) ? s->data + (size_t)low : "", n);
}

// Value-ABI for LLVM.
int64_t mako_native_str_get(MakoNativeString s, int64_t i) {
    return mako_native_str_get_ptr(&s, i);
}

MakoNativeString mako_native_str_slice(MakoNativeString s, int64_t low, int64_t high) {
    MakoNativeString *p = mako_native_str_slice_ptr(&s, low, high);
    MakoNativeString out = *p;
    free(p);
    return out;
}

// UTF-8 decode helpers for `for i, r in range s` (Go-like runes).
static size_t mako_native_utf8_decode_raw(const char *data, size_t len, size_t off,
                                          int64_t *out) {
    if (off >= len) {
        *out = 0xFFFD;
        return 1;
    }
    unsigned char c = (unsigned char)data[off];
    if (c < 0x80) {
        *out = c;
        return 1;
    }
    if ((c & 0xe0) == 0xc0 && off + 1 < len) {
        *out = ((c & 0x1f) << 6) | ((unsigned char)data[off + 1] & 0x3f);
        return 2;
    }
    if ((c & 0xf0) == 0xe0 && off + 2 < len) {
        *out = ((c & 0x0f) << 12) | (((unsigned char)data[off + 1] & 0x3f) << 6)
               | ((unsigned char)data[off + 2] & 0x3f);
        return 3;
    }
    if ((c & 0xf8) == 0xf0 && off + 3 < len) {
        *out = ((c & 0x07) << 18) | (((unsigned char)data[off + 1] & 0x3f) << 12)
               | (((unsigned char)data[off + 2] & 0x3f) << 6)
               | ((unsigned char)data[off + 3] & 0x3f);
        return 4;
    }
    *out = 0xFFFD;
    return 1;
}

int64_t mako_native_utf8_decode_rune_ptr(const MakoNativeString *s, int64_t off) {
    int64_t r = 0xFFFD;
    size_t len = s && s->data ? mako_str_raw_len(s->len) : 0;
    const char *d = s && s->data ? s->data : "";
    if (off < 0) off = 0;
    mako_native_utf8_decode_raw(d, len, (size_t)off, &r);
    return r;
}

int64_t mako_native_utf8_decode_width_ptr(const MakoNativeString *s, int64_t off) {
    int64_t r = 0;
    size_t len = s && s->data ? mako_str_raw_len(s->len) : 0;
    const char *d = s && s->data ? s->data : "";
    if (off < 0) off = 0;
    return (int64_t)mako_native_utf8_decode_raw(d, len, (size_t)off, &r);
}

int64_t mako_native_utf8_decode_rune(MakoNativeString s, int64_t off) {
    return mako_native_utf8_decode_rune_ptr(&s, off);
}

int64_t mako_native_utf8_decode_width(MakoNativeString s, int64_t off) {
    return mako_native_utf8_decode_width_ptr(&s, off);
}



int64_t mako_native_string_slice_cap_ptr(const MakoNativeStringSlice *value) {
    return value ? (int64_t)value->cap : 0;
}

int64_t mako_native_string_slice_len_ptr(const MakoNativeStringSlice *value) {
    return value ? (int64_t)value->len : 0;
}

MakoNativeString *mako_native_string_slice_get_ptr(const MakoNativeStringSlice *value,
                                                    int64_t index) {
    if (index < 0 || (uint64_t)index >= value->len) abort();
    return mako_native_string_clone_ptr_value(value->data[index]);
}

void mako_native_string_slice_set_ptr(MakoNativeStringSlice *value, int64_t index,
                                       MakoNativeString *element) {
    if (index < 0 || (uint64_t)index >= value->len) abort();
    mako_native_string_drop_ptr(value->data[index]);
    value->data[index] = mako_native_string_clone_ptr_value(element);
}

MakoNativeStringSlice *mako_native_string_slice_clone_ptr(
    const MakoNativeStringSlice *value) {
    // Null-safe: inactive aggregate slots hold a null header.
    if (!value) return NULL;
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(value->len, value->len);
    for (size_t i = 0; i < value->len; ++i) out->data[i] = mako_native_string_clone_ptr_value(value->data[i]);
    return out;
}

MakoNativeStringSlice *mako_native_string_slice_append_ptr(MakoNativeStringSlice *value,
                                                             MakoNativeString *element) {
    if (value->owned && value->len < value->cap) {
        value->data[value->len++] = mako_native_string_clone_ptr_value(element);
        return value;
    }
    size_t cap = value->cap;
    if (cap < value->len + 1) cap = cap ? cap * 2 : 1;
    MakoNativeStringSlice *out = calloc(1, sizeof(*out));
    if (!out) abort();
    out->data = calloc(cap, sizeof(*out->data));
    if (!out->data) abort();
    out->len = value->len + 1; out->cap = cap; out->owned = 1;
    // Transfer existing owned element pointers; do not clone the whole slice.
    if (value->len) memcpy(out->data, value->data, value->len * sizeof(*out->data));
    out->data[value->len] = mako_native_string_clone_ptr_value(element);
    free(value->data); free(value);
    return out;
}

MakoNativeStringSlice *mako_native_string_slice_slice_ptr(
    const MakoNativeStringSlice *value, int64_t low, int64_t high, int64_t max) {
    int64_t length = (int64_t)value->len;
    if (low < 0) low = 0; if (high < 0 || high > length) high = length;
    if (low > length) low = length; if (high < low) high = low;
    (void)max;
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr((size_t)(high - low), (size_t)(high - low));
    for (int64_t i = low; i < high; ++i) out->data[i - low] = mako_native_string_clone_ptr_value(value->data[i]);
    return out;
}

void mako_native_string_slice_drop_ptr(MakoNativeStringSlice *value) {
    if (!value) return;
    mako_native_string_slice_free_elements(value); free(value);
}

// Value ABI for []string (LLVM). Elements are MakoNativeString values
// ({data,len}) packed in a flat array — matching the int-slice value layout
// shape of (data, len, cap, owned). Clone/drop recurse into each element.
typedef struct {
    MakoNativeString *data;
    size_t len;
    size_t cap;
    int64_t owned;
} MakoNativeStrSliceValue;

void mako_native_str_slice_make(MakoNativeStrSliceValue *result, size_t len, size_t cap) {
    if (cap < len) abort();
    MakoNativeString *data = cap ? calloc(cap, sizeof(*data)) : NULL;
    if (cap && data == NULL) abort();
    *result = (MakoNativeStrSliceValue){data, len, cap, 1};
}

void mako_native_str_slice_literal(MakoNativeStrSliceValue *result,
                                   const MakoNativeString *values, size_t len) {
    mako_native_str_slice_make(result, len, len);
    for (size_t i = 0; i < len; ++i) {
        result->data[i] = mako_native_string_clone(values[i]);
    }
}

int64_t mako_native_str_slice_len(MakoNativeString *data, size_t len, size_t cap, int64_t owned) {
    (void)data; (void)cap; (void)owned;
    return (int64_t)len;
}

MakoNativeString mako_native_str_slice_get(MakoNativeString *data, size_t len, size_t cap,
                                           int64_t owned, int64_t index) {
    (void)cap; (void)owned;
    if (index < 0 || (uint64_t)index >= len) abort();
    return mako_native_string_clone(data[index]);
}

void mako_native_str_slice_set(MakoNativeString *data, size_t len, size_t cap, int64_t owned,
                               int64_t index, MakoNativeString element) {
    (void)cap; (void)owned;
    if (index < 0 || (uint64_t)index >= len) abort();
    mako_native_string_drop(data[index]);
    data[index] = mako_native_string_clone(element);
}

void mako_native_str_slice_append(MakoNativeStrSliceValue *result, MakoNativeString *data,
                                  size_t len, size_t cap, int64_t owned,
                                  MakoNativeString element) {
    if (owned && len < cap) {
        data[len] = mako_native_string_clone(element);
        *result = (MakoNativeStrSliceValue){data, len + 1, cap, 1};
        return;
    }
    size_t new_cap = cap ? cap * 2 : 1;
    if (new_cap < len + 1) new_cap = len + 1;
    MakoNativeString *new_data = calloc(new_cap, sizeof(*new_data));
    if (!new_data) abort();
    if (len) memcpy(new_data, data, len * sizeof(*new_data));
    new_data[len] = mako_native_string_clone(element);
    if (owned) free(data);
    *result = (MakoNativeStrSliceValue){new_data, len + 1, new_cap, 1};
}

void mako_native_str_slice_slice(MakoNativeStrSliceValue *result, MakoNativeString *data,
                                 size_t len, size_t cap, int64_t owned,
                                 int64_t low, int64_t high, int64_t max) {
    (void)cap; (void)owned; (void)max;
    int64_t length = (int64_t)len;
    if (low < 0) low = 0;
    if (high < 0 || high > length) high = length;
    if (low > length) low = length;
    if (high < low) high = low;
    size_t out_len = (size_t)(high - low);
    mako_native_str_slice_make(result, out_len, out_len);
    for (int64_t i = low; i < high; ++i) {
        result->data[i - low] = mako_native_string_clone(data[i]);
    }
}

void mako_native_str_slice_clone(MakoNativeStrSliceValue *result, MakoNativeString *data,
                                 size_t len, size_t cap, int64_t owned) {
    (void)cap; (void)owned;
    if (data == NULL && len == 0) {
        *result = (MakoNativeStrSliceValue){NULL, 0, 0, 0};
        return;
    }
    mako_native_str_slice_make(result, len, len);
    for (size_t i = 0; i < len; ++i) {
        result->data[i] = mako_native_string_clone(data[i]);
    }
}

void mako_native_str_slice_drop(MakoNativeString *data, size_t len, size_t cap, int64_t owned) {
    (void)cap;
    if (!owned || !data) return;
    for (size_t i = 0; i < len; ++i) {
        mako_native_string_drop(data[i]);
    }
    free(data);
}

// ---- Formatting / interop seeds (value + pointer ABIs) --------------------

MakoNativeString mako_native_int_to_string(int64_t n) {
    char buf[32];
    int written = snprintf(buf, sizeof(buf), "%lld", (long long)n);
    if (written < 0) written = 0;
    return mako_native_string_clone((MakoNativeString){buf, (size_t)written});
}

MakoNativeString *mako_native_int_to_string_ptr(int64_t n) {
    MakoNativeString value = mako_native_int_to_string(n);
    MakoNativeString *out = malloc(sizeof(*out));
    if (!out) abort();
    *out = value;
    return out;
}

MakoNativeString mako_native_bool_to_string(int64_t b) {
    // Match the C backend's f-string / print path, which writes bools as 0/1.
    const char *s = b ? "1" : "0";
    return mako_native_string_clone((MakoNativeString){s, 1});
}

MakoNativeString *mako_native_bool_to_string_ptr(int64_t b) {
    MakoNativeString value = mako_native_bool_to_string(b);
    MakoNativeString *out = malloc(sizeof(*out));
    if (!out) abort();
    *out = value;
    return out;
}

// ---- Host / process builtins ------------------------------------------------

void mako_native_exit(int64_t code) {
    exit((int)code);
}

int64_t mako_native_argc(void) {
    // Populated only if the host main called mako_native_set_args; default 0.
    extern int mako_argc_g;
    return (int64_t)mako_argc_g;
}

// Weak fallback when the full runtime is not linked. The native entry
// (`main`) calls mako_native_set_args(argc, argv) so process args work.
int mako_argc_g __attribute__((weak)) = 0;

void mako_native_set_args(int64_t argc, int64_t argv_ptr) {
    extern int mako_argc_g;
    extern char **mako_argv_g;
    if (argc < 0) {
        argc = 0;
    }
    mako_argc_g = (int)argc;
    mako_argv_g = (char **)(uintptr_t)argv_ptr;
}

void mako_native_sleep_ms(int64_t ms) {
    if (ms <= 0) return;
#if defined(_WIN32)
    // Not used on this host; keep a portable no-op fallback.
    (void)ms;
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&ts, NULL);
#endif
}

int64_t mako_native_parse_int(MakoNativeString value) {
    size_t nlen = value.data ? mako_str_raw_len(value.len) : 0;
    if (value.data == NULL || nlen == 0) return 0;
    // Ensure NUL-terminated for strtoll.
    char *buf = malloc(nlen + 1);
    if (!buf) abort();
    memcpy(buf, value.data, nlen);
    buf[nlen] = '\0';
    char *end = NULL;
    long long n = strtoll(buf, &end, 10);
    free(buf);
    return (int64_t)n;
}

int64_t mako_native_parse_int_ptr(const MakoNativeString *value) {
    if (!value) return 0;
    return mako_native_parse_int(*value);
}

// Overflow-trapping arithmetic used when `--overflow trap` is selected.
static void mako_native_overflow_trap(const char *op) {
    fprintf(stderr, "integer overflow in %s\n", op ? op : "arithmetic");
    abort();
}

int64_t mako_native_add_i64_trap(int64_t a, int64_t b) {
    int64_t r;
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_add_overflow(a, b, &r)) mako_native_overflow_trap("add");
#else
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        mako_native_overflow_trap("add");
    r = a + b;
#endif
    return r;
}

int64_t mako_native_sub_i64_trap(int64_t a, int64_t b) {
    int64_t r;
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_sub_overflow(a, b, &r)) mako_native_overflow_trap("sub");
#else
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
        mako_native_overflow_trap("sub");
    r = a - b;
#endif
    return r;
}

int64_t mako_native_mul_i64_trap(int64_t a, int64_t b) {
    int64_t r;
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_mul_overflow(a, b, &r)) mako_native_overflow_trap("mul");
#else
    if (a != 0 && b != 0 && ((a > 0 && b > 0 && a > INT64_MAX / b) ||
                             (a > 0 && b < 0 && b < INT64_MIN / a) ||
                             (a < 0 && b > 0 && a < INT64_MIN / b) ||
                             (a < 0 && b < 0 && a < INT64_MAX / b)))
        mako_native_overflow_trap("mul");
    r = a * b;
#endif
    return r;
}

// Struct ABI used by the shared-IR backend. A struct value is a heap block of
// scalar fields laid out one 8-byte slot each (offset index*8). The block owns
// no nested heap in the scalar-field increment, so make/clone/drop are a flat
// calloc / memcpy / free. Field loads and stores are emitted inline by codegen.
void *mako_native_struct_make_ptr(int64_t nbytes) {
    void *out = calloc(1, nbytes > 0 ? (size_t)nbytes : 1);
    if (out == NULL) abort();
    return out;
}

void *mako_native_struct_clone_ptr(const void *value, int64_t nbytes) {
    void *out = mako_native_struct_make_ptr(nbytes);
    if (value && nbytes > 0) memcpy(out, value, (size_t)nbytes);
    return out;
}

void mako_native_struct_drop_ptr(void *value) {
    free(value);
}

// ---- map[int]int / map[string]int (pointer ABI for shared IR) ---------------
// Simplified open-addressing tables for the native backend. API mirrors the
// C-runtime surface enough for make / index / set / has / delete / len.

enum { MAKO_NMAP_EMPTY = 0, MAKO_NMAP_FULL = 1, MAKO_NMAP_TOMB = 2 };

typedef struct {
    uint8_t *state;
    int64_t *keys;
    int64_t *vals;
    size_t cap;
    size_t len;
} MakoNativeMapII;

typedef struct {
    uint8_t *state;
    MakoNativeString **keys;
    int64_t *vals;
    size_t cap;
    size_t len;
} MakoNativeMapSI;

static uint64_t mako_native_hash_i64(int64_t k) {
    uint64_t x = (uint64_t)k;
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static uint64_t mako_native_hash_str(const MakoNativeString *s) {
    uint64_t h = 14695981039346656037ULL;
    if (!s || !s->data) return h;
    size_t n = mako_str_raw_len(s->len);
    for (size_t i = 0; i < n; ++i) {
        h ^= (uint8_t)s->data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* nest_nf_pack / nest_sm_pack: up to 4 nested-struct fields, 16 bits each,
 * in ascending field-index order among bits set in nest_mask. */
static void mako_native_unpack_nest(
    int64_t nest_mask, int64_t nf_pack, int64_t sm_pack,
    int64_t *nf_by_field, int64_t *sm_by_field
) {
    for (int i = 0; i < 62; i++) {
        nf_by_field[i] = 0;
        sm_by_field[i] = 0;
    }
    int j = 0;
    for (int i = 0; i < 62 && j < 4; i++) {
        if (nest_mask & (1LL << i)) {
            nf_by_field[i] = (nf_pack >> (j * 16)) & 0xFFFF;
            sm_by_field[i] = (sm_pack >> (j * 16)) & 0xFFFF;
            j++;
        }
    }
}

/* Content key for map[Struct]V: mix field words (str_mask bit i → string header;
 * nest_mask bit i → nested struct pointer, content-hashed one level deep). */
int64_t mako_native_struct_content_key(
    void *p, int64_t nfields, int64_t str_mask,
    int64_t nest_mask, int64_t nest_nf_pack, int64_t nest_sm_pack
) {
    uint64_t h = 14695981039346656037ULL;
    if (!p || nfields <= 0) return (int64_t)h;
    if (nfields > 62) nfields = 62;
    int64_t nest_nf[62], nest_sm[62];
    mako_native_unpack_nest(nest_mask, nest_nf_pack, nest_sm_pack, nest_nf, nest_sm);
    int64_t *fields = (int64_t *)p;
    for (int64_t i = 0; i < nfields; i++) {
        if (str_mask & (1LL << i)) {
            MakoNativeString *s = (MakoNativeString *)(intptr_t)fields[i];
            uint64_t sh = mako_native_hash_str(s);
            h ^= sh;
            h *= 1099511628211ULL;
        } else if (nest_mask & (1LL << i)) {
            void *nested = (void *)(intptr_t)fields[i];
            /* One level: nested has no further nest packs. */
            int64_t nh = mako_native_struct_content_key(
                nested, nest_nf[i], nest_sm[i], 0, 0, 0
            );
            h ^= (uint64_t)nh;
            h *= 1099511628211ULL;
        } else {
            uint64_t w = (uint64_t)fields[i];
            h ^= w + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            h *= 1099511628211ULL;
        }
    }
    return (int64_t)h;
}

/* Backward-compatible wrapper (no nest). */
int64_t mako_native_struct_content_key_flat(void *p, int64_t nfields, int64_t str_mask) {
    return mako_native_struct_content_key(p, nfields, str_mask, 0, 0, 0);
}

static MakoNativeMapII *mako_native_map_ii_new(size_t hint) {
    size_t cap = 8;
    while (cap < hint * 2 + 8) cap *= 2;
    MakoNativeMapII *m = calloc(1, sizeof(*m));
    if (!m) abort();
    m->state = calloc(cap, 1);
    m->keys = calloc(cap, sizeof(int64_t));
    m->vals = calloc(cap, sizeof(int64_t));
    if (!m->state || !m->keys || !m->vals) abort();
    m->cap = cap; m->len = 0;
    return m;
}

MakoNativeMapII *mako_native_map_ii_make_ptr(int64_t hint) {
    return mako_native_map_ii_new(hint > 0 ? (size_t)hint : 0);
}

static void mako_native_map_ii_rehash(MakoNativeMapII *m, size_t ncap);

void mako_native_map_ii_set_ptr(MakoNativeMapII *m, int64_t key, int64_t val) {
    if (!m) abort();
    if (m->len * 10 >= m->cap * 7) mako_native_map_ii_rehash(m, m->cap * 2);
    size_t i = (size_t)(mako_native_hash_i64(key) & (m->cap - 1));
    size_t first_tomb = (size_t)-1;
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) {
            size_t slot = first_tomb != (size_t)-1 ? first_tomb : i;
            m->state[slot] = MAKO_NMAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (m->state[i] == MAKO_NMAP_TOMB && first_tomb == (size_t)-1) first_tomb = i;
        if (m->state[i] == MAKO_NMAP_FULL && m->keys[i] == key) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
    abort();
}

static void mako_native_map_ii_rehash(MakoNativeMapII *m, size_t ncap) {
    MakoNativeMapII *n = mako_native_map_ii_new(ncap / 2);
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            mako_native_map_ii_set_ptr(n, m->keys[i], m->vals[i]);
    }
    free(m->state); free(m->keys); free(m->vals);
    *m = *n; free(n);
}

int64_t mako_native_map_ii_get_ptr(const MakoNativeMapII *m, int64_t key) {
    if (!m || !m->cap) return 0;
    size_t i = (size_t)(mako_native_hash_i64(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return 0;
        if (m->state[i] == MAKO_NMAP_FULL && m->keys[i] == key) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
    return 0;
}

int64_t mako_native_map_ii_has_ptr(const MakoNativeMapII *m, int64_t key) {
    if (!m || !m->cap) return 0;
    size_t i = (size_t)(mako_native_hash_i64(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return 0;
        if (m->state[i] == MAKO_NMAP_FULL && m->keys[i] == key) return 1;
        i = (i + 1) & (m->cap - 1);
    }
    return 0;
}

void mako_native_map_ii_delete_ptr(MakoNativeMapII *m, int64_t key) {
    if (!m || !m->cap) return;
    size_t i = (size_t)(mako_native_hash_i64(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return;
        if (m->state[i] == MAKO_NMAP_FULL && m->keys[i] == key) {
            m->state[i] = MAKO_NMAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

int64_t mako_native_map_ii_len_ptr(const MakoNativeMapII *m) {
    return m ? (int64_t)m->len : 0;
}

/* map[Struct]V: keys are owned struct pointers; bucket by content hash, match by content. */
static int mako_native_struct_key_eq(
    void *a, void *b, int64_t nfields, int64_t str_mask,
    int64_t nest_mask, int64_t nest_nf_pack, int64_t nest_sm_pack
) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (nfields < 0 || nfields > 62) return 0;
    int64_t nest_nf[62], nest_sm[62];
    mako_native_unpack_nest(nest_mask, nest_nf_pack, nest_sm_pack, nest_nf, nest_sm);
    int64_t *fa = (int64_t *)a;
    int64_t *fb = (int64_t *)b;
    for (int64_t i = 0; i < nfields; i++) {
        if (str_mask & (1LL << i)) {
            MakoNativeString *sa = (MakoNativeString *)(intptr_t)fa[i];
            MakoNativeString *sb = (MakoNativeString *)(intptr_t)fb[i];
            if (!mako_native_str_content_eq(sa, sb)) return 0;
        } else if (nest_mask & (1LL << i)) {
            void *na = (void *)(intptr_t)fa[i];
            void *nb = (void *)(intptr_t)fb[i];
            if (!mako_native_struct_key_eq(na, nb, nest_nf[i], nest_sm[i], 0, 0, 0))
                return 0;
        } else if (fa[i] != fb[i]) {
            return 0;
        }
    }
    return 1;
}

/* Clone field words; deep-clone strings and one level of nested struct keys. */
static void *mako_native_struct_key_clone(
    void *key, int64_t nfields, int64_t str_mask,
    int64_t nest_mask, int64_t nest_nf_pack, int64_t nest_sm_pack
) {
    if (!key || nfields <= 0) return NULL;
    size_t bytes = (size_t)nfields * sizeof(int64_t);
    int64_t *out = (int64_t *)malloc(bytes);
    if (!out) abort();
    int64_t nest_nf[62], nest_sm[62];
    mako_native_unpack_nest(nest_mask, nest_nf_pack, nest_sm_pack, nest_nf, nest_sm);
    int64_t *in = (int64_t *)key;
    for (int64_t i = 0; i < nfields; i++) {
        if (str_mask & (1LL << i)) {
            MakoNativeString *s = (MakoNativeString *)(intptr_t)in[i];
            out[i] = (int64_t)(intptr_t)mako_native_string_clone_ptr(s);
        } else if (nest_mask & (1LL << i)) {
            void *nested = (void *)(intptr_t)in[i];
            out[i] = (int64_t)(intptr_t)mako_native_struct_key_clone(
                nested, nest_nf[i], nest_sm[i], 0, 0, 0
            );
        } else {
            out[i] = in[i];
        }
    }
    return out;
}

void mako_native_map_struct_key_set_ptr(
    MakoNativeMapII *m, void *key, int64_t nfields, int64_t str_mask,
    int64_t nest_mask, int64_t nest_nf_pack, int64_t nest_sm_pack, int64_t val
) {
    if (!m || !key) abort();
    if (m->len * 10 >= m->cap * 7) mako_native_map_ii_rehash(m, m->cap * 2);
    int64_t hk = mako_native_struct_content_key(
        key, nfields, str_mask, nest_mask, nest_nf_pack, nest_sm_pack
    );
    size_t i = (size_t)(mako_native_hash_i64(hk) & (m->cap - 1));
    size_t first_tomb = (size_t)-1;
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) {
            size_t slot = first_tomb != (size_t)-1 ? first_tomb : i;
            m->state[slot] = MAKO_NMAP_FULL;
            m->keys[slot] = (int64_t)(intptr_t)mako_native_struct_key_clone(
                key, nfields, str_mask, nest_mask, nest_nf_pack, nest_sm_pack
            );
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (m->state[i] == MAKO_NMAP_TOMB && first_tomb == (size_t)-1) first_tomb = i;
        if (m->state[i] == MAKO_NMAP_FULL) {
            void *exist = (void *)(intptr_t)m->keys[i];
            if (mako_native_struct_key_eq(
                    exist, key, nfields, str_mask, nest_mask, nest_nf_pack, nest_sm_pack
                )) {
                m->vals[i] = val;
                return;
            }
        }
        i = (i + 1) & (m->cap - 1);
    }
    abort();
}

int64_t mako_native_map_struct_key_get_ptr(
    const MakoNativeMapII *m, void *key, int64_t nfields, int64_t str_mask,
    int64_t nest_mask, int64_t nest_nf_pack, int64_t nest_sm_pack
) {
    if (!m || !m->cap || !key) return 0;
    int64_t hk = mako_native_struct_content_key(
        key, nfields, str_mask, nest_mask, nest_nf_pack, nest_sm_pack
    );
    size_t i = (size_t)(mako_native_hash_i64(hk) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return 0;
        if (m->state[i] == MAKO_NMAP_FULL) {
            void *exist = (void *)(intptr_t)m->keys[i];
            if (mako_native_struct_key_eq(
                    exist, key, nfields, str_mask, nest_mask, nest_nf_pack, nest_sm_pack
                ))
                return m->vals[i];
        }
        i = (i + 1) & (m->cap - 1);
    }
    return 0;
}

int64_t mako_native_map_struct_key_has_ptr(
    const MakoNativeMapII *m, void *key, int64_t nfields, int64_t str_mask,
    int64_t nest_mask, int64_t nest_nf_pack, int64_t nest_sm_pack
) {
    if (!m || !m->cap || !key) return 0;
    int64_t hk = mako_native_struct_content_key(
        key, nfields, str_mask, nest_mask, nest_nf_pack, nest_sm_pack
    );
    size_t i = (size_t)(mako_native_hash_i64(hk) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return 0;
        if (m->state[i] == MAKO_NMAP_FULL) {
            void *exist = (void *)(intptr_t)m->keys[i];
            if (mako_native_struct_key_eq(
                    exist, key, nfields, str_mask, nest_mask, nest_nf_pack, nest_sm_pack
                ))
                return 1;
        }
        i = (i + 1) & (m->cap - 1);
    }
    return 0;
}

void mako_native_map_struct_key_delete_ptr(
    MakoNativeMapII *m, void *key, int64_t nfields, int64_t str_mask,
    int64_t nest_mask, int64_t nest_nf_pack, int64_t nest_sm_pack
) {
    if (!m || !m->cap || !key) return;
    int64_t hk = mako_native_struct_content_key(
        key, nfields, str_mask, nest_mask, nest_nf_pack, nest_sm_pack
    );
    size_t i = (size_t)(mako_native_hash_i64(hk) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return;
        if (m->state[i] == MAKO_NMAP_FULL) {
            void *exist = (void *)(intptr_t)m->keys[i];
            if (mako_native_struct_key_eq(
                    exist, key, nfields, str_mask, nest_mask, nest_nf_pack, nest_sm_pack
                )) {
                m->state[i] = MAKO_NMAP_TOMB;
                m->len--;
                return;
            }
        }
        i = (i + 1) & (m->cap - 1);
    }
}

/* Clone / copy / equal for content-keyed struct maps. */
MakoNativeMapII *mako_native_map_struct_key_clone_ptr(
    const MakoNativeMapII *m, int64_t nfields, int64_t str_mask,
    int64_t nest_mask, int64_t nest_nf_pack, int64_t nest_sm_pack
) {
    if (!m) return NULL;
    MakoNativeMapII *n = mako_native_map_ii_new(m->len);
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL) {
            void *k = (void *)(intptr_t)m->keys[i];
            mako_native_map_struct_key_set_ptr(
                n, k, nfields, str_mask, nest_mask, nest_nf_pack, nest_sm_pack, m->vals[i]
            );
        }
    }
    return n;
}

void mako_native_maps_copy_struct_key(
    MakoNativeMapII *dst, const MakoNativeMapII *src, int64_t nfields, int64_t str_mask,
    int64_t nest_mask, int64_t nest_nf_pack, int64_t nest_sm_pack
) {
    if (dst) {
        for (size_t i = 0; i < dst->cap; ++i) dst->state[i] = MAKO_NMAP_EMPTY;
        dst->len = 0;
    }
    if (!src || !dst) return;
    for (size_t i = 0; i < src->cap; ++i) {
        if (src->state[i] == MAKO_NMAP_FULL) {
            void *k = (void *)(intptr_t)src->keys[i];
            mako_native_map_struct_key_set_ptr(
                dst, k, nfields, str_mask, nest_mask, nest_nf_pack, nest_sm_pack, src->vals[i]
            );
        }
    }
}

/* val_kind: 0 = i64 bits, 1 = string content, 2 = nested struct value. */
int64_t mako_native_maps_equal_struct_key(
    const MakoNativeMapII *a,
    const MakoNativeMapII *b,
    int64_t nfields,
    int64_t str_mask,
    int64_t nest_mask,
    int64_t nest_nf_pack,
    int64_t nest_sm_pack,
    int64_t val_kind,
    int64_t val_nfields,
    int64_t val_str_mask
) {
    if (!a || !b) return a == b ? 1 : 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; ++i) {
        if (a->state[i] != MAKO_NMAP_FULL) continue;
        void *k = (void *)(intptr_t)a->keys[i];
        if (!mako_native_map_struct_key_has_ptr(
                b, k, nfields, str_mask, nest_mask, nest_nf_pack, nest_sm_pack
            ))
            return 0;
        int64_t va = a->vals[i];
        int64_t vb = mako_native_map_struct_key_get_ptr(
            b, k, nfields, str_mask, nest_mask, nest_nf_pack, nest_sm_pack
        );
        if (val_kind == 1) {
            MakoNativeString *sa = (MakoNativeString *)(intptr_t)va;
            MakoNativeString *sb = (MakoNativeString *)(intptr_t)vb;
            if (!mako_native_str_content_eq(sa, sb)) return 0;
        } else if (val_kind == 2) {
            void *pa = (void *)(intptr_t)va;
            void *pb = (void *)(intptr_t)vb;
            if (!mako_native_struct_key_eq(pa, pb, val_nfields, val_str_mask, 0, 0, 0))
                return 0;
        } else if (va != vb) {
            return 0;
        }
    }
    return 1;
}

void mako_native_map_ii_drop_ptr(MakoNativeMapII *m) {
    if (!m) return;
    free(m->state); free(m->keys); free(m->vals); free(m);
}

MakoNativeMapII *mako_native_map_ii_clone_ptr(const MakoNativeMapII *m) {
    if (!m) return NULL;
    MakoNativeMapII *n = mako_native_map_ii_new(m->len);
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            mako_native_map_ii_set_ptr(n, m->keys[i], m->vals[i]);
    }
    return n;
}

static MakoNativeMapSI *mako_native_map_si_new(size_t hint) {
    size_t cap = 8;
    while (cap < hint * 2 + 8) cap *= 2;
    MakoNativeMapSI *m = calloc(1, sizeof(*m));
    if (!m) abort();
    m->state = calloc(cap, 1);
    m->keys = calloc(cap, sizeof(*m->keys));
    m->vals = calloc(cap, sizeof(int64_t));
    if (!m->state || !m->keys || !m->vals) abort();
    m->cap = cap; m->len = 0;
    return m;
}

MakoNativeMapSI *mako_native_map_si_make_ptr(int64_t hint) {
    return mako_native_map_si_new(hint > 0 ? (size_t)hint : 0);
}

static void mako_native_map_si_rehash(MakoNativeMapSI *m, size_t ncap);

void mako_native_map_si_set_ptr(MakoNativeMapSI *m, MakoNativeString *key, int64_t val) {
    if (!m) abort();
    if (m->len * 10 >= m->cap * 7) mako_native_map_si_rehash(m, m->cap * 2);
    size_t i = (size_t)(mako_native_hash_str(key) & (m->cap - 1));
    size_t first_tomb = (size_t)-1;
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) {
            size_t slot = first_tomb != (size_t)-1 ? first_tomb : i;
            m->state[slot] = MAKO_NMAP_FULL;
            m->keys[slot] = mako_native_string_clone_ptr(key);
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (m->state[i] == MAKO_NMAP_TOMB && first_tomb == (size_t)-1) first_tomb = i;
        if (m->state[i] == MAKO_NMAP_FULL &&
            mako_native_str_content_eq(m->keys[i], key)) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
    abort();
}

static void mako_native_map_si_rehash(MakoNativeMapSI *m, size_t ncap) {
    MakoNativeMapSI *n = mako_native_map_si_new(ncap / 2);
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            mako_native_map_si_set_ptr(n, m->keys[i], m->vals[i]);
    }
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL) mako_native_string_drop_ptr(m->keys[i]);
    }
    free(m->state); free(m->keys); free(m->vals);
    *m = *n; free(n);
}

int64_t mako_native_map_si_get_ptr(const MakoNativeMapSI *m, MakoNativeString *key) {
    if (!m || !m->cap) return 0;
    size_t i = (size_t)(mako_native_hash_str(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return 0;
        if (m->state[i] == MAKO_NMAP_FULL && mako_native_str_content_eq(m->keys[i], key))
            return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
    return 0;
}

int64_t mako_native_map_si_has_ptr(const MakoNativeMapSI *m, MakoNativeString *key) {
    if (!m || !m->cap) return 0;
    size_t i = (size_t)(mako_native_hash_str(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return 0;
        if (m->state[i] == MAKO_NMAP_FULL && mako_native_str_content_eq(m->keys[i], key))
            return 1;
        i = (i + 1) & (m->cap - 1);
    }
    return 0;
}

void mako_native_map_si_delete_ptr(MakoNativeMapSI *m, MakoNativeString *key) {
    if (!m || !m->cap) return;
    size_t i = (size_t)(mako_native_hash_str(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return;
        if (m->state[i] == MAKO_NMAP_FULL && mako_native_str_content_eq(m->keys[i], key)) {
            mako_native_string_drop_ptr(m->keys[i]);
            m->keys[i] = NULL;
            m->state[i] = MAKO_NMAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

int64_t mako_native_map_si_len_ptr(const MakoNativeMapSI *m) {
    return m ? (int64_t)m->len : 0;
}

void mako_native_map_si_drop_ptr(MakoNativeMapSI *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL) mako_native_string_drop_ptr(m->keys[i]);
    }
    free(m->state); free(m->keys); free(m->vals); free(m);
}

MakoNativeMapSI *mako_native_map_si_clone_ptr(const MakoNativeMapSI *m) {
    if (!m) return NULL;
    MakoNativeMapSI *n = mako_native_map_si_new(m->len);
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            mako_native_map_si_set_ptr(n, m->keys[i], m->vals[i]);
    }
    return n;
}

// ---- map[string]string ------------------------------------------------------

typedef struct {
    uint8_t *state;
    MakoNativeString **keys;
    MakoNativeString **vals;
    size_t cap;
    size_t len;
} MakoNativeMapSS;

static MakoNativeMapSS *mako_native_map_ss_new(size_t hint) {
    size_t cap = 8;
    while (cap < hint * 2 + 8) cap *= 2;
    MakoNativeMapSS *m = calloc(1, sizeof(*m));
    if (!m) abort();
    m->state = calloc(cap, 1);
    m->keys = calloc(cap, sizeof(*m->keys));
    m->vals = calloc(cap, sizeof(*m->vals));
    if (!m->state || !m->keys || !m->vals) abort();
    m->cap = cap; m->len = 0;
    return m;
}

MakoNativeMapSS *mako_native_map_ss_make_ptr(int64_t hint) {
    return mako_native_map_ss_new(hint > 0 ? (size_t)hint : 0);
}

static void mako_native_map_ss_rehash(MakoNativeMapSS *m, size_t ncap);

void mako_native_map_ss_set_ptr(MakoNativeMapSS *m, MakoNativeString *key,
                                MakoNativeString *val) {
    if (!m) abort();
    if (m->len * 10 >= m->cap * 7) mako_native_map_ss_rehash(m, m->cap * 2);
    size_t i = (size_t)(mako_native_hash_str(key) & (m->cap - 1));
    size_t first_tomb = (size_t)-1;
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) {
            size_t slot = first_tomb != (size_t)-1 ? first_tomb : i;
            m->state[slot] = MAKO_NMAP_FULL;
            m->keys[slot] = mako_native_string_clone_ptr(key);
            m->vals[slot] = mako_native_string_clone_ptr(val);
            m->len++;
            return;
        }
        if (m->state[i] == MAKO_NMAP_TOMB && first_tomb == (size_t)-1) first_tomb = i;
        if (m->state[i] == MAKO_NMAP_FULL && mako_native_str_content_eq(m->keys[i], key)) {
            mako_native_string_drop_ptr(m->vals[i]);
            m->vals[i] = mako_native_string_clone_ptr(val);
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
    abort();
}

static void mako_native_map_ss_rehash(MakoNativeMapSS *m, size_t ncap) {
    MakoNativeMapSS *n = mako_native_map_ss_new(ncap / 2);
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            mako_native_map_ss_set_ptr(n, m->keys[i], m->vals[i]);
    }
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL) {
            mako_native_string_drop_ptr(m->keys[i]);
            mako_native_string_drop_ptr(m->vals[i]);
        }
    }
    free(m->state); free(m->keys); free(m->vals);
    *m = *n; free(n);
}

MakoNativeString *mako_native_map_ss_get_ptr(const MakoNativeMapSS *m,
                                            MakoNativeString *key) {
    if (!m || !m->cap) return mako_native_string_literal_ptr("", 0);
    size_t i = (size_t)(mako_native_hash_str(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY)
            return mako_native_string_literal_ptr("", 0);
        if (m->state[i] == MAKO_NMAP_FULL && mako_native_str_content_eq(m->keys[i], key))
            return mako_native_string_clone_ptr(m->vals[i]);
        i = (i + 1) & (m->cap - 1);
    }
    return mako_native_string_literal_ptr("", 0);
}

int64_t mako_native_map_ss_has_ptr(const MakoNativeMapSS *m, MakoNativeString *key) {
    if (!m || !m->cap) return 0;
    size_t i = (size_t)(mako_native_hash_str(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return 0;
        if (m->state[i] == MAKO_NMAP_FULL && mako_native_str_content_eq(m->keys[i], key))
            return 1;
        i = (i + 1) & (m->cap - 1);
    }
    return 0;
}

void mako_native_map_ss_delete_ptr(MakoNativeMapSS *m, MakoNativeString *key) {
    if (!m || !m->cap) return;
    size_t i = (size_t)(mako_native_hash_str(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return;
        if (m->state[i] == MAKO_NMAP_FULL && mako_native_str_content_eq(m->keys[i], key)) {
            mako_native_string_drop_ptr(m->keys[i]);
            mako_native_string_drop_ptr(m->vals[i]);
            m->keys[i] = NULL; m->vals[i] = NULL;
            m->state[i] = MAKO_NMAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

int64_t mako_native_map_ss_len_ptr(const MakoNativeMapSS *m) {
    return m ? (int64_t)m->len : 0;
}

void mako_native_map_ss_drop_ptr(MakoNativeMapSS *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL) {
            mako_native_string_drop_ptr(m->keys[i]);
            mako_native_string_drop_ptr(m->vals[i]);
        }
    }
    free(m->state); free(m->keys); free(m->vals); free(m);
}

MakoNativeMapSS *mako_native_map_ss_clone_ptr(const MakoNativeMapSS *m) {
    if (!m) return NULL;
    MakoNativeMapSS *n = mako_native_map_ss_new(m->len);
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            mako_native_map_ss_set_ptr(n, m->keys[i], m->vals[i]);
    }
    return n;
}

// ---- map[int]float ----------------------------------------------------------

typedef struct {
    uint8_t *state;
    int64_t *keys;
    double *vals;
    size_t cap;
    size_t len;
} MakoNativeMapIF;

static MakoNativeMapIF *mako_native_map_if_new(size_t hint) {
    size_t cap = 8;
    while (cap < hint * 2 + 8) cap *= 2;
    MakoNativeMapIF *m = calloc(1, sizeof(*m));
    if (!m) abort();
    m->state = calloc(cap, 1);
    m->keys = calloc(cap, sizeof(int64_t));
    m->vals = calloc(cap, sizeof(double));
    if (!m->state || !m->keys || !m->vals) abort();
    m->cap = cap; m->len = 0;
    return m;
}

MakoNativeMapIF *mako_native_map_if_make_ptr(int64_t hint) {
    return mako_native_map_if_new(hint > 0 ? (size_t)hint : 0);
}

static void mako_native_map_if_rehash(MakoNativeMapIF *m, size_t ncap);

void mako_native_map_if_set_ptr(MakoNativeMapIF *m, int64_t key, double val) {
    if (!m) abort();
    if (m->len * 10 >= m->cap * 7) mako_native_map_if_rehash(m, m->cap * 2);
    size_t i = (size_t)(mako_native_hash_i64(key) & (m->cap - 1));
    size_t first_tomb = (size_t)-1;
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) {
            size_t slot = first_tomb != (size_t)-1 ? first_tomb : i;
            m->state[slot] = MAKO_NMAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (m->state[i] == MAKO_NMAP_TOMB && first_tomb == (size_t)-1) first_tomb = i;
        if (m->state[i] == MAKO_NMAP_FULL && m->keys[i] == key) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
    abort();
}

static void mako_native_map_if_rehash(MakoNativeMapIF *m, size_t ncap) {
    MakoNativeMapIF *n = mako_native_map_if_new(ncap / 2);
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            mako_native_map_if_set_ptr(n, m->keys[i], m->vals[i]);
    }
    free(m->state); free(m->keys); free(m->vals);
    *m = *n; free(n);
}

double mako_native_map_if_get_ptr(const MakoNativeMapIF *m, int64_t key) {
    if (!m || !m->cap) return 0.0;
    size_t i = (size_t)(mako_native_hash_i64(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return 0.0;
        if (m->state[i] == MAKO_NMAP_FULL && m->keys[i] == key) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
    return 0.0;
}

int64_t mako_native_map_if_has_ptr(const MakoNativeMapIF *m, int64_t key) {
    if (!m || !m->cap) return 0;
    size_t i = (size_t)(mako_native_hash_i64(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return 0;
        if (m->state[i] == MAKO_NMAP_FULL && m->keys[i] == key) return 1;
        i = (i + 1) & (m->cap - 1);
    }
    return 0;
}

void mako_native_map_if_delete_ptr(MakoNativeMapIF *m, int64_t key) {
    if (!m || !m->cap) return;
    size_t i = (size_t)(mako_native_hash_i64(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return;
        if (m->state[i] == MAKO_NMAP_FULL && m->keys[i] == key) {
            m->state[i] = MAKO_NMAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

int64_t mako_native_map_if_len_ptr(const MakoNativeMapIF *m) {
    return m ? (int64_t)m->len : 0;
}

void mako_native_map_if_drop_ptr(MakoNativeMapIF *m) {
    if (!m) return;
    free(m->state); free(m->keys); free(m->vals); free(m);
}

MakoNativeMapIF *mako_native_map_if_clone_ptr(const MakoNativeMapIF *m) {
    if (!m) return NULL;
    MakoNativeMapIF *n = mako_native_map_if_new(m->len);
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            mako_native_map_if_set_ptr(n, m->keys[i], m->vals[i]);
    }
    return n;
}

// ---- map[float]int ----------------------------------------------------------

typedef struct {
    uint8_t *state;
    double *keys;
    int64_t *vals;
    size_t cap;
    size_t len;
} MakoNativeMapFI;

static uint64_t mako_native_hash_f64(double k) {
    uint64_t bits;
    memcpy(&bits, &k, sizeof(bits));
    return mako_native_hash_i64((int64_t)bits);
}

static MakoNativeMapFI *mako_native_map_fi_new(size_t hint) {
    size_t cap = 8;
    while (cap < hint * 2 + 8) cap *= 2;
    MakoNativeMapFI *m = calloc(1, sizeof(*m));
    if (!m) abort();
    m->state = calloc(cap, 1);
    m->keys = calloc(cap, sizeof(double));
    m->vals = calloc(cap, sizeof(int64_t));
    if (!m->state || !m->keys || !m->vals) abort();
    m->cap = cap; m->len = 0;
    return m;
}

MakoNativeMapFI *mako_native_map_fi_make_ptr(int64_t hint) {
    return mako_native_map_fi_new(hint > 0 ? (size_t)hint : 0);
}

static void mako_native_map_fi_rehash(MakoNativeMapFI *m, size_t ncap);

void mako_native_map_fi_set_ptr(MakoNativeMapFI *m, double key, int64_t val) {
    if (!m) abort();
    if (m->len * 10 >= m->cap * 7) mako_native_map_fi_rehash(m, m->cap * 2);
    size_t i = (size_t)(mako_native_hash_f64(key) & (m->cap - 1));
    size_t first_tomb = (size_t)-1;
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) {
            size_t slot = first_tomb != (size_t)-1 ? first_tomb : i;
            m->state[slot] = MAKO_NMAP_FULL;
            m->keys[slot] = key;
            m->vals[slot] = val;
            m->len++;
            return;
        }
        if (m->state[i] == MAKO_NMAP_TOMB && first_tomb == (size_t)-1) first_tomb = i;
        if (m->state[i] == MAKO_NMAP_FULL && m->keys[i] == key) {
            m->vals[i] = val;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
    abort();
}

static void mako_native_map_fi_rehash(MakoNativeMapFI *m, size_t ncap) {
    MakoNativeMapFI *n = mako_native_map_fi_new(ncap / 2);
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            mako_native_map_fi_set_ptr(n, m->keys[i], m->vals[i]);
    }
    free(m->state); free(m->keys); free(m->vals);
    *m = *n; free(n);
}

int64_t mako_native_map_fi_get_ptr(const MakoNativeMapFI *m, double key) {
    if (!m || !m->cap) return 0;
    size_t i = (size_t)(mako_native_hash_f64(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return 0;
        if (m->state[i] == MAKO_NMAP_FULL && m->keys[i] == key) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
    return 0;
}

int64_t mako_native_map_fi_has_ptr(const MakoNativeMapFI *m, double key) {
    if (!m || !m->cap) return 0;
    size_t i = (size_t)(mako_native_hash_f64(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return 0;
        if (m->state[i] == MAKO_NMAP_FULL && m->keys[i] == key) return 1;
        i = (i + 1) & (m->cap - 1);
    }
    return 0;
}

void mako_native_map_fi_delete_ptr(MakoNativeMapFI *m, double key) {
    if (!m || !m->cap) return;
    size_t i = (size_t)(mako_native_hash_f64(key) & (m->cap - 1));
    for (size_t n = 0; n < m->cap; ++n) {
        if (m->state[i] == MAKO_NMAP_EMPTY) return;
        if (m->state[i] == MAKO_NMAP_FULL && m->keys[i] == key) {
            m->state[i] = MAKO_NMAP_TOMB;
            m->len--;
            return;
        }
        i = (i + 1) & (m->cap - 1);
    }
}

int64_t mako_native_map_fi_len_ptr(const MakoNativeMapFI *m) {
    return m ? (int64_t)m->len : 0;
}

void mako_native_map_fi_drop_ptr(MakoNativeMapFI *m) {
    if (!m) return;
    free(m->state); free(m->keys); free(m->vals); free(m);
}

MakoNativeMapFI *mako_native_map_fi_clone_ptr(const MakoNativeMapFI *m) {
    if (!m) return NULL;
    MakoNativeMapFI *n = mako_native_map_fi_new(m->len);
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            mako_native_map_fi_set_ptr(n, m->keys[i], m->vals[i]);
    }
    return n;
}

// maps_keys for map[int]int → owned []int of full-slot keys.
MakoNativeIntSlice *mako_native_maps_keys_ii_ptr(const MakoNativeMapII *m) {
    int64_t n = m ? (int64_t)m->len : 0;
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr((size_t)n, (size_t)n);
    if (!m || !n) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            out->data[j++] = m->keys[i];
    }
    out->len = j;
    return out;
}

// maps_keys for map[string]* → owned []string of full-slot keys.
MakoNativeStringSlice *mako_native_maps_keys_si_ptr(const MakoNativeMapSI *m) {
    size_t n = m ? m->len : 0;
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(n, n);
    if (!m || !n) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            out->data[j++] = mako_native_string_clone_ptr(m->keys[i]);
    }
    out->len = j;
    return out;
}

MakoNativeStringSlice *mako_native_maps_keys_ss_ptr(const MakoNativeMapSS *m) {
    size_t n = m ? m->len : 0;
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(n, n);
    if (!m || !n) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            out->data[j++] = mako_native_string_clone_ptr(m->keys[i]);
    }
    out->len = j;
    return out;
}

// maps_values: for int-value maps → []int; for pointer maps → ptr slice of values.
MakoNativeIntSlice *mako_native_maps_values_ii_ptr(const MakoNativeMapII *m) {
    int64_t n = m ? (int64_t)m->len : 0;
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr((size_t)n, (size_t)n);
    if (!m || !n) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            out->data[j++] = m->vals[i];
    }
    out->len = j;
    return out;
}

// Defined in native_bridge.c (same archive).
typedef struct MakoNativePtrSlice MakoNativePtrSlice;
extern MakoNativePtrSlice *mako_native_ptr_slice_make(int64_t len, int64_t cap);
extern void mako_native_ptr_slice_set(MakoNativePtrSlice *s, int64_t i, void *elem);

MakoNativePtrSlice *mako_native_maps_values_si_ptr(const MakoNativeMapSI *m) {
    int64_t n = m ? (int64_t)m->len : 0;
    MakoNativePtrSlice *out = mako_native_ptr_slice_make(n, n);
    if (!m || !n || !out) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL) {
            mako_native_ptr_slice_set(out, (int64_t)j, (void *)(uintptr_t)m->vals[i]);
            j++;
        }
    }
    return out;
}

// maps_values for map[int]* pointer maps (MapIPtr).
MakoNativePtrSlice *mako_native_maps_values_ii_as_ptr_slice(const MakoNativeMapII *m) {
    int64_t n = m ? (int64_t)m->len : 0;
    MakoNativePtrSlice *out = mako_native_ptr_slice_make(n, n);
    if (!m || !n || !out) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL) {
            mako_native_ptr_slice_set(out, (int64_t)j, (void *)(uintptr_t)m->vals[i]);
            j++;
        }
    }
    return out;
}

MakoNativeIntSlice *mako_native_maps_values_si_as_i64_ptr(const MakoNativeMapSI *m) {
    int64_t n = m ? (int64_t)m->len : 0;
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr((size_t)n, (size_t)n);
    if (!m || !n) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            out->data[j++] = m->vals[i];
    }
    out->len = j;
    return out;
}

MakoNativeStringSlice *mako_native_maps_values_ss_ptr(const MakoNativeMapSS *m) {
    size_t n = m ? m->len : 0;
    MakoNativeStringSlice *out = mako_native_string_slice_make_ptr(n, n);
    if (!m || !n) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            out->data[j++] = mako_native_string_clone_ptr(m->vals[i]);
    }
    out->len = j;
    return out;
}

int64_t mako_native_maps_equal_ii(const MakoNativeMapII *a, const MakoNativeMapII *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; ++i) {
        if (a->state[i] != MAKO_NMAP_FULL) continue;
        int64_t k = a->keys[i];
        if (!mako_native_map_ii_has_ptr(b, k)) return 0;
        if (mako_native_map_ii_get_ptr(b, k) != a->vals[i]) return 0;
    }
    return 1;
}

int64_t mako_native_maps_equal_si(const MakoNativeMapSI *a, const MakoNativeMapSI *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; ++i) {
        if (a->state[i] != MAKO_NMAP_FULL) continue;
        MakoNativeString *k = a->keys[i];
        if (!mako_native_map_si_has_ptr(b, k)) return 0;
        if (mako_native_map_si_get_ptr(b, k) != a->vals[i]) return 0;
    }
    return 1;
}

int64_t mako_native_maps_equal_ss(const MakoNativeMapSS *a, const MakoNativeMapSS *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; ++i) {
        if (a->state[i] != MAKO_NMAP_FULL) continue;
        MakoNativeString *k = a->keys[i];
        if (!mako_native_map_ss_has_ptr(b, k)) return 0;
        MakoNativeString *va = a->vals[i];
        MakoNativeString *vb = mako_native_map_ss_get_ptr(b, k);
        int eq = mako_native_str_content_eq(va, vb);
        mako_native_string_drop_ptr(vb);
        if (!eq) return 0;
    }
    return 1;
}

/* Deep-equal heap struct values (flat Str / i64 / f64 fields). str_mask bit i
 * means field i is a MakoNativeString*; otherwise an i64/f64 bit pattern. */
static int64_t mako_native_struct_fields_eq(
    void *a, void *b, int64_t nfields, int64_t str_mask
) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (nfields < 0 || nfields > 62) return 0;
    int64_t *fa = (int64_t *)a;
    int64_t *fb = (int64_t *)b;
    for (int64_t i = 0; i < nfields; i++) {
        if (str_mask & (1LL << i)) {
            MakoNativeString *sa = (MakoNativeString *)(intptr_t)fa[i];
            MakoNativeString *sb = (MakoNativeString *)(intptr_t)fb[i];
            if (!sa && !sb) continue;
            if (!sa || !sb) return 0;
            if (!mako_native_str_content_eq(sa, sb)) return 0;
        } else {
            if (fa[i] != fb[i]) return 0;
        }
    }
    return 1;
}

int64_t mako_native_maps_equal_ii_deep(
    const MakoNativeMapII *a, const MakoNativeMapII *b, int64_t nfields, int64_t str_mask
) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; ++i) {
        if (a->state[i] != MAKO_NMAP_FULL) continue;
        int64_t k = a->keys[i];
        if (!mako_native_map_ii_has_ptr(b, k)) return 0;
        void *va = (void *)(intptr_t)a->vals[i];
        void *vb = (void *)(intptr_t)mako_native_map_ii_get_ptr(b, k);
        if (!mako_native_struct_fields_eq(va, vb, nfields, str_mask)) return 0;
    }
    return 1;
}

int64_t mako_native_maps_equal_si_deep(
    const MakoNativeMapSI *a, const MakoNativeMapSI *b, int64_t nfields, int64_t str_mask
) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; ++i) {
        if (a->state[i] != MAKO_NMAP_FULL) continue;
        MakoNativeString *k = a->keys[i];
        if (!mako_native_map_si_has_ptr(b, k)) return 0;
        void *va = (void *)(intptr_t)a->vals[i];
        void *vb = (void *)(intptr_t)mako_native_map_si_get_ptr(b, k);
        if (!mako_native_struct_fields_eq(va, vb, nfields, str_mask)) return 0;
    }
    return 1;
}

void mako_native_maps_clear_ii(MakoNativeMapII *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; ++i) m->state[i] = MAKO_NMAP_EMPTY;
    m->len = 0;
}

void mako_native_maps_clear_si(MakoNativeMapSI *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL) mako_native_string_drop_ptr(m->keys[i]);
        m->state[i] = MAKO_NMAP_EMPTY;
    }
    m->len = 0;
}

void mako_native_maps_clear_ss(MakoNativeMapSS *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL) {
            mako_native_string_drop_ptr(m->keys[i]);
            mako_native_string_drop_ptr(m->vals[i]);
        }
        m->state[i] = MAKO_NMAP_EMPTY;
    }
    m->len = 0;
}

void mako_native_maps_copy_ii(MakoNativeMapII *dst, const MakoNativeMapII *src) {
    mako_native_maps_clear_ii(dst);
    if (!src) return;
    for (size_t i = 0; i < src->cap; ++i) {
        if (src->state[i] == MAKO_NMAP_FULL)
            mako_native_map_ii_set_ptr(dst, src->keys[i], src->vals[i]);
    }
}

void mako_native_maps_copy_si(MakoNativeMapSI *dst, const MakoNativeMapSI *src) {
    mako_native_maps_clear_si(dst);
    if (!src) return;
    for (size_t i = 0; i < src->cap; ++i) {
        if (src->state[i] == MAKO_NMAP_FULL)
            mako_native_map_si_set_ptr(dst, src->keys[i], src->vals[i]);
    }
}

void mako_native_maps_copy_ss(MakoNativeMapSS *dst, const MakoNativeMapSS *src) {
    mako_native_maps_clear_ss(dst);
    if (!src) return;
    for (size_t i = 0; i < src->cap; ++i) {
        if (src->state[i] == MAKO_NMAP_FULL)
            mako_native_map_ss_set_ptr(dst, src->keys[i], src->vals[i]);
    }
}

// Map slot iteration helpers for `for k, v in range m`.
int64_t mako_native_map_ii_cap_ptr(const MakoNativeMapII *m) {
    return m ? (int64_t)m->cap : 0;
}

int64_t mako_native_map_ii_slot_full_ptr(const MakoNativeMapII *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap) return 0;
    return m->state[i] == MAKO_NMAP_FULL ? 1 : 0;
}

int64_t mako_native_map_ii_slot_key_ptr(const MakoNativeMapII *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap) return 0;
    return m->keys[i];
}

int64_t mako_native_map_ii_slot_val_ptr(const MakoNativeMapII *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap) return 0;
    return m->vals[i];
}

int64_t mako_native_map_si_cap_ptr(const MakoNativeMapSI *m) {
    return m ? (int64_t)m->cap : 0;
}

int64_t mako_native_map_si_slot_full_ptr(const MakoNativeMapSI *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap) return 0;
    return m->state[i] == MAKO_NMAP_FULL ? 1 : 0;
}

MakoNativeString *mako_native_map_si_slot_key_ptr(const MakoNativeMapSI *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap || m->state[i] != MAKO_NMAP_FULL)
        return mako_native_string_literal_ptr("", 0);
    return mako_native_string_clone_ptr(m->keys[i]);
}

int64_t mako_native_map_si_slot_val_ptr(const MakoNativeMapSI *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap) return 0;
    return m->vals[i];
}

// MapSS slot iteration + LLVM value-ABI string returns.
int64_t mako_native_map_ss_cap_ptr(const MakoNativeMapSS *m) {
    return m ? (int64_t)m->cap : 0;
}

int64_t mako_native_map_ss_slot_full_ptr(const MakoNativeMapSS *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap) return 0;
    return m->state[i] == MAKO_NMAP_FULL ? 1 : 0;
}

MakoNativeString *mako_native_map_ss_slot_key_ptr(const MakoNativeMapSS *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap || m->state[i] != MAKO_NMAP_FULL)
        return mako_native_string_literal_ptr("", 0);
    return mako_native_string_clone_ptr(m->keys[i]);
}

MakoNativeString *mako_native_map_ss_slot_val_ptr(const MakoNativeMapSS *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap || m->state[i] != MAKO_NMAP_FULL)
        return mako_native_string_literal_ptr("", 0);
    return mako_native_string_clone_ptr(m->vals[i]);
}

// Value-ABI wrappers for LLVM (string struct by value; free the heap header).
MakoNativeString mako_native_map_ss_get_val(const MakoNativeMapSS *m, MakoNativeString key) {
    MakoNativeString *p = mako_native_map_ss_get_ptr(m, &key);
    MakoNativeString out = p ? *p : (MakoNativeString){NULL, 0};
    free(p);
    return out;
}

MakoNativeString mako_native_map_ss_slot_key_val(const MakoNativeMapSS *m, int64_t i) {
    MakoNativeString *p = mako_native_map_ss_slot_key_ptr(m, i);
    MakoNativeString out = p ? *p : (MakoNativeString){NULL, 0};
    free(p);
    return out;
}

MakoNativeString mako_native_map_ss_slot_val_val(const MakoNativeMapSS *m, int64_t i) {
    MakoNativeString *p = mako_native_map_ss_slot_val_ptr(m, i);
    MakoNativeString out = p ? *p : (MakoNativeString){NULL, 0};
    free(p);
    return out;
}

MakoNativeString mako_native_map_si_slot_key_val(const MakoNativeMapSI *m, int64_t i) {
    MakoNativeString *p = mako_native_map_si_slot_key_ptr(m, i);
    MakoNativeString out = p ? *p : (MakoNativeString){NULL, 0};
    free(p);
    return out;
}

// MapIF / MapFI slot iteration.
int64_t mako_native_map_if_cap_ptr(const MakoNativeMapIF *m) {
    return m ? (int64_t)m->cap : 0;
}

int64_t mako_native_map_if_slot_full_ptr(const MakoNativeMapIF *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap) return 0;
    return m->state[i] == MAKO_NMAP_FULL ? 1 : 0;
}

int64_t mako_native_map_if_slot_key_ptr(const MakoNativeMapIF *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap) return 0;
    return m->keys[i];
}

double mako_native_map_if_slot_val_ptr(const MakoNativeMapIF *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap) return 0.0;
    return m->vals[i];
}

int64_t mako_native_map_fi_cap_ptr(const MakoNativeMapFI *m) {
    return m ? (int64_t)m->cap : 0;
}

int64_t mako_native_map_fi_slot_full_ptr(const MakoNativeMapFI *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap) return 0;
    return m->state[i] == MAKO_NMAP_FULL ? 1 : 0;
}

double mako_native_map_fi_slot_key_ptr(const MakoNativeMapFI *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap) return 0.0;
    return m->keys[i];
}

int64_t mako_native_map_fi_slot_val_ptr(const MakoNativeMapFI *m, int64_t i) {
    if (!m || i < 0 || (uint64_t)i >= m->cap) return 0;
    return m->vals[i];
}

/* maps_keys / maps_values for float maps */
MakoNativeIntSlice *mako_native_maps_keys_if_ptr(const MakoNativeMapIF *m) {
    int64_t n = m ? (int64_t)m->len : 0;
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr((size_t)n, (size_t)n);
    if (!m || !n) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            out->data[j++] = m->keys[i];
    }
    out->len = j;
    return out;
}

MakoNativeFloatSlice *mako_native_maps_values_if_ptr(const MakoNativeMapIF *m) {
    int64_t n = m ? (int64_t)m->len : 0;
    MakoNativeFloatSlice *out = mako_native_float_slice_make_ptr(n, n);
    if (!m || !n) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            out->data[j++] = m->vals[i];
    }
    out->len = j;
    return out;
}

MakoNativeFloatSlice *mako_native_maps_keys_fi_ptr(const MakoNativeMapFI *m) {
    int64_t n = m ? (int64_t)m->len : 0;
    MakoNativeFloatSlice *out = mako_native_float_slice_make_ptr(n, n);
    if (!m || !n) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            out->data[j++] = m->keys[i];
    }
    out->len = j;
    return out;
}

MakoNativeIntSlice *mako_native_maps_values_fi_ptr(const MakoNativeMapFI *m) {
    int64_t n = m ? (int64_t)m->len : 0;
    MakoNativeIntSlice *out = mako_native_int_slice_make_ptr((size_t)n, (size_t)n);
    if (!m || !n) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL)
            out->data[j++] = m->vals[i];
    }
    out->len = j;
    return out;
}

int64_t mako_native_maps_equal_if(const MakoNativeMapIF *a, const MakoNativeMapIF *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; ++i) {
        if (a->state[i] != MAKO_NMAP_FULL) continue;
        int64_t k = a->keys[i];
        if (!mako_native_map_if_has_ptr(b, k)) return 0;
        if (mako_native_map_if_get_ptr(b, k) != a->vals[i]) return 0;
    }
    return 1;
}

int64_t mako_native_maps_equal_fi(const MakoNativeMapFI *a, const MakoNativeMapFI *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; ++i) {
        if (a->state[i] != MAKO_NMAP_FULL) continue;
        double k = a->keys[i];
        if (!mako_native_map_fi_has_ptr(b, k)) return 0;
        if (mako_native_map_fi_get_ptr(b, k) != a->vals[i]) return 0;
    }
    return 1;
}

void mako_native_maps_clear_if(MakoNativeMapIF *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; ++i) m->state[i] = MAKO_NMAP_EMPTY;
    m->len = 0;
}

void mako_native_maps_clear_fi(MakoNativeMapFI *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; ++i) m->state[i] = MAKO_NMAP_EMPTY;
    m->len = 0;
}

void mako_native_maps_copy_if(MakoNativeMapIF *dst, const MakoNativeMapIF *src) {
    if (!dst || !src) return;
    mako_native_maps_clear_if(dst);
    for (size_t i = 0; i < src->cap; ++i) {
        if (src->state[i] == MAKO_NMAP_FULL)
            mako_native_map_if_set_ptr(dst, src->keys[i], src->vals[i]);
    }
}

void mako_native_maps_copy_fi(MakoNativeMapFI *dst, const MakoNativeMapFI *src) {
    if (!dst || !src) return;
    mako_native_maps_clear_fi(dst);
    for (size_t i = 0; i < src->cap; ++i) {
        if (src->state[i] == MAKO_NMAP_FULL)
            mako_native_map_fi_set_ptr(dst, src->keys[i], src->vals[i]);
    }
}

// ---- ShareInt (RC int cell; pointer ABI for shared IR) ----------------------
// Single heap cell {value, refcount}. Clone bumps RC and returns the same
// pointer; drop decrements and frees at zero. Matches mako_share_* semantics
// for differential parity with the C backend.

typedef struct {
    int64_t value;
    int64_t refcount;
} MakoNativeShareInt;

MakoNativeShareInt *mako_native_share_int(int64_t v) {
    MakoNativeShareInt *s = malloc(sizeof(*s));
    if (!s) abort();
    s->value = v;
    s->refcount = 1;
    return s;
}

MakoNativeShareInt *mako_native_share_clone_ptr(const MakoNativeShareInt *s) {
    if (!s) return NULL;
    __atomic_add_fetch(&((MakoNativeShareInt *)s)->refcount, (int64_t)1, __ATOMIC_SEQ_CST);
    return (MakoNativeShareInt *)s;
}

int64_t mako_native_share_get_ptr(const MakoNativeShareInt *s) {
    return s ? s->value : 0;
}

void mako_native_share_set_ptr(MakoNativeShareInt *s, int64_t v) {
    if (s) {
        __atomic_store_n(&s->value, v, __ATOMIC_SEQ_CST);
    }
}

void mako_native_share_drop_ptr(MakoNativeShareInt *s) {
    if (!s) return;
    if (__atomic_sub_fetch(&s->refcount, (int64_t)1, __ATOMIC_SEQ_CST) <= 0) {
        free(s);
    }
}

// ---- path_join / now_ms / str_len (pointer ABI) -----------------------------

int64_t mako_native_now_ms(void) {
#if defined(_WIN32)
    return 0;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
#endif
}

int64_t mako_native_now_ns(void) {
#if defined(_WIN32)
    return 0;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
#endif
}

int64_t mako_native_str_len_ptr(const MakoNativeString *s) {
    return s ? (int64_t)mako_str_raw_len(s->len) : 0;
}

MakoNativeString *mako_native_path_join_ptr(const MakoNativeString *a,
                                           const MakoNativeString *b) {
    size_t al = a && a->data ? mako_str_raw_len(a->len) : 0;
    size_t bl = b && b->data ? mako_str_raw_len(b->len) : 0;
    int need_sep = 1;
    if (al == 0) {
        return mako_native_string_clone_ptr(b);
    }
    if (bl == 0) {
        return mako_native_string_clone_ptr(a);
    }
    if (a->data[al - 1] == '/') need_sep = 0;
    if (b->data[0] == '/') need_sep = 0;
    size_t n = al + bl + (need_sep ? 1 : 0);
    char *d = malloc(n + 1);
    if (!d) abort();
    memcpy(d, a->data, al);
    size_t o = al;
    if (need_sep) d[o++] = '/';
    memcpy(d + o, b->data, bl);
    d[n] = '\0';
    MakoNativeString *out = malloc(sizeof(*out));
    if (!out) abort();
    out->data = d;
    out->len = n;
    return out;
}

// Value-ABI variants for the LLVM backend.
MakoNativeString mako_native_path_join(MakoNativeString a, MakoNativeString b) {
    MakoNativeString *p = mako_native_path_join_ptr(&a, &b);
    MakoNativeString out = *p;
    free(p);
    return out;
}

int64_t mako_native_str_len(MakoNativeString s) {
    return (int64_t)mako_str_raw_len(s.len);
}

// ---- args / env / files / base64 (pointer ABI) ------------------------------

// Weak argv symbols — populated by the host main when the full runtime links.
char **mako_argv_g __attribute__((weak)) = NULL;
// mako_argc_g is defined earlier as a weak int.

MakoNativeString *mako_native_arg_get_ptr(int64_t i) {
    extern char **mako_argv_g;
    extern int mako_argc_g;
    if (i < 0 || i >= (int64_t)mako_argc_g || !mako_argv_g) {
        return mako_native_string_literal_ptr("", 0);
    }
    const char *s = mako_argv_g[i] ? mako_argv_g[i] : "";
    return mako_native_string_literal_ptr(s, strlen(s));
}

MakoNativeString *mako_native_env_get_ptr(const MakoNativeString *key) {
    const char *k = (key && key->data) ? key->data : "";
    const char *v = getenv(k);
    if (!v) return mako_native_string_literal_ptr("", 0);
    return mako_native_string_literal_ptr(v, strlen(v));
}

int64_t mako_native_env_set_ptr(const MakoNativeString *key, const MakoNativeString *val) {
    const char *k = (key && key->data) ? key->data : "";
    const char *v = (val && val->data) ? val->data : "";
#if defined(__wasi__)
    (void)k;
    (void)v;
    return -1;
#else
    return setenv(k, v, 1) == 0 ? 0 : -1;
#endif
}

MakoNativeString *mako_native_read_file_ptr(const MakoNativeString *path) {
    if (!path || !path->data) return mako_native_string_literal_ptr("", 0);
    FILE *f = fopen(path->data, "rb");
    if (!f) return mako_native_string_literal_ptr("", 0);
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return mako_native_string_literal_ptr("", 0);
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return mako_native_string_literal_ptr("", 0);
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        abort();
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    MakoNativeString *out = malloc(sizeof(*out));
    if (!out) abort();
    out->data = buf;
    out->len = n;
    return out;
}

int64_t mako_native_write_file_ptr(const MakoNativeString *path,
                                   const MakoNativeString *contents) {
    if (!path || !path->data) return -1;
    /* Reject embedded NUL — match mako_write_file / mako_fs_path_buf. */
    size_t plen = mako_str_raw_len(path->len);
    for (size_t i = 0; i < plen; i++) {
        if (path->data[i] == 0) return -1;
    }
    char pbuf[4096];
    if (plen >= sizeof(pbuf)) return -1;
    memcpy(pbuf, path->data, plen);
    pbuf[plen] = 0;
    FILE *f = fopen(pbuf, "wb");
    if (!f) return -1;
    size_t n = contents && contents->data ? mako_str_raw_len(contents->len) : 0;
    size_t w = n ? fwrite(contents->data, 1, n, f) : 0;
    fclose(f);
    return (w == n) ? 0 : -1;
}

static const char MAKO_NATIVE_B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

MakoNativeString *mako_native_base64_encode_ptr(const MakoNativeString *s) {
    size_t n = s && s->data ? mako_str_raw_len(s->len) : 0;
    size_t out_len = 4 * ((n + 2) / 3);
    char *o = malloc(out_len + 1);
    if (!o) abort();
    size_t j = 0;
    const char *data = s && s->data ? s->data : "";
    for (size_t i = 0; i < n; i += 3) {
        unsigned int v = (unsigned char)data[i] << 16;
        if (i + 1 < n) v |= (unsigned char)data[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned char)data[i + 2];
        o[j++] = MAKO_NATIVE_B64[(v >> 18) & 63];
        o[j++] = MAKO_NATIVE_B64[(v >> 12) & 63];
        o[j++] = (i + 1 < n) ? MAKO_NATIVE_B64[(v >> 6) & 63] : '=';
        o[j++] = (i + 2 < n) ? MAKO_NATIVE_B64[v & 63] : '=';
    }
    o[j] = 0;
    MakoNativeString *out = malloc(sizeof(*out));
    if (!out) abort();
    out->data = o;
    out->len = j;
    return out;
}

static int mako_native_b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

MakoNativeString *mako_native_base64_decode_ptr(const MakoNativeString *s) {
    size_t n = s && s->data ? mako_str_raw_len(s->len) : 0;
    size_t cap = n / 4 * 3 + 4;
    char *o = malloc(cap + 1);
    if (!o) abort();
    size_t j = 0;
    unsigned int buf = 0;
    int bits = 0;
    const char *data = s && s->data ? s->data : "";
    for (size_t i = 0; i < n; i++) {
        if (data[i] == '=') break;
        int v = mako_native_b64_val(data[i]);
        if (v < 0) continue;
        buf = (buf << 6) | (unsigned int)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            o[j++] = (char)((buf >> bits) & 0xff);
        }
    }
    o[j] = 0;
    MakoNativeString *out = malloc(sizeof(*out));
    if (!out) abort();
    out->data = o;
    out->len = j;
    return out;
}

// Value-ABI aliases for LLVM.
MakoNativeString mako_native_arg_get(int64_t i) {
    MakoNativeString *p = mako_native_arg_get_ptr(i);
    MakoNativeString out = *p;
    free(p);
    return out;
}

MakoNativeString mako_native_env_get(MakoNativeString key) {
    MakoNativeString *p = mako_native_env_get_ptr(&key);
    MakoNativeString out = *p;
    free(p);
    return out;
}

int64_t mako_native_env_set(MakoNativeString key, MakoNativeString val) {
    return mako_native_env_set_ptr(&key, &val);
}

MakoNativeString mako_native_read_file(MakoNativeString path) {
    MakoNativeString *p = mako_native_read_file_ptr(&path);
    MakoNativeString out = *p;
    free(p);
    return out;
}

int64_t mako_native_write_file(MakoNativeString path, MakoNativeString contents) {
    return mako_native_write_file_ptr(&path, &contents);
}

MakoNativeString mako_native_base64_encode(MakoNativeString s) {
    MakoNativeString *p = mako_native_base64_encode_ptr(&s);
    MakoNativeString out = *p;
    free(p);
    return out;
}

MakoNativeString mako_native_base64_decode(MakoNativeString s) {
    MakoNativeString *p = mako_native_base64_decode_ptr(&s);
    MakoNativeString out = *p;
    free(p);
    return out;
}

int64_t mako_native_str_contains_ptr(const MakoNativeString *hay,
                                     const MakoNativeString *needle) {
    size_t nlen = needle && needle->data ? mako_str_raw_len(needle->len) : 0;
    if (!needle || nlen == 0) return 1;
    size_t hlen = hay && hay->data ? mako_str_raw_len(hay->len) : 0;
    if (!hay || !hay->data || nlen > hlen) return 0;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        if (memcmp(hay->data + i, needle->data, nlen) == 0) return 1;
    }
    return 0;
}

int64_t mako_native_str_contains(MakoNativeString hay, MakoNativeString needle) {
    return mako_native_str_contains_ptr(&hay, &needle);
}

void mako_native_assert(int64_t cond) {
    if (!cond) {
        fputs("assert failed\n", stderr);
        fflush(stderr);
        abort();
    }
}

void mako_native_assert_eq(int64_t a, int64_t b) {
    if (a != b) {
        fprintf(stderr, "assert_eq failed: got %lld, want %lld\n",
                (long long)a, (long long)b);
        fflush(stderr);
        abort();
    }
}

// ---- []bool (thin wrappers over []byte: 0/1 elements) -----------------------

typedef MakoNativeByteSlice MakoNativeBoolSlice;

MakoNativeBoolSlice *mako_native_bool_slice_make_ptr(int64_t len, int64_t cap) {
    return mako_native_byte_slice_make_ptr(len, cap);
}

int64_t mako_native_bool_slice_len_ptr(const MakoNativeBoolSlice *v) {
    return mako_native_byte_slice_len_ptr(v);
}

int64_t mako_native_bool_slice_cap_ptr(const MakoNativeBoolSlice *v) {
    return mako_native_byte_slice_cap_ptr(v);
}

int64_t mako_native_bool_slice_get_ptr(const MakoNativeBoolSlice *v, int64_t index) {
    return mako_native_byte_slice_get_ptr(v, index) != 0 ? 1 : 0;
}

void mako_native_bool_slice_set_ptr(MakoNativeBoolSlice *v, int64_t index, int64_t element) {
    mako_native_byte_slice_set_ptr(v, index, element ? 1 : 0);
}

MakoNativeBoolSlice *mako_native_bool_slice_append_ptr(MakoNativeBoolSlice *v, int64_t element) {
    return mako_native_byte_slice_append_ptr(v, element ? 1 : 0);
}

MakoNativeBoolSlice *mako_native_bool_slice_clone_ptr(const MakoNativeBoolSlice *v) {
    return mako_native_byte_slice_clone_ptr(v);
}

void mako_native_bool_slice_drop_ptr(MakoNativeBoolSlice *v) {
    mako_native_byte_slice_drop_ptr(v);
}

int64_t mako_native_bool_slice_copy_ptr(MakoNativeBoolSlice *dst, const MakoNativeBoolSlice *src) {
    return mako_native_byte_slice_copy_ptr(dst, src);
}

MakoNativeFloatSlice *mako_native_maps_values_ii_as_float_ptr(const MakoNativeMapII *m) {
    int64_t n = m ? (int64_t)m->len : 0;
    MakoNativeFloatSlice *out = mako_native_float_slice_make_ptr(n, n);
    if (!m || !n) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL) {
            union { int64_t i; double d; } u;
            u.i = m->vals[i];
            out->data[j++] = u.d;
        }
    }
    out->len = j;
    return out;
}

MakoNativeFloatSlice *mako_native_maps_values_si_as_float_ptr(const MakoNativeMapSI *m) {
    int64_t n = m ? (int64_t)m->len : 0;
    MakoNativeFloatSlice *out = mako_native_float_slice_make_ptr(n, n);
    if (!m || !n) return out;
    size_t j = 0;
    for (size_t i = 0; i < m->cap; ++i) {
        if (m->state[i] == MAKO_NMAP_FULL) {
            union { int64_t i; double d; } u;
            u.i = m->vals[i];
            out->data[j++] = u.d;
        }
    }
    out->len = j;
    return out;
}
