# 3. Language Tour

Mako feels like a practical backend language: braces, local inference,
familiar operators, and explicit types where they matter. Sources are `.mko`. Entry point
is `fn main()`.

## Program shape

Top-level items: `fn`, `struct`, `enum`, `actor`, `interface`, `const`,
`extern "C"`, `import`.

```mko
fn add(a: int, b: int) -> int {
    return a + b
}

fn main() {
    let n: int = 1
    print_int(add(n, 2))
}
```

Trailing expressions can return from a block when the type matches; prefer
explicit `return` while learning.

## Types

| Type | Notes |
|------|--------|
| `int` | Platform natural (checker-distinct; C backend → `int64_t`) |
| `int64` / `int32` / `int8` / `uint64` / `byte` | Fixed widths; no silent mix |
| `float` / `float64`, `bool`, `string` | No silent mix with ints |
| `[]T` | Slices (`[]int`, `[]byte`, …) |
| `map[K]V` | Maps |
| `Option[T]`, `Result[T, E]` | No nil |
| `chan[T]` | Typed channels |

Convert explicitly: `int64(a)`, `string(n)`, `bytes("hi")` / `[]byte(s)`.

```mko
let a: int = 10
let b = int64(a)
print(string(a))
let buf = bytes("hi")
print(string(buf))
```

## Operators

`=` is **assignment only**. Equality is `==`.

| Class | Operators |
|-------|-----------|
| Compare | `==` `!=` `<` `>` `<=` `>=` |
| Logical | `&&` `\|\|` `!` (also `and` / `or` / `not`) |
| Arithmetic | `+` `-` `*` `/` `%` |
| Bitwise | `&` `\|` `^` `&^` `<<` `>>` · unary `^x` |

`&&` / `||` **short-circuit**. `!!x` is two unary `!`.

```mko
if x == 0 || y > 1 {
    return
}
let bits = (flags &^ mask) << 2
assert(!!ok)
```

## Imports

```mko
import "strings"
import "./lib.mko" as lib

import (
    "path"
    "fmt"
    x "./other.mko"
)
```

Brace form `import { "a"; "b" }` also works. `mako fmt` groups two or more
imports into `import ( … )`. Bare names like `"strings"` resolve under `std/`
(override with `MAKO_STD`).

## Control flow

```mko
if n <= 1 {
    return n
} else {
    return n - 1
}

while i < n {
    i = i + 1
}

for i, v in range xs {
    print_int(v)
}

defer cleanup()
```

`match` is exhaustive on enums / `Option` / `Result`:

```mko
match parse_int(s) {
    Ok(v) => print_int(v),
    Err(_) => print("bad"),
}
```

## Slices and maps

```mko
let mut s = [1, 2, 3]
s = append(s, 4)
print_int(len(s))

let mut m = make(map[string]int, 4)
m["a"] = 1
```

Pre-size with `make([]T, 0, n)` / `make(map[K]V, n)` when you know capacity.

## Structs and enums

```mko
struct Point {
    x: int,
    y: int,
}

enum Shape {
    Circle(int),
    Rect(int, int),
    Point,
}

fn area(s: Shape) -> int {
    match s {
        Circle(r) => r * r,
        Rect(w, h) => w * h,
        Point => 0,
    }
}
```

Full reference: [GUIDE.md](../../GUIDE.md) · keywords: [KEYWORDS.md](../../KEYWORDS.md).

Next: [Ownership](ch04-ownership.md).
