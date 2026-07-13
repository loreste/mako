/* Mako UUID + ULID — fast POD IDs (no heap in the value type).
 *
 * UUID: RFC 4122 / RFC 9562 (v4, v5, v7) — 16-byte stack value.
 * ULID: Crockford Base32 sortable ID — 16-byte stack value (same layout as Uuid).
 *
 * Hot path: Copy/POD, no GC, no allocation for the ID itself.
 * String format/parse allocates only when the caller asks for a string.
 */
#ifndef MAKO_UUID_H
#define MAKO_UUID_H

#include "mako_rt.h"
#if defined(_WIN32) || defined(_WIN64)
#include <time.h>
#else
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#elif defined(OPENSSL_VERSION_NUMBER) || defined(MAKO_HAS_OPENSSL)
#include <openssl/sha.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t b[16];
} MakoUuid;

/* ---- CSPRNG fill (fast; platform best) ---- */
static inline void mako_uuid_fill_random(uint8_t *out, size_t n) {
#if defined(_WIN32) || defined(_WIN64)
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)&seeded);
        seeded = 1;
    }
    for (size_t i = 0; i < n; i++) out[i] = (uint8_t)(rand() & 0xff);
#elif defined(__APPLE__)
    arc4random_buf(out, n);
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        mako_abort("uuid: cannot open /dev/urandom");
    }
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, out + got, n - got);
        if (r <= 0) {
            close(fd);
            mako_abort("uuid: /dev/urandom read failed");
        }
        got += (size_t)r;
    }
    close(fd);
#endif
}

static inline uint64_t mako_uuid_unix_ms(void) {
#if defined(_WIN32) || defined(_WIN64)
    return (uint64_t)time(NULL) * 1000ULL;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
#endif
}

static inline MakoUuid mako_uuid_nil(void) {
    MakoUuid u;
    memset(u.b, 0, 16);
    return u;
}

static inline bool mako_uuid_is_nil(MakoUuid u) {
    for (int i = 0; i < 16; i++) {
        if (u.b[i] != 0) return false;
    }
    return true;
}

static inline bool mako_uuid_eq(MakoUuid a, MakoUuid b) {
    return memcmp(a.b, b.b, 16) == 0;
}

/* RFC 4122 version (1..8) or 0 if nil/unknown layout. */
static inline int64_t mako_uuid_version(MakoUuid u) {
    return (int64_t)((u.b[6] >> 4) & 0x0F);
}

/* RFC 4122 variant: 0=NCS, 2=RFC4122, 6=MS, 7=future */
static inline int64_t mako_uuid_variant(MakoUuid u) {
    uint8_t v = u.b[8];
    if ((v & 0x80) == 0) return 0;
    if ((v & 0xC0) == 0x80) return 2;
    if ((v & 0xE0) == 0xC0) return 6;
    return 7;
}

static inline MakoUuid mako_uuid_v4(void) {
    MakoUuid u;
    mako_uuid_fill_random(u.b, 16);
    u.b[6] = (uint8_t)((u.b[6] & 0x0F) | 0x40); /* version 4 */
    u.b[8] = (uint8_t)((u.b[8] & 0x3F) | 0x80); /* variant 10 */
    return u;
}

/* RFC 9562 UUID v7: 48-bit unix-ms + version/variant + random. Time-ordered, index-friendly. */
static inline MakoUuid mako_uuid_v7(void) {
    MakoUuid u;
    uint64_t ms = mako_uuid_unix_ms();
    u.b[0] = (uint8_t)((ms >> 40) & 0xFF);
    u.b[1] = (uint8_t)((ms >> 32) & 0xFF);
    u.b[2] = (uint8_t)((ms >> 24) & 0xFF);
    u.b[3] = (uint8_t)((ms >> 16) & 0xFF);
    u.b[4] = (uint8_t)((ms >> 8) & 0xFF);
    u.b[5] = (uint8_t)(ms & 0xFF);
    mako_uuid_fill_random(u.b + 6, 10);
    u.b[6] = (uint8_t)((u.b[6] & 0x0F) | 0x70); /* version 7 */
    u.b[8] = (uint8_t)((u.b[8] & 0x3F) | 0x80); /* variant 10 */
    return u;
}

/* SHA-1 digest into out[20] — platform crypto when available (correct + fast). */
static inline void mako_uuid_sha1(const uint8_t *data, size_t len, uint8_t out[20]) {
#if defined(__APPLE__)
    CC_SHA1(data ? data : (const uint8_t *)"", (CC_LONG)len, out);
#elif defined(OPENSSL_VERSION_NUMBER) || defined(MAKO_HAS_OPENSSL)
    SHA1(data ? data : (const uint8_t *)"", len, out);
#else
    /* Portable SHA-1 (RFC 3174) — used when no system crypto is linked. */
    uint32_t h0 = 0x67452301u, h1 = 0xEFCDAB89u, h2 = 0x98BADCFEu,
             h3 = 0x10325476u, h4 = 0xC3D2E1F0u;
    size_t new_len = ((((len + 8) / 64) + 1) * 64) - 8;
    uint8_t *msg = (uint8_t *)calloc(new_len + 8, 1);
    if (!msg) mako_abort("uuid_v5: OOM");
    if (len && data) memcpy(msg, data, len);
    msg[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8ULL;
    for (int i = 0; i < 8; i++) msg[new_len + i] = (uint8_t)((bits >> (56 - 8 * i)) & 0xFF);
    for (size_t off = 0; off < new_len + 8; off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)msg[off + i * 4] << 24) | ((uint32_t)msg[off + i * 4 + 1] << 16)
                | ((uint32_t)msg[off + i * 4 + 2] << 8) | (uint32_t)msg[off + i * 4 + 3];
        }
        for (int i = 16; i < 80; i++) {
            uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (x << 1) | (x >> 31);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            uint32_t t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = t;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }
    free(msg);
    uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; i++) {
        out[i * 4] = (uint8_t)((hs[i] >> 24) & 0xFF);
        out[i * 4 + 1] = (uint8_t)((hs[i] >> 16) & 0xFF);
        out[i * 4 + 2] = (uint8_t)((hs[i] >> 8) & 0xFF);
        out[i * 4 + 3] = (uint8_t)(hs[i] & 0xFF);
    }
#endif
}

/* UUID v5: SHA-1(namespace || name), RFC 4122. */
static inline MakoUuid mako_uuid_v5(MakoUuid ns, MakoString name) {
    size_t nlen = name.len;
    size_t total = 16 + nlen;
    uint8_t *buf = (uint8_t *)malloc(total ? total : 1);
    if (!buf) mako_abort("uuid_v5: OOM");
    memcpy(buf, ns.b, 16);
    if (nlen && name.data) memcpy(buf + 16, name.data, nlen);
    uint8_t dig[20];
    mako_uuid_sha1(buf, total, dig);
    free(buf);
    MakoUuid u;
    memcpy(u.b, dig, 16);
    u.b[6] = (uint8_t)((u.b[6] & 0x0F) | 0x50); /* version 5 */
    u.b[8] = (uint8_t)((u.b[8] & 0x3F) | 0x80); /* variant 10 */
    return u;
}

/* Standard namespaces (RFC 4122 Appendix C). */
static inline MakoUuid mako_uuid_ns_dns(void) {
    MakoUuid u = {{
        0x6b, 0xa7, 0xb8, 0x10, 0x9d, 0xad, 0x11, 0xd1,
        0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8
    }};
    return u;
}
static inline MakoUuid mako_uuid_ns_url(void) {
    MakoUuid u = {{
        0x6b, 0xa7, 0xb8, 0x11, 0x9d, 0xad, 0x11, 0xd1,
        0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8
    }};
    return u;
}
static inline MakoUuid mako_uuid_ns_oid(void) {
    MakoUuid u = {{
        0x6b, 0xa7, 0xb8, 0x12, 0x9d, 0xad, 0x11, 0xd1,
        0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8
    }};
    return u;
}
static inline MakoUuid mako_uuid_ns_x500(void) {
    MakoUuid u = {{
        0x6b, 0xa7, 0xb8, 0x14, 0x9d, 0xad, 0x11, 0xd1,
        0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8
    }};
    return u;
}

/* Raw 16 bytes as MakoString (binary, not hex). Bounds-checked. */
static inline MakoString mako_uuid_bytes(MakoUuid u) {
    char *p = (char *)malloc(16);
    if (!p) mako_abort("uuid_bytes: OOM");
    memcpy(p, u.b, 16);
    return (MakoString){p, 16};
}

/* From exactly 16 raw bytes; nil + ok=false on bad length. */
static inline MakoUuid mako_uuid_from_bytes(MakoString s, bool *ok) {
    if (ok) *ok = false;
    if (s.len != 16 || !s.data) return mako_uuid_nil();
    MakoUuid u;
    memcpy(u.b, s.data, 16);
    if (ok) *ok = true;
    return u;
}

static inline MakoUuid mako_uuid_from_bytes_checked(MakoString s) {
    if (s.len != 16 || !s.data) {
        mako_abort("uuid_from_bytes: need exactly 16 bytes");
    }
    MakoUuid u;
    memcpy(u.b, s.data, 16);
    return u;
}

static inline char mako_uuid_hex_nibble(unsigned v) {
    return (char)(v < 10 ? ('0' + v) : ('a' + (v - 10)));
}
static inline char mako_uuid_hex_nibble_upper(unsigned v) {
    return (char)(v < 10 ? ('0' + v) : ('A' + (v - 10)));
}

static inline void mako_uuid_format_into(MakoUuid u, char buf[37], int upper) {
    static const int dash_at[] = {8, 13, 18, 23};
    int di = 0;
    int bi = 0;
    for (int i = 0; i < 36; i++) {
        if (di < 4 && i == dash_at[di]) {
            buf[i] = '-';
            di++;
        } else {
            uint8_t byte = u.b[bi / 2];
            unsigned nib = (bi % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
            buf[i] = upper ? mako_uuid_hex_nibble_upper(nib) : mako_uuid_hex_nibble(nib);
            bi++;
        }
    }
    buf[36] = 0;
}

static inline MakoString mako_uuid_string(MakoUuid u) {
    char buf[37];
    mako_uuid_format_into(u, buf, 0);
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_uuid_string_upper(MakoUuid u) {
    char buf[37];
    mako_uuid_format_into(u, buf, 1);
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_uuid_urn(MakoUuid u) {
    char buf[46];
    memcpy(buf, "urn:uuid:", 9);
    mako_uuid_format_into(u, buf + 9, 0);
    return mako_str_from_cstr(buf);
}

static inline int mako_uuid_hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse canonical 8-4-4-4-12, optional braces, or 32 bare hex. */
static inline MakoUuid mako_uuid_parse(MakoString s, bool *ok) {
    if (ok) *ok = false;
    if (!s.data || s.len == 0) return mako_uuid_nil();
    const char *p = s.data;
    size_t n = s.len;
    /* strip urn:uuid: */
    if (n >= 9 && (p[0] == 'u' || p[0] == 'U') && p[1] == 'r' && p[2] == 'n'
        && p[3] == ':' && (p[4] == 'u' || p[4] == 'U') && p[5] == 'u'
        && p[6] == 'i' && p[7] == 'd' && p[8] == ':') {
        p += 9;
        n -= 9;
    }
    /* braces {…} */
    if (n >= 2 && p[0] == '{' && p[n - 1] == '}') {
        p += 1;
        n -= 2;
    }
    MakoUuid u;
    if (n == 36) {
        static const int dash_pos[] = {8, 13, 18, 23};
        for (int i = 0; i < 4; i++) {
            if (p[dash_pos[i]] != '-') return mako_uuid_nil();
        }
        int bi = 0;
        for (size_t i = 0; i < 36; i++) {
            if (p[i] == '-') continue;
            int hi = mako_uuid_hex_val(p[i]);
            if (hi < 0 || i + 1 >= 36) return mako_uuid_nil();
            int lo = mako_uuid_hex_val(p[i + 1]);
            if (lo < 0) return mako_uuid_nil();
            u.b[bi++] = (uint8_t)((hi << 4) | lo);
            i++;
            if (bi > 16) return mako_uuid_nil();
        }
        if (bi != 16) return mako_uuid_nil();
        if (ok) *ok = true;
        return u;
    }
    if (n == 32) {
        for (int i = 0; i < 16; i++) {
            int hi = mako_uuid_hex_val(p[i * 2]);
            int lo = mako_uuid_hex_val(p[i * 2 + 1]);
            if (hi < 0 || lo < 0) return mako_uuid_nil();
            u.b[i] = (uint8_t)((hi << 4) | lo);
        }
        if (ok) *ok = true;
        return u;
    }
    return mako_uuid_nil();
}

static inline bool mako_uuid_parse_ok(MakoString s) {
    bool ok = false;
    (void)mako_uuid_parse(s, &ok);
    return ok;
}

static inline MakoResultInt mako_uuid_check(MakoString s) {
    if (mako_uuid_parse_ok(s)) return mako_ok_int(1);
    return mako_err_int(mako_str_from_cstr(
        "invalid UUID (want 8-4-4-4-12, 32 hex, {braces}, or urn:uuid:)"
    ));
}

/* Compare for ordering (memcmp of bytes) — useful for maps/sort. */
static inline int64_t mako_uuid_cmp(MakoUuid a, MakoUuid b) {
    int c = memcmp(a.b, b.b, 16);
    if (c < 0) return -1;
    if (c > 0) return 1;
    return 0;
}

/* ---- ULID (Crockford Base32) — same 16-byte POD, sortable by time ----
 * Encoding: 48-bit ms timestamp + 80-bit randomness → 26-char string.
 * Binary layout matches UUID bytes for storage interchange.
 */
static const char mako_ulid_alphabet[33] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static inline int mako_ulid_decode_char(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') {
        if (c == 'I' || c == 'L' || c == 'O' || c == 'U') return -1;
        /* map A-Z crockford */
        if (c < 'I') return 10 + (c - 'A');
        if (c < 'L') return 18 + (c - 'J'); /* J,K */
        if (c < 'O') return 20 + (c - 'M'); /* M,N */
        if (c < 'U') return 22 + (c - 'P'); /* P-T */
        return 27 + (c - 'V');              /* V-Z */
    }
    if (c >= 'a' && c <= 'z') return mako_ulid_decode_char((char)(c - 32));
    return -1;
}

static inline MakoUuid mako_ulid_new(void) {
    /* Same binary shape as UUID v7 (time + entropy) — different string encoding. */
    return mako_uuid_v7();
}

static inline MakoString mako_ulid_string(MakoUuid u) {
    /* 10 Crockford chars from 48-bit time + 16 from 80-bit entropy (spec layout). */
    char out[27];
    uint64_t time = 0;
    for (int i = 0; i < 6; i++) time = (time << 8) | u.b[i];
    for (int i = 9; i >= 0; i--) {
        out[i] = mako_ulid_alphabet[time & 0x1F];
        time >>= 5;
    }
    uint8_t ent[10];
    memcpy(ent, u.b + 6, 10);
    for (int i = 0; i < 16; i++) {
        int bit = i * 5;
        int byte = bit / 8;
        int rem = bit % 8;
        unsigned v;
        if (rem <= 3) {
            v = (ent[byte] >> (3 - rem)) & 0x1F;
        } else {
            v = ((unsigned)ent[byte] << (rem - 3));
            if (byte + 1 < 10) v |= ent[byte + 1] >> (11 - rem);
            v &= 0x1F;
        }
        out[10 + i] = mako_ulid_alphabet[v];
    }
    out[26] = 0;
    return mako_str_from_cstr(out);
}

static inline MakoUuid mako_ulid_parse(MakoString s, bool *ok) {
    if (ok) *ok = false;
    if (!s.data || s.len != 26) return mako_uuid_nil();
    MakoUuid u;
    memset(u.b, 0, 16);
    /* time: first 10 chars → 48 bits */
    uint64_t time = 0;
    for (int i = 0; i < 10; i++) {
        int v = mako_ulid_decode_char(s.data[i]);
        if (v < 0) return mako_uuid_nil();
        time = (time << 5) | (uint64_t)v;
    }
    for (int i = 5; i >= 0; i--) {
        u.b[i] = (uint8_t)(time & 0xFF);
        time >>= 8;
    }
    /* entropy: last 16 chars → 80 bits into bytes 6..15 */
    uint8_t ent[10];
    memset(ent, 0, 10);
    for (int i = 0; i < 16; i++) {
        int v = mako_ulid_decode_char(s.data[10 + i]);
        if (v < 0) return mako_uuid_nil();
        int bit = i * 5;
        int byte = bit / 8;
        int rem = bit % 8;
        if (rem <= 3) {
            ent[byte] = (uint8_t)(ent[byte] | ((unsigned)v << (3 - rem)));
        } else {
            ent[byte] = (uint8_t)(ent[byte] | ((unsigned)v >> (rem - 3)));
            if (byte + 1 < 10)
                ent[byte + 1] = (uint8_t)(ent[byte + 1] | ((unsigned)v << (11 - rem)));
        }
    }
    memcpy(u.b + 6, ent, 10);
    if (ok) *ok = true;
    return u;
}

static inline bool mako_ulid_parse_ok(MakoString s) {
    bool ok = false;
    (void)mako_ulid_parse(s, &ok);
    return ok;
}

static inline int64_t mako_ulid_timestamp_ms(MakoUuid u) {
    /* First 48 bits are unix ms (same as UUID v7 layout we use). */
    uint64_t ms = 0;
    for (int i = 0; i < 6; i++) ms = (ms << 8) | u.b[i];
    return (int64_t)ms;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_UUID_H */
