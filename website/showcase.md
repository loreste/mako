# Projects Built with Mako

Independent projects that exercise Mako in real systems programs. Project
status and production claims belong to each project's own repository.

---

## Load balancer / reverse proxy

A Linux load balancer written entirely in Mako and maintained in the separate
[loreste/leba repository](https://github.com/loreste/leba). It is a systems
programming showcase, not a blanket production-readiness claim for Mako.

### What It Does

Handles HTTP/1.1, HTTP/2, TCP, and SIP traffic across multiple backends with
automatic health checks, TLS termination, and live metrics.

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

This class of service exercises latency-sensitive, concurrent network
infrastructure. It exercises channels, fan-out, shared state, graceful
shutdown, and the networking surface; consult Leba's own README for supported
protocols and known limits.

### Quick Look

```mko
// Concurrency primitives fan requests across backends
fn forward_loop(front: i32, backends: []Backend, ch: chan[Conn]) {
    while 1 == 1 {
        let conn = http_accept(front)
        if conn < 0 {
            break
        }
        let target = pick_backend(backends)
        // kick worker / channel handoff …
    }
}
```

---

## More projects

As more independent Mako services ship, they will be listed here with links to
their own source and status documentation.
