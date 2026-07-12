/* Checked integer arithmetic — trap on overflow instead of silent wrap.
 * Prefer GCC/Clang __builtin_*_overflow; MSVC-style manual checks as fallback.
 */
#ifndef MAKO_OVERFLOW_H
#define MAKO_OVERFLOW_H

#include "mako_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Overflow modes: 0=wrap (C default), 1=trap/abort, 2=ignore (same as wrap but
 * documented), 3=checked Result via builtins only. */
#ifndef MAKO_OVERFLOW_MODE
#define MAKO_OVERFLOW_MODE 0
#endif

static inline void mako_overflow_trap(const char *op) {
    char buf[96];
    snprintf(buf, sizeof(buf), "integer overflow in %s", op ? op : "arithmetic");
    mako_abort(buf);
}

/* --- i64 checked ops: return 1 on success, 0 on overflow; *out set only on success --- */

static inline int mako_checked_add_i64(int64_t a, int64_t b, int64_t *out) {
#if defined(__GNUC__) || defined(__clang__)
    return !__builtin_add_overflow(a, b, out);
#else
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return 0;
    if (out) *out = a + b;
    return 1;
#endif
}

static inline int mako_checked_sub_i64(int64_t a, int64_t b, int64_t *out) {
#if defined(__GNUC__) || defined(__clang__)
    return !__builtin_sub_overflow(a, b, out);
#else
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) return 0;
    if (out) *out = a - b;
    return 1;
#endif
}

static inline int mako_checked_mul_i64(int64_t a, int64_t b, int64_t *out) {
#if defined(__GNUC__) || defined(__clang__)
    return !__builtin_mul_overflow(a, b, out);
#else
    if (a == 0 || b == 0) {
        if (out) *out = 0;
        return 1;
    }
    if (a == -1 && b == INT64_MIN) return 0;
    if (b == -1 && a == INT64_MIN) return 0;
    if (a > 0) {
        if (b > 0) {
            if (a > INT64_MAX / b) return 0;
        } else {
            if (b < INT64_MIN / a) return 0;
        }
    } else {
        if (b > 0) {
            if (a < INT64_MIN / b) return 0;
        } else {
            if (a != 0 && b < INT64_MAX / a) return 0;
        }
    }
    if (out) *out = a * b;
    return 1;
#endif
}

/* Codegen helpers: compute with trap on overflow when MAKO_OVERFLOW_MODE==1. */

static inline int64_t mako_add_i64(int64_t a, int64_t b) {
#if MAKO_OVERFLOW_MODE == 1
    int64_t r = 0;
    if (!mako_checked_add_i64(a, b, &r)) mako_overflow_trap("add");
    return r;
#else
    return a + b;
#endif
}

static inline int64_t mako_sub_i64(int64_t a, int64_t b) {
#if MAKO_OVERFLOW_MODE == 1
    int64_t r = 0;
    if (!mako_checked_sub_i64(a, b, &r)) mako_overflow_trap("sub");
    return r;
#else
    return a - b;
#endif
}

static inline int64_t mako_mul_i64(int64_t a, int64_t b) {
#if MAKO_OVERFLOW_MODE == 1
    int64_t r = 0;
    if (!mako_checked_mul_i64(a, b, &r)) mako_overflow_trap("mul");
    return r;
#else
    return a * b;
#endif
}

/* Explicit builtins always check and return Result-like int: ok→value, or trap.
 * Prefer returning via out-param style for Mako: checked_add(a,b) aborts or returns sum
 * when trap mode; for soft mode returns sum only if ok else aborts with message.
 * Soft-fail variant: returns 0 and sets thread-local flag — use trap for safety. */

static inline int64_t mako_checked_add(int64_t a, int64_t b) {
    int64_t r = 0;
    if (!mako_checked_add_i64(a, b, &r)) mako_overflow_trap("checked_add");
    return r;
}

static inline int64_t mako_checked_sub(int64_t a, int64_t b) {
    int64_t r = 0;
    if (!mako_checked_sub_i64(a, b, &r)) mako_overflow_trap("checked_sub");
    return r;
}

static inline int64_t mako_checked_mul(int64_t a, int64_t b) {
    int64_t r = 0;
    if (!mako_checked_mul_i64(a, b, &r)) mako_overflow_trap("checked_mul");
    return r;
}

/* Returns 1 if a+b would overflow (no side effects). */
static inline int64_t mako_would_overflow_add(int64_t a, int64_t b) {
    int64_t r = 0;
    return mako_checked_add_i64(a, b, &r) ? 0 : 1;
}

static inline int64_t mako_would_overflow_sub(int64_t a, int64_t b) {
    int64_t r = 0;
    return mako_checked_sub_i64(a, b, &r) ? 0 : 1;
}

static inline int64_t mako_would_overflow_mul(int64_t a, int64_t b) {
    int64_t r = 0;
    return mako_checked_mul_i64(a, b, &r) ? 0 : 1;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_OVERFLOW_H */
