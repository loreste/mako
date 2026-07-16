# Mako vision

**North star**

> **Speed · speed · speed** · concurrency & parallelism **first-class** · security **first-class** · no mandatory GC · fast builds · single binary · strong stdlib.

**The game:** run **as close to Rust as possible**, with **first-class concurrency
and parallelism** (`crew` / `kick` / `join` / `fan` / channels / actors) that do
not leak work or color the whole language, and **first-class security** (memory,
crews, secrets, secure defaults) that does not silently tax the hot path.
Write-ups: [SPEED.md](SPEED.md) · [SECURITY.md](SECURITY.md).

**Why Mako exists:** fix the real **pain points of Go and Rust** without cloning
either language. Go’s GC/nil/err-noise/goroutine leaks and Rust’s
lifetime/trait/async ceremony are product problems we answer with *Mako*
tools (`hold`/`share`/`arena`, `crew`/`kick`/`fan`, `Result`/`?`, `enum`/`match`,
`pack`/`pull`). Full map: [PAIN_POINTS.md](PAIN_POINTS.md).

**Product contract:** Mako is a versatile, general-purpose backend and
infrastructure language. It is approachable and deployable, with strong safety
guarantees, **Rust-class speed**, first-class concurrent/parallel work, and low
cognitive overhead for everyday backend work.

| Principle | What it means |
|-----------|---------------|
| **Speed first** | **The name of the game** — as close to Rust as possible ([PERFORMANCE.md](PERFORMANCE.md), [SPEED.md](SPEED.md)) |
| **Concurrency first-class** | `crew` / `kick` / `join` / channels / `select` / `actor` — structured, no free-fire leaks |
| **Parallelism first-class** | `fan` + multi-kick crews — use the cores without a third-party pool |
| **Security first-class** | Compiler + runtime contract — NLL, bounds, secrets, secure stdlib ([SECURITY.md](SECURITY.md)) |
| Simple syntax | Clean, readable code that gets out of your way |
| **Low ceremony** | Real work without a lot of typing ([ERGONOMICS.md](ERGONOMICS.md)) |
| Fast builds | Compile times stay short even as projects grow |
| Easy deploy | Static binaries, no runtime dependencies |
| Practical stdlib | Batteries included for real backend work |
| Memory safety | Ownership, arenas, explicit resource control — no UAF |
| Zero-cost abstractions | High-level constructs compile to efficient machine code |

Mako is for **backend software, cloud infrastructure, networking systems,
developer tools, databases, and high-performance services** — without academic
ceremony and without a mandatory garbage collector. Product surfaces that must
work over time:

- **Backend applications** and **API services** (HTTP/JSON, `mako init --backend`)
- **CLI and developer tools** (flags, env, files, subprocesses, static binary deploy)
- **Cloud and infrastructure tools** (agents, operators, sidecars, proxies, gateways)
- **Systems programming** (arenas, hold/share, bytes/files, append logs)
- **Database / storage engines** (mini embedded KV in `examples/db_engine/`, plus SQL clients)
- **Realtime and telecom systems** (actors, timers, protocol stacks, session state)
- **Fast native binaries** (release `-O3 -flto`, no mandatory GC — [PERFORMANCE.md](PERFORMANCE.md))

**Core promise:** Ship fast binaries, run concurrent and parallel work as a
first-class part of the language, stay safe without a GC, keep everyday code short.

**Syntax promise:** Mako has its **own flair** — not a Go or Rust clone. It may
accept dual spellings for familiarity, but preferred docs, examples, and
`mako fmt` always lead with Mako forms: `fn`, `let`, `on`, `pack` / `pull`,
`hold` / `share` / `arena`, `crew` / `kick` / `join`, `match`, `export`,
`.mko`. See [IDENTITY.md](IDENTITY.md).

Honest status lives in [STATUS.md](STATUS.md). **How to write Mako today:**
[The Mako Book](book/) (guided tour) and [GUIDE.md](GUIDE.md) (verified syntax).
This file is the product map (includes Target ideas).

---

## Identity checklist

| Pillar | Target |
|--------|--------|
| Memory | Ownership + arenas; RC/manual escapes; **optional** GC for apps only |
| Speed | Rust-class hot path; measure; no silent cost |
| Concurrency | First-class `crew` / channels / actors / `select` (structured) |
| Parallelism | First-class `fan` + multi-kick crews |
| Syntax | Unique Mako surface; familiar, concise, practical backend style |
| Errors | Explicit, typed, easy `?` — unused Result is illegal |
| Tooling | pkg, fmt, lint, test, bench, docs, audit, cross-compile, IDE/LSP |
| Stdlib | net/tls/quic/http/ws, JSON/CBOR/…, DB drivers, queues, observability |
| Systems | Drivers, protocols, DBs, engines, compilers |
| Generics | `List<T>`, `Map<K,V>`, `Result<T,E>` — light interfaces |
| Deploy | Static binaries, small containers, WASM later, fast startup |

---

## Versatility Goal

Mako should be equally comfortable for:

- REST / GraphQL / gRPC APIs, modular monoliths, microservices, background jobs
- CLI tools, developer tools, migration tools, deployment tools, cloud CLIs
- Cloud infrastructure: operators, controllers, sidecars, agents, gateways
- Network services: proxies, load balancers, WebSocket servers, streaming APIs
- Data systems: databases, caches, search engines, queues, storage engines
- AI inference services, realtime applications, telecom platforms, edge/WASM apps

The standard for "general purpose" is practical: a beginner should be able to
ship a simple API, and an expert should be able to build a database, proxy,
compiler, or distributed runtime.

---

## Domain Track — Sessions

Session-oriented servers remain a high-value proving ground: VoIP/telecom,
game rooms, connection brokers, streaming gateways, and realtime collaboration.

```mko
// Target surface (actors)
actor Session {
    state Call
    receive Invite
    receive Bye
    receive Timer
}
```

Each actor owns its state — no shared-memory races. Under the hood today:
mailboxes on channels + crew (see `examples/actor.mko`).

### Realtime / telecom priority stack

This track should guide runtime and networking decisions without becoming the
language's entire identity:

| Layer | Priority |
|-------|----------|
| Transport | TCP, UDP, TLS, WebSocket, HTTP/1.1, HTTP/2, HTTP/3, QUIC |
| Telecom | SIP parsing/building, RTP/SRTP packet primitives, session timers |
| Runtime | Lightweight tasks, actors, bounded channels, high-performance timers |
| Memory | Request arenas, pools, ring buffers, zero-copy packet views |
| Observability | Metrics, tracing, profiling, structured logs |
| Deploy | Static binaries, fast cross-compile, Linux/macOS/Windows/ARM/WASM |

The rule of thumb: a SIP proxy, signaling server, session controller, WebSocket
gateway, or job worker should all feel like obvious Mako programs.

---

## Memory

| Mode | When | Status |
|------|------|--------|
| Ownership / scopes | Default | Now |
| `arena` | Request/batch | Now |
| `share` / RC | Opt-in graphs | Next |
| `hold` / manual | Systems escapes | Next |
| Optional GC | App opt-in only | Later |

No mandatory GC. See [SECURITY.md](SECURITY.md).

**Zero-copy:** string region ops (`str_slice_*` / `str_at_eq` / `str_byte_at`) are
**Now** — compare/search `s[off:off+len]` without substring alloc. Packet/file
views, borrowed buffers, and pools continue under Next/Later.

---

## Concurrency & async

| Feature | Status |
|---------|--------|
| `crew` / `kick` / `join` / `fan` | Now |
| Channels | Now |
| `crew.cancel` | Now |
| Actors (mailbox + owned state) | Now (seed) |
| Timeouts (portable) | Now (seed) — `timeout_portable_test` |
| Async I/O without colored functions | Now (seed) — native IO / poll / wait |
| Auto cancellation on scope exit | Now (partial via crew) — full product residual |
| Deterministic scheduling | Later |

Async goal: **normal-looking code**, runtime does I/O; structured concurrency
keeps lifetimes honest.

---

## Actors (first-class)

**Now (seed):**

```mko
actor Counter {
    n: int = 0
    receive Inc { self.n = self.n + 1 }
    receive Bye { let _ = 0 }
}
// Counter_spawn() / Counter_spawn_cap(n) · Counter_send · Counter_loop
```

Owned state + `self.field` in receives; packed mailboxes (tag + optional int
payload: `receive Inc(delta)`); `Bye`/`Stop` ends the loop. Multi-field /
string payloads and supervision remain Later.

---

## Networking

Target builtins: `tcp` / `udp` / `tls` / `dns` / `quic` / `http` / `websocket` / `grpc` with
`.listen()`-style APIs.

| Piece | Status |
|-------|--------|
| `tcp_listen` / `tcp_accept` / `tcp_close` / write helpers | Now |
| `http_serve` / `http_echo` | Now |
| TLS / QUIC / WebSocket / DNS / gRPC | Now (seeds) — depth/product residual |

Backend APIs also need routing, middleware, auth, validation, cookies/sessions,
file uploads, rate limiting, compression, caching, graceful shutdown, health
checks, and background jobs. These should live in stdlib packages or official
extensions before third-party frameworks become mandatory.

---

## Serialization & derive

| Format | Status |
|--------|--------|
| JSON (manual / echo) | Seed |
| XML / YAML / TOML / CSV | Now (YAML/TOML config subset + CSV/XML; full YAML 1.2 / TOML 1.0 Later) |
| Binary / CBOR / MessagePack / Protobuf / Avro | Later |
| `derive(JSON)` / `derive(SQL)` compile-time | Later — no runtime reflect tax |

**Built-in SQL (Later):** typed `select` verified by the compiler. Raw SQL must
always remain available. Database support should cover PostgreSQL, MySQL /
MariaDB, SQLite, Redis, MongoDB-style document stores, Cassandra/ClickHouse, and
Elasticsearch-compatible systems through stdlib or official packages.

---

## Interfaces / contracts

Light **interfaces** — compile-time method sets, dyn dispatch, easy mocks.
**Now (seed):** `interface Adder { fn add(int) -> int }` +
`on Counter : Adder { fn add(self, …) }` (or free `Adder_Counter_add`).
No trait maze; embedding/generic bounds remain Later.

---

## Dependencies & supply chain

| Capability | Status |
|------------|--------|
| `mako.toml` + `mako.lock` | Now (foundation) |
| Version pinning / content hashes | Now (lock stub) |
| Private modules / path deps | Next |
| Reproducible builds | Next |
| Supply-chain / vuln scan (`mako pkg audit`) | Now (offline advisory + license policy) |
| Minimal transitive deps (culture + tooling) | Ongoing |

---

## Security defaults

No null · bounds checks · safe strings · unused Result illegal · secure TLS
defaults (Next) · crypto done right (Next) · vuln checks in pkg (Next).
Details: [SECURITY.md](SECURITY.md).

---

## Debugging & performance tooling

| Tool | Status |
|------|--------|
| Clear runtime abort messages | Now |
| Stack traces / debugger | Next |
| Race / deadlock / leak detectors | Next |
| CPU / memory / alloc profiler | Next |
| Compiler hints (avoidable allocs, useless locks, escape analysis) | Later |
| Deterministic latency / scheduling | Later |

Observability is part of the language product, not an afterthought: backend
programs should get cheap counters, spans, structured logs, and profiling hooks
without pulling in a large framework.

---

## Testing (built-in)

Unit · integration · fuzz · bench · race · property · snapshot · mocks —
**Next** (`mako test` / `mako bench` stubs exist).

Coverage, fixtures, parallel tests, leak/race detection, and end-to-end test
helpers are part of the general-purpose backend bar.

---

## Compatibility

Strong backward compat, clear versioning (**0.1.9** tip), stable stdlib, no constant breaks.
API stability annotations — Later. Module isolation — Later.

---

## Tooling & IDE

| Tool | Status |
|------|--------|
| `mako check` / `build` / `run` | Now |
| `mako fmt` / `lint` / `test` / `bench` | Stub |
| `mako pkg init` / `lock` / `audit` | Now |
| Docs generator | Later |
| LSP: rename / extract / debug / profile | Later (day-one intent) |
| Cross-compile | Later |
| Incremental compile (1M LOC < 2s rebuild) | Later |
| WASM target | Later |
| Runtime introspection / AI compiler metadata hooks | Later |
| Domain extensions without fragmentation | Later |

The official toolchain should be a single coherent product: compiler, package
manager, formatter, linter, tests, benchmarks, docs, dependency auditor,
profiler, debugger integration, LSP, cross-compiler, and build system — all
working together out of the box.

---

## Systems / SIMD / GPU / DB / comptime

| Theme | Status |
|-------|--------|
| Systems audience (drivers, engines, protocols) | Vision Now |
| SIMD vector types | Later |
| GPU AI seed (matmul/activations; OpenCL multi-vendor + host) | Seed done — build inference blocks in Mako |
| Local models (safetensors + author nets + .makomodel) | Seed done |
| AI depth (attention, layernorm, GGUF/LLM runtime, tokenizer) | Later |
| `gpu fn` / Metal-native / CUDA / Vulkan | Later (same buffer/dispatch surface) |
| DB engine primitives (storage/index/tx/cache) | Later |
| Comptime `const x = scan(...)` | Later |

---

## Deployment

Single static binaries · small containers · easy cross-compile · fast startup ·
low memory · deploy awareness in tooling (Later).

Targets: Linux, Windows, macOS, FreeBSD, ARM, x86-64, RISC-V, and WebAssembly.
Applications should fit single binaries, minimal containers, system services,
serverless functions, edge apps, desktop utilities, and embedded agents.

---

## Prioritized roadmap

1. ~~Channels + cancel~~ · ~~actor mailbox seed~~ · ~~tcp_listen~~ · ~~pkg lock foundation~~  
2. Full `actor` / `receive` syntax · portable timeouts · real `http.Request` + TLS  
3. `hold` / `share` · interfaces · `mako test` for real  
4. JSON derive · Postgres · race detector  
5. Async I/O · QUIC/WS · incremental compile · LSP  
6. Optional GC · SIMD/GPU · comptime · WASM  

---

## Related

[LANGUAGE.md](LANGUAGE.md) · [STDLIB.md](STDLIB.md) · [SECURITY.md](SECURITY.md) · [ROADMAP.md](ROADMAP.md)
