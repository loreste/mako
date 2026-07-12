# Agent / contributor north star

## Mako is its own language

**Do not treat Mako as a Go dialect or a Rust dialect.**  
It is a **unique language** with **unique syntax**. That is non-negotiable.

## What the game is

1. **Speed, speed, speed** — runtime as close to **Rust** as possible.  
   No mandatory GC. Native binaries. Release `-O3 -flto`. Hot-path regressions are bugs.  
   See [docs/PERFORMANCE.md](docs/PERFORMANCE.md) · [docs/SPEED.md](docs/SPEED.md).

2. **Concurrency and parallelism are first-class** — not library afterthoughts.  
   Language tools: `crew` / `kick` / `join` / `fan` / channels / `select` / `actor`.  
   Structured (no orphan tasks). No async coloring as the primary model.  
   See [docs/SPEED.md](docs/SPEED.md).

3. **Fix Go/Rust pain** with Mako-shaped answers — never by cloning their syntax.  
   See [docs/PAIN_POINTS.md](docs/PAIN_POINTS.md).

4. **Real work, less typing** — short happy path; power opt-in.  
   See [docs/ERGONOMICS.md](docs/ERGONOMICS.md).

5. **Preferred surface is Mako-only.** Docs, examples, tests, and `mako fmt` lead with it.  
   Dual spellings (`func`, `:=`, `import`, `package`, …) are **compat sugar only**.

### Canonical Mako surface (use these)

| Area | Prefer |
|------|--------|
| Functions | `fn name(x: T) -> R` |
| Locals | `let` / `let mut` |
| Types | `struct` / `enum` / `interface` |
| Methods | `on Type { fn m(self) … }` |
| Units | `pack name` · `pull "path"` · `pull "path" as name` |
| Memory | `hold` · `share` · `arena` |
| Concurrency | `crew` · `kick` · `join` · channels · `select` · `actor` |
| Parallelism | `fan` · multi-`kick` crews |
| Errors | `Result` · `Option` · `?` · `match` |
| Export | `export` |
| Files | `.mko` |

### When adding or changing syntax / runtime

1. Does this look like **Mako** (not Go/Rust wearing a costume)?
2. Does it **hurt speed** on a hot path? If yes, opt-in or reject.
3. Does it make **concurrency/parallelism** clearer, safer, or faster — or weaker?
4. Which **pain point** does it close? ([docs/PAIN_POINTS.md](docs/PAIN_POINTS.md))
5. Does the **happy path get shorter**? ([docs/ERGONOMICS.md](docs/ERGONOMICS.md))
6. Dual forms only for migration; preferred forms stay in [docs/IDENTITY.md](docs/IDENTITY.md).
7. Pitch **what Mako does** (speed, crew, fan) — not “like Go/Rust.”

### Always check your work

Before calling a change done, **re-run and report evidence** (do not claim PASS from intent):

1. **Build** — `cargo build --release`
2. **Tests** — related `mako test examples/testing/…` and negative `examples/bad/…`
3. **Demos** — `mako run` for examples you touched
4. **Speed** — if concurrency/runtime/codegen changed: `./scripts/bench-gate.sh` (and `1.5` when relevant)
5. **Identity** — preferred surface should stay clean under `mako lint --identity` on Mako-native samples

### Source of truth

- [docs/SPEED.md](docs/SPEED.md) — **speed + concurrency + parallelism**  
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md) — measure, profiles, bar vs Rust  
- [docs/IDENTITY.md](docs/IDENTITY.md) — flair + identity  
- [docs/PAIN_POINTS.md](docs/PAIN_POINTS.md) — Go/Rust pain → Mako  
- [docs/ERGONOMICS.md](docs/ERGONOMICS.md) — less typing  
- [docs/COMPAT.md](docs/COMPAT.md) — dual vs preferred  
- [docs/VISION.md](docs/VISION.md) — product north star  
