# Mako roadmap

**Product version:** **0.4.10** (tip) · last tagged **v0.4.5** · Last sync: **2026-07-22**.  
**Suite:** **367** Mako tests on `examples/testing` (C + native backends) + Rust
unit tests, 0 failures on the native gate · CI ASan/UBSan/TSan as configured.

**Versioning:** [VERSIONING.md](VERSIONING.md) — **prefer small patches** over mega-minors.  
**Release:** tag **`v0.4.5`** published; tip train **0.4.6 → 0.4.7 → … → 0.5.0**.

**Verified:** [STATUS.md](STATUS.md) · **Stdlib:** [STDLIB.md](STDLIB.md) · **Security:** [SECURITY.md](SECURITY.md) · **Release:** [RELEASE.md](RELEASE.md).  
**Book:** [The Mako Book](book/) · **Identity:** [IDENTITY.md](IDENTITY.md).  
**Soundness:** [SOUNDNESS.md](SOUNDNESS.md) · **Memory model:** [MEMORY_MODEL.md](MEMORY_MODEL.md).  
**Native plan detail:** [NATIVE_COMPILER_PLAN.md](NATIVE_COMPILER_PLAN.md).

---

## Version map (0.4.5 → patches → 0.5.x → 1.0)

| Version | Theme | Status |
|---------|-------|--------|
| **0.1.9–0.2.5** | Generics → stdlib → soundness → tooling honesty | **Shipped** |
| **0.3.0** | Cross-platform, CI green, ownership hardening | **Shipped** |
| **0.4.0** | Performance — DCE, constant folding, runtime speed, lint | **Shipped** |
| **0.4.1** | Windows/runtime/edge stability | **Shipped** (see CHANGELOG) |
| **0.4.5** | Native compiler product path (language + release cut) | **Shipped** — tag `v0.4.5` |
| **0.4.6** | Residual: string_slice + binary size + bench bars + backend policy env | **In tree** (untagged; may ship with 0.4.7) |
| **0.4.7** | Cross / WASM / static / sanitize truth table (hard-error gaps) | **In tree** |
| **0.4.8** | Map/I/O workload gates + perf regression budget | **In tree** |
| **0.4.9** | LLVM CI (macOS) + install/doctor smoke | **In tree** |
| **0.4.10** | Years-up soak foundation (vs JVM long-running) | **In tree** (tip; cut when ready to tag) |
| **0.5.0** | Native-first **default** (CLI default flip — minor theme) | **Planned** minor |
| **0.5.1** | Toolchain & IDE depth (LSP, DAP/DWARF, doc/bench product) | **Planned** |
| **0.5.2** | Runtime trust & production concurrency soaks | **Planned** |
| **0.5.x** | Further patches on 0.5 | **Planned** as needed |
| **1.0** | Stability contract (compat, LTS-ish discipline) | **Planned** after 0.5 series |

**Principle:** ship **measurable** gates each **patch or minor**; do not reopen identity (no free `go`, no lifetime params, no silent native→C fallback). Prefer **0.4.N** patches over waiting for **0.5.0**.

```text
0.4.5  language gate ✓  [tagged]
0.4.6  residual perf + size + MAKO_BACKEND policy  [in tree]
0.4.7  modes truth table  [in tree]
0.4.8  map/I/O gates + regression baselines  [in tree]
0.4.9  LLVM CI (macOS) + install/doctor smoke  [in tree]
0.4.10 years-up soak foundation  [tip]
0.5.0  native-first CLI default (minor)
0.5.1  toolchain/IDE
0.5.2  runtime trust
0.5.x  patches
1.0    stability freeze
```

---

## 0.4.5 — Native compiler product path

### North star

Ship **0.4.5** as:

1. **Full language on native** — Cranelift debug path runs the entire testing corpus.
2. **Release optimizer path** — LLVM competitive with C/Rust on **published** workloads (honest per-workload gates).
3. **Installable product** — one-binary install + packaging seeds + versioned notes/checksums.
4. **No silent lying** — unsupported modes hard-error; docs match the tree.

**Not 0.4.5:** 1.0 stability freeze, full DWARF IDE debugger product, line-for-line Go
stdlib, free `go` outside `crew`, or lifetime parameters.

### Phase 0 — Language gate — **DONE**

| Item | Evidence |
|------|----------|
| Shared ownership-explicit IR | `src/native_ir.rs` |
| Cranelift debug backend | `src/native_codegen.rs` |
| Native bridge + embedded runtime archive | `runtime/native_bridge.c` |
| Full testing corpus on native | **367/367** `mako test examples/testing --backend native` |
| Portable IO + concurrent select | seek/read_exact/append3 bridges; TLS select; recv closed vs timeout |
| Language residual pack | mut-self `for` iterators; multi-stmt mut captures; const `s[i]` |
| Product version string | `0.4.5` in `Cargo.toml` |

### Track A — Performance (release path) — **NEXT**

LLVM is the release optimizer; Cranelift stays debug / fast-compile.

| ID | Deliverable | Acceptance |
|----|-------------|------------|
| **A1** | `mako build --backend llvm --release` on host arm64/x86_64 (`--features llvm-backend`) | Builds + links with bundled lld; end user needs no system clang for that path |
| **A2** | Broad workload gate vs C backend + hand C + Rust | Extend `./scripts/native-bench-gate.sh` (or sibling) for **slice, map, I/O, CPU, RSS** — medians, not one-offs |
| **A3** | Honest published numbers | Update [NATIVE_COMPILER_PLAN.md](NATIVE_COMPILER_PLAN.md) / [PERFORMANCE.md](PERFORMANCE.md) / [SPEED.md](SPEED.md) with hardware + flags |
| **A4** | Compile latency + binary-size gates | Documented bounds (compile vs C backend; size bar where applicable) |

**Exit A:** Release path = LLVM; debug path = Cranelift; both documented; gates green on CI host.  
**Out of scope for A:** beating every Rust crate on every microbench.

### Track B — Packaging & distribution

| ID | Deliverable | Acceptance |
|----|-------------|------------|
| **B1** | `scripts/package-release.sh` slim + full tarballs | `dist/mako-<triple>.tar.gz` + `.sha256` |
| **B2** | Linux deb/rpm seeds | `package-deb.sh` / `package-rpm.sh` smoke |
| **B3** | Install scripts pin v0.4.5 | `install-release.sh` / `install-linux.sh --version v0.4.5` |
| **B4** | GitHub Release | Tag `v0.4.5`, notes from CHANGELOG, artifacts + checksums |
| **B5** | Homebrew / winget seeds | Real SHA for the tag (soft-fail only while pending) |
| **B6** | Clean-install doctor | `mako doctor` reports runtime + std + version |

**Exit B:** curl\|bash install works for at least Linux x86_64 and macOS arm64 (or documented exceptions).

### Track C — Modes, CI honesty, residual polish

| ID | Deliverable | Acceptance |
|----|-------------|------------|
| **C1** | Sanitizer / overflow / static on native/LLVM | Implement **or** hard-error pointing at C backend |
| **C2** | Cross-compile + WASM status | Document working triples; fail closed otherwise |
| **C3** | CI matrix | `mako test examples/testing --backend c` + `--backend native` on PR; optional LLVM job |
| **C4** | Leak / RSS / size in packaging CI | Fail on regression thresholds |
| **C5** | Optional soundness soaks (non-blocking for tag) | TSan capture matrix; channel monomorph take matrix — nightly/optional |
| **C6** | Doc drift | Corpus **367**, residual pack Done, version lines consistent |

**Exit C:** CI reflects truth; 0.4.5 docs are consistent.

### Phased schedule

```text
Phase 0 — Freeze language gate          [DONE]
Phase 1 — Perf credibility (Track A)    [NEXT]
Phase 2 — Package & publish (Track B)
Phase 3 — CI + modes honesty (Track C) → tag v0.4.5
Phase 4 — Post-tag (Homebrew/winget follow-through; then 0.5 / 1.0 planning)
```

### Ship checklist

- [x] Native language gate: full `examples/testing` green (**367/367**)  
- [x] Version `0.4.5` in tree  
- [x] LLVM release path usable on primary host (Apple arm64) when llvm-backend + lld present  
- [x] Bench numbers published (fib/parity ~1.01× Rust; slice ~1.12×; string_slice ~1.15× residual; binary ~1.01× after dead_strip)  
- [x] Release tarball + sha256 for host triple (`package-release.sh --slim`)  
- [x] Install script smoke (macOS arm64 from local dist)  
- [x] CHANGELOG **0.4.5** section  
- [x] GitHub tag `v0.4.5` (multi-OS artifacts via `release.yml` on tag)  
- [x] CI runs c + **native** tests on Linux/macOS  
- [x] Docs numbers match tip  

### Native compiler completion checklist

The C backend remains the language-feature oracle. Native support is verified
feature-by-feature; unsupported constructs must remain hard errors rather than
silently falling back to C.

- [x] Backend-neutral ownership-explicit IR
- [x] Cranelift scalar CFG, strings, primitive slices, and initial `[]string`
- [x] Native/C differential fixtures and Guard Malloc/leak coverage
- [x] Bundled runtime archive and Linux x86_64 package workflow seed
- [x] Nested slices and nested owned aggregate layout/drop (shared IR)
- [x] Complete structs, enums, maps, tuples, methods, `?`, match (incl. arm `return`)
- [x] `defer`, labeled loops, match guards (shared IR + corpus)
- [x] Native runtime interop via `runtime/native_bridge.c` (net/TLS/SQL/HTTP/SIP/…)
- [x] `crew`, `kick`, `fan`, channels, and `select` (corpus green)
- [x] Full `examples/testing` native correctness gate — **367/367** (2026-07-22)
- [x] Mut-self iterators · multi-stmt mut captures · const string index
- [ ] LLVM release path: broad workload runtime ≤ C and ≤ Rust (slice/map/I/O/CPU/RSS)
- [ ] Cross-compilation, WASM, static, sanitizer, and overflow build modes (or hard-error)
- [ ] Full leak, latency, RSS, and binary-size gates in CI packaging

---

## 0.4.6 — Residual perf + size + backend policy env

**Depends on:** tagged **0.4.5**.  
**Theme:** close the post-tag residual bars and document how to select backends — **without** flipping the CLI default yet.

| ID | Deliverable | Status |
|----|-------------|--------|
| **46-A** | Immortal string share + LLVM `[]string` hot path | **Done** in tip |
| **46-B** | Runtime archive dead_strip / gc-sections; binary ~1.01× slim C | **Done** in tip |
| **46-C** | Honest bench gate defaults (runtime 1.25×, binary 1.05×, RSS 1.25×) | **Done** in tip |
| **46-D** | Backend policy docs + `MAKO_BACKEND` / `MAKO_TEST_BACKEND` | **Done** in tip |
| **46-E** | Tag `v0.4.6` + package SHAs when maintainers cut | **Pending** |

**Exit:** CHANGELOG **0.4.6**, `Cargo.toml` **0.4.6**, gate PASS, tag optional same week.

---

## 0.4.7 — Modes truth table

**Theme:** every unsupported mode hard-errors with a clear pointer (no silent hybrid).

| ID | Deliverable | Status |
|----|-------------|--------|
| **47-A** | Sanitize / static / emit-c / target on native/LLVM | **Done** — `validate_direct_backend_modes` fail-closed |
| **47-B** | Modes matrix documented | **Done** — [BUILD.md § Modes matrix](BUILD.md) |
| **47-C** | Doctor reports mode support | **Done** — `mako doctor` backends / modes block |
| **47-D** | LLVM tests use release opt level | **Done** — harness honors release-only llvm |

---

## 0.4.8 — Workload gates + regression budget

| ID | Deliverable | Status |
|----|-------------|--------|
| **48-A** | Map + I/O benches in `native-bench-gate.sh` | **Done** — `native_map`, `native_io` |
| **48-B** | Regression budget vs recorded ratios | **Done** — `native-bench-baselines.json` × 1.15 |
| **48-C** | string_slice residual notes | Residual ≤1.25× ship bar; SSA toward ≤1.00× still open |

---

## 0.4.9 — LLVM CI + install smoke

| ID | Deliverable | Status |
|----|-------------|--------|
| **49-A** | LLVM CI job (macOS) | **Done** — `llvm-backend` in `.github/workflows/ci.yml`; skip policy in `llvm-backend-test.sh` |
| **49-B** | Install/doctor smoke | **Done** — `scripts/install-smoke.sh` on primary CI matrix |

---

## 0.5.0 — Native-first default (minor)

**Depends on:** **0.4.6+** patches green (language gate + residual + policy env).  
**Theme:** make the native path the **default product experience**, with C as oracle/fallback — **CLI default flips here**, not in a patch.

### North star

1. Backend policy already documented (0.4.6); **default** becomes native (debug) / llvm-when-available (release) or explicit equivalent.
2. **CI green** on both c and native (done in 0.4.5 CI; keep red-on-fail).
3. No silent hybrid; sanitizers/cross still route to C or hard-error.
4. Windows + Linux + macOS install paths honest for what works.

### Deliverables

| ID | Deliverable | Acceptance |
|----|-------------|------------|
| **50-A** | Backend policy in GUIDE/BUILD/RELEASE | **Done seed** in 0.4.6 — [BUILD.md](BUILD.md) |
| **50-B** | CI: c + native required | **Done** on Linux/macOS CI jobs |
| **50-C** | Optional LLVM CI job | **Done** in **0.4.9** (macOS `llvm-backend` job) |
| **50-D** | **Default backend flip** | `mako build` / `test` default native (or documented MAKO_BACKEND default=native); `--backend c` override |
| **50-E** | Cross / WASM / static matrix | Prefer land in **0.4.7** |
| **50-F** | Perf regression budget | Prefer land in **0.4.8** |

### Exit 0.5.0

- Tagged `v0.5.0` with changelog.
- Primary hosts: install + test native + (where available) LLVM release build.
- C backend still passes full suite (oracle).

### Non-goals for 0.5.0

- Removing the C backend.
- Self-hosting the full compiler in Mako.
- Full IDE debugger product (that is 0.5.1).

---

## 0.5.1 — Toolchain & IDE depth

**Depends on:** 0.5.0 platform defaults stable enough that tooling targets one primary native path.

### North star

Coherent **official toolchain**: LSP, debug, docs, bench, and doctor feel like one product.

### Deliverables

| ID | Deliverable | Acceptance |
|----|-------------|------------|
| **51-A** | LSP product depth | go-to-def / references / rename / diagnostics stable on multi-file packages |
| **51-B** | Debugger depth | DWARF locals + DAP stdio usable from VS Code for native binaries (beyond seed) |
| **51-C** | `mako doc` + package docs | Publishable API docs for std and user packs |
| **51-D** | `mako bench` + gate scripts | Official microbench entry; documents vs C/Rust |
| **51-E** | Editor extension polish | VS Code tasks, problem matcher, launch configs match 0.5 defaults |
| **51-F** | `mako doctor` / install matrix | Catches missing runtime/std/LLVM-optional components cleanly |

### Exit 0.5.1

- Tagged `v0.5.1`.
- “Open a package, jump to def, run tests, attach debugger” works for a hello backend on primary OS.

### Non-goals for 0.5.1

- Full JetBrains / multi-IDE parity.
- Time-travel debugging / rr product.

---

## 0.5.2 — Runtime trust & production concurrency (years-up)

**Depends on:** 0.5.0 (and ideally 0.5.1 for debugability under load).  
**Product story:** backends that stay up for **months–years** should beat
**Java/Kotlin (JVM)** on **p99 stability and RSS ceiling** (no GC). Strategy:
[LONG_RUNNING.md](LONG_RUNNING.md). Seed gate: `scripts/long-run-soak.sh` (LR-1).

### North star

Production backends can rely on **structured concurrency + ownership** under stress, with evidence (not only unit tests). Steady-state memory does not creep under fixed load.

### Deliverables

| ID | Deliverable | Acceptance |
|----|-------------|------------|
| **52-A** | TSan soaks | Capture matrix + channel/select stress in optional/nightly CI |
| **52-B** | Channel monomorph take matrix | Beyond int/float/string for send/take/timeout |
| **52-C** | Scheduler depth | Document pool limits; optional work-stealing only if soaks demand it |
| **52-D** | Cancellation / deadline product | Portable timeout story end-to-end (task + channel + net APIs) |
| **52-E** | Leak / resource census under load | RT-006-style APIs + soak that fails on growth (**seed:** long-run-soak) |
| **52-F** | Race model docs ↔ code | MEMORY_MODEL and typecheck `is_sync_ty` table stay in lockstep |
| **52-G** | Years-up vs JVM evidence | Reproducible p99/RSS soaks; claims only with public methodology |

### Exit 0.5.2

- Tagged `v0.5.2`.
- Published soak results; no known “must not ship” races on documented patterns.
- Long-run soak green in CI; HTTP/channel soaks documented.

### Non-goals for 0.5.2

- Claiming data-race freedom for all unsafe/FFI.
- Actor model as the only concurrency story (crew/channels remain first-class).

---

## 0.5.x — Patch trains

Use **0.5.3+** only for:

- Perf regressions / install breakage after 0.5.0–0.5.2.
- Portability fixes (new triple, WASM polish).
- Security patches.
- Doc/tooling hotfixes that do not change language surface.

Avoid stuffing large language features into 0.5.x patches; open **0.6** if needed for a new theme (e.g. self-hosting, domain packs).

---

## Toward 1.0 (after 0.5 series)

Not a commitment to ship immediately after 0.5.2. Preconditions:

| Gate | Meaning |
|------|---------|
| Compat | 0.x → 1.0 migration story; dual syntax freeze policy ([COMPAT.md](COMPAT.md)) |
| Backends | Native (debug+release) default; C optional oracle |
| CI | Multi-OS matrix green for install + test |
| Soundness | SAFE/RT core + documented soaks |
| Docs | Book + GUIDE match preferred syntax; STATUS honest |

**1.0 theme:** stability and support discipline — not a feature dump.

---

## Dependency sketch

```text
0.4.5 ──tag──► 0.5.0 (native-first platform)
                  │
                  ├──────────► 0.5.1 (toolchain / IDE)
                  │
                  └──────────► 0.5.2 (runtime trust soaks)
                                   │
                                   └── 0.5.x patches ──► 1.0 when gates hold
```

---

## Soundness & runtime (SAFE / RT)

Program of record: **[SOUNDNESS.md](SOUNDNESS.md)** · memory model:
**[MEMORY_MODEL.md](MEMORY_MODEL.md)**.

Shipped as **0.2.4** after the **2026-07-18** audit: field/index mut roots,
empty `[]`, owning free (slice/map/string), return transfer + materialize,
`?` early free, nested `[][]T` free-on-reassign, stack POD array lits, lockfile
build verification (PR #3), `string_view`, scheduler pool, capture matrix.
Optional depth (TSan soaks, monomorph take-send) remains open.

| ID | Theme | Status |
|----|--------|--------|
| SAFE-001 | Bounds checks in safe release | **Done** |
| SAFE-002 | Ownership categories in LANGUAGE_SPEC | **Done** |
| SAFE-003 | Slice drops, views, return transfer, free-on-reassign, nested release_replaced | **Done** |
| SAFE-004 | Map free (built-in + monomorph) | **Done** |
| SAFE-005 | String free + surface `string_view` | **Done** |
| SAFE-006 | CFG drops (return / break / continue / `?` / match / block exit); discarded bag payloads; bind-scope free; alias-mut `__own`; move/clone store (no double-free) | **Done** (core) |
| SAFE-007 | Escape checks (arena return/store, slice view) | **Done** |
| SAFE-008 | Capture matrix (kick/fan) | **Done** (core) |
| SAFE-009 | CMap readers/writer gate | **Done** |
| SAFE-010 | Concurrency memory model doc | **Done** |
| RT-001 | Crew exit / cancel / failure | **Done** |
| RT-002…003 | Bounded scheduler / `spawn_blocking` | **Done** (seed) |
| RT-004 | Channel send ownership (clone/take/timeout) | **Done** (core) |
| RT-005 | Seeded channel/select stress with TSan replay | **Done** (core) |
| RT-006 | Task/resource census APIs | **Done** |
| Pkg lock | Build-time locked dep verification (PR #3) | **Done** |
| Struct Own fields | Deep free of string/slice fields on drop | **Done** |

#### Speed (hot path, with free on)

| Item | Status |
|------|--------|
| Stack POD array lits (`cap==0` view) | **Done** |
| Empty slices no-malloc | **Done** |
| Escape heapify (`to_owned`) | **Done** |
| Cold free (`MAKO_UNLIKELY`) | **Done** |

#### Soundness next (optional depth)

1. Longer TSan soak jobs on capture matrix (CI optional).
2. RT-004 monomorph channel take matrix beyond int/float/string.
3. Deeper scheduler (work-stealing / dynamic resize) if pool soaks demand it.
4. Longer channel/select soaks with additional seeds.
5. Make discarded bag cleanup type-complete when typed expression metadata reaches codegen.

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
| **Iterator protocol** | Done | `fn next(mut self) -> Option[T]` drives `for v in it` (advances binding); by-value next remains non-advancing |
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

## v0.2.4 — Soundness wave + residuals — **shipped**

| Feature | Description |
|---------|-------------|
| **SAFE-001…010 core** | Bounds, ownership categories, slice/map free, string_view, CFG/`?` drops, capture matrix, CMap, memory model |
| **RT-001…006 core** | Crew cancel, opt-in scheduler pool + spawn_blocking, channel take-send, census, select stress seed |
| **Speed** | Stack POD array lits, empty no-malloc, cold free, escape `to_owned` |
| **Pkg lock verify** | Build-time locked dependency integrity (PR #3) |
| **Struct Own free** | Deep free of string/slice fields on scope exit |

---

## v0.2.5 — Tooling

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
| **Direct machine-code backend** | Compile Mako IR directly to native object code, bypassing the source-to-Clang pipeline; staged by target with parity, debug-info, linker, and safety-gate requirements |

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

1. Deeper NLL edge polish (core sequential mut-ref multi-stmt + kick ShareInt gate shipped)  
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
