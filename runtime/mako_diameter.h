/* Diameter (RFC 6733) — one protocol pack over general runtime primitives.
 *
 * General building blocks live elsewhere and are not Diameter-shaped:
 *   timer_heap  (mako_timer.h)  — deadline min-heap
 *   peer_table  (mako_peer.h)   — named peers + string-key routes
 *   tcp / sctp / tls_pool       — transports
 *
 * This header provides Diameter wire codec, per-conn HbH multiplexing,
 * optional mgr/tcm facades (CER/CEA/DWR base handling), and pools that
 * open TCP/SCTP. Prefer composing timer_heap + peer_table + transports
 * for multi-protocol services; use diameter_* only when speaking Diameter.
 *
 * Out of scope: Gx/S6a application state machines, full AVP dictionary,
 * DTLS, multi-stream SCTP per-app-id policy, full proxy/relay agent.
 *
 * You own: application answers, Session-Id policy, business routing.
 */
#ifndef MAKO_DIAMETER_H
#define MAKO_DIAMETER_H

#include "mako_rt.h"
#include "mako_net.h"
#include "mako_sctp.h"
#include "mako_timer.h"
#include "mako_peer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RFC 6733 fixed header size (necessary wire constant). */
#define MAKO_DIAMETER_HEADER_LEN     20
/* Static table capacities (allocation bounds — not policy defaults). */
#define MAKO_DIAMETER_CONN_MAX       64
#define MAKO_DIAMETER_PENDING_MAX    128
#define MAKO_DIAMETER_INQ_MAX        32
#define MAKO_DIAMETER_PEER_MAX       32
#define MAKO_DIAMETER_ROUTE_MAX      64
#define MAKO_DIAMETER_MGR_MAX        8
#define MAKO_DIAMETER_POOL_MAX       32
#define MAKO_DIAMETER_POOL_IDLE      16
#define MAKO_DIAMETER_ORIGIN_MAX     256
#define MAKO_DIAMETER_REALM_MAX      256
#define MAKO_DIAMETER_IDENT_MAX      256
/* Absolute safety ceilings for runtime-configurable limits (DoS only). */
#define MAKO_DIAMETER_LIMIT_CEILING  (16 * 1024 * 1024)

/* RFC 6733 Result-Code values used by auto-answer policy. */
#define MAKO_DIAM_RC_UNABLE_TO_COMPLY 5012
#define MAKO_DIAM_RC_MISSING_AVP      5005

/* Command codes (base protocol, RFC 6733) */
#define MAKO_DIAM_CMD_CER  257
#define MAKO_DIAM_CMD_CEA  257
#define MAKO_DIAM_CMD_DWR  280
#define MAKO_DIAM_CMD_DWA  280
#define MAKO_DIAM_CMD_DPR  282
#define MAKO_DIAM_CMD_DPA  282

/* Flags: R=0x80 P=0x40 E=0x20 T=0x10 (RFC) */
#define MAKO_DIAM_FLAG_R  0x80
#define MAKO_DIAM_FLAG_P  0x40
#define MAKO_DIAM_FLAG_E  0x20
#define MAKO_DIAM_FLAG_T  0x10

/* AVP codes (RFC 6733 base) */
#define MAKO_DIAM_AVP_ORIGIN_HOST    264
#define MAKO_DIAM_AVP_ORIGIN_REALM   296
#define MAKO_DIAM_AVP_HOST_IP        257
#define MAKO_DIAM_AVP_VENDOR_ID      266
#define MAKO_DIAM_AVP_PRODUCT_NAME   269
#define MAKO_DIAM_AVP_RESULT_CODE    268
#define MAKO_DIAM_AVP_ORIGIN_STATE   278
#define MAKO_DIAM_AVP_DISCONNECT_CAUSE 273

/* Runtime limits: 0 = unset (fail closed where a bound is required).
 * Set via diameter_limits_* before accepting untrusted input. */
static size_t mako_diam_limit_max_msg = 0;
static size_t mako_diam_limit_inbuf = 0;
static size_t mako_diam_limit_inq_bytes = 0;
static int64_t mako_diam_limit_reasm_ms = 0; /* 0 = no incomplete-message timeout */

static inline int64_t mako_diameter_limits_set(
    int64_t max_msg, int64_t inbuf_max, int64_t inq_bytes_max, int64_t reasm_timeout_ms
) {
    if (max_msg < 0 || inbuf_max < 0 || inq_bytes_max < 0 || reasm_timeout_ms < 0) return -1;
    if (max_msg > MAKO_DIAMETER_LIMIT_CEILING
        || inbuf_max > MAKO_DIAMETER_LIMIT_CEILING
        || inq_bytes_max > MAKO_DIAMETER_LIMIT_CEILING) {
        return -1;
    }
    /* max_msg 0 leaves framing unbounded only by INT — reject; must be explicit > header */
    if (max_msg > 0 && max_msg < MAKO_DIAMETER_HEADER_LEN) return -1;
    mako_diam_limit_max_msg = (size_t)max_msg;
    mako_diam_limit_inbuf = (size_t)inbuf_max;
    mako_diam_limit_inq_bytes = (size_t)inq_bytes_max;
    mako_diam_limit_reasm_ms = reasm_timeout_ms;
    return 0;
}

static inline int64_t mako_diameter_limits_max_msg(void) {
    return (int64_t)mako_diam_limit_max_msg;
}
static inline int64_t mako_diameter_limits_inbuf(void) {
    return (int64_t)mako_diam_limit_inbuf;
}
static inline int64_t mako_diameter_limits_inq_bytes(void) {
    return (int64_t)mako_diam_limit_inq_bytes;
}
static inline int64_t mako_diameter_limits_reasm_ms(void) {
    return mako_diam_limit_reasm_ms;
}

/* Effective max message: 0 config means reject oversized checks use SIZE_MAX only if set. */
static inline size_t mako_diam_eff_max_msg(void) {
    return mako_diam_limit_max_msg;
}
static inline size_t mako_diam_eff_inbuf(void) {
    return mako_diam_limit_inbuf;
}
static inline size_t mako_diam_eff_inq_bytes(void) {
    return mako_diam_limit_inq_bytes;
}

/* Conn states */
#define MAKO_DIAM_ST_CLOSED    0
#define MAKO_DIAM_ST_WAIT_CEA  1
#define MAKO_DIAM_ST_OPEN      2
#define MAKO_DIAM_ST_CLOSING   3

/* Transport */
#define MAKO_DIAM_TR_TCP   0
#define MAKO_DIAM_TR_SCTP  1
#define MAKO_DIAM_TR_TLS   2

/* ---- byte helpers ---- */
static inline uint32_t mako_diam_u24(const unsigned char *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}
static inline void mako_diam_put_u24(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)((v >> 16) & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)(v & 0xff);
}
static inline uint32_t mako_diam_u32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline void mako_diam_put_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)((v >> 24) & 0xff);
    p[1] = (unsigned char)((v >> 16) & 0xff);
    p[2] = (unsigned char)((v >> 8) & 0xff);
    p[3] = (unsigned char)(v & 0xff);
}

/* ---- framing / header accessors ---- */
static inline int64_t mako_diameter_header_len(void) {
    return MAKO_DIAMETER_HEADER_LEN;
}

static inline int64_t mako_diameter_msg_len(MakoString buf) {
    if (!buf.data || buf.len < 4) return -1;
    const unsigned char *p = (const unsigned char *)buf.data;
    if (p[0] != 1) return -1; /* Version */
    uint32_t len = mako_diam_u24(p + 1);
    if (len < MAKO_DIAMETER_HEADER_LEN) return -1;
    size_t cap = mako_diam_eff_max_msg();
    if (cap > 0 && (size_t)len > cap) return -1;
    return (int64_t)len;
}

static inline int64_t mako_diameter_msg_complete(MakoString buf) {
    int64_t n = mako_diameter_msg_len(buf);
    if (n < 0) return 0;
    return (int64_t)buf.len >= n ? 1 : 0;
}

static inline int64_t mako_diameter_msg_needed(MakoString buf) {
    if (!buf.data || buf.len < 4) {
        return (int64_t)(4 - (buf.data ? buf.len : 0));
    }
    int64_t n = mako_diameter_msg_len(buf);
    if (n < 0) return -1;
    if ((int64_t)buf.len >= n) return 0;
    return n - (int64_t)buf.len;
}

static inline int64_t mako_diameter_first_message_len(MakoString buf) {
    int64_t n = mako_diameter_msg_len(buf);
    if (n < 0) return 0;
    if ((int64_t)buf.len < n) return 0;
    return n;
}

static inline int64_t mako_diameter_version(MakoString msg) {
    if (!msg.data || msg.len < 1) return -1;
    return (unsigned char)msg.data[0];
}

static inline int64_t mako_diameter_flags(MakoString msg) {
    if (!msg.data || msg.len < 5) return -1;
    return (unsigned char)msg.data[4];
}

static inline int64_t mako_diameter_is_request(MakoString msg) {
    int64_t f = mako_diameter_flags(msg);
    return f >= 0 && (f & MAKO_DIAM_FLAG_R) ? 1 : 0;
}

static inline int64_t mako_diameter_is_answer(MakoString msg) {
    int64_t f = mako_diameter_flags(msg);
    return f >= 0 && !(f & MAKO_DIAM_FLAG_R) ? 1 : 0;
}

static inline int64_t mako_diameter_is_error(MakoString msg) {
    int64_t f = mako_diameter_flags(msg);
    return f >= 0 && (f & MAKO_DIAM_FLAG_E) ? 1 : 0;
}

static inline int64_t mako_diameter_cmd(MakoString msg) {
    if (!msg.data || msg.len < 8) return -1;
    return (int64_t)mako_diam_u24((const unsigned char *)msg.data + 5);
}

static inline int64_t mako_diameter_app_id(MakoString msg) {
    if (!msg.data || msg.len < 12) return -1;
    return (int64_t)mako_diam_u32((const unsigned char *)msg.data + 8);
}

static inline int64_t mako_diameter_hbh(MakoString msg) {
    if (!msg.data || msg.len < 16) return -1;
    return (int64_t)mako_diam_u32((const unsigned char *)msg.data + 12);
}

static inline int64_t mako_diameter_e2e(MakoString msg) {
    if (!msg.data || msg.len < 20) return -1;
    return (int64_t)mako_diam_u32((const unsigned char *)msg.data + 16);
}

static inline MakoString mako_diameter_header_build(
    int64_t flags, int64_t cmd, int64_t app, int64_t hbh, int64_t e2e, int64_t body_len
) {
    if (body_len < 0) body_len = 0;
    size_t maxm = mako_diam_eff_max_msg();
    if (maxm > 0
        && (uint64_t)body_len > (uint64_t)(maxm - MAKO_DIAMETER_HEADER_LEN)) {
        return mako_str_from_cstr("");
    }
    uint32_t total = (uint32_t)MAKO_DIAMETER_HEADER_LEN + (uint32_t)body_len;
    if (maxm > 0 && (size_t)total > maxm) return mako_str_from_cstr("");
    char *buf = (char *)malloc(MAKO_DIAMETER_HEADER_LEN + 1);
    if (!buf) return mako_str_from_cstr("");
    unsigned char *p = (unsigned char *)buf;
    p[0] = 1;
    mako_diam_put_u24(p + 1, total);
    p[4] = (unsigned char)(flags & 0xff);
    mako_diam_put_u24(p + 5, (uint32_t)cmd);
    mako_diam_put_u32(p + 8, (uint32_t)app);
    mako_diam_put_u32(p + 12, (uint32_t)hbh);
    mako_diam_put_u32(p + 16, (uint32_t)e2e);
    buf[MAKO_DIAMETER_HEADER_LEN] = 0;
    return (MakoString){buf, MAKO_DIAMETER_HEADER_LEN};
}

static inline MakoString mako_diameter_msg_build(
    int64_t flags, int64_t cmd, int64_t app, int64_t hbh, int64_t e2e, MakoString avps
) {
    size_t blen = avps.data ? avps.len : 0;
    size_t maxm = mako_diam_eff_max_msg();
    if (maxm > 0 && blen > maxm - MAKO_DIAMETER_HEADER_LEN) {
        return mako_str_from_cstr("");
    }
    MakoString hdr = mako_diameter_header_build(flags, cmd, app, hbh, e2e, (int64_t)blen);
    if (!hdr.data || hdr.len == 0) return mako_str_from_cstr("");
    if (blen == 0) return hdr;
    /* Guard wrap: hdr.len is 20; blen already capped above. */
    size_t total = hdr.len + blen;
    if (total < hdr.len) {
        mako_str_free(hdr);
        return mako_str_from_cstr("");
    }
    char *out = (char *)malloc(total + 1);
    if (!out) {
        mako_str_free(hdr);
        return mako_str_from_cstr("");
    }
    memcpy(out, hdr.data, hdr.len);
    if (blen && avps.data) memcpy(out + hdr.len, avps.data, blen);
    out[total] = 0;
    size_t hlen = hdr.len;
    mako_str_free(hdr);
    return (MakoString){out, hlen + blen};
}

static inline MakoString mako_diameter_set_hbh(MakoString msg, int64_t hbh) {
    if (!msg.data || msg.len < 16) return mako_str_from_cstr("");
    char *d = (char *)malloc(msg.len + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, msg.data, msg.len);
    d[msg.len] = 0;
    mako_diam_put_u32((unsigned char *)d + 12, (uint32_t)hbh);
    return (MakoString){d, msg.len};
}

static inline MakoString mako_diameter_set_e2e(MakoString msg, int64_t e2e) {
    if (!msg.data || msg.len < 20) return mako_str_from_cstr("");
    char *d = (char *)malloc(msg.len + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, msg.data, msg.len);
    d[msg.len] = 0;
    mako_diam_put_u32((unsigned char *)d + 16, (uint32_t)e2e);
    return (MakoString){d, msg.len};
}

/* ---- AVP encode/decode ---- */
/* AVP header: Code(4) Flags(1) Length(3) [Vendor-Id(4)] Value + pad to 4 */
static inline MakoString mako_diameter_avp_encode(
    int64_t code, int64_t flags, int64_t vendor, MakoString value
) {
    int vbit = (flags & 0x80) || vendor != 0;
    if (vendor != 0) flags |= 0x80;
    size_t vlen = value.data ? value.len : 0;
    size_t hdr = 8 + (vbit ? 4 : 0);
    size_t maxm = mako_diam_eff_max_msg();
    /* Overflow-safe length check before raw/pad arithmetic. */
    if (maxm > 0 && (vlen > maxm || hdr > maxm - vlen)) {
        return mako_str_from_cstr("");
    }
    size_t raw = hdr + vlen;
    size_t pad = (4 - (raw % 4)) % 4;
    if (maxm > 0 && raw > maxm - pad) return mako_str_from_cstr("");
    size_t total = raw + pad;
    char *buf = (char *)malloc(total + 1);
    if (!buf) return mako_str_from_cstr("");
    unsigned char *p = (unsigned char *)buf;
    mako_diam_put_u32(p, (uint32_t)code);
    p[4] = (unsigned char)(flags & 0xff);
    mako_diam_put_u24(p + 5, (uint32_t)(hdr + vlen));
    size_t off = 8;
    if (vbit) {
        mako_diam_put_u32(p + 8, (uint32_t)vendor);
        off = 12;
    }
    if (vlen && value.data) memcpy(buf + off, value.data, vlen);
    if (pad) memset(buf + off + vlen, 0, pad);
    buf[total] = 0;
    return (MakoString){buf, total};
}

static inline MakoString mako_diameter_avp_put_u32(int64_t code, int64_t flags, int64_t vendor, int64_t n) {
    unsigned char tmp[4];
    mako_diam_put_u32(tmp, (uint32_t)n);
    /* Stack buffer is only borrowed for the duration of encode's memcpy. */
    MakoString owned = {(char *)tmp, 4};
    return mako_diameter_avp_encode(code, flags, vendor, owned);
}

static inline MakoString mako_diameter_avp_put_str(
    int64_t code, int64_t flags, int64_t vendor, MakoString s
) {
    return mako_diameter_avp_encode(code, flags, vendor, s);
}

/* Parse first AVP starting at offset; returns value slice as owned string.
 * Sets *next_off to padded end. Returns empty on fail. */
static inline int mako_diameter_avp_parse_at(
    MakoString msg, size_t off,
    uint32_t *code_out, uint8_t *flags_out, uint32_t *vendor_out,
    const char **val_out, size_t *vlen_out, size_t *next_out
) {
    if (!msg.data || off + 8 > msg.len) return 0;
    const unsigned char *p = (const unsigned char *)msg.data + off;
    uint32_t code = mako_diam_u32(p);
    uint8_t flags = p[4];
    uint32_t alen = mako_diam_u24(p + 5);
    if (alen < 8 || off + alen > msg.len) return 0;
    size_t hdr = 8;
    uint32_t vendor = 0;
    if (flags & 0x80) {
        if (alen < 12) return 0;
        vendor = mako_diam_u32(p + 8);
        hdr = 12;
    }
    size_t vlen = alen - hdr;
    size_t pad = (4 - (alen % 4)) % 4;
    size_t next = off + alen + pad;
    if (next > msg.len) next = off + alen; /* tolerate missing pad at end */
    if (code_out) *code_out = code;
    if (flags_out) *flags_out = flags;
    if (vendor_out) *vendor_out = vendor;
    if (val_out) *val_out = msg.data + off + hdr;
    if (vlen_out) *vlen_out = vlen;
    if (next_out) *next_out = next;
    return 1;
}

static inline MakoString mako_diameter_avp_at(MakoString msg, int64_t index) {
    if (!msg.data || msg.len < MAKO_DIAMETER_HEADER_LEN || index < 0) {
        return mako_str_from_cstr("");
    }
    size_t off = MAKO_DIAMETER_HEADER_LEN;
    int64_t i = 0;
    while (off + 8 <= msg.len) {
        uint32_t code;
        uint8_t flags;
        uint32_t vendor;
        const char *val;
        size_t vlen, next;
        if (!mako_diameter_avp_parse_at(msg, off, &code, &flags, &vendor, &val, &vlen, &next)) {
            break;
        }
        if (i == index) {
            /* return full AVP including header+pad */
            size_t alen = next - off;
            char *d = (char *)malloc(alen + 1);
            if (!d) return mako_str_from_cstr("");
            memcpy(d, msg.data + off, alen);
            d[alen] = 0;
            return (MakoString){d, alen};
        }
        i++;
        off = next;
    }
    return mako_str_from_cstr("");
}

static inline MakoString mako_diameter_avp_find(MakoString msg, int64_t code) {
    if (!msg.data || msg.len < MAKO_DIAMETER_HEADER_LEN) return mako_str_from_cstr("");
    size_t off = MAKO_DIAMETER_HEADER_LEN;
    while (off + 8 <= msg.len) {
        uint32_t c;
        uint8_t flags;
        uint32_t vendor;
        const char *val;
        size_t vlen, next;
        if (!mako_diameter_avp_parse_at(msg, off, &c, &flags, &vendor, &val, &vlen, &next)) break;
        if ((int64_t)c == code) {
            char *d = (char *)malloc(vlen + 1);
            if (!d) return mako_str_from_cstr("");
            if (vlen) memcpy(d, val, vlen);
            d[vlen] = 0;
            return (MakoString){d, vlen};
        }
        off = next;
    }
    return mako_str_from_cstr("");
}

static inline MakoString mako_diameter_avp_find_vendor(
    MakoString msg, int64_t code, int64_t vendor
) {
    if (!msg.data || msg.len < MAKO_DIAMETER_HEADER_LEN) return mako_str_from_cstr("");
    size_t off = MAKO_DIAMETER_HEADER_LEN;
    while (off + 8 <= msg.len) {
        uint32_t c;
        uint8_t flags;
        uint32_t v;
        const char *val;
        size_t vlen, next;
        if (!mako_diameter_avp_parse_at(msg, off, &c, &flags, &v, &val, &vlen, &next)) break;
        if ((int64_t)c == code && (int64_t)v == vendor) {
            char *d = (char *)malloc(vlen + 1);
            if (!d) return mako_str_from_cstr("");
            if (vlen) memcpy(d, val, vlen);
            d[vlen] = 0;
            return (MakoString){d, vlen};
        }
        off = next;
    }
    return mako_str_from_cstr("");
}

static inline int64_t mako_diameter_avp_u32(MakoString value) {
    if (!value.data || value.len < 4) return -1;
    return (int64_t)mako_diam_u32((const unsigned char *)value.data);
}

static inline MakoString mako_diameter_avp_str(MakoString value) {
    if (!value.data) return mako_str_from_cstr("");
    char *d = (char *)malloc(value.len + 1);
    if (!d) return mako_str_from_cstr("");
    if (value.len) memcpy(d, value.data, value.len);
    d[value.len] = 0;
    return (MakoString){d, value.len};
}

static inline int64_t mako_diameter_result_code(MakoString msg) {
    MakoString v = mako_diameter_avp_find(msg, MAKO_DIAM_AVP_RESULT_CODE);
    int64_t n = mako_diameter_avp_u32(v);
    mako_str_free(v);
    return n;
}

/* ---- identity keys / generators ---- */
static inline MakoString mako_diameter_txn_key(int64_t hbh, int64_t app_id) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%u|%u", (unsigned)hbh, (unsigned)app_id);
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_diameter_e2e_key(int64_t e2e, int64_t app_id) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%u|%u", (unsigned)e2e, (unsigned)app_id);
    return mako_str_from_cstr(buf);
}

static inline MakoString mako_diameter_session_key(MakoString session_id) {
    if (!session_id.data || session_id.len == 0) return mako_str_from_cstr("");
    char *d = (char *)malloc(session_id.len + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, session_id.data, session_id.len);
    d[session_id.len] = 0;
    return (MakoString){d, session_id.len};
}

static uint32_t mako_diam_hbh_counter = 1;
static uint32_t mako_diam_e2e_counter = 1;
static pthread_mutex_t mako_diam_id_mu = MAKO_MUTEX_INIT;

static inline int64_t mako_diameter_hbh_new(void) {
    pthread_mutex_lock(&mako_diam_id_mu);
    uint32_t v = mako_diam_hbh_counter++;
    if (v == 0) v = mako_diam_hbh_counter++;
    pthread_mutex_unlock(&mako_diam_id_mu);
    return (int64_t)v;
}

static inline int64_t mako_diameter_e2e_new(void) {
    pthread_mutex_lock(&mako_diam_id_mu);
    uint32_t v = mako_diam_e2e_counter++;
    if (v == 0) v = mako_diam_e2e_counter++;
    pthread_mutex_unlock(&mako_diam_id_mu);
    return (int64_t)v;
}

/* ---- pure base-protocol builders ---- */
static inline MakoString mako_diameter_avps_origin(
    MakoString origin_host, MakoString origin_realm
) {
    MakoString h = mako_diameter_avp_put_str(
        MAKO_DIAM_AVP_ORIGIN_HOST, 0x40, 0, origin_host
    );
    MakoString r = mako_diameter_avp_put_str(
        MAKO_DIAM_AVP_ORIGIN_REALM, 0x40, 0, origin_realm
    );
    MakoString out = mako_str_concat(h, r);
    mako_str_free(h);
    mako_str_free(r);
    return out;
}

static inline MakoString mako_diameter_build_cer(
    MakoString origin_host, MakoString origin_realm, int64_t hbh, int64_t e2e
) {
    MakoString avps = mako_diameter_avps_origin(origin_host, origin_realm);
    MakoString vendor = mako_diameter_avp_put_u32(MAKO_DIAM_AVP_VENDOR_ID, 0x40, 0, 0);
    MakoString pname = mako_str_from_cstr("mako");
    MakoString prod = mako_diameter_avp_put_str(
        MAKO_DIAM_AVP_PRODUCT_NAME, 0, 0, pname
    );
    mako_str_free(pname);
    MakoString mid = mako_str_concat(avps, vendor);
    MakoString all = mako_str_concat(mid, prod);
    mako_str_free(avps);
    mako_str_free(vendor);
    mako_str_free(prod);
    mako_str_free(mid);
    MakoString msg = mako_diameter_msg_build(
        MAKO_DIAM_FLAG_R | MAKO_DIAM_FLAG_P, MAKO_DIAM_CMD_CER, 0, hbh, e2e, all
    );
    mako_str_free(all);
    return msg;
}

static inline MakoString mako_diameter_build_cea(
    MakoString req, MakoString origin_host, MakoString origin_realm, int64_t result
) {
    int64_t hbh = mako_diameter_hbh(req);
    int64_t e2e = mako_diameter_e2e(req);
    if (hbh < 0) hbh = 0;
    if (e2e < 0) e2e = 0;
    MakoString avps = mako_diameter_avps_origin(origin_host, origin_realm);
    MakoString rc = mako_diameter_avp_put_u32(
        MAKO_DIAM_AVP_RESULT_CODE, 0x40, 0, result
    );
    MakoString all = mako_str_concat(rc, avps);
    mako_str_free(rc);
    mako_str_free(avps);
    MakoString msg = mako_diameter_msg_build(
        MAKO_DIAM_FLAG_P, MAKO_DIAM_CMD_CEA, 0, hbh, e2e, all
    );
    mako_str_free(all);
    return msg;
}

static inline MakoString mako_diameter_build_dwr(
    MakoString origin_host, MakoString origin_realm, int64_t hbh, int64_t e2e
) {
    MakoString avps = mako_diameter_avps_origin(origin_host, origin_realm);
    MakoString msg = mako_diameter_msg_build(
        MAKO_DIAM_FLAG_R | MAKO_DIAM_FLAG_P, MAKO_DIAM_CMD_DWR, 0, hbh, e2e, avps
    );
    mako_str_free(avps);
    return msg;
}

static inline MakoString mako_diameter_build_dwa(
    MakoString req, MakoString origin_host, MakoString origin_realm, int64_t result
) {
    int64_t hbh = mako_diameter_hbh(req);
    int64_t e2e = mako_diameter_e2e(req);
    if (hbh < 0) hbh = 0;
    if (e2e < 0) e2e = 0;
    MakoString avps = mako_diameter_avps_origin(origin_host, origin_realm);
    MakoString rc = mako_diameter_avp_put_u32(
        MAKO_DIAM_AVP_RESULT_CODE, 0x40, 0, result
    );
    MakoString all = mako_str_concat(rc, avps);
    mako_str_free(rc);
    mako_str_free(avps);
    MakoString msg = mako_diameter_msg_build(
        MAKO_DIAM_FLAG_P, MAKO_DIAM_CMD_DWA, 0, hbh, e2e, all
    );
    mako_str_free(all);
    return msg;
}

static inline MakoString mako_diameter_build_dpr(
    MakoString origin_host, MakoString origin_realm, int64_t cause, int64_t hbh, int64_t e2e
) {
    MakoString avps = mako_diameter_avps_origin(origin_host, origin_realm);
    MakoString c = mako_diameter_avp_put_u32(
        MAKO_DIAM_AVP_DISCONNECT_CAUSE, 0x40, 0, cause
    );
    MakoString all = mako_str_concat(avps, c);
    mako_str_free(avps);
    mako_str_free(c);
    MakoString msg = mako_diameter_msg_build(
        MAKO_DIAM_FLAG_R | MAKO_DIAM_FLAG_P, MAKO_DIAM_CMD_DPR, 0, hbh, e2e, all
    );
    mako_str_free(all);
    return msg;
}

static inline MakoString mako_diameter_build_dpa(
    MakoString req, MakoString origin_host, MakoString origin_realm, int64_t result
) {
    int64_t hbh = mako_diameter_hbh(req);
    int64_t e2e = mako_diameter_e2e(req);
    if (hbh < 0) hbh = 0;
    if (e2e < 0) e2e = 0;
    MakoString avps = mako_diameter_avps_origin(origin_host, origin_realm);
    MakoString rc = mako_diameter_avp_put_u32(
        MAKO_DIAM_AVP_RESULT_CODE, 0x40, 0, result
    );
    MakoString all = mako_str_concat(rc, avps);
    mako_str_free(rc);
    mako_str_free(avps);
    MakoString msg = mako_diameter_msg_build(
        MAKO_DIAM_FLAG_P, MAKO_DIAM_CMD_DPA, 0, hbh, e2e, all
    );
    mako_str_free(all);
    return msg;
}

/* ---- connection state machine ---- */
typedef struct {
    int used;
    uint32_t hbh;
    uint32_t e2e;
    uint32_t cmd;
    uint32_t app;
    int64_t sent_ms;
} MakoDiamPending;

typedef struct {
    int live;
    int state;
    uint32_t next_hbh;
    uint32_t gen; /* ABA guard: high bits of handle */
    char origin_host[MAKO_DIAMETER_ORIGIN_MAX];
    char origin_realm[MAKO_DIAMETER_REALM_MAX];
    size_t origin_host_len;
    size_t origin_realm_len;
    /* reassembly */
    char *inbuf;
    size_t inlen;
    size_t incap;
    int64_t last_byte_ms; /* incomplete reassembly watchdog */
    /* complete inbound queue */
    MakoString inq[MAKO_DIAMETER_INQ_MAX];
    int inq_n;
    size_t inq_bytes; /* sum of inq[].len — memory budget */
    /* outbound */
    char *outbuf;
    size_t outlen;
    size_t outcap;
    MakoDiamPending pending[MAKO_DIAMETER_PENDING_MAX];
} MakoDiamConn;

/* Handle layout: low 16 bits = slot+1, high 16 bits = generation (non-zero). */
static inline int64_t mako_diam_pack_handle(int slot, uint32_t gen) {
    if (gen == 0) gen = 1;
    return (int64_t)(((uint32_t)(slot + 1) & 0xffffu) | ((gen & 0xffffu) << 16));
}
static inline int mako_diam_handle_slot(int64_t h) {
    int id = (int)(h & 0xffff);
    if (id <= 0 || id > MAKO_DIAMETER_CONN_MAX) return -1;
    return id - 1;
}
static inline uint32_t mako_diam_handle_gen(int64_t h) {
    return (uint32_t)((h >> 16) & 0xffffu);
}

static MakoDiamConn mako_diam_conns[MAKO_DIAMETER_CONN_MAX];
static pthread_mutex_t mako_diam_conn_mu = MAKO_MUTEX_INIT;

static inline void mako_diam_conn_clear_bufs(MakoDiamConn *c) {
    if (c->inbuf) {
        free(c->inbuf);
        c->inbuf = NULL;
    }
    c->inlen = c->incap = 0;
    c->last_byte_ms = 0;
    if (c->outbuf) {
        free(c->outbuf);
        c->outbuf = NULL;
    }
    c->outlen = c->outcap = 0;
    for (int i = 0; i < c->inq_n; i++) mako_str_free(c->inq[i]);
    c->inq_n = 0;
    c->inq_bytes = 0;
    memset(c->pending, 0, sizeof(c->pending));
}

/* Drop reassembly buffer entirely (fail-closed framing). */
static inline void mako_diam_conn_drop_inbuf(MakoDiamConn *c) {
    if (c->inbuf) {
        free(c->inbuf);
        c->inbuf = NULL;
    }
    c->inlen = c->incap = 0;
    c->last_byte_ms = 0;
}

static inline int mako_diam_conn_live_locked(int64_t h, MakoDiamConn **out) {
    int slot = mako_diam_handle_slot(h);
    if (slot < 0) return 0;
    MakoDiamConn *c = &mako_diam_conns[slot];
    if (!c->live) return 0;
    uint32_t g = mako_diam_handle_gen(h);
    /* gen 0: legacy/compat accept only if slot gen is also 0 (should not happen) */
    if (g != 0 && c->gen != g) return 0;
    if (g == 0 && c->gen != 0) return 0;
    if (out) *out = c;
    return 1;
}

static inline int64_t mako_diameter_conn_new(void) {
    pthread_mutex_lock(&mako_diam_conn_mu);
    int slot = -1;
    for (int i = 0; i < MAKO_DIAMETER_CONN_MAX; i++) {
        if (!mako_diam_conns[i].live) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1;
    }
    MakoDiamConn *c = &mako_diam_conns[slot];
    uint32_t prev_gen = c->gen;
    memset(c, 0, sizeof(*c));
    c->live = 1;
    c->state = MAKO_DIAM_ST_CLOSED;
    c->next_hbh = 1;
    c->gen = (prev_gen + 1) & 0xffffu;
    if (c->gen == 0) c->gen = 1;
    int64_t handle = mako_diam_pack_handle(slot, c->gen);
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return handle;
}

static inline int64_t mako_diameter_conn_free(int64_t h) {
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    if (!mako_diam_conn_live_locked(h, &c)) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1;
    }
    uint32_t gen = c->gen;
    mako_diam_conn_clear_bufs(c);
    memset(c, 0, sizeof(*c));
    c->gen = gen; /* keep gen so next new() increments; live=0 */
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return 0;
}

static inline int64_t mako_diameter_conn_reset(int64_t h) {
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    if (!mako_diam_conn_live_locked(h, &c)) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1;
    }
    mako_diam_conn_clear_bufs(c);
    c->state = MAKO_DIAM_ST_CLOSED;
    c->next_hbh = 1;
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return 0;
}

static inline int64_t mako_diameter_conn_set_origin(
    int64_t h, MakoString host, MakoString realm
) {
    if (!host.data || host.len == 0 || host.len >= MAKO_DIAMETER_ORIGIN_MAX) return -1;
    if (!realm.data || realm.len == 0 || realm.len >= MAKO_DIAMETER_REALM_MAX) return -1;
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    if (!mako_diam_conn_live_locked(h, &c)) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1;
    }
    memcpy(c->origin_host, host.data, host.len);
    c->origin_host[host.len] = 0;
    c->origin_host_len = host.len;
    memcpy(c->origin_realm, realm.data, realm.len);
    c->origin_realm[realm.len] = 0;
    c->origin_realm_len = realm.len;
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return 0;
}

static inline int64_t mako_diameter_conn_state(int64_t h) {
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    int st = -1;
    if (mako_diam_conn_live_locked(h, &c)) st = c->state;
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return st;
}

static inline int64_t mako_diameter_conn_set_state(int64_t h, int64_t st) {
    if (st < 0 || st > MAKO_DIAM_ST_CLOSING) return -1;
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    if (!mako_diam_conn_live_locked(h, &c)) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1;
    }
    c->state = (int)st;
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return 0;
}

/* Append bytes; always keep capacity for a trailing NUL so take_out is safe.
 * max_total bounds the reassembly buffer (adversarial incomplete-msg stall). */
static inline int mako_diam_buf_append_max(
    char **buf, size_t *len, size_t *cap, const char *data, size_t n, size_t max_total
) {
    if (n == 0) return 1;
    if (!data) return 0;
    if (n > max_total || *len > max_total - n) return 0;
    size_t need = *len + n;
    size_t need_cap = need + 1;
    if (need_cap < need) return 0;
    if (need_cap > *cap) {
        size_t nc = *cap ? *cap : 4096;
        while (nc < need_cap) {
            if (nc > SIZE_MAX / 2) return 0;
            nc *= 2;
        }
        char *p = (char *)realloc(*buf, nc);
        if (!p) return 0;
        *buf = p;
        *cap = nc;
    }
    memcpy(*buf + *len, data, n);
    *len = need;
    (*buf)[*len] = 0;
    return 1;
}

static inline int mako_diam_buf_append(
    char **buf, size_t *len, size_t *cap, const char *data, size_t n
) {
    size_t lim = mako_diam_eff_inbuf();
    if (lim == 0) return 0; /* inbuf limit must be configured */
    return mako_diam_buf_append_max(buf, len, cap, data, n, lim);
}

/* Fail-closed extract: invalid framing drops the whole reassembly buffer.
 * No single-byte memmove resync (A1 CPU bomb). */
static inline void mako_diam_conn_extract_msgs(MakoDiamConn *c) {
    while (c->inlen >= 4 && c->inq_n < MAKO_DIAMETER_INQ_MAX) {
        MakoString view = {c->inbuf, c->inlen};
        int64_t mlen = mako_diameter_msg_len(view);
        if (mlen < 0) {
            /* Hostile/corrupt stream — drop all partial input. */
            mako_diam_conn_drop_inbuf(c);
            return;
        }
        if (c->inlen < (size_t)mlen) {
            /* Incomplete: wait for more (subject to reasm timeout). */
            return;
        }
        size_t inq_lim = mako_diam_eff_inq_bytes();
        /* mlen is wire-supplied: check it against the budget before subtracting
         * (mlen > inq_lim would wrap and let the message through). */
        if (inq_lim > 0
            && ((size_t)mlen > inq_lim || c->inq_bytes > inq_lim - (size_t)mlen)) {
            /* Memory budget: refuse to queue more complete messages. */
            return;
        }
        char *d = (char *)malloc((size_t)mlen + 1);
        if (!d) return;
        memcpy(d, c->inbuf, (size_t)mlen);
        d[mlen] = 0;
        c->inq[c->inq_n++] = (MakoString){d, (size_t)mlen};
        c->inq_bytes += (size_t)mlen;
        memmove(c->inbuf, c->inbuf + mlen, c->inlen - (size_t)mlen);
        c->inlen -= (size_t)mlen;
        if (c->inlen == 0) c->last_byte_ms = 0;
    }
}

static inline int64_t mako_diameter_conn_feed(int64_t h, MakoString bytes) {
    if (!bytes.data || bytes.len == 0) return 0;
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    if (!mako_diam_conn_live_locked(h, &c)) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1;
    }
    /* Incomplete reassembly timeout (0 = disabled). */
    int64_t now = mako_mono_ms();
    int64_t reasm_ms = mako_diam_limit_reasm_ms;
    if (reasm_ms > 0 && c->inlen > 0 && c->last_byte_ms > 0
        && now - c->last_byte_ms > reasm_ms) {
        mako_diam_conn_drop_inbuf(c);
    }
    size_t inbuf_lim = mako_diam_eff_inbuf();
    if (inbuf_lim == 0) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1; /* must configure diameter_limits_set */
    }
    if (!mako_diam_buf_append_max(
            &c->inbuf, &c->inlen, &c->incap, bytes.data, bytes.len, inbuf_lim)) {
        /* Oversize reassembly — fail closed. */
        mako_diam_conn_drop_inbuf(c);
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1;
    }
    c->last_byte_ms = now;
    mako_diam_conn_extract_msgs(c);
    int n = c->inq_n;
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return (int64_t)n;
}

static inline MakoString mako_diameter_conn_pop(int64_t h) {
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    if (!mako_diam_conn_live_locked(h, &c) || c->inq_n <= 0) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return mako_str_from_cstr("");
    }
    MakoString out = c->inq[0];
    if (c->inq_bytes >= out.len) c->inq_bytes -= out.len;
    else c->inq_bytes = 0;
    for (int i = 1; i < c->inq_n; i++) c->inq[i - 1] = c->inq[i];
    c->inq_n--;
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return out;
}

static inline int64_t mako_diameter_conn_pending(int64_t h) {
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    int n = mako_diam_conn_live_locked(h, &c) ? c->inq_n : -1;
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return (int64_t)n;
}

static inline int64_t mako_diameter_conn_queue_out(int64_t h, MakoString msg) {
    if (!msg.data || msg.len == 0) return 0;
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    if (!mako_diam_conn_live_locked(h, &c)) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1;
    }
    size_t out_lim = mako_diam_eff_inbuf();
    if (out_lim == 0) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1;
    }
    int ok = mako_diam_buf_append_max(
        &c->outbuf, &c->outlen, &c->outcap, msg.data, msg.len, out_lim
    );
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return ok ? (int64_t)msg.len : -1;
}

static inline MakoString mako_diameter_conn_take_out(int64_t h) {
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    if (!mako_diam_conn_live_locked(h, &c) || c->outlen == 0 || !c->outbuf) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return mako_str_from_cstr("");
    }
    if (c->outcap < c->outlen + 1) {
        char *p = (char *)realloc(c->outbuf, c->outlen + 1);
        if (!p) {
            pthread_mutex_unlock(&mako_diam_conn_mu);
            return mako_str_from_cstr("");
        }
        c->outbuf = p;
        c->outcap = c->outlen + 1;
    }
    c->outbuf[c->outlen] = 0;
    MakoString out = {c->outbuf, c->outlen};
    c->outbuf = NULL;
    c->outlen = c->outcap = 0;
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return out;
}

static inline int64_t mako_diameter_conn_next_hbh(int64_t h) {
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    if (!mako_diam_conn_live_locked(h, &c)) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1;
    }
    uint32_t v = c->next_hbh++;
    if (v == 0) v = c->next_hbh++;
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return (int64_t)v;
}

static inline int64_t mako_diameter_conn_register(
    int64_t h, int64_t hbh, int64_t e2e, int64_t cmd, int64_t app
) {
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    if (!mako_diam_conn_live_locked(h, &c)) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1;
    }
    int idx = -1;
    for (int i = 0; i < MAKO_DIAMETER_PENDING_MAX; i++) {
        if (!c->pending[i].used) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1;
    }
    c->pending[idx].used = 1;
    c->pending[idx].hbh = (uint32_t)hbh;
    c->pending[idx].e2e = (uint32_t)e2e;
    c->pending[idx].cmd = (uint32_t)cmd;
    c->pending[idx].app = (uint32_t)app;
    c->pending[idx].sent_ms = mako_mono_ms();
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return 0;
}

/* Strict match: answer bit + hbh + app_id + command-code (A5). */
static inline int64_t mako_diameter_conn_match(int64_t h, MakoString answer) {
    if (!mako_diameter_is_answer(answer)) return 0;
    int64_t hbh = mako_diameter_hbh(answer);
    int64_t app = mako_diameter_app_id(answer);
    int64_t cmd = mako_diameter_cmd(answer);
    if (hbh < 0 || cmd < 0) return 0;
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    if (!mako_diam_conn_live_locked(h, &c)) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return 0;
    }
    int found = 0;
    for (int i = 0; i < MAKO_DIAMETER_PENDING_MAX; i++) {
        if (c->pending[i].used
            && c->pending[i].hbh == (uint32_t)hbh
            && c->pending[i].app == (uint32_t)app
            && c->pending[i].cmd == (uint32_t)cmd) {
            c->pending[i].used = 0;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return found ? 1 : 0;
}

static inline int64_t mako_diameter_conn_expire(
    int64_t h, int64_t now_ms, int64_t timeout_ms
) {
    if (timeout_ms <= 0) {
        return -1; /* no invented default timeout */
    }
    pthread_mutex_lock(&mako_diam_conn_mu);
    MakoDiamConn *c = NULL;
    if (!mako_diam_conn_live_locked(h, &c)) {
        pthread_mutex_unlock(&mako_diam_conn_mu);
        return -1;
    }
    /* Also drop incomplete reassembly on expire pass when configured. */
    int64_t reasm_ms = mako_diam_limit_reasm_ms;
    if (reasm_ms > 0 && c->inlen > 0 && c->last_byte_ms > 0
        && now_ms - c->last_byte_ms > reasm_ms) {
        mako_diam_conn_drop_inbuf(c);
    }
    int n = 0;
    for (int i = 0; i < MAKO_DIAMETER_PENDING_MAX; i++) {
        if (c->pending[i].used
            && now_ms - c->pending[i].sent_ms >= timeout_ms) {
            c->pending[i].used = 0;
            n++;
        }
    }
    pthread_mutex_unlock(&mako_diam_conn_mu);
    return (int64_t)n;
}

static inline int64_t mako_diameter_conn_on_cea(int64_t h, MakoString cea) {
    if (!mako_diameter_is_answer(cea) || mako_diameter_cmd(cea) != MAKO_DIAM_CMD_CEA) {
        return -1;
    }
    int64_t rc = mako_diameter_result_code(cea);
    if (rc != 2001 && rc != 2002) {
        mako_diameter_conn_set_state(h, MAKO_DIAM_ST_CLOSED);
        return -1;
    }
    mako_diameter_conn_match(h, cea);
    return mako_diameter_conn_set_state(h, MAKO_DIAM_ST_OPEN);
}

/* ---- connection pool (raw fd associations) ---- */
typedef struct {
    int live;
    char host[256];
    int port;
    int max;
    int timeout_ms;
    int transport; /* TCP/SCTP */
    int idle_fds[MAKO_DIAMETER_POOL_IDLE];
    int idle_n;
    int open_n;
} MakoDiamPool;

static MakoDiamPool mako_diam_pools[MAKO_DIAMETER_POOL_MAX];
static pthread_mutex_t mako_diam_pool_mu = MAKO_MUTEX_INIT;

static inline int64_t mako_diameter_pool_open(
    MakoString host, int64_t port, int64_t max, int64_t timeout_ms, int64_t transport
) {
    if (!host.data || host.len == 0 || host.len >= 256) return -1;
    if (port <= 0 || port > 65535) return -1;
    if (transport != MAKO_DIAM_TR_TCP && transport != MAKO_DIAM_TR_SCTP) return -1;
    if (max <= 0) return -1; /* capacity required */
    if (max > MAKO_DIAMETER_POOL_IDLE) max = MAKO_DIAMETER_POOL_IDLE;
    if (timeout_ms <= 0) return -1; /* connect timeout required */
    pthread_mutex_lock(&mako_diam_pool_mu);
    int slot = -1;
    for (int i = 0; i < MAKO_DIAMETER_POOL_MAX; i++) {
        if (!mako_diam_pools[i].live) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&mako_diam_pool_mu);
        return -1;
    }
    MakoDiamPool *p = &mako_diam_pools[slot];
    memset(p, 0, sizeof(*p));
    memcpy(p->host, host.data, host.len);
    p->host[host.len] = 0;
    p->port = (int)port;
    p->max = (int)max;
    p->timeout_ms = (int)timeout_ms;
    p->transport = (int)transport;
    p->live = 1;
    pthread_mutex_unlock(&mako_diam_pool_mu);
    return (int64_t)slot;
}

static inline int64_t mako_diam_pool_connect_one(MakoDiamPool *p) {
    MakoString h = {p->host, strlen(p->host)};
    /* timeout_ms must have been set on open — no invented default. */
    if (p->timeout_ms <= 0) return -1;
    int64_t cto = (int64_t)p->timeout_ms;
    if (p->transport == MAKO_DIAM_TR_SCTP) {
        return mako_sctp_connect_timeout(h, p->port, cto);
    }
    return mako_tcp_connect_timeout(h, p->port, cto);
}

static inline int64_t mako_diameter_pool_acquire(int64_t pool) {
    if (pool < 0 || pool >= MAKO_DIAMETER_POOL_MAX) return -1;
    pthread_mutex_lock(&mako_diam_pool_mu);
    MakoDiamPool *p = &mako_diam_pools[pool];
    if (!p->live) {
        pthread_mutex_unlock(&mako_diam_pool_mu);
        return -1;
    }
    while (p->idle_n > 0) {
        int fd = p->idle_fds[--p->idle_n];
        if (mako_sock_error(fd) == 0) {
            pthread_mutex_unlock(&mako_diam_pool_mu);
            return (int64_t)fd;
        }
        mako_sock_close(fd);
        p->open_n--;
    }
    if (p->open_n >= p->max) {
        pthread_mutex_unlock(&mako_diam_pool_mu);
        return -1;
    }
    int64_t fd = mako_diam_pool_connect_one(p);
    if (fd < 0) {
        pthread_mutex_unlock(&mako_diam_pool_mu);
        return -1;
    }
    p->open_n++;
    pthread_mutex_unlock(&mako_diam_pool_mu);
    return fd;
}

static inline int64_t mako_diameter_pool_release(int64_t pool, int64_t fd, int64_t reusable) {
    if (pool < 0 || pool >= MAKO_DIAMETER_POOL_MAX || fd < 0) return 0;
    pthread_mutex_lock(&mako_diam_pool_mu);
    MakoDiamPool *p = &mako_diam_pools[pool];
    if (!p->live) {
        pthread_mutex_unlock(&mako_diam_pool_mu);
        return 0;
    }
    if (reusable && p->idle_n < p->max && p->idle_n < MAKO_DIAMETER_POOL_IDLE) {
        p->idle_fds[p->idle_n++] = (int)fd;
        pthread_mutex_unlock(&mako_diam_pool_mu);
        return 1;
    }
    mako_sock_close((int)fd);
    if (p->open_n > 0) p->open_n--;
    pthread_mutex_unlock(&mako_diam_pool_mu);
    return 1;
}

static inline int64_t mako_diameter_pool_close(int64_t pool) {
    if (pool < 0 || pool >= MAKO_DIAMETER_POOL_MAX) return 0;
    pthread_mutex_lock(&mako_diam_pool_mu);
    MakoDiamPool *p = &mako_diam_pools[pool];
    if (!p->live) {
        pthread_mutex_unlock(&mako_diam_pool_mu);
        return 0;
    }
    for (int i = 0; i < p->idle_n; i++) mako_sock_close(p->idle_fds[i]);
    memset(p, 0, sizeof(*p));
    pthread_mutex_unlock(&mako_diam_pool_mu);
    return 1;
}

static inline int64_t mako_diameter_pool_idle(int64_t pool) {
    if (pool < 0 || pool >= MAKO_DIAMETER_POOL_MAX) return -1;
    pthread_mutex_lock(&mako_diam_pool_mu);
    int n = mako_diam_pools[pool].live ? mako_diam_pools[pool].idle_n : -1;
    pthread_mutex_unlock(&mako_diam_pool_mu);
    return n;
}

/* ---- multi-peer manager ---- */
typedef struct {
    int used;
    char identity[MAKO_DIAMETER_IDENT_MAX];
    size_t identity_len;
    char host[256];
    int port;
    int transport;
    int priority;
    int state;
    int fd;
    int64_t conn; /* diameter_conn handle */
    int64_t last_activity_ms;
    int64_t dwr_sent_ms;
    int dwr_outstanding;
    int fail_count;
} MakoDiamPeer;

typedef struct {
    int used;
    char realm[MAKO_DIAMETER_REALM_MAX];
    size_t realm_len;
    char peer_identity[MAKO_DIAMETER_IDENT_MAX];
    size_t peer_identity_len;
} MakoDiamRoute;

typedef struct {
    int live;
    char origin_host[MAKO_DIAMETER_ORIGIN_MAX];
    size_t origin_host_len;
    char origin_realm[MAKO_DIAMETER_REALM_MAX];
    size_t origin_realm_len;
    int64_t tw_ms;       /* watchdog interval */
    int64_t tw_timeout;  /* wait for DWA */
    MakoDiamPeer peers[MAKO_DIAMETER_PEER_MAX];
    MakoDiamRoute routes[MAKO_DIAMETER_ROUTE_MAX];
    /* app messages ready (non-base) */
    MakoString appq[MAKO_DIAMETER_INQ_MAX];
    int appq_n;
} MakoDiamMgr;

static MakoDiamMgr mako_diam_mgrs[MAKO_DIAMETER_MGR_MAX];
static pthread_mutex_t mako_diam_mgr_mu = MAKO_MUTEX_INIT;

static inline int64_t mako_diameter_mgr_new(void) {
    pthread_mutex_lock(&mako_diam_mgr_mu);
    int slot = -1;
    for (int i = 0; i < MAKO_DIAMETER_MGR_MAX; i++) {
        if (!mako_diam_mgrs[i].live) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    memset(m, 0, sizeof(*m));
    m->live = 1;
    m->tw_ms = 0;       /* DWR disabled until diameter_mgr_set_watchdog */
    m->tw_timeout = 0;
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return (int64_t)slot + 1;
}

static inline int64_t mako_diameter_mgr_free(int64_t h) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    for (int i = 0; i < MAKO_DIAMETER_PEER_MAX; i++) {
        if (!m->peers[i].used) continue;
        if (m->peers[i].fd >= 0) mako_sock_close(m->peers[i].fd);
        if (m->peers[i].conn > 0) mako_diameter_conn_free(m->peers[i].conn);
    }
    for (int i = 0; i < m->appq_n; i++) mako_str_free(m->appq[i]);
    memset(m, 0, sizeof(*m));
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return 0;
}

static inline int64_t mako_diameter_mgr_set_origin(
    int64_t h, MakoString host, MakoString realm
) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    if (!host.data || host.len == 0 || host.len >= MAKO_DIAMETER_ORIGIN_MAX) return -1;
    if (!realm.data || realm.len == 0 || realm.len >= MAKO_DIAMETER_REALM_MAX) return -1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    memcpy(m->origin_host, host.data, host.len);
    m->origin_host[host.len] = 0;
    m->origin_host_len = host.len;
    memcpy(m->origin_realm, realm.data, realm.len);
    m->origin_realm[realm.len] = 0;
    m->origin_realm_len = realm.len;
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return 0;
}

static inline int64_t mako_diameter_mgr_set_watchdog(
    int64_t h, int64_t tw_ms, int64_t timeout_ms
) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    if (tw_ms < 0 || timeout_ms < 0) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    m->tw_ms = tw_ms;
    m->tw_timeout = timeout_ms;
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return 0;
}

static inline int64_t mako_diameter_peer_add(
    int64_t h, MakoString identity, MakoString host, int64_t port,
    int64_t transport, int64_t priority
) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    if (!identity.data || identity.len == 0 || identity.len >= MAKO_DIAMETER_IDENT_MAX) return -1;
    if (!host.data || host.len == 0 || host.len >= 256) return -1;
    if (port <= 0 || port > 65535) return -1;
    if (transport != MAKO_DIAM_TR_TCP && transport != MAKO_DIAM_TR_SCTP
        && transport != MAKO_DIAM_TR_TLS) {
        return -1;
    }
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    int idx = -1;
    for (int i = 0; i < MAKO_DIAMETER_PEER_MAX; i++) {
        if (!m->peers[i].used) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    MakoDiamPeer *p = &m->peers[idx];
    memset(p, 0, sizeof(*p));
    p->used = 1;
    memcpy(p->identity, identity.data, identity.len);
    p->identity[identity.len] = 0;
    p->identity_len = identity.len;
    memcpy(p->host, host.data, host.len);
    p->host[host.len] = 0;
    p->port = (int)port;
    p->transport = (int)transport;
    p->priority = (int)priority;
    p->state = MAKO_DIAM_ST_CLOSED;
    p->fd = -1;
    p->conn = mako_diameter_conn_new();
    if (p->conn > 0 && m->origin_host_len) {
        MakoString oh = {m->origin_host, m->origin_host_len};
        MakoString orv = {m->origin_realm, m->origin_realm_len};
        mako_diameter_conn_set_origin(p->conn, oh, orv);
    }
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return (int64_t)idx;
}

static inline int64_t mako_diameter_peer_remove(int64_t h, int64_t peer_id) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    if (peer_id < 0 || peer_id >= MAKO_DIAMETER_PEER_MAX) return -1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live || !m->peers[peer_id].used) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    if (m->peers[peer_id].fd >= 0) mako_sock_close(m->peers[peer_id].fd);
    if (m->peers[peer_id].conn > 0) mako_diameter_conn_free(m->peers[peer_id].conn);
    memset(&m->peers[peer_id], 0, sizeof(m->peers[peer_id]));
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return 0;
}

static inline int64_t mako_diameter_peer_state(int64_t h, int64_t peer_id) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    if (peer_id < 0 || peer_id >= MAKO_DIAMETER_PEER_MAX) return -1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    int st = -1;
    if (mako_diam_mgrs[slot].live && mako_diam_mgrs[slot].peers[peer_id].used) {
        st = mako_diam_mgrs[slot].peers[peer_id].state;
    }
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return st;
}

static inline int64_t mako_diameter_peer_count(int64_t h) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    int n = 0;
    for (int i = 0; i < MAKO_DIAMETER_PEER_MAX; i++) if (m->peers[i].used) n++;
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return n;
}

static inline int64_t mako_diameter_peer_fd(int64_t h, int64_t peer_id) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    if (peer_id < 0 || peer_id >= MAKO_DIAMETER_PEER_MAX) return -1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    int fd = -1;
    if (mako_diam_mgrs[slot].live && mako_diam_mgrs[slot].peers[peer_id].used) {
        fd = mako_diam_mgrs[slot].peers[peer_id].fd;
    }
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return fd;
}

static inline int64_t mako_diameter_route_add(
    int64_t h, MakoString realm, MakoString peer_identity
) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    if (!realm.data || realm.len >= MAKO_DIAMETER_REALM_MAX) return -1;
    if (!peer_identity.data || peer_identity.len == 0
        || peer_identity.len >= MAKO_DIAMETER_IDENT_MAX) {
        return -1;
    }
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    int idx = -1;
    for (int i = 0; i < MAKO_DIAMETER_ROUTE_MAX; i++) {
        if (!m->routes[i].used) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    MakoDiamRoute *r = &m->routes[idx];
    r->used = 1;
    if (realm.len) memcpy(r->realm, realm.data, realm.len);
    r->realm[realm.len] = 0;
    r->realm_len = realm.len;
    memcpy(r->peer_identity, peer_identity.data, peer_identity.len);
    r->peer_identity[peer_identity.len] = 0;
    r->peer_identity_len = peer_identity.len;
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return (int64_t)idx;
}

static inline int64_t mako_diameter_route_remove(int64_t h, int64_t route_id) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    if (route_id < 0 || route_id >= MAKO_DIAMETER_ROUTE_MAX) return -1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    if (!mako_diam_mgrs[slot].live) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    memset(&mako_diam_mgrs[slot].routes[route_id], 0, sizeof(MakoDiamRoute));
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return 0;
}

/* Returns peer_id for realm, or -1. Matches exact realm then "*" / empty default. */
static inline int64_t mako_diameter_route_lookup(int64_t h, MakoString realm) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    const char *want = realm.data ? realm.data : "";
    size_t wlen = realm.data ? realm.len : 0;
    int found_peer = -1;
    int found_default = -1;
    for (int i = 0; i < MAKO_DIAMETER_ROUTE_MAX; i++) {
        if (!m->routes[i].used) continue;
        int is_def = (m->routes[i].realm_len == 0
                      || (m->routes[i].realm_len == 1 && m->routes[i].realm[0] == '*'));
        if (is_def) {
            /* resolve later if no exact match */
            for (int p = 0; p < MAKO_DIAMETER_PEER_MAX; p++) {
                if (m->peers[p].used
                    && m->peers[p].identity_len == m->routes[i].peer_identity_len
                    && memcmp(m->peers[p].identity, m->routes[i].peer_identity,
                              m->routes[i].peer_identity_len) == 0) {
                    found_default = p;
                    break;
                }
            }
            continue;
        }
        if (m->routes[i].realm_len == wlen
            && memcmp(m->routes[i].realm, want, wlen) == 0) {
            for (int p = 0; p < MAKO_DIAMETER_PEER_MAX; p++) {
                if (m->peers[p].used
                    && m->peers[p].identity_len == m->routes[i].peer_identity_len
                    && memcmp(m->peers[p].identity, m->routes[i].peer_identity,
                              m->routes[i].peer_identity_len) == 0) {
                    found_peer = p;
                    break;
                }
            }
            if (found_peer >= 0) break;
        }
    }
    int out = found_peer >= 0 ? found_peer : found_default;
    /* If no routes, pick lowest-priority open peer or lowest priority overall. */
    if (out < 0) {
        int best = -1;
        int best_pri = 1 << 30;
        for (int p = 0; p < MAKO_DIAMETER_PEER_MAX; p++) {
            if (!m->peers[p].used) continue;
            if (m->peers[p].priority < best_pri) {
                best_pri = m->peers[p].priority;
                best = p;
            }
        }
        out = best;
    }
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return (int64_t)out;
}

/* Failover: ordered peer ids by priority (stable for equal priority by index). */
static inline int64_t mako_diameter_mgr_pick_failover(int64_t h, int64_t after_peer) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    int best = -1;
    int best_pri = 1 << 30;
    for (int p = 0; p < MAKO_DIAMETER_PEER_MAX; p++) {
        if (!m->peers[p].used) continue;
        if (after_peer >= 0 && p == (int)after_peer) continue;
        if (m->peers[p].state == MAKO_DIAM_ST_CLOSING) continue;
        if (m->peers[p].priority < best_pri
            || (m->peers[p].priority == best_pri && (best < 0 || p < best))) {
            /* Prefer peers with priority >= after_peer's if after set */
            if (after_peer >= 0 && m->peers[after_peer].used
                && m->peers[p].priority < m->peers[after_peer].priority) {
                continue; /* already tried better priority */
            }
            if (after_peer >= 0 && m->peers[after_peer].used
                && m->peers[p].priority == m->peers[after_peer].priority
                && p <= (int)after_peer) {
                continue;
            }
            best_pri = m->peers[p].priority;
            best = p;
        }
    }
    /* Simpler failover: next higher priority number (worse) or next index. */
    if (best < 0 && after_peer >= 0) {
        int min_pri = 1 << 30;
        for (int p = 0; p < MAKO_DIAMETER_PEER_MAX; p++) {
            if (!m->peers[p].used || p == (int)after_peer) continue;
            if (m->peers[p].priority < min_pri) {
                min_pri = m->peers[p].priority;
                best = p;
            } else if (m->peers[p].priority == min_pri && (best < 0 || p < best)) {
                best = p;
            }
        }
        /* Prefer peers with priority worse than or equal after, excluding after */
        best = -1;
        min_pri = 1 << 30;
        int after_pri = m->peers[after_peer].used ? m->peers[after_peer].priority : -1;
        for (int p = 0; p < MAKO_DIAMETER_PEER_MAX; p++) {
            if (!m->peers[p].used || p == (int)after_peer) continue;
            if (m->peers[p].priority < after_pri) continue;
            if (m->peers[p].priority < min_pri
                || (m->peers[p].priority == min_pri && p > (int)after_peer)) {
                if (m->peers[p].priority == after_pri && p <= (int)after_peer) continue;
                min_pri = m->peers[p].priority;
                best = p;
            }
        }
        if (best < 0) {
            min_pri = 1 << 30;
            for (int p = 0; p < MAKO_DIAMETER_PEER_MAX; p++) {
                if (!m->peers[p].used || p == (int)after_peer) continue;
                if (m->peers[p].priority < min_pri) {
                    min_pri = m->peers[p].priority;
                    best = p;
                }
            }
        }
    }
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return (int64_t)best;
}

static inline int64_t mako_diameter_peer_mark_open(int64_t h, int64_t peer_id) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    if (peer_id < 0 || peer_id >= MAKO_DIAMETER_PEER_MAX) return -1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live || !m->peers[peer_id].used) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    m->peers[peer_id].state = MAKO_DIAM_ST_OPEN;
    m->peers[peer_id].last_activity_ms = mako_mono_ms();
    m->peers[peer_id].fail_count = 0;
    m->peers[peer_id].dwr_outstanding = 0;
    if (m->peers[peer_id].conn > 0) {
        mako_diameter_conn_set_state(m->peers[peer_id].conn, MAKO_DIAM_ST_OPEN);
    }
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return 0;
}

static inline int64_t mako_diameter_peer_mark_closed(int64_t h, int64_t peer_id) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    if (peer_id < 0 || peer_id >= MAKO_DIAMETER_PEER_MAX) return -1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live || !m->peers[peer_id].used) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    if (m->peers[peer_id].fd >= 0) {
        mako_sock_close(m->peers[peer_id].fd);
        m->peers[peer_id].fd = -1;
    }
    m->peers[peer_id].state = MAKO_DIAM_ST_CLOSED;
    m->peers[peer_id].dwr_outstanding = 0;
    if (m->peers[peer_id].conn > 0) {
        mako_diameter_conn_reset(m->peers[peer_id].conn);
    }
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return 0;
}

/* Attach an already-connected fd to a peer (offline tests / external connect).
 * Rejects stdin/stdout/stderr and negative fds (A7). */
static inline int64_t mako_diameter_peer_attach_fd(int64_t h, int64_t peer_id, int64_t fd) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    if (peer_id < 0 || peer_id >= MAKO_DIAMETER_PEER_MAX) return -1;
    if (fd < 3) return -1; /* never attach 0/1/2 */
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live || !m->peers[peer_id].used) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    if (m->peers[peer_id].fd >= 0) mako_sock_close(m->peers[peer_id].fd);
    m->peers[peer_id].fd = (int)fd;
    m->peers[peer_id].state = MAKO_DIAM_ST_WAIT_CEA;
    m->peers[peer_id].last_activity_ms = mako_mono_ms();
    if (m->peers[peer_id].conn > 0) {
        mako_diameter_conn_set_state(m->peers[peer_id].conn, MAKO_DIAM_ST_WAIT_CEA);
    }
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return 0;
}

/* Feed inbound bytes for peer; auto-answer CER/DWR/DPR; queue app msgs. */
static inline int64_t mako_diameter_mgr_handle_io(
    int64_t h, int64_t peer_id, MakoString inbound
) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    if (peer_id < 0 || peer_id >= MAKO_DIAMETER_PEER_MAX) return -1;

    int64_t conn = -1;
    char oh[MAKO_DIAMETER_ORIGIN_MAX];
    char orv[MAKO_DIAMETER_REALM_MAX];
    size_t oh_len = 0, or_len = 0;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live || !m->peers[peer_id].used) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    conn = m->peers[peer_id].conn;
    oh_len = m->origin_host_len;
    or_len = m->origin_realm_len;
    if (oh_len) memcpy(oh, m->origin_host, oh_len + 1);
    if (or_len) memcpy(orv, m->origin_realm, or_len + 1);
    m->peers[peer_id].last_activity_ms = mako_mono_ms();
    pthread_mutex_unlock(&mako_diam_mgr_mu);

    if (conn <= 0) return -1;
    if (inbound.data && inbound.len) {
        if (mako_diameter_conn_feed(conn, inbound) < 0) return -1;
    }

    int handled = 0;
    for (;;) {
        MakoString msg = mako_diameter_conn_pop(conn);
        if (!msg.data || msg.len == 0) {
            mako_str_free(msg);
            break;
        }
        handled++;
        int64_t cmd = mako_diameter_cmd(msg);
        int is_req = mako_diameter_is_request(msg);
        MakoString ohs = {oh, oh_len};
        MakoString ors = {orv, or_len};

        if (is_req && cmd == MAKO_DIAM_CMD_CER) {
            /* A4: require local Origin and peer Origin-Host before success CEA. */
            MakoString peer_oh = mako_diameter_avp_find(msg, MAKO_DIAM_AVP_ORIGIN_HOST);
            int have_local = (oh_len > 0 && or_len > 0);
            int have_peer = (peer_oh.data && peer_oh.len > 0);
            int64_t rc = 2001;
            if (!have_local || !have_peer) {
                rc = !have_local ? MAKO_DIAM_RC_UNABLE_TO_COMPLY : MAKO_DIAM_RC_MISSING_AVP;
            }
            MakoString cea = mako_diameter_build_cea(msg, ohs, ors, rc);
            mako_diameter_conn_queue_out(conn, cea);
            mako_str_free(cea);
            mako_str_free(peer_oh);
            if (rc == 2001) mako_diameter_peer_mark_open(h, peer_id);
        } else if (is_req && cmd == MAKO_DIAM_CMD_DWR) {
            if (oh_len == 0 || or_len == 0) {
                /* No local identity — do not invent Origin on DWA. */
            } else {
                MakoString dwa = mako_diameter_build_dwa(msg, ohs, ors, 2001);
                mako_diameter_conn_queue_out(conn, dwa);
                mako_str_free(dwa);
            }
        } else if (is_req && cmd == MAKO_DIAM_CMD_DPR) {
            MakoString dpa = mako_diameter_build_dpa(
                msg, ohs, ors, (oh_len && or_len) ? 2001 : MAKO_DIAM_RC_UNABLE_TO_COMPLY
            );
            mako_diameter_conn_queue_out(conn, dpa);
            mako_str_free(dpa);
            mako_diameter_peer_mark_closed(h, peer_id);
        } else if (!is_req && cmd == MAKO_DIAM_CMD_CEA) {
            mako_diameter_conn_on_cea(conn, msg);
            mako_diameter_peer_mark_open(h, peer_id);
        } else if (!is_req && cmd == MAKO_DIAM_CMD_DWA) {
            mako_diameter_conn_match(conn, msg);
            pthread_mutex_lock(&mako_diam_mgr_mu);
            if (mako_diam_mgrs[slot].live && mako_diam_mgrs[slot].peers[peer_id].used) {
                mako_diam_mgrs[slot].peers[peer_id].dwr_outstanding = 0;
                mako_diam_mgrs[slot].peers[peer_id].last_activity_ms = mako_mono_ms();
            }
            pthread_mutex_unlock(&mako_diam_mgr_mu);
        } else if (!is_req && cmd == MAKO_DIAM_CMD_DPA) {
            mako_diameter_conn_match(conn, msg);
            mako_diameter_peer_mark_closed(h, peer_id);
        } else {
            /* Application message: match answers before transferring ownership. */
            if (!is_req) (void)mako_diameter_conn_match(conn, msg);
            pthread_mutex_lock(&mako_diam_mgr_mu);
            MakoDiamMgr *mm = &mako_diam_mgrs[slot];
            if (mm->live && mm->appq_n < MAKO_DIAMETER_INQ_MAX) {
                mm->appq[mm->appq_n++] = msg;
                msg.data = NULL;
                msg.len = 0;
            }
            if (mm->live && mm->peers[peer_id].used) {
                mm->peers[peer_id].last_activity_ms = mako_mono_ms();
            }
            pthread_mutex_unlock(&mako_diam_mgr_mu);
            /* If appq was full, msg still owned here and freed below. */
        }
        mako_str_free(msg);
    }
    return (int64_t)handled;
}

static inline MakoString mako_diameter_mgr_poll(int64_t h) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return mako_str_from_cstr("");
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live || m->appq_n <= 0) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return mako_str_from_cstr("");
    }
    MakoString out = m->appq[0];
    for (int i = 1; i < m->appq_n; i++) m->appq[i - 1] = m->appq[i];
    m->appq_n--;
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    return out;
}

/* Watchdog: send DWR when idle; close peer if DWA timeout. Returns DWRs queued. */
static inline int64_t mako_diameter_mgr_tick(int64_t h, int64_t now_ms) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    if (now_ms <= 0) now_ms = mako_mono_ms();

    int queued = 0;
    /* Snapshot peers to avoid long lock while building messages */
    typedef struct {
        int id;
        int64_t conn;
        int state;
        int64_t last;
        int64_t dwr_sent;
        int dwr_out;
        char oh[MAKO_DIAMETER_ORIGIN_MAX];
        char orv[MAKO_DIAMETER_REALM_MAX];
        size_t oh_len, or_len;
    } Snap;
    Snap snaps[MAKO_DIAMETER_PEER_MAX];
    int nsnap = 0;
    int64_t tw = 0, twt = 0;

    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    tw = m->tw_ms;
    twt = m->tw_timeout;
    if (tw <= 0) {
        /* Watchdog not configured — no DWR scheduling. */
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return 0;
    }
    for (int i = 0; i < MAKO_DIAMETER_PEER_MAX; i++) {
        if (!m->peers[i].used || m->peers[i].state != MAKO_DIAM_ST_OPEN) continue;
        Snap *s = &snaps[nsnap++];
        s->id = i;
        s->conn = m->peers[i].conn;
        s->state = m->peers[i].state;
        s->last = m->peers[i].last_activity_ms;
        s->dwr_sent = m->peers[i].dwr_sent_ms;
        s->dwr_out = m->peers[i].dwr_outstanding;
        s->oh_len = m->origin_host_len;
        s->or_len = m->origin_realm_len;
        if (s->oh_len) memcpy(s->oh, m->origin_host, s->oh_len + 1);
        if (s->or_len) memcpy(s->orv, m->origin_realm, s->or_len + 1);
    }
    pthread_mutex_unlock(&mako_diam_mgr_mu);

    for (int i = 0; i < nsnap; i++) {
        Snap *s = &snaps[i];
        if (twt > 0 && s->dwr_out && s->dwr_sent > 0 && now_ms - s->dwr_sent >= twt) {
            mako_diameter_peer_mark_closed(h, s->id);
            continue;
        }
        if (!s->dwr_out && s->last > 0 && now_ms - s->last >= tw && s->conn > 0) {
            MakoString ohs = {s->oh, s->oh_len};
            MakoString ors = {s->orv, s->or_len};
            int64_t hbh = mako_diameter_conn_next_hbh(s->conn);
            int64_t e2e = mako_diameter_e2e_new();
            MakoString dwr = mako_diameter_build_dwr(ohs, ors, hbh, e2e);
            mako_diameter_conn_register(s->conn, hbh, e2e, MAKO_DIAM_CMD_DWR, 0);
            mako_diameter_conn_queue_out(s->conn, dwr);
            mako_str_free(dwr);
            pthread_mutex_lock(&mako_diam_mgr_mu);
            if (mako_diam_mgrs[slot].live && mako_diam_mgrs[slot].peers[s->id].used) {
                mako_diam_mgrs[slot].peers[s->id].dwr_outstanding = 1;
                mako_diam_mgrs[slot].peers[s->id].dwr_sent_ms = now_ms;
            }
            pthread_mutex_unlock(&mako_diam_mgr_mu);
            queued++;
        }
    }
    return (int64_t)queued;
}

/* Queue a request on the best peer for realm. Returns peer_id or -1. */
static inline int64_t mako_diameter_mgr_request(
    int64_t h, MakoString realm, int64_t flags, int64_t cmd, int64_t app, MakoString avps
) {
    int64_t peer = mako_diameter_route_lookup(h, realm);
    if (peer < 0) return -1;
    int slot = (int)h - 1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    MakoDiamMgr *m = &mako_diam_mgrs[slot];
    if (!m->live || !m->peers[peer].used) {
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        return -1;
    }
    int64_t conn = m->peers[peer].conn;
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    if (conn <= 0) return -1;
    int64_t hbh = mako_diameter_conn_next_hbh(conn);
    int64_t e2e = mako_diameter_e2e_new();
    if (!(flags & MAKO_DIAM_FLAG_R)) flags |= MAKO_DIAM_FLAG_R;
    MakoString msg = mako_diameter_msg_build(flags, cmd, app, hbh, e2e, avps);
    if (!msg.data || msg.len == 0) return -1;
    mako_diameter_conn_register(conn, hbh, e2e, cmd, app);
    if (mako_diameter_conn_queue_out(conn, msg) < 0) {
        mako_str_free(msg);
        return -1;
    }
    mako_str_free(msg);
    return peer;
}

/* Drain outbound for all peers into their sockets when fd is set.
 * Returns total bytes written. */
static inline int64_t mako_diameter_mgr_flush(int64_t h) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return -1;
    int64_t total = 0;
    for (int p = 0; p < MAKO_DIAMETER_PEER_MAX; p++) {
        int64_t conn = -1;
        int fd = -1;
        pthread_mutex_lock(&mako_diam_mgr_mu);
        MakoDiamMgr *m = &mako_diam_mgrs[slot];
        if (!m->live || !m->peers[p].used) {
            pthread_mutex_unlock(&mako_diam_mgr_mu);
            continue;
        }
        conn = m->peers[p].conn;
        fd = m->peers[p].fd;
        pthread_mutex_unlock(&mako_diam_mgr_mu);
        if (conn <= 0 || fd < 0) continue;
        MakoString out = mako_diameter_conn_take_out(conn);
        if (!out.data || out.len == 0) {
            mako_str_free(out);
            continue;
        }
        int64_t n = mako_tcp_write_all(fd, out);
        if (n > 0) total += n;
        mako_str_free(out);
    }
    return total;
}

/* Take outbound bytes for a peer without writing (for tests / custom I/O). */
static inline MakoString mako_diameter_mgr_take_out(int64_t h, int64_t peer_id) {
    int slot = (int)h - 1;
    if (slot < 0 || slot >= MAKO_DIAMETER_MGR_MAX) return mako_str_from_cstr("");
    if (peer_id < 0 || peer_id >= MAKO_DIAMETER_PEER_MAX) return mako_str_from_cstr("");
    int64_t conn = -1;
    pthread_mutex_lock(&mako_diam_mgr_mu);
    if (mako_diam_mgrs[slot].live && mako_diam_mgrs[slot].peers[peer_id].used) {
        conn = mako_diam_mgrs[slot].peers[peer_id].conn;
    }
    pthread_mutex_unlock(&mako_diam_mgr_mu);
    if (conn <= 0) return mako_str_from_cstr("");
    return mako_diameter_conn_take_out(conn);
}

/* Feed raw outbound injection for offline tests (queue into peer conn out). */
static inline int64_t mako_diameter_mgr_inject_in(
    int64_t h, int64_t peer_id, MakoString msg
) {
    return mako_diameter_mgr_handle_io(h, peer_id, msg);
}

/* =====================================================================
 * diameter_tcm_* — thin Diameter facade over general primitives
 *
 * Composes peer_table (peers/routes) + timer_heap (deadlines) + diameter_conn
 * (wire). Diameter-only state: Origin-Host/Realm, Tw watchdog, DWR flags.
 * Not a general peer manager — use peer_table + timer_heap directly for that.
 * ===================================================================== */
/* Process-wide TCM slot table (not a policy default for peer counts). */
#define MAKO_DIAM_TCM_MAX          4
/* Absolute allocator ceilings only (DoS); caller must pass explicit sizes. */
#define MAKO_DIAM_TCM_PEER_HARD    4096
#define MAKO_DIAM_TCM_CONN_HARD    8192
#define MAKO_DIAM_TCM_TIMER_HARD   131072

#define MAKO_DIAM_TMR_DWR     1
#define MAKO_DIAM_TMR_TXN     2
#define MAKO_DIAM_TMR_REASM   3

typedef struct {
    int live;
    uint32_t gen;
    int max_peers;
    int max_conns; /* reserved / accounting */
    int max_pending;
    int max_timers;
    int64_t peers;   /* peer_table handle (general) */
    int64_t timers;  /* timer_heap handle (general) */
    char origin_host[MAKO_DIAMETER_ORIGIN_MAX];
    size_t origin_host_len;
    char origin_realm[MAKO_DIAMETER_REALM_MAX];
    size_t origin_realm_len;
    int64_t tw_ms;
    int64_t tw_timeout;
    uint8_t *dwr_outstanding; /* parallel to peer ids; Diameter-only */
    int64_t drops;
    int conn_open_n;
    int run_budget; /* max expiries per run_due; 0 = process all due */
} MakoDiamTcm;

static MakoDiamTcm mako_diam_tcms[MAKO_DIAM_TCM_MAX];
static pthread_mutex_t mako_diam_tcm_mu = MAKO_MUTEX_INIT;

static inline int64_t mako_diam_tcm_pack_handle(int slot, uint32_t gen) {
    if (gen == 0) gen = 1;
    return (int64_t)(((uint32_t)(slot + 1) & 0xffffu)
                     | ((gen & 0xffffu) << 16));
}
static inline int mako_diam_tcm_handle_slot(int64_t h) {
    int id = (int)(h & 0xffff);
    if (id <= 0 || id > MAKO_DIAM_TCM_MAX) return -1;
    return id - 1;
}
static inline uint32_t mako_diam_tcm_handle_gen(int64_t h) {
    return (uint32_t)((h >> 16) & 0xffffu);
}

/* Caller holds mako_diam_tcm_mu. */
static inline int mako_diam_tcm_live_locked(int64_t h, MakoDiamTcm **out) {
    int slot = mako_diam_tcm_handle_slot(h);
    if (slot < 0) return 0;
    MakoDiamTcm *t = &mako_diam_tcms[slot];
    if (!t->live) return 0;
    if (mako_diam_tcm_handle_gen(h) != t->gen) return 0;
    if (out) *out = t;
    return 1;
}

static inline int64_t mako_diameter_tcm_new(
    int64_t max_peers, int64_t max_conns, int64_t max_pending, int64_t max_timers
) {
    /* All four capacities are required; no invented defaults. */
    if (max_peers <= 0 || max_conns <= 0 || max_pending <= 0 || max_timers <= 0) return -1;
    if (max_peers > MAKO_DIAM_TCM_PEER_HARD) return -1;
    if (max_conns > MAKO_DIAM_TCM_CONN_HARD) return -1;
    if (max_timers > MAKO_DIAM_TCM_TIMER_HARD) return -1;
    if (max_pending > 65536) return -1;

    int64_t peers = mako_peer_table_new(max_peers, max_peers);
    if (peers < 0) return -1;
    int64_t timers = mako_timer_heap_new(max_timers);
    if (timers < 0) {
        mako_peer_table_free(peers);
        return -1;
    }
    uint8_t *dwr = (uint8_t *)calloc((size_t)max_peers, 1);
    if (!dwr) {
        mako_timer_heap_free(timers);
        mako_peer_table_free(peers);
        return -1;
    }

    pthread_mutex_lock(&mako_diam_tcm_mu);
    int slot = -1;
    for (int i = 0; i < MAKO_DIAM_TCM_MAX; i++) {
        if (!mako_diam_tcms[i].live) { slot = i; break; }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        free(dwr);
        mako_timer_heap_free(timers);
        mako_peer_table_free(peers);
        return -1;
    }
    MakoDiamTcm *t = &mako_diam_tcms[slot];
    uint32_t prev_gen = t->gen;
    memset(t, 0, sizeof(*t));
    t->live = 1;
    t->gen = (prev_gen + 1) & 0xffffu;
    if (t->gen == 0) t->gen = 1;
    t->max_peers = (int)max_peers;
    t->max_conns = (int)max_conns;
    t->max_pending = (int)max_pending;
    t->max_timers = (int)max_timers;
    t->peers = peers;
    t->timers = timers;
    t->dwr_outstanding = dwr;
    t->tw_ms = 0;
    t->tw_timeout = 0;
    t->run_budget = 0; /* 0 = all due timers this call */
    int64_t handle = mako_diam_tcm_pack_handle(slot, t->gen);
    pthread_mutex_unlock(&mako_diam_tcm_mu);
    return handle;
}

static inline int64_t mako_diameter_tcm_free(int64_t h) {
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    int64_t peers = t->peers;
    int64_t timers = t->timers;
    uint8_t *dwr = t->dwr_outstanding;
    int max_peers = t->max_peers;
    uint32_t gen = t->gen;
    memset(t, 0, sizeof(*t));
    t->gen = gen;
    pthread_mutex_unlock(&mako_diam_tcm_mu);

    if (peers > 0) {
        for (int i = 0; i < max_peers; i++) {
            if (!mako_peer_table_alive(peers, i)) continue;
            int64_t conn = mako_peer_table_get_conn(peers, i);
            if (conn > 0) mako_diameter_conn_free(conn);
        }
        mako_peer_table_free(peers);
    }
    if (timers > 0) mako_timer_heap_free(timers);
    free(dwr);
    return 0;
}

static inline int64_t mako_diameter_tcm_set_origin(
    int64_t h, MakoString host, MakoString realm
) {
    if (!host.data || host.len == 0 || host.len >= MAKO_DIAMETER_ORIGIN_MAX) return -1;
    if (!realm.data || realm.len == 0 || realm.len >= MAKO_DIAMETER_REALM_MAX) return -1;
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    memcpy(t->origin_host, host.data, host.len);
    t->origin_host[host.len] = 0;
    t->origin_host_len = host.len;
    memcpy(t->origin_realm, realm.data, realm.len);
    t->origin_realm[realm.len] = 0;
    t->origin_realm_len = realm.len;
    pthread_mutex_unlock(&mako_diam_tcm_mu);
    return 0;
}

static inline int64_t mako_diameter_tcm_set_watchdog(
    int64_t h, int64_t tw_ms, int64_t timeout_ms
) {
    if (tw_ms < 0 || timeout_ms < 0) return -1;
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    t->tw_ms = tw_ms;
    t->tw_timeout = timeout_ms;
    pthread_mutex_unlock(&mako_diam_tcm_mu);
    return 0;
}

/* 0 = unlimited (process every due timer in one run_due). */
static inline int64_t mako_diameter_tcm_set_run_budget(int64_t h, int64_t budget) {
    if (budget < 0) return -1;
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    t->run_budget = (int)budget;
    pthread_mutex_unlock(&mako_diam_tcm_mu);
    return 0;
}

static inline int64_t mako_diameter_tcm_peer_add(
    int64_t h, MakoString identity, MakoString host, int64_t port,
    int64_t transport, int64_t priority
) {
    if (!identity.data || identity.len == 0 || identity.len >= MAKO_DIAMETER_IDENT_MAX) return -1;
    if (!host.data || host.len == 0 || host.len >= 256) return -1;
    if (port <= 0 || port > 65535) return -1;

    int64_t peers = -1;
    char oh[MAKO_DIAMETER_ORIGIN_MAX], orv[MAKO_DIAMETER_REALM_MAX];
    size_t oh_len = 0, or_len = 0;
    uint32_t gen = 0;

    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    if (t->conn_open_n >= t->max_conns) {
        t->drops++;
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    peers = t->peers;
    gen = t->gen;
    oh_len = t->origin_host_len;
    or_len = t->origin_realm_len;
    if (oh_len) memcpy(oh, t->origin_host, oh_len + 1);
    if (or_len) memcpy(orv, t->origin_realm, or_len + 1);
    pthread_mutex_unlock(&mako_diam_tcm_mu);

    int64_t idx = mako_peer_table_add(peers, identity, host, port, transport, priority);
    if (idx < 0) return -1;

    int64_t conn = mako_diameter_conn_new();
    if (conn > 0) {
        if (oh_len) {
            MakoString ohs = {oh, oh_len};
            MakoString ors = {orv, or_len};
            mako_diameter_conn_set_origin(conn, ohs, ors);
        }
        mako_peer_table_set_conn(peers, idx, conn);
        mako_peer_table_set_state(peers, idx, MAKO_DIAM_ST_CLOSED);
        pthread_mutex_lock(&mako_diam_tcm_mu);
        if (mako_diam_tcm_live_locked(h, &t) && t->gen == gen
            && idx < t->max_peers && t->dwr_outstanding) {
            t->dwr_outstanding[idx] = 0;
            t->conn_open_n++;
        } else {
            /* TCM freed under us — drop the orphan conn. */
            pthread_mutex_unlock(&mako_diam_tcm_mu);
            mako_diameter_conn_free(conn);
            (void)mako_peer_table_set_conn(peers, idx, -1);
            return -1;
        }
        pthread_mutex_unlock(&mako_diam_tcm_mu);
    }
    return idx;
}

static inline int64_t mako_diameter_tcm_route_add(
    int64_t h, MakoString realm, MakoString peer_identity
) {
    if (!peer_identity.data || peer_identity.len == 0
        || peer_identity.len >= MAKO_DIAMETER_IDENT_MAX) {
        return -1;
    }
    if (realm.len >= MAKO_DIAMETER_REALM_MAX) return -1;
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    int64_t peers = t->peers;
    pthread_mutex_unlock(&mako_diam_tcm_mu);
    return mako_peer_table_route_add(peers, realm, peer_identity);
}

static inline int64_t mako_diameter_tcm_route_lookup(int64_t h, MakoString realm) {
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    int64_t peers = t->peers;
    pthread_mutex_unlock(&mako_diam_tcm_mu);
    return mako_peer_table_route_lookup(peers, realm);
}

/* Arm timer via general timer_heap; returns stable token or -1 if full. */
static inline int64_t mako_diameter_tcm_arm(
    int64_t h, int64_t deadline_ms, int64_t kind, int64_t peer_id
) {
    if (kind <= 0) kind = MAKO_DIAM_TMR_DWR;
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    int64_t timers = t->timers;
    pthread_mutex_unlock(&mako_diam_tcm_mu);
    return mako_timer_heap_arm(timers, deadline_ms, kind, peer_id);
}

static inline int64_t mako_diameter_tcm_cancel(int64_t h, int64_t timer_token) {
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    int64_t timers = t->timers;
    pthread_mutex_unlock(&mako_diam_tcm_mu);
    return mako_timer_heap_cancel(timers, timer_token);
}

static inline int64_t mako_diameter_tcm_poll_deadline(int64_t h, int64_t now_ms) {
    (void)now_ms;
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    int64_t timers = t->timers;
    pthread_mutex_unlock(&mako_diam_tcm_mu);
    return mako_timer_heap_next(timers);
}

static inline int64_t mako_diameter_tcm_run_due(int64_t h, int64_t now_ms) {
    if (now_ms <= 0) now_ms = mako_mono_ms();
    int fired = 0;
    int budget_lim = 0;
    uint32_t gen = 0;

    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t0 = NULL;
    if (!mako_diam_tcm_live_locked(h, &t0)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    budget_lim = t0->run_budget;
    int64_t timers = t0->timers;
    int64_t peers = t0->peers;
    int max_peers = t0->max_peers;
    int64_t tw_ms = t0->tw_ms;
    gen = t0->gen;
    pthread_mutex_unlock(&mako_diam_tcm_mu);

    for (int budget = 0; ; budget++) {
        if (budget_lim > 0 && budget >= budget_lim) break;

        int peer_id = -1;
        int kind = 0;
        int64_t conn = -1;
        char oh[MAKO_DIAMETER_ORIGIN_MAX];
        char orv[MAKO_DIAMETER_REALM_MAX];
        size_t oh_len = 0, or_len = 0;

        if (mako_timer_heap_pop_due1(timers, now_ms)) {
            kind = (int)mako_timer_last_kind();
            peer_id = (int)mako_timer_last_id();
        } else if (tw_ms > 0) {
            for (int p = 0; p < max_peers; p++) {
                if (!mako_peer_table_alive(peers, p)) continue;
                if (mako_peer_table_get_state(peers, p) != MAKO_DIAM_ST_OPEN) continue;
                int64_t last = mako_peer_table_last_ms(peers, p);
                pthread_mutex_lock(&mako_diam_tcm_mu);
                MakoDiamTcm *t = NULL;
                int dwr_out = 0;
                if (mako_diam_tcm_live_locked(h, &t) && t->gen == gen
                    && p < t->max_peers && t->dwr_outstanding) {
                    dwr_out = t->dwr_outstanding[p];
                } else {
                    pthread_mutex_unlock(&mako_diam_tcm_mu);
                    return (int64_t)fired;
                }
                pthread_mutex_unlock(&mako_diam_tcm_mu);
                if (last > 0 && now_ms - last >= tw_ms && !dwr_out) {
                    peer_id = p;
                    kind = MAKO_DIAM_TMR_DWR;
                    mako_peer_table_touch(peers, p, now_ms);
                    pthread_mutex_lock(&mako_diam_tcm_mu);
                    if (mako_diam_tcm_live_locked(h, &t) && t->gen == gen
                        && p < t->max_peers && t->dwr_outstanding) {
                        t->dwr_outstanding[p] = 1;
                    }
                    pthread_mutex_unlock(&mako_diam_tcm_mu);
                    break;
                }
            }
            if (peer_id < 0) break;
        } else {
            break;
        }

        if (peer_id >= 0 && mako_peer_table_alive(peers, peer_id)) {
            conn = mako_peer_table_get_conn(peers, peer_id);
            pthread_mutex_lock(&mako_diam_tcm_mu);
            MakoDiamTcm *t = NULL;
            if (mako_diam_tcm_live_locked(h, &t) && t->gen == gen) {
                oh_len = t->origin_host_len;
                or_len = t->origin_realm_len;
                if (oh_len) memcpy(oh, t->origin_host, oh_len + 1);
                if (or_len) memcpy(orv, t->origin_realm, or_len + 1);
                if (kind == MAKO_DIAM_TMR_DWR
                    && peer_id < t->max_peers && t->dwr_outstanding) {
                    t->dwr_outstanding[peer_id] = 1;
                }
            }
            pthread_mutex_unlock(&mako_diam_tcm_mu);
        }

        if (kind == MAKO_DIAM_TMR_DWR && conn > 0 && oh_len > 0) {
            MakoString ohs = {oh, oh_len};
            MakoString ors = {orv, or_len};
            int64_t hbh = mako_diameter_conn_next_hbh(conn);
            int64_t e2e = mako_diameter_e2e_new();
            MakoString dwr = mako_diameter_build_dwr(ohs, ors, hbh, e2e);
            mako_diameter_conn_register(conn, hbh, e2e, MAKO_DIAM_CMD_DWR, 0);
            mako_diameter_conn_queue_out(conn, dwr);
            mako_str_free(dwr);
            fired++;
        } else if (kind == MAKO_DIAM_TMR_TXN && conn > 0) {
            (void)mako_diameter_conn_expire(conn, now_ms, 1);
            fired++;
        } else if (kind == MAKO_DIAM_TMR_REASM && conn > 0) {
            (void)mako_diameter_conn_expire(conn, now_ms, 1);
            fired++;
        } else if (kind != 0) {
            fired++;
        }
    }
    return (int64_t)fired;
}

static inline int64_t mako_diameter_tcm_peer_count(int64_t h) {
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    int64_t peers = t->peers;
    pthread_mutex_unlock(&mako_diam_tcm_mu);
    return mako_peer_table_count(peers);
}

static inline int64_t mako_diameter_tcm_timer_count(int64_t h) {
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    int64_t timers = t->timers;
    pthread_mutex_unlock(&mako_diam_tcm_mu);
    return mako_timer_heap_count(timers);
}

static inline int64_t mako_diameter_tcm_drops(int64_t h) {
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    int64_t d = t->drops;
    int64_t peers = t->peers;
    int64_t timers = t->timers;
    pthread_mutex_unlock(&mako_diam_tcm_mu);
    int64_t pd = mako_peer_table_drops(peers);
    int64_t td = mako_timer_heap_drops(timers);
    if (pd > 0) d += pd;
    if (td > 0) d += td;
    return d;
}

/* Diameter base I/O on peer_table-attached diameter_conn handles. */
static inline int64_t mako_diameter_tcm_handle_io(
    int64_t h, int64_t peer_id, MakoString inbound
) {
    int64_t peers = -1;
    char oh[MAKO_DIAMETER_ORIGIN_MAX], orv[MAKO_DIAMETER_REALM_MAX];
    size_t oh_len = 0, or_len = 0;
    uint32_t gen = 0;
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return -1;
    }
    peers = t->peers;
    gen = t->gen;
    oh_len = t->origin_host_len;
    or_len = t->origin_realm_len;
    if (oh_len) memcpy(oh, t->origin_host, oh_len + 1);
    if (or_len) memcpy(orv, t->origin_realm, or_len + 1);
    pthread_mutex_unlock(&mako_diam_tcm_mu);

    if (!mako_peer_table_alive(peers, peer_id)) return -1;
    int64_t conn = mako_peer_table_get_conn(peers, peer_id);
    mako_peer_table_touch(peers, peer_id, mako_mono_ms());
    if (conn <= 0) return -1;
    if (inbound.data && inbound.len) {
        if (mako_diameter_conn_feed(conn, inbound) < 0) return -1;
    }
    int handled = 0;
    for (;;) {
        MakoString msg = mako_diameter_conn_pop(conn);
        if (!msg.data || msg.len == 0) {
            mako_str_free(msg);
            break;
        }
        handled++;
        int64_t cmd = mako_diameter_cmd(msg);
        int is_req = mako_diameter_is_request(msg);
        MakoString ohs = {oh, oh_len};
        MakoString ors = {orv, or_len};
        if (is_req && cmd == MAKO_DIAM_CMD_CER) {
            MakoString peer_oh = mako_diameter_avp_find(msg, MAKO_DIAM_AVP_ORIGIN_HOST);
            int64_t rc = (oh_len && or_len && peer_oh.len)
                ? 2001 : MAKO_DIAM_RC_UNABLE_TO_COMPLY;
            MakoString cea = mako_diameter_build_cea(msg, ohs, ors, rc);
            mako_diameter_conn_queue_out(conn, cea);
            mako_str_free(cea);
            mako_str_free(peer_oh);
            if (rc == 2001) {
                mako_peer_table_set_state(peers, peer_id, MAKO_DIAM_ST_OPEN);
                mako_peer_table_touch(peers, peer_id, mako_mono_ms());
                mako_diameter_conn_set_state(conn, MAKO_DIAM_ST_OPEN);
            }
        } else if (is_req && cmd == MAKO_DIAM_CMD_DWR && oh_len) {
            MakoString dwa = mako_diameter_build_dwa(msg, ohs, ors, 2001);
            mako_diameter_conn_queue_out(conn, dwa);
            mako_str_free(dwa);
        } else if (!is_req && cmd == MAKO_DIAM_CMD_DWA) {
            mako_diameter_conn_match(conn, msg);
            pthread_mutex_lock(&mako_diam_tcm_mu);
            if (mako_diam_tcm_live_locked(h, &t) && t->gen == gen
                && peer_id >= 0 && peer_id < t->max_peers && t->dwr_outstanding) {
                t->dwr_outstanding[peer_id] = 0;
            }
            pthread_mutex_unlock(&mako_diam_tcm_mu);
            mako_peer_table_touch(peers, peer_id, mako_mono_ms());
        } else if (!is_req) {
            mako_diameter_conn_match(conn, msg);
        }
        mako_str_free(msg);
    }
    return handled;
}

static inline int64_t mako_diameter_tcm_inject_in(
    int64_t h, int64_t peer_id, MakoString msg
) {
    return mako_diameter_tcm_handle_io(h, peer_id, msg);
}

static inline MakoString mako_diameter_tcm_take_out(int64_t h, int64_t peer_id) {
    pthread_mutex_lock(&mako_diam_tcm_mu);
    MakoDiamTcm *t = NULL;
    if (!mako_diam_tcm_live_locked(h, &t)) {
        pthread_mutex_unlock(&mako_diam_tcm_mu);
        return mako_str_from_cstr("");
    }
    int64_t peers = t->peers;
    pthread_mutex_unlock(&mako_diam_tcm_mu);
    if (!mako_peer_table_alive(peers, peer_id)) return mako_str_from_cstr("");
    int64_t conn = mako_peer_table_get_conn(peers, peer_id);
    if (conn <= 0) return mako_str_from_cstr("");
    return mako_diameter_conn_take_out(conn);
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_DIAMETER_H */
