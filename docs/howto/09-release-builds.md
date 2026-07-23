# Release Builds

Debug builds are the default during development. For deployment, use release
mode to produce fast, small binaries.

## Debug vs Release

| Profile | Compiler flags | Behavior |
|---------|---------------|----------|
| Debug (default) | `-O0 -g` | Full debug symbols, bounds checks enabled |
| Release | `-O3 -flto -DNDEBUG` | Maximum optimization; safe indexing checks retained |

## Building for release

```bash
mako build --release main.mko -o server
```

The binary is optimized with link-time optimization (LTO) across all
compilation units.

## Stripping symbols

Remove debug symbols for smaller binaries:

```bash
MAKO_STRIP=1 mako build --release main.mko -o server
```

## Parallel compilation

Speed up builds by compiling object files in parallel:

```bash
mako build --release -j 8 main.mko -o server
```

Or set it globally:

```bash
export MAKO_JOBS=8
mako build --release main.mko -o server
```

## Timing the build

See where time is spent:

```bash
mako build --release --time main.mko -o server
```

## Incremental builds

Mako caches compiled object files under `.mako/cache/`. Unchanged packages
reuse their cached `.o` files. This is on by default.

To force a clean build:

```bash
mako build --release --no-incremental main.mko -o server
```

## Link-time optimization (LTO)

Release builds pass **`-O3 -flto`** by default (native clang/gcc path). This is
the product speed path.

Disable LTO when link time matters more than peak speed (or a toolchain is flaky):

```bash
MAKO_NO_LTO=1 mako build --release main.mko -o server
```

The incremental cache fingerprints the release optimization mode and C compiler
identity, so switching between default LTO and `MAKO_NO_LTO=1` cannot reuse
objects from the other mode. `MAKO_CFLAGS` and PGO builds bypass incremental
reuse because external headers and profile contents are not fully represented
by Mako source fingerprints.

## Profile-guided optimization (PGO)

Two-pass PGO with the system C compiler (years-up servers: train on real traffic
shapes — see [LONG_RUNNING.md](../LONG_RUNNING.md)):

```bash
# Recipe script (instrument → train → llvm-profdata merge → rebuild):
./scripts/pgo-build.sh main.mko -o server -- /* train args */

# Manual:
# 1) Instrument
MAKO_PGO_GEN=1 mako build --release main.mko -o server
# 2) Train on representative load
LLVM_PROFILE_FILE=default-%p.profraw ./server …
# 3) Merge (clang) and rebuild
llvm-profdata merge -o default.profdata default-*.profraw
MAKO_PGO_USE=default.profdata mako build --release main.mko -o server
```

## Production allocators (optional)

For multi-month processes, a purpose-built allocator can reduce fragmentation
vs the system `malloc` (measure with `scripts/long-run-soak.sh` /
`scripts/http-long-run-soak.sh`):

```bash
MAKO_ALLOCATOR=mimalloc MAKO_LDFLAGS="-L$(brew --prefix mimalloc 2>/dev/null)/lib" \
  mako build --release main.mko -o server
MAKO_ALLOCATOR=jemalloc mako build --release main.mko -o server
MAKO_ALLOCATOR=/path/to/libmimalloc.a mako build --release main.mko -o server
```

## Extra flags

```bash
MAKO_CFLAGS="-march=native" mako build --release main.mko -o server
MAKO_LDFLAGS="-L/opt/lib -lfoo" mako build --release main.mko -o server
```

## Static linking

On a target with a static-capable toolchain, produce a fully static binary with
no dynamic loader dependency:

```bash
mako build --release --static-link main.mko -o server
```

This is the default for Linux musl targets. On other platforms, use
`--static-link` explicitly when supported.

The repository CI contract currently verifies x86-64 and ARM64 Linux musl
artifacts. Verify a produced artifact before shipping it:

```bash
scripts/verify-target-artifact.sh \
  x86_64-unknown-linux-musl ./server --static
```

Windows GNU is also cross-compiled and checked as a PE32+ x86-64 artifact, but
it is not claimed to be statically linked by Mako's default policy.

To force dynamic linking:

```bash
mako build --release --no-static-link main.mko -o server
```

## Cross-compilation

Build for a different target triple:

```bash
# Linux (static musl)
mako build --release --target x86_64-unknown-linux-musl main.mko -o server

# WebAssembly
mako build --target wasm32-wasip1 main.mko -o app.wasm
```

The target triple follows the pattern: `arch-vendor-os-env`.

## Docker deployment

Generate a multi-stage Dockerfile for containerized deployment:

```bash
mako deploy docker . --entry main.mko --bin server --port 8080
```

This creates:

- `Dockerfile` -- multi-stage build (compile in builder, copy to scratch)
- `.dockerignore` -- excludes build artifacts

Default mode builds a static `x86_64-unknown-linux-musl` binary and copies it
into a `scratch` container (minimal image size).

For applications that need CA certificates or shell access:

```bash
mako deploy docker . --entry main.mko --bin server --port 8080 --mode debian
```

This uses `debian:bookworm-slim` as the runtime image.

## Serverless deployment

Generate provider-specific deployment manifests:

```bash
# Google Cloud Run
mako deploy serverless . --provider cloud-run --name my-api

# Fly.io
mako deploy serverless . --provider fly --name my-api
```

These build on the Docker scaffold and add the appropriate service
configuration files.

## Performance practices

For the fastest runtime performance:

1. Pre-size slices and maps:
```mko
let mut s = make([]int, 0, 1000)      // avoid repeated growth
let mut m = make(map[string]int, 64)  // hint expected size
```

2. Use arenas for request-scoped work:
```mko
arena req {
    let mut buf = make([]byte, 0, 4096)
    // all allocations freed at block end
}
```

3. Prefer `hold` over `share` (no reference-counting traffic)

4. Measure with `now_ns`:
```mko
let t0 = now_ns()
do_work()
let elapsed = now_ns() - t0
print_int(elapsed)
```

## Benchmarking

Run benchmarks to measure performance:

```bash
mako bench .
mako bench . -p app --json    # JSON output for CI
./scripts/bench.sh            # comparative benchmarks
```

Use `black_box(x)` to prevent the optimizer from eliminating benchmark work:

```mko
fn main() {
    let t0 = now_ns()
    let mut sum = 0
    for i in range 1000000 {
        sum = sum + black_box(i)
    }
    let _ = black_box(sum)
    let elapsed = now_ns() - t0
    print_int(elapsed)
}
```

## Build profiles summary

```bash
# Development (fast compile, debug symbols)
mako build main.mko -o app

# Release (optimized, no debug)
mako build --release main.mko -o app

# Release stripped (smallest binary)
MAKO_STRIP=1 mako build --release main.mko -o app

# Release with sanitizer (find bugs in optimized code)
mako build --release --sanitize=address main.mko -o app

# Static release for deployment
mako build --release --static-link --target x86_64-unknown-linux-musl main.mko -o app
```

## Emitting C source

Inspect the generated C code (useful for debugging codegen):

```bash
mako build --emit-c main.mko
# Writes the intermediate .c file alongside the output
```

## Next steps

- [Docker and serverless deployment](../RELEASE.md)
- [Performance tuning](../PERFORMANCE.md)
- [Build system internals](../BUILD.md)
