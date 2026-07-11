# Mako

Mako is a compiled programming language. You write `.mko` files, the compiler
turns them into native binaries, and that's what you ship. No garbage collector,
no virtual machine, no runtime.

I built it because I wanted memory safety without paying for it with GC pauses
or slow compile times. Mako uses ownership tracking and arena allocators to
manage memory at compile time. It has structured concurrency that actually
cleans up after itself, error handling the compiler enforces, and a standard
library with enough in it to build real things without chasing dependencies.

This is **version 0.1.0**. It works. 130 tests pass. The standard library
covers a lot of ground. But it's still early — things will change, some corners
are rough, and there's plenty left to build.

[mako-lang.com](https://mako-lang.com) · [Status](docs/STATUS.md) · [Roadmap](docs/ROADMAP.md)

---

## Install

**macOS / Linux**

```bash
curl -fsSL https://github.com/loreste/mako/releases/latest/download/install-release.sh | bash
```

From source:

```bash
make install
mako version
```

**Windows**

```powershell
cargo build --release
.\scripts\install.ps1
mako version
```

Needs **clang** on your system. On macOS that's Xcode, on Linux `apt install clang`,
on Windows install LLVM. Optional libraries (OpenSSL, SQLite, libpq) unlock
extra features but aren't required.

---

## Write something

```bash
mako init hello && cd hello
mako run main.mko
```

```mko
fn main() {
    print("hello from mako")
    print_int(fib(10))
}

fn fib(n: int) -> int {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
```

---

## How it works

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

`crew` blocks own their tasks. When the block ends, every task gets joined.
You can't leak a thread:

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

HTTP, TLS, WebSocket, JSON, SQLite, Postgres, crypto, compression, regex,
file I/O, event loops, binary buffers, rate limiters, circuit breakers,
consistent hashing, game networking, session management — it's all built in.

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
| [How-to Guides](docs/howto/README.md) | Practical walkthroughs |
| [Language Guide](docs/GUIDE.md) | Full syntax reference |
| [Standard Library](docs/STDLIB.md) | What's included |
| [Built-in Functions](docs/BUILTINS.md) | Every function, every signature |
| [Language Spec](LANGUAGE_SPEC.md) | Formal specification |
| [CLI Reference](docs/CLI.md) | Every command, flag, and workflow |
| [Tutorials](website/tutorials/) | Backend, game networking, databases, FFI, concurrency, cloud |
| [Security](docs/SECURITY.md) | Safety model |
| [Performance](docs/PERFORMANCE.md) | Benchmarks |
| [Status](docs/STATUS.md) | What works, what doesn't |
| [Roadmap](docs/ROADMAP.md) | What's next |
| [Changelog](CHANGELOG.md) | What changed |

## Editor support

VS Code extension with syntax highlighting, LSP, debugging, format-on-save,
and a custom dark theme. See [editors/vscode/](editors/vscode/).

The language server (`mako lsp`) works with any LSP-compatible editor.

## Testing

```bash
mako test examples/testing
mako test -r TestAdd -v
mako test --coverage
```

Unit, property, fuzz, snapshot, fixture, and mock tests. Default suite runs
without external services.

## What's not done yet

- Full Unicode property database for regex
- JPEG readable by standard viewers
- Struct field reflection beyond schema registry
- SMTP AUTH over TLS
- Generics syntax may change
- Direct I/O and HTTP engine are POSIX-only for now

Full list in [STATUS.md](docs/STATUS.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT
