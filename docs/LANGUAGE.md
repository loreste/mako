# Mako language

Mako is a systems and backend language: clear to write, strict at compile time,
fast at runtime, and designed so **builds stay fast**.

**Product version:** **0.1.7** (`mako version` → `mako0.1.7`).

**Guided tour:** [The Mako Book](book/).  
**Full syntax guide:** [GUIDE.md](GUIDE.md).  
**Low ceremony:** [ERGONOMICS.md](ERGONOMICS.md).  
**Identity (our syntax):** [IDENTITY.md](IDENTITY.md).  
**Keywords:** [KEYWORDS.md](KEYWORDS.md).  
**Product north star:** [VISION.md](VISION.md).  
**Honest matrix:** [STATUS.md](STATUS.md).  
**Changelog:** [../CHANGELOG.md](../CHANGELOG.md).

---

## Design pillars

| Pillar | How |
|--------|-----|
| Clear | Concise keywords, braces, local inference |
| Strict | Static types, no null, exhaustive `match`, `Result` / `Option` |
| Fast binaries | Native code via C (today) |
| Fast builds | Linear frontend; debug `-O0` by default |
| **Speed** | As close to Rust as possible — no GC, native `-O3 -flto` ([SPEED.md](SPEED.md)) |
| **Concurrent (first-class)** | `crew` / `kick` / `join` / channels / `select` / `actor` — structured, no orphans |
| **Parallel (first-class)** | `fan` — data-parallel map over cores; multi-kick crews |
| Memory | `hold` / `share` / `arena` — **no mandatory GC** |
| Safe by default | Bounds checks; unused `Result` is an error |

---

## Syntax identity — **ours**

**Mako is a unique language with unique syntax.**  
Not a Go dialect. Not a Rust dialect. Not a hybrid costume of either.

- **Simplicity** (as a *goal*): short programs, little ceremony, good stdlib.
- **Control** (as a *goal*): ownership, no GC, explicit errors, fast binaries.
- **Surface** (as a *requirement*): keywords and forms that are distinctly Mako —
  `fn`, `on`, `pack`, `pull`, `hold`, `share`, `arena`, `crew`, `kick`, …

```mko
// Mako — preferred
fn handle(req: Request) -> Result[int, string] {
    hold let body = req.body
    arena a {
        let msg = arena_text(a, body)
        crew t {
            let j = t.kick(process(msg))
            return Ok(j.join())
        }
    }
}

on Point {
    fn distance(self) -> int {
        return self.x + self.y
    }
}
```

| Mako-native (preferred) | Dual / compat sugar |
|-------------------------|---------------------|
| `fn` | `func` |
| `let` / `let mut` | `:=` / `var` |
| `x: int` | `x int` |
| `-> int` | bare `int` after `)` |
| `on Point { … }` | `func (p Point) …` |
| `struct Point` | `type Point struct` |
| `export` | Capitalized names |
| `crew` / `kick` / `join` | (no free `go`) |
| `hold` / `share` / `arena` | — |

**Rule:** docs, book, and `mako fmt` lead with the left column.  
Dual forms stay for familiarity and migration — they do not define the brand.

Full identity checklist + %: [IDENTITY.md](IDENTITY.md) (**~86%**).  
Optional dual-form inventory: [GO_SYNTAX_CHECKLIST.md](GO_SYNTAX_CHECKLIST.md).

---

## Operators

`=` is assignment only. Comparisons: `==` `!=` `<` `>` `<=` `>=`.  
Logical: `&&` `||` `!` (and `and` / `or` / `not`).  
Bitwise: `&` `|` `^` `&^` `<<` `>>`, unary `^`.  
`==` / `!=` work on strings (by content), named structs (field-wise), and
enums (tag + payload).

## Strings

Byte strings with length. Prefer region ops when you only need to compare or
search a span — no substring allocation:

| Builtin | Role |
|---------|------|
| `str_eq` / `str_contains` / `str_index` | whole-string |
| `str_slice_eq` / `str_slice_ci_eq` / `str_slice_contains` / `str_slice_index` | `s[off:off+len]` without alloc |
| `str_at_eq` / `str_byte_at` | prefix-at-offset · single byte |

See [BUILTINS.md](BUILTINS.md) § Strings and `examples/testing/str_slice_zc_test.mko`.

## Comptime (`const` / `const fn`)

Integer `const` bindings and `const fn` bodies fold at compile time:

```mko
const fn clamp(x: int, lo: int, hi: int) -> int {
    if x < lo { return lo } else {
        if x > hi { return hi } else { return x }
    }
}
const N = clamp(50, 0, 10)  // 10
```

Supported in const: `+ - * / %` bitwise, comparisons, `&&` `||` `!`, `let` /
`assign`, `return`, `if`/`else`, `if`-expressions, **`match` on ints** (`|`, `_`,
bind), **bounded `while` / `for`**, **C-style `for`**, bare **`break` /
`continue`**, **string seeds** (`const S = "…"`, `+`, `str_len` / `==`), and
**`const fn` string params/returns** (plus int fns that use string locals).
Not a full CTFE interpreter.

## Actors

```mko
actor Counter {
    n: int = 0
    receive Inc { self.n = self.n + 1 }           // tag only
    receive Add(delta) { self.n = self.n + delta } // int payload seed
    receive Bye { let _ = 0 }
}
// Counter_spawn() · Counter_send(m, Counter_Add(5)) · Counter_loop(m)
```

Desugars to mailbox helpers; messages pack **tag + optional int payload**
(`actor_pack` / `actor_msg_tag` / `actor_msg_payload`). `Bye`/`Stop` stops the
loop. Tests: `actor_test.mko`.

## Interfaces

```mko
interface Adder { fn add(int) -> int }
on Counter : Adder {
    fn add(self, delta: int) -> int { return self.n + delta }
}
fn use(a: Adder, d: int) -> int { return a.add(d) }
```

Dyn values use a fat pointer + vtable. Also free-fn form `Adder_Counter_add`.
Tests: `iface_*_test.mko`, `iface_on_iface_test.mko`.

## Collections (maps & slices)

One monomorphized surface — no special collection package for everyday work.
Codegen emits **only the map monomorphs used in the unit** (demand-driven), so
large packs stay O(used shapes), not O(types²).

| Form | Notes |
|------|--------|
| `[]T` | int/string/float/bool/byte/Struct/Enum; nested `[][]T`; bag slices `[]Option` / `[]Result` |
| `map[K]V` | **K:** int\|string\|float\|bool\|Struct\|Enum · **V:** same, `[]T`/`[][]T`, nested maps (depth ≤3), bags `Option`/`Result` (incl. nests), tuples `(T,U[,…])` (incl. bag/chan fields), `chan[T]` |
| Ops | `m[k]`, `m[k]=v`, `has`, `delete`, `len`, comma-ok, `range`, `maps_*` |

Short patterns (sets, groups, nested maps, bag values): [ERGONOMICS.md](ERGONOMICS.md).  
Hands-on: [howto/10-collections.md](howto/10-collections.md) · book [ch03](book/src/ch03-language-tour.md).  
Full guide: [GUIDE.md](GUIDE.md) §4b–4c · builtins: [BUILTINS.md](BUILTINS.md).

## Packs & pulls

Always pack-qualified (clear call sites). Default name from optional
`pack` clause or path basename. Prefer `as` for aliases.

```mko
pull "strings"
pull "./lib.mko"                // → lib.* if pack lib / path lib
pull "./util.mko" as helper     // alias
pull _ "fmt"                    // blank: load only
pull . "./helpers.mko"          // specialized: bare names
pull (
    "path"
    "fmt"
)
// dual: import / package still parse

// Types are pack-qualified too (same surface as calls):
//   fn use(t: eng.Table) -> eng.Table
//   let t = eng.Table { n: 0 }
//   match t { eng.Table { n } => … }
// Enums: eng.Red / eng.Green(n) / eng.Color.Red / eng.Color.Green(n)
// Multi-return of pack structs: let t, n = eng.grow_pair(t0, 1)
// Maps: keys int|string|float|bool|Struct|Enum
//   values: same | []T | map[K2]V (depth 2) — any combo
//   e.g. map[string][]int, map[Point]int, map[Color][]string,
//        map[string]map[string]int, map[string]bool
// Nested slices: [][]T (make / append / index / range)
```

## Concurrency & parallelism (first-class)

Speed is the game; concurrent and parallel work is **in the language**, not a package:

```mko
// Concurrent — structured crew
crew t {
    let j = t.kick(work())
    print(j.join())
}

// Parallel — fan across cores
let out = fan items, fn(x) { heavy(x) }
```

No orphan background work: kicked jobs finish with the crew.  
No async coloring. Details: [SPEED.md](SPEED.md).

## Memory

| Tool | Role |
|------|------|
| Scope / `let` | Default |
| `arena` | Request/batch bulk free |
| `hold` | Unique / move |
| `share` | Shared read (RC seed) |

## Errors

No null. Use `Option[T]` and `Result[T, E]`. Propagate with `?`.  
Discarding a `Result` is a compile error unless `let _ = …`.

## Related

[GUIDE.md](GUIDE.md) · [IDENTITY.md](IDENTITY.md) · [COMPAT.md](COMPAT.md) · [SECURITY.md](SECURITY.md) · [VISION.md](VISION.md)
