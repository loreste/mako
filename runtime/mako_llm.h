/* Mako LLM programming — OpenAI-compatible chat, SSE stream parse, tools, JSON extract.
 * Market gaps closed: no async coloring, mono-deadline timeouts, secret-safe keys,
 * stream/tool parse without a Python SDK, JSON-from-markdown extract, token estimate.
 * Default provider: xAI / SpaceXAI (api.x.ai) via XAI_API_KEY. */
#ifndef MAKO_LLM_H
#define MAKO_LLM_H

#include "mako_rt.h"
#include "mako_std.h"
#include <errno.h>
#include <ctype.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#if !defined(_WIN32)
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- JSON string helpers (LLM-safe escape already in mako_json_escape) ---- */

static inline MakoString mako_llm_json_escape(MakoString s) {
    return mako_json_escape(s);
}

/* "role":"user","content":"..."  (no braces) */
static inline MakoString mako_llm_message(MakoString role, MakoString content) {
    MakoString er = mako_json_escape(role);
    MakoString ec = mako_json_escape(content);
    size_t n = er.len + ec.len + 48;
    char *d = (char *)malloc(n);
    if (!d) {
        mako_str_free(er);
        mako_str_free(ec);
        return mako_str_from_cstr("");
    }
    int w = snprintf(
        d, n, "{\"role\":\"%s\",\"content\":\"%s\"}",
        er.data ? er.data : "", ec.data ? ec.data : ""
    );
    mako_str_free(er);
    mako_str_free(ec);
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

/* Append message object into a JSON array string (starts empty → "[]"). */
static inline MakoString mako_llm_messages_append(MakoString arr, MakoString msg_obj) {
    const char *a = arr.data ? arr.data : "[]";
    size_t al = arr.data ? arr.len : 2;
    if (al < 2) {
        a = "[]";
        al = 2;
    }
    const char *m = msg_obj.data ? msg_obj.data : "{}";
    size_t ml = msg_obj.data ? msg_obj.len : 2;
    /* empty array: [ + msg + ] */
    int empty = (al == 2 && a[0] == '[' && a[1] == ']');
    size_t n = al + ml + 4;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("[]");
    if (empty) {
        int w = snprintf(d, n, "[%.*s]", (int)ml, m);
        if (w < 0) {
            free(d);
            return mako_str_from_cstr("[]");
        }
        return (MakoString){d, (size_t)w};
    }
    /* insert before final ] */
    if (a[al - 1] != ']') {
        free(d);
        return mako_str_from_cstr("[]");
    }
    memcpy(d, a, al - 1);
    d[al - 1] = ',';
    memcpy(d + al, m, ml);
    d[al + ml] = ']';
    d[al + ml + 1] = 0;
    return (MakoString){d, al + ml + 1};
}

/* Build chat/completions body: model + messages array + optional stream flag. */
static inline MakoString mako_llm_chat_body(
    MakoString model, MakoString messages_json, int64_t stream
) {
    MakoString em = mako_json_escape(model);
    const char *msgs = messages_json.data ? messages_json.data : "[]";
    size_t ml = messages_json.data ? messages_json.len : 2;
    size_t n = em.len + ml + 80;
    char *d = (char *)malloc(n);
    if (!d) {
        mako_str_free(em);
        return mako_str_from_cstr("");
    }
    int w = snprintf(
        d, n,
        "{\"model\":\"%s\",\"messages\":%.*s,\"stream\":%s}",
        em.data ? em.data : "grok-4.5",
        (int)ml, msgs,
        stream ? "true" : "false"
    );
    mako_str_free(em);
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

/* system + user convenience body. */
static inline MakoString mako_llm_system_user(
    MakoString model, MakoString system, MakoString user
) {
    MakoString s = mako_llm_message(mako_str_from_cstr("system"), system);
    MakoString u = mako_llm_message(mako_str_from_cstr("user"), user);
    MakoString arr = mako_str_from_cstr("[]");
    MakoString a1 = mako_llm_messages_append(arr, s);
    mako_str_free(arr);
    mako_str_free(s);
    MakoString a2 = mako_llm_messages_append(a1, u);
    mako_str_free(a1);
    mako_str_free(u);
    MakoString body = mako_llm_chat_body(model, a2, 0);
    mako_str_free(a2);
    return body;
}

/* Inject "tools":[...] into a chat body object (before final }). */
static inline MakoString mako_llm_body_with_tools(MakoString body, MakoString tools_json) {
    const char *b = body.data ? body.data : "{}";
    size_t bl = body.data ? body.len : 2;
    const char *t = tools_json.data ? tools_json.data : "[]";
    size_t tl = tools_json.data ? tools_json.len : 2;
    if (bl < 2 || b[bl - 1] != '}') return mako_str_from_cstr(b);
    size_t n = bl + tl + 16;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr(b);
    memcpy(d, b, bl - 1);
    int w = snprintf(d + bl - 1, n - (bl - 1), ",\"tools\":%.*s}", (int)tl, t);
    if (w < 0) {
        free(d);
        return mako_str_from_cstr(b);
    }
    return (MakoString){d, (size_t)(bl - 1 + (size_t)w)};
}

/* Rough token estimate: ~4 chars/token (market-standard heuristic for budgeting). */
static inline int64_t mako_llm_estimate_tokens(MakoString text) {
    size_t n = text.data ? text.len : 0;
    if (n == 0) return 0;
    return (int64_t)((n + 3) / 4);
}

/* Exponential backoff delay for retries (attempt 0 → base_ms). */
static inline int64_t mako_llm_retry_delay_ms(int64_t attempt, int64_t base_ms, int64_t max_ms) {
    if (base_ms < 1) base_ms = 100;
    if (max_ms < base_ms) max_ms = base_ms;
    if (attempt < 0) attempt = 0;
    int64_t d = base_ms;
    while (attempt > 0 && d < max_ms) {
        if (d > max_ms / 2) {
            d = max_ms;
            break;
        }
        d *= 2;
        attempt--;
    }
    return d > max_ms ? max_ms : d;
}

/* Redact API key for logs: show first 4 + "…" + last 2. */
static inline MakoString mako_llm_redact_key(MakoString key) {
    if (!key.data || key.len < 8) return mako_str_from_cstr("***");
    char out[32];
    snprintf(
        out, sizeof(out), "%c%c%c%c…%c%c",
        key.data[0], key.data[1], key.data[2], key.data[3],
        key.data[key.len - 2], key.data[key.len - 1]
    );
    return mako_str_from_cstr(out);
}

/* ---- Response field extractors (OpenAI-compatible) ---- */

/* First choice message content: choices[0].message.content */
static inline MakoString mako_llm_content(MakoString response_json) {
    /* Prefer nested path: find "content":" after "message" */
    const char *src = response_json.data ? response_json.data : "";
    const char *msg = strstr(src, "\"message\"");
    const char *scan = msg ? msg : src;
    MakoString key = mako_str_from_cstr("content");
    MakoString out = mako_json_get_string(
        (MakoString){(char *)(uintptr_t)scan, strlen(scan)}, key
    );
    /* Unescape common sequences in place */
    if (!out.data) return mako_str_from_cstr("");
    char *d = out.data;
    size_t j = 0;
    for (size_t i = 0; i < out.len; i++) {
        if (d[i] == '\\' && i + 1 < out.len) {
            char n = d[i + 1];
            if (n == 'n') {
                d[j++] = '\n';
                i++;
            } else if (n == 't') {
                d[j++] = '\t';
                i++;
            } else if (n == '"') {
                d[j++] = '"';
                i++;
            } else if (n == '\\') {
                d[j++] = '\\';
                i++;
            } else {
                d[j++] = d[i];
            }
        } else {
            d[j++] = d[i];
        }
    }
    d[j] = 0;
    out.len = j;
    return out;
}

static inline MakoString mako_llm_finish_reason(MakoString response_json) {
    return mako_json_get_string(response_json, mako_str_from_cstr("finish_reason"));
}

static inline int64_t mako_llm_usage_prompt_tokens(MakoString response_json) {
    const char *src = response_json.data ? response_json.data : "";
    const char *u = strstr(src, "\"usage\"");
    if (!u) return -1;
    return mako_json_get_int(
        (MakoString){(char *)(uintptr_t)u, strlen(u)},
        mako_str_from_cstr("prompt_tokens")
    );
}

static inline int64_t mako_llm_usage_completion_tokens(MakoString response_json) {
    const char *src = response_json.data ? response_json.data : "";
    const char *u = strstr(src, "\"usage\"");
    if (!u) return -1;
    return mako_json_get_int(
        (MakoString){(char *)(uintptr_t)u, strlen(u)},
        mako_str_from_cstr("completion_tokens")
    );
}

static inline int64_t mako_llm_usage_total_tokens(MakoString response_json) {
    const char *src = response_json.data ? response_json.data : "";
    const char *u = strstr(src, "\"usage\"");
    if (!u) return -1;
    return mako_json_get_int(
        (MakoString){(char *)(uintptr_t)u, strlen(u)},
        mako_str_from_cstr("total_tokens")
    );
}

/* Tool calls: count "\"arguments\":" after "tool_calls" (stable across providers). */
static inline int64_t mako_llm_tool_call_count(MakoString response_json) {
    const char *src = response_json.data ? response_json.data : "";
    const char *tc = strstr(src, "\"tool_calls\"");
    if (!tc) return 0;
    int64_t n = 0;
    const char *p = tc;
    while ((p = strstr(p, "\"arguments\"")) != NULL) {
        n++;
        p += 12;
    }
    return n;
}

/* Find start of i-th tool function object after tool_calls. */
static inline const char *mako_llm_tool_fn_at(const char *src, int64_t idx) {
    const char *tc = strstr(src, "\"tool_calls\"");
    if (!tc || idx < 0) return NULL;
    const char *p = tc;
    int64_t n = 0;
    while ((p = strstr(p, "\"arguments\"")) != NULL) {
        if (n == idx) {
            /* Walk back to nearest "function" before this arguments. */
            const char *fn = p;
            while (fn > tc && strncmp(fn, "\"function\"", 10) != 0) fn--;
            if (strncmp(fn, "\"function\"", 10) == 0) return fn;
            return p;
        }
        n++;
        p += 12;
    }
    return NULL;
}

/* Extract i-th tool function name (0-based). */
static inline MakoString mako_llm_tool_call_name(MakoString response_json, int64_t idx) {
    const char *src = response_json.data ? response_json.data : "";
    const char *p = mako_llm_tool_fn_at(src, idx);
    if (!p) return mako_str_from_cstr("");
    return mako_json_get_string(
        (MakoString){(char *)(uintptr_t)p, strlen(p)},
        mako_str_from_cstr("name")
    );
}

/* Extract i-th tool arguments JSON string (often a stringified object). */
static inline MakoString mako_llm_tool_call_args(MakoString response_json, int64_t idx) {
    const char *src = response_json.data ? response_json.data : "";
    const char *p = mako_llm_tool_fn_at(src, idx);
    if (!p) return mako_str_from_cstr("");
    return mako_json_get_string(
        (MakoString){(char *)(uintptr_t)p, strlen(p)},
        mako_str_from_cstr("arguments")
    );
}

/* ---- SSE stream parsing (token deltas) ---- */

/* If line is "data: {...}", return the JSON payload; "data: [DONE]" → empty. */
static inline MakoString mako_llm_sse_data(MakoString line) {
    const char *s = line.data ? line.data : "";
    size_t n = line.data ? line.len : 0;
    while (n && (s[0] == ' ' || s[0] == '\r')) {
        s++;
        n--;
    }
    if (n >= 5 && strncmp(s, "data:", 5) == 0) {
        s += 5;
        n -= 5;
        while (n && s[0] == ' ') {
            s++;
            n--;
        }
        if (n >= 6 && strncmp(s, "[DONE]", 6) == 0) return mako_str_from_cstr("");
        char *d = (char *)malloc(n + 1);
        if (!d) return mako_str_from_cstr("");
        memcpy(d, s, n);
        d[n] = 0;
        /* strip trailing \r */
        while (n && (d[n - 1] == '\r' || d[n - 1] == '\n')) d[--n] = 0;
        return (MakoString){d, n};
    }
    return mako_str_from_cstr("");
}

/* From a stream chunk JSON, extract choices[0].delta.content */
static inline MakoString mako_llm_sse_delta(MakoString chunk_json) {
    const char *src = chunk_json.data ? chunk_json.data : "";
    const char *delta = strstr(src, "\"delta\"");
    if (!delta) return mako_str_from_cstr("");
    return mako_json_get_string(
        (MakoString){(char *)(uintptr_t)delta, strlen(delta)},
        mako_str_from_cstr("content")
    );
}

/* Accumulate stream: append delta to acc. Returns new accumulator. */
static inline MakoString mako_llm_stream_append(MakoString acc, MakoString delta) {
    size_t al = acc.data ? acc.len : 0;
    size_t dl = delta.data ? delta.len : 0;
    if (dl == 0) {
        if (acc.data) {
            char *c = (char *)malloc(al + 1);
            if (!c) return mako_str_from_cstr("");
            memcpy(c, acc.data, al);
            c[al] = 0;
            return (MakoString){c, al};
        }
        return mako_str_from_cstr("");
    }
    char *d = (char *)malloc(al + dl + 1);
    if (!d) return mako_str_from_cstr("");
    if (al && acc.data) memcpy(d, acc.data, al);
    memcpy(d + al, delta.data, dl);
    d[al + dl] = 0;
    return (MakoString){d, al + dl};
}

/* Extract JSON object/array from markdown fences or first {...}/[...]. */
static inline MakoString mako_llm_json_extract(MakoString text) {
    const char *s = text.data ? text.data : "";
    size_t n = text.data ? text.len : 0;
    /* ```json ... ``` or ``` ... ``` */
    const char *fence = strstr(s, "```");
    if (fence) {
        const char *start = fence + 3;
        while (*start == 'j' || *start == 's' || *start == 'o' || *start == 'n'
               || *start == ' ')
            start++;
        if (*start == '\n') start++;
        const char *end = strstr(start, "```");
        if (end && end > start) {
            size_t len = (size_t)(end - start);
            char *d = (char *)malloc(len + 1);
            if (!d) return mako_str_from_cstr("");
            memcpy(d, start, len);
            d[len] = 0;
            return (MakoString){d, len};
        }
    }
    /* First { ... } balanced-ish (string-aware). */
    const char *b = strchr(s, '{');
    if (!b) b = strchr(s, '[');
    if (!b) return mako_str_from_cstr("");
    char open = *b;
    char close = (open == '{') ? '}' : ']';
    int depth = 0;
    int in_str = 0;
    const char *p = b;
    for (; *p; p++) {
        char c = *p;
        if (in_str) {
            if (c == '\\' && p[1]) {
                p++;
                continue;
            }
            if (c == '"') in_str = 0;
            continue;
        }
        if (c == '"') {
            in_str = 1;
            continue;
        }
        if (c == open) depth++;
        else if (c == close) {
            depth--;
            if (depth == 0) {
                size_t len = (size_t)(p - b + 1);
                char *d = (char *)malloc(len + 1);
                if (!d) return mako_str_from_cstr("");
                memcpy(d, b, len);
                d[len] = 0;
                return (MakoString){d, len};
            }
        }
    }
    (void)n;
    return mako_str_from_cstr("");
}

/* Default config from env: XAI_API_KEY or OPENAI_API_KEY; base api.x.ai */
static inline MakoString mako_llm_api_key(void) {
    const char *k = getenv("XAI_API_KEY");
    if (!k || !k[0]) k = getenv("OPENAI_API_KEY");
    if (!k || !k[0]) k = getenv("MAKO_LLM_API_KEY");
    return mako_str_from_cstr(k ? k : "");
}

static inline MakoString mako_llm_base_url(void) {
    const char *b = getenv("MAKO_LLM_BASE_URL");
    if (!b || !b[0]) b = getenv("OPENAI_BASE_URL");
    if (!b || !b[0]) b = "https://api.x.ai/v1";
    return mako_str_from_cstr(b);
}

static inline MakoString mako_llm_default_model(void) {
    const char *m = getenv("MAKO_LLM_MODEL");
    if (!m || !m[0]) m = "grok-4.5";
    return mako_str_from_cstr(m);
}

/* ---- HTTPS POST (OpenAI-compatible) ---- */

#if defined(MAKO_TLS_REAL) || defined(MAKO_HAS_OPENSSL)
#include <openssl/ssl.h>
#include <openssl/err.h>

/* Parse https://host[:port]/path into components. Returns 1 ok. */
static inline int mako_llm_parse_https_url(
    MakoString url, char *host, size_t host_cap, int *port, char *path, size_t path_cap
) {
    const char *u = url.data ? url.data : "";
    if (strncmp(u, "https://", 8) != 0) return 0;
    u += 8;
    const char *slash = strchr(u, '/');
    size_t hlen = slash ? (size_t)(slash - u) : strlen(u);
    if (hlen == 0 || hlen >= host_cap) return 0;
    memcpy(host, u, hlen);
    host[hlen] = 0;
    *port = 443;
    char *colon = strchr(host, ':');
    if (colon) {
        *port = atoi(colon + 1);
        *colon = 0;
    }
    if (slash) {
        size_t plen = strlen(slash);
        if (plen >= path_cap) plen = path_cap - 1;
        memcpy(path, slash, plen);
        path[plen] = 0;
    } else {
        snprintf(path, path_cap, "/");
    }
    return 1;
}

/* Last HTTP status from llm_https_post / stream (0 if unknown). */
static __thread int64_t mako_llm_tls_last_status = 0;

static inline int64_t mako_llm_last_status(void) {
    return mako_llm_tls_last_status;
}

/* POST JSON over HTTPS with Bearer token. timeout_ms uses SO_RCVTIMEO.
 * verify: 1 = default CA store, 0 = insecure (dev only).
 * Sets mako_llm_tls_last_status from the response status line. */
static inline MakoString mako_llm_https_post(
    MakoString url,
    MakoString api_key,
    MakoString json_body,
    int64_t timeout_ms,
    int64_t verify
) {
    mako_llm_tls_last_status = 0;
    char host[256], path[1024];
    int port = 443;
    if (!mako_llm_parse_https_url(url, host, sizeof(host), &port, path, sizeof(path))) {
        return mako_str_from_cstr("{\"error\":\"invalid_https_url\"}");
    }
    if (!api_key.data || api_key.len == 0) {
        return mako_str_from_cstr("{\"error\":\"missing_api_key\"}");
    }

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) return mako_str_from_cstr("{\"error\":\"ssl_ctx\"}");

    if (verify) {
        SSL_CTX_set_default_verify_paths(ctx);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", port);
    if (getaddrinfo(host, portbuf, &hints, &res) != 0 || !res) {
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("{\"error\":\"dns\"}");
    }

    int fd = -1;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (timeout_ms > 0) {
            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("{\"error\":\"connect\"}");
    }

    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host);
    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("{\"error\":\"tls_handshake\"}");
    }
    if (verify && SSL_get_verify_result(ssl) != X509_V_OK) {
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("{\"error\":\"tls_verify\"}");
    }

    size_t blen = json_body.data ? json_body.len : 0;
    /* Key as C string (reject embedded NUL). */
    char keybuf[512];
    if (api_key.len >= sizeof(keybuf)) {
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("{\"error\":\"key_too_long\"}");
    }
    for (size_t i = 0; i < api_key.len; i++) {
        if (api_key.data[i] == 0) {
            SSL_free(ssl);
            close(fd);
            SSL_CTX_free(ctx);
            return mako_str_from_cstr("{\"error\":\"key_embedded_nul\"}");
        }
    }
    memcpy(keybuf, api_key.data, api_key.len);
    keybuf[api_key.len] = 0;

    char hdr[1024];
    int hn = snprintf(
        hdr, sizeof(hdr),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Accept: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        path, host, keybuf, blen
    );
    /* Wipe key from stack-ish buffer after header built */
    memset(keybuf, 0, sizeof(keybuf));
    if (hn <= 0 || (size_t)hn >= sizeof(hdr)) {
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("{\"error\":\"header\"}");
    }
    if (SSL_write(ssl, hdr, hn) <= 0) {
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("{\"error\":\"write_header\"}");
    }
    if (blen > 0 && json_body.data) {
        size_t off = 0;
        while (off < blen) {
            int w = SSL_write(ssl, json_body.data + off, (int)(blen - off));
            if (w <= 0) break;
            off += (size_t)w;
        }
    }

    size_t cap = 8192, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("{\"error\":\"oom\"}");
    }
    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            if (cap > 32 * 1024 * 1024) break;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) break;
            buf = nb;
        }
        int r = SSL_read(ssl, buf + len, (int)(cap - len - 1));
        if (r <= 0) break;
        len += (size_t)r;
    }
    buf[len] = 0;
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);

    /* Capture status before stripping headers */
    mako_llm_tls_last_status = 0;
    if (len >= 12 && strncmp(buf, "HTTP/", 5) == 0) {
        const char *p = buf;
        while (p < buf + len && *p != ' ') p++;
        if (p < buf + len) {
            p++;
            int code = 0;
            for (int i = 0; i < 3 && p < buf + len && *p >= '0' && *p <= '9'; i++, p++)
                code = code * 10 + (*p - '0');
            mako_llm_tls_last_status = code;
        }
    }
    char *body = strstr(buf, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t bl = len - (size_t)(body - buf);
        char *out = (char *)malloc(bl + 1);
        if (!out) {
            free(buf);
            return mako_str_from_cstr("{\"error\":\"oom\"}");
        }
        memcpy(out, body, bl);
        out[bl] = 0;
        free(buf);
        return (MakoString){out, bl};
    }
    return (MakoString){buf, len};
}

/* Parse "HTTP/1.x NNN" from response header start; 0 if missing. */
static inline int64_t mako_llm_parse_status_line(const char *buf, size_t len) {
    if (!buf || len < 12) return 0;
    const char *p = buf;
    if (len < 5 || strncmp(p, "HTTP/", 5) != 0) return 0;
    while (p < buf + len && *p != ' ') p++;
    if (p >= buf + len) return 0;
    p++;
    int code = 0;
    for (int i = 0; i < 3 && p < buf + len && *p >= '0' && *p <= '9'; i++, p++)
        code = code * 10 + (*p - '0');
    return code;
}

/* Chat completions convenience: base_url + "/chat/completions". */
static inline MakoString mako_llm_chat(
    MakoString base_url,
    MakoString api_key,
    MakoString body,
    int64_t timeout_ms
) {
    const char *b = base_url.data ? base_url.data : "https://api.x.ai/v1";
    size_t bl = base_url.data ? base_url.len : strlen(b);
    while (bl > 0 && b[bl - 1] == '/') bl--;
    char url[1024];
    if (bl + 20 >= sizeof(url)) return mako_str_from_cstr("{\"error\":\"url\"}");
    memcpy(url, b, bl);
    memcpy(url + bl, "/chat/completions", 18);
    MakoString resp = mako_llm_https_post(
        mako_str_from_cstr(url), api_key, body, timeout_ms, 1
    );
    return resp;
}

/* One-shot: env key + default base + system/user. */
static inline MakoString mako_llm_ask(
    MakoString system, MakoString user, int64_t timeout_ms
) {
    MakoString key = mako_llm_api_key();
    MakoString base = mako_llm_base_url();
    MakoString model = mako_llm_default_model();
    MakoString body = mako_llm_system_user(model, system, user);
    MakoString resp = mako_llm_chat(base, key, body, timeout_ms);
    mako_str_free(key);
    mako_str_free(base);
    mako_str_free(model);
    mako_str_free(body);
    return resp;
}

/* Ensure body has "stream":true (clone + insert/replace). */
static inline MakoString mako_llm_body_force_stream(MakoString body) {
    if (!body.data || body.len < 2) return mako_str_from_cstr("{\"stream\":true}");
    if (strstr(body.data, "\"stream\":true")) {
        char *c = (char *)malloc(body.len + 1);
        if (!c) return mako_str_from_cstr("");
        memcpy(c, body.data, body.len);
        c[body.len] = 0;
        return (MakoString){c, body.len};
    }
    const char *p = strstr(body.data, "\"stream\":false");
    if (p) {
        size_t off = (size_t)(p - body.data);
        /* "stream":false → "stream":true  (shrink by 1) */
        size_t nlen = body.len - 1;
        char *d = (char *)malloc(nlen + 1);
        if (!d) return mako_str_from_cstr("");
        memcpy(d, body.data, off);
        memcpy(d + off, "\"stream\":true", 13);
        size_t rest = body.len - off - 14;
        if (rest) memcpy(d + off + 13, body.data + off + 14, rest);
        d[nlen] = 0;
        return (MakoString){d, nlen};
    }
    if (body.data[body.len - 1] != '}') {
        char *c = (char *)malloc(body.len + 1);
        if (!c) return mako_str_from_cstr("");
        memcpy(c, body.data, body.len);
        c[body.len] = 0;
        return (MakoString){c, body.len};
    }
    size_t n = body.len + 20;
    char *d = (char *)malloc(n);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, body.data, body.len - 1);
    int w = snprintf(d + body.len - 1, n - (body.len - 1), ",\"stream\":true}");
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, body.len - 1 + (size_t)w};
}

/* Streaming chat: SSE over HTTPS. Accumulates delta content; returns a
 * synthetic chat JSON so llm_content() works: {"choices":[{"message":{"content":"..."}}]}
 * On hard failure returns {"error":"..."}. */
static inline MakoString mako_llm_chat_stream(
    MakoString base_url,
    MakoString api_key,
    MakoString body,
    int64_t timeout_ms
) {
    mako_llm_tls_last_status = 0;
    MakoString sbody = mako_llm_body_force_stream(body);
    const char *b = base_url.data ? base_url.data : "https://api.x.ai/v1";
    size_t bl = base_url.data ? base_url.len : strlen(b);
    while (bl > 0 && b[bl - 1] == '/') bl--;
    char url[1024];
    if (bl + 20 >= sizeof(url)) {
        mako_str_free(sbody);
        return mako_str_from_cstr("{\"error\":\"url\"}");
    }
    memcpy(url, b, bl);
    memcpy(url + bl, "/chat/completions", 18);

    char host[256], path[1024];
    int port = 443;
    if (!mako_llm_parse_https_url(
            mako_str_from_cstr(url), host, sizeof(host), &port, path, sizeof(path)
        )) {
        mako_str_free(sbody);
        return mako_str_from_cstr("{\"error\":\"invalid_https_url\"}");
    }
    if (!api_key.data || api_key.len == 0) {
        mako_str_free(sbody);
        return mako_str_from_cstr("{\"error\":\"missing_api_key\"}");
    }

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        mako_str_free(sbody);
        return mako_str_from_cstr("{\"error\":\"ssl_ctx\"}");
    }
    SSL_CTX_set_default_verify_paths(ctx);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", port);
    if (getaddrinfo(host, portbuf, &hints, &res) != 0 || !res) {
        SSL_CTX_free(ctx);
        mako_str_free(sbody);
        return mako_str_from_cstr("{\"error\":\"dns\"}");
    }
    int fd = -1;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (timeout_ms > 0) {
            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        SSL_CTX_free(ctx);
        mako_str_free(sbody);
        return mako_str_from_cstr("{\"error\":\"connect\"}");
    }
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host);
    if (SSL_connect(ssl) <= 0 || SSL_get_verify_result(ssl) != X509_V_OK) {
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        mako_str_free(sbody);
        return mako_str_from_cstr("{\"error\":\"tls\"}");
    }

    char keybuf[512];
    if (api_key.len >= sizeof(keybuf)) {
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        mako_str_free(sbody);
        return mako_str_from_cstr("{\"error\":\"key_too_long\"}");
    }
    memcpy(keybuf, api_key.data, api_key.len);
    keybuf[api_key.len] = 0;
    size_t blen = sbody.data ? sbody.len : 0;
    char hdr[1024];
    int hn = snprintf(
        hdr, sizeof(hdr),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Accept: text/event-stream\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        path, host, keybuf, blen
    );
    memset(keybuf, 0, sizeof(keybuf));
    if (hn <= 0 || (size_t)hn >= sizeof(hdr) || SSL_write(ssl, hdr, hn) <= 0) {
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        mako_str_free(sbody);
        return mako_str_from_cstr("{\"error\":\"write_header\"}");
    }
    if (blen && sbody.data) {
        size_t off = 0;
        while (off < blen) {
            int w = SSL_write(ssl, sbody.data + off, (int)(blen - off));
            if (w <= 0) break;
            off += (size_t)w;
        }
    }
    mako_str_free(sbody);

    /* Read loop: line buffer → SSE data → accumulate deltas */
    char line[8192];
    size_t lpos = 0;
    int headers_done = 0;
    int hdr_nl = 0;
    char status_line[128];
    size_t slen = 0;
    MakoString acc = mako_str_from_cstr("");
    char rbuf[2048];
    for (;;) {
        int r = SSL_read(ssl, rbuf, (int)sizeof(rbuf));
        if (r <= 0) break;
        for (int i = 0; i < r; i++) {
            char c = rbuf[i];
            if (!headers_done) {
                if (c != '\n' && c != '\r' && slen + 1 < sizeof(status_line))
                    status_line[slen++] = c;
                if (c == '\n') {
                    hdr_nl++;
                    if (hdr_nl >= 2) {
                        headers_done = 1;
                        status_line[slen] = 0;
                        mako_llm_tls_last_status =
                            mako_llm_parse_status_line(status_line, slen);
                        lpos = 0;
                    }
                } else if (c != '\r') {
                    hdr_nl = 0;
                }
                continue;
            }
            if (c == '\n') {
                line[lpos] = 0;
                MakoString ln = {(char *)line, lpos};
                MakoString data = mako_llm_sse_data(ln);
                if (data.data && data.len) {
                    MakoString delta = mako_llm_sse_delta(data);
                    if (delta.data && delta.len) {
                        MakoString nacc = mako_llm_stream_append(acc, delta);
                        mako_str_free(acc);
                        acc = nacc;
                    }
                    mako_str_free(delta);
                }
                mako_str_free(data);
                lpos = 0;
            } else if (c != '\r') {
                if (lpos + 1 < sizeof(line)) line[lpos++] = c;
            }
        }
    }
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);

    if (mako_llm_tls_last_status >= 400) {
        mako_str_free(acc);
        char err[96];
        snprintf(err, sizeof(err), "{\"error\":\"http_%lld\"}",
                 (long long)mako_llm_tls_last_status);
        return mako_str_from_cstr(err);
    }
    /* Build synthetic response for llm_content */
    MakoString esc = mako_json_escape(acc);
    mako_str_free(acc);
    size_t n = esc.len + 80;
    char *out = (char *)malloc(n);
    if (!out) {
        mako_str_free(esc);
        return mako_str_from_cstr("{\"error\":\"oom\"}");
    }
    int w = snprintf(
        out, n,
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"%s\"},"
        "\"finish_reason\":\"stop\"}]}",
        esc.data ? esc.data : ""
    );
    mako_str_free(esc);
    if (w < 0) {
        free(out);
        return mako_str_from_cstr("{\"error\":\"format\"}");
    }
    return (MakoString){out, (size_t)w};
}

/* Embeddings body: {"model":"...","input":"..."} */
static inline MakoString mako_llm_embed_body(MakoString model, MakoString input) {
    MakoString em = mako_json_escape(model);
    MakoString ei = mako_json_escape(input);
    size_t n = em.len + ei.len + 48;
    char *d = (char *)malloc(n);
    if (!d) {
        mako_str_free(em);
        mako_str_free(ei);
        return mako_str_from_cstr("");
    }
    int w = snprintf(
        d, n, "{\"model\":\"%s\",\"input\":\"%s\"}",
        em.data ? em.data : "", ei.data ? ei.data : ""
    );
    mako_str_free(em);
    mako_str_free(ei);
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

static inline MakoString mako_llm_embeddings(
    MakoString base_url,
    MakoString api_key,
    MakoString body,
    int64_t timeout_ms
) {
    const char *b = base_url.data ? base_url.data : "https://api.x.ai/v1";
    size_t bl = base_url.data ? base_url.len : strlen(b);
    while (bl > 0 && b[bl - 1] == '/') bl--;
    char url[1024];
    if (bl + 16 >= sizeof(url)) return mako_str_from_cstr("{\"error\":\"url\"}");
    memcpy(url, b, bl);
    memcpy(url + bl, "/embeddings", 12);
    return mako_llm_https_post(
        mako_str_from_cstr(url), api_key, body, timeout_ms, 1
    );
}

/* One-shot embed with env config. */
static inline MakoString mako_llm_embed(MakoString input, int64_t timeout_ms) {
    MakoString key = mako_llm_api_key();
    MakoString base = mako_llm_base_url();
    MakoString model = mako_llm_default_model();
    /* Prefer embedding model env if set */
    const char *em = getenv("MAKO_LLM_EMBED_MODEL");
    MakoString m = em && em[0]
        ? mako_str_from_cstr(em)
        : model;
    MakoString body = mako_llm_embed_body(m, input);
    MakoString resp = mako_llm_embeddings(base, key, body, timeout_ms);
    mako_str_free(key);
    mako_str_free(base);
    mako_str_free(model);
    if (em && em[0]) mako_str_free(m);
    mako_str_free(body);
    return resp;
}

#else /* no OpenSSL */

#ifndef MAKO_LLM_LAST_STATUS_DEFINED
#define MAKO_LLM_LAST_STATUS_DEFINED
static __thread int64_t mako_llm_tls_last_status = 0;
static inline int64_t mako_llm_last_status(void) { return mako_llm_tls_last_status; }
#endif

static inline MakoString mako_llm_https_post(
    MakoString url, MakoString api_key, MakoString json_body,
    int64_t timeout_ms, int64_t verify
) {
    (void)url;
    (void)api_key;
    (void)json_body;
    (void)timeout_ms;
    (void)verify;
    return mako_str_from_cstr("{\"error\":\"openssl_not_linked\"}");
}

static inline MakoString mako_llm_chat(
    MakoString base_url, MakoString api_key, MakoString body, int64_t timeout_ms
) {
    (void)base_url;
    (void)api_key;
    (void)body;
    (void)timeout_ms;
    return mako_str_from_cstr("{\"error\":\"openssl_not_linked\"}");
}

static inline MakoString mako_llm_ask(
    MakoString system, MakoString user, int64_t timeout_ms
) {
    (void)system;
    (void)user;
    (void)timeout_ms;
    return mako_str_from_cstr("{\"error\":\"openssl_not_linked\"}");
}

static inline MakoString mako_llm_body_force_stream(MakoString body) {
    (void)body;
    return mako_str_from_cstr("{\"stream\":true}");
}

static inline MakoString mako_llm_chat_stream(
    MakoString base_url, MakoString api_key, MakoString body, int64_t timeout_ms
) {
    (void)base_url;
    (void)api_key;
    (void)body;
    (void)timeout_ms;
    return mako_str_from_cstr("{\"error\":\"openssl_not_linked\"}");
}

static inline MakoString mako_llm_embed_body(MakoString model, MakoString input) {
    MakoString em = mako_json_escape(model);
    MakoString ei = mako_json_escape(input);
    size_t n = em.len + ei.len + 48;
    char *d = (char *)malloc(n);
    if (!d) {
        mako_str_free(em);
        mako_str_free(ei);
        return mako_str_from_cstr("");
    }
    int w = snprintf(
        d, n, "{\"model\":\"%s\",\"input\":\"%s\"}",
        em.data ? em.data : "", ei.data ? ei.data : ""
    );
    mako_str_free(em);
    mako_str_free(ei);
    if (w < 0) {
        free(d);
        return mako_str_from_cstr("");
    }
    return (MakoString){d, (size_t)w};
}

static inline MakoString mako_llm_embeddings(
    MakoString base_url, MakoString api_key, MakoString body, int64_t timeout_ms
) {
    (void)base_url;
    (void)api_key;
    (void)body;
    (void)timeout_ms;
    return mako_str_from_cstr("{\"error\":\"openssl_not_linked\"}");
}

static inline MakoString mako_llm_embed(MakoString input, int64_t timeout_ms) {
    (void)input;
    (void)timeout_ms;
    return mako_str_from_cstr("{\"error\":\"openssl_not_linked\"}");
}

#endif /* TLS */

/* ---- Offline-safe error / retry / embedding parse (no OpenSSL needed) ---- */

static inline int64_t mako_llm_is_error(MakoString resp) {
    if (!resp.data || resp.len == 0) return 1;
    if (mako_llm_last_status() >= 400) return 1;
    /* {"error": ... } at top level */
    const char *s = resp.data;
    while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++;
    if (*s != '{') return 0;
    const char *e = strstr(s, "\"error\"");
    if (!e) return 0;
    /* crude: error key before first "choices" is a failure shape */
    const char *ch = strstr(s, "\"choices\"");
    if (ch && ch < e) return 0;
    return 1;
}

static inline MakoString mako_llm_error_message(MakoString resp) {
    if (!resp.data) return mako_str_from_cstr("empty");
    /* Prefer nested message, then error string */
    const char *s = resp.data;
    const char *err = strstr(s, "\"error\"");
    if (!err) {
        if (mako_llm_last_status() >= 400) {
            char b[48];
            snprintf(b, sizeof(b), "http_%lld", (long long)mako_llm_last_status());
            return mako_str_from_cstr(b);
        }
        return mako_str_from_cstr("");
    }
    /* "error":"text" */
    const char *q = strchr(err, ':');
    if (!q) return mako_str_from_cstr("error");
    q++;
    while (*q == ' ') q++;
    if (*q == '"') {
        q++;
        const char *end = q;
        while (*end && *end != '"') {
            if (*end == '\\' && end[1]) end += 2;
            else end++;
        }
        size_t n = (size_t)(end - q);
        char *d = (char *)malloc(n + 1);
        if (!d) return mako_str_from_cstr("");
        memcpy(d, q, n);
        d[n] = 0;
        return (MakoString){d, n};
    }
    if (*q == '{') {
        MakoString msg = mako_json_get_string(
            (MakoString){(char *)(uintptr_t)q, strlen(q)},
            mako_str_from_cstr("message")
        );
        if (msg.data && msg.len) return msg;
        mako_str_free(msg);
    }
    return mako_str_from_cstr("error");
}

/* Retry on 429, 5xx, or transport error objects. */
static inline int64_t mako_llm_should_retry(MakoString resp) {
    int64_t st = mako_llm_last_status();
    if (st == 429 || st == 408) return 1;
    if (st >= 500 && st <= 599) return 1;
    if (!resp.data) return 1;
    if (strstr(resp.data, "\"error\":\"connect\"") ||
        strstr(resp.data, "\"error\":\"dns\"") ||
        strstr(resp.data, "\"error\":\"tls") ||
        strstr(resp.data, "\"error\":\"http_429\"") ||
        strstr(resp.data, "\"error\":\"http_5"))
        return 1;
    if (strstr(resp.data, "rate_limit") || strstr(resp.data, "Rate limit")) return 1;
    return 0;
}

/* Chat with exponential backoff retries. max_attempts >= 1. */
static inline MakoString mako_llm_chat_retry(
    MakoString base_url,
    MakoString api_key,
    MakoString body,
    int64_t timeout_ms,
    int64_t max_attempts
) {
    if (max_attempts < 1) max_attempts = 1;
    if (max_attempts > 8) max_attempts = 8;
    MakoString last = mako_str_from_cstr("");
    for (int64_t attempt = 0; attempt < max_attempts; attempt++) {
        mako_str_free(last);
        last = mako_llm_chat(base_url, api_key, body, timeout_ms);
        if (!mako_llm_is_error(last) && !mako_llm_should_retry(last)) return last;
        if (attempt + 1 >= max_attempts) break;
        if (!mako_llm_should_retry(last) && mako_llm_is_error(last)) {
            /* non-retryable error (4xx other than 429) */
            int64_t st = mako_llm_last_status();
            if (st > 0 && st != 429 && st < 500) break;
            if (st == 0 && mako_llm_is_error(last) && !mako_llm_should_retry(last))
                break;
        }
        int64_t delay = mako_llm_retry_delay_ms(attempt, 200, 8000);
#if !defined(_WIN32)
        struct timespec ts;
        ts.tv_sec = delay / 1000;
        ts.tv_nsec = (delay % 1000) * 1000000L;
        nanosleep(&ts, NULL);
#else
        Sleep((DWORD)delay);
#endif
    }
    return last;
}

/* Embedding vector length (first data[0].embedding array). */
static inline int64_t mako_llm_embedding_dim(MakoString resp) {
    if (!resp.data) return 0;
    const char *emb = strstr(resp.data, "\"embedding\"");
    if (!emb) return 0;
    const char *br = strchr(emb, '[');
    if (!br) return 0;
    int depth = 0;
    int64_t count = 0;
    int in_num = 0;
    for (const char *p = br; *p; p++) {
        if (*p == '[') {
            depth++;
            continue;
        }
        if (*p == ']') {
            if (in_num) {
                count++;
                in_num = 0;
            }
            depth--;
            if (depth == 0) break;
            continue;
        }
        if (depth == 1) {
            if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.' ||
                *p == 'e' || *p == 'E') {
                in_num = 1;
            } else if (*p == ',') {
                if (in_num) {
                    count++;
                    in_num = 0;
                }
            } else if (*p == ' ' || *p == '\n' || *p == '\t') {
                /* skip */
            } else if (in_num) {
                count++;
                in_num = 0;
            }
        }
    }
    return count;
}

/* Extract first embedding as JSON array string "[0.1,0.2,...]". */
static inline MakoString mako_llm_embedding_json(MakoString resp) {
    if (!resp.data) return mako_str_from_cstr("[]");
    const char *emb = strstr(resp.data, "\"embedding\"");
    if (!emb) return mako_str_from_cstr("[]");
    const char *br = strchr(emb, '[');
    if (!br) return mako_str_from_cstr("[]");
    int depth = 0;
    const char *p = br;
    for (; *p; p++) {
        if (*p == '[') depth++;
        else if (*p == ']') {
            depth--;
            if (depth == 0) {
                p++;
                break;
            }
        }
    }
    size_t n = (size_t)(p - br);
    char *d = (char *)malloc(n + 1);
    if (!d) return mako_str_from_cstr("[]");
    memcpy(d, br, n);
    d[n] = 0;
    return (MakoString){d, n};
}

/* Available flag for tests. */
static inline int64_t mako_llm_https_available(void) {
#if defined(MAKO_TLS_REAL) || defined(MAKO_HAS_OPENSSL)
    return 1;
#else
    return 0;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_LLM_H */
