#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    return mako_native_string_literal_ptr(value->data, value->len);
}

MakoNativeString *mako_native_string_concat_ptr(const MakoNativeString *left,
                                                const MakoNativeString *right) {
    MakoNativeString *out = malloc(sizeof(*out));
    if (out == NULL) abort();
    if (right->len > SIZE_MAX - left->len) abort();
    out->len = left->len + right->len;
    out->data = malloc(out->len + 1);
    if (out->data == NULL) abort();
    memcpy((char *)out->data, left->data, left->len);
    memcpy((char *)out->data + left->len, right->data, right->len);
    ((char *)out->data)[out->len] = '\0';
    return out;
}

int32_t mako_native_string_equal_ptr(const MakoNativeString *left,
                                     const MakoNativeString *right) {
    return left->len == right->len && memcmp(left->data, right->data, left->len) == 0;
}

void mako_native_string_print_ptr(const MakoNativeString *value) {
    fwrite(value->data, 1, value->len, stdout);
    fputc('\n', stdout);
}

void mako_native_string_drop_ptr(MakoNativeString *value) {
    if (value == NULL) return;
    free((void *)value->data);
    free(value);
}

MakoNativeString mako_native_string_clone(MakoNativeString value) {
    // Null-safe (see the pointer-ABI clone): an inactive payload slot is
    // {NULL, 0} and must clone to {NULL, 0}, never allocate.
    if (value.data == NULL) return (MakoNativeString){NULL, 0};
    char *data = malloc(value.len + 1);
    if (data == NULL) abort();
    memcpy(data, value.data, value.len);
    data[value.len] = '\0';
    return (MakoNativeString){data, value.len};
}

MakoNativeString mako_native_string_concat(MakoNativeString left,
                                           MakoNativeString right) {
    if (right.len > SIZE_MAX - left.len) abort();
    size_t len = left.len + right.len;
    char *data = malloc(len + 1);
    if (data == NULL) abort();
    memcpy(data, left.data, left.len);
    memcpy(data + left.len, right.data, right.len);
    data[len] = '\0';
    return (MakoNativeString){data, len};
}

int32_t mako_native_string_equal(MakoNativeString left,
                                 MakoNativeString right) {
    return left.len == right.len && memcmp(left.data, right.data, left.len) == 0;
}

void mako_native_string_print(MakoNativeString value) {
    fwrite(value.data, 1, value.len, stdout);
    fputc('\n', stdout);
}

void mako_native_string_drop(MakoNativeString value) {
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
    return (int64_t)value->len;
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

int64_t mako_native_string_slice_len_ptr(const MakoNativeStringSlice *value) {
    return (int64_t)value->len;
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
