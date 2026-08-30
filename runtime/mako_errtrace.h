/* Error tracing: lightweight source-location context for Result error chains.
 *
 * Errors are plain MakoStrings with an optional trace suffix separated by
 * '\x01' (SOH). The suffix contains source locations as "file:line" entries
 * separated by '\x02' (STX). This encoding is invisible to normal string
 * operations (print, str_eq on the message part) while allowing error_chain()
 * to reconstruct the full trace.
 *
 * Wire format:
 *   "message\x01file.mko:42\x02caller.mko:10\x02main.mko:5"
 *
 * Memory safety: all functions return owned MakoStrings. No internal heap
 * structures beyond the string itself.
 */
#ifndef MAKO_ERRTRACE_H
#define MAKO_ERRTRACE_H

#include "mako_rt.h"
#include <stdio.h>

#define MAKO_ERR_SEP '\x01'  /* separates message from trace */
#define MAKO_ERR_LOC '\x02'  /* separates trace entries */

/* Create an error with source location. */
static inline MakoString mako_error_new(MakoString msg, const char *file, int line) {
    char loc[512];
    int n = snprintf(loc, sizeof(loc), "%c%s:%d", MAKO_ERR_SEP, file, line);
    if (n <= 0 || (size_t)n >= sizeof(loc)) return msg;
    size_t total = msg.len + (size_t)n;
    char *buf = (char *)malloc(total);
    if (!buf) return msg;
    if (msg.len > 0 && msg.data) memcpy(buf, msg.data, msg.len);
    memcpy(buf + msg.len, loc, (size_t)n);
    MakoString out = {buf, total};
    return out;
}

/* Wrap an existing error with context and a new source location.
 * Result: "context: original_message" with both locations in the trace. */
static inline MakoString mako_error_wrap(MakoString err, MakoString context, const char *file, int line) {
    /* Find the trace separator in the original error. */
    const char *sep = NULL;
    for (size_t i = 0; i < err.len; i++) {
        if (err.data[i] == MAKO_ERR_SEP) { sep = err.data + i; break; }
    }
    size_t msg_len = sep ? (size_t)(sep - err.data) : err.len;
    size_t trace_len = sep ? err.len - msg_len : 0;

    /* New location entry. */
    char loc[512];
    int loc_n = snprintf(loc, sizeof(loc), "%c%s:%d", MAKO_ERR_LOC, file, line);
    if (loc_n <= 0 || (size_t)loc_n >= sizeof(loc)) loc_n = 0;

    /* Build: "context: message\x01old_trace\x02new_loc" */
    int has_context = context.len > 0 && context.data != NULL;
    size_t colon_len = has_context ? 2 : 0; /* ": " */
    size_t ctx_len = has_context ? context.len : 0;
    size_t total = ctx_len + colon_len + msg_len + trace_len + (size_t)loc_n;

    /* If no trace existed in original, add separator before first loc. */
    int need_sep = (trace_len == 0 && loc_n > 0) ? 1 : 0;
    if (need_sep) {
        /* Replace the LOC marker in loc with SEP marker. */
        loc[0] = MAKO_ERR_SEP;
    }

    char *buf = (char *)malloc(total);
    if (!buf) return err;

    size_t off = 0;
    if (has_context) {
        memcpy(buf, context.data, context.len);
        off += context.len;
        buf[off++] = ':';
        buf[off++] = ' ';
    }
    if (msg_len > 0) { memcpy(buf + off, err.data, msg_len); off += msg_len; }
    if (trace_len > 0) { memcpy(buf + off, sep, trace_len); off += trace_len; }
    if (loc_n > 0) { memcpy(buf + off, loc, (size_t)loc_n); off += (size_t)loc_n; }

    MakoString out = {buf, off};
    return out;
}

/* Extract just the message part (without trace). */
static inline MakoString mako_error_message(MakoString err) {
    for (size_t i = 0; i < err.len; i++) {
        if (err.data[i] == MAKO_ERR_SEP) {
            if (i == 0) return mako_str_from_cstr("");
            char *buf = (char *)malloc(i);
            if (!buf) return mako_str_from_cstr("");
            memcpy(buf, err.data, i);
            return (MakoString){buf, i};
        }
    }
    /* No trace — return a clone of the whole string. */
    return mako_str_clone(err);
}

/* Extract the root cause (innermost error message, no trace). */
static inline MakoString mako_error_cause(MakoString err) {
    MakoString msg = mako_error_message(err);
    /* Find last ": " to get the innermost message. */
    for (size_t i = msg.len; i >= 2; i--) {
        if (msg.data[i - 2] == ':' && msg.data[i - 1] == ' ') {
            size_t start = i;
            size_t len = msg.len - start;
            char *buf = (char *)malloc(len);
            if (!buf) return msg;
            memcpy(buf, msg.data + start, len);
            mako_str_free(msg);
            return (MakoString){buf, len};
        }
    }
    return msg;
}

/* Format the full error chain for display.
 * Output: "context: message [file.mko:42 → caller.mko:10]" */
static inline MakoString mako_error_chain(MakoString err) {
    /* Find trace separator. */
    const char *sep = NULL;
    for (size_t i = 0; i < err.len; i++) {
        if (err.data[i] == MAKO_ERR_SEP) { sep = err.data + i; break; }
    }
    if (!sep) return mako_str_clone(err); /* no trace, return as-is */

    size_t msg_len = (size_t)(sep - err.data);

    /* Build formatted output. */
    MakoStrBuilder *b = mako_str_builder_new();
    if (msg_len > 0) {
        MakoString msg_part = {err.data, msg_len};
        mako_str_builder_write(b, msg_part);
    }
    mako_str_builder_write(b, mako_str_from_cstr(" ["));

    /* Parse trace entries: \x01loc1\x02loc2\x02loc3 */
    const char *p = sep + 1;
    const char *end = err.data + err.len;
    int first = 1;
    while (p < end) {
        const char *next = p;
        while (next < end && *next != MAKO_ERR_LOC) next++;
        if (next > p) {
            if (!first) mako_str_builder_write(b, mako_str_from_cstr(" \xe2\x86\x92 ")); /* → */
            MakoString loc = {(char *)p, (size_t)(next - p)};
            mako_str_builder_write(b, loc);
            first = 0;
        }
        p = next + 1;
    }
    mako_str_builder_write(b, mako_str_from_cstr("]"));
    MakoString result = mako_str_builder_string(b);
    mako_str_builder_free(b);
    return result;
}

#endif /* MAKO_ERRTRACE_H */
