# Mako compatibility policy (0.x)

Mako aims for **simple everyday code**, **native performance** (a design goal,
not yet broadly benchmarked), and active memory/resource safety mechanisms
without a GC — with a **syntax surface that is Mako’s own**.

**Current product:** **0.3.0**. See [IDENTITY.md](IDENTITY.md) for preferred forms.

## Guarantees (0.x)

1. **Source compatibility:** A program that `mako check`s on 0.1.0 keeps checking
   on later 0.x releases (including **0.3.0**), unless the package opts into a stricter flag.
2. **Dual syntax stays:** Compat spellings remain accepted. Docs and `mako fmt`
   prefer Mako-native forms.
3. **Additive APIs only:** New builtins and syntax never retype existing ones.
4. **Opt-in strictness:** Package flags tighten rules without changing defaults.

| Flag | Default | Effect |
|------|---------|--------|
| `[package] visibility = "explicit"` | open | Only `export` / capital items are package-public (seed) |
| `[package] systems = true` | false | Legacy marker; ownership is always enforced and no GC exists |
| `[profile.release] bounds_checks = "on"` | debug_only | Legacy-compatible setting; safe bounds checks are always retained |

5. **No silent default ownership change:** Bare `let` stays simple. `hold` /
   `share` / `arena` remain the explicit power path.

## Preferred vs dual

| Feature | **Preferred (Mako)** | Dual (compat / familiar) |
|---------|----------------------|---------------------------|
| Functions | `fn f(a: int) -> int` | `func f(a int) int` |
| Params / fields | `a: int` / `x: int` | `a int` / `x int` |
| Methods | `on Point { fn distance(self) … }` | `func (p Point) distance()` |
| Locals | `let` / `let mut` | `:=` / `var` |
| Multi-return | `let a, b = f()` | `a, b := f()` |
| Types | `struct Point { … }` | `type Point struct { … }` |
| Export | `export fn` | Capitalized names |
| Unit name | `pack name` (default pull qualifier) | `package name` |
| Pulls | `pull "path"` · `pull "path" as name` · always qualify | `import …` · `import name "path"` · `_` / `.` |
| Concurrency | `crew` / `kick` / `join` | (no free `go`) |
| Memory | scopes + `hold` / `share` / `arena` | — |

Mako is not a Go dialect and not a Rust dialect. Dual spellings help migration;
docs and `mako fmt` always lead with the Mako column.

## Performance & safety

- **No GC.**
- **Release:** `-O3 -flto`.
- **Safety:** Debug bounds checks by default; release profile can keep them on.
- **`#line`** maps generated C back to `.mko`.

## What will wait for 1.0 (or stay opt-in forever)

- Making bare `let` move-by-default
- Data-race freedom as the only concurrency mode
- Deleting dual spellings
- Renaming or retyping existing builtins
