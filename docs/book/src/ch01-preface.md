# 1. Preface -- Why Mako Exists

## The problem

Building backend services, infrastructure, and developer tools requires four
things at once: **simplicity**, **memory safety**, **predictable performance**,
and **fast iteration**. Most approaches force a trade-off:

- Managed runtimes give safety and a rich standard library but pay with garbage
  collection pauses, unpredictable latency spikes, and heavier deployment
  stories.
- Low-level systems approaches give total control but leave memory safety,
  ownership discipline, and concurrency correctness to the programmer's
  vigilance.
- Ownership-focused approaches give safety guarantees but can feel heavy for
  everyday HTTP servers and session-oriented work.

Mako's position is practical: you should not have to choose between safety and
simplicity. The language is designed so that the common case is both safe and
concise, with explicit annotations only where they genuinely prevent bugs.

## The Mako bet

> Memory safety without a mandatory garbage collector. Simple structured
> concurrency. Fast compiles. Clean error handling. Single-binary deployment.
> A strong standard library. Great tooling from day one.

These are not aspirational goals -- they are the shipped reality of Mako 0.1.5.

## Design philosophy

### 1. Clarity over cleverness

Mako favors explicit, readable code. There are no implicit conversions between
numeric types. Assignment (`=`) and equality (`==`) are visually distinct.
Control flow uses braces and does not rely on indentation. The formatter
(`mako fmt`) enforces a single canonical style so teams never argue about
formatting.

```mko
fn classify(code: int) -> string {
    match code {
        200 => "ok",
        404 => "not found",
        500 => "server error",
        _ => "unknown",
    }
}
```

### 2. Safety at compile time, not runtime

The ownership system (`hold` and `share`) catches use-after-move, double-free,
and data races at compile time. Result types are enforced -- you cannot silently
ignore a fallible operation. The compiler rejects programs that would have
undefined behavior, before any code runs.

```mko
fn safe_divide(a: int, b: int) -> Result[int, string] {
    if b == 0 {
        return error("division by zero")
    }
    Ok(a / b)
}

fn main() {
    // This line would be a compile error if uncommented:
    // safe_divide(10, 0)  // error: unused Result

    let r = safe_divide(10, 0)
    match r {
        Ok(v) => print_int(v),
        Err(e) => print(e),
    }
}
```

### 3. No garbage collector

Mako achieves memory safety without a tracing garbage collector. Instead, it
uses three complementary mechanisms:

- **Scope-based cleanup**: Local values are freed when their enclosing scope
  exits. `defer` statements run cleanup in LIFO order.
- **Ownership tracking**: `hold` bindings enforce move semantics. When a value
  is moved, the original binding becomes unusable -- caught at compile time.
- **Arena allocators**: For request-scoped work (HTTP handlers, message
  processing), an arena allocates many objects and frees them all at once when
  the arena exits. One deallocation for an entire request's worth of memory.

This means no GC pauses, no stop-the-world events, and predictable latency
under load.

### 4. Fast compiles

Mako compiles `.mko` sources to C, then invokes clang. This pipeline is fast:
incremental builds recompile only changed translation units. Parallel object
compilation (`mako build -j 8`) scales with cores. The result is a tight
edit-compile-run loop even for large projects.

### 5. Single binary deployment

`mako build --release` produces a statically-linked native binary. No runtime
to install on the target machine. No dependency conflicts. Copy the binary,
run it. This is ideal for containers, edge deployments, and distribution to
end users.

### 6. Batteries included

The standard library covers the common needs of backend development:

- HTTP/1.1 and HTTP/2 client and server
- TLS with certificate verification
- JSON encoding and decoding
- SQL database access (SQLite, PostgreSQL)
- WebSocket client and server
- UUID generation, base64, hashing
- File I/O, path manipulation, environment variables
- Logging with timestamps
- Regular expressions
- Sorting, string utilities, byte manipulation

You should be able to build a production service without reaching for third-party
packages for basic functionality.

### 7. Structured concurrency

Concurrency in Mako is structured through `crew` blocks. A crew spawns tasks
that must all complete before the crew exits. Combined with typed channels
(`chan[T]`) and actors, this makes concurrent programs easy to reason about:
no dangling goroutines, no fire-and-forget spawns leaking resources.

```mko
fn main() {
    let ch = make(chan[int], 4)
    crew {
        spawn { send(ch, 42) }
        spawn {
            let v = recv(ch)
            print_int(v)
        }
    }
    // crew exits only when both spawns complete
}
```

## What problems Mako solves

### Latency-sensitive services

Session-shaped servers -- long-lived connections, real-time messaging,
deterministic response times -- benefit from Mako's lack of GC pauses and its
arena-based memory model. Each request allocates from its own arena; cleanup is
a single pointer bump reset.

### Microservice backends

REST APIs and gRPC services benefit from fast startup (native binary, no
runtime warm-up), small memory footprint, and straightforward deployment (one
file to copy).

### Infrastructure tools

Proxies, load balancers, and protocol implementations benefit from low-level
control over memory layout combined with high-level safety guarantees. Mako
gives you both without forcing you to choose.

### Developer tools and CLIs

Command-line tools benefit from instant startup, single-binary distribution,
and cross-compilation to multiple targets.

### Data pipelines

Batch processing and streaming systems benefit from predictable memory usage,
arena allocation for per-record work, and straightforward concurrency via
crews and channels.

## What "done" means in this book

As of the current release:

| Claim | Meaning |
|-------|---------|
| Version 0.1.5 | Current product (patch after 0.1.4); first public was 0.1.0 release, core language complete |
| Stdlib coverage | Major backend areas covered (HTTP, TLS, JSON, SQL, etc.) |
| Test suite | 165+ examples pass in the test suite |

The language is usable for real work today. Some areas are still maturing
(full Unicode support, reflection, advanced generics). These are tracked in
STATUS.md.

## How to use this book

If you are new to Mako, read chapters 2 through 6 in order. They build on each
other:

1. **Getting Started** -- install and run your first program
2. **Language Tour** -- syntax, types, control flow
3. **Ownership** -- memory safety without GC
4. **Errors** -- Result types and error handling

Once you have the foundations, jump to chapters 7 through 10 when building
services. Use chapter 14 as a recipe index when you need to accomplish a
specific task.

When something looks wrong or you are unsure about syntax, run `mako check` on
your code. The compiler is always the source of truth.

Next: [Getting Started](ch02-getting-started.md).
