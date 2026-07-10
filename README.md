# Mako

Mako started because I love programming and kept running into the same
frustrations: garbage collector pauses killing tail latency, slow compile times
breaking flow, and the feeling that safe code shouldn't be this hard to write.

So I built a language. Mako gives you memory safety through ownership and arenas
instead of a garbage collector, structured concurrency that cleans up after
itself, and a standard library that covers what you actually need for backend
work — HTTP, TLS, JSON, databases, crypto, all there out of the box.

The compiler turns `.mko` source files into C, then clang (or zig) produces
a native binary. One file in, one binary out. No runtime to deploy, no VM to
configure.

This is **version 0.1.0**. The core language works, 130 tests are passing, and
the standard library has real coverage. But this is early days — expect rough
edges, breaking changes, and things that aren't done yet. If that's exciting to
you rather than scary, you're in the right place.

[Full status](docs/STATUS.md) · [What's next](docs/ROADMAP.md)

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
block ends, every task is joined. No orphaned threads, no forgotten cleanup,
no leaked goroutines. Channels handle communication between tasks:

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

HTTP servers, TLS, WebSocket, JSON, database drivers (SQLite, Postgres), crypto,
compression, regex, email, encoding, file I/O, and more. Build a JSON API in
one file:

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

## Documentation

| | |
|---|---|
| **[The Mako Book](docs/book/)** | Start here — walks you from install to shipping |
| [How-to Guides](docs/howto/README.md) | Practical guides for HTTP, errors, concurrency, and more |
| [Language Guide](docs/GUIDE.md) | The full syntax reference |
| [Standard Library](docs/STDLIB.md) | What's in the box |
| [Security](docs/SECURITY.md) | How Mako keeps your code safe |
| [Performance](docs/PERFORMANCE.md) | Numbers |
| [Status](docs/STATUS.md) | Honest accounting of what works and what's left |
| [Vision](docs/VISION.md) | Where this is all going |
| [Roadmap](docs/ROADMAP.md) | What's next |
| [Release](docs/RELEASE.md) | Packaging and cross-compilation |
| [Debug](docs/DEBUG.md) | lldb, gdb, `dbg()`, sanitizers |
| [Changelog](CHANGELOG.md) | What changed |

## Editor support

There's a **VS Code** extension with syntax highlighting, LSP, debugging, and
commands for build/run/test/format. Details in [editors/vscode/](editors/vscode/).

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

The honest list lives in [STATUS.md](docs/STATUS.md).

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) has the details.

## License

MIT — [LICENSE](LICENSE).
