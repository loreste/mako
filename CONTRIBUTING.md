# Contributing to Mako

Thanks for being interested in helping out. Here's how to get set up and what
we expect from contributions.

## Prerequisites

- **Rust** (stable toolchain)
- **clang** (Xcode on macOS, `apt install clang` on Linux, LLVM on Windows)
- Optional: OpenSSL, libnghttp2, SQLite, libpq (for live integration tests)

## Building from source

```bash
cargo build --release
./target/release/mako --version
```

Or use the Makefile:

```bash
make release       # cargo build --release
make install       # installs to ~/.local/bin + ~/.local/share/mako/runtime
```

## Running tests

```bash
# Full Mako test suite (130 tests)
cargo run --release -- test examples/testing

# Specific test
cargo run --release -- test examples/testing -r TestAdd -v

# Rust-level checks
cargo clippy
cargo fmt --check
```

For live integration tests (TLS, HTTP/2, QUIC), set the relevant env vars:

```bash
MAKO_LIVE_TLS=1 cargo run --release -- test examples/testing
```

## Project structure

```
src/              Compiler (Rust) — lexer, parser, types, codegen, CLI
runtime/          C runtime headers (included by emitted C code)
std/              Standard library (.mko modules)
examples/         Example programs and test suite
  testing/        Test files (*_test.mko)
  bad/            Negative tests (expected compiler errors)
docs/             Documentation
  book/           The Mako Book (mdBook)
  howto/          Task-oriented guides
editors/vscode/   VS Code extension
scripts/          Build, test, and release scripts
```

## Compiler pipeline

```
Source (.mko) → Lexer → Parser → Desugarer → Typechecker (NLL) → Codegen (C) → clang/zig → Binary
```

Key modules:
- `src/lexer/` — tokenization
- `src/parser/` — recursive descent
- `src/types/` — type checking + NLL move analysis
- `src/codegen/` — C code emission
- `src/cc.rs` — C compiler invocation

## Making changes

Process bar (same as [Agents.md](Agents.md)):

1. **Speed is the name of the game** (≈ Rust) — no silent cost on the default hot path.
   Runtime/codegen/concurrency changes: `./scripts/bench-gate.sh` ([docs/SPEED.md](docs/SPEED.md)).
2. **Concurrency first-class** — structured `crew` / `fan` / channels; no orphan tasks; no async coloring as the default model.
3. **Security first-class** — hard errors over advice; secure defaults; costly checks stay opt-in ([docs/SECURITY.md](docs/SECURITY.md)).
4. **Always check your work** — report command + outcome; do not claim green from intent.
5. **Be adversarial** — empty/null, overflow, timeout, double-close, races, injection edges.
6. **Always test** — automated coverage that would fail on regression.
7. **Build consensus** — **speed first**, then concurrency/security, then identity; one path only.

Steps:

1. Create a branch from `main`
2. Make your changes
3. Add or update tests if behavior changes
4. **Update docs in the same change** — no feature without docs. Touch every
   applicable surface (BUILTINS, STDLIB, GUIDE/book, CLI/DEBUG/PERFORMANCE,
   CHANGELOG, STATUS/ROADMAP). See [Agents.md](Agents.md) *Always update the docs*
5. Run `cargo clippy` and `cargo fmt`
6. Run `cargo run --release -- test examples/testing` and make sure it passes
7. Open a PR with a clear description of what and why

### Do not commit

| Keep out of commits | Why |
|---------------------|-----|
| Accidental binaries (`trap_ov`, root `hello`, `/out/…`) | Build artifacts — listed in `.gitignore` |
| `.claude/`, `.cursor/`, `.grok/`, `*.local.md` | Local agent/editor state |
| Secrets, certs, personal env files | Security |
| Unrelated local edits to `AGENTS.md` process notes | Prefer human-facing policy in this file / `docs/`; only change `AGENTS.md` when the **project** north star intentionally changes |

Never `git add -A` / `git add .` blindly — stage only source, tests, docs, and intentional config for the change.

## Style

- Keep it simple. Don't over-abstract.
- Match the existing code style in whatever file you're editing.
- Runtime C code follows the existing naming: `mako_` prefix, snake_case.
- Mako source files use `.mko` extension.

## Adding to the standard library

Standard library modules live in `std/`. Each module has a corresponding runtime
implementation in `runtime/mako_*.h`. If you're adding a new stdlib module:

1. Create `std/<package>/<module>.mko` with the public API
2. Implement the runtime in `runtime/mako_<name>.h`
3. Wire it into codegen (see existing patterns in `src/codegen/mod.rs`)
4. Add tests in `examples/testing/`
5. Document in `docs/STDLIB.md`, `docs/BUILTINS.md`, and the book/guide as needed

## Reporting bugs

Open an issue on GitHub with:
- What you expected to happen
- What actually happened
- Minimal `.mko` file that reproduces the problem
- Output of `mako --version`

## License

By contributing, you agree that your contributions will be licensed under the
MIT License.
