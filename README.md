# Mako

Mako started because I love programming and kept running into the same
frustrations: garbage collector pauses killing tail latency, slow compile times
breaking flow, and the feeling that safe code shouldn't be this hard to write.

So I built a language. Mako gives you memory safety through ownership and arenas
instead of a garbage collector, structured concurrency that cleans up after
itself, and a standard library that covers what you actually need for real
work — HTTP, TLS, databases, crypto, event loops, binary protocols, and more,
all there out of the box.

The compiler turns `.mko` source files into C, then clang (or zig) produces
a native binary. One file in, one binary out. No runtime to deploy, no VM to
configure.

This is **version 0.1.0**. The core language works, 130 tests are passing, and
the standard library has real coverage. But this is early days — expect rough
edges, breaking changes, and things that aren't done yet. If that's exciting to
you rather than scary, you're in the right place.

[Website](https://mako-lang.com) · [Full status](docs/STATUS.md) · [What's next](docs/ROADMAP.md)

---

## Get started

**macOS / Linux**

```bash
curl -fsSL https://github.com/loreste/mako/releases/latest/download/install-release.sh | bash
```

Or build from source:

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

You need **clang** installed — Xcode on macOS, `apt install clang` on Linux,
LLVM on Windows. Some optional features use OpenSSL, libnghttp2, SQLite, libpq,
or quiche, but the core language works without them.

Cross-compile: `mako build --target <triple>` — zig cc is used automatically
when available. Details in [RELEASE.md](docs/RELEASE.md).

---

## Hello, Mako

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

## What Mako gives you

### Memory safety without a garbage collector

Mako tracks ownership at compile time. `hold` bindings move on use — the
compiler catches use-after-free before your code ever runs. When you need
short-lived allocations (a request handler, a batch job), arenas let you
allocate many objects and free them all at once:

```mko
arena a {
    let msg = arena_text(a, "hello arena")
    let xs = arena_ints(a, 1000)
    // use them freely
}
// everything in `a` is freed here — one call, no GC pause
```

### Structured concurrency

The `crew` block manages concurrent tasks and guarantees cleanup. When the
block ends, every task is joined. No orphaned threads, no forgotten cleanup.
Channels handle communication between tasks:

```mko
fn producer(ch: chan[int], n: int) -> int {
    for i in n {
        let _ = ch.send(i + 1)
    }
    ch.close()
    return n
}

fn consumer(ch: chan[int]) -> int {
    let mut sum = 0
    for i in 5 {
        sum = sum + ch.recv()
    }
    return sum
}

fn main() {
    let ch = chan_new(4)
    crew t {
        let p = t.kick(producer(ch, 5))
        let c = t.kick(consumer(ch))
        let _ = p.join()
        print_int(c.join())     // 15
    }
}
```

### Errors you can't ignore

`Result` types are enforced. The compiler won't build code that throws away
a `Result`. Error propagation with `?` and wrapping are built in:

```mko
fn open_cfg(path: string) -> Result[int, string] {
    if str_eq(path, "") {
        return error("empty path")
    }
    Ok(1)
}

fn load() -> Result[int, string] {
    let fd = open_cfg("config.toml")?   // propagates on error
    Ok(fd)
}
```

### A standard library you can actually use

Mako ships with a broad standard library so you can build real services without
hunting for third-party packages:

| Area | What's included |
|------|----------------|
| **Networking** | HTTP server/client, TLS/HTTPS, HTTP/2, WebSocket, TCP, UDP, QUIC |
| **Data** | JSON, CSV, XML, base64, hex, gob, binary encoding |
| **Databases** | SQLite, PostgreSQL (parameterized queries, transactions) |
| **Crypto** | SHA-256, SHA-512, HMAC, AES-GCM, argon2, constant-time compare |
| **Concurrency** | Channels, actors, CMap (concurrent hashmap), mutex, RWMutex |
| **I/O** | Buffered I/O, direct I/O, memory-mapped files, file system ops |
| **Infrastructure** | Event loop (epoll/kqueue), rate limiter, circuit breaker, consistent hashing |
| **Game networking** | UDP with peer tracking, tick timing, binary buffers |
| **HTTP engine** | Declarative routing, multi-core, zero-allocation hot path |
| **Text** | Strings, regex, strconv, unicode/utf8, fmt, templates |
| **System** | OS, path, env, exec, flag, time, math, compress/gzip |

Build a JSON API in one file:

```mko
fn main() {
    let fd = http_bind(8080)
    print("listening on :8080")
    while true {
        let c = http_accept(fd)
        let method = http_method(c)
        let path = http_path(c)

        if str_eq(path, "/health") {
            let _ = http_respond_json(c, 200, "{\"ok\":true}")
        } else {
            if str_eq(method, "POST") {
                let body = http_body(c)
                let _ = http_respond_json(c, 201, body)
            } else {
                let _ = http_respond_json(c, 404, "{\"error\":\"not found\"}")
            }
        }
        let _ = http_close(c)
    }
}
```

### Fast builds

Incremental compilation is on by default. The compiler caches intermediate
objects and only rebuilds what changed. Release builds optimize with
`-O3 -flto`. [Benchmarks](docs/PERFORMANCE.md).

---

## More of the language

**Defer** — cleanup that runs on function exit, last-in first-out:

```mko
fn main() {
    defer print("third")
    defer print("second")
    print("first")
}
// output: first, second, third
```

**Enums with methods:**

```mko
enum Shape {
    Circle(int),
    Rect(int, int),
    Point,
}

fn Shape_area(self: Shape) -> int {
    match self {
        Circle(r) => r * r,
        Rect(w, h) => w * h,
        Point => 0,
    }
}

fn main() {
    print_int(Circle(5).area())   // 25
    print_int(Rect(3, 4).area())  // 12
}
```

**Generics and interfaces:**

```mko
interface Writer {
    fn write(string) -> int
}

fn takes_result(r: Result<int, string>) -> int {
    return result_unwrap_or(r, 0)
}

fn main() {
    let xs: List<int> = [1, 2, 3]
    print_int(len(xs))
    print_int(takes_result(Ok(42)))
}
```

**Derive macros** — generate JSON serialization from a struct:

```mko
#[derive(json)]
struct Person {
    name: string
    age: int
}

fn main() {
    let j = Person_to_json("Ada", 36)
    print(j)
    let name = Person_name_from_json(j)
    let age = Person_age_from_json(j)
    print(name)
    print_int(age)
}
```

**Concurrent hashmap** — thread-safe, lock-free reads:

```mko
fn main() {
    let m = cmap_new()
    crew t {
        let _ = t.kick(fn() -> int {
            cmap_set(m, "hits", "0")
            cmap_incr(m, "hits", 100)
            return 0
        })
    }
    print(cmap_get(m, "hits"))
}
```

**Actors** — message-passing concurrency with a mailbox:

```mko
actor Session {
    receive Invite { print("invite") }
    receive Timer  { print("tick") }
    receive Bye    { print("bye") }
}

fn main() {
    let session = Session_spawn()
    crew t {
        let loopj = t.kick(Session_loop(session))
        let _ = Session_send(session, Session_Invite())
        let _ = Session_send(session, Session_Timer())
        let _ = Session_send(session, Session_Bye())
        print_int(loopj.join())
    }
}
```

**Channel select** — multiplex across channels with a timeout:

```mko
fn sender(ch: chan[int], v: int) -> int {
    sleep_ms(30)
    let _ = ch.send(v)
    return 0
}

fn main() {
    let a = chan_new(2)
    let b = chan_new(2)
    crew t {
        let _ = t.kick(sender(a, 11))
        let _ = t.kick(sender(b, 22))
        let which = chan_select2(a, b, 500)
        print_int(chan_select_value())
    }
}
```

**Direct I/O and memory-mapped files** — for databases and storage engines:

```mko
fn main() {
    let m = mmap_create("data.bin", 4096)
    let _ = mmap_write(m, 0, "hello mmap")
    let data = mmap_read(m, 0, 10)
    print(data)
    let _ = mmap_sync(m, 0)
    let _ = mmap_close(m)
}
```

**Binary buffers** — read and write binary protocols:

```mko
fn main() {
    let b = buf_pack_new(64)
    buf_write_u8(b, 0x01)
    buf_write_u32be(b, 1024)
    buf_write_str(b, "hello")
    buf_seek(b, 0)
    print_int(buf_read_u8(b))       // 1
    print_int(buf_read_u32be(b))    // 1024
}
```

**C FFI** — call into C directly:

```mko
extern "C" fn mako_c_abs(n: int) -> int
extern "C" fn mako_c_add(a: int, b: int) -> int

fn main() {
    print_int(mako_c_abs(0 - 42))  // 42
    print_int(mako_c_add(20, 22))  // 42
}
```

More examples in [examples/](examples/) and [The Mako Book](docs/book/).

---

## Day-to-day commands

```bash
mako init myapp                  # start a new project
mako run main.mko                # compile and run
mako build main.mko              # just compile
mako build --release main.mko    # optimized build
mako test examples/testing       # run the test suite
mako test -r TestAdd -v          # run one test, verbose
mako fmt -w                      # format your code
mako lint                        # catch issues early
mako check main.mko              # type-check without building
mako build --target wasm32-wasip1 main.mko  # target WebAssembly
```

## Packages

Mako projects use `mako.toml`:

```bash
mako init mylib
mako pkg add helper ../helper
mako pkg fetch
mako pkg lock
mako pkg audit
```

## Multi-file projects

Real programs outgrow a single file pretty fast. Mako makes splitting things up
straightforward — just `import` what you need, and `mako run` pulls everything
together automatically.

**Same-directory imports** — the simplest case:

```mko
// db.mko
fn db_init() -> int {
    // set up database connection
    return 0
}

fn db_get_user(id: int) -> string {
    // look up user
    return "alice"
}
```

```mko
// routes.mko
fn handle_health(c: int) {
    let _ = http_respond_json(c, 200, "{\"ok\":true}")
}

fn handle_user(c: int, id: int) {
    let name = db_get_user(id)
    let _ = http_respond_json(c, 200, json_object("name", name))
}
```

```mko
// main.mko
import "./db.mko"
import "./routes.mko"

fn main() {
    let _ = db_init()
    let fd = http_bind(8080)
    while true {
        let c = http_accept(fd)
        let path = http_path(c)
        if str_eq(path, "/health") {
            handle_health(c)
        }
        let _ = http_close(c)
    }
}
```

```bash
mako run main.mko     # automatically compiles db.mko and routes.mko too
```

**Aliased imports** — give an imported file a namespace:

```mko
import "./db.mko" as db
import "./routes.mko" as routes
```

**Grouped imports** — when you have several:

```mko
import (
    "./routes.mko"
    "./db.mko"
    "strings"
    "encoding/json"
)
```

**Package dependencies** — for code in a separate directory with its own
`mako.toml`:

```toml
# app/mako.toml
[dependencies]
helper = { path = "../helper" }
```

```mko
fn main() {
    print_int(helper.add(1, 2))
}
```

**Workspaces** — multiple packages under one roof:

```toml
# mako.toml (workspace root)
[workspace]
members = ["core", "helper", "app"]
```

Working examples live in `examples/db_engine/` (file imports) and
`examples/pkg_path_dep/` (workspace with package dependencies).

## Documentation

| | |
|---|---|
| **[The Mako Book](docs/book/)** | Start here — walks you from install to shipping |
| [How-to Guides](docs/howto/README.md) | Practical guides for HTTP, errors, concurrency, and more |
| [Language Guide](docs/GUIDE.md) | The full syntax reference |
| [Standard Library](docs/STDLIB.md) | What's in the box |
| [Built-in Functions](docs/BUILTINS.md) | Complete reference — every function, signature, and description |
| [Security](docs/SECURITY.md) | How Mako keeps your code safe |
| [Performance](docs/PERFORMANCE.md) | Numbers |
| [Status](docs/STATUS.md) | Honest accounting of what works and what's left |
| [Vision](docs/VISION.md) | Where this is all going |
| [Roadmap](docs/ROADMAP.md) | What's next |
| [Release](docs/RELEASE.md) | Packaging and cross-compilation |
| [Debug](docs/DEBUG.md) | lldb, gdb, `dbg()`, sanitizers |
| [Changelog](CHANGELOG.md) | What changed |

## Editor support

There's a **VS Code** extension with syntax highlighting, LSP (completions,
hover, go-to-definition, rename), debugging via CodeLLDB, format-on-save,
and built-in commands for build/run/test/format. See [editors/vscode/](editors/vscode/).

The language server (`mako lsp`) speaks standard LSP, so it works with any
editor that supports it.

## Testing

```bash
mako test examples/testing
mako test examples/testing -r TestAdd -v
mako test --coverage
```

The suite covers unit, property, fuzz, snapshot, fixture, and mock tests.
Some tests need live services — enable with `MAKO_LIVE_TLS=1`,
`MAKO_LIVE_NGHTTP2=1`, or `MAKO_LIVE_QUIC=1`. The default suite runs clean
without them.

## What's not done yet

This is 0.1.0. Some things are still in progress:

- Unicode property escapes work for common scripts but the full database isn't there yet
- JPEG encoding uses JFIF headers with a custom payload — not yet readable by all viewers
- Struct field reflection has a schema registry but field values are still string-typed
- SMTP AUTH works over plaintext; full AUTH over TLS is partial
- Generics syntax is working but may see refinements
- Direct I/O and HTTP engine are POSIX-only for now (stubs on Windows)

The honest list lives in [STATUS.md](docs/STATUS.md).

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) has the details.

## License

MIT — [LICENSE](LICENSE).
