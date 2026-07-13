/* Real TLS via OpenSSL when available; otherwise clear Stub. */
#ifndef MAKO_TLS_H
#define MAKO_TLS_H

#include "mako_http.h"
#include <signal.h>

/* Auto-enable when the compiler is given OpenSSL includes (mako build probes). */
#if defined(MAKO_HAS_OPENSSL) || defined(MAKO_USE_OPENSSL)
#include <openssl/ssl.h>
#include <openssl/err.h>
#define MAKO_TLS_REAL 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MAKO_TLS_REAL

#include <openssl/evp.h>

/* Buffer AEAD (AES-128-GCM) — not TLS record AEAD / handshake.
 * seal: key=16B, nonce=12B → ciphertext||tag(16)
 * open: same key/nonce/aad → plaintext or empty on auth fail
 */
static inline MakoString mako_tls_aead_seal(
    MakoString key,
    MakoString nonce,
    MakoString plaintext,
    MakoString aad
) {
    if (key.len != 16 || nonce.len != 12) {
        fprintf(stderr, "mako tls_aead_seal: need 16-byte key and 12-byte nonce\n");
        return mako_str_from_cstr("");
    }
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return mako_str_from_cstr("");
    MakoString out = mako_str_from_cstr("");
    unsigned char tag[16];
    int len = 0, outlen = 0;
    size_t cap = plaintext.len + 16 + 32;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        EVP_CIPHER_CTX_free(ctx);
        return out;
    }
    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) != 1
        || EVP_EncryptInit_ex(ctx, NULL, NULL, (const unsigned char *)key.data,
                              (const unsigned char *)nonce.data) != 1) {
        free(buf);
        EVP_CIPHER_CTX_free(ctx);
        return out;
    }
    if (aad.len > 0) {
        if (EVP_EncryptUpdate(ctx, NULL, &len, (const unsigned char *)aad.data, (int)aad.len) != 1) {
            free(buf);
            EVP_CIPHER_CTX_free(ctx);
            return out;
        }
    }
    if (EVP_EncryptUpdate(ctx, (unsigned char *)buf, &len,
                          (const unsigned char *)plaintext.data, (int)plaintext.len) != 1) {
        free(buf);
        EVP_CIPHER_CTX_free(ctx);
        return out;
    }
    outlen = len;
    if (EVP_EncryptFinal_ex(ctx, (unsigned char *)buf + outlen, &len) != 1) {
        free(buf);
        EVP_CIPHER_CTX_free(ctx);
        return out;
    }
    outlen += len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1) {
        free(buf);
        EVP_CIPHER_CTX_free(ctx);
        return out;
    }
    EVP_CIPHER_CTX_free(ctx);
    memcpy(buf + outlen, tag, 16);
    outlen += 16;
    buf[outlen] = 0;
    out.data = buf;
    out.len = (size_t)outlen;
    return out;
}

static inline MakoString mako_tls_aead_open(
    MakoString key,
    MakoString nonce,
    MakoString sealed,
    MakoString aad
) {
    if (key.len != 16 || nonce.len != 12 || sealed.len < 16) {
        return mako_str_from_cstr("");
    }
    size_t ct_len = sealed.len - 16;
    const unsigned char *ct = (const unsigned char *)sealed.data;
    const unsigned char *tag = ct + ct_len;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return mako_str_from_cstr("");
    MakoString out = mako_str_from_cstr("");
    int len = 0, outlen = 0;
    char *buf = (char *)malloc(ct_len + 1);
    if (!buf) {
        EVP_CIPHER_CTX_free(ctx);
        return out;
    }
    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) != 1
        || EVP_DecryptInit_ex(ctx, NULL, NULL, (const unsigned char *)key.data,
                              (const unsigned char *)nonce.data) != 1) {
        free(buf);
        EVP_CIPHER_CTX_free(ctx);
        return out;
    }
    if (aad.len > 0) {
        if (EVP_DecryptUpdate(ctx, NULL, &len, (const unsigned char *)aad.data, (int)aad.len) != 1) {
            free(buf);
            EVP_CIPHER_CTX_free(ctx);
            return out;
        }
    }
    if (ct_len > 0) {
        if (EVP_DecryptUpdate(ctx, (unsigned char *)buf, &len, ct, (int)ct_len) != 1) {
            free(buf);
            EVP_CIPHER_CTX_free(ctx);
            return out;
        }
        outlen = len;
    }
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void *)tag) != 1) {
        free(buf);
        EVP_CIPHER_CTX_free(ctx);
        return out;
    }
    if (EVP_DecryptFinal_ex(ctx, (unsigned char *)buf + outlen, &len) != 1) {
        free(buf);
        EVP_CIPHER_CTX_free(ctx);
        return out; /* auth fail */
    }
    outlen += len;
    EVP_CIPHER_CTX_free(ctx);
    buf[outlen] = 0;
    out.data = buf;
    out.len = (size_t)outlen;
    return out;
}

/* TLS 1.3-like application_data record seed (RFC 8446 shape):
 * type=23, legacy_version=0x0303, length=|ciphertext||tag|.
 * AAD for AEAD = the 5-byte record header (TLS 1.3 record protection style).
 * Explicit-nonce seal/open below; seq variants XOR seq into IV (still no handshake). */
static inline MakoString mako_tls_record_appdata_seal(
    MakoString key, MakoString nonce, MakoString plaintext
) {
    size_t clen = plaintext.len + 16; /* ciphertext + GCM tag */
    if (clen > 0xffff) return mako_str_from_cstr("");
    unsigned char hdr[5] = {
        23, 0x03, 0x03,
        (unsigned char)((clen >> 8) & 0xff),
        (unsigned char)(clen & 0xff)
    };
    MakoString aad = {(char *)hdr, 5};
    MakoString sealed = mako_tls_aead_seal(key, nonce, plaintext, aad);
    if (!sealed.data || sealed.len != clen) {
        free(sealed.data);
        return mako_str_from_cstr("");
    }
    char *out = (char *)malloc(5 + sealed.len + 1);
    if (!out) {
        free(sealed.data);
        return mako_str_from_cstr("");
    }
    memcpy(out, hdr, 5);
    memcpy(out + 5, sealed.data, sealed.len);
    out[5 + sealed.len] = 0;
    size_t n = 5 + sealed.len;
    free(sealed.data);
    return (MakoString){out, n};
}

static inline MakoString mako_tls_record_appdata_open(
    MakoString key, MakoString nonce, MakoString record
) {
    if (!record.data || record.len < 5 + 16) return mako_str_from_cstr("");
    if ((unsigned char)record.data[0] != 23) return mako_str_from_cstr("");
    int64_t rlen = ((int64_t)(unsigned char)record.data[3] << 8)
                 | (int64_t)(unsigned char)record.data[4];
    if (rlen < 16 || (size_t)(5 + rlen) != record.len) return mako_str_from_cstr("");
    MakoString aad = mako_str_slice(record, 0, 5);
    MakoString sealed = mako_str_slice(record, 5, 5 + rlen);
    MakoString pt = mako_tls_aead_open(key, nonce, sealed, aad);
    free(aad.data);
    free(sealed.data);
    return pt;
}

/* Per-direction record sequence (TLS 1.3 style). Reset between tests/sessions. */
static uint64_t mako_tls_rec_write_seq = 0;
static uint64_t mako_tls_rec_read_seq = 0;

static inline int64_t mako_tls_record_seq_reset(void) {
    mako_tls_rec_write_seq = 0;
    mako_tls_rec_read_seq = 0;
    return 0;
}
static inline int64_t mako_tls_record_write_seq(void) {
    return (int64_t)mako_tls_rec_write_seq;
}
static inline int64_t mako_tls_record_read_seq(void) {
    return (int64_t)mako_tls_rec_read_seq;
}

/* TLS 1.3 nonce: 12-byte IV with 64-bit seq XORed into the last 8 bytes. */
static inline MakoString mako_tls_record_nonce_from_seq(MakoString iv, int64_t seq) {
    if (!iv.data || iv.len != 12 || seq < 0) return mako_str_from_cstr("");
    char *d = (char *)malloc(13);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, iv.data, 12);
    d[12] = 0;
    uint64_t s = (uint64_t)seq;
    for (int i = 0; i < 8; i++) {
        d[4 + i] ^= (char)((s >> (56 - 8 * i)) & 0xff);
    }
    return (MakoString){d, 12};
}

static inline MakoString mako_tls_record_appdata_seal_seq(
    MakoString key, MakoString iv, MakoString plaintext
) {
    MakoString nonce = mako_tls_record_nonce_from_seq(iv, (int64_t)mako_tls_rec_write_seq);
    if (!nonce.data || nonce.len != 12) {
        free(nonce.data);
        return mako_str_from_cstr("");
    }
    MakoString rec = mako_tls_record_appdata_seal(key, nonce, plaintext);
    free(nonce.data);
    if (rec.data && rec.len > 0) mako_tls_rec_write_seq++;
    return rec;
}

static inline MakoString mako_tls_record_appdata_open_seq(
    MakoString key, MakoString iv, MakoString record
) {
    MakoString nonce = mako_tls_record_nonce_from_seq(iv, (int64_t)mako_tls_rec_read_seq);
    if (!nonce.data || nonce.len != 12) {
        free(nonce.data);
        return mako_str_from_cstr("");
    }
    MakoString pt = mako_tls_record_appdata_open(key, nonce, record);
    free(nonce.data);
    if (pt.data && pt.len > 0) mako_tls_rec_read_seq++;
    return pt;
}

static inline SSL_CTX *mako_tls_make_ctx(const char *cert, const char *key) {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) return NULL;
    /* Secure defaults: TLS 1.2+, prefer server ciphers, no compression. */
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_cipher_list(
        ctx,
        "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
        "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
        "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305"
    );
    if (SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM) != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (!SSL_CTX_check_private_key(ctx)) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    return ctx;
}

/* ------------------------------------------------------------------------- */
/* Socket-style TLS server API.                                                */
/*                                                                             */
/* Unlike the fixed-response `tls_serve*` helpers, this lets you own the accept */
/* loop: bind/accept a plain TCP fd yourself (tcp_listen/tcp_accept), optionally */
/* read pre-TLS bytes (e.g. a Postgres SSLRequest), then upgrade that same fd to */
/* TLS and read/write decrypted data. Handles are opaque `void*` so the API      */
/* still compiles when TLS is not linked (the stubs below just fail).           */
/* ------------------------------------------------------------------------- */

typedef struct { SSL *ssl; int fd; } MakoTlsConn;

/* ALPN select for reverse-proxy TLS.
 *
 * IMPORTANT: Prefer http/1.1 until HTTP/2 multi-stream is solid. Browsers keep
 * one H2 connection open and send later navigations (e.g. / → /docs) as new
 * streams; a bug that reuses stream id 1 for those replies makes every click
 * after the first page appear to "stay on home". http/1.1 uses one request per
 * connection and routes correctly.
 *
 * When H2 multi-request is fixed, restore: "\x02h2\x08http/1.1"
 */
static inline int mako_tls_alpn_cb(SSL *ssl, const unsigned char **out,
                                   unsigned char *outlen, const unsigned char *in,
                                   unsigned int inlen, void *arg) {
    (void)ssl; (void)arg;
    /* Only http/1.1 — do NOT fall back to the client's first offer (often h2). */
    static const unsigned char pref[] = "\x08http/1.1";
    if (SSL_select_next_proto((unsigned char **)out, outlen, pref, sizeof(pref) - 1,
                              in, inlen) == OPENSSL_NPN_NEGOTIATED)
        return SSL_TLSEXT_ERR_OK;
    return SSL_TLSEXT_ERR_NOACK;
}

/* Create a server TLS context from cert + key PEM paths. NULL on failure. */
static inline void *mako_tls_server_new(MakoString cert, MakoString key) {
    char cbuf[1024], kbuf[1024];
    if (cert.len >= sizeof(cbuf) || key.len >= sizeof(kbuf)) return NULL;
    memcpy(cbuf, cert.data ? cert.data : "", cert.len); cbuf[cert.len] = 0;
    memcpy(kbuf, key.data ? key.data : "", key.len); kbuf[key.len] = 0;
    SSL_CTX *ctx = mako_tls_make_ctx(cbuf, kbuf);
    if (ctx) SSL_CTX_set_alpn_select_cb(ctx, mako_tls_alpn_cb, NULL);
    return (void *)ctx;
}

/* Like tls_server_new but requires TLS 1.3: clients offering only 1.2 or below
 * are rejected at handshake. NULL on failure. */
static inline void *mako_tls_server_new_tls13(MakoString cert, MakoString key) {
    SSL_CTX *ctx = (SSL_CTX *)mako_tls_server_new(cert, key);
    if (ctx) SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    return (void *)ctx;
}

/* Server TLS handshake on an already-accepted TCP fd (also used for STARTTLS-
 * style upgrades on the same socket). NULL on failure. */
static inline void *mako_tls_accept(void *ctx, int64_t fd) {
    if (!ctx || fd < 0) return NULL;
    SSL *ssl = SSL_new((SSL_CTX *)ctx);
    if (!ssl) return NULL;
    SSL_set_fd(ssl, (int)fd);
    if (SSL_accept(ssl) <= 0) { SSL_free(ssl); return NULL; }
    MakoTlsConn *c = (MakoTlsConn *)malloc(sizeof(MakoTlsConn));
    if (!c) { SSL_free(ssl); return NULL; }
    c->ssl = ssl; c->fd = (int)fd;
    return (void *)c;
}

/* Nonblocking TLS accept start: puts fd in nonblocking mode, creates SSL, and
 * kicks SSL_accept once. Handshake may be incomplete — drive with
 * tls_handshake_step / poll on want-read/want-write. Returns conn or NULL. */
static inline void *mako_tls_accept_start(void *ctx, int64_t fd) {
    if (!ctx || fd < 0) return NULL;
#if !defined(_WIN32)
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags >= 0) fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
#else
    u_long mode = 1UL;
    ioctlsocket((mako_sock_t)fd, FIONBIO, &mode);
#endif
    SSL *ssl = SSL_new((SSL_CTX *)ctx);
    if (!ssl) return NULL;
    SSL_set_fd(ssl, (int)fd);
    SSL_set_accept_state(ssl);
    int rc = SSL_accept(ssl);
    if (rc <= 0) {
        int err = SSL_get_error(ssl, rc);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            SSL_free(ssl);
            return NULL;
        }
    }
    MakoTlsConn *c = (MakoTlsConn *)malloc(sizeof(MakoTlsConn));
    if (!c) { SSL_free(ssl); return NULL; }
    c->ssl = ssl;
    c->fd = (int)fd;
    return (void *)c;
}

/* Drive a nonblocking handshake one step.
 * Returns: 1 = finished, 0 = want read, 2 = want write, -1 = error. */
static inline int64_t mako_tls_handshake_step(void *conn) {
    MakoTlsConn *c = (MakoTlsConn *)conn;
    if (!c || !c->ssl) return -1;
    if (SSL_is_init_finished(c->ssl)) return 1;
    int rc = SSL_do_handshake(c->ssl);
    if (rc == 1) return 1;
    int err = SSL_get_error(c->ssl, rc);
    if (err == SSL_ERROR_WANT_READ) return 0;
    if (err == SSL_ERROR_WANT_WRITE) return 2;
    return -1;
}

static inline int64_t mako_tls_is_init_finished(void *conn) {
    MakoTlsConn *c = (MakoTlsConn *)conn;
    if (!c || !c->ssl) return 0;
    return SSL_is_init_finished(c->ssl) ? 1 : 0;
}

/* 1 if last handshake/IO wants readable socket. */
static inline int64_t mako_tls_want_read(void *conn) {
    MakoTlsConn *c = (MakoTlsConn *)conn;
    if (!c || !c->ssl) return 0;
    if (SSL_is_init_finished(c->ssl)) return 0;
    /* Probe without advancing: OpenSSL tracks want via SSL_get_error after op.
     * We re-issue do_handshake which is idempotent for WANT_* states. */
    int rc = SSL_do_handshake(c->ssl);
    if (rc == 1) return 0;
    return SSL_get_error(c->ssl, rc) == SSL_ERROR_WANT_READ ? 1 : 0;
}

static inline int64_t mako_tls_want_write(void *conn) {
    MakoTlsConn *c = (MakoTlsConn *)conn;
    if (!c || !c->ssl) return 0;
    if (SSL_is_init_finished(c->ssl)) return 0;
    int rc = SSL_do_handshake(c->ssl);
    if (rc == 1) return 0;
    return SSL_get_error(c->ssl, rc) == SSL_ERROR_WANT_WRITE ? 1 : 0;
}

/* Underlying TCP fd for event-loop registration. */
static inline int64_t mako_tls_conn_fd(void *conn) {
    MakoTlsConn *c = (MakoTlsConn *)conn;
    if (!c) return -1;
    return (int64_t)c->fd;
}

/* Nonblocking TLS read: empty string on WANT_READ / close; check want flags. */
static inline MakoString mako_tls_read_nb(void *conn, int64_t max) {
    MakoTlsConn *c = (MakoTlsConn *)conn;
    if (!c || !c->ssl || max <= 0) return mako_str_from_cstr("");
    if (max > 65536) max = 65536;
    char *buf = (char *)malloc((size_t)max + 1);
    if (!buf) return mako_str_from_cstr("");
    int n = SSL_read(c->ssl, buf, (int)max);
    if (n <= 0) {
        free(buf);
        return mako_str_from_cstr("");
    }
    buf[n] = 0;
    return (MakoString){buf, (size_t)n};
}

static inline int64_t mako_tls_write_nb(void *conn, MakoString data) {
    MakoTlsConn *c = (MakoTlsConn *)conn;
    if (!c || !c->ssl || !data.data) return -1;
    int n = SSL_write(c->ssl, data.data, (int)data.len);
    if (n <= 0) {
        int err = SSL_get_error(c->ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
        return -1;
    }
    return (int64_t)n;
}

/* Read up to `max` bytes of decrypted data. Empty on close/error. */
static inline MakoString mako_tls_read(void *conn, int64_t max) {
    MakoTlsConn *c = (MakoTlsConn *)conn;
    if (!c || !c->ssl || max <= 0) return mako_str_from_cstr("");
    if (max > 65536) max = 65536;
    char *buf = (char *)malloc((size_t)max + 1);
    if (!buf) return mako_str_from_cstr("");
    int n = SSL_read(c->ssl, buf, (int)max);
    if (n <= 0) { free(buf); return mako_str_from_cstr(""); }
    buf[n] = 0;
    return (MakoString){buf, (size_t)n};
}

/* Write plaintext (encrypted on the wire). Bytes written, or -1. */
static inline int64_t mako_tls_write(void *conn, MakoString data) {
    MakoTlsConn *c = (MakoTlsConn *)conn;
    if (!c || !c->ssl || !data.data) return -1;
    return (int64_t)SSL_write(c->ssl, data.data, (int)data.len);
}

/* Negotiated ALPN protocol (e.g. "h2"), or "" if none. */
static inline MakoString mako_tls_conn_alpn(void *conn) {
    MakoTlsConn *c = (MakoTlsConn *)conn;
    if (!c || !c->ssl) return mako_str_from_cstr("");
    const unsigned char *p = NULL; unsigned int len = 0;
    SSL_get0_alpn_selected(c->ssl, &p, &len);
    if (!p || len == 0) return mako_str_from_cstr("");
    char *d = (char *)malloc(len + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, p, len); d[len] = 0;
    return (MakoString){d, len};
}

static inline int64_t mako_tls_conn_close(void *conn) {
    MakoTlsConn *c = (MakoTlsConn *)conn;
    if (!c) return -1;
    if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); }
    if (c->fd >= 0) close(c->fd);
    free(c);
    return 0;
}

static inline int64_t mako_tls_server_free(void *ctx) {
    if (ctx) SSL_CTX_free((SSL_CTX *)ctx);
    return 0;
}

static inline int64_t mako_tls_server_available(void) { return 1; }

static inline SSL_CTX *mako_tls_make_client_ctx(void) {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return NULL;
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_cipher_list(
        ctx,
        "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
        "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
        "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305"
    );
    /* Also allow TLS 1.3 default suites (OpenSSL 3). */
    SSL_CTX_set_ciphersuites(ctx,
        "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");
    return ctx;
}

/* tls_serve(port, cert_path, key_path, body) — HTTPS fixed-body server (blocks). */
static inline int64_t mako_tls_serve(
    int64_t port,
    MakoString cert_path,
    MakoString key_path,
    MakoString body
) {
    char cert[512], key[512];
    if (cert_path.len >= sizeof(cert) || key_path.len >= sizeof(key)) return 1;
    memcpy(cert, cert_path.data, cert_path.len);
    cert[cert_path.len] = 0;
    memcpy(key, key_path.data, key_path.len);
    key[key_path.len] = 0;

    SSL_CTX *ctx = mako_tls_make_ctx(cert, key);
    if (!ctx) {
        fprintf(stderr, "error: tls: failed to load cert/key\n");
        ERR_print_errors_fp(stderr);
        return 1;
    }
    int fd = mako_http_listen_fd(port);
    if (fd < 0) {
        SSL_CTX_free(ctx);
        return 1;
    }
    fprintf(stderr, "mako tls listening on :%lld (OpenSSL)\n", (long long)port);
    char req[8192];
    for (;;) {
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0) continue;
        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, cfd);
        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            close(cfd);
            continue;
        }
        int n = SSL_read(ssl, req, (int)sizeof(req) - 1);
        if (n < 0) n = 0;
        req[n] = 0;

        const char *reason = "OK";
        char hdr[320];
        int hn = snprintf(
            hdr,
            sizeof(hdr),
            "HTTP/1.1 200 %s\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n",
            reason,
            body.len
        );
        if (hn > 0) SSL_write(ssl, hdr, hn);
        if (body.len) SSL_write(ssl, body.data, (int)body.len);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(cfd);
    }
}

/* One-shot HTTPS: accept one connection, reply, return 0. For smoke tests. */
static inline int64_t mako_tls_serve_once(
    int64_t port,
    MakoString cert_path,
    MakoString key_path,
    MakoString body
) {
    /* Handshake-only clients close before HTTP; ignore SIGPIPE on write. */
    signal(SIGPIPE, SIG_IGN);
    char cert[512], key[512];
    if (cert_path.len >= sizeof(cert) || key_path.len >= sizeof(key)) return 1;
    memcpy(cert, cert_path.data, cert_path.len);
    cert[cert_path.len] = 0;
    memcpy(key, key_path.data, key_path.len);
    key[key_path.len] = 0;

    SSL_CTX *ctx = mako_tls_make_ctx(cert, key);
    if (!ctx) {
        fprintf(stderr, "error: tls: failed to load cert/key\n");
        ERR_print_errors_fp(stderr);
        return 1;
    }
    int fd = mako_http_listen_fd(port);
    if (fd < 0) {
        SSL_CTX_free(ctx);
        return 1;
    }
    fprintf(stderr, "mako tls_once on :%lld\n", (long long)port);
    int cfd = accept(fd, NULL, NULL);
    if (cfd < 0) {
        close(fd);
        SSL_CTX_free(ctx);
        return 1;
    }
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, cfd);
    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(cfd);
        close(fd);
        SSL_CTX_free(ctx);
        return 1;
    }
    char req[4096];
    int n = SSL_read(ssl, req, (int)sizeof(req) - 1);
    /* n<=0: peer closed after handshake (tls_handshake_ok) — no HTTP reply. */
    if (n > 0) {
        req[n] = 0;
        char hdr[320];
        int hn = snprintf(
            hdr,
            sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n",
            body.len
        );
        if (hn > 0) SSL_write(ssl, hdr, hn);
        if (body.len) SSL_write(ssl, body.data, (int)body.len);
    }
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(cfd);
    close(fd);
    SSL_CTX_free(ctx);
    return 0;
}

/* Max-N HTTPS demo: route `/health` → JSON, else fixed body; then close listener.
 * Soft Partial — not a full HTTPS framework. Returns 0 on success, 1 on setup fail. */
static inline int64_t mako_tls_serve_n(
    int64_t port,
    MakoString cert_path,
    MakoString key_path,
    MakoString body,
    int64_t max_reqs
) {
    signal(SIGPIPE, SIG_IGN);
    if (max_reqs < 1) max_reqs = 1;
    char cert[512], key[512];
    if (cert_path.len >= sizeof(cert) || key_path.len >= sizeof(key)) return 1;
    memcpy(cert, cert_path.data, cert_path.len);
    cert[cert_path.len] = 0;
    memcpy(key, key_path.data, key_path.len);
    key[key_path.len] = 0;

    SSL_CTX *ctx = mako_tls_make_ctx(cert, key);
    if (!ctx) {
        fprintf(stderr, "error: tls: failed to load cert/key\n");
        ERR_print_errors_fp(stderr);
        return 1;
    }
    int fd = mako_http_listen_fd(port);
    if (fd < 0) {
        SSL_CTX_free(ctx);
        return 1;
    }
    fprintf(stderr, "mako tls_serve_n(%lld) on :%lld\n",
            (long long)max_reqs, (long long)port);

    int done = 0;
    while (done < (int)max_reqs) {
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0) continue;
        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, cfd);
        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            close(cfd);
            continue;
        }
        char req[8192];
        int n = SSL_read(ssl, req, (int)sizeof(req) - 1);
        if (n < 0) n = 0;
        req[n] = 0;

        /* Path from request line: METHOD SP path SP … */
        const char *path = "/";
        char pathbuf[256];
        pathbuf[0] = '/';
        pathbuf[1] = 0;
        {
            const char *sp1 = strchr(req, ' ');
            if (sp1) {
                const char *p0 = sp1 + 1;
                const char *sp2 = strchr(p0, ' ');
                size_t plen = sp2 ? (size_t)(sp2 - p0) : strlen(p0);
                if (plen >= sizeof(pathbuf)) plen = sizeof(pathbuf) - 1;
                memcpy(pathbuf, p0, plen);
                pathbuf[plen] = 0;
                path = pathbuf;
            }
        }

        const char *ctype = "text/plain; charset=utf-8";
        const char *payload = body.data ? body.data : "";
        size_t plen = body.len;
        int status = 200;
        const char *reason = "OK";
        static const char health_json[] = "{\"ok\":true}\n";
        static const char missing[] = "missing\n";
        if (strcmp(path, "/health") == 0) {
            ctype = "application/json";
            payload = health_json;
            plen = sizeof(health_json) - 1;
        } else if (strcmp(path, "/") != 0) {
            status = 404;
            reason = "Not Found";
            payload = missing;
            plen = sizeof(missing) - 1;
        }

        char hdr[360];
        int hn = snprintf(
            hdr,
            sizeof(hdr),
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n",
            status,
            reason,
            ctype,
            plen
        );
        if (hn > 0) SSL_write(ssl, hdr, hn);
        if (plen) SSL_write(ssl, payload, (int)plen);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(cfd);
        done++;
    }
    close(fd);
    SSL_CTX_free(ctx);
    fprintf(stderr, "mako tls_serve_n done\n");
    return 0;
}

static inline int64_t mako_tls_listen_stub(int64_t port) {
    fprintf(stderr, "mako tls: use tls_serve / tls_serve_once (OpenSSL linked) port %lld\n",
            (long long)port);
    return 0;
}

/* HTTPS GET once — insecure verify (for local self-signed / demos).
 * Returns response body as MakoString (empty on failure). */
static inline MakoString mako_tls_get_insecure(MakoString host, int64_t port, MakoString path) {
    char hbuf[256], pbuf[512];
    if (host.len >= sizeof(hbuf) || path.len >= sizeof(pbuf))
        return mako_str_from_cstr("");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;

    SSL_CTX *ctx = mako_tls_make_client_ctx();
    if (!ctx) return mako_str_from_cstr("");
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, hbuf, &addr.sin_addr) != 1) {
        /* try localhost alias */
        if (strcmp(hbuf, "localhost") == 0)
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        else {
            close(fd);
            SSL_CTX_free(ctx);
            return mako_str_from_cstr("");
        }
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, hbuf);
    if (SSL_connect(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    char req[640];
    int rn = snprintf(
        req,
        sizeof(req),
        "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
        pbuf,
        hbuf
    );
    if (rn > 0) SSL_write(ssl, req, rn);
    char resp[8192];
    int total = 0;
    for (;;) {
        int n = SSL_read(ssl, resp + total, (int)sizeof(resp) - 1 - total);
        if (n <= 0) break;
        total += n;
        if (total >= (int)sizeof(resp) - 1) break;
    }
    resp[total] = 0;
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);

    char *body = strstr(resp, "\r\n\r\n");
    if (body) {
        body += 4;
        return mako_str_from_cstr(body);
    }
    return mako_str_from_cstr(resp);
}

/* HTTPS GET with PEM trust store (self-signed: pass the server cert as CA). */
static inline MakoString mako_tls_get(
    MakoString host,
    int64_t port,
    MakoString path,
    MakoString ca_pem_path
) {
    char hbuf[256], pbuf[512], cabuf[512];
    if (host.len >= sizeof(hbuf) || path.len >= sizeof(pbuf) || ca_pem_path.len >= sizeof(cabuf))
        return mako_str_from_cstr("");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    memcpy(cabuf, ca_pem_path.data, ca_pem_path.len);
    cabuf[ca_pem_path.len] = 0;

    SSL_CTX *ctx = mako_tls_make_client_ctx();
    if (!ctx) return mako_str_from_cstr("");
    if (SSL_CTX_load_verify_locations(ctx, cabuf, NULL) != 1) {
        fprintf(stderr, "tls_get: failed to load CA %s\n", cabuf);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, hbuf, &addr.sin_addr) != 1) {
        if (strcmp(hbuf, "localhost") == 0)
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        else {
            close(fd);
            SSL_CTX_free(ctx);
            return mako_str_from_cstr("");
        }
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, hbuf);
    if (SSL_connect(ssl) <= 0) {
        fprintf(stderr, "tls_get: handshake/verify failed\n");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        fprintf(stderr, "tls_get: verify_result=%ld\n", SSL_get_verify_result(ssl));
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    char req[640];
    int rn = snprintf(
        req,
        sizeof(req),
        "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
        pbuf,
        hbuf
    );
    if (rn > 0) SSL_write(ssl, req, rn);
    char resp[8192];
    int total = 0;
    for (;;) {
        int n = SSL_read(ssl, resp + total, (int)sizeof(resp) - 1 - total);
        if (n <= 0) break;
        total += n;
        if (total >= (int)sizeof(resp) - 1) break;
    }
    resp[total] = 0;
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);
    char *body = strstr(resp, "\r\n\r\n");
    if (body) {
        body += 4;
        return mako_str_from_cstr(body);
    }
    return mako_str_from_cstr(resp);
}

/* Live handshake probe: OpenSSL SSL_connect + peer verify; returns "ok" or "".
 * Does not send HTTP — proves production TLS path independently of GET. */
static inline MakoString mako_tls_handshake_ok(
    MakoString host, int64_t port, MakoString ca_pem_path
) {
    char hbuf[256], cabuf[512];
    if (host.len >= sizeof(hbuf) || ca_pem_path.len >= sizeof(cabuf))
        return mako_str_from_cstr("");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(cabuf, ca_pem_path.data, ca_pem_path.len);
    cabuf[ca_pem_path.len] = 0;

    SSL_CTX *ctx = mako_tls_make_client_ctx();
    if (!ctx) return mako_str_from_cstr("");
    if (SSL_CTX_load_verify_locations(ctx, cabuf, NULL) != 1) {
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { SSL_CTX_free(ctx); return mako_str_from_cstr(""); }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, hbuf, &addr.sin_addr) != 1) {
        if (strcmp(hbuf, "localhost") == 0)
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        else {
            close(fd);
            SSL_CTX_free(ctx);
            return mako_str_from_cstr("");
        }
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, hbuf);
    if (SSL_connect(ssl) <= 0 || SSL_get_verify_result(ssl) != X509_V_OK) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);
    return mako_str_from_cstr("ok");
}

/* Negotiated TLS version string after handshake (e.g. "TLSv1.3"), or empty. */
static inline MakoString mako_tls_handshake_version(
    MakoString host, int64_t port, MakoString ca_pem_path
) {
    char hbuf[256], cabuf[512];
    if (host.len >= sizeof(hbuf) || ca_pem_path.len >= sizeof(cabuf))
        return mako_str_from_cstr("");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(cabuf, ca_pem_path.data, ca_pem_path.len);
    cabuf[ca_pem_path.len] = 0;

    SSL_CTX *ctx = mako_tls_make_client_ctx();
    if (!ctx) return mako_str_from_cstr("");
    if (SSL_CTX_load_verify_locations(ctx, cabuf, NULL) != 1) {
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { SSL_CTX_free(ctx); return mako_str_from_cstr(""); }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, hbuf, &addr.sin_addr) != 1) {
        if (strcmp(hbuf, "localhost") == 0)
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        else {
            close(fd);
            SSL_CTX_free(ctx);
            return mako_str_from_cstr("");
        }
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, hbuf);
    if (SSL_connect(ssl) <= 0 || SSL_get_verify_result(ssl) != X509_V_OK) {
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    const char *ver = SSL_get_version(ssl);
    MakoString out = mako_str_from_cstr(ver ? ver : "");
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);
    return out;
}

/* ALPN select callback: prefer "h2".
 * SSL_select_next_proto(server, client): result must point into *client* list. */
static int mako_tls_alpn_select_cb(
    SSL *ssl, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg
) {
    (void)ssl;
    (void)arg;
    unsigned char *sel = NULL;
    unsigned char sell = 0;
    int r = SSL_select_next_proto(
        &sel, &sell,
        (const unsigned char *)"\x02h2", 3,
        in, inlen
    );
    if (r != OPENSSL_NPN_NEGOTIATED || !sel) return SSL_TLSEXT_ERR_NOACK;
    *out = sel;
    *outlen = sell;
    return SSL_TLSEXT_ERR_OK;
}

/* Write one HTTP/2 frame over TLS. */
static inline int mako_tls_h2_write_frame(
    SSL *ssl, unsigned char type, unsigned char flags,
    uint32_t stream, const void *payload, size_t len
) {
    if (len > 0xffffffu) return 0;
    unsigned char hdr[9];
    hdr[0] = (unsigned char)((len >> 16) & 0xff);
    hdr[1] = (unsigned char)((len >> 8) & 0xff);
    hdr[2] = (unsigned char)(len & 0xff);
    hdr[3] = type;
    hdr[4] = flags;
    hdr[5] = (unsigned char)((stream >> 24) & 0x7f);
    hdr[6] = (unsigned char)((stream >> 16) & 0xff);
    hdr[7] = (unsigned char)((stream >> 8) & 0xff);
    hdr[8] = (unsigned char)(stream & 0xff);
    if (SSL_write(ssl, hdr, 9) != 9) return 0;
    if (len > 0 && payload) {
        if (SSL_write(ssl, payload, (int)len) != (int)len) return 0;
    }
    return 1;
}

/* Read into buf until at least need bytes (or EOF/error). Returns bytes available. */
static inline int mako_tls_h2_fill(SSL *ssl, unsigned char *buf, int *have, int cap, int need) {
    while (*have < need) {
        if (*have >= cap) return *have;
        int n = SSL_read(ssl, buf + *have, cap - *have);
        if (n <= 0) break;
        *have += n;
    }
    return *have;
}

/* Consume one frame from buf[*off..*have); advances *off. Returns 1 on ok. */
static inline int mako_tls_h2_take_frame(
    const unsigned char *buf, int have, int *off,
    unsigned char *otype, unsigned char *oflags, uint32_t *ostream,
    const unsigned char **opay, size_t *olen
) {
    if (*off + 9 > have) return 0;
    size_t len = ((size_t)buf[*off] << 16) | ((size_t)buf[*off + 1] << 8) | (size_t)buf[*off + 2];
    if (*off + 9 + (int)len > have) return 0;
    *otype = buf[*off + 3];
    *oflags = buf[*off + 4];
    *ostream = ((uint32_t)(buf[*off + 5] & 0x7f) << 24)
        | ((uint32_t)buf[*off + 6] << 16)
        | ((uint32_t)buf[*off + 7] << 8)
        | (uint32_t)buf[*off + 8];
    *opay = buf + *off + 9;
    *olen = len;
    *off += 9 + (int)len;
    return 1;
}

/* HPACK request block: :method (GET=0x82 / POST=0x83), :scheme https, :path, :authority. */
static inline int mako_tls_h2_build_req_headers(
    unsigned char *out, size_t cap,
    int is_post, const char *host, const char *path
) {
    size_t hlen = strlen(host);
    size_t plen = strlen(path);
    int use_path_idx = (plen == 1 && path[0] == '/');
    size_t need = 3;
    if (!use_path_idx) need = 2 + 1 + 1 + plen;
    need += 1 + 1 + hlen;
    if (need > cap) return -1;
    size_t o = 0;
    out[o++] = is_post ? 0x83 : 0x82; /* :method POST / GET */
    out[o++] = 0x87; /* :scheme https */
    if (use_path_idx) {
        out[o++] = 0x84; /* :path / */
    } else {
        out[o++] = 0x04;
        if (plen > 127) return -1;
        out[o++] = (unsigned char)plen;
        memcpy(out + o, path, plen);
        o += plen;
    }
    out[o++] = 0x01; /* :authority literal */
    if (hlen > 127) return -1;
    out[o++] = (unsigned char)hlen;
    memcpy(out + o, host, hlen);
    o += hlen;
    return (int)o;
}

static inline int mako_tls_h2_build_get_headers(
    unsigned char *out, size_t cap, const char *host, const char *path
) {
    return mako_tls_h2_build_req_headers(out, cap, 0, host, path);
}

/* Compact read-loop helper: compact buffer when off advances. */
static inline void mako_tls_h2_compact(unsigned char *buf, int *have, int *off) {
    if (*off > 0 && *have > *off) {
        memmove(buf, buf + *off, (size_t)(*have - *off));
        *have -= *off;
        *off = 0;
    } else if (*off >= *have) {
        *have = 0;
        *off = 0;
    }
}

/* Read one complete frame; returns 1 on success. */
static inline int mako_tls_h2_read_frame(
    SSL *ssl, unsigned char *buf, int *have, int *off, int cap,
    unsigned char *otype, unsigned char *oflags, uint32_t *ostream,
    const unsigned char **opay, size_t *olen
) {
    mako_tls_h2_compact(buf, have, off);
    if (mako_tls_h2_fill(ssl, buf, have, cap, *off + 9) < *off + 9) return 0;
    size_t flen = ((size_t)buf[*off] << 16) | ((size_t)buf[*off + 1] << 8) | (size_t)buf[*off + 2];
    if (mako_tls_h2_fill(ssl, buf, have, cap, *off + 9 + (int)flen) < *off + 9 + (int)flen)
        return 0;
    return mako_tls_h2_take_frame(buf, *have, off, otype, oflags, ostream, opay, olen);
}

/* Send :status 200 (+ optional DATA body) on stream. */
static inline void mako_tls_h2_reply_200(SSL *ssl, uint32_t stream, MakoString body) {
    unsigned char hblock[1] = {0x88};
    unsigned char hflags = 0x04; /* END_HEADERS */
    if (body.len == 0) hflags |= 0x01;
    mako_tls_h2_write_frame(ssl, 0x01, hflags, stream, hblock, 1);
    if (body.len > 0) {
        mako_tls_h2_write_frame(ssl, 0x00, 0x01, stream, body.data, body.len);
    }
}

/* Format "200" or "200\\n<body>" into freshly allocated MakoString. */
static inline MakoString mako_tls_h2_fmt_resp(int status_ok, const char *body_acc, size_t body_len) {
    if (!status_ok) return mako_str_from_cstr("");
    if (body_len == 0) return mako_str_from_cstr("200");
    char out[4200];
    if (body_len > sizeof(out) - 5) body_len = sizeof(out) - 5;
    memcpy(out, "200\n", 4);
    memcpy(out + 4, body_acc, body_len);
    out[4 + body_len] = 0;
    return mako_str_from_cstr(out);
}

/* Drain frames until stream completes; ACK SETTINGS; collect :status 200 + DATA.
 * Returns 1 if status 200 seen. Leaves unread frames in buf. */
static inline int mako_tls_h2_await_response(
    SSL *ssl, unsigned char *buf, int *have, int *off, int cap,
    uint32_t want_stream, char *body_acc, size_t body_cap, size_t *body_len
) {
    int status_ok = 0;
    int done = 0;
    *body_len = 0;
    for (int iter = 0; iter < 48 && !done; iter++) {
        unsigned char typ = 0, flags = 0;
        uint32_t stream = 0;
        const unsigned char *pay = NULL;
        size_t plen = 0;
        if (!mako_tls_h2_read_frame(ssl, buf, have, off, cap, &typ, &flags, &stream, &pay, &plen))
            break;
        if (typ == 0x04) {
            if (!(flags & 0x01)) {
                static const unsigned char sack[9] = {
                    0, 0, 0, 0x04, 0x01, 0, 0, 0, 0
                };
                SSL_write(ssl, sack, 9);
            }
        } else if (typ == 0x01 && stream == want_stream) {
            for (size_t i = 0; i < plen; i++) {
                if (pay[i] == 0x88) { status_ok = 1; break; }
            }
            if (flags & 0x01) done = 1;
        } else if (typ == 0x00 && stream == want_stream) {
            size_t copy = plen;
            if (*body_len + copy > body_cap) copy = body_cap - *body_len;
            if (copy && pay) {
                memcpy(body_acc + *body_len, pay, copy);
                *body_len += copy;
            }
            if (flags & 0x01) done = 1;
        } else if (typ == 0x07) {
            done = 1;
        }
    }
    return status_ok;
}

/* Strip PADDED / PRIORITY prefix from HEADERS payload before HPACK decode. */
static inline int mako_tls_h2_headers_hpack_slice(
    unsigned char flags, const unsigned char **pay, size_t *plen
) {
    if (!pay || !*pay || !plen) return 0;
    if (flags & 0x08) { /* PADDED */
        if (*plen < 1) return 0;
        size_t pad = (size_t)(*pay)[0];
        (*pay)++;
        (*plen)--;
        if (pad > *plen) return 0;
        *plen -= pad;
    }
    if (flags & 0x20) { /* PRIORITY */
        if (*plen < 5) return 0;
        *pay += 5;
        *plen -= 5;
    }
    return 1;
}

/* Extract :path via the full HPACK decoder (RFC 7541).
 *
 * Critical: name-indexed literals (e.g. 0x44 / 0x04 / 0x14 with name index 4
 * for :path) MUST use the literal value from the wire — never the static
 * table's default value for that index (`/` for index 4, `/index.html` for 5).
 * A naive byte scan that treats 0x84 (indexed :path /) as a full-block search
 * also false-positives when 0x84 appears inside other fields.
 *
 * Uses mako_hpack_decode_block so Huffman, varints, incremental indexing, and
 * never-indexed forms all work for real clients (curl, browsers). */
static inline int mako_tls_h2_extract_path_simple(
    const unsigned char *pay, size_t plen, char *out, size_t cap
) {
    if (!out || cap < 2) return 0;
    out[0] = 0;
    if (!pay || plen == 0) return 0;

    MakoString block = {(char *)pay, plen};
    int64_t n = mako_hpack_decode_block(block);
    if (n < 0) return 0;

    int found = 0;
    for (int64_t i = 0; i < n; i++) {
        MakoString nm = mako_hpack_decoded_name(i);
        if (!nm.data) continue;
        if (nm.len == 5 && memcmp(nm.data, ":path", 5) == 0) {
            MakoString vl = mako_hpack_decoded_value(i);
            size_t copy = vl.data ? vl.len : 0;
            if (copy + 1 > cap) copy = cap - 1;
            if (copy && vl.data) memcpy(out, vl.data, copy);
            out[copy] = 0;
            free(nm.data);
            free(vl.data);
            found = 1;
            break;
        }
        free(nm.data);
    }
    /* decoded_name/value allocate; clear table storage too */
    mako_hpack_decode_clear();
    return found;
}

/* Legacy byte-scan fallback (no Huffman). Prefer extract_path_simple above. */
static inline int mako_tls_h2_extract_path_scan(
    const unsigned char *pay, size_t plen, char *out, size_t cap
) {
    if (!out || cap < 2 || !pay) return 0;
    out[0] = 0;
    size_t i = 0;
    while (i < plen) {
        unsigned char b = pay[i];
        /* Fully indexed header — only the field start (not a mid-string byte). */
        if (b & 0x80) {
            size_t idx = (size_t)(b & 0x7f);
            size_t off = i + 1;
            if (idx == 0x7f) {
                /* Multi-byte index: skip continuation for scan purposes */
                while (off < plen && (pay[off] & 0x80)) off++;
                if (off < plen) off++;
            }
            if (idx == 4) { /* :path / */
                out[0] = '/';
                out[1] = 0;
                return 1;
            }
            if (idx == 5) { /* :path /index.html */
                const char *p = "/index.html";
                size_t n = 11;
                if (n + 1 > cap) n = cap - 1;
                memcpy(out, p, n);
                out[n] = 0;
                return 1;
            }
            i = (idx == 0x7f) ? off : i + 1;
            continue;
        }
        /* Literal with incremental indexing — 6-bit name index */
        if ((b & 0xc0) == 0x40) {
            size_t ni = (size_t)(b & 0x3f);
            size_t off = i + 1;
            if (ni == 0x3f) {
                while (off < plen && (pay[off] & 0x80)) off++;
                if (off < plen) off++;
            }
            /* name if ni==0 */
            if (ni == 0) {
                if (off >= plen) return 0;
                if (pay[off] & 0x80) return 0; /* Huffman name — need full decoder */
                size_t nl = (size_t)(pay[off] & 0x7f);
                off += 1 + nl;
            }
            if (off >= plen) return 0;
            int huff = (pay[off] & 0x80) != 0;
            size_t vl = (size_t)(pay[off] & 0x7f);
            off++;
            if (huff) return 0; /* need full decoder */
            if (off + vl > plen) return 0;
            if (ni == 4 || ni == 5) {
                size_t n = vl;
                if (n + 1 > cap) n = cap - 1;
                if (n) memcpy(out, pay + off, n);
                out[n] = 0;
                return 1;
            }
            i = off + vl;
            continue;
        }
        /* Dynamic table size update */
        if ((b & 0xe0) == 0x20) {
            size_t off = i + 1;
            if ((b & 0x1f) == 0x1f) {
                while (off < plen && (pay[off] & 0x80)) off++;
                if (off < plen) off++;
            }
            i = off;
            continue;
        }
        /* Literal without indexing / never indexed — 4-bit name index */
        {
            size_t ni = (size_t)(b & 0x0f);
            size_t off = i + 1;
            if (ni == 0x0f) {
                while (off < plen && (pay[off] & 0x80)) off++;
                if (off < plen) off++;
            }
            if (ni == 0) {
                if (off >= plen) return 0;
                if (pay[off] & 0x80) return 0;
                size_t nl = (size_t)(pay[off] & 0x7f);
                off += 1 + nl;
            }
            if (off >= plen) return 0;
            int huff = (pay[off] & 0x80) != 0;
            size_t vl = (size_t)(pay[off] & 0x7f);
            off++;
            if (huff) return 0;
            if (off + vl > plen) return 0;
            if (ni == 4 || ni == 5) {
                size_t n = vl;
                if (n + 1 > cap) n = cap - 1;
                if (n) memcpy(out, pay + off, n);
                out[n] = 0;
                return 1;
            }
            i = off + vl;
        }
    }
    return 0;
}

#if defined(MAKO_HAS_NGHTTP2)
#include <nghttp2/nghttp2.h>
/* Real HPACK inflate — needed when libnghttp2 clients Huffman-encode :path.
 * inflater must be connection-scoped so dynamic-table refs across streams work. */
static inline int mako_tls_h2_extract_path_ng(
    nghttp2_hd_inflater *inflater,
    const unsigned char *pay, size_t plen, char *out, size_t cap
) {
    if (!inflater || !out || cap < 2 || !pay || plen == 0) return 0;
    out[0] = 0;
    int found = 0;
    int final = 1;
    while (plen > 0) {
        nghttp2_nv nv;
        int inflate_flags = 0;
        ssize_t rv = nghttp2_hd_inflate_hd2(
            inflater, &nv, &inflate_flags,
            (uint8_t *)pay, plen, final
        );
        if (rv < 0) break;
        pay += (size_t)rv;
        plen -= (size_t)rv;
        if (inflate_flags & NGHTTP2_HD_INFLATE_EMIT) {
            if (nv.namelen == 5 && memcmp(nv.name, ":path", 5) == 0) {
                size_t n = nv.valuelen;
                if (n + 1 > cap) n = cap - 1;
                memcpy(out, nv.value, n);
                out[n] = 0;
                found = 1;
            }
        }
        if (inflate_flags & NGHTTP2_HD_INFLATE_FINAL) {
            nghttp2_hd_inflate_end_headers(inflater);
            break;
        }
    }
    return found;
}
#endif

/* flags: HEADERS frame flags (PADDED/PRIORITY).
 * hd_inflater: optional connection-scoped nghttp2 inflater (NULL = simple only). */
static inline int mako_tls_h2_extract_path_ex(
    unsigned char flags, const unsigned char *pay, size_t plen,
    char *out, size_t cap, void *hd_inflater
) {
    if (!mako_tls_h2_headers_hpack_slice(flags, &pay, &plen)) return 0;
#if defined(MAKO_HAS_NGHTTP2)
    if (hd_inflater) {
        if (mako_tls_h2_extract_path_ng(
                (nghttp2_hd_inflater *)hd_inflater, pay, plen, out, cap))
            return 1;
    }
#endif
    (void)hd_inflater;
    return mako_tls_h2_extract_path_simple(pay, plen, out, cap);
}

static inline int mako_tls_h2_extract_path(
    const unsigned char *pay, size_t plen, char *out, size_t cap
) {
    return mako_tls_h2_extract_path_ex(0, pay, plen, out, cap, NULL);
}

/* Pick response body for routed path. / → root, /health → health, else not_found. */
static inline MakoString mako_tls_h2_route_body(
    const char *path, MakoString root, MakoString health, MakoString not_found
) {
    if (path && strcmp(path, "/health") == 0) return health;
    if (path && strcmp(path, "/") == 0) return root;
    return not_found;
}

static inline void mako_tls_h2_reply_404(SSL *ssl, uint32_t stream, MakoString body) {
    unsigned char hblock[1] = {0x8d}; /* :status 404 indexed */
    unsigned char hflags = 0x04;
    if (body.len == 0) hflags |= 0x01;
    mako_tls_h2_write_frame(ssl, 0x01, hflags, stream, hblock, 1);
    if (body.len > 0) {
        mako_tls_h2_write_frame(ssl, 0x00, 0x01, stream, body.data, body.len);
    }
}

static inline void mako_tls_h2_reply_routed(
    SSL *ssl, uint32_t stream, const char *path,
    MakoString root, MakoString health
) {
    static const char nf[] = "not-found\n";
    MakoString not_found = {(char *)nf, sizeof(nf) - 1};
    if (path && strcmp(path, "/") != 0 && strcmp(path, "/health") != 0) {
        mako_tls_h2_reply_404(ssl, stream, not_found);
        return;
    }
    MakoString body = mako_tls_h2_route_body(path, root, health, not_found);
    mako_tls_h2_reply_200(ssl, stream, body);
}

/* Await responses for two streams (multiplex-safe). Frames for either stream accepted
 * in any order. Returns 1 only if both saw :status 200. */
static inline int mako_tls_h2_await_two(
    SSL *ssl, unsigned char *buf, int *have, int *off, int cap,
    uint32_t s1, char *b1, size_t b1cap, size_t *l1, int *ok1,
    uint32_t s2, char *b2, size_t b2cap, size_t *l2, int *ok2
) {
    *l1 = 0;
    *l2 = 0;
    *ok1 = 0;
    *ok2 = 0;
    int done1 = 0, done2 = 0;
    for (int iter = 0; iter < 96 && !(done1 && done2); iter++) {
        unsigned char typ = 0, flags = 0;
        uint32_t stream = 0;
        const unsigned char *pay = NULL;
        size_t plen = 0;
        if (!mako_tls_h2_read_frame(ssl, buf, have, off, cap, &typ, &flags, &stream, &pay, &plen))
            break;
        if (typ == 0x04) {
            if (!(flags & 0x01)) {
                static const unsigned char sack[9] = {
                    0, 0, 0, 0x04, 0x01, 0, 0, 0, 0
                };
                SSL_write(ssl, sack, 9);
            }
            continue;
        }
        if (typ == 0x07) break;
        int is1 = (stream == s1);
        int is2 = (stream == s2);
        if (!is1 && !is2) continue;
        int *ok = is1 ? ok1 : ok2;
        int *done = is1 ? &done1 : &done2;
        char *body = is1 ? b1 : b2;
        size_t *blen = is1 ? l1 : l2;
        size_t bcap = is1 ? b1cap : b2cap;
        if (typ == 0x01) {
            for (size_t i = 0; i < plen; i++) {
                if (pay[i] == 0x88) { *ok = 1; break; }
            }
            if (flags & 0x01) *done = 1;
        } else if (typ == 0x00) {
            size_t copy = plen;
            if (*blen + copy > bcap) copy = bcap - *blen;
            if (copy && pay) {
                memcpy(body + *blen, pay, copy);
                *blen += copy;
            }
            if (flags & 0x01) *done = 1;
        }
    }
    return *ok1 && *ok2;
}

/* Shared: connect TLS client with ALPN h2 + verify. Caller owns ssl/fd/ctx on success. */
static inline SSL *mako_tls_h2_connect(
    const char *hbuf, int64_t port, const char *cabuf,
    SSL_CTX **out_ctx, int *out_fd
) {
    SSL_CTX *ctx = mako_tls_make_client_ctx();
    if (!ctx) return NULL;
    if (SSL_CTX_load_verify_locations(ctx, cabuf, NULL) != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    static const unsigned char protos[] = {2, 'h', '2'};
    if (SSL_CTX_set_alpn_protos(ctx, protos, sizeof(protos)) != 0) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { SSL_CTX_free(ctx); return NULL; }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, hbuf, &addr.sin_addr) != 1) {
        if (strcmp(hbuf, "localhost") == 0)
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        else {
            close(fd);
            SSL_CTX_free(ctx);
            return NULL;
        }
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        SSL_CTX_free(ctx);
        return NULL;
    }
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, hbuf);
    if (SSL_connect(ssl) <= 0 || SSL_get_verify_result(ssl) != X509_V_OK) {
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return NULL;
    }
    const unsigned char *alpn = NULL;
    unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(ssl, &alpn, &alpn_len);
    if (!(alpn_len == 2 && alpn && alpn[0] == 'h' && alpn[1] == '2')) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return NULL;
    }
    *out_ctx = ctx;
    *out_fd = fd;
    return ssl;
}

/* Serve up to max_reqs with path routing: `/` → root_body, `/health` → health_body. */
static inline int64_t mako_tls_serve_h2_routes(
    int64_t port,
    MakoString cert_path,
    MakoString key_path,
    MakoString root_body,
    MakoString health_body,
    int64_t max_reqs
) {
    signal(SIGPIPE, SIG_IGN);
    if (max_reqs <= 0) max_reqs = 1;
    if (max_reqs > 16) max_reqs = 16;
    char cert[512], key[512];
    if (cert_path.len >= sizeof(cert) || key_path.len >= sizeof(key)) return 1;
    memcpy(cert, cert_path.data, cert_path.len);
    cert[cert_path.len] = 0;
    memcpy(key, key_path.data, key_path.len);
    key[key_path.len] = 0;

    SSL_CTX *ctx = mako_tls_make_ctx(cert, key);
    if (!ctx) {
        fprintf(stderr, "error: tls: failed to load cert/key\n");
        ERR_print_errors_fp(stderr);
        return 1;
    }
    SSL_CTX_set_alpn_select_cb(ctx, mako_tls_alpn_select_cb, NULL);

    int fd = mako_http_listen_fd(port);
    if (fd < 0) {
        SSL_CTX_free(ctx);
        return 1;
    }
    fprintf(stderr, "mako tls_h2_routes(%lld) on :%lld\n",
            (long long)max_reqs, (long long)port);

    /* Multi-accept: each TLS connection can serve one or more streams until
     * the global max_reqs budget is exhausted (curl --http2 multi-URL or
     * separate connections both work). */
    int total_done = 0;
    while (total_done < (int)max_reqs) {
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0) continue;
        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, cfd);
        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            close(cfd);
            continue;
        }
        const unsigned char *alpn = NULL;
        unsigned int alpn_len = 0;
        SSL_get0_alpn_selected(ssl, &alpn, &alpn_len);
        int is_h2 = (alpn_len == 2 && alpn && alpn[0] == 'h' && alpn[1] == '2');
        int conn_done = 0;

        if (is_h2) {
            unsigned char buf[8192];
            int have = 0, off = 0;
            if (mako_tls_h2_fill(ssl, buf, &have, (int)sizeof(buf), 24) < 24)
                goto h2_conn_done;
            off = 24;
            static const unsigned char empty_settings[9] = {
                0, 0, 0, 0x04, 0x00, 0, 0, 0, 0
            };
            SSL_write(ssl, empty_settings, 9);

            int settings_acked = 0;
            uint32_t pending_stream = 0;
            int pending_open = 0;
            char pending_path[128];
            pending_path[0] = 0;
#if defined(MAKO_HAS_NGHTTP2)
            nghttp2_hd_inflater *hd_inf = NULL;
            nghttp2_hd_inflate_new(&hd_inf);
#else
            void *hd_inf = NULL;
#endif

            for (int iter = 0;
                 iter < 96 && total_done + conn_done < (int)max_reqs;
                 iter++) {
                unsigned char typ = 0, flags = 0;
                uint32_t stream = 0;
                const unsigned char *pay = NULL;
                size_t plen = 0;
                if (!mako_tls_h2_read_frame(
                        ssl, buf, &have, &off, (int)sizeof(buf),
                        &typ, &flags, &stream, &pay, &plen))
                    break;
                if (typ == 0x04) {
                    if (!(flags & 0x01) && !settings_acked) {
                        static const unsigned char sack[9] = {
                            0, 0, 0, 0x04, 0x01, 0, 0, 0, 0
                        };
                        SSL_write(ssl, sack, 9);
                        settings_acked = 1;
                    }
                } else if (typ == 0x01 && stream != 0 && (stream & 1)) {
                    char path[128];
                    /* Never invent "/" when decode fails — that maps every
                     * unknown path to the root handler (the /docs bug). */
                    if (!mako_tls_h2_extract_path_ex(
                            flags, pay, plen, path, sizeof(path), hd_inf)) {
                        path[0] = 0;
                    }
                    if (flags & 0x01) {
                        mako_tls_h2_reply_routed(
                            ssl, stream, path, root_body, health_body
                        );
                        conn_done++;
                    } else {
                        pending_stream = stream;
                        pending_open = 1;
                        size_t pl = strlen(path);
                        if (pl >= sizeof(pending_path)) pl = sizeof(pending_path) - 1;
                        memcpy(pending_path, path, pl);
                        pending_path[pl] = 0;
                    }
                } else if (typ == 0x00 && pending_open && stream == pending_stream) {
                    if (flags & 0x01) {
                        mako_tls_h2_reply_routed(
                            ssl, stream, pending_path, root_body, health_body
                        );
                        pending_open = 0;
                        conn_done++;
                    }
                } else if (typ == 0x07) {
                    break;
                }
            }
#if defined(MAKO_HAS_NGHTTP2)
            if (hd_inf) nghttp2_hd_inflate_del(hd_inf);
#endif
        h2_conn_done:
            (void)0;
        } else {
            char req[4096];
            int n = SSL_read(ssl, req, (int)sizeof(req) - 1);
            if (n > 0) {
                req[n] = 0;
                MakoString body = root_body;
                if (strstr(req, " /health")) body = health_body;
                char hdr[320];
                int hn = snprintf(
                    hdr, sizeof(hdr),
                    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                    "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                    body.len
                );
                if (hn > 0) SSL_write(ssl, hdr, hn);
                if (body.len) SSL_write(ssl, body.data, (int)body.len);
                conn_done++;
            }
        }
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(cfd);
        total_done += conn_done;
        if (conn_done == 0) {
            /* Handshake-only / empty peer — keep accepting. */
            continue;
        }
    }
    close(fd);
    SSL_CTX_free(ctx);
    fprintf(stderr, "mako tls_h2_routes done (%d)\n", total_done);
    return 0;
}

/* Serve up to max_reqs; `/` and unknown use body, `/health` → "ok\\n". */
static inline int64_t mako_tls_serve_h2_n(
    int64_t port,
    MakoString cert_path,
    MakoString key_path,
    MakoString body,
    int64_t max_reqs
) {
    MakoString health = mako_str_from_cstr("ok\n");
    return mako_tls_serve_h2_routes(port, cert_path, key_path, body, health, max_reqs);
}

/* One-shot HTTPS server with ALPN h2 (single request). */
static inline int64_t mako_tls_serve_once_h2(
    int64_t port,
    MakoString cert_path,
    MakoString key_path,
    MakoString body
) {
    return mako_tls_serve_h2_n(port, cert_path, key_path, body, 1);
}

/* Client: TLS+ALPN h2, send preface+SETTINGS, read server SETTINGS frames.
 * Returns "h2" if ALPN negotiated and we got a SETTINGS frame; else "". */
static inline MakoString mako_tls_h2_settings_exchange(
    MakoString host, int64_t port, MakoString ca_pem_path
) {
    char hbuf[256], cabuf[512];
    if (host.len >= sizeof(hbuf) || ca_pem_path.len >= sizeof(cabuf))
        return mako_str_from_cstr("");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(cabuf, ca_pem_path.data, ca_pem_path.len);
    cabuf[ca_pem_path.len] = 0;

    SSL_CTX *ctx = NULL;
    int fd = -1;
    SSL *ssl = mako_tls_h2_connect(hbuf, port, cabuf, &ctx, &fd);
    if (!ssl) return mako_str_from_cstr("");

    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    static const unsigned char settings[9] = {0, 0, 0, 0x04, 0x00, 0, 0, 0, 0};
    SSL_write(ssl, preface, (int)(sizeof(preface) - 1));
    SSL_write(ssl, settings, 9);
    char resp[64];
    int n = SSL_read(ssl, resp, (int)sizeof(resp));
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);
    if (n >= 9 && (unsigned char)resp[3] == 0x04)
        return mako_str_from_cstr("h2");
    return mako_str_from_cstr("");
}

/* Live h2 GET: SETTINGS exchange, HEADERS on stream 1, read :status (+ DATA body).
 * Returns "200" or "200\\n<body>" on success; empty on failure. */
static inline MakoString mako_tls_h2_get(
    MakoString host, int64_t port, MakoString path, MakoString ca_pem_path
) {
    char hbuf[256], pbuf[512], cabuf[512];
    if (host.len >= sizeof(hbuf) || path.len >= sizeof(pbuf) || ca_pem_path.len >= sizeof(cabuf))
        return mako_str_from_cstr("");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    memcpy(cabuf, ca_pem_path.data, ca_pem_path.len);
    cabuf[ca_pem_path.len] = 0;

    SSL_CTX *ctx = NULL;
    int fd = -1;
    SSL *ssl = mako_tls_h2_connect(hbuf, port, cabuf, &ctx, &fd);
    if (!ssl) return mako_str_from_cstr("");

    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    static const unsigned char settings[9] = {0, 0, 0, 0x04, 0x00, 0, 0, 0, 0};
    SSL_write(ssl, preface, (int)(sizeof(preface) - 1));
    SSL_write(ssl, settings, 9);

    unsigned char hblock[256];
    int hblen = mako_tls_h2_build_req_headers(hblock, sizeof(hblock), 0, hbuf, pbuf);
    if (hblen < 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    mako_tls_h2_write_frame(ssl, 0x01, 0x05, 1, hblock, (size_t)hblen);

    unsigned char buf[8192];
    int have = 0, off = 0;
    char body_acc[4096];
    size_t body_len = 0;
    int status_ok = mako_tls_h2_await_response(
        ssl, buf, &have, &off, (int)sizeof(buf), 1, body_acc, sizeof(body_acc), &body_len
    );

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);
    return mako_tls_h2_fmt_resp(status_ok, body_acc, body_len);
}

/* Live h2 POST: HEADERS (POST) + DATA body on stream 1; returns "200\\n<body>" or "". */
static inline MakoString mako_tls_h2_post(
    MakoString host, int64_t port, MakoString path,
    MakoString req_body, MakoString ca_pem_path
) {
    char hbuf[256], pbuf[512], cabuf[512];
    if (host.len >= sizeof(hbuf) || path.len >= sizeof(pbuf) || ca_pem_path.len >= sizeof(cabuf))
        return mako_str_from_cstr("");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    memcpy(cabuf, ca_pem_path.data, ca_pem_path.len);
    cabuf[ca_pem_path.len] = 0;

    SSL_CTX *ctx = NULL;
    int fd = -1;
    SSL *ssl = mako_tls_h2_connect(hbuf, port, cabuf, &ctx, &fd);
    if (!ssl) return mako_str_from_cstr("");

    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    static const unsigned char settings[9] = {0, 0, 0, 0x04, 0x00, 0, 0, 0, 0};
    SSL_write(ssl, preface, (int)(sizeof(preface) - 1));
    SSL_write(ssl, settings, 9);

    unsigned char hblock[256];
    int hblen = mako_tls_h2_build_req_headers(hblock, sizeof(hblock), 1, hbuf, pbuf);
    if (hblen < 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    /* END_HEADERS only — body follows as DATA */
    mako_tls_h2_write_frame(ssl, 0x01, 0x04, 1, hblock, (size_t)hblen);
    mako_tls_h2_write_frame(
        ssl, 0x00, 0x01, 1,
        req_body.data ? req_body.data : "",
        req_body.len
    );

    unsigned char buf[8192];
    int have = 0, off = 0;
    char body_acc[4096];
    size_t body_len = 0;
    int status_ok = mako_tls_h2_await_response(
        ssl, buf, &have, &off, (int)sizeof(buf), 1, body_acc, sizeof(body_acc), &body_len
    );

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);
    return mako_tls_h2_fmt_resp(status_ok, body_acc, body_len);
}

/* Two GETs on one h2 connection (streams 1 then 3). Returns "200\\n200" or with bodies
 * "200\\n<body1>\\n200\\n<body2>" — proves keep-alive / second request. */
static inline MakoString mako_tls_h2_get_twice(
    MakoString host, int64_t port, MakoString path, MakoString ca_pem_path
) {
    char hbuf[256], pbuf[512], cabuf[512];
    if (host.len >= sizeof(hbuf) || path.len >= sizeof(pbuf) || ca_pem_path.len >= sizeof(cabuf))
        return mako_str_from_cstr("");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    memcpy(cabuf, ca_pem_path.data, ca_pem_path.len);
    cabuf[ca_pem_path.len] = 0;

    SSL_CTX *ctx = NULL;
    int fd = -1;
    SSL *ssl = mako_tls_h2_connect(hbuf, port, cabuf, &ctx, &fd);
    if (!ssl) return mako_str_from_cstr("");

    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    static const unsigned char settings[9] = {0, 0, 0, 0x04, 0x00, 0, 0, 0, 0};
    SSL_write(ssl, preface, (int)(sizeof(preface) - 1));
    SSL_write(ssl, settings, 9);

    unsigned char hblock[256];
    int hblen = mako_tls_h2_build_req_headers(hblock, sizeof(hblock), 0, hbuf, pbuf);
    if (hblen < 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }

    unsigned char buf[8192];
    int have = 0, off = 0;
    char b1[2048], b2[2048];
    size_t l1 = 0, l2 = 0;

    mako_tls_h2_write_frame(ssl, 0x01, 0x05, 1, hblock, (size_t)hblen);
    int ok1 = mako_tls_h2_await_response(
        ssl, buf, &have, &off, (int)sizeof(buf), 1, b1, sizeof(b1), &l1
    );
    if (!ok1) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }

    mako_tls_h2_write_frame(ssl, 0x01, 0x05, 3, hblock, (size_t)hblen);
    int ok2 = mako_tls_h2_await_response(
        ssl, buf, &have, &off, (int)sizeof(buf), 3, b2, sizeof(b2), &l2
    );

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);
    if (!ok2) return mako_str_from_cstr("");

    /* "200\\n" + body1 + "\\n200\\n" + body2  (or "200\\n200" if empty bodies) */
    char out[4500];
    size_t o = 0;
    memcpy(out + o, "200\n", 4); o += 4;
    if (l1 > 0) {
        if (l1 > 2000) l1 = 2000;
        memcpy(out + o, b1, l1); o += l1;
        if (o > 0 && out[o - 1] != '\n') out[o++] = '\n';
    }
    memcpy(out + o, "200", 3); o += 3;
    if (l2 > 0) {
        out[o++] = '\n';
        if (l2 > 2000) l2 = 2000;
        memcpy(out + o, b2, l2); o += l2;
    }
    out[o] = 0;
    return mako_str_from_cstr(out);
}

/* Overlapping multiplex: fire GET / on stream 1 and GET /health on stream 3 before
 * reading either response. Returns "root=<body1>;health=<body2>" on success. */
static inline MakoString mako_tls_h2_mux(
    MakoString host, int64_t port, MakoString ca_pem_path
) {
    char hbuf[256], cabuf[512];
    if (host.len >= sizeof(hbuf) || ca_pem_path.len >= sizeof(cabuf))
        return mako_str_from_cstr("");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(cabuf, ca_pem_path.data, ca_pem_path.len);
    cabuf[ca_pem_path.len] = 0;

    SSL_CTX *ctx = NULL;
    int fd = -1;
    SSL *ssl = mako_tls_h2_connect(hbuf, port, cabuf, &ctx, &fd);
    if (!ssl) return mako_str_from_cstr("");

    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    static const unsigned char settings[9] = {0, 0, 0, 0x04, 0x00, 0, 0, 0, 0};
    SSL_write(ssl, preface, (int)(sizeof(preface) - 1));
    SSL_write(ssl, settings, 9);

    unsigned char h1[256], h3[256];
    int n1 = mako_tls_h2_build_req_headers(h1, sizeof(h1), 0, hbuf, "/");
    int n3 = mako_tls_h2_build_req_headers(h3, sizeof(h3), 0, hbuf, "/health");
    if (n1 < 0 || n3 < 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    /* Overlap: both HEADERS before any response read. */
    mako_tls_h2_write_frame(ssl, 0x01, 0x05, 1, h1, (size_t)n1);
    mako_tls_h2_write_frame(ssl, 0x01, 0x05, 3, h3, (size_t)n3);

    unsigned char buf[8192];
    int have = 0, off = 0;
    char b1[2048], b2[2048];
    size_t l1 = 0, l2 = 0;
    int ok1 = 0, ok2 = 0;
    int both = mako_tls_h2_await_two(
        ssl, buf, &have, &off, (int)sizeof(buf),
        1, b1, sizeof(b1), &l1, &ok1,
        3, b2, sizeof(b2), &l2, &ok2
    );

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);
    if (!both) return mako_str_from_cstr("");

    /* Trim trailing newlines for stable compare. */
    while (l1 > 0 && (b1[l1 - 1] == '\n' || b1[l1 - 1] == '\r')) l1--;
    while (l2 > 0 && (b2[l2 - 1] == '\n' || b2[l2 - 1] == '\r')) l2--;
    char out[4200];
    int wn = snprintf(
        out, sizeof(out), "root=%.*s;health=%.*s",
        (int)l1, b1, (int)l2, b2
    );
    if (wn <= 0) return mako_str_from_cstr("");
    return mako_str_from_cstr(out);
}

/* ---- Live h2 WINDOW_UPDATE probe ----
 * Server sends connection WINDOW_UPDATE after SETTINGS; client must observe it
 * and still complete a GET. Proves WINDOW_UPDATE on the wire + peer continues. */

static inline int64_t mako_tls_serve_h2_wu(
    int64_t port,
    MakoString cert_path,
    MakoString key_path,
    MakoString body,
    int64_t wu_increment
) {
    signal(SIGPIPE, SIG_IGN);
    if (wu_increment <= 0) wu_increment = 65535;
    if (wu_increment > 0x7fffffff) wu_increment = 0x7fffffff;
    char cert[512], key[512];
    if (cert_path.len >= sizeof(cert) || key_path.len >= sizeof(key)) return 1;
    memcpy(cert, cert_path.data, cert_path.len);
    cert[cert_path.len] = 0;
    memcpy(key, key_path.data, key_path.len);
    key[key_path.len] = 0;

    SSL_CTX *ctx = mako_tls_make_ctx(cert, key);
    if (!ctx) {
        fprintf(stderr, "error: tls: failed to load cert/key\n");
        ERR_print_errors_fp(stderr);
        return 1;
    }
    SSL_CTX_set_alpn_select_cb(ctx, mako_tls_alpn_select_cb, NULL);

    int fd = mako_http_listen_fd(port);
    if (fd < 0) {
        SSL_CTX_free(ctx);
        return 1;
    }
    fprintf(stderr, "mako tls_h2_wu on :%lld\n", (long long)port);
    int cfd = accept(fd, NULL, NULL);
    if (cfd < 0) {
        close(fd);
        SSL_CTX_free(ctx);
        return 1;
    }
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, cfd);
    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(cfd);
        close(fd);
        SSL_CTX_free(ctx);
        return 1;
    }
    const unsigned char *alpn = NULL;
    unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(ssl, &alpn, &alpn_len);
    if (!(alpn_len == 2 && alpn && alpn[0] == 'h' && alpn[1] == '2')) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(cfd);
        close(fd);
        SSL_CTX_free(ctx);
        return 1;
    }

    unsigned char buf[8192];
    int have = 0, off = 0;
    if (mako_tls_h2_fill(ssl, buf, &have, (int)sizeof(buf), 24) < 24) goto done;
    off = 24;
    static const unsigned char empty_settings[9] = {
        0, 0, 0, 0x04, 0x00, 0, 0, 0, 0
    };
    SSL_write(ssl, empty_settings, 9);

    /* Connection-level WINDOW_UPDATE (stream 0). */
    {
        unsigned int inc = (unsigned int)(wu_increment & 0x7fffffff);
        unsigned char wu[13] = {
            0, 0, 4, 0x08, 0x00,
            0, 0, 0, 0,
            (unsigned char)((inc >> 24) & 0xff),
            (unsigned char)((inc >> 16) & 0xff),
            (unsigned char)((inc >> 8) & 0xff),
            (unsigned char)(inc & 0xff)
        };
        SSL_write(ssl, wu, 13);
    }

    int settings_acked = 0;
    int replied = 0;
    for (int iter = 0; iter < 48 && !replied; iter++) {
        unsigned char typ = 0, flags = 0;
        uint32_t stream = 0;
        const unsigned char *pay = NULL;
        size_t plen = 0;
        if (!mako_tls_h2_read_frame(
                ssl, buf, &have, &off, (int)sizeof(buf),
                &typ, &flags, &stream, &pay, &plen))
            break;
        if (typ == 0x04) {
            if (!(flags & 0x01) && !settings_acked) {
                static const unsigned char sack[9] = {
                    0, 0, 0, 0x04, 0x01, 0, 0, 0, 0
                };
                SSL_write(ssl, sack, 9);
                settings_acked = 1;
            }
        } else if (typ == 0x08) {
            /* Client WINDOW_UPDATE — acknowledge by continuing; no reply needed. */
            (void)pay;
        } else if (typ == 0x01 && stream != 0 && (stream & 1)) {
            if (flags & 0x01) {
                mako_tls_h2_reply_200(ssl, stream, body);
                replied = 1;
            }
        } else if (typ == 0x07) {
            break;
        }
    }

done:
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(cfd);
    close(fd);
    SSL_CTX_free(ctx);
    return 0;
}

/* Client: observe server WINDOW_UPDATE, then GET /. Returns "wu:<inc>;200\\n<body>" or "". */
static inline MakoString mako_tls_h2_window_get(
    MakoString host, int64_t port, MakoString path, MakoString ca_pem_path
) {
    char hbuf[256], pbuf[512], cabuf[512];
    if (host.len >= sizeof(hbuf) || path.len >= sizeof(pbuf) || ca_pem_path.len >= sizeof(cabuf))
        return mako_str_from_cstr("");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    memcpy(cabuf, ca_pem_path.data, ca_pem_path.len);
    cabuf[ca_pem_path.len] = 0;

    SSL_CTX *ctx = NULL;
    int fd = -1;
    SSL *ssl = mako_tls_h2_connect(hbuf, port, cabuf, &ctx, &fd);
    if (!ssl) return mako_str_from_cstr("");

    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    static const unsigned char settings[9] = {0, 0, 0, 0x04, 0x00, 0, 0, 0, 0};
    SSL_write(ssl, preface, (int)(sizeof(preface) - 1));
    SSL_write(ssl, settings, 9);

    /* Client also sends a WINDOW_UPDATE so the peer sees one from us. */
    {
        unsigned char wu[13] = {
            0, 0, 4, 0x08, 0x00,
            0, 0, 0, 0,
            0, 0, 0x01, 0x00 /* increment 256 */
        };
        SSL_write(ssl, wu, 13);
    }

    unsigned char buf[8192];
    int have = 0, off = 0;
    int64_t saw_wu = -1;
    int got_get = 0;
    char body_acc[4096];
    size_t body_len = 0;
    int status_ok = 0;

    unsigned char hblock[256];
    int hblen = mako_tls_h2_build_req_headers(hblock, sizeof(hblock), 0, hbuf, pbuf);
    if (hblen < 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }

    for (int iter = 0; iter < 64; iter++) {
        unsigned char typ = 0, flags = 0;
        uint32_t stream = 0;
        const unsigned char *pay = NULL;
        size_t plen = 0;
        if (!mako_tls_h2_read_frame(
                ssl, buf, &have, &off, (int)sizeof(buf),
                &typ, &flags, &stream, &pay, &plen))
            break;
        if (typ == 0x04) {
            if (!(flags & 0x01)) {
                static const unsigned char sack[9] = {
                    0, 0, 0, 0x04, 0x01, 0, 0, 0, 0
                };
                SSL_write(ssl, sack, 9);
            }
            /* After first SETTINGS from server, fire GET (WU may arrive interleaved). */
            if (!got_get && saw_wu >= 0) {
                mako_tls_h2_write_frame(ssl, 0x01, 0x05, 1, hblock, (size_t)hblen);
                got_get = 1;
            }
        } else if (typ == 0x08 && plen >= 4 && pay) {
            saw_wu = ((int64_t)pay[0] << 24) | ((int64_t)pay[1] << 16)
                | ((int64_t)pay[2] << 8) | (int64_t)pay[3];
            saw_wu &= 0x7fffffff;
            if (!got_get) {
                mako_tls_h2_write_frame(ssl, 0x01, 0x05, 1, hblock, (size_t)hblen);
                got_get = 1;
            }
        } else if (typ == 0x01 && stream == 1) {
            for (size_t i = 0; i < plen; i++) {
                if (pay[i] == 0x88) { status_ok = 1; break; }
            }
            if (flags & 0x01) break;
        } else if (typ == 0x00 && stream == 1) {
            size_t copy = plen;
            if (body_len + copy > sizeof(body_acc)) copy = sizeof(body_acc) - body_len;
            if (copy && pay) {
                memcpy(body_acc + body_len, pay, copy);
                body_len += copy;
            }
            if (flags & 0x01) break;
        } else if (typ == 0x07) {
            break;
        }
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);

    if (saw_wu < 0 || !status_ok) return mako_str_from_cstr("");
    while (body_len > 0 && (body_acc[body_len - 1] == '\n' || body_acc[body_len - 1] == '\r'))
        body_len--;
    char out[4200];
    int wn = snprintf(
        out, sizeof(out), "wu:%lld;200;%.*s",
        (long long)saw_wu, (int)body_len, body_acc
    );
    if (wn <= 0) return mako_str_from_cstr("");
    return mako_str_from_cstr(out);
}

/* ---- Live gRPC-ish unary over TLS+h2 (OpenSSL) ----
 * Speaks enough HTTP/2 + gRPC framing for one unary method using existing
 * mako_grpc_* / HPACK helpers. Not a full gRPC stack. */

/* Build gRPC request HEADERS block: POST + https + path + authority + content-type. */
static inline int mako_tls_grpc_build_headers(
    unsigned char *out, size_t cap,
    const char *host, const char *path
) {
    size_t hlen = strlen(host);
    size_t plen = strlen(path);
    /* 0x83 POST, 0x87 https, path lit, auth lit, content-type lit */
    const char *ctn = "content-type";
    const char *ctv = "application/grpc";
    size_t need = 2 + 1 + 1 + plen + 1 + 1 + hlen + 1 + 1 + 12 + 1 + 16;
    if (need > cap || plen > 127 || hlen > 127) return -1;
    size_t o = 0;
    out[o++] = 0x83; /* :method POST */
    out[o++] = 0x87; /* :scheme https */
    out[o++] = 0x04; /* :path literal */
    out[o++] = (unsigned char)plen;
    memcpy(out + o, path, plen);
    o += plen;
    out[o++] = 0x01; /* :authority */
    out[o++] = (unsigned char)hlen;
    memcpy(out + o, host, hlen);
    o += hlen;
    out[o++] = 0x00; /* literal content-type */
    out[o++] = 12;
    memcpy(out + o, ctn, 12);
    o += 12;
    out[o++] = 16;
    memcpy(out + o, ctv, 16);
    o += 16;
    return (int)o;
}

/* Write bytes already framed as HTTP/2 over TLS. */
static inline int mako_tls_h2_write_buf(SSL *ssl, const char *data, size_t len) {
    if (!data || !len) return 1;
    size_t off = 0;
    while (off < len) {
        int n = SSL_write(ssl, data + off, (int)(len - off));
        if (n <= 0) return 0;
        off += (size_t)n;
    }
    return 1;
}

/* Collect frames on stream until trailer HEADERS with END_STREAM; append to acc. */
static inline int mako_tls_grpc_collect_response(
    SSL *ssl, unsigned char *buf, int *have, int *off, int cap,
    uint32_t want_stream, char *acc, size_t acc_cap, size_t *acc_len
) {
    *acc_len = 0;
    int done = 0;
    for (int iter = 0; iter < 64 && !done; iter++) {
        unsigned char typ = 0, flags = 0;
        uint32_t stream = 0;
        const unsigned char *pay = NULL;
        size_t plen = 0;
        if (!mako_tls_h2_read_frame(ssl, buf, have, off, cap, &typ, &flags, &stream, &pay, &plen))
            break;
        if (typ == 0x04) {
            if (!(flags & 0x01)) {
                static const unsigned char sack[9] = {
                    0, 0, 0, 0x04, 0x01, 0, 0, 0, 0
                };
                SSL_write(ssl, sack, 9);
            }
            continue;
        }
        if (stream != want_stream) continue;
        /* Frame bytes sit just before *off (header 9 + payload). */
        size_t flen = 9 + plen;
        if (*off < (int)flen) return 0;
        int fstart = *off - (int)flen;
        if (*acc_len + flen > acc_cap) return 0;
        memcpy(acc + *acc_len, buf + fstart, flen);
        *acc_len += flen;
        if (typ == 0x01 && (flags & 0x01)) done = 1; /* trailer END_STREAM */
        if (typ == 0x07) break;
    }
    return done;
}

/* One-shot gRPC unary server: echo request name/id with grpc-status 0. */
static inline int64_t mako_tls_serve_grpc_once(
    int64_t port, MakoString cert_path, MakoString key_path
) {
    signal(SIGPIPE, SIG_IGN);
    char cert[512], key[512];
    if (cert_path.len >= sizeof(cert) || key_path.len >= sizeof(key)) return 1;
    memcpy(cert, cert_path.data, cert_path.len);
    cert[cert_path.len] = 0;
    memcpy(key, key_path.data, key_path.len);
    key[key_path.len] = 0;

    SSL_CTX *ctx = mako_tls_make_ctx(cert, key);
    if (!ctx) {
        fprintf(stderr, "error: tls: failed to load cert/key\n");
        ERR_print_errors_fp(stderr);
        return 1;
    }
    SSL_CTX_set_alpn_select_cb(ctx, mako_tls_alpn_select_cb, NULL);

    int fd = mako_http_listen_fd(port);
    if (fd < 0) {
        SSL_CTX_free(ctx);
        return 1;
    }
    fprintf(stderr, "mako tls_grpc_once on :%lld\n", (long long)port);
    int cfd = accept(fd, NULL, NULL);
    if (cfd < 0) {
        close(fd);
        SSL_CTX_free(ctx);
        return 1;
    }
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, cfd);
    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(cfd);
        close(fd);
        SSL_CTX_free(ctx);
        return 1;
    }
    const unsigned char *alpn = NULL;
    unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(ssl, &alpn, &alpn_len);
    if (!(alpn_len == 2 && alpn && alpn[0] == 'h' && alpn[1] == '2')) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(cfd);
        close(fd);
        SSL_CTX_free(ctx);
        return 1;
    }

    unsigned char buf[8192];
    int have = 0, off = 0;
    if (mako_tls_h2_fill(ssl, buf, &have, (int)sizeof(buf), 24) < 24) goto done;
    off = 24;
    static const unsigned char empty_settings[9] = {
        0, 0, 0, 0x04, 0x00, 0, 0, 0, 0
    };
    SSL_write(ssl, empty_settings, 9);

    int settings_acked = 0;
    uint32_t stream_id = 0;
    char data_acc[4096];
    size_t data_len = 0;
    int got_data = 0;

    for (int iter = 0; iter < 48 && !got_data; iter++) {
        unsigned char typ = 0, flags = 0;
        uint32_t stream = 0;
        const unsigned char *pay = NULL;
        size_t plen = 0;
        if (!mako_tls_h2_read_frame(
                ssl, buf, &have, &off, (int)sizeof(buf),
                &typ, &flags, &stream, &pay, &plen))
            break;
        if (typ == 0x04) {
            if (!(flags & 0x01) && !settings_acked) {
                static const unsigned char sack[9] = {
                    0, 0, 0, 0x04, 0x01, 0, 0, 0, 0
                };
                SSL_write(ssl, sack, 9);
                settings_acked = 1;
            }
        } else if (typ == 0x01 && stream != 0 && (stream & 1)) {
            stream_id = stream;
            /* HEADERS may or may not END_STREAM; wait for DATA if not. */
            if (flags & 0x01) {
                /* unusual for gRPC — no body */
                got_data = 1;
            }
        } else if (typ == 0x00 && stream == stream_id) {
            size_t copy = plen;
            if (data_len + copy > sizeof(data_acc)) copy = sizeof(data_acc) - data_len;
            if (copy && pay) {
                memcpy(data_acc + data_len, pay, copy);
                data_len += copy;
            }
            if (flags & 0x01) got_data = 1;
        } else if (typ == 0x07) {
            break;
        }
    }

    if (got_data && stream_id && data_len >= 5) {
        MakoString framed = {(char *)data_acc, data_len};
        MakoString rname = mako_grpc_unary_name(framed);
        int64_t rid = mako_grpc_unary_id(framed);
        MakoString reply_name = mako_str_from_cstr("echo");
        int64_t reply_id = rid > 0 ? rid : 1;
        MakoString resp = mako_grpc_http2_unary_response_status(
            (int64_t)stream_id, reply_name, reply_id, 0
        );
        free(reply_name.data);
        free(rname.data);
        if (resp.data && resp.len) {
            mako_tls_h2_write_buf(ssl, resp.data, resp.len);
        }
        free(resp.data);
    }

done:
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(cfd);
    close(fd);
    SSL_CTX_free(ctx);
    return 0;
}

/* Live gRPC unary client. Returns "ok:<name>:<id>:<status>" or "". */
static inline MakoString mako_tls_grpc_unary(
    MakoString host, int64_t port, MakoString path,
    MakoString name, int64_t id, MakoString ca_pem_path
) {
    char hbuf[256], pbuf[512], cabuf[512], nbuf[256];
    if (host.len >= sizeof(hbuf) || path.len >= sizeof(pbuf)
        || ca_pem_path.len >= sizeof(cabuf) || name.len >= sizeof(nbuf))
        return mako_str_from_cstr("");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    memcpy(cabuf, ca_pem_path.data, ca_pem_path.len);
    cabuf[ca_pem_path.len] = 0;
    memcpy(nbuf, name.data, name.len);
    nbuf[name.len] = 0;

    SSL_CTX *ctx = NULL;
    int fd = -1;
    SSL *ssl = mako_tls_h2_connect(hbuf, port, cabuf, &ctx, &fd);
    if (!ssl) return mako_str_from_cstr("");

    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    static const unsigned char settings[9] = {0, 0, 0, 0x04, 0x00, 0, 0, 0, 0};
    SSL_write(ssl, preface, (int)(sizeof(preface) - 1));
    SSL_write(ssl, settings, 9);

    unsigned char hblock[512];
    int hblen = mako_tls_grpc_build_headers(hblock, sizeof(hblock), hbuf, pbuf);
    if (hblen < 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    mako_tls_h2_write_frame(ssl, 0x01, 0x04, 1, hblock, (size_t)hblen); /* END_HEADERS */

    MakoString body = mako_grpc_unary_request(mako_str_from_cstr(nbuf), id);
    if (!body.data) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    mako_tls_h2_write_frame(ssl, 0x00, 0x01, 1, body.data, body.len); /* DATA END_STREAM */
    free(body.data);

    unsigned char buf[8192];
    int have = 0, off = 0;
    char acc[8192];
    size_t acc_len = 0;
    int got = mako_tls_grpc_collect_response(
        ssl, buf, &have, &off, (int)sizeof(buf), 1, acc, sizeof(acc), &acc_len
    );

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);

    if (!got || acc_len == 0) return mako_str_from_cstr("");
    MakoString wire = {acc, acc_len};
    int64_t st = mako_grpc_http2_response_status(wire);
    MakoString payload = mako_grpc_http2_response_payload(wire);
    MakoString rname = mako_grpc_unary_name(payload);
    int64_t rid = mako_grpc_unary_id(payload);
    free(payload.data);

    char out[512];
    int wn = snprintf(
        out, sizeof(out), "ok:%.*s:%lld:%lld",
        (int)(rname.data ? rname.len : 0),
        rname.data ? rname.data : "",
        (long long)rid,
        (long long)st
    );
    free(rname.data);
    if (wn <= 0 || st < 0) return mako_str_from_cstr("");
    return mako_str_from_cstr(out);
}

/* Parse nth length-prefixed gRPC message from a DATA byte buffer.
 * Returns framed slice into buf (not owned) via out_*; 1 on success. */
static inline int mako_tls_grpc_nth_framed(
    const char *buf, size_t len, int index,
    const char **out_ptr, size_t *out_len
) {
    size_t off = 0;
    int seen = 0;
    while (off + 5 <= len) {
        if ((unsigned char)buf[off] != 0) return 0;
        uint32_t plen = ((uint32_t)(unsigned char)buf[off + 1] << 24)
            | ((uint32_t)(unsigned char)buf[off + 2] << 16)
            | ((uint32_t)(unsigned char)buf[off + 3] << 8)
            | ((uint32_t)(unsigned char)buf[off + 4]);
        if (off + 5 + plen > len) return 0;
        if (seen == index) {
            *out_ptr = buf + off;
            *out_len = 5 + plen;
            return 1;
        }
        seen++;
        off += 5 + plen;
    }
    return 0;
}

/* Count DATA frames on stream in an accumulated wire buffer. */
static inline int mako_tls_grpc_count_data_frames(
    const char *acc, size_t acc_len, uint32_t want_stream
) {
    size_t off = 0;
    int count = 0;
    while (off + 9 <= acc_len) {
        size_t flen = ((size_t)(unsigned char)acc[off] << 16)
            | ((size_t)(unsigned char)acc[off + 1] << 8)
            | (size_t)(unsigned char)acc[off + 2];
        unsigned char typ = (unsigned char)acc[off + 3];
        uint32_t stream = ((uint32_t)(acc[off + 5] & 0x7f) << 24)
            | ((uint32_t)(unsigned char)acc[off + 6] << 16)
            | ((uint32_t)(unsigned char)acc[off + 7] << 8)
            | (uint32_t)(unsigned char)acc[off + 8];
        if (off + 9 + flen > acc_len) break;
        if (typ == 0 && stream == want_stream) count++;
        off += 9 + flen;
    }
    return count;
}

/* Extract name/id from the i-th DATA frame payload on stream in wire buffer. */
static inline int mako_tls_grpc_data_msg_at(
    const char *acc, size_t acc_len, uint32_t want_stream, int index,
    char *name_out, size_t name_cap, int64_t *id_out
) {
    size_t off = 0;
    int seen = 0;
    name_out[0] = 0;
    *id_out = 0;
    while (off + 9 <= acc_len) {
        size_t flen = ((size_t)(unsigned char)acc[off] << 16)
            | ((size_t)(unsigned char)acc[off + 1] << 8)
            | (size_t)(unsigned char)acc[off + 2];
        unsigned char typ = (unsigned char)acc[off + 3];
        uint32_t stream = ((uint32_t)(acc[off + 5] & 0x7f) << 24)
            | ((uint32_t)(unsigned char)acc[off + 6] << 16)
            | ((uint32_t)(unsigned char)acc[off + 7] << 8)
            | (uint32_t)(unsigned char)acc[off + 8];
        if (off + 9 + flen > acc_len) break;
        if (typ == 0 && stream == want_stream) {
            if (seen == index) {
                MakoString framed = {(char *)(acc + off + 9), flen};
                MakoString nm = mako_grpc_unary_name(framed);
                *id_out = mako_grpc_unary_id(framed);
                size_t n = nm.data ? nm.len : 0;
                if (n >= name_cap) n = name_cap - 1;
                if (n && nm.data) memcpy(name_out, nm.data, n);
                name_out[n] = 0;
                free(nm.data);
                return 1;
            }
            seen++;
        }
        off += 9 + flen;
    }
    return 0;
}

/* Build streaming response: HEADERS + DATA + DATA + trailer (no END_STREAM on DATA). */
static inline MakoString mako_tls_grpc_stream_response(
    int64_t stream,
    MakoString name1, int64_t id1,
    MakoString name2, int64_t id2,
    int64_t status
) {
    MakoString ct = mako_grpc_content_type();
    MakoString lit = mako_hpack_encode_literal(mako_str_from_cstr("content-type"), ct);
    free(ct.data);
    MakoString hdrs = mako_http2_headers_frame(stream, lit, 0x4);
    free(lit.data);
    MakoString d1 = mako_grpc_http2_stream_data(stream, name1, id1, 0);
    MakoString d2 = mako_grpc_http2_stream_data(stream, name2, id2, 0);
    char scode[16];
    int sn = snprintf(scode, sizeof(scode), "%lld", (long long)status);
    if (sn < 0 || sn >= (int)sizeof(scode)) {
        free(hdrs.data);
        free(d1.data);
        free(d2.data);
        return (MakoString){NULL, 0};
    }
    MakoString tlit = mako_hpack_encode_literal(
        mako_str_from_cstr("grpc-status"), mako_str_from_cstr(scode)
    );
    MakoString th = mako_http2_headers_frame(stream, tlit, 0x5);
    free(tlit.data);
    MakoString a = mako_http2_concat_frames(hdrs, d1);
    free(hdrs.data);
    free(d1.data);
    MakoString b = mako_http2_concat_frames(a, d2);
    free(a.data);
    free(d2.data);
    MakoString out = mako_http2_concat_frames(b, th);
    free(b.data);
    free(th.data);
    return out;
}

/* One-shot gRPC streaming server: expect 2 client DATA msgs; echo 2 DATA + trailer. */
static inline int64_t mako_tls_serve_grpc_stream(
    int64_t port, MakoString cert_path, MakoString key_path
) {
    signal(SIGPIPE, SIG_IGN);
    char cert[512], key[512];
    if (cert_path.len >= sizeof(cert) || key_path.len >= sizeof(key)) return 1;
    memcpy(cert, cert_path.data, cert_path.len);
    cert[cert_path.len] = 0;
    memcpy(key, key_path.data, key_path.len);
    key[key_path.len] = 0;

    SSL_CTX *ctx = mako_tls_make_ctx(cert, key);
    if (!ctx) {
        fprintf(stderr, "error: tls: failed to load cert/key\n");
        ERR_print_errors_fp(stderr);
        return 1;
    }
    SSL_CTX_set_alpn_select_cb(ctx, mako_tls_alpn_select_cb, NULL);

    int fd = mako_http_listen_fd(port);
    if (fd < 0) {
        SSL_CTX_free(ctx);
        return 1;
    }
    fprintf(stderr, "mako tls_grpc_stream on :%lld\n", (long long)port);
    int cfd = accept(fd, NULL, NULL);
    if (cfd < 0) {
        close(fd);
        SSL_CTX_free(ctx);
        return 1;
    }
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, cfd);
    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(cfd);
        close(fd);
        SSL_CTX_free(ctx);
        return 1;
    }
    const unsigned char *alpn = NULL;
    unsigned int alpn_len = 0;
    SSL_get0_alpn_selected(ssl, &alpn, &alpn_len);
    if (!(alpn_len == 2 && alpn && alpn[0] == 'h' && alpn[1] == '2')) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(cfd);
        close(fd);
        SSL_CTX_free(ctx);
        return 1;
    }

    unsigned char buf[8192];
    int have = 0, off = 0;
    if (mako_tls_h2_fill(ssl, buf, &have, (int)sizeof(buf), 24) < 24) goto done;
    off = 24;
    static const unsigned char empty_settings[9] = {
        0, 0, 0, 0x04, 0x00, 0, 0, 0, 0
    };
    SSL_write(ssl, empty_settings, 9);

    int settings_acked = 0;
    uint32_t stream_id = 0;
    char data_acc[8192];
    size_t data_len = 0;
    int data_frames = 0;
    int client_done = 0;

    for (int iter = 0; iter < 64 && !client_done; iter++) {
        unsigned char typ = 0, flags = 0;
        uint32_t stream = 0;
        const unsigned char *pay = NULL;
        size_t plen = 0;
        if (!mako_tls_h2_read_frame(
                ssl, buf, &have, &off, (int)sizeof(buf),
                &typ, &flags, &stream, &pay, &plen))
            break;
        if (typ == 0x04) {
            if (!(flags & 0x01) && !settings_acked) {
                static const unsigned char sack[9] = {
                    0, 0, 0, 0x04, 0x01, 0, 0, 0, 0
                };
                SSL_write(ssl, sack, 9);
                settings_acked = 1;
            }
        } else if (typ == 0x01 && stream != 0 && (stream & 1)) {
            stream_id = stream;
            if (flags & 0x01) client_done = 1;
        } else if (typ == 0x00 && stream == stream_id) {
            size_t copy = plen;
            if (data_len + copy > sizeof(data_acc)) copy = sizeof(data_acc) - data_len;
            if (copy && pay) {
                memcpy(data_acc + data_len, pay, copy);
                data_len += copy;
            }
            data_frames++;
            if (flags & 0x01) client_done = 1;
        } else if (typ == 0x07) {
            break;
        }
    }

    if (client_done && stream_id && data_frames >= 2 && data_len >= 10) {
        const char *f1 = NULL, *f2 = NULL;
        size_t l1 = 0, l2 = 0;
        if (mako_tls_grpc_nth_framed(data_acc, data_len, 0, &f1, &l1)
            && mako_tls_grpc_nth_framed(data_acc, data_len, 1, &f2, &l2)) {
            MakoString framed1 = {(char *)f1, l1};
            MakoString framed2 = {(char *)f2, l2};
            MakoString n1 = mako_grpc_unary_name(framed1);
            MakoString n2 = mako_grpc_unary_name(framed2);
            int64_t i1 = mako_grpc_unary_id(framed1);
            int64_t i2 = mako_grpc_unary_id(framed2);
            /* Echo both messages back, then grpc-status 0 trailer. */
            MakoString resp = mako_tls_grpc_stream_response(
                (int64_t)stream_id, n1, i1, n2, i2, 0
            );
            free(n1.data);
            free(n2.data);
            if (resp.data && resp.len) {
                mako_tls_h2_write_buf(ssl, resp.data, resp.len);
            }
            free(resp.data);
        }
    }

done:
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(cfd);
    close(fd);
    SSL_CTX_free(ctx);
    return 0;
}

/* Live gRPC client stream: 2 DATA msgs; expect 2 echoed DATA + trailer.
 * Returns "ok:<n1>:<i1>;<n2>:<i2>:<status>" or "". */
static inline MakoString mako_tls_grpc_stream(
    MakoString host, int64_t port, MakoString path,
    MakoString name1, int64_t id1,
    MakoString name2, int64_t id2,
    MakoString ca_pem_path
) {
    char hbuf[256], pbuf[512], cabuf[512], n1buf[256], n2buf[256];
    if (host.len >= sizeof(hbuf) || path.len >= sizeof(pbuf)
        || ca_pem_path.len >= sizeof(cabuf)
        || name1.len >= sizeof(n1buf) || name2.len >= sizeof(n2buf))
        return mako_str_from_cstr("");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    memcpy(cabuf, ca_pem_path.data, ca_pem_path.len);
    cabuf[ca_pem_path.len] = 0;
    memcpy(n1buf, name1.data, name1.len);
    n1buf[name1.len] = 0;
    memcpy(n2buf, name2.data, name2.len);
    n2buf[name2.len] = 0;

    SSL_CTX *ctx = NULL;
    int fd = -1;
    SSL *ssl = mako_tls_h2_connect(hbuf, port, cabuf, &ctx, &fd);
    if (!ssl) return mako_str_from_cstr("");

    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    static const unsigned char settings[9] = {0, 0, 0, 0x04, 0x00, 0, 0, 0, 0};
    SSL_write(ssl, preface, (int)(sizeof(preface) - 1));
    SSL_write(ssl, settings, 9);

    unsigned char hblock[512];
    int hblen = mako_tls_grpc_build_headers(hblock, sizeof(hblock), hbuf, pbuf);
    if (hblen < 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    mako_tls_h2_write_frame(ssl, 0x01, 0x04, 1, hblock, (size_t)hblen);

    MakoString two = mako_grpc_http2_stream_two(
        1, mako_str_from_cstr(n1buf), id1, mako_str_from_cstr(n2buf), id2
    );
    if (!two.data) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    mako_tls_h2_write_buf(ssl, two.data, two.len);
    free(two.data);

    unsigned char buf[8192];
    int have = 0, off = 0;
    char acc[8192];
    size_t acc_len = 0;
    int got = mako_tls_grpc_collect_response(
        ssl, buf, &have, &off, (int)sizeof(buf), 1, acc, sizeof(acc), &acc_len
    );

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);

    if (!got || acc_len == 0) return mako_str_from_cstr("");
    MakoString wire = {acc, acc_len};
    int64_t st = mako_grpc_http2_response_status(wire);
    int dcount = mako_tls_grpc_count_data_frames(acc, acc_len, 1);
    if (dcount < 2 || st < 0) return mako_str_from_cstr("");

    char rn1[128], rn2[128];
    int64_t ri1 = 0, ri2 = 0;
    if (!mako_tls_grpc_data_msg_at(acc, acc_len, 1, 0, rn1, sizeof(rn1), &ri1))
        return mako_str_from_cstr("");
    if (!mako_tls_grpc_data_msg_at(acc, acc_len, 1, 1, rn2, sizeof(rn2), &ri2))
        return mako_str_from_cstr("");

    char out[512];
    int wn = snprintf(
        out, sizeof(out), "ok:%s:%lld;%s:%lld:%lld",
        rn1, (long long)ri1, rn2, (long long)ri2, (long long)st
    );
    if (wn <= 0) return mako_str_from_cstr("");
    return mako_str_from_cstr(out);
}

/* HTTPS POST with PEM trust store. Returns response body or empty. */
static inline MakoString mako_tls_post(
    MakoString host,
    int64_t port,
    MakoString path,
    MakoString body,
    MakoString ca_pem_path
) {
    char hbuf[256], pbuf[512], cabuf[512];
    if (host.len >= sizeof(hbuf) || path.len >= sizeof(pbuf) || ca_pem_path.len >= sizeof(cabuf))
        return mako_str_from_cstr("");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    memcpy(cabuf, ca_pem_path.data, ca_pem_path.len);
    cabuf[ca_pem_path.len] = 0;

    SSL_CTX *ctx = mako_tls_make_client_ctx();
    if (!ctx) return mako_str_from_cstr("");
    if (SSL_CTX_load_verify_locations(ctx, cabuf, NULL) != 1) {
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { SSL_CTX_free(ctx); return mako_str_from_cstr(""); }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, hbuf, &addr.sin_addr) != 1) {
        if (strcmp(hbuf, "localhost") == 0)
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        else {
            close(fd);
            SSL_CTX_free(ctx);
            return mako_str_from_cstr("");
        }
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, hbuf);
    if (SSL_connect(ssl) <= 0 || SSL_get_verify_result(ssl) != X509_V_OK) {
        SSL_free(ssl);
        close(fd);
        SSL_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    char req[768];
    int rn = snprintf(
        req, sizeof(req),
        "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %zu\r\n"
        "Content-Type: text/plain\r\nConnection: close\r\n\r\n",
        pbuf, hbuf, body.len
    );
    if (rn > 0) SSL_write(ssl, req, rn);
    if (body.len && body.data) SSL_write(ssl, body.data, (int)body.len);
    char resp[8192];
    int total = 0;
    for (;;) {
        int n = SSL_read(ssl, resp + total, (int)sizeof(resp) - 1 - total);
        if (n <= 0) break;
        total += n;
        if (total >= (int)sizeof(resp) - 1) break;
    }
    resp[total] = 0;
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(fd);
    SSL_CTX_free(ctx);
    char *b = strstr(resp, "\r\n\r\n");
    if (b) return mako_str_from_cstr(b + 4);
    return mako_str_from_cstr(resp);
}

#else /* !MAKO_TLS_REAL */

/* Socket-style TLS server API — unavailable without a linked TLS backend. */
static inline void *mako_tls_server_new(MakoString cert, MakoString key) {
    (void)cert; (void)key; return NULL;
}
static inline void *mako_tls_server_new_tls13(MakoString cert, MakoString key) {
    (void)cert; (void)key; return NULL;
}
static inline void *mako_tls_accept(void *ctx, int64_t fd) {
    (void)ctx; (void)fd; return NULL;
}
static inline void *mako_tls_accept_start(void *ctx, int64_t fd) {
    (void)ctx; (void)fd; return NULL;
}
static inline int64_t mako_tls_handshake_step(void *conn) { (void)conn; return -1; }
static inline int64_t mako_tls_is_init_finished(void *conn) { (void)conn; return 0; }
static inline int64_t mako_tls_want_read(void *conn) { (void)conn; return 0; }
static inline int64_t mako_tls_want_write(void *conn) { (void)conn; return 0; }
static inline int64_t mako_tls_conn_fd(void *conn) { (void)conn; return -1; }
static inline MakoString mako_tls_read_nb(void *conn, int64_t max) {
    (void)conn; (void)max; return mako_str_from_cstr("");
}
static inline int64_t mako_tls_write_nb(void *conn, MakoString data) {
    (void)conn; (void)data; return -1;
}
static inline MakoString mako_tls_read(void *conn, int64_t max) {
    (void)conn; (void)max; return mako_str_from_cstr("");
}
static inline int64_t mako_tls_write(void *conn, MakoString data) {
    (void)conn; (void)data; return -1;
}
static inline MakoString mako_tls_conn_alpn(void *conn) {
    (void)conn; return mako_str_from_cstr("");
}
static inline int64_t mako_tls_conn_close(void *conn) { (void)conn; return -1; }
static inline int64_t mako_tls_server_free(void *ctx) { (void)ctx; return 0; }
static inline int64_t mako_tls_server_available(void) { return 0; }

static inline MakoString mako_tls_aead_seal(
    MakoString key,
    MakoString nonce,
    MakoString plaintext,
    MakoString aad
) {
    (void)key;
    (void)nonce;
    (void)plaintext;
    (void)aad;
    fprintf(stderr, "mako tls_aead_seal: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline MakoString mako_tls_aead_open(
    MakoString key,
    MakoString nonce,
    MakoString sealed,
    MakoString aad
) {
    (void)key;
    (void)nonce;
    (void)sealed;
    (void)aad;
    fprintf(stderr, "mako tls_aead_open: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline MakoString mako_tls_record_appdata_seal(
    MakoString key, MakoString nonce, MakoString plaintext
) {
    (void)key;
    (void)nonce;
    (void)plaintext;
    fprintf(stderr, "mako tls_record_appdata_seal: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline MakoString mako_tls_record_appdata_open(
    MakoString key, MakoString nonce, MakoString record
) {
    (void)key;
    (void)nonce;
    (void)record;
    fprintf(stderr, "mako tls_record_appdata_open: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline int64_t mako_tls_record_seq_reset(void) { return 0; }
static inline int64_t mako_tls_record_write_seq(void) { return 0; }
static inline int64_t mako_tls_record_read_seq(void) { return 0; }

static inline MakoString mako_tls_record_nonce_from_seq(MakoString iv, int64_t seq) {
    (void)iv;
    (void)seq;
    return mako_str_from_cstr("");
}

static inline MakoString mako_tls_record_appdata_seal_seq(
    MakoString key, MakoString iv, MakoString plaintext
) {
    (void)key;
    (void)iv;
    (void)plaintext;
    fprintf(stderr, "mako tls_record_appdata_seal_seq: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline MakoString mako_tls_record_appdata_open_seq(
    MakoString key, MakoString iv, MakoString record
) {
    (void)key;
    (void)iv;
    (void)record;
    fprintf(stderr, "mako tls_record_appdata_open_seq: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline int64_t mako_tls_serve(
    int64_t port,
    MakoString cert_path,
    MakoString key_path,
    MakoString body
) {
    (void)cert_path;
    (void)key_path;
    (void)body;
    fprintf(stderr,
            "mako tls_serve: OpenSSL not linked (port %lld) — rebuild with OpenSSL\n",
            (long long)port);
    return 1;
}

static inline int64_t mako_tls_serve_once(
    int64_t port,
    MakoString cert_path,
    MakoString key_path,
    MakoString body
) {
    return mako_tls_serve(port, cert_path, key_path, body);
}

static inline int64_t mako_tls_serve_n(
    int64_t port,
    MakoString cert_path,
    MakoString key_path,
    MakoString body,
    int64_t max_reqs
) {
    (void)cert_path;
    (void)key_path;
    (void)body;
    (void)max_reqs;
    fprintf(stderr,
            "mako tls_serve_n: OpenSSL not linked (port %lld) — rebuild with OpenSSL\n",
            (long long)port);
    return 1;
}

static inline int64_t mako_tls_listen_stub(int64_t port) {
    fprintf(stderr,
            "mako tls: stub — OpenSSL not available (port %lld)\n",
            (long long)port);
    return 1;
}

static inline MakoString mako_tls_get_insecure(MakoString host, int64_t port, MakoString path) {
    (void)host;
    (void)port;
    (void)path;
    fprintf(stderr, "mako tls_get_insecure: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline MakoString mako_tls_get(
    MakoString host,
    int64_t port,
    MakoString path,
    MakoString ca_pem_path
) {
    (void)ca_pem_path;
    return mako_tls_get_insecure(host, port, path);
}

static inline MakoString mako_tls_handshake_ok(
    MakoString host, int64_t port, MakoString ca_pem_path
) {
    (void)host;
    (void)port;
    (void)ca_pem_path;
    return mako_str_from_cstr("");
}

static inline MakoString mako_tls_handshake_version(
    MakoString host, int64_t port, MakoString ca_pem_path
) {
    (void)host;
    (void)port;
    (void)ca_pem_path;
    return mako_str_from_cstr("");
}

static inline int64_t mako_tls_serve_once_h2(
    int64_t port, MakoString cert_path, MakoString key_path, MakoString body
) {
    (void)cert_path;
    (void)key_path;
    (void)body;
    fprintf(stderr, "mako tls_serve_once_h2: OpenSSL not linked (port %lld)\n",
            (long long)port);
    return 1;
}

static inline MakoString mako_tls_h2_settings_exchange(
    MakoString host, int64_t port, MakoString ca_pem_path
) {
    (void)host;
    (void)port;
    (void)ca_pem_path;
    return mako_str_from_cstr("");
}

static inline MakoString mako_tls_h2_get(
    MakoString host, int64_t port, MakoString path, MakoString ca_pem_path
) {
    (void)host;
    (void)port;
    (void)path;
    (void)ca_pem_path;
    fprintf(stderr, "mako tls_h2_get: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline MakoString mako_tls_h2_post(
    MakoString host, int64_t port, MakoString path,
    MakoString req_body, MakoString ca_pem_path
) {
    (void)host;
    (void)port;
    (void)path;
    (void)req_body;
    (void)ca_pem_path;
    fprintf(stderr, "mako tls_h2_post: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline MakoString mako_tls_h2_get_twice(
    MakoString host, int64_t port, MakoString path, MakoString ca_pem_path
) {
    (void)host;
    (void)port;
    (void)path;
    (void)ca_pem_path;
    fprintf(stderr, "mako tls_h2_get_twice: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline MakoString mako_tls_h2_mux(
    MakoString host, int64_t port, MakoString ca_pem_path
) {
    (void)host;
    (void)port;
    (void)ca_pem_path;
    fprintf(stderr, "mako tls_h2_mux: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline int64_t mako_tls_serve_h2_wu(
    int64_t port, MakoString cert_path, MakoString key_path,
    MakoString body, int64_t wu_increment
) {
    (void)cert_path;
    (void)key_path;
    (void)body;
    (void)wu_increment;
    fprintf(stderr, "mako tls_serve_h2_wu: OpenSSL not linked (port %lld)\n",
            (long long)port);
    return 1;
}

static inline MakoString mako_tls_h2_window_get(
    MakoString host, int64_t port, MakoString path, MakoString ca_pem_path
) {
    (void)host;
    (void)port;
    (void)path;
    (void)ca_pem_path;
    fprintf(stderr, "mako tls_h2_window_get: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline int64_t mako_tls_serve_grpc_once(
    int64_t port, MakoString cert_path, MakoString key_path
) {
    (void)cert_path;
    (void)key_path;
    fprintf(stderr, "mako tls_serve_grpc_once: OpenSSL not linked (port %lld)\n",
            (long long)port);
    return 1;
}

static inline MakoString mako_tls_grpc_unary(
    MakoString host, int64_t port, MakoString path,
    MakoString name, int64_t id, MakoString ca_pem_path
) {
    (void)host;
    (void)port;
    (void)path;
    (void)name;
    (void)id;
    (void)ca_pem_path;
    fprintf(stderr, "mako tls_grpc_unary: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline int64_t mako_tls_serve_grpc_stream(
    int64_t port, MakoString cert_path, MakoString key_path
) {
    (void)cert_path;
    (void)key_path;
    fprintf(stderr, "mako tls_serve_grpc_stream: OpenSSL not linked (port %lld)\n",
            (long long)port);
    return 1;
}

static inline MakoString mako_tls_grpc_stream(
    MakoString host, int64_t port, MakoString path,
    MakoString name1, int64_t id1,
    MakoString name2, int64_t id2,
    MakoString ca_pem_path
) {
    (void)host;
    (void)port;
    (void)path;
    (void)name1;
    (void)id1;
    (void)name2;
    (void)id2;
    (void)ca_pem_path;
    fprintf(stderr, "mako tls_grpc_stream: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

static inline int64_t mako_tls_serve_h2_n(
    int64_t port, MakoString cert_path, MakoString key_path,
    MakoString body, int64_t max_reqs
) {
    (void)cert_path;
    (void)key_path;
    (void)body;
    (void)max_reqs;
    fprintf(stderr, "mako tls_serve_h2_n: OpenSSL not linked (port %lld)\n",
            (long long)port);
    return 1;
}

static inline int64_t mako_tls_serve_h2_routes(
    int64_t port, MakoString cert_path, MakoString key_path,
    MakoString root_body, MakoString health_body, int64_t max_reqs
) {
    (void)cert_path;
    (void)key_path;
    (void)root_body;
    (void)health_body;
    (void)max_reqs;
    fprintf(stderr, "mako tls_serve_h2_routes: OpenSSL not linked (port %lld)\n",
            (long long)port);
    return 1;
}

static inline MakoString mako_tls_post(
    MakoString host, int64_t port, MakoString path, MakoString body, MakoString ca_pem_path
) {
    (void)host;
    (void)port;
    (void)path;
    (void)body;
    (void)ca_pem_path;
    fprintf(stderr, "mako tls_post: OpenSSL not linked\n");
    return mako_str_from_cstr("");
}

#endif /* MAKO_TLS_REAL */

/* TLS handshake seed (RFC 8446 shape) — not a real handshake.
 * ClientHello: handshake type=1, legacy_version=0x0303, 32-byte random,
 * empty session id, cipher TLS_AES_128_GCM_SHA256 (0x1301), null compression.
 * ServerHello parse: extract 32-byte server random from synthetic buffer. */

static inline MakoString mako_tls_client_hello(MakoString random32) {
    if (!random32.data || random32.len != 32) return mako_str_from_cstr("");
    /* body: ver(2)+random(32)+sid_len(1)+cs_len(2)+cs(2)+comp_len(1)+comp(1) = 41 */
    size_t body = 41;
    size_t total = 4 + body; /* handshake header + body */
    char *d = (char *)malloc(total + 1);
    if (!d) return mako_str_from_cstr("");
    d[0] = 1; /* ClientHello */
    d[1] = (char)((body >> 16) & 0xff);
    d[2] = (char)((body >> 8) & 0xff);
    d[3] = (char)(body & 0xff);
    d[4] = 0x03;
    d[5] = 0x03; /* legacy_version TLS 1.2 */
    memcpy(d + 6, random32.data, 32);
    d[38] = 0; /* session_id length */
    d[39] = 0;
    d[40] = 2; /* cipher_suites length */
    d[41] = 0x13;
    d[42] = 0x01; /* TLS_AES_128_GCM_SHA256 */
    d[43] = 1; /* compression_methods length */
    d[44] = 0; /* null */
    d[total] = 0;
    return (MakoString){d, total};
}

static inline int64_t mako_tls_client_hello_legacy_version(MakoString ch) {
    if (!ch.data || ch.len < 6 || (unsigned char)ch.data[0] != 1) return -1;
    return ((int64_t)(unsigned char)ch.data[4] << 8) | (int64_t)(unsigned char)ch.data[5];
}

static inline MakoString mako_tls_client_hello_random(MakoString ch) {
    if (!ch.data || ch.len < 38 || (unsigned char)ch.data[0] != 1) return mako_str_from_cstr("");
    return mako_str_slice(ch, 6, 38);
}

static inline int64_t mako_tls_client_hello_has_aes128_gcm(MakoString ch) {
    if (!ch.data || ch.len < 45 || (unsigned char)ch.data[0] != 1) return 0;
    return ((unsigned char)ch.data[41] == 0x13 && (unsigned char)ch.data[42] == 0x01) ? 1 : 0;
}

static inline MakoString mako_tls_server_hello(MakoString random32) {
    if (!random32.data || random32.len != 32) return mako_str_from_cstr("");
    size_t body = 38; /* ver(2)+random(32)+sid(1)+cipher(2)+comp(1) */
    size_t total = 4 + body;
    char *d = (char *)malloc(total + 1);
    if (!d) return mako_str_from_cstr("");
    d[0] = 2; /* ServerHello */
    d[1] = (char)((body >> 16) & 0xff);
    d[2] = (char)((body >> 8) & 0xff);
    d[3] = (char)(body & 0xff);
    d[4] = 0x03;
    d[5] = 0x03;
    memcpy(d + 6, random32.data, 32);
    d[38] = 0; /* session id len */
    d[39] = 0x13;
    d[40] = 0x01; /* selected cipher */
    d[41] = 0; /* null compression */
    d[total] = 0;
    return (MakoString){d, total};
}

static inline MakoString mako_tls_server_hello_random(MakoString sh) {
    if (!sh.data || sh.len < 38 || (unsigned char)sh.data[0] != 2) return mako_str_from_cstr("");
    return mako_str_slice(sh, 6, 38);
}

/* Certificate (type=11): one raw DER cert wrapped as TLS 1.3 Certificate message.
 * Body: certificate_request_context_len=0 | cert_list_len(3) | cert_len(3) | DER | ext_len(2)=0.
 * Not a real X.509 parse — wire shape only. */
static inline MakoString mako_tls_certificate(MakoString der) {
    if (!der.data || der.len == 0 || der.len > 0xffffff) return mako_str_from_cstr("");
    size_t entry = 3 + der.len + 2; /* cert_len + DER + extensions */
    size_t body = 1 + 3 + entry; /* context_len + list_len + entry */
    size_t total = 4 + body;
    char *d = (char *)malloc(total + 1);
    if (!d) return mako_str_from_cstr("");
    d[0] = 11; /* Certificate */
    d[1] = (char)((body >> 16) & 0xff);
    d[2] = (char)((body >> 8) & 0xff);
    d[3] = (char)(body & 0xff);
    d[4] = 0; /* empty certificate_request_context */
    d[5] = (char)((entry >> 16) & 0xff);
    d[6] = (char)((entry >> 8) & 0xff);
    d[7] = (char)(entry & 0xff);
    d[8] = (char)((der.len >> 16) & 0xff);
    d[9] = (char)((der.len >> 8) & 0xff);
    d[10] = (char)(der.len & 0xff);
    memcpy(d + 11, der.data, der.len);
    d[11 + der.len] = 0;
    d[12 + der.len] = 0; /* extensions length */
    d[total] = 0;
    return (MakoString){d, total};
}

static inline MakoString mako_tls_certificate_der(MakoString msg) {
    if (!msg.data || msg.len < 13 || (unsigned char)msg.data[0] != 11) return mako_str_from_cstr("");
    size_t der_len = ((size_t)(unsigned char)msg.data[8] << 16)
                   | ((size_t)(unsigned char)msg.data[9] << 8)
                   | (size_t)(unsigned char)msg.data[10];
    if (msg.len < 11 + der_len + 2) return mako_str_from_cstr("");
    return mako_str_slice(msg, 11, (int64_t)(11 + der_len));
}

/* CertificateVerify stub (type=15): scheme(2) + sig_len(2) + signature.
 * Not a real signature — length-prefix wire shape only. */
static inline MakoString mako_tls_certificate_verify(int64_t scheme, MakoString sig) {
    if (!sig.data || sig.len > 0xffff || scheme < 0 || scheme > 0xffff)
        return mako_str_from_cstr("");
    size_t body = 2 + 2 + sig.len;
    size_t total = 4 + body;
    char *d = (char *)malloc(total + 1);
    if (!d) return mako_str_from_cstr("");
    d[0] = 15; /* CertificateVerify */
    d[1] = (char)((body >> 16) & 0xff);
    d[2] = (char)((body >> 8) & 0xff);
    d[3] = (char)(body & 0xff);
    d[4] = (char)((scheme >> 8) & 0xff);
    d[5] = (char)(scheme & 0xff);
    d[6] = (char)((sig.len >> 8) & 0xff);
    d[7] = (char)(sig.len & 0xff);
    memcpy(d + 8, sig.data, sig.len);
    d[total] = 0;
    return (MakoString){d, total};
}

static inline int64_t mako_tls_certificate_verify_scheme(MakoString msg) {
    if (!msg.data || msg.len < 8 || (unsigned char)msg.data[0] != 15) return -1;
    return ((int64_t)(unsigned char)msg.data[4] << 8) | (int64_t)(unsigned char)msg.data[5];
}

static inline MakoString mako_tls_certificate_verify_sig(MakoString msg) {
    if (!msg.data || msg.len < 8 || (unsigned char)msg.data[0] != 15) return mako_str_from_cstr("");
    size_t slen = ((size_t)(unsigned char)msg.data[6] << 8) | (size_t)(unsigned char)msg.data[7];
    if (msg.len < 8 + slen) return mako_str_from_cstr("");
    return mako_str_slice(msg, 8, (int64_t)(8 + slen));
}

/* EncryptedExtensions stub (type=8): empty extensions list. */
static inline MakoString mako_tls_encrypted_extensions(void) {
    size_t body = 2; /* extensions_length = 0 */
    size_t total = 4 + body;
    char *d = (char *)malloc(total + 1);
    if (!d) return mako_str_from_cstr("");
    d[0] = 8;
    d[1] = 0;
    d[2] = 0;
    d[3] = 2;
    d[4] = 0;
    d[5] = 0;
    d[total] = 0;
    return (MakoString){d, total};
}

/* Finished handshake message (type=20): verify_data bytes. */
static inline MakoString mako_tls_finished(MakoString verify_data) {
    if (!verify_data.data || verify_data.len == 0 || verify_data.len > 0xffffff)
        return mako_str_from_cstr("");
    size_t body = verify_data.len;
    size_t total = 4 + body;
    char *d = (char *)malloc(total + 1);
    if (!d) return mako_str_from_cstr("");
    d[0] = 20;
    d[1] = (char)((body >> 16) & 0xff);
    d[2] = (char)((body >> 8) & 0xff);
    d[3] = (char)(body & 0xff);
    memcpy(d + 4, verify_data.data, body);
    d[total] = 0;
    return (MakoString){d, total};
}

static inline int64_t mako_tls_hs_msg_type(MakoString msg) {
    if (!msg.data || msg.len < 1) return -1;
    return (int64_t)(unsigned char)msg.data[0];
}

/* Client 1-RTT handshake state seed (not a live TLS stack).
 * 0 start, 1 wait_sh, 2 wait_ee, 3 wait_cert, 4 wait_cv, 5 wait_finished, 6 app.
 * Advance by feeding handshake messages in order (CH→SH→EE→Cert→CV→Finished). */
static int mako_tls_hs_state = 0;

static inline int64_t mako_tls_hs_reset(void) {
    mako_tls_hs_state = 0;
    return 0;
}
static inline int64_t mako_tls_hs_state_get(void) {
    return (int64_t)mako_tls_hs_state;
}
/* Advance SM with one handshake message. Returns new state, or -1 if unexpected. */
static inline int64_t mako_tls_hs_advance(MakoString msg) {
    int64_t t = mako_tls_hs_msg_type(msg);
    if (t < 0) return -1;
    switch (mako_tls_hs_state) {
    case 0: /* start: ClientHello → wait_sh */
        if (t != 1) return -1;
        mako_tls_hs_state = 1;
        break;
    case 1: /* wait_sh: ServerHello → wait_ee */
        if (t != 2) return -1;
        mako_tls_hs_state = 2;
        break;
    case 2: /* wait_ee: EncryptedExtensions → wait_cert */
        if (t != 8) return -1;
        mako_tls_hs_state = 3;
        break;
    case 3: /* wait_cert: Certificate → wait_cv */
        if (t != 11) return -1;
        mako_tls_hs_state = 4;
        break;
    case 4: /* wait_cv: CertificateVerify → wait_finished */
        if (t != 15) return -1;
        mako_tls_hs_state = 5;
        break;
    case 5: /* wait_finished: Finished → app */
        if (t != 20) return -1;
        mako_tls_hs_state = 6;
        break;
    default:
        return -1; /* already app or unknown */
    }
    return (int64_t)mako_tls_hs_state;
}
static inline int64_t mako_tls_hs_is_app(void) {
    return mako_tls_hs_state == 6 ? 1 : 0;
}

/* Finished verify_data seed: HMAC-SHA256(base_key, SHA256(transcript)).
 * Returns 32-byte binary (TLS 1.3 Hash.length for SHA-256). Not a live Finished. */
static inline MakoString mako_tls_finished_verify_data(MakoString base_key, MakoString transcript) {
    if (!base_key.data || base_key.len == 0) return mako_str_from_cstr("");
    MakoString th = mako_sha256_raw(transcript);
    if (!th.data || th.len != 32) { free(th.data); return mako_str_from_cstr(""); }
    MakoString vd = mako_hmac_sha256_raw(base_key, th);
    free(th.data);
    return vd;
}

static inline MakoString mako_tls_finished_verify_data_hex(MakoString base_key, MakoString transcript) {
    MakoString vd = mako_tls_finished_verify_data(base_key, transcript);
    if (!vd.data || vd.len == 0) return mako_str_from_cstr("");
    char *out = (char *)malloc(vd.len * 2 + 1);
    if (!out) { free(vd.data); return mako_str_from_cstr(""); }
    for (size_t i = 0; i < vd.len; i++)
        sprintf(out + i * 2, "%02x", (unsigned char)vd.data[i]);
    out[vd.len * 2] = 0;
    size_t n = vd.len * 2;
    free(vd.data);
    return (MakoString){out, n};
}

/* Handshake transcript accumulator seed (process-global).
 * Append ClientHello||ServerHello||… then feed Finished verify_data. */
#define MAKO_TLS_TX_MAX (64 * 1024)
static char mako_tls_tx_buf[MAKO_TLS_TX_MAX];
static size_t mako_tls_tx_len = 0;

static inline int64_t mako_tls_transcript_reset(void) {
    mako_tls_tx_len = 0;
    return 0;
}

static inline int64_t mako_tls_transcript_append(MakoString msg) {
    if (!msg.data || msg.len == 0) return 0;
    if (mako_tls_tx_len + msg.len > MAKO_TLS_TX_MAX) return -1;
    memcpy(mako_tls_tx_buf + mako_tls_tx_len, msg.data, msg.len);
    mako_tls_tx_len += msg.len;
    return (int64_t)mako_tls_tx_len;
}

static inline int64_t mako_tls_transcript_len(void) {
    return (int64_t)mako_tls_tx_len;
}

static inline MakoString mako_tls_transcript_bytes(void) {
    if (mako_tls_tx_len == 0) return mako_str_from_cstr("");
    char *d = (char *)malloc(mako_tls_tx_len + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, mako_tls_tx_buf, mako_tls_tx_len);
    d[mako_tls_tx_len] = 0;
    return (MakoString){d, mako_tls_tx_len};
}

static inline MakoString mako_tls_transcript_finished_hex(MakoString base_key) {
    MakoString tx = mako_tls_transcript_bytes();
    MakoString out = mako_tls_finished_verify_data_hex(base_key, tx);
    free(tx.data);
    return out;
}

/* Session helper: reset HS SM + transcript together. */
static inline int64_t mako_tls_hs_session_reset(void) {
    mako_tls_hs_reset();
    mako_tls_transcript_reset();
    return 0;
}

/* Wire a handshake message: append to transcript, then advance SM.
 * On SM reject, transcript is not rolled back (caller should reset session).
 * Returns new state, or -1 if unexpected. */
static inline int64_t mako_tls_hs_session_feed(MakoString msg) {
    if (!msg.data || msg.len == 0) return -1;
    if (mako_tls_transcript_append(msg) < 0) return -1;
    return mako_tls_hs_advance(msg);
}

/* Convenience: build ClientHello from random, feed session. */
static inline int64_t mako_tls_hs_session_client_hello(MakoString random32) {
    MakoString ch = mako_tls_client_hello(random32);
    if (!ch.data || ch.len == 0) return -1;
    int64_t st = mako_tls_hs_session_feed(ch);
    free(ch.data);
    return st;
}

/* Convenience: build ServerHello from random, feed session. */
static inline int64_t mako_tls_hs_session_server_hello(MakoString random32) {
    MakoString sh = mako_tls_server_hello(random32);
    if (!sh.data || sh.len == 0) return -1;
    int64_t st = mako_tls_hs_session_feed(sh);
    free(sh.data);
    return st;
}

/* Finished verify_data over the session transcript (after CH||SH||…). */
static inline MakoString mako_tls_hs_session_finished_hex(MakoString base_key) {
    return mako_tls_transcript_finished_hex(base_key);
}

/* Synthetic EE / Cert / CertVerify / Finished feed helpers (build + session_feed). */
static inline int64_t mako_tls_hs_session_encrypted_extensions(void) {
    MakoString ee = mako_tls_encrypted_extensions();
    if (!ee.data || ee.len == 0) return -1;
    int64_t st = mako_tls_hs_session_feed(ee);
    free(ee.data);
    return st;
}

static inline int64_t mako_tls_hs_session_certificate(MakoString der) {
    MakoString cert = mako_tls_certificate(der);
    if (!cert.data || cert.len == 0) return -1;
    int64_t st = mako_tls_hs_session_feed(cert);
    free(cert.data);
    return st;
}

static inline int64_t mako_tls_hs_session_certificate_verify(int64_t scheme, MakoString sig) {
    MakoString cv = mako_tls_certificate_verify(scheme, sig);
    if (!cv.data || cv.len == 0) return -1;
    int64_t st = mako_tls_hs_session_feed(cv);
    free(cv.data);
    return st;
}

/* Finished: if verify_data empty/null, derive from session transcript + base_key. */
static inline int64_t mako_tls_hs_session_finished(MakoString base_key, MakoString verify_data) {
    MakoString vd;
    int owned = 0;
    if (!verify_data.data || verify_data.len == 0) {
        MakoString tx = mako_tls_transcript_bytes();
        vd = mako_tls_finished_verify_data(base_key, tx);
        free(tx.data);
        owned = 1;
        if (!vd.data || vd.len == 0) return -1;
    } else {
        vd = verify_data;
    }
    MakoString fin = mako_tls_finished(vd);
    if (owned) free(vd.data);
    if (!fin.data || fin.len == 0) return -1;
    int64_t st = mako_tls_hs_session_feed(fin);
    free(fin.data);
    return st;
}

/* QUIC Initial payload AEAD (RFC 9001 §5.3) — payload only.
 * AAD is the unprotected header bytes supplied by the caller.
 * protect: ciphertext||tag; unprotect: plaintext or empty on auth fail. */
static inline MakoString mako_quic_initial_protect(
    MakoString dcid, int64_t packet_number, MakoString aad, MakoString plaintext
) {
    MakoString key = mako_quic_initial_client_key(dcid);
    MakoString iv = mako_quic_initial_client_iv(dcid);
    if (!key.data || key.len != 16 || !iv.data || iv.len != 12) {
        free(key.data);
        free(iv.data);
        return mako_str_from_cstr("");
    }
    MakoString nonce = mako_quic_aead_nonce(iv, packet_number);
    free(iv.data);
    if (!nonce.data || nonce.len != 12) {
        free(key.data);
        free(nonce.data);
        return mako_str_from_cstr("");
    }
    MakoString sealed = mako_tls_aead_seal(key, nonce, plaintext, aad);
    free(key.data);
    free(nonce.data);
    return sealed;
}

static inline MakoString mako_quic_initial_unprotect(
    MakoString dcid, int64_t packet_number, MakoString aad, MakoString sealed
) {
    MakoString key = mako_quic_initial_client_key(dcid);
    MakoString iv = mako_quic_initial_client_iv(dcid);
    if (!key.data || key.len != 16 || !iv.data || iv.len != 12) {
        free(key.data);
        free(iv.data);
        return mako_str_from_cstr("");
    }
    MakoString nonce = mako_quic_aead_nonce(iv, packet_number);
    free(iv.data);
    if (!nonce.data || nonce.len != 12) {
        free(key.data);
        free(nonce.data);
        return mako_str_from_cstr("");
    }
    MakoString pt = mako_tls_aead_open(key, nonce, sealed, aad);
    free(key.data);
    free(nonce.data);
    return pt;
}

/* AES-ECB header protection (RFC 9001 §5.4.3): mask = AES-ECB(hp_key, sample)[0..4].
 * sample must be 16 bytes; returns 5-byte mask. */
static inline MakoString mako_quic_header_protection_mask(MakoString hp_key, MakoString sample) {
#ifdef MAKO_TLS_REAL
    if (!hp_key.data || hp_key.len != 16 || !sample.data || sample.len != 16)
        return mako_str_from_cstr("");
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return mako_str_from_cstr("");
    unsigned char out[16];
    int len = 0, outlen = 0;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL,
                           (const unsigned char *)hp_key.data, NULL) != 1
        || EVP_CIPHER_CTX_set_padding(ctx, 0) != 1
        || EVP_EncryptUpdate(ctx, out, &len, (const unsigned char *)sample.data, 16) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    outlen = len;
    if (EVP_EncryptFinal_ex(ctx, out + outlen, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return mako_str_from_cstr("");
    }
    EVP_CIPHER_CTX_free(ctx);
    char *d = (char *)malloc(6);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, out, 5);
    d[5] = 0;
    return (MakoString){d, 5};
#else
    (void)hp_key;
    (void)sample;
    fprintf(stderr, "mako quic_header_protection_mask: OpenSSL not linked\n");
    return mako_str_from_cstr("");
#endif
}

static inline MakoString mako_quic_header_protection_mask_hex(MakoString hp_key, MakoString sample) {
    MakoString m = mako_quic_header_protection_mask(hp_key, sample);
    if (!m.data || m.len == 0) return mako_str_from_cstr("");
    char *out = (char *)malloc(m.len * 2 + 1);
    if (!out) { free(m.data); return mako_str_from_cstr(""); }
    for (size_t i = 0; i < m.len; i++)
        sprintf(out + i * 2, "%02x", (unsigned char)m.data[i]);
    out[m.len * 2] = 0;
    size_t n = m.len * 2;
    free(m.data);
    return (MakoString){out, n};
}

/* Derive Initial HP key from DCID then AES-ECB mask (A.2 sample path). */
static inline MakoString mako_quic_initial_hp_mask(MakoString dcid, MakoString sample) {
    MakoString hp = mako_quic_initial_client_hp(dcid);
    MakoString mask = mako_quic_header_protection_mask(hp, sample);
    free(hp.data);
    return mask;
}

static inline MakoString mako_quic_initial_hp_mask_hex(MakoString dcid, MakoString sample) {
    MakoString m = mako_quic_initial_hp_mask(dcid, sample);
    if (!m.data || m.len == 0) return mako_str_from_cstr("");
    char *out = (char *)malloc(m.len * 2 + 1);
    if (!out) { free(m.data); return mako_str_from_cstr(""); }
    for (size_t i = 0; i < m.len; i++)
        sprintf(out + i * 2, "%02x", (unsigned char)m.data[i]);
    out[m.len * 2] = 0;
    size_t n = m.len * 2;
    free(m.data);
    return (MakoString){out, n};
}

/* Apply header protection (RFC 9001 §5.4.1 protect path).
 * pn_length taken from unprotected first byte before masking. */
static inline MakoString mako_quic_header_protect_apply(
    MakoString header, int64_t pn_offset, MakoString mask
) {
    if (!header.data || header.len == 0 || !mask.data || mask.len < 5) return mako_str_from_cstr("");
    if (pn_offset < 1 || (size_t)pn_offset >= header.len) return mako_str_from_cstr("");
    char *d = (char *)malloc(header.len + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, header.data, header.len);
    d[header.len] = 0;
    unsigned char *p = (unsigned char *)d;
    int long_hdr = (p[0] & 0x80) != 0;
    int pn_len = (p[0] & 0x03) + 1;
    if ((size_t)pn_offset + (size_t)pn_len > header.len) {
        free(d);
        return mako_str_from_cstr("");
    }
    if (long_hdr)
        p[0] ^= (unsigned char)mask.data[0] & 0x0f;
    else
        p[0] ^= (unsigned char)mask.data[0] & 0x1f;
    for (int i = 0; i < pn_len; i++)
        p[pn_offset + i] ^= (unsigned char)mask.data[1 + i];
    return (MakoString){d, header.len};
}

/* Remove header protection (unprotect path): unmask first byte, then PN. */
static inline MakoString mako_quic_header_protect_remove(
    MakoString header, int64_t pn_offset, MakoString mask
) {
    if (!header.data || header.len == 0 || !mask.data || mask.len < 5) return mako_str_from_cstr("");
    if (pn_offset < 1 || (size_t)pn_offset >= header.len) return mako_str_from_cstr("");
    char *d = (char *)malloc(header.len + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, header.data, header.len);
    d[header.len] = 0;
    unsigned char *p = (unsigned char *)d;
    int long_hdr = (p[0] & 0x80) != 0;
    if (long_hdr)
        p[0] ^= (unsigned char)mask.data[0] & 0x0f;
    else
        p[0] ^= (unsigned char)mask.data[0] & 0x1f;
    int pn_len = (p[0] & 0x03) + 1;
    if ((size_t)pn_offset + (size_t)pn_len > header.len) {
        free(d);
        return mako_str_from_cstr("");
    }
    for (int i = 0; i < pn_len; i++)
        p[pn_offset + i] ^= (unsigned char)mask.data[1 + i];
    return (MakoString){d, header.len};
}

/* Minimal protected Initial packet: unprotected_header || AEAD(payload) with HP applied.
 * Requires: unprotected header ends with PN; pn_offset = start of PN field;
 * payload len + tag >= 16 so sample = first 16 ciphertext bytes (4-byte PN case).
 * Roundtrip seed — not full QUIC wire parser / CRYPTO frame decode. */
static inline MakoString mako_quic_initial_packet_protect(
    MakoString dcid, int64_t packet_number, MakoString unprotected_header,
    int64_t pn_offset, MakoString plaintext
) {
    if (!unprotected_header.data || unprotected_header.len == 0
        || !plaintext.data || plaintext.len == 0
        || pn_offset < 1 || (size_t)pn_offset >= unprotected_header.len)
        return mako_str_from_cstr("");
    MakoString sealed = mako_quic_initial_protect(
        dcid, packet_number, unprotected_header, plaintext
    );
    if (!sealed.data || sealed.len < 16) {
        free(sealed.data);
        return mako_str_from_cstr("");
    }
    MakoString sample = mako_str_slice(sealed, 0, 16);
    MakoString mask = mako_quic_initial_hp_mask(dcid, sample);
    free(sample.data);
    if (!mask.data || mask.len < 5) {
        free(sealed.data);
        free(mask.data);
        return mako_str_from_cstr("");
    }
    MakoString prot_hdr = mako_quic_header_protect_apply(
        unprotected_header, pn_offset, mask
    );
    free(mask.data);
    if (!prot_hdr.data || prot_hdr.len == 0) {
        free(sealed.data);
        free(prot_hdr.data);
        return mako_str_from_cstr("");
    }
    size_t total = prot_hdr.len + sealed.len;
    char *out = (char *)malloc(total + 1);
    if (!out) {
        free(prot_hdr.data);
        free(sealed.data);
        return mako_str_from_cstr("");
    }
    memcpy(out, prot_hdr.data, prot_hdr.len);
    memcpy(out + prot_hdr.len, sealed.data, sealed.len);
    out[total] = 0;
    free(prot_hdr.data);
    free(sealed.data);
    return (MakoString){out, total};
}

/* Decode protected Initial: remove HP, AEAD-open. Returns plaintext or empty.
 * header_len = length of long header including PN field (before ciphertext). */
static inline MakoString mako_quic_initial_packet_unprotect(
    MakoString dcid, MakoString packet, int64_t header_len, int64_t pn_offset
) {
    if (!packet.data || packet.len == 0 || header_len < 1
        || (size_t)header_len >= packet.len
        || pn_offset < 1 || pn_offset >= header_len)
        return mako_str_from_cstr("");
    size_t ct_len = packet.len - (size_t)header_len;
    if (ct_len < 16) return mako_str_from_cstr("");
    MakoString sample = mako_str_slice(packet, header_len, header_len + 16);
    MakoString mask = mako_quic_initial_hp_mask(dcid, sample);
    free(sample.data);
    if (!mask.data || mask.len < 5) {
        free(mask.data);
        return mako_str_from_cstr("");
    }
    MakoString prot_hdr = mako_str_slice(packet, 0, header_len);
    MakoString unprot_hdr = mako_quic_header_protect_remove(prot_hdr, pn_offset, mask);
    free(prot_hdr.data);
    free(mask.data);
    if (!unprot_hdr.data || unprot_hdr.len == 0) {
        free(unprot_hdr.data);
        return mako_str_from_cstr("");
    }
    /* Recover packet number from unprotected header (big-endian, pn_len bytes). */
    unsigned char *h = (unsigned char *)unprot_hdr.data;
    int pn_len = (h[0] & 0x03) + 1;
    if ((size_t)pn_offset + (size_t)pn_len > unprot_hdr.len) {
        free(unprot_hdr.data);
        return mako_str_from_cstr("");
    }
    int64_t pn = 0;
    for (int i = 0; i < pn_len; i++)
        pn = (pn << 8) | (int64_t)h[pn_offset + i];
    MakoString sealed = mako_str_slice(packet, header_len, (int64_t)packet.len);
    MakoString pt = mako_quic_initial_unprotect(dcid, pn, unprot_hdr, sealed);
    free(unprot_hdr.data);
    free(sealed.data);
    return pt;
}

/* TLS 1.3 HKDF-Expand-Label with optional context (RFC 8446).
 * Differs from quic_* helper which always uses empty context. */
static inline MakoString mako_tls_hkdf_expand_label(
    MakoString secret, MakoString label, MakoString context, int64_t out_len
) {
    if (!secret.data || secret.len == 0 || !label.data || out_len <= 0 || out_len > 32)
        return mako_str_from_cstr("");
    size_t ctx_n = (context.data && context.len > 0) ? context.len : 0;
    size_t prefix = 6;
    size_t lab_n = prefix + label.len;
    if (lab_n > 255 || ctx_n > 255) return mako_str_from_cstr("");
    size_t hklen = 2 + 1 + lab_n + 1 + ctx_n;
    char *hk = (char *)malloc(hklen);
    if (!hk) return mako_str_from_cstr("");
    hk[0] = (char)((out_len >> 8) & 0xff);
    hk[1] = (char)(out_len & 0xff);
    hk[2] = (char)lab_n;
    memcpy(hk + 3, "tls13 ", 6);
    memcpy(hk + 3 + 6, label.data, label.len);
    hk[3 + lab_n] = (char)ctx_n;
    if (ctx_n > 0) memcpy(hk + 3 + lab_n + 1, context.data, ctx_n);
    char *info1 = (char *)malloc(hklen + 1);
    if (!info1) { free(hk); return mako_str_from_cstr(""); }
    memcpy(info1, hk, hklen);
    info1[hklen] = 1;
    free(hk);
    MakoString msg = {info1, hklen + 1};
    MakoString okm = mako_hmac_sha256_raw(secret, msg);
    free(info1);
    if (!okm.data || okm.len < (size_t)out_len) {
        free(okm.data);
        return mako_str_from_cstr("");
    }
    if (okm.len == (size_t)out_len) return okm;
    MakoString clipped = mako_str_slice(okm, 0, out_len);
    free(okm.data);
    return clipped;
}

/* Derive-Secret(Secret, Label, Messages) = Expand-Label(Secret, Label, Hash(Messages), HashLen).
 * Demo seed — not a live TLS key schedule. */
static inline MakoString mako_tls_derive_secret(
    MakoString secret, MakoString label, MakoString transcript
) {
    MakoString th = mako_sha256_raw(transcript);
    if (!th.data || th.len != 32) { free(th.data); return mako_str_from_cstr(""); }
    MakoString out = mako_tls_hkdf_expand_label(secret, label, th, 32);
    free(th.data);
    return out;
}

static inline MakoString mako_tls_derive_secret_hex(
    MakoString secret, MakoString label, MakoString transcript
) {
    MakoString raw = mako_tls_derive_secret(secret, label, transcript);
    if (!raw.data || raw.len == 0) return mako_str_from_cstr("");
    char *out = (char *)malloc(raw.len * 2 + 1);
    if (!out) { free(raw.data); return mako_str_from_cstr(""); }
    for (size_t i = 0; i < raw.len; i++)
        sprintf(out + i * 2, "%02x", (unsigned char)raw.data[i]);
    out[raw.len * 2] = 0;
    size_t n = raw.len * 2;
    free(raw.data);
    return (MakoString){out, n};
}

/* Traffic secrets from handshake secret (RFC 8446 labels). */
static inline MakoString mako_tls_client_handshake_traffic_secret(
    MakoString handshake_secret, MakoString transcript
) {
    MakoString lab = mako_str_from_cstr("c hs traffic");
    MakoString out = mako_tls_derive_secret(handshake_secret, lab, transcript);
    free(lab.data);
    return out;
}

static inline MakoString mako_tls_server_handshake_traffic_secret(
    MakoString handshake_secret, MakoString transcript
) {
    MakoString lab = mako_str_from_cstr("s hs traffic");
    MakoString out = mako_tls_derive_secret(handshake_secret, lab, transcript);
    free(lab.data);
    return out;
}

static inline MakoString mako_tls_client_handshake_traffic_secret_hex(
    MakoString handshake_secret, MakoString transcript
) {
    MakoString raw = mako_tls_client_handshake_traffic_secret(handshake_secret, transcript);
    if (!raw.data || raw.len == 0) return mako_str_from_cstr("");
    char *out = (char *)malloc(raw.len * 2 + 1);
    if (!out) { free(raw.data); return mako_str_from_cstr(""); }
    for (size_t i = 0; i < raw.len; i++)
        sprintf(out + i * 2, "%02x", (unsigned char)raw.data[i]);
    out[raw.len * 2] = 0;
    size_t n = raw.len * 2;
    free(raw.data);
    return (MakoString){out, n};
}

static inline MakoString mako_tls_server_handshake_traffic_secret_hex(
    MakoString handshake_secret, MakoString transcript
) {
    MakoString raw = mako_tls_server_handshake_traffic_secret(handshake_secret, transcript);
    if (!raw.data || raw.len == 0) return mako_str_from_cstr("");
    char *out = (char *)malloc(raw.len * 2 + 1);
    if (!out) { free(raw.data); return mako_str_from_cstr(""); }
    for (size_t i = 0; i < raw.len; i++)
        sprintf(out + i * 2, "%02x", (unsigned char)raw.data[i]);
    out[raw.len * 2] = 0;
    size_t n = raw.len * 2;
    free(raw.data);
    return (MakoString){out, n};
}

/* Application traffic secrets from master secret placeholder (RFC 8446 labels).
 * Demo only — not a live TLS key schedule / exporter. */
static inline MakoString mako_tls_client_application_traffic_secret(
    MakoString master_secret, MakoString transcript
) {
    MakoString lab = mako_str_from_cstr("c ap traffic");
    MakoString out = mako_tls_derive_secret(master_secret, lab, transcript);
    free(lab.data);
    return out;
}

static inline MakoString mako_tls_server_application_traffic_secret(
    MakoString master_secret, MakoString transcript
) {
    MakoString lab = mako_str_from_cstr("s ap traffic");
    MakoString out = mako_tls_derive_secret(master_secret, lab, transcript);
    free(lab.data);
    return out;
}

static inline MakoString mako_tls_client_application_traffic_secret_hex(
    MakoString master_secret, MakoString transcript
) {
    MakoString raw = mako_tls_client_application_traffic_secret(master_secret, transcript);
    if (!raw.data || raw.len == 0) return mako_str_from_cstr("");
    char *out = (char *)malloc(raw.len * 2 + 1);
    if (!out) { free(raw.data); return mako_str_from_cstr(""); }
    for (size_t i = 0; i < raw.len; i++)
        sprintf(out + i * 2, "%02x", (unsigned char)raw.data[i]);
    out[raw.len * 2] = 0;
    size_t n = raw.len * 2;
    free(raw.data);
    return (MakoString){out, n};
}

static inline MakoString mako_tls_server_application_traffic_secret_hex(
    MakoString master_secret, MakoString transcript
) {
    MakoString raw = mako_tls_server_application_traffic_secret(master_secret, transcript);
    if (!raw.data || raw.len == 0) return mako_str_from_cstr("");
    char *out = (char *)malloc(raw.len * 2 + 1);
    if (!out) { free(raw.data); return mako_str_from_cstr(""); }
    for (size_t i = 0; i < raw.len; i++)
        sprintf(out + i * 2, "%02x", (unsigned char)raw.data[i]);
    out[raw.len * 2] = 0;
    size_t n = raw.len * 2;
    free(raw.data);
    return (MakoString){out, n};
}

static inline int64_t mako_quic_stub(int64_t port) {
    fprintf(stderr, "mako quic: stub (port %lld)\n", (long long)port);
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_TLS_H */
