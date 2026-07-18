/* Mako strong logging — structured KV + JSON lines, levels, redaction, trace.
 * Production defaults: filterable levels, ISO-8601 timestamps, optional JSON,
 * service name, multi-field records, stderr or file. Fast path: one fprintf
 * family write; no GC. Include after mako_trace.h when possible. */
#ifndef MAKO_LOG_H
#define MAKO_LOG_H

#include "mako_rt.h" /* includes platform pthread / CRITICAL_SECTION shims */
#include <stdio.h>
#include <string.h>
#include <time.h>
/* Do not #include <pthread.h> here — Windows builds use mako_platform shims. */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- config (process-global, mutex for file/service swaps) ---- */
static int mako_slog_min_level = 1; /* default info in strong mode; set "debug" to loosen */
static int mako_slog_json_mode = 0;  /* 0 = logfmt-ish, 1 = JSON object per line */
static char mako_slog_service[96];
static FILE *mako_slog_fp = NULL; /* NULL → stderr */
/* Lazy-init mutex: PTHREAD_MUTEX_INITIALIZER is not portable to CRITICAL_SECTION. */
static pthread_mutex_t mako_slog_mu;
static int mako_slog_mu_ready = 0;

static inline void mako_slog_mu_ensure(void) {
    if (mako_slog_mu_ready) return;
#if defined(_WIN32) || defined(_WIN64)
    static LONG once = 0;
    if (InterlockedCompareExchange(&once, 1, 0) == 0) {
        pthread_mutex_init(&mako_slog_mu, NULL);
        mako_slog_mu_ready = 1;
        InterlockedExchange(&once, 2);
    } else {
        while (InterlockedCompareExchange(&once, 2, 2) != 2)
            Sleep(0);
    }
#else
    static pthread_mutex_t boot = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&boot);
    if (!mako_slog_mu_ready) {
        pthread_mutex_init(&mako_slog_mu, NULL);
        mako_slog_mu_ready = 1;
    }
    pthread_mutex_unlock(&boot);
#endif
}

static inline void mako_slog_lock(void) {
    mako_slog_mu_ensure();
    pthread_mutex_lock(&mako_slog_mu);
}

static inline void mako_slog_unlock(void) {
    pthread_mutex_unlock(&mako_slog_mu);
}

static inline FILE *mako_slog_out(void) {
    return mako_slog_fp ? mako_slog_fp : stderr;
}

static inline int64_t mako_slog_level_num(MakoString level) {
    if (!level.data || level.len == 0) return 1;
    if (level.len >= 5 && strncmp(level.data, "debug", 5) == 0) return 0;
    if (level.len >= 5 && strncmp(level.data, "trace", 5) == 0) return 0;
    if (level.len >= 4 && strncmp(level.data, "info", 4) == 0) return 1;
    if (level.len >= 4 && strncmp(level.data, "warn", 4) == 0) return 2;
    if (level.len >= 5 && strncmp(level.data, "error", 5) == 0) return 3;
    if (level.len >= 5 && strncmp(level.data, "fatal", 5) == 0) return 4;
    return 1;
}

static inline int mako_slog_enabled(MakoString level) {
    return (int)mako_slog_level_num(level) >= mako_slog_min_level;
}

static inline void mako_slog_set_level(MakoString level) {
    mako_slog_min_level = (int)mako_slog_level_num(level);
}

static inline int64_t mako_slog_get_level(void) {
    return (int64_t)mako_slog_min_level;
}

static inline void mako_slog_set_json(int64_t on) {
    mako_slog_json_mode = on ? 1 : 0;
}

static inline int64_t mako_slog_is_json(void) {
    return mako_slog_json_mode ? 1 : 0;
}

static inline void mako_slog_set_service(MakoString name) {
    size_t n = name.data ? name.len : 0;
    if (n >= sizeof(mako_slog_service)) n = sizeof(mako_slog_service) - 1;
    if (n && name.data) memcpy(mako_slog_service, name.data, n);
    mako_slog_service[n] = 0;
}

static inline MakoString mako_slog_service_name(void) {
    return mako_str_from_cstr(mako_slog_service[0] ? mako_slog_service : "");
}

/* Open append path for logs; empty path → stderr. Returns 1 ok, 0 fail. */
static inline int64_t mako_slog_set_output(MakoString path) {
    mako_slog_lock();
    if (mako_slog_fp && mako_slog_fp != stderr) {
        fclose(mako_slog_fp);
        mako_slog_fp = NULL;
    }
    if (!path.data || path.len == 0) {
        mako_slog_fp = NULL;
        mako_slog_unlock();
        return 1;
    }
    char pbuf[1024];
    if (path.len >= sizeof(pbuf)) {
        mako_slog_unlock();
        return 0;
    }
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    FILE *f = fopen(pbuf, "a");
    if (!f) {
        mako_slog_unlock();
        return 0;
    }
    mako_slog_fp = f;
    mako_slog_unlock();
    return 1;
}

static inline void mako_slog_flush(void) {
    mako_slog_lock();
    FILE *fp = mako_slog_out();
    if (fp) fflush(fp);
    mako_slog_unlock();
}

/* ISO-8601 UTC with milliseconds: 2026-07-13T12:34:56.789Z */
static inline void mako_slog_timestamp(char *buf, size_t cap) {
    if (!buf || cap < 28) return;
    int64_t wms = mako_wall_ms();
    time_t sec = (time_t)(wms / 1000);
    int ms = (int)(wms % 1000);
    if (ms < 0) ms = 0;
    struct tm tm;
#if defined(_WIN32)
    gmtime_s(&tm, &sec);
#else
    gmtime_r(&sec, &tm);
#endif
    snprintf(
        buf, cap, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, ms
    );
}

/* JSON string escape into dst; returns bytes written (no trailing NUL in count). */
static inline size_t mako_slog_json_escape(
    char *dst, size_t cap, const char *src, size_t len
) {
    size_t o = 0;
    if (!dst || cap == 0) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)(src ? src[i] : 0);
        const char *esc = NULL;
        char u[8];
        size_t el = 0;
        if (c == '"' || c == '\\') {
            u[0] = '\\';
            u[1] = (char)c;
            esc = u;
            el = 2;
        } else if (c == '\n') {
            esc = "\\n";
            el = 2;
        } else if (c == '\r') {
            esc = "\\r";
            el = 2;
        } else if (c == '\t') {
            esc = "\\t";
            el = 2;
        } else if (c < 0x20) {
            snprintf(u, sizeof(u), "\\u%04x", c);
            esc = u;
            el = 6;
        } else {
            if (o + 1 >= cap) break;
            dst[o++] = (char)c;
            continue;
        }
        if (o + el >= cap) break;
        memcpy(dst + o, esc, el);
        o += el;
    }
    if (o < cap) dst[o] = 0;
    return o;
}

static inline void mako_slog_write_trace(FILE *fp) {
#if defined(MAKO_TRACE_H)
    MakoString tid = mako_trace_current();
    if (tid.data && tid.len > 0) {
        if (mako_slog_json_mode) {
            fprintf(fp, ",\"trace\":\"");
            char esc[96];
            size_t n = mako_slog_json_escape(esc, sizeof(esc), tid.data, tid.len);
            fwrite(esc, 1, n, fp);
            fputc('"', fp);
        } else {
            fprintf(fp, " trace=%.*s", (int)tid.len, tid.data);
        }
    }
    mako_str_free(tid); /* may be empty singleton */
#else
    (void)fp;
#endif
}

typedef struct {
    const char *k;
    size_t klen;
    const char *v;
    size_t vlen;
    int is_int;
    int64_t iv;
} MakoSlogField;

static inline void mako_slog_emit(
    MakoString level,
    MakoString msg,
    const MakoSlogField *fields,
    int nfields
) {
    if (!mako_slog_enabled(level)) return;
    char ts[40];
    mako_slog_timestamp(ts, sizeof(ts));
    mako_slog_lock();
    FILE *fp = mako_slog_out();
    if (mako_slog_json_mode) {
        fprintf(fp, "{\"ts\":\"%s\",\"level\":\"%.*s\"",
                ts, (int)level.len, level.data ? level.data : "info");
        if (mako_slog_service[0]) {
            char se[128];
            size_t sn = mako_slog_json_escape(
                se, sizeof(se), mako_slog_service, strlen(mako_slog_service)
            );
            fprintf(fp, ",\"service\":\"");
            fwrite(se, 1, sn, fp);
            fputc('"', fp);
        }
        {
            char me[2048];
            size_t mn = mako_slog_json_escape(
                me, sizeof(me), msg.data ? msg.data : "", msg.len
            );
            fprintf(fp, ",\"msg\":\"");
            fwrite(me, 1, mn, fp);
            fputc('"', fp);
        }
        for (int i = 0; i < nfields; i++) {
            if (!fields[i].k || fields[i].klen == 0) continue;
            char ke[128];
            size_t kn = mako_slog_json_escape(
                ke, sizeof(ke), fields[i].k, fields[i].klen
            );
            fprintf(fp, ",\"");
            fwrite(ke, 1, kn, fp);
            fputc('"', fp);
            fputc(':', fp);
            if (fields[i].is_int) {
                fprintf(fp, "%lld", (long long)fields[i].iv);
            } else {
                char ve[2048];
                size_t vn = mako_slog_json_escape(
                    ve, sizeof(ve), fields[i].v ? fields[i].v : "", fields[i].vlen
                );
                fputc('"', fp);
                fwrite(ve, 1, vn, fp);
                fputc('"', fp);
            }
        }
        mako_slog_write_trace(fp);
        fputc('}', fp);
        fputc('\n', fp);
    } else {
        /* logfmt-style: ts=... level=... service=... msg=... k=v ... */
        fprintf(fp, "ts=%s level=%.*s",
                ts, (int)level.len, level.data ? level.data : "info");
        if (mako_slog_service[0]) {
            fprintf(fp, " service=%s", mako_slog_service);
        }
        mako_slog_write_trace(fp);
        fprintf(fp, " msg=");
        /* quote msg if spaces */
        int need_q = 0;
        for (size_t i = 0; i < msg.len; i++) {
            char c = msg.data[i];
            if (c == ' ' || c == '"' || c == '=' || c == '\n') {
                need_q = 1;
                break;
            }
        }
        if (need_q) fputc('"', fp);
        if (msg.data && msg.len) {
            for (size_t i = 0; i < msg.len; i++) {
                char c = msg.data[i];
                if (c == '"' || c == '\\') fputc('\\', fp);
                if (c == '\n') {
                    fputc('\\', fp);
                    fputc('n', fp);
                } else {
                    fputc(c, fp);
                }
            }
        }
        if (need_q) fputc('"', fp);
        for (int i = 0; i < nfields; i++) {
            if (!fields[i].k || fields[i].klen == 0) continue;
            fputc(' ', fp);
            fwrite(fields[i].k, 1, fields[i].klen, fp);
            fputc('=', fp);
            if (fields[i].is_int) {
                fprintf(fp, "%lld", (long long)fields[i].iv);
            } else {
                int q = 0;
                for (size_t j = 0; j < fields[i].vlen; j++) {
                    char c = fields[i].v ? fields[i].v[j] : 0;
                    if (c == ' ' || c == '"' || c == '=' || c == '\n') {
                        q = 1;
                        break;
                    }
                }
                if (q) fputc('"', fp);
                if (fields[i].v && fields[i].vlen) {
                    for (size_t j = 0; j < fields[i].vlen; j++) {
                        char c = fields[i].v[j];
                        if (c == '"' || c == '\\') fputc('\\', fp);
                        if (c == '\n') {
                            fputc('\\', fp);
                            fputc('n', fp);
                        } else {
                            fputc(c, fp);
                        }
                    }
                }
                if (q) fputc('"', fp);
            }
        }
        fputc('\n', fp);
    }
    mako_slog_unlock();
}

static inline void mako_slog_log(MakoString level, MakoString msg) {
    mako_slog_emit(level, msg, NULL, 0);
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

static inline void mako_slog_with(
    MakoString level, MakoString msg, MakoString k1, MakoString v1
) {
    MakoSlogField f[1];
    f[0].k = k1.data;
    f[0].klen = k1.len;
    f[0].v = v1.data;
    f[0].vlen = v1.len;
    f[0].is_int = 0;
    f[0].iv = 0;
    mako_slog_emit(level, msg, f, 1);
}

static inline void mako_slog_with2(
    MakoString level, MakoString msg,
    MakoString k1, MakoString v1, MakoString k2, MakoString v2
) {
    MakoSlogField f[2];
    f[0].k = k1.data;
    f[0].klen = k1.len;
    f[0].v = v1.data;
    f[0].vlen = v1.len;
    f[0].is_int = 0;
    f[0].iv = 0;
    f[1].k = k2.data;
    f[1].klen = k2.len;
    f[1].v = v2.data;
    f[1].vlen = v2.len;
    f[1].is_int = 0;
    f[1].iv = 0;
    mako_slog_emit(level, msg, f, 2);
}

static inline void mako_slog_with3(
    MakoString level, MakoString msg,
    MakoString k1, MakoString v1, MakoString k2, MakoString v2,
    MakoString k3, MakoString v3
) {
    MakoSlogField f[3];
    f[0].k = k1.data;
    f[0].klen = k1.len;
    f[0].v = v1.data;
    f[0].vlen = v1.len;
    f[0].is_int = 0;
    f[0].iv = 0;
    f[1].k = k2.data;
    f[1].klen = k2.len;
    f[1].v = v2.data;
    f[1].vlen = v2.len;
    f[1].is_int = 0;
    f[1].iv = 0;
    f[2].k = k3.data;
    f[2].klen = k3.len;
    f[2].v = v3.data;
    f[2].vlen = v3.len;
    f[2].is_int = 0;
    f[2].iv = 0;
    mako_slog_emit(level, msg, f, 3);
}

static inline void mako_slog_with_int(
    MakoString level, MakoString msg, MakoString key, int64_t val
) {
    MakoSlogField f[1];
    f[0].k = key.data;
    f[0].klen = key.len;
    f[0].v = NULL;
    f[0].vlen = 0;
    f[0].is_int = 1;
    f[0].iv = val;
    mako_slog_emit(level, msg, f, 1);
}

/* Redact secrets for logs — never echoes the value. */
static inline MakoString mako_slog_redact(MakoString value) {
    (void)value;
    return mako_str_from_cstr("[REDACTED]");
}

static inline void mako_slog_with_redacted(
    MakoString level, MakoString msg, MakoString key
) {
    mako_slog_with(level, msg, key, mako_str_from_cstr("[REDACTED]"));
}

/* Strong log_* — same backend as slog (level filter + format). */
static inline void mako_log_info_strong(MakoString msg) {
    mako_slog_info(msg);
}
static inline void mako_log_warn_strong(MakoString msg) {
    mako_slog_warn(msg);
}
static inline void mako_log_error_strong(MakoString msg) {
    mako_slog_error(msg);
}
static inline void mako_log_debug_strong(MakoString msg) {
    mako_slog_debug(msg);
}

static inline void mako_log_kv(MakoString level, MakoString key, MakoString val) {
    mako_slog_with(level, mako_str_from_cstr(""), key, val);
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_LOG_H */
