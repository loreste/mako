# Mako by example

Complete, runnable programs that demonstrate core Mako features. Copy any
example into a `.mko` file and run it with `mako run`.

**Preferred style is Mako-native** (`fn`, `let`, `on`, `crew`, `hold`…).  
See [IDENTITY.md](IDENTITY.md) and [`examples/mako_style.mko`](../examples/mako_style.mko).  
Dual Go-like forms still work for compatibility.

---

## Table of contents

0. [Mako-style tour](#0-mako-style-tour)
1. [Hello world](#1-hello-world)
2. [Fibonacci (functions, recursion)](#2-fibonacci)
3. [Read and write files](#3-read-and-write-files)
4. [Command-line arguments](#4-command-line-arguments)
5. [HTTP server with JSON responses](#5-http-server-with-json-responses)
6. [Parse JSON from a request](#6-parse-json-from-a-request)
7. [Database CRUD (SQLite)](#7-database-crud-sqlite)
8. [Concurrent workers with channels](#8-concurrent-workers-with-channels)
9. [Error handling chain](#9-error-handling-chain)
10. [Struct with derive(json)](#10-struct-with-derivejson)
11. [Enum with match](#11-enum-with-match)
12. [Arena-scoped work](#12-arena-scoped-work)
13. [Multi-file project](#13-multi-file-project)
14. [Testing](#14-testing)

---

## 0. Mako-style tour

Full file: [`examples/mako_style.mko`](../examples/mako_style.mko).

```mko
export struct Point {
    x: int
    y: int
}

on Point {
    fn distance(self) -> int {
        return self.x + self.y
    }
}

fn divmod(a: int, b: int) -> (int, int) {
    return (a / b, a % b)
}

fn main() {
    let p = Point { x: 3, y: 4 }
    print_int(p.distance())
    let q, r = divmod(17, 5)
    print_int(q)
    print_int(r)
}
```

```bash
mako run examples/mako_style.mko
```

---

## 1. Hello world

The simplest Mako program. Every program needs `fn main()`.

```mko
// hello.mko
fn main() {
    print("hello from mako")
}
```

```bash
mako run hello.mko
```

Output:

```
hello from mako
```

---

## 2. Fibonacci

Functions, recursion, and integer printing.

```mko
// fib.mko
fn fib(n: int) -> int {
    if n <= 1 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

fn main() {
    // Print the first 10 Fibonacci numbers
    for i in 10 {
        print_int(fib(i))
    }
}
```

```bash
mako run fib.mko
```

Output:

```
0
1
1
2
3
5
8
13
21
34
```

---

## 3. Read and write files

Write a string to a file, read it back, and print the contents.

```mko
// files.mko
fn main() {
    // Write a file
    let content = "Mako was here.\nLine two.\n"
    let w = write_file("/tmp/mako_demo.txt", content)
    match w {
        Ok(_) => print("wrote file")
        Err(e) => {
            print(e)
            return
        }
    }

    // Read it back
    let r = read_file("/tmp/mako_demo.txt")
    match r {
        Ok(data) => {
            print("contents:")
            print(data)
        }
        Err(e) => print(e)
    }

    // Clean up
    let _ = remove_file("/tmp/mako_demo.txt")
    print("done")
}
```

```bash
mako run files.mko
```

Output:

```
wrote file
contents:
Mako was here.
Line two.

done
```

---

## 4. Command-line arguments

Access argc, args, and individual arguments.

```mko
// cli.mko
fn main() {
    let n = argc()
    print("argument count:")
    print_int(n)

    // Print each argument
    let a = args()
    for i in len(a) {
        print(arg_get(i))
    }

    // Use flags for structured CLI parsing
    let name = flag_string("name", "world", "who to greet")
    let count = flag_int("count", 1, "how many times")
    print("greeting:")
    for i in count {
        let msg = fmt_sprintf("hello, %s!", name)
        print(msg)
    }
}
```

```bash
mako run cli.mko -- --name Mako --count 3
```

Output:

```
argument count:
5
./cli
--name
Mako
--count
3
greeting:
hello, Mako!
hello, Mako!
hello, Mako!
```

---

## 5. HTTP server with JSON responses

A minimal HTTP server that handles GET and POST on different routes.

```mko
// server.mko
fn main() {
    let fd = http_bind(8080)
    if fd < 0 {
        print("failed to bind port 8080")
        return
    }
    print("listening on :8080")

    let mut running = true
    while running {
        let c = http_accept(fd)
        if c < 0 {
            continue
        }

        let method = http_method(c)
        let path = http_path(c)

        if str_eq(path, "/health") {
            let _ = http_respond_json(c, 200, "{\"status\":\"ok\"}\n")
        } else {
            if str_eq(path, "/greet") {
                if str_eq(method, "GET") {
                    let _ = http_respond_json(c, 200, "{\"message\":\"hello from mako\"}\n")
                } else {
                    let _ = http_respond_json(c, 405, "{\"error\":\"GET only\"}\n")
                }
            } else {
                if str_eq(path, "/stop") {
                    let _ = http_respond_json(c, 200, "{\"stopping\":true}\n")
                    running = false
                } else {
                    let _ = http_respond(c, 404, "not found\n")
                }
            }
        }
        let _ = http_close(c)
    }

    let _ = http_close_listener(fd)
    print("server stopped")
}
```

```bash
mako run server.mko &
curl http://localhost:8080/health
curl http://localhost:8080/greet
curl http://localhost:8080/stop
```

---

## 6. Parse JSON from a request

Accept a POST with a JSON body, extract fields, and respond.

```mko
// json_api.mko
fn main() {
    let fd = http_bind(8081)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("json api on :8081")

    let mut count = 0
    while count < 5 {
        let c = http_accept(fd)
        if c < 0 {
            continue
        }

        let method = http_method(c)
        let path = http_path(c)

        if str_eq(path, "/echo") {
            if str_eq(method, "POST") {
                let body = http_body(c)
                // Parse JSON fields from the body
                let name = json_get_string(body, "name")
                let age = json_get_int(body, "age")

                // Build a response
                let resp = json_new()
                let resp = json_set_string(resp, "greeting",
                    fmt_sprintf("hello, %s", name))
                let resp = json_set_int(resp, "birth_year", 2026 - age)
                let _ = http_respond_json(c, 200, resp)
            } else {
                let _ = http_respond_json(c, 405, "{\"error\":\"POST only\"}\n")
            }
        } else {
            let _ = http_respond(c, 404, "not found\n")
        }

        let _ = http_close(c)
        count = count + 1
    }

    let _ = http_close_listener(fd)
    print("done")
}
```

Test it:

```bash
mako run json_api.mko &
curl -X POST http://localhost:8081/echo \
     -d '{"name":"Ada","age":36}'
```

---

## 7. Database CRUD (SQLite)

Create a table, insert rows, query them, update, and delete.

```mko
// db_crud.mko
fn main() {
    let db = sql_open_sqlite("/tmp/mako_demo.db")
    if sql_ok(db) == 0 {
        print("failed to open database")
        return
    }
    defer sql_close(db)

    // Create table
    let _ = sql_exec_plain(db,
        "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")

    // Insert rows using parameterized queries
    let _ = sql_exec_str4(db,
        "INSERT INTO users (name, age) VALUES ($1, $2)", "Alice", "30", "", "")
    let _ = sql_exec_str4(db,
        "INSERT INTO users (name, age) VALUES ($1, $2)", "Bob", "25", "", "")
    let _ = sql_exec_str4(db,
        "INSERT INTO users (name, age) VALUES ($1, $2)", "Carol", "28", "", "")

    // Query: count all users
    let count = sql_query_int(db, "SELECT COUNT(*) FROM users", [])
    print("user count:")
    print_int(count)

    // Query: find a name
    let name = sql_query_str(db, "SELECT name FROM users WHERE age = $1", "25")
    print("age 25:")
    print(name)

    // Update
    let _ = sql_exec_str4(db,
        "UPDATE users SET age = $1 WHERE name = $2", "31", "Alice", "", "")

    // Delete
    let _ = sql_exec_str4(db,
        "DELETE FROM users WHERE name = $1", "Carol", "", "", "")

    // Verify
    let final_count = sql_query_int(db, "SELECT COUNT(*) FROM users", [])
    print("final count:")
    print_int(final_count)

    // Clean up the demo database
    let _ = remove_file("/tmp/mako_demo.db")
    print("done")
}
```

```bash
mako run db_crud.mko
```

---

## 8. Concurrent workers with channels

Spawn workers in a crew, distribute work via channels, and collect results.

Element types: int/bool/float/string, **named structs/enums**, and **tuples**.
`chan_len` / `chan_cap` accept any `chan[T]`. Prefer `chan[Struct]` for
multi-field worker results (see [ERGONOMICS.md](ERGONOMICS.md)).

```mko
// workers.mko
fn worker(id: int, jobs: chan[int], results: chan[int]) -> int {
    let mut processed = 0
    // Each worker pulls jobs until done
    while processed < 3 {
        let job = jobs.recv()
        if job < 0 {
            break
        }
        let answer = job * job      // compute: square the input
        let _ = results.send(answer)
        processed = processed + 1
    }
    return processed
}

fn main() {
    let jobs = chan_new(10)
    let results = chan_new(10)

    // Send 9 jobs
    for i in 9 {
        let _ = jobs.send(i + 1)
    }

    // Spawn 3 workers in a crew
    crew t {
        let w1 = t.kick(worker(1, jobs, results))
        let w2 = t.kick(worker(2, jobs, results))
        let w3 = t.kick(worker(3, jobs, results))

        // Collect 9 results
        let mut total = 0
        for i in 9 {
            let r = results.recv()
            total = total + r
        }
        print("sum of squares:")
        print_int(total)

        // Wait for workers to finish
        let _ = w1.join()
        let _ = w2.join()
        let _ = w3.join()
    }

    print("done")
}
```

```bash
mako run workers.mko
```

Output:

```
sum of squares:
285
done
```

---

## 9. Error handling chain

Demonstrates `Result`, the `?` operator, `wrap_err`, and `error_is`.

```mko
// errors.mko
fn validate_name(name: string) -> Result[string, string] {
    if str_eq(name, "") {
        return error("name cannot be empty")
    }
    if str_len(name) < 2 {
        return error("name too short")
    }
    Ok(name)
}

fn validate_age(age: int) -> Result[int, string] {
    if age < 0 {
        return errorf("invalid age: %d", age)
    }
    if age > 150 {
        return error("age out of range")
    }
    Ok(age)
}

fn create_profile(name: string, age: int) -> Result[string, string] {
    let valid_name = validate_name(name)?      // propagates error if Err
    let valid_age = validate_age(age)?         // same here

    let profile = fmt_sprintf("%s (age %d)", valid_name, valid_age)
    Ok(profile)
}

fn main() {
    // Success case
    let r1 = create_profile("Ada", 36)
    match r1 {
        Ok(profile) => print(profile)
        Err(e) => print(e)
    }

    // Error case: empty name
    let r2 = create_profile("", 25)
    let r2 = wrap_err(r2, "create_profile")
    match r2 {
        Ok(_) => print("unexpected ok")
        Err(e) => {
            print(error_string(r2))
            // Check if the root cause matches
            if error_is(r2, "empty") {
                print("caught: empty name error")
            }
        }
    }

    // Error case: bad age
    let r3 = create_profile("Bob", -5)
    match r3 {
        Ok(_) => print("unexpected ok")
        Err(e) => print(e)
    }

    print("done")
}
```

```bash
mako run errors.mko
```

Output:

```
Ada (age 36)
create_profile: name cannot be empty
caught: empty name error
invalid age: -5
done
```

---

## 10. Struct with derive(json)

Define a struct, derive JSON serialization, and round-trip it.

```mko
// person.mko
#[derive(json)]
struct Person {
    name: string
    age: int
}

#[derive(json)]
struct Config {
    host: string
    port: int
}

fn main() {
    // Serialize a Person to JSON
    let j = Person_to_json("Ada", 36)
    print(j)

    // Deserialize individual fields back
    let name = Person_name_from_json(j)
    let age = Person_age_from_json(j)
    print(name)
    print_int(age)

    // Round-trip: rebuild from extracted fields
    let j2 = Person_to_json(name, age)
    assert(str_eq(j, j2))
    print("round-trip ok")

    // Another struct
    let cfg = Config_to_json("localhost", 8080)
    print(cfg)
    let host = Config_host_from_json(cfg)
    let port = Config_port_from_json(cfg)
    print(host)
    print_int(port)
}
```

```bash
mako run person.mko
```

Output:

```
{"name":"Ada","age":36}
Ada
36
round-trip ok
{"host":"localhost","port":8080}
localhost
8080
```

---

## 11. Enum with match

Define an enum with variants, add methods, and use exhaustive matching.

```mko
// shapes.mko
enum Shape {
    Circle(int),
    Rect(int, int),
    Point,
}

fn Shape_area(self: Shape) -> int {
    match self {
        Circle(r) => r * r * 3,
        Rect(w, h) => w * h,
        Point => 0,
    }
}

fn Shape_describe(self: Shape) -> string {
    match self {
        Circle(r) => fmt_sprintf("circle with radius %d", r),
        Rect(w, h) => fmt_sprintf("rectangle %d x %d", w, h),
        Point => "a point",
    }
}

fn main() {
    let shapes = [Circle(5), Rect(3, 4), Point]

    // Using the for-in-len pattern to iterate
    let areas = [0, 0, 0]
    let mut i = 0

    // Compute area for each shape
    print_int(Circle(5).area())         // 75
    print_int(Rect(3, 4).area())        // 12
    print_int(Point.area())             // 0

    // Describe each shape
    print(Circle(10).describe())
    print(Rect(6, 2).describe())
    print(Point.describe())

    // Match with Option
    let maybe: Option[int] = Some(42)
    match maybe {
        Some(v) => {
            print("got value:")
            print_int(v)
        }
        None => print("nothing")
    }
}
```

```bash
mako run shapes.mko
```

Output:

```
75
12
0
circle with radius 10
rectangle 6 x 2
a point
got value:
42
```

---

## 12. Arena-scoped work

Arenas let you allocate freely within a block, then free everything at once
when the block exits. No garbage collector needed.

```mko
// arena_demo.mko
fn main() {
    arena a {
        // Allocate text in the arena
        let greeting = arena_text(a, "hello, arena")
        print(greeting)

        // Allocate an int array in the arena
        let nums = arena_ints(a, 5)
        // Fill it
        for i in 5 {
            nums[i] = (i + 1) * 10
        }
        // Print the values
        for i in 5 {
            print_int(nums[i])
        }

        // Stamp a value (useful for tagging allocations)
        let tag = arena_stamp(a, 999)
        print_int(tag)

        print("inside arena block")
    }
    // All memory from arena `a` is freed here in one operation.
    // No individual frees, no GC pause.

    print("arena done")
}
```

```bash
mako run arena_demo.mko
```

Output:

```
hello, arena
10
20
30
40
50
999
inside arena block
arena done
```

---

## 13. Multi-file project

Mako supports multi-file projects using `import` and `mako.toml` package
dependencies.

### Project layout

```
myproject/
  mako.toml
  main.mko
  mathutil/
    mako.toml
    lib.mko
```

### myproject/mako.toml

```toml
[package]
name = "myproject"
version = "0.1.0"

[dependencies]
"mathutil" = { path = "./mathutil" }
```

### myproject/mathutil/mako.toml

```toml
[package]
name = "mathutil"
version = "0.1.0"
```

### myproject/mathutil/lib.mko

```mko
// Exported functions (all top-level fns are visible to importers)
fn add(a: int, b: int) -> int {
    return a + b
}

fn multiply(a: int, b: int) -> int {
    return a * b
}

fn factorial(n: int) -> int {
    if n <= 1 {
        return 1
    }
    return n * factorial(n - 1)
}
```

### myproject/main.mko

```mko
// Use functions from the mathutil package
fn main() {
    print_int(mathutil.add(10, 20))           // 30
    print_int(mathutil.multiply(6, 7))        // 42
    print_int(mathutil.factorial(5))           // 120
    print("done")
}
```

### Running

```bash
cd myproject
mako run main.mko
```

For standard library imports, use bare names:

```mko
import "strings"
import "path"

fn main() {
    let parts = strings.split("a,b,c", ",")
    let p = path.join("/usr", "local", "bin")
    print(p)
}
```

---

## 14. Testing

Mako discovers test functions automatically. Any function named `TestXxx` in a
`*_test.mko` file is a test.

### The code under test

Create `calc.mko`:

```mko
// calc.mko
fn add(a: int, b: int) -> int {
    return a + b
}

fn divide(a: int, b: int) -> Result[int, string] {
    if b == 0 {
        return error("division by zero")
    }
    Ok(a / b)
}

fn clamp_value(n: int, lo: int, hi: int) -> int {
    if n < lo {
        return lo
    }
    if n > hi {
        return hi
    }
    return n
}
```

### The test file

Create `calc_test.mko` in the same directory:

```mko
// calc_test.mko
fn TestAdd() {
    assert_eq(add(2, 3), 5)
    assert_eq(add(-1, 1), 0)
    assert_eq(add(0, 0), 0)
}

fn TestDivide() {
    match divide(10, 2) {
        Ok(v) => assert_eq(v, 5)
        Err(_) => assert(false)
    }

    // Division by zero returns Err
    match divide(10, 0) {
        Ok(_) => assert(false)
        Err(e) => assert(str_contains(e, "zero"))
    }
}

fn TestClamp() {
    assert_eq(clamp_value(5, 0, 10), 5)     // in range
    assert_eq(clamp_value(-3, 0, 10), 0)    // below
    assert_eq(clamp_value(99, 0, 10), 10)   // above
}

fn TestAddTable() {
    // Table-driven tests
    let a_vals = [1, 0, -5, 100]
    let b_vals = [2, 0, 5, -100]
    let expected = [3, 0, 0, 0]
    for i in 4 {
        assert_eq(add(a_vals[i], b_vals[i]), expected[i])
    }
}

fn TestDivideSubtests() {
    t_run("positive", fn() {
        match divide(20, 4) {
            Ok(v) => assert_eq(v, 5)
            Err(_) => assert(false)
        }
    })
    t_run("by zero", fn() {
        match divide(1, 0) {
            Err(e) => assert(str_contains(e, "zero"))
            Ok(_) => assert(false)
        }
    })
}
```

### Running tests

```bash
# Run all tests in the directory
mako test .

# Verbose output (show each test name)
mako test . -v

# Run a specific test
mako test . -run TestDivide

# Run with thread sanitizer
mako test . --race
```

Verbose output:

```
TestAdd ... ok
TestDivide ... ok
TestClamp ... ok
TestAddTable ... ok
TestDivideSubtests/positive ... ok
TestDivideSubtests/by zero ... ok
5 passed, 0 failed
```

---

## 15. Showcase — everything together

Struct, database, error handling, ownership, arenas, and concurrency in one
program. See [examples/showcase.mko](../examples/showcase.mko).

```mko
#[derive(json)]
struct Task {
    id: int
    title: string
    done: int
}

fn create_table(db: SqlDB) -> Result[int, string] {
    let _ = sql_exec_plain(db, "CREATE TABLE IF NOT EXISTS tasks (id INTEGER PRIMARY KEY, title TEXT, done INTEGER)")?
    Ok(0)
}

fn insert_task(db: SqlDB, title: string) -> Result[int, string] {
    let _ = sql_exec_str4(db, "INSERT INTO tasks (title, done) VALUES ($1, $2)", title, "0", "", "")?
    Ok(0)
}

fn get_task(db: SqlDB, title: string) -> Result[string, string] {
    let row = sql_query_str(db, "SELECT title FROM tasks WHERE title = $1", title)
    if str_eq(row, "") {
        return error("task not found")
    }
    Ok(row)
}

fn worker(ch: chan[int], id: int, results: CMap) -> int {
    arena a {
        let label = arena_text(a, format_int(id))
        while true {
            let task_id = ch.recv()
            if task_id == 0 { break }
            cmap_set(results, format_int(task_id), "worker " + label + " processed task " + format_int(task_id))
        }
    }
    return id
}

fn main() {
    let db = sql_open_sqlite("/tmp/mako_showcase.db")
    let _ = create_table(db)

    let titles = ["build parser", "write tests", "deploy service", "fix bug", "review PR"]
    for i in 5 {
        match insert_task(db, titles[i]) {
            Ok(_) => {}
            Err(e) => print("insert error: " + e)
        }
    }

    match get_task(db, "write tests") {
        Ok(title) => print("found: " + title),
        Err(e) => print("error: " + e),
    }

    let results = cmap_new()
    let ch = chan_new(10)

    crew t {
        let w1 = t.kick(worker(ch, 1, results))
        let w2 = t.kick(worker(ch, 2, results))
        let w3 = t.kick(worker(ch, 3, results))

        for i in 5 { let _ = ch.send(i + 1) }
        let _ = ch.send(0)
        let _ = ch.send(0)
        let _ = ch.send(0)

        let _ = w1.join()
        let _ = w2.join()
        let _ = w3.join()
    }

    for i in 5 { print(cmap_get(results, format_int(i + 1))) }
    let _ = sql_close(db)
    let _ = remove_file("/tmp/mako_showcase.db")
    print("done")
}
```

```bash
mako run examples/showcase.mko
```

**What this covers:**

- `#[derive(json)]` struct
- SQLite database (create, insert, query with parameterized queries)
- `Result` with `?` propagation and `match` on `Ok`/`Err`
- `arena` block for worker-scoped memory
- `crew` with 3 concurrent workers
- `chan` for distributing work
- `CMap` for thread-safe shared results

---

## What next

- **[GUIDE.md](GUIDE.md)** -- full language reference with all syntax details.
- **[STDLIB.md](STDLIB.md)** -- standard library packages and function listings.
- **[DEBUG.md](DEBUG.md)** -- debugging tools, sanitizers, and error messages.
- **[howto/](howto/)** -- task-oriented how-to guides for common scenarios.
