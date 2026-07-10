# Errors and Debugging

Mako enforces error handling at compile time. Every function that can fail
returns a `Result[T, E]`. This guide covers patterns for working with results,
adding context, and debugging when things go wrong.

## Result basics

A function signals failure by returning `error(...)` and success with `Ok(...)`:

```mko
fn parse_port(s: string) -> Result[int, string] {
    match parse_int(s) {
        Ok(n) => {
            if n < 1 || n > 65535 {
                return error("port out of range")
            }
            return Ok(n)
        }
        Err(e) => return error("not a number")
    }
}
```

## The `?` operator

Use `?` to propagate errors up the call stack. If the result is `Err`, the
function returns immediately with that error:

```mko
fn load_config(path: string) -> Result[int, string] {
    let content = read_file(path)
    let port = parse_port(content)?
    return Ok(port)
}
```

Without `?`, you would need an explicit match on every fallible call.

## Matching on results

When you need to handle both cases explicitly:

```mko
fn main() {
    let r = parse_port("8080")
    match r {
        Ok(port) => print_int(port),
        Err(msg) => {
            print("error: ")
            print(msg)
        },
    }
}
```

## Wrapping errors with context

Use `wrap_err` to add context as errors propagate:

```mko
fn connect_db() -> Result[int, string] {
    let r = parse_port(env_get("DB_PORT"))
    return wrap_err(r, "connect_db")
}
// On failure: "connect_db: port out of range"
```

Use `errorf` for formatted error messages:

```mko
fn open_config(name: string) -> Result[int, string] {
    if not file_exists(name) {
        return errorf("missing %s", name)
    }
    return Ok(1)
}
```

## Error inspection

```mko
let e = error("connection refused")
let wrapped = wrap_err(e, "redis")
let msg = error_string(wrapped)       // "redis: connection refused"
assert(error_is(wrapped, "refused"))  // substring check
```

## Compile-time enforcement

Mako refuses to compile code that ignores a Result:

```mko
fn main() {
    // parse_port("bad")     // COMPILE ERROR: unused Result
    let _ = parse_port("bad")  // explicit discard — compiles
}
```

This ensures you never silently swallow failures.

## Debugging with dbg

Insert `dbg` calls to print values to stderr with file and line info:

```mko
fn process(n: int) -> int {
    let x = dbg(n * 2)         // [dbg] file.mko:3: 42
    let s = dbg_str("step 2")  // [dbg] file.mko:4: step 2
    return x + 1
}
```

`dbg` returns its argument, so you can inline it in expressions without
changing program behavior.

## Native debugging with lldb

Debug builds (the default) include full debug symbols (`-O0 -g`):

```bash
mako build main.mko -o app
lldb ./app
```

Inside lldb:

```
(lldb) breakpoint set --name main
(lldb) run
(lldb) step
(lldb) print x
(lldb) bt
```

All local variables, struct fields, and function arguments are visible to the
debugger because Mako compiles through C with debug info preserved.

## Address sanitizer

Catch out-of-bounds access and use-after-free at runtime:

```bash
mako build --sanitize=address main.mko -o app_asan
./app_asan
```

The sanitizer will print a detailed report if any memory violation occurs,
including the exact source location.

## Thread sanitizer

Detect data races in concurrent programs:

```bash
mako build --sanitize=thread main.mko -o app_tsan
./app_tsan
```

## Practical error handling pattern

A complete example combining these techniques:

```mko
fn read_config(path: string) -> Result[int, string] {
    if not file_exists(path) {
        return errorf("missing %s", path)
    }
    let content = read_file(path)
    let port = parse_port(content)?
    return Ok(port)
}

fn start_server() -> Result[int, string] {
    let port = wrap_err(read_config("config.txt"), "config")?
    let fd = http_bind(port)
    if fd < 0 {
        return error("bind failed")
    }
    return Ok(fd)
}

fn main() {
    match start_server() {
        Ok(fd) => {
            print("server started")
        }
        Err(e) => {
            log_error(e)
        }
    }
}
```

## Summary

| Tool | When to use |
|------|-------------|
| `Result[T, E]` | Any operation that can fail |
| `?` | Propagate error to caller |
| `wrap_err(r, ctx)` | Add context string to errors |
| `errorf(fmt, ...)` | Create formatted error messages |
| `error_is(e, sub)` | Check if error contains substring |
| `match` | Handle Ok/Err explicitly |
| `let _ = ...` | Explicitly discard a result |
| `dbg(x)` / `dbg_str(s)` | Print debug info to stderr |
| `--sanitize=address` | Detect memory errors |
| `--sanitize=thread` | Detect data races |
| lldb | Step through native code |

## Next steps

- [Organize code into packages](04-packages.md)
- [Memory safety with hold/share](06-memory.md)
