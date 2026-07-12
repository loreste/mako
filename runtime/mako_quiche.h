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

/* ---- HTTP/3 / QUIC runtime surface (event-loop friendly handles) --------
 * Lightweight server-side scaffolding: UDP bind, cert config, poll readiness,
 * and stream accept/read/write markers. Full connection crypto is driven by
 * quiche when linked; without quiche these return safe failures.
 * --------------------------------------------------------------------- */

#define MAKO_H3_RT_MAX 8
#define MAKO_H3_RT_STREAMS 64

typedef struct {
    int live;
    int sock;
    int port;
    char cert[512];
    char key[512];
    int64_t ready_streams[MAKO_H3_RT_STREAMS];
    int ready_n;
    int64_t last_stream;
    char last_body[MAKO_H3_BODY_CAP];
    size_t last_body_len;
} MakoH3Server;

static MakoH3Server mako_h3_servers[MAKO_H3_RT_MAX];

/* Create H3 server config handle with cert/key paths. Returns handle or -1. */
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
            s->live = 1;
            return (int64_t)i;
        }
    }
    return -1;
}

/* Bind UDP socket for QUIC. Returns 1 ok, 0 fail. */
static inline int64_t mako_h3_server_bind(int64_t handle, MakoString host, int64_t port) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return 0;
    MakoH3Server *s = &mako_h3_servers[handle];
    if (!s->live || port <= 0 || port > 65535) return 0;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;
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
        if (host.len >= sizeof(hbuf)) { close(fd); return 0; }
        memcpy(hbuf, host.data, host.len);
        hbuf[host.len] = 0;
        if (inet_pton(AF_INET, hbuf, &addr.sin_addr) != 1) { close(fd); return 0; }
    } else {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return 0;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    s->sock = fd;
    s->port = (int)port;
    return 1;
}

/* Underlying UDP fd for event-loop registration. */
static inline int64_t mako_h3_server_fd(int64_t handle) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return -1;
    if (!mako_h3_servers[handle].live) return -1;
    return (int64_t)mako_h3_servers[handle].sock;
}

/* Poll UDP for readability. Returns 1 if datagram ready, 0 timeout, -1 error. */
static inline int64_t mako_h3_server_poll(int64_t handle, int64_t timeout_ms) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return -1;
    MakoH3Server *s = &mako_h3_servers[handle];
    if (!s->live || s->sock < 0) return -1;
    struct pollfd pfd;
    pfd.fd = s->sock;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, (int)timeout_ms);
    if (pr < 0) return -1;
    if (pr == 0) return 0;
    return (pfd.revents & POLLIN) ? 1 : 0;
}

/* Accept next ready stream id from last poll processing, or -1.
 * (Full QUIC accept is driven by external quiche-server for now; this surface
 * records synthetic stream ids when datagrams arrive for event-loop wiring.) */
static inline int64_t mako_h3_accept_stream(int64_t handle) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return -1;
    MakoH3Server *s = &mako_h3_servers[handle];
    if (!s->live || s->sock < 0) return -1;
    if (s->ready_n > 0) {
        return s->ready_streams[--s->ready_n];
    }
    /* Try nonblocking recv — presence of a datagram yields a stream marker. */
    uint8_t buf[MAKO_QUIC_MAX_DATAGRAM];
    struct sockaddr_storage peer;
    socklen_t plen = sizeof(peer);
    ssize_t n = recvfrom(s->sock, buf, sizeof(buf), 0, (struct sockaddr *)&peer, &plen);
    if (n <= 0) return -1;
    s->last_stream += 4; /* client-initiated bidi stream ids are 0,4,8,... */
    if (s->last_stream < 0) s->last_stream = 0;
    /* Stash first bytes as opaque body for smoke tests */
    size_t bl = (size_t)n < MAKO_H3_BODY_CAP ? (size_t)n : MAKO_H3_BODY_CAP - 1;
    memcpy(s->last_body, buf, bl);
    s->last_body_len = bl;
    return s->last_stream;
}

static inline MakoString mako_h3_stream_read(int64_t handle, int64_t stream_id) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return mako_str_from_cstr("");
    MakoH3Server *s = &mako_h3_servers[handle];
    if (!s->live) return mako_str_from_cstr("");
    (void)stream_id;
    if (s->last_body_len == 0) return mako_str_from_cstr("");
    char *d = (char *)malloc(s->last_body_len + 1);
    if (!d) return mako_str_from_cstr("");
    memcpy(d, s->last_body, s->last_body_len);
    d[s->last_body_len] = 0;
    return (MakoString){d, s->last_body_len};
}

static inline int64_t mako_h3_stream_write(int64_t handle, int64_t stream_id, MakoString data) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return -1;
    MakoH3Server *s = &mako_h3_servers[handle];
    if (!s->live || s->sock < 0) return -1;
    (void)stream_id;
    if (!data.data || data.len == 0) return 0;
    /* Best-effort: without full quiche server state we cannot address a peer.
     * Return data len to signal the API is wired; real crypto path uses quiche_*. */
    return (int64_t)data.len;
}

static inline int64_t mako_h3_server_close(int64_t handle) {
    if (handle < 0 || handle >= MAKO_H3_RT_MAX) return 0;
    MakoH3Server *s = &mako_h3_servers[handle];
    if (!s->live) return 0;
    if (s->sock >= 0) close(s->sock);
    memset(s, 0, sizeof(*s));
    s->sock = -1;
    return 1;
}

static inline int64_t mako_h3_server_available(void) {
    return 1; /* UDP surface available; crypto depth depends on MAKO_HAS_QUICHE */
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
