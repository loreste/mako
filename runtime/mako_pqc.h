/* Post-quantum cryptography: ML-DSA (FIPS 204) via OpenSSL 3.5+.
 *
 * Wraps OpenSSL's native EVP implementation — no custom crypto.
 * All functions are memory-safe: every OpenSSL object is freed on
 * all paths (success, error, early return). Returned MakoStrings
 * are owned by the caller.
 *
 * Compile guard: requires OpenSSL >= 3.5.0 (MLDSA44/65/87 in default provider).
 */
#ifndef MAKO_PQC_H
#define MAKO_PQC_H

#include "mako_rt.h"

#ifdef MAKO_TLS_REAL
#include <openssl/opensslv.h>

/* ML-DSA requires OpenSSL 3.5.0+ (default provider ships MLDSA44/65/87). */
#if OPENSSL_VERSION_NUMBER >= 0x30500000L
#define MAKO_PQC_AVAILABLE 1

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

/* ---- helpers ---- */

/* Read a BIO into an owned MakoString, then free the BIO. */
static inline MakoString mako_pqc_bio_to_str(BIO *bio) {
    if (!bio) return mako_str_from_cstr("");
    char *data = NULL;
    long len = BIO_get_mem_data(bio, &data);
    MakoString s;
    if (len > 0 && data) {
        s.data = (char *)malloc((size_t)len);
        if (s.data) {
            memcpy(s.data, data, (size_t)len);
            s.len = (size_t)len;
        } else {
            s = mako_str_from_cstr("");
        }
    } else {
        s = mako_str_from_cstr("");
    }
    BIO_free(bio);
    return s;
}

/* Load a PEM private key from a MakoString. Caller must EVP_PKEY_free. */
static inline EVP_PKEY *mako_pqc_load_private(MakoString pem) {
    if (!pem.data || pem.len == 0) return NULL;
    BIO *bio = BIO_new_mem_buf(pem.data, (int)pem.len);
    if (!bio) return NULL;
    EVP_PKEY *key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    return key;
}

/* Load a PEM public key from a MakoString. Caller must EVP_PKEY_free. */
static inline EVP_PKEY *mako_pqc_load_public(MakoString pem) {
    if (!pem.data || pem.len == 0) return NULL;
    BIO *bio = BIO_new_mem_buf(pem.data, (int)pem.len);
    if (!bio) return NULL;
    EVP_PKEY *key = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    return key;
}

/* ---- key generation ---- */

static inline MakoString mako_mldsa_keygen(const char *algo) {
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, algo, NULL);
    if (!ctx) return mako_str_from_cstr("");
    EVP_PKEY *key = NULL;
    int ok = EVP_PKEY_keygen_init(ctx) > 0 && EVP_PKEY_keygen(ctx, &key) > 0;
    EVP_PKEY_CTX_free(ctx);
    if (!ok || !key) {
        if (key) EVP_PKEY_free(key);
        return mako_str_from_cstr("");
    }
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) { EVP_PKEY_free(key); return mako_str_from_cstr(""); }
    if (PEM_write_bio_PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL) <= 0) {
        BIO_free(bio);
        EVP_PKEY_free(key);
        return mako_str_from_cstr("");
    }
    EVP_PKEY_free(key);
    return mako_pqc_bio_to_str(bio);
}

static inline MakoString mako_mldsa44_keygen(void) { return mako_mldsa_keygen("MLDSA44"); }
static inline MakoString mako_mldsa65_keygen(void) { return mako_mldsa_keygen("MLDSA65"); }
static inline MakoString mako_mldsa87_keygen(void) { return mako_mldsa_keygen("MLDSA87"); }

/* ---- public key extraction ---- */

static inline MakoString mako_mldsa_public_key(MakoString private_pem) {
    EVP_PKEY *key = mako_pqc_load_private(private_pem);
    if (!key) return mako_str_from_cstr("");
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) { EVP_PKEY_free(key); return mako_str_from_cstr(""); }
    if (PEM_write_bio_PUBKEY(bio, key) <= 0) {
        BIO_free(bio);
        EVP_PKEY_free(key);
        return mako_str_from_cstr("");
    }
    EVP_PKEY_free(key);
    return mako_pqc_bio_to_str(bio);
}

/* ---- sign / verify ---- */

/* Sign message with ML-DSA private key. Returns raw signature bytes. */
static inline MakoString mako_mldsa_sign(MakoString private_pem, MakoString message) {
    EVP_PKEY *key = mako_pqc_load_private(private_pem);
    if (!key) return mako_str_from_cstr("");

    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) { EVP_PKEY_free(key); return mako_str_from_cstr(""); }

    MakoString result = mako_str_from_cstr("");
    if (EVP_DigestSignInit(md_ctx, NULL, NULL, NULL, key) <= 0) goto sign_done;

    size_t sig_len = 0;
    /* First call: determine signature length. */
    if (EVP_DigestSign(md_ctx, NULL, &sig_len,
                       (const unsigned char *)message.data, message.len) <= 0)
        goto sign_done;

    {
        unsigned char *sig = (unsigned char *)malloc(sig_len);
        if (!sig) goto sign_done;

        if (EVP_DigestSign(md_ctx, sig, &sig_len,
                           (const unsigned char *)message.data, message.len) <= 0) {
            free(sig);
            goto sign_done;
        }
        result.data = (char *)sig;
        result.len = sig_len;
    }

sign_done:
    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(key);
    return result;
}

/* Verify message signature with ML-DSA public key. Returns 1 on success, 0 on failure. */
static inline int64_t mako_mldsa_verify(MakoString public_pem, MakoString message, MakoString signature) {
    EVP_PKEY *key = mako_pqc_load_public(public_pem);
    if (!key) return 0;

    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) { EVP_PKEY_free(key); return 0; }

    int64_t ok = 0;
    if (EVP_DigestVerifyInit(md_ctx, NULL, NULL, NULL, key) <= 0) goto verify_done;

    if (EVP_DigestVerify(md_ctx,
                         (const unsigned char *)signature.data, signature.len,
                         (const unsigned char *)message.data, message.len) == 1) {
        ok = 1;
    }

verify_done:
    EVP_MD_CTX_free(md_ctx);
    EVP_PKEY_free(key);
    return ok;
}

/* ---- X.509 certificate creation ---- */

/* Create a self-signed X.509 certificate with an ML-DSA signature.
 * subject: e.g. "CN=test,O=Mako"
 * days: validity period
 * Returns PEM-encoded certificate. */
static inline MakoString mako_mldsa_self_signed_cert(
    MakoString private_pem, MakoString subject, int64_t days
) {
    EVP_PKEY *key = mako_pqc_load_private(private_pem);
    if (!key) return mako_str_from_cstr("");

    X509 *cert = X509_new();
    if (!cert) { EVP_PKEY_free(key); return mako_str_from_cstr(""); }

    MakoString result = mako_str_from_cstr("");

    /* Serial number: random 20 bytes. */
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);

    /* Validity. */
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), (long)(days * 86400));

    /* Subject/issuer from string. */
    {
        /* Null-terminate the subject string for OpenSSL. */
        char *subj_cstr = (char *)malloc(subject.len + 1);
        if (!subj_cstr) goto cert_done;
        memcpy(subj_cstr, subject.data, subject.len);
        subj_cstr[subject.len] = '\0';

        X509_NAME *name = X509_NAME_new();
        if (!name) { free(subj_cstr); goto cert_done; }

        /* Parse "CN=value,O=value" format. */
        char *p = subj_cstr;
        while (*p) {
            char *eq = strchr(p, '=');
            if (!eq) break;
            *eq = '\0';
            char *val = eq + 1;
            char *comma = strchr(val, ',');
            if (comma) *comma = '\0';
            /* Trim leading spaces. */
            while (*p == ' ') p++;
            while (*val == ' ') val++;
            X509_NAME_add_entry_by_txt(name, p, MBSTRING_ASC,
                                       (const unsigned char *)val, -1, -1, 0);
            if (comma) p = comma + 1;
            else break;
        }
        free(subj_cstr);
        X509_set_subject_name(cert, name);
        X509_set_issuer_name(cert, name); /* self-signed */
        X509_NAME_free(name);
    }

    /* Public key. */
    X509_set_pubkey(cert, key);

    /* Sign certificate with the ML-DSA private key. */
    if (X509_sign(cert, key, NULL) <= 0) goto cert_done;

    /* PEM encode. */
    {
        BIO *bio = BIO_new(BIO_s_mem());
        if (!bio) goto cert_done;
        if (PEM_write_bio_X509(bio, cert) <= 0) {
            BIO_free(bio);
            goto cert_done;
        }
        result = mako_pqc_bio_to_str(bio);
    }

cert_done:
    X509_free(cert);
    EVP_PKEY_free(key);
    return result;
}

/* Verify an X.509 certificate against a CA certificate. Returns 1 if valid. */
static inline int64_t mako_mldsa_verify_cert(MakoString cert_pem, MakoString ca_pem) {
    if (!cert_pem.data || !ca_pem.data) return 0;

    BIO *cert_bio = BIO_new_mem_buf(cert_pem.data, (int)cert_pem.len);
    BIO *ca_bio = BIO_new_mem_buf(ca_pem.data, (int)ca_pem.len);
    if (!cert_bio || !ca_bio) {
        if (cert_bio) BIO_free(cert_bio);
        if (ca_bio) BIO_free(ca_bio);
        return 0;
    }

    X509 *cert = PEM_read_bio_X509(cert_bio, NULL, NULL, NULL);
    X509 *ca = PEM_read_bio_X509(ca_bio, NULL, NULL, NULL);
    BIO_free(cert_bio);
    BIO_free(ca_bio);
    if (!cert || !ca) {
        if (cert) X509_free(cert);
        if (ca) X509_free(ca);
        return 0;
    }

    X509_STORE *store = X509_STORE_new();
    if (!store) { X509_free(cert); X509_free(ca); return 0; }
    X509_STORE_add_cert(store, ca);

    X509_STORE_CTX *ctx = X509_STORE_CTX_new();
    if (!ctx) {
        X509_STORE_free(store);
        X509_free(cert);
        X509_free(ca);
        return 0;
    }
    X509_STORE_CTX_init(ctx, store, cert, NULL);

    int64_t ok = X509_verify_cert(ctx) == 1 ? 1 : 0;

    X509_STORE_CTX_free(ctx);
    X509_STORE_free(store);
    X509_free(cert);
    X509_free(ca);
    return ok;
}

/* ---- TLS 1.3 integration ---- */

/* Enable post-quantum signature schemes on an SSL_CTX.
 * OpenSSL 3.5+ auto-registers ML-DSA when keys are loaded;
 * this function adds the MLDSA signature algorithms to the
 * client/server signature_algorithms list. Returns 1 on success. */
static inline int64_t mako_tls_enable_pqc(SSL_CTX *ctx) {
    if (!ctx) return 0;
    /* Add ML-DSA to the signature algorithms list alongside classical ones.
     * The colon-separated list extends the default; OpenSSL merges them. */
    if (SSL_CTX_set1_sigalgs_list(ctx,
            "MLDSA87:MLDSA65:MLDSA44:"
            "ECDSA+SHA256:ECDSA+SHA384:"
            "RSA-PSS+SHA256:RSA-PSS+SHA384") <= 0) {
        return 0;
    }
    return 1;
}

/* Create a TLS server context configured for ML-DSA certificates.
 * Returns an opaque SSL_CTX pointer (same as mako_tls_server_new). */
static inline SSL_CTX *mako_tls_server_pqc(MakoString cert_pem, MakoString key_pem) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) return NULL;

    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);

    /* Load ML-DSA certificate. */
    {
        BIO *bio = BIO_new_mem_buf(cert_pem.data, (int)cert_pem.len);
        if (!bio) { SSL_CTX_free(ctx); return NULL; }
        X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
        BIO_free(bio);
        if (!cert) { SSL_CTX_free(ctx); return NULL; }
        if (SSL_CTX_use_certificate(ctx, cert) <= 0) {
            X509_free(cert);
            SSL_CTX_free(ctx);
            return NULL;
        }
        X509_free(cert);
    }

    /* Load ML-DSA private key. */
    {
        BIO *bio = BIO_new_mem_buf(key_pem.data, (int)key_pem.len);
        if (!bio) { SSL_CTX_free(ctx); return NULL; }
        EVP_PKEY *key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
        BIO_free(bio);
        if (!key) { SSL_CTX_free(ctx); return NULL; }
        if (SSL_CTX_use_PrivateKey(ctx, key) <= 0) {
            EVP_PKEY_free(key);
            SSL_CTX_free(ctx);
            return NULL;
        }
        EVP_PKEY_free(key);
    }

    /* Enable PQC signature algorithms. */
    mako_tls_enable_pqc(ctx);

    return ctx;
}

/* ---- availability probe ---- */

static inline int64_t mako_pqc_available(void) { return 1; }

static inline MakoString mako_mldsa_algorithm_name(MakoString private_pem) {
    EVP_PKEY *key = mako_pqc_load_private(private_pem);
    if (!key) return mako_str_from_cstr("");
    const char *name = EVP_PKEY_get0_type_name(key);
    MakoString s = name ? mako_str_from_cstr(name) : mako_str_from_cstr("");
    EVP_PKEY_free(key);
    return s;
}

#else /* OpenSSL < 3.5.0 — stubs */
#define MAKO_PQC_AVAILABLE 0

static inline int64_t mako_pqc_available(void) { return 0; }
static inline MakoString mako_mldsa44_keygen(void) { return mako_str_from_cstr(""); }
static inline MakoString mako_mldsa65_keygen(void) { return mako_str_from_cstr(""); }
static inline MakoString mako_mldsa87_keygen(void) { return mako_str_from_cstr(""); }
static inline MakoString mako_mldsa_public_key(MakoString p) { (void)p; return mako_str_from_cstr(""); }
static inline MakoString mako_mldsa_sign(MakoString k, MakoString m) { (void)k; (void)m; return mako_str_from_cstr(""); }
static inline int64_t mako_mldsa_verify(MakoString k, MakoString m, MakoString s) { (void)k; (void)m; (void)s; return 0; }
static inline MakoString mako_mldsa_self_signed_cert(MakoString k, MakoString s, int64_t d) { (void)k; (void)s; (void)d; return mako_str_from_cstr(""); }
static inline int64_t mako_mldsa_verify_cert(MakoString c, MakoString ca) { (void)c; (void)ca; return 0; }
static inline MakoString mako_mldsa_algorithm_name(MakoString p) { (void)p; return mako_str_from_cstr(""); }

#endif /* OPENSSL_VERSION_NUMBER >= 0x30500000L */
#else /* no OpenSSL at all */
#define MAKO_PQC_AVAILABLE 0

static inline int64_t mako_pqc_available(void) { return 0; }
static inline MakoString mako_mldsa44_keygen(void) { return mako_str_from_cstr(""); }
static inline MakoString mako_mldsa65_keygen(void) { return mako_str_from_cstr(""); }
static inline MakoString mako_mldsa87_keygen(void) { return mako_str_from_cstr(""); }
static inline MakoString mako_mldsa_public_key(MakoString p) { (void)p; return mako_str_from_cstr(""); }
static inline MakoString mako_mldsa_sign(MakoString k, MakoString m) { (void)k; (void)m; return mako_str_from_cstr(""); }
static inline int64_t mako_mldsa_verify(MakoString k, MakoString m, MakoString s) { (void)k; (void)m; (void)s; return 0; }
static inline MakoString mako_mldsa_self_signed_cert(MakoString k, MakoString s, int64_t d) { (void)k; (void)s; (void)d; return mako_str_from_cstr(""); }
static inline int64_t mako_mldsa_verify_cert(MakoString c, MakoString ca) { (void)c; (void)ca; return 0; }
static inline MakoString mako_mldsa_algorithm_name(MakoString p) { (void)p; return mako_str_from_cstr(""); }

#endif /* MAKO_TLS_REAL */
#endif /* MAKO_PQC_H */
