/* DTLS 1.2 over UDP + SRTP key export + cert fingerprints.
 *
 * WebRTC-style DTLS-SRTP building block: blocking handshake over a datagram
 * socket (retransmit timers driven internally), SHA-256 cert fingerprints
 * (SDP `a=fingerprint`), and RFC 5764 keying-material export for SRTP.
 *
 * Mirrors mako_tls.h conventions: static inline everywhere, handles are
 * opaque `void*` so the API compiles when OpenSSL is not linked (stubs fail
 * clearly with NULL / -1 / "" and mako_dtls_available() == 0), per-connection
 * state lives in the handle structs (no file-scope statics), diagnostics go
 * to stderr via ERR_print_errors_fp, and mako_abort is reserved for OOM.
 *
 * v1 limitations (documented follow-ups):
 *   - DTLS 1.2 only (no 1.3), PEM-path certs (RSA 2048 dev certs work fine).
 *   - One peer owns the UDP socket: after accept the socket is connect()ed to
 *     that peer (kernel filters other senders). Multi-peer demux on one
 *     socket (real ICE server) is a follow-up.
 *   - POSIX only: the real implementation is compiled out on _WIN32 and
 *     mako_dtls_available() reports 0 there.
 */
#ifndef MAKO_DTLS_H
#define MAKO_DTLS_H

#include "mako_net.h"
#include <time.h>

/* Auto-enable when the compiler is given OpenSSL includes (mako build probes). */
#if defined(MAKO_HAS_OPENSSL) || defined(MAKO_USE_OPENSSL)
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/srtp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#define MAKO_DTLS_REAL 1
#endif

/* The real implementation is POSIX-only for v1 (select/fcntl/UDP connect). */
#if defined(MAKO_DTLS_REAL) && defined(_WIN32)
#undef MAKO_DTLS_REAL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MAKO_DTLS_REAL

/* SRTP master key + salt lengths per negotiated profile (RFC 5764 §4.1.2).
 * Exported material is 2 * (key + salt) bytes:
 *   SRTP_AES128_CM_SHA1_80/32: 16 + 14 → 60 bytes
 *   SRTP_AEAD_AES_256_GCM:     32 + 12 → 88 bytes
 *   SRTP_AEAD_AES_128_GCM:     16 + 12 → 56 bytes
 * Layout: client_key | client_salt | server_key | server_salt. */
#define MAKO_DTLS_SRTP_EXPORT_LABEL "EXTRACTOR-dtls_srtp"
#define MAKO_DTLS_SRTP_EXPORT_LABEL_LEN 19

typedef struct MakoDtlsCtx {
    SSL_CTX *ctx;
} MakoDtlsCtx;

typedef struct MakoDtlsConn {
    SSL *ssl;
    int fd; /* borrowed UDP socket — NOT closed by mako_dtls_close */
    unsigned char *cookie_secret; /* server only; freed by mako_dtls_close */
} MakoDtlsConn;

/* RFC 6347 stateless cookie exchange: OpenSSL requires app callbacks when
 * SSL_OP_COOKIE_EXCHANGE is set (DTLSv1_listen fails with "cookie gen
 * callback failure" otherwise). Cookie = HMAC-SHA256(secret, peer addr+port);
 * the 16-byte secret is per-connection, heap-allocated in accept and stashed
 * via SSL app_data so the callbacks can reach it. */
#define MAKO_DTLS_COOKIE_SECRET_LEN 16

static inline int mako_dtls_cookie_peer_bytes(
    SSL *ssl, unsigned char *out, size_t *out_len
) {
    BIO_ADDR *peer = BIO_ADDR_new();
    if (!peer) return 0;
    int ok = 0;
    if (BIO_dgram_get_peer(SSL_get_rbio(ssl), peer) > 0) {
        int fam = BIO_ADDR_family(peer);
        unsigned short port = BIO_ADDR_rawport(peer);
        if (fam == AF_INET) {
            struct in_addr a;
            size_t l = sizeof(a);
            if (BIO_ADDR_rawaddress(peer, &a, &l) == 1 && l == sizeof(a)) {
                memcpy(out, &a, 4);
                memcpy(out + 4, &port, 2);
                *out_len = 6;
                ok = 1;
            }
#ifdef AF_INET6
        } else if (fam == AF_INET6) {
            struct in6_addr a;
            size_t l = sizeof(a);
            if (BIO_ADDR_rawaddress(peer, &a, &l) == 1 && l == sizeof(a)) {
                memcpy(out, &a, 16);
                memcpy(out + 16, &port, 2);
                *out_len = 18;
                ok = 1;
            }
#endif
        }
    }
    BIO_ADDR_free(peer);
    return ok;
}

static inline int mako_dtls_cookie_gen_cb(
    SSL *ssl, unsigned char *cookie, unsigned int *cookie_len
) {
    const unsigned char *secret = (const unsigned char *)SSL_get_app_data(ssl);
    unsigned char addr[18];
    size_t alen = 0;
    if (!secret || !mako_dtls_cookie_peer_bytes(ssl, addr, &alen)) return 0;
    unsigned int mlen = 0;
    if (!HMAC(EVP_sha256(), secret, MAKO_DTLS_COOKIE_SECRET_LEN,
              addr, alen, cookie, &mlen))
        return 0;
    *cookie_len = mlen;
    return 1;
}

static inline int mako_dtls_cookie_verify_cb(
    SSL *ssl, const unsigned char *cookie, unsigned int cookie_len
) {
    unsigned char expect[EVP_MAX_MD_SIZE];
    unsigned int elen = 0;
    if (!mako_dtls_cookie_gen_cb(ssl, expect, &elen)) return 0;
    return elen == cookie_len && CRYPTO_memcmp(cookie, expect, cookie_len) == 0;
}

static inline int64_t mako_dtls_available(void) { return 1; }

/* Wait for the fd to become readable (want_write=0) or writable, or for tv
 * to elapse. Returns select() result: >0 ready, 0 timeout, <0 error. */
static inline int mako_dtls_wait_fd(int fd, int want_write, struct timeval *tv) {
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    if (want_write) FD_SET(fd, &wfds);
    else FD_SET(fd, &rfds);
    return select(fd + 1, want_write ? NULL : &rfds, want_write ? &wfds : NULL,
                  NULL, tv);
}

/* Clamp the select wait to the DTLS retransmit timer (or 1s when the stack
 * has no timer armed yet) and handle a retransmit on expiry.
 * Returns 0 to keep driving, -1 on a hard error. */
static inline int mako_dtls_drive_timers(SSL *ssl, int fd, int want_write) {
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (DTLSv1_get_timeout(ssl, &tv)) {
        if (tv.tv_sec > 1) {
            tv.tv_sec = 1;
            tv.tv_usec = 0;
        }
    }
    int s = mako_dtls_wait_fd(fd, want_write, &tv);
    if (s == 0) {
        if (DTLSv1_handle_timeout(ssl) < 0) {
            ERR_print_errors_fp(stderr);
            return -1;
        }
        return 0;
    }
    if (s < 0 && errno != EINTR) return -1;
    return 0;
}

/* Blocking DTLS handshake with RFC 6347 retransmit timers. The fd must be
 * nonblocking while this runs. ~15s overall cap. 0 on success, -1 on fail. */
static inline int mako_dtls_handshake_loop(SSL *ssl, int fd) {
    time_t deadline = time(NULL) + 15;
    for (;;) {
        int rc = SSL_do_handshake(ssl);
        if (rc == 1) return 0;
        int err = SSL_get_error(ssl, rc);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            ERR_print_errors_fp(stderr);
            return -1;
        }
        if (time(NULL) > deadline) {
            fprintf(stderr, "mako dtls: handshake timed out\n");
            return -1;
        }
        if (mako_dtls_drive_timers(ssl, fd, err == SSL_ERROR_WANT_WRITE) != 0)
            return -1;
    }
}

/* Server cookie exchange (HelloVerifyRequest) + peer discovery via
 * DTLSv1_listen. Same timer discipline as the handshake loop. */
static inline int mako_dtls_listen_loop(SSL *ssl, int fd, BIO_ADDR *peer) {
    time_t deadline = time(NULL) + 15;
    for (;;) {
        int rc = DTLSv1_listen(ssl, peer);
        if (rc > 0) return 0;
        int err = SSL_get_error(ssl, rc);
        if (err == SSL_ERROR_SSL) {
            /* Genuine protocol/config error (not the routine post-
             * HelloVerifyRequest WANT_READ) — report and keep waiting. */
            ERR_print_errors_fp(stderr);
        } else if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            ERR_clear_error();
        }
        if (time(NULL) > deadline) {
            fprintf(stderr, "mako dtls: listen timed out\n");
            return -1;
        }
        if (mako_dtls_drive_timers(ssl, fd, err == SSL_ERROR_WANT_WRITE) != 0)
            return -1;
    }
}

/* Point a dgram BIO at a connected peer (OpenSSL 1.1.1+ expects BIO_ADDR). */
static inline int mako_dtls_bio_set_connected(BIO *bio, const struct sockaddr *sa) {
    BIO_ADDR *ba = BIO_ADDR_new();
    if (!ba) return 0;
    int ok = 0;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
        ok = BIO_ADDR_rawmake(ba, AF_INET, &sin->sin_addr,
                              sizeof(sin->sin_addr), sin->sin_port);
#ifdef AF_INET6
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
        ok = BIO_ADDR_rawmake(ba, AF_INET6, &sin6->sin6_addr,
                              sizeof(sin6->sin6_addr), sin6->sin6_port);
#endif
    }
    if (ok) ok = BIO_ctrl_set_connected(bio, ba);
    BIO_ADDR_free(ba);
    return ok;
}

/* Extract a sockaddr from the BIO_ADDR that DTLSv1_listen discovered. */
static inline int mako_dtls_peer_sockaddr(const BIO_ADDR *peer,
                                          struct sockaddr_storage *out,
                                          socklen_t *out_len) {
    memset(out, 0, sizeof(*out));
    int fam = BIO_ADDR_family(peer);
    if (fam == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)out;
        size_t l = sizeof(sin->sin_addr);
        sin->sin_family = AF_INET;
        if (BIO_ADDR_rawaddress(peer, &sin->sin_addr, &l) != 1) return 0;
        sin->sin_port = BIO_ADDR_rawport(peer); /* network byte order */
        *out_len = (socklen_t)sizeof(*sin);
        return 1;
#ifdef AF_INET6
    } else if (fam == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)out;
        size_t l = sizeof(sin6->sin6_addr);
        sin6->sin6_family = AF_INET6;
        if (BIO_ADDR_rawaddress(peer, &sin6->sin6_addr, &l) != 1) return 0;
        sin6->sin6_port = BIO_ADDR_rawport(peer); /* network byte order */
        *out_len = (socklen_t)sizeof(*sin6);
        return 1;
#endif
    }
    return 0;
}

/* Create a DTLS context from cert + key PEM paths (usable as client or
 * server; the role is chosen by connect/accept). use_srtp=1 advertises the
 * SRTP protection profiles needed for DTLS-SRTP key export. NULL on fail. */
static inline void *mako_dtls_ctx_new(
    MakoString cert_path, MakoString key_path, int64_t use_srtp
) {
    char cbuf[1024], kbuf[1024];
    if (!cert_path.data || cert_path.len == 0 || cert_path.len >= sizeof(cbuf))
        return NULL;
    if (!key_path.data || key_path.len == 0 || key_path.len >= sizeof(kbuf))
        return NULL;
    memcpy(cbuf, cert_path.data, cert_path.len);
    cbuf[cert_path.len] = 0;
    memcpy(kbuf, key_path.data, key_path.len);
    kbuf[key_path.len] = 0;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    SSL_CTX *ctx = SSL_CTX_new(DTLS_method());
    if (ctx) {
        /* Pin to DTLS 1.2 (WebRTC interop target; 1.3 is a follow-up). */
        SSL_CTX_set_min_proto_version(ctx, DTLS1_2_VERSION);
        SSL_CTX_set_max_proto_version(ctx, DTLS1_2_VERSION);
    }
#else
    SSL_CTX *ctx = SSL_CTX_new(DTLSv1_2_method());
#endif
    if (!ctx) return NULL;
    if (SSL_CTX_use_certificate_chain_file(ctx, cbuf) != 1
        || SSL_CTX_use_PrivateKey_file(ctx, kbuf, SSL_FILETYPE_PEM) != 1
        || !SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr, "mako dtls_ctx_new: bad cert/key (%s, %s)\n", cbuf, kbuf);
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (use_srtp) {
        if (SSL_CTX_set_tlsext_use_srtp(
                ctx,
                "SRTP_AEAD_AES_256_GCM:SRTP_AES128_CM_SHA1_80:"
                "SRTP_AES128_CM_SHA1_32") != 0) {
            fprintf(stderr, "mako dtls_ctx_new: SRTP profiles unsupported\n");
            ERR_print_errors_fp(stderr);
            SSL_CTX_free(ctx);
            return NULL;
        }
    }
    /* Stateless cookie exchange (HelloVerifyRequest) for the server role;
     * harmless for clients. The per-connection secret is set on the SSL via
     * app_data in mako_dtls_accept. */
    SSL_CTX_set_cookie_generate_cb(ctx, mako_dtls_cookie_gen_cb);
    SSL_CTX_set_cookie_verify_cb(ctx, mako_dtls_cookie_verify_cb);
    MakoDtlsCtx *c = (MakoDtlsCtx *)calloc(1, sizeof(MakoDtlsCtx));
    if (!c) {
        SSL_CTX_free(ctx);
        mako_abort("mako dtls: out of memory");
    }
    c->ctx = ctx;
    return (void *)c;
}

static inline int64_t mako_dtls_ctx_free(void *ctx_ptr) {
    MakoDtlsCtx *c = (MakoDtlsCtx *)ctx_ptr;
    if (!c) return 0;
    if (c->ctx) SSL_CTX_free(c->ctx);
    free(c);
    return 0;
}

/* Client handshake: connect()s the UDP socket to host:port, then runs the
 * full blocking DTLS handshake with retransmit timers. Returns the conn or
 * NULL on failure. The caller keeps owning udp_fd. */
static inline void *mako_dtls_connect(
    void *ctx_ptr, int64_t udp_fd, MakoString host, int64_t port
) {
    MakoDtlsCtx *c = (MakoDtlsCtx *)ctx_ptr;
    if (!c || !c->ctx || udp_fd < 0 || port <= 0 || port > 65535) return NULL;
    char hbuf[256];
    if (!host.data || host.len == 0 || host.len >= sizeof(hbuf)) return NULL;
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    int fd = (int)udp_fd;

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", (int)port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC;
    struct addrinfo *res = NULL;
    if (getaddrinfo(hbuf, portstr, &hints, &res) != 0 || !res) return NULL;
    int connected = 0;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        if (connect(fd, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0) {
            connected = 1;
            break;
        }
    }
    freeaddrinfo(res);
    if (!connected) return NULL;

    SSL *ssl = SSL_new(c->ctx);
    if (!ssl) return NULL;
    DTLS_set_link_mtu(ssl, 1200);
    BIO *bio = BIO_new_dgram(fd, BIO_NOCLOSE);
    if (!bio) {
        SSL_free(ssl);
        return NULL;
    }
    struct sockaddr_storage peer;
    socklen_t plen = (socklen_t)sizeof(peer);
    if (getpeername(fd, (struct sockaddr *)&peer, &plen) != 0
        || !mako_dtls_bio_set_connected(bio, (struct sockaddr *)&peer)) {
        SSL_free(ssl);
        return NULL;
    }
    SSL_set_bio(ssl, bio, bio);
    SSL_set_connect_state(ssl);

    mako_sock_set_nonblock(fd, 1);
    int ok = mako_dtls_handshake_loop(ssl, fd);
    mako_sock_set_nonblock(fd, 0);
    if (ok != 0) {
        SSL_free(ssl);
        return NULL;
    }
    MakoDtlsConn *conn = (MakoDtlsConn *)malloc(sizeof(MakoDtlsConn));
    if (!conn) {
        SSL_free(ssl);
        mako_abort("mako dtls: out of memory");
    }
    conn->ssl = ssl;
    conn->fd = fd;
    conn->cookie_secret = NULL;
    return (void *)conn;
}

/* Server handshake on a bound UDP socket: stateless cookie exchange
 * (HelloVerifyRequest) to discover the peer, then the socket is connect()ed
 * to that peer (v1: one peer owns the socket) and the blocking handshake
 * runs to completion. Returns the conn or NULL on failure/timeout. */
static inline void *mako_dtls_accept(void *ctx_ptr, int64_t udp_fd) {
    MakoDtlsCtx *c = (MakoDtlsCtx *)ctx_ptr;
    if (!c || !c->ctx || udp_fd < 0) return NULL;
    int fd = (int)udp_fd;

    SSL *ssl = SSL_new(c->ctx);
    if (!ssl) return NULL;
    SSL_set_options(ssl, SSL_OP_COOKIE_EXCHANGE);
    DTLS_set_link_mtu(ssl, 1200);
    unsigned char *secret = (unsigned char *)malloc(MAKO_DTLS_COOKIE_SECRET_LEN);
    if (!secret) {
        SSL_free(ssl);
        mako_abort("mako dtls: out of memory");
    }
    if (RAND_bytes(secret, MAKO_DTLS_COOKIE_SECRET_LEN) != 1) {
        free(secret);
        SSL_free(ssl);
        return NULL;
    }
    SSL_set_app_data(ssl, (char *)secret);
    BIO *bio = BIO_new_dgram(fd, BIO_NOCLOSE);
    if (!bio) {
        free(secret);
        SSL_free(ssl);
        return NULL;
    }
    SSL_set_bio(ssl, bio, bio);
    SSL_set_accept_state(ssl);

    BIO_ADDR *peer = BIO_ADDR_new();
    if (!peer) {
        free(secret);
        SSL_free(ssl);
        mako_abort("mako dtls: out of memory");
    }
    mako_sock_set_nonblock(fd, 1);
    int ok = mako_dtls_listen_loop(ssl, fd, peer);
    if (ok == 0) {
        struct sockaddr_storage ss;
        socklen_t ss_len = 0;
        if (!mako_dtls_peer_sockaddr(peer, &ss, &ss_len)
            || connect(fd, (struct sockaddr *)&ss, ss_len) != 0
            || !BIO_ctrl_set_connected(bio, peer)) {
            ok = -1;
        } else {
            ok = mako_dtls_handshake_loop(ssl, fd);
        }
    }
    mako_sock_set_nonblock(fd, 0);
    BIO_ADDR_free(peer);
    if (ok != 0) {
        free(secret);
        SSL_free(ssl);
        return NULL;
    }
    MakoDtlsConn *conn = (MakoDtlsConn *)malloc(sizeof(MakoDtlsConn));
    if (!conn) {
        free(secret);
        SSL_free(ssl);
        mako_abort("mako dtls: out of memory");
    }
    conn->ssl = ssl;
    conn->fd = fd;
    conn->cookie_secret = secret;
    return (void *)conn;
}

/* Bytes written, or -1 on error. Datagram semantics: one send = one record. */
static inline int64_t mako_dtls_send(void *conn_ptr, MakoString data) {
    MakoDtlsConn *conn = (MakoDtlsConn *)conn_ptr;
    if (!conn || !conn->ssl || (!data.data && data.len > 0)) return -1;
    int n = SSL_write(conn->ssl, data.data ? data.data : "", (int)data.len);
    if (n <= 0) return -1;
    return (int64_t)n;
}

/* Read one record (up to max bytes). "" on close/would-block/error. */
static inline MakoString mako_dtls_recv(void *conn_ptr, int64_t max) {
    MakoDtlsConn *conn = (MakoDtlsConn *)conn_ptr;
    if (!conn || !conn->ssl || max <= 0) return mako_str_from_cstr("");
    char *buf = (char *)malloc((size_t)max + 1);
    if (!buf) mako_abort("mako dtls: out of memory");
    int n = SSL_read(conn->ssl, buf, (int)max);
    if (n <= 0) {
        free(buf);
        return mako_str_from_cstr("");
    }
    buf[n] = 0;
    MakoString s;
    s.data = buf;
    s.len = (size_t)n;
    return s;
}

/* Sends close_notify and frees the conn. Does NOT close the UDP fd. */
static inline int64_t mako_dtls_close(void *conn_ptr) {
    MakoDtlsConn *conn = (MakoDtlsConn *)conn_ptr;
    if (!conn) return 0;
    if (conn->ssl) {
        (void)SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
    }
    free(conn->cookie_secret);
    free(conn);
    return 0;
}

/* Underlying UDP fd (borrowed; useful for udp_close after dtls_close). */
static inline int64_t mako_dtls_conn_fd(void *conn_ptr) {
    MakoDtlsConn *conn = (MakoDtlsConn *)conn_ptr;
    if (!conn) return -1;
    return (int64_t)conn->fd;
}

/* SHA-256 fingerprint of a cert as "AA:BB:…" uppercase colon-hex (SDP
 * `a=fingerprint:sha-256`). "" if the cert cannot be digested. */
static inline MakoString mako_dtls_fingerprint_of(X509 *cert) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mlen = 0;
    if (!cert || X509_digest(cert, EVP_sha256(), md, &mlen) != 1 || mlen == 0)
        return mako_str_from_cstr("");
    char *buf = (char *)malloc((size_t)mlen * 3);
    if (!buf) mako_abort("mako dtls: out of memory");
    char *p = buf;
    for (unsigned int i = 0; i < mlen; i++) {
        if (i) *p++ = ':';
        static const char hexd[] = "0123456789ABCDEF";
        *p++ = hexd[(md[i] >> 4) & 0xf];
        *p++ = hexd[md[i] & 0xf];
    }
    MakoString s;
    s.data = buf;
    s.len = (size_t)(p - buf);
    return s;
}

/* Peer's cert fingerprint; "" when the peer presented none. */
static inline MakoString mako_dtls_peer_fingerprint(void *conn_ptr) {
    MakoDtlsConn *conn = (MakoDtlsConn *)conn_ptr;
    if (!conn || !conn->ssl) return mako_str_from_cstr("");
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    X509 *cert = SSL_get1_peer_certificate(conn->ssl);
#else
    X509 *cert = SSL_get_peer_certificate(conn->ssl);
#endif
    if (!cert) return mako_str_from_cstr("");
    MakoString s = mako_dtls_fingerprint_of(cert);
    X509_free(cert);
    return s;
}

/* Own cert fingerprint (for an SDP offer/answer); "" without a loaded cert. */
static inline MakoString mako_dtls_local_fingerprint(void *ctx_ptr) {
    MakoDtlsCtx *c = (MakoDtlsCtx *)ctx_ptr;
    if (!c || !c->ctx) return mako_str_from_cstr("");
    return mako_dtls_fingerprint_of(SSL_CTX_get0_certificate(c->ctx));
}

/* RFC 5764 keying material for the negotiated SRTP profile:
 * client_key | client_salt | server_key | server_salt (binary, 2*(key+salt)
 * bytes — 60 for AES128_CM_SHA1_80/32, 88 for AEAD_AES_256_GCM).
 * "" on failure, when not handshaken, or when SRTP was not negotiated.
 * Treat as key material: wrap via secret_from_str + secret_drop to wipe. */
static inline MakoString mako_dtls_export_srtp_keys(void *conn_ptr) {
    MakoDtlsConn *conn = (MakoDtlsConn *)conn_ptr;
    if (!conn || !conn->ssl || !SSL_is_init_finished(conn->ssl))
        return mako_str_from_cstr("");
    SRTP_PROTECTION_PROFILE *p = SSL_get_selected_srtp_profile(conn->ssl);
    if (!p || !p->name) return mako_str_from_cstr("");
    size_t klen = 16, slen = 14;
    if (strcmp(p->name, "SRTP_AEAD_AES_256_GCM") == 0) {
        klen = 32;
        slen = 12;
    } else if (strcmp(p->name, "SRTP_AEAD_AES_128_GCM") == 0) {
        klen = 16;
        slen = 12;
    }
    size_t n = 2 * (klen + slen);
    char *buf = (char *)malloc(n);
    if (!buf) mako_abort("mako dtls: out of memory");
    if (SSL_export_keying_material(conn->ssl, (unsigned char *)buf, n,
                                   MAKO_DTLS_SRTP_EXPORT_LABEL,
                                   MAKO_DTLS_SRTP_EXPORT_LABEL_LEN,
                                   NULL, 0, 0) != 1) {
        free(buf);
        ERR_print_errors_fp(stderr);
        return mako_str_from_cstr("");
    }
    MakoString s;
    s.data = buf;
    s.len = n;
    return s;
}

/* Negotiated SRTP protection profile name; "" when none was negotiated. */
static inline MakoString mako_dtls_srtp_profile(void *conn_ptr) {
    MakoDtlsConn *conn = (MakoDtlsConn *)conn_ptr;
    if (!conn || !conn->ssl) return mako_str_from_cstr("");
    SRTP_PROTECTION_PROFILE *p = SSL_get_selected_srtp_profile(conn->ssl);
    if (!p || !p->name) return mako_str_from_cstr("");
    return mako_str_from_cstr(p->name);
}

#else /* !MAKO_DTLS_REAL */

/* DTLS — unavailable without a linked TLS backend (or on _WIN32 for v1). */
static inline int64_t mako_dtls_available(void) { return 0; }
static inline void *mako_dtls_ctx_new(
    MakoString cert_path, MakoString key_path, int64_t use_srtp
) {
    (void)cert_path; (void)key_path; (void)use_srtp; return NULL;
}
static inline int64_t mako_dtls_ctx_free(void *ctx) { (void)ctx; return 0; }
static inline void *mako_dtls_connect(
    void *ctx, int64_t udp_fd, MakoString host, int64_t port
) {
    (void)ctx; (void)udp_fd; (void)host; (void)port; return NULL;
}
static inline void *mako_dtls_accept(void *ctx, int64_t udp_fd) {
    (void)ctx; (void)udp_fd; return NULL;
}
static inline int64_t mako_dtls_send(void *conn, MakoString data) {
    (void)conn; (void)data; return -1;
}
static inline MakoString mako_dtls_recv(void *conn, int64_t max) {
    (void)conn; (void)max; return mako_str_from_cstr("");
}
static inline int64_t mako_dtls_close(void *conn) { (void)conn; return 0; }
static inline int64_t mako_dtls_conn_fd(void *conn) { (void)conn; return -1; }
static inline MakoString mako_dtls_peer_fingerprint(void *conn) {
    (void)conn; return mako_str_from_cstr("");
}
static inline MakoString mako_dtls_local_fingerprint(void *ctx) {
    (void)ctx; return mako_str_from_cstr("");
}
static inline MakoString mako_dtls_export_srtp_keys(void *conn) {
    (void)conn; return mako_str_from_cstr("");
}
static inline MakoString mako_dtls_srtp_profile(void *conn) {
    (void)conn; return mako_str_from_cstr("");
}

#endif /* MAKO_DTLS_REAL */

#ifdef __cplusplus
}
#endif

#endif /* MAKO_DTLS_H */
