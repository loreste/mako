# Mako concurrency memory model

**SAFE-010 · RT-001 · RT-004** · Product tip **0.4.0**

Safe Mako aims for **data-race freedom by construction** for ordinary locals,
with an explicit **Sync** escape hatch. There is no GC. Speed comes from
structured concurrency and Send checks at kick boundaries — not from optional
sanitizers (those remain opt-in for FFI and runtime smoke).

Related: [SOUNDNESS.md](SOUNDNESS.md) · [SECURITY.md](SECURITY.md) ·
[ASYNC.md](ASYNC.md) · [SPEED.md](SPEED.md).

---

## 1. Values and threads

A **task** is a unit of concurrent work started by `crew.kick` / `go` inside a
`crew` (or `fan` workers). Tasks may run on OS threads today (RT-002 will insert
a bounded scheduler behind the same surface).

Ordinary `let` / `let mut` locals are **task-local**. Crossing a task boundary
requires a **Send** value or an explicit **Sync** handle.

### Happens-before (informal)

| Edge | Ordering |
|------|----------|
| Sequentially in one task | Program order |
| `kick` argument setup → body of kicked task | Kick synchronizes-with start of task body |
| Task body finish → `join` / crew exit join | Join synchronizes-with waiter |
| Channel `send` → matching `recv` | Send synchronizes-with receive of that message |
| Channel `close` → `recv` seeing drain/closed | Close happens-before closed observations |
| `share_int` / Mutex / RWMutex / CMap / AtomicInt ops | As defined by those primitives (lock/unlock or atomic) |

Safe Mako rejects compilations that would allow two tasks to race on the same
unsynchronized mutable location.

---

## 2. Sync handles (intentional sharing)

| Type | Role |
|------|------|
| `AtomicInt` | Atomic integer |
| `ShareInt` | RC shared int with NLL vs source mutation |
| `Mutex` / `RWMutex` | Exclusive / shared locks |
| `CMap` | Concurrent map with readers/writer gate (**SAFE-009**) |
| Channels | Message transfer, not shared memory |

Use these for shared mutable state across kicks. Do not rely on `let mut`
captured by two tasks.

---

## 3. Kick Send (compiler contract)

`crew.kick(f(args…))` accepts **Send** arguments only:

- Copy scalars (int family, bool, float, Uuid/ULID POD)
- Deep-POD structs / POD enums
- `string` (heap-cloned at boundary)
- Channels, Sync handles (RC/clone as defined)
- Option / Result / tuple of Send (heap-boxed when needed)

**Rejected:** arrays, maps, non-POD structs, `Arena`, nested `Crew`.

Captures: unsynchronized mutable captures and unknown environments are
rejected; `fan` mappers must be capture-free.

Evidence: `examples/bad/kick_*.mko`, `fan_capture.mko`.

---

## 4. Crew lifecycle (RT-001)

```
crew c {
    let j = c.kick(work())
    // …
}   // ← cancel + join all ordinary kicks
```

| Event | Behavior |
|-------|----------|
| Enter crew | New nursery (`MakoNursery`) |
| `kick` / `go` | Spawn task; if already cancelled, task does not start |
| Explicit cancel | Sets cancel flag; cooperative observation via nursery cancelled |
| Leave crew block | **cancel_join**: set cancel, join all tasks |
| Child `Result` Err | First error message retained (`first_err`); count incremented |
| Blocked C/FFI | May delay join until the call returns (documented limit) |
| `detach` | Explicit; ordinary code should not use it for fire-and-forget |

Cancellation is **cooperative** for already-running work. Prefer short critical
sections and channel-based shutdown for production services.

Tests: `examples/testing/cancel_policy_test.mko`.

---

## 5. Sequential Own free (SAFE-003–006, single freer)

Within one task, heap **Own** values free **once** at the end of their live
range. Path-insensitive codegen (if/match arms) must not free the same buffer
from two freers or free a still-aliased parameter.

| Rule | Behavior |
|------|----------|
| Scope / block exit | Free still-live owns registered for that scope |
| Reassign | Free previous Own when backing pointer changes (nested: `*_release_replaced`) |
| Return | Transfer (no free of returned Own); materialize before free of other locals |
| break / continue / `?` | Free loop-body or early-return lives as documented in [SOUNDNESS.md](SOUNDNESS.md) |
| **match** Own payload | Free at arm exit, or **move** into the match result / `let` |
| Store of live Own | **Move** free duty to destination |
| Store of alias / field / index | **Clone** so the original freer and the destination do not share free |
| Alias mut (`let mut out = path`) | Runtime freer flag: free only after a reassign takes Own |

Evidence (ASan): `own_branch_regress_test`, `match_own_free_test`,
`double_free_guard_test`. Details: [SOUNDNESS.md SAFE-006](SOUNDNESS.md).

---

## 6. Channel ownership (RT-004)

| Channel element | `send` / `try_send` / timeout | Failed send | `recv` |
|-----------------|-------------------------------|-------------|--------|
| `int` / `float` / `bool` / POD enum | By value (copy) | Caller keeps value | By value |
| `string` (default) | **Clone** into channel | Caller keeps owned string | **Owned** string |
| `string` take (`chan_str_send_take` / `try_send_take`) | **Move** into channel | Payload freed by runtime (no double-free of caller local) | **Owned** string |
| `chan[Struct]` / ptr ring | Box / handle as documented | Caller keeps unless take API | Owned / handle |

### Stats

Failed try-sends increment `channel_try_send_drops` in `runtime_stats_json()`.

### Select

`select` with timeout: at most one arm receives; ownership follows the arm that
fires. Timeout arm receives nothing.

### Scheduler (RT-002 / RT-003)

| API | Role |
|-----|------|
| `sched_set_workers(n)` | n>0 enables fixed worker pool for kicks; n=0 = one pthread per kick (default) |
| `sched_workers()` | Current configured worker count |
| `mako_spawn` | Pool when configured; else dedicated pthread |
| `mako_spawn_blocking` | Always dedicated pthread (blocking I/O / FFI) |

---

## 7. Data races vs Sync races

| Class | Safe Mako |
|-------|-----------|
| Two tasks write `let mut x` without Sync | **Compile error** (race model / capture rules) |
| Two tasks use `CMap` / Mutex correctly | Allowed; runtime serializes |
| FFI / `unsafe` raw memory | Outside the model — use TSan (`mako test --race`) |

---

## 8. Census (RT-006)

```mko
runtime_stats_reset()
// … work …
let s = runtime_stats_json()
// tasks_spawned, tasks_joined, channels_*, lock_wait_ns, …
```

Use in soak tests to assert `tasks_spawned == tasks_joined` after crews complete
and to watch channel peak depth / try_send drops.

---

## 9. What this model is not

- Not the C++/LLVM memory model formalization (yet).
- Not a promise that every FFI call is race-free.
- Not a license to disable bounds checks (SAFE-001).

Formalization may deepen; the **surface contracts** above are what the compiler
and runtime implement today.
