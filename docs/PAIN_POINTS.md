# Pain points Mako is built to fix

**Mako is not a clone of Go or Rust.**  
It is a unique language. The product job is to **remove the real pain** of those
ecosystems while keeping their strengths as *goals* (simple code, strong control)
— never as surfaces to copy.

| Keep as goals | Reject as identity |
|---------------|--------------------|
| Short programs, fast builds, great stdlib | Go keywords / ceremony as preferred syntax |
| Memory safety, **performance as close to Rust as possible**, no mandatory GC, explicit errors | Lifetime maze, trait soup, async coloring |

Canonical surface: [IDENTITY.md](IDENTITY.md). Compat duals only: [COMPAT.md](COMPAT.md).

---

## Scoreboard (honest)

| Source of pain | Mako response | Status |
|----------------|---------------|--------|
| Go GC / tail latency | No mandatory GC; `hold` / `share` / `arena` | **Strong** |
| Go `nil` + silent mistakes | No null; `Option` / `Result`; unused `Result` is illegal | **Strong** |
| Go `if err != nil` noise | `Result[T, E]` + `?` + `match` | **Strong** |
| Go no real sum types | `enum` + exhaustive `match` | **Strong** |
| Go goroutine leaks | **First-class** structured `crew` / `kick` / `join` (jobs cannot outlive crew) | **Strong** |
| Go shared-memory races | Ownership + structured concurrency; deep Send + static race on mut captures until join; TSan opt-in | **Strong** (type-level race model seed; TSan for residual UB) |
| Slow / second-class parallel work | **First-class** `fan` + multi-kick crews ([SPEED.md](SPEED.md)) | **Strong** (keep optimizing) |
| Go weak generics / empty `any` | User generics (monomorphized) + interfaces seed | **Partial → Strong** |
| Go export-by-capital only | Explicit `export` (capital dual only) | **Strong** |
| Rust lifetime ceremony | Local ownership: `hold` / `share` / scopes / arenas — no `'a` in everyday code | **Strong** |
| Rust borrow-checker fights | Simple default `let`; power path is explicit `hold`/`share` | **Strong** (refine NLL) |
| Rust `impl` / trait complexity | `on Type { … }` methods; light interfaces | **Strong** |
| Rust async coloring (`async`/`await`/`Pin`) | **First-class** `crew` / channels / actors — no colored functions | **Strong** |
| Rust compile-time pain | Linear-ish frontend; debug `-O0`; C pipeline | **Partial** (keep watching) |
| Rust error boilerplate | Same `Result`/`?` idea, lighter surface | **Strong** |
| Both: hard to ship “just a binary” | Single native binary; no VM; release `-O3 -flto` | **Strong** |
| Both: stdlib / dep sprawl for backends | Batteries-included std (HTTP, SQL, crypto, …) | **Strong** |
| Both: too much typing / ceremony for everyday work | Local inference, one `print`, `==` on strings, `?`, `match`, opt-in power, full map/slice grid without packages | **Strong** — [ERGONOMICS.md](ERGONOMICS.md) |
| Go map key limits (only comparable builtins; no struct keys without workarounds) | `map[Struct\|Enum\|float\|bool]…`, `map[K][]T`, nested `map[K]map[…]` (depth 2), bag values `map[K]Option[T]` / `map[K]Result[T,E]` | **Strong** |
| Index soup (`while i < len`) across large backends | `for i, v in range s` / `for k, v in range m` / C-style `for` | **Strong** — [ERGONOMICS.md](ERGONOMICS.md) |
| Manual `str_builder` for every log/JSON fragment | `fmt_sprintf` / `fmt_sprintf2`… / `fmt_sprintf_d` (no full `f"…"` interpol yet) | **Strong** (fmt) / **Partial** (interpol residual) |
| Nested `if str_eq(key, …)` config trees | `match key { "…" => …, _ => {} }` · `switch` on ints | **Strong** |
| Bit-packing multi-field worker results into one `int` | POD `struct` on `chan[Struct]` or as kick arg | **Strong** for POD · **Partial** for enum fields on kick |
| Duplicated pipelines for want of callbacks | `fan` lambdas seed; general first-class fn params residual | **Partial** |

**Product rule:** every major language change should close a row above, or be
rejected. “Looks like Go/Rust” is never a sufficient reason.

---

## 1. Go pain → Mako answer

### 1.1 Garbage collector pauses and latency jitter

| Go | Mako |
|----|------|
| GC is always on; hard real-time / p99 latency is a fight | **No mandatory GC.** Scope cleanup, `arena` bulk free, `hold` moves, `share` when needed |

Everyday code stays simple (`let`). Power tools are **visible** (`hold` / `share` / `arena`), not hidden runtime cost.

### 1.2 `nil` and “works until production”

| Go | Mako |
|----|------|
| `nil` maps, slices, interfaces, pointers | **No null.** Use `Option[T]`. Missing cases show up at compile time |

### 1.3 Error handling noise

| Go | Mako |
|----|------|
| `if err != nil { return err }` everywhere | `Result[T, E]`, `?` propagation, exhaustive `match` |
| Easy to ignore errors | **Unused `Result` is a compile error** unless `let _ = …` |

### 1.4 Missing algebraic data types

| Go | Mako |
|----|------|
| `interface{}` / tagged structs by hand | First-class `enum` + `match` |

### 1.5 Concurrency without structure

| Go | Mako |
|----|------|
| Free `go f()`; leaks and lost cancellation are common | **`crew` scopes** — kicked work cannot outlive the crew; `join` is first-class |
| Channels + select are powerful but easy to misuse | Same power with Mako keywords (`select` / `timeout` / `default`) + typed `chan_open[T]` |

### 1.6 Shared mutable state and races

| Go | Mako |
|----|------|
| Race detector optional; ownership is social convention | Ownership keywords + structured concurrency |
| Data-race freedom not in the type system | Deep Send (Option/Result/tuple/deep-POD) + static race diagnostics on mut captures before join; `--race` TSan |

### 1.7 Generics and “just use `any`”

| Go | Mako |
|----|------|
| Generics arrived late; often avoided | User generics monomorphized (`fn id[T](x: T) -> T`) |
| Empty interface as escape hatch | Prefer typed APIs; interfaces for method sets |

### 1.8 Export and module culture

| Go | Mako |
|----|------|
| Capital letter = public (easy to miss) | Preferred: explicit **`export`**. Packs via **`pack` / `pull`** (always qualify) |

### 1.9 Systems / no-GC work is awkward

| Go | Mako |
|----|------|
| cgo and GC make low-level control painful | Native C backend, arenas, ownership — built for engines, proxies, DBs |

---

## 2. Rust pain → Mako answer

### 2.1 Lifetime and borrow ceremony

| Rust | Mako |
|------|------|
| Explicit lifetimes, HRTBs, “fighting the borrow checker” | Everyday: `let` / scopes. When ownership matters: **`hold`** / **`share`** / **`arena`** — short words, no `'a` tax on every API |

Goal: **Rust-class safety where it counts**, without making the common path academic.

### 2.2 Trait / impl / orphan complexity

| Rust | Mako |
|------|------|
| Traits, coherence, orphan rules, derive proc-macros | **`on Type { fn … }`** for methods; light **interfaces**; derive only where it pays (`#[derive(json)]` seed) |

### 2.3 Async coloring and runtime maze

| Rust | Mako |
|------|------|
| `async fn`, `Pin`, executor choice, `Send` bounds on every future | **No async/await coloring.** Structured **`crew`**, channels, **actors** — same program model for sync and concurrent code |

### 2.4 Compile times and mental load

| Rust | Mako |
|------|------|
| Heavy monomorphization + trait solving can crush iteration | Frontend kept simple; debug builds cheap; user generics are opt-in; **map/bag monomorphs are demand-driven** (only shapes used in the unit) so large packs stay O(used maps) |
| Dense syntax | Prefer readable Mako flair (`crew`, `hold`, `pull`) over symbol soup |

### 2.5 “Safe but I need `unsafe` constantly”

| Rust | Mako |
|------|------|
| Safe subset fights some systems patterns | Explicit **`unsafe`** block when needed; arenas and ownership cover many hot paths without it |

### 2.6 Error types and library friction

| Rust | Mako |
|------|------|
| Great `Result`, but ecosystem error types / traits get heavy | `Result` + `?` + stringly/`Err` seed today; **richer error types** still a residual |

### 2.7 Self-referential and graph structures

| Rust | Mako |
|------|------|
| Often needs arenas, `Rc`, or unsafe | **`arena`** is a first-class language tool for request/batch graphs |

---

## 3. What we refuse to re-import

These are common “fixes” that would **recreate** the pain:

| Temptation | Why we reject it as preferred |
|------------|-------------------------------|
| Make Mako look like Go so Gophers feel home | Identity dies; we keep dual sugar only |
| Full lifetime annotations on every reference | Rust’s #1 learning wall |
| Colored `async`/`await` as the main concurrency story | Splits the language in two |
| Mandatory GC “for convenience” | Brings back Go’s latency tax |
| Ignoring errors by default | Production footgun |
| Free fire-and-forget tasks | Goroutine leak class of bugs |

---

## 4. Residuals — pain we still owe users

Tracked also in [STATUS.md](STATUS.md) / [ROADMAP.md](ROADMAP.md). Closing these is how we finish the job.

| # | Pain still open | Direction (Mako-shaped) |
|---|-----------------|-------------------------|
| R1 | Data races / runtime trust across `crew` | **Send seed done** (Copy/string/chan/deep-POD/Option·Result·tuple). Still open: portable timeouts, child error prop, detached lifecycle, fuller race model |
| R2 | Richer errors than stringly `Err` | Typed error enums + context, still `?`-friendly |
| R3 | NLL / ownership edge cases | Stronger checker without new surface ceremony |
| R4 | Package visibility beyond seed | `export` + `visibility = "explicit"` finished |
| R5 | Observability / diagnostics depth | Prom + span JSON seed done; OTLP, profiles, source stacks residual |
| R6 | Identity lint | `mako lint --identity` flags dual forms as style |
| R7 | Compile-time discipline at scale | Keep frontend linear; demand-driven monomorphs done; avoid trait-solver cliffs |
| R8 | Field defaults on `struct` def | **Update done** (`S { ..base, err: 1 }`); explicit field defaults still open |
| R9 | General first-class functions | Named/lambda `fn` values as ordinary params (beyond `fan`) for shared pipelines |
| R10 | True string interpolation | Optional `f"…{x}"` sugar over existing `fmt_sprintf*` |
| R11 | Enum fields on kick-POD + `chan[Enum]` | **Done** for POD enums — `struct_update_test` |

---

## 5. Design test for new features

Before landing syntax or runtime:

1. **Which pain row does this close?** (Go or Rust table above.)
2. **Does preferred syntax still look like Mako?** ([IDENTITY.md](IDENTITY.md))
3. **Does the common path stay simple?** Power is opt-in and visible.
4. **Does it avoid GC / free tasks / ignored errors / async coloring?**

If it fails (1) or (2), it does not ship as preferred.

---

## 6. One-screen pitch

```
Go pain:  GC, nil, err noise, goroutine leaks, no sum types
Rust pain: lifetimes, traits, async coloring, compile friction

Mako:     unique syntax
          hold / share / arena     → safety without lifetime essays
          crew / kick / join       → concurrency without leaks or coloring
          Result / Option / ?      → errors you cannot ignore
          enum / match / switch    → real variants + string/int dispatch
          for … in range           → slices, maps, channels
          fmt_sprintf*             → logs/JSON without builder walls
          chan[Struct] / POD kick  → multi-field worker results (no bit-pack)
          pack / pull / export     → clear units
          no mandatory GC          → predictable performance
```

---

## Related

| Doc | Role |
|-----|------|
| [IDENTITY.md](IDENTITY.md) | Unique surface / flair |
| [VISION.md](VISION.md) | Product north star |
| [COMPAT.md](COMPAT.md) | Dual sugar only |
| [STATUS.md](STATUS.md) | What’s verified Done |
| [ROADMAP.md](ROADMAP.md) | Residuals queue |
| [AGENTS.md](../AGENTS.md) | Contributor/agent rules |
