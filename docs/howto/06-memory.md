# Memory Management

Mako has no garbage collector. Active memory/resource safety mechanisms include
ownership rules (`hold`/`share`) and region-based allocation (`arena`). This
guide explains when to use each strategy; generated C and FFI remain outside
the Mako type system.

**Deeper contracts:** ownership categories and SAFE/RT drop/escape rules are
in [SOUNDNESS.md](../SOUNDNESS.md) and [MEMORY_MODEL.md](../MEMORY_MODEL.md).
As of **0.3.0**, owning slices, maps, strings, and struct Own fields free at
scope exit, reassign, break/continue, return transfer, `?` early-return, and
**match** Own payloads. Free is **once** per allocation: live owns **move** into
a new freer; aliases and field/index borrows **clone**. Alias muts that start as
a view of another owner (`let mut out = path`) only free after they take Own
(conditional freer flag — no double-free with the caller).

## Default bindings

Regular `let` bindings are the simplest form. They work like stack values:

```mko
let x = 42              // immutable
let mut y = 10          // mutable
y = y + 1

let mut s = make([]int, 0, 8)
s = append(s, 1)        // s freed at end of scope

let v: string_view = "route"   // zero-copy view — never free
let owned = f"id={1}"          // owning string — free at scope exit
let w = str_as_view(owned)
```

For most local computation, this is all you need. Prefer `string_view` for
read-only hot paths; use `make([]T, 0, n)` when you will grow.

### Match and free

Matching on `Result` / `Option` Own payloads takes ownership of the payload for
the arm. The arm frees it unless you move it into a larger expression result:

```mko
fn load() -> Result[string, string] {
    return Ok("payload")
}

fn use_match() {
    match load() {
        Ok(s) => {
            // s freed at end of arm
            assert(str_len(s) > 0)
        },
        Err(e) => {
            // e freed at end of arm
            assert(str_len(e) > 0)
        },
    }
    // Move into a let — only the let frees
    let s = match load() {
        Ok(x) => x,
        Err(e) => e,
    }
}
```

### Explicit discard

`let _ = value` and `_ = value` destroy the resolved payload of a fresh
`Option` or `Result`. Common string, slice, map, struct, and nested-bag shapes
are supported. Bags borrowed from fields or indexes are left untouched so the
containing value remains valid. A debug compiler warns when it cannot resolve
the payload type and therefore cannot complete cleanup.

```mko
let _ = load()
```

### Alias mut reassign

```mko
fn branch(a: string, path: string) -> string {
    let mut out = path          // alias of path until reassigned
    if str_eq(a, "f") {
        out = "custom"          // out becomes freer of its Own buffer
    }
    return out                  // free only if out took Own (not the raw param)
}
```

## hold -- unique ownership

`hold` marks a binding as move-semantics. Once moved, the original is gone:

```mko
hold let x = 7
hold let y = x          // x is moved into y
print_int(y)            // 7
// print_int(x)         // COMPILE ERROR: use of moved value `x`
```

Moving into a function call also consumes the binding:

```mko
fn consume(n: int) -> int {
    return n * 2
}

hold let val = 42
print_int(consume(val))
// print_int(val)       // COMPILE ERROR: moved into consume
```

### Mutable hold

```mko
hold let mut x = 7
x = 9                   // allowed -- still owned
print_int(x)            // 9
```

### Partial moves on structs

Move individual fields while keeping the rest usable:

```mko
struct Point { x: int  y: int }

hold let p = Point { x: 1, y: 2 }
let px = p.x            // moves only x
print_int(p.y)          // y still usable
// print_int(p.x)       // COMPILE ERROR: x already moved
```

### Copy types

Integer and bool types are `Copy` -- they can be re-read after a hold binding
without consuming:

```mko
hold let n = 42
let a = n
let b = n              // fine -- int is Copy
print_int(a + b)       // 84
```

## share -- shared read access

`share` creates a read-only shared reference. While a share exists, the
original cannot be mutated:

```mko
hold let a = 1
share let s = share_int(a)
print_int(share_get(s))    // 1
// a = 5                   // COMPILE ERROR: cannot mutate while shared
share_drop(s)
// Now a is free again (if mut)
```

Rules enforced at compile time:

- `share let` is always immutable (no `share let mut`)
- Cannot assign to the shared source while a share is live
- Cannot create two shares of the same source simultaneously
- Share ends at `share_drop`, block exit, or last use (NLL)

## When to use hold vs share

| Situation | Use |
|-----------|-----|
| Value has one owner, passed linearly | `hold` |
| Multiple readers, no mutation needed | `share` |
| Short-lived local computation | plain `let` |
| Large allocation, bounded lifetime | `arena` |

Prefer `hold` (unique ownership) whenever possible. It adds no reference-counting
traffic and gives the compiler maximum freedom to optimize.

## Arenas -- region-based allocation

An arena allocates many objects cheaply and frees them all at once when the
scope exits:

```mko
fn main() {
    arena a {
        let msg = arena_text(a, "hello arena")
        print(msg)

        let xs = arena_ints(a, 4)    // 4 ints, zeroed
        print_int(len(xs))           // 4

        let stamp = arena_stamp(a, 99)
        print_int(stamp)             // 99
    }
    // Everything in `a` freed here -- one deallocation for the whole region
    print("arena done")
}
```

### Arena slices

Inside an arena block, `make` allocates from the arena automatically:

```mko
arena a {
    let mut s = make([]int, 3, 8)    // backed by arena memory
    s[0] = 10
    s[1] = 20
    s = append(s, 30)                // grows within the arena
    print_int(len(s))                // 4
}
```

### Arena structs

```mko
struct Point { x: int  y: int }

arena a {
    let mut pts = make([]Point, 0, 4)
    pts = append(pts, Point { x: 1, y: 2 })
    pts = append(pts, Point { x: 3, y: 4 })
    print_int(pts[0].x)             // 1
}
```

### When to use arenas

- Request-scoped work in a server (allocate for one request, free at end)
- Batch processing (parse a file, process, discard)
- Any bounded-lifetime allocation pattern

## Control flow and ownership

The compiler tracks ownership through branches and loops:

```mko
hold let x = 7

if some_condition() {
    let y = x          // moves x on this branch
} else {
    print_int(x)       // x still alive on this branch
}
// x may or may not be moved here -- compiler tracks both paths
```

Moves in a loop iteration are checked per-iteration:

```mko
hold let x = 42
while condition() {
    // Cannot move x here -- would be used-after-move on next iteration
    print_int(x)       // read of Copy type is fine
}
```

## Defer for cleanup

Use `defer` to ensure cleanup runs when a scope exits:

```mko
fn process() {
    let fd = open_resource()
    defer close_resource(fd)

    // ... work with fd ...
    // close_resource runs automatically on return
}
```

Defers execute in LIFO (last-in, first-out) order.

## Complete example: request handler with arena

```mko
fn handle_request(fd: int) {
    arena req {
        let mut buf = make([]byte, 0, 4096)
        let c = http_accept(fd)
        if c < 0 { return }

        let path = http_path(c)
        let body = http_body(c)

        if str_eq(path, "/data") {
            let _ = http_respond_json(c, 200, body)
        } else {
            let _ = http_respond(c, 404, "not found\n")
        }
        let _ = http_close(c)
    }
    // All request memory freed in one shot
}
```

## Summary

| Mechanism | Overhead | Lifetime | Use case |
|-----------|----------|----------|----------|
| `let` | None (stack) | Lexical scope | Local computation |
| `hold` | None (move) | Until moved | Linear resource passing |
| `share` | RC seed | Until drop/end | Multiple readers |
| `arena` | Bump alloc | Block scope | Batch/request work |

## Next steps

- [Concurrency patterns](05-concurrency.md) (channels move values between jobs)
- [Testing](08-testing.md)
- [Release builds](09-release-builds.md) (optimization interacts with ownership)
