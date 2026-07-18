# C FFI and Interop

This tutorial covers calling C functions from Mako, writing C functions
that Mako can call, linking external libraries, and working with C
types. Mako's `extern "C"` declarations provide direct, low-overhead
interop with the C ABI.

---

## How FFI Works

Mako compiles to C. `extern "C" fn` declarations emit direct calls to
C symbols. Place C files in `runtime/` for automatic linking.

---

## Declaring External Functions

Use `extern "C" fn` at the top level of your `.mko` file. These
declarations have no body -- they tell the compiler what symbol to call
and what types to expect.

```mko
extern "C" fn mako_c_abs(n: int) -> int
extern "C" fn mako_c_add(a: int, b: int) -> int
```

The function names must match the C symbol names exactly. Mako `int`
maps to `int64_t` in the C backend.

---

## Calling C Functions from Mako

Once declared, call them like any Mako function:

```mko
extern "C" fn mako_c_abs(n: int) -> int
extern "C" fn mako_c_add(a: int, b: int) -> int

fn main() {
    let result = mako_c_abs(0 - 42)
    print(result)  // 42

    let sum = mako_c_add(20, 22)
    print(sum)  // 42
}
```

The compiler generates a direct C function call with no wrapper
overhead.

---

## Writing C Functions for Mako

C functions called from Mako must use `int64_t` for Mako's `int` type.
Place your C file in the `runtime/` directory so it gets linked
automatically.

### Example: `runtime/mako_extern_demo.c`

```c
#include <stdint.h>

int64_t mako_c_abs(int64_t n) {
    return n < 0 ? -n : n;
}

int64_t mako_c_add(int64_t a, int64_t b) {
    return a + b;
}
```

### Type Mapping

| Mako Type | C Type |
|-----------|--------|
| `int` | `int64_t` |
| `int64` | `int64_t` |
| `int32` | `int32_t` |
| `int8` | `int8_t` |
| `uint64` | `uint64_t` |
| `byte` | `uint8_t` |
| `float` / `float64` | `double` |
| `string` | `MakoString` (struct with `data` and `len`) |
| `bool` | `int64_t` (0 or 1) |

---

## Working with Strings

Mako strings are passed as `MakoString` structs in C. The struct
contains a `const char *data` pointer and a `size_t len` field.

### C Side: Receiving a String

```c
#include <stdint.h>
#include <string.h>

typedef struct {
    const char *data;
    size_t len;
} MakoString;

int64_t mako_c_str_len(MakoString s) {
    return (int64_t)s.len;
}

int64_t mako_c_starts_with(MakoString s, MakoString prefix) {
    if (prefix.len > s.len) return 0;
    return memcmp(s.data, prefix.data, prefix.len) == 0 ? 1 : 0;
}
```

### Mako Side: Declarations

```mko
extern "C" fn mako_c_str_len(s: string) -> int
extern "C" fn mako_c_starts_with(s: string, prefix: string) -> int

fn main() {
    let n = mako_c_str_len("hello mako")
    print(n)  // 10

    let ok = mako_c_starts_with("hello mako", "hello")
    print(ok)  // 1
}
```

---

## Practical Example: Math Library

Create a small math library in C and call it from Mako.

### `runtime/mako_math_ext.c`

```c
#include <stdint.h>
#include <math.h>

int64_t mako_c_factorial(int64_t n) {
    int64_t result = 1;
    for (int64_t i = 2; i <= n; i++) {
        result *= i;
    }
    return result;
}

int64_t mako_c_gcd(int64_t a, int64_t b) {
    while (b != 0) {
        int64_t t = b;
        b = a % b;
        a = t;
    }
    return a < 0 ? -a : a;
}

int64_t mako_c_is_prime(int64_t n) {
    if (n < 2) return 0;
    if (n < 4) return 1;
    if (n % 2 == 0 || n % 3 == 0) return 0;
    for (int64_t i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) return 0;
    }
    return 1;
}
```

### `math_demo.mko`

```mko
extern "C" fn mako_c_factorial(n: int) -> int
extern "C" fn mako_c_gcd(a: int, b: int) -> int
extern "C" fn mako_c_is_prime(n: int) -> int

fn main() {
    // Factorial
    print("10! =")
    print(mako_c_factorial(10))  // 3628800

    // Greatest common divisor
    print("gcd(48, 18) =")
    print(mako_c_gcd(48, 18))  // 6

    // Primality test
    let mut n = 2
    while n < 20 {
        if mako_c_is_prime(n) == 1 {
            print(n)
        }
        n = n + 1
    }
}
```

---

## Linking External Libraries

When your C code depends on external libraries (like `libm`, `libz`,
or system libraries), the Mako build system links them through the
standard C toolchain.

### System Libraries

System libraries available on the platform link automatically when
included in the C file. For example, `math.h` functions come from
`libm` which is linked by default on most systems.

### Third-Party Libraries

For third-party C libraries:

1. Write a C wrapper file that includes the library headers
2. Place it in `runtime/`
3. Declare the functions with `extern "C" fn` in your `.mko` file

```c
// runtime/mako_zlib_wrap.c
#include <stdint.h>
#include <zlib.h>

int64_t mako_c_compress_bound(int64_t src_len) {
    return (int64_t)compressBound((uLong)src_len);
}
```

```mko
extern "C" fn mako_c_compress_bound(src_len: int) -> int

fn main() {
    let bound = mako_c_compress_bound(1024)
    print("compress bound for 1024 bytes:")
    print(bound)
}
```

---

## Build and Run

```bash
mako build main.mko -o out/ffi_demo
out/ffi_demo
```

---

## Key Takeaways

- `extern "C" fn` declares a C function callable from Mako
- Function names must match the C symbol exactly
- Mako `int` maps to C `int64_t`; `string` maps to `MakoString`
- Place C files in `runtime/` for automatic linking
- No wrapper overhead -- calls go directly to the C function
- The `MakoString` struct has `data` (pointer) and `len` (size) fields
- Use C libraries by writing thin wrapper files in `runtime/`
