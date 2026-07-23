# 13. Tooling

Mako ships as a single binary that includes the compiler, test runner, formatter,
linter, package manager, profiler, documentation generator, and language server.
This chapter is a reference for the current subcommands; platform-specific and
optional integration limits are called out where they apply.

---

## `mako version`

Prints the installed version, operating system, and architecture.

```bash
mako version
# mako version mako0.4.0 darwin/arm64

mako --version        # same output
mako -V               # same output

mako version -v       # verbose: includes git commit hash if available
# mako version mako0.4.0 darwin/arm64
# commit: a1b2c3d
```

---

## `mako check`

Runs the full frontend pipeline -- lexing, parsing, and type checking -- without
producing a binary. This is the fastest way to verify correctness.

```bash
mako check main.mko                 # check a single file
mako check .                        # check all workspace members
mako check -p mylib                 # check one workspace member
mako check --json main.mko          # legacy JSON diagnostics array
mako check --json=v1 main.mko       # versioned report for new integrations
```

The checker validates:
- Syntax (matching braces, correct keyword usage)
- Type correctness (argument types, return types, no mixed integer kinds)
- Ownership (`hold` moves, `share` borrow rules, NLL analysis)
- Exhaustive `match` on enums, `Option`, and `Result`
- Unused `Result` as a statement (must use `?`, `match`, or `let _ = ...`)
- Call arity (wrong number of arguments)
- Interface method implementations

### JSON Output

Bare `--json` preserves the original JSON array for existing CI and editor
integrations. Each checked target has its own result and diagnostics array:

```json
[{"ok":false,"file":"main.mko","diagnostics":[{"severity":"error","file":"main.mko","line":12,"column":5,"message":"use of moved value `x`"}]}]
```

New integrations should use `--json=v1`. It wraps the target reports in a
versioned envelope and adds aggregate counts:

```json
{"schemaVersion":1,"command":"check","ok":false,"targets":[{"file":"main.mko","ok":false,"symbols":null,"diagnostics":[{"severity":"error","file":"main.mko","line":12,"column":5,"message":"use of moved value `x`"}]}],"summary":{"checked":1,"passed":0,"failed":1,"diagnostics":1},"errors":[]}
```

Successful targets report their top-level `symbols` count. Failed targets use
`null` because symbol collection did not complete. Resolution failures such as
a missing path use the top-level `errors` array and leave `targets` empty.
Incompatible changes require a new schema version; consumers should ignore
unknown fields added to v1. Both formats exit non-zero when any target fails.

---

## `mako build`

Compiles a `.mko` file to a native binary. The pipeline is:
Mako source -> C code -> object files -> linked executable.

```bash
mako build main.mko                 # debug binary (same name as source, minus .mko)
mako build main.mko -o bin/app      # specify output path
mako build --release main.mko       # optimized: -O3 -flto
mako build -j 8 main.mko           # 8 parallel clang invocations
mako build --no-incremental main.mko  # skip object cache
mako build --time main.mko          # print timing breakdown
mako build --emit-c main.mko        # also write the generated .c file
mako build --target wasm32-wasip1 main.mko -o out.wasm  # cross-compile
mako build --sanitize=address main.mko  # AddressSanitizer instrumentation
mako build --sanitize=thread main.mko   # ThreadSanitizer instrumentation
mako build --static-link main.mko   # force static linking
mako build .                        # build all workspace members with main.mko
mako build -p app                   # build one workspace member
```

### Build Flags Reference

| Flag                 | Effect                                              |
|----------------------|-----------------------------------------------------|
| `-o PATH`            | Output binary path                                  |
| `--release`          | Enable `-O3 -flto` optimization                    |
| `-j N`               | Parallel object compilation (also `MAKO_JOBS`)      |
| `--no-incremental`   | Disable `.mako/cache/` object reuse                 |
| `--time`             | Print frontend/backend/link durations               |
| `--emit-c`           | Write generated C alongside the binary              |
| `--target TRIPLE`    | Cross-compilation target                            |
| `--sanitize=MODE`    | `address` or `thread` sanitizer instrumentation     |
| `--static-link`      | Force static linking                                |
| `--no-static-link`   | Force dynamic linking (override musl default)       |
| `-p NAME`            | Target a specific workspace member                  |

---

## `mako run`

Compiles and immediately runs the program. Equivalent to `mako build` followed
by executing the binary.

```bash
mako run main.mko                   # compile and run
mako run main.mko -- arg1 arg2      # pass arguments to the program
mako run -p app                     # run a workspace member
mako run .                          # run the workspace member with main.mko
```

Arguments after `--` are forwarded to the compiled program and accessible via
`argc()`, `arg_get(i)`, and `args()`.

```mko
fn main() {
    print_int(argc())       // number of arguments
    let a = args()          // []string of all arguments
    for i, v in range a {
        print(v)
    }
}
```

```bash
mako run cli.mko -- hello world
# 3
# out/cli
# hello
# world
```

---

## `mako test`

Discovers and runs test functions. Test files end in `_test.mko`. Test functions
start with `Test` (e.g., `fn TestAdd()`).

```bash
mako test examples/testing           # run all tests in a directory
mako test examples/testing/add_test.mko  # run tests in one file
mako test .                          # run tests in all workspace members
mako test -p mylib                   # run tests for one member
mako test path --json               # stable machine-readable report
```

### Filtering with `-r` / `--run`

```bash
mako test path -r TestAdd            # substring match
mako test path -r 'TestAdd*'         # glob pattern (* and ?)
mako test path -r '/^TestAdd$/'      # regex (anchored)
mako test path -r '/Add|Mul/'        # regex alternation
```

Matching rules (in priority order):
1. If wrapped in `/…/`, treated as a regex. Invalid regex matches nothing.
2. If contains `*` or `?`, treated as a glob.
3. Otherwise, substring match.

### Verbose Mode

```bash
mako test path -v                    # lists matched test functions before running
mako test path -v -r 'TestAdd*'     # verbose + filter
```

Output:

```text
run: TestAdd, TestAddTable
--- PASS: TestAdd (0ms)
--- PASS: TestAddTable (1ms)
ok   2 passed, 0 failed
```

### Repeat

```bash
mako test path --count 10            # run matching tests 10 times
```

Useful for catching flaky tests that depend on timing or concurrency.
Repeats stop at the first failing iteration.

### JSON Reports

`mako test --json` emits one object with `schemaVersion: 1`. It records each
iteration and test file, matched function names, duration, captured stdout and
stderr, and structured exit, signal, timeout, compile, runner, or load failures.
Coverage data is included when `--coverage` is also present. Test-process output
is captured rather than mixed into the JSON stream and is capped at 1 MiB per
stream with explicit truncation flags. The command exits nonzero if a run fails.
Summary pass/fail/skip values count files, while `tests` counts matched test
functions. Signal numbers are reported only on POSIX systems.

### Coverage

```bash
mako test path --coverage            # instrument and report line coverage
```

Reports which lines were executed during the test run.

### Test Categories

Mako recognizes test function prefixes as categories:

| Prefix       | Category    | Purpose                                    |
|--------------|-------------|--------------------------------------------|
| `Test`       | Unit        | Standard correctness tests                 |
| `Property`   | Property    | Property-based / generative tests          |
| `Fuzz`       | Fuzz        | Fuzz tests with random input               |
| `Snapshot`   | Snapshot    | Output comparison against saved snapshots  |
| `Fixture`    | Fixture     | Tests with setup/teardown data files       |
| `Mock`       | Mock        | Tests using mock services                  |

```mko
fn TestAdd() {
    assert_eq(add(2, 3), 5)
}

fn PropertyAddCommutative() {
    // property: add(a, b) == add(b, a) for all a, b
    let a = 7
    let b = 13
    assert_eq(add(a, b), add(b, a))
}

fn FuzzParser() {
    // fuzz: feed random bytes to the parser
    let input = "random"
    let r = parse_positive(42)
    match r {
        Ok(v) => assert_eq(v, 42),
        Err(_) => {}
    }
}
```

### Subtests

Use `t_run` to create named sub-sections within a test:

```mko
fn TestMath() {
    t_run("addition")
    assert_eq(1 + 1, 2)

    t_run("multiplication")
    assert_eq(3 * 4, 12)
}

fn TestNested() {
    t_run("outer")
    assert_eq(1, 1)
    t_run_nested("inner")     // prints TestNested/outer/inner
    assert_eq(2, 2)
}
```

### Test Assertions

| Function           | Purpose                          |
|--------------------|----------------------------------|
| `assert(cond)`     | Fails if condition is false      |
| `assert_eq(a, b)`  | Fails if `a != b` (int)         |
| `assert_eq_str(a, b)` | Fails if strings differ       |
| `fail("msg")`     | Unconditionally fail with message|

A failed assertion fails the current test/subtest and continues to the next.
The process exits non-zero if any test failed.

---

## `mako fmt`

Formats Mako source code. By default prints formatted output to stdout.

```bash
mako fmt main.mko                   # print formatted to stdout
mako fmt main.mko -w                # write back to file (in-place)
mako fmt . -w                       # format all .mko files in workspace
mako fmt main.mko -l                # list files that would change
mako fmt main.mko -d                # show diff of changes
mako fmt -p mylib -w                # format one workspace member
```

### What the Formatter Does

- Consistent indentation (4 spaces)
- Normalized brace placement
- Sorted and grouped imports (single `import ( ... )` block)
- Consistent spacing around operators
- Trailing newline at end of file

Example: multiple imports are consolidated:

```mko
// Before:
import "strings"
import "path"

// After mako fmt:
import (
    "path"
    "strings"
)
```

---

## `mako lint`

Runs type checking plus additional lint rules. Workspace-aware.

```bash
mako lint main.mko                  # lint one file
mako lint .                         # lint all workspace members
mako lint -p mylib                  # lint one workspace member
```

The linter flags:
- Unused variables
- Unreachable code after `return`/`break`/`continue`
- `unsafe` blocks (informational)
- Shadowed variables in nested scopes

---

## `mako doc`

Generates API documentation in Markdown format with runnable examples and a
search index.

```bash
mako doc main.mko                   # generate docs for a file
mako doc .                          # generate docs for workspace
```

Output:
- `docs/api/` -- Markdown files per module
- `docs/api/examples.md` -- extracted runnable examples
- `docs/api/search-index.json` -- symbol search for tooling

---

## `mako profile`

Profiles the compilation and execution of a program, reporting timing at each
stage.

```bash
mako profile main.mko               # human-readable output
mako profile main.mko --json        # structured JSON output
mako profile main.mko --release     # profile with release optimizations
mako profile . -p app --json        # profile one workspace member
mako profile main.mko -- arg1 arg2  # pass args to the profiled program
```

Output fields:
- **frontend_ms** -- lexing, parsing, type checking, C generation
- **backend_ms** -- clang compilation and linking
- **run_ms** -- program execution time
- **total_ms** -- wall clock for the entire operation

---

## `mako bench`

Runs benchmark files (`bench_*.mko`) and reports wall-clock execution time.

```bash
mako bench examples/bench           # run all bench files in directory
mako bench . -p app                 # run benchmarks for workspace member
mako bench path --json              # JSON output for CI tracking
```

Benchmark files should use `now_ns()` and `black_box()` for accurate measurement:

```mko
// bench_sort.mko
fn main() {
    let mut xs = make([]int, 0, 1000)
    for i in range 1000 {
        xs = append(xs, 1000 - i)
    }
    let start = now_ns()
    let sorted = sort_ints(xs)
    let _ = black_box(sorted)
    let elapsed = now_ns() - start
    print_int(elapsed)
}
```

---

## `mako pkg`

Package management commands for dependencies declared in `mako.toml`.

### Subcommands

```bash
mako pkg init mylib                  # create a new package scaffold
mako pkg list                        # show dependencies and their status
mako pkg fetch                       # clone git dependencies into .mako/deps/
mako pkg lock                        # pin SHA-256 content hashes in mako.lock
mako pkg install                     # verify and install locked dependencies
mako pkg update                      # accept changes / migrate v1 locks
mako pkg add helper ../helper        # add a path dependency
mako pkg add path=../helper          # same (name from directory basename)
mako pkg remove helper               # remove a dependency
mako pkg audit                       # offline advisory + license policy check
```

### `mako pkg audit`

Checks dependencies against known advisories and license policies:

```bash
mako pkg audit
# Checking 3 dependencies...
# OK: helper (path, MIT)
# OK: core (path, MIT)
# WARN: crypto (git, no license file found)
```

Exits non-zero if a dependency has a known vulnerability advisory.

---

## `mako lsp`

Starts the Mako Language Server Protocol implementation. Point your editor at
the `mako` binary with `lsp` as the command.

```bash
mako lsp                             # start LSP server on stdin/stdout
```

### Supported LSP Features

| Feature              | Status    |
|----------------------|-----------|
| Diagnostics          | Live errors and warnings on save |
| Hover                | Type information and doc comments |
| Completion           | Identifiers, keywords, builtins |
| Go to Definition     | Jump to function/struct/enum source |
| Find References      | All usages of a symbol |
| Rename               | Rename a symbol across files |
| Code Actions         | Quick fixes for common issues |
| Document Symbols     | Outline of functions/structs/enums |
| Signature Help       | Parameter hints while typing |

### VS Code Integration

The `editors/vscode/` extension provides:
- Syntax highlighting for `.mko` files
- Snippets for common patterns
- Task integration (build, run, test)
- Command palette actions
- Debug launch configurations (`mako-native` via CodeLLDB or cppdbg)

Configure the extension:
- `mako.path` -- path to the `mako` binary
- `mako.debug.adapter` -- `lldb` (CodeLLDB) or `cppdbg` (Microsoft C/C++)

---

## `mako doctor`

Checks your development environment and reports the status of required tools:

```bash
mako doctor
# mako: 0.2.2 (darwin/arm64)
# clang: Apple clang version 15.0.0
# zig: 0.11.0 (optional, for cross-compilation)
# wasi-sdk: /opt/wasi-sdk (optional, for WASM)
# wasmtime: 14.0.0 (optional, for running WASM)
# openssl: 3.1.4 (optional, for TLS)
```

---

## `mako init`

Scaffolds a new project.

```bash
mako init hello                      # basic application
mako init mysvc --backend            # HTTP API service scaffold
mako init myws --workspace           # workspace with multiple packages
```

The `--backend` scaffold includes a `main.mko` with HTTP server boilerplate,
health endpoint, and a README.

The `--workspace` scaffold creates:
- Root `mako.toml` with `[workspace] members = ["lib", "app"]`
- `lib/lib.mko` -- library code
- `app/main.mko` -- application that depends on lib

---

## Incremental Builds

Incremental compilation is on by default. The system works as follows:

1. **Frontend** generates C code for each translation unit.
2. A content hash of each `.c` file is computed.
3. If the hash matches a cached `.o` file in `.mako/cache/`, the object file
   is reused without invoking clang.
4. Only changed translation units are recompiled.
5. Final linking always runs (but is fast with cached objects).

On a warm build (no source changes), the build completes in milliseconds
because it short-circuits after the hash check.

### Controlling the Cache

```bash
mako build main.mko                  # uses cache (default)
mako build --no-incremental main.mko # forces full recompile
MAKO_CACHE=/tmp/mako-cache mako build main.mko  # custom cache location
```

### Parallel Jobs

Multiple `.c` -> `.o` compilations run in parallel:

```bash
mako build -j 8 main.mko            # 8 parallel clang processes
MAKO_JOBS=4 mako build main.mko     # same via environment
```

Default is the number of CPU cores detected.

---

## `mako metadata`

Emits a JSON symbol graph and AST summary for tooling integration:

```bash
mako metadata main.mko              # JSON to stdout
```

Useful for building custom documentation tools, IDE integrations, or analysis
scripts.

---

## `mako api diff`

Detects breaking API changes between two versions of a package:

```bash
mako api diff v0.1.0 v0.2.0         # compare tags/directories
```

Reports removed functions, changed signatures, and removed struct fields.

---

## `mako deploy`

Generates deployment artifacts.

```bash
mako deploy docker . --entry main.mko --bin server --port 8080
mako deploy serverless . --provider cloud-run --name myapp
mako deploy wasm dist --entry main.mko --wasm app.wasm
mako deploy plugin my-plugin --name my-plugin --kind native
```

| Subcommand   | Output                                          |
|--------------|-------------------------------------------------|
| `docker`     | Multi-stage Dockerfile + .dockerignore          |
| `serverless` | Docker + provider manifest (Cloud Run / Fly.io) |
| `wasm`       | Browser/edge WASI starter (HTML + JS loader)    |
| `plugin`     | Native or WASM plugin ABI starter               |

---

## Command Summary Table

| Command             | Purpose                              | Key Flags                    |
|---------------------|--------------------------------------|------------------------------|
| `mako check`       | Typecheck without building           | `--json`, `-p`               |
| `mako build`       | Compile to native binary             | `--release`, `-j`, `--target`|
| `mako run`         | Compile and execute                  | `-- args...`                 |
| `mako test`        | Discover and run tests               | `-r`, `-v`, `--coverage`, `--json` |
| `mako fmt`         | Format source code                   | `-w`, `-l`, `-d`             |
| `mako lint`        | Lint with additional rules           | `-p`                         |
| `mako bench`       | Run benchmarks                       | `--json`, `-p`               |
| `mako profile`     | Time frontend/backend/run            | `--json`, `--release`        |
| `mako doc`         | Generate API documentation           | —                            |
| `mako pkg`         | Package management                   | `init/list/fetch/add/remove/audit` |
| `mako lsp`         | Language server                      | —                            |
| `mako doctor`      | Environment health check             | —                            |
| `mako version`     | Print version info                   | `-v`                         |
| `mako init`        | Scaffold new project                 | `--backend`, `--workspace`   |
| `mako metadata`    | JSON symbol graph                    | —                            |
| `mako api diff`    | Breaking change detection            | —                            |
| `mako deploy`      | Deployment artifact generation       | `docker/serverless/wasm/plugin` |

---

## Debug Tooling

### `dbg` / `dbg_str`

Lightweight debug prints that show file, line, and value:

```mko
fn main() {
    let n = 42
    dbg(n)              // [dbg] main.mko:3: 42
    dbg_str("hello")   // [dbg] main.mko:4: hello
}
```

Output goes to stderr. Available in all builds (debug builds include `-g`
symbols for native debuggers).

### Native Debuggers

Debug builds produce symbols compatible with `lldb` and `gdb`:

```bash
mako build main.mko -o bin/app
lldb bin/app
(lldb) breakpoint set --file main.c --line 10
(lldb) run
```

### Sanitizers

```bash
mako build --sanitize=address main.mko   # detect buffer overflows, use-after-free
mako build --sanitize=thread main.mko    # detect data races
```

Run the resulting binary normally. On violation, the sanitizer prints a detailed
report with stack traces.

Next: [Cookbook](ch14-cookbook.md).
