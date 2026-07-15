# Release Builds

Debug builds are the default during development. For deployment, use release
mode to produce fast, small binaries.

## Debug vs Release

| Profile | Compiler flags | Behavior |
|---------|---------------|----------|
| Debug (default) | `-O0 -g` | Full debug symbols, bounds checks enabled |
| Release | `-O3 -flto -DNDEBUG` | Maximum optimization, checks elided |

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

## Profile-guided optimization (PGO)

Two-pass PGO with the system C compiler:

```bash
# 1) Instrument
MAKO_PGO_GEN=1 mako build --release main.mko -o server
# 2) Train on representative load
./server …   # writes default.profraw / .gcda next to the binary (compiler-dependent)
# 3) Rebuild using profiles (clang: llvm-profdata merge may be needed first)
MAKO_PGO_USE=1 mako build --release main.mko -o server
# Or point at a profile directory / .profdata path:
MAKO_PGO_USE=/path/to/default.profdata mako build --release main.mko -o server
```

Extra C flags for both compile and link:

```bash
MAKO_CFLAGS="-march=native" mako build --release main.mko -o server
```

## Static linking

On supported targets, produce a fully static binary with no runtime
dependencies:

```bash
mako build --release --static-link main.mko -o server
```

This is the default for Linux musl targets. On other platforms, use
`--static-link` explicitly when supported.

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
mako build --target wasm32-wasi main.mko -o app.wasm
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

3. Prefer `hold` over `share` (zero overhead vs reference counting)

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
