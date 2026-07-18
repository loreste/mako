# 3. Language Tour

This chapter is a comprehensive tour of Mako's syntax and semantics. Sources use
the `.mko` extension. Every program begins with `fn main()`.

## Program structure

Top-level items in a Mako file: `pack`, `pull`, `export`, `fn`, `struct`,
`enum`, `actor`, `interface`, `const`, and `extern "C"`.

```mko
fn main() {
    print("hello")
}
```

Functions can appear in any order -- the compiler resolves them regardless of
declaration position in the file.

## Variables: let and let mut

Variables are declared with `let`. They are immutable by default.

```mko
fn main() {
    let x = 42           // immutable, type inferred as int
    let y: int = 10      // explicit type annotation
    let name = "mako"    // inferred as string

    // x = 99  // compile error: x is not mutable

    let mut counter = 0  // mutable variable
    counter = counter + 1
    print_int(counter)
}
```

Type annotations are optional when the type can be inferred from the
initializer. Prefer annotations on function signatures and omit them on locals
when the type is obvious.

## Primitive types

| Type | Description |
|------|-------------|
| `int` | Platform-native integer (maps to 64-bit in the C backend) |
| `int64` | 64-bit signed integer |
| `int32` | 32-bit signed integer |
| `int8` | 8-bit signed integer |
| `uint64` | 64-bit unsigned integer |
| `byte` | 8-bit unsigned (alias for uint8) |
| `float` | Floating-point (64-bit double) |
| `float64` | Explicit 64-bit float |
| `bool` | Boolean (`true` or `false`) |
| `string` | Immutable UTF-8 byte sequence |

```mko
fn main() {
    let a: int = 10
    let b: int64 = 1000000
    let c: int32 = 42
    let d: int8 = 7
    let e: uint64 = 99
    let f: byte = 65
    let g: float = 3.14
    let h: bool = true
    let s: string = "hello"

    print_int(a)
    print_int64(b)
    print_int32(c)
    print_int8(d)
    print_uint64(e)
    print_int(int(f))
}
```

There are no implicit conversions between numeric types. You must convert
explicitly.

## Type conversions

Use the target type as a function to convert:

```mko
fn main() {
    let a: int = 10
    let b = int64(a)       // int -> int64
    let c = int32(b)       // int64 -> int32
    let d = int8(c)        // int32 -> int8
    let e = uint64(a)      // int -> uint64
    let f = byte(65)       // int literal -> byte
    let g = float64(a)     // int -> float64
    let h = int(g)         // float64 -> int (truncates)

    // String conversions
    print(string(a))       // int -> string (decimal representation)
    print(string(b))       // int64 -> string

    // String <-> bytes
    let buf = bytes("hello")     // string -> []byte
    print(string(buf))           // []byte -> string
    let buf2 = []byte("world")   // alternative syntax
    print(string(buf2))
}
```

## Strings

Strings are immutable UTF-8 byte sequences. `len()` returns the byte length.
Indexing returns bytes. Use `rune_count()` for Unicode code point count.

```mko
fn main() {
    // Literals and escapes
    let s = "hi\tthere\n"
    print_int(len(s))        // byte length

    // Concatenation with +
    let t = "ma" + "ko"
    print(t)

    // Comparison — == and != work directly on strings
    if t == "mako" {
        print("equal")
    }
    if t != "other" {
        print("not equal")
    }

    // Unicode
    let u = "cafe\u0301"
    print_int(len(u))          // byte length
    print_int(rune_count(u))   // code point count

    // Byte indexing
    let hello = "hello"
    print_int(int(hello[0]))   // 104 (ASCII 'h')
    print_int(int(hello[1]))   // 101 (ASCII 'e')

    // Slicing (by byte offsets, yields string)
    print(hello[1:4])    // "ell"
    print(hello[:2])     // "he"
    print(hello[3:])     // "lo"
    print(hello[:])      // "hello"

    // Empty strings
    let empty = ""
    print_int(len(empty))  // 0

    // String helpers
    if str_eq("x", "x") {
        print("equal")
    }
    if str_contains("hello world", "world") {
        print("found")
    }

    // Range over string yields runes (index is byte offset)
    for i, r in range "abc" {
        print_int(i)
        print_int(r)
    }
}
```

## Arrays and slices

Slices (`[]T`) are dynamically-sized views into contiguous memory. They have a
length and capacity. Literal syntax creates a slice directly.

```mko
fn main() {
    // Slice literal
    let mut s = [1, 2, 3]
    print_int(len(s))     // 3
    print_int(cap(s))     // 3 (or more, implementation-defined)

    // Indexing (zero-based)
    print_int(s[0])       // 1
    print_int(s[2])       // 3

    // Mutation (requires let mut)
    s[0] = 99
    print_int(s[0])       // 99

    // Append (may reallocate)
    s = append(s, 4)
    print_int(len(s))     // 4
    print_int(s[3])       // 4

    // Slicing: s[low:high]
    let t = s[1:3]
    print_int(len(t))     // 2
    print_int(t[0])       // 2
    print_int(t[1])       // 3

    // Open-ended slicing
    let u = s[1:]         // from index 1 to end
    let v = s[:2]         // from start to index 2
    let w = s[:]          // full slice

    // Three-index slice: s[low:high:max] (controls capacity)
    let x = s[0:2:3]
    print_int(len(x))    // 2
    print_int(cap(x))    // 3

    // Pre-sized slices with make
    let mut buf = make([]int, 0, 100)   // len=0, cap=100
    buf = append(buf, 42)
    print_int(buf[0])

    // Byte slices
    let b: []byte = [65, 66, 67]
    print_int(int(b[0]))  // 65

    // String slices
    let names: []string = ["alice", "bob", "carol"]
    print(names[1])       // "bob"

    // Nested slices
    let grid: [][]int = [[1, 2], [3, 4]]
    print_int(grid[0][1]) // 2
    let mut rows = make([][]int, 0, 4)
    rows = append(rows, [10, 20])

    // Bool and enum slices
    let flags: []bool = [true, false]
    enum Color { Red, Green }
    let palette: []Color = [Red, Green]

    // Option / Result element slices
    let mut maybe = make([]Option[int], 0, 4)
    maybe = append(maybe, Some(1))
    maybe = append(maybe, None)
    match maybe[0] {
        Some(v) => print_int(v),
        None => print("none"),
    }
    let opts: []Option[int] = [Some(10), None]
    let mut tried = make([]Result[int, string], 0, 2)
    tried = append(tried, Ok(1))
    tried = append(tried, Err("no"))
}
```

Element types include `int`, `string`, `float`, `bool`, `byte`, named structs,
named enums, **`Option[T]`**, and **`Result[T,E]`**. Nested `[][]T` stores outer
headers of inner slices.

### Iterating over slices

```mko
fn main() {
    let xs = [10, 20, 30]

    // Index and value
    for i, v in range xs {
        print_int(i)
        print_int(v)
    }

    // Index only
    for i in range xs {
        print_int(i)
    }

    // Value only (blank index)
    for _, v in range xs {
        print_int(v)
    }

    // No binders (just iterate N times)
    let mut count = 0
    for range xs {
        count = count + 1
    }
    print_int(count)
}
```

## Maps

Maps (`map[K]V`) are hash tables. **Keys:** `int`, `string`, `float`, **`bool`**,
named **structs**, or named **enums**. **Values:** the same set, **slices**
`[]T`, **nested maps** `map[K2]V` (depth 2), or **bags** `Option[T]` /
`Result[T,E]` — any combination, including `map[Point]Label`, `map[Color]int`,
`map[string][]int`, `map[Point][]int`, `map[string]map[string]int`, set-style
`map[string]bool`, `map[bool]int`, `map[string]Option[int]`, and
`map[int]Result[string,string]`. Pack types work as keys or values. `[]bool`,
`[]Enum`, and nested `[][]T` slices are supported (make/append/index).

Float keys treat `+0.0` / `-0.0` as one key; all NaNs share one key.
Struct keys use field-wise equality and a stable field hash.

```mko
fn main() {
    // Create with make
    let mut m = make(map[string]int)
    m["a"] = 1
    m["b"] = 2

    // Access
    print_int(m["a"])     // 1
    print_int(len(m))     // 2

    // Check existence
    if has(m, "a") {
        print("has key a")
    }

    // Delete
    delete(m, "a")
    print_int(len(m))     // 1
    print_int(m["a"])     // 0 (zero value for missing keys)

    // Iterate
    for k, v in range m {
        print(k)
        print_int(v)
    }

    // Integer keys
    let mut mi = make(map[int]int)
    mi[10] = 100
    mi[20] = 200
    print_int(mi[10])

    // String values
    let mut ms = make(map[string]string)
    ms["greeting"] = "hello"
    print(ms["greeting"])

    // Float values / float keys
    let mut mf = make(map[int]float)
    mf[1] = 2.5
    let mut fi = make(map[float]int)
    fi[1.5] = 3

    // Struct values / struct keys
    struct Point { x: int, y: int }
    struct Label { text: string, id: int }
    let mut pts = make(map[int]Point)
    pts[1] = Point { x: 1, y: 2 }
    let mut pf = make(map[float]Point)
    pf[1.5] = Point { x: 3, y: 4 }
    let mut by_pt = make(map[Point]int)
    by_pt[Point { x: 1, y: 2 }] = 10
    let mut by_ss = make(map[Point]Label)
    by_ss[Point { x: 0, y: 0 }] = Label { text: "o", id: 0 }

    // Bool keys / set-style values
    let mut seen = make(map[string]bool)
    seen["a"] = true
    let mut by_b = make(map[bool]int)
    by_b[true] = 1

    // Enum keys / values
    enum Color { Red, Green }
    let mut by_e = make(map[Color]int)
    by_e[Red] = 1
    let mut statuses = make(map[int]Color)
    statuses[1] = Green

    // Slice values (groups) and named-key slice maps
    let mut groups = make(map[string][]int)
    groups["a"] = [1, 2, 3]
    let mut by_pt_rows = make(map[Point][]int)
    by_pt_rows[Point { x: 0, y: 0 }] = [10, 20]

    // Nested maps (depth 2): store an inner map pointer
    let mut nested = make(map[string]map[string]int)
    let mut row = make(map[string]int)
    row["x"] = 1
    nested["a"] = row
    print_int(nested["a"]["x"])  // 1

    // Bag values: Option / Result per key
    let mut maybe = make(map[string]Option[int])
    maybe["a"] = Some(42)
    maybe["b"] = None
    match maybe["a"] {
        Some(v) => print_int(v),
        None => {},
    }
    let mut tried = make(map[int]Result[string, string])
    tried[1] = Ok("yes")
    tried[2] = Err("no")

    // Comma-ok (missing → zero value / ok false)
    let v, ok = m["a"]
    if ok {
        print_int(v)
    }

    // Helpers (all map kinds)
    let ks = maps_keys(m)
    let vs = maps_values(m)
    let c = maps_clone(m)
    assert_eq(maps_equal(m, c), 1)
    maps_copy(c, m)
    maps_clear(c)

    // Pre-sized (hint for initial capacity)
    let mut big = make(map[string]int, 1024)
    big["x"] = 1

    // Nested-slice values and slice of maps
    let mut grids = make(map[string][][]int)
    grids["board"] = [[1, 0], [0, 1]]
    let mut table = make([]map[string]int, 0, 2)
    let mut row0 = make(map[string]int)
    row0["n"] = 1
    table = append(table, row0)
}
```

### Map kinds at a glance

| Need | Form |
|------|------|
| Scalar / set | `map[string]int`, `map[string]bool` |
| Groups | `map[K][]T` (e.g. `map[string][]int`) |
| Nested table | `map[K]map[K2]V` (depth 2) · `map[K]map[K2]map[K3]V` (depth 3) |
| Sparse grid | `map[string][][]int` |
| Optional / fallible per key | `map[K]Option[T]`, `map[K]Result[T,E]` |
| Optional whole map | `Option[map[K]V]`, `Result[map[K]V, E]` |
| Rows of maps | `[]map[K]V` |
| Channel per key | `map[K]chan[T]` (int/bool/float/string/struct) |
| Channels per key | `map[K][]chan[T]` |
| Optional / fallible channel | `Option[chan[T]]`, `map[K]Option[chan[T]]`, `Result[chan[T],E]` |
| Nested channel bags | `map[K][]Option[chan[T]]`, `map[K]Option[[]chan[T]]` |
| Nested channel slices | `map[K][][]chan[T]` |
| Channel + scalar pair | `map[K](chan[T], int)` / `(int, chan[T])` |
| Channel 3-tuple | `map[K](chan[T], int, int)` / mid / trailing channel |
| Nested optional | `Option[Option[T]]`, `map[K]Option[Option[chan[T]]]` |
| Mixed bag nests | `map[K]Option[Result[T,E]]`, `Option[Option[Option[T]]]`, `Result[Result[T,E],E]` |
| Nested bag slices | `map[K][]Option[Option[T]]`, `map[K]Option[[]Option[T]]`, `[]Result[Result[T,E],E]` |
| Bag-field tuples | `map[K](Option[T], U)`, `(Result[T,E], U)`, `(Option[chan[T]], int)` |

Missing key → zero value (`0` / `""` / `false` / empty slice / nil inner map /
`None` / `Err("")` / nil channel). Nested-map and channel-map `maps_clone` /
`maps_equal` are shallow (pointer identity for inner maps and channels).

Codegen monomorphizes only the map shapes your unit actually uses
(demand-driven), so a large package with many named types does not emit every
possible `map[StructA]StructB` helper.

Hands-on guide: [howto/10-collections.md](../../howto/10-collections.md) ·
low-ceremony patterns: [ERGONOMICS.md](../../ERGONOMICS.md).

## Structs

Structs are named collections of fields. `==` / `!=` compare field-wise
(strings by content). Enums also support structural `==` / `!=` (tag + payload).

```mko
struct Point {
    x: int,
    y: int,
}

struct Person {
    name: string,
    age: int,
}

fn main() {
    // Named struct literal
    let p = Point { x: 10, y: 20 }
    print_int(p.x)
    print_int(p.y)

    // Positional literal (fields in declaration order), Go-style
    let q = Point{3, 4}
    print_int(q.x)     // 3

    // Zero value — every field defaulted
    let z = Point{}
    print_int(z.x)     // 0

    // Mutable struct
    let mut person = Person { name: "Ada", age: 30 }
    person.age = 31
    print(person.name)
    print_int(person.age)

    // Structural equality (strings by content)
    let a = Point { x: 1, y: 2 }
    let b = Point { x: 1, y: 2 }
    if a == b {
        print("equal")
    }
    assert(Person { name: "Ada", age: 36 } == Person { name: "Ada", age: 36 })
}
```

> Positional literals are suppressed inside `if` / `for` / `while` / `switch`
> conditions, so `if p { … }` is still an identifier followed by a block. Wrap
> the literal in parentheses or a call if you need one in a condition.

### Nested structs

```mko
struct Addr {
    city: string,
    zip: int,
}

struct Person {
    name: string,
    addr: Addr,
}

fn main() {
    let mut p = Person {
        name: "Ada",
        addr: Addr { city: "Paris", zip: 75001 }
    }
    print(p.addr.city)
    print_int(p.addr.zip)

    p.addr.city = "Lyon"
    p.addr.zip = 69001
    print(p.addr.city)
}
```

### Methods on structs

Methods are defined inside an `on` block attached to the struct. The first
parameter `self` refers to the receiver instance:

```mko
struct Rect {
    w: int,
    h: int,
}

on Rect {
    fn area(self) -> int {
        return self.w * self.h
    }

    fn perimeter(self) -> int {
        return 2 * (self.w + self.h)
    }
}

fn main() {
    let r = Rect { w: 3, h: 4 }
    print_int(r.area())        // 12
    print_int(r.perimeter())   // 14
}
```

The older `fn Rect_area(self: Rect)` naming pattern still works but `on` blocks
are the preferred style -- they group related methods together and read more
naturally.

## Enums

Enums define a type with a fixed set of variants. Variants can carry data.

```mko
enum Shape {
    Circle(int),
    Rect(int, int),
    Point,
}

fn main() {
    let s = Circle(5)
    let r = Rect(3, 4)
    let p = Point

    print_int(area(s))
    print_int(area(r))
    print_int(area(p))
}

fn area(s: Shape) -> int {
    match s {
        Circle(r) => r * r,
        Rect(w, h) => w * h,
        Point => 0,
    }
}
```

### Enum methods

Enums support `on` blocks just like structs:

```mko
enum Shape {
    Circle(int),
    Rect(int, int),
    Point,
}

on Shape {
    fn area(self) -> int {
        match self {
            Circle(r) => r * r,
            Rect(w, h) => w * h,
            Point => 0,
        }
    }
}

fn main() {
    print_int(Circle(5).area())
    print_int(Rect(3, 4).area())
}
```

## Functions

Functions are declared with `fn`. Parameters must have type annotations. Return
type follows `->`.

```mko
fn add(a: int, b: int) -> int {
    return a + b
}

fn greet(name: string) -> string {
    return "hello, " + name
}

fn do_nothing() {
    // no return type means void
}

fn main() {
    print_int(add(2, 3))
    print(greet("world"))
}
```

### Tuples and multiple return values

Mako has tuple types for grouping values. A function can return a tuple and the
caller can destructure it with `let a, b = f()`:

```mko
fn divmod(a: int, b: int) -> (int, int) {
    return (a / b, a % b)
}

fn main() {
    let q, r = divmod(17, 5)
    print_int(q)    // 3
    print_int(r)    // 2
}
```

Tuple types are written `(T, U, ...)`. You can also bind them to a single
variable and access elements positionally:

```mko
fn pair() -> (string, int) {
    return ("hello", 42)
}

fn main() {
    let p = pair()       // p is (string, int)
    print(p.0)           // "hello"
    print_int(p.1)       // 42

    // Or destructure directly:
    let name, age = pair()
    print(name)
    print_int(age)
}
```

Tuples may hold named structs as well as primitives — including pack-qualified
structs after a `pull` (`let t, n = eng.grow_pair(t0, 1)`).

For more complex cases you can still use a struct, but tuples cover the common
multi-return pattern without boilerplate.

## Generics (0.2.0)

Functions, **structs**, and **enums** can be parameterized over types using
square-bracket syntax (angle brackets are dual sugar). The compiler
**monomorphizes** each instantiation — no runtime dictionaries.

```mko
fn identity[T](x: T) -> T {
    return x
}

fn first[T, U](a: T, b: U) -> T {
    return a
}

fn main() {
    print(identity(42))          // T = int
    print(identity("hi"))        // T = string
    print(first(1, "x"))         // T = int, U = string
}
```

### Generic structs and enums

Type arguments are required at construction sites:

```mko
struct Pair[T] {
    a: T
    b: T
}

struct Triple[A, B] {
    first: A
    second: B
    third: int
}

enum MyBox[T] {
    Val(T)
    Nothing
}

fn make_pair[T](a: T, b: T) -> Pair[T] {
    return Pair[T] { a: a, b: b }
}

fn wrap(v: int) -> MyBox[int] {
    return Val(v)
}

fn main() {
    let p = Pair[int] { a: 1, b: 2 }
    let t = Triple[string, float] { first: "x", second: 2.5, third: 7 }
    let q = make_pair("hi", "lo")
    match wrap(42) {
        Val(v) => print(v),
        Nothing => {},
    }
}
```

### Interface bounds

Constrain a type parameter with a structural interface:

```mko
interface Describable {
    fn describe(self) -> string
}

fn get_description[T: Describable](thing: T) -> string {
    return thing.describe()
}
```

The concrete type must provide the methods (via `on T { … }` or
`T_method` free functions). Missing methods are a compile error.

### Iterator protocol (seed)

If a type has `next() -> Option[T]` (codegen: `Type_next`), `for x in expr`
can call it. **Limitation:** by-value `self` does not advance the outer
iterator automatically — design `next` carefully or mutate explicitly.

### Mutable captures (seed)

Closures that assign to outer locals use a heap cell for those captures.
Simple by-value capture (`|x| x + n`) remains the common path.

## Closures (lambdas)

Closures use the `|args| body` syntax or `fn(args) { body }` for multi-line
bodies:

```mko
fn main() {
    let doubled = fan([1, 2, 3], |n| n * 2)
    for _, v in range doubled {
        print_int(v)
    }

    // Named-function style closure (useful for longer bodies)
    let squares = fan([4, 5, 6], fn(x) { x * x })
    for _, v in range squares {
        print_int(v)
    }
}
```

Closures can capture variables from their enclosing scope (by value by default;
see mutable-capture seed above when the body assigns to an outer local).

## Control flow

### if / else

```mko
fn classify(n: int) -> string {
    if n > 0 {
        return "positive"
    } else if n < 0 {
        return "negative"
    } else {
        return "zero"
    }
}
```

Conditions must be `bool` -- there is no truthy/falsy concept for integers or
strings.

**`if` is also an expression.** In value position each branch yields its trailing
expression, so you can bind or return the result directly. An `else` branch is
required, and both branches must yield the same type:

```mko
let label = if n > 0 { "positive" } else { "non-positive" }

fn classify(n: int) -> int {
    return if n < 0 { -n } else { n }
}
```

A branch may end in `return`/`break` instead of a value; the result then comes
from the other branch.

**if with an init clause** — declare a value used only by the `if`/`else`:

```mko
fn lookup(n: int) -> string {
    if v := n * 2; v > 10 {
        return "big"      // `v` is in scope here
    } else {
        return "small"    // ...and here
    }
    // `v` is not visible past the if
}
```

A function whose body always returns on every path is accepted even without a
trailing `return` — `if c { return a } else { return b }` is a complete body.

### while loops

```mko
fn main() {
    let mut i = 0
    while i < 5 {
        print_int(i)
        i = i + 1
    }
}
```

### for loops

Mako's `for` has four forms, matching Go:

```mko
fn main() {
    // C-style three-clause: init; condition; post
    for i := 0; i < 5; i++ {
        print_int(i)
    }

    // while-style: loop while a condition holds
    var n = 3
    for n > 0 {
        print_int(n)
        n--
    }

    // infinite loop (exit with break)
    var k = 0
    for {
        k++
        if k == 4 { break }
    }
}
```

In the C-style form the loop variable is scoped to the loop, the condition is
re-checked each iteration, and `continue` runs the post clause (so `i++` still
happens) before the next check.

### range loops

```mko
fn main() {
    // Integer range (0 to n-1)
    for i in range 5 {
        print_int(i)
    }

    // Slice iteration with index and value
    let xs = [10, 20, 30]
    for i, v in range xs {
        print_int(i)
        print_int(v)
    }

    // Map iteration
    let mut m = make(map[string]int)
    m["a"] = 1
    m["b"] = 2
    for k, v in range m {
        print(k)
        print_int(v)
    }

    // String iteration (yields runes)
    for i, r in range "hello" {
        print_int(i)
        print_int(r)
    }
}
```

### break and continue

```mko
fn main() {
    let mut i = 0
    while i < 10 {
        i = i + 1
        if i < 3 {
            continue    // skip to next iteration
        }
        if i > 5 {
            break       // exit the loop
        }
        print_int(i)   // prints 3, 4, 5
    }

    for j in range 8 {
        if j == 1 {
            continue
        }
        if j == 4 {
            break
        }
        print_int(j)   // prints 0, 2, 3
    }
}
```

### Labeled break

Use labels to break out of nested loops:

```mko
fn main() {
    let mut n = 0
    outer: while true {
        let mut j = 0
        while j < 3 {
            j = j + 1
            n = n + 1
            if n == 2 {
                break outer    // breaks the outer loop
            }
        }
    }
    print_int(n)    // 2
}
```

### match expressions

`match` is exhaustive -- the compiler requires all variants to be covered.

```mko
// Match on integers (requires _ wildcard)
fn classify(n: int) -> int {
    match n {
        0 => 100,
        1 => 200,
        _ => -1,
    }
}

// Multi-value match with |
fn bucket(n: int) -> int {
    match n {
        0 | 1 => 10,
        2 | 3 | 4 => 20,
        _ => -1,
    }
}

// Match on enums (all variants must be covered)
enum Color {
    Red,
    Green,
    Blue,
}

fn name(c: Color) -> string {
    match c {
        Red => "red",
        Green => "green",
        Blue => "blue",
    }
}

// Match on Result
fn handle(r: Result[int, string]) -> int {
    match r {
        Ok(v) => v,
        Err(e) => -1,
    }
}

// Match on Option
fn unwrap_or(o: Option[int], fallback: int) -> int {
    match o {
        Some(v) => v,
        None => fallback,
    }
}
```

### switch

`switch` is Go-style multi-way branching. Unlike `match`, cases take arbitrary
expressions, `default` is optional, and a case that matches nothing simply does
nothing (there is no fall-through):

```mko
fn label(n: int) -> string {
    switch n {
    case 1:
        return "one"
    case 2, 3:            // comma = multiple values
        return "few"
    default:
        return "many"
    }
}

fn sign(n: int) -> string {
    switch {              // expression-less: cases are conditions
    case n > 0:
        return "positive"
    case n < 0:
        return "negative"
    default:
        return "zero"
    }
}

fn describe(n: int) -> string {
    switch v := n * n; v {    // optional init clause
    case 0:
        return "zero"
    default:
        return "nonzero"
    }
}
```

Reach for `match` when you want exhaustiveness over an enum or `Result`/`Option`;
reach for `switch` for value dispatch and condition chains.

### defer

`defer` schedules a statement to run when the enclosing function exits, in
LIFO (last-in, first-out) order:

```mko
fn main() {
    defer print("third")
    defer print("second")
    defer print("first")
    print("body")
}
// Output:
// body
// first
// second
// third
```

Use `defer` for cleanup: closing files, releasing resources, printing logs.

## Operators

### Assignment

`=` is assignment only. It is never an expression.

```mko
let mut x = 0
x = 42
```

Compound assignment and increment/decrement work on variables, struct fields,
and index targets. They are shorthand for `target = target <op> value`:

```mko
var i = 0
i++                 // i = i + 1
i--                 // i = i - 1
i += 5              // also -=  *=  /=  %=

var xs = [1, 2, 3]
xs[0] += 10         // on an index

let mut p = Point{0, 0}
p.x++               // on a field
```

**Parallel binding and assignment** bind or update several targets at once. The
right-hand side is evaluated in full before any target is written, so a swap
needs no temporary:

```mko
var a, b = 1, 2     // parallel binding
a, b = b, a         // swap: a is now 2, b is now 1

var x, y, z = 1, 2, 3
x, y, z = z, x, y   // rotate

var p, q = pair()   // unpack a function's multiple return values
```

### Comparison operators

| Operator | Meaning |
|----------|---------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less than or equal |
| `>=` | Greater than or equal |

### Logical operators

| Operator | Keyword | Meaning |
|----------|---------|---------|
| `&&` | `and` | Logical AND (short-circuits) |
| `\|\|` | `or` | Logical OR (short-circuits) |
| `!` | `not` | Logical NOT |

```mko
if x > 0 && y > 0 {
    print("both positive")
}
if x == 0 || y == 0 {
    print("at least one zero")
}
if !done {
    print("still going")
}
```

### Arithmetic operators

| Operator | Meaning |
|----------|---------|
| `+` | Addition (also string concatenation) |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Division (integer division for int types) |
| `%` | Modulo |

### Bitwise operators

| Operator | Meaning |
|----------|---------|
| `&` | Bitwise AND |
| `\|` | Bitwise OR |
| `^` | Bitwise XOR (also unary complement: `^x`) |
| `&^` | Bit clear (AND NOT) |
| `<<` | Left shift |
| `>>` | Right shift |

```mko
fn main() {
    let flags = 0b1010
    let mask = 0b1100
    let result = (flags &^ mask) << 2
    print_int(result)
}
```

## Interfaces

Interfaces define a set of methods that types can implement:

```mko
interface Writer {
    fn write(string) -> int
}

fn Writer_write(s: string) -> int {
    print(s)
    return str_len(s)
}

fn main() {
    let n = Writer_write("hello")
    print_int(n)
}
```

## Packs & pulls

Mako's module system uses three keywords: `pack`, `pull`, and `export`.

**`pack`** declares the current file's package identity. Files that share the
same `pack` name form one logical unit:

```mko
pack mylib
```

**`pull`** imports another pack. Symbols are accessed through the pack name:

```mko
// Standard library — resolved from std/
pull "strings"           // strings.contains(...)

// Local file — qualifier is the file's pack name (or basename)
pull "./lib.mko"         // lib.add(...)

// Explicit alias
pull "./other.mko" as helper   // helper.greet(...)

// Grouped
pull (
    "path"
    "fmt"
    "./other.mko" as x
)
```

**`export`** marks items as visible to other packs:

```mko
pack mylib

export fn add(a: int, b: int) -> int { return a + b }
export struct Point { x: int, y: int }
```

Exported types are pack-qualified in annotations, return types, struct
literals, and struct patterns — same alias as function calls. Exported enums
accept pack (and pack+type) variant paths:

```mko
pull "./mylib.mko"

fn use(p: mylib.Point) -> int {
    return p.x + p.y
}

fn main() {
    let p: mylib.Point = mylib.Point { x: 0, y: 0 }
    match p {
        mylib.Point { x, y } => print_int(use(mylib.Point { x: x, y: y })),
    }
    let c = mylib.Color.Green(3)   // or mylib.Green(3) / mylib.Red
    match c {
        mylib.Red => {},
        mylib.Green(n) => print_int(n),
    }
}
```

Items without `export` are private to their pack. See
[Packages](ch10-packages.md) for the full module system reference.

Bare path names like `"strings"` resolve under `std/`.
`MAKO_STD` overrides the standard library root.

`mako fmt` groups two or more pulls and emits `pull` + `"path" as name`.

## Constants

```mko
const MAX_SIZE = 1024
const PI = 3.14159
const GREETING = "hello"

fn main() {
    print_int(MAX_SIZE)
}
```

## Option and Result

These are built-in generic types central to Mako's approach to nullability and
error handling.

```mko
// Option[T] -- represents a value that may or may not exist
fn find(xs: []int, target: int) -> Option[int] {
    for i, v in range xs {
        if v == target {
            return Some(i)
        }
    }
    return None
}

// Result[T, E] -- represents success or failure
fn parse_positive(n: int) -> Result[int, string] {
    if n <= 0 {
        return error("must be positive")
    }
    return Ok(n)
}

fn main() {
    match find([1, 2, 3], 2) {
        Some(idx) => print_int(idx),
        None => print("not found"),
    }

    match parse_positive(5) {
        Ok(v) => print_int(v),
        Err(e) => print(e),
    }
}
```

## Concurrent Maps (CMap)

For thread-safe key-value storage shared across concurrent tasks, use `CMap`:

```mko
fn main() {
    let m = cmap_new()
    cmap_set(m, "key", "value")
    print(cmap_get(m, "key"))       // "value"
    print_int(cmap_has(m, "key"))   // 1
    print_int(cmap_len(m))          // 1
    let n = cmap_incr(m, "hits", 1) // atomic increment -> 1
    print_int(n)
}
```

CMap uses a portable readers/writer gate internally: reads share the read side
and writes take the exclusive side. It can be shared across `crew` tasks
without wrapping in channels or mutexes; each operation is linearizable, but
separate operations do not establish cross-task ordering.

## Channels

Typed channels for communication between concurrent tasks:

```mko
fn main() {
    let ch = make(chan[int], 4)   // buffered channel, capacity 4
    send(ch, 42)
    let v = recv(ch)
    print_int(v)
}
```

## Visibility: `export`

By default, top-level items are private to their pack. Mark an item with
`export` to make it available to other packs that `pull` this one:

```mko
pack mathutil

export fn add(a: int, b: int) -> int {
    return a + b
}

export struct Vec2 {
    x: float,
    y: float,
}

// not exported — internal helper
fn clamp(n: int, lo: int, hi: int) -> int {
    if n < lo { return lo }
    if n > hi { return hi }
    return n
}
```

Only `export`ed functions and structs are visible to consumers; everything else
remains an internal implementation detail.

## Data parallelism: `fan`

`fan` applies a function to every element of a slice in parallel, distributing
work across available cores. It preserves order:

```mko
fn square(x: int) -> int {
    return x * x
}

fn main() {
    // With a named function
    let results = fan([1, 2, 3, 4], square)
    for _, v in range results {
        print_int(v)
    }

    // With an inline closure
    let doubled = fan([10, 20, 30], fn(x) { x * 2 })
    for _, v in range doubled {
        print_int(v)
    }
}
```

See [Concurrency](ch06-concurrency.md) for more on `fan` and crew blocks.

## Summary of built-in functions

| Function | Purpose |
|----------|---------|
| `print(x)` | Print any value to stdout (polymorphic -- works on strings, ints, etc.) |
| `print_int(n)` | Print int to stdout |
| `print_int64(n)` | Print int64 to stdout |
| `print_int32(n)` | Print int32 to stdout |
| `print_int8(n)` | Print int8 to stdout |
| `print_uint64(n)` | Print uint64 to stdout |
| `len(x)` | Length of slice, map, or string (bytes) |
| `cap(x)` | Capacity of slice |
| `append(s, v)` | Append to slice, returns new slice |
| `make(T, ...)` | Allocate slice, map, or channel |
| `has(m, k)` | Check if map contains key |
| `delete(m, k)` | Delete key from map |
| `assert(cond)` | Panic if condition is false |
| `str_eq(a, b)` | String equality (note: `==` also works on strings) |
| `str_contains(s, sub)` | Substring check |
| `str_len(s)` | String length (same as `len`) |
| `rune_count(s)` | Number of Unicode code points |
| `bytes(s)` | Convert string to `[]byte` |
| `string(x)` | Convert to string |
| `sort_ints(xs)` | Return sorted copy of int slice |
| `sort_strings(xs)` | Return sorted copy of string slice |

Next: [Ownership](ch04-ownership.md).
