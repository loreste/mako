# Debugging Mako

Debug builds are the default (`mako build` / `mako run` / `mako test` use
**clang `-O0 -g`**). Release: `mako build --release` (`-O2`, still linkable).

## lldb / gdb

```bash
mako build examples/hello.mko -o /tmp/hello
lldb /tmp/hello          # macOS
# or: gdb /tmp/hello     # Linux
(lldb) run
(lldb) bt                # backtrace on crash
```

Runtime aborts print `error: …` plus a pointer to this doc. Prefer `Result` +
`?` over panicking for expected failures.

## `dbg` / `dbg_str`

```mko
let x = dbg(n)           // stderr: [dbg] file.c:LINE: n = 42
let s = dbg_str(msg)
```

Emits to **stderr** with `__FILE__`/`__LINE__` from generated C (maps to the
compiled unit). Keep in debug sessions; remove or gate for production logs.

## Tests

```bash
mako test examples/testing -v
```

Failures show `assert_eq failed: got N, want M` (and test name). Subtests use
`t_run`.

## Sanitizers

```bash
mako build main.mko --sanitize=address
mako build main.mko --sanitize=thread
```

## Errors

See GUIDE § Errors: `error` / `errorf` / `wrap_err` / `error_is` / `error_string` / `?`.
