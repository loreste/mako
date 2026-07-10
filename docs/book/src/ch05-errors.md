# 5. Errors & Result

Mako errors are **values**. There is no null and no silent exception path for
ordinary fallible work. Unused `Result` is a compile error.

## `Result` and `Option`

```mko
fn parse_port(s: string) -> Result[int, string] {
    match parse_int(s) {
        Ok(v) => {
            if v <= 0 {
                return error("port must be positive")
            }
            Ok(v)
        }
        Err(_) => Err("not an int"),
    }
}
```

`error("…")` is sugar for `Err(…)`. Prefer `Ok` / `Err` constructors explicitly
when teaching.

## `?` propagation

```mko
fn load_port(s: string) -> Result[int, string] {
    let p = parse_port(s)?
    Ok(p)
}
```

## Error wrapping

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
    let w = wrap_err(r, "load")
    assert(error_is(w, "invalid path"))
    assert(error_is(w, "load"))
    print(error_string(w))
}
```

| Helper | Role |
|--------|------|
| `error` / `errorf` | Construct `Err` |
| `wrap_err` | Add context (like `%w`) |
| `error_is` | Match substring / wrapped marker |
| `error_string` | Flatten message |

## Matching

```mko
match load() {
    Ok(fd) => print_int(fd),
    Err(e) => print(e),
}
```

## Style

- Return `Result[T, string]` (or a richer error type) from fallible APIs.
- Wrap at boundaries (`wrap_err(r, "load config")`) so call sites see context.
- Never ignore a `Result` — assign to `_` only when you intentionally discard
  after handling (and the checker allows it).

See `examples/errors_wrap.mko` and [howto/03-errors-debugging.md](../../howto/03-errors-debugging.md).

Next: [Concurrency](ch06-concurrency.md).
