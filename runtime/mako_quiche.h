/* Quiche FFI — handshake + HTTP/3 GET via quiche_h3_* (MAKO_HAS_QUICHE). */
#ifndef MAKO_QUICHE_H
#define MAKO_QUICHE_H

#include "mako_rt.h"

#if defined(MAKO_HAS_QUICHE)
#include <quiche.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAKO_QUIC_SCID_LEN 16
#define MAKO_QUIC_MAX_DATAGRAM 1350
#define MAKO_H3_BODY_CAP 4096

static inline int64_t mako_quiche_available(void) {
    return 1;
}

static inline MakoString mako_quiche_version(void) {
    const char *v = quiche_version();
    if (!v || !v[0]) return mako_str_from_cstr("");
    return mako_str_from_cstr(v);
}

static inline int mako_quic_flush(
    int sock, quiche_conn *conn, uint8_t *out, size_t out_cap
) {
    int sent_any = 0;
    while (1) {
        quiche_send_info send_info;
        memset(&send_info, 0, sizeof(send_info));
        ssize_t written = quiche_conn_send(conn, out, out_cap, &send_info);
        if (written == QUICHE_ERR_DONE) break;
        if (written < 0) return -1;
        ssize_t sent = sendto(
            sock, out, (size_t)written, 0,
            (struct sockaddr *)&send_info.to, send_info.to_len
        );
        if (sent != written) return -1;
        sent_any = 1;
    }
    return sent_any;
}

typedef struct {
    int sock;
    struct addrinfo *peer;
    struct sockaddr_storage local_addr;
    socklen_t local_len;
    quiche_config *config;
    quiche_conn *conn;
} MakoQuicClient;

static inline void mako_quic_client_cleanup(MakoQuicClient *c) {
    if (!c) return;
    if (c->conn) quiche_conn_free(c->conn);
    if (c->config) quiche_config_free(c->config);
    if (c->sock >= 0) close(c->sock);
    if (c->peer) freeaddrinfo(c->peer);
    memset(c, 0, sizeof(*c));
    c->sock = -1;
}

/* Open UDP + quiche_connect + drive until established. reason must be writable. */
static inline int mako_quic_client_connect(
    MakoQuicClient *c,
    const char *host, const char *port, const char *sni, int verify_peer,
    char *reason, size_t reason_cap
) {
    memset(c, 0, sizeof(*c));
    c->sock = -1;
    if (reason && reason_cap) reason[0] = 0;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    if (getaddrinfo(host, port, &hints, &c->peer) != 0 || !c->peer) {
        if (reason) snprintf(reason, reason_cap, "resolve");
        return -1;
    }
    c->sock = socket(c->peer->ai_family, SOCK_DGRAM, 0);
    if (c->sock < 0) {
        if (reason) snprintf(reason, reason_cap, "socket");
        mako_quic_client_cleanup(c);
        return -1;
    }
    if (fcntl(c->sock, F_SETFL, O_NONBLOCK) != 0) {
        if (reason) snprintf(reason, reason_cap, "nonblock");
        mako_quic_client_cleanup(c);
        return -1;
    }
    if (c->peer->ai_family == AF_INET) {
        struct sockaddr_in any;
        memset(&any, 0, sizeof(any));
        any.sin_family = AF_INET;
        any.sin_addr.s_addr = htonl(INADDR_ANY);
        any.sin_port = 0;
        if (bind(c->sock, (struct sockaddr *)&any, sizeof(any)) != 0) {
            if (reason) snprintf(reason, reason_cap, "bind");
            mako_quic_client_cleanup(c);
            return -1;
        }
    } else {
        struct sockaddr_in6 any6;
        memset(&any6, 0, sizeof(any6));
        any6.sin6_family = AF_INET6;
        any6.sin6_port = 0;
        if (bind(c->sock, (struct sockaddr *)&any6, sizeof(any6)) != 0) {
            if (reason) snprintf(reason, reason_cap, "bind6");
            mako_quic_client_cleanup(c);
            return -1;
        }
    }

    c->config = quiche_config_new(0xbabababa);
    if (!c->config) {
        if (reason) snprintf(reason, reason_cap, "config");
        mako_quic_client_cleanup(c);
        return -1;
    }
    quiche_config_verify_peer(c->config, verify_peer != 0);
    quiche_config_set_application_protos(
        c->config,
        (uint8_t *)QUICHE_H3_APPLICATION_PROTOCOL,
        sizeof(QUICHE_H3_APPLICATION_PROTOCOL) - 1
    );
    quiche_config_set_max_idle_timeout(c->config, 5000);
    quiche_config_set_max_recv_udp_payload_size(c->config, MAKO_QUIC_MAX_DATAGRAM);
    quiche_config_set_max_send_udp_payload_size(c->config, MAKO_QUIC_MAX_DATAGRAM);
    quiche_config_set_initial_max_data(c->config, 10000000);
    quiche_config_set_initial_max_stream_data_bidi_local(c->config, 1000000);
    quiche_config_set_initial_max_stream_data_bidi_remote(c->config, 1000000);
    quiche_config_set_initial_max_stream_data_uni(c->config, 1000000);
    quiche_config_set_initial_max_streams_bidi(c->config, 100);
    quiche_config_set_initial_max_streams_uni(c->config, 100);
    quiche_config_set_disable_active_migration(c->config, true);

    uint8_t scid[MAKO_QUIC_SCID_LEN];
    int rng = open("/dev/urandom", O_RDONLY);
    if (rng < 0 || read(rng, scid, sizeof(scid)) != (ssize_t)sizeof(scid)) {
        if (rng >= 0) close(rng);
        if (reason) snprintf(reason, reason_cap, "scid");
        mako_quic_client_cleanup(c);
        return -1;
    }
    close(rng);

    c->local_len = sizeof(c->local_addr);
    if (getsockname(c->sock, (struct sockaddr *)&c->local_addr, &c->local_len) != 0) {
        if (reason) snprintf(reason, reason_cap, "getsockname");
        mako_quic_client_cleanup(c);
        return -1;
    }

    c->conn = quiche_connect(
        sni, scid, sizeof(scid),
        (struct sockaddr *)&c->local_addr, c->local_len,
        c->peer->ai_addr, c->peer->ai_addrlen, c->config
    );
    if (!c->conn) {
        if (reason) snprintf(reason, reason_cap, "connect");
        mako_quic_client_cleanup(c);
        return -1;
    }

    uint8_t out[MAKO_QUIC_MAX_DATAGRAM];
    uint8_t inbuf[65535];
    if (mako_quic_flush(c->sock, c->conn, out, sizeof(out)) < 0) {
        if (reason) snprintf(reason, reason_cap, "send-initial");
        mako_quic_client_cleanup(c);
        return -1;
    }

    for (int iter = 0; iter < 256; iter++) {
        if (quiche_conn_is_established(c->conn)) return 0;
        if (quiche_conn_is_closed(c->conn)) {
            if (reason) snprintf(reason, reason_cap, "closed");
            mako_quic_client_cleanup(c);
            return -1;
        }
        struct pollfd pfd;
        pfd.fd = c->sock;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, 50);
        if (pr < 0 && errno == EINTR) continue;
        if (pr > 0 && (pfd.revents & POLLIN)) {
            while (1) {
                struct sockaddr_storage peer_addr;
                socklen_t peer_len = sizeof(peer_addr);
                ssize_t n = recvfrom(
                    c->sock, inbuf, sizeof(inbuf), 0,
                    (struct sockaddr *)&peer_addr, &peer_len
                );
                if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    if (reason) snprintf(reason, reason_cap, "recv");
                    mako_quic_client_cleanup(c);
                    return -1;
                }
                quiche_recv_info ri = {
                    (struct sockaddr *)&peer_addr, peer_len,
                    (struct sockaddr *)&c->local_addr, c->local_len,
                };
                quiche_conn_recv(c->conn, inbuf, (size_t)n, &ri);
            }
        } else {
            quiche_conn_on_timeout(c->conn);
        }
        if (mako_quic_flush(c->sock, c->conn, out, sizeof(out)) < 0) {
            if (reason) snprintf(reason, reason_cap, "send");
            mako_quic_client_cleanup(c);
            return -1;
        }
    }
    if (reason) snprintf(reason, reason_cap, "timeout");
    mako_quic_client_cleanup(c);
    return -1;
}

static inline int mako_quic_client_pump_once(MakoQuicClient *c) {
    uint8_t out[MAKO_QUIC_MAX_DATAGRAM];
    uint8_t inbuf[65535];
    struct pollfd pfd;
    pfd.fd = c->sock;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, 50);
    if (pr < 0 && errno == EINTR) return 0;
    if (pr > 0 && (pfd.revents & POLLIN)) {
        while (1) {
            struct sockaddr_storage peer_addr;
            socklen_t peer_len = sizeof(peer_addr);
            ssize_t n = recvfrom(
                c->sock, inbuf, sizeof(inbuf), 0,
                (struct sockaddr *)&peer_addr, &peer_len
            );
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                return -1;
            }
            quiche_recv_info ri = {
                (struct sockaddr *)&peer_addr, peer_len,
                (struct sockaddr *)&c->local_addr, c->local_len,
            };
            quiche_conn_recv(c->conn, inbuf, (size_t)n, &ri);
        }
    } else {
        quiche_conn_on_timeout(c->conn);
    }
    return mako_quic_flush(c->sock, c->conn, out, sizeof(out)) < 0 ? -1 : 0;
}

typedef struct {
    int32_t status;
    char body[MAKO_H3_BODY_CAP];
    size_t body_len;
    int finished;
} MakoH3Resp;

typedef struct {
    int64_t stream_id;
    MakoH3Resp resp;
} MakoH3StreamResp;

static inline int mako_h3_on_header(
    uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp
) {
    MakoH3Resp *r = (MakoH3Resp *)argp;
    if (name_len == 7 && memcmp(name, ":status", 7) == 0) {
        int32_t st = 0;
        for (size_t i = 0; i < value_len; i++) {
            if (value[i] < '0' || value[i] > '9') break;
            st = st * 10 + (value[i] - '0');
        }
        r->status = st;
    }
    return 0;
}

static inline void mako_h3_trim_body(MakoH3Resp *r) {
    while (r->body_len > 0
           && (r->body[r->body_len - 1] == '\n'
               || r->body[r->body_len - 1] == '\r'))
        r->body_len--;
    r->body[r->body_len] = 0;
}

static inline MakoH3StreamResp *mako_h3_find_stream(
    MakoH3StreamResp *streams, int n, int64_t sid
) {
    for (int i = 0; i < n; i++) {
        if (streams[i].stream_id == sid) return &streams[i];
    }
    return NULL;
}

static inline size_t mako_h3_fill_get_headers(
    quiche_h3_header *headers, const char *sni, const char *path
) {
    size_t nh = 0;
    headers[nh].name = (const uint8_t *)":method";
    headers[nh].name_len = 7;
    headers[nh].value = (const uint8_t *)"GET";
    headers[nh].value_len = 3;
    nh++;
    headers[nh].name = (const uint8_t *)":scheme";
    headers[nh].name_len = 7;
    headers[nh].value = (const uint8_t *)"https";
    headers[nh].value_len = 5;
    nh++;
    headers[nh].name = (const uint8_t *)":authority";
    headers[nh].name_len = 10;
    headers[nh].value = (const uint8_t *)sni;
    headers[nh].value_len = strlen(sni);
    nh++;
    headers[nh].name = (const uint8_t *)":path";
    headers[nh].name_len = 5;
    headers[nh].value = (const uint8_t *)path;
    headers[nh].value_len = strlen(path);
    nh++;
    headers[nh].name = (const uint8_t *)"user-agent";
    headers[nh].name_len = 10;
    headers[nh].value = (const uint8_t *)"mako";
    headers[nh].value_len = 4;
    nh++;
    return nh;
}

static inline int mako_h3_poll_into_streams(
    quiche_h3_conn *h3, quiche_conn *conn,
    MakoH3StreamResp *streams, int nstreams, uint8_t *body_tmp, size_t body_tmp_cap
) {
    int any = 0;
    while (1) {
        quiche_h3_event *ev = NULL;
        int64_t sid = quiche_h3_conn_poll(h3, conn, &ev);
        if (sid < 0) break;
        any = 1;
        MakoH3StreamResp *sr = mako_h3_find_stream(streams, nstreams, sid);
        if (!sr) {
            quiche_h3_event_free(ev);
            continue;
        }
        switch (quiche_h3_event_type(ev)) {
        case QUICHE_H3_EVENT_HEADERS:
            quiche_h3_event_for_each_header(ev, mako_h3_on_header, &sr->resp);
            break;
        case QUICHE_H3_EVENT_DATA:
            for (;;) {
                ssize_t len = quiche_h3_recv_body(
                    h3, conn, (uint64_t)sid, body_tmp, body_tmp_cap
                );
                if (len <= 0) break;
                size_t copy = (size_t)len;
                if (sr->resp.body_len + copy > sizeof(sr->resp.body) - 1)
                    copy = sizeof(sr->resp.body) - 1 - sr->resp.body_len;
                if (copy) {
                    memcpy(sr->resp.body + sr->resp.body_len, body_tmp, copy);
                    sr->resp.body_len += copy;
                }
            }
            break;
        case QUICHE_H3_EVENT_FINISHED:
            sr->resp.finished = 1;
            break;
        case QUICHE_H3_EVENT_RESET:
            sr->resp.finished = 1;
            if (sr->resp.status <= 0) sr->resp.status = 0;
            break;
        default:
            break;
        }
        quiche_h3_event_free(ev);
    }
    return any;
}

static inline int mako_h3_all_finished(MakoH3StreamResp *streams, int n) {
    for (int i = 0; i < n; i++) {
        if (!streams[i].resp.finished) return 0;
    }
    return 1;
}

/* Drive client until established.
 * Returns: "quic:ok;<alpn>" | "quic:fail;<reason>" */
static inline MakoString mako_quiche_handshake(
    MakoString host, int64_t port, MakoString sni, int64_t verify_peer
) {
    char hbuf[256], snibuf[256], portbuf[32], reason[64];
    if (host.len < 1 || host.len >= sizeof(hbuf) || sni.len < 1
        || sni.len >= sizeof(snibuf) || port <= 0 || port > 65535)
        return mako_str_from_cstr("quic:fail;bad-args");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(snibuf, sni.data, sni.len);
    snibuf[sni.len] = 0;
    snprintf(portbuf, sizeof(portbuf), "%lld", (long long)port);

    MakoQuicClient c;
    if (mako_quic_client_connect(
            &c, hbuf, portbuf, snibuf, (int)verify_peer, reason, sizeof(reason)
        ) != 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "quic:fail;%s", reason[0] ? reason : "unknown");
        return mako_str_from_cstr(buf);
    }

    const uint8_t *app = NULL;
    size_t app_len = 0;
    quiche_conn_application_proto(c.conn, &app, &app_len);
    char alpn_note[64];
    if (app && app_len > 0 && app_len < sizeof(alpn_note)) {
        memcpy(alpn_note, app, app_len);
        alpn_note[app_len] = 0;
    } else {
        snprintf(alpn_note, sizeof(alpn_note), "unknown");
    }
    char out[128];
    snprintf(out, sizeof(out), "quic:ok;%s", alpn_note);
    MakoString result = mako_str_from_cstr(out);
    mako_quic_client_cleanup(&c);
    return result;
}

/* HTTP/3 request helper. method is "GET" or "POST"; body only for POST.
 * Returns "h3:<status>;<body>" or "h3:fail;<reason>".
 * Note: stock quiche-server returns 405 for non-GET (proves POST on the wire). */
static inline MakoString mako_quiche_h3_request(
    MakoString host, int64_t port, MakoString path,
    MakoString sni, int64_t verify_peer,
    const char *method, MakoString req_body
) {
    char hbuf[256], snibuf[256], pbuf[512], portbuf[32], reason[64];
    if (host.len < 1 || host.len >= sizeof(hbuf) || sni.len < 1
        || sni.len >= sizeof(snibuf) || path.len < 1 || path.len >= sizeof(pbuf)
        || path.data[0] != '/' || port <= 0 || port > 65535
        || !method || !method[0])
        return mako_str_from_cstr("h3:fail;bad-args");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(snibuf, sni.data, sni.len);
    snibuf[sni.len] = 0;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;
    snprintf(portbuf, sizeof(portbuf), "%lld", (long long)port);

    int is_post = (strcmp(method, "POST") == 0);

    MakoQuicClient c;
    if (mako_quic_client_connect(
            &c, hbuf, portbuf, snibuf, (int)verify_peer, reason, sizeof(reason)
        ) != 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "h3:fail;%s", reason[0] ? reason : "unknown");
        return mako_str_from_cstr(buf);
    }

    quiche_h3_config *h3cfg = quiche_h3_config_new();
    if (!h3cfg) {
        mako_quic_client_cleanup(&c);
        return mako_str_from_cstr("h3:fail;h3-config");
    }
    quiche_h3_conn *h3 = quiche_h3_conn_new_with_transport(c.conn, h3cfg);
    quiche_h3_config_free(h3cfg);
    if (!h3) {
        mako_quic_client_cleanup(&c);
        return mako_str_from_cstr("h3:fail;h3-conn");
    }

    quiche_h3_header headers[6];
    size_t nh = 0;
    headers[nh].name = (const uint8_t *)":method";
    headers[nh].name_len = 7;
    headers[nh].value = (const uint8_t *)method;
    headers[nh].value_len = strlen(method);
    nh++;
    headers[nh].name = (const uint8_t *)":scheme";
    headers[nh].name_len = 7;
    headers[nh].value = (const uint8_t *)"https";
    headers[nh].value_len = 5;
    nh++;
    headers[nh].name = (const uint8_t *)":authority";
    headers[nh].name_len = 10;
    headers[nh].value = (const uint8_t *)snibuf;
    headers[nh].value_len = strlen(snibuf);
    nh++;
    headers[nh].name = (const uint8_t *)":path";
    headers[nh].name_len = 5;
    headers[nh].value = (const uint8_t *)pbuf;
    headers[nh].value_len = strlen(pbuf);
    nh++;
    headers[nh].name = (const uint8_t *)"user-agent";
    headers[nh].name_len = 10;
    headers[nh].value = (const uint8_t *)"mako";
    headers[nh].value_len = 4;
    nh++;
    if (is_post) {
        headers[nh].name = (const uint8_t *)"content-type";
        headers[nh].name_len = 12;
        headers[nh].value = (const uint8_t *)"text/plain";
        headers[nh].value_len = 10;
        nh++;
    }

    /* POST: headers without FIN, then body with FIN. GET: headers with FIN. */
    int64_t stream_id = quiche_h3_send_request(
        h3, c.conn, headers, nh, is_post ? false : true
    );
    if (stream_id < 0) {
        quiche_h3_conn_free(h3);
        mako_quic_client_cleanup(&c);
        return mako_str_from_cstr("h3:fail;send-request");
    }

    if (is_post) {
        const uint8_t *bptr = (const uint8_t *)"";
        size_t blen = 0;
        if (req_body.data && req_body.len > 0) {
            bptr = (const uint8_t *)req_body.data;
            blen = req_body.len;
        }
        ssize_t wrote = quiche_h3_send_body(
            h3, c.conn, (uint64_t)stream_id, bptr, blen, true
        );
        if (wrote < 0) {
            quiche_h3_conn_free(h3);
            mako_quic_client_cleanup(&c);
            return mako_str_from_cstr("h3:fail;send-body");
        }
    }

    uint8_t out[MAKO_QUIC_MAX_DATAGRAM];
    if (mako_quic_flush(c.sock, c.conn, out, sizeof(out)) < 0) {
        quiche_h3_conn_free(h3);
        mako_quic_client_cleanup(&c);
        return mako_str_from_cstr("h3:fail;send-h3");
    }

    MakoH3Resp resp;
    memset(&resp, 0, sizeof(resp));
    resp.status = -1;
    uint8_t body_tmp[2048];

    for (int iter = 0; iter < 512 && !resp.finished; iter++) {
        if (quiche_conn_is_closed(c.conn)) break;
        if (mako_quic_client_pump_once(&c) < 0) break;

        while (1) {
            quiche_h3_event *ev = NULL;
            int64_t sid = quiche_h3_conn_poll(h3, c.conn, &ev);
            if (sid < 0) break;

            switch (quiche_h3_event_type(ev)) {
            case QUICHE_H3_EVENT_HEADERS:
                quiche_h3_event_for_each_header(ev, mako_h3_on_header, &resp);
                break;
            case QUICHE_H3_EVENT_DATA:
                for (;;) {
                    ssize_t len = quiche_h3_recv_body(
                        h3, c.conn, (uint64_t)sid, body_tmp, sizeof(body_tmp)
                    );
                    if (len <= 0) break;
                    size_t copy = (size_t)len;
                    if (resp.body_len + copy > sizeof(resp.body) - 1)
                        copy = sizeof(resp.body) - 1 - resp.body_len;
                    if (copy) {
                        memcpy(resp.body + resp.body_len, body_tmp, copy);
                        resp.body_len += copy;
                    }
                }
                break;
            case QUICHE_H3_EVENT_FINISHED:
                resp.finished = 1;
                quiche_conn_close(c.conn, true, 0, NULL, 0);
                break;
            case QUICHE_H3_EVENT_RESET:
                quiche_conn_close(c.conn, true, 0, NULL, 0);
                break;
            default:
                break;
            }
            quiche_h3_event_free(ev);
        }
        mako_quic_flush(c.sock, c.conn, out, sizeof(out));
    }

    MakoString result;
    if (resp.finished && resp.status > 0) {
        while (resp.body_len > 0
               && (resp.body[resp.body_len - 1] == '\n'
                   || resp.body[resp.body_len - 1] == '\r'))
            resp.body_len--;
        resp.body[resp.body_len] = 0;
        char fmt[MAKO_H3_BODY_CAP + 64];
        snprintf(
            fmt, sizeof(fmt), "h3:%d;%.*s",
            (int)resp.status, (int)resp.body_len, resp.body
        );
        result = mako_str_from_cstr(fmt);
    } else if (resp.status > 0) {
        result = mako_str_from_cstr("h3:fail;incomplete");
    } else {
        result = mako_str_from_cstr("h3:fail;timeout");
    }

    quiche_h3_conn_free(h3);
    mako_quic_client_cleanup(&c);
    return result;
}

/* HTTP/3 GET over a fresh quiche connection. */
static inline MakoString mako_quiche_h3_get(
    MakoString host, int64_t port, MakoString path,
    MakoString sni, int64_t verify_peer
) {
    MakoString empty = {NULL, 0};
    return mako_quiche_h3_request(
        host, port, path, sni, verify_peer, "GET", empty
    );
}

/* HTTP/3 POST with body via quiche_h3_send_request + quiche_h3_send_body.
 * Stock quiche-server returns 405 for POST (still proves method+body on wire). */
static inline MakoString mako_quiche_h3_post(
    MakoString host, int64_t port, MakoString path,
    MakoString body, MakoString sni, int64_t verify_peer
) {
    return mako_quiche_h3_request(
        host, port, path, sni, verify_peer, "POST", body
    );
}

/* Two overlapping H3 GETs on one quiche connection.
 * Returns "h3:<st1>;<b1>|<st2>;<b2>" on success. */
static inline MakoString mako_quiche_h3_get_two(
    MakoString host, int64_t port,
    MakoString path1, MakoString path2,
    MakoString sni, int64_t verify_peer
) {
    char hbuf[256], snibuf[256], p1buf[512], p2buf[512], portbuf[32], reason[64];
    if (host.len < 1 || host.len >= sizeof(hbuf) || sni.len < 1
        || sni.len >= sizeof(snibuf) || path1.len < 1 || path1.len >= sizeof(p1buf)
        || path2.len < 1 || path2.len >= sizeof(p2buf)
        || path1.data[0] != '/' || path2.data[0] != '/'
        || port <= 0 || port > 65535)
        return mako_str_from_cstr("h3:fail;bad-args");
    memcpy(hbuf, host.data, host.len);
    hbuf[host.len] = 0;
    memcpy(snibuf, sni.data, sni.len);
    snibuf[sni.len] = 0;
    memcpy(p1buf, path1.data, path1.len);
    p1buf[path1.len] = 0;
    memcpy(p2buf, path2.data, path2.len);
    p2buf[path2.len] = 0;
    snprintf(portbuf, sizeof(portbuf), "%lld", (long long)port);

    MakoQuicClient c;
    if (mako_quic_client_connect(
            &c, hbuf, portbuf, snibuf, (int)verify_peer, reason, sizeof(reason)
        ) != 0) {
        char buf[96];
        snprintf(buf, sizeof(buf), "h3:fail;%s", reason[0] ? reason : "unknown");
        return mako_str_from_cstr(buf);
    }

    quiche_h3_config *h3cfg = quiche_h3_config_new();
    if (!h3cfg) {
        mako_quic_client_cleanup(&c);
        return mako_str_from_cstr("h3:fail;h3-config");
    }
    quiche_h3_conn *h3 = quiche_h3_conn_new_with_transport(c.conn, h3cfg);
    quiche_h3_config_free(h3cfg);
    if (!h3) {
        mako_quic_client_cleanup(&c);
        return mako_str_from_cstr("h3:fail;h3-conn");
    }

    quiche_h3_header hdrs1[5], hdrs2[5];
    size_t n1 = mako_h3_fill_get_headers(hdrs1, snibuf, p1buf);
    size_t n2 = mako_h3_fill_get_headers(hdrs2, snibuf, p2buf);

    /* Submit both before driving I/O — overlapping streams. */
    int64_t sid1 = quiche_h3_send_request(h3, c.conn, hdrs1, n1, true);
    int64_t sid2 = quiche_h3_send_request(h3, c.conn, hdrs2, n2, true);
    if (sid1 < 0 || sid2 < 0) {
        quiche_h3_conn_free(h3);
        mako_quic_client_cleanup(&c);
        return mako_str_from_cstr("h3:fail;send-request");
    }

    MakoH3StreamResp streams[2];
    memset(streams, 0, sizeof(streams));
    streams[0].stream_id = sid1;
    streams[0].resp.status = -1;
    streams[1].stream_id = sid2;
    streams[1].resp.status = -1;

    uint8_t out[MAKO_QUIC_MAX_DATAGRAM];
    uint8_t body_tmp[2048];
    if (mako_quic_flush(c.sock, c.conn, out, sizeof(out)) < 0) {
        quiche_h3_conn_free(h3);
        mako_quic_client_cleanup(&c);
        return mako_str_from_cstr("h3:fail;send-h3");
    }

    for (int iter = 0; iter < 512 && !mako_h3_all_finished(streams, 2); iter++) {
        if (quiche_conn_is_closed(c.conn)) break;
        if (mako_quic_client_pump_once(&c) < 0) break;
        mako_h3_poll_into_streams(
            h3, c.conn, streams, 2, body_tmp, sizeof(body_tmp)
        );
        mako_quic_flush(c.sock, c.conn, out, sizeof(out));
    }

    if (mako_h3_all_finished(streams, 2)
        && streams[0].resp.status > 0 && streams[1].resp.status > 0) {
        mako_h3_trim_body(&streams[0].resp);
        mako_h3_trim_body(&streams[1].resp);
        char fmt[MAKO_H3_BODY_CAP * 2 + 80];
        snprintf(
            fmt, sizeof(fmt), "h3:%d;%.*s|%d;%.*s",
            (int)streams[0].resp.status,
            (int)streams[0].resp.body_len, streams[0].resp.body,
            (int)streams[1].resp.status,
            (int)streams[1].resp.body_len, streams[1].resp.body
        );
        quiche_conn_close(c.conn, true, 0, NULL, 0);
        mako_quic_flush(c.sock, c.conn, out, sizeof(out));
        MakoString result = mako_str_from_cstr(fmt);
        quiche_h3_conn_free(h3);
        mako_quic_client_cleanup(&c);
        return result;
    }

    quiche_h3_conn_free(h3);
    mako_quic_client_cleanup(&c);
    return mako_str_from_cstr("h3:fail;mux-incomplete");
}

/* Locate quiche-server binary for opt-in live tests. */
static inline int mako_quic_find_server(char *out, size_t cap) {
    const char *cands[] = {
        "runtime/third_party/quiche/bin/quiche-server",
        "./runtime/third_party/quiche/bin/quiche-server",
        NULL,
    };
    for (int i = 0; cands[i]; i++) {
        if (access(cands[i], X_OK) == 0) {
            snprintf(out, cap, "%s", cands[i]);
            return 1;
        }
    }
    if (getenv("MAKO_QUICHE_SERVER")
        && access(getenv("MAKO_QUICHE_SERVER"), X_OK) == 0) {
        snprintf(out, cap, "%s", getenv("MAKO_QUICHE_SERVER"));
        return 1;
    }
    return 0;
}

/* Fork+exec stock quiche-server. Returns child pid (>0) or 0 on failure. */
static inline int64_t mako_quiche_start_server(
    int64_t port,
    MakoString cert, MakoString key, MakoString root, MakoString name
) {
    char certb[512], keyb[512], rootb[512], nameb[256], listen[64], bin[512];
    if (port <= 0 || port > 65535) return 0;
    if (cert.len >= sizeof(certb) || key.len >= sizeof(keyb)
        || root.len >= sizeof(rootb) || name.len >= sizeof(nameb))
        return 0;
    if (!mako_quic_find_server(bin, sizeof(bin))) {
        fprintf(stderr, "mako quiche_start_server: quiche-server binary not found\n");
        return 0;
    }
    memcpy(certb, cert.data, cert.len);
    certb[cert.len] = 0;
    memcpy(keyb, key.data, key.len);
    keyb[key.len] = 0;
    memcpy(rootb, root.data, root.len);
    rootb[root.len] = 0;
    memcpy(nameb, name.data, name.len);
    nameb[name.len] = 0;
    snprintf(listen, sizeof(listen), "127.0.0.1:%lld", (long long)port);

    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        execl(
            bin, bin,
            "--listen", listen,
            "--cert", certb,
            "--key", keyb,
            "--root", rootb,
            "--name", nameb,
            "--no-retry",
            (char *)NULL
        );
        _exit(127);
    }
    for (int i = 0; i < 40; i++) {
        usleep(25000);
        int st = 0;
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) {
            fprintf(stderr, "mako quiche_start_server: child exited early\n");
            return 0;
        }
        if (i >= 4) break;
    }
    usleep(100000);
    return (int64_t)pid;
}

static inline int64_t mako_quiche_stop_server(int64_t pid) {
    if (pid <= 0) return 0;
    kill((pid_t)pid, SIGTERM);
    int st = 0;
    for (int i = 0; i < 40; i++) {
        pid_t r = waitpid((pid_t)pid, &st, WNOHANG);
        if (r == (pid_t)pid) return 1;
        usleep(25000);
    }
    kill((pid_t)pid, SIGKILL);
    waitpid((pid_t)pid, &st, 0);
    return 1;
}

/* ---- HTTP/3 / QUIC reverse-proxy server surface -------------------------
 * Real quiche-backed HTTP/3 ingress: UDP bind, multi-connection accept,
 * request queue, and response write. Poll-driven (no libev).
 *
 * Request body returned by h3_stream_read is a pseudo-HTTP/1.1 request:
 *   METHOD path HTTP/1.1\r\nHost: authority\r\n...\r\n\r\nbody
 * h3_stream_write accepts "STATUS\nbody" (status defaults to 200).
 * --------------------------------------------------------------------- */

#define MAKO_H3_RT_MAX 8
#define MAKO_H3_RT_CONNS 16
#define MAKO_H3_RT_READY 32
#define MAKO_H3_CID_LEN 16
#define MAKO_H3_REQ_CAP 8192
#define MAKO_H3_TOKEN_MAX (6 + sizeof(struct sockaddr_storage) + QUICHE_MAX_CONN_ID_LEN)

typedef struct {
    int live;
    uint8_t cid[MAKO_H3_CID_LEN];
    size_t cid_len;
    quiche_conn *conn;
    quiche_h3_conn *h3;
    struct sockaddr_storage peer;
    socklen_t peer_len;
} MakoH3Conn;

typedef struct {
    int live;
    int taken;
    int conn_idx;
    int64_t stream_id;
    char method[16];
    char path[1024];
    char authority[256];
    char cookie[512];
    char traceparent[128];
    char body[MAKO_H3_BODY_CAP];
    size_t body_len;
    int headers_done;
    int finished;
} MakoH3ReadyReq;

typedef struct {
    int live;
    int sock;
    int port;
    char cert[512];
    char key[512];
    quiche_config *qconfig;
    quiche_h3_config *h3config;
    struct sockaddr_storage local_addr;
    socklen_t local_len;
    MakoH3Conn conns[MAKO_H3_RT_CONNS];
    MakoH3ReadyReq ready[MAKO_H3_RT_READY];
    int ready_head;
    int ready_tail;
    int ready_count;
    int last_req_idx;
} MakoH3Server;

static MakoH3Server mako_h3_servers[MAKO_H3_RT_MAX];

static inline void mako_h3_mint_token(
    const uint8_t *dcid, size_t dcid_len,
    struct sockaddr_storage *addr, socklen_t addr_len,
    uint8_t *token, size_t *token_len
) {
    size_t n = 0;
    memcpy(token + n, "quiche", 6); n += 6;
    memcpy(token + n, addr, addr_len); n += addr_len;
    memcpy(token + n, dcid, dcid_len); n += dcid_len;
    *token_len = n;
}

static inline int mako_h3_validate_token(
    const uint8_t *token, size_t token_len,
    struct sockaddr_storage *addr, socklen_t addr_len,
    uint8_t *odcid, size_t *odcid_len
) {
    if (token_len < 6 || memcmp(token, "quiche", 6) != 0) return 0;
    token += 6; token_len -= 6;
    if (token_len < addr_len || memcmp(token, addr, addr_len) != 0) return 0;
    token += addr_len; token_len -= addr_len;
    if (token_len > *odcid_len) return 0;
    memcpy(odcid, token, token_len);
    *odcid_len = token_len;
    return 1;
}

static inline int mako_h3_gen_cid(uint8_t *cid, size_t len) {
    int rng = open("/dev/urandom", O_RDONLY);
    if (rng < 0) return 0;
    ssize_t n = read(rng, cid, len);
    close(rng);
    return n == (ssize_t)len;
}

static inline void mako_h3_flush_conn(MakoH3Server *s, MakoH3Conn *c) {
    if (!s || !c || !c->live || !c->conn || s->sock < 0) return;
    uint8_t out[MAKO_QUIC_MAX_DATAGRAM];
    while (1) {
        quiche_send_info si;
        memset(&si, 0, sizeof(si));
        ssize_t written = quiche_conn_send(c->conn, out, sizeof(out), &si);
        if (written == QUICHE_ERR_DONE) break;
        if (written < 0) break;
        (void)sendto(
            s->sock, out, (size_t)written, 0,
            (struct sockaddr *)&c->peer, c->peer_len
        );
    }
}

static inline int mako_h3_find_conn(MakoH3Server *s, const uint8_t *dcid, size_t dcid_len) {
    for (int i = 0; i < MAKO_H3_RT_CONNS; i++) {
        if (!s->conns[i].live) continue;
        if (s->conns[i].cid_len == dcid_len
            && memcmp(s->conns[i].cid, dcid, dcid_len) == 0)
            return i;
    }
    return -1;
}

static inline int mako_h3_alloc_conn(MakoH3Server *s) {
    for (int i = 0; i < MAKO_H3_RT_CONNS; i++) {
        if (!s->conns[i].live) return i;
    }
    return -1;
}

static inline void mako_h3_free_conn(MakoH3Conn *c) {
    if (!c) return;
    if (c->h3) quiche_h3_conn_free(c->h3);
    if (c->conn) quiche_conn_free(c->conn);
    memset(c, 0, sizeof(*c));
}

static inline int mako_h3_ready_push(MakoH3Server *s) {
    if (s->ready_count >= MAKO_H3_RT_READY) return -1;
    int idx = s->ready_tail;
    s->ready_tail = (s->ready_tail + 1) % MAKO_H3_RT_READY;
    s->ready_count++;
    memset(&s->ready[idx], 0, sizeof(s->ready[idx]));
    s->ready[idx].live = 1;
    return idx;
}

static inline int mako_h3_find_ready_stream(MakoH3Server *s, int conn_idx, int64_t sid) {
    for (int i = 0; i < MAKO_H3_RT_READY; i++) {
        if (!s->ready[i].live || s->ready[i].taken) continue;
        if (s->ready[i].conn_idx == conn_idx && s->ready[i].stream_id == sid)
            return i;
    }
    return -1;
}

static inline int mako_h3_on_req_header(
    uint8_t *name, size_t name_len, uint8_t *value, size_t value_len, void *argp
) {
    MakoH3ReadyReq *r = (MakoH3ReadyReq *)argp;
    if (!r || !name || !value) return 0;
    if (name_len == 7 && memcmp(name, ":method", 7) == 0) {
        size_t n = value_len < sizeof(r->method) - 1 ? value_len : sizeof(r->method) - 1;
        memcpy(r->method, value, n);
        r->method[n] = 0;
    } else if (name_len == 5 && memcmp(name, ":path", 5) == 0) {
        size_t n = value_len < sizeof(r->path) - 1 ? value_len : sizeof(r->path) - 1;
        memcpy(r->path, value, n);
        r->path[n] = 0;
    } else if (name_len == 10 && memcmp(name, ":authority", 10) == 0) {
        size_t n = value_len < sizeof(r->authority) - 1 ? value_len : sizeof(r->authority) - 1;
        memcpy(r->authority, value, n);
        r->authority[n] = 0;
    } else if (name_len == 6 && memcmp(name, "cookie", 6) == 0) {
        size_t n = value_len < sizeof(r->cookie) - 1 ? value_len : sizeof(r->cookie) - 1;
        memcpy(r->cookie, value, n);
        r->cookie[n] = 0;
    } else if (name_len == 11 && memcmp(name, "traceparent", 11) == 0) {
        size_t n = value_len < sizeof(r->traceparent) - 1 ? value_len : sizeof(r->traceparent) - 1;
        memcpy(r->traceparent, value, n);
        r->traceparent[n] = 0;
    }
    return 0;
}

static inline void mako_h3_poll_h3_events(MakoH3Server *s, int conn_idx) {
    MakoH3Conn *c = &s->conns[conn_idx];
    if (!c->live || !c->conn || !c->h3) return;
    while (1) {
        quiche_h3_event *ev = NULL;
        int64_t sid = quiche_h3_conn_poll(c->h3, c->conn, &ev);
        if (sid < 0) break;
        switch (quiche_h3_event_type(ev)) {
        case QUICHE_H3_EVENT_HEADERS: {
            int ri = mako_h3_find_ready_stream(s, conn_idx, sid);
            if (ri < 0) {
                ri = mako_h3_ready_push(s);
                if (ri < 0) break;
                s->ready[ri].conn_idx = conn_idx;
                s->ready[ri].stream_id = sid;
            }
            quiche_h3_event_for_each_header(ev, mako_h3_on_req_header, &s->ready[ri]);
            s->ready[ri].headers_done = 1;
            if (!quiche_h3_event_headers_has_more_frames(ev)) {
                s->ready[ri].finished = 1;
            }
            break;
        }
        case QUICHE_H3_EVENT_DATA: {
            int ri = mako_h3_find_ready_stream(s, conn_idx, sid);
            if (ri < 0) break;
            uint8_t tmp[1024];
            for (;;) {
                ssize_t n = quiche_h3_recv_body(
                    c->h3, c->conn, (uint64_t)sid, tmp, sizeof(tmp)
                );
                if (n <= 0) break;
                size_t room = MAKO_H3_BODY_CAP - 1 - s->ready[ri].body_len;
                size_t take = (size_t)n < room ? (size_t)n : room;
                if (take) {
                    memcpy(s->ready[ri].body + s->ready[ri].body_len, tmp, take);
                    s->ready[ri].body_len += take;
                    s->ready[ri].body[s->ready[ri].body_len] = 0;
                }
            }
            break;
        }
        case QUICHE_H3_EVENT_FINISHED: {
            int ri = mako_h3_find_ready_stream(s, conn_idx, sid);
            if (ri >= 0) s->ready[ri].finished = 1;
            break;
        }
        case QUICHE_H3_EVENT_RESET: {
            int ri = mako_h3_find_ready_stream(s, conn_idx, sid);
            if (ri >= 0) {
                s->ready[ri].finished = 1;
                s->ready[ri].taken = 1;
                s->ready[ri].live = 0;
            }
            break;
        }
        default:
            break;
        }
        quiche_h3_event_free(ev);
    }
}

static inline void mako_h3_process_one_packet(
    MakoH3Server *s, uint8_t *buf, size_t n,
    struct sockaddr_storage *peer, socklen_t peer_len
) {
    uint8_t type = 0;
    uint32_t version = 0;
    uint8_t scid[QUICHE_MAX_CONN_ID_LEN];
    size_t scid_len = sizeof(scid);
    uint8_t dcid[QUICHE_MAX_CONN_ID_LEN];
    size_t dcid_len = sizeof(dcid);
    uint8_t token[MAKO_H3_TOKEN_MAX];
    size_t token_len = sizeof(token);
    int rc = quiche_header_info(
        buf, n, MAKO_H3_CID_LEN, &version, &type,
        scid, &scid_len, dcid, &dcid_len, token, &token_len
    );
    if (rc < 0) return;

    int ci = mako_h3_find_conn(s, dcid, dcid_len);
    uint8_t out[MAKO_QUIC_MAX_DATAGRAM];

    if (ci < 0) {
        if (!quiche_version_is_supported(version)) {
            ssize_t written = quiche_negotiate_version(
                scid, scid_len, dcid, dcid_len, out, sizeof(out)
            );
            if (written > 0) {
                (void)sendto(
                    s->sock, out, (size_t)written, 0,
                    (struct sockaddr *)peer, peer_len
                );
            }
            return;
        }
        if (token_len == 0) {
            uint8_t new_cid[MAKO_H3_CID_LEN];
            if (!mako_h3_gen_cid(new_cid, MAKO_H3_CID_LEN)) return;
            size_t tlen = sizeof(token);
            mako_h3_mint_token(dcid, dcid_len, peer, peer_len, token, &tlen);
            ssize_t written = quiche_retry(
                scid, scid_len, dcid, dcid_len,
                new_cid, MAKO_H3_CID_LEN, token, tlen,
                version, out, sizeof(out)
            );
            if (written > 0) {
                (void)sendto(
                    s->sock, out, (size_t)written, 0,
                    (struct sockaddr *)peer, peer_len
                );
            }
            return;
        }
        uint8_t odcid[QUICHE_MAX_CONN_ID_LEN];
        size_t odcid_len = sizeof(odcid);
        if (!mako_h3_validate_token(token, token_len, peer, peer_len, odcid, &odcid_len))
            return;
        ci = mako_h3_alloc_conn(s);
        if (ci < 0) return;
        MakoH3Conn *nc = &s->conns[ci];
        memset(nc, 0, sizeof(*nc));
        size_t cid_copy = dcid_len < MAKO_H3_CID_LEN ? dcid_len : MAKO_H3_CID_LEN;
        memcpy(nc->cid, dcid, cid_copy);
        nc->cid_len = cid_copy;
        nc->conn = quiche_accept(
            nc->cid, nc->cid_len, odcid, odcid_len,
            (struct sockaddr *)&s->local_addr, s->local_len,
            (struct sockaddr *)peer, peer_len, s->qconfig
        );
        if (!nc->conn) return;
        memcpy(&nc->peer, peer, peer_len);
        nc->peer_len = peer_len;
        nc->live = 1;
    }

    MakoH3Conn *c = &s->conns[ci];
    quiche_recv_info ri = {
        (struct sockaddr *)peer, peer_len,
        (struct sockaddr *)&s->local_addr, s->local_len,
    };
    ssize_t done = quiche_conn_recv(c->conn, buf, n, &ri);
    if (done < 0) return;

    if (quiche_conn_is_established(c->conn)) {
        if (!c->h3) {
            c->h3 = quiche_h3_conn_new_with_transport(c->conn, s->h3config);
        }
        if (c->h3) mako_h3_poll_h3_events(s, ci);
    }
    mako_h3_flush_conn(s, c);

    if (quiche_conn_is_closed(c->conn)) {
        mako_h3_free_conn(c);
    }
}

/* Drain all pending datagrams and drive connections. */
static inline void mako_h3_server_drive(int64_t handle) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return;
    MakoH3Server *s = &mako_h3_servers[handle];
    if (!s->live || s->sock < 0 || !s->qconfig) return;
    uint8_t buf[65535];
    for (int iter = 0; iter < 64; iter++) {
        struct sockaddr_storage peer;
        socklen_t peer_len = sizeof(peer);
        ssize_t n = recvfrom(
            s->sock, buf, sizeof(buf), 0,
            (struct sockaddr *)&peer, &peer_len
        );
        if (n < 0) break;
        if (n == 0) continue;
        mako_h3_process_one_packet(s, buf, (size_t)n, &peer, peer_len);
    }
    for (int i = 0; i < MAKO_H3_RT_CONNS; i++) {
        if (!s->conns[i].live || !s->conns[i].conn) continue;
        quiche_conn_on_timeout(s->conns[i].conn);
        if (s->conns[i].h3) mako_h3_poll_h3_events(s, i);
        mako_h3_flush_conn(s, &s->conns[i]);
        if (quiche_conn_is_closed(s->conns[i].conn)) {
            mako_h3_free_conn(&s->conns[i]);
        }
    }
}

static inline int64_t mako_h3_server_new(MakoString cert, MakoString key) {
    if (!cert.data || !key.data || cert.len >= 512 || key.len >= 512) return -1;
    for (int i = 0; i < MAKO_H3_RT_MAX; i++) {
        if (!mako_h3_servers[i].live) {
            MakoH3Server *s = &mako_h3_servers[i];
            memset(s, 0, sizeof(*s));
            memcpy(s->cert, cert.data, cert.len);
            s->cert[cert.len] = 0;
            memcpy(s->key, key.data, key.len);
            s->key[key.len] = 0;
            s->sock = -1;
            s->last_req_idx = -1;
            s->live = 1;
            return (int64_t)i;
        }
    }
    return -1;
}

static inline int64_t mako_h3_server_bind(int64_t handle, MakoString host, int64_t port) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return 0;
    MakoH3Server *s = &mako_h3_servers[handle];
    if (!s->live || port <= 0 || port > 65535) return 0;
    if (s->sock >= 0) return 1;

    s->qconfig = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (!s->qconfig) return 0;
    if (quiche_config_load_cert_chain_from_pem_file(s->qconfig, s->cert) < 0) {
        quiche_config_free(s->qconfig); s->qconfig = NULL; return 0;
    }
    if (quiche_config_load_priv_key_from_pem_file(s->qconfig, s->key) < 0) {
        quiche_config_free(s->qconfig); s->qconfig = NULL; return 0;
    }
    quiche_config_set_application_protos(
        s->qconfig,
        (uint8_t *)QUICHE_H3_APPLICATION_PROTOCOL,
        sizeof(QUICHE_H3_APPLICATION_PROTOCOL) - 1
    );
    quiche_config_set_max_idle_timeout(s->qconfig, 30000);
    quiche_config_set_max_recv_udp_payload_size(s->qconfig, MAKO_QUIC_MAX_DATAGRAM);
    quiche_config_set_max_send_udp_payload_size(s->qconfig, MAKO_QUIC_MAX_DATAGRAM);
    quiche_config_set_initial_max_data(s->qconfig, 10000000);
    quiche_config_set_initial_max_stream_data_bidi_local(s->qconfig, 1000000);
    quiche_config_set_initial_max_stream_data_bidi_remote(s->qconfig, 1000000);
    quiche_config_set_initial_max_stream_data_uni(s->qconfig, 1000000);
    quiche_config_set_initial_max_streams_bidi(s->qconfig, 100);
    quiche_config_set_initial_max_streams_uni(s->qconfig, 100);
    quiche_config_set_disable_active_migration(s->qconfig, true);
    quiche_config_verify_peer(s->qconfig, false);

    s->h3config = quiche_h3_config_new();
    if (!s->h3config) {
        quiche_config_free(s->qconfig); s->qconfig = NULL; return 0;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        quiche_h3_config_free(s->h3config); s->h3config = NULL;
        quiche_config_free(s->qconfig); s->qconfig = NULL;
        return 0;
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#if defined(SO_REUSEPORT)
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (host.data && host.len && !(host.len == 1 && host.data[0] == '*')) {
        char hbuf[256];
        if (host.len >= sizeof(hbuf)) {
            close(fd);
            quiche_h3_config_free(s->h3config); s->h3config = NULL;
            quiche_config_free(s->qconfig); s->qconfig = NULL;
            return 0;
        }
        memcpy(hbuf, host.data, host.len);
        hbuf[host.len] = 0;
        if (inet_pton(AF_INET, hbuf, &addr.sin_addr) != 1) {
            close(fd);
            quiche_h3_config_free(s->h3config); s->h3config = NULL;
            quiche_config_free(s->qconfig); s->qconfig = NULL;
            return 0;
        }
    } else {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        quiche_h3_config_free(s->h3config); s->h3config = NULL;
        quiche_config_free(s->qconfig); s->qconfig = NULL;
        return 0;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    s->sock = fd;
    s->port = (int)port;
    s->local_len = sizeof(s->local_addr);
    if (getsockname(fd, (struct sockaddr *)&s->local_addr, &s->local_len) != 0) {
        memcpy(&s->local_addr, &addr, sizeof(addr));
        s->local_len = sizeof(addr);
    }
    return 1;
}

static inline int64_t mako_h3_server_fd(int64_t handle) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return -1;
    if (!mako_h3_servers[handle].live) return -1;
    return (int64_t)mako_h3_servers[handle].sock;
}

/* Poll + drive. Returns 1 if ready requests exist, 0 idle/timeout, -1 error. */
static inline int64_t mako_h3_server_poll(int64_t handle, int64_t timeout_ms) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return -1;
    MakoH3Server *s = &mako_h3_servers[handle];
    if (!s->live || s->sock < 0) return -1;
    struct pollfd pfd;
    pfd.fd = s->sock;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, (int)timeout_ms);
    if (pr < 0) return -1;
    if (pr > 0 && (pfd.revents & POLLIN)) {
        mako_h3_server_drive(handle);
    } else {
        /* Still run timeouts even on idle poll. */
        mako_h3_server_drive(handle);
    }
    return s->ready_count > 0 ? 1 : (pr == 0 ? 0 : 1);
}

/* Next finished request stream id, or -1. */
static inline int64_t mako_h3_accept_stream(int64_t handle) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return -1;
    MakoH3Server *s = &mako_h3_servers[handle];
    if (!s->live || s->sock < 0) return -1;
    mako_h3_server_drive(handle);
    for (int n = 0; n < MAKO_H3_RT_READY; n++) {
        int idx = (s->ready_head + n) % MAKO_H3_RT_READY;
        MakoH3ReadyReq *r = &s->ready[idx];
        if (!r->live || r->taken) continue;
        if (r->headers_done == 0) continue;
        /* Accept once headers are in; body may still be partial for POST. */
        r->taken = 1;
        s->last_req_idx = idx;
        if (s->ready_count > 0) s->ready_count--;
        s->ready_head = (idx + 1) % MAKO_H3_RT_READY;
        return r->stream_id;
    }
    return -1;
}

/* Pseudo-HTTP/1.1 request for the last accepted stream. */
static inline MakoString mako_h3_stream_read(int64_t handle, int64_t stream_id) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return mako_str_from_cstr("");
    MakoH3Server *s = &mako_h3_servers[handle];
    if (!s->live) return mako_str_from_cstr("");
    int idx = s->last_req_idx;
    if (idx < 0 || idx >= MAKO_H3_RT_READY) return mako_str_from_cstr("");
    MakoH3ReadyReq *r = &s->ready[idx];
    if (!r->live || r->stream_id != stream_id) {
        /* Fall back to search. */
        idx = -1;
        for (int i = 0; i < MAKO_H3_RT_READY; i++) {
            if (s->ready[i].live && s->ready[i].stream_id == stream_id) {
                idx = i;
                r = &s->ready[i];
                break;
            }
        }
        if (idx < 0) return mako_str_from_cstr("");
    }
    char buf[MAKO_H3_REQ_CAP];
    const char *method = r->method[0] ? r->method : "GET";
    const char *path = r->path[0] ? r->path : "/";
    const char *auth = r->authority[0] ? r->authority : "localhost";
    int n = snprintf(
        buf, sizeof(buf),
        "%s %s HTTP/1.1\r\nHost: %s\r\n",
        method, path, auth
    );
    if (n < 0 || n >= (int)sizeof(buf)) return mako_str_from_cstr("");
    if (r->cookie[0]) {
        int m = snprintf(buf + n, sizeof(buf) - (size_t)n, "Cookie: %s\r\n", r->cookie);
        if (m > 0) n += m;
    }
    if (r->traceparent[0]) {
        int m = snprintf(
            buf + n, sizeof(buf) - (size_t)n, "traceparent: %s\r\n", r->traceparent
        );
        if (m > 0) n += m;
    }
    if (r->body_len > 0) {
        int m = snprintf(
            buf + n, sizeof(buf) - (size_t)n,
            "Content-Length: %zu\r\n\r\n", r->body_len
        );
        if (m > 0) n += m;
        size_t room = sizeof(buf) - 1 - (size_t)n;
        size_t take = r->body_len < room ? r->body_len : room;
        memcpy(buf + n, r->body, take);
        n += (int)take;
    } else {
        int m = snprintf(buf + n, sizeof(buf) - (size_t)n, "\r\n");
        if (m > 0) n += m;
    }
    char *d = (char *)malloc((size_t)n + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, buf, (size_t)n);
    d[n] = 0;
    return (MakoString){d, (size_t)n};
}

/* Write response: optional "STATUS\n" prefix, then body. */
static inline int64_t mako_h3_stream_write(int64_t handle, int64_t stream_id, MakoString data) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return -1;
    MakoH3Server *s = &mako_h3_servers[handle];
    if (!s->live || s->sock < 0) return -1;

    int conn_idx = -1;
    int req_idx = -1;
    for (int i = 0; i < MAKO_H3_RT_READY; i++) {
        if (s->ready[i].live && s->ready[i].stream_id == stream_id) {
            conn_idx = s->ready[i].conn_idx;
            req_idx = i;
            break;
        }
    }
    if (conn_idx < 0 || conn_idx >= MAKO_H3_RT_CONNS) return -1;
    MakoH3Conn *c = &s->conns[conn_idx];
    if (!c->live || !c->conn || !c->h3) return -1;

    int status = 200;
    const char *body = data.data ? data.data : "";
    size_t body_len = data.data ? data.len : 0;
    if (data.data && data.len >= 2) {
        /* Parse leading "NNN\n" status line. */
        if (data.data[0] >= '1' && data.data[0] <= '5'
            && data.data[1] >= '0' && data.data[1] <= '9') {
            int st = 0;
            size_t i = 0;
            while (i < data.len && data.data[i] >= '0' && data.data[i] <= '9') {
                st = st * 10 + (data.data[i] - '0');
                i++;
            }
            if (i < data.len && (data.data[i] == '\n' || data.data[i] == '\r') && st >= 100 && st <= 599) {
                status = st;
                while (i < data.len && (data.data[i] == '\n' || data.data[i] == '\r')) i++;
                body = data.data + i;
                body_len = data.len - i;
            }
        }
    }

    char status_buf[8];
    snprintf(status_buf, sizeof(status_buf), "%d", status);
    char cl_buf[32];
    snprintf(cl_buf, sizeof(cl_buf), "%zu", body_len);

    quiche_h3_header headers[4];
    size_t nh = 0;
    headers[nh].name = (const uint8_t *)":status";
    headers[nh].name_len = 7;
    headers[nh].value = (const uint8_t *)status_buf;
    headers[nh].value_len = strlen(status_buf);
    nh++;
    headers[nh].name = (const uint8_t *)"server";
    headers[nh].name_len = 6;
    headers[nh].value = (const uint8_t *)"leba";
    headers[nh].value_len = 4;
    nh++;
    headers[nh].name = (const uint8_t *)"content-length";
    headers[nh].name_len = 14;
    headers[nh].value = (const uint8_t *)cl_buf;
    headers[nh].value_len = strlen(cl_buf);
    nh++;

    int fin = body_len == 0 ? 1 : 0;
    if (quiche_h3_send_response(c->h3, c->conn, (uint64_t)stream_id, headers, nh, fin) < 0)
        return -1;
    if (body_len > 0) {
        ssize_t wr = quiche_h3_send_body(
            c->h3, c->conn, (uint64_t)stream_id,
            (uint8_t *)body, body_len, true
        );
        if (wr < 0) return -1;
    }
    mako_h3_flush_conn(s, c);
    if (req_idx >= 0) {
        s->ready[req_idx].live = 0;
        s->ready[req_idx].taken = 1;
    }
    return (int64_t)(body_len > 0 ? body_len : 1);
}

static inline int64_t mako_h3_server_close(int64_t handle) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return 0;
    MakoH3Server *s = &mako_h3_servers[handle];
    if (!s->live) return 0;
    for (int i = 0; i < MAKO_H3_RT_CONNS; i++) {
        if (s->conns[i].live) mako_h3_free_conn(&s->conns[i]);
    }
    if (s->h3config) quiche_h3_config_free(s->h3config);
    if (s->qconfig) quiche_config_free(s->qconfig);
    if (s->sock >= 0) close(s->sock);
    memset(s, 0, sizeof(*s));
    s->sock = -1;
    return 1;
}

static inline int64_t mako_h3_server_available(void) {
    return 1;
}

#else /* !MAKO_HAS_QUICHE */

static inline int64_t mako_quiche_available(void) {
    return 0;
}

static inline MakoString mako_quiche_version(void) {
    return mako_str_from_cstr("");
}

static inline MakoString mako_quiche_handshake(
    MakoString host, int64_t port, MakoString sni, int64_t verify_peer
) {
    (void)host;
    (void)port;
    (void)sni;
    (void)verify_peer;
    fprintf(stderr, "mako quiche_handshake: quiche not linked (need MAKO_HAS_QUICHE)\n");
    return mako_str_from_cstr("quic:fail;nolink");
}

static inline MakoString mako_quiche_h3_get(
    MakoString host, int64_t port, MakoString path,
    MakoString sni, int64_t verify_peer
) {
    (void)host;
    (void)port;
    (void)path;
    (void)sni;
    (void)verify_peer;
    fprintf(stderr, "mako quiche_h3_get: quiche not linked (need MAKO_HAS_QUICHE)\n");
    return mako_str_from_cstr("h3:fail;nolink");
}

static inline MakoString mako_quiche_h3_post(
    MakoString host, int64_t port, MakoString path,
    MakoString body, MakoString sni, int64_t verify_peer
) {
    (void)host;
    (void)port;
    (void)path;
    (void)body;
    (void)sni;
    (void)verify_peer;
    fprintf(stderr, "mako quiche_h3_post: quiche not linked (need MAKO_HAS_QUICHE)\n");
    return mako_str_from_cstr("h3:fail;nolink");
}

static inline MakoString mako_quiche_h3_get_two(
    MakoString host, int64_t port,
    MakoString path1, MakoString path2,
    MakoString sni, int64_t verify_peer
) {
    (void)host;
    (void)port;
    (void)path1;
    (void)path2;
    (void)sni;
    (void)verify_peer;
    fprintf(stderr, "mako quiche_h3_get_two: quiche not linked (need MAKO_HAS_QUICHE)\n");
    return mako_str_from_cstr("h3:fail;nolink");
}

static inline int64_t mako_quiche_start_server(
    int64_t port,
    MakoString cert, MakoString key, MakoString root, MakoString name
) {
    (void)port;
    (void)cert;
    (void)key;
    (void)root;
    (void)name;
    return 0;
}

static inline int64_t mako_quiche_stop_server(int64_t pid) {
    (void)pid;
    return 0;
}

/* H3 server surface stubs when quiche is not linked. */
static inline int64_t mako_h3_server_new(MakoString cert, MakoString key) {
    (void)cert; (void)key; return -1;
}
static inline int64_t mako_h3_server_bind(int64_t h, MakoString host, int64_t port) {
    (void)h; (void)host; (void)port; return 0;
}
static inline int64_t mako_h3_server_fd(int64_t h) { (void)h; return -1; }
static inline int64_t mako_h3_server_poll(int64_t h, int64_t ms) {
    (void)h; (void)ms; return -1;
}
static inline int64_t mako_h3_accept_stream(int64_t h) { (void)h; return -1; }
static inline MakoString mako_h3_stream_read(int64_t h, int64_t sid) {
    (void)h; (void)sid; return mako_str_from_cstr("");
}
static inline int64_t mako_h3_stream_write(int64_t h, int64_t sid, MakoString data) {
    (void)h; (void)sid; (void)data; return -1;
}
static inline int64_t mako_h3_server_close(int64_t h) { (void)h; return 0; }
static inline int64_t mako_h3_server_available(void) { return 0; }

#endif /* MAKO_HAS_QUICHE */

#endif /* MAKO_QUICHE_H */
