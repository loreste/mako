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

### SAFE-003 — Compiler-generated drops for core slices — **Done**

| | |
|--|--|
| **Contract** | Owning slices free at scope exit / reassign (when backing pointer changes). Views use `cap==0`. Nested `[][]T` free outer + scalar inners at final drop; free-on-reassign uses `*_release_replaced` so shared inners after append/grow are not double-freed. |
| **Code** | `mako_*_array_free`, `mako_arr_arr_*_free` / `*_release_replaced`; codegen `own_drop_scopes` + reassign free; nested append uses malloc+copy (not realloc). |
| **Evidence** | `own_drop_slice_test`, `slice_return_own_test`, `nested_arr_drop_test`, ASan reassign/break/append probes. |
| **Also** | Return transfers ownership + materialize-before-free; view escape/return type errors; stack POD lits (`cap==0`) heapify on escape. |

### SAFE-004 — Compiler-generated drops for maps — **Done**

| | |
|--|--|
| **Contract** | Built-in and monomorph `map[K]V` heap handles free on scope exit (`mako_map_*_free` / `{fnp}_free`). |
| **Code** | `emit_map_heap_free` for monomorphs; built-ins in `mako_rt.h` / `mako_goext.h`. |
| **Evidence** | `map_*` suite including nested/helpers; map pointer arrays free shallowly (no alias double-free). |

### SAFE-005 — String ownership / view annotations and drop verification — **Done**

| | |
|--|--|
| **Done** | Owning `string` free on scope exit / reassign. Surface type **`string_view`**: zero-copy lit binding, `str_as_view(s)`, `str_to_owned(v)`; views never free. Empty singleton never freed. |
| **Evidence** | `string_drop_test`, `string_view_test`. |

### SAFE-006 — Branch / return / loop / `?` correct drop insertion — **Done** (core)

| | |
|--|--|
| **Done** | Block exit free; return transfer + materialize-before-free; **break/continue** free owns in loop-body scopes; **`?` early-return** frees all live owns/shares/fn-envs + runs defers (`try_drop_test`). |
| **Evidence** | `cfg_drop_break_test`, `slice_return_own_test`, `try_drop_test`, ASan probes. |
| **Residual** | Labeled multi-loop edge cases if/when labels land. |

### SAFE-007 — Reject borrowed and arena value escapes — **Done** (core)

| | |
|--|--|
| **Done** | Arena return escape; slice view store/return escape; **arena store into fields/outer assigns** (`arena_store_field.mko`). |

### SAFE-008 — Closure and task capture ownership — **Done** (core matrix)

| | |
|--|--|
| **Contract** | Kick/fan reject unsynchronized mutable captures; POD/string/ShareInt/Sync allowed. Nested crews OK. Fn env drop frees string/share fields. |
| **Capture matrix** | See table below. |
| **Evidence** | `capture_matrix_test`, `kick_*` tests, `bad/kick_mutable_*`, `bad/fan_capture`; ASan. |

| Capture / kick arg | Allowed? |
|--------------------|----------|
| POD (int/float/bool/enum unit) | Yes (copy) |
| `string` | Yes (clone onto heap for task) |
| Deep-POD struct | Yes (box) |
| `ShareInt` / Sync handles | Yes (auto-clone / handle share) |
| `chan[T]` | Yes (handle share) |
| `let mut` captured into kick/fan without Sync | **No** (type error) |
| Arrays / maps / arenas as kick args | **No** |

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

### RT-002 — Bounded scheduler abstraction behind `kick` — **Done** (seed)

| | |
|--|--|
| **API** | `sched_set_workers(n)` / `sched_workers()` — opt-in pool (n>0). Default n=0 keeps one pthread per kick. |
| **Code** | `mako_sched_*` + `mako_spawn` routes non-blocking work to the pool when configured. |
| **Evidence** | `sched_pool_test`. |

### RT-003 — Separate non-blocking and blocking task execution — **Done** (seed)

| | |
|--|--|
| **API** | `mako_spawn_blocking` always uses a dedicated pthread (I/O/FFI). Pool path is for compute kicks only. |
| **Code** | `mako_spawn_ex(..., blocking)`. |

### RT-004 — Ownership for failed / timed-out channel sends — **Done** (core)

| | |
|--|--|
| **Contract** | POD: by value; failed try/timeout does not consume. String default send clones; take-send moves (failed take frees payload, not double-free local). |
| **Doc** | [MEMORY_MODEL.md](MEMORY_MODEL.md#channel-ownership) |
| **Evidence** | `channel_ownership_test` (clone, take, try_take full, float, timeout). |

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

## Implementation order

### Shipped (0.2.3+)

1. **SAFE-001 / 002 / 009 / 010 / RT-001 / RT-005 seed / RT-006** — bounds, categories, CMap, docs, crew, census.
2. **SAFE-003 / 004** — owning slice/map free (built-in + monomorph); views; return transfer; free-on-reassign; nested free (safe).
3. **SAFE-006 (break/continue/return/`?`)** — loop-exit cleanup; return transfer + materialize; `?` early free.
4. **SAFE-007** — arena return/store escape; slice view escape/return.
5. **RT-004 seed** — `channel_ownership_test`.
6. **Lang safety** — field/index mut roots; temp lvalue reject; empty `[]` lit.

### Remaining (0.2.4+)

1. Longer TSan soak jobs on capture matrix (CI optional).
2. RT-004 monomorph channel matrix beyond int/float/string.
3. Deeper scheduler (dynamic resize, work-stealing) if pool soaks demand it.
5. Nested Own fields inside structs (header free vs deep field free).
6. **RT-005** — randomized longer soaks.

See also [ROADMAP.md](ROADMAP.md) · [ROADMAP_IMPL.md](../ROADMAP_IMPL.md).

## Non-goals

- Tracing GC
- Disabling bounds checks in “safe release”
- Free `go` without a crew
- Making Sync handles the default for ordinary locals
