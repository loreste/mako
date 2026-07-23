# Long-running applications (years-up)

**North star:** Mako should be the better default for backend services that stay
up for **months to years** — not only for microbench fib, but for **stable
latency, stable RSS, and no GC tax**.

This is a product commitment, not a completed claim. Evidence is built in
patches (soaks, gates, docs). Last sync: **2026-07-22** · tip **0.4.15**.

Related: [SPEED.md](SPEED.md) · [PERFORMANCE.md](PERFORMANCE.md) ·
[MEMORY_MODEL.md](MEMORY_MODEL.md) · [SECURITY.md](SECURITY.md) ·
[ROADMAP.md](ROADMAP.md) § 0.5.2.

---

## Why long-running is different from microbenches

| Concern | Microbench (fib/map) | Years-up service |
|---------|----------------------|------------------|
| Time scale | milliseconds | months–years |
| Failure mode | slow code | **RSS creep**, fd/thread leaks, p99 spikes |
| Optimizer | AOT `-O3 -flto` | AOT + offline PGO + cheap hot-site feedback; **no JIT warmup tax** |
| Memory | peak of one run | **steady-state** after warmup |
| Latency | mean | **p99 / p999** under load |

Managed runtimes with online JIT can look strong on peak throughput after
warmup. Long-running products often lose on **GC pauses**, **heap bloat**, and
**unpredictable tail latency**. Mako’s contract is the opposite: **no GC**,
ownership + arenas, native code from process start.

---

## How Mako is structured for years-up

| Axis | Mako |
|------|------|
| **GC pauses** | None |
| **p99 predictability** | Deterministic free on scope exit |
| **Startup / cold path** | Native binary; no warmup tiers |
| **RSS ceiling** | Live bytes ≈ owned graph |
| **Deployment** | Single binary |
| **Ownership** | Compiler + `hold` / `share` / `arena` |

**Tradeoffs to be honest about:** peak throughput after long online
specialization elsewhere can still win some microkernels; ecosystem and APM
depth are still maturing. **Our proof** for years-up is soaks + gates — close
the *evidence* gap with soaks and tooling, and the *throughput* gap with LLVM
release, LTO, optional PGO, and allocator choice — without ever taking a GC.

---

## Design principles for years-up Mako

1. **No silent growth** — a process that runs forever must have **bounded
   live memory** for a fixed concurrency and payload shape.
2. **Request-scoped memory** — prefer `arena` / stack views per request; escape
   only what must outlive the request.
3. **Explicit sharing** — `share` / channels / Sync handles; no accidental
   global maps that only grow.
4. **Bounded concurrency** — crew pools, channel caps, accept queues; never
   unbounded `kick` fans under load.
5. **Free is cold, hot path stays simple** — drop paths stay out of the p50
   line; allocators stay predictable.
6. **Measure steady-state** — soak gates fail on **RSS / live-bytes growth**,
   not only on correctness of one run.
7. **Fail closed on leaks in CI** — `leak_mark` / `leak_scope_*` / soaks.

---

## Work track (ships in small patches)

| Track | What | Evidence |
|-------|------|----------|
| **LR-1 Foundation** | Soak fixture + RSS/live-bytes gate | `scripts/long-run-soak.sh` (**done**) |
| **LR-2 Runtime trust** | TSan soaks, channel stress, cancel/deadline | ROADMAP **0.5.2** |
| **LR-3 Allocators** | mimalloc/jemalloc link knobs for long-run fragmentation | `MAKO_ALLOCATOR` / `MAKO_LDFLAGS` (**done seed**) |
| **LR-4 PGO / LTO product** | Two-pass PGO recipe for release servers | `scripts/pgo-build.sh` (**done seed**) |
| **LR-4b Adaptive opt** | Traffic feedback **without** online JIT | `hot_site_*` + [ADAPTIVE_OPT.md](ADAPTIVE_OPT.md) + `adaptive-opt-cycle.sh` (**done seed**) |
| **LR-5 Observability** | pprof / metrics without GC pauses | `mako profile-serve` depth |
| **LR-6 HTTP / net soaks** | Accept loop under load, RSS while serving | `scripts/http-long-run-soak.sh` (**done seed**) |
| **LR-7 Claims honesty** | Published soaks *only* when methodology is public | no invented numbers |

**Product tip patches** land LR-1/3/4/6 seeds. **0.5.2** owns LR-2 depth + broader soaks.

---

## How to write a years-up service in Mako (checklist)

```text
[ ] Per-request arena or short-lived owns (no immortal request maps)
[ ] Fixed worker pool / bounded channels (no unbounded kick)
[ ] Timeouts on every external wait (recv_timeout / join_deadline)
[ ] leak_scope around tests; long-run soak before ship
[ ] Release build: -O3 -flto (and llvm backend when available)
[ ] Cap logs / metrics series (no unbounded in-memory history)
[ ] Graceful shutdown: drain crew, close listeners, join
```

Pattern sketch:

```mko
// Prefer request-scoped work that drops at end of the cycle.
fn handle(id: int) -> int {
    var m = make(map[int]int, 16)
    m[0] = id
    // … pure work …
    return m[0]
}

fn main() {
    // Compressed “years” of cycles — used by scripts/long-run-soak.sh
    var i = 0
    var acc = 0
    while i < 100000 {
        acc = acc + handle(i)
        i = i + 1
    }
    print_int(acc)
}
```

---

## Soak gates

### CPU/alloc steady-state (LR-1)

```bash
./scripts/long-run-soak.sh
```

**Pass criteria:** ownership live-delta **0**; multi-sample RSS growth within bar.

### HTTP accept loop (LR-6)

```bash
./scripts/http-long-run-soak.sh
# MAKO_HTTP_SOAK_REQUESTS=5000 MAKO_HTTP_SOAK_CLIENTS=16 ./scripts/http-long-run-soak.sh
```

Drives thousands of `/` + `/health` hits against
`examples/bench/http_long_run_server.mko` (per-request map/string work that
must free). Samples **server RSS under load**; requires clean shutdown after
the request budget.

Throughput microbench (not a soak): `./scripts/bench-http.sh` (needs wrk/hey).

### Production allocator (LR-3)

Link a long-running-friendly allocator when the host has one installed:

```bash
# Homebrew mimalloc example:
MAKO_ALLOCATOR=mimalloc MAKO_LDFLAGS="-L$(brew --prefix mimalloc)/lib" \
  mako build --release app.mko -o app

# jemalloc:
MAKO_ALLOCATOR=jemalloc MAKO_LDFLAGS="-L/usr/local/lib" \
  mako build --release app.mko -o app

# Explicit static archive (overrides malloc):
MAKO_ALLOCATOR=/path/to/libmimalloc.a mako build --release app.mko -o app

# Raw flags:
MAKO_LDFLAGS="-L/opt/homebrew/lib -lmimalloc" mako build --release app.mko -o app
```

Default remains the **system** allocator. Prefer measuring with
`http-long-run-soak` / `long-run-soak` before and after.

### PGO (LR-4) and adaptive opt without JIT (LR-4b)

We want *learning from production traffic* without the *slowdown* of online
JIT (warmup, deopt, in-process compiler, GC).

- **Live process:** full AOT; optional `hot_site_hit(id)` (relaxed atomic when
  enabled). **Never** rewrite machine code in-process. Details:
  [ADAPTIVE_OPT.md](ADAPTIVE_OPT.md).
- **Offline:** two-pass PGO under representative load; blue/green the result.

```bash
# Adaptive cycle (AOT + guidance note + offline PGO):
./scripts/adaptive-opt-cycle.sh examples/bench/http_long_run_server.mko \
  -o out/http_pgo -- 2000 19820

# Full two-pass recipe only:
./scripts/pgo-build.sh examples/bench/http_long_run_server.mko \
  -o out/http_pgo -- 2000 19820

# Manual:
MAKO_PGO_GEN=1 mako build --release app.mko -o app.pgo-gen
LLVM_PROFILE_FILE=out/pgo/default-%p.profraw ./app.pgo-gen …   # train
llvm-profdata merge -o out/pgo/merged.profdata out/pgo/*.profraw
MAKO_PGO_USE=out/pgo/merged.profdata mako build --release app.mko -o app
```

Train on **representative** traffic (same shapes as production). Combine with
release **LTO** (default) and optional `MAKO_ALLOCATOR`.
**Do not** ship `MAKO_PGO_GEN` instrumentation to years-up production boxes.

These gates prove the **steady-state memory contract** under compressed load —
the main failure mode that ends “years-up” processes early — not multi-year
wall-clock uptime by themselves.

---

## Claims policy

- **Do** say: no GC; deterministic ownership; soak gates on live growth.
- **Do not** invent “faster on all servers” without a named workload,
  hardware, and methodology checked into `scripts/`.
- **Do** publish native vs hand-C vs Rust micro numbers (existing gates).
- Cross-runtime comparisons land only with **reproducible** harnesses
  (future LR-7).

---

## Next concrete steps

1. Keep LLVM release + LTO as the default *product* release path (0.5.0).
2. Expand soaks: channels, HTTP accept, metrics series bounds (0.5.2+).
3. Optional production allocator + PGO docs once soaks are green.
4. Only then: public years-up page with measured p99/RSS.
