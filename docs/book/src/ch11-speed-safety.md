# 11. Speed and Safety

Mako compiles to C, then to native machine code via clang. There is no garbage
collector. Memory is managed through ownership (`hold`/`share`) and arena
allocation. Own free is **once** per allocation: live owns **move** (no extra
alloc); aliases and field borrows **clone** only when required. This chapter
explains how Mako keeps your programs both fast and safe, and the tools you
have when you need to push further in either direction.

---

## Release Builds

By default, `mako build` produces a debug binary with `-O0 -g` -- fast compile
times, full debug symbols, and all runtime safety checks enabled. When you are
ready to ship:

```bash
mako build --release main.mko -o bin/app
```

The `--release` flag tells the backend to compile with `-O3 -flto`:

- **`-O3`** enables aggressive optimizations: inlining, vectorization, loop
  unrolling, dead code elimination, and constant propagation.
- **`-flto`** (link-time optimization) lets the optimizer see across translation
  units, eliminating unused functions and inlining across module boundaries.

You can measure where time is spent:

```bash
mako build --time main.mko          # prints frontend + backend + link durations
mako profile main.mko --json        # structured output for CI dashboards
```

A typical release binary for a small service is under 200 KB on arm64.

---

## Incremental and Parallel Builds

Mako uses an incremental compilation cache by default. Object files are stored
in `.mako/cache/` and reused when source has not changed.

| Flag / Environment Variable | Meaning |
|-----------------------------|---------|
| (default)                   | Incremental on, cache at `.mako/cache/` |
| `--no-incremental`          | Bypass the object cache entirely |
| `-j N` / `MAKO_JOBS`       | Number of parallel clang invocations |
| `MAKO_CACHE`               | Override the cache directory path |

Example: building a workspace with 8 parallel jobs:

```bash
mako build -j 8 . --release
```

On a cold build the frontend (lex, parse, typecheck, C codegen) is typically
under 100 ms. The clang backend dominates. Incremental builds skip unchanged
translation units entirely.

---

## Bounds Checking: Debug vs Release

All slice and array accesses are bounds-checked at runtime in **both** debug and
release builds. An out-of-bounds index aborts the program immediately with a
message indicating the file, line, index, and length:

```text
abort: index 5 out of bounds (len 3) at main.mko:12
```

For safe Mako indexing, this prevents an out-of-bounds access from becoming a
buffer overflow. The check is typically a comparison and branch; measure the
cost on the workload that matters before opting into `unsafe_index`.

### When You Need to Opt Out

In extremely hot loops where profiling shows the bounds check is measurable, you
can use `unsafe_index`:

```mko
fn sum_hot(xs: []int) -> int {
    let mut total = 0
    let n = len(xs)
    let mut i = 0
    while i < n {
        unsafe {
            total = total + unsafe_index(xs, i)
        }
        i = i + 1
    }
    return total
}
```

`unsafe_index` skips the bounds check. It must appear inside an `unsafe` block.
If you pass an invalid index, behavior is undefined -- there is no safety net.

**Guideline:** Only use `unsafe_index` when you have profiled and confirmed the
bounds check is the bottleneck. In most code, the checked path is free.

---

## The Hold/Share Move Checker

Mako enforces ownership at compile time through `hold` and `share` bindings. The
checker runs during `mako check` and prevents use-after-move, double-free, and
aliasing violations without any runtime cost.

### Hold: Unique Ownership

A `hold` binding has exclusive ownership. When the value is rebound or passed
to a function, ownership transfers and the original binding is dead:

```mko
fn consume(s: string) {
    print(s)
}

fn main() {
    hold let x = "hello"
    consume(x)          // x moved into consume
    // print(x)        // COMPILE ERROR: use of moved value `x`
}
```

### Partial Moves on Structs

For struct values, individual fields can be moved independently:

```mko
struct Pair {
    left: string
    right: string
}

fn main() {
    hold let p = Pair { left: "a", right: "b" }
    let l = p.left      // moves only `left`
    print(p.right)      // `right` still usable
    // print(p.left)    // COMPILE ERROR: field already moved
}
```

### Copy Types

Primitive types (`int`, `int64`, `int32`, `int8`, `uint64`, `byte`, `float64`,
`bool`) are Copy. A `hold` binding of a Copy type can be read multiple times
without consuming it:

```mko
fn main() {
    hold let n = 42
    print_int(n)        // fine
    print_int(n)        // still fine -- int is Copy
}
```

### Share: Borrowed Access

`share` creates an immutable borrow. While a share is live, the source cannot
be mutated:

```mko
fn main() {
    let mut x = 10
    share let s = share_int(x)
    print_int(share_get(s))
    // x = 20           // COMPILE ERROR: cannot mutate while share is live
    share_drop(s)
    x = 20              // fine now
}
```

The checker uses control-flow-graph analysis to determine precisely when a share
ends. A share that is last used on line 5 does not block mutations on line 7,
even within the same scope (mid-scope drop).

---

## Non-Lexical Lifetimes (NLL)

The move checker does not use lexical scopes to determine when a binding is
live. Instead, it traces actual control flow through the program.

### If/Else Branches

```mko
fn main() {
    hold let x = "data"
    if some_condition() {
        consume(x)      // moves x on this path
    } else {
        // x not moved here
    }
    // After the if: x MAY be moved (moved on one arm)
    // print(x)         // COMPILE ERROR
}
```

If x is moved on **all** non-diverging branches, it is dead after the join. If
moved on no branches, it remains live.

### Diverging Arms

A branch that always returns, breaks, or continues does not contribute to the
join point:

```mko
fn process(data: string) -> int {
    hold let x = data
    if len(x) == 0 {
        return -1       // diverges -- this arm's move state is irrelevant
    }
    // x is still live here because the only non-diverging path did not move it
    print(x)
    return 0
}
```

### Loop-Carried Analysis

The checker iterates loop bodies to detect moves that could be reached on a
second iteration:

```mko
fn main() {
    hold let x = "once"
    let mut i = 0
    while i < 3 {
        // print(x)     // COMPILE ERROR: loop-carried move
        i = i + 1
    }
}
```

An always-break loop body does not trigger the second-pass check:

```mko
fn main() {
    hold let x = "data"
    while true {
        print(x)        // OK: always breaks, never re-enters
        break
    }
}
```

### Const-Bool Edge Pruning

The checker recognizes constant booleans. `if false { ... }` is dead code and
does not affect move state:

```mko
fn main() {
    hold let x = "kept"
    if false {
        consume(x)      // dead -- does not move x
    }
    print(x)            // fine
}
```

---

## Arena Allocators for Predictable Latency

Arenas provide bump-pointer allocation: many allocations, one bulk free at scope
exit. There is no per-object free and no fragmentation within the arena's
lifetime.

```mko
fn handle_request(fd: int) {
    arena a {
        // All allocations below come from the arena
        let mut buf = make([]byte, 0, 4096)
        let mut headers = make([]string, 0, 16)
        let body = read_body(fd, buf)
        let response = process(body, headers)
        send_response(fd, response)
    }
    // One free here -- no GC pause, no per-allocation overhead
}
```

### Arena-Backed Structs and Slices

Inside an `arena` block, `make` allocates from that arena:

```mko
struct Point {
    x: int
    y: int
}

fn main() {
    arena a {
        let mut xs = make([]Point, 2, 4)
        xs[0] = Point { x: 1, y: 2 }
        xs[1] = Point { x: 3, y: 4 }
        xs = append(xs, Point { x: 5, y: 6 })
        print_int(len(xs))   // 3
        print_int(xs[2].x)   // 5
    }
}
```

### When to Use Arenas

- **Request-scoped buffers** in servers: allocate everything for one request
  from a single arena, free it all when the response is sent.
- **Batch processing**: parse a document into many small nodes, process them,
  then discard everything at once.
- **Latency-sensitive paths**: avoid unpredictable allocation times from the
  system allocator's free-list walk.

Arenas are not appropriate for data that must outlive a scope. For long-lived
data, use normal allocations with `hold` ownership.

---

## Unsafe Blocks

The `unsafe` keyword marks code where the compiler's safety guarantees are
suspended. Today this means:

- `unsafe_index(slice, i)` -- unchecked bounds access.

```mko
fn main() {
    let xs = [10, 20, 30]
    unsafe {
        let v = unsafe_index(xs, 1)
        print_int(v)    // 20
    }
}
```

Unsafe blocks are syntactically visible and greppable. Code review tools and
`mako lint` flag them. The goal is that 99.9% of code never needs `unsafe`.

### Guidelines for Unsafe

1. **Prove correctness locally.** The code immediately around `unsafe_index`
   should make it obvious why the index is valid (e.g., a preceding length
   check or loop bound).
2. **Keep unsafe blocks small.** Wrap a single operation, not an entire
   function.
3. **Document the invariant.** A comment above the unsafe block should state
   why it is safe.
4. **Profile first.** Do not reach for `unsafe_index` without profiling
   evidence that bounds checking is the bottleneck.

---

## Secret Handling

Mako provides primitives for handling sensitive data (API keys, passwords,
tokens) that should not linger in memory:

```mko
fn main() {
    let key = secret_from_str("sk-live-abc123xyz")
    // ... use key for authentication ...
    secret_drop(key)    // zeroes the memory before freeing
}
```

### How It Works

- `secret_from_str(s)` copies the string into a dedicated allocation and
  returns an opaque handle.
- `secret_drop(handle)` overwrites the allocation with zeroes, then frees it.
  The original string `s` is a normal Mako string (GC-free but not zeroed);
  `secret_from_str` exists so you can control the lifetime of the sensitive
  copy.

This prevents secrets from persisting in freed memory where they could be
exposed by a crash dump, memory inspector, or allocation reuse.

---

## Constant-Time Compare

When comparing secrets (tokens, HMACs, password hashes), a naive `==` leaks
information through timing differences. Mako provides `const_eq`:

```mko
fn verify_token(got: string, want: string) -> bool {
    return const_eq(got, want) == 1
}
```

`const_eq` always examines every byte of both strings, regardless of where they
differ. It returns `1` for equal, `0` for not equal. Use this for any
security-sensitive comparison.

---

## Header Injection Prevention

The HTTP builtins include `http_header_ok` which rejects header names or values
containing CR/LF characters:

```mko
fn safe_set_header(name: string, val: string) -> bool {
    if http_header_ok(name, val) == 1 {
        // safe to use
        return true
    }
    return false
}
```

This prevents HTTP response splitting attacks at the API level.

---

## Thread-Safe Data Structures

For concurrent workloads that need shared mutable state, Mako provides `CMap` --
a concurrent hashmap with shared-reader and exclusive-writer synchronization. It can
be shared across crew tasks without channels, mutexes, or ownership annotations:

```mko
fn main() {
    let counters = cmap_new()
    crew t {
        let _ = t.kick(increment(counters, "a"))
        let _ = t.kick(increment(counters, "b"))
        let _ = t.kick(increment(counters, "a"))
    }
    print_int(cmap_incr(counters, "a", 0))  // 2
    print_int(cmap_incr(counters, "b", 0))  // 1
}

fn increment(m: CMap, key: string) -> int {
    let _ = cmap_incr(m, key, 1)
    return 0
}
```

CMap provides internally synchronized concurrent key-value operations: each
operation is linearizable, reads share a readers/writer gate, and writes take
its exclusive side. For direct shared key-value state, this can avoid
application-managed channel coordination.

---

## Rate Limiting and Circuit Breaking

For distributed services, Mako provides two safety primitives that protect
systems from overload and cascading failures (`runtime/mako_cloud.h`).

### Rate Limiter

The `RateLimiter` type implements a token-bucket algorithm. It prevents callers
from exceeding a defined request rate:

```mko
fn main() {
    // Allow 100 requests/second with a burst of 10
    let rl = ratelimit_new(100, 10)

    let mut allowed = 0
    let mut rejected = 0
    for _ in range 20 {
        if ratelimit_allow(rl) == 1 {
            allowed = allowed + 1
        } else {
            rejected = rejected + 1
        }
    }
    print_int(allowed)              // up to 10 (burst)
    print_int(rejected)             // remainder
    print_int(ratelimit_remaining(rl))
    ratelimit_free(rl)
}
```

Use rate limiters at API boundaries to prevent abuse and ensure fair resource
sharing between clients.

### Circuit Breaker

The `CircuitBreaker` type prevents repeated calls to a failing downstream
service. After a threshold of failures, the breaker opens and rejects requests
immediately (fail-fast), protecting both the caller and the downstream:

```mko
fn call_downstream(cb: CircuitBreaker) -> int {
    if breaker_allow(cb) == 0 {
        return -1  // circuit open, fail fast
    }
    // Attempt the call...
    let success = 0  // simulate failure
    if success == 1 {
        breaker_success(cb)
    } else {
        breaker_failure(cb)
    }
    return success
}

fn main() {
    // Open after 5 failures; wait 30s before half-open; allow 3 probes
    let cb = breaker_new(5, 30000, 3)
    for _ in range 10 {
        let _ = call_downstream(cb)
    }
    // After 5 failures, state transitions to open
    print_int(breaker_state(cb))  // 1 (open)
    breaker_reset(cb)
    print_int(breaker_state(cb))  // 0 (closed)
    breaker_free(cb)
}
```

Circuit breaker states:
- **0 (closed)**: Normal operation. Failures are counted.
- **1 (open)**: Too many failures. All requests rejected immediately.
- **2 (half-open)**: After timeout expires, a limited number of probe requests
  are allowed through. If they succeed, the breaker closes; if they fail, it
  reopens.

These primitives complement the memory-safety guarantees with operational
safety for networked services.

---

## Session and Authentication Security

Mako's session management and authentication toolkit (`runtime/mako_security.h`)
extends the safety-by-default philosophy to web security:

- **Constant-time comparisons everywhere.** All token, password, session, and
  CSRF comparisons use `const_eq` internally. Functions like
  `auth_session_cookie`, `auth_check_bearer`, `auth_check_basic`,
  `auth_token_check`, and `csrf_check` are immune to timing attacks by
  construction. Application code never needs to implement its own comparison
  logic.

- **Secure cookie defaults.** `cookie_make` produces cookies with `HttpOnly`
  (prevents JavaScript access), `SameSite=Lax` (mitigates cross-site request
  forgery), and `Path=/`. There is no "insecure cookie" API to misuse.

- **Cryptographic session IDs.** `session_id_new` generates 16 bytes of
  cryptographic randomness (via `mako_random_bytes`), formatted as 32 hex
  characters. This provides 128 bits of entropy, making session ID guessing
  computationally infeasible.

- **Secret memory wiping.** Signing keys and API tokens can be stored via
  `secret_from_str` and explicitly zeroed with `secret_drop`, preventing
  sensitive material from lingering in freed memory where crash dumps or
  allocation reuse could expose it.

- **HMAC-SHA256 signed tokens.** `auth_token_sign` produces tamper-evident
  tokens using HMAC-SHA256. Verification via `auth_token_check` is constant-time.

---

## Memory Safety Contract Summary

| Risk               | Prevention                                  |
|--------------------|---------------------------------------------|
| Use-after-move     | CFG NLL + `hold` checker at compile time    |
| Buffer overflow    | Bounds checks on every index (release too)  |
| Orphan threads     | `crew` structured concurrency -- cancel/join|
| Data races         | Channels + crew isolation + CMap (thread-safe by design) |
| Header injection   | `http_header_ok` rejects CR/LF             |
| Secret residue     | `secret_from_str` + `secret_drop` zeroing  |
| Timing side-channel| `const_eq` constant-time comparison (all auth/session/CSRF functions) |
| Session hijacking  | Crypto-random session IDs (128-bit entropy), HttpOnly cookies |
| CSRF               | `csrf_token` / `csrf_check` with constant-time verify; SameSite=Lax cookies |
| SQL injection      | Parameterized queries only (`sqlite_query_int_params`) |

---

## Performance Habits

1. **Pre-size slices and maps.** `make([]T, 0, n)` avoids repeated growth
   copies. `make(map[K]V, n)` avoids rehashing.

2. **Use arenas for request-scoped work.** One bulk free is cheaper than many
   individual frees.

3. **Prefer `hold` over `share`.** Unique ownership has zero runtime cost.
   Shared references add reference counting overhead.

4. **Avoid string copies in hot paths.** Use `str_builder` for concatenation.
   Use `[]byte` when building output incrementally.

5. **Measure with `now_ns`.** The monotonic nanosecond clock is the right tool
   for benchmarking:

```mko
fn main() {
    let start = now_ns()
    // ... work ...
    let elapsed = now_ns() - start
    print_int(elapsed)
}
```

6. **Use `black_box` in benchmarks.** Prevents the optimizer from eliminating
   work that has no observable side effect:

```mko
fn main() {
    let start = now_ns()
    let mut sum = 0
    for i in range 1000000 {
        sum = sum + i
    }
    let _ = black_box(sum)
    print_int(now_ns() - start)
}
```

7. **Profile before optimizing.** `mako profile main.mko --json` reports
   frontend, backend, and run time. Focus on what the profile shows.

---

## Sanitizers

For catching memory and threading bugs that slip past static analysis:

```bash
mako build --sanitize=address path.mko    # AddressSanitizer: buffer overruns, use-after-free
mako build --sanitize=thread path.mko     # ThreadSanitizer: data races
```

These add runtime instrumentation. Binaries are slower but report precise
diagnostics on violation. Use them in CI test runs.

---

## The legacy `[package] systems = true` Marker

In `mako.toml`, a package can declare:

```toml
[package]
name = "mykernel"
systems = true
```

The marker is retained for manifest compatibility only. Mako has no tracing GC
mode, and the `hold`/`share` ownership rules are enforced for every package.

---

## Checked Integer Arithmetic

Integer overflow is a common source of security vulnerabilities and silent
corruption. Mako provides checked arithmetic functions that return
`Result[int, string]` on overflow instead of wrapping or aborting:

```mko
fn safe_transfer(balance: int, amount: int) -> Result[int, string] {
    let new_balance = checked_sub(balance, amount)?
    return Ok(new_balance)
}

fn main() {
    match safe_transfer(100, 50) {
        Ok(v) => print_int(v),       // 50
        Err(e) => log_error(e),
    }

    // Check before performing the operation
    if would_overflow_add(9223372036854775807, 1) == 1 {
        print("would overflow, skipping")
    }
}
```

The checked functions:

| Function | Purpose |
|----------|---------|
| `checked_add(a, b)` | Add with overflow detection |
| `checked_sub(a, b)` | Subtract with overflow detection |
| `checked_mul(a, b)` | Multiply with overflow detection |
| `would_overflow_add(a, b)` | Returns 1 if add would overflow |
| `would_overflow_sub(a, b)` | Returns 1 if sub would overflow |
| `would_overflow_mul(a, b)` | Returns 1 if mul would overflow |

For blanket protection, use `mako build --overflow trap` which rewrites all
integer `+`, `-`, `*` to abort on overflow. The checked functions give you
finer-grained control where you want to handle overflow as a normal error path.

---

## Leak Detection

Mako provides a built-in leak detector for finding memory leaks during
development and testing. It tracks allocations at scope boundaries:

```mko
fn TestNoLeaks() {
    leak_scope_enter()

    let mut buf = make([]byte, 0, 1024)
    // ... use buf ...
    // buf freed at scope end

    let leaked = leak_scope_exit()
    assert_eq(leaked, 0)
}
```

For more detailed investigation:

```mko
fn main() {
    let mark = leak_mark()

    // ... code under test ...

    let bytes = leak_bytes_since(mark)
    if leak_check(mark) > 0 {
        print(leak_report_json())
    }
}
```

The leak detector is intended for testing and debugging. It has minimal overhead
but should not be relied upon in production builds. Use it in your test suite to
catch regressions early.

---

## Graceful Shutdown

Production servers need to drain in-flight requests before exiting. Mako
provides a shutdown lifecycle that integrates with signal handling:

```mko
fn main() {
    install_graceful_shutdown()
    let fd = http_bind(8080)

    while shutdown_requested() == 0 {
        let c = http_accept(fd)
        if c < 0 { continue }
        handle(c)
        let _ = http_close(c)
    }

    // Drain remaining connections
    server_shutdown_begin(5000)
    server_drain(5000)
    let _ = http_close_listener(fd)
    print("clean exit")
}
```

The shutdown functions handle the full lifecycle: signal registration, stop
accepting new connections, drain in-flight work, and clean exit. The HTTP-specific
variants (`http_shutdown_begin`, `http_shutdown_drain_conn`, etc.) give finer
control over per-connection drain when needed.

---

## Summary

Mako achieves speed through direct compilation to C with `-O3 -flto`, arena
allocation, low-overhead ownership, and no garbage collector. It achieves safety
through compile-time move analysis, runtime bounds checks, structured
concurrency, and explicit `unsafe` opt-out. The two goals reinforce each other:
the ownership system eliminates the need for a GC (speed) while preventing
use-after-free (safety). Bounds checks prevent overflows (safety); measure their
cost on the workload that matters (speed).

Next: [Cross-platform and WASI](ch12-cross-platform.md).
