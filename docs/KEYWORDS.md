# Mako keywords

Source of truth: `src/lexer/mod.rs` → `lex_ident` (**46** reserved words, including duals; `pack`/`pull`/`go`/`switch` are contextual).  
Every identifier that matches one of these strings is always a keyword token — never
an `Ident`. There are **no contextual keywords** today: you cannot name a variable `crew` or `default`.

**Mako flair (preferred):** `fn`, `let`, `pack`, `pull`, `on`, `hold`, `share`, `arena`,
`crew`, `kick`, `join`, `fan`, `export`, … — see [IDENTITY.md](IDENTITY.md).

**Dual / compat:** `func`, `var`, `package`, `import`, `type`, plus `:=` (token, not a keyword).

**Not keywords** (ordinary identifiers / builtins): type names (`int`, `int8`, `int32`, `int64`,
`uint64`, `byte`, `float`/`float64`, `string`, …), `map` (type constructor `map[K]V`, not reserved),
conversions `T(x)` / `bytes(s)`, `Ok`/`Err`/`assert*`/`t_run`, `len`/`cap`/`append`/`copy`/
`rune_count`/`has`/`delete`/`str_builder`/`builder_*`/`uuid_*`, etc. See [GUIDE.md](GUIDE.md).

Guided tour: [The Mako Book](book/) · Current syntax: [GUIDE.md](GUIDE.md) · Design: [LANGUAGE.md](LANGUAGE.md).

---

## Declarations

| Keyword | Meaning |
|---------|---------|
| `fn` / `func` | Function (or interface method) — `fn` preferred; `func` dual |
| `var` | Mutable local (`var x = 1`) — dual of `let mut` |
| `pack` / `package` | Unit name (`pack lib`) — default pull qualifier; `package` dual |
| `type` | Dual type decl: `type Point struct { … }` |
| `struct` | Product type with named fields; generics: `struct Pair[T] { … }` (0.2.1) |
| `enum` | Sum type with variants; generics: `enum Box[T] { … }` (0.2.1) |
| `actor` | Actor type with `receive` arms |
| `receive` | Actor message handler arm |
| `interface` | Named method set (light interfaces) |
| `extern` | Foreign declaration (`extern "C" fn …`) |
| `const` | Compile-time constant binding |
| `pull` / `import` | Bring in another `.mko` / std unit — always qualify; `pull` preferred |
| `let` | Local binding |
| `mut` | Mutable parameter or binding marker |
| `export` | Package-public declaration (`export fn` / `export struct` / `export on`) |
| `on` | Method block: `on Point { fn distance(self) … }` (desugars to `Point_distance`) |

## Control flow

| Keyword | Meaning |
|---------|---------|
| `if` / `else` | Conditional |
| `while` | Loop while condition holds |
| `for` / `in` / `range` | Iteration (`for i, v in range s`, `for i in n`, …) |
| `break` / `continue` | Exit / next iteration of innermost `for`/`while` |
| `fallthrough` | Go dual: last statement of a `switch` `case` arm only |
| `return` | Return from function |
| `defer` | Run on function exit (LIFO), including before `return` |
| `match` | Pattern match on enums / Option / Result / ints |

## Literals / logic

| Keyword | Meaning |
|---------|---------|
| `true` / `false` | Bool literals |
| `and` / `or` / `not` | Boolean operators (word forms; `&&` / `\|\|` / `!` also work) |

## Operators (not keywords)

| Form | Meaning |
|------|---------|
| `=` | Assignment only (never equality) |
| `==` `!=` `<` `>` `<=` `>=` | Comparison |
| `&&` `\|\|` `!` | Logical and / or / not (short-circuit `&&`/`\|\|`); `!!x` is two `!` |
| `and` `or` `not` | Same as `&&` / `\|\|` / `!` |
| `&` `\|` `^` `&^` `<<` `>>` | Bitwise; unary `^x` is bitwise complement |
| Leading `\|…\|` | Still a lambda; infix `\|` is bitwise or |

## Concurrency

| Keyword | Meaning |
|---------|---------|
| `crew` | Structured concurrency scope |
| `kick` | Spawn work on a crew (also `.kick(…)`) |
| `join` | Wait for a kicked job (also `.join()`) |
| `fan` | Data-parallel map over a range/collection |
| `select` | Multi-way channel wait |
| `timeout` | Select arm: wait up to N ms |
| `default` | Select arm: non-blocking fallback |

## Memory / ownership

| Keyword | Meaning |
|---------|---------|
| `arena` | Bump-allocation region (freed on scope exit) |
| `hold` | Move-on-rebind ownership binding |
| `share` | Shared / RC-style binding (seed) |
| `as` | Type / ownership cast helper in expressions |

## Alphabetical (complete)

```
actor and arena as break const continue crew default defer else enum extern false
fallthrough fan fn for hold if import in interface join kick let match mut not or
range receive return select share struct timeout true while
```

(Count must match `lex_ident` keywords; duals `func`/`var`/`package`/`type`/`pull`/`pack` also reserved.)
