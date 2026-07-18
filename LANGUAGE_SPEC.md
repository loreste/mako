# Mako Language Specification

**Version:** 0.2.3
**Date:** 2026-07-13
**Status:** Draft

This document is the formal specification for the Mako programming language. It
defines the lexical structure, type system, declarations, expressions, statements,
ownership model, concurrency primitives, error handling, module system, standard
library surface, compiler pipeline, and platform support.

Source files use the **`.mko`** extension.

**Preferred surface is Mako-native** (`fn`, `let`, `x: T`, `on Type`, `hold` /
`share` / `arena`, `crew` / `kick`). See [docs/IDENTITY.md](docs/IDENTITY.md).
Dual spellings (`func`, `:=`, bare `a int`) remain valid as compat sugar only.

---

## Table of Contents

1. [Lexical Structure](#1-lexical-structure)
2. [Types](#2-types)
3. [Declarations](#3-declarations)
4. [Expressions and Operators](#4-expressions-and-operators)
5. [Statements and Control Flow](#5-statements-and-control-flow)
6. [Ownership and Memory](#6-ownership-and-memory)
7. [Concurrency](#7-concurrency)
8. [Error Handling](#8-error-handling)
9. [Modules and Imports](#9-modules-and-imports)
10. [Standard Library Summary](#10-standard-library-summary)
11. [Compiler Pipeline](#11-compiler-pipeline)
12. [Platform Support](#12-platform-support)

---

## 1. Lexical Structure

### 1.1 Character Set

Mako source files are encoded in **UTF-8**. All keywords, operators, and
punctuation are within the ASCII subset. String literals and comments may contain
any valid UTF-8 sequence. A byte-order mark (BOM) at the start of a file is
rejected.

### 1.2 Line Structure

Mako is a free-form language. Statements are terminated by newlines or
semicolons. Semicolons are optional at end-of-line but permitted for
multi-statement lines.

### 1.3 Comments

Two forms of comments are supported:

```
// Line comment: from // to end of line

/* Block comment: may span
   multiple lines */
```

Block comments do **not** nest. A `/*` inside a block comment is ignored; the
first `*/` closes the comment.

Comments are stripped during lexing and do not appear in the token stream.

### 1.4 Identifiers

An identifier is a sequence of ASCII letters, digits, and underscores, beginning
with a letter or underscore:

```
identifier = ( letter | '_' ) { letter | digit | '_' }
letter     = 'a'..'z' | 'A'..'Z'
digit      = '0'..'9'
```

Identifiers are case-sensitive. The identifier `_` (a single underscore) is the
**blank identifier**; it discards a value and cannot be read.

### 1.5 Keywords

Mako has **46 reserved words** (including duals `func`, `var`, `package`,
`import`, `type`). Preferred flair: `fn`, `pack`, `pull`, `on`, `hold`, `crew`,
…. Every matching identifier is always a keyword token, never an `Ident`.
There are no contextual keywords.

#### Declaration Keywords

| Keyword     | Meaning                                          |
|-------------|--------------------------------------------------|
| `fn` / `func` | Function or interface method (`fn` preferred)  |
| `struct`    | Product type with named fields                   |
| `type`      | Dual type decl: `type T struct { … }`            |
| `enum`      | Sum type with variants                           |
| `actor`     | Actor type with `receive` arms                   |
| `receive`   | Actor message handler arm                        |
| `interface` | Named method set (light interfaces)              |
| `extern`    | Foreign declaration (`extern "C" fn ...`)        |
| `const`     | Compile-time constant binding                    |
| `pull` / `import` | Bring in another unit (`pull` preferred)   |
| `pack` / `package` | Unit name (`pack lib` preferred)          |
| `let` / `var` | Local binding (`let` preferred)                |
| `mut`       | Mutable parameter or binding marker              |
| `export`    | Explicit unit export                             |
| `on`        | Method block: `on T { fn … }`                    |

#### Control Flow Keywords

| Keyword    | Meaning                                    |
|------------|--------------------------------------------|
| `if`       | Conditional branch                         |
| `else`     | Alternative branch                         |
| `while`    | Loop while condition holds                 |
| `for`      | Iteration                                  |
| `in`       | Iterator binding separator                 |
| `range`    | Range expression for iteration             |
| `break`    | Exit innermost `for`/`while` loop          |
| `continue` | Skip to next iteration                     |
| `return`   | Return from function                       |
| `defer`    | Run on function exit (LIFO order)          |
| `match`    | Pattern match on enums, Option, Result     |

#### Literal and Logic Keywords

| Keyword | Meaning                                       |
|---------|-----------------------------------------------|
| `true`  | Boolean literal true                          |
| `false` | Boolean literal false                         |
| `and`   | Boolean AND (equivalent to `&&`)              |
| `or`    | Boolean OR (equivalent to `\|\|`)             |
| `not`   | Boolean NOT (equivalent to `!`)               |

#### Concurrency Keywords

| Keyword   | Meaning                                    |
|-----------|--------------------------------------------|
| `crew`    | Structured concurrency scope               |
| `kick`    | Spawn work on a crew                       |
| `join`    | Wait for a kicked job                      |
| `fan`     | Data-parallel map over a collection        |
| `select`  | Multi-way channel wait                     |
| `timeout` | Select arm: wait up to N milliseconds      |
| `default` | Select arm: non-blocking fallback          |

#### Memory and Ownership Keywords

| Keyword | Meaning                                      |
|---------|----------------------------------------------|
| `arena` | Bump-allocation region (freed on scope exit) |
| `hold`  | Move-on-rebind ownership binding             |
| `share` | Shared / read-only binding (RC seed)         |
| `as`    | Type or ownership cast helper                |

#### Complete Alphabetical List

```
actor and arena as break const continue crew default defer else enum export extern
false fan fn for func hold if import in interface join kick let match mut not on or
pack package pull range receive return select share struct timeout true type var while
```

### 1.6 Non-Keywords (Builtins)

The following are **not** keywords. They are ordinary identifiers or built-in
type names and may appear in user code as function calls or type annotations:

- Type names: `int`, `int8`, `int32`, `int64`, `uint64`, `byte`, `float`,
  `float64`, `string`, `void`
- Type constructors: `map`, `chan`, `Option`, `Result`
- Built-in functions: `len`, `cap`, `append`, `copy`, `make`, `print`,
  `print_int`, `assert`, `Ok`, `Err`, `error`, `errorf`
- Conversions: `int64(x)`, `int32(x)`, `int8(x)`, `uint64(x)`, `byte(x)`,
  `float64(x)`, `string(x)`, `bytes(s)`

### 1.7 Integer Literals

```
decimal_lit = '0' | ( '1'..'9' { digit } )
```

Untyped integer literals default to `int`. A literal may inhabit `int`, `int64`,
`int32`, `int8`, or `byte` when the context provides a type annotation.

Negative literals are expressed as unary minus applied to a positive literal:
`-42`.

### 1.8 Float Literals

```
float_lit = digits '.' digits [ exponent ]
exponent  = ( 'e' | 'E' ) [ '+' | '-' ] digits
```

Float literals always contain a decimal point. They produce values of type
`float` (64-bit IEEE 754).

Examples: `3.14`, `0.5`, `1.0e10`, `2.5E-3`

### 1.9 String Literals

String literals are enclosed in double quotes. The following escape sequences
are recognized:

| Escape | Meaning          |
|--------|------------------|
| `\n`   | Newline (0x0A)   |
| `\t`   | Horizontal tab   |
| `\r`   | Carriage return  |
| `\\`   | Backslash        |
| `\"`   | Double quote     |

```
string_lit = '"' { char | escape } '"'
```

String literals produce values of type `string` (owned UTF-8).

### 1.10 Boolean Literals

The keywords `true` and `false` are the only boolean literals. They produce
values of type `bool`.

### 1.11 Operators and Punctuation

Operators are listed in order of decreasing precedence in Section 4. Punctuation
tokens include:

```
( ) [ ] { } , : ; . .. -> => ? #
```

---

## 2. Types

### 2.1 Primitive Types

| Type     | Size       | Description                                    |
|----------|------------|------------------------------------------------|
| `int`    | 64-bit     | Platform natural integer; maps to `int64_t` on current 64-bit targets; distinct from `int64` in the type system |
| `int64`  | 64-bit     | Fixed-width signed 64-bit integer              |
| `int32`  | 32-bit     | Fixed-width signed 32-bit integer              |
| `int8`   | 8-bit      | Fixed-width signed 8-bit integer (-128..127)   |
| `uint64` | 64-bit     | Unsigned 64-bit integer                        |
| `byte`   | 8-bit      | Unsigned 8-bit integer (0..255); element type of `[]byte` |
| `float`  | 64-bit     | IEEE 754 double-precision floating-point; `float64` is an alias |
| `bool`   | 1 byte     | Boolean: `true` or `false`                     |
| `string` | ptr+len    | Owned UTF-8 text; immutable content, growable via concatenation |
| `void`   | 0          | Unit type; return type of procedures           |

#### Integer Family Rules

- Untyped integer literals default to `int`.
- Named integer kinds **do not mix** in arithmetic. Explicit conversion is
  required: `int64(x)`, `int32(x)`, `int8(x)`, `uint64(x)`, `byte(x)`, `int(x)`.
- `int8(x)` aborts at runtime if the value is outside `-128..127`.
- `uint64(x)` from a negative signed value aborts at runtime.
- Constant expressions are range-checked at compile time.

#### Float Rules

- `float` and `float64` are the same type.
- No silent mixing of `float` with any integer type. Explicit `float64(n)` is
  required.
- Truncation on `int(f)` from `float`.

### 2.2 Composite Types

#### Slices: `[]T`

A slice is a view into a contiguous array of elements. It consists of a pointer,
a length, and a capacity.

```
[]int          // slice of int
[]int64        // slice of int64
[]byte         // slice of bytes
[]string       // slice of strings
[]float        // slice of floats
[]bool         // slice of bools
[]Point        // slice of struct Point
[][]int        // nested slices (any supported element type)
```

Slice literals use bracket syntax:

```mko
let s = [1, 2, 3]                // []int
let a: []int64 = [10, 20, 30]    // []int64
let b: []byte = [72, 105]        // []byte
let names: []string = ["a", "b"] // []string
let flags = [true, false]        // []bool
let grid: [][]int = [[1, 2], [3]] // nested
```

Pre-sized allocation uses `make`:

```mko
let s = make([]int, 3, 8)    // len=3, cap=8
let z = make([]byte, 2)      // len=2, cap=2
let f = make([]bool, 0, 8)   // empty bool slice with capacity
let rows = make([][]int, 0, 4) // nested outer slice
```

Slice operations:

| Operation        | Result    | Description                           |
|------------------|-----------|---------------------------------------|
| `s[i]`           | `T`       | Index access (bounds-checked)         |
| `s[i:j]`         | `[]T`     | Sub-slice (shares backing store)      |
| `s[:j]`          | `[]T`     | From start to j                       |
| `s[i:]`          | `[]T`     | From i to end                         |
| `s[:]`           | `[]T`     | Full slice                            |
| `len(s)`         | `int`     | Number of elements                    |
| `cap(s)`         | `int`     | Capacity                              |
| `append(s, v)`   | `[]T`     | Append element, may grow; assign back |
| `copy(dst, src)` | `int`     | Copy elements, returns count copied   |

Out-of-bounds access aborts at runtime.

Element types include scalars, named structs/enums, nested `[][]T`, maps
(`[]map[K]V`), and bags **`[]Option[T]`** / **`[]Result[T,E]`** (make, append,
index, range, annotated literals).

#### Maps: `map[K]V`

Hash maps with open addressing. Supported key types: `string`, `int`,
**`float`**, **`bool`**, **named structs**, and **named enums** (including
pack-qualified types). Supported value types: the same set **plus slices**
`[]T` / `[][]T` (e.g. `map[string][]int`, `map[string][][]int`,
`map[Point][]int`), **nested maps** `map[K2]V` (depth 2 only), or
**bags** `Option[T]` / `Result[T,E]` (scalar/struct/enum payload) — any
key×value combination of those (including `map[Struct]Struct`, `map[Enum]V`,
`map[K]Enum`, `map[Struct|Enum][]T`, `map[K][][]T`,
`map[string]map[string]int`, `map[string]map[string][]int`, `[]map[K]V`,
`map[K][]map[K2]V`, set-style `map[K]bool`, `map[bool]V`,
`map[string]Option[int]`, `map[int]Result[string,string]`,
`map[string][]Option[int]`, `map[int][]Result[string,string]`,
`map[string]Option[[]int]`, `map[int]Result[[]int,string]`,
`map[string]Option[map[string]int]`, `map[int]Result[map[string]int,string]`,
and `map[string](int, int)` / `map[int](string, float)` scalar tuples).
Nested-map values are pointers: a missing outer key yields a nil map
(`len` is 0); `maps_clone` / `maps_equal` are shallow. Missing bag value →
zero bag (`None` / `Err("")`).

Float keys: `+0.0` and `-0.0` are the same key; all NaN values share one key.
Struct keys use field-wise equality and a stable hash over fields (string
fields by content).

```mko
let mut m = make(map[string]int)
let mut mi = make(map[int]int)
let mut ms = make(map[string]string)
let mut mf = make(map[int]float)
let mut msf = make(map[string]float)
let mut fi = make(map[float]int)
let mut ff = make(map[float]float)
let mut pts = make(map[int]Point)       // struct values
let mut by_name = make(map[string]Point)
let mut by_f = make(map[float]Point)   // float key + struct value
let mut by_pt = make(map[Point]int)    // struct keys
let mut by_pt_s = make(map[Point]string)
let mut by_pt_f = make(map[Point]float)
let mut by_ss = make(map[Point]Label)  // struct key + struct value
let mut seen = make(map[string]bool)   // set-style
let mut by_b = make(map[bool]int)      // bool keys
let mut by_e = make(map[Color]int)     // enum keys
let mut statuses = make(map[int]Color) // enum values
let mut groups = make(map[string][]int) // slice values
let mut by_pt_rows = make(map[Point][]int) // named key + slice value
let mut by_e_rows = make(map[Color][]string)
let mut nested = make(map[string]map[string]int) // nested maps (depth 2)
let mut maybe = make(map[string]Option[int]) // bag values
let mut tried = make(map[int]Result[string, string])
let mut pack_m = make(map[int]eng.Table) // after pull
let mut pack_f = make(map[float]eng.Table)
let mut pack_k = make(map[eng.Table]int) // pack type as key
let mut pack_ss = make(map[eng.Table]Point)
```

Map operations:

| Operation              | Result    | Description                        |
|------------------------|-----------|------------------------------------|
| `make(map[K]V)`        | `map[K]V` | Allocate new map                  |
| `make(map[K]V, hint)`  | `map[K]V` | Allocate with size hint           |
| `m[k] = v`             | —         | Insert or update                  |
| `m[k]`                 | `V`       | Get value (zero value if missing) |
| `let v, ok = m[k]`     | `V, bool` | Comma-ok pattern                 |
| `has(m, k)`            | `bool`    | Key presence test                 |
| `delete(m, k)`         | —         | Remove entry                      |
| `len(m)`               | `int`     | Entry count                       |
| `for k, v in range m`  | —         | Iteration (order unspecified)     |
| `maps_keys(m)`         | `[]K`     | All keys (incl. struct / float)   |
| `maps_values(m)`       | `[]V`     | All values                        |
| `maps_clear(m)`        | —         | Remove all entries                |
| `maps_clone(m)`        | `map[K]V` | Shallow clone                     |
| `maps_equal(a, b)`     | `int`     | 1 if same keys/values             |
| `maps_copy(dst, src)`  | —         | Copy entries into `dst`           |

Wrong key/value combo is rejected at compile time. `maps_*` works for all
supported map kinds (including float keys/values and struct keys/values).
Struct map equality (keys or values) is **structural** (field-wise; strings by
content).

#### Channels: `chan[T]`

Typed, buffered channels for inter-task communication.

```mko
let ch = chan_new(4)           // chan[int] with buffer size 4
let cs = make(chan[string], 2)
let cp = make(chan[Point], 1)  // structs (same as chan_open[Point](1))
let ct = make(chan[(int, string)], 1)
let ce = chan_open[eng.Point](1)
```

`make(chan[T], n)` and `chan_open[T](n)` accept int family, `bool`, `float`,
`string`, **named structs**, **named enums**, and **tuples** (including
pack-qualified types).

`chan_len(ch)` and `chan_cap(ch)` accept any `chan[T]` (not only `chan[int]`).

Channel operations are described in Section 7.

#### Structs

Product types with named fields. See Section 3.4.

#### Enums

Sum types with variants. See Section 3.5.

### 2.3 Generic Types

Mako provides built-in generic types using square-bracket parameterization:

#### `Option[T]`

Represents an optional value. No null pointers exist in Mako.

```mko
Option[int]       // optional integer
Option[string]    // optional string
```

Variants:

- `Some(value)` — contains a value of type `T`
- `None` — absence of a value

Must be consumed via `match`; cannot be used as a bare value.

#### `Result[T, E]`

Represents either a success value or an error.

```mko
Result[int, string]     // success int or error string
```

Variants:

- `Ok(value)` — success value of type `T`
- `Err(error)` — error value of type `E`

An unused `Result` as a statement expression is a **compile error**. Must be
consumed via `match`, `?`, or `let _ = ...`.

#### `Job[T]`

Represents a handle to a concurrent job spawned by `kick`. The return type of
`t.kick(expr)`.

```mko
Job[int]      // job that produces an int
```

The `.join()` method blocks until the job completes and returns the result
value of type `T`.

#### `List[T]`

Generic list type (angle-bracket form also accepted):

```mko
List[int]
```

#### User-defined generic types (0.1.9)

In addition to built-ins, **structs and enums** may declare type parameters
(Section 3.4 / 3.5). Instantiations are monomorphized at compile time.

```mko
struct Pair[T] { a: T, b: T }
enum Tree[T] { Leaf(T), Empty }
```

### 2.4 Special Types

| Type             | Description                                |
|------------------|--------------------------------------------|
| `Crew`           | Structured concurrency scope handle        |
| `Arena`          | Bump allocator handle                      |
| `StrBuilder`     | Growable string buffer                     |
| `Mutex`          | Mutual exclusion lock                      |
| `RWMutex`        | Readers-writer mutex                       |
| `CMap`           | Concurrent hash map                        |
| `MMap`           | Memory-mapped file handle                  |
| `EvLoop`         | Event loop handle                          |
| `Buf`            | Binary buffer for protocol encoding        |
| `GameUDP`        | Game UDP socket handle                     |
| `CHash`          | Consistent hash ring                       |
| `RateLimiter`    | Token bucket rate limiter                  |
| `CircuitBreaker` | Circuit breaker state machine              |
| `HttpEngine`     | High-level HTTP server engine              |
| `BufReader`      | Buffered reader                            |
| `BufWriter`      | Buffered writer                            |
| `HttpRequest`    | Typed HTTP request                         |
| `SqlDB`          | Unified SQL database handle                |
| `WaitGroup`      | Concurrency barrier                        |
| `Uuid`           | RFC 4122 UUID (16 bytes)                   |

### 2.5 Function Types

```
fn(ParamTypes...) -> ReturnType
```

Function types describe callable values, including lambdas:

```mko
let f: fn(int) -> int = |x| x * 2
```

### 2.6 Type Conversions

Explicit conversions use the target type name as a function call:

```mko
let a: int = 10
let b = int64(a)       // int -> int64
let f = float64(a)     // int -> float
let s = string(a)      // int -> string "10"
let buf = bytes("hi")  // string -> []byte
let txt = string(buf)  // []byte -> string
```

Conversion table (rows = source, columns = target):

| From / To  | int family | int8       | uint64      | byte       | float64    | string     | []byte      |
|------------|------------|------------|-------------|------------|------------|------------|-------------|
| int family | `int(x)`   | `int8(x)`  | `uint64(x)` | `byte(x)` | `float64(x)` | `string(x)` | —         |
| float64    | truncates  | via int8   | via uint64  | via byte   | identity   | —          | —           |
| string     | —          | —          | —           | —          | —          | identity   | `bytes(s)`  |
| []byte     | —          | —          | —           | index      | —          | `string(b)` | identity   |

### 2.7 Copy Types

The following types have **Copy semantics**: `int`, `int64`, `int32`, `int8`,
`uint64`, `byte`, `float`, `bool`. Values of these types are always copied on
assignment, argument passing, and return. They are never moved, even under
`hold` bindings.

### 2.8 Zero Values

When a map lookup misses, or a variable is declared without initialization in
certain contexts, zero values apply:

| Type     | Zero Value |
|----------|------------|
| `int`    | `0`        |
| `float`  | `0.0`      |
| `bool`   | `false`    |
| `string` | `""`       |

---

## 3. Declarations

### 3.1 Variable Declarations

#### Immutable Binding

```mko
let x = 1
let y: int64 = 42
```

A `let` binding is immutable after initialization. The type may be inferred
from the initializer or explicitly annotated.

#### Mutable Binding

```mko
let mut y = 2
y = y + 1
```

The `mut` modifier allows reassignment of the variable. Only `let mut`
bindings may appear on the left side of an assignment.

#### Constants

```mko
const PORT = 8080
const MAX = 10 * 100 + 80 / 2
```

`const` bindings are compile-time constants. The initializer must be a
constant expression (integer arithmetic on literals and other constants).
Constants are folded at compile time.

### 3.2 Function Declarations

```
fn name(param: Type, param: Type) -> ReturnType {
    body
}
```

Functions are declared with the `fn` keyword:

```mko
fn add(a: int, b: int) -> int {
    return a + b
}
```

#### Type parameters and bounds (0.1.9)

User generics use square brackets (angle brackets are dual sugar). The compiler
**monomorphizes** each concrete instantiation — no runtime dictionary.

```mko
fn identity[T](x: T) -> T {
    return x
}

// Interface bound: T must provide the methods of Describable (structural).
fn get_description[T: Describable](thing: T) -> string {
    return thing.describe()
}
```

- Type args may be inferred at call sites (`identity(42)` → `T = int`) or
  written explicitly where required by context.
- Bounds are checked against the concrete type’s method set (`on T { … }` /
  `T_method` free functions). Missing methods are a **type error**.
- Dual syntax: `fn id<T>(x: T) -> T` is accepted as sugar.

#### Return Types

- Explicit `-> ReturnType` specifies the return type.
- Omitted return type implies `void` (procedure).
- A non-void function **must** have a `return` statement or a trailing
  expression on all code paths. Missing return is a type error.

#### Trailing Expression Return

When the last expression in a function body is not followed by a semicolon or
newline-then-statement, and the function has a non-void return type, the
expression value is implicitly returned:

```mko
fn area(w: int, h: int) -> int {
    match Ok(w * h) {
        Ok(v) => v,
        Err(e) => 0,
    }
}
```

#### Mutable Parameters

Parameters may be declared `mut` to allow mutation within the function body:

```mko
fn inc(mut n: int) -> int {
    n = n + 1
    return n
}
```

A shared local cannot be passed into a `mut` parameter while the share is live.

#### Lambdas

Anonymous functions use pipe-delimited parameter syntax:

```mko
let f = |x| x * 2
let g = |a, b| a + b
```

Lambdas capture variables from the enclosing scope. Capture follows ownership
rules: a `hold` variable moved into a lambda is consumed.

### 3.3 Method Declarations

Methods are declared as free functions with a naming convention:

```mko
fn TypeName_methodName(self: TypeName, params...) -> ReturnType {
    body
}
```

The first parameter named `self` enables dot-call syntax on the receiver:

```mko
fn Point_distance(self: Point) -> int {
    return self.x + self.y
}

// Called as:
let d = p.distance()   // desugars to Point_distance(p)
```

Methods without `self` are called via the receiver's type:

```mko
fn Writer_write(s: string) -> int {
    print(s)
    return str_len(s)
}
```

### 3.4 Struct Declarations

```mko
struct Name {
    field1: Type
    field2: Type
}

// Generic (0.1.9): monomorphized to one C struct per concrete args
struct Name[T, U] {
    field1: T
    field2: U
}
```

Examples:

```mko
struct Point {
    x: int
    y: int
}

struct Addr {
    city: string
    zip: int
}

struct Person {
    name: string
    addr: Addr
}

struct Pair[T] {
    a: T
    b: T
}

struct Triple[A, B] {
    first: A
    second: B
    third: int
}
```

Struct construction uses field-value syntax. **Generic structs require type
arguments** at the construction site:

```mko
let p = Point { x: 3, y: 4 }
let person = Person {
    name: "Ada",
    addr: Addr { city: "Paris", zip: 75001 },
}
let pair = Pair[int] { a: 1, b: 2 }
let mixed = Triple[string, float] { first: "x", second: 2.5, third: 7 }
// Nested monomorphs: Box[Pair[int]]
```

Internally, monomorph names look like `Pair__int` / `Triple__string__float`
(implementation detail — write the surface form `Pair[int]`).

Field access and mutation (when the binding is `mut`):

```mko
print_int(p.x)
let mut p2 = Point { x: 1, y: 2 }
p2.x = 10
person.addr.city = "Lyon"    // nested field assign
```

Structs may be used as slice elements:

```mko
let mut xs = make([]Point, 0, 4)
xs = append(xs, Point { x: 1, y: 2 })
let lit: []Point = [Point { x: 7, y: 8 }]
```

#### JSON Derivation

The `#[derive(json)]` attribute generates JSON serialization methods:

```mko
#[derive(json)]
struct Person {
    name: string
    age: int
}

// Generates:
// Person_to_json(name, age) -> string
// Person_name_from_json(json) -> string
// Person_age_from_json(json) -> int
```

### 3.5 Enum Declarations

```mko
enum Name {
    Variant,              // unit variant
    Variant(Type),        // single-payload variant
    Variant(Type, Type),  // multi-payload variant
}

// Generic (0.1.9)
enum Name[T] {
    Val(T),
    Nothing,
}
```

Examples:

```mko
enum Shape {
    Circle(int),
    Rect(int, int),
    Point,
}

enum MyBox[T] {
    Val(T),
    Nothing,
}
```

Enum values are constructed by variant name. Generic enums monomorphize like
structs; match works on the monomorphized variants:

```mko
let c = Circle(5)
let r = Rect(3, 4)
let p = Point

fn wrap(v: int) -> MyBox[int] {
    return Val(v)
}

fn main() {
    match wrap(42) {
        Val(v) => print(v),
        Nothing => {},
    }
}
```

Enums are consumed via `match` (see Section 5.6). Match must be **exhaustive**:
all variants must be covered.

#### Enum Methods

Methods on enums follow the same convention as struct methods:

```mko
fn Shape_area(self: Shape) -> int {
    match self {
        Circle(r) => r * r,
        Rect(w, h) => w * h,
        Point => 0,
    }
}

// Called as:
print_int(Circle(5).area())
```

### 3.6 Interface Declarations

```mko
interface Name {
    fn method(params...) -> ReturnType
}
```

Interfaces define a set of required methods. Implementations are provided as
free functions using the naming convention `InterfaceName_methodName` or
`InterfaceName_ConcreteType_methodName` for multi-concrete dispatch.

```mko
interface Writer {
    fn write(string) -> int
}

// Implementation
fn Writer_write(s: string) -> int {
    print(s)
    return str_len(s)
}
```

#### Dynamic Dispatch

An interface value boxes a concrete receiver as a fat pointer:

```mko
let w: Writer = concrete_value    // boxes the concrete value
w.write("hello")                  // dynamic dispatch via vtable
```

Multi-concrete implementations use qualified names:

```mko
fn Adder_Counter_add(self: Counter, delta: int) { ... }
fn Adder_Doubler_add(self: Doubler, delta: int) { ... }
```

No-self interfaces use a unit vtable for dynamic dispatch.

### 3.7 Actor Declarations

Actors are message-processing entities with isolated state:

```mko
actor Name {
    receive MessageType {
        body
    }
    receive AnotherMessage {
        body
    }
}
```

Example:

```mko
actor Session {
    receive Invite { print("invite") }
    receive Timer  { print("tick") }
    receive Bye    { print("bye") }
}
```

Actors desugar to a mailbox and a `crew` loop. The `Bye` or `Stop` variant
ends the loop by convention.

Actor operations:

```mko
let session = Session_spawn()                 // spawn actor
let _ = Session_send(session, Session_Invite()) // send message
let loopj = t.kick(Session_loop(session))     // run actor loop in crew
```

### 3.8 Foreign Declarations

```mko
extern "C" fn function_name(params...) -> ReturnType
```

Declares a function with C linkage. The function body is provided by a C source
file linked at compile time.

```mko
extern "C" fn mako_c_abs(n: int) -> int
extern "C" fn mako_c_add(a: int, b: int) -> int
```

---

## 4. Expressions and Operators

### 4.1 Operator Precedence

Operators are listed from highest to lowest precedence:

| Precedence | Operators                    | Associativity | Description       |
|------------|------------------------------|---------------|-------------------|
| 1          | `()` `[]` `.` `?`            | left          | Grouping, index, field access, error propagation |
| 2          | `!` `not` `^` (unary) `-` (unary) | right    | Unary operators   |
| 3          | `*` `/` `%`                  | left          | Multiplicative    |
| 4          | `+` `-`                      | left          | Additive          |
| 5          | `<<` `>>`                    | left          | Shift             |
| 6          | `&`                          | left          | Bitwise AND       |
| 7          | `^`                          | left          | Bitwise XOR       |
| 8          | `\|`                         | left          | Bitwise OR        |
| 9          | `&^`                         | left          | Bit clear (AND NOT) |
| 10         | `==` `!=` `<` `>` `<=` `>=`  | left          | Comparison        |
| 11         | `&&` `and`                   | left          | Logical AND       |
| 12         | `\|\|` `or`                  | left          | Logical OR        |
| 13         | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | right | Assignment |

### 4.2 Arithmetic Operators

| Operator | Description    | Operand Types           |
|----------|----------------|-------------------------|
| `+`      | Addition       | `int`, `float`, `string` (concatenation) |
| `-`      | Subtraction    | `int`, `float`          |
| `*`      | Multiplication | `int`, `float`          |
| `/`      | Division       | `int` (truncating), `float` |
| `%`      | Modulo         | `int`                   |

Arithmetic operands must be the same type. No implicit coercion between
integer kinds or between `int` and `float`.

String concatenation with `+` produces a new `string`.

### 4.3 Comparison Operators

| Operator | Description              |
|----------|--------------------------|
| `==`     | Equal                    |
| `!=`     | Not equal                |
| `<`      | Less than                |
| `>`      | Greater than             |
| `<=`     | Less than or equal       |
| `>=`     | Greater than or equal    |

Comparison always produces `bool`. Strings support only `==` and `!=` (no
ordering operators on strings; content equality). **Named structs** support
`==` / `!=` field-wise (strings by content; nested structs recursive).
**Enums** support `==` / `!=` on tag plus payload. Assignment is `=` only;
`=` is never equality.

### 4.4 Logical Operators

| Operator     | Description           |
|--------------|-----------------------|
| `&&` / `and` | Logical AND (short-circuit) |
| `\|\|` / `or` | Logical OR (short-circuit) |
| `!` / `not`  | Logical NOT           |

The word forms `and`, `or`, `not` are equivalent to `&&`, `||`, `!` respectively.
Both forms may be used interchangeably.

`&&` and `||` are **short-circuit**: the right operand is not evaluated when the
result is determined by the left operand alone.

`!!x` is two unary `!` operators applied sequentially. There is no special `!!`
token.

### 4.5 Bitwise Operators

| Operator | Description           |
|----------|-----------------------|
| `&`      | Bitwise AND           |
| `\|`     | Bitwise OR            |
| `^`      | Bitwise XOR (binary), bitwise complement (unary) |
| `&^`     | Bit clear (AND NOT)   |
| `<<`     | Left shift            |
| `>>`     | Right shift           |

Unary `^x` is the bitwise complement of `x`.

The leading `|...|` in a lambda is disambiguated from infix `|` by the parser.

### 4.6 Assignment Operators

| Operator | Description                |
|----------|----------------------------|
| `=`      | Simple assignment          |
| `+=`     | Add and assign             |
| `-=`     | Subtract and assign        |
| `*=`     | Multiply and assign        |
| `/=`     | Divide and assign          |
| `%=`     | Modulo and assign          |
| `&=`     | Bitwise AND and assign     |
| `\|=`    | Bitwise OR and assign      |
| `^=`     | Bitwise XOR and assign     |
| `<<=`    | Left shift and assign      |
| `>>=`    | Right shift and assign     |

Assignment targets must be `let mut` bindings, mutable struct fields, slice
elements, or map entries. Index targets may be chained through slice views and
nested slices (`s[1:3][0] = value`, `matrix[i][j] = value`); each base and index
expression is evaluated once, with the normal bounds, mutability, NLL, and race
checks still enforced.

### 4.7 Special Operators

| Operator | Description                                    |
|----------|------------------------------------------------|
| `?`      | Error propagation (see Section 8)              |
| `->`     | Return type annotation in function signatures  |
| `=>`     | Match arm separator                            |
| `..`     | Range (future extension)                       |

### 4.8 String Operations

| Expression           | Result   | Description                    |
|----------------------|----------|--------------------------------|
| `s + t`              | `string` | Concatenation                  |
| `s == t` / `s != t`  | `bool`   | Equality comparison            |
| `len(s)` / `str_len(s)` | `int` | Byte length                   |
| `rune_count(s)`      | `int`    | Unicode code point count       |
| `s[i]`               | `byte`   | Byte at index (bounds-checked) |
| `s[i:j]`             | `string` | Byte substring (copy)          |
| `str_contains(s, t)` | `bool`   | Substring test                 |
| `str_eq(s, t)`       | `bool`   | String equality                |

Strings are byte-oriented for indexing and `len`, but rune-oriented for `range`
iteration and `rune_count`. Example: `len("cafe\u0301")` returns `5` (bytes),
`rune_count("cafe\u0301")` returns `4` (code points).

### 4.9 Struct and Field Expressions

```mko
let p = Point { x: 3, y: 4 }    // construction
p.x                               // field access
p.distance()                      // method call (dot syntax)
```

### 4.10 Call Expressions

```mko
add(1, 2)                  // function call
p.distance()               // method call on receiver
t.kick(producer(ch, 5))   // method-style call on crew
```

Call arity and argument types must match the function signature. Mismatches are
compile errors.

---

## 5. Statements and Control Flow

### 5.1 Expression Statements

Any expression may be used as a statement. An unused `Result` value as a
statement is a compile error (see Section 8).

### 5.2 If / Else

```mko
if condition {
    body
}

if condition {
    body
} else {
    alternative
}

if condition {
    body
} else if other_condition {
    body
} else {
    default
}
```

The condition must be of type `bool`. Braces are required for the body.
Parentheses around the condition are optional and not idiomatic.

### 5.3 While Loops

```mko
while condition {
    body
}
```

Executes the body as long as the condition evaluates to `true`.

`while true { ... }` is the infinite loop idiom.

### 5.4 For Loops

Mako supports several `for` loop forms:

#### Range over Collection

```mko
for i, v in range s {    // index + value
    // i: int, v: element type
}

for i in range s {        // index only
    // i: int
}

for _, v in range s {     // value only (blank index)
    // v: element type
}

for range s {             // no binders
    // iterate for side effects / count
}
```

#### Range over Integer

```mko
for i in range n {        // 0..n-1
    // i: int
}

for i in n {              // legacy form, same as range n
    // i: int
}
```

#### Range over String

```mko
for i, r in range s {
    // i: byte offset (int)
    // r: rune / Unicode code point (int)
}
```

Iterating over a string yields **runes** (Unicode code points), not bytes.
The index `i` is the byte offset of each rune.

#### Range over Map

```mko
for k, v in range m {
    // k: key type
    // v: value type
    // iteration order is unspecified
}
```

#### Range over Channel

```mko
for v in range ch {
    // receives until channel is closed and drained
}
```

#### Value-Only Short Form

```mko
for v in s {              // legacy: values only
    // v: element type
}
```

### 5.5 Break and Continue

`break` exits the innermost `for` or `while` loop. `continue` skips to the
next iteration.

```mko
while true {
    if done { break }
    if skip { continue }
}
```

Both are only valid inside loops. Using `break` or `continue` outside a loop
is a compile error.

#### Labeled Break and Continue

Loops may be labeled for targeted `break` and `continue`:

```mko
outer: while true {
    for i in range items {
        if condition {
            break outer       // exits the while loop
        }
        if other {
            continue outer    // skips to next while iteration
        }
    }
}
```

An unknown label is a compile error.

### 5.6 Match

Pattern matching on enums, `Option`, `Result`, and integer values:

```mko
match expr {
    Pattern1 => result_expr,
    Pattern2(binding) => result_expr,
    Pattern3(a, b) => {
        // block body
        result_expr
    },
}
```

#### Enum Match

```mko
match shape {
    Circle(r) => r * r,
    Rect(w, h) => w * h,
    Point => 0,
}
```

#### Option Match

```mko
match opt {
    Some(v) => v,
    None => fallback,
}
```

#### Result Match

```mko
match result {
    Ok(v) => v,
    Err(e) => -1,
}
```

#### Integer Match

```mko
match n {
    0 => "zero",
    1 => "one",
    _ => "other",
}
```

Match must be **exhaustive**: all variants of an enum must be covered, and
`Option`/`Result` must cover all cases. The wildcard `_` matches any
remaining value.

#### Or-Patterns

Multiple patterns may share an arm:

```mko
match n {
    1 | 2 | 3 => "small",
    _ => "big",
}
```

### 5.7 Return

```mko
return          // void function
return expr     // non-void function
```

A `return` statement exits the current function, delivering the expression
value (if any) to the caller.

A non-void function must return on all code paths. Missing return is a compile
error.

### 5.8 Defer

```mko
defer expression

defer {
    // block body
}
```

A `defer` statement schedules execution of the expression or block for when
the enclosing function exits. Multiple defers execute in **LIFO** (last-in,
first-out) order.

Defers execute before `return`, including early returns. A defer body is
type-checked at function exit against the final NLL state: a moved `hold`
binding cannot be used in a defer.

```mko
fn example() {
    let f = file_open("/tmp/x", 0, 0)
    defer file_close(f)
    // ... use f ...
    // file_close(f) runs on exit
}
```

---

## 6. Ownership and Memory

Mako provides deterministic resource management without a garbage collector.
Memory is managed through a combination of scope-based cleanup, move semantics,
shared references, and arena allocation.

### 6.1 Default Bindings (`let`)

A `let` binding owns its value. The value is cleaned up when the binding goes
out of scope (scope-based deterministic cleanup).

```mko
{
    let s = make([]int, 0, 8)
    // s is freed when this block exits
}
```

### 6.2 Hold (Move Semantics)

The `hold` keyword creates a binding with **move semantics**. When a `hold`
value is rebound or passed to another binding, the original becomes
**inaccessible**. Use after move is a compile error.

```mko
hold let x = 7
hold let y = x      // x is moved to y
// print_int(x)     // COMPILE ERROR: use of moved value `x`
print_int(y)        // OK
```

#### Move into Function Calls

```mko
fn id(n: int) -> int { return n }
hold let x = 42
print_int(id(x))    // x moved into id
// print_int(x)     // COMPILE ERROR: use of moved value after move into call
```

#### Mutable Hold

```mko
hold let mut x = 7
x = 9               // OK: reassign before move
print_int(x)
```

#### Partial Moves (Struct Fields)

A field access on a `hold` struct moves only that field path:

```mko
hold let p = Point { x: 1, y: 2 }
let x = p.x         // moves only field x
print_int(p.y)      // y still usable
// print_int(p.x)   // COMPILE ERROR: field x already moved
```

Nested paths are tracked individually:

```mko
hold let o = Outer { inner: Inner { a: 1, b: 2 }, n: 9 }
let a = o.inner.a   // moves path "inner.a" only
print_int(o.inner.b) // OK
```

#### Copy Types Under Hold

Copy types (`int`, `bool`, `float`, `byte`, and their fixed-width variants)
may be re-read and rebound without triggering a move:

```mko
hold let x = 42     // int is Copy
hold let y = x      // copies, x still usable
print_int(x)        // OK
```

### 6.3 Share (Read-Only Shared References)

The `share` keyword creates a shared, immutable reference. `share let`
bindings cannot be mutated and cannot be declared `mut`.

```mko
hold let a = 1
share let s = share_int(a)
print_int(share_get(s))
share_drop(s)
```

#### Share Rules

- `share let mut` is rejected at compile time.
- Assignment to a `share let` binding is a compile error.
- The source variable cannot be mutated while a `share` reference is live.
- The same local cannot be shared twice while the first share is live.
- A shared local cannot be passed into a `mut` parameter.

#### NLL-Based Share Lifetime

Share bindings follow **Non-Lexical Lifetime (NLL)** rules:

- A share is automatically ended at block exit (`}`).
- Mid-scope end: if the last use of a share binding is followed only by
  straight-line code, mutation of the source is allowed after that point.
- If/else: share ended on **both** arms allows mutation after the join.
  Share live on both arms keeps the conflict.
- Diverging arms (`return`, `break`, `continue`): moves and share ends in
  a diverging arm do not poison code after the `if`.
- While/for: shares introduced in the body end each iteration, allowing
  re-borrow on the next iteration.
- Match: share last-used on all arms allows mutation after the match.
  Diverging match arms follow the same rules as diverging if arms.

### 6.4 Arena Allocation

Arena blocks provide bump allocation with free-all-at-once semantics:

```mko
arena a {
    let mut s = make([]int, 3, 8)   // allocated from arena
    s[0] = 10
    s = append(s, 4)                // grows from arena
}
// all arena memory freed here
```

Inside an `arena name { ... }` block:
- `make([]T, ...)` allocates from the arena instead of the heap.
- `append` grows from the arena.
- All arena memory is freed when the block exits.

Arenas are useful for request-scoped work in servers, where many small
allocations can be freed together.

### 6.5 Unsafe Blocks

```mko
unsafe {
    let v = unsafe_index(xs, i)    // bounds-check opt-out
}
```

`unsafe` blocks disable certain safety checks (e.g., bounds checking on
array access). The compiler tracks `unsafe` nesting depth. Code inside
`unsafe` blocks should be minimized and carefully audited.

### 6.6 NLL Control Flow Analysis

The ownership checker uses a **Control Flow Graph (CFG)** with NLL analysis:

- **If/else joins**: moves from non-diverging arms are unioned. If neither
  arm moves a `hold` binding, it remains usable after the `if`.
- **Match joins**: same union semantics across match arms.
- **While/for loops**: the body is re-checked only when some path can
  re-enter the header (via `continue` or fall-through). An always-`break`
  body skips the second pass.
- **Const-bool edge pruning**: `if false { ... }` and `while false { ... }`
  are dead code and do not move bindings. `if true { ... }` takes only the
  then-arm.
- **Loop-carried moves**: a move inside a loop body that would be re-read on
  the next iteration is a compile error.
- **Break/continue**: dead code after a diverging statement (`break`,
  `continue`, `return`) does not contribute to move analysis.

### 6.7 Legacy Systems Marker

`[package] systems = true` is accepted as a legacy manifest marker. It does not
change the safety model: all Mako packages use the same ownership rules, and
Mako has no garbage collector to enable or disable.

---

## 7. Concurrency

Mako provides structured concurrency with crew blocks, channels, actors, and
data-parallel fan operations. There are no `async`/`await` keywords; the
runtime handles I/O multiplexing underneath.

### 7.1 Crew Blocks

A `crew` block defines a structured concurrency scope. All jobs spawned within
the crew must complete before the crew block exits.

```mko
crew t {
    let job1 = t.kick(some_function())
    let job2 = t.kick(another_function())
    let result1 = job1.join()
    let result2 = job2.join()
}
```

#### Crew Operations

| Operation      | Description                                     |
|----------------|-------------------------------------------------|
| `crew t { }`   | Create a structured concurrency scope named `t` |
| `t.kick(expr)` | Spawn `expr` as a concurrent job; returns `Job[T]` |
| `job.join()`   | Block until job completes; returns the result   |
| `t.cancel()`   | Cooperatively cancel all jobs in the crew       |
| `t.cancelled()` | Check if cancellation has been requested       |

Jobs cannot outlive their `crew` block. If the crew block exits before all
jobs complete, remaining jobs are cancelled and joined.

### 7.2 Channels

Typed, buffered channels for communication between concurrent tasks:

```mko
let ch = chan_new(4)          // buffered channel with capacity 4
let _ = ch.send(42)          // send a value
let v = ch.recv()            // receive a value (blocks if empty)
ch.close()                   // close the channel
```

#### Channel Operations

| Operation         | Description                                 |
|-------------------|---------------------------------------------|
| `chan_new(size)`   | Create a buffered channel with given capacity |
| `ch.send(value)`  | Send a value into the channel               |
| `ch.recv()`       | Receive a value (blocks until available)    |
| `ch.close()`      | Close the channel                           |
| `chan_len(ch)`    | Buffered item count — any `chan[T]`         |
| `chan_cap(ch)`    | Capacity — any `chan[T]` (set at create)    |

The send value must match the channel's element type. Receiving from a closed,
drained channel returns a zero value.

#### Range over Channel

```mko
for v in range ch {
    // receives values until channel is closed and drained
}
```

### 7.3 Select

Multi-way channel wait with up to 16 arms:

```mko
select timeout 30 {
    a => { print("got a") }
    b => { print("got b") }
    default => { print("nothing ready") }
}
```

| Component    | Description                                 |
|--------------|---------------------------------------------|
| `select`     | Begin a multi-way wait                      |
| `timeout N`  | Wait up to N milliseconds                   |
| `channel =>`| Arm: execute when channel has data           |
| `default =>` | Non-blocking fallback when no channel ready |

The value of the ready arm is available via `chan_select_value()`.

Fairness: when multiple channels are ready simultaneously, selection uses
**round-robin** to prevent starvation.

Helper functions: `chan_select2`, `chan_select3`, `chan_select4` for 2-4 channel
select without the full `select` block syntax.

### 7.4 Actors

Actors are message-processing entities with isolated mailboxes:

```mko
actor Session {
    receive Invite { print("invite") }
    receive Timer  { print("tick") }
    receive Bye    { print("bye") }
}
```

#### Actor Operations

| Operation              | Description                          |
|------------------------|--------------------------------------|
| `ActorName_spawn()`    | Create and spawn an actor            |
| `ActorName_send(a, msg)` | Send a message to the actor        |
| `ActorName_loop(a)`    | Run the actor's message processing loop |

Actors desugar to a mailbox plus a crew loop internally.

```mko
fn main() {
    let session = Session_spawn()
    crew t {
        let loopj = t.kick(Session_loop(session))
        let _ = Session_send(session, Session_Invite())
        let _ = Session_send(session, Session_Bye())
        print_int(loopj.join())
    }
}
```

### 7.5 Fan (Data-Parallel Map)

`fan` applies a function to each element of a collection in parallel:

```mko
let xs = [1, 2, 3, 4]
let ys = fan(xs, |x| x * x)
// ys = [1, 4, 9, 16]
```

`fan` distributes work across available threads and collects results in order.

### 7.6 Synchronization Primitives

| Type        | Constructor        | Description                     |
|-------------|--------------------|---------------------------------|
| `Mutex`     | `mutex_new()`      | Mutual exclusion lock           |
| `RWMutex`   | `rwmutex_new()`    | Readers-writer mutex            |
| `WaitGroup` | `wait_group_new()` | Concurrency barrier             |
| `CMap`      | `cmap_new()`       | Lock-free concurrent hash map   |

#### CMap (Concurrent Map)

Lock-free reads and per-stripe spinlock writes (512 stripes, FNV-1a hash).
Safe to share across crew tasks without `hold`/`share`:

```mko
let m = cmap_new()
cmap_set(m, "hello", "world")
print(cmap_get(m, "hello"))
print_int(cmap_has(m, "hello"))
let new_val = cmap_incr(m, "counter", 5)   // atomic increment
cmap_del(m, "hello")
print_int(cmap_len(m))
```

---

## 8. Error Handling

Mako uses `Result[T, E]` for recoverable errors and runtime aborts for
unrecoverable errors. There are no exceptions, no panics, and no null.

### 8.1 Result Type

```mko
Result[T, E]
```

Two variants:
- `Ok(value)` — success, carrying a value of type `T`
- `Err(error)` — failure, carrying an error of type `E`

### 8.2 Creating Errors

```mko
error("must be positive")              // returns Err(string)
errorf("missing %s", "config.toml")    // formatted error
```

### 8.3 The `?` Operator

The `?` operator propagates errors. When applied to a `Result` value:
- If `Ok(v)`, unwraps to `v`
- If `Err(e)`, immediately returns `Err(e)` from the enclosing function

The enclosing function must have a compatible `Result` return type.

```mko
fn parse_positive(n: int) -> Result[int, string] {
    if n <= 0 {
        return error("must be positive")
    }
    return Ok(n)
}

fn double_positive(n: int) -> Result[int, string] {
    let v = parse_positive(n)?    // propagates error if Err
    return Ok(v * 2)
}
```

### 8.4 Error Wrapping and Inspection

| Function                  | Description                              |
|---------------------------|------------------------------------------|
| `wrap_err(result, prefix)` | Wraps error message: `Err("prefix: msg")` |
| `error_is(result, substr)` | Check if error message contains substring |
| `error_string(result)`    | Extract error message as string           |

```mko
let r = wrap_err(error("boom"), "parse")    // Err("parse: boom")
let ok = wrap_err(Ok(7), "unused")          // still Ok(7)
let e = errorf("missing %s", "config.toml")
assert(error_is(e, "config.toml"))
let msg = error_string(wrap_err(e, "load")) // "load: missing config.toml"
```

### 8.5 Unused Result Rule

An unused `Result` value as a statement expression is a **compile error**.
This prevents accidentally ignoring errors. To explicitly discard a result:

```mko
let _ = function_returning_result()
```

Or consume it via `match` or `?`.

### 8.6 Option Type

`Option[T]` works similarly to `Result` for optional values:

```mko
fn option_or(o: Option[int], fallback: int) -> int {
    match o {
        Some(v) => v,
        None => fallback,
    }
}
```

---

## 9. Modules — packs & pulls

Mako units are **packs**. You **pull** them in. Normal pulls always use a pack
name at the call site. The default qualifier is the pulled file’s optional
`pack` clause (if not `main`), otherwise the last path element (with `.mko`
stripped). Call sites use `pkg.name(...)`; the compiler mangles definitions to
`pkg__name` internally and rewrites references inside the pulled unit.

**Types are pack-qualified the same way:** `eng.Table` in annotations, return
types, struct literals (`eng.Table { n: 0 }` / `eng.Table { 0 }`), and struct
patterns (`eng.Table { n }`) is the surface form of the mangled name
`eng__Table`. Multi-return and tuple match of pack structs work
(`let t, n = eng.f()`, `match eng.f() { (t, n) => … }`).

**Enums:** after pull, construct and match with pack (or pack+type) paths —
`eng.Red`, `eng.Green(n)`, `eng.Color.Red`, `eng.Color.Green(n)` — as well as
bare variant names (`Red`, `Green(n)`) when unambiguous.

Preferred spellings: `pack` / `pull`. Dual: `package` / `import`. See
[docs/IDENTITY.md](docs/IDENTITY.md).

### 9.1 File pulls

```mko
// math_utils.mko has `pack math` (or path basename math_utils)
pull "./math_utils.mko"

fn main() {
    print_int(math.add(2, 3))
}
```

Without an explicit alias, symbols are **not** merged bare into the importer.

```mko
// Pack-qualified type annotations (export the type in the pack)
fn use(t: eng.Table) -> eng.Table { return t }
let t: eng.Table = eng.table_new()
let t2, n = eng.table_grow_pair(t, 1)
```

### 9.2 Aliased pulls

```mko
pull "./math_utils.mko" as math
math.add(10, 32)

// Dual form still accepted
import math "./math_utils.mko"
```

### 9.3 Blank and Dot

```mko
// Blank: compile dependency only; no names bound
pull _ "fmt"

// Dot: merge symbols without a prefix (specialized; use sparingly)
pull . "./helpers.mko"
// then: helper_fn() bare
```

### 9.4 Standard library

Std units are pulled by name (resolved under `std/`) and always qualified:

```mko
pull "strings"
pull "sync"

strings.contains("hi", "h")
let m = sync.rwmutex()
```

### 9.5 Grouped pulls

Multiple pulls may be grouped with parentheses or braces:

```mko
// Parenthesized form (preferred)
pull (
    "strings"
    "path"
    "./import_ns_lib.mko" as lib
)

// Brace form
pull { "fmt"; "strings" }
```

`mako fmt` normalizes into a single `pull ( ... )` block when there are two or
more, and emits `"path" as name` for aliases.

### 9.6 Package Manifest (`mako.toml`)

Each package has a `mako.toml` manifest at its root:

```toml
[package]
name = "myapp"
version = "0.1.0"

[dependencies]
"helper" = { path = "../helper", version = "0.1.0" }
"tool" = { path = "../tool", version = "0.1.0" }
```

#### Dependency Resolution

| Dep Type | Declaration                                          |
|----------|------------------------------------------------------|
| Path     | `"name" = { path = "../dir" }`                       |
| Git      | `"name" = { git = "url", tag = "v0.1.0" }`          |
| Registry | `"name" = { version = "^1.0.0" }`                   |

Path dependencies are the primary surface. Git dependencies require
`mako pkg fetch` to clone into `.mako/deps/`. Registry deps resolve from
`.mako/registry/<name>/<ver>/`.

Version strings support SemVer: `^` (compatible), `~` (patch-compatible), and
exact.

#### Dependency Layout

| Layout                          | What is included                      |
|---------------------------------|---------------------------------------|
| Path to a `.mko` file          | That file                             |
| Package dir with `lib.mko`     | Only `lib.mko`                        |
| Package dir without `lib.mko`  | All top-level `.mko` except tests     |

Symbols are namespaced by the dependency key declared in the parent manifest.

### 9.6 Workspaces

A workspace groups multiple packages:

```toml
[workspace]
members = ["core", "helper", "app"]
```

Workspace commands operate on all members or a specific one with `-p`:

```
mako check .          # all members
mako build -p app     # specific member
mako test .           # all members with tests
mako run -p app       # run specific member
```

### 9.7 Package Commands

```
mako pkg init mylib           # scaffold new package
mako pkg list                 # show name + deps
mako pkg fetch                # clone git deps
mako pkg add helper ../helper # add/update path dep
mako pkg remove helper        # remove dep
mako pkg lock                 # pin content_hash in mako.lock
mako pkg audit                # offline advisory + license check
```

---

## 10. Standard Library Summary

The Mako standard library provides built-in functions organized by category.
All are available without explicit import unless noted.

### 10.1 I/O and Printing

| Function          | Signature              | Description                |
|-------------------|------------------------|----------------------------|
| `print(s)`        | `string -> void`       | Print string to stdout     |
| `print_int(n)`    | `int -> void`          | Print integer              |
| `print_int64(n)`  | `int64 -> void`        | Print 64-bit integer       |
| `print_int32(n)`  | `int32 -> void`        | Print 32-bit integer       |
| `print_int8(n)`   | `int8 -> void`         | Print 8-bit integer        |
| `print_uint64(n)` | `uint64 -> void`       | Print unsigned 64-bit      |
| `print_float(f)`  | `float -> void`        | Print float                |
| `dbg(n)`          | `int -> void`          | Debug print to stderr      |
| `dbg_str(s)`      | `string -> void`       | Debug print string to stderr |

### 10.2 String Operations

| Function                    | Description                          |
|-----------------------------|--------------------------------------|
| `str_len(s)`                | Byte length of string                |
| `str_eq(a, b)`              | String equality                      |
| `str_contains(s, sub)`      | Substring check                      |
| `str_split(s, sep)`         | Split string by separator            |
| `str_join(parts, sep)`      | Join string parts with separator     |
| `rune_count(s)`             | Unicode code point count             |
| `fmt_sprintf(fmt, args...)` | Formatted string construction        |

### 10.3 String Builder

| Function                    | Description                          |
|-----------------------------|--------------------------------------|
| `str_builder()`             | Create new growable string buffer    |
| `builder_write(b, s)`       | Append string to builder            |
| `builder_write_byte(b, c)` | Append byte to builder               |
| `builder_string(b)`        | Extract built string                 |
| `builder_len(b)`           | Current length of builder            |

### 10.4 Collections

| Function            | Description                            |
|---------------------|----------------------------------------|
| `len(s)`            | Length of slice, string, or map        |
| `cap(s)`            | Capacity of slice                      |
| `append(s, v)`      | Append to slice (returns new slice)    |
| `copy(dst, src)`    | Copy slice elements                    |
| `make(type, ...)`   | Allocate slice or map with size        |
| `has(m, k)`         | Map key presence                       |
| `delete(m, k)`      | Remove map entry                       |
| `sort_ints(xs)`     | Sort integer slice                     |
| `sort_strings(ss)`  | Sort string slice                      |
| `ints_contains(xs, v)` | Check if sorted slice contains value |

### 10.5 Math and Conversion

| Function          | Description                              |
|-------------------|------------------------------------------|
| `abs(n)`          | Absolute value                           |
| `parse_int(s)`    | Parse string to `Result[int, string]`    |
| `parse_float(s)`  | Parse string to float                    |
| `base64_encode(s)` | Base64 encode                           |
| `hex_encode(s)`   | Hex encode                               |
| `hex_decode(s)`   | Hex decode                               |

### 10.6 File System

| Function              | Description                            |
|-----------------------|----------------------------------------|
| `read_file(path)`     | Read file contents as string           |
| `write_file(path, s)` | Write string to file                  |
| `append_file(path, s)` | Append string to file                |
| `file_exists(path)`   | Check if file exists                   |
| `is_dir(path)`        | Check if path is a directory           |
| `mkdir(path)`         | Create directory                       |
| `remove_file(path)`   | Remove file                            |
| `file_open(path, mode, flags)` | Low-level file open            |
| `file_close(fd)`      | Close file descriptor                  |
| `pread(fd, n, off)`   | Positional read                        |
| `pwrite(fd, data, off)` | Positional write                     |
| `fsync(fd)`           | Flush data + metadata to disk          |
| `fdatasync(fd)`       | Flush data only                        |

### 10.7 Path Operations

| Function              | Description                            |
|-----------------------|----------------------------------------|
| `path_join(a, b)`     | Join two path components               |
| `path_clean(p)`       | Normalize path (resolve `.`, `..`)     |

### 10.8 Time

| Function           | Description                             |
|--------------------|-----------------------------------------|
| `now_ms()`         | Wall clock milliseconds                 |
| `now_ns()`         | Monotonic nanoseconds (for benchmarks)  |
| `sleep_ms(n)`      | Sleep for n milliseconds                |
| `time_sleep_ms(n)` | Alias for `sleep_ms`                    |
| `time_format(ms)`  | Format milliseconds as RFC 3339 UTC     |
| `elapsed_ms(t0)`   | Milliseconds since timestamp t0         |
| `tick_now_us()`    | Microsecond timestamp for game ticks    |
| `tick_sleep_us(start, interval)` | Sleep to maintain tick rate |
| `black_box(x)`     | Prevent LTO from erasing bench work     |

### 10.9 Environment and CLI

| Function       | Description                               |
|----------------|-------------------------------------------|
| `env_get(key)` | Get environment variable                  |
| `env_set(k, v)` | Set environment variable                 |
| `argc()`       | Argument count                            |
| `arg_get(i)`   | Get argument at index                     |
| `args()`       | All arguments as `[]string`               |
| `exit(code)`   | Exit process with status code             |

### 10.10 Logging

| Function              | Description                          |
|-----------------------|--------------------------------------|
| `log_info(msg)`       | Log at info level with timestamp     |
| `log_warn(msg)`       | Log at warn level                    |
| `log_error(msg)`      | Log at error level                   |
| `log_kv(level, k, v)` | Log key-value pair                  |

### 10.11 Regular Expressions

| Function                       | Description                       |
|--------------------------------|-----------------------------------|
| `regex_match(pattern, text)`   | Test if pattern matches text      |
| `regex_find(pattern, text)`    | Find first match in text          |
| `regex_capture(pattern, text, group)` | Extract capture group      |

Supported: literals, `.`, `X*`/`X+`/`X?`, `|`, `[abc]`/`[a-z]`/`[^...]`,
`(...)` groups, `^`/`$` anchors.

### 10.12 UUID

| Function            | Description                             |
|---------------------|-----------------------------------------|
| `uuid_v4()` / `uuid_v7()` / `uuid_v5(ns, name)` | Random / time-ordered / name-based |
| `uuid_string` / `uuid_string_upper` / `uuid_urn` | Format |
| `uuid_parse` / `uuid_parse_ok` / `uuid_check` | Parse (canonical, braces, URN, 32-hex) |
| `uuid_bytes` / `uuid_from_bytes` | Raw 16 bytes (hard-fail length on from) |
| `uuid_eq` / `uuid_cmp` / `uuid_version` / `uuid_variant` / `uuid_is_nil` / `uuid_nil` | Inspect |
| `uuid_ns_dns` / `url` / `oid` / `x500` | Standard namespaces |
| `ulid_new` / `ulid_string` / `ulid_parse` / `ulid_timestamp_ms` | ULID (same 16-byte POD) |

`Uuid` is **Copy** (free re-read / Send across `kick`).

### 10.13 Assertions and Testing

| Function              | Description                          |
|-----------------------|--------------------------------------|
| `assert(cond)`        | Assert boolean condition             |
| `assert_eq(a, b)`     | Assert integer equality              |
| `assert_eq_str(a, b)` | Assert string equality              |
| `fail(msg)`           | Fail current test with message       |
| `t_run(name)`         | Start a subtest                      |
| `t_run_nested(name)`  | Start a nested subtest               |

### 10.14 Networking

#### TCP

| Function                   | Description                      |
|----------------------------|----------------------------------|
| `tcp_listen(port)`         | Create listening socket          |
| `tcp_accept(fd)`           | Accept connection                |
| `tcp_connect(host, port)`  | Connect to remote host           |
| `tcp_write(fd, data)`      | Write data to connection         |
| `tcp_close(fd)`            | Close connection                 |

#### HTTP/1.1

| Function                          | Description                    |
|-----------------------------------|--------------------------------|
| `http_bind(port)`                 | Listen for HTTP connections    |
| `http_accept(fd)`                 | Accept and parse HTTP request  |
| `http_method(c)` / `http_path(c)` | Request method / path         |
| `http_header(c, name)`           | Get request header             |
| `http_body(c)`                    | Get request body               |
| `http_respond(c, code, body)`    | Send HTTP response              |
| `http_respond_ct(c, code, ct, body)` | Send response with Content-Type |
| `http_respond_json(c, code, json)` | Send JSON response            |
| `http_close(c)`                  | Close connection                |
| `http_get(url)`                   | HTTP GET client                |
| `http_post(url, body)`           | HTTP POST client                |
| `http_last_status()`             | Status of last client response  |

#### HTTPS / TLS

| Function                       | Description                       |
|--------------------------------|-----------------------------------|
| `tls_serve_n(port, cert, key, body, max)` | HTTPS server (max N requests) |
| `tls_get(url)` / `tls_post(url, body)` | HTTPS client (verifies peer) |
| `tls_get_insecure(url)`       | HTTPS client (skips verify)       |
| `tls_handshake_ok(host, port)` | Test TLS handshake               |

#### WebSocket

RFC 6455 client and server: HTTP upgrade, masked client frames, unmasked server
frames, text/binary/ping/pong/close, extended lengths (cap 16 MiB), fragment
reassembly, auto-pong, and close codes via `ws_*` builtins (see
[docs/BUILTINS.md](docs/BUILTINS.md) § WebSocket).

### 10.15 JSON

| Function                       | Description                       |
|--------------------------------|-----------------------------------|
| `json_object(key, value)`      | Create JSON object string         |
| `json_get_string(json, key)`   | Extract string field              |
| `json_get_int(json, key)`      | Extract integer field             |
| `json_nest(key, json)`         | Nest JSON object under key        |
| `json_merge(a, b)`             | Merge two JSON objects            |
| `json_path_string(json, keys...)` | Deep field access              |
| `json_array_ints3(a, b, c)`   | Create JSON integer array          |
| `json_array_push_int(arr, v)`  | Append to JSON array             |
| `json_object_from_map_ss(m)`   | Map to JSON object               |

### 10.16 Database

| Function                      | Description                        |
|-------------------------------|------------------------------------|
| `sql_open_sqlite(path)`       | Open SQLite database               |
| `sql_open_postgres(url)`      | Open PostgreSQL connection         |
| `sql_exec_plain(db, sql)`     | Execute statement (no params)      |
| `sql_exec_str4(db, sql, ...)`  | Execute with up to 4 string params |
| `sql_query_str(db, sql, param)` | Query single string value        |
| `sql_close(db)`               | Close database connection          |
| `sqlite_query_int(db, sql)`   | SQLite integer query               |
| `sqlite_query_text(db, sql)`  | SQLite text query                  |

### 10.17 Security

| Function                 | Description                             |
|--------------------------|-----------------------------------------|
| `const_eq(a, b)`        | Timing-safe string comparison           |
| `secret_from_str(s)`    | Create secret (wipeable memory)         |
| `secret_drop(s)`        | Wipe secret from memory                 |
| `http_header_ok(n, v)`  | Reject CR/LF header injection           |
| `session_id_new()`      | Generate 32-char random hex session ID  |
| `csrf_token()`          | Generate CSRF token                     |
| `csrf_check(exp, got)`  | Constant-time CSRF token comparison     |
| `auth_bearer(header)`   | Extract bearer token                    |
| `auth_check_bearer(h, t)` | Verify bearer token                   |
| `auth_basic_header(u, p)` | Build Basic auth header               |
| `auth_check_basic(h, u, p)` | Verify Basic auth credentials       |
| `auth_token_sign(sub, secret)` | HMAC-SHA256 sign                  |
| `auth_token_check(tok, secret)` | Verify signed token              |
| `auth_role_has(csv, role)` | Check role membership                |
| `authz_allow_role(user, req)` | Check role authorization          |
| `cookie_get(header, name)` | Parse cookie from header             |
| `cookie_make(name, val, age)` | Create Set-Cookie header          |

### 10.18 Distributed Primitives

| Function                       | Description                       |
|--------------------------------|-----------------------------------|
| `chash_new(nodes, vnodes)`     | Create consistent hash ring       |
| `chash_get(ring, key)`         | Get node for key                  |
| `chash_add_node(ring)`         | Add node to ring                  |
| `chash_remove_node(ring, id)`  | Remove node                       |
| `ratelimit_new(rate, burst)`   | Token bucket rate limiter         |
| `ratelimit_allow(r)`           | Consume token (1=yes, 0=no)       |
| `breaker_new(thresh, ms, max)` | Circuit breaker                   |
| `breaker_allow(cb)`            | Check if request should proceed   |
| `breaker_success(cb)` / `breaker_failure(cb)` | Record outcome  |

### 10.19 Memory-Mapped Files

| Function                  | Description                          |
|---------------------------|--------------------------------------|
| `mmap_open(path, mode)`   | Map existing file into memory       |
| `mmap_create(path, size)` | Create and map new file             |
| `mmap_read(m, off, n)`    | Read from mapping                   |
| `mmap_write(m, off, data)` | Write to mapping                   |
| `mmap_sync(m, flags)`     | Flush mapping to disk               |
| `mmap_close(m)`           | Unmap and close                     |

### 10.20 Binary Buffers

| Function                  | Description                          |
|---------------------------|--------------------------------------|
| `buf_pack_new(cap)`       | Create write buffer                  |
| `buf_from_string(s)`      | Buffer from existing bytes           |
| `buf_to_string(b)`        | Extract as string                    |
| `buf_write_u8/u16/u32/u64` | Write unsigned integers            |
| `buf_read_u8/u16/u32/u64` | Read unsigned integers               |
| `buf_write_str(b, s)`     | Write string to buffer               |
| `buf_read_str(b, n)`      | Read n bytes as string               |
| `buf_free(b)`             | Free buffer                          |

### 10.21 Event Loop

| Function                       | Description                       |
|--------------------------------|-----------------------------------|
| `evloop_new()`                 | Create event loop (kqueue/epoll)  |
| `evloop_add(el, fd, flags)`    | Register fd                       |
| `evloop_mod(el, fd, flags)`    | Modify interest                   |
| `evloop_del(el, fd)`           | Remove fd                         |
| `evloop_wait(el, timeout_ms)`  | Wait for events                   |
| `evloop_event_fd(el, i)`       | Get fd from ready event           |
| `evloop_event_flags(el, i)`    | Get flags from ready event        |
| `evloop_close(el)`             | Destroy event loop                |
| `nb_listen(port)`              | Non-blocking TCP listener         |
| `nb_accept(fd)`                | Non-blocking accept               |
| `nb_read(fd)` / `nb_write(fd, data)` | Non-blocking I/O            |

### 10.22 Redis

| Function                    | Description                         |
|-----------------------------|-------------------------------------|
| `redis_ping(host, port)`   | RESP PING (returns "PONG")          |
| `redis_set(h, p, k, v)`    | SET key value                       |
| `redis_get(h, p, k)`       | GET key                             |
| `redis_del(h, p, k)`       | DEL key                             |
| `redis_exists(h, p, k)`    | EXISTS key                          |

---

## 11. Compiler Pipeline

### 11.1 Overview

The Mako compiler translates `.mko` source files through the following stages:

```
Source (.mko)
  |
  v
Lexer          -- tokenize UTF-8 source into token stream
  |
  v
Parser         -- build AST from token stream
  |
  v
Desugarer      -- expand syntactic sugar (actors, derives, method syntax)
  |
  v
Type Checker   -- type inference, ownership (NLL), exhaustive match, arity
  |
  v
Code Generator -- emit C source code
  |
  v
C Compiler     -- clang or zig cc compiles C to object files
  |
  v
Linker         -- link object files + runtime into native binary
  |
  v
Native Binary
```

### 11.2 Lexer

The lexer (`src/lexer/mod.rs`) tokenizes UTF-8 source into a stream of tokens.
It recognizes the 38 reserved keywords (via `lex_ident`), integer and float
literals, string literals with escape sequences, operators, and punctuation.

### 11.3 Parser

The parser builds an Abstract Syntax Tree (AST) from the token stream. It
handles operator precedence, block structure, match arms, and all declaration
forms.

### 11.4 Desugarer

The desugarer expands syntactic sugar before type checking:

- Actor declarations expand to struct + mailbox + loop + spawn + send functions
- `#[derive(json)]` generates serialization/deserialization methods
- Method call syntax (`x.method()`) desugars to `Type_method(x)`
- Import aliases expand to namespaced identifiers

### 11.5 Type Checker

The type checker (`src/types/mod.rs`) performs:

- **Type inference** with explicit annotations where required
- **Ownership analysis** using CFG-aware NLL (Non-Lexical Lifetimes)
  - Hold/move tracking with partial move support
  - Share/borrow lifetime analysis with mid-scope end
  - Diverge-aware if/match join semantics
  - Loop fixpoints only when the body can re-enter the header
  - Const-bool edge pruning (`if false` is dead code)
- **Exhaustive match** checking on enums, Option, Result
- **Call arity and type** checking
- **Return type** checking on all code paths
- **Unused Result** detection (compile error)
- **Integer kind** mixing prevention (no implicit int/int64 coercion)
- **Compile-time range** checking for constant `int8(n)` / `byte(n)` / `uint64(n)`
- **Interface method** implementation requirements

### 11.6 Code Generator

The code generator emits C source code targeting the Mako runtime. The C output
uses `int64_t` for integer types, the runtime's slice/map/channel
implementations, and standard C calling conventions.

### 11.7 C Backend

The generated C code is compiled by **clang** (preferred) or **zig cc**:

| Mode     | Flags             | Description                      |
|----------|-------------------|----------------------------------|
| Debug    | `-O0 -g`          | Fast compilation, debug symbols  |
| Release  | `-O3 -flto`       | Optimized with link-time optimization |

### 11.8 Incremental Compilation

Incremental builds are **on by default**:

- Object files are cached per-source-file hash
- Only changed files are recompiled
- Parallel compilation with `-j N` or `MAKO_JOBS` environment variable
- Disable with `--no-incremental`

### 11.9 Build Commands

```
mako check path.mko                  # typecheck only
mako check --json path.mko           # JSON diagnostics for CI/IDE
mako build path.mko [-o output]      # compile to native binary
mako build --release path.mko        # optimized build (-O3 -flto)
mako build -j 8 path.mko             # parallel compilation
mako run path.mko [-- args...]       # compile and run
mako run path.mko -- arg1 arg2       # forward args to program
mako test [path] [--run PAT] [-v]    # run tests
mako fmt [paths...] [-w|-l|-d]       # format source
mako lint [path]                     # lint check
mako bench [path] [--json]           # run benchmarks
mako profile [path] [--json]         # compile/run profiling
mako doc [path]                      # generate API docs
mako metadata [path]                 # JSON symbol graph
mako api diff old new                # API change detection
```

### 11.10 Additional Flags

| Flag                | Description                              |
|---------------------|------------------------------------------|
| `--time`            | Print compilation timing                 |
| `--target <triple>` | Cross-compile for target platform        |
| `--sanitize=thread` | Enable thread sanitizer                  |
| `--sanitize=address` | Enable address sanitizer                |
| `--static-link`     | Force static linking                     |
| `--no-static-link`  | Force dynamic linking                    |
| `--emit-c`          | Output generated C source                |

### 11.11 Tooling

#### Formatter

`mako fmt` formats source code. Modes:

- Default: print formatted output to stdout
- `-w`: write changes back to file
- `-l`: list files that would change
- `-d`: show diff

Imports are sorted and consolidated into a single `import ( ... )` block.

#### Linter

`mako lint` runs the type checker plus additional lint rules. Workspace-aware
with `-p` for single-member focus.

#### Test Runner

Test files use the naming convention `*_test.mko`. Test functions are prefixed
with `Test`:

```mko
fn TestAdd() {
    assert_eq(add(2, 3), 5)
}
```

Filter patterns: substring, glob (`*`, `?`), or regex (`/pattern/`).

Additional categories: `Fuzz*`, `Property*`, `Snapshot*`, `Mock*`, `Fixture*`.

Subtests: `t_run("name")`, `t_run_nested("child")`.

#### Documentation Generator

`mako doc` generates API markdown with runnable examples and a search index.

#### Deployment

```
mako deploy docker [path] --entry main.mko --bin server --port 8080
mako deploy serverless [path] --provider cloud-run|fly --name app
mako deploy wasm [dir] --entry main.mko --wasm output.wasm
mako deploy plugin [dir] --name plugin --kind native|wasm
```

Docker builds produce static `x86_64-unknown-linux-musl` binaries in a
`scratch` container by default, or `debian:bookworm-slim` with `--mode debian`.

### 11.12 Editor Support

VS Code extension (`editors/vscode/`):

- Syntax highlighting for `.mko` files
- Snippets for common patterns
- Task integration
- Command palette actions
- Debug launch configurations (CodeLLDB / cppdbg)
- LSP client for `mako lsp` providing:
  - Diagnostics
  - Hover information
  - Completion
  - Go to definition
  - Find references
  - Rename
  - Code actions
  - Document symbols
  - Signature help

---

## 12. Platform Support

### 12.1 Supported Targets

| Platform | Architecture  | Triple                        | Status |
|----------|---------------|-------------------------------|--------|
| macOS    | ARM64         | `aarch64-apple-darwin`        | Primary |
| macOS    | x86_64        | `x86_64-apple-darwin`         | Supported |
| Linux    | x86_64        | `x86_64-unknown-linux-gnu`    | Supported |
| Linux    | x86_64 musl   | `x86_64-unknown-linux-musl`   | Supported (static) |
| Linux    | ARM64         | `aarch64-unknown-linux-gnu`   | Supported |
| Windows  | x86_64        | `x86_64-pc-windows-msvc`      | Supported |
| WASM     | wasm32        | `wasm32-wasip1`               | Supported |

### 12.2 Cross-Compilation

Cross-compilation is performed with the `--target` flag:

```
mako build --target x86_64-unknown-linux-musl main.mko
mako build --target wasm32-wasi main.mko
```

`wasm32-wasi` normalizes to `wasm32-wasip1` (WASI preview 1).

### 12.3 Static Linking

Linux musl targets default to static linking. Other targets use dynamic linking
unless `--static-link` is explicitly requested. The `--no-static-link` flag
forces dynamic linking on targets that default to static.

### 12.4 WASM / WASI

WASM builds require **wasi-sdk** installed (`WASI_SDK_PATH` environment
variable). The WASI target supports:

- `print` / `print_int` (via `fd_write`)
- `argc` / `arg_get` / `args`
- `env_get` (read-only; `env_set` soft-fails on WASI)
- `read_file` / `write_file` (with `--dir` preopens)

Features not available on WASI: sockets, TLS, database clients.

```
mako build examples/wasi_hello.mko --target wasm32-wasi -o out/hello.wasm
wasmtime out/hello.wasm
```

Environment variables from the host are passed via `wasmtime --env`.

For browser/edge deployment:

```
mako deploy wasm dist/ --entry main.mko --wasm app.wasm
```

This generates `index.html`, `mako-wasi-loader.js`, `build-wasm.sh`, and a
README describing the preview2/component boundary.

### 12.5 Runtime

The Mako runtime is a set of C headers and source files under `runtime/`:

- `mako_runtime.h` — core slice, map, channel, print, memory
- `mako_http.h` — HTTP/1.1 server and client
- `mako_tls.h` — TLS/HTTPS (optional OpenSSL)
- `mako_cmap.h` — concurrent hash map
- `mako_buf.h` — binary buffer
- `mako_dio.h` — direct I/O, memory-mapped files
- `mako_evloop.h` — event loop (kqueue/epoll)
- `mako_game.h` — game UDP networking
- `mako_cloud.h` — distributed primitives
- `mako_httpengine.h` — high-level HTTP engine
- `mako_security.h` — security APIs (sessions, CSRF, auth)
- `mako_plugin.h` — native/WASM plugin ABI
- `mako_extern_demo.c` — example extern C functions

The runtime path is resolved relative to the installed binary or via
`MAKO_RUNTIME` environment variable.

---

## Appendix A: Grammar Summary

This appendix provides an abbreviated EBNF grammar for reference.

### A.1 Top-Level Declarations

```ebnf
Program      = { TopDecl } .
TopDecl      = FnDecl | StructDecl | EnumDecl | ActorDecl
             | InterfaceDecl | ConstDecl | ImportDecl | ExternDecl .
```

### A.2 Function Declaration

```ebnf
FnDecl       = "fn" Ident "(" [ ParamList ] ")" [ "->" Type ] Block .
ParamList    = Param { "," Param } .
Param        = [ "mut" ] Ident ":" Type .
```

### A.3 Struct and Enum

```ebnf
StructDecl   = [ Attribute ] "struct" Ident "{" { FieldDecl } "}" .
FieldDecl    = Ident ":" Type .
EnumDecl     = "enum" Ident "{" { VariantDecl "," } "}" .
VariantDecl  = Ident [ "(" TypeList ")" ] .
```

### A.4 Statements

```ebnf
Statement    = LetStmt | AssignStmt | ExprStmt | ReturnStmt
             | IfStmt | WhileStmt | ForStmt | MatchStmt
             | DeferStmt | BreakStmt | ContinueStmt
             | CrewStmt | ArenaStmt | UnsafeStmt .
LetStmt      = [ "hold" | "share" ] "let" [ "mut" ] Ident [ ":" Type ] "=" Expr .
AssignStmt   = AssignTarget AssignOp Expr .
AssignTarget = Ident | FieldTarget | IndexExpr .
FieldTarget  = Ident { "." Ident } .
AssignOp     = "=" | "+=" | "-=" | "*=" | "/=" | "%=" .
```

### A.5 Types

```ebnf
Type         = "int" | "int64" | "int32" | "int8" | "uint64" | "byte"
             | "float" | "float64" | "bool" | "string" | "void"
             | "[]" Type | "map" "[" Type "]" Type
             | "chan" "[" Type "]"
             | "Option" "[" Type "]"
             | "Result" "[" Type "," Type "]"
             | "Job" "[" Type "]"
             | "fn" "(" [ TypeList ] ")" "->" Type
             | Ident .
```

### A.6 Expressions

```ebnf
Expr         = UnaryExpr | Expr BinaryOp Expr
             | CallExpr | IndexExpr | FieldExpr
             | MatchExpr | LambdaExpr | Literal .
LambdaExpr   = "|" [ ParamList ] "|" ( Expr | Block ) .
```

---

## Appendix B: File Extension and Entry Point

- Source files use the **`.mko`** extension.
- The entry point of a program is `fn main()`.
- A library package exposes symbols through its `lib.mko` file.

---

## Appendix C: Version Information

```
mako version mako0.2.3 <os>/<arch>
```

The `mako version` command (also `mako --version` or `mako -V`) prints the
version string. Use `mako version -v` for an optional commit hash line.

---

## Appendix D: Reserved for Future Extension

The following features are planned but not yet part of the specification:

- Colored `async`/`await` (currently not needed; runtime handles I/O)
- SIMD vector types / ISA intrinsics
- GPU backends beyond OpenCL seed (`gpu_*` + OpenCL multi-vendor + host; Metal-native/CUDA/Vulkan later)
- Full PCRE-compatible regular expressions
- WASI preview 2 / component model
- Tracing garbage collector (Mako intentionally does not provide one)
- Deeper nesting beyond `[][]T` / `map[K]map[K2]V` (depth 3+ maps, `map` of
  slice-maps, deeper composite monomorphization) as needed

---

*End of Mako Language Specification*
