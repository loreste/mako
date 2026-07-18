# Mako roadmap

**Product version:** **0.2.3** · Last roadmap sync: **2026-07-18**.

**Verified:** [STATUS.md](STATUS.md) · **Stdlib:** [STDLIB.md](STDLIB.md) · **Security:** [SECURITY.md](SECURITY.md) · **Release:** [RELEASE.md](RELEASE.md).  
**Book:** [The Mako Book](book/) · **Identity:** [IDENTITY.md](IDENTITY.md) · **Pain map:** [PAIN_POINTS.md](PAIN_POINTS.md).

---

## What is next

| Version | Theme | Status |
|---------|-------|--------|
| **0.1.9** | Generics & iterators | **Shipped** |
| **0.1.10** | Deepen generics + speed | **Shipped** |
| **0.2.0** | Stdlib written in Mako | **Shipped** |
| **0.2.1** | Safety & correctness | **Shipped** |
| **0.2.2** | TLS SNI / HTTPS / JWT / lock integrity | **Shipped** |
| **0.2.3** | JWT/HTTPS input hardening | **Shipped** |
| **0.2.4** | Tooling (LSP depth) | **Next** |
| **0.3.0** | Cross-platform | Planned |
| **0.4.0** | Performance ceiling | Planned |
| **1.0** | Stability | Planned |

---

## v0.1.10 — Deepen generics + speed — **shipped**

Resolved the two blockers that prevented the stdlib rewrite.

| Feature | Status | Description |
|---------|--------|-------------|
| **Multi-statement lambda bodies** | Done | Lambdas support `let`, assignments, `if`/`else`, `while`, nested loops. Blocks emitted as full C function bodies. |
| **`mut self` on methods** | Done | `fn m(mut self)` passes receiver by pointer. Mutations persist in caller. Enables real iterators. |
| **Generic enum variant disambiguation** | Done | Multiple instantiations of same generic enum no longer collide on variant names. Qualified lookup by return type context. |
| **Tuple channel codegen** | Done | `chan[(int,int,int,int,int)]` send/recv works. Required by leba 0.6+. |
| **`chan_len` / `chan_cap` for all channel types** | Done | Works on int, float, string, struct, enum, tuple channels. |
| **Speed: wyhash** | Done | Map key hashing 4-8x faster. |
| **Speed: stack f-strings** | Done | 256B stack buffer, zero malloc for short strings. |
| **Speed: constant folding** | Done | `1 + 2` folded at compile time. |
| **Speed: zero-copy strings** | Done | Comparisons, match arms, print, str_eq, str_has_prefix use `mako_str_view`. |
| **Speed: select condvar** | Done | Channel select wakes on send, not 2ms polling. |
| **Speed: emit_line** | Done | Codegen hot paths use `format_args!` — no per-line heap allocation. |

Tests: `multi_stmt_lambda_test`, `mut_self_test`, `generic_enum_multi_test`, `v0110_adversarial_test`.

### Known limitations

- `return` inside multi-statement lambda bodies triggers a type error (type checker uses enclosing function's return type). Workaround: use `let mut out = ...; return out` pattern.
- Wrong-count type args on generic structs produce confusing error messages.
- Generic structs can't be used as map keys yet.

---

## v0.1.9 — Generics & Iterators — **shipped**

The foundation for everything that follows.

| Feature | Status | Description |
|---------|--------|-------------|
| **Generic structs** | Done | `struct Pair[T] { a: T, b: T }` — monomorphized; multi-param; nested; in generic fns |
| **Generic enums** | Done | `enum MyBox[T] { Val(T), Nothing }` — monomorphized; match works |
| **Interface bounds** | Done | `fn f[T: Describable](x: T)` — structural method-set check; compile error on violation |
| **Iterator protocol** | Seed | Types with `next() -> Option[T]` recognized as iterables; by-value self limits mutation |
| **Mutable closures** | Seed | Heap-cell infrastructure built; needs multi-statement lambda bodies |

Tests: `generic_struct_test`, `generic_enum_test`, `generic_bounds_test`,
`generic_adversarial_test`, `iterator_test`, `mutable_closure_test`,
`examples/bad/generic_bound_fail.mko`.

### Ecosystem: Leba load balancer

[Leba](https://github.com/loreste/leba) (v0.7.0) is an independently maintained
Mako application and systems-programming showcase. Its deployment and
production claims belong to the Leba repository; this roadmap does not use it
as evidence that every Mako program or the Mako toolchain is production-ready.
It exercises channels, TLS, HTTP proxying, structured concurrency, and the
networking stdlib. Recent work:

- Session cookie authentication with HMAC-signed tokens
- Full admin dashboard (standalone HTML/CSS/JS) with RBAC
- Proxy host management UI (add domains, servers, backends via browser)
- Analytics dashboard (top paths, status breakdown, request rate chart)
- Access log file output, per-client rate limiting, CORS, header rules
- Config API with secret redaction, disabled write for security

---

## v0.2.0 — Stdlib in Mako

The standard library moves from C runtime wrappers to real Mako code. The stdlib
must be idiomatic Mako and serve as example code for the community.

| Feature | Description |
|---------|-------------|
| **io.Reader / io.Writer** | Composable I/O interfaces — bufio, compression, TLS all work through them |
| **Generic collections** | `List[T]`, `Set[T]`, `Queue[T]`, `PriorityQueue[T]` written in Mako |
| **encoding/json** | Struct-aware marshal/unmarshal using reflect — written in Mako, not C |
| **net/http middleware** | Handler chains, request context, streaming bodies |
| **context cancellation** | Deadline propagation, cancel trees, timeout scoping |
| **database/sql pool** | Connection pooling, prepared statements, transactions |

---

## v0.2.1 — Safety & Correctness — **shipped**

Close the gap between what Mako promises and what it verifies.

| Feature | Description |
|---------|-------------|
| **Ownership verification** | Static use-after-move analysis — compiler error, not runtime crash |
| **Lifetime tracking** | Prevent dangling pointers from sub-slices and borrowed views |
| **Compile-time race safety** | Safe Mako rejects unsynchronized mutable closure captures and unknown function environments across every `kick`; `fan` mappers are capture-free; nested field/index writes are checked |
| **Match exhaustiveness** | Compiler error when match arms do not cover all enum variants |
| **Match guards** | `Ok(n) if n > 0 => ...` — boolean conditions on match arms |
| **Nested destructuring** | `Some(Point { x, y }) => ...` — destructure through multiple layers |

---

## v0.2.2 — TLS · HTTPS · JWT · lock integrity — **shipped**

| Feature | Description |
|---------|-------------|
| **SNI multi-cert** | Concurrent-safe set rebuild; `sni_add` / `sni_update` / `sni_remove` |
| **HTTPS client** | `https_get` / `https_post` / `https_request` + last status/headers |
| **OIDC helpers** | `oidc_discovery` / `oidc_token` |
| **JWT RS256 / JWKS** | `jwt_verify_rs256` / `jwt_verify_jwks` |
| **pkg lock v2** | Deterministic SHA-256 integrity on install |

---

## v0.2.3 — JWT / HTTPS input hardening — **shipped**

| Feature | Description |
|---------|-------------|
| **Strict JWT JSON** | Numbers/literals/objects/arrays with depth limit; reject trailing junk |
| **JWKS fail-closed** | Malformed JSON and unknown primitives no longer skip as metadata |
| **JWT sign safety** | Payload size cap, HMAC length checks, free signature buffers |
| **HTTPS inputs** | Dead helper cleanup; dual-stack HTTP listen; hardened TLS live tests |

---

## v0.2.4 — Tooling

Developer-experience hardening.

| Feature | Description |
|---------|-------------|
| **LSP: find-all-references** | Across files, respecting imports |
| **LSP: rename refactoring** | Safe symbol rename across the project |
| **LSP: signature help** | Parameter hints as you type function calls |
| **LSP: inlay hints** | Show inferred types inline |
| **Debugger** | Source-level breakpoints in `.mko` files, step through Mako lines, inspect variables |
| **Package registry** | `mako publish` / `mako install` from a central registry |
| **Dependency solver** | Version conflict resolution with integrity hashes |

---

## v0.3.0 — Cross-Platform

Every target tested in CI, not just scripts.

| Feature | Description |
|---------|-------------|
| **Windows** | All tests pass in CI, native threading (not pthread shims), IOCP networking, MSI installer |
| **WASM** | Browser target with DOM bindings, no POSIX deps, WASI Preview 2 component model |
| **ARM / RISC-V** | Tested in CI via QEMU or real hardware, cross-compilation from x86 works |

---

## v0.4.0 — Performance Ceiling

Move beyond what the C backend can give.

| Feature | Description |
|---------|-------------|
| **IR layer** | Intermediate representation between AST and C — enables language-aware optimizations |
| **Dead code elimination** | Import-aware reachability — only emit code that is actually used |
| **Escape analysis** | Stack-allocate values that do not escape their scope |
| **Interface devirtualization** | Inline interface calls when the concrete type is known |
| **Closure inlining** | Inline small closures at call sites |
| **LLVM backend** | Optional direct LLVM IR emission for targets where clang is slow or unavailable |

---

## v1.0 — Stability

| Feature | Description |
|---------|-------------|
| **Syntax frozen** | No breaking changes to the language |
| **Stdlib API stable** | Semver guarantees on all public symbols |
| **Self-hosting compiler** | The Mako compiler written in Mako |
| **Formal memory model** | Documented guarantees for concurrent access |
| **Ecosystem** | Package registry with community packages, IDE plugins, CI templates |

---

## What shipped recently

### 0.1.8 — Speed & memory safety

- wyhash for map keys (4-8x faster than FNV-1a)
- Stack-based f-string builder (zero malloc for short strings)
- Compile-time constant folding for integer expressions
- Zero-copy string comparisons, match arms, and print
- Channel select uses condvar wake (not 2ms polling)
- HTTP connection table scaled to 1024 with atomic counter
- HTTP header interning via length-bucketed dispatch
- Codegen emit_line with format_args (no per-line String allocation)
- Lock-free chan_cap, codegen monomorph cache

---

## Just closed (2026-07-15) — const fn strings

| Area | Status |
|------|--------|
| `const fn f(s: string) -> string` | **Done seed** — shout/greet/pick |
| Int const fn with string locals | **Done seed** — `len_greet` |
| Full CTFE (heap, mutate, index, loops on strings) | Still product residual |

## Just closed (2026-07-15) — const string seed

| Area | Status |
|------|--------|
| `const S = "…"` / `+` concat | **Done seed** |
| `str_len` / `len` / `==` / `!=` / `str_eq` | **Done seed** → int fold |
| Full CTFE strings (mutate, index, heap) | Still product residual |

## Just closed (2026-07-15) — const-fn break/continue

| Area | Status |
|------|--------|
| Const bare `break` / `continue` | **Done seed** — while / for / C-for · `TestConstFnBreakContinue` |
| C-for `continue` runs post | **Done** (Go/C semantics) |
| Labeled break/continue in const | Not yet (runtime labels still work) |

## Just closed (2026-07-15) — const-fn for

| Area | Status |
|------|--------|
| Const `for i in n` / `for i in range n` | **Done seed** — count 0..n-1 · `TestConstFnFor` |
| Const C-style `for init; cond; post` | **Done seed** — let/assign init + post |
| Domain CTFE product | Still open (strings, heap, collection range) |

## Just closed (2026-07-15) — const-fn depth

| Area | Status |
|------|--------|
| Const `match` (int / `\|` / `_` / bind) | **Done seed** — `const_fn_test` |
| Const `while` + assign (≤100k iters) | **Done seed** — `sum_to` / `pow2` fold |
| Domain CTFE product | Still open (strings, heap, unlimited loops) |

## Just closed (2026-07-15) — actor int payload

| Area | Status |
|------|--------|
| Actor message payload seed | **Done** — `receive Inc(delta)` packs tag+int · `actor_pack` / `msg_tag` / `msg_payload` |
| Existing no-payload actors | Unchanged surface (`Counter_Inc()` packs payload 0) |

## Just closed (2026-07-15) — implicit interfaces

| Area | Status |
|------|--------|
| Go-like method sets | **Done** — `on T` / `T_m` implements I without `on T : I` · `iface_implicit_test` |
| Dual-form checklist | **~94%** — remaining open item is intentional `*T`/`&x` (won't) |

## Just closed (2026-07-15) — package-per-dir · rendezvous

| Area | Status |
|------|--------|
| Package-per-directory model | **Done** — multi-file merge · pack name check · path dep + pull |
| Unbuffered rendezvous channels | **Done** — `chan_new(0)` handoff · `chan_rendezvous_test` |

## Just closed (2026-07-15) — seeds & syntax

| Area | Status |
|------|--------|
| Error chain peel + tag helpers | **Done seed** — `error_unwrap` / `root` / `as_tag` / `has_tag` · `error_chain_test` · `std/errors` |
| `fallthrough` switch dual | **Done seed** — `fallthrough_test` |
| IDENTITY errors track | **100%** — richer than stringly defaults |

## Just closed (2026-07-14)

| Area | Status |
|------|--------|
| Demand-driven map/bag monomorphs (O(used), not N² grid) | **Done** — large packs stay usable |
| Nested bag / Option / Result / tuple map values | **Done** — suite coverage |
| **P1 — Runtime trust** | **Done seed** — timeouts, crew errors, detach, actors |
| **P2 — Stdlib / security product polish** | **Done** |
| → `path_file_size` | Done |
| → PEM helpers (`pem_*` + `crypto.x509`) | Done |
| → mTLS + cert lab (`tls_make_self_signed` / `tls_make_csr` / `tls_server_reload`) | Done |
| → SCRAM-PLUS adoption (`scram_tls_unique_cbind` / `scram_plus_client_final_bare`) | Done |
| → Docs: **crypto core only** (no high-level SASL state machine) | Done |
| → Observability: `metrics_export_prom`, `trace_export_json` | Done (seed depth) |
| **P2 — Observability depth** | **Done seed** |
| → OTLP/HTTP JSON (`trace_export_otlp_json` / `metrics_export_otlp_json`) | Done |
| → Profile snapshot + RSS/CPU + lock_wait counters | Done |
| → `stack_trace` / `crash_report_install` | Done |
| → PGO/LTO env workflow | Done |
| Tests | `security_product_test` · `observability_depth_test` |

---

## Landed (foundation — do not re-open)

- Compiler → C → native; `.mko`; crew / actors / arenas / `Result` / `Option`
- Mako operators · packs/pulls · `mako version` / `test` / `check` / `build` / `run`
- Core stdlib + Waves 1–9 · suite **165+**
- **The Mako Book** (`docs/book/`)
- Full map/slice/bag *language* surface with demand-driven monomorph emission
- Backend app surface, API protocols, SQL/data, toolchain/IDE tracks at intention **100%**
- TLS/HTTP/2/H3/QUIC seeds · crypto digests/AEAD/KDF/SCRAM core · session/auth helpers

---

## Next (ordered queue)

Work below is **not** MVP. Order is product leverage, not strict dependency.

### P1 — Runtime trust (concurrency)

Highest remaining risk for production backends.

1. ~~Portable timeouts and deadlines~~ **Done seed** — `timeout_portable_test`  
2. ~~Structured child error propagation~~ **Done seed** — `crew.first_err` / `err_count` / `wait` (`crew_error_prop_test`)  
3. ~~Detached-task lifecycle~~ **Done seed** — `detach f()` + `detached_join_all()` (`detach_test`)  
4. ~~Actor / receive + owned state~~ **Done seed** — fields + `self.x` (`actor_test`)

### P2 — Observability depth

Metrics/prom + span-lite JSON are in; **depth seeds landed** (2026-07-14).

1. ~~Full OpenTelemetry export (OTLP wire)~~ **Done seed** — `trace_export_otlp_json` / `metrics_export_otlp_json` (OTLP/HTTP JSON; not protobuf)  
2. ~~CPU / memory / allocation / scheduler / lock-contention profiling~~ **Done seed** — `profile_snapshot_json`, `process_rss_bytes`, lock_wait counters  
3. ~~Stack traces with source locations~~ **Done seed** — `stack_trace()` (symbolized via `backtrace_symbols`)  
4. ~~Debugger depth~~ **Done seed** — `debug_break` / `tasks_inspect_json` / `task_done` / `task_id` (locals/breakpoints residual)  
5. ~~Crash reports~~ **Done seed** — `crash_report_install` · ~~PGO/LTO workflow~~ **Done seed** — `MAKO_PGO_*` / `MAKO_NO_LTO` / howto  

### P3 — Install, distribution, portability

1. ~~Installer UX polish~~ **Done seed** — manifest (Unix+Windows) · doctor schema/fields · `DOCTOR_STRICT` matrix  

2. ~~Windows winget / Linux deb·rpm seeds~~ **Done seed** — `packaging/winget/` · `scripts/package-deb.sh` · `package-rpm.sh` (MSI/notarize residual)  
3. ~~Homebrew formula~~ **Done seed** — `Formula/mako.rb` (core publish is external)  
4. ~~Multi-OS matrix validation seed~~ **Done seed** — `scripts/validate-matrix.sh`  
5. ARM / x86-64 / RISC-V target validation — listed in matrix script; CI residual  

### P4 — Domain & advanced systems

1. Telecom/realtime — **SIP proxy library built-in** (`mako_sip.h` / `std/sip`); RTP/SRTP helpers; **SIPREC/WebRTC out of scope**  

2. ~~Storage product seeds~~ **Done seed** — page/WAL/hindex/store + btree save/load + SST + pcache + MVCC GC (`storage_depth_test`)  
3. ~~Graphics/audio/physics soft seeds~~ **Done seed** — `gfx_*` / `audio_mix` / `physics_step_*`  
4. ~~Multiplayer snapshot + rollback ring~~ **Done seed** — `snap_*` / `rollback_*`  
5. ~~GPU AI depth seeds~~ **Done seed** — `gemm2x2` / RoPE / `kv_cache_*` / f16 bits (host); Metal/CUDA residual  
6. Interop beyond C · hot reload · safe comptime domain extensions — **open**  

### Language ergonomics — production backends

**Already on tip (do not re-open as “missing language features”):**

| Feature | Surface | Tests / docs |
|---------|---------|--------------|
| Loops | `for i, v in range s` · `for k, v in range m` · C-style `for` | `for_forms_test` · [ERGONOMICS.md](ERGONOMICS.md) |
| Formatting | `fmt_sprintf*` / `fmt_sprint*` / `fmt_errorf` | `fmt_print_test` |
| String/int dispatch | `match "…" { … }` · `switch` / `case` | `ergonomics_test` · `switch_test` |
| Generalized mutable index lvalues | `s[1:3][0] = value` and `matrix[i][j] = value`; single evaluation plus bounds, mutability, NLL, and race checks | `slice_test` |
| Multi-field worker I/O | `chan[Struct]` + deep-POD kick args | `chan_struct_test` · [SPEED.md](SPEED.md) |
| Struct update (spread) | `S { field: v, ..base }` / `S { ...base, field: v }` | `struct_update_test` |
| Enum on kick-POD / channels | POD enum fields; `chan[Enum]` | `struct_update_test` |
| First-class fn values | `fn apply(f: fn(int)->int, …)` · named + lambda | `lang_ergonomics_test` · `first_class_fn_test` |
| Capturing closures (POD + string + struct + ShareInt) | value / clone / shared mut handle | `capturing_closure_test` · `struct_capture_test` · `share_capture_test` |
| Kick `fn` values across crew | `kick(apply(f, x))` with bare/capturing `MakoFn` | `kick_fn_test` |
| `f"…{x}"` + format specs | `+` ` ` `#` `-` `0` · `xXob` · float `fe` · width | `fstring_fmt_test` |
| Struct field defaults | `field: int = 0` on `struct` | `lang_ergonomics_test` |
| Tuple channels | `chan[(int, string)]` | `lang_ergonomics_test` |

**Still open (true residuals):**

1. Stack mut-ref captures (use `ShareInt` / share handles for shared mut) · deeper NLL  
2. Remaining printf exotics (`%n`, dynamic `*`, locale) — use `fmt_sprintf*`  
3. Full debugger DWARF/locals UI (seed: `debug_set_int` / `debug_locals_json` / `debug_bp`)  

### Language / stdlib residuals (lower priority)

- Exotic `Result` / `Option` / `?` edges beyond current suite  
- Full Unicode / PCRE / UCD depth (common `\p{…}` seeds landed)  
- Symbol-level stdlib parity with every Go package name (not a goal line-for-line)  
- JPEG viewer Huffman residual if still needed beyond baseline encode  

---

## Product focus (contract)

General-purpose **backend and infrastructure first**; telecom is one domain track,
not the language identity.

| # | Focus | State |
|---|--------|--------|
| 1 | Backend app surface | **Done** |
| 2 | API protocols & networking | **Done** |
| 3 | Data / SQL / serialization | **Done** |
| 4 | CLI / devtools | **Done** (depth residual in install) |
| 5 | Cloud / K8s / sidecars | Partial — helpers + containers; operator patterns open |
| 6 | Runtime trust | **Partial** — see P1 |
| 7 | Observability / debugging | **Partial (~78%)** — see P2 (OTLP/profile seeds Done) |
| 8 | Domain tracks | **Partial (~70%)** — security polish Done; stacks open |
| 9 | Deployment / WASM | Strong seeds; matrix polish open |

---

## General-purpose intention tracker

Checklist for **100% of the product intention**, not the MVP/STATUS bar.  
Percentages are weighted; update when a task flips.

**Overall intention completion:** **~96% / 100%**  
**Mako identity (preferred syntax):** **~100%** — [IDENTITY.md](IDENTITY.md).

| Track | Weight | Current |
|-------|--------|---------|
| 1. Language identity and core type system | 10% | **100%** |
| 2. Memory safety and allocation control | 10% | 88% |
| 3. Concurrency and runtime trust | 10% | **88%** |
| 4. Backend app surface | 12% | **100%** |
| 5. API protocols and networking | 10% | **100%** |
| 6. Data, SQL, and serialization | 10% | **100%** |
| 7. Toolchain, packages, and IDE | 10% | **100%** |
| 8. Observability and debugging | 8% | **86%** |
| 9. Installer, distribution, and portability | 10% | **88%** |
| 10. Domain tracks and advanced systems | 10% | **95%** |

### 1. Language identity and core type system — 10%

- [x] Mako-owned syntax identity (preferred surface).
- [x] Static types, local inference, `Result`, `Option`, enums, `match`.
- [x] Interfaces seed and dynamic interface dispatch.
- [x] User generics with monomorphization (`fn id[T](x: T) -> T`; dual `[]`/`<>` for built-ins).
- [x] Demand-driven map/bag monomorph emission (AST-collected used shapes only; O(used) not N²).
- [x] Nested bag values: Option/Result/tuple/slice nests as map values (suite-backed).
- [x] Unique Mako surface preferred; dual sugar only — [IDENTITY.md](IDENTITY.md).
- [x] Packs/pulls: `pack` / `pull` (dual `package` / `import`).
- [x] Tuples + multi-return: `(int, int)` + `let a, b = f()`.
- [x] Explicit `export`; opt-in `visibility = "explicit"`.
- [x] Typed channels: `chan_open[T]` / `make(chan[T], n)`.
- [x] Pain map: [PAIN_POINTS.md](PAIN_POINTS.md).
- [x] Language pain residuals seed (deep Send/race, NLL multi-label, nested patterns).
- [x] `if init; cond { }` · `go f()` → kick · compound assign · Go `for`/`switch` forms.
- [x] `fallthrough` switch dual seed (`fallthrough_test`).
- [x] Richer error chain seed — `error_unwrap` / `root` / `as_tag` / `has_tag` · `std/errors` · `Result[T, Enum]`.
- [x] Compiler-enforced API stability annotations (`#[stable]` / `#[deprecated]`).
- [x] Richer pattern matching — struct field patterns + nested variant patterns (typecheck).

### 2. Memory safety and allocation control — 10%

- [x] No null by default; explicit `Option`.
- [x] Scope ownership, `arena`, `hold`, `share` seed, CFG/NLL checks.
- [x] Debug bounds checks and explicit `unsafe` blocks.
- [x] Release safety profile: safe indexing checks are retained; the legacy
  `[profile.release] bounds_checks = "on"` setting remains accepted.
- [x] Memory pools and reusable buffers.
- [x] Borrowed string/byte views and zero-copy packet/file APIs.
- [x] Core string region ops without substring alloc (`str_slice_eq` / `str_slice_index` / `str_at_eq` / `str_byte_at`).
- [x] GC-free runtime invariant — no tracing collector or `gc_*` API.
- [x] Leak detector and allocation reporting.

### 3. Concurrency and runtime trust — 10%

- [x] `crew`, `kick`, `join`, channels, cancel seed, `fan`.
- [x] Actor runtime seed.
- [x] Full first-class `actor` / `receive` syntax with owned state (seed: fields + self).
- [x] Actor int payload seed (`receive Inc(delta)` · packed tag+payload mailbox).
- [x] Portable timeouts and deadlines across task/channel (seed; network has per-API timeouts).
- [x] Structured error propagation from child tasks (nursery first_err / wait).
- [x] Explicit detached-task syntax and lifecycle (`detach` + `detached_join_all`).
- [x] Backpressure primitives and bounded queues.
- [x] Race safety at language boundaries (Send/Sync; closure capture analysis; mut captures until join; nested field/index writes; TSan via `--race`). Leak scopes Done.
- [x] Scheduler observability.

### 4. Backend app surface — 12%

- [x] Typed `HttpRequest` parse/accessors.
- [x] Route helpers / router package / middleware / request context.
- [x] Validation · authn/authz · sessions · cookies · CSRF-safe defaults.
- [x] Multipart/upload · rate limit · compression · cache.
- [x] Health checks · graceful shutdown · background jobs.

### 5. API protocols and networking — 10%

- [x] TCP, HTTP/1.1, HTTP/2, gRPC-ish unary/stream seeds, H3 client/server pieces.
- [x] TLS/QUIC/WebSocket seeds; UDP and Unix sockets; DNS polish.
- [x] WebSocket APIs; OpenAPI; GraphQL seed; SSE / streaming RPC.
- [x] Connection pooling, load-balancing, backpressure-aware network I/O.

### 6. Data, SQL, and serialization — 10%

- [x] JSON, CSV/XML seeds, binary/protobuf/gob, SQLite/Postgres seeds, local KV.
- [x] Pooling, transactions, prepared statements, migrations, typed SQL checker.
- [x] MySQL/MariaDB and Redis polish; wider store packages.
- [x] YAML, TOML, MessagePack, CBOR, Avro; compile-time serialization seeds.

### 7. Toolchain, packages, and IDE — 10%

- [x] `check` / `build` / `run` / `fmt` / `test` · package manifest/lock seed.
- [x] Incremental/object cache · parallel jobs · minimal LSP seed.
- [x] VS Code extension: grammar, tasks, problem matcher, debug launch, LSP client.
- [x] Package registry protocol · audit · coverage/fuzz/property/snapshot · bench · `mako doc`.
- [x] LSP autocomplete / go-to-def / references / rename / code actions.
- [x] Compiler JSON diagnostics · symbol graph · API breaking-change detector.

### 8. Observability and debugging — 8%

- [x] Logs / slog + redaction; clear runtime abort messages.
- [x] Metrics counters / gauges / histograms.
- [x] Prometheus text exposition (`metrics_export_prom`).
- [x] Wall-clock compile/run profile reports with stable JSON.
- [x] Trace span-lite JSON seed (`trace_export_json`).
- [x] Runtime introspection endpoint/hooks.
- [x] OTLP/HTTP JSON export seed (`trace_export_otlp_json` / `metrics_export_otlp_json`).
- [x] Profile snapshot seed (`profile_snapshot_json` — RSS/CPU/alloc/sched/lock).
- [x] Stack traces with symbols (`stack_trace`).
- [x] Crash report install seed (`crash_report_install`).
- [x] PGO/LTO workflow (`MAKO_PGO_GEN` / `MAKO_PGO_USE` / `MAKO_NO_LTO` · howto).
- [x] Debugger seed: `debug_break` / hits · `tasks_inspect_json` · `task_done` / `task_id` / `task_joined`.
- [x] Closure env free: `fn_drop` / `fn_has_env` (+ generated drop_env for string fields).
- [x] Auto `fn_drop` on scope exit; kick **moves** env into the task (no double-free).
- [x] Debug locals registry + soft BP ids (`debug_set_int` / `debug_locals_json` / `debug_bp`).
- [x] Debug source frame seed (`debug_set_loc` / `debug_file` / `debug_line` / `debug_frame_json`).
- [x] Debugger depth seed: line BPs · frame stack · async parent · trap flag · `debug_snapshot_json`.
- [x] OTLP protobuf export seed (`trace_export_otlp_pb`) + HTTP exporter (`otlp_http_export` / `otlp_export_traces_*`).
- [x] Sampling CPU profiler seed (`profile_sample_*` · SIGPROF + cooperative · `profile_samples_json`).
- [x] DAP JSON seed (`dap_initialize_response` / `dap_stopped_event` / `dap_request_command`) · lldb still primary for DWARF.
- [x] DAP dispatch + CLI seed (`dap_handle_request` · `mako dap --request …`).
- [x] DAP stdio Content-Length loop (`mako dap --stdio` · scopes/variables/step seeds).
- [x] pprof-text + multi-thread tid seed (`profile_samples_pprof_text` / `profile_sample_thread_count`).
- [x] Profile HTTP export seed (`profile_http_route` / `profile_pprof_http_body` for `/debug/pprof/*`).
- [x] Continuous profile HTTP CLI (`mako profile-serve --port N --max-requests K`).
- [ ] Full DWARF-local product debugger (lldb/DAP UI product; stdio seed is not a full IDE).
- [ ] Multi-process fleet pprof aggregator product.

### 9. Installer, distribution, and portability — 10%

- [x] Native single binary · install scripts · release docs · `mako doctor` · update/uninstall.
- [x] One-command install with version selection and checksum verification.
- [x] Installer ships compiler, runtime headers, stdlib, VS Code scaffold.
- [x] Release archives + checksums + install smoke + CI installer smoke.
- [x] Cross-target flag · WASI preview1/2 seeds · static defaults · container/serverless helpers.
- [x] Stable ABI, dynamic libraries, native plugins, WASM plugins.
- [x] Installer manifest + doctor schema/field validation (`install-manifest.json` seed).
- [x] Windows `install.ps1` writes the same manifest schema.
- [x] Packaging seeds: `package-deb.sh` · `package-rpm.sh` · `packaging/winget/` · `Formula/mako.rb` · `validate-matrix.sh`.
- [x] MSI / macOS notarize **workflow notes** (`scripts/package-msi-notes.md` · `package-macos-notarize-notes.md`).
- [x] MSI WiX skeleton + dry-run seeds (`packaging/windows/mako.wxs` · `package-msi-seed.sh`).
- [x] Notarize dry-run seed (`package-notarize-seed.sh`) · notes remain for real Apple credentials.
- [x] homebrew / winget publish **seed scripts** (`publish-homebrew-tap-seed.sh` · `publish-winget-seed.sh`).
- [x] CI package-seed workflow (validate packaging scripts).
- [x] Cross-target dry-run seed (`scripts/cross-target-seed.sh` · FreeBSD/RISC-V triples · CI workflow).
- [x] Product-seeds CI workflow (packaging dry-run + cross-compile seed + optional sign notes).
- [ ] Signed MSI / notarized pkg with secrets in production release CI (secrets-gated residual).
- [ ] homebrew-core / winget-pkgs merge (external maintainers).
- [ ] CI multi-OS matrix green on FreeBSD / RISC-V **hosts** (real runners).
- [ ] Console/platform-specific toolchain path where licensing permits.

### 10. Domain tracks and advanced systems — 10%

- [x] Storage engine example · HTTP/H2/H3/gRPC/QUIC seeds.
- [x] **Security product polish** — mTLS, CSR/self-signed/reload, PEM helpers, SCRAM-PLUS cbind, `path_file_size`.
- [x] Crypto **core only** documented: digests, AEAD, KDF, SCRAM schedule + channel binding; **no** high-level SASL state machine.
- [x] Game-loop / fixed-timestep · frame allocators · object pools · ECS seed.
- [x] Deterministic simulation · FSM helpers · rings / SPSC / scatter-gather.
- [x] GPU AI seed (OpenCL path) · local model store · GGUF F32/F16 · MHA · Q4_0/Q8_0 · BPE seed.
- [x] **SIP proxy library** (builtins + `std/sip`): parse/build, Via/RR/rport, Digest HA1, framing; RTP/SRTP helpers; **SIPREC/WebRTC out of scope**.
- [x] Zero-copy SIP hot path (`sip_header_view` / `sip_method_eq` / `sip_header_eq` / `sip_view_*`).
- [x] Graphics/audio/physics soft seeds (`gfx_*`, `audio_mix`, `physics_step_*`).
- [x] Multiplayer snapshot + rollback ring seeds (`snap_*`, `rollback_*`).
- [x] Storage page/WAL/hindex/store + btree/LSM/MVCC seeds.
- [x] On-disk btree save/load · sorted SST · page cache LRU · MVCC GC · SIMD dot/sum seed.
- [x] GPU AI depth host seeds (RoPE, KV-cache, gemm2x2, f16 bits) + OpenCL matmul.
- [x] LSM L0→L1 compact seed (`lsm_compact`) · `store_recover_wal` crash replay · `hot_reload_*` mtime watch.
- [x] Multi-level LSM (L1–L3 via `lsm_compact_down` / `lsm_sst_levels` / `lsm_level_len`).
- [x] Page-backed btree seed (`pbtree_*` — nodes in `MakoPage`).
- [x] Storage polish seeds: `bloom_*` · `btree_range` / `sst_range` + `range_*` · `pman_*` disk page manager.
- [x] Window soft poll + backend name (`gfx_poll` / `gfx_backend_name`).
- [x] Soft framebuffer (`gfx_window_fill` / `set_pixel` / `get_pixel` / `pixels`).
- [x] GPU Metal/CUDA/Vulkan **availability probes** (`gpu_metal_ok` / `cuda_ok` / `vulkan_ok`).
- [x] Netcode seeds: `snap_diff` / `snap_apply_delta` · `netcode_lag_comp_tick` / `netcode_interp`.
- [x] Plugin host loader seed (`plugin_open` / `call` / `close`) · `ffi_abi_name`.
- [x] Rich plugin package (`std/plugin` + info/error/slots/close_all · `plugin_package_test`).
- [x] Plugin product (live dylib load/call/reload/manifest · `plugin_product_test`).
- [x] Full unicode/utf8 package (UCD seed + encode/decode · `std/unicode` · `unicode_full_test`).
- [x] List[T] + richer collections (list/set/heap/ring/stack/queue · `collections_*_test`).
- [x] Full time package (calendar/parse/format/duration · `time_full_test`).
- [x] Full syscall package (portable OS primitives · `syscall_full_test`).
- [x] YAML + TOML encoding packages (`yaml_toml_test`).
- [x] Product version **0.1.6**.
- [x] CBOR + MessagePack binary subset · list combinators (`cbor_msgpack_test`).
- [x] Avro binary · GraphQL/protobuf packages · named TZ offsets (`avro_graphql_tz_test`).
- [x] Product version **0.1.7**.
- [x] Speed wave: wyhash · stack f-strings · zero-copy string lits · select condvar · HTTP 1024.
- [x] Product version **0.1.8**.
- [x] Product version **0.1.9** — generic structs/enums, bounds, iterator/closure seeds.
- [x] `chan_len` / `chan_cap` on any `chan[T]` (struct/tuple/string rings).
- [x] Hot-reload unwatch + count (`hot_reload_unwatch` / `hot_reload_watch_count`).
- [x] Client prediction service seed (`predict_new` / `input` / `reconcile` / `state` / `tick`).
- [x] Live dylib hot-reload seed (`hot_reload_plugin_watch` / `poll` / `call` / `close`).
- [ ] Real OS windowing / GPU shaders / asset pipelines product.
- [ ] Full multiplayer netcode product (interest mgmt / session service).
- [ ] Real Metal-native / CUDA / Vulkan compute backends (drivers).
- [x] SIMD portable seed (`simd_dot_i64_4` / `simd_sum_i64_4` — autovec-friendly).
- [x] Hot-reload mtime watch seed (`file_mtime_ns` / `hot_reload_watch` / `hot_reload_changed`).
- [x] Hot-reload depth seed (`note_swap` / `swap_count` / `stamp` / `status_json`).
- [x] Comptime depth seed: const `if` / comparisons / `if`-expr fold (`const_fn_test`).
- [x] Const-fn match + bounded while seed (int patterns, assign loops; max 100k iters).
- [x] Const-fn for seed (`for i in n` / `range n` / C-style; max 100k).
- [x] Const-fn break/continue seed (bare; C-for continue runs post).
- [x] Const string seed (literals, `+`, `str_len`, equality → int).
- [x] Const fn string params/returns seed (`shout` / `greet` / mixed int).
- [ ] Domain CTFE product beyond seed (heap, mutate, string loops, full interpreter).

---

## Later (VISION — not scheduled)

- Deep comptime and browser DOM  
- In-process H3 server productization beyond current seeds  
- WASI sockets depth · full LSP “IDE product” polish beyond seed  

## External (user / ecosystem)

1. Publish Homebrew / **homebrew-core**  
2. Community package registry population  
3. Production case studies (backend, infra, domain stacks built *in* Mako)  

---

## How to use this file

1. **STATUS.md** is the adversarial Done bar for MVP claims.  
2. **This file** orders *product intention* residuals after MVP.  
3. When a checkbox flips, update the track % and overall ~% in the same edit.  
4. Prefer small, suite-backed landings over roadmap thrash.
