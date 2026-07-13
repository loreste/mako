# Projects Built with Mako

Real-world projects demonstrating what Mako can do in production.

---

## Leba — Load Balancer

A full-featured Linux load balancer written entirely in Mako. Leba is both
a production-ready tool and a showcase of the language's systems programming
capabilities.

**Repository:** [github.com/loreste/leba](https://github.com/loreste/leba)

### What It Does

Leba handles HTTP/1.1, HTTP/2, TCP, and SIP traffic across multiple backends
with automatic health checks, TLS termination, and live metrics.

### Key Features

- **Multi-protocol** — HTTP reverse proxy, raw TCP forwarding, SIP signaling
  with Call-ID affinity
- **Six balancing strategies** — round-robin, least-connection, weighted,
  random, IP-hash, Call-ID hash
- **TLS termination** — terminate HTTPS at the load balancer, forward plain
  HTTP to backends
- **Health checks** — automatic backend monitoring with drain/ready/disable
  state machine
- **Rate limiting** — per-client connection and request rate caps
- **Sticky sessions** — cookie-based session persistence
- **Admin dashboard** — built-in HTML stats page, JSON API, and
  Prometheus-compatible metrics endpoint
- **Role-based access** — viewer, operator, and admin roles for the control
  plane
- **Hot config** — certain settings apply live without restart

### Why It Matters

Leba demonstrates that Mako is ready for latency-sensitive, concurrent network
infrastructure. The project exercises channels, fan-out, shared state, graceful
shutdown, and the full networking standard library — all in clear, readable
code.

### Quick Look

```mko
// Leba uses Mako's concurrency primitives to fan requests across backends
fn forward_loop(front: i32, backends: []Backend, ch: chan<Conn>) {
    while 1 == 1 {
        let conn = http_accept(front)
        if conn < 0 {
            break
        }
        let target = pick_backend(backends)
        push(ch, Conn{ fd: conn, backend: target })
    }
}
```

---

*Building something with Mako? Open a PR to add your project here.*
