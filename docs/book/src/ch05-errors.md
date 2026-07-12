# 5. Errors and Result Types

Mako errors are **values**. There is no null, no silent exception unwinding, and
no way to accidentally ignore a fallible operation. If a function returns
`Result[T, E]` and you do not handle it, the compiler rejects your program.

## The Result type

`Result[T, E]` has exactly two variants:

- `Ok(value)` -- the operation succeeded, carrying a value of type `T`
- `Err(error)` -- the operation failed, carrying an error of type `E`

Most Mako code uses `Result[T, string]` where the error is a human-readable
message. Richer error types (structs, enums) are also supported.

```mko
fn parse_positive(n: int) -> Result[int, string] {
    if n <= 0 {
        return error("must be positive")
    }
    return Ok(n)
}

fn main() {
    let r = parse_positive(5)
    match r {
        Ok(v) => print_int(v),
        Err(e) => print(e),
    }
}
```

## Constructing errors

| Function | Purpose |
|----------|---------|
| `Ok(value)` | Construct a success Result |
| `Err(msg)` | Construct a failure Result |
| `error(msg)` | Sugar for `Err(msg)` -- returns a Result |
| `errorf(fmt, args...)` | Formatted error (like printf for errors) |

```mko
fn validate_port(p: int) -> Result[int, string] {
    if p <= 0 {
        return error("port must be positive")
    }
    if p > 65535 {
        return errorf("port %d out of range", p)
    }
    return Ok(p)
}
```

`error("...")` and `Err("...")` are equivalent. Use whichever reads more
naturally at the call site. `error` is preferred in most Mako code because it
is concise and clearly signals failure.

## The ? operator

The `?` operator is the primary way to propagate errors up the call stack. When
applied to a `Result`, it:

1. If the Result is `Ok(v)`, unwraps and returns the value `v`
2. If the Result is `Err(e)`, immediately returns `Err(e)` from the enclosing
   function

```mko
fn parse_port(s: string) -> Result[int, string] {
    let v = parse_int(s)?         // if parse_int fails, return its error
    if v <= 0 || v > 65535 {
        return error("port out of range")
    }
    Ok(v)
}

fn load_config() -> Result[int, string] {
    let port = parse_port("8080")?    // propagates error if any
    Ok(port)
}
```

The `?` operator can only be used inside a function that itself returns
`Result`. The error types must be compatible.

### Chaining multiple ? operations

```mko
fn setup_server() -> Result[int, string] {
    let port = parse_port("443")?
    let fd = bind_socket(port)?
    let listener = start_listening(fd)?
    Ok(listener)
}
```

Each `?` is a potential early return. If any step fails, the function returns
immediately with that error. This keeps the happy path linear and readable.

## Unused Result is a compile error

One of Mako's strictest rules: you cannot ignore a `Result`. If a function
returns `Result` and you call it without handling the return value, the
compiler rejects the program.

```mko
fn might_fail() -> Result[int, string] {
    return Ok(42)
}

fn main() {
    // might_fail()      // compile error: unused Result
    let _ = might_fail() // OK: explicitly discarding
    let r = might_fail() // OK: binding it for later use
    match r {
        Ok(v) => print_int(v),
        Err(e) => print(e),
    }
}
```

This guarantee means that errors cannot silently slip by. Every fallible
operation must be consciously handled or explicitly discarded.

## Error wrapping with wrap_err

When errors propagate through multiple layers, context is often lost. `wrap_err`
adds context to an error result without discarding the original message:

```mko
fn open_cfg(path: string) -> Result[int, string] {
    if str_eq(path, "") {
        return error("empty path")
    }
    if str_contains(path, "..") {
        return errorf("invalid path %s", path)
    }
    Ok(1)
}

fn load() -> Result[int, string] {
    let fd = open_cfg("bad..x")?
    Ok(fd)
}

fn main() {
    let r = load()
    let w = wrap_err(r, "load config")
    // The wrapped error contains both "load config" and "invalid path bad..x"
    assert(error_is(w, "invalid path"))
    assert(error_is(w, "load config"))
    print(error_string(w))
}
```

`wrap_err` works like adding a prefix/context layer. The original error is
preserved inside, and both the wrapper and the original can be matched with
`error_is`.

## Inspecting errors

| Function | Purpose |
|----------|---------|
| `error_is(r, substring)` | Check if the error chain contains a substring |
| `error_string(r)` | Flatten the entire error chain into a single string |

### error_is

`error_is` checks whether any part of the error chain (including wrapped
layers) contains the given substring:

```mko
fn main() {
    let r: Result[int, string] = error("file not found")
    let w = wrap_err(r, "loading config")

    if error_is(w, "file not found") {
        print("original error matched")
    }
    if error_is(w, "loading config") {
        print("wrapper matched")
    }
    if !error_is(w, "network") {
        print("not a network error")
    }
}
```

### error_string

`error_string` flattens the entire error (including all wrapping layers) into a
single string for display or logging:

```mko
fn main() {
    let r: Result[int, string] = error("connection refused")
    let w = wrap_err(r, "connecting to database")
    let msg = error_string(w)
    print(msg)
    // something like: "connecting to database: connection refused"
}
```

## Matching on Result

`match` on `Result` is exhaustive -- you must handle both `Ok` and `Err`:

```mko
fn handle(r: Result[int, string]) -> int {
    match r {
        Ok(v) => v,
        Err(e) => {
            print(e)
            -1
        },
    }
}
```

### Matching with different actions

```mko
fn process(input: string) -> Result[int, string] {
    let n = parse_int(input)?
    if n < 0 {
        return error("negative not allowed")
    }
    Ok(n * 2)
}

fn main() {
    match process("42") {
        Ok(v) => print_int(v),
        Err(e) => print(e),
    }

    match process("-5") {
        Ok(v) => print_int(v),
        Err(e) => print(e),     // "negative not allowed"
    }

    match process("abc") {
        Ok(v) => print_int(v),
        Err(e) => print(e),     // parse_int error propagated
    }
}
```

## Option type

`Option[T]` represents a value that may or may not exist. It has two variants:

- `Some(value)` -- the value exists
- `None` -- the value is absent

```mko
fn find_first_even(xs: []int) -> Option[int] {
    for _, v in range xs {
        if v % 2 == 0 {
            return Some(v)
        }
    }
    return None
}

fn main() {
    match find_first_even([1, 3, 4, 7]) {
        Some(v) => print_int(v),    // 4
        None => print("none found"),
    }

    match find_first_even([1, 3, 7]) {
        Some(v) => print_int(v),
        None => print("none found"),  // this branch
    }
}
```

### Option with a fallback

```mko
fn unwrap_or(o: Option[int], fallback: int) -> int {
    match o {
        Some(v) => v,
        None => fallback,
    }
}

fn main() {
    print_int(unwrap_or(Some(42), 0))    // 42
    print_int(unwrap_or(None, -1))       // -1
}
```

## Patterns for real-world error handling

### Pattern: Wrap at boundaries

Add context at each layer boundary so that when an error surfaces at the top
level, you can see the full path:

```mko
fn read_file(path: string) -> Result[string, string] {
    if str_eq(path, "") {
        return error("empty path")
    }
    // ... actual file reading ...
    Ok("file contents")
}

fn load_config(path: string) -> Result[string, string] {
    let content = read_file(path)?    // propagates read_file errors
    Ok(content)
}

fn start_server() -> Result[int, string] {
    let cfg = load_config("/etc/app.conf")
    let wrapped = wrap_err(cfg, "start_server")
    // Error message: "start_server: empty path"
    match wrapped {
        Ok(_) => Ok(1),
        Err(e) => Err(e),
    }
}
```

### Pattern: Convert Option to Result

When you need an Option to participate in a Result-based pipeline:

```mko
fn require_env(name: string) -> Result[string, string] {
    let val = get_env(name)
    match val {
        Some(v) => Ok(v),
        None => error("missing env var: " + name),
    }
}
```

### Pattern: Collect errors from a loop

```mko
fn validate_all(inputs: []int) -> Result[int, string] {
    let mut total = 0
    for _, v in range inputs {
        let validated = parse_positive(v)?
        total = total + validated
    }
    Ok(total)
}

fn parse_positive(n: int) -> Result[int, string] {
    if n <= 0 {
        return errorf("invalid value: %d", n)
    }
    Ok(n)
}
```

### Pattern: Fallback with match

When you want to try one approach and fall back to another:

```mko
fn load_port() -> int {
    match parse_port_from_env() {
        Ok(p) => p,
        Err(_) => 8080,   // default port
    }
}

fn parse_port_from_env() -> Result[int, string] {
    // try to parse PORT from environment
    let port_str = "8080"  // placeholder
    let v = parse_int(port_str)?
    if v <= 0 || v > 65535 {
        return error("port out of range")
    }
    Ok(v)
}
```

### Pattern: Early return with cleanup

Combine `defer` with `?` for clean resource management:

```mko
fn process_data(path: string) -> Result[int, string] {
    let fd = open_file(path)?
    defer close_file(fd)

    let data = read_all(fd)?
    let parsed = parse_data(data)?
    Ok(len(parsed))
}
```

The `defer` ensures `close_file` runs whether the function returns via `Ok` or
an early `?` return.

## Typed Result enums

While `Result[T, string]` covers most cases, you can use enum error types for
structured error handling where callers need to distinguish error categories
programmatically:

```mko
enum DbError {
    NotFound(string),
    Conflict(string),
    Timeout,
}

fn find_user(id: int) -> Result[string, DbError] {
    if id <= 0 {
        return Err(DbError.NotFound("user " + string(id)))
    }
    return Ok("Ada")
}

fn main() {
    match find_user(0) {
        Ok(name) => print(name),
        Err(e) => match e {
            NotFound(msg) => print("not found: " + msg),
            Conflict(msg) => print("conflict: " + msg),
            Timeout => print("timed out"),
        },
    }
}
```

Typed Result enums give you exhaustive match checking on error variants, so the
compiler ensures every error case is handled.

## Multi-error recovery

When multiple independent operations can fail and you want to collect all errors
rather than stopping at the first, use `error_join`:

```mko
fn validate_config(port: int, host: string, timeout: int) -> Result[int, string] {
    let r1 = validate_port(port)
    let r2 = if host == "" { error("host required") } else { Ok(1) }
    let r3 = if timeout <= 0 { error("timeout must be positive") } else { Ok(1) }

    let combined = error_join(r1, error_join(r2, r3))
    match combined {
        Ok(_) => Ok(1),
        Err(e) => Err(e),
    }
}
```

`error_join` combines two Results: if both are Ok, the result is Ok. If either
or both are Err, the error messages are joined. This lets callers see all
validation failures at once rather than fixing them one at a time.

## Style guide for errors

1. **Return `Result[T, string]`** from fallible functions. Use richer error
   types (enums, structs) only when callers need to match on specific error
   categories.

2. **Use `?` for propagation.** Keep the happy path linear. Avoid deeply nested
   match statements.

3. **Wrap at boundaries.** Each layer should add its own context with
   `wrap_err`. A top-level error message should read like a path:
   `"start_server: load_config: read_file: permission denied"`.

4. **Never ignore a Result.** If you truly do not care about the result, assign
   to `_` -- but think twice about whether you really should be ignoring it.

5. **Use `error_is` for conditional handling.** When you need to take different
   actions based on error type, check with `error_is` rather than brittle
   string equality.

6. **Keep error messages lowercase and without trailing punctuation.** This
   makes them compose well when wrapped:
   `"start_server: load_config: file not found"` reads naturally.

```mko
// Good
return error("connection refused")
return errorf("port %d out of range", port)

// Avoid
return error("Connection refused.")
return error("ERROR: connection refused")
```

## Summary

| Concept | Purpose |
|---------|---------|
| `Result[T, E]` | Represent success or failure |
| `Ok(v)` / `Err(e)` | Construct Result variants |
| `error(msg)` | Shorthand for `Err(msg)` |
| `errorf(fmt, ...)` | Formatted error construction |
| `?` | Propagate errors (early return on Err) |
| `wrap_err(r, ctx)` | Add context to an error |
| `error_is(r, sub)` | Check if error chain contains substring |
| `error_string(r)` | Flatten error chain to string |
| `Option[T]` | Represent presence or absence |
| `Some(v)` / `None` | Construct Option variants |
| Unused Result | Compile error (enforced) |

The error system is designed so that the path of least resistance is also the
safe path. You cannot forget to handle errors, and the tooling makes propagation
(`?`) and wrapping (`wrap_err`) concise enough that doing the right thing is
never burdensome.

Next: [Concurrency](ch06-concurrency.md).
