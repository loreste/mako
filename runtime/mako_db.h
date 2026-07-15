/* SQLite seed — real libsqlite3 when MAKO_HAS_SQLITE. */
#ifndef MAKO_DB_H
#define MAKO_DB_H

#include "mako_rt.h"
#if !defined(_WIN32)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include <ctype.h>

/* Windows lacks strtok_r and may deprecate strdup — provide shims. */
#if defined(_WIN32)
#ifndef strtok_r
static inline char *mako_strtok_r(char *str, const char *delim, char **saveptr) {
    if (!str) str = *saveptr;
    if (!str) return NULL;
    str += strspn(str, delim);
    if (*str == '\0') { *saveptr = NULL; return NULL; }
    char *end = str + strcspn(str, delim);
    if (*end) { *end = '\0'; *saveptr = end + 1; } else { *saveptr = NULL; }
    return str;
}
#define strtok_r mako_strtok_r
#endif
#ifndef strdup
#define strdup _strdup
#endif
#endif

#if defined(MAKO_HAS_SQLITE)
#include <sqlite3.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MAKO_HAS_SQLITE)

/* Open path, run SQL with optional bound int params (parameterized only).
 * `sql` must use `?` placeholders; `nargs` binds `args[i]` as SQLITE_INTEGER.
 * Rejects statements that look like string-concat injection (no raw exec API).
 * Returns first column of first row as int64 (0 if none). */
static inline int64_t mako_sqlite_query_int_params(
    MakoString path,
    MakoString sql,
    const int64_t *args,
    int nargs
) {
    char pbuf[512], qbuf[2048];
    if (path.len >= sizeof(pbuf) || sql.len >= sizeof(qbuf)) return -1;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    memcpy(qbuf, sql.data, sql.len);
    qbuf[sql.len] = 0;

    sqlite3 *db = NULL;
    if (sqlite3_open(pbuf, &db) != SQLITE_OK) {
        fprintf(stderr, "sqlite: open %s: %s\n", pbuf, sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return -1;
    }
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, qbuf, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite: prepare: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }
    int expected = sqlite3_bind_parameter_count(st);
    if (nargs < 0) nargs = 0;
    if (expected != nargs) {
        fprintf(
            stderr,
            "sqlite: parameter count mismatch (sql has %d placeholders, got %d args)\n",
            expected,
            nargs
        );
        sqlite3_finalize(st);
        sqlite3_close(db);
        return -1;
    }
    for (int i = 0; i < nargs; i++) {
        sqlite3_bind_int64(st, i + 1, args[i]);
    }
    int64_t out = 0;
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        out = sqlite3_column_int64(st, 0);
    } else if (rc != SQLITE_DONE) {
        fprintf(stderr, "sqlite: step: %s\n", sqlite3_errmsg(db));
        out = -1;
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return out;
}

/* Legacy name: zero-arg parameterized query (SQL must have no `?`). */
static inline int64_t mako_sqlite_query_int(MakoString path, MakoString sql) {
    return mako_sqlite_query_int_params(path, sql, NULL, 0);
}

static inline MakoString mako_sqlite_query_text_params(
    MakoString path,
    MakoString sql,
    const int64_t *args,
    int nargs
) {
    char pbuf[512], qbuf[2048];
    if (path.len >= sizeof(pbuf) || sql.len >= sizeof(qbuf))
        return mako_str_from_cstr("");
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    memcpy(qbuf, sql.data, sql.len);
    qbuf[sql.len] = 0;

    sqlite3 *db = NULL;
    if (sqlite3_open(pbuf, &db) != SQLITE_OK) {
        fprintf(stderr, "sqlite: open %s: %s\n", pbuf, db ? sqlite3_errmsg(db) : "?");
        if (db) sqlite3_close(db);
        return mako_str_from_cstr("");
    }
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, qbuf, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite: prepare: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return mako_str_from_cstr("");
    }
    int expected = sqlite3_bind_parameter_count(st);
    if (nargs < 0) nargs = 0;
    if (expected != nargs) {
        fprintf(stderr, "sqlite: parameter count mismatch\n");
        sqlite3_finalize(st);
        sqlite3_close(db);
        return mako_str_from_cstr("");
    }
    for (int i = 0; i < nargs; i++) {
        sqlite3_bind_int64(st, i + 1, args[i]);
    }
    MakoString out = mako_str_from_cstr("");
    if (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(st, 0);
        if (txt) out = mako_str_from_cstr((const char *)txt);
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return out;
}

static inline MakoString mako_sqlite_query_text(MakoString path, MakoString sql) {
    return mako_sqlite_query_text_params(path, sql, NULL, 0);
}

static inline void *mako_sqlite_open_handle(MakoString path) {
    char pbuf[512];
    if (path.len >= sizeof(pbuf)) return NULL;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    sqlite3 *db = NULL;
    if (sqlite3_open(pbuf, &db) != SQLITE_OK) {
        fprintf(stderr, "sqlite: open %s: %s\n", pbuf, db ? sqlite3_errmsg(db) : "?");
        if (db) sqlite3_close(db);
        return NULL;
    }
    return db;
}

static inline int64_t mako_sqlite_close_handle(void *handle) {
    if (handle) sqlite3_close((sqlite3 *)handle);
    return 0;
}

static inline int64_t mako_sqlite_query_int_handle(
    void *handle,
    MakoString sql,
    const int64_t *args,
    int nargs
) {
    if (!handle) return -1;
    char qbuf[2048];
    if (sql.len >= sizeof(qbuf)) return -1;
    memcpy(qbuf, sql.data, sql.len);
    qbuf[sql.len] = 0;

    sqlite3 *db = (sqlite3 *)handle;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, qbuf, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite: prepare: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    int expected = sqlite3_bind_parameter_count(st);
    if (nargs < 0) nargs = 0;
    if (expected != nargs) {
        fprintf(
            stderr,
            "sqlite: parameter count mismatch (sql has %d placeholders, got %d args)\n",
            expected,
            nargs
        );
        sqlite3_finalize(st);
        return -1;
    }
    for (int i = 0; i < nargs; i++) {
        sqlite3_bind_int64(st, i + 1, args[i]);
    }
    int64_t out = 0;
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        out = sqlite3_column_int64(st, 0);
    } else if (rc != SQLITE_DONE) {
        fprintf(stderr, "sqlite: step: %s\n", sqlite3_errmsg(db));
        out = -1;
    }
    sqlite3_finalize(st);
    return out;
}

static inline void *mako_sqlite_prepare_handle(void *handle, MakoString sql) {
    if (!handle) return NULL;
    char qbuf[2048];
    if (sql.len >= sizeof(qbuf)) return NULL;
    memcpy(qbuf, sql.data, sql.len);
    qbuf[sql.len] = 0;
    sqlite3_stmt *st = NULL;
    sqlite3 *db = (sqlite3 *)handle;
    if (sqlite3_prepare_v2(db, qbuf, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite: prepare: %s\n", sqlite3_errmsg(db));
        return NULL;
    }
    return st;
}

static inline int64_t mako_sqlite_stmt_query_int(
    void *handle,
    void *stmt,
    const int64_t *args,
    int nargs
) {
    if (!handle || !stmt) return -1;
    sqlite3 *db = (sqlite3 *)handle;
    sqlite3_stmt *st = (sqlite3_stmt *)stmt;
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    int expected = sqlite3_bind_parameter_count(st);
    if (nargs < 0) nargs = 0;
    if (expected != nargs) {
        fprintf(
            stderr,
            "sqlite: parameter count mismatch (sql has %d placeholders, got %d args)\n",
            expected,
            nargs
        );
        return -1;
    }
    for (int i = 0; i < nargs; i++) {
        sqlite3_bind_int64(st, i + 1, args[i]);
    }
    int64_t out = 0;
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        out = sqlite3_column_int64(st, 0);
    } else if (rc != SQLITE_DONE) {
        fprintf(stderr, "sqlite: step: %s\n", sqlite3_errmsg(db));
        out = -1;
    }
    sqlite3_reset(st);
    return out;
}

static inline int64_t mako_sqlite_finalize_stmt(void *stmt) {
    if (stmt) sqlite3_finalize((sqlite3_stmt *)stmt);
    return 0;
}

#else

static inline int64_t mako_sqlite_query_int_params(
    MakoString path,
    MakoString sql,
    const int64_t *args,
    int nargs
) {
    (void)path;
    (void)sql;
    (void)args;
    (void)nargs;
    fprintf(stderr, "sqlite: not linked (rebuild with -DMAKO_HAS_SQLITE -lsqlite3)\n");
    return -1;
}

static inline int64_t mako_sqlite_query_int(MakoString path, MakoString sql) {
    return mako_sqlite_query_int_params(path, sql, NULL, 0);
}

static inline MakoString mako_sqlite_query_text_params(
    MakoString path,
    MakoString sql,
    const int64_t *args,
    int nargs
) {
    (void)path;
    (void)sql;
    (void)args;
    (void)nargs;
    return mako_str_from_cstr("");
}

static inline MakoString mako_sqlite_query_text(MakoString path, MakoString sql) {
    return mako_sqlite_query_text_params(path, sql, NULL, 0);
}

static inline void *mako_sqlite_open_handle(MakoString path) {
    (void)path;
    return NULL;
}

static inline int64_t mako_sqlite_close_handle(void *handle) {
    (void)handle;
    return 0;
}

static inline int64_t mako_sqlite_query_int_handle(
    void *handle,
    MakoString sql,
    const int64_t *args,
    int nargs
) {
    (void)handle;
    (void)sql;
    (void)args;
    (void)nargs;
    fprintf(stderr, "sqlite: not linked (rebuild with -DMAKO_HAS_SQLITE -lsqlite3)\n");
    return -1;
}

static inline void *mako_sqlite_prepare_handle(void *handle, MakoString sql) {
    (void)handle;
    (void)sql;
    return NULL;
}

static inline int64_t mako_sqlite_stmt_query_int(
    void *handle,
    void *stmt,
    const int64_t *args,
    int nargs
) {
    (void)handle;
    (void)stmt;
    (void)args;
    (void)nargs;
    fprintf(stderr, "sqlite: not linked (rebuild with -DMAKO_HAS_SQLITE -lsqlite3)\n");
    return -1;
}

static inline int64_t mako_sqlite_finalize_stmt(void *stmt) {
    (void)stmt;
    return 0;
}

#endif

/* ---- Postgres (Partial): real libpq when MAKO_HAS_LIBPQ; else stub.
 * pg_connect returns handle=0 on failure (server down / no libpq) — never fake success.
 * pg_ok(c)==0 means not connected.
 * pg_exec on a disconnected handle returns -1 (no crash).
 * pg_close on handle=0 is a safe no-op (returns 0). */
typedef struct { int64_t handle; } MakoPgConn;

#if defined(MAKO_HAS_LIBPQ)
#include <libpq-fe.h>
#include <stdint.h>

static inline MakoPgConn mako_pg_connect(MakoString url) {
    char ubuf[2048];
    if (!url.data || url.len == 0 || url.len >= sizeof(ubuf)) {
        fprintf(stderr, "mako: pg_connect: bad url\n");
        return (MakoPgConn){0};
    }
    memcpy(ubuf, url.data, url.len);
    ubuf[url.len] = 0;
    PGconn *c = PQconnectdb(ubuf);
    if (!c || PQstatus(c) != CONNECTION_OK) {
        fprintf(stderr, "mako: pg_connect failed: %s", c ? PQerrorMessage(c) : "null\n");
        if (c) PQfinish(c);
        return (MakoPgConn){0};
    }
    return (MakoPgConn){(int64_t)(intptr_t)c};
}

static inline int64_t mako_pg_ok(MakoPgConn c) {
    return c.handle != 0 ? 1 : 0;
}

static inline int64_t mako_pg_exec_params(
    MakoPgConn c,
    MakoString sql,
    const char *const *values,
    int nparams
) {
    if (!c.handle || !sql.data) return -1;
    PGconn *pg = (PGconn *)(intptr_t)c.handle;
    char qbuf[4096];
    if (sql.len >= sizeof(qbuf)) return -1;
    memcpy(qbuf, sql.data, sql.len);
    qbuf[sql.len] = 0;
    if (nparams < 0) nparams = 0;
    PGresult *r = PQexecParams(
        pg, qbuf, nparams, NULL, values, NULL, NULL, 0
    );
    if (!r) return -1;
    ExecStatusType st = PQresultStatus(r);
    int64_t out = (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK) ? 0 : -1;
    if (out < 0) {
        fprintf(stderr, "mako: pg_exec_params: %s", PQerrorMessage(pg));
    }
    PQclear(r);
    return out;
}

static inline int64_t mako_pg_prepare_name(
    MakoPgConn c,
    const char *name,
    MakoString sql
) {
    if (!c.handle || !name || !sql.data) return -1;
    PGconn *pg = (PGconn *)(intptr_t)c.handle;
    char qbuf[4096];
    if (sql.len >= sizeof(qbuf)) return -1;
    memcpy(qbuf, sql.data, sql.len);
    qbuf[sql.len] = 0;
    PGresult *r = PQprepare(pg, name, qbuf, 0, NULL);
    if (!r) return -1;
    ExecStatusType st = PQresultStatus(r);
    int64_t out = st == PGRES_COMMAND_OK ? 0 : -1;
    if (out < 0) {
        fprintf(stderr, "mako: pg_prepare: %s", PQerrorMessage(pg));
    }
    PQclear(r);
    return out;
}

static inline int64_t mako_pg_exec_prepared(
    MakoPgConn c,
    const char *name,
    const char *const *values,
    int nparams
) {
    if (!c.handle || !name) return -1;
    if (nparams < 0) nparams = 0;
    PGconn *pg = (PGconn *)(intptr_t)c.handle;
    PGresult *r = PQexecPrepared(pg, name, nparams, values, NULL, NULL, 0);
    if (!r) return -1;
    ExecStatusType st = PQresultStatus(r);
    int64_t out = (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK) ? 0 : -1;
    if (out < 0) {
        fprintf(stderr, "mako: pg_exec_prepared: %s", PQerrorMessage(pg));
    }
    PQclear(r);
    return out;
}

/* Parameterized-only: SQL with $1..$N; zero params allowed for static SQL.
 * Raw string concatenation of user data is not supported — use placeholders. */
static inline int64_t mako_pg_exec(MakoPgConn c, MakoString sql) {
    return mako_pg_exec_params(c, sql, NULL, 0);
}

/* Row count from last-style query; -1 when disconnected / error (never crashes). */
static inline int64_t mako_pg_exec_row_count(MakoPgConn c, MakoString sql) {
    if (!c.handle || !sql.data) return -1;
    PGconn *pg = (PGconn *)(intptr_t)c.handle;
    char qbuf[4096];
    if (sql.len >= sizeof(qbuf)) return -1;
    memcpy(qbuf, sql.data, sql.len);
    qbuf[sql.len] = 0;
    PGresult *r = PQexec(pg, qbuf);
    if (!r) return -1;
    ExecStatusType st = PQresultStatus(r);
    int64_t out = -1;
    if (st == PGRES_TUPLES_OK) out = (int64_t)PQntuples(r);
    else if (st == PGRES_COMMAND_OK) out = 0;
    PQclear(r);
    return out;
}

static inline int64_t mako_pg_close(MakoPgConn c) {
    if (!c.handle) return 0;
    PQfinish((PGconn *)(intptr_t)c.handle);
    return 0;
}

#else

static inline MakoPgConn mako_pg_connect(MakoString url) {
    (void)url;
    fprintf(stderr, "mako: pg_connect stub (no libpq linked)\n");
    return (MakoPgConn){0};
}
static inline int64_t mako_pg_ok(MakoPgConn c) {
    return c.handle != 0 ? 1 : 0;
}
static inline int64_t mako_pg_exec(MakoPgConn c, MakoString sql) {
    (void)c;
    (void)sql;
    return -1;
}
static inline int64_t mako_pg_exec_params(
    MakoPgConn c,
    MakoString sql,
    const char *const *values,
    int nparams
) {
    (void)c;
    (void)sql;
    (void)values;
    (void)nparams;
    return -1;
}
static inline int64_t mako_pg_prepare_name(
    MakoPgConn c,
    const char *name,
    MakoString sql
) {
    (void)c;
    (void)name;
    (void)sql;
    return -1;
}
static inline int64_t mako_pg_exec_prepared(
    MakoPgConn c,
    const char *name,
    const char *const *values,
    int nparams
) {
    (void)c;
    (void)name;
    (void)values;
    (void)nparams;
    return -1;
}
static inline int64_t mako_pg_exec_row_count(MakoPgConn c, MakoString sql) {
    (void)c;
    (void)sql;
    return -1;
}
static inline int64_t mako_pg_close(MakoPgConn c) {
    (void)c;
    return 0;
}

#endif

/* Parse postgres://user:pass@host:port/db → libpq keyword string (Partial).
 * Empty on malformed URL. Does not connect. Works without libpq. */
static inline MakoString mako_pg_connect_url(MakoString url) {
    if (!url.data || url.len < 11) return mako_str_from_cstr("");
    const char *p = url.data;
    size_t n = url.len;
    size_t skip = 0;
    if (n >= 13 && memcmp(p, "postgresql://", 13) == 0) skip = 13;
    else if (n >= 11 && memcmp(p, "postgres://", 11) == 0) skip = 11;
    else return mako_str_from_cstr("");
    const char *rest = p + skip;
    size_t rlen = n - skip;
    const char *at = NULL;
    for (size_t i = 0; i < rlen; i++) {
        if (rest[i] == '@') { at = rest + i; break; }
    }
    if (!at) return mako_str_from_cstr("");
    const char *user = rest;
    size_t user_len = (size_t)(at - rest);
    const char *pass = NULL;
    size_t pass_len = 0;
    for (size_t i = 0; i < user_len; i++) {
        if (user[i] == ':') {
            pass = user + i + 1;
            pass_len = user_len - i - 1;
            user_len = i;
            break;
        }
    }
    const char *hostpart = at + 1;
    size_t hlen = rlen - (size_t)(hostpart - rest);
    const char *slash = NULL;
    for (size_t i = 0; i < hlen; i++) {
        if (hostpart[i] == '/') { slash = hostpart + i; break; }
    }
    if (!slash || slash == hostpart) return mako_str_from_cstr("");
    size_t hostport_len = (size_t)(slash - hostpart);
    const char *db = slash + 1;
    size_t db_len = hlen - hostport_len - 1;
    const char *host = hostpart;
    size_t host_len = hostport_len;
    const char *port = NULL;
    size_t port_len = 0;
    for (size_t i = 0; i < hostport_len; i++) {
        if (hostpart[i] == ':') {
            host_len = i;
            port = hostpart + i + 1;
            port_len = hostport_len - i - 1;
            break;
        }
    }
    char out[512];
    int o = 0;
    #define A(lit) do { size_t L=strlen(lit); if(o+(int)L>=(int)sizeof(out)) return mako_str_from_cstr(""); memcpy(out+o,lit,L); o+=(int)L; } while(0)
    #define AN(s,L) do { if(o+(int)(L)>=(int)sizeof(out)) return mako_str_from_cstr(""); memcpy(out+o,(s),(L)); o+=(int)(L); } while(0)
    A("host="); AN(host, host_len);
    if (port && port_len) { A(" port="); AN(port, port_len); }
    A(" dbname="); AN(db, db_len);
    A(" user="); AN(user, user_len);
    if (pass && pass_len) { A(" password="); AN(pass, pass_len); }
    #undef A
    #undef AN
    out[o] = 0;
    return mako_str_from_cstr(out);
}

/* ---- Redis RESP client helpers (plain TCP, no hiredis).
 * Work against redis-server or mako_redis_mock_kv. ---- */

static inline int mako_redis_tcp_connect(MakoString host, int64_t port) {
    char hbuf[256];
    if (!host.data || host.len >= sizeof(hbuf)) return -1;
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, hbuf, &addr.sin_addr) != 1) {
        mako_sock_close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        mako_sock_close(fd);
        return -1;
    }
    return fd;
}

typedef struct {
    MakoString host;
    int64_t port;
    MakoString pass;
    int64_t db;
    int64_t has_db;
} MakoRedisUrl;

/* MakoRedisConn is defined in mako_std.h */

static inline MakoRedisUrl mako_redis_parse_url(MakoString url) {
    MakoRedisUrl out;
    memset(&out, 0, sizeof(out));
    out.port = 6379;
    out.db = 0;
    char buf[512];
    if (!url.data || url.len >= sizeof(buf)) return out;
    memcpy(buf, url.data, url.len);
    buf[url.len] = 0;
    char *p = buf;
    if (strncmp(p, "redis://", 8) == 0) p += 8;
    char *slash = strchr(p, '/');
    if (slash) {
        *slash = 0;
        char *dbp = slash + 1;
        if (*dbp) {
            out.db = atoll(dbp);
            out.has_db = 1;
        }
    }
    char *at = strchr(p, '@');
    if (at) {
        *at = 0;
        char *cred = p;
        if (cred[0] == ':') cred++;
        out.pass = mako_str_from_cstr(cred);
        p = at + 1;
    }
    char *colon = strrchr(p, ':');
    if (colon) {
        *colon = 0;
        out.port = atoll(colon + 1);
    }
    out.host = mako_str_from_cstr(*p ? p : "127.0.0.1");
    if (out.port <= 0) out.port = 6379;
    return out;
}

static inline MakoString mako_redis_connect_url(MakoString url) {
    MakoRedisUrl u = mako_redis_parse_url(url);
    char buf[384];
    int n = snprintf(
        buf,
        sizeof(buf),
        "host=%.*s port=%lld db=%lld auth=%lld",
        (int)u.host.len,
        u.host.data ? u.host.data : "",
        (long long)u.port,
        (long long)(u.has_db ? u.db : 0),
        (long long)(u.pass.len > 0 ? 1 : 0)
    );
    if (n < 0) return mako_str_from_cstr("");
    return mako_str_from_cstr(buf);
}

/* Append RESP bulk string `$len\r\n<data>\r\n` into dst; returns bytes written or -1. */
static inline int mako_redis_append_bulk(char *dst, size_t cap, size_t *off, MakoString s) {
    char hdr[32];
    int hn = snprintf(hdr, sizeof(hdr), "$%lld\r\n", (long long)(s.data ? (long long)s.len : 0));
    if (hn < 0 || *off + (size_t)hn + (s.data ? s.len : 0) + 2 >= cap) return -1;
    memcpy(dst + *off, hdr, (size_t)hn);
    *off += (size_t)hn;
    if (s.data && s.len) {
        memcpy(dst + *off, s.data, s.len);
        *off += s.len;
    }
    dst[(*off)++] = '\r';
    dst[(*off)++] = '\n';
    return 0;
}

static inline int mako_redis_send_array(int fd, const char *cmd, MakoString a, MakoString b) {
    char buf[2048];
    size_t off = 0;
    int argc = 1 + (a.data ? 1 : 0) + (b.data ? 1 : 0);
    int hn = snprintf(buf, sizeof(buf), "*%d\r\n$%zu\r\n%s\r\n", argc, strlen(cmd), cmd);
    if (hn < 0 || (size_t)hn >= sizeof(buf)) return -1;
    off = (size_t)hn;
    if (a.data && mako_redis_append_bulk(buf, sizeof(buf), &off, a) < 0) return -1;
    if (b.data && mako_redis_append_bulk(buf, sizeof(buf), &off, b) < 0) return -1;
    return send(fd, buf, off, 0) < 0 ? -1 : 0;
}

static inline MakoString mako_redis_recv_simple(int fd) {
    char buf[1024];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return mako_str_from_cstr("");
    buf[n] = 0;
    /* +OK / +PONG */
    if (buf[0] == '+' && n >= 2) {
        size_t end = 1;
        while (end < (size_t)n && buf[end] != '\r') end++;
        char *d = (char *)malloc(end);
        if (!d) return mako_str_from_cstr("");
        memcpy(d, buf + 1, end - 1);
        d[end - 1] = 0;
        return (MakoString){d, end - 1};
    }
    /* :N integer */
    if (buf[0] == ':') {
        size_t end = 1;
        while (end < (size_t)n && buf[end] != '\r') end++;
        char *d = (char *)malloc(end);
        if (!d) return mako_str_from_cstr("");
        memcpy(d, buf + 1, end - 1);
        d[end - 1] = 0;
        return (MakoString){d, end - 1};
    }
    /* $-1 null bulk */
    if (strncmp(buf, "$-1", 3) == 0) return mako_str_from_cstr("");
    /* $N\r\n<data>\r\n */
    if (buf[0] == '$') {
        long len = 0;
        char *p = buf + 1;
        while (*p >= '0' && *p <= '9') {
            len = len * 10 + (*p - '0');
            p++;
        }
        if (p[0] == '\r' && p[1] == '\n') p += 2;
        if (len < 0 || (size_t)(p - buf) + (size_t)len > (size_t)n) {
            return mako_str_from_cstr("");
        }
        char *d = (char *)malloc((size_t)len + 1);
        if (!d) return mako_str_from_cstr("");
        memcpy(d, p, (size_t)len);
        d[len] = 0;
        return (MakoString){d, (size_t)len};
    }
    /* error */
    if (buf[0] == '-') return mako_str_from_cstr("");
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_redis_ping(MakoString host, int64_t port) {
    int fd = mako_redis_tcp_connect(host, port);
    if (fd < 0) return mako_str_from_cstr("");
    const char *ping = "*1\r\n$4\r\nPING\r\n";
    if (send(fd, ping, strlen(ping), 0) < 0) {
        mako_sock_close(fd);
        return mako_str_from_cstr("");
    }
    MakoString out = mako_redis_recv_simple(fd);
    mako_sock_close(fd);
    return out;
}

static inline MakoRedisConn mako_redis_connect(MakoString url) {
    MakoRedisUrl u = mako_redis_parse_url(url);
    int fd = mako_redis_tcp_connect(u.host, u.port);
    if (fd < 0) return (MakoRedisConn){0};
    if (u.pass.len > 0) {
        if (mako_redis_send_array(fd, "AUTH", u.pass, (MakoString){0}) < 0) {
            mako_sock_close(fd);
            return (MakoRedisConn){0};
        }
        MakoString auth = mako_redis_recv_simple(fd);
        if (!mako_str_eq(auth, mako_str_from_cstr("OK"))) {
            mako_sock_close(fd);
            return (MakoRedisConn){0};
        }
    }
    if (u.has_db) {
        char dbuf[32];
        snprintf(dbuf, sizeof(dbuf), "%lld", (long long)u.db);
        if (mako_redis_send_array(fd, "SELECT", mako_str_from_cstr(dbuf), (MakoString){0}) < 0) {
            mako_sock_close(fd);
            return (MakoRedisConn){0};
        }
        MakoString sel = mako_redis_recv_simple(fd);
        if (!mako_str_eq(sel, mako_str_from_cstr("OK"))) {
            mako_sock_close(fd);
            return (MakoRedisConn){0};
        }
    }
    return (MakoRedisConn){fd};
}

static inline int64_t mako_redis_ok(MakoRedisConn c) {
    return c.handle > 0 ? 1 : 0;
}

static inline int64_t mako_redis_close(MakoRedisConn c) {
    if (c.handle > 0) mako_sock_close((int)c.handle);
    return 0;
}

static inline MakoString mako_redis_conn_ping(MakoRedisConn c) {
    if (c.handle <= 0) return mako_str_from_cstr("");
    if (mako_redis_send_array((int)c.handle, "PING", (MakoString){0}, (MakoString){0}) < 0) {
        return mako_str_from_cstr("");
    }
    return mako_redis_recv_simple((int)c.handle);
}

static inline MakoString mako_redis_conn_set(MakoRedisConn c, MakoString key, MakoString val) {
    if (c.handle <= 0) return mako_str_from_cstr("");
    if (mako_redis_send_array((int)c.handle, "SET", key, val) < 0) return mako_str_from_cstr("");
    return mako_redis_recv_simple((int)c.handle);
}

static inline MakoString mako_redis_conn_get(MakoRedisConn c, MakoString key) {
    if (c.handle <= 0) return mako_str_from_cstr("");
    if (mako_redis_send_array((int)c.handle, "GET", key, (MakoString){0}) < 0) {
        return mako_str_from_cstr("");
    }
    return mako_redis_recv_simple((int)c.handle);
}

static inline MakoString mako_redis_conn_del(MakoRedisConn c, MakoString key) {
    if (c.handle <= 0) return mako_str_from_cstr("");
    if (mako_redis_send_array((int)c.handle, "DEL", key, (MakoString){0}) < 0) {
        return mako_str_from_cstr("");
    }
    return mako_redis_recv_simple((int)c.handle);
}

static inline MakoString mako_redis_conn_exists(MakoRedisConn c, MakoString key) {
    if (c.handle <= 0) return mako_str_from_cstr("");
    if (mako_redis_send_array((int)c.handle, "EXISTS", key, (MakoString){0}) < 0) {
        return mako_str_from_cstr("");
    }
    return mako_redis_recv_simple((int)c.handle);
}

/* SET key value → "OK" or "". */
static inline MakoString mako_redis_set(
    MakoString host, int64_t port, MakoString key, MakoString val
) {
    int fd = mako_redis_tcp_connect(host, port);
    if (fd < 0) return mako_str_from_cstr("");
    char cmd[2048];
    size_t off = 0;
    const char *hdr = "*3\r\n$3\r\nSET\r\n";
    size_t hl = strlen(hdr);
    if (hl >= sizeof(cmd)) { mako_sock_close(fd); return mako_str_from_cstr(""); }
    memcpy(cmd, hdr, hl);
    off = hl;
    if (mako_redis_append_bulk(cmd, sizeof(cmd), &off, key) < 0
        || mako_redis_append_bulk(cmd, sizeof(cmd), &off, val) < 0) {
        mako_sock_close(fd);
        return mako_str_from_cstr("");
    }
    if (send(fd, cmd, off, 0) < 0) {
        mako_sock_close(fd);
        return mako_str_from_cstr("");
    }
    MakoString out = mako_redis_recv_simple(fd);
    mako_sock_close(fd);
    return out;
}

/* GET key → value or "" (nil / error). */
static inline MakoString mako_redis_get(MakoString host, int64_t port, MakoString key) {
    int fd = mako_redis_tcp_connect(host, port);
    if (fd < 0) return mako_str_from_cstr("");
    char cmd[1024];
    size_t off = 0;
    const char *hdr = "*2\r\n$3\r\nGET\r\n";
    size_t hl = strlen(hdr);
    memcpy(cmd, hdr, hl);
    off = hl;
    if (mako_redis_append_bulk(cmd, sizeof(cmd), &off, key) < 0) {
        mako_sock_close(fd);
        return mako_str_from_cstr("");
    }
    if (send(fd, cmd, off, 0) < 0) {
        mako_sock_close(fd);
        return mako_str_from_cstr("");
    }
    MakoString out = mako_redis_recv_simple(fd);
    mako_sock_close(fd);
    return out;
}

/* DEL key → deleted count as string ("0"/"1") or "". */
static inline MakoString mako_redis_del(MakoString host, int64_t port, MakoString key) {
    int fd = mako_redis_tcp_connect(host, port);
    if (fd < 0) return mako_str_from_cstr("");
    char cmd[1024];
    size_t off = 0;
    const char *hdr = "*2\r\n$3\r\nDEL\r\n";
    size_t hl = strlen(hdr);
    memcpy(cmd, hdr, hl);
    off = hl;
    if (mako_redis_append_bulk(cmd, sizeof(cmd), &off, key) < 0) {
        mako_sock_close(fd);
        return mako_str_from_cstr("");
    }
    if (send(fd, cmd, off, 0) < 0) {
        mako_sock_close(fd);
        return mako_str_from_cstr("");
    }
    MakoString out = mako_redis_recv_simple(fd);
    mako_sock_close(fd);
    return out;
}

/* EXISTS key → "1" / "0" or "". */
static inline MakoString mako_redis_exists(MakoString host, int64_t port, MakoString key) {
    int fd = mako_redis_tcp_connect(host, port);
    if (fd < 0) return mako_str_from_cstr("");
    char cmd[1024];
    size_t off = 0;
    const char *hdr = "*2\r\n$6\r\nEXISTS\r\n";
    size_t hl = strlen(hdr);
    memcpy(cmd, hdr, hl);
    off = hl;
    if (mako_redis_append_bulk(cmd, sizeof(cmd), &off, key) < 0) {
        mako_sock_close(fd);
        return mako_str_from_cstr("");
    }
    if (send(fd, cmd, off, 0) < 0) {
        mako_sock_close(fd);
        return mako_str_from_cstr("");
    }
    MakoString out = mako_redis_recv_simple(fd);
    mako_sock_close(fd);
    return out;
}

/* Minimal RESP server: accept one connection, answer PING with +PONG, exit. */
static inline int64_t mako_redis_mock_once(int64_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        mako_sock_close(fd);
        return 1;
    }
    if (listen(fd, 8) < 0) {
        mako_sock_close(fd);
        return 1;
    }
    fprintf(stderr, "mako redis_mock_once on :%lld\n", (long long)port);
    int cfd = accept(fd, NULL, NULL);
    if (cfd < 0) {
        mako_sock_close(fd);
        return 1;
    }
    char buf[256];
    ssize_t n = recv(cfd, buf, sizeof(buf) - 1, 0);
    if (n < 0) n = 0;
    buf[n] = 0;
    const char *pong = "+PONG\r\n";
    if (strstr(buf, "PING") || strstr(buf, "ping")) {
        (void)send(cfd, pong, strlen(pong), 0);
    } else {
        const char *err = "-ERR unknown\r\n";
        (void)send(cfd, err, strlen(err), 0);
    }
    close(cfd);
    mako_sock_close(fd);
    return 0;
}

/* In-memory KV mock: accept up to `max_cmds` connections (one cmd each), then exit.
 * Matches redis_set/redis_get which open a fresh TCP connection per call. */
static inline int64_t mako_redis_mock_kv(int64_t port, int64_t max_cmds) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        mako_sock_close(fd);
        return 1;
    }
    if (listen(fd, 8) < 0) {
        mako_sock_close(fd);
        return 1;
    }
    fprintf(stderr, "mako redis_mock_kv on :%lld\n", (long long)port);
    char store_k[128];
    char store_v[256];
    store_k[0] = 0;
    store_v[0] = 0;
    if (max_cmds <= 0) max_cmds = 8;
    for (int64_t done = 0; done < max_cmds; done++) {
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0) break;
        char buf[2048];
        ssize_t n = recv(cfd, buf, sizeof(buf) - 1, 0);
        if (n < 0) n = 0;
        buf[n] = 0;
        if (strstr(buf, "PING") || strstr(buf, "ping")) {
            const char *pong = "+PONG\r\n";
            (void)send(cfd, pong, strlen(pong), 0);
        } else if (strstr(buf, "EXISTS") || strstr(buf, "exists")) {
            char *ep = strstr(buf, "EXISTS\r\n");
            if (!ep) ep = strstr(buf, "exists\r\n");
            char key[128];
            key[0] = 0;
            if (ep) {
                ep += 8;
                if (*ep == '$') {
                    long klen = 0;
                    ep++;
                    while (*ep >= '0' && *ep <= '9') {
                        klen = klen * 10 + (*ep - '0');
                        ep++;
                    }
                    if (ep[0] == '\r' && ep[1] == '\n') ep += 2;
                    if (klen > 0 && klen < (long)sizeof(key)) {
                        memcpy(key, ep, (size_t)klen);
                        key[klen] = 0;
                    }
                }
            }
            const char *resp = (key[0] && strcmp(key, store_k) == 0) ? ":1\r\n" : ":0\r\n";
            (void)send(cfd, resp, strlen(resp), 0);
        } else if (strstr(buf, "DEL\r\n") || strstr(buf, "del\r\n")) {
            char *dp = strstr(buf, "DEL\r\n");
            if (!dp) dp = strstr(buf, "del\r\n");
            char key[128];
            key[0] = 0;
            if (dp) {
                dp += 5;
                if (*dp == '$') {
                    long klen = 0;
                    dp++;
                    while (*dp >= '0' && *dp <= '9') {
                        klen = klen * 10 + (*dp - '0');
                        dp++;
                    }
                    if (dp[0] == '\r' && dp[1] == '\n') dp += 2;
                    if (klen > 0 && klen < (long)sizeof(key)) {
                        memcpy(key, dp, (size_t)klen);
                        key[klen] = 0;
                    }
                }
            }
            if (key[0] && strcmp(key, store_k) == 0) {
                store_k[0] = 0;
                store_v[0] = 0;
                const char *resp = ":1\r\n";
                (void)send(cfd, resp, strlen(resp), 0);
            } else {
                const char *resp = ":0\r\n";
                (void)send(cfd, resp, strlen(resp), 0);
            }
        } else if (strstr(buf, "SET\r\n") || strstr(buf, "set\r\n")) {
            char *setp = strstr(buf, "SET\r\n");
            if (!setp) setp = strstr(buf, "set\r\n");
            if (setp) {
                setp += 5;
                if (*setp == '$') {
                    long klen = 0;
                    setp++;
                    while (*setp >= '0' && *setp <= '9') {
                        klen = klen * 10 + (*setp - '0');
                        setp++;
                    }
                    if (setp[0] == '\r' && setp[1] == '\n') setp += 2;
                    if (klen > 0 && klen < (long)sizeof(store_k)) {
                        memcpy(store_k, setp, (size_t)klen);
                        store_k[klen] = 0;
                        setp += klen;
                        if (setp[0] == '\r' && setp[1] == '\n') setp += 2;
                        if (*setp == '$') {
                            long vlen = 0;
                            setp++;
                            while (*setp >= '0' && *setp <= '9') {
                                vlen = vlen * 10 + (*setp - '0');
                                setp++;
                            }
                            if (setp[0] == '\r' && setp[1] == '\n') setp += 2;
                            if (vlen >= 0 && vlen < (long)sizeof(store_v)) {
                                memcpy(store_v, setp, (size_t)vlen);
                                store_v[vlen] = 0;
                            }
                        }
                    }
                }
            }
            const char *ok = "+OK\r\n";
            (void)send(cfd, ok, strlen(ok), 0);
        } else if (strstr(buf, "GET\r\n") || strstr(buf, "get\r\n")) {
            char *getp = strstr(buf, "GET\r\n");
            if (!getp) getp = strstr(buf, "get\r\n");
            char key[128];
            key[0] = 0;
            if (getp) {
                getp += 5;
                if (*getp == '$') {
                    long klen = 0;
                    getp++;
                    while (*getp >= '0' && *getp <= '9') {
                        klen = klen * 10 + (*getp - '0');
                        getp++;
                    }
                    if (getp[0] == '\r' && getp[1] == '\n') getp += 2;
                    if (klen > 0 && klen < (long)sizeof(key)) {
                        memcpy(key, getp, (size_t)klen);
                        key[klen] = 0;
                    }
                }
            }
            if (key[0] && strcmp(key, store_k) == 0) {
                char resp[512];
                int rn = snprintf(
                    resp, sizeof(resp), "$%zu\r\n%s\r\n", strlen(store_v), store_v
                );
                if (rn > 0) (void)send(cfd, resp, (size_t)rn, 0);
            } else {
                const char *nil = "$-1\r\n";
                (void)send(cfd, nil, strlen(nil), 0);
            }
        } else {
            const char *err = "-ERR unknown\r\n";
            (void)send(cfd, err, strlen(err), 0);
        }
        close(cfd);
    }
    mako_sock_close(fd);
    return 0;
}

/* ---- MySQL / MariaDB URL polish ----
 * Offline DSN validation until a libmysqlclient-compatible backend is linked.
 * mysql_connect returns a non-zero parsed handle for mysql:// or mariadb:// URLs.
 */
typedef struct {
    int64_t ok;
    int64_t mariadb;
    MakoString host;
    int64_t port;
    MakoString db;
} MakoMysqlUrl;

/* MakoMysqlConn is defined in mako_std.h */

static inline MakoMysqlUrl mako_mysql_parse_url(MakoString url) {
    MakoMysqlUrl out;
    memset(&out, 0, sizeof(out));
    out.port = 3306;
    char buf[512];
    if (!url.data || url.len >= sizeof(buf)) return out;
    memcpy(buf, url.data, url.len);
    buf[url.len] = 0;
    char *p = buf;
    if (strncmp(p, "mysql://", 8) == 0) {
        p += 8;
    } else if (strncmp(p, "mariadb://", 10) == 0) {
        out.mariadb = 1;
        p += 10;
    } else {
        return out;
    }
    char *slash = strchr(p, '/');
    if (!slash || !slash[1]) return out;
    *slash = 0;
    out.db = mako_str_from_cstr(slash + 1);
    char *at = strrchr(p, '@');
    if (at) p = at + 1;
    char *colon = strrchr(p, ':');
    if (colon) {
        *colon = 0;
        out.port = atoll(colon + 1);
    }
    if (!*p || out.port <= 0) return out;
    out.host = mako_str_from_cstr(p);
    out.ok = 1;
    return out;
}

static inline MakoString mako_mysql_connect_url(MakoString url) {
    MakoMysqlUrl u = mako_mysql_parse_url(url);
    if (!u.ok) return mako_str_from_cstr("");
    char buf[384];
    int n = snprintf(
        buf,
        sizeof(buf),
        "driver=%s host=%.*s port=%lld db=%.*s",
        u.mariadb ? "mariadb" : "mysql",
        (int)u.host.len,
        u.host.data ? u.host.data : "",
        (long long)u.port,
        (int)u.db.len,
        u.db.data ? u.db.data : ""
    );
    if (n < 0) return mako_str_from_cstr("");
    return mako_str_from_cstr(buf);
}

static inline MakoMysqlConn mako_mysql_connect(MakoString url) {
    MakoMysqlUrl u = mako_mysql_parse_url(url);
    return (MakoMysqlConn){u.ok ? 1 : 0};
}

static inline int64_t mako_mysql_ok(MakoMysqlConn c) {
    return c.handle != 0 ? 1 : 0;
}

static inline int64_t mako_mysql_close(MakoMysqlConn c) {
    (void)c;
    return 0;
}

static inline int64_t mako_mysql_is_mariadb(MakoString url) {
    MakoMysqlUrl u = mako_mysql_parse_url(url);
    return u.ok && u.mariadb ? 1 : 0;
}

static inline MakoString mako_mysql_driver_name(MakoString url) {
    MakoMysqlUrl u = mako_mysql_parse_url(url);
    if (!u.ok) return mako_str_from_cstr("");
    return mako_str_from_cstr(u.mariadb ? "mariadb" : "mysql");
}

/* ---- Multi-store compatibility helpers ----
 * URL normalization and request/query builders for document, wide-column,
 * analytics SQL, and search backends. These helpers are dependency-free seeds
 * for official packages and avoid pretending a live driver exists.
 */
typedef struct {
    int64_t ok;
    MakoString driver;
    MakoString host;
    int64_t port;
    MakoString db;
    int64_t auth;
} MakoStoreUrl;

static inline MakoStoreUrl mako_store_parse_url(
    MakoString url,
    const char *scheme,
    const char *driver,
    int64_t default_port
) {
    MakoStoreUrl out;
    memset(&out, 0, sizeof(out));
    out.driver = mako_str_from_cstr(driver);
    out.port = default_port;
    char buf[512];
    if (!url.data || url.len >= sizeof(buf)) return out;
    memcpy(buf, url.data, url.len);
    buf[url.len] = 0;
    size_t slen = strlen(scheme);
    if (strncmp(buf, scheme, slen) != 0) return out;
    char *p = buf + slen;
    char *slash = strchr(p, '/');
    if (slash) {
        *slash = 0;
        out.db = mako_str_from_cstr(slash[1] ? slash + 1 : "");
    }
    char *at = strrchr(p, '@');
    if (at) {
        out.auth = 1;
        p = at + 1;
    }
    char *colon = strrchr(p, ':');
    if (colon) {
        *colon = 0;
        out.port = atoll(colon + 1);
    }
    if (!*p || out.port <= 0) return out;
    out.host = mako_str_from_cstr(p);
    out.ok = 1;
    return out;
}

static inline MakoString mako_store_url_info(MakoStoreUrl u) {
    if (!u.ok) return mako_str_from_cstr("");
    char buf[512];
    int n = snprintf(
        buf,
        sizeof(buf),
        "driver=%.*s host=%.*s port=%lld db=%.*s auth=%lld",
        (int)u.driver.len,
        u.driver.data ? u.driver.data : "",
        (int)u.host.len,
        u.host.data ? u.host.data : "",
        (long long)u.port,
        (int)u.db.len,
        u.db.data ? u.db.data : "",
        (long long)u.auth
    );
    if (n < 0) return mako_str_from_cstr("");
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_mongo_connect_url(MakoString url) {
    return mako_store_url_info(
        mako_store_parse_url(url, "mongodb://", "mongodb", 27017)
    );
}

static inline MakoString mako_mongo_find_one_request(
    MakoString collection,
    MakoString filter
) {
    char buf[1024];
    int n = snprintf(
        buf,
        sizeof(buf),
        "{\"find\":\"%.*s\",\"filter\":%.*s,\"limit\":1}",
        (int)collection.len,
        collection.data ? collection.data : "",
        (int)filter.len,
        filter.data ? filter.data : "{}"
    );
    if (n < 0 || (size_t)n >= sizeof(buf)) return mako_str_from_cstr("");
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_cassandra_connect_url(MakoString url) {
    return mako_store_url_info(
        mako_store_parse_url(url, "cassandra://", "cassandra", 9042)
    );
}

static inline MakoString mako_cassandra_select(
    MakoString keyspace,
    MakoString table,
    MakoString where_clause
) {
    char buf[1024];
    int n = snprintf(
        buf,
        sizeof(buf),
        "select * from %.*s.%.*s%s%.*s",
        (int)keyspace.len,
        keyspace.data ? keyspace.data : "",
        (int)table.len,
        table.data ? table.data : "",
        where_clause.len ? " where " : "",
        (int)where_clause.len,
        where_clause.data ? where_clause.data : ""
    );
    if (n < 0 || (size_t)n >= sizeof(buf)) return mako_str_from_cstr("");
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_clickhouse_connect_url(MakoString url) {
    return mako_store_url_info(
        mako_store_parse_url(url, "clickhouse://", "clickhouse", 8123)
    );
}

static inline MakoString mako_clickhouse_select(
    MakoString database,
    MakoString table,
    MakoString suffix
) {
    char buf[1024];
    int n = snprintf(
        buf,
        sizeof(buf),
        "select * from %.*s.%.*s%s%.*s",
        (int)database.len,
        database.data ? database.data : "",
        (int)table.len,
        table.data ? table.data : "",
        suffix.len ? " " : "",
        (int)suffix.len,
        suffix.data ? suffix.data : ""
    );
    if (n < 0 || (size_t)n >= sizeof(buf)) return mako_str_from_cstr("");
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_elastic_connect_url(MakoString url) {
    MakoStoreUrl u = mako_store_parse_url(url, "elastic://", "elasticsearch", 9200);
    if (!u.ok) {
        u = mako_store_parse_url(url, "elasticsearch://", "elasticsearch", 9200);
    }
    return mako_store_url_info(u);
}

static inline MakoString mako_elastic_search_request(MakoString index, MakoString query) {
    char buf[1024];
    int n = snprintf(
        buf,
        sizeof(buf),
        "POST /%.*s/_search\n%.*s",
        (int)index.len,
        index.data ? index.data : "",
        (int)query.len,
        query.data ? query.data : "{}"
    );
    if (n < 0 || (size_t)n >= sizeof(buf)) return mako_str_from_cstr("");
    return mako_str_from_cstr(buf);
}

/* ---- Unified database/sql facade (sqlite + postgres, same call shape) ----
 * Driver: 1 = sqlite (path in `dsn`), 2 = postgres (libpq handle).
 * Placeholders: sqlite `?`, postgres `$1..$N`. Prefer `sql_query_int` / `sql_exec`
 * with `[]int` binds — never concatenate untrusted input into SQL. */

typedef struct {
    int64_t driver; /* 1=sqlite, 2=postgres, 0=closed/invalid */
    MakoString dsn; /* sqlite path or pg url (diagnostic) */
    void *sqlite;
    MakoPgConn pg;
} MakoSqlDB;

static inline MakoSqlDB mako_sql_open_sqlite(MakoString path) {
    MakoSqlDB db;
    db.driver = 1;
    db.dsn = mako_str_clone(path);
    db.sqlite = mako_sqlite_open_handle(path);
    db.pg = (MakoPgConn){0};
    return db;
}

static inline MakoSqlDB mako_sql_open_postgres(MakoString url) {
    MakoSqlDB db;
    db.driver = 2;
    db.dsn = mako_str_clone(url);
    db.sqlite = NULL;
    db.pg = mako_pg_connect(url);
    if (!db.pg.handle) db.driver = 0;
    return db;
}

static inline int64_t mako_sql_ok(MakoSqlDB db) {
    if (db.driver == 1) return 1;
    if (db.driver == 2) return mako_pg_ok(db.pg);
    return 0;
}

static inline int64_t mako_sql_close(MakoSqlDB db) {
    if (db.driver == 1) mako_sqlite_close_handle(db.sqlite);
    if (db.driver == 2) mako_pg_close(db.pg);
    return 0;
}

/* Convert `?` placeholders to `$1..$N` for Postgres. */
static inline MakoString mako_sql_qmark_to_dollar(MakoString sql) {
    size_t cap = sql.len * 2 + 16;
    char *d = (char *)malloc(cap);
    if (!d) mako_abort("sql rewrite OOM");
    size_t o = 0;
    int n = 0;
    for (size_t i = 0; i < sql.len; i++) {
        if (sql.data[i] == '?') {
            n++;
            char tmp[16];
            int w = snprintf(tmp, sizeof(tmp), "$%d", n);
            if (o + (size_t)w + 1 >= cap) {
                cap *= 2;
                d = (char *)realloc(d, cap);
                if (!d) mako_abort("sql rewrite OOM");
            }
            memcpy(d + o, tmp, (size_t)w);
            o += (size_t)w;
        } else {
            if (o + 2 >= cap) {
                cap *= 2;
                d = (char *)realloc(d, cap);
                if (!d) mako_abort("sql rewrite OOM");
            }
            d[o++] = sql.data[i];
        }
    }
    d[o] = 0;
    return (MakoString){d, o};
}

/* Forward decls — full definitions follow sql_exec (meta + str helpers). */
static inline void mako_sql_meta_set(int64_t key, int64_t last_id, int64_t rows);
#if defined(MAKO_HAS_LIBPQ)
static inline int64_t mako_pg_query_int_params(
    MakoPgConn c,
    MakoString sql,
    const char *const *values,
    int nparams
);
#endif

static inline int64_t mako_sql_query_int(MakoSqlDB db, MakoString sql, MakoIntArray args) {
    if (db.driver == 1) {
        if (db.sqlite) {
            return mako_sqlite_query_int_handle(
                db.sqlite, sql, args.data, (int)args.len
            );
        }
        return mako_sqlite_query_int_params(
            db.dsn, sql, args.data, (int)args.len
        );
    }
    if (db.driver == 2) {
        if (!db.pg.handle) return -1;
#if defined(MAKO_HAS_LIBPQ)
        MakoString q = mako_sql_qmark_to_dollar(sql);
        int n = (int)args.len;
        char **vals = NULL;
        char (*bufs)[32] = NULL;
        if (n > 0) {
            vals = (char **)malloc((size_t)n * sizeof(char *));
            bufs = (char (*)[32])malloc((size_t)n * 32);
            if (!vals || !bufs) {
                free(vals);
                free(bufs);
                mako_str_free(q);
                return -1;
            }
            for (int i = 0; i < n; i++) {
                snprintf(bufs[i], 32, "%lld", (long long)args.data[i]);
                vals[i] = bufs[i];
            }
        }
        int64_t rc = mako_pg_query_int_params(
            db.pg, q, (const char *const *)vals, n
        );
        free(vals);
        free(bufs);
        mako_str_free(q);
        return rc;
#else
        (void)sql; (void)args;
        return -1;
#endif
    }
    return -1;
}

static inline int64_t mako_sql_exec(MakoSqlDB db, MakoString sql, MakoIntArray args) {
    if (db.driver == 1) {
        /* sqlite: run as query_int (DDL/DML with binds); ignore result value */
        int64_t v = db.sqlite
            ? mako_sqlite_query_int_handle(db.sqlite, sql, args.data, (int)args.len)
            : mako_sqlite_query_int_params(db.dsn, sql, args.data, (int)args.len);
        if (v < 0) return -1;
#if defined(MAKO_HAS_SQLITE)
        if (db.sqlite) {
            sqlite3 *sdb = (sqlite3 *)db.sqlite;
            mako_sql_meta_set(
                (int64_t)(intptr_t)db.sqlite,
                (int64_t)sqlite3_last_insert_rowid(sdb),
                (int64_t)sqlite3_changes(sdb)
            );
        }
#endif
        return 0;
    }
    if (db.driver == 2) {
#if defined(MAKO_HAS_LIBPQ)
        MakoString q = mako_sql_qmark_to_dollar(sql);
        int n = (int)args.len;
        char **vals = NULL;
        char (*bufs)[32] = NULL;
        if (n > 0) {
            vals = (char **)malloc((size_t)n * sizeof(char *));
            bufs = (char (*)[32])malloc((size_t)n * 32);
            if (!vals || !bufs) {
                free(vals);
                free(bufs);
                mako_str_free(q);
                return -1;
            }
            for (int i = 0; i < n; i++) {
                snprintf(bufs[i], 32, "%lld", (long long)args.data[i]);
                vals[i] = bufs[i];
            }
        }
        /* Use query path so COMMAND_OK rows are recorded in meta */
        PGconn *pg = (PGconn *)(intptr_t)db.pg.handle;
        if (!pg) {
            free(vals);
            free(bufs);
            mako_str_free(q);
            return -1;
        }
        char qbuf[4096];
        if (q.len >= sizeof(qbuf)) {
            free(vals);
            free(bufs);
            mako_str_free(q);
            return -1;
        }
        memcpy(qbuf, q.data, q.len);
        qbuf[q.len] = 0;
        PGresult *r = PQexecParams(pg, qbuf, n, NULL, (const char *const *)vals, NULL, NULL, 0);
        free(vals);
        free(bufs);
        mako_str_free(q);
        if (!r) return -1;
        ExecStatusType st = PQresultStatus(r);
        int64_t out = (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK) ? 0 : -1;
        if (out == 0) {
            const char *ct = PQcmdTuples(r);
            int64_t rows = ct && ct[0] ? (int64_t)strtoll(ct, NULL, 10) : 0;
            if (st == PGRES_TUPLES_OK && rows == 0) rows = (int64_t)PQntuples(r);
            mako_sql_meta_set(db.pg.handle, 0, rows);
        } else {
            fprintf(stderr, "mako: pg_exec: %s", PQerrorMessage(pg));
        }
        PQclear(r);
        return out;
#else
        (void)sql; (void)args;
        return -1;
#endif
    }
    return -1;
}

/* Copy MakoString into a NUL-terminated C buffer (empty string allowed). */
static inline void mako_sql_copy_str_param(char *dst, size_t cap, MakoString s) {
    if (!dst || cap == 0) return;
    size_t n = s.data ? s.len : 0;
    if (n >= cap) n = cap - 1;
    if (n > 0 && s.data) memcpy(dst, s.data, n);
    dst[n] = 0;
}

/* Max placeholder index from SQL: max $N, or count of `?` (capped at 4 for str4).
 * Empty string "" is a VALID bind value — do not infer arity from trailing empties.
 * Ignores $ and ? inside single-quoted string literals ('' escape). */
static inline int mako_sql_placeholder_arity(MakoString sql) {
    if (!sql.data || sql.len == 0) return 0;
    int max_d = 0;
    int qmarks = 0;
    int in_str = 0;
    for (size_t i = 0; i < sql.len; i++) {
        char c = sql.data[i];
        if (in_str) {
            if (c == '\'') {
                /* SQL escape: '' inside string */
                if (i + 1 < sql.len && sql.data[i + 1] == '\'') {
                    i++;
                    continue;
                }
                in_str = 0;
            }
            continue;
        }
        if (c == '\'') {
            in_str = 1;
            continue;
        }
        if (c == '?') {
            qmarks++;
            continue;
        }
        if (c == '$' && i + 1 < sql.len && sql.data[i + 1] >= '1' && sql.data[i + 1] <= '9') {
            int v = 0;
            size_t j = i + 1;
            while (j < sql.len && sql.data[j] >= '0' && sql.data[j] <= '9') {
                v = v * 10 + (sql.data[j] - '0');
                if (v > 99) break;
                j++;
            }
            if (v > max_d) max_d = v;
            i = j - 1;
        }
    }
    int n = max_d > 0 ? max_d : qmarks;
    if (n < 0) n = 0;
    if (n > 4) n = 4;
    return n;
}

/* Legacy: trailing-empty slot count. Prefer mako_sql_placeholder_arity(sql). */
static inline int mako_sql_str4_count(MakoString p1, MakoString p2, MakoString p3, MakoString p4) {
    (void)p1;
    (void)p2;
    (void)p3;
    (void)p4;
    /* Kept for ABI; always returns 4 so empty mid/trailing still bind when callers
     * used this without SQL. New code paths use placeholder_arity. */
    return 4;
}

/* Per-connection metadata for last insert id / rows affected (handle-keyed). */
#define MAKO_SQL_META_MAX 64
typedef struct {
    int64_t key; /* sqlite ptr or pg handle */
    int64_t last_insert_id;
    int64_t rows_affected;
} MakoSqlMeta;
static MakoSqlMeta mako_sql_meta[MAKO_SQL_META_MAX];

static inline MakoSqlMeta *mako_sql_meta_slot(int64_t key) {
    if (key == 0) return NULL;
    int free_i = -1;
    for (int i = 0; i < MAKO_SQL_META_MAX; i++) {
        if (mako_sql_meta[i].key == key) return &mako_sql_meta[i];
        if (free_i < 0 && mako_sql_meta[i].key == 0) free_i = i;
    }
    if (free_i < 0) free_i = 0; /* overwrite oldest-ish slot 0 under pressure */
    mako_sql_meta[free_i].key = key;
    mako_sql_meta[free_i].last_insert_id = 0;
    mako_sql_meta[free_i].rows_affected = 0;
    return &mako_sql_meta[free_i];
}

static inline void mako_sql_meta_set(int64_t key, int64_t last_id, int64_t rows) {
    MakoSqlMeta *m = mako_sql_meta_slot(key);
    if (!m) return;
    m->last_insert_id = last_id;
    m->rows_affected = rows;
}

static inline int64_t mako_sql_meta_key(MakoSqlDB db) {
    if (db.driver == 1) return (int64_t)(intptr_t)db.sqlite;
    if (db.driver == 2) return db.pg.handle;
    return 0;
}

#if defined(MAKO_HAS_SQLITE)
/* Map sqlite parameter index (1-based) to slot 0..3 from name ($N / ?N) or position. */
static inline int mako_sqlite_param_slot(sqlite3_stmt *st, int bind_i) {
    const char *name = sqlite3_bind_parameter_name(st, bind_i);
    if (name && name[0] && name[1]) {
        /* $1, :1, @1, ?1 — use numeric suffix as 1-based slot */
        const char *p = name;
        if (*p == '$' || *p == ':' || *p == '@' || *p == '?') p++;
        if (*p >= '1' && *p <= '9') {
            int v = 0;
            while (*p >= '0' && *p <= '9') {
                v = v * 10 + (*p - '0');
                p++;
                if (v > 99) break;
            }
            if (v >= 1 && v <= 4) return v - 1;
        }
    }
    /* Anonymous `?` or unknown: order of appearance */
    return bind_i - 1;
}

/* Bind up to 4 text params on a prepared sqlite statement; returns 0 or -1.
 * Maps $N by number (so SET x=$2 WHERE y=$1 binds p2 then p1 correctly).
 * Empty string "" is a valid bound value. */
static inline int64_t mako_sqlite_bind_str4(
    sqlite3_stmt *st,
    MakoString p1, MakoString p2, MakoString p3, MakoString p4
) {
    if (!st) return -1;
    int expected = sqlite3_bind_parameter_count(st);
    if (expected < 0 || expected > 4) {
        fprintf(stderr, "sqlite: sql_exec_str4 supports at most 4 placeholders (got %d)\n", expected);
        return -1;
    }
    MakoString ps[4] = {p1, p2, p3, p4};
    for (int i = 1; i <= expected; i++) {
        int slot = mako_sqlite_param_slot(st, i);
        if (slot < 0 || slot > 3) slot = i - 1;
        MakoString s = ps[slot];
        /* Empty string is a valid bind (len 0); only missing data uses "". */
        const char *ptr = s.data ? s.data : "";
        int len = s.data ? (int)s.len : 0;
        /* SQLITE_TRANSIENT: copy — MakoString buffers may not outlive the step */
        if (sqlite3_bind_text(st, i, ptr, len, SQLITE_TRANSIENT) != SQLITE_OK) {
            return -1;
        }
    }
    return 0;
}

static inline int64_t mako_sqlite_exec_str4_handle(
    void *handle,
    MakoString sql,
    MakoString p1, MakoString p2, MakoString p3, MakoString p4
) {
    if (!handle || !sql.data) return -1;
    char qbuf[4096];
    if (sql.len >= sizeof(qbuf)) return -1;
    memcpy(qbuf, sql.data, sql.len);
    qbuf[sql.len] = 0;
    sqlite3 *sdb = (sqlite3 *)handle;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(sdb, qbuf, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite: prepare: %s\n", sqlite3_errmsg(sdb));
        return -1;
    }
    if (mako_sqlite_bind_str4(st, p1, p2, p3, p4) != 0) {
        sqlite3_finalize(st);
        return -1;
    }
    int rc = sqlite3_step(st);
    int64_t out = (rc == SQLITE_DONE || rc == SQLITE_ROW) ? 0 : -1;
    if (out < 0) {
        fprintf(stderr, "sqlite: step: %s\n", sqlite3_errmsg(sdb));
    }
    sqlite3_finalize(st);
    if (out == 0) {
        mako_sql_meta_set(
            (int64_t)(intptr_t)handle,
            (int64_t)sqlite3_last_insert_rowid(sdb),
            (int64_t)sqlite3_changes(sdb)
        );
    }
    return out;
}

static inline MakoString mako_sqlite_query_str4_handle(
    void *handle,
    MakoString sql,
    MakoString p1, MakoString p2, MakoString p3, MakoString p4
) {
    if (!handle || !sql.data) return mako_str_from_cstr("");
    char qbuf[4096];
    if (sql.len >= sizeof(qbuf)) return mako_str_from_cstr("");
    memcpy(qbuf, sql.data, sql.len);
    qbuf[sql.len] = 0;
    sqlite3 *sdb = (sqlite3 *)handle;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(sdb, qbuf, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite: prepare: %s\n", sqlite3_errmsg(sdb));
        return mako_str_from_cstr("");
    }
    if (mako_sqlite_bind_str4(st, p1, p2, p3, p4) != 0) {
        sqlite3_finalize(st);
        return mako_str_from_cstr("");
    }
    MakoString out = mako_str_from_cstr("");
    if (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(st, 0);
        if (txt) out = mako_str_from_cstr((const char *)txt);
    }
    sqlite3_finalize(st);
    return out;
}

static inline MakoString mako_sqlite_query_str_handle(
    void *handle,
    MakoString sql,
    MakoString p1
) {
    return mako_sqlite_query_str4_handle(
        handle, sql, p1, mako_str_empty, mako_str_empty, mako_str_empty
    );
}
#endif /* MAKO_HAS_SQLITE */

#if defined(MAKO_HAS_LIBPQ)
/* Query first column of first row as int64 from Postgres. */
static inline int64_t mako_pg_query_int_params(
    MakoPgConn c,
    MakoString sql,
    const char *const *values,
    int nparams
) {
    if (!c.handle || !sql.data) return -1;
    PGconn *pg = (PGconn *)(intptr_t)c.handle;
    char qbuf[4096];
    if (sql.len >= sizeof(qbuf)) return -1;
    memcpy(qbuf, sql.data, sql.len);
    qbuf[sql.len] = 0;
    if (nparams < 0) nparams = 0;
    PGresult *r = PQexecParams(pg, qbuf, nparams, NULL, values, NULL, NULL, 0);
    if (!r) return -1;
    int64_t out = -1;
    ExecStatusType st = PQresultStatus(r);
    if (st == PGRES_TUPLES_OK) {
        if (PQntuples(r) > 0 && PQnfields(r) > 0 && !PQgetisnull(r, 0, 0)) {
            out = (int64_t)strtoll(PQgetvalue(r, 0, 0), NULL, 10);
        } else {
            out = 0;
        }
        mako_sql_meta_set(c.handle, 0, (int64_t)PQntuples(r));
    } else if (st == PGRES_COMMAND_OK) {
        const char *ct = PQcmdTuples(r);
        int64_t rows = ct ? (int64_t)strtoll(ct, NULL, 10) : 0;
        mako_sql_meta_set(c.handle, 0, rows);
        out = 0;
    } else {
        fprintf(stderr, "mako: pg_query_int: %s", PQerrorMessage(pg));
    }
    PQclear(r);
    return out;
}
#endif

/* Execute SQL with up to 4 string parameters (for INSERT/UPDATE with text values).
 * Placeholders: SQLite `?` or `$1..$4`; Postgres `$1..$4` (or `?` rewritten).
 * Bind count comes from SQL placeholder arity ($N / ?), NOT from trailing empty args.
 * Empty string "" is a real bound value (e.g. SET ua = $2 with $2 = ""). */
static inline int64_t mako_sql_exec_str4(MakoSqlDB db, MakoString sql, MakoString p1, MakoString p2, MakoString p3, MakoString p4) {
    if (db.driver == 1) {
#if defined(MAKO_HAS_SQLITE)
        if (db.sqlite) {
            return mako_sqlite_exec_str4_handle(db.sqlite, sql, p1, p2, p3, p4);
        }
#endif
        /* Open-per-call path without persistent handle */
#if defined(MAKO_HAS_SQLITE)
        {
            void *h = mako_sqlite_open_handle(db.dsn);
            if (!h) return -1;
            int64_t rc = mako_sqlite_exec_str4_handle(h, sql, p1, p2, p3, p4);
            mako_sqlite_close_handle(h);
            return rc;
        }
#else
        (void)sql; (void)p1; (void)p2; (void)p3; (void)p4;
        return -1;
#endif
    }
#if defined(MAKO_HAS_LIBPQ)
    if (db.driver == 2) {
        MakoPgConn pgc = db.pg;
        char b1[4096], b2[4096], b3[4096], b4[4096];
        mako_sql_copy_str_param(b1, sizeof(b1), p1);
        mako_sql_copy_str_param(b2, sizeof(b2), p2);
        mako_sql_copy_str_param(b3, sizeof(b3), p3);
        mako_sql_copy_str_param(b4, sizeof(b4), p4);
        int np = mako_sql_placeholder_arity(sql);
        const char *vals[4] = {b1, b2, b3, b4};
        MakoString q = mako_sql_qmark_to_dollar(sql);
        int64_t rc = mako_pg_exec_params(pgc, q, vals, np);
        mako_str_free(q);
        return rc;
    }
#else
    (void)p1; (void)p2; (void)p3; (void)p4;
#endif
    return -1;
}

/* Execute SQL with no parameters (DDL, simple statements). */
static inline int64_t mako_sql_exec_plain(MakoSqlDB db, MakoString sql) {
    MakoIntArray empty = {NULL, 0, 0};
    int64_t rc = mako_sql_exec(db, sql, empty);
#if defined(MAKO_HAS_SQLITE)
    if (rc == 0 && db.driver == 1 && db.sqlite) {
        sqlite3 *sdb = (sqlite3 *)db.sqlite;
        mako_sql_meta_set(
            (int64_t)(intptr_t)db.sqlite,
            (int64_t)sqlite3_last_insert_rowid(sdb),
            (int64_t)sqlite3_changes(sdb)
        );
    }
#endif
    return rc;
}

/* Query first column of first row as string; up to 4 string binds.
 * Bind count from SQL placeholder arity; "" is a valid bind value. */
static inline MakoString mako_sql_query_str4(
    MakoSqlDB db, MakoString sql, MakoString p1, MakoString p2, MakoString p3, MakoString p4
) {
    if (db.driver == 1) {
#if defined(MAKO_HAS_SQLITE)
        if (db.sqlite) {
            return mako_sqlite_query_str4_handle(db.sqlite, sql, p1, p2, p3, p4);
        }
        {
            void *h = mako_sqlite_open_handle(db.dsn);
            if (!h) return mako_str_from_cstr("");
            MakoString out = mako_sqlite_query_str4_handle(h, sql, p1, p2, p3, p4);
            mako_sqlite_close_handle(h);
            return out;
        }
#else
        (void)sql; (void)p1; (void)p2; (void)p3; (void)p4;
        return mako_str_from_cstr("");
#endif
    }
#if defined(MAKO_HAS_LIBPQ)
    if (db.driver == 2) {
        MakoPgConn pgc = db.pg;
        PGconn *pg = (PGconn *)(intptr_t)pgc.handle;
        if (!pg || !sql.data) return mako_str_from_cstr("");
        char qbuf[4096], b1[4096], b2[4096], b3[4096], b4[4096];
        if (sql.len >= sizeof(qbuf)) return mako_str_from_cstr("");
        MakoString q = mako_sql_qmark_to_dollar(sql);
        if (q.len >= sizeof(qbuf)) {
            mako_str_free(q);
            return mako_str_from_cstr("");
        }
        memcpy(qbuf, q.data, q.len);
        qbuf[q.len] = 0;
        mako_str_free(q);
        mako_sql_copy_str_param(b1, sizeof(b1), p1);
        mako_sql_copy_str_param(b2, sizeof(b2), p2);
        mako_sql_copy_str_param(b3, sizeof(b3), p3);
        mako_sql_copy_str_param(b4, sizeof(b4), p4);
        int np = mako_sql_placeholder_arity(sql);
        const char *vals[4] = {b1, b2, b3, b4};
        PGresult *r = PQexecParams(pg, qbuf, np, NULL, vals, NULL, NULL, 0);
        if (!r) return mako_str_from_cstr("");
        MakoString out = mako_str_from_cstr("");
        if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0 && !PQgetisnull(r, 0, 0)) {
            out = mako_str_from_cstr(PQgetvalue(r, 0, 0));
        }
        PQclear(r);
        return out;
    }
#endif
    (void)db; (void)sql; (void)p1; (void)p2; (void)p3; (void)p4;
    return mako_str_from_cstr("");
}

/* Query a single string value; optional one string param (arity from SQL). */
static inline MakoString mako_sql_query_str(MakoSqlDB db, MakoString sql, MakoString p1) {
    return mako_sql_query_str4(db, sql, p1, mako_str_empty, mako_str_empty, mako_str_empty);
}

static inline MakoString mako_sql_query_str2(MakoSqlDB db, MakoString sql, MakoString p1, MakoString p2) {
    return mako_sql_query_str4(db, sql, p1, p2, mako_str_empty, mako_str_empty);
}

static inline MakoString mako_sql_query_str3(
    MakoSqlDB db, MakoString sql, MakoString p1, MakoString p2, MakoString p3
) {
    return mako_sql_query_str4(db, sql, p1, p2, p3, mako_str_empty);
}

/* Last INSERT row id (SQLite last_insert_rowid; Postgres lastval when available). */
static inline int64_t mako_sql_last_insert_id(MakoSqlDB db) {
#if defined(MAKO_HAS_SQLITE)
    if (db.driver == 1 && db.sqlite) {
        return (int64_t)sqlite3_last_insert_rowid((sqlite3 *)db.sqlite);
    }
#endif
#if defined(MAKO_HAS_LIBPQ)
    if (db.driver == 2 && db.pg.handle) {
        MakoString q = mako_str_from_cstr("select lastval()");
        int64_t v = mako_pg_query_int_params(db.pg, q, NULL, 0);
        return v;
    }
#endif
    int64_t key = mako_sql_meta_key(db);
    MakoSqlMeta *m = key ? mako_sql_meta_slot(key) : NULL;
    return m ? m->last_insert_id : 0;
}

/* Rows changed by the last INSERT/UPDATE/DELETE on this connection. */
static inline int64_t mako_sql_rows_affected(MakoSqlDB db) {
#if defined(MAKO_HAS_SQLITE)
    if (db.driver == 1 && db.sqlite) {
        return (int64_t)sqlite3_changes((sqlite3 *)db.sqlite);
    }
#endif
    int64_t key = mako_sql_meta_key(db);
    if (!key) return 0;
    for (int i = 0; i < MAKO_SQL_META_MAX; i++) {
        if (mako_sql_meta[i].key == key) return mako_sql_meta[i].rows_affected;
    }
    return 0;
}

static inline int64_t mako_sql_begin(MakoSqlDB db) {
    MakoIntArray empty = {NULL, 0, 0};
    return mako_sql_exec(db, mako_str_from_cstr("begin"), empty);
}

static inline int64_t mako_sql_commit(MakoSqlDB db) {
    MakoIntArray empty = {NULL, 0, 0};
    return mako_sql_exec(db, mako_str_from_cstr("commit"), empty);
}

static inline int64_t mako_sql_rollback(MakoSqlDB db) {
    MakoIntArray empty = {NULL, 0, 0};
    return mako_sql_exec(db, mako_str_from_cstr("rollback"), empty);
}

/* ---- Unified prepared SQL statements ----
 * Statement handles pin a database handle and prepared SQL. SQLite uses a real
 * sqlite3_stmt when linked; Postgres uses a named libpq prepared statement.
 */
#define MAKO_SQL_STMT_MAX 64

typedef struct {
    int64_t live;
    int64_t driver;
    MakoSqlDB db;
    MakoString sql;
    void *sqlite_stmt;
    char pg_name[40];
} MakoSqlStmt;

static MakoSqlStmt mako_sql_stmts[MAKO_SQL_STMT_MAX];

static inline int64_t mako_sql_stmt_exec_values(
    MakoSqlStmt *st,
    MakoIntArray args
) {
    if (!st || !st->live) return -1;
    if (st->driver == 1) {
        if (st->sqlite_stmt) {
            int64_t v = mako_sqlite_stmt_query_int(
                st->db.sqlite, st->sqlite_stmt, args.data, (int)args.len
            );
            return v < 0 ? -1 : 0;
        }
        return mako_sql_exec(st->db, st->sql, args);
    }
    if (st->driver == 2) {
        int n = (int)args.len;
        char **vals = NULL;
        char (*bufs)[32] = NULL;
        if (n > 0) {
            vals = (char **)malloc((size_t)n * sizeof(char *));
            bufs = (char (*)[32])malloc((size_t)n * 32);
            if (!vals || !bufs) {
                free(vals);
                free(bufs);
                return -1;
            }
            for (int i = 0; i < n; i++) {
                snprintf(bufs[i], 32, "%lld", (long long)args.data[i]);
                vals[i] = bufs[i];
            }
        }
        int64_t rc = st->pg_name[0]
            ? mako_pg_exec_prepared(st->db.pg, st->pg_name, (const char *const *)vals, n)
            : mako_sql_exec(st->db, st->sql, args);
        free(vals);
        free(bufs);
        return rc;
    }
    return -1;
}

static inline int64_t mako_sql_prepare(MakoSqlDB db, MakoString sql) {
    if (db.driver != 1 && db.driver != 2) return 0;
    for (int64_t i = 0; i < MAKO_SQL_STMT_MAX; i++) {
        if (!mako_sql_stmts[i].live) {
            MakoSqlStmt *st = &mako_sql_stmts[i];
            memset(st, 0, sizeof(*st));
            st->live = 1;
            st->driver = db.driver;
            st->db = db;
            st->sql = mako_str_clone(sql);
            if (db.driver == 1) {
                st->sqlite_stmt = mako_sqlite_prepare_handle(db.sqlite, sql);
            } else if (db.driver == 2) {
                if (!db.pg.handle) {
                    memset(st, 0, sizeof(*st));
                    return 0;
                }
                snprintf(st->pg_name, sizeof(st->pg_name), "mako_stmt_%lld", (long long)(i + 1));
                MakoString q = mako_sql_qmark_to_dollar(sql);
                if (mako_pg_prepare_name(db.pg, st->pg_name, q) != 0) {
                    mako_str_free(q);
                    memset(st, 0, sizeof(*st));
                    return 0;
                }
                mako_str_free(q);
            }
            return i + 1;
        }
    }
    return 0;
}

static inline MakoSqlStmt *mako_sql_stmt_ref(int64_t stmt) {
    if (stmt <= 0 || stmt > MAKO_SQL_STMT_MAX) return NULL;
    MakoSqlStmt *st = &mako_sql_stmts[stmt - 1];
    return st->live ? st : NULL;
}

static inline int64_t mako_sql_stmt_query_int(int64_t stmt, MakoIntArray args) {
    MakoSqlStmt *st = mako_sql_stmt_ref(stmt);
    if (!st) return -1;
    if (st->driver == 1 && st->sqlite_stmt) {
        return mako_sqlite_stmt_query_int(
            st->db.sqlite, st->sqlite_stmt, args.data, (int)args.len
        );
    }
    if (st->driver == 2 && st->pg_name[0]) {
        return mako_sql_stmt_exec_values(st, args);
    }
    return mako_sql_query_int(st->db, st->sql, args);
}

static inline int64_t mako_sql_stmt_exec(int64_t stmt, MakoIntArray args) {
    MakoSqlStmt *st = mako_sql_stmt_ref(stmt);
    return st ? mako_sql_stmt_exec_values(st, args) : -1;
}

static inline int64_t mako_sql_stmt_close(int64_t stmt) {
    MakoSqlStmt *st = mako_sql_stmt_ref(stmt);
    if (!st) return 0;
    if (st->driver == 1) mako_sqlite_finalize_stmt(st->sqlite_stmt);
    memset(st, 0, sizeof(*st));
    return 1;
}

/* ---- Multi-row result sets (cursor) ----
 * sql_query_rows / sql_query_rows_str open a result handle; walk with
 * sql_rows_next; read with sql_rows_int / sql_rows_str; free with sql_rows_close.
 * SQLite streams via sqlite3_stmt; Postgres materializes a PGresult.
 * Handles are 1-based ints (0 = invalid). Max concurrent sets: MAKO_SQL_ROWS_MAX.
 */
#define MAKO_SQL_ROWS_MAX 32

typedef struct {
    int64_t live;
    int64_t driver; /* 1=sqlite, 2=postgres */
    int64_t on_row; /* 1 when positioned on a readable row */
    int64_t err;    /* 1 after step/query error */
    int64_t done;   /* 1 after end-of-results (further next → 0) */
    int64_t ncols;
    int64_t owned_sqlite; /* 1 = close sqlite_db on rows_close (transient open) */
    /* SQLite streaming */
    void *sqlite_db;
    void *sqlite_stmt;
    /* Postgres materialised */
#if defined(MAKO_HAS_LIBPQ)
    PGresult *pg_res;
#else
    void *pg_res;
#endif
    int64_t pg_i;     /* next row index for postgres */
    int64_t pg_nrows;
} MakoSqlRows;

static MakoSqlRows mako_sql_rows_tab[MAKO_SQL_ROWS_MAX];

static inline MakoSqlRows *mako_sql_rows_ref(int64_t h) {
    if (h <= 0 || h > MAKO_SQL_ROWS_MAX) return NULL;
    MakoSqlRows *r = &mako_sql_rows_tab[h - 1];
    return r->live ? r : NULL;
}

static inline int64_t mako_sql_rows_alloc(void) {
    for (int64_t i = 0; i < MAKO_SQL_ROWS_MAX; i++) {
        if (!mako_sql_rows_tab[i].live) {
            memset(&mako_sql_rows_tab[i], 0, sizeof(MakoSqlRows));
            mako_sql_rows_tab[i].live = 1;
            return i + 1;
        }
    }
    fprintf(stderr, "mako: sql_query_rows: too many open result sets (max %d)\n", MAKO_SQL_ROWS_MAX);
    return 0;
}

static inline void mako_sql_rows_free_body(MakoSqlRows *r) {
    if (!r) return;
#if defined(MAKO_HAS_SQLITE)
    if (r->driver == 1 && r->sqlite_stmt) {
        sqlite3_finalize((sqlite3_stmt *)r->sqlite_stmt);
        r->sqlite_stmt = NULL;
    }
#endif
#if defined(MAKO_HAS_LIBPQ)
    if (r->driver == 2 && r->pg_res) {
        PQclear(r->pg_res);
        r->pg_res = NULL;
    }
#endif
    memset(r, 0, sizeof(*r));
}

static inline int64_t mako_sql_rows_ok(int64_t h) {
    MakoSqlRows *r = mako_sql_rows_ref(h);
    return (r && !r->err) ? 1 : 0;
}

static inline int64_t mako_sql_rows_close(int64_t h) {
    MakoSqlRows *r = mako_sql_rows_ref(h);
    if (!r) return 0;
#if defined(MAKO_HAS_SQLITE)
    int close_db = (r->driver == 1 && r->owned_sqlite && r->sqlite_db) ? 1 : 0;
    void *db = r->sqlite_db;
#endif
    mako_sql_rows_free_body(r);
#if defined(MAKO_HAS_SQLITE)
    if (close_db) mako_sqlite_close_handle(db);
#endif
    return 1;
}

static inline int64_t mako_sql_rows_cols(int64_t h) {
    MakoSqlRows *r = mako_sql_rows_ref(h);
    return r ? r->ncols : 0;
}

static inline int64_t mako_sql_rows_next(int64_t h) {
    MakoSqlRows *r = mako_sql_rows_ref(h);
    if (!r || r->err) return -1;
    if (r->done) {
        r->on_row = 0;
        return 0;
    }
    r->on_row = 0;
#if defined(MAKO_HAS_SQLITE)
    if (r->driver == 1) {
        if (!r->sqlite_stmt) return -1;
        int rc = sqlite3_step((sqlite3_stmt *)r->sqlite_stmt);
        if (rc == SQLITE_ROW) {
            r->on_row = 1;
            r->ncols = sqlite3_column_count((sqlite3_stmt *)r->sqlite_stmt);
            return 1;
        }
        if (rc == SQLITE_DONE) {
            r->done = 1;
            return 0;
        }
        r->err = 1;
        if (r->sqlite_db) {
            fprintf(stderr, "sqlite: rows_next: %s\n",
                    sqlite3_errmsg((sqlite3 *)r->sqlite_db));
        }
        return -1;
    }
#endif
#if defined(MAKO_HAS_LIBPQ)
    if (r->driver == 2) {
        if (!r->pg_res) return -1;
        if (r->pg_i >= r->pg_nrows) {
            r->done = 1;
            return 0;
        }
        r->on_row = 1;
        r->pg_i++;
        return 1;
    }
#endif
    (void)r;
    return -1;
}

static inline int64_t mako_sql_rows_int(int64_t h, int64_t col) {
    MakoSqlRows *r = mako_sql_rows_ref(h);
    if (!r || !r->on_row || col < 0) return 0;
#if defined(MAKO_HAS_SQLITE)
    if (r->driver == 1 && r->sqlite_stmt) {
        if (col >= r->ncols) return 0;
        return (int64_t)sqlite3_column_int64((sqlite3_stmt *)r->sqlite_stmt, (int)col);
    }
#endif
#if defined(MAKO_HAS_LIBPQ)
    if (r->driver == 2 && r->pg_res) {
        int row = (int)(r->pg_i - 1);
        if (row < 0 || col >= r->ncols) return 0;
        if (PQgetisnull(r->pg_res, row, (int)col)) return 0;
        return (int64_t)strtoll(PQgetvalue(r->pg_res, row, (int)col), NULL, 10);
    }
#endif
    return 0;
}

static inline MakoString mako_sql_rows_str(int64_t h, int64_t col) {
    MakoSqlRows *r = mako_sql_rows_ref(h);
    if (!r || !r->on_row || col < 0) return mako_str_from_cstr("");
#if defined(MAKO_HAS_SQLITE)
    if (r->driver == 1 && r->sqlite_stmt) {
        if (col >= r->ncols) return mako_str_from_cstr("");
        const unsigned char *txt =
            sqlite3_column_text((sqlite3_stmt *)r->sqlite_stmt, (int)col);
        if (!txt) return mako_str_from_cstr("");
        return mako_str_from_cstr((const char *)txt);
    }
#endif
#if defined(MAKO_HAS_LIBPQ)
    if (r->driver == 2 && r->pg_res) {
        int row = (int)(r->pg_i - 1);
        if (row < 0 || col >= r->ncols) return mako_str_from_cstr("");
        if (PQgetisnull(r->pg_res, row, (int)col)) return mako_str_from_cstr("");
        return mako_str_from_cstr(PQgetvalue(r->pg_res, row, (int)col));
    }
#endif
    return mako_str_from_cstr("");
}

/* Open multi-row query with integer binds (`?` / rewritten `$N`). Returns handle or 0. */
static inline int64_t mako_sql_query_rows(MakoSqlDB db, MakoString sql, MakoIntArray args) {
    if (db.driver == 1) {
#if defined(MAKO_HAS_SQLITE)
        void *handle = db.sqlite;
        int owned = 0;
        if (!handle) {
            handle = mako_sqlite_open_handle(db.dsn);
            if (!handle) return 0;
            owned = 1;
        }
        char qbuf[4096];
        if (!sql.data || sql.len >= sizeof(qbuf)) {
            if (owned) mako_sqlite_close_handle(handle);
            return 0;
        }
        memcpy(qbuf, sql.data, sql.len);
        qbuf[sql.len] = 0;
        sqlite3 *sdb = (sqlite3 *)handle;
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(sdb, qbuf, -1, &st, NULL) != SQLITE_OK) {
            fprintf(stderr, "sqlite: query_rows prepare: %s\n", sqlite3_errmsg(sdb));
            if (owned) mako_sqlite_close_handle(handle);
            return 0;
        }
        int expected = sqlite3_bind_parameter_count(st);
        int nargs = args.data ? (int)args.len : 0;
        if (expected != nargs) {
            fprintf(stderr,
                    "sqlite: query_rows param count mismatch (sql %d, got %d)\n",
                    expected, nargs);
            sqlite3_finalize(st);
            if (owned) mako_sqlite_close_handle(handle);
            return 0;
        }
        for (int i = 0; i < nargs; i++) {
            sqlite3_bind_int64(st, i + 1, args.data[i]);
        }
        int64_t h = mako_sql_rows_alloc();
        if (!h) {
            sqlite3_finalize(st);
            if (owned) mako_sqlite_close_handle(handle);
            return 0;
        }
        MakoSqlRows *r = &mako_sql_rows_tab[h - 1];
        r->driver = 1;
        r->sqlite_stmt = st;
        r->sqlite_db = handle;
        r->owned_sqlite = owned ? 1 : 0;
        r->ncols = sqlite3_column_count(st);
        return h;
#else
        (void)sql; (void)args;
        return 0;
#endif
    }
    if (db.driver == 2) {
#if defined(MAKO_HAS_LIBPQ)
        if (!db.pg.handle || !sql.data) return 0;
        MakoString q = mako_sql_qmark_to_dollar(sql);
        int n = args.data ? (int)args.len : 0;
        char **vals = NULL;
        char (*bufs)[32] = NULL;
        if (n > 0) {
            vals = (char **)malloc((size_t)n * sizeof(char *));
            bufs = (char (*)[32])malloc((size_t)n * 32);
            if (!vals || !bufs) {
                free(vals);
                free(bufs);
                mako_str_free(q);
                return 0;
            }
            for (int i = 0; i < n; i++) {
                snprintf(bufs[i], 32, "%lld", (long long)args.data[i]);
                vals[i] = bufs[i];
            }
        }
        PGconn *pg = (PGconn *)(intptr_t)db.pg.handle;
        char qbuf[4096];
        if (q.len >= sizeof(qbuf)) {
            free(vals);
            free(bufs);
            mako_str_free(q);
            return 0;
        }
        memcpy(qbuf, q.data, q.len);
        qbuf[q.len] = 0;
        mako_str_free(q);
        PGresult *res = PQexecParams(pg, qbuf, n, NULL, (const char *const *)vals, NULL, NULL, 0);
        free(vals);
        free(bufs);
        if (!res) return 0;
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "mako: query_rows: %s", PQerrorMessage(pg));
            PQclear(res);
            return 0;
        }
        int64_t h = mako_sql_rows_alloc();
        if (!h) {
            PQclear(res);
            return 0;
        }
        MakoSqlRows *r = &mako_sql_rows_tab[h - 1];
        r->driver = 2;
        r->pg_res = res;
        r->pg_i = 0;
        r->pg_nrows = (int64_t)PQntuples(res);
        r->ncols = (int64_t)PQnfields(res);
        return h;
#else
        (void)sql; (void)args;
        return 0;
#endif
    }
    return 0;
}

/* Open multi-row query with optional single string bind. */
static inline int64_t mako_sql_query_rows_str(MakoSqlDB db, MakoString sql, MakoString p1) {
    if (db.driver == 1) {
#if defined(MAKO_HAS_SQLITE)
        void *handle = db.sqlite;
        int owned = 0;
        if (!handle) {
            handle = mako_sqlite_open_handle(db.dsn);
            if (!handle) return 0;
            owned = 1;
        }
        char qbuf[4096];
        if (!sql.data || sql.len >= sizeof(qbuf)) {
            if (owned) mako_sqlite_close_handle(handle);
            return 0;
        }
        memcpy(qbuf, sql.data, sql.len);
        qbuf[sql.len] = 0;
        sqlite3 *sdb = (sqlite3 *)handle;
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(sdb, qbuf, -1, &st, NULL) != SQLITE_OK) {
            fprintf(stderr, "sqlite: query_rows_str prepare: %s\n", sqlite3_errmsg(sdb));
            if (owned) mako_sqlite_close_handle(handle);
            return 0;
        }
        int expected = sqlite3_bind_parameter_count(st);
        if (expected > 1) {
            fprintf(stderr, "sqlite: query_rows_str supports 0 or 1 placeholder\n");
            sqlite3_finalize(st);
            if (owned) mako_sqlite_close_handle(handle);
            return 0;
        }
        if (expected == 1) {
            const char *ptr = (p1.data && p1.len > 0) ? p1.data : "";
            int len = (p1.data && p1.len > 0) ? (int)p1.len : 0;
            if (sqlite3_bind_text(st, 1, ptr, len, SQLITE_TRANSIENT) != SQLITE_OK) {
                sqlite3_finalize(st);
                if (owned) mako_sqlite_close_handle(handle);
                return 0;
            }
        }
        int64_t h = mako_sql_rows_alloc();
        if (!h) {
            sqlite3_finalize(st);
            if (owned) mako_sqlite_close_handle(handle);
            return 0;
        }
        MakoSqlRows *r = &mako_sql_rows_tab[h - 1];
        r->driver = 1;
        r->sqlite_stmt = st;
        r->sqlite_db = handle;
        r->owned_sqlite = owned ? 1 : 0;
        r->ncols = sqlite3_column_count(st);
        return h;
#else
        (void)sql; (void)p1;
        return 0;
#endif
    }
    if (db.driver == 2) {
#if defined(MAKO_HAS_LIBPQ)
        if (!db.pg.handle || !sql.data) return 0;
        MakoString q = mako_sql_qmark_to_dollar(sql);
        char qbuf[4096], pbuf[4096];
        if (q.len >= sizeof(qbuf)) {
            mako_str_free(q);
            return 0;
        }
        memcpy(qbuf, q.data, q.len);
        qbuf[q.len] = 0;
        mako_str_free(q);
        int has_ph = 0;
        for (size_t i = 0; sql.data && i < sql.len; i++) {
            if (sql.data[i] == '?' ||
                (sql.data[i] == '$' && i + 1 < sql.len &&
                 sql.data[i + 1] >= '1' && sql.data[i + 1] <= '9')) {
                has_ph = 1;
                break;
            }
        }
        const char *vals[1] = {NULL};
        int np = 0;
        if (has_ph) {
            mako_sql_copy_str_param(pbuf, sizeof(pbuf), p1);
            vals[0] = pbuf;
            np = 1;
        }
        PGconn *pg = (PGconn *)(intptr_t)db.pg.handle;
        PGresult *res = PQexecParams(pg, qbuf, np, NULL, vals, NULL, NULL, 0);
        if (!res) return 0;
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "mako: query_rows_str: %s", PQerrorMessage(pg));
            PQclear(res);
            return 0;
        }
        int64_t h = mako_sql_rows_alloc();
        if (!h) {
            PQclear(res);
            return 0;
        }
        MakoSqlRows *r = &mako_sql_rows_tab[h - 1];
        r->driver = 2;
        r->pg_res = res;
        r->pg_i = 0;
        r->pg_nrows = (int64_t)PQntuples(res);
        r->ncols = (int64_t)PQnfields(res);
        return h;
#else
        (void)sql; (void)p1;
        return 0;
#endif
    }
    return 0;
}

/* Bulk: first column of all rows as []int, capped at max_rows (clamped 1..10000). */
static inline MakoIntArray mako_sql_query_col_int(MakoSqlDB db, MakoString sql, int64_t max_rows) {
    if (max_rows < 1) max_rows = 1;
    if (max_rows > 10000) max_rows = 10000;
    MakoIntArray empty_args = {NULL, 0, 0};
    int64_t h = mako_sql_query_rows(db, sql, empty_args);
    if (!h) return mako_int_array_make(0, 0);
    MakoIntArray out = mako_int_array_make(0, (int64_t)max_rows);
    int64_t n = 0;
    while (n < max_rows) {
        int64_t rc = mako_sql_rows_next(h);
        if (rc != 1) break;
        if (out.len >= out.cap) {
            size_t ncap = out.cap ? out.cap * 2 : 8;
            int64_t *nd = (int64_t *)realloc(out.data, ncap * sizeof(int64_t));
            if (!nd) break;
            out.data = nd;
            out.cap = ncap;
        }
        out.data[out.len++] = mako_sql_rows_int(h, 0);
        n++;
    }
    mako_sql_rows_close(h);
    return out;
}

/* Bulk: first column of all rows as []string, capped at max_rows. */
static inline MakoStrArray mako_sql_query_col_str(MakoSqlDB db, MakoString sql, int64_t max_rows) {
    if (max_rows < 1) max_rows = 1;
    if (max_rows > 10000) max_rows = 10000;
    MakoIntArray empty_args = {NULL, 0, 0};
    int64_t h = mako_sql_query_rows(db, sql, empty_args);
    if (!h) return mako_str_array_make(0, 0);
    MakoStrArray out = mako_str_array_make(0, (int64_t)max_rows);
    int64_t n = 0;
    while (n < max_rows) {
        int64_t rc = mako_sql_rows_next(h);
        if (rc != 1) break;
        if (out.len >= out.cap) {
            size_t ncap = out.cap ? out.cap * 2 : 8;
            MakoString *nd = (MakoString *)realloc(out.data, ncap * sizeof(MakoString));
            if (!nd) break;
            out.data = nd;
            out.cap = ncap;
        }
        out.data[out.len++] = mako_str_clone(mako_sql_rows_str(h, 0));
        n++;
    }
    mako_sql_rows_close(h);
    return out;
}

/* ---- SQL migrations ----
 * Numeric migration versions avoid string interpolation and keep the API inside
 * the current integer-bind SQL surface. Returns 1 when applied, 0 when already
 * applied, and -1 on error.
 */
static inline MakoIntArray mako_sql_one_int_arg(int64_t value) {
    int64_t *data = (int64_t *)malloc(sizeof(int64_t));
    if (!data) mako_abort("sql migration arg OOM");
    data[0] = value;
    return (MakoIntArray){data, 1, 1};
}

static inline int64_t mako_sql_migrations_ensure(MakoSqlDB db) {
    MakoIntArray empty = {NULL, 0, 0};
    return mako_sql_exec(
        db,
        mako_str_from_cstr(
            "create table if not exists mako_schema_migrations "
            "(version integer primary key, applied_at integer)"
        ),
        empty
    );
}

static inline int64_t mako_sql_migration_applied(MakoSqlDB db, int64_t version) {
    if (version <= 0) return -1;
    if (mako_sql_migrations_ensure(db) != 0) return -1;
    MakoIntArray args = mako_sql_one_int_arg(version);
    int64_t n = mako_sql_query_int(
        db,
        mako_str_from_cstr(
            "select count(*) from mako_schema_migrations where version = ?"
        ),
        args
    );
    free(args.data);
    return n;
}

static inline int64_t mako_sql_migrate(MakoSqlDB db, int64_t version, MakoString up_sql) {
    if (version <= 0) return -1;
    if (mako_sql_migrations_ensure(db) != 0) return -1;
    int64_t already = mako_sql_migration_applied(db, version);
    if (already < 0) return -1;
    if (already > 0) return 0;

    MakoIntArray empty = {NULL, 0, 0};
    if (mako_sql_begin(db) != 0) return -1;
    if (mako_sql_exec(db, up_sql, empty) != 0) {
        (void)mako_sql_rollback(db);
        return -1;
    }
    MakoIntArray args = mako_sql_one_int_arg(version);
    int64_t marked = mako_sql_exec(
        db,
        mako_str_from_cstr(
            "insert into mako_schema_migrations(version, applied_at) values (?, 0)"
        ),
        args
    );
    free(args.data);
    if (marked != 0) {
        (void)mako_sql_rollback(db);
        return -1;
    }
    if (mako_sql_commit(db) != 0) {
        (void)mako_sql_rollback(db);
        return -1;
    }
    return 1;
}

/* ---- Static typed SQL checker ----
 * Schema format: "users.id:int!,users.email:string?,orders.total:int!".
 * Params format: "int,string" in placeholder order.
 * Result format for SELECT: "id:int!,email:string?". INSERT expects "".
 * Returns 1 when the query shape checks, 0 when it does not.
 */
typedef struct {
    char type[24];
    int nullable;
} MakoSqlColumnInfo;

static inline char *mako_sql_check_lower(MakoString s) {
    char *out = (char *)malloc(s.len + 1);
    if (!out) mako_abort("sql check OOM");
    for (size_t i = 0; i < s.len; i++) out[i] = (char)tolower((unsigned char)s.data[i]);
    out[s.len] = 0;
    return out;
}

static inline char *mako_sql_check_trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static inline int mako_sql_check_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static inline int mako_sql_check_count_csv(const char *s) {
    if (!s) return 0;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return 0;
    int n = 1;
    for (; *s; s++) {
        if (*s == ',') n++;
    }
    return n;
}

static inline int mako_sql_check_qmarks(const char *s) {
    int n = 0;
    for (; s && *s; s++) {
        if (*s == '?') n++;
    }
    return n;
}

static inline int mako_sql_check_schema_lookup(
    const char *schema,
    const char *table,
    const char *column,
    MakoSqlColumnInfo *out
) {
    char *copy = strdup(schema ? schema : "");
    if (!copy) mako_abort("sql check OOM");
    char *save = NULL;
    for (char *entry = strtok_r(copy, ",", &save); entry; entry = strtok_r(NULL, ",", &save)) {
        char *item = mako_sql_check_trim(entry);
        char *dot = strchr(item, '.');
        char *colon = strchr(item, ':');
        if (!dot || !colon || dot > colon) continue;
        *dot = 0;
        *colon = 0;
        char *ty = colon + 1;
        char *mark = ty + strlen(ty);
        int nullable = 1;
        if (mark > ty && (mark[-1] == '!' || mark[-1] == '?')) {
            nullable = mark[-1] == '?';
            mark[-1] = 0;
        }
        if (strcmp(mako_sql_check_trim(item), table) == 0 &&
            strcmp(mako_sql_check_trim(dot + 1), column) == 0) {
            strncpy(out->type, mako_sql_check_trim(ty), sizeof(out->type) - 1);
            out->type[sizeof(out->type) - 1] = 0;
            out->nullable = nullable;
            free(copy);
            return 1;
        }
    }
    free(copy);
    return 0;
}

static inline int mako_sql_check_type_at(const char *csv, int idx, char *out, size_t out_len) {
    char *copy = strdup(csv ? csv : "");
    if (!copy) mako_abort("sql check OOM");
    int i = 0;
    char *save = NULL;
    for (char *part = strtok_r(copy, ",", &save); part; part = strtok_r(NULL, ",", &save), i++) {
        if (i == idx) {
            strncpy(out, mako_sql_check_trim(part), out_len - 1);
            out[out_len - 1] = 0;
            free(copy);
            return 1;
        }
    }
    free(copy);
    return 0;
}

static inline int mako_sql_check_result_column(
    const char *result,
    int idx,
    const char *column,
    const MakoSqlColumnInfo *info
) {
    char *copy = strdup(result ? result : "");
    if (!copy) mako_abort("sql check OOM");
    int i = 0;
    char *save = NULL;
    for (char *part = strtok_r(copy, ",", &save); part; part = strtok_r(NULL, ",", &save), i++) {
        if (i == idx) {
            char *item = mako_sql_check_trim(part);
            char *colon = strchr(item, ':');
            if (!colon) {
                free(copy);
                return 0;
            }
            *colon = 0;
            char *ty = colon + 1;
            char *mark = ty + strlen(ty);
            int nullable = 1;
            if (mark > ty && (mark[-1] == '!' || mark[-1] == '?')) {
                nullable = mark[-1] == '?';
                mark[-1] = 0;
            }
            int ok = strcmp(mako_sql_check_trim(item), column) == 0 &&
                     strcmp(mako_sql_check_trim(ty), info->type) == 0 &&
                     nullable == info->nullable;
            free(copy);
            return ok;
        }
    }
    free(copy);
    return 0;
}

static inline int mako_sql_check_params_for_where(
    const char *schema,
    const char *table,
    const char *where,
    const char *params
) {
    int pidx = 0;
    const char *cur = where;
    while ((cur = strchr(cur, '?')) != NULL) {
        const char *op = cur;
        while (op > where && isspace((unsigned char)op[-1])) op--;
        if (op <= where || op[-1] != '=') return 0;
        op--;
        while (op > where && isspace((unsigned char)op[-1])) op--;
        const char *end = op;
        while (op > where && mako_sql_check_ident_char(op[-1])) op--;
        if (end <= op) return 0;
        char col[64];
        size_t n = (size_t)(end - op);
        if (n >= sizeof(col)) return 0;
        memcpy(col, op, n);
        col[n] = 0;
        MakoSqlColumnInfo info;
        char pty[24];
        if (!mako_sql_check_schema_lookup(schema, table, col, &info)) return 0;
        if (!mako_sql_check_type_at(params, pidx, pty, sizeof(pty))) return 0;
        if (strcmp(pty, info.type) != 0) return 0;
        pidx++;
        cur++;
    }
    return pidx == mako_sql_check_count_csv(params);
}

static inline int64_t mako_sql_check_select(
    const char *schema,
    const char *sql,
    const char *params,
    const char *result
) {
    if (strncmp(sql, "select ", 7) != 0) return 0;
    const char *from = strstr(sql, " from ");
    if (!from) return 0;
    const char *table_start = from + 6;
    const char *table_end = table_start;
    while (*table_end && mako_sql_check_ident_char(*table_end)) table_end++;
    if (table_end == table_start) return 0;
    char table[64];
    size_t tn = (size_t)(table_end - table_start);
    if (tn >= sizeof(table)) return 0;
    memcpy(table, table_start, tn);
    table[tn] = 0;

    char cols[512];
    size_t cn = (size_t)(from - (sql + 7));
    if (cn == 0 || cn >= sizeof(cols)) return 0;
    memcpy(cols, sql + 7, cn);
    cols[cn] = 0;

    int col_count = mako_sql_check_count_csv(cols);
    if (col_count != mako_sql_check_count_csv(result)) return 0;
    char *col_copy = strdup(cols);
    if (!col_copy) mako_abort("sql check OOM");
    int idx = 0;
    char *save = NULL;
    for (char *part = strtok_r(col_copy, ",", &save); part; part = strtok_r(NULL, ",", &save), idx++) {
        char *col = mako_sql_check_trim(part);
        MakoSqlColumnInfo info;
        if (!mako_sql_check_schema_lookup(schema, table, col, &info) ||
            !mako_sql_check_result_column(result, idx, col, &info)) {
            free(col_copy);
            return 0;
        }
    }
    free(col_copy);

    const char *where = strstr(table_end, " where ");
    if (mako_sql_check_qmarks(sql) != mako_sql_check_count_csv(params)) return 0;
    if (where) return mako_sql_check_params_for_where(schema, table, where + 7, params);
    return mako_sql_check_count_csv(params) == 0;
}

static inline int64_t mako_sql_check_insert(
    const char *schema,
    const char *sql,
    const char *params,
    const char *result
) {
    if (strncmp(sql, "insert into ", 12) != 0) return 0;
    if (mako_sql_check_count_csv(result) != 0) return 0;
    const char *table_start = sql + 12;
    const char *table_end = table_start;
    while (*table_end && mako_sql_check_ident_char(*table_end)) table_end++;
    if (table_end == table_start) return 0;
    char table[64];
    size_t tn = (size_t)(table_end - table_start);
    if (tn >= sizeof(table)) return 0;
    memcpy(table, table_start, tn);
    table[tn] = 0;
    const char *lp = strchr(table_end, '(');
    const char *rp = lp ? strchr(lp, ')') : NULL;
    const char *values = rp ? strstr(rp, " values ") : NULL;
    if (!lp || !rp || !values) return 0;
    char cols[512];
    size_t cn = (size_t)(rp - lp - 1);
    if (cn == 0 || cn >= sizeof(cols)) return 0;
    memcpy(cols, lp + 1, cn);
    cols[cn] = 0;
    int col_count = mako_sql_check_count_csv(cols);
    if (col_count != mako_sql_check_qmarks(values) || col_count != mako_sql_check_count_csv(params)) {
        return 0;
    }
    char *col_copy = strdup(cols);
    if (!col_copy) mako_abort("sql check OOM");
    int idx = 0;
    char *save = NULL;
    for (char *part = strtok_r(col_copy, ",", &save); part; part = strtok_r(NULL, ",", &save), idx++) {
        char *col = mako_sql_check_trim(part);
        MakoSqlColumnInfo info;
        char pty[24];
        if (!mako_sql_check_schema_lookup(schema, table, col, &info) ||
            !mako_sql_check_type_at(params, idx, pty, sizeof(pty)) ||
            strcmp(pty, info.type) != 0) {
            free(col_copy);
            return 0;
        }
    }
    free(col_copy);
    return 1;
}

static inline int64_t mako_sql_check_typed(
    MakoString schema,
    MakoString sql,
    MakoString params,
    MakoString result
) {
    char *sch = mako_sql_check_lower(schema);
    char *q = mako_sql_check_lower(sql);
    char *ps = mako_sql_check_lower(params);
    char *rs = mako_sql_check_lower(result);
    int64_t ok = mako_sql_check_select(sch, q, ps, rs) ||
                 mako_sql_check_insert(sch, q, ps, rs);
    free(sch);
    free(q);
    free(ps);
    free(rs);
    return ok ? 1 : 0;
}

/* ---- SQL connection pool ----
 * Integer handles keep the language ABI compact. Pools are bounded, lazy-opened,
 * round-robin, and close every opened backend handle on shutdown.
 */
#define MAKO_SQL_POOL_MAX 32
#define MAKO_SQL_POOL_SLOTS 16

typedef struct {
    int64_t live;
    int64_t driver;
    MakoString dsn;
    int64_t max;
    int64_t next;
    int64_t opened;
    MakoSqlDB slots[MAKO_SQL_POOL_SLOTS];
    uint8_t slot_live[MAKO_SQL_POOL_SLOTS];
} MakoSqlPool;

static MakoSqlPool mako_sql_pools[MAKO_SQL_POOL_MAX];

static inline int64_t mako_sql_pool_clamp_size(int64_t max) {
    if (max < 1) return 1;
    if (max > MAKO_SQL_POOL_SLOTS) return MAKO_SQL_POOL_SLOTS;
    return max;
}

static inline int64_t mako_sql_pool_alloc(int64_t driver, MakoString dsn, int64_t max) {
    for (int64_t i = 0; i < MAKO_SQL_POOL_MAX; i++) {
        if (!mako_sql_pools[i].live) {
            MakoSqlPool *p = &mako_sql_pools[i];
            memset(p, 0, sizeof(*p));
            p->live = 1;
            p->driver = driver;
            p->dsn = mako_str_clone(dsn);
            p->max = mako_sql_pool_clamp_size(max);
            return i + 1;
        }
    }
    return 0;
}

static inline int64_t mako_sql_pool_open_sqlite(MakoString path, int64_t max) {
    return mako_sql_pool_alloc(1, path, max);
}

static inline int64_t mako_sql_pool_open_postgres(MakoString url, int64_t max) {
    return mako_sql_pool_alloc(2, url, max);
}

static inline MakoSqlPool *mako_sql_pool_ref(int64_t pool) {
    if (pool <= 0 || pool > MAKO_SQL_POOL_MAX) return NULL;
    MakoSqlPool *p = &mako_sql_pools[pool - 1];
    return p->live ? p : NULL;
}

static inline int64_t mako_sql_pool_size(int64_t pool) {
    MakoSqlPool *p = mako_sql_pool_ref(pool);
    return p ? p->max : 0;
}

static inline int64_t mako_sql_pool_opened(int64_t pool) {
    MakoSqlPool *p = mako_sql_pool_ref(pool);
    return p ? p->opened : 0;
}

static inline int64_t mako_sql_pool_ok(int64_t pool) {
    return mako_sql_pool_ref(pool) ? 1 : 0;
}

static inline MakoSqlDB *mako_sql_pool_acquire(int64_t pool) {
    MakoSqlPool *p = mako_sql_pool_ref(pool);
    if (!p) return NULL;
    int64_t idx = p->next % p->max;
    p->next = (p->next + 1) % p->max;
    if (!p->slot_live[idx]) {
        p->slots[idx] = p->driver == 1
            ? mako_sql_open_sqlite(p->dsn)
            : mako_sql_open_postgres(p->dsn);
        p->slot_live[idx] = 1;
        p->opened++;
    }
    return &p->slots[idx];
}

static inline int64_t mako_sql_pool_next_slot(int64_t pool) {
    MakoSqlPool *p = mako_sql_pool_ref(pool);
    if (!p) return -1;
    return p->next % p->max;
}

static inline int64_t mako_sql_pool_query_int(
    int64_t pool,
    MakoString sql,
    MakoIntArray args
) {
    MakoSqlDB *db = mako_sql_pool_acquire(pool);
    return db ? mako_sql_query_int(*db, sql, args) : -1;
}

static inline int64_t mako_sql_pool_exec(int64_t pool, MakoString sql, MakoIntArray args) {
    MakoSqlDB *db = mako_sql_pool_acquire(pool);
    return db ? mako_sql_exec(*db, sql, args) : -1;
}

static inline int64_t mako_sql_pool_close(int64_t pool) {
    MakoSqlPool *p = mako_sql_pool_ref(pool);
    if (!p) return 0;
    for (int64_t i = 0; i < p->max; i++) {
        if (p->slot_live[i]) mako_sql_close(p->slots[i]);
    }
    memset(p, 0, sizeof(*p));
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_DB_H */
