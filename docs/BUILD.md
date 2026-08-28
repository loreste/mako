# Makori builds (v0.6.2)

**Versioning:** [VERSIONING.md](VERSIONING.md) — ship small patches often.

Makori compiles to native binaries via three backends: **native** (Cranelift, default),
**c** (clang/gcc), and **llvm** (optimizing). The native backend on macOS ships with a
bundled linker — no external toolchain required. The C backend uses incremental
**cached objects** under `.mako/cache/` (or `$MAKO_CACHE`).

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
| `-j N` / `MAKO_JOBS` | Parallel compilation jobs (default: CPU count) |
| `MAKO_CACHE` | Override cache root |
| `MAKO_CACHE_LOG=1` | Print HIT/MISS lines |

Wired into `makori build`, `makori check`, and `makori run`. When `mako.lock` exists,
locked dependencies are rehashed before dependency loading or cache reuse, and
compilation uses the verified source snapshot rather than reopening those files.

## Parallelism

Independent object units compile in parallel (owned jobs + channels — no shared mutable typechecker). Dependency order for packages is preserved by merging path deps before codegen; object units for one binary are independent.

## When is a system linker used?

| Backend | macOS | Linux | Windows |
|---------|-------|-------|---------|
| **native** (default) | **Bundled LLD** — no system tools | `gcc`/`clang` for linking | `clang` |
| **c** | `clang` (compile + link) | `gcc`/`clang` | `clang` |
| **llvm** | **Bundled LLD** | `gcc`/`clang` for linking | `clang` |
| wasm / `--emit-c` / unsupported sanitizers / cross | Use explicit `--backend c` + system compiler |

## Backend policy

Makori has three codegen backends. **There is no silent fallback** between them:
unsupported constructs hard-error on native/LLVM rather than dropping to C.

| Backend | CLI | Role | When to use |
|---------|-----|------|-------------|
| **C** | `--backend c` | Mature Mako → C → system `cc` | Explicit oracle; sanitizers; cross/`--target`; wasm; widest host support |
| **Native** | `--backend native` | Shared IR → Cranelift object | Fast debug iteration; full language gate (`examples/testing` **420/420**) |
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
| `MAKO_BACKEND` | Default override for `build` / `run` / `test` when no explicit `--backend` is passed |
| `MAKO_TEST_BACKEND` | Test-only override (checked before `MAKO_BACKEND`) |
| Explicit `--backend …` | Always wins over env |

**CI (primary hosts):** both `makori test examples/testing` (C) and
`makori test examples/testing --backend native` are required. **LLVM** runs as a
dedicated **macOS** job (`llvm-backend`) that bootstraps static lld and executes
`scripts/llvm-backend-test.sh`. Non-Darwin hosts skip LLVM today (bundled
`lldMachO` only); set `MAKO_LLVM_SKIP_IF_UNAVAILABLE=1` for a soft skip in
local/optional scripts. Install smoke: `scripts/install-smoke.sh` (`doctor` +
init/run).

**Product default:** native is the default debug path. Unsupported direct-backend
modes hard-error; choose `--backend c` explicitly when you need C-only modes.

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
| `--sanitize=leak\|address` | yes | yes (see note) | yes (see note) |
| `--sanitize=thread\|memory` / `--race` | yes | **hard-error** | **hard-error** |
| `--static-link` | yes (non-macOS) | **hard-error** | **hard-error** |
| `--emit-c` | yes | **hard-error** | **hard-error** |
| `--overflow wrap` | yes | yes | yes |
| `--overflow trap` | yes | yes (shared IR) | yes (shared IR) |
| `--overflow ignore` | yes (≡ wrap) | yes (≡ wrap) | yes (≡ wrap) |
| Debug (`makori build`) | yes | yes | **hard-error** (use native) |
| Release (`--release`) | yes | yes | yes (needs `llvm-backend` feature) |

Example:

```bash
# Works — leak/address are supported on the direct backends:
mako build app.mko --backend native --sanitize leak

# Wrong — fails closed, because thread/memory need instrumented loads/stores
# that an uninstrumented code generator cannot provide:
mako build app.mko --backend native --sanitize thread
# → error: --sanitize=thread is not implemented for --backend native (only
#   `leak` and `address` are; they rely on allocator interception, while
#   thread needs instrumented loads/stores); use `--backend c`
```

**Note on direct-backend sanitizers.** `leak` and `address` work because
essentially all heap traffic goes through the C runtime, which is compiled from
source at link time and so *is* instrumented, and because both sanitizers also
intercept the allocator process-wide — that covers allocations made from
generated code. What they cannot see is a bad access *inside* Cranelift- or
LLVM-emitted machine code, which carries no redzones or shadow memory. For that,
use `--backend c`. Sanitizer runs must happen on Linux: the runtime deadlocks at
startup on macOS.

`makori doctor` prints this matrix for the installed binary (including whether
llvm-backend was compiled in).

## Memory safety of the cache

- **No `unsafe`** in `src/incremental.rs`.
- Cache paths: hex fingerprints only (no `..` traversal).
- Writes: temp file + rename; objects written to `.o.tmp.*` then renamed.
- Typecheck HIT only when the fingerprint of **all** safety-relevant sources (entry + transitive `.mko` + manifests) matches a prior successful check — **NLL/borrow is never skipped on a partial fingerprint**.
- Object keys include full generated C + flags so a stale `.o` cannot be linked after codegen-affecting changes.

See [SECURITY.md](SECURITY.md) and [PERFORMANCE.md](PERFORMANCE.md).
