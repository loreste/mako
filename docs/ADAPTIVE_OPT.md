# Adaptive optimization without in-process recompile

Learn from real traffic so the *next* binary is better shaped. That’s the whole
idea. No rewriting machine code in a live process, no garbage collector in the
loop.

**0.4.15+**. See [LONG_RUNNING.md](LONG_RUNNING.md),
[PERFORMANCE.md](PERFORMANCE.md), [MEMORY_SAFETY.md](MEMORY_SAFETY.md),
[SPEED_SAFE.md](SPEED_SAFE.md).

---

## How we do it

You ship full native AOT (`-O3` + LTO on release). If traffic shapes matter,
turn on a few cheap counters, scrape them out of band, then rebuild with
offline PGO and blue/green the result. Mid-request, the process never patches
itself. Free stays ownership-based the whole time.

In practice that means:

- Release AOT from process start is the default.
- Live feedback is opt-in and cheap (relaxed atomics, sampling).
- Heavy specialization is offline: train, merge, rebuild, ship.

---

## What’s in the binary vs what’s offline

**Always on: AOT.** `mako build --release` → `-O3 -flto`, optional LLVM. No
interpreter, no warmup tier, no GC.

**Optional: hot sites.** Call `hot_site_enable(1)` and `hot_site_hit(id)` on
the sites you care about. Off by default — cost is basically a load and a
branch. On, it’s a relaxed atomic. Pull JSON with `hot_sites_json()` or hit
`/debug/hot_sites`. Stack sampling (`profile_sample_*`) is heavier; don’t put
it on every request unless you mean to.

**Offline: PGO and friends.** `scripts/pgo-build.sh` or
`scripts/adaptive-opt-cycle.sh`. Train with `MAKO_PGO_GEN` on staging, rebuild
production with `MAKO_PGO_USE`. `MAKO_ALLOCATOR=mimalloc|jemalloc` is there if
you want to poke at fragmentation under long soaks.

Leave out of production hot paths: `-fprofile-generate`-style clang
instrumentation, stack walkers on every call, and anything that patches code
while requests are in flight.

---

## Hot site API

You name the sites. App-defined ids, 0..255:

```mko
fn handle(route: int) -> int {
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
| `hot_site_hit(id)` | One hit (0 if disabled; −1 if id out of range) |
| `hot_site_count(id)` | Count for a site |
| `hot_site_total()` | Sum since last clear |
| `hot_site_top_id` / `top_count` | Hottest site |
| `hot_site_clear()` | Zero counters |
| `hot_sites_json()` | Compact export (`mako.hot_sites.v1`) |

HTTP path sits next to pprof: `/debug/hot_sites`.

---

## A loop that works for long-running services

```bash
./scripts/pgo-build.sh app.mko -o out/app -- <train-args>
./scripts/adaptive-opt-cycle.sh app.mko -o out/app
```

Ship release AOT first (LTO; mimalloc only if you’ve measured it helping).
Enable `hot_site_*` on a few route or handler ids and scrape
`/debug/hot_sites` out of band. Nightly or on staging, rebuild with
`pgo-build.sh` under traffic that looks like production. Swap with blue/green.
Nothing in the fleet recompiled itself.

---

## Tradeoffs, without the pitch deck

Systems that specialize in-process can win on some microkernels after a long
warmup. They also tend to bring cold-start cost, deopt stories, code growth
inside the process, and sometimes a collector or an embedded compiler. We took
the other side: full AOT from the start, fixed binary size at deploy, no GC,
no compiler living in the process.

That doesn’t mean online specialization never wins. On a few kernels it will.
We care more about predictable free and RSS over months, and we reach for
offline PGO when a workload needs another shot at peak throughput.

---

## Honesty bar

True: no GC, no live recompile, optional counters, offline PGO.

False or oversold: “the binary rewrites itself,” or throughput numbers with no
named soak and hardware (see LONG_RUNNING LR-7).

Tests: `examples/testing/hot_site_test.mko`. AOT layout work should leave
`hot_site_*` default-off and the PGO env wiring alone.
