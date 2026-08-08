# Mako Self-Hosted Compiler Migration and `main` Parity Plan

**Repository:** `loreste/mako`  
**Prepared:** 2026-08-08  
**Compared refs:** `selfhosted` at `a2749f5` and `main` at `f638e64`  
**Merge base:** `d54c190`  
**Audience:** AI coding agents and maintainers implementing the integration

## Mission

Make the compiler written in Mako the **canonical and only active production Mako compiler**. Reproduce the required behavior and safety fixes from historical `main` in `selfhosted` without importing its Rust/C implementation.

Parity is an intermediate milestone, not the destination. During the transition it means:

1. Everything supported by `main` remains supported and passes the same gates.
2. The self-hosted stage-1 ELF/backend gate remains present and passing.
3. Unsupported stage-1 input fails closed with deterministic diagnostics.
4. No documentation overstates the bootstrap state.
5. The integrated branch contains the native-backend safety fixes currently on `main`.

The final destination is stricter:

1. The Mako compiler builds the complete Mako compiler sources.
2. Stage 1 builds stage 2, and stage 2 builds a byte-identical or reproducibly equivalent stage 3.
3. Normal `mako build`, `mako run`, `mako test`, package, and bootstrap workflows use the Mako-written compiler.
4. The production compiler does not require Rust, Cargo, C, Clang/GCC, a system assembler, or an external linker.
5. Rust/C source and binaries are not part of the active Mako implementation or toolchain. Historical behavior may be captured as test vectors and expected outputs, but parity is reimplemented in Mako.
6. The compiler and generated programs are memory-safe by default, with unsafe operations explicit, narrow, and auditable.
7. Concurrency is race-resistant, cancellation-aware, bounded, and efficient under contention.
8. Compilation is fast and incremental, generated programs are performant, and both compiler and runtime use CPU resources intelligently.
9. The toolchain, cache, standard library, and generated artifacts are disk-efficient, reproducible, inspectable, and safely reclaimable.
10. Cryptography and secure transport follow current standards, support algorithm agility, and ship with secure defaults rather than requiring every application to become a security expert.
11. The standard library is broad enough for production systems work without routine dependency hunting, with Go's standard library used as the minimum capability benchmark rather than an API to copy blindly.
12. Mako ships a source-level debugger and an integrated developer toolchain suitable for building, testing, profiling, diagnosing, packaging, and maintaining production systems.
13. Cross-compilation is a core compiler property. Tier-1 targets require complete ABI, object/executable format, runtime, standard-library, debugger, test, and release support—not merely object emission.

## Non-functional product requirements

These requirements are part of language correctness. A self-hosted compiler that is unsafe, slow, wasteful, or unreliable under concurrency is not complete.

### Memory safety

- Safe Mako code must not cause use-after-free, double-free, invalid free, out-of-bounds access, use-after-move, uninitialized reads, iterator invalidation, or data races.
- Ownership, borrowing/lifetime rules, moves, clones, drops, escape analysis, and definite initialization must be represented in typed IR and verified before code generation.
- Every owned value must have exactly one valid terminal action per control-flow path: move, return, transfer, or drop.
- Partial initialization and partial moves of aggregates must use drop flags or an equivalent proven mechanism.
- Panic, early return, `?`, cancellation, task failure, `break`, and `continue` paths must release owned resources correctly.
- Bounds checks remain enabled unless an optimization pass proves them redundant. The proof must be invalidated when aliases or mutation make it unsafe.
- `unsafe` must be lexical, explicit, minimized, and reviewable. Unsafe primitives require documented preconditions and tests for invalid inputs.
- Foreign handles and FFI values must have typed ownership policies: borrowed, shared/refcounted, uniquely owned, or externally managed.

### Concurrency safety and scalability

- Structured concurrency is the default: tasks belong to a scope, cancellation propagates, and unjoined work cannot silently outlive its owner unless explicitly detached.
- `Send`/shareability rules prevent non-thread-safe values from crossing worker boundaries.
- Shared mutable state requires synchronization or a compiler-proven exclusive access path.
- Channels define ownership transfer, close behavior, wake-up semantics, backpressure, and destruction of buffered values.
- Blocking operations must not stall the entire scheduler. The runtime must distinguish CPU work, asynchronous I/O, timers, and unavoidable blocking calls.
- Scheduler queues, timers, and cancellation paths must not busy-spin while idle.
- The scheduler must avoid unbounded thread creation and provide bounded worker and queue policies.
- Atomics require documented memory ordering; defaulting everything to sequential consistency is safe but may be needlessly expensive, while relaxed ordering without proof is unacceptable.
- Race detection, deterministic stress tests, randomized schedule tests, deadlock/livelock timeouts, and high-contention benchmarks are required.

### Compilation speed

- Separate frontend, semantic, IR, optimization, and backend timings must be measurable with stable machine-readable output.
- Incremental compilation must hash inputs and dependencies precisely; editing one leaf module must not rebuild unrelated packages.
- Parse trees, type information, monomorphizations, package metadata, and object artifacts should be cached with versioned, deterministic keys.
- Multi-file compilation should parallelize independent work without making output nondeterministic.
- Avoid full-arena scans, repeated cloning, quadratic symbol lookup, repeated parsing, and repeated generic instantiation.
- Provide fast development and optimized release profiles. Expensive optimization passes must be selected by profile and measured benefit.
- Compiler memory growth must be bounded and proportional to live compilation state; arenas and caches need explicit release/eviction points.

### Generated-program performance and CPU efficiency

- Optimize for useful work per CPU cycle, not only wall-clock benchmark wins obtained by consuming more cores.
- Implement SSA-based constant propagation, dead-code elimination, copy propagation, common-subexpression elimination where profitable, loop simplification, devirtualization, escape analysis, and bounds-check elimination with correctness proofs.
- Add inlining with a cost model; never inline blindly. Account for call frequency, body size, code growth, cache pressure, and downstream optimization opportunities.
- Use profile-guided decisions where available, with deterministic static heuristics as the default.
- Register allocation must minimize spills while keeping compile-time cost appropriate to the selected build profile.
- Data layouts should reduce pointer chasing, padding, unnecessary allocation, copies, cache misses, and false sharing.
- Small values should stay in registers or stack storage when escape analysis proves it safe. Heap allocation is not the default solution for every aggregate.
- Strings, slices, maps, channels, and builders need capacity-growth policies that avoid both repeated reallocation and excessive retained memory.
- Runtime hot paths must avoid unnecessary locks, syscalls, allocations, wakeups, polling, and context switches.
- Idle programs must approach zero CPU consumption. Backoff and parking policies must be adaptive and measurable.
- Performance optimizations must preserve deterministic semantics and safety; an optimization that cannot prove correctness must not run.

### “Smart” optimization policy

“Smart” means evidence-driven and workload-aware, not opaque or unpredictable:

- Every optimization pass declares prerequisites, invariants, cost, expected benefit, and a verifier or differential test.
- Passes collect counters explaining what they changed and why they declined an optimization.
- Optimization budgets prevent pathological compile times on large functions or generic-heavy packages.
- The compiler recognizes cold paths, hot loops, allocation-heavy paths, blocking operations, and contention points when reliable evidence exists.
- Stable heuristics are used when profile data is absent; builds remain reproducible.
- Users can inspect timing, allocation, pass, and code-size reports without enabling debug builds.
- Performance regressions beyond the agreed budget fail CI instead of being documented after release.

### Disk and artifact efficiency

- Incremental caches must be content-addressed, deduplicated, versioned, checksummed, and safe to delete at any time.
- Cache keys must include compiler version, target, build profile, feature set, public dependency interface hashes, relevant environment inputs, and code-generation options—never timestamps alone.
- Store one canonical copy of identical parse, type, IR, monomorphization, object, package, and downloaded-module content.
- Separate stable shared artifacts from per-workspace metadata so clean builds do not duplicate large immutable content.
- Use compact binary serialization with explicit schema versions, bounded decoding, corruption detection, and deterministic ordering.
- Avoid embedding duplicate runtime, debug, generic, string, and metadata payloads in every object or executable.
- Apply dead-code/data elimination, identical-code folding where safe, compact unwind/source tables, string pooling, section garbage collection, and deterministic stripping profiles.
- Debug information must support split/debug-package output so release binaries remain small without losing diagnosability.
- Generic instantiations should be shared when ABI and semantics permit; prevent uncontrolled monomorphization and code-size explosions with budgets and diagnostics.
- Package downloads and build outputs need atomic writes, crash-safe manifests, least-recently-used and size-budgeted garbage collection, dry-run inspection, and explicit pinning for artifacts that must not be evicted.
- `mako cache status`, `mako cache gc`, `mako cache verify`, and `mako clean` must report exact byte counts and never delete unrelated user data.
- Release archives should use reproducible modern compression selected by measured decode speed, tooling availability, and size. Compression must never be applied repeatedly to already-compressed assets.
- The compiler must expose per-package artifact size, duplicate bytes, debug bytes, generic-instantiation bytes, and cache reuse so disk regressions are attributable.

### Cryptography engineering policy

- Do not design novel cryptographic algorithms or protocol variants. Implement published standards and validated constructions with known-answer, differential, interoperability, misuse, and side-channel tests.
- Cryptographic APIs must be high-level and safe by default. Low-level primitives live in clearly marked expert packages and must not make nonce reuse, unauthenticated encryption, weak randomness, or silent downgrade easy.
- Private keys, shared secrets, ephemeral secrets, and key schedules use dedicated secret types with zeroization, non-copy semantics, redacted formatting, controlled serialization, and locked-memory support where the platform permits.
- Secret-dependent operations must be constant-time with respect to secret data. No secret-dependent branches, table indexes, early exits, allocation sizes, or error distinctions unless the construction explicitly permits them.
- Randomness comes from the operating system CSPRNG through one audited interface. Failure to obtain secure randomness is fatal for cryptographic operations; there is no insecure fallback.
- Provide algorithm agility through typed suites and policy objects, not arbitrary strings. Policies define approved, deprecated, legacy-interoperability, experimental, and forbidden algorithms.
- Deprecation data is versioned and updateable. Insecure algorithms can be disabled centrally without changing every application.
- Hardware acceleration may be used only with runtime feature detection, constant-time fallbacks, cross-platform equivalence tests, and protection against illegal-instruction execution.
- Every parser and decoder is bounded against CPU, memory, recursion, and amplification attacks.
- Cryptographic errors avoid oracles while preserving actionable local diagnostics and audit events.
- Security-sensitive code requires fuzzing, property tests, published test vectors, independent review, dependency/SBOM tracking, and a vulnerability response process.

## Secure transport and cryptography baseline

This baseline is dated **2026-08-08** and must be reviewed at every release. “Latest” means the newest stable standards and errata accepted by the project security policy—not an automatically enabled Internet-Draft.

### TLS, DTLS, and QUIC

- Implement **TLS 1.3 per RFC 9846** as the default and required version for new Mako protocols.
- Keep TLS 1.2 only behind an explicit legacy interoperability policy. Disable SSLv2, SSLv3, TLS 1.0, and TLS 1.1. Reject static RSA, static DH/ECDH, and other deprecated key exchanges.
- Implement **DTLS 1.3 per RFC 9147**, including replay protection, connection IDs where applicable, retransmission behavior, cookie/DoS defenses, MTU handling, and loss/reordering tests.
- Implement QUIC transport and HTTP/3 with its TLS integration, including 0-RTT replay protections and application opt-in for replay-safe requests.
- Support X.509 parsing/path validation, hostname/IP verification, EKU/key usage, name constraints, revocation policy, OCSP stapling, certificate transparency hooks, delegated credentials when standardized/supported, SNI, ALPN, session tickets, PSK/resumption, mutual TLS, and secure key logging controls for diagnostics.
- Default TLS 1.3 AEAD suites should cover AES-128-GCM, AES-256-GCM, and ChaCha20-Poly1305. Choose using peer capabilities and local CPU features without fingerprint-prone or inconsistent policy behavior.
- Default groups/signatures should include X25519, P-256/P-384 as required for interoperability, and Ed25519 plus appropriate ECDSA/RSA-PSS certificate compatibility. Legacy algorithms are verification/interoperability-only when policy permits.
- Disable TLS compression and insecure renegotiation; cap handshake bytes, certificate-chain depth/size, fragment buffering, ticket counts, and concurrent handshakes.
- 0-RTT is off by default for application operations unless the API declares replay safety and the server enforces anti-replay policy.
- Conformance requires RFC vectors, malformed-handshake corpus, downgrade tests, resumption tests, cross-implementation interop, packet loss/reorder tests, fuzzing, and timing analysis.

### Core modern primitives

Provide reviewed implementations and safe constructions for:

- hashes/XOFs: SHA-256, SHA-384, SHA-512, SHA-3 family, SHAKE128/256, and BLAKE2; add BLAKE3 only with a clear interoperability/use-case policy;
- MAC/KDF: HMAC, HKDF, and approved password KDFs including Argon2id and scrypt, with PBKDF2 for required interoperability/compliance;
- AEAD: AES-GCM and ChaCha20-Poly1305; misuse-resistant alternatives only when standardized and justified;
- public key: X25519/X448, Ed25519/Ed448, NIST P-256/P-384/P-521, RSA-PSS/OAEP for compatibility, and strict key/point/signature validation;
- envelope/key agreement: HPKE with explicit suite selection and domain separation;
- authenticated formats: X.509/PKIX, PEM/DER, PKCS #8, PKCS #12 where needed, JWK/JWS/JWE/JWT with strict algorithms and no permissive `alg=none` behavior;
- secure comparison, zeroization, secret sharing only if standardized and reviewed, key wrapping, and deterministic test RNGs that cannot be imported accidentally into production code.

### Post-quantum readiness

- Implement the finalized NIST standards: **ML-KEM (FIPS 203)**, **ML-DSA (FIPS 204)**, and **SLH-DSA (FIPS 205)** with official vectors and errata tracking.
- Prefer hybrid classical+post-quantum key establishment for transitional deployments so failure of either family alone does not silently remove all protection.
- Enable post-quantum TLS groups only after stable IETF/IANA specifications and interoperability profiles exist. Draft identifiers and wire formats stay behind explicit experimental flags and never become default security claims.
- Inventory every stored key, certificate, signature, encrypted artifact, and protocol so cryptographic migration is possible without full application rewrites.
- Design keys and certificates with algorithm/parameter metadata, versioned serialization, rotation, revocation, expiry, and dual-signature/dual-certificate migration support.
- Benchmark handshake size, CPU, memory, latency, fragmentation, and denial-of-service exposure; post-quantum support must not introduce unbounded resource use.

### Compliance and assurance profiles

- Provide a modern default profile and optional FIPS-compatible policy boundaries where validated implementations/platform modules are available.
- Never claim FIPS validation merely because an algorithm appears in a FIPS document; validation applies to a specific cryptographic module and operational boundary.
- Maintain a machine-readable algorithm registry containing standard, status, minimum key size, approved uses, deprecation date, replacement, test-vector source, and implementation owner.
- Run cryptographic known-answer tests at build/test time and lightweight startup self-tests where the selected compliance profile requires them.

## Product decision

The self-hosted line is the future of Mako, not an optional experimental backend. Treat `main` as the behavior and safety baseline to absorb, and treat `selfhosted` as the architecture that must ultimately replace it.

Until the fixed point and clean-machine gates pass, releases must clearly label the Mako-written compiler as bootstrap/stage-1. Do not make it the default based only on fixture parity; first prove that it can rebuild itself.

## Absolute implementation rule: 100% Mako source

The final production toolchain must be implemented **100% in Mako**. This is a source-ownership and dependency-graph requirement, not a marketing description.

The canonical implementation of every required component must be Mako source:

- lexer, parser, resolver, type checker, ownership/concurrency verifier, typed IR, optimizers, instruction selection, register allocation, and code generation;
- machine-code encoders, ELF/Mach-O/PE-COFF/WebAssembly writers, static/dynamic linkers, symbol/relocation handling, build IDs, debug/unwind emitters, and resource embedding;
- process entry, allocator, strings, slices, maps, files, clocks, randomness, networking, DNS, concurrency scheduler, atomics/synchronization, panic/unwind, TLS/DTLS/QUIC, cryptography, and platform runtime;
- standard library and first-party extended library components required by the supported product profile;
- build driver, package manager, registry/signature verification, test/benchmark/fuzz runner, formatter, linter, documentation generator, debugger, profiler, tracer, coverage tools, object inspection, cache manager, migration tool, language server, and IDE protocols;
- bootstrap orchestration, cross-target tooling, sysroot/package construction, release packaging logic, conformance tools, and CI helper programs that are part of the maintained Mako product.

### What is allowed

- A versioned, reproducible **trusted seed executable** may be distributed to start the first build on a machine that has no Mako compiler. The seed is an artifact, not the canonical implementation.
- The operating-system kernel and stable platform APIs/syscalls are external execution platforms, not language implementation dependencies.
- Firmware and hardware instructions may be used through Mako-written runtime code.
- User applications may call foreign libraries through an optional, explicit FFI. FFI support does not permit the Mako compiler/toolchain itself to require those libraries.
- Historical `main` outputs, diagnostics, fixtures, protocol traces, benchmark records, and documented behavior may be imported as inert test evidence. The Rust/C programs that created them are not invoked by the Mako build or test process.

### What is forbidden at final cutover

- No Rust or Cargo source, binary, library, build metadata, implementation component, or helper.
- No required C/C++ source, compiler, runtime shim, libc dependency, or build script.
- No LLVM, Cranelift, GCC, Clang, system assembler, or system linker invocation.
- No Python, JavaScript, shell, Go, or another language implementing a required product tool.
- No opaque native cryptographic/TLS library standing in for a Mako implementation.
- No debugger, package, test, documentation, LSP, cross-compilation, or release feature that works only by launching a non-Mako helper.
- No generated source checked in from a non-Mako generator unless the generator itself is replaced by Mako and reproducibility is proven.
- No silent fallback from Mako to a legacy implementation.

Shell/CI configuration may invoke the already-built Mako tools to orchestrate a hosted CI service, but product logic must live in Mako. If deleting every legacy source checkout and removing every development-language tool from `PATH` breaks any supported build, test, debug, package, cross-compile, or stage operation, the toolchain is not 100% Mako.

### Trusted seed model

Self-hosting cannot begin from source on a machine with no compiler, so the project must define a small, auditable trust anchor:

1. Publish the seed binary, source commit, target, build recipe, checksum, signature, provenance, and reproducibility evidence.
2. Use the seed only to build Mako stage 1 from canonical Mako source.
3. Use stage 1 to build stage 2 from the identical source.
4. Use stage 2 to build stage 3 and prove the fixed point.
5. Build all distributed tools and libraries from the fixed-point compiler, not by copying functionality from the seed.

Maintain at least one recovery seed per supported bootstrap architecture, initially AMD64 and ARM64. Minimize seed size and privilege, but do not create a separate permanently underpowered compiler language: the seed must build the real canonical sources.

### Language purity audit

Add `mako audit purity` to produce a machine-readable dependency and provenance report:

- enumerate every executable, dynamic library, file, environment variable, network access, and child process used during stages 1–3;
- classify each dependency as seed, operating-system interface, Mako artifact, source input, or forbidden;
- scan release artifacts and build manifests for Rust/C/C++/LLVM/Cranelift/system-linker dependencies and unexpected dynamic libraries;
- fail on undeclared child processes, tool discovery through `PATH`, build scripts in another language, or runtime loading of legacy components;
- verify that every shipped executable maps to canonical Mako source and a fixed-point build record;
- emit an attestation suitable for release publication.

The purity audit is required from the beginning of the migration and is release-blocking. It must run inside a minimal clean environment where Rust, Cargo, C/C++ source and compilers, libc development files, assemblers, system linkers, Python, Node.js, Go, and legacy compiler binaries are unavailable after the trusted seed starts stage 1.

## Verified branch state

The branches have diverged:

- `main` contains **12 commits** not in `selfhosted`.
- `selfhosted` contains **1 commit** not in `main`.
- The comparison reports **35 changed files** on `main` relative to `selfhosted`.
- `main` reports **395/395 Mako fixtures** and **140 Rust unit tests** at the DTLS/WSI point.
- `selfhosted` adds a Mako-written stage-1 Linux x86-64 ELF backend, deterministic image pins, frontend/negative gates, and a dedicated Linux execution job. It explicitly does **not** prove a stage-1/stage-2 fixed point.

The safe interpretation is: integrate two active lines of compiler work and validate both, rather than treating either branch as a simple superset.

## Mainline behavior that must be reimplemented in Mako

Use these commits to extract requirements, tests, expected diagnostics, protocol behavior, and safety regressions. **Do not cherry-pick their Rust/C implementation into the self-hosted product branch.** Implement each outcome in canonical `.mko` compiler/runtime/library/tool sources and commit the Mako work in the same chronological dependency order.

| Order | Commit | Required outcome |
|---:|---|---|
| 1 | `8aa5305` | Honest claims; mandatory memory-safety, sanitizer, and native-backend CI gates |
| 2 | `fa1f744` | CI installs `ripgrep` and `clang`; accurate test counts |
| 3 | `1d0746c` | Preserve the distinction between known-leak fixtures and the true zero-leak subset in Mako-native instrumentation |
| 4 | `2c9476c` | Backend selection no longer invalidates native-specific memory tests |
| 5 | `8bd662e` | Keep the struct-key known leak out of zero-leak claims until fixed and proven by Mako-native instrumentation |
| 6 | `145a564` | DTLS 1.2, SRTP key export, DCE underscore/mangled-pack fix, flushed native output, v0.5.1 docs |
| 7 | `f79d193` | Real WSI on Cocoa/X11/Win32 plus window/input builtins and platform linking |
| 8 | `f430ad9` | Headless WSI behavior in CI and conditional X11 detection/include |
| 9 | `c8595cc` | WASI documentation and corrected `wasm32-wasip1` target names |
| 10 | `24f36a6` | Native multi-module support, correct IR diagnostics, missing bridge builtins, handle coercions, wide nested structs |
| 11 | `5ef5186` | Installed distributions include `native_bridge.c` and `native_runtime.c` |
| 12 | `f638e64` | Null moved-from string slots; complete slice-field clone/drop handling |

### Non-negotiable release blockers

The integration must not ship without these three recent correctness fixes:

- **`f638e64`: moved-from slot clearing.** A transferred string local must be nulled immediately. Otherwise an ownership snapshot restored after an `if` can drop a stale pointer, causing use-after-free or double-free. Preserve the added `FloatSlice`, `ByteSlice`, `BoolSlice`, and `Builder` struct clone/drop handling.
- **`24f36a6`: shared-IR multi-module behavior.** Preserve the bridge builtin registrations, `Opaque`/`I64` domain-handle coercions, wide nested-struct support, and preference for the most truthful shared-IR diagnostic when both native paths fail.
- **`5ef5186`: installed runtime sources.** Installed-prefix native builds must compile the current `native_bridge.c` and `native_runtime.c`, not accidentally link stale installed copies.

## Required migration strategy

Work from `selfhosted`, not from the Rust `main` implementation:

```bash
git fetch origin main selfhosted
git switch -c integrate/mako-only-selfhost origin/selfhosted
```

For each missing mainline capability:

1. Read the commit's behavior, tests, security rationale, and user-visible contract.
2. Write or port the regression test into the Mako test format without retaining a Rust/C helper.
3. Implement the capability in Mako.
4. Run it with the trusted seed-built stage 1 and every later stage.
5. Record parity evidence and the purity-audit result.

`main` is a requirements archive and historical oracle, not the code foundation. This avoids importing the architecture being replaced.

After capability parity and fixed-point proof, replace `main` with the reviewed Mako-only line. Preserve the old implementation only as archived history/tag outside the active source and release dependency graph. Every new feature must be implemented in Mako and covered by self-host tests.

Do not solve parity by copying Rust/C modules into new locations, translating them at build time, wrapping them behind FFI, or shipping precompiled native libraries. Port the behavior and algorithms into reviewable Mako source.

## Porting rules by historical implementation area

The paths below identify where the old behavior currently lives. They are inputs to analysis only. The delivered implementation belongs in Mako source.

### 1. `.github/workflows/ci.yml`

The final workflow must contain both families of gates:

- existing mainline build/test matrix;
- mandatory native backend suite;
- memory-safety gate;
- sanitizer quick sweep;
- headless WSI environment (`MAKO_GFX_HEADLESS=1`);
- Linux x86-64 self-host gate that **executes** generated ELF images;
- correct backend pinning for C-only sanitizer/cross/Windows jobs;
- `MAKO_RUNTIME` wherever native linking requires it.

Do not mark mandatory jobs `continue-on-error`, hide failures behind conditional skips, or let a global `MAKO_BACKEND=c` override a test that explicitly requires native.

### 2. `Makefile`

Retain all current install behavior, including installation of runtime headers **and** `runtime/native_bridge.c` and `runtime/native_runtime.c`. Also retain:

```make
selfhost:
	MAKO_RUNTIME="$(CURDIR)/runtime" bash scripts/selfhost-gate.sh
```

Keep `selfhost` in `.PHONY` and in help output. Verify staged-prefix installation rather than only in-tree execution.

### 3. Historical driver behavior (`src/main.rs`)

Reimplement required driver behavior in the Mako-written CLI, package orchestrator, target manager, and compiler. Do not preserve the Rust driver as a launcher.

Required regression checks:

- an in-tree native build uses the in-tree runtime;
- an installed native build uses the installed current runtime sources;
- Linux links X11 only when detected;
- headless builds do not require a display server;
- macOS and Windows retain the required WSI system libraries;
- WASI uses `wasm32-wasip1` and rejects unsupported native sources/features clearly.

### 4. Historical IR/codegen behavior (`src/native_ir.rs`, `src/codegen/mod.rs`, and `src/native_codegen.rs`)

This is the highest-risk semantic port. Reimplement in the Mako-written IR, verifier, optimizer, and backends:

- current native aggregate and nested-struct lowering;
- all bridge builtins required by multi-module applications;
- domain-handle coercion rules;
- ownership move/clone/drop semantics;
- moved-from slot nulling;
- every added slice kind in struct clone/drop;
- self-host frontend/IR performance improvements;
- fail-closed self-host lowering and deterministic output.

Never “fix” a stage-1 rejection by silently falling back to a different backend inside the self-host proof. During migration, a normal product build may follow a documented compatibility fallback policy. At final cutover, unsupported input must be a compiler error unless the missing capability is explicitly outside the language specification.

### 5. Historical type/builtin behavior (`src/types/mod.rs`)

Define all required builtin/type registrations in Mako for DTLS, WSI, HTTP/proxy/TLS/UDP/JWT/event-loop operations and self-host primitives. Add tests that enumerate the builtin registry so a future change cannot silently omit one family.

### 6. Historical runtime behavior

Use the following old files only to inventory required behavior:

- `runtime/mako_dtls.h`;
- `runtime/mako_wsi.h`;
- updated `runtime/mako_domain.h`;
- behavior formerly in `runtime/native_bridge.c`;
- behavior formerly in `runtime/native_runtime.c`;
- self-host syscall/ELF support where introduced by `a2749f5`.

Reimplement all required behavior in Mako modules and direct platform APIs/syscalls. The final runtime package must contain no C source, C object, C header implementation, libc dependency, or native bridge. Verify symbols, registrations, installation manifests, and target dependencies as one atomic interface.

### 7. DCE

Keep the mangled alias reachability fix from `145a564`. Add a test where an underscored pack function such as `ctx_new` is reachable only through its mangled alias. Run the same program through C, native, and the stage-1 subset when supported.

### 8. Documentation and version metadata

Merge facts; do not concatenate stale claims. At minimum reconcile:

- `README.md`
- `CHANGELOG.md`
- `docs/STATUS.md`
- `docs/ROADMAP.md`
- `docs/RELEASE.md`
- `docs/SECURITY.md`
- `docs/BUILTINS.md`
- `docs/STDLIB.md`

Required wording:

- historical `main` used a native/Cranelift product path, but the active self-hosted product does not;
- the Mako-written compiler is a stage-1 subset, not a completed self-host;
- Linux x86-64 deterministic static ELF emission is real and gated;
- a stage-1 → stage-2 → stage-3 fixed point is still pending;
- WASI is Preview 1 and does not imply browser DOM, sockets, or TLS support;
- WSI platform/headless limitations are explicit;
- package signing limitations remain explicit;
- test counts are generated or verified, never copied forward by assumption.

## Standard library parity program

The goal is at least the production breadth developers expect from Go: networking, security, data formats, operating-system integration, testing, profiling, and tooling must work together with consistent APIs. Do not measure parity by package count alone. Measure API completeness, correctness, portability, performance, documentation, examples, fuzz coverage, and real application capability.

### Tier 0 — foundations required by every package

| Domain | Required Mako packages/capabilities |
|---|---|
| Core values | strings, bytes, runes/Unicode, slices, arrays, maps, sets, options/results, ordering, comparison, hashing |
| Numeric | integer/float math, bits, big integers/rationals/floats, complex numbers, checked/saturating/wrapping arithmetic, random distributions |
| Memory | allocators, arenas, buffers/builders, ownership helpers, atomics, weak/shared references where justified, secure secret buffers |
| I/O | reader/writer interfaces, buffered I/O, seeking, pipes, copy/limit/tee/multi readers and writers, async adapters |
| Errors | wrapped/typed errors, causes, source locations, stack traces, classification, retryability, multi-errors |
| Time | monotonic and wall clocks, durations, timers, tickers, deadlines, time zones, parsing/formatting |
| Context | cancellation, deadlines, scoped values, propagation across tasks and network requests |
| Reflection | safe type metadata, field/tag inspection, dynamic construction where needed, no unrestricted memory unsafety |
| Text | Unicode normalization, case folding, segmentation, width, UTF encodings, regex, scanning, formatting, templates |

### Tier 1 — systems and application essentials

| Domain | Required Mako packages/capabilities |
|---|---|
| Filesystem | paths, files, directories, metadata, permissions, links, walking, temporary files, atomic replace, file locking, watching |
| OS/process | args/env, signals, subprocesses, pipes, users/groups, terminals, exit status, resource limits, platform feature detection |
| Archives/compression | tar, zip, gzip/deflate, zlib; modern zstd support; streaming APIs and archive bomb limits |
| Encoding | base16/32/64, binary endian/varints, CSV, JSON, XML, ASN.1 DER, PEM, hex, text escaping, MIME |
| Networking | IP/CIDR, DNS, TCP, UDP, Unix sockets, socket options, deadlines, resolvers, proxy dialing, Happy Eyeballs |
| HTTP | HTTP/1.1, HTTP/2, HTTP/3, client/server, routing, cookies, multipart, proxies, trailers, streaming, connection pools |
| URLs/mail | URI/URL parsing, query escaping, IDNA, MIME, mail address/message parsing, SMTP client basics |
| Security | crypto primitives, secure RNG, X.509/PKIX, TLS 1.3, DTLS 1.3, SSH, HPKE, key formats, secure tokens |
| Concurrency | tasks/scopes, channels/select, mutex/RW lock, conditions, semaphores, barriers, once, wait groups, atomics, concurrent queues |
| Databases | `database` interfaces, connection pooling, transactions, cancellation, prepared statements, rows/scanning, driver contracts |

### Tier 2 — production completeness

| Domain | Required Mako packages/capabilities |
|---|---|
| Testing | unit tests, subtests, table tests, examples, benchmarks, fuzzing, snapshots/goldens, coverage, race/sanitizer integration |
| Observability | structured logging, tracing context, metrics primitives, runtime/compiler profiling, stack/heap/task dumps |
| Debugging | symbol/source maps, backtraces, crash reports, pprof-compatible or equivalent CPU/heap/block/mutex profiles |
| RPC/protocols | reusable HTTP/RPC foundations, WebSocket, Server-Sent Events, JSON-RPC; gRPC/Protobuf may be first-party extended library |
| Data | SQL driver interfaces, JSON streaming, XML streaming, CSV, text/template and HTML-safe template engines |
| Networking advanced | QUIC, multicast, raw socket policy, packet parsing, DNS client/server building blocks, TLS listeners/dialers |
| Platform | Linux, macOS, Windows abstractions with capability queries; WASI subset with explicit unsupported operations |
| Build/tooling | package metadata, semantic versions, module fetching, checksums/signatures, embed resources, code generation hooks, documentation |

### First-party extended library

Not every useful capability belongs in the immutable core distribution. Maintain a first-party, release-coordinated extended library for frequently needed production features:

- PostgreSQL, MySQL/MariaDB, SQLite, Redis, and common database drivers;
- Protobuf, gRPC, OpenAPI, WebSocket, MQTT, AMQP, SIP, RTP/RTCP, SRTP, STUN/TURN/ICE, and DNS server components;
- OAuth 2.0/OIDC, ACME, JOSE, SSH/SFTP, certificate automation, secret-store interfaces;
- YAML, TOML, MessagePack, CBOR, BSON, and schema validation;
- cloud object storage and portable cloud authentication interfaces;
- OpenTelemetry-compatible tracing/metrics/log export;
- image/audio primitives only when they meet security, size, and maintenance gates.

Core packages receive the strongest compatibility guarantee. Extended packages may evolve faster but use the same security, documentation, testing, and performance standards.

### Standard library quality gate

Every package requires:

- a stable ownership and concurrency contract;
- context/deadline/cancellation support for blocking or remote work;
- streaming APIs for potentially large data;
- bounded memory/CPU/disk behavior for untrusted input;
- platform support matrix and explicit unsupported errors;
- examples, API docs, unit/property/fuzz tests, benchmarks, and malformed-input corpus;
- deterministic behavior where the protocol permits it;
- no hidden global mutable state;
- semantic-version/API compatibility review;
- dependency, binary-size, compile-time, CPU, allocation, and disk-cost accounting.

### Go parity tracking method

- Generate a versioned inventory of Go standard-library packages and exported capability groups from the official documentation.
- Map each capability to `complete`, `partial`, `planned`, `not applicable`, or `extended library` in Mako.
- For `not applicable`, document the language-design reason and the Mako replacement.
- Add representative application gates: HTTP service, TLS client/server, CLI, file processor, concurrent worker, database service, package tool, test/fuzz suite, and cross-platform application.
- Publish the matrix with every release. A package name without working behavioral coverage does not count as parity.

## Language/compiler extensions required for the library

To reach this standard-library breadth without fragile workarounds, extend Mako in this order:

1. **Complete aggregates and ownership:** structs, enums/tagged unions, tuples where justified, field visibility, nested aggregates, partial initialization/moves, destructors, move-only secret/handle types, and `[]Struct`.
2. **General generics and interfaces:** constrained generics, associated behavior where needed, zero-cost static dispatch, explicit dynamic interface objects, coherent method resolution, and controlled monomorphization.
3. **Error model:** typed errors, wrapping/causes, `?`, cleanup on every propagation path, panic boundaries, and safe FFI error conversion.
4. **Concurrency type system:** `Send`/shareability, scoped tasks, cancellation, async I/O integration, atomics and memory model, synchronization primitives, and thread-local storage.
5. **Const and compile-time facilities:** capable constant evaluation, generated lookup tables, layout assertions, conditional compilation, target features, embedded files, and safe code-generation hooks.
6. **Binary/layout control:** fixed-width integers, endian operations, bitfields or safe bit manipulation, alignment, packed/repr attributes, ABI declarations, unions limited to unsafe/verified contexts, and checked pointer arithmetic.
7. **Function values and callbacks:** closures, function pointers, vtables/interface calls, C callbacks during transition, and no-allocation callback paths.
8. **Reflection and tags:** bounded runtime metadata plus compile-time derive/code generation for serialization; avoid forcing reflection into performance-critical paths.
9. **SIMD and CPU features:** portable vectors, target-feature multiversioning, runtime dispatch, intrinsics behind safe wrappers, and scalar fallbacks.
10. **Module/package system:** reproducible resolution, checksums, signatures, lockfiles, workspaces, feature control without exponential combinations, private registries, offline/vendor mode, and vulnerability metadata.
11. **Cross-platform I/O backend:** epoll, kqueue, IOCP, io_uring when beneficial, and WASI polling behind one cancellation/deadline-aware interface.
12. **Optional foreign ABI boundary:** opaque handles, ownership annotations, generated bindings, callback safety, and foreign calling conventions for user interoperability. No foreign ABI is required to build or run the Mako toolchain itself.
13. **Fuzzing/sanitizer hooks:** coverage-guided fuzz instrumentation, race detection support, address/undefined behavior checks for unsafe/FFI paths, and deterministic scheduler hooks.
14. **Compiler plugins cautiously:** prefer declarative derives and out-of-process tools; do not make arbitrary in-process compiler plugins part of the trusted computing base.

For every extension, update the language specification, parser, semantic types, IR, ownership verifier, optimizer, every active backend, formatter, documentation, positive/negative tests, fuzz corpus, and self-host compiler before declaring it shipped.

## Debugger program

Build a native Mako source debugger, provisionally `mako debug`, with a reusable debug protocol/library rather than hard-wiring all behavior into one CLI.

### Required debugger capabilities

- launch, attach, detach, continue, pause, restart, terminate, and core/minidump inspection;
- source, function, address, conditional, hit-count, and log breakpoints;
- step in, step over, step out, instruction step, run to cursor, and reverse execution only when a supported recording backend exists;
- stack frames, threads/tasks, scheduler state, locals, arguments, globals, closures, captured values, interfaces, enums, maps, slices, strings, channels, handles, and ownership state;
- safe expression evaluation using the program's types without silently mutating state;
- memory/register/disassembly views for expert use, clearly separated from safe source-level views;
- exception/panic, signal, allocation, task creation, channel operation, and data watchpoints where hardware/OS support permits;
- async-aware logical stacks that connect task creation, await/block points, cancellation, and worker execution;
- pretty-printer/provider API for standard and application types with strict execution/time/memory limits;
- remote debugging with authenticated encrypted transport and an explicit opt-in listener;
- machine interface compatible with IDE adapters, preferably Debug Adapter Protocol support through a first-party adapter;
- GDB/LLDB interoperability where DWARF permits, plus WinDbg-compatible symbols/minidumps for Windows.

### Compiler/debug information requirements

- Emit accurate DWARF for ELF/Mach-O and PDB/CodeView-compatible information for PE/COFF; support split debug files and source bundles.
- Preserve source spans through parsing, monomorphization, inlining, optimization, async/task lowering, and machine-code emission.
- Emit variable location lists, inline call sites, type layouts, unwind data, frame information, symbol names, and deterministic path remapping.
- Define optimized-debug behavior: variables may be optimized out only with an honest explanation; stepping must not fabricate source events.
- Make panic/crash reports useful without the debugger: symbolized stack, task ancestry, ownership/resource census, build ID, and source mapping.
- Keep debug metadata optional and separable so release binary and disk budgets remain enforceable.

### Debugger safety and tests

- Debug expression evaluation runs in a constrained mode and must not execute arbitrary target code by default.
- Remote attach requires authentication, authorization, encryption, audit logging, and explicit bind configuration.
- Add debugger protocol tests, scripted sessions, optimized/unoptimized stepping goldens, async/task tests, stripped/split-debug tests, attach/core tests, and malformed-symbol fuzzing.
- Validate breakpoint and stepping behavior on every Tier-1 OS/architecture pair.

## Full testing and verification program

“Full test suite” means multiple independent layers; a single fixture directory is insufficient.

| Layer | Required coverage |
|---|---|
| Lexer/parser | positive grammar, malformed input, recovery, spans, Unicode, nesting/size limits, fuzzing |
| Semantic/types | resolution, inference, generics, interfaces, constants, visibility, ownership, Send/shareability, compile-fail diagnostics |
| IR/verifiers | construction invariants, CFG/SSA, dominance, ownership linearity, type/layout validity, serialization round trips |
| Optimizers | per-pass unit/property tests, verifier after every pass, differential optimized/unoptimized execution, miscompile fuzzing |
| Backends | instruction selection, register allocation, spills, ABI, relocations, unwind/debug data, object validation, disassembly goldens |
| Runtime | allocation/drop, panic, files, clocks, randomness, sockets, DNS, TLS, scheduler, tasks, channels, cancellation, shutdown |
| Standard library | unit, property, malformed input, fuzz, concurrency, examples, benchmarks, platform tests, protocol interop |
| Tooling | CLI contracts, formatter idempotence, linter fixes, package resolution, cache, docs, debugger, profiler, coverage, LSP |
| Bootstrap | stage 0→1→2→3, hashes, diagnostics, clean environment, forbidden-dependency trace, repeat build |
| Cross-target | compile, link, inspect, boot/run, standard-library subset, debugger/symbol validation per target |
| Security | vectors, fuzzing, side channels, downgrade, malformed protocols, secret zeroization, unsafe audit, supply chain |
| Performance | compiler wall/CPU/RSS/disk, generated code cycles/allocations/cache, concurrency scaling, startup and tail latency |
| Compatibility | language spec, API compatibility, package lockfiles, artifact formats, previous release corpus |

Required methods include unit, integration, end-to-end, compile-pass, compile-fail, golden, snapshot, property-based, metamorphic, differential, mutation, coverage-guided fuzz, stress, soak, fault-injection, deterministic-scheduler, sanitizer, race, model-checking for small concurrency primitives, benchmark, and interoperability tests.

### Test infrastructure requirements

- One manifest describes expected result, target constraints, backend/stage, timeout, resources, feature flags, diagnostics, and artifacts for each test.
- Tests are hermetic by default, use temporary roots, pin time/random/network behavior when needed, and cannot read undeclared host state.
- Sharding and caching reduce CI time without hiding flaky behavior.
- Flaky tests are failures: quarantine requires an issue, owner, expiry, retained seed/log, and cannot cover a release blocker.
- Failures retain compiler invocation, environment allowlist, seed, reduced input, stage hashes, logs, traces, and produced artifacts.
- Coverage tracks source lines/branches plus language features, IR operations, optimization rules, instructions, ABI cases, standard-library APIs, error paths, and target pairs.
- Mutation testing is required for ownership verifier, bounds checks, cryptographic policy, package verification, and critical optimizer rules.
- A test is not counted twice merely because the same source is launched by two wrappers.

## Developer tooling expansion

Ship a coherent toolchain under the `mako` command with stable machine-readable output and library APIs:

| Command/capability | Required function |
|---|---|
| `mako build/run/test` | reproducible builds, profiles, targets, features, structured reports |
| `mako check` | fast semantic/ownership checking without code generation |
| `mako fmt` | deterministic, idempotent formatting and check/diff modes |
| `mako lint` | correctness, security, concurrency, performance, portability, deprecation, and API lints with safe fixes |
| `mako doc` | local/API documentation, cross-links, examples, search index, versioned output |
| `mako debug` | source debugger described above |
| `mako profile` | CPU, wall, heap, allocation, block, mutex, scheduler, I/O, and compiler-phase profiles |
| `mako cover` | source/branch plus feature/IR coverage and mergeable profiles |
| `mako bench` | statistically sound benchmarks, comparisons, budgets, CPU counters where available |
| `mako trace` | tasks, scheduler, channels, timers, network, syscalls, GC-free allocator/resource events |
| `mako pkg` | init/add/remove/update/vendor/publish/audit/sign/verify/tree/why, offline and private registries |
| `mako target` | list/add/remove/info/doctor target support, sysroots, runners, emulators, link policy |
| `mako cache` | status/verify/gc/prune/export/import with exact disk accounting |
| `mako doctor` | toolchain, target, linker/runtime, certificates, cache, environment, and reproducibility diagnostics |
| `mako objdump/size` | sections, symbols, relocations, disassembly, debug data, code/data/generic bloat reports |
| `mako fix/migrate` | language/API edition migrations with preview and reversible patches |
| `mako lsp` | first-party language server: diagnostics, completion, navigation, rename, references, semantic tokens, actions |

Also provide shell completion, man pages, editor integrations, stable JSON/SARIF where appropriate, build IDs, response files, reproducible path remapping, and a documented tooling protocol/version policy.

## Cross-compilation and architecture program

Support is tiered so claims stay honest.

### Tier 1 — release blocking

| Architecture | Required operating systems/formats |
|---|---|
| `x86_64` / AMD64 | Linux ELF/glibc and musl, Windows PE/COFF, macOS Mach-O |
| `aarch64` / ARM64 | Linux ELF/glibc and musl, Windows PE/COFF, macOS Mach-O |

Every Tier-1 pair requires hosted and cross builds, runtime/stdlib completeness, package artifacts, debugger symbols/stepping, unwind/crash handling, sanitizer strategy, full applicable tests, representative applications, and CI on real hardware for release qualification.

### Tier 2 — supported, not every release blocker

- `riscv64` Linux ELF;
- `wasm32-wasip1` and the project's explicitly selected future WASI component target;
- `armv7` Linux hard-float where ecosystem demand justifies it;
- `x86` Windows/Linux for legacy compatibility if maintained;
- `s390x`, `ppc64le`, and `loongarch64` Linux based on runtime/CI availability.

Tier 2 requires compiler, linker/object, runtime subset, standard-library support matrix, emulator plus periodic real-hardware validation, and documented debugger limitations.

### Tier 3 — experimental

- bare-metal/freestanding profiles;
- additional embedded ARM/RISC-V targets;
- kernels, firmware, accelerators, and architectures without maintained CI.

Experimental targets never imply standard-library, debugger, TLS, threads, process, or filesystem completeness.

### Target implementation checklist

For each OS/architecture pair implement and test:

- target triple, data layout, endian, pointer width, alignment, atomics, CPU feature model, relocation and TLS models;
- calling convention/ABI, varargs policy, stack alignment, red zone, registers, aggregate passing/return, unwind convention;
- instruction selection, encoding, register allocation classes, spills, branch ranges/veneers, atomics, SIMD, feature multiversioning;
- ELF, Mach-O, PE/COFF, or WebAssembly writer; symbols, relocations, sections, imports/exports, dead stripping, build IDs;
- executable/static/shared library production and a self-hosted linker path where required by the no-external-linker goal;
- process entry, syscalls/platform APIs, allocator, clocks, randomness, threads, synchronization, sockets, DNS, files, signals/exceptions;
- standard-library capability matrix, TLS/crypto acceleration, debugger/unwind format, profiler/coverage support;
- emulator/runner configuration plus periodic real-hardware execution.

Cross compilation must not require executing target binaries during the build unless a declared runner/emulator is configured. Build scripts and code generation run on the host with explicit host-vs-target APIs.

### Cross-target release gates

- [ ] `mako build --target` works from each Tier-1 host to every Tier-1 target where platform SDK licensing permits.
- [ ] Clean sysroots/toolchain components are versioned, checksummed, signed, relocatable, and disk-budgeted.
- [ ] Hello, compiler torture corpus, runtime suite, standard-library application suite, TLS client/server, concurrency stress, and debugger smoke execute on each Tier-1 target.
- [ ] ABI conformance tests use checked-in binary/object fixtures and platform ABI specifications without compiling or invoking C.
- [ ] Object validators and platform loaders accept all artifacts; symbols, unwind, relocations, and reproducibility are checked.
- [ ] Cross-built and natively built outputs are behaviorally equivalent, and reproducibly identical where inputs/toolchain are identical.
- [ ] Architecture-specific optimizations always have portable semantic tests and safe fallbacks.

## Implementation workstreams

### Workstream A — establish a reproducible baseline

- [ ] Record exact SHAs for `main`, `selfhosted`, and the merge base.
- [ ] Import historical expected behavior, diagnostics, fixtures, security regressions, protocol traces, benchmark results, and image hashes as inert evidence.
- [ ] Convert all required tests to the Mako test format.
- [ ] Record OS/architecture, seed identity, Mako stage hashes, target/runtime versions, and environment allowlist.
- [ ] Do not execute the historical Rust/C implementation to make the active suite pass.

### Workstream B — integrate and compile

- [ ] Start from `origin/selfhosted` and implement each missing capability in `.mko` source.
- [ ] Run `mako fmt --check`, `mako check`, and the Mako compiler/tooling tests.
- [ ] Build development and release profiles using the seed-built Mako stage.
- [ ] Run `mako audit purity` and fail on Rust, C/C++, Cargo, LLVM/Cranelift, external assembler/linker, or opaque native helper use.
- [ ] Verify that compiler/runtime discovery works both in-tree and from a temporary Mako-only install prefix.

### Workstream C — restore product parity

- [ ] Port and run all 395+ historical fixtures through the Mako-written compiler/backend.
- [ ] Run the complete Mako-native backend fixture suite.
- [ ] Run DTLS loopback and isolated black-box interoperability tests against independent implementations; these are test peers, never build/runtime dependencies.
- [ ] Run WSI in headless mode in CI.
- [ ] On supported hosts, run a real-window WSI smoke test for Cocoa, X11, and Win32.
- [ ] Run WASI args/environment/filesystem examples with `wasm32-wasip1`.
- [ ] Test a multi-file application exercising HTTP/proxy/TLS/UDP/JWT/event-loop bridge builtins.
- [ ] Test installed-prefix native compilation from outside the repository.

### Workstream D — preserve and extend the self-host proof

- [ ] Run `make selfhost` on Linux x86-64.
- [ ] Ensure every generated ELF is executed, not merely hashed.
- [ ] Preserve deterministic image pins; update a pin only after reviewing and documenting the byte-level reason.
- [ ] Run every positive self-host fixture through the stage-0 oracle and stage 1, comparing exit status and stdout/stderr.
- [ ] Run every negative fixture and compare diagnostic class, source location, and nonzero exit status.
- [ ] Confirm no fixture invokes Rust, C, Cranelift, a system assembler, a system linker, or another-language helper.
- [ ] Re-measure how many `compiler/*.mko` functions stage 1 lowers; publish the count and denominator from tooling, not prose.

### Workstream E — safety regressions

- [ ] Add a regression fixture for ownership transfer inside an `if`, followed by scope exit; run under Mako-native guarded-allocation, UAF, double-free, and leak instrumentation.
- [ ] Add struct clone/drop tests covering float, byte, bool, and builder slice fields.
- [ ] Add multi-module nested-string-struct coverage modeled on the failure fixed by `24f36a6`.
- [ ] Run `scripts/memory-safety-gate.sh` with no global backend override.
- [ ] Run `scripts/sanitize-all.sh --quick`.
- [ ] Keep zero-leak and UAF-safety claims separate. Known exit-path leaks must be documented and must not be presented as passing zero-leak coverage.

### Workstream F — CI and release evidence

- [ ] Make branch protection require the native suite, memory-safety gate, sanitizer sweep, and Linux self-host execution job.
- [ ] Upload or retain self-host hashes and relevant failure logs.
- [ ] Verify release version/tag metadata matches the Mako package manifest, lockfile, README, changelog, and release docs.
- [ ] Test source archive/install artifact contents for both native runtime `.c` files, DTLS/WSI headers, standard-library packs, examples, and gate scripts.
- [ ] Produce a final parity report listing every gate, command, platform, result, and artifact hash.

### Workstream G — concurrency correctness

- [ ] Specify task lifecycle, cancellation, detach, channel close, select fairness, panic propagation, and shutdown behavior.
- [ ] Add compile-fail tests for illegal cross-thread transfers and unsynchronized shared mutation.
- [ ] Run race detection over channels, scheduler queues, cancellation, timers, maps, reference counting, and foreign handles.
- [ ] Add deterministic tests for close/send/receive races, cancellation during I/O, panic during cleanup, and nested task-scope exit.
- [ ] Add randomized scheduler stress with reproducible seeds and retained failure traces.
- [ ] Measure throughput and tail latency at 1, 2, 4, 8, 16, and available-core worker counts.
- [ ] Verify idle scheduler CPU usage, bounded thread count, bounded queues, absence of busy loops, and clean shutdown with no leaked tasks.
- [ ] Test oversubscription, blocking foreign calls, high contention, channel saturation, and cancellation storms.

### Workstream H — compiler and CPU performance

- [ ] Add machine-readable phase timings, peak RSS, allocation count/bytes, emitted code size, and cache hit/miss metrics.
- [ ] Establish small, medium, compiler-self-build, and multi-package benchmark corpora.
- [ ] Measure cold build, no-change rebuild, one-leaf edit, one-public-interface edit, and clean fixed-point bootstrap.
- [ ] Add CPU-time measurements in addition to wall time so parallel speedups cannot hide excess CPU consumption.
- [ ] Profile frontend, type checking, IR construction, optimization, register allocation, object writing, and package orchestration.
- [ ] Remove repeated arena scans, unnecessary clones, quadratic passes, and duplicate generic instantiations found by profiles.
- [ ] Benchmark generated code against recorded historical baselines and current Mako stages for integer, string, slice, map, allocation, call, branch, loop, I/O, networking, and concurrency workloads.
- [ ] Track instructions, cycles, branches, branch misses, cache misses, context switches, syscalls, allocations, peak RSS, binary size, startup time, throughput, and p50/p95/p99 latency where supported.
- [ ] Add performance budgets to CI using noise-resistant repeated samples and a documented comparison policy.
- [ ] Require a written explanation and approval for any accepted regression.

### Workstream I — disk, cache, and binary size

- [ ] Measure source, dependency download, clean build, incremental cache, fixed-point bootstrap, installed SDK, debug artifact, release archive, and stripped binary sizes.
- [ ] Add content-addressed deduplication and public-interface dependency hashes.
- [ ] Implement cache inspection, verification, size budgets, LRU/age garbage collection, pinning, and atomic cleanup.
- [ ] Add duplicate-code/data, generic-bloat, debug-size, and embedded-resource reports.
- [ ] Implement dead-section elimination, string/constant pooling, split debug output, and deterministic release stripping.
- [ ] Test cache corruption, interrupted writes, concurrent builds, compiler-version changes, target changes, and deletion during an inactive build.
- [ ] Prove `mako clean` and cache GC cannot escape resolved Mako cache/build roots.

### Workstream J — cryptography and secure transport

- [ ] Create the algorithm/policy registry and standards/errata review process.
- [ ] Implement secret types, OS CSPRNG, constant-time utilities, zeroization, and safe high-level APIs before adding broad algorithms.
- [ ] Add official vectors and differential tests for every primitive.
- [ ] Implement TLS 1.3 (RFC 9846), then DTLS 1.3 (RFC 9147), then QUIC/HTTP3 integration.
- [ ] Add certificate/path validation, hostname checking, ALPN/SNI, resumption, mutual TLS, revocation policy, ticket rotation, and anti-replay controls.
- [ ] Add ML-KEM, ML-DSA, and SLH-DSA with NIST vectors and errata tracking; keep draft TLS wire integration experimental until standardized.
- [ ] Run interoperability against at least two mature independent implementations.
- [ ] Run fuzzers over ASN.1, X.509, TLS/DTLS records and handshakes, key formats, JOSE, and post-quantum encodings.
- [ ] Run timing/leakage analysis and review all hardware-accelerated code paths.
- [ ] Publish a threat model and obtain independent cryptographic review before secure-default release status.

### Workstream K — standard library breadth

- [ ] Generate and publish the Go capability comparison matrix.
- [ ] Implement Tier 0 foundations before multiplying package-specific abstractions.
- [ ] Implement Tier 1 systems/networking/security packages sufficient for representative production applications.
- [ ] Implement Tier 2 testing, observability, profiling, advanced networking, and cross-platform capabilities.
- [ ] Establish the release-coordinated first-party extended library.
- [ ] Require standard quality gates and representative application tests for each capability group.
- [ ] Track compile-time, runtime, CPU, allocation, binary-size, dependency, and disk costs per package.

### Workstream L — debugger and observability tooling

- [ ] Define debug metadata and optimized-debug semantics before implementing the UI.
- [ ] Emit complete DWARF on ELF/Mach-O and PDB/CodeView-compatible data on Windows.
- [ ] Implement launch/attach, breakpoints, stepping, stacks, values, tasks, channels, panic/signal stops, core/minidump reading, and split debug files.
- [ ] Add DAP integration, IDE smoke tests, remote-debug security, and debugger fuzzing.
- [ ] Implement CPU/heap/allocation/block/mutex/task/I/O profiling, execution tracing, coverage, crash symbolization, and artifact inspection.
- [ ] Validate debugging on optimized and unoptimized outputs for all Tier-1 target pairs.

### Workstream M — complete test platform

- [ ] Build the hermetic test manifest/runner and migrate every existing fixture.
- [ ] Add property, differential, mutation, fuzz, stress, soak, fault-injection, deterministic scheduler, compatibility, and interop suites.
- [ ] Track feature/IR/instruction/ABI/API/error-path coverage in addition to source coverage.
- [ ] Add automatic failing-input reduction and full reproduction bundles.
- [ ] Enforce flaky-test policy and test-count integrity.
- [ ] Run stage, backend, optimization-profile, target, and host matrices without silently treating skipped combinations as passes.

### Workstream N — multi-architecture cross compilation

- [ ] Generalize target descriptions and remove x86-64 assumptions from shared IR, layout, runtime, object writing, and driver code.
- [ ] Complete AMD64 and ARM64 code generators and ABIs first.
- [ ] Complete ELF, Mach-O, and PE/COFF writers/linkers for AMD64 and ARM64.
- [ ] Port runtime/standard library, unwind/debug, profiler, crypto acceleration, and atomic/concurrency primitives per Tier-1 pair.
- [ ] Add real AMD64 and ARM64 Linux, Windows, and macOS CI; emulation supplements but does not replace real hardware.
- [ ] Add RISC-V 64 and WASI as the first Tier-2 targets, followed by demand/CI-driven ports.
- [ ] Publish `mako target info` capability data generated from actual gates.

## Initial performance budgets

These are starting release gates and should be tightened after stable baselines are collected. Compare on pinned hardware/toolchains with warm-up, repeated samples, medians, and variance reported.

| Metric | Required migration gate |
|---|---|
| Clean self-build wall time | No more than 1.25× current `main` native compiler |
| Clean self-build CPU time | No more than 1.20× current `main` native compiler |
| No-change incremental build | Under 250 ms for the standard benchmark workspace, then tighten |
| Single-leaf edit rebuild | Recompile only affected dependency closure; no unrelated package codegen |
| Compiler peak RSS | No more than 1.20× current `main` on the self-build corpus |
| Generated-code geomean runtime | No more than 10% slower than current native backend |
| Generated-code CPU time | No more than 10% higher on CPU-bound corpus |
| Hot benchmark regression | No individual critical benchmark more than 15% slower without an approved exception |
| Binary startup | No more than 10% slower or 1 ms absolute regression, whichever is larger |
| Idle scheduler | Below 1% of one CPU core after timers settle |
| Parallel scalability | Positive throughput scaling through the smaller of 8 workers or available physical cores |
| Memory safety | Zero sanitizer/race-detector failures in required corpus |
| No-change cache growth | Zero new content blobs and only bounded metadata updates |
| Duplicate cache storage | Less than 2% unexplained duplicate immutable bytes |
| Incremental cache budget | Configurable hard size cap with verified GC and no correctness dependency on cache survival |
| Stripped hello/service binaries | Baseline and track by target; no unexplained >5% regression |
| Compiler/SDK install size | No unexplained >5% release-over-release regression |
| Crypto regression | Zero vector, interop, fuzz, downgrade, or policy failures |
| TLS default | TLS 1.3 only for new protocols; legacy TLS 1.2 explicitly opted in |
| Tier-1 cross targets | 100% applicable correctness/runtime/stdlib release gates pass on real hardware |
| Debugger stepping | 100% scripted unoptimized tests and documented optimized-location behavior pass |
| Test flakiness | Zero unexplained retries in release-blocking suites |
| Feature/IR coverage | Every specified language feature and emitted IR operation has positive and negative coverage |

Do not game these budgets by disabling safety checks, omitting work, increasing nondeterminism, consuming unbounded memory, or shifting work outside the measured process. Release mode may eliminate checks only through compiler-proven safety.

## Required new regression matrix

| Area | Historical expected behavior | Mako-native backend | Stage 1+ | Safety/other proof |
|---|---:|---:|---:|---|
| Existing language fixtures | required | required | supported subset | Mako-native memory/UB instrumentation |
| Moved string in `if` | required | required | when supported | Mako UAF/leak instrumentation, repeated run |
| Struct fields with all slice kinds | required | required | when supported | leak + double-free check |
| Multi-file nested string structs | required | required | expected reject until supported | diagnostic parity |
| DTLS loopback | required | required | expected reject until supported | independent black-box protocol interop |
| DCE underscored pack function | required | required | when supported | symbol/reachability assertion |
| WSI headless | required | required | expected reject until supported | Linux CI with no display |
| WSI real window | platform-specific | platform-specific | out of scope | Cocoa/X11/Win32 smoke |
| WASI args/env/fs | required | documented fallback/reject | out of scope | `wasm32-wasip1` runtime |
| Installed-prefix native app | N/A | required | N/A | run outside source tree |
| Self-host ELF corpus | oracle | product comparison | required | hashes + Linux execution |
| Negative compiler corpus | required | required | required for supported parser/type subset | deterministic diagnostics |
| Scheduler/channel stress | required | required | when runtime is supported | race detector + seeded stress |
| Compiler throughput | baseline | baseline | required | wall time + CPU time + RSS |
| Generated-code performance | baseline | required | required after full lowering | counters + benchmark statistics |
| Idle/runtime efficiency | baseline | required | required after runtime migration | CPU, wakeups, syscalls, threads |

“Expected reject” is a pass only when the rejection is deterministic, correctly classified, nonzero, documented, and does not produce or execute a partial artifact.

## Self-host roadmap after parity

Do not let parity work obscure the actual distance to full self-hosting. The current self-host evidence reports only about **24 of 231 compiler functions** lowered at the measured point. The next architectural work is:

1. Retain annotated local type roots so named struct identity survives typing.
2. Add semantic struct types, layout tables, struct literals, and field access.
3. Complete `[]int`/`[]Struct` make, append, clone, index, length, and drop behavior.
4. Replace shape-matched lowering with general CFG construction.
5. Add merge blocks and block parameters using dominance information.
6. Generalize jump/backedge encoding for arbitrary loops and nesting.
7. Support multi-file/package resolution in the Mako-written compiler.
8. Replace slot-per-value allocation with register allocation and spills.
9. Implement every mandatory runtime behavior directly in Mako with no native bridge.
10. Implement and prove stage 0 → 1 → 2 → 3 reproducible bootstrap.

The order matters: more special-case control-flow shapes will not close the gap. Struct/owned aggregate support, general CFG/SSA, packages, and register allocation are the critical path.

## Canonical self-hosted cutover plan

### Cutover 1 — complete compiler-language coverage

- [ ] Generate a machine-readable inventory of every syntax, semantic type, IR operation, builtin, runtime service, and package feature used by `compiler/*.mko`.
- [ ] Make stage 1 accept and correctly lower 100% of compiler functions; the denominator must include every source file in the compiler package.
- [ ] Delete shape-specific IR/backend paths as general CFG/SSA lowering supersedes them.
- [ ] Add negative tests for every newly supported feature so malformed input fails deterministically.
- [ ] Require the self-host compiler to compile real multi-file applications, not only isolated fixtures.

### Cutover 2 — replace external code generation

- [ ] Emit x86-64 machine code directly from Mako for the complete compiler and runtime corpus.
- [ ] Implement general calling conventions, register allocation, spills, stack frames, relocations, symbols, and debug/source metadata.
- [ ] Produce ELF64 executables and relocatable objects directly in Mako.
- [ ] Prove that the self-host path invokes no C compiler, assembler, external linker, LLVM, or Cranelift.
- [ ] Keep macOS/Windows cross-platform promises scoped honestly until Mach-O and PE/COFF writers and platform runtimes exist.

### Cutover 3 — move the runtime into Mako

- [ ] Implement process entry, arguments/environment, allocation, ownership drops, strings, slices, maps, files, time, randomness, networking, TLS/DTLS, concurrency, synchronization, panic, and cleanup in Mako.
- [ ] Replace required `native_bridge.c`, `native_runtime.c`, and runtime-header behavior with Mako-native modules or direct syscalls.
- [ ] Retain C only as an optional FFI target/compatibility layer, never as a required compiler runtime.
- [ ] Run memory-safety, leak, concurrency, resource-census, and failure-injection gates against the Mako runtime.
- [ ] Prove scheduler idle efficiency, bounded workers, backpressure, cancellation cleanup, and scalable throughput.
- [ ] Move secure randomness, secret memory, cryptographic primitives, TLS 1.3, and required certificate handling into the audited Mako runtime/library boundary.

### Cutover 4 — prove the fixed point

Use isolated output directories and fresh processes for every stage:

```text
trusted stage 0 -> builds Mako stage 1
Mako stage 1    -> builds Mako stage 2
Mako stage 2    -> builds Mako stage 3
```

- [ ] Stage 1 compiles the complete compiler and required runtime from Mako source.
- [ ] Stage 2 compiles the identical source set with no stage-0 components loaded or invoked.
- [ ] Stage 3 repeats the build under the same normalized inputs.
- [ ] Stage 2 and stage 3 artifacts are byte-identical, or all differences are explained by a narrowly documented reproducibility normalization rule.
- [ ] Dependency tracing proves that stages 1+ do not execute forbidden tools.
- [ ] All stages produce equivalent diagnostics, exit statuses, and executable behavior across the positive and negative corpus.
- [ ] Repeat the bootstrap twice from clean directories to catch hidden state and undeclared inputs.

### Cutover 5 — make self-hosted Mako the product

- [ ] Change the default `mako` compiler/driver to the Mako-written implementation.
- [ ] Build release packages from the fixed-point compiler.
- [ ] Require bootstrap, parity, safety, reproducibility, and clean-machine jobs in branch protection.
- [ ] Keep historical Rust/C commits only in an archived repository tag, absent from active source, packages, CI, and release inputs.
- [ ] Reject any production change implemented outside Mako.
- [ ] Verify source archives and release packages contain no Rust/C source, objects, libraries, metadata, or helpers.
- [ ] Update all public claims only after published artifacts independently reproduce the fixed point.
- [ ] Require compiler-speed, generated-code, CPU-efficiency, and concurrency-performance budgets for releases.
- [ ] Require disk/cache/binary-size budgets, cryptographic conformance, secure-transport interoperability, and standard-library application gates.
- [ ] Ship the debugger, test platform, LSP/tooling suite, and AMD64/ARM64 Tier-1 cross toolchains with the self-hosted compiler.

## Target repository architecture

The exact directories may change, but ownership must be unambiguous:

| Area | Canonical implementation at cutover | Transitional role |
|---|---|---|
| Lexer/parser/type checker | Mako | Historical expected-output fixtures only |
| Typed IR and optimizers | Mako | Historical regression cases only |
| Machine-code generators | Mako | Recorded benchmark/disassembly evidence only |
| ELF/Mach-O/PE-COFF/Wasm writers/linkers | Mako | Platform specifications and checked-in conformance fixtures |
| Runtime | Mako | Direct platform APIs/syscalls; no native bridge |
| CLI/package/test/debug/tooling | Mako | No alternate-language launcher or helper |
| Bootstrap tooling | Mako plus minimal trusted seed artifact | Seed starts stage 1; all later logic is Mako |
| Language and compiler tests | Shared, backend-neutral corpus | Must run against every active stage |

## Rust/C exclusion gate

Rust and C are excluded from the active implementation immediately. Historical source may remain reachable only through an archive tag for research. The active branch and release must satisfy:

- [ ] fixed-point bootstrap passes repeatedly on clean Linux x86-64 machines;
- [ ] the complete language, package, compiler, and application corpus passes;
- [ ] diagnostics meet the documented parity rules;
- [ ] security and memory-safety gates pass on the Mako implementation;
- [ ] performance is within the agreed release budget;
- [ ] release artifacts can rebuild themselves from documented inputs;
- [ ] a last-known-good seed compiler and recovery procedure are published;
- [ ] rollback uses the last-known-good Mako seed/release, never the Rust/C implementation.

## Definition of mainline parity (migration milestone)

Parity is achieved only when every item below is true:

- [ ] The integration contains all 12 mainline commits and `a2749f5`, with no lost behavior.
- [ ] Every required historical regression is ported, and the full Mako-native test suites pass with updated measured counts.
- [ ] All required historical regressions are represented by Mako tests and pass at every active stage.
- [ ] Mandatory memory-safety and sanitizer jobs pass.
- [ ] The moved-from-slot UAF/double-free regression is proven fixed.
- [ ] DTLS/SRTP, WSI, WASI, DCE, and multi-module regressions pass on their supported platforms.
- [ ] A clean installed-prefix native application compiles and runs outside the source checkout.
- [ ] The Linux x86-64 self-host gate executes all generated ELF images and matches approved deterministic hashes.
- [ ] Positive and negative stage-0/stage-1 differentials pass for the supported subset.
- [ ] No build, test, debug, package, cross-target, or stage operation uses Rust, C, Cranelift, an external assembler/linker, or another-language helper.
- [ ] Documentation accurately distinguishes product-native compilation, the stage-1 subset, and an unproven fixed point.
- [ ] Release artifacts contain all required runtime sources, headers, packs, examples, and scripts.
- [ ] CI required checks enforce all of the above.

Passing this checklist authorizes continuation to the canonical cutover; it does **not** by itself authorize a “fully self-hosted” claim.

## Definition of fully self-hosted Mako

- [ ] The Mako-written compiler implements the complete supported language and toolchain behavior.
- [ ] It compiles all compiler and required runtime sources without fallback.
- [ ] Stage 1 builds stage 2 and stage 2 builds reproducibly equivalent stage 3.
- [ ] Stages 1+ require no Rust, Cargo, C compiler, assembler, external linker, LLVM, or Cranelift.
- [ ] The required production runtime is implemented in Mako.
- [ ] Clean-machine release builds and self-rebuilds pass.
- [ ] Full correctness, negative, security, memory, concurrency, package, and application suites pass against the self-hosted compiler.
- [ ] Memory safety is enforced in typed IR and verified across all exits, unwinding, cancellation, ownership transfer, aggregates, and concurrency boundaries.
- [ ] Structured concurrency, race prevention, scheduler behavior, backpressure, and shutdown semantics are specified and pass stress/race gates.
- [ ] Clean, incremental, and self-build compiler speed meets the recorded wall-time, CPU-time, memory, and cache-efficiency budgets.
- [ ] Generated programs meet runtime, CPU-cycle, allocation, memory, startup, throughput, and tail-latency budgets.
- [ ] Idle runtimes do not busy-spin, and parallel workloads scale without uncontrolled oversubscription or excessive synchronization.
- [ ] Build caches and artifacts meet deduplication, size, corruption, reproducibility, inspection, and safe-GC requirements.
- [ ] TLS 1.3, DTLS 1.3, modern primitives, certificate validation, and post-quantum foundations pass vectors, interoperability, fuzzing, timing, downgrade, and policy gates.
- [ ] The standard library satisfies the published Go capability matrix or documents deliberate Mako replacements, and representative production applications require no missing foundational package.
- [ ] Language, compiler, runtime, and tooling extensions needed by the standard library are implemented in the self-hosted compiler—not hidden in the legacy oracle.
- [ ] Source-level debugging, symbols, unwind/crash diagnostics, profiling, tracing, coverage, and IDE protocol support pass on every Tier-1 target.
- [ ] The layered full test program covers compiler, IR, optimizers, backends, runtime, library, tooling, bootstrap, security, compatibility, performance, and cross targets.
- [ ] AMD64 and ARM64 Linux, Windows, and macOS meet Tier-1 native and cross-build gates; Tier-2 claims match their published capability matrices.
- [ ] Cross-produced artifacts do not depend on executing undeclared target tools and are behaviorally equivalent to native builds.
- [ ] Dependency traces and release artifacts proving the above are published.
- [ ] The Mako-written compiler is the default distributed `mako` implementation.

## Final parity report template

```markdown
# Mako Selfhosted Parity Report

- Integration SHA:
- Main baseline SHA: f638e64bed038aa674463a84d4977d80af9ac720
- Selfhost source SHA: a2749f53399cde49d80a27bc1f4af11400a40d1c
- Trusted seed identity:
- Purity-audit attestation:
- Platforms tested:

| Gate | Command | Platform | Result | Artifact/log |
|---|---|---|---|---|
| Mako compiler/tool unit tests | ... | ... | ... | ... |
| Historical regressions ported to Mako | ... | ... | ... | ... |
| Full Mako-native fixtures | ... | ... | ... | ... |
| Memory safety | ... | ... | ... | ... |
| Sanitizer sweep | ... | ... | ... | ... |
| DTLS interop | ... | ... | ... | ... |
| WSI headless/platform | ... | ... | ... | ... |
| WASI | ... | ... | ... | ... |
| Installed prefix | ... | ... | ... | ... |
| Self-host execution | ... | ... | ... | ... |
| Differential negatives | ... | ... | ... | ... |
| Mako-only purity audit | ... | ... | ... | ... |

## Remaining differences

List intentional differences only. Each entry requires an owner, issue, risk,
documented user-visible behavior, and removal criterion.

## Decision

- [ ] Parity accepted
- [ ] Parity rejected

Reason:
```

## Agent operating rules

- Work in small, reviewable commits organized by conflict area or regression.
- Do not update golden hashes simply to make a gate green.
- Do not weaken or skip tests to obtain parity.
- Do not convert a crash or unsupported construct into silent fallback.
- Keep stage-0 behavior as an oracle only where its behavior is known-correct; document known oracle defects.
- When product behavior and self-host proof need different backend policies, make the distinction explicit in code, flags, CI, and docs.
- After each merge conflict, add or identify the test that proves both sides' intended behavior survived.
