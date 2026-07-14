# Speed · concurrency · parallelism · security

**The name of the game is speed.**  
Mako exists to run **as close to Rust as possible** on real workloads — then go
further with **first-class concurrency and parallelism** that do not leak tasks
or paint the language with async colors, and **first-class security** that does
not silently tax the hot path.

| Priority | Bar |
|----------|-----|
| **1. Speed** | **Name of the game.** Hot path ≈ Rust: native binary, no GC, release `-O3 -flto`, zero-cost defaults |
| **2. Concurrency** | **First-class:** `crew` / `kick` / `join` / channels / `select` / actors |
| **3. Parallelism** | **First-class:** `fan` and crew work across cores — not a library afterthought |
| **4. Security** | **First-class:** memory + resource contracts, secure defaults — see [SECURITY.md](SECURITY.md) |

Syntax stays **Mako’s own**. Speed is not optional. Concurrency is not bolted on.
Security is not a linter plugin.

---

## Speed (non-negotiable)

- **No mandatory GC** — no stop-the-world tax on p99
- **Native codegen** — `.mko` → C → clang
- **Release:** `-O3 -flto`
- **Visible cost** — `hold` / `share` / `arena` / channels when you pay
- **Measure** — [PERFORMANCE.md](PERFORMANCE.md), `scripts/bench-vs-go-rust.sh`

Any feature that silently slows the hot path must justify itself or stay opt-in.

Security tools that cost (bounds-always, overflow trap, sanitizers) stay **opt-in**
or debug-default so release hot paths stay Rust-competitive. Safe-by-construction
features (NLL, structured crews, parameterized DB) are free at steady state.

---

## Concurrency (first-class)

Not “use a package.” Not colored `async`/`await`. Keywords in the language:

| Tool | Role |
|------|------|
| `crew t { … }` | Structured scope — jobs cannot outlive the crew |
| `t.kick(work())` | Start concurrent work on the crew |
| `job.join()` | Wait for result (`int` / `string` / `Result` / float; heap-box non-int) |
| `t.drain(ms)` | Cancel + join with timeout |
| channels + `select` | Message-passing (`chan_open[T]`; select is int-ring today) |
| `actor` / `receive` | Long-lived concurrent entities |

```mko
crew t {
    let a = t.kick(fetch_a())
    let b = t.kick(fetch_b())
    print(a.join() + b.join())
}
// both jobs done — no orphans
```

**Vs Go:** no free `go f()` leaks.  
**Vs Rust:** no async coloring / executor maze for the default path.

Race smoke: `mako test --race` · CI TSan job on concurrency tests.

---

## Parallelism (first-class)

| Tool | Role |
|------|------|
| `fan` | Data-parallel map: `[]int` / `[]float` / `[]string` / `[]Struct` (`mako_par_map_*`) |
| `crew` + many `kick`s | Task parallelism within a scope |

```mko
// Parallel map — first-class, not a third-party pool
let out = fan(items, fn(x) { heavy(x) })
// also: fan(items, |x| heavy(x))
// structs: fan(points, |p| Point { x: p.x * 2, y: p.y * 2 })
```

Parallelism should be **easy to spell** and **hard to misuse** (scoped, joinable).

Runnable: `examples/concurrency.mko`, `examples/parallel.mko` · tests:
`crew_fan_test.mko`, `fan_struct_test.mko`, `fan_float_test.mko`, `kick_send_test.mko`.

**CI speed bar:** `./scripts/bench-gate.sh` (ubuntu job; default ≤2.0× Rust).

### Send-like kick (seed)

`crew.kick(f(args…))` allows:

| OK | How |
|----|-----|
| Copy (int/bool/float/…) | packed as `intptr_t` |
| `string` | heap-cloned for the task |
| Deep-**POD** named structs (scalar/string/nested POD fields) | heap-boxed for the task |
| `Option` / `Result` / tuples of sendables | boxed payloads |
| Enums with sendable payloads | boxed (no map/array fields) |
| `ShareInt` | atomic RC + **auto-clone** onto the heap |
| `chan[T]` | handle shared (channel is the sync point) |

**Not OK as kick args:** arrays, maps, arenas, non-POD structs (e.g. map/slice fields),
struct fields that are enums today (not deep-POD). Prefer channels for large or
mutating results:

```mko
struct Point { x: int, y: int }

// POD struct as kick arg
crew t {
    let j = t.kick(work(Point { x: 1, y: 2 }))
    let _ = j.join()
}

// Result fan-in: named struct on a channel (no int bit-packing)
let ch = chan_open[Point](2)
let _ = ch.send(Point { x: 1, y: 2 })
let p = ch.recv()
```

Tests: `kick_send_test`, `wave10_queue_test` / `wave11_queue_test` (POD kick),
`chan_struct_test`, `lang_residuals_test` (deep POD).

### Parallel map

`fan` uses **hardware concurrency** (capped), and stays single-threaded for small
inputs to avoid spawn overhead on the speed path.

| Collection | Runtime |
|------------|---------|
| `[]int` family | `mako_par_map_int` |
| `[]float` | `mako_par_map_float` |
| `[]string` | `mako_par_map_str` (clones; ≤8 threads) |

```mko
let ys = fan([1, 2, 3, 4], |x| x * x)
let zs = fan([1.0, 2.0], fn(x) { x * 2.0 })
let ss = fan(["a", "b"], |x| x + "!")
```

### Speed gate

```bash
./scripts/bench-gate.sh              # fib + slice + map vs Rust (default max 2.0×)
./scripts/bench-gate.sh 1.5          # strict stretch goal
MAKO_BENCH_STRICT=1 ./scripts/bench-gate.sh
./scripts/bench-vs-go-rust.sh        # full microbench table
```

### Identity surface

```bash
mako lint --identity path/   # dual spellings as style warnings (func, :=, import, …)
```

---

## Design rules

1. **Speed first** on the sequential hot path.
2. **Concurrency/parallelism are language features**, not stdlib footnotes.
3. **Structured by default** — work has a lifetime (`crew`).
4. **No async coloring** as the primary model.
5. **Measure** before claiming wins; reject regressions on release kernels.

---

## Related

| Doc | Role |
|-----|------|
| [PERFORMANCE.md](PERFORMANCE.md) | Numbers, profiles, bench methodology |
| [PAIN_POINTS.md](PAIN_POINTS.md) | Why we reject GC / free tasks / async color |
| [IDENTITY.md](IDENTITY.md) | Mako surface (`crew`, `fan`, …) |
| [GUIDE.md](GUIDE.md) | Full concurrency syntax |
| [ERGONOMICS.md](ERGONOMICS.md) | Short happy path |
