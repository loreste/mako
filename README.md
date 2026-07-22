# Mako

Mako is a compiled language for backend and systems work. You write `.mko`
files; Mako turns them into standalone native binaries — no garbage collector,
no VM, nothing extra to install next to them at runtime.

Memory is managed by ownership (`hold`), shared references (`share`), and
arenas, so it's freed deterministically instead of by a tracing GC that stalls
your service at the worst moment. Concurrency is built into the language rather
than bolted on: structured `crew` / `kick` / `fan`, channels, and actors that
tidy up after themselves. The standard library covers the everyday backend
surface — HTTP, TLS, JSON, databases, networking — and is candid about which
corners are battle-tested and which are still shallow.

**Status: experimental/alpha (v0.4.5).** The language works and compiles real
programs, but the surface is young. Expect breaking changes, missing features,
and bugs. The ownership model is actively being hardened — the full test suite
(360 programs) passes under AddressSanitizer with zero memory errors, but edge
cases remain. This is not yet suitable for production use without careful
evaluation.

**What's new in 0.4.5:** Mako is growing a second way to reach machine code — a
direct backend that skips C entirely. It lowers your program to an
ownership-explicit IR and emits object code through LLVM (release builds) or
Cranelift (fast debug builds), then links with a bundled linker, so there's no
external toolchain to install. On the compute workloads it handles today it's
already matching or beating hand-written C and Rust on speed, with a fraction of
the memory footprint and binaries an order of magnitude smaller than Rust's —
and every step is checked against the C backend for byte-identical output under
leak and AddressSanitizer gates. It doesn't cover the whole language yet, and
the docs say so plainly.

Before that: 0.3.0 hardened memory safety (ASan-clean free paths, no
double-frees) and grew the LSP; 0.2.4 landed the ownership drop system, string
views, and channel ownership. Full history is in the
[changelog](CHANGELOG.md); where it's headed is in the
[roadmap](docs/ROADMAP.md) · [soundness program](docs/SOUNDNESS.md).

[mako-lang.com](https://mako-lang.com) · [Status](docs/STATUS.md) · [Roadmap](docs/ROADMAP.md) · [Guide](docs/GUIDE.md) · [Book](docs/book/) · [Soundness](docs/SOUNDNESS.md) · [Memory model](docs/MEMORY_MODEL.md)

---

## Install

### One-shot (recommended) — prebuilt binary, no Rust/cargo

Mako is built with cargo **on our CI**. You download the finished binary
bundle (CLI + runtime + std). You never install Rust or run cargo yourself.

**Linux**

```bash
curl -fsSL https://github.com/loreste/mako/releases/latest/download/install-linux.sh | bash
source "$HOME/.local/share/mako/env.sh"
mako version
```

**macOS**

```bash
curl -fsSL https://github.com/loreste/mako/releases/latest/download/install-release.sh | bash
source "$HOME/.local/share/mako/env.sh"
```

That single command:

1. Installs **clang** if missing (so `.mko` files can compile)
2. Downloads the **cargo-built** `mako` binary package for your CPU
3. Installs runtime headers + standard library beside it
4. Verifies SHA-256, writes `env.sh`, updates shell RC when possible

```bash
mako init hello && cd hello && mako run main.mko
```

Or grab the raw binary only (still needs headers via the tarball/installer for compiles):

```text
https://github.com/loreste/mako/releases/latest/download/mako-x86_64-unknown-linux-gnu.tar.gz
```

Options:

```bash
curl -fsSL …/install-linux.sh | bash -s -- --prefix /opt/mako --yes
curl -fsSL …/install-linux.sh | bash -s -- --no-deps    # skip clang install
curl -fsSL …/install-linux.sh | bash -s -- --version v0.4.5
```

**You do not need Rust or cargo on the machine that runs Mako.**

### From source (large — installs Rust toolchain + crates)

```bash
make install
mako version
```

### Windows

Prefer the `.zip` from [Releases](https://github.com/loreste/mako/releases), or build from source with LLVM clang on PATH.

---

## Write something

```bash
mako init hello && cd hello
mako run main.mko
```

```mko
fn main() {
    print("hello from mako")
    print(fib(10))
}

fn fib(n: int) -> int {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
```

Real work, low ceremony: infer locals, one `print`, `?` for errors, `match` for
routes, power (`hold` / `crew` / `arena`) only when you need it.
[Ergonomics](docs/ERGONOMICS.md).

---

## How it works

### From source to a binary

Two paths, one language. The mature path lowers your `.mko` to C and lets clang
optimize it (`-O3 -flto` in release) — that's what compiles the full language
today. Alongside it, a newer **direct backend** skips C altogether: it turns
your program into an ownership-explicit IR and emits machine code through LLVM
(release) or Cranelift (fast debug builds), links it with a bundled linker, and
ships a single standalone binary — no clang, no system linker, nothing to
install. It doesn't cover the whole language yet, but where it does, the
binaries come out small and fast.

Either way there's no interpreter, no JIT, no runtime VM, and no tracing
collector. Memory is freed deterministically — ownership, scope exits, arenas —
and concurrency (`crew`, `fan`, channels) is part of the language, not a library
bolt-on.

### Ownership instead of garbage collection

The compiler tracks who owns what. `hold` bindings move when you use them —
try to use a value after it's moved and the compiler stops you. For
request-scoped work, arenas let you allocate a bunch of things and free them
all at once when the scope ends:

```mko
arena a {
    let msg = arena_text(a, "hello arena")
    let xs = arena_ints(a, 1000)
}
// everything in `a` is gone — one free, zero bookkeeping
```

### Concurrency that cleans up

`crew` blocks own jobs started with `kick`. When the block ends, those jobs are
cancelled and joined. Explicit `detach` is a process-scoped escape and must be
followed by `detached_join_all()`:

```mko
fn main() {
    let ch = chan_new(4)
    crew t {
        let p = t.kick(producer(ch, 5))
        let c = t.kick(consumer(ch))
        let _ = p.join()
        print_int(c.join())
    }
    // everything joined and done here
}
```

### Errors the compiler enforces

If a function returns a `Result`, you have to deal with it. The compiler
won't let you ignore one:

```mko
fn load() -> Result[int, string] {
    let fd = open_cfg("config.toml")?
    Ok(fd)
}
```

### Standard library

The stdlib provides APIs for common backend tasks: HTTP server/client,
TLS, WebSocket, JSON, SQLite, regex, file I/O, channels, binary buffers,
and more. Some are fully tested integrations; others are early API surfaces
that verify shape but lack deep integration testing. External dependencies
(OpenSSL, SQLite, etc.) are optional — the stdlib soft-skips when they're
absent. See [STDLIB](docs/STDLIB.md) for what's implemented vs. planned.

```mko
fn main() {
    let fd = http_bind(8080)
    while true {
        let c = http_accept(fd)
        let path = http_path(c)
        if str_eq(path, "/health") {
            let _ = http_respond_json(c, 200, "{\"ok\":true}")
        }
        let _ = http_close(c)
    }
}
```

### Builds

Incremental by default. Release builds use `-O3 -flto`.
[Benchmarks](docs/PERFORMANCE.md).

---

## Syntax tour

```mko
// Defer — runs on exit, last-in first-out
defer print("cleanup")

// Enums with methods
enum Shape {
    Circle(int),
    Rect(int, int),
}

fn Shape_area(self: Shape) -> int {
    match self {
        Circle(r) => r * r,
        Rect(w, h) => w * h,
    }
}

// Interfaces
interface Writer {
    fn write(string) -> int
}

// Derive macros
#[derive(json)]
struct Person {
    name: string
    age: int
}

// Actors
actor Session {
    receive Invite { print("invite") }
    receive Bye    { print("bye") }
}

// Memory-mapped files
let m = mmap_create("data.bin", 4096)
let _ = mmap_write(m, 0, "hello")

// Binary protocols
let b = buf_pack_new(64)
buf_write_u32be(b, 1024)

// Concurrent hashmaps
let m = cmap_new()
cmap_set(m, "key", "value")

// FFI
extern "C" fn my_c_function(n: int) -> int
```

**Putting it all together** — struct, database, error handling, arenas, and
concurrency in one program ([examples/showcase.mko](examples/showcase.mko)):

```mko
#[derive(json)]
struct Task {
    id: int
    title: string
    done: int
}

fn insert_task(db: SqlDB, title: string) -> Result[int, string] {
    let rc = sql_exec_str4(db, "INSERT INTO tasks (title, done) VALUES ($1, $2)", title, "0", "", "")
    if rc != 0 { return error("insert failed") }
    Ok(0)
}

fn worker(ch: chan[int], id: int, results: CMap) -> int {
    arena a {
        let label = arena_text(a, format_int(id))
        while true {
            let task_id = ch.recv()
            if task_id == 0 { break }
            cmap_set(results, format_int(task_id), "worker " + label + " done")
        }
    }
    return id
}

fn main() {
    let db = sql_open_sqlite("/tmp/showcase.db")
    let _ = sql_exec_plain(db, "CREATE TABLE IF NOT EXISTS tasks (id INTEGER PRIMARY KEY, title TEXT, done INTEGER)")

    match insert_task(db, "write tests") {
        Ok(_) => print("inserted"),
        Err(e) => print("error: " + e),
    }

    let results = cmap_new()
    let ch = chan_new(10)
    crew t {
        let w1 = t.kick(worker(ch, 1, results))
        let w2 = t.kick(worker(ch, 2, results))
        for i in 5 { let _ = ch.send(i + 1) }
        let _ = ch.send(0)
        let _ = ch.send(0)
        let _ = w1.join()
        let _ = w2.join()
    }

    for i in 5 { print(cmap_get(results, format_int(i + 1))) }
    let _ = sql_close(db)
}
```

More in [examples/](examples/) and [The Mako Book](docs/book/).

---

## Commands

```bash
mako init myapp                  # new project
mako run main.mko                # compile and run
mako build main.mko              # compile
mako build --release main.mko    # optimized
mako test examples/testing       # test suite
mako fmt -w                      # format
mako lint                        # lint
mako check main.mko              # type-check only
mako build --target wasm32-wasip1 main.mko  # WebAssembly
```

## Multi-file projects

Split your code across files, import what you need:

```mko
// main.mko
import "./db.mko"
import "./routes.mko"

fn main() {
    let _ = db_init()
    let fd = http_bind(8080)
    while true {
        let c = http_accept(fd)
        handle(c)
        let _ = http_close(c)
    }
}
```

Grouped and aliased imports work too:

```mko
import (
    "./db.mko"
    "./routes.mko"
    "strings"
)

import "./helpers.mko" as h
```

For separate packages, use `mako.toml`:

```toml
[dependencies]
helper = { path = "../helper" }
```

## Docs

| | |
|---|---|
| **[The Mako Book](docs/book/)** | Start here |
| **[Identity](docs/IDENTITY.md)** | Our syntax + identity checklist |
| [Dual-form inventory](docs/GO_SYNTAX_CHECKLIST.md) | Optional sugar (not preferred) |
| [How-to Guides](docs/howto/README.md) | Practical walkthroughs |
| [Language Guide](docs/GUIDE.md) | Current syntax reference (Mako-native) |
| [Compat](docs/COMPAT.md) | Dual forms / what won't break |
| [Standard Library](docs/STDLIB.md) | What's included |
| [Built-in Functions](docs/BUILTINS.md) | Documented built-ins and signatures |
| [Language Spec](LANGUAGE_SPEC.md) | Formal specification |
| [CLI Reference](docs/CLI.md) | Current commands, flags, and workflows |
| [Tutorials](website/tutorials/) | Backend, game networking, databases, FFI, concurrency, cloud |
| [Examples](docs/EXAMPLES.md) | Runnable programs |
| [Debugging](docs/DEBUG.md) | dbg(), lldb, sanitizers, error messages |
| [Security](docs/SECURITY.md) | Safety model |
| [Soundness](docs/SOUNDNESS.md) | SAFE/RT program (bounds, drops, ownership) |
| [Memory model](docs/MEMORY_MODEL.md) | Concurrency, crew lifecycle, channel ownership |
| [Performance](docs/PERFORMANCE.md) | Benchmarks |
| [Status](docs/STATUS.md) | What works, what doesn't |
| [Roadmap](docs/ROADMAP.md) | What's next |
| [Changelog](CHANGELOG.md) | What changed |

## Editor support

VS Code extension with syntax highlighting, LSP, format-on-save, launch
configurations, and a custom dark theme. Debugging uses host adapters where
available. See [editors/vscode/](editors/vscode/).

The language server (`mako lsp`) speaks stdio JSON-RPC and supports the
implemented features in LSP-compatible editors.

## Testing

```bash
mako test examples/testing
mako test -r TestAdd -v
mako test --sanitize address examples/testing  # ASan
mako test --race examples/testing/crew_fan_test.mko  # TSan
```

360 test programs. The suite runs under AddressSanitizer and
ThreadSanitizer in CI. Tests that require optional external libraries
(SQLite, QUIC) soft-skip when the dep is absent — they verify API shape
but not full integration. See [TEST_CATEGORIES.md](examples/testing/TEST_CATEGORIES.md)
for what each test actually proves.

## Our syntax — its own

Mako is **its own language** with **its own syntax**. We take simplicity and
control as *goals*, and we design the surface around the problems backend
engineers actually hit — GC pauses, null, error-handling noise, leaked tasks,
and lifetime ceremony — solving them with Mako's own tools rather than borrowing
another look.

Keywords: `fn`, `on`, `pack`, `pull`, `hold`, `share`, `arena`, `crew`, `kick`,
`match`, `export`.  
[Identity](docs/IDENTITY.md) · [Pain points](docs/PAIN_POINTS.md).

```mko
export struct Point {
    x: int
    y: int
}

on Point {
    fn distance(self) -> int {
        return self.x + self.y
    }
}

fn divmod(a: int, b: int) -> (int, int) {
    return (a / b, a % b)
}

fn main() {
    let p = Point { x: 3, y: 4 }
    print(p.distance())

    let q, r = divmod(17, 5)
    print(q)
    print(r)

    // Structured concurrency (ordinary kicked tasks are joined)
    crew t {
        let j = t.kick(work())
        print(j.join())
    }
}
```

Identity: [docs/IDENTITY.md](docs/IDENTITY.md).  
Sample: [examples/mako_style.mko](examples/mako_style.mko).  
Dual spellings (`func`, `:=`, …) still parse for compatibility.

## What's not done yet

- Full Unicode property database for regex
- JPEG readable by standard viewers
- Struct field reflection beyond schema registry
- SMTP AUTH over TLS
- Direct I/O and HTTP engine are POSIX-only for now

Full list in [STATUS.md](docs/STATUS.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT
