# Mako builds (v0.4.13 tip)

**Versioning:** [VERSIONING.md](VERSIONING.md) — ship small patches often.

Mako compiles packages to **cached native objects** under `.mako/cache/` (or `$MAKO_CACHE`), then links. Unchanged units skip `clang -c`.

**Product tip:** **0.4.5**. Generic monomorphs and channel ptr helpers participate
in unit fingerprints like any other generated C.

## Layout

```text
.mako/cache/
  meta.txt                 # COMPILER_CACHE_VERSION
  typecheck/<fp>.ok        # whole-program typecheck stamp
  c/<fp>.c                 # generated C per unit
  obj/<fp>.o               # clang -c output
```

Fingerprints include: compiler cache version, **full source + transitive deps**, generated C bytes, opt/sanitize/target flags, and host feature defines (OpenSSL, etc.).

## CLI

| Flag / env | Meaning |
|------------|---------|
| (default) | Incremental **on** |
| `--no-incremental` | Bypass caches |
| `-j N` / `MAKO_JOBS` | Parallel `clang -c` jobs (default: CPU count) |
| `MAKO_CACHE` | Override cache root |
| `MAKO_CACHE_LOG=1` | Print HIT/MISS lines |

Wired into `mako build`, `mako check`, and `mako run`. When `mako.lock` exists,
locked dependencies are rehashed before dependency loading or cache reuse, and
compilation uses the verified source snapshot rather than reopening those files.

## Parallelism

Independent object units compile in parallel (owned jobs + channels — no shared mutable typechecker). Dependency order for packages is preserved by merging path deps before codegen; object units for one binary are independent.

## Residual clang usage

| Step | Clang? |
|------|--------|
| Unchanged unit | **No** — reuse `.o` |
| Changed unit | `clang -c` only |
| Final binary | `clang`/`ld` link of `.o`s + libs |
| wasm / `--emit-c` / `--sanitize` / cross `--target` | Monolithic `build_c` path |

## Backend policy

Mako has three codegen backends. **There is no silent fallback** between them:
unsupported constructs hard-error on native/LLVM rather than dropping to C.

| Backend | CLI | Role | When to use |
|---------|-----|------|-------------|
| **C** (default) | `--backend c` | Mature Mako → C → system `cc` | Default; sanitizers; cross/`--target`; wasm; widest host support |
| **Native** | `--backend native` | Shared IR → Cranelift object | Fast debug iteration; full language gate (`examples/testing` **367/367**) |
| **LLVM** | `--backend llvm` | Shared IR → LLVM object (release only) | Optimizing release path when built with `--features llvm-backend` + bundled lld |

**Recommended local workflow (0.5 prep):**

```bash
# Debug / test on Cranelift (or set once in the shell)
export MAKO_BACKEND=native
mako build app.mko -o app
mako test examples/testing          # or MAKO_TEST_BACKEND=native

# Release speed (host with llvm-backend feature)
mako build app.mko --release --backend llvm -o app

# Sanitizers / oracle remain on C
mako test examples/testing --backend c --sanitize address
```

| Env | Meaning |
|-----|---------|
| `MAKO_BACKEND` | Default for `build` / `run` / `test` when CLI is still the default `c` |
| `MAKO_TEST_BACKEND` | Test-only override (checked before `MAKO_BACKEND`) |
| Explicit `--backend …` | Always wins over env |

**CI (primary hosts):** both `mako test examples/testing` (C) and
`mako test examples/testing --backend native` are required. **LLVM** runs as a
dedicated **macOS** job (`llvm-backend`) that bootstraps static lld and executes
`scripts/llvm-backend-test.sh`. Non-Darwin hosts skip LLVM today (bundled
`lldMachO` only); set `MAKO_LLVM_SKIP_IF_UNAVAILABLE=1` for a soft skip in
local/optional scripts. Install smoke: `scripts/install-smoke.sh` (`doctor` +
init/run).

**Product default:** remains **c** until 0.5.0 flips the default after policy +
CI confidence; use env/`--backend` today for native-first workflows.

## Native / LLVM backends

```bash
mako build app.mko --backend native -o app
mako run app.mko --backend native
mako build app.mko --release --backend llvm -o app   # needs llvm-backend feature
```

Native and LLVM emit machine-code objects directly (no generated C). The shared
IR covers the full testing corpus on Cranelift. Unsupported modes hard-error with
a pointer at the C backend (**no silent fallback**).

## Modes matrix (0.4.7)

| Mode / flag | `--backend c` | `--backend native` | `--backend llvm` |
|-------------|---------------|--------------------|------------------|
| Host target (default) | yes | yes | yes (`--release` required) |
| `--target` cross | yes (zig/clang) | **hard-error** | **hard-error** |
| `wasm32-wasip1` | yes | **hard-error** | **hard-error** |
| `--sanitize=…` / `--race` | yes | **hard-error** | **hard-error** |
| `--static` | yes (non-macOS) | **hard-error** | **hard-error** |
| `--emit-c` | yes | **hard-error** | **hard-error** |
| `--overflow wrap` | yes | yes | yes |
| `--overflow trap` | yes | yes (shared IR) | yes (shared IR) |
| `--overflow ignore` | yes (≡ wrap) | yes (≡ wrap) | yes (≡ wrap) |
| Debug (`mako build`) | yes | yes | **hard-error** (use native) |
| Release (`--release`) | yes | yes | yes (needs `llvm-backend` feature) |

Example:

```bash
# Wrong — fails closed:
mako build app.mko --backend native --sanitize address
# → error: --sanitize=address is not implemented for --backend native; use `--backend c`

# Right:
mako build app.mko --backend c --sanitize address
```

`mako doctor` prints this matrix for the installed binary (including whether
llvm-backend was compiled in).

## Memory safety of the cache

- **No `unsafe`** in `src/incremental.rs`.
- Cache paths: hex fingerprints only (no `..` traversal).
- Writes: temp file + rename; objects written to `.o.tmp.*` then renamed.
- Typecheck HIT only when the fingerprint of **all** safety-relevant sources (entry + transitive `.mko` + manifests) matches a prior successful check — **NLL/borrow is never skipped on a partial fingerprint**.
- Object keys include full generated C + flags so a stale `.o` cannot be linked after codegen-affecting changes.

See [SECURITY.md](SECURITY.md) and [PERFORMANCE.md](PERFORMANCE.md).
