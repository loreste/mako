# Mako incremental builds

Mako compiles packages to **cached native objects** under `.mako/cache/` (or `$MAKO_CACHE`), then links. Unchanged units skip `clang -c`.

**Product tip:** **0.2.3**. Generic monomorphs and channel ptr helpers participate
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

There is no full LLVM/cranelift backend yet; C remains the IR.

## Memory safety of the cache

- **No `unsafe`** in `src/incremental.rs`.
- Cache paths: hex fingerprints only (no `..` traversal).
- Writes: temp file + rename; objects written to `.o.tmp.*` then renamed.
- Typecheck HIT only when the fingerprint of **all** safety-relevant sources (entry + transitive `.mko` + manifests) matches a prior successful check — **NLL/borrow is never skipped on a partial fingerprint**.
- Object keys include full generated C + flags so a stale `.o` cannot be linked after codegen-affecting changes.

See [SECURITY.md](SECURITY.md) and [PERFORMANCE.md](PERFORMANCE.md).
