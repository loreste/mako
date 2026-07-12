# 8. Networking & HTTP

Mako provides a systems-level HTTP stack: synchronous, one-request-at-a-time
per connection, with no colored async. You scale concurrency by running handlers
inside crew blocks. This chapter covers TCP, HTTP/1.1, HTTPS, HTTP/2, WebSockets,
REST APIs, and request routing patterns.

---

## TCP Fundamentals

At the lowest level, Mako provides raw TCP socket operations:

```mko
fn main() {
    // Server side
    let fd = tcp_listen(18082)
    let client = tcp_accept(fd)
    let _ = tcp_write(client, "hello from server\n")
    let _ = tcp_close(client)
    let _ = tcp_close(fd)
}
```

```mko
fn main() {
    // Client side
    let peer = tcp_connect("127.0.0.1", 18082)
    let data = tcp_read(peer)
    print(data)
    let _ = tcp_close(peer)
}
```

TCP operations block the calling thread. Use crew blocks to handle multiple
connections concurrently.

---

## HTTP/1.1 Server

The HTTP server API is synchronous and explicit. You bind a port, accept
connections in a loop, inspect the request, send a response, and close.

### Minimal server

```mko
fn main() {
    let fd = http_bind(18100)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("listening on :18100")

    let c = http_accept(fd)
    if c >= 0 {
        let _ = http_respond(c, 200, "hello from mako\n")
        let _ = http_close(c)
    }
    let _ = http_close_listener(fd)
}
```

### Core API functions

| Function | Purpose |
|----------|---------|
| `http_bind(port)` | Bind and listen on a TCP port. Returns listener fd (< 0 on error). |
| `http_accept(fd)` | Accept one connection, parse the HTTP request. Returns connection handle. |
| `http_method(c)` | Get the request method (GET, POST, PUT, DELETE, etc.). |
| `http_path(c)` | Get the request path (e.g., "/users/42"). |
| `http_body(c)` | Get the request body as a string. |
| `http_header(c, name)` | Get a specific request header value. |
| `http_respond(c, status, body)` | Send response with text/plain content type. |
| `http_respond_ct(c, status, content_type, body)` | Send response with explicit content type. |
| `http_respond_json(c, status, json)` | Send response with application/json content type. |
| `http_close(c)` | Close the connection (frees the slot). |
| `http_close_listener(fd)` | Close the listening socket. |

### Multi-request server loop

```mko
fn main() {
    let fd = http_bind(18100)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("http_server on :18100")

    let mut n = 0
    while n < 50 {
        let c = http_accept(fd)
        if c < 0 {
            // accept failed, skip
        } else {
            let p = http_path(c)
            if str_eq(p, "/health") {
                let _ = http_respond_ct(
                    c,
                    200,
                    "application/json",
                    "{\"ok\":true}\n"
                )
            } else {
                if str_eq(p, "/") {
                    let _ = http_respond(c, 200, "hello from mako\n")
                } else {
                    let _ = http_respond(c, 404, "not found\n")
                }
            }
            let _ = http_close(c)
            n = n + 1
        }
    }
    let _ = http_close_listener(fd)
    print("server done")
}
```

---

## Request Inspection

### Method

```mko
let c = http_accept(fd)
let method = http_method(c)
if str_eq(method, "POST") {
    // handle POST
} else {
    if str_eq(method, "GET") {
        // handle GET
    }
}
```

### Path

```mko
let path = http_path(c)
// path is the raw URI path, e.g. "/users/42"
```

### Body

```mko
let body = http_body(c)
// body contains the raw request body (up to Content-Length or 1MB max)
```

### Headers

```mko
let host = http_header(c, "Host")
let ua = http_header(c, "User-Agent")
let ct = http_header(c, "Content-Type")
print(host)
print(ua)
print(ct)
```

Header lookup is case-insensitive. The runtime validates header names and values,
rejecting CR/LF/NUL to prevent header injection attacks.

---

## Response Functions

### Plain text response

```mko
let _ = http_respond(c, 200, "OK\n")
```

### Response with content type

```mko
let _ = http_respond_ct(c, 200, "text/html", "<h1>Hello</h1>")
```

### JSON response

```mko
let _ = http_respond_json(c, 200, "{\"status\":\"ok\"}")
```

This is equivalent to `http_respond_ct(c, 200, "application/json", body)`.

### Status codes

The runtime maps standard status codes to reason phrases automatically:

| Code | Meaning |
|------|---------|
| 200 | OK |
| 201 | Created |
| 204 | No Content |
| 400 | Bad Request |
| 401 | Unauthorized |
| 403 | Forbidden |
| 404 | Not Found |
| 405 | Method Not Allowed |
| 500 | Internal Server Error |

---

## Request Routing Patterns

### Simple path-based routing

```mko
fn handle_request(c: int) {
    let method = http_method(c)
    let path = http_path(c)

    if str_eq(path, "/") {
        let _ = http_respond(c, 200, "home\n")
    } else {
        if str_eq(path, "/health") {
            let _ = http_respond_json(c, 200, "{\"ok\":true}\n")
        } else {
            if str_eq(path, "/api/users") {
                if str_eq(method, "GET") {
                    let _ = http_respond_json(c, 200, "[]\n")
                } else {
                    if str_eq(method, "POST") {
                        let body = http_body(c)
                        let _ = http_respond_json(c, 201, body)
                    } else {
                        let _ = http_respond(c, 405, "method not allowed\n")
                    }
                }
            } else {
                let _ = http_respond(c, 404, "not found\n")
            }
        }
    }
}

fn main() {
    let fd = http_bind(18090)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("routes on :18090")

    while 1 == 1 {
        let c = http_accept(fd)
        if c >= 0 {
            handle_request(c)
            let _ = http_close(c)
        }
    }
}
```

### Prefix-based routing

```mko
fn handle_request(c: int) {
    let path = http_path(c)

    if str_contains(path, "/api/") {
        // API routes
        if str_eq(path, "/api/status") {
            let _ = http_respond_json(c, 200, "{\"status\":\"running\"}\n")
        } else {
            let _ = http_respond_json(c, 404, "{\"error\":\"not found\"}\n")
        }
    } else {
        // Static/page routes
        let _ = http_respond(c, 200, "page\n")
    }
}
```

---

## Concurrent Request Handling

Use crew blocks to serve multiple requests in parallel:

```mko
fn handle_connection(fd: int) -> int {
    let c = http_accept(fd)
    if c >= 0 {
        let p = http_path(c)
        if str_eq(p, "/slow") {
            sleep_ms(100)
            let _ = http_respond(c, 200, "done\n")
        } else {
            let _ = http_respond(c, 200, "fast\n")
        }
        let _ = http_close(c)
    }
    return 0
}

fn main() {
    let fd = http_bind(18100)
    if fd < 0 {
        print("bind failed")
        return
    }

    crew t {
        // Kick multiple handlers — each accepts one connection
        let h1 = t.kick(handle_connection(fd))
        let h2 = t.kick(handle_connection(fd))
        let h3 = t.kick(handle_connection(fd))
        let _ = h1.join()
        let _ = h2.join()
        let _ = h3.join()
    }
    let _ = http_close_listener(fd)
}
```

---

## Reload & graceful shutdown (signals)

Long-running servers react to OS signals — reload config on `HUP`, drain and exit
on `TERM`/`INT`. `signal_watch` installs a hook by name; `signal_fired` reports
(and clears) whether that signal arrived. Because the handlers interrupt blocking
calls, a `TERM` breaks out of a blocking `accept`.

```mko
fn main() {
    let _ = signal_ignore("PIPE")   // don't die writing to a closed socket
    let _ = signal_watch("HUP")     // reload
    let _ = signal_watch("TERM")    // shutdown

    let fd = http_bind(8080)
    while true {
        if signal_fired("TERM") == 1 { break }
        if signal_fired("HUP") == 1 { reload_config() }
        let c = http_accept(fd)     // interrupted (returns < 0) when a signal arrives
        if c < 0 { continue }
        handle(c)
        let _ = http_close(c)
    }
    // drain in-flight work, then exit cleanly
}
```

Names: `HUP`, `TERM`, `INT`, `QUIT`, `USR1`, `USR2`, `PIPE`, `CHLD`.

To reload automatically when a config file changes (rather than waiting for a
`HUP`), watch it. `watch_poll` blocks up to a timeout and returns the path that
changed, over kqueue (macOS/BSD) or inotify (Linux):

```mko
fn main() {
    let w = watch_new()
    let _ = watch_add(w, "servers.conf")
    while true {
        let changed = watch_poll(w, 1000)   // wait up to 1s
        if len(changed) > 0 {
            reload_config()
        }
    }
}
```

---

## TLS / HTTPS

When OpenSSL is linked, Mako supports HTTPS with TLS termination:

```mko
fn main() {
    let code = tls_serve_n(
        18443,
        "runtime/certs/dev.crt",
        "runtime/certs/dev.key",
        "hello from mako https\n",
        3   // serve 3 requests then exit
    )
    if code != 0 {
        print("tls_serve_n failed (OpenSSL missing or bind error)")
        return
    }
    print("https_server done")
}
```

### TLS configuration

- Certificate and key files are specified as paths.
- Self-signed certificates work for development (`runtime/certs/dev.crt`).
- In production, use certificates from a trusted CA.
- Never ship with TLS verification disabled.

### TLS client

```mko
fn main() {
    let result = tls_connect("example.com", 443)
    if result >= 0 {
        print("connected via TLS")
    }
}
```

### TLS server — owning the accept loop

The `tls_serve*` helpers answer with a fixed response. When you need to inspect
each connection and drive it yourself (a proxy, a database wire protocol), use
the socket-style API: bind and accept a plain TCP socket, then **upgrade that fd
to TLS**. Because you own the raw fd first, you can exchange bytes before the
handshake — for example Postgres's `SSLRequest` negotiation.

```mko
fn main() {
    let srv = tls_server_new("cert.pem", "key.pem")   // TlsServer (SSL context)
    let lfd = tcp_listen(5432)
    while true {
        let fd = tcp_accept(lfd)

        // Optional pre-TLS phase on the same socket (e.g. Postgres SSLRequest):
        let hello = tcp_read(fd)
        let _ = tcp_write(fd, "S")        // "yes, let's do TLS"

        // Upgrade the same fd to TLS, then read/write decrypted data.
        let conn = tls_accept(srv, fd)    // TlsConn (performs the handshake)
        let proto = tls_conn_alpn(conn)   // e.g. "h2", or "" if none
        let req = tls_read(conn, 4096)
        let _2 = tls_write(conn, "response")
        tls_conn_close(conn)
    }
}
```

For plain HTTPS (no pre-TLS bytes), just `tcp_accept` then `tls_accept`
immediately. `tls_server_available()` reports whether a TLS backend is linked.

### Require TLS 1.3

`tls_server_new_tls13(cert, key)` is the same context but with the minimum
protocol pinned to TLS 1.3 — clients that only offer 1.2 or older are rejected
at the handshake with a `protocol_version` alert. Use it when policy forbids
legacy TLS:

```mko
let srv = tls_server_new_tls13("cert.pem", "key.pem")
```

The default `tls_server_new` negotiates TLS 1.2+ (picking 1.3 when the client
supports it).

---

## Bind address & session controls

By default `tcp_listen(port)` binds all interfaces. To bind a specific address —
loopback-only, or one NIC on a multi-homed host — use `tcp_listen_addr`:

```mko
let fd = tcp_listen_addr("127.0.0.1", 5432)   // loopback only
let all = tcp_listen_addr("*", 5432)          // every interface (same as tcp_listen)
```

An unbindable address (not present on the host) returns `< 0`.

Once a connection is accepted, several socket controls keep sessions healthy:

| Call | Purpose |
|------|---------|
| `tcp_set_timeout(fd, ms)` | recv+send timeout — a stalled peer can't hold the session open forever (`0` blocks forever) |
| `tcp_keepalive(fd, idle, interval, count)` | detect dead peers and reap half-open connections (seconds; `0` keeps the OS default) |
| `tcp_listen_backlog(host, port, backlog)` | bound the kernel accept queue — the first lever against inbound floods |
| `tcp_listen_reuseport(host, port, backlog)` | listen with `SO_REUSEPORT` when available |
| `tcp_nodelay(fd)` | disable Nagle for low-latency writes |
| `tcp_set_recv_buf` / `tcp_set_send_buf` | socket buffer sizing |
| `tcp_accept4(listener)` | accept with nonblocking + cloexec flags |
| `tcp_connect_nb` / `tcp_connect_wait` | nonblocking connect for slow backends |

```mko
let lfd = tcp_listen_backlog("0.0.0.0", 5432, 256)
let fd = tcp_accept(lfd)
let _ = tcp_keepalive(fd, 30, 10, 3)   // 30s idle, 10s probes, 3 tries
let _2 = tcp_set_timeout(fd, 5000)     // 5s recv/send timeout
```

### Upstream connection pool

Keep backend TCP connections warm per `host:port`:

```mko
let pool = tcp_pool_open("127.0.0.1", 8080, 16, 2000)
let fd = tcp_pool_acquire(pool)
// … use fd, then:
let _ = tcp_pool_release(pool, fd, 1)   // 1 = reusable if still healthy
let _2 = tcp_pool_close(pool)
```

Release validates the fd **without blocking** (hot path). Closed peers and
fds with unexpected buffered data are not returned to the idle list.

### Efficient stream copy

```mko
let n = tcp_fd_copy(src_fd, dst_fd, 65536)   // Linux: splice when possible
let n2 = tcp_proxy_pump(a, b, 1000, 16 * 1024 * 1024)
```

---

## HTTP/2

HTTP/2 support uses TLS with ALPN `h2` negotiation:

```mko
fn main() {
    let code = tls_serve_h2_routes(
        18446,
        "runtime/certs/dev.crt",
        "runtime/certs/dev.key",
        "hello from mako h2\n",      // body for /
        "{\"ok\":true}\n",            // body for /health
        2                             // max requests
    )
    if code != 0 {
        print("h2 server failed (OpenSSL missing or bind error)")
        return
    }
    print("h2_server done")
}
```

HTTP/2 features:
- Multiplexed streams over a single TLS connection
- HPACK header compression
- ALPN protocol negotiation (`h2`)

Test with: `curl -sk --http2 https://127.0.0.1:18446/health`

### Building your own H2 server (proxy / dynamic responses)

When the fixed-route helper isn't enough — you need to inspect each request and
respond dynamically (a reverse proxy) — drive the connection yourself. Each
connection gets its own state with `http2_conn_new` / `http2_conn_use`; feed it
bytes with `http2_conn_recv`, and when a stream's header block is complete, decode
it with HPACK to recover the request:

```mko
fn read_request(conn: Http2Conn, bytes: string) -> string {
    let _ = http2_conn_use(conn)
    let _ = http2_conn_recv(bytes)          // process frames
    let stream = http2_conn_header_stream() // stream with complete headers, or 0
    if stream == 0 { return "" }

    let _d = hpack_decode_block(http2_conn_header_block(stream))
    // walk hpack_decoded_count() / hpack_decoded_name(i) / hpack_decoded_value(i)
    // to read :method, :path, and forward upstream …
    return "routed"
}
```

Reply with `http2_response(stream, status, body)`, which builds the whole
response — a HEADERS frame carrying `:status` and `content-length`, then a DATA
frame with the body and END_STREAM — ready to write back over the connection:

```mko
let bytes = http2_response(stream, 200, "hello from mako\n")
let _ = tls_write(conn, bytes)
```

Pair with `tls_accept` (ALPN `h2`) to terminate TLS. For finer control you can
still build frames by hand from `hpack_encode_indexed` / `hpack_encode_literal`
blocks wrapped in `http2_headers_frame` + `http2_data_frame`.

### Reverse proxy

**Body-only (simple):**

```mko
let upstream = http_forward("127.0.0.1", 8080, "GET", request_path(), "")
let _ = tls_write(conn, http2_response(stream, 200, upstream))
```

**Full result (status + body + headers, chunked OK):**

```mko
let r = http_forward_full("127.0.0.1", 8080, "GET", "/api", "", "", 2000)
if http_forward_ok(r) == 1 {
    let status = http_forward_status(r)
    let body = http_forward_body(r)
    // …
}
```

**Pooled fd:**

```mko
let fd = tcp_pool_acquire(pool)
let r = http_forward_fd(fd, "GET", "/api", "127.0.0.1", "", "", 2000)
let reusable = if http_forward_ok(r) == 1 { 1 } else { 0 }
let _ = tcp_pool_release(pool, fd, reusable)
```

**Raw byte pump** (no header rewrite):

```mko
let io = http_proxy_raw(client_fd, backend_fd, raw_request, 2000)
if proxy_io_ok(io) == 1 {
    // proxy_io_bytes_written(io) went to the client
}
```

**C-side request parse** (avoid repeated `str_split` in Mako):

```mko
let req = http_parse(raw_bytes)
if http_parsed_ok(req) == 1 {
    let path = http_parsed_path(req)
    let host = http_parsed_host(req)
}
```

Edge cases: caller headers without trailing CRLF are normalized; Host and
Content-Length are not duplicated if already present; 1xx/204/304 yield empty
bodies; incomplete chunked is a failure; CR/LF in method/path is rejected.
See [BUILTINS.md](https://github.com/loreste/mako/blob/main/docs/BUILTINS.md)
*Reverse-proxy notes*.

[`examples/h2_reverse_proxy.mko`](https://github.com/loreste/mako/blob/main/examples/h2_reverse_proxy.mko)
is a complete reverse proxy — verified end-to-end: `curl --http2` → the Mako
proxy → a plain-HTTP backend → relayed response. Unit coverage:
`examples/testing/proxy_pool_test.mko`, `proxy_edge_test.mko`.

### HTTP/2 stream multiplexing

After `http2_conn_recv`, completed HEADERS land in a ready queue (up to 32
stream slots):

```mko
let sid = http2_next_ready_stream()
if sid > 0 {
    let _ = http2_stream_take(sid)
    let body = http2_stream_body(sid)
    let out = http2_response(sid, 200, body)
}
```

### Async TLS accept

Avoid blocking the accept loop on slow handshakes:

```mko
let conn = tls_accept_start(srv, client_fd)
// poll tls_conn_fd(conn) until:
//   tls_handshake_step(conn) == 1  (done)
//   or == 0 want-read / == 2 want-write
```

### HTTP/3 surface

`h3_server_new` / `bind` / `poll` / `accept_stream` / `stream_read` / `write`
provide UDP event-loop wiring. Full crypto depth still uses quiche client helpers
(`quiche_h3_get`, …) when `MAKO_HAS_QUICHE` is linked.

---

## WebSocket

Mako provides WebSocket server support with RFC6455 upgrade, text/binary frames,
and automatic ping/pong handling.

### Echo server

```mko
fn main() {
    // Starts a WebSocket server that echoes one client's messages then exits
    print_int(ws_echo_once(18092))
}
```

The `ws_echo_once` builtin:
1. Binds the specified port
2. Accepts one TCP connection
3. Performs the WebSocket upgrade handshake (SHA-1 + base64 accept key)
4. Echoes text and binary frames back to the client
5. Responds to ping frames with pong automatically
6. Closes when the client disconnects or sends a close frame

### Ping/pong

```mko
fn main() {
    // WebSocket server with ping/pong support
    print_int(ws_echo_once(18095))
}
```

WebSocket ping/pong is handled automatically by the runtime. When a ping frame
arrives, a pong frame with the same payload is sent back immediately.

### WebSocket protocol details

- Upgrade negotiation uses `Sec-WebSocket-Key` + magic GUID, SHA-1 hashed,
  base64 encoded.
- Text frames (opcode 0x1) and binary frames (opcode 0x2) are supported.
- Close frames (opcode 0x8) trigger graceful shutdown.
- Frame masking from client-to-server is enforced per RFC6455.

---

## Building REST APIs

A complete REST API example combining routing, JSON, and proper HTTP methods:

```mko
fn handle_users_get(c: int) {
    let _ = http_respond_json(c, 200, "[{\"id\":1,\"name\":\"Ada\"}]\n")
}

fn handle_users_post(c: int) {
    let body = http_body(c)
    let name = json_get_string(body, "name")
    if len(name) == 0 {
        let _ = http_respond_json(c, 400, "{\"error\":\"name required\"}\n")
    } else {
        let resp = json_ss("id", "2", "name", name)
        let _ = http_respond_json(c, 201, resp)
    }
}

fn handle_health(c: int) {
    let _ = http_respond_json(c, 200, "{\"status\":\"healthy\"}\n")
}

fn route(c: int) {
    let method = http_method(c)
    let path = http_path(c)

    if str_eq(path, "/health") {
        handle_health(c)
    } else {
        if str_eq(path, "/api/users") {
            if str_eq(method, "GET") {
                handle_users_get(c)
            } else {
                if str_eq(method, "POST") {
                    handle_users_post(c)
                } else {
                    let _ = http_respond(c, 405, "method not allowed\n")
                }
            }
        } else {
            let _ = http_respond_json(c, 404, "{\"error\":\"not found\"}\n")
        }
    }
}

fn main() {
    let fd = http_bind(18100)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("REST API on :18100")

    let mut n = 0
    while n < 100 {
        let c = http_accept(fd)
        if c >= 0 {
            route(c)
            let _ = http_close(c)
            n = n + 1
        }
    }
    let _ = http_close_listener(fd)
}
```

### Testing the API

```bash
# Health check
curl -s http://127.0.0.1:18100/health

# List users
curl -s http://127.0.0.1:18100/api/users

# Create user
curl -s -X POST http://127.0.0.1:18100/api/users \
  -H "Content-Type: application/json" \
  -d '{"name":"Grace"}'

# 404
curl -s http://127.0.0.1:18100/api/unknown
```

---

## Graceful Shutdown

The runtime provides shutdown coordination for long-running servers:

```mko
fn main() {
    let fd = http_bind(18100)
    if fd < 0 {
        return
    }

    // Begin graceful shutdown with 5-second grace period
    let deadline = http_shutdown_begin(5000)

    // Check shutdown state
    if http_shutdown_requested() == 1 {
        // Drain in-flight requests
        while http_active_connections() > 0 {
            if http_shutdown_expired() == 1 {
                break
            }
            sleep_ms(100)
        }
    }

    let _ = http_close_listener(fd)
}
```

Shutdown API:

| Function | Purpose |
|----------|---------|
| `http_shutdown_begin(grace_ms)` | Start graceful shutdown with grace period |
| `http_shutdown_requested()` | Check if shutdown was requested (returns 1 or 0) |
| `http_shutdown_ready()` | Check if server is ready to accept (returns 1 or 0) |
| `http_shutdown_remaining()` | Milliseconds until grace period expires |
| `http_shutdown_expired()` | Check if grace period has elapsed |
| `http_active_connections()` | Count of currently active connection slots |
| `http_shutdown_reset()` | Reset shutdown state (for testing) |

---

## Security Checklist

| Concern | Practice |
|---------|----------|
| Header injection | Runtime validates all header names/values, rejects CR/LF/NUL |
| Content-Length | Enforced automatically on responses |
| Request body limits | 1MB default (or Content-Length, whichever is smaller) |
| Secrets in memory | Use `secret_from_str` / `secret_drop` |
| TLS in production | Use trusted CA certificates; never disable verification |
| Concurrent safety | Each connection fd is independent; do not share across tasks |
| Header validation | Use `http_header_ok` for custom header checks |

### Validating headers

```mko
fn main() {
    let fd = http_bind(18100)
    if fd < 0 {
        return
    }
    let c = http_accept(fd)
    if c >= 0 {
        let auth = http_header(c, "Authorization")
        // Header values are already validated by the runtime
        // (no CR/LF/NUL injection possible)
        if len(auth) == 0 {
            let _ = http_respond(c, 401, "unauthorized\n")
        } else {
            let _ = http_respond(c, 200, "ok\n")
        }
        let _ = http_close(c)
    }
    let _ = http_close_listener(fd)
}
```

---

## HTTP Client

Client-side HTTP helpers for making outbound requests:

```mko
fn main() {
    let body = http_get("http://127.0.0.1:18100/health")
    print(body)
}
```

For production use:
- Always set explicit timeouts
- Validate URLs before making requests
- Use parameterized URL construction (no string concatenation of user input)

---

## Keep-Alive Connections

HTTP/1.1 connections support keep-alive by default. The runtime detects the
`Connection: keep-alive` header and maintains the connection slot:

```mko
fn main() {
    let fd = http_bind(18100)
    if fd < 0 {
        return
    }
    // The runtime automatically handles Connection: keep-alive
    // and Connection: close headers on incoming requests.
    // Use http_close(c) when you want to force-close regardless.
    let c = http_accept(fd)
    if c >= 0 {
        let _ = http_respond(c, 200, "first response\n")
        // For one-shot servers, close explicitly:
        let _ = http_close(c)
    }
    let _ = http_close_listener(fd)
}
```

---

## Connection Limits

The runtime maintains a fixed connection table (32 slots by default). Each
`http_accept` occupies one slot; `http_close` frees it. If all slots are in use,
`http_accept` blocks until a slot becomes available.

Design accordingly:
- Close connections promptly after responding
- Use crew blocks to serve connections concurrently within the slot limit
- For high-concurrency scenarios, dispatch to worker tasks quickly

---

## Event Loop and Non-blocking I/O

For servers that must handle many concurrent connections without a thread per
connection, Mako provides a non-blocking event loop (`runtime/mako_evloop.h`).
It uses epoll on Linux and kqueue on macOS under the hood.

### Non-blocking TCP Server

```mko
fn main() {
    let el = evloop_new()
    let server_fd = nb_listen(8080)
    let _ = evloop_add(el, server_fd, 1)  // monitor for readability

    while true {
        let n = evloop_wait(el, 1000)
        let mut i = 0
        while i < n {
            let fd = evloop_event_fd(el, i)
            if fd == server_fd {
                let client = nb_accept(server_fd)
                if client >= 0 {
                    let _ = evloop_add(el, client, 1)
                }
            } else {
                let data = nb_read(fd)
                if len(data) > 0 {
                    let _ = nb_write(fd, "echo: " + data)
                }
                let _ = evloop_del(el, fd)
                let _ = nb_close(fd)
            }
            i = i + 1
        }
    }
    let _ = evloop_close(el)
}
```

A complete, runnable version — the template to build a real protocol server (e.g.
a database wire protocol) on one thread — is in
[`examples/nb_echo_server.mko`](https://github.com/loreste/mako/blob/main/examples/nb_echo_server.mko).
Test it with `printf 'hello' | nc 127.0.0.1 9099`.

### Event Loop API

| Function | Purpose |
|----------|---------|
| `evloop_new()` | Create event loop |
| `evloop_add(el, fd, flags)` | Register fd |
| `evloop_mod(el, fd, flags)` | Modify interest flags |
| `evloop_del(el, fd)` | Remove fd |
| `evloop_wait(el, timeout_ms)` | Wait for events, returns count |
| `evloop_event_fd(el, index)` | Get fd from event at index |
| `evloop_event_flags(el, index)` | Get flags from event at index |
| `evloop_close(el)` | Destroy event loop |

### Non-blocking Socket Helpers

| Function | Purpose |
|----------|---------|
| `nb_listen(port)` | Create non-blocking TCP listener |
| `nb_accept(fd)` | Non-blocking accept |
| `nb_read(fd)` | Non-blocking read (returns available data) |
| `nb_write(fd, data)` | Non-blocking write |
| `nb_udp_bind(port)` | Non-blocking UDP socket |
| `nb_udp_recv(fd)` | Non-blocking UDP receive |
| `nb_close(fd)` | Close non-blocking socket |

---

## Game UDP Networking

For real-time game servers, Mako provides a dedicated UDP networking subsystem
(`runtime/mako_game.h`) that tracks connected peers and supports broadcast:

```mko
fn main() {
    let u = game_udp_bind(27015)
    let el = evloop_new()
    let _ = evloop_add(el, game_udp_fd(u), 1)

    let interval_us = 16666  // ~60 Hz tick rate
    while true {
        let start = tick_now_us()
        let n = evloop_wait(el, 1)
        if n > 0 {
            let data = game_udp_recv(u)
            let peer = game_udp_sender(u)
            // Process input from peer, then broadcast state
            let _ = game_udp_broadcast(u, "world_state")
        }
        let _ = tick_sleep_us(start, interval_us)
    }
    game_udp_close(u)
    let _ = evloop_close(el)
}
```

### Game UDP API

| Function | Purpose |
|----------|---------|
| `game_udp_bind(port)` | Bind UDP game socket |
| `game_udp_recv(u)` | Receive packet (tracks sender) |
| `game_udp_sender(u)` | Get peer ID of last sender |
| `game_udp_sender_addr(u)` | `host:port` of the last sender (for routing replies) |
| `game_udp_send(u, peer, data)` | Send to specific peer |
| `game_udp_send_to(u, host, port, data)` | Send to an arbitrary address (upstream forwarding) |
| `game_udp_broadcast(u, data)` | Send to all connected peers |
| `game_udp_kick(u, peer)` | Disconnect a peer |
| `game_udp_peers(u)` | Number of connected peers |
| `game_udp_fd(u)` | Raw fd for event loop integration |
| `game_udp_close(u)` | Close socket |
| `tick_now_us()` | Microsecond timestamp |
| `tick_sleep_us(start, interval)` | Sleep to maintain tick rate |

The `game_udp_fd` function returns the raw file descriptor so you can integrate
the game socket into an event loop alongside other I/O sources.

**Request/response routing.** `game_udp_sender_addr` and `game_udp_send_to` make
the socket usable as a frontend that forwards traffic and routes replies back to
the original sender — the basis for a UDP proxy:

```mko
fn main() {
    let front = game_udp_bind(5060)
    while true {
        let req = game_udp_recv(front)
        if len(req) == 0 { continue }
        let client = game_udp_sender_addr(front)   // remember who asked

        // forward upstream, then route the reply back to `client`
        let _ = game_udp_send_to(front, "10.0.0.2", 5060, req)
        let resp = game_udp_recv(front)
        let parts = str_split(client, ":")
        match parse_int(parts[1]) {
            Ok(port) => { let _ = game_udp_send_to(front, parts[0], port, resp) },
            Err(_) => {},
        }
    }
}
```

**Running several frontends.** To hold more than one listener, wrap the handle in
a struct — a struct field stores a `GameUDP`, and a struct array holds many. This
is also where per-frontend state (name, upstream, a transaction table) naturally
lives:

```mko
struct Front { u: GameUDP, name: string, upstream: string }

fn main() {
    let fronts = [
        Front { u: game_udp_bind(5060), name: "sip", upstream: "10.0.0.2" },
        Front { u: game_udp_bind(5061), name: "sip-tls", upstream: "10.0.0.3" },
    ]
    // poll each front's fd through an event loop and route per upstream
}
```

---

## HTTP Engine (Declarative Routing)

For applications that want declarative route registration instead of manual
path matching, Mako provides an HTTP engine (`runtime/mako_httpengine.h`):

```mko
fn main() {
    let e = httpengine_new()
    let _ = httpengine_route(e, "GET", "/health", 1)
    let _ = httpengine_route(e, "GET", "/api/users", 2)
    let _ = httpengine_route(e, "POST", "/api/users", 3)
    let _ = httpengine_start(e, 8080)
    // Engine runs, dispatching requests to handler IDs
    httpengine_stop(e)
    httpengine_free(e)
}
```

### HTTP Engine API

| Function | Purpose |
|----------|---------|
| `httpengine_new()` | Create HTTP engine instance |
| `httpengine_route(e, method, path, handler_id)` | Register a route with a handler ID |
| `httpengine_start(e, port)` | Start listening and serving |
| `httpengine_stop(e)` | Stop the engine |
| `httpengine_free(e)` | Destroy the engine |

The engine builds on the event loop internally for non-blocking I/O. Use it
when you want a higher-level abstraction over the raw `http_bind`/`http_accept`
loop.

---

## Sessions and Authentication

Mako provides built-in session management and authentication primitives that
integrate with the HTTP server. These live in `runtime/mako_security.h` and
use constant-time comparisons throughout to prevent timing attacks.

### Cookie-Based Sessions with CMap

Use `CMap` as a thread-safe session store. Session IDs are generated with
cryptographic randomness, and cookies default to HttpOnly + SameSite=Lax:

```mko
let sessions = cmap_new()

fn handle_login(c: int) {
    let body = http_body(c)
    let user = json_get_string(body, "user")
    let pass = json_get_string(body, "pass")

    // verify credentials against your database

    let sid = session_id_new()                     // 32-char hex, crypto-random
    cmap_set(sessions, sid, user)
    let cookie = cookie_make("sid", sid, 86400)    // HttpOnly; SameSite=Lax; Path=/; 24h
    let _ = http_respond_ct(c, 200, "application/json", "{\"ok\":true}", cookie)
}

fn handle_protected(c: int) {
    let cookie_hdr = http_header(c, "Cookie")
    let sid = cookie_get(cookie_hdr, "sid")
    if cmap_has(sessions, sid) == 0 {
        let _ = http_respond_json(c, 401, "{\"error\":\"unauthorized\"}")
        return
    }
    let user = cmap_get(sessions, sid)
    let _ = http_respond_json(c, 200, json_object("user", user))
}

fn handle_logout(c: int) {
    let cookie_hdr = http_header(c, "Cookie")
    let sid = cookie_get(cookie_hdr, "sid")
    let _ = cmap_del(sessions, sid)
    let expired = cookie_make("sid", "", 0)        // expire the cookie
    let _ = http_respond_ct(c, 200, "application/json", "{\"ok\":true}", expired)
}
```

### Bearer Token Authentication

For API-to-API or mobile clients that send tokens in the `Authorization` header:

```mko
fn handle_api(c: int) {
    let auth_hdr = http_header(c, "Authorization")
    if auth_check_bearer(auth_hdr, expected_api_key) == 0 {
        let _ = http_respond_json(c, 401, "{\"error\":\"invalid token\"}")
        return
    }
    // authorized -- proceed
    let _ = http_respond_json(c, 200, "{\"data\":\"secret\"}")
}
```

### CSRF Protection

For browser-facing forms, generate a CSRF token per session and verify it on
state-changing requests:

```mko
fn handle_form_page(c: int) {
    let token = csrf_token()
    // store token in session
    let cookie_hdr = http_header(c, "Cookie")
    let sid = cookie_get(cookie_hdr, "sid")
    cmap_set(csrf_store, sid, token)
    // include token in the HTML form as a hidden field
    let html = "<form><input type=\"hidden\" name=\"csrf\" value=\"" + token + "\">...</form>"
    let _ = http_respond_ct(c, 200, "text/html", html)
}

fn handle_form_submit(c: int) {
    let cookie_hdr = http_header(c, "Cookie")
    let sid = cookie_get(cookie_hdr, "sid")
    let expected = cmap_get(csrf_store, sid)
    let body = http_body(c)
    let submitted = json_get_string(body, "csrf")
    if csrf_check(expected, submitted) == 0 {
        let _ = http_respond_json(c, 403, "{\"error\":\"CSRF token mismatch\"}")
        return
    }
    // CSRF valid -- process form
}
```

### Session/Auth API Reference

| Function | Purpose |
|----------|---------|
| `cookie_get(header, name)` | Parse cookie value from Cookie header |
| `cookie_make(name, value, max_age)` | Create Set-Cookie string (HttpOnly, SameSite=Lax) |
| `session_id_new()` | 32-char crypto-random hex session ID |
| `auth_session_cookie(header, name, expected)` | Constant-time session cookie check |
| `csrf_token()` | Generate random CSRF token |
| `csrf_check(expected, submitted)` | Constant-time CSRF comparison |
| `auth_bearer(authorization)` | Extract token from Bearer header |
| `auth_check_bearer(authorization, expected)` | Constant-time bearer verification |
| `auth_basic_header(user, pass)` | Build Basic auth header |
| `auth_check_basic(authorization, user, pass)` | Verify Basic auth credentials |
| `auth_token_sign(subject, secret)` | HMAC-SHA256 sign: "subject.signature" |
| `auth_token_check(token, secret)` | Verify signed token |
| `auth_token_subject(token)` | Extract subject from signed token |
| `auth_role_has(roles_csv, role)` | Check role membership |
| `authz_allow_role(user_roles, required_roles)` | Check if user has any required role |

---

## Summary

| Layer | Functions |
|-------|-----------|
| TCP | `tcp_listen`, `tcp_accept`, `tcp_connect`, `tcp_read`, `tcp_write`, `tcp_close` |
| HTTP Server | `http_bind`, `http_accept`, `http_method`, `http_path`, `http_body`, `http_header` |
| HTTP Response | `http_respond`, `http_respond_ct`, `http_respond_json`, `http_close` |
| HTTPS | `tls_serve_n`, `tls_serve_h2_routes`, `tls_connect` |
| WebSocket | `ws_echo_once` |
| Event Loop | `evloop_new`, `evloop_add`, `evloop_wait`, `evloop_event_fd`, `evloop_close` |
| Non-blocking I/O | `nb_listen`, `nb_accept`, `nb_read`, `nb_write`, `nb_close` |
| Game UDP | `game_udp_bind`, `game_udp_recv`, `game_udp_send`, `game_udp_broadcast` |
| HTTP Engine | `httpengine_new`, `httpengine_route`, `httpengine_start`, `httpengine_stop` |
| Shutdown | `http_shutdown_begin`, `http_shutdown_requested`, `http_active_connections` |
| Sessions/Auth | `session_id_new`, `cookie_get`, `cookie_make`, `auth_bearer`, `auth_check_bearer`, `csrf_token`, `csrf_check` |
| Listener | `http_close_listener` |

Next: [Data](ch09-data.md).
