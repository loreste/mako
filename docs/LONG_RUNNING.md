# Long-running applications (years-up)

Months to years of uptime. Stable latency, stable RSS, no GC. That’s the bar
this note is about — not “fib looked fine on my laptop.”

It’s a direction we’re building toward, not a finished claim. Evidence is in
soaks, gates, and the rest of these docs. Last sync **2026-07-22**, tip
**0.4.15**.

More context: [SPEED.md](SPEED.md), [PERFORMANCE.md](PERFORMANCE.md),
[MEMORY_MODEL.md](MEMORY_MODEL.md), [SECURITY.md](SECURITY.md),
[ROADMAP.md](ROADMAP.md) § 0.5.2.

---

## Microbenches don’t care about your uptime

A fib run finishes in milliseconds. A service that has to stay up for years
dies from different things: RSS creep, fd and thread leaks, p99 spikes when
the heap is ugly under load.

Mako’s optimizer story here is AOT (`-O3 -flto`), plus optional offline PGO
and cheap hot-site counters. No in-process recompile. Memory that matters is
steady state after warmup. Latency that matters is p99 / p999 under load, not
just the mean of a short run.

Some runtimes look fantastic once they’ve specialized after a long warmup, then
lose months later to GC pauses, heap bloat, and tail latency you can’t
schedule around. We went the other way: no GC, ownership and arenas, native
code from the first request.

---

## What that looks like in the runtime

No collector means no GC pauses. Free on scope exit is deterministic, which
helps p99 stay less of a lottery. Startup is “run the binary” — no warmup
tiers. Live bytes should track what you own, not a heap that might shrink
later if you’re lucky. One binary to deploy. Ownership is explicit:
`hold`, `share`, `arena`.

Other stacks may still beat us on peak throughput or tooling depth. Fine.
Years-up proof for Mako is soaks and gates, not a blog chart. Throughput still
moves with LLVM release, LTO, optional PGO, and allocator choice — still
without a GC.

---

## Habits that keep a process alive

Don’t let live memory grow for a fixed concurrency and payload shape. Prefer
`arena` or stack views per request; only escape what has to outlive the
request. Share on purpose (`share`, channels, Sync handles) — not by growing
a global map and hoping it plateaus. Bound concurrency: crew pools, channel
caps, accept queues. Unbounded `kick` fans under load will hurt you.

Keep free off the p50 line. Measure steady state — soaks should fail on RSS /
live-bytes growth, not only on “one run returned the right answer.” Fail
closed on leaks in CI (`leak_mark`, `leak_scope_*`, soaks).

---

## Work track

| Track | What | Evidence |
|-------|------|----------|
| **LR-1 Foundation** | Soak fixture + RSS/live-bytes gate | `scripts/long-run-soak.sh` (**done**) |
| **LR-2 Runtime trust** | TSan soaks, channel stress, cancel/deadline | ROADMAP **0.5.2** |
| **LR-3 Allocators** | mimalloc/jemalloc knobs for fragmentation | `MAKO_ALLOCATOR` / `MAKO_LDFLAGS` (**done seed**) |
| **LR-4 PGO / LTO** | Two-pass PGO for release servers | `scripts/pgo-build.sh` (**done seed**) |
| **LR-4b Adaptive opt** | Traffic feedback without live recompile | `hot_site_*` + [ADAPTIVE_OPT.md](ADAPTIVE_OPT.md) + `adaptive-opt-cycle.sh` (**done seed**) |
| **LR-5 Observability** | pprof / metrics without GC pauses | `mako profile-serve` depth |
| **LR-6 HTTP / net soaks** | Accept loop under load, RSS while serving | `scripts/http-long-run-soak.sh` (**done seed**) |
| **LR-7 Claims honesty** | Publish soaks only with public methodology | no invented numbers |

Tip patches landed LR-1/3/4/6 seeds. **0.5.2** owns LR-2 depth and broader
soaks.

---

## Writing a service that can stay up

Per-request arena or short-lived owns — no immortal request maps. Fixed worker
pool and bounded channels. Timeouts on every external wait
(`recv_timeout`, `join_deadline`). `leak_scope` in tests; long-run soak before
you ship. Release build with `-O3 -flto` (llvm when available). Cap log and
metrics history. Drain the crew, close listeners, join on shutdown.

Sketch of the soak pattern:

```mko
// Request-scoped work that drops at end of the cycle.
fn handle(id: int) -> int {
    var m = make(map[int]int, 16)
    m[0] = id
    // … pure work …
    return m[0]
}

fn main() {
    // Compressed “years” of cycles — scripts/long-run-soak.sh
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

### CPU / alloc steady-state (LR-1)

```bash
./scripts/long-run-soak.sh
```

Pass: ownership live-delta **0**, multi-sample RSS growth within the bar.

### HTTP accept loop (LR-6)

```bash
./scripts/http-long-run-soak.sh
# MAKO_HTTP_SOAK_REQUESTS=5000 MAKO_HTTP_SOAK_CLIENTS=16 ./scripts/http-long-run-soak.sh
```

Thousands of `/` and `/health` hits against
`examples/bench/http_long_run_server.mko`. Per-request map/string work has to
free. Samples server RSS under load and wants a clean shutdown after the
budget.

Throughput microbench (not a soak): `./scripts/bench-http.sh` (wrk or hey).

### Allocator (LR-3)

If the host has a better allocator:

```bash
# Homebrew mimalloc
MAKO_ALLOCATOR=mimalloc MAKO_LDFLAGS="-L$(brew --prefix mimalloc)/lib" \
  mako build --release app.mko -o app

# jemalloc
MAKO_ALLOCATOR=jemalloc MAKO_LDFLAGS="-L/usr/local/lib" \
  mako build --release app.mko -o app

# Static archive
MAKO_ALLOCATOR=/path/to/libmimalloc.a mako build --release app.mko -o app

# Raw flags
MAKO_LDFLAGS="-L/opt/homebrew/lib -lmimalloc" mako build --release app.mko -o app
```

Default is still the system allocator. Measure with the soak scripts before
and after you change it.

### PGO and adaptive opt (LR-4 / LR-4b)

Learn from traffic without the cost of in-process specialization (warmup,
deopt, embedded compiler, GC).

Live process: full AOT, optional `hot_site_hit(id)`. Never rewrite machine code
in-process — details in [ADAPTIVE_OPT.md](ADAPTIVE_OPT.md). Offline: two-pass
PGO under representative load, then blue/green.

```bash
./scripts/adaptive-opt-cycle.sh examples/bench/http_long_run_server.mko \
  -o out/http_pgo -- 2000 19820

./scripts/pgo-build.sh examples/bench/http_long_run_server.mko \
  -o out/http_pgo -- 2000 19820

# Manual
MAKO_PGO_GEN=1 mako build --release app.mko -o app.pgo-gen
LLVM_PROFILE_FILE=out/pgo/default-%p.profraw ./app.pgo-gen …
llvm-profdata merge -o out/pgo/merged.profdata out/pgo/*.profraw
MAKO_PGO_USE=out/pgo/merged.profdata mako build --release app.mko -o app
```

Train on shapes that look like production. Keep release LTO (default); try
`MAKO_ALLOCATOR` if you care. Don’t ship `MAKO_PGO_GEN` instrumentation to
years-up boxes.

These gates prove steady-state memory under compressed load — the usual way
“years-up” processes die early. They are not multi-year wall-clock proof by
themselves.

---

## What we’ll stand behind

No GC. Deterministic ownership. Soak gates on live growth. Micro numbers from
the existing gates vs hand-C and Rust when the harness says so.

We won’t invent “faster on all servers” without a named workload, hardware,
and methodology in `scripts/`. Broader cross-runtime comparisons wait on
reproducible harnesses (LR-7).

---

## What’s next

Keep LLVM release + LTO as the product release path (0.5.0). Expand soaks —
channels, HTTP accept, metrics series bounds (0.5.2+). Allocator and PGO docs
can deepen once soaks stay green. Public years-up page with measured p99/RSS
only after that.
