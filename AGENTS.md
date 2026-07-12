# Agent / contributor north star

## The name of the game

| Pillar | Bar |
|--------|-----|
| **Speed** | **The name of the game.** Hot path as close to **Rust** as possible. No mandatory GC. Native `-O3 -flto`. Silent slowdowns are bugs. Measure: [docs/PERFORMANCE.md](docs/PERFORMANCE.md) · [docs/SPEED.md](docs/SPEED.md) · `./scripts/bench-gate.sh`. |
| **Concurrency** | **First-class** — language tools, not a package. `crew` / `kick` / `join` / `fan` / channels / `select` / `actor`. Structured (no orphan tasks). No async coloring as the primary model. |
| **Security** | **First-class** — compiler + runtime contract, not a style guide. Memory safety (NLL, `hold`/`share`/`arena`), bounds, secrets wipe, secure stdlib defaults. See [docs/SECURITY.md](docs/SECURITY.md). |

Speed wins only if concurrent work stays safe and security does not force a slow path. Prefer **fast and safe by construction**; costlier checks stay **opt-in** or debug-only.

## Mako is its own language

**Do not treat Mako as a Go dialect or a Rust dialect.**  
It is a **unique language** with **unique syntax**. That is non-negotiable.  
Identity is Mako’s own — **speed, concurrency, and security still ship first-class**.

## What the game is

1. **Speed, speed, speed** — runtime as close to **Rust** as possible.  
   No mandatory GC. Native binaries. Release `-O3 -flto`. Hot-path regressions are bugs.  
   See [docs/PERFORMANCE.md](docs/PERFORMANCE.md) · [docs/SPEED.md](docs/SPEED.md).

2. **Concurrency and parallelism are first-class** — not library afterthoughts.  
   Language tools: `crew` / `kick` / `join` / `fan` / channels / `select` / `actor`.  
   Structured (no orphan tasks). No async coloring as the primary model.  
   See [docs/SPEED.md](docs/SPEED.md).

3. **Security is first-class** — prevent footguns by construction (moves, bounds, crews, secrets, parameterized DB).  
   Do not trade the default hot path for advisory-only safety. See [docs/SECURITY.md](docs/SECURITY.md).

4. **Fix Go/Rust pain** with Mako-shaped answers — never by cloning their syntax.  
   See [docs/PAIN_POINTS.md](docs/PAIN_POINTS.md).

5. **Real work, less typing** — short happy path; power opt-in.  
   See [docs/ERGONOMICS.md](docs/ERGONOMICS.md).

6. **Preferred surface is Mako-only.** Docs, examples, tests, and `mako fmt` lead with it.  
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
4. Does it strengthen **security** (memory, secrets, isolation, secure defaults) without silent cost — or weaken it?
5. Which **pain point** does it close? ([docs/PAIN_POINTS.md](docs/PAIN_POINTS.md))
6. Does the **happy path get shorter**? ([docs/ERGONOMICS.md](docs/ERGONOMICS.md))
7. Dual forms only for migration; preferred forms stay in [docs/IDENTITY.md](docs/IDENTITY.md).
8. Pitch **what Mako does** (speed, crew, fan, safety) — not “like Go/Rust.”

### Always update the docs

**Every behavior or surface change ships with matching docs in the same change.**  
Do not leave documentation for a follow-up. **No “docs later.”**  
A change is incomplete until docs that users read match the new surface.

Update all of these that apply:

| Surface | Where |
|---------|--------|
| Builtins / signatures | [docs/BUILTINS.md](docs/BUILTINS.md) |
| Stdlib overview | [docs/STDLIB.md](docs/STDLIB.md) |
| Guide / book chapters | [docs/GUIDE.md](docs/GUIDE.md) · [docs/book/](docs/book/) |
| Status / roadmap | [docs/STATUS.md](docs/STATUS.md) · [docs/ROADMAP.md](docs/ROADMAP.md) when milestones move |
| Changelog | [CHANGELOG.md](CHANGELOG.md) — user-visible notes |
| CLI / debug / performance | [docs/CLI.md](docs/CLI.md) · [docs/DEBUG.md](docs/DEBUG.md) · [docs/PERFORMANCE.md](docs/PERFORMANCE.md) when flags or gates change |
| Language / identity | [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md) · [docs/IDENTITY.md](docs/IDENTITY.md) when syntax changes |
| Examples | Prefer a small `examples/` or `examples/testing/` sample that matches the docs |

Edge cases, failure modes, and hot-path constraints belong in the docs — not only in tests.
Prefer correcting outdated signatures over leaving “aspirational” tables.

### Always check your work

Before calling a change done, **re-run and report evidence** (do not claim PASS from intent):

1. **Build** — `cargo build --release`
2. **Tests** — related `mako test examples/testing/…` and negative `examples/bad/…`
3. **Demos** — `mako run` for examples you touched
4. **Speed** — if concurrency/runtime/codegen changed: `./scripts/bench-gate.sh` (and `1.5` when relevant)
5. **Concurrency** — crews join/cancel; no orphan tasks; hot-path must not block on SO timeouts
6. **Security** — no new footguns (bounds, secrets, injection); prefer hard errors over soft advice
7. **Identity** — preferred surface should stay clean under `mako lint --identity` on Mako-native samples
8. **Docs** — tables/examples match the new surface (see above)

### Source of truth

- [docs/SPEED.md](docs/SPEED.md) — **speed + concurrency + parallelism**  
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md) — measure, profiles, bar vs Rust  
- [docs/SECURITY.md](docs/SECURITY.md) — **security as a first-class contract**  
- [docs/IDENTITY.md](docs/IDENTITY.md) — flair + identity  
- [docs/PAIN_POINTS.md](docs/PAIN_POINTS.md) — Go/Rust pain → Mako  
- [docs/ERGONOMICS.md](docs/ERGONOMICS.md) — less typing  
- [docs/COMPAT.md](docs/COMPAT.md) — dual vs preferred  
- [docs/VISION.md](docs/VISION.md) — product north star  
