# Speed · concurrency · parallelism

**The name of the game is speed.**  
Mako exists to run **as close to Rust as possible** on real workloads — then go
further with **first-class concurrency and parallelism** that do not leak tasks
or paint the language with async colors.

| Priority | Bar |
|----------|-----|
| **1. Speed** | Hot path ≈ Rust: native binary, no GC, release `-O3 -flto`, zero-cost defaults |
| **2. Concurrency** | First-class: `crew` / `kick` / `join` / channels / `select` / actors |
| **3. Parallelism** | First-class: `fan` and crew work across cores — not a library afterthought |

Syntax stays **Mako’s own**. Speed is not optional. Concurrency is not bolted on.

---

## Speed (non-negotiable)

- **No mandatory GC** — no stop-the-world tax on p99
- **Native codegen** — `.mko` → C → clang
- **Release:** `-O3 -flto`
- **Visible cost** — `hold` / `share` / `arena` / channels when you pay
- **Measure** — [PERFORMANCE.md](PERFORMANCE.md), `scripts/bench-vs-go-rust.sh`

Any feature that silently slows the hot path must justify itself or stay opt-in.

---

## Concurrency (first-class)

Not “use a package.” Not colored `async`/`await`. Keywords in the language:

| Tool | Role |
|------|------|
| `crew t { … }` | Structured scope — jobs cannot outlive the crew |
| `t.kick(work())` | Start concurrent work on the crew |
| `job.join()` | Wait for a result |
| channels + `select` | Message-passing, multi-way wait |
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

---

## Parallelism (first-class)

| Tool | Role |
|------|------|
| `fan` | Data-parallel map over a collection/range (use the cores) |
| `crew` + many `kick`s | Task parallelism within a scope |

```mko
// Parallel map — first-class, not a third-party pool
let out = fan(items, fn(x) { heavy(x) })
// also: fan(items, |x| heavy(x))
```

Parallelism should be **easy to spell** and **hard to misuse** (scoped, joinable).

Runnable: `examples/concurrency.mko`, `examples/parallel.mko` · tests: `examples/testing/crew_fan_test.mko`, `kick_send_test.mko`.

### Send-like kick (seed)

`crew.kick(f(args…))` allows:

| OK | How |
|----|-----|
| Copy (int/bool/float/…) | packed as `intptr_t` |
| `string` | heap-cloned for the task |
| `ShareInt` | atomic RC + **auto-clone** onto the heap |
| `chan[T]` | handle shared (channel is the sync point) |
| `chan[Struct]` | heap-box send / unbox recv (`MakoChanPtr`) |

**Not OK as kick args:** arrays, maps, bare structs, arenas — send structs on **channels**.

```mko
let ch = chan_open[Point](2)
let _ = ch.send(Point { x: 1, y: 2 })
let p = ch.recv()
```

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
