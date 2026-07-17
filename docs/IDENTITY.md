# Mako syntax identity

**Product tip:** **0.1.9**. Preferred surface: [LANGUAGE.md](LANGUAGE.md) · [GUIDE.md](GUIDE.md).

**Mako is its own unique language with its own unique syntax.**

Not a Go clone. Not a Rust clone. Not “Go with ownership” or “Rust with simpler
keywords.” It may take *ideas* from other languages; the **surface is Mako**.

Judging a syntax choice:

1. Does it look and feel like **Mako**? (If it looks like Go or Rust first, reject it as preferred.)
2. Does it help people **build real work without a lot of typing**? ([ERGONOMICS.md](ERGONOMICS.md))
3. Is safety visible where it matters — without taxing the common path?
4. Does it stay **as close to Rust-class performance as possible**? ([PERFORMANCE.md](PERFORMANCE.md), [SPEED.md](SPEED.md))
5. If it touches concurrent/parallel work, is that still **first-class, structured, and fast**?

If (1), (2), (4), or (5) fails, the form is wrong even if it is familiar.

---

## Mako flair

Short, punchy English. Verbs for action. One clear metaphor per domain.
Not a Go dialect. Not a Rust dialect.

| Domain | Our words | Feel |
|--------|-----------|------|
| **Speed** | native · no GC · `-O3 -flto` | The name of the game ([SPEED.md](SPEED.md)) |
| Memory | `hold` · `share` · `arena` | You own it, share it, or bulk-free it |
| **Concurrency** | `crew` · `kick` · `join` · channels · `select` · `actor` | **First-class** — structured, no orphans |
| **Parallelism** | `fan` · multi-`kick` crews | **First-class** — use the cores |
| Methods | `on Type { … }` | Behavior sits *on* the type |
| Units | `pack` · `pull` · `export` | Name a pack, pull it in, export what leaves |
| Errors | `Result` · `Option` · `?` · `match` | Explicit, exhaustive |
| Files | `.mko` | Ours |

Dual spellings (`func`, `:=`, `import`, `package`, …) exist for migration.
They are **compat sugar**, not the brand. Docs and `mako fmt` always lead with the flair column.

---

## Canonical Mako surface (preferred in docs & `mako fmt`)

| Area | Mako form |
|------|-----------|
| Functions | `fn name(x: T) -> R` |
| Locals | `let` / `let mut` |
| Types | `struct` / `enum` / `interface` |
| Methods | `on Point { fn distance(self) -> int { … } }` |
| Export | `export fn` / `export struct` |
| Units | `pack lib` · `pull "path"` · `pull "path" as name` · always qualify |
| Memory | `hold` · `share` · `arena` |
| Concurrency | `crew` · `kick` · `join` · `fan` · `select` |
| Errors | `Result[T, E]` · `Option[T]` · `?` · `match` |
| Actors | `actor` · `receive` |
| Unsafe | `unsafe { … }` |
| Files | `.mko` |

### Idiomatic sample (low ceremony)

```mko
pull "strings"

export struct Point {
    x: int
    y: int
}

on Point {
    fn distance(self) -> int {
        return self.x + self.y
    }
}

fn divmod(a: int, b: int) -> (int, int) {
    return (a / b, a % b)
}

fn main() {
    let p = Point { x: 3, y: 4 }
    print(p.distance())

    let q, r = divmod(17, 5)
    print(q)
    print(r)

    if path == "/health" {
        print("ok")
    }

    hold let msg = "owned"
    arena a {
        let buf = arena_text(a, msg)
        print(buf)
    }

    crew t {
        let job = t.kick(work())
        print(job.join())
    }
}
```

Runnable: [`examples/mako_style.mko`](../examples/mako_style.mko) · [ERGONOMICS.md](ERGONOMICS.md)
(includes maps/slices short path: sets, groups, nested maps, bag values)

---

## What we borrow (ideas, not identity)

| Inspiration | What we take | What we do **not** copy as identity |
|-------------|--------------|-------------------------------------|
| **Go** | Clear packages, qualified call sites, channels, multi-value returns, fast builds | `func` / `:=` / `package` / `import` as the *preferred* surface |
| **Rust** | Ownership discipline, `Result`/`Option`, exhaustive match, no GC by default | Lifetime ceremony, trait maze, `impl` as primary methods |

| Dual (compat) | Mako flair (preferred) |
|---------------|------------------------|
| `func` | `fn` |
| `:=` / `var` | `let` / `let mut` |
| `a int` | `a: int` |
| `func (p T) M()` | `on T { fn M(self) … }` |
| `type T struct` | `struct T` |
| Capital-only export | `export` |
| `package name` | `pack name` |
| `import …` | `pull …` |
| `import lib "path"` | `pull "path" as lib` |

**Unit rule:** normal pulls always qualify (`pkg.fn(...)`). Clear namespaces —
our spelling is `pack` / `pull`.

---

## Identity checklist (Mako-owned surface)

**Last update:** 2026-07-15  
**Mako identity strength:** **~100%**

| Track | Weight | Done | % |
|-------|--------|------|---|
| 1. Core declaration forms (`fn`, `struct`, `let`) | 20% | 5/5 | **100%** |
| 2. Distinct keywords (`hold`/`share`/`arena`/`crew`/`on`/`pack`/`pull`) | 25% | 10/10 | **100%** |
| 3. Errors & match | 15% | 5/5 | **100%** |
| 4. Docs lead with Mako forms (not clones) | 20% | 5/5 | **100%** |
| 5. Dual sugar stays dual (not preferred) | 10% | 3/3 | **100%** |
| 6. `mako fmt` emits Mako-native spellings | 10% | 2/2 | **100%** |
| **Weighted** | **100%** | — | **~100%** |

### Detail

#### 1. Core declarations — 100%

- [x] `fn` as primary function keyword in docs
- [x] `struct` / `enum` / `interface`
- [x] `let` / `let mut`
- [x] `x: Type` annotations
- [x] `->` return types

#### 2. Distinct keywords — 100%

- [x] `hold` / `share` / `arena`
- [x] `crew` / `kick` / `join` / `fan`
- [x] `on Type { … }` methods
- [x] `export`
- [x] `pack` / `pull` (units — dual `package` / `import`)
- [x] `actor` / `receive`
- [x] `match` / `Result` / `?`
- [x] `unsafe`
- [x] `.mko` extension
- [x] Stable “Mako-only” lint pass (`mako lint --identity`) that flags dual forms as style, not errors

#### 3. Errors & match — 100%

- [x] `Result[T, E]` / `Option[T]`
- [x] unused `Result` is illegal
- [x] `?` propagation
- [x] exhaustive `match`
- [x] Richer errors seed — `Result[T, Enum]` · wrap chain · `error_unwrap` / `error_root` / `error_as_tag` / `error_has_tag` · `std/errors`

#### 4. Docs lead with Mako — 100%

- [x] This identity doc + flair table
- [x] LANGUAGE / GUIDE / COMPAT re-centered
- [x] Canonical `examples/mako_style.mko`
- [x] Dual forms documented as dual
- [x] Units presented as `pack` / `pull`, not a Go dialect surface

#### 5. Dual stays dual — 100%

- [x] Familiar dual forms still parse (compat)
- [x] COMPAT table lists dual vs preferred
- [x] No plan to delete dual in 0.x

#### 6. Formatter — 100%

- [x] Prefer `fn` / Mako spellings in `mako fmt`
- [x] Prefer `pack` / `pull "path" as name`

---

## Related

| Doc | Role |
|-----|------|
| [SPEED.md](SPEED.md) | **Speed + first-class concurrency/parallelism** |
| [PAIN_POINTS.md](PAIN_POINTS.md) | **Why we exist** — Go/Rust pain → Mako answers |
| [ERGONOMICS.md](ERGONOMICS.md) | **Less typing** — happy path short, power opt-in |
| [LANGUAGE.md](LANGUAGE.md) | Design overview |
| [COMPAT.md](COMPAT.md) | Compatibility + dual forms |
| [GUIDE.md](GUIDE.md) | Full syntax guide |
| [GO_SYNTAX_CHECKLIST.md](GO_SYNTAX_CHECKLIST.md) | Optional dual/interop checklist (**not** identity) |
| [KEYWORDS.md](KEYWORDS.md) | Reserved words |
| [VISION.md](VISION.md) | Product north star |
