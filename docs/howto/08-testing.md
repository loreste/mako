# Testing

Mako has a built-in test framework. Tests are functions named `TestXxx` in
files ending with `_test.mko`. No external test library needed.

## Writing your first test

Create the code to test in `math.mko`:

```mko
// math.mko
fn add(a: int, b: int) -> int {
    return a + b
}

fn mul(a: int, b: int) -> int {
    return a * b
}
```

Create tests in `math_test.mko` (same directory):

```mko
// math_test.mko
fn TestAdd() {
    assert_eq(add(2, 3), 5)
    assert_eq(add(-1, 1), 0)
    assert_eq(add(0, 0), 0)
}

fn TestMul() {
    assert_eq(mul(3, 4), 12)
    assert_eq(mul(0, 5), 0)
}
```

Run them:

```bash
mako test .
# PASS: TestAdd
# PASS: TestMul
# 2 passed, 0 failed
```

## Test assertions

| Helper | Purpose |
|--------|---------|
| `assert(cond)` | Fails if condition is false |
| `assert_eq(got, want)` | Fails if integers differ |
| `assert_eq_str(got, want)` | Fails if strings differ |
| `fail("message")` | Unconditionally fail with message |

A failed assertion fails the current test and continues to the next test
function. The exit code is non-zero if any test failed.

## Running tests

```bash
# Run all tests in a directory
mako test examples/testing

# Run a specific test file
mako test math_test.mko

# Verbose output (shows which tests are running)
mako test . -v
```

## Filtering tests

Use `--run` (or `-r`) to select which tests execute:

```bash
# Substring match
mako test . -r TestAdd

# Glob pattern
mako test . -r 'Test*Mul'

# Regex (wrapped in /.../)
mako test . -r '/^TestAdd$/'
mako test . -r '/Add|Mul/'
```

## Table-driven tests

Test multiple cases with parallel data arrays:

```mko
fn TestAddTable() {
    let inputs_a = [1, 2, 10, -5]
    let inputs_b = [1, 3, 5, 5]
    let expected = [2, 5, 15, 0]

    for i in range 4 {
        assert_eq(add(inputs_a[i], inputs_b[i]), expected[i])
    }
}
```

## Subtests

Use `t_run` to name sections within a test:

```mko
fn TestParser() {
    t_run("valid input")
    assert_eq(parse_port("8080"), Ok(8080))

    t_run("negative")
    assert(error_is(parse_port("-1"), "out of range"))

    t_run("not a number")
    assert(error_is(parse_port("abc"), "not a number"))
}
```

Output:

```
  TestParser/valid input  PASS
  TestParser/negative     PASS
  TestParser/not a number PASS
```

Nested subtests with `t_run_nested`:

```mko
fn TestNested() {
    t_run("outer")
    assert_eq(1, 1)
    t_run_nested("inner")
    assert_eq(2, 2)
}
// Prints: TestNested/outer/inner
```

## Repeating tests

Run tests multiple times to catch flaky behavior:

```bash
mako test . --count 5
```

## Coverage

Measure which code paths your tests exercise:

```bash
mako test . --coverage
```

This reports line coverage percentages per file.

## Test categories

Beyond `TestXxx`, Mako recognizes additional category prefixes that run in the
same harness:

| Prefix | Purpose |
|--------|---------|
| `TestXxx` | Standard unit test |
| `FuzzXxx` | Fuzz / randomized test |
| `PropertyXxx` | Property-based test |
| `SnapshotXxx` | Snapshot comparison test |
| `MockXxx` | Test with mocked dependencies |
| `FixtureXxx` | Test using fixture data |

All are zero-argument functions discovered and run by `mako test`.

## Testing with environment flags

For tests that need external services (databases, network):

```mko
fn TestLiveRedis() {
    let host = env_get("REDIS_HOST")
    if str_eq(host, "") {
        return    // skip when not configured
    }
    let r = redis_ping(host, 6379)
    assert_eq_str(r, "PONG")
}
```

Run with the flag set:

```bash
REDIS_HOST=127.0.0.1 mako test . -r TestLiveRedis
```

## Complete test file example

```mko
// server_test.mko

fn TestHealthEndpoint() {
    t_run("returns 200")
    let resp = health_response()
    assert_eq_str(resp, "{\"ok\":true}\n")
}

fn TestParsePort() {
    t_run("valid")
    match parse_port("8080") {
        Ok(p) => assert_eq(p, 8080),
        Err(_) => fail("expected Ok"),
    }

    t_run("out of range")
    match parse_port("99999") {
        Ok(_) => fail("expected Err"),
        Err(e) => assert(error_is(e, "out of range")),
    }

    t_run("not numeric")
    match parse_port("abc") {
        Ok(_) => fail("expected Err"),
        Err(e) => assert(error_is(e, "not a number")),
    }
}

fn TestAddTable() {
    let a = [1, 2, 10]
    let b = [1, 3, 5]
    let want = [2, 5, 15]
    for i in range 3 {
        assert_eq(add(a[i], b[i]), want[i])
    }
}
```

```bash
mako test . -v
# run: TestHealthEndpoint, TestParsePort, TestAddTable
# PASS: TestHealthEndpoint
# PASS: TestParsePort
# PASS: TestAddTable
# 3 passed, 0 failed
```

## Next steps

- [Release builds](09-release-builds.md)
- [Error handling patterns](03-errors-debugging.md)
