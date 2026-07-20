# Mako soundness and runtime program

**Product tip:** 0.3.0 · **Last sync:** 2026-07-19

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

### SAFE-006 — Branch / return / loop / `?` / match correct drop insertion — **Done** (core)

| | |
|--|--|
| **Done** | Block exit free; return transfer + materialize-before-free; **break/continue** free owns in loop-body scopes; **`?` early-return** frees all live owns/shares/fn-envs + runs defers; **match** Result/Option Own payloads free at arm exit (or move into match result); fresh discarded bags free resolved payload shapes while indexed/field bags remain borrowed; if/match arm-local free via pop-before-restore. |
| **Double-free guards** | Live Own **moves** on store/arm value; **aliases** and field/index borrows **clone**. Free attaches to **bind scope** (not nested if/match arm). Alias muts (`let mut out = path`) use a runtime `{name}__own` flag so path-insensitive free never frees a still-aliased caller/param buffer when a reassign arm is not taken. |
| **Path-local free** | Early-return free only names whose bind scope is still on the stack (`bind_scope_active`). Sequential if-arms reusing `let blob` rebind `own_bind_scope` per arm; finished arms clear freer flags so sibling early-returns do not emit `free(blob)` for an undeclared C local (leba admin). |
| **Code** | `own_drop_scopes` / `own_bind_scope` / `own_cond_flags`; `prepare_own_store_rhs`; `finish_arm_own_live`; `bind_scope_active` / `emit_free_one`; match pattern `register_own_drop`; `transfer_or_clone_expr_own`. |
| **Evidence** | `cfg_drop_break_test`, `slice_return_own_test`, `try_drop_test`, `own_branch_regress_test`, `match_own_free_test`, `discarded_bag_drop_test`, `discarded_bag_borrow_test`, `double_free_guard_test`, `early_return_path_free_test`, ASan ownership suite; leba `main.mko` builds. |
| **Speed** | Move when the source is the registered freer (no extra alloc). Clone only for aliases / field-index borrows. Conditional free is one branch per alias mut. |
| **Residual** | Payload cleanup for discarded expressions whose source type cannot yet be recovered by codegen; labeled multi-loop edge cases if/when multi-level labels land. |

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

### RT-005 — Seeded channel/select stress tests — **Done (core)**

| | |
|--|--|
| **Today** | Seed-controlled producer jitter, channel capacity, close/receive races, and concurrent int, string, and struct selectors. |
| **Replay** | Set `MAKO_RT_STRESS_SEED` and `MAKO_RT_STRESS_ROUNDS` when running `examples/testing/chan_select_stress_test.mko`. CI replays two fixed seeds under TSan. |
| **Optional depth** | Periodic jobs can raise the round count or add more seeds for longer soaks. |

### RT-006 — Task and resource census APIs for soak tests — **Done**

| | |
|--|--|
| **API** | `runtime_stats_json()` / `runtime_stats_reset()` — tasks spawned/joined, channel counters, lock wait ns. |
| **Evidence** | `examples/testing/runtime_census_test.mko` |
| **Doc** | [BUILTINS.md](BUILTINS.md) · book ch06 |

---

## Implementation order

### Shipped (0.2.4, extended in 0.2.5)

1. **SAFE-001 / 002 / 009 / 010 / RT-001 / RT-005 / RT-006** — bounds, categories, CMap, docs, crew, channel stress, census.
2. **SAFE-003 / 004** — owning slice/map free (built-in + monomorph); views; return transfer; free-on-reassign; nested free (safe).
3. **SAFE-005** — `string_view` + owning string free.
4. **SAFE-006 (break/continue/return/`?`/match)** — loop-exit cleanup; return transfer + materialize; `?` early free; match Own free; bind-scope free; alias-mut `__own` flag; move/clone store rules (no double-free).
5. **SAFE-007 / 008** — arena/slice escape; capture matrix.
6. **RT-002 / 003 / 004** — scheduler pool + spawn_blocking; channel take-send ownership.
7. **Struct Own free** — deep free of string/slice fields on drop.
8. **Lang safety** — field/index mut roots; temp lvalue reject; empty `[]` lit; stack POD lits.

### Remaining (optional depth / 0.3.0+)

1. Longer TSan soak jobs on capture matrix (CI optional).
2. RT-004 monomorph channel take matrix beyond int/float/string.
3. Deeper scheduler (dynamic resize, work-stealing) if pool soaks demand it.
4. Longer channel/select soaks with additional seeds.
5. Make discarded bag cleanup type-complete when typed expression metadata reaches codegen.

See also [ROADMAP.md](ROADMAP.md) · [ROADMAP_IMPL.md](../ROADMAP_IMPL.md).

## Non-goals

- Tracing GC
- Disabling bounds checks in “safe release”
- Free `go` without a crew
- Making Sync handles the default for ordinary locals
