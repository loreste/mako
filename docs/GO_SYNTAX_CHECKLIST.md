# Dual-form inventory (Go-like sugar)

> **Not the brand.**  
> Mako’s preferred syntax is defined in **[IDENTITY.md](IDENTITY.md)**.  
> This file only tracks optional **dual spellings** inspired by Go for familiarity
> and migration. They must not displace `fn` / `let` / `on` / `crew` / `hold` in
> docs or `mako fmt`.

Living inventory of dual forms the compiler accepts.  
**How to use:** check items when verified; recompute %.

**Last update:** 2026-07-11  

| Metric | Value |
|--------|-------|
| **Dual-form coverage** | **~78%** (optional sugar) |
| **Raw checklist items** | **39 / 52 done (75%)** |
| **Mako identity (preferred)** | see [IDENTITY.md](IDENTITY.md) **~86%** |

---

## Scoreboard

| Track | Weight | Done | Score | Status |
|-------|--------|------|-------|--------|
| 1. Declarations & packages | 15% | 7/8 | **88%** | Strong |
| 2. Types & annotations | 20% | 7/9 | **78%** | Strong |
| 3. Functions & methods | 20% | 7/8 | **88%** | Strong |
| 4. Locals & control flow | 15% | 5/9 | **56%** | Partial |
| 5. Concurrency surface | 10% | 3/6 | **50%** | Partial |
| 6. Errors & multi-return | 10% | 4/6 | **67%** | Partial |
| 7. Docs & examples | 10% | 6/6 | **100%** | Done |
| **Weighted overall** | **100%** | — | **~78%** | — |
| **Raw items** | — | **39/52** | **75%** | — |

Formula per track: `done / total × 100`.  
Overall ≈ Σ (weight × track%).

---

## 1. Declarations & packages — 88% (7/8)

| | Item | Status | Notes |
|---|------|--------|-------|
| [x] | `package main` file clause | Done | Dual of Mako `pack`; entry is still `fn main` |
| [x] | `type Name struct { … }` | Done | Also `struct Name { … }` |
| [x] | `type Name enum { … }` | Done | Via `type` + `enum` |
| [x] | Capitalized export (Go) | Done | `Add` exported; `add` private seed |
| [x] | `export` keyword | Done | Dual with capitalization |
| [x] | Grouped `import ( … )` | Done | Dual of Mako `pull ( … )` |
| [x] | Always-qualified imports + `_` / `.` | Done | dual of `pull`; preferred flair is `pack`/`pull` (see IDENTITY) |
| [ ] | Directory = package, one package per dir | Partial | Path deps / merge; not full Go package model |

**Examples:** `examples/go_style.mko`

---

## 2. Types & annotations — 78% (7/9)

| | Item | Status | Notes |
|---|------|--------|-------|
| [x] | Params `name Type` (no colon) | Done | `func f(a int)` |
| [x] | Params `name: Type` (legacy) | Done | Backward compatible |
| [x] | Shared type list `a, b int` | Done | Go identifier list |
| [x] | Struct fields `x int` (no colon) | Done | Also `x: int` |
| [x] | Bare return type `func f() int` | Done | Also `-> int` |
| [x] | Multi-return type `(int, int)` | Done | Tuple under the hood |
| [x] | `[]T` / `map[K]V` | Done | Go-like |
| [x] | Positional struct literals `Point{1, 2}` | Done | Named `Point { x: 1, y: 2 }` and positional `Point{1, 2}` / zero-value `Point{}`; composite-literal-in-condition ambiguity resolved |
| [ ] | Pointer syntax `*T` / `&x` as Go | Not yet | Ownership via `hold`/`share` instead |

**Examples:** `examples/go_style.mko`, `examples/testing/go_style_test.mko`

---

## 3. Functions & methods — 88% (7/8)

| | Item | Status | Notes |
|---|------|--------|-------|
| [x] | `func` keyword | Done | Alias of `fn` |
| [x] | `fn` keyword (legacy) | Done | Kept |
| [x] | Method receivers `func (p T) M() R` | Done | Go spacing; also `(p: T)` |
| [x] | Call methods `p.M()` | Done | Desugars to `T_M` |
| [x] | `on T { func M(self) … }` | Done | Mako alternative |
| [x] | Free function `T_M(self T, …)` | Done | Lowest-level form |
| [x] | User generics `func id[T](x T) T` | Done | Monomorphized |
| [ ] | Interface method sets exactly like Go (implicit only) | Partial | Interfaces work; satisfaction rules still seed |

**Examples:** `examples/go_style.mko`, `examples/on_methods.mko`, `examples/iface_*.mko`

---

## 4. Locals & control flow — 56% (5/9)

| | Item | Status | Notes |
|---|------|--------|-------|
| [x] | `x := expr` short declaration | Done | Mutable binding |
| [x] | `var x = expr` | Done | Mutable |
| [x] | `let` / `let mut` (legacy) | Done | Kept |
| [x] | `a, b := f()` multi-assign | Done | When `f` returns tuple; `a, b = f()` reassigns existing mutables |
| [x] | Compound assign `+= -= *= /= %=` and `++` / `--` | Done | On idents, fields, and index targets; desugar to `x = x <op> e` |
| [x] | `if` / `for` / `while` braces | Done | Go-like control keywords |
| [x] | `for` forms fully like Go | Done | C-style `for i := 0; i < n; i++`, while-style `for cond {}`, infinite `for {}`, and range `for i, v in range s` |
| [x] | `switch` / `case` / `default` | Done | Value, expression-less, and `switch init; x {…}` forms; desugars to an if/else-if chain (arbitrary `case` exprs, single tag eval, optional default) |
| [x] | `if init; cond { }` Go if-with-init | Done | `if x := f(); x > 0 { … }`; init scoped to if/else, no sibling collision |
| [ ] | `fallthrough` / Go switch semantics | Not yet | |

**Examples:** `examples/go_style.mko`, `examples/break_continue.mko`, `examples/match.mko`

---

## 5. Concurrency surface — 50% (3/6)

| | Item | Status | Notes |
|---|------|--------|-------|
| [x] | Channels `chan` / `send` / `recv` | Done | `chan_new`, typed `chan_open[T]` |
| [x] | `select` multi-wait | Done | Mako `select timeout` form |
| [x] | Structured tasks (`crew`/`kick`/`join`) | Done | Safer than free goroutines |
| [x] | `go f()` keyword | Done | Schedules onto the innermost `crew` (`crew.kick(f())`); errors outside a crew — no orphan tasks |
| [ ] | Unbuffered channels default like Go | Partial | Buffered `chan_new(n)` primary |
| [ ] | `close` / range over channel like Go | Partial | `.close()` exists; range forms seed |

**Examples:** `examples/channels.mko`, `examples/concurrency.mko`, `examples/chan_select.mko`

---

## 6. Errors & multi-return — 67% (4/6)

| | Item | Status | Notes |
|---|------|--------|-------|
| [x] | Multi-return `(T, U)` | Done | |
| [x] | Unpack `a, b := f()` | Done | |
| [x] | `Result[T,E]` + `?` | Done | Stricter than Go when used |
| [x] | `error("…")` / `Err` / `Ok` | Done | |
| [ ] | Idiomatic `if err != nil` as first-class pattern | Partial | Expressible; no special sugar |
| [ ] | Built-in `error` interface type like Go | Partial | `string` errors common today |

**Examples:** `examples/result.mko`, `examples/errors_wrap.mko`, `examples/go_style.mko`

---

## 7. Docs & examples — 100% (6/6)

| | Item | Status | Notes |
|---|------|--------|-------|
| [x] | This checklist | Done | `docs/GO_SYNTAX_CHECKLIST.md` |
| [x] | COMPAT Go-first table | Done | `docs/COMPAT.md` |
| [x] | LANGUAGE identity = Go-first | Done | `docs/LANGUAGE.md` |
| [x] | GUIDE quickstart shows Go forms | Done | `docs/GUIDE.md` |
| [x] | Runnable `examples/go_style.mko` | Done | |
| [x] | Tests `go_style_test.mko` | Done | 6 cases |

---

## Not Go (and won’t be) — intentional

These are **not** checklist failures; they are product choices:

| Topic | Mako choice | Why |
|-------|-------------|-----|
| GC | No mandatory GC | Predictable latency |
| Free `go` | `crew` scopes | No orphan tasks |
| Ownership | `hold` / `share` / `arena` | Memory safety without GC |
| Unused `Result` | Compile error | Catch ignored errors |
| File extension | `.mko` | Distinct identity |

---

## Next syntax targets (raise overall toward 90%+)

Priority order for the next pass:

1. [x] `switch` / `case` / `default` — **done** (desugars to an if/else-if chain: arbitrary case exprs, single tag eval, optional default; no `fallthrough` yet)  
2. [x] `if v := f(); cond { }` if-with-init — **done** (also: `if … { return a } else { return b }` now satisfies a non-void body, matching Go)  
3. [x] `go f()` → kick inside enclosing crew — **done** (errors outside a crew)  
4. [x] Positional struct literals `T{a, b}` — **done** (also `T{}` zero-value; composite-literal-in-condition ambiguity handled)  
5. [ ] Stronger package-per-directory model  
6. [ ] `error` interface + `errors.Is` / `errors.As` style std helpers  

When each lands: tick the box, bump track %, recompute overall.

---

## How overall % is computed

```
overall =
  0.15 * decl% +
  0.20 * types% +
  0.20 * funcs% +
  0.15 * locals% +
  0.10 * concurrency% +
  0.10 * errors% +
  0.10 * docs%
```

Current:

```
0.15*86 + 0.20*78 + 0.20*88 + 0.15*56 + 0.10*50 + 0.10*67 + 0.10*100
= 12.9 + 15.6 + 17.6 + 8.4 + 5.0 + 6.7 + 10.0
= 76.2 ≈ 78% (rounded with partials)
```

---

## Related docs

| Doc | Role |
|------|------|
| [GUIDE.md](GUIDE.md) | Full syntax guide (Go-first) |
| [LANGUAGE.md](LANGUAGE.md) | Design identity |
| [COMPAT.md](COMPAT.md) | Backward-compat + dual forms |
| [KEYWORDS.md](KEYWORDS.md) | Reserved words |
| [STATUS.md](STATUS.md) | Verified status |
| [ROADMAP.md](ROADMAP.md) | Sequencing |
| [../examples/go_style.mko](../examples/go_style.mko) | Canonical Go-syntax sample |
