# Debugging Mako

This guide covers the implemented Mako debugging tools, from quick inline
prints to generated-C debugger sessions and sanitizer runs.

**Product tip:** **0.2.5**.

---

## Table of contents

1. [Debug vs release builds](#debug-vs-release-builds)
2. [Inline debugging: dbg and dbg_str](#inline-debugging-dbg-and-dbg_str)
2b. [DAP JSON seed](#dap-json-seed)
3. [Running with lldb](#running-with-lldb)
4. [Address sanitizer](#address-sanitizer)
5. [Thread sanitizer](#thread-sanitizer)
6. [Compiler error messages](#compiler-error-messages)
7. [Common error patterns](#common-error-patterns)
8. [Inspecting generated code with --emit-c](#inspecting-generated-code-with---emit-c)
9. [Tooling integration with mako check --json](#tooling-integration-with-mako-check---json)
10. [Testing and test failures](#testing-and-test-failures)
11. [Example debugging session](#example-debugging-session)

---

## DAP JSON seed

Mako ships **helpers** for Debug Adapter Protocol–shaped JSON (not a full DAP
server). Use them to prototype adapters; real source-level locals still go
through **lldb on generated C** (`-g` + `#line` mapping).

```mko
let init = dap_initialize_response(1)
let stop = dap_stopped_event("breakpoint", 1)
let cmd = dap_request_command(req_json)  // extract "command"
let resp = dap_handle_request(req_json)  // one-shot dispatch
let snap = debug_snapshot_json()         // tasks + locals + frames
```

CLI seed (not a full IDE debugger):

```bash
# One-shot
mako dap --request '{"seq":1,"type":"request","command":"initialize"}'

# Content-Length multi-message loop (exit on disconnect; cap with --max-messages)
mako dap --stdio --max-messages 4
```

Profile continuous seed:

```bash
mako profile-serve --port 9470 --max-requests 3
# GET /debug/pprof/text | /debug/pprof/json | /debug/profile | /health
```

Also: `debug_line_bp_*`, `debug_push_frame` / `debug_frames_json`, soft
`debug_break` / optional `debug_trap_enable`.

## Debug vs release builds

Debug is the default. Every `mako build`, `mako run`, and `mako test` invocation
compiles with **clang `-O0 -g`**, which means:

- Full debug symbols are embedded in the binary.
- No optimizations reorder or remove code.
- Stack frames are complete and readable in debuggers.

For release builds, pass `--release`:

```bash
mako build --release main.mko      # -O2, still linkable
```

Release builds strip debug info and enable optimizations. Only use them for
benchmarking, profiling, and shipping.

---

## Inline debugging: dbg and dbg_str

The fastest way to inspect values during development is `dbg()` for integers
and `dbg_str()` for strings. Both print to **stderr** and return the value
unchanged, so you can drop them into any expression without altering program
flow.

### dbg(value)

Prints an integer value with file and line information, then returns it:

```mko
fn process(n: int) -> int {
    let doubled = dbg(n * 2)        // stderr: [dbg] main.c:5: n * 2 = 84
    return doubled + 1
}

fn main() {
    let x = 42
    let y = dbg(x)                  // stderr: [dbg] main.c:9: x = 42
    print_int(y)                    // stdout: 42  (dbg returns the value)
}
```

### dbg_str(value)

Same behavior for strings:

```mko
fn greet(name: string) -> string {
    let msg = dbg_str(name)         // stderr: [dbg] main.c:2: name = "Alice"
    return msg
}

fn main() {
    let s = dbg_str("hello")        // stderr: [dbg] main.c:7: "hello" = hello
    print(s)                        // stdout: hello
}
```

### Tips for dbg / dbg_str

- The output goes to **stderr**, so it does not mix with your program's stdout.
- The file and line reference the **generated C** (maps to the compiled unit).
  Cross-reference with `--emit-c` if the line numbers seem off.
- Remove or gate `dbg` calls before shipping. They are for development only.
- You can nest: `print_int(dbg(a) + dbg(b))` prints both values, then their sum.

---

## Running with lldb

For deeper investigation, use lldb (macOS) to step through your program
interactively.

### Building and launching

```bash
mako build main.mko -o /tmp/myapp
lldb /tmp/myapp
```

If your program takes arguments:

```bash
lldb -- /tmp/myapp arg1 arg2
```

### Essential lldb commands

| Command | Shortcut | What it does |
|---------|----------|--------------|
| `breakpoint set --name main` | `b main` | Set a breakpoint on the main function |
| `breakpoint set --file main.c --line 12` | `b main.c:12` | Break at a specific line in the generated C |
| `breakpoint list` | `br l` | Show all breakpoints |
| `breakpoint delete 1` | `br del 1` | Remove breakpoint number 1 |
| `run` | `r` | Start (or restart) the program |
| `run arg1 arg2` | `r arg1 arg2` | Run with command-line arguments |
| `step` | `s` | Step into the next function call |
| `next` | `n` | Step over (execute function, stop at next line) |
| `finish` | `f` | Run until the current function returns |
| `continue` | `c` | Continue until the next breakpoint |
| `print variable_name` | `p variable_name` | Print a variable's value |
| `print/x variable_name` | `p/x variable_name` | Print in hexadecimal |
| `frame variable` | `fr v` | Show all local variables in current frame |
| `bt` | `bt` | Print the full backtrace (call stack) |
| `bt all` | | Backtrace of all threads |
| `thread list` | | Show all threads |
| `quit` | `q` | Exit lldb |

### Typical lldb workflow

```
(lldb) b main
Breakpoint 1: where = myapp`main ...
(lldb) r
Process launched ...
(lldb) n                    # step over lines
(lldb) p n                  # inspect variable n
(int64_t) $0 = 42
(lldb) bt                   # see call stack
* thread #1, ...
  * frame #0: myapp`process at main.c:5
    frame #1: myapp`main at main.c:12
(lldb) c                    # continue to end or next breakpoint
```

### Debugging crashes

When a Mako program aborts at runtime (out-of-bounds, integer overflow, failed
assert), it prints an `error: ...` message. To catch the exact point:

```bash
lldb /tmp/myapp
(lldb) b abort               # break when the runtime calls abort()
(lldb) r
# ... program runs until the abort ...
(lldb) bt                    # see what triggered it
(lldb) frame variable        # inspect locals at the crash site
```

---

## Address sanitizer

The address sanitizer (ASan) detects memory bugs at runtime: out-of-bounds
access, use-after-free, double-free, and stack buffer overflows.

### Building with ASan

```bash
mako build --sanitize address main.mko
```

Then run the binary normally. If ASan detects a violation, it prints a detailed
report with the exact source location and a stack trace.

### What ASan catches

| Bug class | Example |
|-----------|---------|
| Heap buffer overflow | Writing past the end of a slice's backing array |
| Stack buffer overflow | Overflowing a fixed-size local buffer |
| Use after free | Accessing arena memory after the arena block exits |
| Double free | Freeing the same allocation twice |
| Memory leak | Allocations never freed (reported at exit) |

### Reading ASan output

ASan reports look like this:

```
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x...
READ of size 8 at 0x... thread T0
    #0 0x... in process main.c:14
    #1 0x... in main main.c:22
```

The stack trace points to the **generated C** file. Use `--emit-c` to map it
back to your `.mko` source.

### Performance note

ASan adds roughly 2x overhead. Do not use it in production. It is a development
and CI tool.

---

## Thread sanitizer

The thread sanitizer (TSan) detects data races in concurrent programs that use
`crew`, channels, or shared mutable state.

### Running with TSan

For tests:

```bash
mako test --race
# CI smoke (subset):
mako test --race examples/testing/crew_fan_test.mko
mako test --race examples/testing/kick_send_test.mko
mako test --race examples/testing/chan_struct_test.mko
mako test --race examples/testing/crew_drain_test.mko
```

For a standalone build:

```bash
mako build --sanitize thread main.mko
```

CI: `.github/workflows/ci.yml` job **TSan concurrency smoke** (ubuntu).

### What TSan catches

- Two threads accessing the same memory without synchronization, where at
  least one access is a write.
- Lock-order inversions that could deadlock.
- Missing mutex locks around shared mutable data.

### Reading TSan output

```
WARNING: ThreadSanitizer: data race (pid=12345)
  Write of size 8 at 0x... by thread T2:
    #0 worker main.c:18
  Previous read of size 8 at 0x... by thread T1:
    #0 reader main.c:24
```

Fix: protect the shared state with a `mutex_new()` / `mutex_lock()` /
`mutex_unlock()` pair, or use channels to communicate instead of shared memory.

---

## Compiler error messages

Mako's type checker (`mako check`) produces structured error messages with
three parts: location, message, and optional help.

### Location format

```
main.mko:12:5: error: type mismatch: expected int, got string
```

This means:
- **main.mko** -- the source file
- **12** -- the line number (1-based)
- **5** -- the column number (1-based)
- **error:** -- the severity (error or warning)

### Caret pointing

For many errors, Mako prints the source line with a caret (`^`) pointing at the
exact position:

```
main.mko:12:5: error: type mismatch: expected int, got string
    let x: int = "hello"
                 ^~~~~~~
```

### Help hints

Some errors include a `help:` line with a suggested fix:

```
main.mko:8:12: error: use of moved value: name
    print(name)
          ^~~~
help: value was moved on line 6; consider using `share` instead of `hold`
```

### Warning vs error

- **Errors** stop compilation. You must fix them.
- **Warnings** allow compilation but flag likely bugs (unused variables,
  unreachable code). Treat them seriously.

---

## Common error patterns

### "use of moved value"

```
main.mko:10:12: error: use of moved value: data
```

**What happened:** You declared a binding with `hold` (unique ownership) and
then used it after passing it to another function or binding, which moved the
value away.

```mko
let hold data = read_file("input.txt")
process(data)           // data is moved here
print(data)             // error: use of moved value
```

**Fix:** Either use `share` for shared ownership, or restructure so you do not
access the value after the move.

```mko
let share data = read_file("input.txt")
process(data)           // shared, not moved
print(data)             // ok
```

### "unused Result"

```
main.mko:5:5: warning: unused Result value
```

**What happened:** A function returned a `Result[T, E]` and you ignored it.
This usually means you are silently discarding an error.

```mko
write_file("out.txt", contents)     // warning: unused Result
```

**Fix:** Handle the result with `?`, `match`, or assign to `let _` if you
truly do not care:

```mko
write_file("out.txt", contents)?            // propagate error
// or
let _ = write_file("out.txt", contents)     // explicitly discard
```

### "type mismatch"

```
main.mko:7:18: error: type mismatch: expected int, got string
```

**What happened:** You passed a value of the wrong type to a function, assigned
it to a variable with a different type annotation, or returned the wrong type.

```mko
fn double(n: int) -> int {
    return n * 2
}
fn main() {
    let x = double("five")     // error: expected int, got string
}
```

**Fix:** Pass the correct type. Use conversion functions (`parse_int`,
`int_to_string`, `int64()`, etc.) when you need to bridge types.

### "break outside loop"

```
main.mko:15:9: error: break outside loop
```

**What happened:** You used `break` or `continue` outside a `for` or `while`
loop. These keywords only make sense inside loops.

```mko
fn check(n: int) {
    if n == 0 {
        break               // error: break outside loop
    }
}
```

**Fix:** Use `return` to exit a function early. Use `break` only inside loops.

### Other common errors

| Error message | Meaning |
|---------------|---------|
| `undeclared name: foo` | You used a name that has not been declared in scope |
| `cannot assign to immutable binding` | You tried to modify a `let` binding; use `let mut` |
| `call arity mismatch: expected 2, got 3` | Wrong number of arguments to a function |
| `unreachable code after return` | Code after a `return` statement can never execute |
| `non-exhaustive match` | Your `match` does not cover all enum variants |
| `duplicate field: name` | A struct has two fields with the same name |

---

## Inspecting generated code with --emit-c

Mako compiles `.mko` to C, then invokes clang. You can inspect the intermediate
C to understand what the compiler generates:

```bash
mako build --emit-c main.mko
```

This writes the generated C file alongside the output. Use it to:

- **Map debugger lines** back to your Mako source when lldb shows C line numbers.
- **Understand performance** by seeing how structs are laid out and how
  closures are lowered.
- **Diagnose codegen bugs** if the compiler produces incorrect C.
- **Verify ownership** by checking where `free()` calls are inserted.

Example workflow:

```bash
mako build --emit-c main.mko -o /tmp/myapp
# Inspect the generated C:
cat /tmp/myapp.c
# The generated code has comments mapping back to .mko line numbers
```

---

## Tooling integration with mako check --json

For editor integrations, CI pipelines, and custom tooling, `mako check` can
output diagnostics as JSON:

```bash
mako check --json main.mko
```

Output format (one JSON object per line):

```json
{
  "file": "main.mko",
  "line": 12,
  "col": 5,
  "severity": "error",
  "message": "type mismatch: expected int, got string",
  "help": "convert with parse_int() or change the type annotation"
}
```

Use this for:

- **Editor plugins** that show inline errors and squiggly underlines.
- **CI pipelines** that parse errors programmatically and post them as review
  comments.
- **Pre-commit hooks** that block commits with type errors.

Example in a CI script:

```bash
mako check --json src/main.mko > diagnostics.json
if [ $? -ne 0 ]; then
    echo "Type errors found:"
    cat diagnostics.json
    exit 1
fi
```

---

## Testing and test failures

### Running tests

```bash
mako test examples/testing -v          # verbose: show each test name
mako test examples/testing --race      # with thread sanitizer
```

### Test output

Passing tests:

```
TestAdd ... ok
TestMul ... ok
2 passed, 0 failed
```

Failing tests:

```
TestAdd ... FAIL
  assert_eq failed: got 4, want 5
  at add_test.mko:4
1 passed, 1 failed
```

### Subtests

Use `t_run` for table-driven subtests:

```mko
fn TestParse() {
    t_run("positive", fn() {
        assert_eq(parse_int("42"), Ok(42))
    })
    t_run("negative", fn() {
        assert_eq(parse_int("-1"), Ok(-1))
    })
    t_run("bad input", fn() {
        match parse_int("abc") {
            Err(_) => {}
            Ok(_) => assert(false)
        }
    })
}
```

### Filtering tests

Run a single test by name:

```bash
mako test examples/testing -run TestAdd
```

---

## Example debugging session

Here is a complete walkthrough of finding and fixing a bug.

### The buggy program

```mko
// buggy.mko
fn sum_positive(nums: []int) -> int {
    let mut total = 0
    for i in len(nums) {
        total = total + nums[i]     // bug: adds ALL numbers, not just positive
    }
    return total
}

fn main() {
    let data = [3, -1, 4, -2, 5]
    let result = sum_positive(data)
    print_int(result)               // prints 9, expected 12
}
```

### Step 1: Add dbg to narrow it down

```mko
fn sum_positive(nums: []int) -> int {
    let mut total = 0
    for i in len(nums) {
        let val = dbg(nums[i])       // see each value on stderr
        total = total + val
        let _ = dbg(total)           // see running total
    }
    return total
}
```

Run: `mako run buggy.mko`

stderr output reveals negative numbers being added:

```
[dbg] buggy.c:8: nums[i] = 3
[dbg] buggy.c:10: total = 3
[dbg] buggy.c:8: nums[i] = -1
[dbg] buggy.c:10: total = 2
...
```

### Step 2: Fix the logic

```mko
fn sum_positive(nums: []int) -> int {
    let mut total = 0
    for i in len(nums) {
        if nums[i] > 0 {
            total = total + nums[i]
        }
    }
    return total
}
```

### Step 3: Add a test

Create `buggy_test.mko` in the same directory:

```mko
fn TestSumPositive() {
    assert_eq(sum_positive([3, -1, 4, -2, 5]), 12)
    assert_eq(sum_positive([]), 0)
    assert_eq(sum_positive([-1, -2]), 0)
}
```

### Step 4: Run the test

```bash
mako test . -v
```

```
TestSumPositive ... ok
1 passed, 0 failed
```

### Step 5: Run with sanitizers in CI

```bash
mako test . --race
mako build --sanitize address buggy.mko && ./buggy
```

Both pass cleanly. The bug is fixed and guarded by a test.

---

## Quick reference

| Task | Command |
|------|---------|
| Debug build (default) | `mako build main.mko` |
| Release build | `mako build --release main.mko` |
| Inline debug print | `dbg(value)` / `dbg_str(value)` |
| Type check only | `mako check main.mko` |
| Type check as JSON | `mako check --json main.mko` |
| Inspect generated C | `mako build --emit-c main.mko` |
| Run in lldb | `mako build main.mko && lldb ./main` |
| Address sanitizer | `mako build --sanitize address main.mko` |
| Thread sanitizer | `mako test --race` |
| Verbose tests | `mako test dir/ -v` |
| Run one test | `mako test dir/ -run TestName` |
