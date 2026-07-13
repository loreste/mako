/* Email / SMTP — build messages and send via SMTP from Mako.
 *
 * Compose RFC 5322 messages (text/html/attachments) and talk SMTP:
 * connect → EHLO → optional STARTTLS → AUTH PLAIN → MAIL/RCPT/DATA/QUIT.
 *
 * Prefer the session API (`smtp_new` …) or one-shot `smtp_send_msg`.
 * Legacy helpers in mako_goext.h remain for soft probes.
 */
#ifndef MAKO_MAIL_H
#define MAKO_MAIL_H

#include "mako_rt.h"
#include "mako_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#if defined(MAKO_HAS_OPENSSL) || defined(MAKO_USE_OPENSSL)
#include <openssl/ssl.h>
#include <openssl/err.h>
#define MAKO_MAIL_SSL 1
#else
#define MAKO_MAIL_SSL 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MAKO_MAIL_MAX_MSG 8
#define MAKO_MAIL_MAX_ADDR 32
#define MAKO_MAIL_MAX_HDR 32
#define MAKO_MAIL_MAX_ATT 8
#define MAKO_SMTP_MAX_CLIENT 8
#define MAKO_MAIL_BUF 65536

/* ---- Message builder ---- */

typedef struct {
    char name[128];
    char value[512];
} MakoMailHdr;

typedef struct {
    char filename[128];
    char ctype[128];
    char *data;
    size_t len;
} MakoMailAtt;

typedef struct {
    int live;
    char from[256];
    char subject[512];
    char *text;
    size_t text_len;
    char *html;
    size_t html_len;
    int n_to, n_cc, n_bcc;
    char to[MAKO_MAIL_MAX_ADDR][256];
    char cc[MAKO_MAIL_MAX_ADDR][256];
    char bcc[MAKO_MAIL_MAX_ADDR][256];
    int n_hdr;
    MakoMailHdr hdr[MAKO_MAIL_MAX_HDR];
    int n_att;
    MakoMailAtt att[MAKO_MAIL_MAX_ATT];
} MakoMailMsg;

static MakoMailMsg mako_mail_msgs[MAKO_MAIL_MAX_MSG];

static inline MakoMailMsg *mako_mail_msg_ref(int64_t h) {
    if (h < 1 || h > MAKO_MAIL_MAX_MSG) return NULL;
    MakoMailMsg *m = &mako_mail_msgs[h - 1];
    return m->live ? m : NULL;
}

static inline void mako_mail_copy_str(char *dst, size_t cap, MakoString s) {
    size_t n = s.len < cap - 1 ? s.len : cap - 1;
    if (n && s.data) memcpy(dst, s.data, n);
    dst[n] = 0;
}

static inline int64_t mako_mail_msg_new(void) {
    for (int i = 0; i < MAKO_MAIL_MAX_MSG; i++) {
        if (!mako_mail_msgs[i].live) {
            memset(&mako_mail_msgs[i], 0, sizeof(MakoMailMsg));
            mako_mail_msgs[i].live = 1;
            return (int64_t)(i + 1);
        }
    }
    return -1;
}

static inline int64_t mako_mail_msg_free(int64_t h) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m) return 0;
    free(m->text);
    free(m->html);
    for (int i = 0; i < m->n_att; i++) free(m->att[i].data);
    memset(m, 0, sizeof(MakoMailMsg));
    return 1;
}

static inline int64_t mako_mail_msg_set_from(int64_t h, MakoString from) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m) return 0;
    mako_mail_copy_str(m->from, sizeof(m->from), from);
    return 1;
}

static inline int64_t mako_mail_msg_add_to(int64_t h, MakoString addr) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m || m->n_to >= MAKO_MAIL_MAX_ADDR) return 0;
    mako_mail_copy_str(m->to[m->n_to++], sizeof(m->to[0]), addr);
    return 1;
}

static inline int64_t mako_mail_msg_add_cc(int64_t h, MakoString addr) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m || m->n_cc >= MAKO_MAIL_MAX_ADDR) return 0;
    mako_mail_copy_str(m->cc[m->n_cc++], sizeof(m->cc[0]), addr);
    return 1;
}

static inline int64_t mako_mail_msg_add_bcc(int64_t h, MakoString addr) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m || m->n_bcc >= MAKO_MAIL_MAX_ADDR) return 0;
    mako_mail_copy_str(m->bcc[m->n_bcc++], sizeof(m->bcc[0]), addr);
    return 1;
}

static inline int64_t mako_mail_msg_set_subject(int64_t h, MakoString subject) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m) return 0;
    mako_mail_copy_str(m->subject, sizeof(m->subject), subject);
    return 1;
}

static inline int64_t mako_mail_msg_set_text(int64_t h, MakoString body) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m) return 0;
    free(m->text);
    m->text = (char *)malloc(body.len + 1);
    if (!m->text) return 0;
    if (body.len && body.data) memcpy(m->text, body.data, body.len);
    m->text[body.len] = 0;
    m->text_len = body.len;
    return 1;
}

static inline int64_t mako_mail_msg_set_html(int64_t h, MakoString html) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m) return 0;
    free(m->html);
    m->html = (char *)malloc(html.len + 1);
    if (!m->html) return 0;
    if (html.len && html.data) memcpy(m->html, html.data, html.len);
    m->html[html.len] = 0;
    m->html_len = html.len;
    return 1;
}

static inline int64_t mako_mail_msg_add_header(int64_t h, MakoString name, MakoString value) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m || m->n_hdr >= MAKO_MAIL_MAX_HDR) return 0;
    mako_mail_copy_str(m->hdr[m->n_hdr].name, sizeof(m->hdr[0].name), name);
    mako_mail_copy_str(m->hdr[m->n_hdr].value, sizeof(m->hdr[0].value), value);
    m->n_hdr++;
    return 1;
}

static inline int64_t mako_mail_msg_attach(
    int64_t h, MakoString filename, MakoString content_type, MakoString data
) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m || m->n_att >= MAKO_MAIL_MAX_ATT) return 0;
    MakoMailAtt *a = &m->att[m->n_att];
    mako_mail_copy_str(a->filename, sizeof(a->filename), filename);
    if (content_type.len)
        mako_mail_copy_str(a->ctype, sizeof(a->ctype), content_type);
    else
        snprintf(a->ctype, sizeof(a->ctype), "application/octet-stream");
    a->data = (char *)malloc(data.len ? data.len : 1);
    if (!a->data) return 0;
    if (data.len && data.data) memcpy(a->data, data.data, data.len);
    a->len = data.len;
    m->n_att++;
    return 1;
}

static inline void mako_mail_append(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t nc = (*cap ? *cap * 2 : 1024);
        while (nc < *len + n + 1) nc *= 2;
        char *nb = (char *)realloc(*buf, nc);
        if (!nb) return;
        *buf = nb;
        *cap = nc;
    }
    if (n) memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = 0;
}

static inline void mako_mail_appends(char **buf, size_t *len, size_t *cap, const char *s) {
    mako_mail_append(buf, len, cap, s, strlen(s));
}

static inline void mako_mail_list_header(
    char **buf, size_t *len, size_t *cap, const char *name, char addrs[][256], int n
) {
    if (n <= 0) return;
    mako_mail_appends(buf, len, cap, name);
    mako_mail_appends(buf, len, cap, ": ");
    for (int i = 0; i < n; i++) {
        if (i) mako_mail_appends(buf, len, cap, ", ");
        mako_mail_appends(buf, len, cap, addrs[i]);
    }
    mako_mail_appends(buf, len, cap, "\r\n");
}

static inline MakoString mako_mail_b64_wrap(const char *data, size_t n) {
    MakoString raw = {(char *)data, n};
    MakoString b64 = mako_base64_encode(raw);
    /* wrap at 76 chars */
    size_t outcap = b64.len + b64.len / 76 * 2 + 4;
    char *o = (char *)malloc(outcap);
    size_t j = 0, col = 0;
    for (size_t i = 0; i < b64.len; i++) {
        o[j++] = b64.data[i];
        col++;
        if (col >= 76) {
            o[j++] = '\r';
            o[j++] = '\n';
            col = 0;
        }
    }
    if (col) {
        o[j++] = '\r';
        o[j++] = '\n';
    }
    o[j] = 0;
    mako_str_free(b64);
    return (MakoString){o, j};
}

static int mako_mail_bnd_seq = 0;
static inline void mako_mail_boundary(char *out, size_t cap) {
    uint64_t x = (uint64_t)mako_mono_ns() ^ 0x9e3779b97f4a7c15ULL;
    int seq = ++mako_mail_bnd_seq;
    snprintf(
        out, cap, "----=_Mako_%016llx_%d", (unsigned long long)x, seq
    );
}

/* Build full RFC822 message bytes. */
static inline MakoString mako_mail_msg_build(int64_t h) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m) return mako_str_from_cstr("");
    char *buf = NULL;
    size_t len = 0, cap = 0;

    /* Required headers */
    if (m->from[0]) {
        mako_mail_appends(&buf, &len, &cap, "From: ");
        mako_mail_appends(&buf, &len, &cap, m->from);
        mako_mail_appends(&buf, &len, &cap, "\r\n");
    }
    mako_mail_list_header(&buf, &len, &cap, "To", m->to, m->n_to);
    mako_mail_list_header(&buf, &len, &cap, "Cc", m->cc, m->n_cc);
    /* Bcc not included in headers (envelope only) */
    if (m->subject[0]) {
        mako_mail_appends(&buf, &len, &cap, "Subject: ");
        mako_mail_appends(&buf, &len, &cap, m->subject);
        mako_mail_appends(&buf, &len, &cap, "\r\n");
    }
    /* Date */
    {
        time_t now = time(NULL);
        struct tm tm;
#if defined(_WIN32)
        gmtime_s(&tm, &now);
#else
        gmtime_r(&now, &tm);
#endif
        char date[64];
        strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S +0000", &tm);
        mako_mail_appends(&buf, &len, &cap, "Date: ");
        mako_mail_appends(&buf, &len, &cap, date);
        mako_mail_appends(&buf, &len, &cap, "\r\n");
    }
    /* Message-ID */
    {
        char mid[128];
        snprintf(mid, sizeof(mid), "<%llx.%llx@mako.local>",
                 (unsigned long long)mako_mono_ns(),
                 (unsigned long long)(uintptr_t)m);
        mako_mail_appends(&buf, &len, &cap, "Message-ID: ");
        mako_mail_appends(&buf, &len, &cap, mid);
        mako_mail_appends(&buf, &len, &cap, "\r\n");
    }
    mako_mail_appends(&buf, &len, &cap, "MIME-Version: 1.0\r\n");
    for (int i = 0; i < m->n_hdr; i++) {
        mako_mail_appends(&buf, &len, &cap, m->hdr[i].name);
        mako_mail_appends(&buf, &len, &cap, ": ");
        mako_mail_appends(&buf, &len, &cap, m->hdr[i].value);
        mako_mail_appends(&buf, &len, &cap, "\r\n");
    }

    int has_text = m->text && m->text_len;
    int has_html = m->html && m->html_len;
    int has_att = m->n_att > 0;
    int has_alt = has_text && has_html;

    char b_mixed[64], b_alt[64];
    mako_mail_boundary(b_mixed, sizeof(b_mixed));
    mako_mail_boundary(b_alt, sizeof(b_alt));

    if (has_att) {
        mako_mail_appends(&buf, &len, &cap, "Content-Type: multipart/mixed; boundary=\"");
        mako_mail_appends(&buf, &len, &cap, b_mixed);
        mako_mail_appends(&buf, &len, &cap, "\"\r\n\r\n");
        mako_mail_appends(&buf, &len, &cap, "This is a multi-part message in MIME format.\r\n\r\n");
        mako_mail_appends(&buf, &len, &cap, "--");
        mako_mail_appends(&buf, &len, &cap, b_mixed);
        mako_mail_appends(&buf, &len, &cap, "\r\n");
    }

    if (has_alt) {
        mako_mail_appends(&buf, &len, &cap, "Content-Type: multipart/alternative; boundary=\"");
        mako_mail_appends(&buf, &len, &cap, b_alt);
        mako_mail_appends(&buf, &len, &cap, "\"\r\n\r\n");
        /* text part */
        mako_mail_appends(&buf, &len, &cap, "--");
        mako_mail_appends(&buf, &len, &cap, b_alt);
        mako_mail_appends(&buf, &len, &cap, "\r\nContent-Type: text/plain; charset=utf-8\r\n");
        mako_mail_appends(&buf, &len, &cap, "Content-Transfer-Encoding: 8bit\r\n\r\n");
        mako_mail_append(&buf, &len, &cap, m->text, m->text_len);
        mako_mail_appends(&buf, &len, &cap, "\r\n--");
        mako_mail_appends(&buf, &len, &cap, b_alt);
        mako_mail_appends(&buf, &len, &cap, "\r\nContent-Type: text/html; charset=utf-8\r\n");
        mako_mail_appends(&buf, &len, &cap, "Content-Transfer-Encoding: 8bit\r\n\r\n");
        mako_mail_append(&buf, &len, &cap, m->html, m->html_len);
        mako_mail_appends(&buf, &len, &cap, "\r\n--");
        mako_mail_appends(&buf, &len, &cap, b_alt);
        mako_mail_appends(&buf, &len, &cap, "--\r\n");
    } else if (has_html) {
        if (!has_att) {
            mako_mail_appends(&buf, &len, &cap, "Content-Type: text/html; charset=utf-8\r\n");
            mako_mail_appends(&buf, &len, &cap, "Content-Transfer-Encoding: 8bit\r\n\r\n");
        } else {
            mako_mail_appends(&buf, &len, &cap, "Content-Type: text/html; charset=utf-8\r\n");
            mako_mail_appends(&buf, &len, &cap, "Content-Transfer-Encoding: 8bit\r\n\r\n");
        }
        mako_mail_append(&buf, &len, &cap, m->html, m->html_len);
        mako_mail_appends(&buf, &len, &cap, "\r\n");
    } else if (has_text) {
        if (!has_att) {
            mako_mail_appends(&buf, &len, &cap, "Content-Type: text/plain; charset=utf-8\r\n");
            mako_mail_appends(&buf, &len, &cap, "Content-Transfer-Encoding: 8bit\r\n\r\n");
        } else {
            mako_mail_appends(&buf, &len, &cap, "Content-Type: text/plain; charset=utf-8\r\n");
            mako_mail_appends(&buf, &len, &cap, "Content-Transfer-Encoding: 8bit\r\n\r\n");
        }
        mako_mail_append(&buf, &len, &cap, m->text, m->text_len);
        mako_mail_appends(&buf, &len, &cap, "\r\n");
    } else if (!has_att) {
        mako_mail_appends(&buf, &len, &cap, "Content-Type: text/plain; charset=utf-8\r\n\r\n");
    }

    for (int i = 0; i < m->n_att; i++) {
        MakoMailAtt *a = &m->att[i];
        mako_mail_appends(&buf, &len, &cap, "--");
        mako_mail_appends(&buf, &len, &cap, b_mixed);
        mako_mail_appends(&buf, &len, &cap, "\r\nContent-Type: ");
        mako_mail_appends(&buf, &len, &cap, a->ctype);
        mako_mail_appends(&buf, &len, &cap, "; name=\"");
        mako_mail_appends(&buf, &len, &cap, a->filename);
        mako_mail_appends(&buf, &len, &cap, "\"\r\nContent-Transfer-Encoding: base64\r\n");
        mako_mail_appends(&buf, &len, &cap, "Content-Disposition: attachment; filename=\"");
        mako_mail_appends(&buf, &len, &cap, a->filename);
        mako_mail_appends(&buf, &len, &cap, "\"\r\n\r\n");
        MakoString b64 = mako_mail_b64_wrap(a->data, a->len);
        mako_mail_append(&buf, &len, &cap, b64.data, b64.len);
        mako_str_free(b64);
    }
    if (has_att) {
        mako_mail_appends(&buf, &len, &cap, "--");
        mako_mail_appends(&buf, &len, &cap, b_mixed);
        mako_mail_appends(&buf, &len, &cap, "--\r\n");
    }

    if (!buf) return mako_str_from_cstr("");
    return (MakoString){buf, len};
}

static inline MakoString mako_mail_msg_envelope_from(int64_t h) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m || !m->from[0]) return mako_str_from_cstr("");
    /* extract bare address if "Name <a@b>" */
    MakoString s = mako_str_from_cstr(m->from);
    MakoString bare = mako_mail_parse_address(s);
    mako_str_free(s);
    return bare;
}

static inline int64_t mako_mail_msg_rcpt_count(int64_t h) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m) return 0;
    return (int64_t)(m->n_to + m->n_cc + m->n_bcc);
}

static inline MakoString mako_mail_msg_rcpt_at(int64_t h, int64_t i) {
    MakoMailMsg *m = mako_mail_msg_ref(h);
    if (!m || i < 0) return mako_str_from_cstr("");
    int idx = (int)i;
    if (idx < m->n_to) {
        MakoString s = mako_str_from_cstr(m->to[idx]);
        MakoString bare = mako_mail_parse_address(s);
        mako_str_free(s);
        return bare;
    }
    idx -= m->n_to;
    if (idx < m->n_cc) {
        MakoString s = mako_str_from_cstr(m->cc[idx]);
        MakoString bare = mako_mail_parse_address(s);
        mako_str_free(s);
        return bare;
    }
    idx -= m->n_cc;
    if (idx < m->n_bcc) {
        MakoString s = mako_str_from_cstr(m->bcc[idx]);
        MakoString bare = mako_mail_parse_address(s);
        mako_str_free(s);
        return bare;
    }
    return mako_str_from_cstr("");
}

/* ---- SMTP client session ---- */

typedef struct {
    int live;
    char host[256];
    int port;
    int timeout_ms;
    mako_sock_t fd;
#if MAKO_MAIL_SSL
    SSL_CTX *ssl_ctx;
    SSL *ssl;
#endif
    int tls; /* 1 if SSL active */
    char last_reply[512];
    int last_code;
} MakoSmtpClient;

static MakoSmtpClient mako_smtp_clients[MAKO_SMTP_MAX_CLIENT];

static inline MakoSmtpClient *mako_smtp_ref(int64_t h) {
    if (h < 1 || h > MAKO_SMTP_MAX_CLIENT) return NULL;
    MakoSmtpClient *c = &mako_smtp_clients[h - 1];
    return c->live ? c : NULL;
}

static inline int64_t mako_smtp_new(MakoString host, int64_t port) {
    for (int i = 0; i < MAKO_SMTP_MAX_CLIENT; i++) {
        if (!mako_smtp_clients[i].live) {
            memset(&mako_smtp_clients[i], 0, sizeof(MakoSmtpClient));
            mako_smtp_clients[i].live = 1;
            mako_mail_copy_str(mako_smtp_clients[i].host, sizeof(mako_smtp_clients[i].host), host);
            mako_smtp_clients[i].port = port > 0 ? (int)port : 25;
            mako_smtp_clients[i].timeout_ms = 10000;
            mako_smtp_clients[i].fd = MAKO_INVALID_SOCK;
            return (int64_t)(i + 1);
        }
    }
    return -1;
}

static inline int64_t mako_smtp_set_timeout_ms(int64_t h, int64_t ms) {
    MakoSmtpClient *c = mako_smtp_ref(h);
    if (!c) return 0;
    c->timeout_ms = ms > 0 ? (int)ms : 10000;
    return 1;
}

static inline MakoString mako_smtp_last_reply(int64_t h) {
    MakoSmtpClient *c = mako_smtp_ref(h);
    if (!c) return mako_str_from_cstr("");
    return mako_str_from_cstr(c->last_reply);
}

static inline int64_t mako_smtp_last_code(int64_t h) {
    MakoSmtpClient *c = mako_smtp_ref(h);
    return c ? (int64_t)c->last_code : 0;
}

static inline int mako_smtp_io_write(MakoSmtpClient *c, const void *buf, size_t n) {
#if MAKO_MAIL_SSL
    if (c->tls && c->ssl) {
        size_t off = 0;
        while (off < n) {
            int w = SSL_write(c->ssl, (const char *)buf + off, (int)(n - off));
            if (w <= 0) return -1;
            off += (size_t)w;
        }
        return 0;
    }
#endif
    size_t off = 0;
    while (off < n) {
        ssize_t w = send(c->fd, (const char *)buf + off, n - off, 0);
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

static inline int mako_smtp_io_read_line(MakoSmtpClient *c, char *buf, size_t cap) {
    size_t n = 0;
    while (n + 1 < cap) {
        char ch = 0;
#if MAKO_MAIL_SSL
        if (c->tls && c->ssl) {
            int r = SSL_read(c->ssl, &ch, 1);
            if (r <= 0) break;
        } else
#endif
        {
            ssize_t r = recv(c->fd, &ch, 1, 0);
            if (r <= 0) break;
        }
        buf[n++] = ch;
        if (ch == '\n') break;
    }
    buf[n] = 0;
    return (int)n;
}

/* Read full SMTP reply (multi-line 250-... until 250 ). */
static inline int mako_smtp_read_reply(MakoSmtpClient *c) {
    c->last_reply[0] = 0;
    c->last_code = 0;
    size_t total = 0;
    for (;;) {
        char line[512];
        int n = mako_smtp_io_read_line(c, line, sizeof(line));
        if (n <= 0) break;
        if (total + (size_t)n < sizeof(c->last_reply) - 1) {
            memcpy(c->last_reply + total, line, (size_t)n);
            total += (size_t)n;
            c->last_reply[total] = 0;
        }
        if (n >= 4 && isdigit((unsigned char)line[0]) && isdigit((unsigned char)line[1])
            && isdigit((unsigned char)line[2]) && line[3] == ' ') {
            c->last_code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
            break;
        }
        if (n >= 3 && isdigit((unsigned char)line[0]) && !c->last_code)
            c->last_code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
    }
    return c->last_code;
}

static inline int mako_smtp_cmd(MakoSmtpClient *c, const char *cmd) {
    if (mako_smtp_io_write(c, cmd, strlen(cmd)) < 0) return -1;
    if (mako_smtp_io_write(c, "\r\n", 2) < 0) return -1;
    return mako_smtp_read_reply(c);
}

static inline int64_t mako_smtp_connect(int64_t h) {
    MakoSmtpClient *c = mako_smtp_ref(h);
    if (!c) return 0;
    if (!mako_net_init()) return 0;
    /* Prefer dual-stack tcp_connect */
    MakoString host = mako_str_from_cstr(c->host);
    int64_t fd = mako_tcp_connect(host, c->port);
    mako_str_free(host);
    if (fd < 0) return 0;
    c->fd = (mako_sock_t)fd;
#if !defined(_WIN32)
    struct timeval tv;
    tv.tv_sec = c->timeout_ms / 1000;
    tv.tv_usec = (c->timeout_ms % 1000) * 1000;
    setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    int code = mako_smtp_read_reply(c);
    return code >= 200 && code < 300 ? 1 : 0;
}

static inline int64_t mako_smtp_ehlo(int64_t h, MakoString hostname) {
    MakoSmtpClient *c = mako_smtp_ref(h);
    if (!c || c->fd == MAKO_INVALID_SOCK) return 0;
    char cmd[320];
    const char *hn = hostname.len && hostname.data ? hostname.data : "mako.local";
    int n = hostname.len > 0 ? (int)hostname.len : 10;
    if (n > 200) n = 200;
    snprintf(cmd, sizeof(cmd), "EHLO %.*s", n, hn);
    int code = mako_smtp_cmd(c, cmd);
    if (code >= 200 && code < 300) return 1;
    /* fallback HELO */
    snprintf(cmd, sizeof(cmd), "HELO %.*s", n, hn);
    code = mako_smtp_cmd(c, cmd);
    return code >= 200 && code < 300 ? 1 : 0;
}

static inline int64_t mako_smtp_starttls(int64_t h) {
    MakoSmtpClient *c = mako_smtp_ref(h);
    if (!c || c->fd == MAKO_INVALID_SOCK) return 0;
    int code = mako_smtp_cmd(c, "STARTTLS");
    if (code < 200 || code >= 300) return 0;
#if MAKO_MAIL_SSL
    c->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!c->ssl_ctx) return 0;
    {
        const char *ver = getenv("MAKO_SMTP_TLS_VERIFY");
        if (ver && ver[0] == '1') {
            SSL_CTX_set_default_verify_paths(c->ssl_ctx);
            SSL_CTX_set_verify(c->ssl_ctx, SSL_VERIFY_PEER, NULL);
        } else {
            SSL_CTX_set_verify(c->ssl_ctx, SSL_VERIFY_NONE, NULL);
        }
    }
    c->ssl = SSL_new(c->ssl_ctx);
    if (!c->ssl) return 0;
    SSL_set_fd(c->ssl, (int)c->fd);
    if (c->host[0]) SSL_set_tlsext_host_name(c->ssl, c->host);
    if (SSL_connect(c->ssl) <= 0) return 0;
    c->tls = 1;
    /* re-EHLO after STARTTLS */
    MakoString hn = mako_str_from_cstr("mako.local");
    int64_t ok = mako_smtp_ehlo(h, hn);
    mako_str_free(hn);
    return ok;
#else
    return 0;
#endif
}

/* Session AUTH PLAIN (name avoids clash with mako_smtp_auth_plain → string command). */
static inline int64_t mako_smtp_do_auth_plain(int64_t h, MakoString user, MakoString pass) {
    MakoSmtpClient *c = mako_smtp_ref(h);
    if (!c || c->fd == MAKO_INVALID_SOCK) return 0;
    /* Build AUTH PLAIN payload */
    size_t raw_len = 1 + user.len + 1 + pass.len;
    char *raw = (char *)malloc(raw_len);
    size_t o = 0;
    raw[o++] = 0;
    if (user.len) {
        memcpy(raw + o, user.data, user.len);
        o += user.len;
    }
    raw[o++] = 0;
    if (pass.len) {
        memcpy(raw + o, pass.data, pass.len);
        o += pass.len;
    }
    MakoString raws = {raw, raw_len};
    MakoString b64 = mako_base64_encode(raws);
    free(raw);
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "AUTH PLAIN %.*s", (int)b64.len, b64.data ? b64.data : "");
    mako_str_free(b64);
    int code = mako_smtp_cmd(c, cmd);
    return code >= 200 && code < 300 ? 1 : 0;
}

static inline int64_t mako_smtp_mail_from(int64_t h, MakoString from) {
    MakoSmtpClient *c = mako_smtp_ref(h);
    if (!c) return 0;
    MakoString bare = mako_mail_parse_address(from);
    char cmd[320];
    snprintf(cmd, sizeof(cmd), "MAIL FROM:<%.*s>", (int)bare.len, bare.data ? bare.data : "");
    mako_str_free(bare);
    int code = mako_smtp_cmd(c, cmd);
    return code >= 200 && code < 300 ? 1 : 0;
}

static inline int64_t mako_smtp_rcpt_to(int64_t h, MakoString to) {
    MakoSmtpClient *c = mako_smtp_ref(h);
    if (!c) return 0;
    MakoString bare = mako_mail_parse_address(to);
    char cmd[320];
    snprintf(cmd, sizeof(cmd), "RCPT TO:<%.*s>", (int)bare.len, bare.data ? bare.data : "");
    mako_str_free(bare);
    int code = mako_smtp_cmd(c, cmd);
    return code >= 200 && code < 300 ? 1 : 0;
}

/* Dot-stuff DATA body and send. */
static inline int64_t mako_smtp_data(int64_t h, MakoString msg) {
    MakoSmtpClient *c = mako_smtp_ref(h);
    if (!c) return 0;
    int code = mako_smtp_cmd(c, "DATA");
    if (code < 300 || code >= 400) return 0; /* expect 354 */
    /* Dot-stuff lines starting with . */
    const char *p = msg.data ? msg.data : "";
    size_t n = msg.len;
    size_t i = 0;
    int line_start = 1;
    while (i < n) {
        if (line_start && p[i] == '.') {
            if (mako_smtp_io_write(c, ".", 1) < 0) return 0;
        }
        char ch = p[i++];
        if (mako_smtp_io_write(c, &ch, 1) < 0) return 0;
        line_start = (ch == '\n');
    }
    if (n < 2 || p[n - 1] != '\n') {
        if (mako_smtp_io_write(c, "\r\n", 2) < 0) return 0;
    }
    if (mako_smtp_io_write(c, ".\r\n", 3) < 0) return 0;
    code = mako_smtp_read_reply(c);
    return code >= 200 && code < 300 ? 1 : 0;
}

static inline int64_t mako_smtp_quit(int64_t h) {
    MakoSmtpClient *c = mako_smtp_ref(h);
    if (!c) return 0;
    if (c->fd != MAKO_INVALID_SOCK) {
        (void)mako_smtp_cmd(c, "QUIT");
    }
    return 1;
}

static inline int64_t mako_smtp_close(int64_t h) {
    MakoSmtpClient *c = mako_smtp_ref(h);
    if (!c) return 0;
#if MAKO_MAIL_SSL
    if (c->ssl) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
        c->ssl = NULL;
    }
    if (c->ssl_ctx) {
        SSL_CTX_free(c->ssl_ctx);
        c->ssl_ctx = NULL;
    }
#endif
    if (c->fd != MAKO_INVALID_SOCK) {
        mako_sock_close(c->fd);
        c->fd = MAKO_INVALID_SOCK;
    }
    c->tls = 0;
    c->live = 0;
    return 1;
}

/* Full send of a built message handle over an open client (MAIL/RCPT/DATA). */
static inline int64_t mako_smtp_send_built(int64_t client, int64_t msg_h) {
    MakoSmtpClient *c = mako_smtp_ref(client);
    MakoMailMsg *m = mako_mail_msg_ref(msg_h);
    if (!c || !m) return 0;
    MakoString from = mako_mail_msg_envelope_from(msg_h);
    if (!mako_smtp_mail_from(client, from)) {
        mako_str_free(from);
        return 0;
    }
    mako_str_free(from);
    int64_t n = mako_mail_msg_rcpt_count(msg_h);
    if (n <= 0) return 0;
    for (int64_t i = 0; i < n; i++) {
        MakoString r = mako_mail_msg_rcpt_at(msg_h, i);
        int ok = mako_smtp_rcpt_to(client, r);
        mako_str_free(r);
        if (!ok) return 0;
    }
    MakoString body = mako_mail_msg_build(msg_h);
    int ok = mako_smtp_data(client, body);
    mako_str_free(body);
    return ok ? 1 : 0;
}

/* One-shot: connect, optional STARTTLS+auth, send message, quit.
 * use_tls: 0 plain, 1 STARTTLS if available, 2 require STARTTLS.
 * Returns 1 success, 0 fail, -1 connect fail.
 */
static inline int64_t mako_smtp_send_msg(
    MakoString host,
    int64_t port,
    MakoString user,
    MakoString pass,
    int64_t msg_h,
    int64_t use_tls
) {
    int64_t c = mako_smtp_new(host, port);
    if (c < 0) return 0;
    if (!mako_smtp_connect(c)) {
        mako_smtp_close(c);
        return -1;
    }
    MakoString hn = mako_str_from_cstr("mako.local");
    if (!mako_smtp_ehlo(c, hn)) {
        mako_str_free(hn);
        mako_smtp_close(c);
        return 0;
    }
    mako_str_free(hn);
    if (use_tls >= 1) {
        int tls_ok = mako_smtp_starttls(c);
        if (!tls_ok && use_tls >= 2) {
            mako_smtp_close(c);
            return 0;
        }
        if (tls_ok) {
            MakoString hn2 = mako_str_from_cstr("mako.local");
            (void)mako_smtp_ehlo(c, hn2);
            mako_str_free(hn2);
        }
    }
    if (user.len > 0) {
        if (!mako_smtp_do_auth_plain(c, user, pass)) {
            mako_smtp_close(c);
            return 0;
        }
    }
    int ok = mako_smtp_send_built(c, msg_h);
    (void)mako_smtp_quit(c);
    mako_smtp_close(c);
    return ok ? 1 : 0;
}

/* Convenience: format simple text message (wraps builder). */
static inline MakoString mako_mail_simple(
    MakoString from, MakoString to, MakoString subject, MakoString body
) {
    int64_t m = mako_mail_msg_new();
    if (m < 0) return mako_str_from_cstr("");
    mako_mail_msg_set_from(m, from);
    mako_mail_msg_add_to(m, to);
    mako_mail_msg_set_subject(m, subject);
    mako_mail_msg_set_text(m, body);
    MakoString out = mako_mail_msg_build(m);
    mako_mail_msg_free(m);
    return out;
}

/* ---- Loopback SMTP capture server (for tests / local programming) ----
 * Minimal SMTP that accepts one connection and stores the DATA body.
 * Not a production MTA — so you can program and verify send() without MailHog.
 */
static int mako_smtp_mock_lfd = -1;
static int mako_smtp_mock_port = 0;
static char *mako_smtp_mock_msg = NULL;
static size_t mako_smtp_mock_msg_len = 0;
static char mako_smtp_mock_from[256];
static char mako_smtp_mock_rcpt[256];

static inline void mako_smtp_mock_reply(mako_sock_t fd, const char *line) {
    send(fd, line, strlen(line), 0);
}

static inline int mako_smtp_mock_readline(mako_sock_t fd, char *buf, size_t cap) {
    size_t n = 0;
    while (n + 1 < cap) {
        char ch = 0;
        ssize_t r = recv(fd, &ch, 1, 0);
        if (r <= 0) break;
        buf[n++] = ch;
        if (ch == '\n') break;
    }
    buf[n] = 0;
    return (int)n;
}

/* Bind 127.0.0.1:port (port 0 → ephemeral). Returns bound port or -1. */
static inline int64_t mako_smtp_mock_start(int64_t port) {
    if (!mako_net_init()) return -1;
    if (mako_smtp_mock_lfd >= 0) {
        mako_sock_close((mako_sock_t)mako_smtp_mock_lfd);
        mako_smtp_mock_lfd = -1;
    }
    free(mako_smtp_mock_msg);
    mako_smtp_mock_msg = NULL;
    mako_smtp_mock_msg_len = 0;
    mako_smtp_mock_from[0] = 0;
    mako_smtp_mock_rcpt[0] = 0;

    MakoString bind_host = mako_str_from_cstr("127.0.0.1");
    int64_t lfd = mako_tcp_listen_addr(bind_host, port);
    mako_str_free(bind_host);
    if (lfd < 0) return -1;
    mako_smtp_mock_lfd = (int)lfd;
    /* resolve actual port if 0 */
    if (port <= 0) {
        struct sockaddr_in sa;
        socklen_t sl = sizeof(sa);
        if (getsockname(mako_smtp_mock_lfd, (struct sockaddr *)&sa, &sl) == 0)
            mako_smtp_mock_port = (int)ntohs(sa.sin_port);
        else
            mako_smtp_mock_port = 0;
    } else {
        mako_smtp_mock_port = (int)port;
    }
    return (int64_t)mako_smtp_mock_port;
}

static inline int64_t mako_smtp_mock_port_get(void) {
    return (int64_t)mako_smtp_mock_port;
}

/* Accept one client, speak minimal SMTP, capture DATA. Returns 1 on success. */
static inline int64_t mako_smtp_mock_serve_once(void) {
    if (mako_smtp_mock_lfd < 0) return 0;
    int cfd = accept(mako_smtp_mock_lfd, NULL, NULL);
    if (cfd < 0) return 0;
#if !defined(_WIN32)
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    mako_smtp_mock_reply((mako_sock_t)cfd, "220 mako-mock ESMTP ready\r\n");
    char line[1024];
    char *data = NULL;
    size_t dlen = 0, dcap = 0;
    int in_data = 0;
    for (;;) {
        int n = mako_smtp_mock_readline((mako_sock_t)cfd, line, sizeof(line));
        if (n <= 0) break;
        if (in_data) {
            /* end of data: lone ".\r\n" */
            if ((n == 3 && line[0] == '.' && line[1] == '\r')
                || (n == 2 && line[0] == '.' && line[1] == '\n')) {
                in_data = 0;
                free(mako_smtp_mock_msg);
                mako_smtp_mock_msg = data;
                mako_smtp_mock_msg_len = dlen;
                data = NULL;
                dlen = dcap = 0;
                mako_smtp_mock_reply((mako_sock_t)cfd, "250 OK queued\r\n");
                continue;
            }
            /* un-dot-stuff */
            const char *p = line;
            int plen = n;
            if (plen >= 2 && p[0] == '.' && p[1] == '.') {
                p++;
                plen--;
            }
            if (dlen + (size_t)plen + 1 > dcap) {
                size_t nc = dcap ? dcap * 2 : 4096;
                while (nc < dlen + (size_t)plen + 1) nc *= 2;
                char *nb = (char *)realloc(data, nc);
                if (!nb) break;
                data = nb;
                dcap = nc;
            }
            memcpy(data + dlen, p, (size_t)plen);
            dlen += (size_t)plen;
            data[dlen] = 0;
            continue;
        }
        /* commands */
        if (n >= 4 && (strncmp(line, "EHLO", 4) == 0 || strncmp(line, "HELO", 4) == 0)) {
            mako_smtp_mock_reply(
                (mako_sock_t)cfd, "250-mako-mock hello\r\n250-AUTH PLAIN\r\n250 OK\r\n"
            );
        } else if (n >= 10 && strncmp(line, "AUTH PLAIN", 10) == 0) {
            mako_smtp_mock_reply((mako_sock_t)cfd, "235 Authentication successful\r\n");
        } else if (n >= 10 && strncmp(line, "MAIL FROM:", 10) == 0) {
            /* extract <addr> */
            char *lt = strchr(line, '<');
            char *gt = lt ? strchr(lt, '>') : NULL;
            if (lt && gt && gt > lt + 1) {
                size_t al = (size_t)(gt - lt - 1);
                if (al >= sizeof(mako_smtp_mock_from)) al = sizeof(mako_smtp_mock_from) - 1;
                memcpy(mako_smtp_mock_from, lt + 1, al);
                mako_smtp_mock_from[al] = 0;
            }
            mako_smtp_mock_reply((mako_sock_t)cfd, "250 OK\r\n");
        } else if (n >= 8 && strncmp(line, "RCPT TO:", 8) == 0) {
            char *lt = strchr(line, '<');
            char *gt = lt ? strchr(lt, '>') : NULL;
            if (lt && gt && gt > lt + 1) {
                size_t al = (size_t)(gt - lt - 1);
                if (al >= sizeof(mako_smtp_mock_rcpt)) al = sizeof(mako_smtp_mock_rcpt) - 1;
                memcpy(mako_smtp_mock_rcpt, lt + 1, al);
                mako_smtp_mock_rcpt[al] = 0;
            }
            mako_smtp_mock_reply((mako_sock_t)cfd, "250 OK\r\n");
        } else if (n >= 4 && strncmp(line, "DATA", 4) == 0) {
            in_data = 1;
            free(data);
            data = NULL;
            dlen = dcap = 0;
            mako_smtp_mock_reply((mako_sock_t)cfd, "354 End data with <CR><LF>.<CR><LF>\r\n");
        } else if (n >= 4 && strncmp(line, "QUIT", 4) == 0) {
            mako_smtp_mock_reply((mako_sock_t)cfd, "221 Bye\r\n");
            break;
        } else if (n >= 4 && strncmp(line, "RSET", 4) == 0) {
            mako_smtp_mock_reply((mako_sock_t)cfd, "250 OK\r\n");
        } else if (n >= 4 && strncmp(line, "NOOP", 4) == 0) {
            mako_smtp_mock_reply((mako_sock_t)cfd, "250 OK\r\n");
        } else {
            mako_smtp_mock_reply((mako_sock_t)cfd, "250 OK\r\n");
        }
    }
    free(data);
    mako_sock_close((mako_sock_t)cfd);
    return mako_smtp_mock_msg ? 1 : 0;
}

static inline MakoString mako_smtp_mock_last_message(void) {
    if (!mako_smtp_mock_msg) return mako_str_from_cstr("");
    char *d = (char *)malloc(mako_smtp_mock_msg_len + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, mako_smtp_mock_msg, mako_smtp_mock_msg_len);
    d[mako_smtp_mock_msg_len] = 0;
    return (MakoString){d, mako_smtp_mock_msg_len};
}

static inline MakoString mako_smtp_mock_last_from(void) {
    return mako_str_from_cstr(mako_smtp_mock_from);
}

static inline MakoString mako_smtp_mock_last_rcpt(void) {
    return mako_str_from_cstr(mako_smtp_mock_rcpt);
}

static inline int64_t mako_smtp_mock_stop(void) {
    if (mako_smtp_mock_lfd >= 0) {
        mako_sock_close((mako_sock_t)mako_smtp_mock_lfd);
        mako_smtp_mock_lfd = -1;
    }
    free(mako_smtp_mock_msg);
    mako_smtp_mock_msg = NULL;
    mako_smtp_mock_msg_len = 0;
    mako_smtp_mock_port = 0;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_MAIL_H */
