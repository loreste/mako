# Speed · concurrency · parallelism · security

Mako compiles to native code via C with `-O3 -flto` in release mode.
There is no garbage collector, interpreter, or VM. Concurrency and
parallelism are language primitives, not library add-ons.

| Priority | Bar |
|----------|-----|
| **1. Speed** | Native binary, no GC, release `-O3 -flto`, low-overhead defaults — **beat C/Rust per published workload** ([SPEED_SAFE.md](SPEED_SAFE.md)) |
| **2. Memory safe** | Ownership free, no GC — [MEMORY_SAFETY.md](MEMORY_SAFETY.md); never trade checks for speed on the safe path |
| **3. Years-up** | Stable RSS + no GC pauses for services that run for months/years — [LONG_RUNNING.md](LONG_RUNNING.md) |
| **4. Concurrency** | **First-class:** `crew` / `kick` / `join` / channels / `select` / actors |
| **5. Parallelism** | **First-class:** `fan` and crew work across cores |
| **6. Security** | Memory + resource contracts, secure defaults — see [SECURITY.md](SECURITY.md) |

Syntax stays **Mako’s own**. Speed is not optional. Concurrency is not bolted on.
Security is not a linter plugin.

---

## Speed (non-negotiable)

- **No GC** — no stop-the-world tax on p99
- **Native codegen** — `.mko` → C → clang
- **Release:** `-O3 -flto`
- **Visible cost** — `hold` / `share` / `arena` / channels when you pay
- **Measure** — [PERFORMANCE.md](PERFORMANCE.md), `scripts/bench-vs-go-rust.sh`

Any feature that silently slows the hot path must justify itself or stay opt-in.

**Long-running servers:** Mako’s structural advantage is **no GC** and
**ownership-bounded live memory**. Microbenches alone do not prove years-up
stability — use `./scripts/long-run-soak.sh` and read [LONG_RUNNING.md](LONG_RUNNING.md).

Sanitizers and overflow traps stay **opt-in** so release hot paths stay
fast. Safe-by-construction features (NLL, checked indexing, structured
crews, parameterized DB) remain part of the default contract.

### Hot-path efficiency (current practice)

| Surface | Cost model |
|---------|------------|
| Non-capturing `fn` values / lambdas | Static C helpers; `void*` + cast — **no env heap** |
| `f"…{x}"` | **Stack-based builder** (256 B on stack, spills to heap only if needed) — zero malloc for short strings |
| `x == "literal"` / `str_eq` / `str_has_prefix` / `match` arms | **Zero-copy view** of literals (points into `.rodata`, no alloc) |
| `print("literal")` | **Zero-copy** — no heap allocation for string literal arguments |
| `1 + 2` (literal operands) | **Compile-time fold** — no runtime code emitted |
| Map key hashing | **wyhash** (8 bytes/iteration, 128-bit mix) — replaced byte-at-a-time FNV-1a |
| HTTP header parsing | **Length-bucketed switch** — skip non-matching headers without comparing |
| HTTP active connections | **Atomic counter** — O(1) instead of O(n) array scan |
| `chan_cap()` | **Lock-free read** — capacity is immutable after creation, no mutex needed |
| `fmt_sprintf*` | Prefer when you need format verbs; fewer pieces than a long f-string |
| `chan[Struct]` / `chan[tuple]` / `chan[Enum]` | Ptr ring; payload boxes via **size freelist** (≤512B) to cut malloc churn |
| `chan_len` / `chan_cap` | Work on **any** `chan[T]` (int/str/ptr rings); int `cap` is lock-free |
| Demand-driven map monomorphs | Emit only used `(K,V)` shapes — O(used), not N²; joined key lookup in codegen |
| Timed chan / join | `send_timeout` / `recv_timeout` / `join_timeout` / `join_deadline` — **2ms sleep slices**, no busy-spin |
| `select` | Shared **condvar** wake on send/close (not 2 ms nanosleep poll); 50 ms max wait slice for races |
| Slice append | `malloc + memcpy` on grow preserves sub-slice aliasing safety — no undefined behavior |
| Codegen emit | Hot paths use `emit_line` / `format_args!` — no intermediate `String` per C line |
| POD array lits `[a,b,c]` | **Stack buffer + `cap==0` view** — zero malloc/free in hot loops (`int`/`float`/`bool`/`byte`) |
| Empty `[]` / `make([],0,0)` | **No heap** — `{NULL,0,0}` until first grow |
| Escape (return / store / map set) | `mako_*_array_to_owned` — identity if already heap-owned; copy views only |
| Slice free | `MAKO_UNLIKELY(cap>0)` — free is cold; views and stack lits are no-ops |
| `string_view` / `str_as_view` | **Zero-copy** reads; never free the view |
| `sched_set_workers(n)` | Opt-in pool reuses worker threads for kicks (default n=0 = one pthread each) |

**Ownership without a speed tax:** keep short-lived POD slices as stack views in
tight loops; only pay for a heap copy when the value escapes. Prefer
`make([]T, 0, n)` when you know capacity and will grow. Avoid reallocating a
fresh lit every iteration when a single buffer can be reused.

**Ownership-based drops on the free path:** scope exit, reassign, break /
continue, return (transfer + materialize), and `?` early-return all free live
owns. Views (`cap==0`) and the empty-string singleton never free backing
storage. The full suite passes under AddressSanitizer (within safe Mako code;
`unsafe` blocks and FFI remain outside this guarantee). Free is cold
(`MAKO_UNLIKELY`); stack lits and zero-copy string compares never malloc.

Capturing closures (env boxes) stay residual until they can pay for themselves.

### Portable timeouts (runtime trust seed)

```mko
// Relative
match ch.recv_timeout(50) {
    Ok(v) => { /* … */ },
    Err(msg) => { /* "timeout" or "closed" */ },
}
match job.join_timeout(100) {
    Ok(v) => { /* … */ },
    Err(_) => { /* "timeout" */ },
}

// Absolute mono deadline (compose with mono_* budgets)
let dl = deadline_ms(200)
match job.join_deadline(dl) {
    Ok(v) => { /* … */ },
    Err(_) => { /* deadline hit */ },
}
assert(deadline_remaining_ms(dl) >= 0)
```

Tests: `timeout_portable_test.mko`. Prefer **mono** `deadline_*` for budgets; wall `now_ms` only for logs.

---

## Concurrency (first-class)

Not “use a package.” Not colored `async`/`await`. Keywords in the language:

| Tool | Role |
|------|------|
| `crew t { … }` | Structured scope — `kick` jobs cannot outlive the crew |
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
// both kicked jobs done — no orphans
```

Kicked tasks are always joined at `crew` exit — no orphaned work.
Concurrency is synchronous by default — no function coloring.

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
| Deep-**POD** named structs (scalar/string/nested POD/enum fields) | heap-boxed for the task |
| `Option` / `Result` / tuples of sendables | boxed payloads |
| POD enums (unit or POD payloads); `chan[Enum]` | boxed / ptr ring |
| `ShareInt` | atomic RC + **auto-clone** onto the heap |
| `chan[T]` | handle shared (channel is the sync point) |

**Not OK as kick args:** arrays, maps, arenas, non-POD structs (e.g. map/slice fields).
Prefer channels for large or mutating results:

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
