# Mako soundness and runtime program

**Product tip:** 0.2.3+ · **Last sync:** 2026-07-18

This is the program of record for memory soundness and structured concurrency.
Each ID has a status, contract, and acceptance bar. Work stays **fast by
default**: checks that cost cycles on the hot path stay compile-time, cold
abort, or explicit opt-in — never a silent tax on steady-state code.

Related: [SECURITY.md](SECURITY.md) · [MEMORY_MODEL.md](MEMORY_MODEL.md) ·
[LANGUAGE_SPEC.md](../LANGUAGE_SPEC.md) · [SPEED.md](SPEED.md).

---

## Status legend

| Status | Meaning |
|--------|---------|
| **Done** | Shipped with tests / claims-gate evidence |
| **Partial** | Core behavior exists; gaps documented |
| **Planned** | Spec and acceptance written; implementation not complete |

---

## Soundness

### SAFE-001 — Bounds checks in safe release builds — **Done**

| | |
|--|--|
| **Contract** | Every safe index (`s[i]`, nested, string/byte) aborts on OOB in debug **and** release (`-O3 -flto -DNDEBUG`). |
| **Opt-out** | Only `unsafe { … }` and `unsafe_index` skip emission. |
| **Code** | `emit_bounds_check` always emits `MAKO_BOUNDS_CHECK`; prologue `#define MAKO_SAFE_DEFAULT 1`; runtime macro always aborts. |
| **Evidence** | `examples/bad/release_bounds_oob.mko` + claims-gate `release-bounds`; helper getters in `mako_rt.h` also abort. |
| **Speed** | Single `MAKO_UNLIKELY` compare; abort is cold. No per-index metadata load. |

### SAFE-002 — Type ownership categories in the language specification — **Done**

| | |
|--|--|
| **Contract** | Spec names **Copy**, **Own**, **View**, **Share**, **Arena**, **Sync** categories and which ops move vs copy. |
| **Doc** | [LANGUAGE_SPEC.md § Ownership categories](../LANGUAGE_SPEC.md#ownership-categories) |
| **Evidence** | Spec text + examples under `hold` / `share` / `arena` / `unsafe`. |

### SAFE-003 — Compiler-generated drops for core slices — **Done** (core shapes)

| | |
|--|--|
| **Contract** | Owning `[]int`/`[]byte`/`[]string`/`[]float`/`[]bool` from lit/`make`/`append`/`[]byte(…)` free at scope exit via `mako_*_array_free`. Sub-slices are **views** (`cap==0`) and never free backing. |
| **Code** | `runtime/mako_rt.h` free helpers + view slice_expr; codegen `own_drop_scopes` on block/`return`. |
| **Evidence** | `own_drop_slice_test.mko`; shared-view writes still pass. |
| **Gap** | Nested `[][]T` / monomorph `MakoArr_*` full free graph; free-on-reassign of intermediate grow-leaks. |

### SAFE-004 — Compiler-generated drops for built-in map shapes — **Done** (built-ins)

| | |
|--|--|
| **Contract** | Built-in `map[K]V` pointer maps free on scope exit (`mako_map_*_free`). |
| **Code** | Free helpers in `mako_rt.h` / `mako_goext.h`; codegen registers fresh `make(map…)` locals. |
| **Gap** | Demand monomorph `MakoMapS_Point*` etc. still need free in monomorph emitter (follow-up). |

### SAFE-005 — String ownership / view annotations and drop verification — **Partial**

| | |
|--|--|
| **Today** | `MakoString` owned vs process-empty singleton; `mako_str_view` / `mako_str_free` no-op for empty; zero-copy compares use views. |
| **Gap** | Type system does not yet distinguish `string` (owned) vs `string_view` (borrowed) at the Mako surface; free of views is a runtime convention. |
| **Target** | Optional surface or analysis: views never free; owned free once; kick/return of view from local backing rejected (ties to SAFE-007). |
| **Acceptance** | Bad examples for free-after-view and view-escape; claims-gate or unit tests. |

### SAFE-006 — Branch / return / loop / `?` correct drop insertion — **Partial**

| | |
|--|--|
| **Today** | Auto-drop for `share` (mid-scope NLL + block exit), MakoFn capture envs, arenas. Hold is move-checked, not drop-inserted for heap payloads. |
| **Target** | Single drop-plan pass over CFG: every Own value dropped exactly once on every exit (return, break, continue, `?`, panic-abort path). |
| **Speed** | Drop plan computed at compile time; no RC on the default path. |
| **Acceptance** | CFG suite: early return, loop continue, `?` from nested Result, both if arms — all free owning temps once (ASan / custom counters). |

### SAFE-007 — Reject borrowed and arena value escapes — **Partial** (arena return **Done**)

| | |
|--|--|
| **Done** | Return of arena handle or `arena_*`-backed local from inside `arena { }` is a type error (`arena_escape_return.mko` + claims-gate). |
| **Gap** | Storing arena views into longer-lived structs; general View lifetime beyond arena. |

### SAFE-008 — Closure and task capture ownership — **Partial**

| | |
|--|--|
| **Today** | Kick/fan reject unsynchronized mutable captures and unknown environments; POD/string/ShareInt/Sync handles allowed. Fn env drop helpers free string/share fields. |
| **Evidence** | `kick_mutable_closure_capture.mko`, `kick_mutable_lambda_capture.mko`, `fan_capture.mko`; claims-gate. |
| **Gap** | Full audit of every capture shape (nested closures, aliasing through `let f = g`) and drop of nested Own fields in env. |
| **Acceptance** | Capture matrix table in this doc + tests; no TSan races in `mako test --race` on the matrix. |

### SAFE-009 — Concurrent map sound design — **Done**

| | |
|--|--|
| **Contract** | `CMap` uses a portable readers/writer gate: concurrent readers, exclusive writers for probe+mutate and grow. No lock-free unsynchronized read of open-addressed tables. |
| **Code** | `runtime/mako_cmap.h` — `mako_rwmutex_rlock` / `lock` around get/set/del/incr/grow. |
| **Speed** | Readers share; writers exclusive; grow under write lock. Prefer batching sets off hot paths. |

### SAFE-010 — Publish the concurrency memory model — **Done**

| | |
|--|--|
| **Doc** | [MEMORY_MODEL.md](MEMORY_MODEL.md) |
| **Covers** | Sync handles, channels as happens-before edges, crew cancel/join, data-race freedom in safe Mako, intentional Sync. |

---

## Runtime and concurrency

### RT-001 — Crew exit, cancellation, failure — **Done** (spec + runtime)

| | |
|--|--|
| **Contract** | Leaving `crew` → cancel flag + join all ordinary kicks. Cooperative cancel; blocked C/FFI may delay join. New kicks after cancel do not start threads. First child `Err` recorded. |
| **Code** | `mako_nursery_cancel_join` / `mako_spawn` cancelled_start path. |
| **Doc** | [MEMORY_MODEL.md](MEMORY_MODEL.md#crew-lifecycle) · [SECURITY.md](SECURITY.md) |
| **Evidence** | `examples/testing/cancel_policy_test.mko` |

### RT-002 — Bounded scheduler abstraction behind `kick` — **Planned**

| | |
|--|--|
| **Today** | Each kick is typically a pthread (or host thread). |
| **Target** | Scheduler interface: spawn, join, cancel; default thread pool with bound; same Mako surface. |
| **Speed** | Pool reuse removes pthread create on hot fan-out. |
| **Acceptance** | Configurable max workers; stress test under bound without process spawn storm. |

### RT-003 — Separate non-blocking and blocking task execution — **Planned**

| | |
|--|--|
| **Target** | Mark or detect blocking kicks (I/O, FFI) so the pool does not stall compute workers; optional blocking pool. |
| **Acceptance** | Doc + API (`kick_blocking` or attribute); soak with mixed load. |

### RT-004 — Ownership for failed / timed-out channel sends — **Partial**

| | |
|--|--|
| **Today** | Int/float/POD: by value; failed try_send does not consume caller locals. Strings: clone-on-send or take variants; try_send drops counted in stats. |
| **Doc** | [MEMORY_MODEL.md](MEMORY_MODEL.md#channel-ownership) |
| **Gap** | Uniform surface for take-vs-clone on all channel element types; timeout path ownership table complete for every monomorph. |
| **Acceptance** | Table in MEMORY_MODEL for every `chan[T]` send/try_send/timeout with Own/View rules + tests. |

### RT-005 — Randomized channel/select stress tests — **Partial**

| | |
|--|--|
| **Today** | Deterministic tests: `chan_*`, select in wave tests, cancel policy. |
| **Shipped seed** | `examples/testing/chan_select_stress_test.mko` — multi-sender/receiver under timeout select. |
| **Target** | Seed-controlled random schedules, longer soaks in CI optional job. |

### RT-006 — Task and resource census APIs for soak tests — **Done**

| | |
|--|--|
| **API** | `runtime_stats_json()` / `runtime_stats_reset()` — tasks spawned/joined, channel counters, lock wait ns. |
| **Evidence** | `examples/testing/runtime_census_test.mko` |
| **Doc** | [BUILTINS.md](BUILTINS.md) · book ch06 |

---

## Implementation order (recommended)

1. **SAFE-001 / 009 / 010 / RT-001 / RT-006** — lock and document (this ship).
2. **SAFE-002** — ownership categories (this ship).
3. **SAFE-003 → 004 → 005 → 006** — drop plan (codegen CFG); largest compiler work.
4. **SAFE-007 / 008** — escape + capture audit on top of categories.
5. **RT-004** — finish channel ownership table with monomorph tests.
6. **RT-002 / 003** — scheduler (speed + isolation).
7. **RT-005** — expand stress/soak.

## Non-goals

- Tracing GC
- Disabling bounds checks in “safe release”
- Free `go` without a crew
- Making Sync handles the default for ordinary locals
