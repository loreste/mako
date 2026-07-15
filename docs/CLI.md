# Mako CLI reference

Complete reference for every `mako` command, flag, and workflow.

---

## mako init

Create a new project.

```bash
mako init myapp                    # default: mako.toml + main.mko
mako init myapp --name myapp       # set package name explicitly
mako init mysvc --backend          # HTTP API scaffold with routing
mako init myws --workspace         # multi-package workspace
```

**Default scaffold:**

```
myapp/
  mako.toml     # package manifest
  main.mko      # entry point (uses pack/pull/export)
```

**Backend scaffold** (`--backend`):

```
mysvc/
  mako.toml
  main.mko      # HTTP server with /health + /v1/hello
  README.md
```

**Workspace scaffold** (`--workspace`):

```
myws/
  mako.toml           # [workspace] members = ["lib", "app"]
  lib/
    mako.toml
    lib.mko           # shared library code
  app/
    mako.toml         # depends on lib via path
    main.mko          # uses lib.greet(), lib.add()
```

| Flag | Description |
|------|-------------|
| `[PATH]` | Directory to create (default: `.`) |
| `--name <NAME>` | Package name (default: directory name) |
| `--backend` | Scaffold an HTTP JSON API service |
| `--workspace` | Scaffold a multi-package workspace with `lib` + `app` |

---

## mako build

Compile a `.mko` file to a native binary.

```bash
mako build main.mko                         # debug build (-O0)
mako build --release main.mko               # optimized (-O3 -flto)
mako build main.mko -o myapp                # custom output name
mako build --target wasm32-wasip1 main.mko  # cross-compile to WebAssembly
mako build --target x86_64-unknown-linux-musl main.mko  # static Linux binary
mako build --emit-c main.mko               # show generated C instead of linking
mako build .                                # build all workspace members
mako build -p app                           # build one workspace member
```

| Flag | Description |
|------|-------------|
| `[FILE]` | Source file or directory (default: `.`) |
| `-o, --out <PATH>` | Output binary path |
| `--release` | Optimize with `-O3 -flto` |
| `--emit-c` | Print generated C to stdout |
| `--target <TRIPLE>` | Cross-compile target |
| `--sanitize <TYPE>` | Pass `-fsanitize=` to clang (e.g. `address`, `thread`) |
| `--static-link` | Force static linking |
| `--overflow <MODE>` | Integer overflow: `wrap` (default), `trap` (abort), `ignore` |
| `--bounds <MODE>` | Bounds checks: `default` or `always`. **Release builds force always** |
| `--no-incremental` | Disable build caching |
| `-j, --jobs <N>` | Parallel compile jobs (default: CPU count) |
| `-p, --package <NAME>` | Build one workspace member |
| `--time` | Print compile timings |

**Supported targets:**

| Target | Platform |
|--------|----------|
| `aarch64-apple-darwin` | macOS Apple Silicon |
| `x86_64-apple-darwin` | macOS Intel |
| `x86_64-unknown-linux-gnu` | Linux x86_64 |
| `aarch64-unknown-linux-gnu` | Linux ARM64 |
| `x86_64-unknown-linux-musl` | Linux static (musl) |
| `x86_64-pc-windows-gnu` | Windows x86_64 |
| `wasm32-wasip1` | WebAssembly (WASI) |

---

## mako run

Compile and run.

```bash
mako run main.mko                  # compile and execute
mako run main.mko -- arg1 arg2     # pass arguments to program
mako run --release main.mko        # run optimized build
mako run -p app                    # run workspace member
```

Arguments after `--` are forwarded to the compiled program. Access them with
`argc()` and `arg_get(index)`.

| Flag | Description |
|------|-------------|
| `[FILE]` | Source file or directory (default: `.`) |
| `[ARGS]...` | Arguments forwarded to the program |
| `-p, --package <NAME>` | Workspace member to run |
| `--release` | Optimize before running |
| `--time` | Print compile timings |
| `--no-incremental` | Disable build caching |
| `-j, --jobs <N>` | Parallel compile jobs |
| `--overflow <MODE>` | `wrap` / `trap` / `ignore` (integer `+ - *`) |
| `--bounds always` | Keep bounds checks in release |

---

## mako dev

Hot-reload seed: poll the entry file mtime, rebuild and rerun on change.

```bash
mako dev main.mko
mako dev . -p app --interval-ms 300
mako dev main.mko --overflow trap
```

| Flag | Description |
|------|-------------|
| `[FILE]` | Source or package directory (default: `.`) |
| `-p, --package` | Workspace member |
| `--interval-ms <N>` | Poll interval (default 500) |
| `--release` | Optimized rebuilds |
| `--overflow` | Same as `build`/`run` (default `trap` for dev) |

Ctrl-C stops the watcher.

---

## mako check

Type-check without compiling. Parse recovery reports **all** top-level parse
errors in a file (not only the first).

```bash
mako check main.mko               # check one file
mako check .                       # check all workspace members
mako check -p lib                  # check one member
mako check --json main.mko        # machine-readable diagnostics
```

| Flag | Description |
|------|-------------|
| `[FILE]` | Source file or directory (default: `.`) |
| `-p, --package <NAME>` | Focus one workspace member |
| `--json` | Emit JSON diagnostics |
| `--no-incremental` | Disable typecheck cache |

---

## mako test

Run tests. Test files are `*_test.mko` with functions named `TestXxx`.

```bash
mako test examples/testing              # run all tests in directory
mako test examples/testing -r TestAdd   # filter by name
mako test examples/testing -r "Test*"   # glob pattern
mako test examples/testing -v           # verbose (show matched names)
mako test --coverage                    # print coverage + category counts
mako test --count 3                     # run suite 3 times
mako test --race                        # enable ThreadSanitizer
```

**Test file structure:**

```mko
// add_test.mko
fn add(a: int, b: int) -> int {
    return a + b
}

fn TestAdd() {
    assert_eq(add(2, 3), 5)
    assert_eq(add(0, 0), 0)
}

fn TestAddNegative() {
    assert_eq(add(-1, 1), 0)
}
```

**Test categories** — the runner detects these from function name prefixes:

| Prefix | Category |
|--------|----------|
| `Test` | Unit test |
| `Fuzz` | Fuzz test |
| `Property` | Property-based test |
| `Snapshot` | Snapshot test |
| `Mock` | Mock test |
| `Fixture` | Fixture test |

| Flag | Description |
|------|-------------|
| `[PATH]` | Test file or directory (default: `.`) |
| `-r, --run <FILTER>` | Filter: substring, glob (`*`/`?`), or `/regex/` |
| `-v, --verbose` | List matched test names |
| `--count <N>` | Run suite N times |
| `--coverage` | Print coverage report |
| `--race` | Enable ThreadSanitizer |
| `--sanitize <TYPE>` | Custom sanitizer |
| `-p, --package <NAME>` | Focus one workspace member |

---

## mako fmt

Format `.mko` source files.

```bash
mako fmt main.mko                 # print formatted to stdout
mako fmt -w main.mko              # write formatted back to file
mako fmt -w .                     # format all files in directory
mako fmt -l .                     # list files that need formatting
mako fmt -d .                     # show diffs
mako fmt -w -p lib                # format one workspace member
```

| Flag | Description |
|------|-------------|
| `[PATHS]...` | Files or directories (default: `.`) |
| `-w, --write` | Write result to file (default: stdout) |
| `-l, --list` | List files that differ from canonical format |
| `-d, --diff` | Show diffs |
| `-p, --package <NAME>` | Focus one workspace member |

---

## mako lint

Static analysis plus type-checking.

```bash
mako lint .                        # lint everything
mako lint -p app                   # lint one workspace member
```

---

## mako doc

Generate API documentation.

```bash
mako doc .                         # generate to docs/api/
mako doc --out api-docs .          # custom output directory
```

Produces markdown files with function signatures, types, and runnable example
commands. Also generates a symbol search index.

---

## mako profile

Build and run with timing instrumentation.

```bash
mako profile main.mko             # print compile + run timings
mako profile --release main.mko   # profile optimized build
mako profile --json main.mko      # JSON output for tooling
mako profile -p app               # profile workspace member
```

---

## mako bench

Run benchmark files (`bench_*.mko`).

```bash
mako bench .                       # run all benchmarks
mako bench -p app                  # benchmark one workspace member
```

---

## mako deploy

Generate deployment scaffolds.

```bash
mako deploy docker                 # Dockerfile + .dockerignore
mako deploy serverless             # serverless/container-edge starters
mako deploy wasm                   # browser/edge WASM starter files
mako deploy plugin                 # native or WASM plugin skeleton
```

---

## mako doctor

Verify your installation is healthy.

```bash
mako doctor
```

Checks: compiler binary, runtime headers, stdlib, clang, zig (optional),
VS Code extension.

---

## mako version

```bash
mako version                       # e.g. mako version mako0.1.5 darwin/arm64
mako version -v                    # include git commit hash
mako --version                     # same as mako version
mako -V                            # same
```

---

## mako lsp

Start the language server (stdio JSON-RPC).

```bash
mako lsp
```

Supports: diagnostics, hover, completion, go-to-definition, references,
rename, document symbols, workspace symbols, signature help.

Configure your editor to run `mako lsp` as the language server for `.mko` files.

---

## mako pkg

Package management commands.

### mako pkg init

Same as `mako init` — creates `mako.toml` and `main.mko`.

### mako pkg add

Add a dependency to `mako.toml`.

```bash
mako pkg add helper ../helper              # path dependency
mako pkg add helper path=../helper         # same
mako pkg add path=../helper                # name from directory basename
mako pkg add mylib https://github.com/user/mylib  # git dependency
```

### mako pkg remove

Remove a dependency from `mako.toml`.

```bash
mako pkg remove helper
```

### mako pkg list

Show package name, version, and dependencies.

```bash
mako pkg list
```

### mako pkg fetch

Clone git dependencies into `.mako/deps/`.

```bash
mako pkg fetch
```

Requires `git` and network access. Not run in CI by default.

### mako pkg lock

Resolve dependencies and write `mako.lock` for reproducible builds.

```bash
mako pkg lock
```

### mako pkg install

Resolve all dependencies (SemVer, path, git, local registry), fetch, and
write `mako.lock`. Alias of `lock`.

```bash
mako pkg install
```

### mako pkg update

Re-resolve within SemVer bounds and refresh `mako.lock`.

```bash
mako pkg update
```

### mako pkg publish

Publish to the local registry (`.mako/registry/` or `$MAKO_REGISTRY`).

```bash
mako pkg publish
```

### mako pkg audit

Check `mako.lock` against advisory and license policy files.

```bash
mako pkg audit
```

Uses `mako-cve.toml` for known vulnerabilities and `mako-license.toml` for
license policy. Both are optional.

---

## mako.toml reference

The package manifest.

### Basic project

```toml
name = "myapp"
version = "0.1.0"

[dependencies]
```

### With dependencies

```toml
name = "myapp"
version = "0.1.0"

[dependencies]
helper = { path = "../helper", version = "0.1.0" }
utils = { git = "https://github.com/user/utils", version = "^1.0" }
```

### Workspace root

```toml
name = "myproject"
version = "0.1.0"

[workspace]
members = ["core", "lib", "app"]
```

### Systems crate (strict ownership)

```toml
[package]
name = "my-engine"
version = "0.1.0"
systems = true    # hold/share rules never weakened, GC forbidden
```

### Fields

| Field | Description |
|-------|-------------|
| `name` | Package name |
| `version` | SemVer version string |
| `systems` | If `true`, ownership rules are strict, no GC weakening |

### Profile / safety vs speed

| Flag / toml | Effect |
|-------------|--------|
| `mako build --release` | `-O3 -flto -DNDEBUG` — **elides** bounds checks on the hot path |
| `mako build --release --bounds always` | Keep bounds checks even under `NDEBUG` (safer, slower) |
| `[profile.release] bounds_checks = "on"` | Same as `--bounds always` for that package |
| `[dependencies]` | Map of dependency name → source |
| `[workspace]` | Workspace configuration |
| `members` | List of member directories |

### Dependency sources

| Form | Example |
|------|---------|
| Path | `helper = { path = "../helper" }` |
| Path + version | `helper = { path = "../helper", version = "0.1.0" }` |
| Git | `utils = { git = "https://github.com/user/utils" }` |
| Git + tag | `utils = { git = "...", tag = "v1.0.0" }` |
| Git + branch | `utils = { git = "...", branch = "main" }` |

---

## mako.lock reference

Reproducible dependency pins. Generated by `mako pkg lock`.

```toml
# mako.lock
version = 1

[[package]]
name = "root"
version = "0.1.0"
source = "path"
path = "."
content_hash = "d996e8d7e2550668"

[[package]]
name = "helper"
version = "0.1.0"
source = "path"
path = "../helper"
content_hash = "a1b2c3d4e5f67890"
```

Do not edit manually. Commit to version control for reproducible builds.

---

## Environment variables

| Variable | Description |
|----------|-------------|
| `MAKO_RUNTIME` | Override runtime header path |
| `MAKO_STD` | Override stdlib path |
| `MAKO_CC` | Override C compiler (default: clang) |
| `MAKO_USE_ZIG` | Force zig cc for compilation |
| `MAKO_JOBS` | Default parallel job count |
| `MAKO_REGISTRY` | Local package registry path |
| `MAKO_LIVE_TLS` | Enable live TLS tests |
| `MAKO_LIVE_NGHTTP2` | Enable live HTTP/2 tests |
| `MAKO_LIVE_QUIC` | Enable live QUIC tests |

---

## Incremental builds

Enabled by default. The compiler caches:

- **Typecheck results** — skip re-checking unchanged files
- **Object files** — skip re-compiling unchanged C output
- **C source** — skip re-generating unchanged codegen output

Cache lives in `.mako/cache/`. Clear with `rm -rf .mako/cache/` or use
`--no-incremental` to bypass.

Parallel compilation uses `-j` flag or `MAKO_JOBS` environment variable.
Default is the number of CPU cores.

---

## Current status

The compiler and runtime pass **178 tests** covering:

- `on Type { fn method(self) -> T { ... } }` method blocks
- `pack` / `pull` / `export` module system (pack-qualified types, lits, enums)
- User generics with monomorphization (`fn name[T](x: T) -> T`)
- Tuples `(int, string)` and multi-return `let a, b = f()` (incl. structs)
- Maps: keys `int|string|float`, values `int|string|float|Struct`; `maps_*`
- `make(chan[T], n)` / `chan_open[T]` for int/bool/float/string/struct
- Structural `==` / `!=` on structs and enums
- `switch` / `case` / `default` alongside `match`
- `fan(xs, fn(x) { x * x })` data parallelism
- `go f()` fire-and-forget onto enclosing crew
- Compound assignments (`+=`, `-=`, `++`, `--`)
- If-expressions (`let x = if c { a } else { b }`)
- Polymorphic `print`
- `==` on strings
- Checked arithmetic (`checked_add/sub/mul`, `would_overflow_*`)
- Graceful shutdown (`install_graceful_shutdown`, `shutdown_requested`, drain lifecycle)
- Distributed tracing (`trace_begin/end/id/set/current/log`, `middleware_trace`)
- Leak detection (`leak_mark/check/scope_enter/exit`, `leak_report_json`)
- `crew_drain(timeout_ms)` for draining crew tasks
- `const fn` compile-time evaluation
- Parser multi-error recovery

Init scaffolds (`mako init`) generate projects using the current `pack`/`pull`/
`export` conventions by default.
