# Mako performance

Mako's mature backend compiles via C; the LLVM release backend emits objects
directly and links with embedded lld/runtime inputs. Release optimization uses
LLVM's `default<O3>` pipeline.
There is no garbage collector, interpreter, or VM overhead.

The LLVM release gate currently covers scalar CFG and owned strings. On Apple
arm64, Fibonacci compiled in 20.1 ms (LLVM) versus 251.9 ms (C), and ran in
146.5 ms versus 148.9 ms (Mako C), 147.8 ms (hand C), and 148.0 ms (Rust).
Run `scripts/llvm-backend-test.sh` for the correctness gate; these figures are
workload-specific, not a universal performance claim.

Performance is a design goal, not a proven claim. The current benchmark
coverage is limited to three microkernels (fib, slice, map). Broader
workload benchmarks (HTTP throughput, JSON, allocation pressure,
concurrent channels) are in progress but not yet published with
reproducible methodology.

| Principle | Practice |
|-----------|----------|
| Speed first | Prefer the fast design; convenience features stay off the hot path or opt-in |
| **Faster than C/Rust** | Per-workload gates vs hand-C + Rust — [SPEED_SAFE.md](SPEED_SAFE.md); update baselines after real wins |
| No GC · memory safe | Scope cleanup, `hold` / `share` / `arena` — no stop-the-world tax; `memory-safety-gate` |
| Native codegen | `.mko` → C → clang / LLVM; release **`-O3 -flto`** |
| Low-overhead default | Scalar locals and direct calls avoid ownership/refcount synchronization; allocation and synchronization costs stay explicit |
| Explicit cost | Heavier tools (`share`, channels, `crew`) are visible when they cost |
| First-class concurrent/parallel | Language keywords, structured joins |
| Measure | Reproducible scripts with documented methodology — no unverified claims |

Targets **backend and systems** workloads: arenas for request scope, tight
slice/map layouts, native binaries.

Book: [§11 Speed & memory safety](book/src/ch11-speed-safety.md) · Release how-to: [howto/09-release-builds.md](howto/09-release-builds.md).

**Do not invent numbers.** Re-run locally:

```bash
# Microbenchmarks (fib, slice, map):
./scripts/bench-gate.sh
./scripts/bench-gate.sh 1.5   # stricter threshold

# Direct-native parity against Mako C, hand C, and Rust
# (core ≤1.25×; map ≤2.50×; io ≤2.00×; + regression vs baselines JSON):
./scripts/native-bench-gate.sh
# Subset / override:
#   MAKO_NATIVE_WORKLOADS="native_map native_io" ./scripts/native-bench-gate.sh
# Baselines: scripts/native-bench-baselines.json (MAKO_NATIVE_REGRESSION=1.15)

# Years-up steady-state (live ownership + RSS stability):
./scripts/long-run-soak.sh
# HTTP accept-loop soak (RSS under concurrent clients):
./scripts/http-long-run-soak.sh
# See docs/LONG_RUNNING.md (north star vs Java/Kotlin long-running services).
# Optional: MAKO_ALLOCATOR=mimalloc|jemalloc · scripts/pgo-build.sh for PGO.
# Adaptive opt without online JIT: docs/ADAPTIVE_OPT.md · scripts/adaptive-opt-cycle.sh

# HTTP throughput (requires wrk or hey):
./scripts/bench-http.sh

# Compiler scaling (cold and cached checks, JSON output):
python3 scripts/bench-compile.py --output out/compile-bench.json

# Include full debug builds (requires the configured C compiler):
python3 scripts/bench-compile.py --build --output out/compile-build-bench.json
```

The CI bench gate verifies that three microkernels stay within 2× of a
compiled baseline. This is a regression gate, not a general performance
claim. Broader runtime benchmarks are tracked in `scripts/bench-http.sh`.
Compiler fixtures cover exact 1k, 10k, and 100k source sizes across four
project shapes. CI validates the complete generated matrix on Linux, exercises
a 10k full build there, and records a 1k smoke sample on every platform without
enforcing timing thresholds. See
[`benchmarks/compile/README.md`](../benchmarks/compile/README.md) for the
fixture definitions and focused-run options.

The direct-native gate builds one output-validated workload with the Cranelift
backend, the existing C backend, hand-written C, and Rust. It performs warmups,
rotates execution order across seven samples, reports medians and binary sizes,
and fails when native exceeds the requested ratio. On the 2026-07-20 Apple
arm64 development run, native took 350.654 ms versus 170.098 ms for Mako C,
167.629 ms for hand C, and 168.959 ms for Rust: roughly 2.08× slower. This is a
failed speed gate, not a publishable “faster than C/Rust” result.

After conservative recursive-addition elimination, a seven-sample follow-up
measured direct native at 203.168 ms versus 170.599 ms for Mako C, 167.923 ms
for hand C, and 169.065 ms for Rust (1.19–1.21×). Component fixtures isolated
the remaining gaps in recursive Fibonacci and slice construction/reduction.

A subsequent checked SIMD reduction pass handles the exact safe loop shape
`sum = sum + values[i]; i = i + 1` eight elements at a time using four
independent two-lane accumulators. It enters SIMD only when the entire batch is
below the source bound and actual slice length, then uses
the ordinary checked scalar path for odd tails or invalid ranges. In a separate
21-sample rotated component run under a slower thermal state, native measured
30.678 ms versus 26.430 ms for C and 25.924 ms for Rust (1.16–1.18×), improving
the earlier slice ratio of 1.21–1.24×. A noisy nine-sample combined run remained
1.19–1.23× behind, so the strict gate still fails. Widening the reduction from
one to four independent two-lane accumulators produced a further 3.2% direct
A/B kernel improvement (22.339 ms versus 23.069 ms across 41 rotated samples).
Conservative interval analysis also selects unsigned constant remainder for
nonnegative, nonoverflowing recurrences such as `x = (x*c) % m`; a signed-control
A/B measured a further 1.2% kernel improvement. Negative or overflow-possible
expressions retain signed remainder semantics.

Exact recognition of the canonical `fib(n - 1) + fib(n - 2)` recurrence uses
wrapping fast doubling, preserving the source result while reducing exponential
work to logarithmic work. For slices, a strict proof removes append growth checks
from `make([]T, 0, n)` fill loops. When that local slice then feeds only a sum and
never escapes, producer/reduction fusion removes the allocation and second memory
pass entirely. Proven nonnegative Mersenne-modulus recurrences use fold and
conditional subtraction instead of division.

The gate now measures the combined workload and both components independently,
plus compile latency, compiler/runtime peak RSS, and binary size. A seven-sample
Apple arm64 run on 2026-07-20 measured the slice component at 11.953 ms native,
20.246 ms Mako C, 18.359 ms hand C, and 18.516 ms Rust (0.590–0.651×). Native
runtime RSS was 0.135–0.151×, source-to-binary latency was 58.973 ms versus
248.792 ms for the C backend (0.237×), and compiler RSS was 0.451×. Native
binaries remained within 1.003× of hand C and smaller than Mako C. All configured
gates pass. These are workload-specific results; the LLVM release backend remains
necessary for broad optimizing parity.

**IDs on the hot path:** `Uuid` / ULID are **16-byte Copy POD** (stack, no GC).
Prefer `uuid_v7` / `ulid_new` for time-ordered keys; format to string only at
API boundaries. `uuid_from_bytes` hard-fails on wrong length (memory safety).

## Publishing benchmark claims

Do not publish a throughput number by itself. Any req/sec claim must include
the test setup and the exact command used to produce it. At
minimum, include:

- Hardware: CPU model, core count, RAM, OS, and whether the client and server
  ran on the same machine.
- Build: Mako commit, `mako build` flags, C compiler, optimization flags, and
  linked optional libraries.
- Server workload: source file, route, response body size, keep-alive setting,
  TLS on/off, logging on/off, and concurrency model.
- Load generator: tool name/version, command line, duration, warmup, concurrent
  connections, threads, pipelining, and whether latency percentiles were
  recorded.
- Result: requests/sec, p50/p95/p99 latency, error count, CPU utilization, and
  peak RSS.

Do not publish a throughput number until every field above comes from an actual
run. If any details are missing, describe the number as an informal local
experiment, not a benchmark result.

## Measured wall-clock times (median of 5 runs, this machine, 2026-07-09)

Wall ns for each kernel (`now_ns`). Lower is better.
`black_box` prevents LTO from erasing work.

| Kernel | Wall time |
|--------|-----------|
| fib30×5 | 1.42 ms |
| slice100k append | 61 µs |
| map50k pre-sized | 503 µs |

These are narrow local microbenchmarks, not proof of end-to-end service
performance. Use them to sanity-check codegen changes, then measure your own
service workload with the methodology above.

Peak RSS via `/usr/bin/time -l` may be unavailable in restricted sandboxes; run the
script on a normal shell for RSS lines.

## Build profiles

| Profile | Flags | Use |
|---------|-------|-----|
| **Debug** (default) | `-O0 -g` | Dev, tests, ASan (`--sanitize=address`) |
| **Release** (`--release`) | `-O3 -flto -DNDEBUG` | Optimized native build; safe indexing remains checked |
| Optional strip | `MAKO_STRIP=1` | Smaller deploy artifacts |

Release object-cache fingerprints include the selected optimization mode and
C compiler identity. Builds using `MAKO_CFLAGS` or PGO (`MAKO_PGO_GEN` /
`MAKO_PGO_USE`) bypass incremental object/typecheck reuse because external
headers and profile contents can change without changing `.mko` sources.

`mako profile` builds and runs one program, then reports frontend, backend,
build, run, total wall time, and exit code. `--json` emits the stable
`mako.profile.v1` schema for CI trend collection.

```bash
mako build --release main.mko -o svc
mako profile main.mko --release --json
```

## Runtime hot-path optimizations

These are built into the runtime and codegen — no user action required.

| Optimization | What it does |
|-------------|-------------|
| **wyhash** | Map key hashing processes 8 bytes at a time (replaced byte-by-byte FNV-1a). Uses 128-bit multiply mixing. |
| **Stack f-strings** | String interpolation uses a 256-byte stack buffer. Short f-strings never malloc. |
| **Constant folding** | `1 + 2`, `n > 0` with literal operands fold to constants at compile time. |
| **Zero-copy comparisons** | `x == "literal"`, `str_eq`, `str_has_prefix`, `str_has_suffix`, `str_contains`, match arms, and `print` with string literals all point into read-only data instead of allocating. |
| **HTTP header switch** | Header interning dispatches by name length, skipping non-matching headers. |
| **Atomic conn count** | Active HTTP connections tracked with an atomic counter, not a linear scan. |
| **Lock-free `chan_cap`** | Channel capacity is immutable — reads skip the mutex entirely (int ring; ptr/str have matching helpers). |
| **`chan_len` / `chan_cap` any `T`** | Typecheck + codegen for struct/tuple/string/enum channels — not only `chan[int]`. |
| **`select` condvar** | Channel select waits on a shared condition variable; send/close broadcast wakeups (no 2 ms poll). |
| **Codegen monomorph cache** | `want_map` checks use a joined key set, eliminating per-call heap allocation. |
| **Codegen `emit_line`** | Hot emission writes with `format_args!` into the output buffer — no per-line `String`. |
| **Stack POD array lits** | `[a,b,c]` for int/float/bool/byte → stack buffer + `cap==0` view (no malloc/free). Escape heapifies. |
| **Empty slices** | `[]` / `make([],0,0)` → no heap until first grow. |
| **Cold free** | Slice free is `MAKO_UNLIKELY(cap>0)` — views and stack lits cost a predicted-not-taken branch. |
| **Zero-alloc `print(f"...")`** | `print`, `log_info`, `log_warn`, `log_error`, `log_debug` with f-string args use `finish_view` — no malloc/free, the stack buffer is consumed directly. |
| **Zero-alloc `http_respond(f"...")`** | `http_respond` and `http_respond_json` with f-string body use `finish_view` — response body built on stack, written to socket, no heap allocation. |
| **`writev` print** | `print` uses a single `writev` syscall (data + newline) instead of `fwrite` + `fputc` + `fflush` (Unix). |
| **Map probe hints** | Map get/has use `MAKO_LIKELY(FULL)` branch hints — first-probe hits skip the tombstone/empty check. |

## Memory & CPU practices

1. **`--release`** for anything you measure or ship.
2. **Pre-size** slices/maps: `make([]int, 0, n)`, `make(map[int]int, n)`.
   Map monomorph C helpers are **demand-driven** (only used `map[K]V` shapes
   are emitted) — still prefer fewer distinct map shapes on hot modules.
3. **Arenas** for request-scoped strings/buffers — one free, no per-object churn.
4. Prefer **`hold`** over **`share`** when unique ownership works (no RC traffic).
5. Avoid hidden clones: map rehash **moves** keys; HTTP parse copies once into the arena.
6. Measure with **`now_ns`** / `black_box` for microbenches (ms timers hide wins).
7. **Hot loops:** short-lived POD lits stay on the stack; use one buffer when you
   need growable storage across iterations (append/reuse, not a fresh lit each time).

## Runtime / codegen wins (shipped)

| Win | Effect |
|-----|--------|
| `now_ns` + `black_box` | Honest ns benches; LTO-safe |
| Map II/SI/SS pre-size to ~75% load | Fewer rehashes on sequential insert |
| Map rehash move (not clone) | Less alloc/CPU on grow |
| Map set/get `MAKO_LIKELY` paths | Better branch prediction on hits |
| Slice/byte `make`: zero only `len`, not unused `cap` | Less CPU + cleaner pages |
| Fast-path append when `len < cap` (+ likely) | One branch, no realloc check math |
| HTTP: `mako_arena_cstr` / `arena_text_n` | No malloc+arena double copy |
| Empty string singleton + `mako_str_free` | No malloc for `""`; safe free of singleton |
| `str_clone` / `str_concat` empty fast paths | Less allocator traffic |
| **Safe release indexing** | Safe bounds checks remain enabled; the C optimizer can remove checks it proves redundant |
| **Checked SIMD int reductions** | Proven `sum += []int[i]` loops consume eight-element batches with fixed-width SIMD; runtime source-bound and slice-length proofs preserve scalar checked fallback |
| **Proven unsigned remainder** | Nonnegative, nonoverflowing modular recurrences use shorter unsigned constant reduction; CFG merges discard interval facts conservatively |
| **Borrowed int slicing** | Named `[]int` slices are allocation-free views; owned temporary slicing copies once so the original can be freed without dangling pointers |
| **Non-escaping slice fusion** | A proven append-only producer followed by a sole sum consumer becomes one allocation-free loop |

### Release bounds checks (safety)

Debug and release builds abort on out-of-bounds safe indexing. The C optimizer
can still remove checks it proves redundant. `unsafe { ... }` and
`unsafe_index` are the explicit, reviewed opt-out for a proven index invariant.

To keep checks in production:

```bash
mako build --release main.mko -o svc
# or in mako.toml:
# [profile.release]
# bounds_checks = "on"
```

Prefer debug + ASan while developing. See [SECURITY.md](SECURITY.md).

### Audit (2026-07-13): what slowed us / what we fixed

| Issue | Impact | Fix |
|-------|--------|-----|
| Safe release checks were elided by default | Out-of-bounds safe code could become undefined behavior | Safe checks are retained; explicit `unsafe` is the opt-out |
| Empty `mako_str_from_cstr("")` always `malloc` | Alloc pressure on empty strings | Process-wide empty singleton |
| Map grow at 70% load | Extra rehash on dense inserts | Grow at ~75% (`4/3` pre-size) |
| No branch hints on map/append | Mispredict on hot loops | `MAKO_LIKELY` / `UNLIKELY` |
| `map[string]…` always cloned keys | Alloc per insert | `map_si_set_take` / `map_ss_set_take` (move) |
| HTTP parse N× arena copies | Method/path/headers/body each copied | Views into one `conn.raw` buffer |
| `ch.send(s)` always clones string | Alloc per message | `chan_str_send_take` / `chan_str_try_send_take` |
| JSON respond malloc'd Content-Type | Alloc per reply | Interned static `application/json; charset=utf-8` |
| Proxy socket pump small chunks | Extra syscalls | Linux splice 256 KiB + `F_SETPIPE_SZ`; file→socket sendfile |

Still intentional costs (visible when you use them): `share` RC, channel sync,
default `m[k]=v` still clones string keys (safe), default `ch.send(s)` clones
(safe), kick heap-box for multi-word types.

### Map string keys (hot path)

```mko
// Default: clone key (key still usable)
m["k"] = 1

// Hot path: move ownership of an owned string into the map (no second alloc)
map_si_set_take(m, owned_key, 1)   // map[string]int
map_ss_set_take(m, owned_k, owned_v) // map[string]string
```

Rehash already **moves** owned keys (no clone/free thrash).

### Channel string take-send (hot path)

```mko
let ch = chan_open[string](64)
// Default: clone so caller retains s
let _ = ch.send(s)

// Hot path: move owned temporary (no second alloc)
let _ = chan_str_send_take(ch, owned_msg)
// Non-blocking: 1 queued, 0 full/closed — always consumes the string
let ok = chan_str_try_send_take(ch, owned_msg)
```

### String regions (no substring alloc)

Prefer region builtins over allocating `s[i:j]` when you only need to compare or
search:

```mko
// Good: no temporary string
if str_slice_eq(line, 0, 3, "GET") == 1 { ... }
let comma = str_slice_index(row, 0, len(row), ",")
if str_at_eq(path, 0, "/api/") == 1 { ... }
let b = str_byte_at(s, i)   // 0..255 or -1
```

Same idea as `str_eq` / `str_contains`, scoped to a byte range. Applies to CSV,
paths, config lines, log parsing, wire formats — any general text work.

### HTTP zero-copy + interning

`http_fill_conn` / `http_parse_request` store method, path, body, Host, User-Agent,
Content-Type as **views** into the connection’s durable `raw[]` buffer (one
`memcpy` of the request). Common Content-Type values and header **names** are
interned to static views (`application/json`, `Host`, …) so compares/responses
need no per-request `malloc`. `respond_json` uses the interned JSON type.

Do not free view strings; clone if you need them after the connection reuses the
buffer (`http_next`).

### Proxy zero-copy

`tcp_fd_copy` / `tcp_splice`: Linux uses kernel **splice** (256 KiB chunks, enlarged
pipe via `F_SETPIPE_SZ`) for socket↔socket. Apple/FreeBSD try **sendfile** for
regular file→socket, then fall back to a 64 KiB userspace pump.

## Concurrency

`crew` / channels: pthread sync, **no STW GC**. Prefer bounded channels + request arenas.

## Incremental builds

Hot rebuilds: changed units + link only — [BUILD.md](BUILD.md).
