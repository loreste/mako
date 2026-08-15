# Mako product focus

Mako's product wedge is **safe, fast backend services with structured concurrency**.

That is the workflow that should become boringly reliable before the project
adds more visible surface area:

1. Build an HTTP or TCP service with `mako init --backend`.
2. Use `Result`, `Option`, checked indexing, and parameterized SQL without
   ceremony.
3. Use `crew`, `kick`, `join`, bounded channels, and request arenas for
   concurrent work.
4. Build a native binary with no GC or VM.
5. Measure throughput, latency, RSS, allocation pressure, channel behavior, and
   compile time with repeatable scripts.

Everything else is secondary until this path is dependable across the primary
platforms and backends.

## First-class

These areas define the product promise:

| Area | Bar |
|------|-----|
| Language | Mako-native syntax, static types, `Result`/`Option`, match, ownership checks |
| Runtime | No tracing GC, safe release bounds checks, deterministic cleanup |
| Concurrency | `crew`/`kick`/`join`, channels, cancellation, no orphan ordinary tasks |
| Backend services | HTTP, TLS, JSON, file/env, logs, metrics, SQL, graceful shutdown |
| Tooling | `check`, `build`, `run`, `fmt`, `test`, LSP basics, reproducible benchmark gates |

## Supported But Not The Identity

These are useful, but they should not drive the public story:

| Area | Position |
|------|----------|
| Go-compat syntax | Migration sugar; docs and formatter prefer Mako forms |
| Telecom/protocol packs | Real examples of backend systems, not the language identity |
| GPU, storage, plugins, local AI | Domain tracks and prototypes until marked stable in `MATURITY.md` |
| WASM | Useful deployment target; not the current wedge |

## Decision Rule

When adding a feature, ask:

1. Does it make the backend-service path safer, faster, or easier to debug?
2. Can it be tested with negative fixtures and a repeatable benchmark or smoke?
3. Does it keep the Mako-native surface clearer than the compat surface?
4. Can it land without adding another large ad hoc branch to the typechecker or
   code generator?

If the answer is no, put it in a domain-track document instead of the core
product path.
