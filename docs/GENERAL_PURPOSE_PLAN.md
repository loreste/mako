# Mako general-purpose backend plan

Source brief: `General_Purpose_Backend_Language_Vision.md`.

**Product tip:** **0.2.0**. Next product version on the roadmap: **0.2.0**
(stdlib in Mako). See [ROADMAP.md](ROADMAP.md) · [STATUS.md](STATUS.md).

Mako's product contract is:

> A simple, safe, fast, and versatile language for backend software, cloud
> infrastructure, networking systems, developer tools, databases, and
> high-performance services.

Telecom and realtime systems are important proving grounds, but not the whole
identity.

## Implementation Ladder

| Wave | Goal | Concrete outcomes |
|------|------|-------------------|
| 1 | Backend app surface | Router helpers (`http_route_match`, `http_route_param`) started; middleware pattern, validation, auth/session seeds, graceful shutdown, health checks |
| 2 | API protocols | REST/OpenAPI metadata, GraphQL seed, gRPC polish, WebSockets, SSE, streaming RPC |
| 3 | Data layer | Pooling, transactions, prepared statements, migrations, typed SQL checks, PostgreSQL/MySQL/SQLite/Redis first |
| 4 | CLI/devtools | Subcommands, config files, secrets/env, shell completion, terminal formatting |
| 5 | Runtime trust | Stronger structured concurrency, cancellation/timeouts, leak/deadlock diagnostics, scheduler introspection |
| 6 | Observability/debugging | Structured logs, metrics, tracing/OpenTelemetry, CPU/memory/alloc profiling, stack traces |
| 7 | Cloud/infrastructure | Operator/controller patterns, sidecars, service discovery, container tooling, monitoring agents |
| 8 | Systems/data/realtime tracks | Storage primitives, zero-copy views, ring buffers, SIP/RTP/SRTP/WebRTC, AI inference hooks |
| 9 | Toolchain maturity | Docs/tests/bench/coverage/audit/profiler/debugger/LSP as one coherent official toolchain |
| 10 | Portability/deploy | Static binary defaults, cross-compile polish, minimal containers, serverless/edge/WASM, plugin/ABI support |

## Non-Negotiables

- Mako-owned syntax is required: familiar enough to learn quickly, but not a
  clone of any single existing language.
- Simplicity and fast iteration stay central.
- No mandatory GC.
- Safe defaults: no null, checked conversions, explicit unsafe, typed errors.
- Fine-grained ownership control exists, but normal backend services should not
  require lifetime expertise.
- Raw escape hatches remain available for systems work.
- Compile-time generation should replace heavy runtime reflection where possible.

## Success Bar

Mako should be comfortable for both:

- a beginner shipping a small API or CLI, and
- an expert building a database, proxy, compiler, distributed runtime, or
  realtime protocol system.
