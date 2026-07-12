# Cloud and Distributed Systems

This tutorial covers rate limiting, circuit breakers, consistent hashing,
JWT authentication, and building a rate-limited API gateway.

---

## Rate Limiting

Create a token-bucket limiter with `ratelimit_new(rate, burst)` where
`rate` is tokens per second and `burst` is the maximum burst size.

```mko
fn main() {
    // Allow 10 requests/sec with a burst of 5
    let rl = ratelimit_new(10, 5)

    let mut allowed = 0
    let mut denied = 0
    let mut i = 0
    while i < 20 {
        if ratelimit_allow(rl) == 1 {
            allowed = allowed + 1
        } else {
            denied = denied + 1
        }
        i = i + 1
    }

    print("allowed:")
    print(allowed)
    print("denied:")
    print(denied)
    print("remaining tokens:")
    print(ratelimit_remaining(rl))

    ratelimit_free(rl)
}
```

For per-client rate limiting, use `CMap` with `cmap_incr` to track
request counts per client identifier.

---

## Circuit Breaker

Protects downstream services by failing fast after errors accumulate.
States: 0=closed (normal), 1=open (failing fast), 2=half-open (probing).

```mko
fn main() {
    let cb = breaker_new(3, 5000, 2)  // 3 failures, 5s timeout, 2 probes
    print(breaker_state(cb))      // 0 = closed

    breaker_failure(cb)
    breaker_failure(cb)
    breaker_failure(cb)
    print(breaker_state(cb))      // 1 = open

    if breaker_allow(cb) == 0 {
        print("circuit open, failing fast")
    }

    breaker_reset(cb)
    print(breaker_state(cb))      // 0 = closed
    breaker_free(cb)
}
```

Wrap downstream calls: check `breaker_allow`, then call
`breaker_success` or `breaker_failure` based on the result.

---

## Consistent Hashing

Consistent hashing distributes keys across nodes with minimal
redistribution when nodes are added or removed. This is essential for
distributed caches, databases, and load balancers.

```mko
fn main() {
    // Create a ring with 3 nodes, 150 virtual nodes each
    let ring = chash_new(3, 150)

    // Route keys to nodes
    let node_a = chash_get(ring, "user:42")
    let node_b = chash_get(ring, "user:99")
    let node_c = chash_get(ring, "session:abc")

    print("user:42 -> node:")
    print(node_a)
    print("user:99 -> node:")
    print(node_b)
    print("session:abc -> node:")
    print(node_c)

    // Add a new node (returns new node ID)
    let new_id = chash_add_node(ring)
    print("added node:")
    print(new_id)  // 3

    // Some keys may now route to the new node
    let node_a2 = chash_get(ring, "user:42")
    print("user:42 after add -> node:")
    print(node_a2)

    // Remove a node
    chash_remove_node(ring, 0)
    print("nodes after remove:")
    print(chash_node_count(ring))

    chash_free(ring)
}
```

---

## JWT Token Signing and Verification

Mako provides HMAC-SHA256 JWT signing and verification built in. This
is useful for API authentication and session tokens.

```mko
fn main() {
    let secret = "my-secret-key"
    let payload = "{\"sub\":\"user-42\",\"role\":\"admin\"}"

    // Sign
    let token = jwt_sign(payload, secret)
    print(token)

    // Verify
    print(jwt_verify(token, secret))       // 1
    print(jwt_verify(token, "wrong-key"))   // 0

    // Extract payload (does NOT verify -- always verify first)
    let data = jwt_payload(token)
    let sub = json_get_string(data, "sub")
    print("subject: " + sub)
}

fn authenticate(token: string, secret: string) -> Result[string, string] {
    if token == "" {
        return error("missing token")
    }
    if jwt_verify(token, secret) == 0 {
        return error("invalid token")
    }
    let payload = jwt_payload(token)
    let sub = json_get_string(payload, "sub")
    if sub == "" {
        return error("missing subject")
    }
    return Ok(sub)
}
```

---

## Building a Rate-Limited API Gateway

Complete example combining all primitives.

```mko
fn main() {
    let mut max = 100
    let fd = http_bind(18400)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("gateway on :18400")

    // Rate limiter: 50 req/sec, burst of 20
    let rl = ratelimit_new(50, 20)

    // Circuit breaker for downstream: 5 failures, 30s timeout, 3 probes
    let cb = breaker_new(5, 30000, 3)

    // JWT secret
    let secret = "gateway-secret-key"

    let mut n = 0
    while n < max {
        let c = http_accept(fd)
        if c < 0 {
            // skip
        } else {
            let path = http_path(c)
            let method = http_method(c)

            if path == "/health" {
                let _ = http_respond_json(c, 200, "{\"ok\":true}\n")
            } else {
                if ratelimit_allow(rl) == 0 {
                    let _ = http_respond_json(c, 429, json_object("error", "rate limit exceeded"))
                } else {
                    if path == "/api/token" {
                        if method == "POST" {
                            let body = http_body(c)
                            let user = json_get_string(body, "user")
                            let payload = json_object("sub", user)
                            let tok = jwt_sign(payload, secret)
                            let _ = http_respond_json(c, 200, json_object("token", tok))
                        } else {
                            let _ = http_respond_json(c, 405, json_object("error", "method not allowed"))
                        }
                    } else {
                        if path == "/api/data" {
                            let token = http_header(c, "Authorization")
                            match authenticate(token, secret) {
                                Ok(user) => {
                                    if breaker_allow(cb) == 0 {
                                        let _ = http_respond_json(c, 503, json_object("error", "service unavailable"))
                                    } else {
                                        breaker_success(cb)
                                        let _ = http_respond_json(c, 200, json_ss("status", "ok", "user", user))
                                    }
                                }
                                Err(e) => {
                                    let _ = http_respond_json(c, 403, json_object("error", e))
                                }
                            }
                        } else {
                            let _ = http_respond_json(c, 404, json_object("error", "not found"))
                        }
                    }
                }
            }
            let _ = http_close(c)
            n = n + 1
        }
    }

    ratelimit_free(rl)
    breaker_free(cb)
    let _ = http_close_listener(fd)
    print("gateway done")
}

fn authenticate(token: string, secret: string) -> Result[string, string] {
    if jwt_verify(token, secret) == 0 {
        return error("invalid token")
    }
    let payload = jwt_payload(token)
    let sub = json_get_string(payload, "sub")
    if sub == "" {
        return error("missing subject")
    }
    return Ok(sub)
}
```

---

## Testing the Gateway

```bash
mako build main.mko -o out/gateway
out/gateway 30 &

# Health check
curl -sS http://127.0.0.1:18400/health

# Get a token
curl -sS -X POST -H 'Content-Type: application/json' \
    -d '{"user":"ada"}' http://127.0.0.1:18400/api/token

# Use the token (replace TOKEN with actual value)
curl -sS -H 'Authorization: TOKEN' http://127.0.0.1:18400/api/data
```

The `/health` endpoint returns circuit breaker state, remaining rate
limit tokens, and total request counts -- giving operators visibility
into graceful degradation. When the circuit opens, `/api/data` returns
503 immediately rather than waiting on a failing downstream service.

---

## Distributed Tracing

For observability across services, Mako provides lightweight distributed tracing
built in. Wrap work in `trace_begin` / `trace_end` spans, attach log events, and
propagate the trace ID across service boundaries.

```mko
fn handle_request(c: int) {
    trace_begin("handle_request")
    let tid = trace_id()

    let method = http_method(c)
    let path = http_path(c)
    trace_log("method=" + method + " path=" + path)

    // Forward trace ID to downstream services via header
    let body = http_get_timeout("http://backend:9000/work?trace=" + tid, 2000)
    trace_log("downstream responded")

    let _ = http_respond_json(c, 200, json_ss("status", "ok", "trace", tid))
    trace_end()
}

fn main() {
    let fd = http_bind(18400)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("traced gateway on :18400")

    let mut n = 0
    while n < 50 {
        let c = http_accept(fd)
        if c < 0 { continue }
        handle_request(c)
        let _ = http_close(c)
        n = n + 1
    }
    let _ = http_close_listener(fd)
}
```

### Tracing API

| Function | Purpose |
|----------|---------|
| `trace_begin(name)` | Start a named span |
| `trace_end()` | End the current span |
| `trace_id()` | Get the current trace ID (hex string) |
| `trace_log(msg)` | Attach a log event to the active span |

Trace IDs are generated per top-level `trace_begin` call. Pass the ID in headers
or query parameters to correlate spans across services.

---

## API Reference

| Function | Purpose |
|----------|---------|
| `ratelimit_new(rate, burst)` | Create token-bucket limiter |
| `ratelimit_allow(rl)` | Consume token (1=ok, 0=denied) |
| `ratelimit_remaining(rl)` | Tokens left |
| `ratelimit_free(rl)` | Destroy limiter |
| `breaker_new(threshold, timeout_ms, max)` | Create circuit breaker |
| `breaker_allow(cb)` | Check if request allowed |
| `breaker_success(cb)` | Record success |
| `breaker_failure(cb)` | Record failure |
| `breaker_state(cb)` | 0=closed, 1=open, 2=half-open |
| `breaker_reset(cb)` | Reset to closed |
| `breaker_free(cb)` | Destroy breaker |
| `chash_new(nodes, vnodes)` | Create hash ring |
| `chash_get(ring, key)` | Get node for key |
| `chash_add_node(ring)` | Add node, returns ID |
| `chash_remove_node(ring, node)` | Remove node |
| `chash_node_count(ring)` | Active node count |
| `chash_free(ring)` | Destroy ring |
| `jwt_sign(payload, secret)` | Sign JWT (HMAC-SHA256) |
| `jwt_verify(token, secret)` | Verify JWT (1=valid, 0=invalid) |
| `jwt_payload(token)` | Extract payload (no verify) |
