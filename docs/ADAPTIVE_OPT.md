# Adaptive optimization without JIT tax

**Goal:** the longer a Mako service runs, the more we know about its real
hot paths — and the next binary is better — **without ever slowing the live
process with online JIT**, and **without a GC**.

Tip: **0.4.15+** · Related: [LONG_RUNNING.md](LONG_RUNNING.md) ·
[PERFORMANCE.md](PERFORMANCE.md) · [MEMORY_SAFETY.md](MEMORY_SAFETY.md) ·
[SPEED_SAFE.md](SPEED_SAFE.md) (AOT speed bar coexists with this feedback loop).

---

## What we take from profile-guided systems — and what we refuse

| Property | Mako stance |
|----------|-------------|
| Learns from production traffic | **Yes** — feedback from live counters + offline PGO |
| Hot paths get better over time | **Yes** — at **redeploy / rebuild**, not mid-request |
| Interpreter / multi-tier compilers | **No** — full native from process start |
| Warmup latency tax | **No** — cold path is already AOT `-O3` (+ LTO) |
| Deopt / code-cache pressure | **No** — no in-process recompile |
| GC while optimizing | **No** — ownership / arenas; no GC |

The product contract:

1. **t = 0 is already native and release-optimized.**
2. **Live process never rewrites its own machine code.**
3. **Feedback is opt-in and cheap** (relaxed atomics / sampling), never full
   PGO instrumentation in production.
4. **Heavier specialization is offline** (train → merge → rebuild → ship).

That is profile-guided learning on the *traffic* axis, without the *slowdown*
axis of an online JIT.

---

## Architecture (three layers)

```text
┌─────────────────────────────────────────────────────────────┐
│  Layer A — AOT always                                        │
│  mako build --release  →  -O3 -flto  (+ optional LLVM)       │
│  No interpreter. No warmup. No GC.                           │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  Layer B — cheap runtime feedback (optional)                 │
│  hot_site_enable(1) + hot_site_hit(id)   // atomic ++        │
│  profile_sample_*  /  /debug/hot_sites   // export only      │
│  Cost when off: one load + branch. When on: relaxed atomic.  │
└─────────────────────────────────────────────────────────────┘
                              │ export JSON / pprof-text
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  Layer C — offline re-opt (deploy cycle)                     │
│  scripts/pgo-build.sh  ·  scripts/adaptive-opt-cycle.sh      │
│  MAKO_PGO_GEN train on staging → MAKO_PGO_USE production     │
│  Optional: MAKO_ALLOCATOR=mimalloc|jemalloc                  │
└─────────────────────────────────────────────────────────────┘
```

**Never in the hot request path:** clang instrumentation (`-fprofile-generate`),
stack-walking profilers on every call, or mid-flight code patching.

---

## Layer B API (hot sites)

Cooperative site counters — you name the sites that matter:

```mko
fn handle(route: int) -> int {
    // site ids are app-defined (0..255)
    let _ = hot_site_hit(route)
    // … work …
    return 0
}

fn main() {
    let _ = hot_site_enable(1)   // default is off
    // serve traffic …
    let js = hot_sites_json()    // mako.hot_sites.v1
    // or HTTP: profile_http_route("/debug/hot_sites")
}
```

| Function | Role |
|----------|------|
| `hot_site_enable(on)` | Master switch; returns previous mode |
| `hot_site_enabled()` | 0/1 |
| `hot_site_hit(id)` | Record one hit (0 if disabled; −1 if id out of range) |
| `hot_site_count(id)` | Current count for site |
| `hot_site_total()` | Sum of all hits since clear |
| `hot_site_top_id` / `top_count` | Hottest site |
| `hot_site_clear()` | Zero counters |
| `hot_sites_json()` | Compact export (`mako.hot_sites.v1`) |

HTTP seed path (same router as pprof): **`/debug/hot_sites`**.

Stack sampling (heavier, still non-JIT) remains under `profile_sample_*` —
use sparingly; prefer hot sites on the request critical path.

---

## Layer C — production feedback loop

```bash
# One-shot recipe: instrumented train on representative load → optimized binary
./scripts/pgo-build.sh app.mko -o out/app -- <train-args>

# Documented continuous cycle (export guidance + offline PGO):
./scripts/adaptive-opt-cycle.sh app.mko -o out/app
```

Recommended ops loop for years-up services:

1. Ship **release AOT** (LTO; optional mimalloc).
2. Enable `hot_site_*` on a few route/handler ids; scrape `/debug/hot_sites`
   and optional pprof text **out of band**.
3. Nightly/staging: rebuild with `pgo-build.sh` under **real shapes**.
4. Blue/green swap the new binary — **no process ever recompiled itself**.

---

## Why this stays faster than “JIT on the box”

| Failure mode | Online JIT | Mako adaptive AOT |
|--------------|------------|-------------------|
| Cold start | Slow tiers | Full speed immediately |
| Deoptimization storms | Possible | Impossible (no live rewrite) |
| Code cache growth in-process | Yes | Binary size fixed at deploy |
| GC while compiling | Common with collectors | No GC |
| p99 during “warmup” | Spiky | Stable from first request |
| Memory for compiler in-process | Large | Zero |

Peak throughput after long online specialization can still win some microkernels
elsewhere. Mako’s bet is **stable p99 + stable RSS + no GC** for months–years,
with PGO closing the peak gap **offline**.

---

## Claims policy

- Do say: no GC; no online JIT; optional cheap feedback; offline PGO.
- Do not say “self-optimizing binary rewrites itself at runtime.”
- Do not invent throughput claims without a named soak + hardware (see
  LONG_RUNNING LR-7).

Tests: `examples/testing/hot_site_test.mko` (counters + map workload coexistence).

**Invariant:** AOT map/layout opts (identity int hash, pre-size, LTO) must leave
`hot_site_*` default-off and PGO env wiring unchanged. Speed ≠ online JIT.
