# Long-running applications (years-up)

**North star:** Mako should be the better default than **Java / Kotlin (JVM)** for
backend services that stay up for **months to years** — not only for microbench
fib, but for **stable latency, stable RSS, and no GC tax**.

This is a product commitment, not a completed claim. Evidence is built in
patches (soaks, gates, docs). Last sync: **2026-07-22** · tip **0.4.9+**.

Related: [SPEED.md](SPEED.md) · [PERFORMANCE.md](PERFORMANCE.md) ·
[MEMORY_MODEL.md](MEMORY_MODEL.md) · [SECURITY.md](SECURITY.md) ·
[ROADMAP.md](ROADMAP.md) § 0.5.2.

---

## Why long-running is different from microbenches

| Concern | Microbench (fib/map) | Years-up service |
|---------|----------------------|------------------|
| Time scale | milliseconds | months–years |
| Failure mode | slow code | **RSS creep**, fd/thread leaks, p99 spikes |
| Optimizer | AOT `-O3 -flto` | AOT + PGO; **no JIT warmup tax** |
| Memory | peak of one run | **steady-state** after warmup |
| Latency | mean | **p99 / p999** under load |

Java/Kotlin can look strong on peak throughput after JIT warmup. Long-running
products often lose on **GC pauses**, **heap bloat**, and **unpredictable
tail latency**. Mako’s contract is the opposite: **no GC**, ownership + arenas,
native code from process start.

---

## Honest comparison vs Java / Kotlin

### Where Mako is already structured to win

| Axis | Mako | JVM (typical) |
|------|------|----------------|
| **GC pauses** | None | Tuning required; still present |
| **p99 predictability** | Deterministic free on scope exit | GC + safepoints |
| **Startup / cold path** | Native binary | Class load + JIT warmup |
| **RSS ceiling** | Live bytes ≈ owned graph | Heap + metaspace + JIT code cache |
| **Deployment** | Single binary | JRE / container image tax |
| **Ownership** | Compiler + `hold` / `share` / `arena` | GC + finalizers / cleaners |

### Where the JVM still has advantages today

| Axis | Reality check |
|------|----------------|
| **Peak throughput** after hours of JIT | Can beat naive AOT on some hot loops |
| **Ecosystem** | Decades of libraries, APM, profilers |
| **Operational muscle** | GC logs, heap dumps, flight recorder |
| **Our proof** | Micro + map gates exist; **years-up soaks are the missing product evidence** |

**Goal:** close the *evidence* gap with soaks and production tooling, and the
*throughput* gap with LLVM release, LTO, optional PGO, and allocator choice —
without ever taking a GC.

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
| **LR-1 Foundation** | Soak fixture + RSS/live-bytes gate | `scripts/long-run-soak.sh` |
| **LR-2 Runtime trust** | TSan soaks, channel stress, cancel/deadline | ROADMAP **0.5.2** |
| **LR-3 Allocators** | Document/link mimalloc/jemalloc for long-run fragmentation | optional `MAKO_*` link flags |
| **LR-4 PGO / LTO product** | Documented PGO recipe for release servers | howto + CI optional |
| **LR-5 Observability** | pprof / metrics without GC pauses | `mako profile-serve` depth |
| **LR-6 HTTP / net soaks** | Accept loop under load, connection caps | `bench-http` + soak |
| **LR-7 Claims honesty** | Published soaks vs JVM *only* when methodology is public | no invented numbers |

**Product tip patches** can land LR-1 immediately. **0.5.2** owns LR-2.
Later **0.5.x** patches own LR-3–LR-6 as evidence appears.

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

## Soak gate (LR-1)

```bash
./scripts/long-run-soak.sh
# Override intensity:
#   MAKO_LONG_RUN_CYCLES=200000 ./scripts/long-run-soak.sh
```

**Pass criteria (default):**

1. Process exits 0 with stable checksum.
2. **Tracked live bytes** after the run ≈ baseline (ownership held).
3. **RSS after warmup** does not grow more than the configured ratio across
   samples (host allocator noise allowed within bound).

This does **not** yet prove multi-year wall-clock uptime. It proves the
**steady-state memory contract** under compressed load — the main failure mode
that ends “years-up” processes early.

---

## Claims policy

- **Do** say: no GC; deterministic ownership; soak gates on live growth.
- **Do not** say “faster than Java on all servers” without a named workload,
  hardware, and methodology checked into `scripts/`.
- **Do** publish native vs hand-C vs Rust micro numbers (existing gates).
- JVM comparisons land only with **reproducible** harnesses (future LR-7).

---

## Next concrete steps

1. Keep LLVM release + LTO as the default *product* release path (0.5.0).
2. Expand soaks: channels, HTTP accept, metrics series bounds (0.5.2+).
3. Optional production allocator + PGO docs once soaks are green.
4. Only then: public “years-up vs JVM” page with measured p99/RSS.
