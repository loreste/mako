# 4. Ownership: hold, share, and arenas

Mako has **no garbage collector**. Active memory/resource safety mechanisms
include compile-time ownership checks and runtime scope/region cleanup. They
prevent important classes of bugs; generated C and FFI remain outside the Mako
type system:

1. **Scope-based cleanup** -- local values are freed when their scope exits
2. **`hold` bindings** -- enforce move semantics with compile-time tracking
3. **`share` bindings** -- reference-counted shared reads
4. **Arena allocators** -- bulk allocation and single-point deallocation

This chapter covers each in detail.

## Scope-based cleanup: the default

Most values in Mako live on the stack or are heap-allocated and freed when their
enclosing scope exits. This is the default -- no annotation needed. Owning
strings, slices, maps, and struct Own fields free at scope exit, reassign,
break/continue, return transfer, `?` early-return, and **match** arm exit
(unless moved into a larger result). Live Own values **move** into a new freer;
aliases and field/index borrows **clone** so only one freer runs.

```mko
fn main() {
    let x = 42        // lives for the duration of main
    let s = "hello"   // same

    if true {
        let inner = 99   // lives only within this block
        print_int(inner)
    }
    // `inner` is gone here

    print_int(x)
}
```

For explicit cleanup ordering, use `defer`:

```mko
fn process() {
    defer print("cleanup done")
    print("processing")
    // "cleanup done" prints after "processing", before function returns
}
```

`defer` statements execute in LIFO order when the function exits:

```mko
fn main() {
    defer print("third")
    defer print("second")
    defer print("first")
    print("body")
}
// Output: body, first, second, third
```

## Copy types

Certain types are **Copy** -- they are duplicated rather than moved when
assigned or passed. These types do not need ownership annotations:

- `int`, `int64`, `int32`, `int8`, `uint64`
- `byte`
- `float`, `float64`
- `bool`

```mko
fn main() {
    let a = 42
    let b = a      // copies the value; both a and b are usable
    print_int(a)   // fine
    print_int(b)   // fine

    let f = 3.14
    let g = f      // copies
    print_int(int(f))
    print_int(int(g))
}
```

Copy types work the same way even under `hold` -- you can read them multiple
times because the "move" is really a copy:

```mko
fn main() {
    hold let x = 7
    print_int(x)    // first read
    print_int(x)    // second read -- OK because int is Copy
}
```

## `hold` -- move semantics

`hold` bindings enforce unique ownership. When a `hold` value is rebound, passed
to a function, or fully consumed, the original binding becomes **dead**. Using it
after that point is a compile error.

### Basic move

```mko
fn main() {
    hold let x = "hello"
    hold let y = x           // x is moved into y
    print(y)                 // OK
    // print(x)             // compile error: use of moved value `x`
}
```

### Move into function calls

Passing a `hold` value to a function is a consuming use:

```mko
fn consume(s: string) {
    print(s)
}

fn main() {
    hold let msg = "important"
    consume(msg)             // msg is moved into the function
    // consume(msg)          // compile error: use of moved value `msg`
}
```

### Single use is fine

If you only use a `hold` binding once, there is no issue:

```mko
fn id(n: int) -> int {
    return n
}

fn main() {
    hold let x = 42
    print_int(id(x))    // single consuming use -- OK
}
```

### Mutable hold bindings

You can reassign a `hold let mut` binding before it is moved:

```mko
fn main() {
    hold let mut x = 7
    x = 9               // reassignment before any move
    print_int(x)        // OK
}
```

### Partial struct moves

When a struct is under `hold`, you can move individual fields independently.
Only the moved field becomes dead:

```mko
struct Point {
    x: int,
    y: int,
}

fn main() {
    hold let p = Point { x: 1, y: 2 }
    let px = p.x        // moves only p.x
    print_int(px)       // OK
    print_int(p.y)      // OK -- p.y was never moved
    // print_int(p.x)   // compile error: p.x was moved
}
```

### Control flow and moves

The compiler tracks moves through all branches of `if/else` and `match`. A
value is considered moved only if it is moved on **all** reachable paths:

```mko
fn main() {
    hold let x = "hi"
    if 0 == 1 {
        let y = x       // moves x in this branch
        print(y)
    } else {
        print(x)        // uses x in this branch
    }
    // After the if/else, x MAY be moved (branch-dependent)
    // The compiler tracks this correctly
}
```

If one branch moves a value and another does not, the compiler understands that
after the if/else the value's status depends on which branch executed. It will
reject uses after the if/else because the move status is uncertain.

### Moves in loops

The compiler is aware of loop re-entry. If a value is moved inside a loop body,
it cannot be used on subsequent iterations:

```mko
fn main() {
    hold let x = "data"
    let mut i = 0
    while i < 1 {
        print(x)       // OK on first iteration
        i = i + 1
    }
    // x may be moved depending on loop execution
}
```

### Diverging branches

`return`, `break`, and `continue` are recognized as diverging -- a branch that
returns does not affect the move status in the remaining code:

```mko
fn process() -> int {
    hold let x = "value"
    if true {
        return 0          // diverges -- does not "use up" x for the else path
    }
    print(x)              // OK -- the if-branch returned, so we know x is live
    return 1
}
```

### Const-condition pruning

The checker prunes branches with constant-false conditions:

```mko
fn main() {
    hold let x = "hi"
    if false {
        let y = x         // dead code -- compiler ignores this move
    }
    print(x)              // OK
}
```

### Closures and hold

Closures that do not capture a `hold` binding leave it usable:

```mko
fn main() {
    hold let x = "hi"
    let ys = fan([1, 2], |n| n + 1)   // closure does not capture x
    print(x)                            // OK
    print_int(ys[0])
}
```

## `share` -- reference-counted shared reads

`share` provides immutable shared access to a value through reference counting.
Multiple `share` bindings can read the same value simultaneously.

### Basic sharing

```mko
fn main() {
    share let a = share_int(7)        // wrap an int in a share
    share let b = share_clone(a)      // bump reference count
    print_int(share_get(a))           // 7
    print_int(share_get(b))           // 7
    share_drop(a)                     // decrement refcount
    print_int(share_get(b))           // still 7 -- b holds a reference
    share_drop(b)                     // refcount hits zero, memory freed
}
```

### Rules of share

- `share let` bindings are always **immutable** -- there is no `share let mut`.
- Sharing a local blocks mutation of that local while the share is live.
- Shares end at: block end, mid-scope after last use (NLL), or explicit
  `share_drop`.

### Non-lexical lifetimes (NLL)

The compiler uses non-lexical lifetime analysis to end shares early. Once the
last use of a share is past, the source value can be mutated again:

```mko
fn main() {
    let mut x = 1
    share let s = share_int(x)
    print_int(share_get(s))    // last use of s
    // share ends here (NLL) even though s is still in scope
    x = 2                       // OK -- share is no longer live
    print_int(x)
}
```

### Share in control flow

```mko
fn main() {
    share let a = share_int(42)
    if true {
        print_int(share_get(a))
    }
    // share lives through the if block
    share_drop(a)
}
```

## When to use hold vs share

| Situation | Use |
|-----------|-----|
| Unique ownership, value consumed once | `hold` |
| Pass data to exactly one consumer | `hold` (move into call) |
| Multiple readers need the same value | `share` |
| Temporary shared read of a scalar | `share_int` / `share_clone` |
| You just need a local variable | plain `let` (no annotation needed) |

**General advice:** Start without ownership annotations. Use plain `let` and
`let mut` for everything. Add `hold` when you want the compiler to enforce that
a value is used exactly once (or transferred to exactly one owner). Add `share`
only when you genuinely need multiple readers of the same heap-allocated value.

## Arena allocators

Arenas provide region-based memory management. You allocate many objects within
an arena block, and they are all freed at once when the arena exits. This is
ideal for request-scoped work where you do not need individual deallocation.

### Basic arena usage

```mko
fn main() {
    arena a {
        let msg = arena_text(a, "hello arena")
        print(msg)

        let xs = arena_ints(a, 4)     // allocate 4 ints from arena
        print_int(len(xs))
        print_int(xs[0])              // zero-initialized

        print_int(arena_stamp(a, 99))
    }
    // everything allocated from `a` is freed here -- one deallocation
    print("arena done")
}
```

### Arena-backed slices

Use `make` inside an arena block to allocate slices from the arena instead of
the heap:

```mko
fn main() {
    arena a {
        let mut s = make([]int, 3, 8)    // len=3, cap=8, from arena
        s[0] = 10
        s[1] = 20
        print_int(len(s))
        print_int(s[0])

        let mut b = make([]byte, 2)
        b[0] = byte(65)
        print_int(int(b[0]))
    }
    print("freed")
}
```

### Arena-backed structs

```mko
struct Point {
    x: int,
    y: int,
}

fn main() {
    arena a {
        let mut xs = make([]Point, 2, 4)
        xs[0] = Point { x: 1, y: 2 }
        xs[1] = Point { x: 3, y: 4 }
        print_int(len(xs))
        print_int(xs[0].x)
        print_int(xs[1].y)
    }
}
```

### When to use arenas

Arenas shine when:

- You are processing a request and need many temporary allocations
- You know the lifetime of a group of objects is the same (they all die
  together)
- You want to avoid individual `free` calls and reduce allocator overhead
- You are building trees, buffers, or intermediate data structures that are
  discarded after one operation

Arenas do **not** work well when:

- Objects have different lifetimes
- You need to free individual objects before the arena exits
- You are building long-lived data structures that outlive a request

### Arena + hold pattern

You can combine arenas with hold for precise tracking:

```mko
fn main() {
    arena a {
        hold let mut buf = make([]int, 0, 64)
        buf = append(buf, 1)
        buf = append(buf, 2)
        process_buffer(buf)
        // buf is moved -- cannot use it again
    }
}

fn process_buffer(data: []int) {
    for _, v in range data {
        print_int(v)
    }
}
```

## Practical patterns

### Pattern: Builder with hold

Use `hold` to ensure a builder is consumed exactly once:

```mko
struct Config {
    host: string,
    port: int,
}

fn build_config(host: string, port: int) -> Config {
    hold let cfg = Config { host: host, port: port }
    return cfg    // ownership transferred to caller
}

fn main() {
    let cfg = build_config("localhost", 8080)
    print(cfg.host)
    print_int(cfg.port)
}
```

### Pattern: Transfer ownership through channels

```mko
fn main() {
    let ch = make(chan[string], 1)
    hold let msg = "important data"
    send(ch, msg)
    // msg is moved into the channel -- cannot use it here
    let received = recv(ch)
    print(received)
}
```

### Pattern: Arena per request

```mko
fn handle_request(body: string) {
    arena req {
        let mut parts = make([]string, 0, 16)
        // parse body into parts...
        parts = append(parts, body)
        print_int(len(parts))
    }
    // all request memory freed, regardless of how many allocations happened
}

fn main() {
    handle_request("hello")
    handle_request("world")
}
```

### Pattern: Scope-guarded resources with defer

```mko
fn process_file(path: string) -> Result[int, string] {
    let fd = open_file(path)?
    defer close_file(fd)

    // work with fd...
    // close_file runs automatically when function exits (success or error)
    Ok(1)
}
```

## Summary

| Mechanism | When to use | Cost |
|-----------|-------------|------|
| Plain `let` | Default for all locals | No ownership bookkeeping |
| `defer` | Cleanup on scope exit | Minimal (LIFO call) |
| `hold` | Enforce single-owner / move | No reference-counting traffic; move checks compile-time |
| `share` | Multiple readers of same data | Reference count increment/decrement |
| `arena` | Bulk temporary allocations | Bump allocator + single free |

The ownership system is designed so that the simple case requires no annotation.
Add `hold` and `share` incrementally where the compiler's help prevents bugs.
Use arenas when you know a group of allocations share a lifetime.

Next: [Errors & Result](ch05-errors.md).
