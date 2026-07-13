# Mako roadmap (ordered)

Short engineering queue. Product map: [VISION.md](VISION.md). Updated backend/game
vision source: internal design document (not tracked in repo).
**Verified:** [STATUS.md](STATUS.md) · **Stdlib:** [STDLIB.md](STDLIB.md) · **Release:** [RELEASE.md](RELEASE.md).  
**Book:** [The Mako Book](book/).

STATUS north-star / MVP: **100%**. Prefer STATUS over this list when claiming Done.  
**Mako identity:** [IDENTITY.md](IDENTITY.md) (**~90%**).  
**Pain map (Go/Rust → Mako):** [PAIN_POINTS.md](PAIN_POINTS.md).  
Dual sugar only: [GO_SYNTAX_CHECKLIST.md](GO_SYNTAX_CHECKLIST.md).  
Last roadmap sync: **2026-07-11** (unique surface · pain-driven design · suite **130+**).

## Landed

- Compiler → C → native; `.mko`; crew / actors / arenas / Result  
- Mako operators + grouped `import (` · `mako version` · `mako test`  
- Core stdlib + Waves 1–9 standard library  
- Suite **130**  
- **The Mako Book** (`docs/book/`)

## Done this pass (docs book + Wave 9 + CLI)

| Area | Status |
|------|--------|
| The Mako Book — 15 chapters + checkable samples + `book.toml` | Done |
| Docs accuracy pass (README / GUIDE / STATUS / howto) | Done |
| RE2 backrefs + `\p{L/N}` ASCII + POSIX class polish | Done |
| UTF-8 regexp `\p{...}` common scripts/categories + lookahead | Done |
| JFIF grayscale encode / detect | Done |
| Codegen `mako_reflect_register_type` for Mako structs | Done |
| SMTP STARTTLS soft + `str_cut`/`str_count` | Done |
| `main.rs` help: flag docs, `version` ordering, after_help | Done |

## Partial / Next (true hard residuals)

**Landed (gap close waves 1–30):** join_timeout flatten · POD+string kick ·
reflect N + nested POD · Result/Option deep nests · nested None/Err ·
**jpeg_is_baseline_gray** · JFIF major/minor/density · SOF0 sampling/Ci ·
`jpeg_has_app7` · NLL for/if/match · more script `\p{…}` · expanded TSan ·
prior work.

**Language pain residuals** (still open — see [PAIN_POINTS.md](PAIN_POINTS.md)):

1. Fuller data-race model beyond expanded TSan  
2. More Result/Option shapes (remaining edge cases)  
3. Stronger NLL product cases (rarer multi-label CFG)  

**Stdlib / product residuals:**

6. Complete Unicode / PCRE · JPEG viewer parity (JFIF density/APP7 shell; Huffman residual)  
7. Reflect for non-POD field types (maps/slices/chan/Option/Result rejected; content residual)  
8. Symbol-level parity  

## Product Focus From General-Purpose Brief

The next language/runtime bets should reinforce the new product contract:
general-purpose backend and infrastructure first, with telecom as one domain
track rather than the language identity.

1. Backend app surface: routing, middleware, validation, auth/session helpers,
   graceful shutdown, health checks, background jobs.
2. API protocols: REST/OpenAPI, GraphQL, gRPC, WebSockets, SSE, streaming RPC,
   API clients, schema validation.
3. Data layer: PostgreSQL/MySQL/SQLite/Redis first, pooling, transactions,
   migrations, prepared statements, typed SQL checks.
4. CLI/devtools: subcommands, config files, env/secrets, shell completion,
   terminal formatting, cross-platform file/process APIs.
5. Cloud/infrastructure: agents, sidecars, service discovery, container tooling,
   Kubernetes operator/controller patterns.
6. Runtime trust: structured concurrency, cancellation, timeouts, leak/deadlock
   diagnostics, scheduler observability, race detection.
7. Observability/debugging: structured logs, metrics, tracing/OpenTelemetry,
   CPU/memory/allocation profiling, stack traces, debugger/LSP depth.
8. Domain tracks: telecom/realtime (SIP/RTP/SRTP/WebRTC), storage systems,
   AI inference, games/game engines, simulations, edge/WASM, plugin/ABI support.
9. Deployment: static binary defaults where practical, cross-compile polish,
   minimal containers, serverless/edge targets, WASM progression.

## General-Purpose Intention Tracker

This is the checklist for reaching **100% of the product intention**, not just
the current MVP/STATUS bar. Percentages are weighted by product importance and
should be updated whenever a task is checked off.

**Overall intention completion:** **~81% / 100%**  
Weighted from the track table below; STATUS remains the MVP implementation bar.  
**Mako identity (preferred syntax):** **~86%** — [IDENTITY.md](IDENTITY.md).

| Track | Weight | Current |
|-------|--------|---------|
| 1. Language identity and core type system | 10% | 90% |
| 2. Memory safety and allocation control | 10% | 75% |
| 3. Concurrency and runtime trust | 10% | 55% |
| 4. Backend app surface | 12% | 100% |
| 5. API protocols and networking | 10% | 100% |
| 6. Data, SQL, and serialization | 10% | 100% |
| 7. Toolchain, packages, and IDE | 10% | 100% |
| 8. Observability and debugging | 8% | 48% |
| 9. Installer, distribution, and portability | 10% | 77% |
| 10. Domain tracks and advanced systems | 10% | 66% |

### 1. Language Identity And Core Type System — 10%

- [x] Define Mako-owned syntax identity; has its own identity.
- [x] Static types, local inference, `Result`, `Option`, enums, `match`.
- [x] Interfaces seed and dynamic interface dispatch.
- [x] User generics with monomorphization (`fn id[T](x: T) -> T`; dual `[]`/`<>` for built-ins).
- [x] Unique Mako surface preferred; dual sugar only (`func`, `:=`, …) — [IDENTITY.md](IDENTITY.md).
- [x] Packs/pulls: `pack` / `pull` (dual `package` / `import`).
- [x] Tuples + multi-return: `(int, int)` + `let a, b = f()`.
- [x] Explicit `export`; opt-in `visibility = "explicit"`.
- [x] Typed channels: `chan_open[T]` / `make(chan[T], n)`.
- [x] Pain map: [PAIN_POINTS.md](PAIN_POINTS.md) — design driven by Go/Rust pain, not clones.
- [ ] Close language pain residuals (races, richer errors, NLL, visibility, identity lint).
- [ ] `if init; cond { }` Go if-with-init.
- [ ] `go f()` sugar → kick inside crew.
- [ ] Compiler-enforced API stability annotations.
- [ ] Richer pattern matching over structs/errors/messages beyond tuples/enums.

### 2. Memory Safety And Allocation Control — 10%

- [x] No null by default; explicit `Option`.
- [x] Scope ownership, `arena`, `hold`, `share` seed, CFG/NLL checks.
- [x] Debug bounds checks and explicit `unsafe` blocks.
- [x] Release safety profile: `[profile.release] bounds_checks = "on"` (default unchanged).
- [x] Memory pools and reusable buffers as first-class stdlib/runtime tools.
- [x] Borrowed string/byte views and zero-copy packet/file APIs.
- [ ] Optional GC for app workloads only, never mandatory.
- [x] Leak detector and allocation reporting.

### 3. Concurrency And Runtime Trust — 10%

- [x] `crew`, `kick`, `join`, channels, cancel seed, `fan`.
- [x] Actor runtime seed.
- [ ] Full first-class `actor` / `receive` syntax with owned state.
- [ ] Portable timeouts and deadlines across task/channel/network APIs.
- [ ] Structured error propagation from child tasks.
- [ ] Explicit detached-task syntax and lifecycle controls.
- [x] Backpressure primitives and bounded queues.
- [ ] Race, leak, and deadlock diagnostics.
- [x] Scheduler observability.

### 4. Backend App Surface — 12%

- [x] Typed `HttpRequest` parse/accessors.
- [x] Route pattern helpers: `http_route_match`, `http_route_param`.
- [x] Router package with grouped routes and handlers.
- [x] Middleware pattern and request context.
- [x] Request validation helpers.
- [x] Authentication and authorization helpers.
- [x] Sessions, cookies, CSRF-safe defaults.
- [x] Multipart/file upload polish.
- [x] Rate limiting, compression, caching.
- [x] Health checks, readiness/liveness helpers.
- [x] Graceful shutdown.
- [x] Background jobs and scheduling.

### 5. API Protocols And Networking — 10%

- [x] TCP, HTTP/1.1, HTTP/2, gRPC-ish unary/stream seeds, H3 client pieces.
- [x] TLS/QUIC/WebSocket seeds.
- [x] UDP and Unix sockets.
- [x] DNS package polish.
- [x] Production-grade WebSocket server/client APIs.
- [x] OpenAPI metadata/generation.
- [x] GraphQL server/client seed.
- [x] Server-sent events and streaming RPC helpers.
- [x] Connection pooling and load-balancing primitives.
- [x] Backpressure-aware network I/O.

### 6. Data, SQL, And Serialization — 10%

- [x] JSON, CSV/XML seeds, binary/protobuf/gob helpers, SQLite/Postgres seeds.
- [x] Local embedded KV example.
- [x] SQL connection pooling.
- [x] Transactions and prepared statements across drivers.
- [x] Migrations.
- [x] Typed SQL checker for table/column/param/nullability/return types.
- [x] MySQL/MariaDB and Redis polish.
- [x] MongoDB/Cassandra/ClickHouse/Elasticsearch-compatible packages.
- [x] YAML, TOML, MessagePack, CBOR, Avro.
- [x] Compile-time serialization/codegen without runtime reflection tax.

### 7. Toolchain, Packages, And IDE — 10%

- [x] `check`, `build`, `run`, `fmt`, `test`, package manifest/lock seed.
- [x] Incremental/object cache and parallel build jobs.
- [x] Minimal LSP seed.
- [x] VS Code extension scaffold/package manifest.
- [x] VS Code syntax highlighting / TextMate grammar for `.mko`.
- [x] VS Code language configuration: comments, brackets, auto-close pairs.
- [x] VS Code tasks/commands for `mako check`, `mako build`, `mako run`, `mako test`.
- [x] VS Code problem matcher for Mako diagnostics.
- [x] VS Code debug adapter story: launch/run binary, attach native debugger where supported.
- [x] VS Code extension command palette actions: format, test file/package, initialize project.
- [x] Full VS Code LSP client integration and install/discovery wiring.
- [x] Package registry protocol, private registries, offline builds.
- [x] Dependency audit with vulnerability and license checks.
- [x] Coverage, fuzzing, property tests, snapshots, mocks, fixtures.
- [x] Benchmark runner with stable reporting.
- [x] Documentation generator with runnable examples and search.
- [x] LSP autocomplete, go-to-definition, references, rename, code actions.
- [x] Compiler JSON diagnostics, symbol graph, AST/type metadata for AI tools.
- [x] API breaking-change detector.

### 8. Observability And Debugging — 8%

- [x] Basic logs/slog helpers and clear runtime abort messages.
- [x] Structured logging package with redaction controls.
- [x] Metrics counters/gauges/histograms.
- [x] Wall-clock compile/run profile reports with stable JSON output.
- [ ] Distributed tracing and OpenTelemetry export.
- [ ] CPU, memory, allocation, scheduler, and lock-contention profiling.
- [ ] Stack traces with source locations.
- [ ] Debugger integration: locals, breakpoints, async task inspection.
- [ ] Crash reports and core dump support.
- [ ] Profile-guided optimization and link-time optimization workflow.
- [x] Runtime introspection endpoint/hooks.

### 9. Installer, Distribution, And Portability — 10%

- [x] Native single binary via C/clang, install scripts, release docs.
- [x] Cross-target flag and WASI preview1 seed.
- [ ] Complete installer UX for macOS, Linux, and Windows.
- [x] One-command install script with version selection and checksum verification.
- [ ] Windows MSI / winget package.
- [ ] macOS pkg or signed/notarized installer.
- [ ] Linux deb/rpm packages and apt/yum repository metadata.
- [ ] Homebrew formula publish and update automation.
- [x] Installer installs compiler, runtime headers, stdlib, and VS Code extension scaffold.
- [x] `mako doctor` to validate binary, clang/zig, runtime, stdlib, and VS Code scaffold.
- [x] Uninstall scripts for Unix and Windows.
- [x] `mako update --from <checkout> --prefix <prefix>` refresh flow.
- [x] Release archives include runtime, stdlib, docs, VS Code scaffold, install/uninstall scripts, and checksums.
- [x] Release archives include the full internal docs tree, book, howtos, and top-level release notes.
- [x] Release archive install smoke: install from unpacked artifact and pass `mako doctor`.
- [x] Release workflow smoke-tests packaged Unix/Windows installers before upload/publish.
- [x] Static binary defaults where practical.
- [x] CI target smoke for Windows GNU cross-build and static Linux musl output.
- [ ] Reliable Linux/macOS/Windows/FreeBSD matrix.
- [ ] ARM, x86-64, RISC-V target validation.
- [ ] Console/platform-specific toolchain path where licensing permits.
- [x] Minimal container generation.
- [x] Serverless and edge deployment helpers.
- [x] WASI preview2 and browser/edge WASM story.
- [x] Stable ABI, dynamic libraries, native plugins, WASM plugins.

### 10. Domain Tracks And Advanced Systems — 10%

- [x] Storage engine example, HTTP/H2/H3/gRPC/QUIC seeds.
- [ ] Telecom/realtime: SIP, RTP, SRTP, SIPREC, Diameter, WebRTC.
- [x] Game-loop and fixed-timestep simulation primitives.
- [x] Frame allocators, object pools, and allocation tracking for games.
- [x] ECS seed: entities, components, systems, archetype/query basics.
- [ ] Graphics/windowing seed: windows, input, render-command abstraction.
- [ ] Graphics backend roadmap: Vulkan, Metal, Direct3D, OpenGL, WebGPU.
- [ ] Shader pipeline: modules, typed uniforms, SPIR-V/HLSL/GLSL/WGSL/Metal validation.
- [ ] Asset pipeline seed: asset packing, dependency tracking, incremental builds, hot reload.
- [ ] Audio seed: no-allocation callback helpers, mixing graph primitives.
- [ ] Physics/simulation seed: collision, rigid-body hooks, deterministic multithreaded stepping.
- [ ] Multiplayer game networking: reliable UDP, snapshot replication, prediction/rollback.
- [x] Deterministic simulation: fixed-point math, deterministic RNG, replay streams.
- [x] Finite-state-machine helpers for session systems.
- [x] Ring buffers, lock-free queues, scatter/gather I/O.
- [ ] Database/storage primitives: pages, WAL, indexes, cache, transactions.
- [ ] AI inference service helpers: model loading, batching, accelerator hooks.
- [ ] SIMD portable vector APIs.
- [ ] GPU/accelerator optional track.
- [ ] Interop beyond C: bridges to other languages.
- [ ] Hot code reload with state-preserving editor iteration.
- [ ] Compile-time execution and safe domain extensions.

## Later (VISION)

- Optional GC · SIMD/GPU · deep comptime · browser DOM  
- In-process H3 server · WASI sockets/preview2 · LSP depth  

## External (user)

1. Publish Homebrew / **homebrew-core**
