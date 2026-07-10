# 14. Cookbook

Practical recipes for common tasks. Each example is a complete, working program
or a self-contained pattern you can paste into your project.

---

## HTTP JSON API Server

A minimal HTTP server that serves JSON responses with routing:

```mko
#[derive(json)]
struct Health {
    ok: bool
    version: string
}

fn handle_health(c: int) {
    let body = "{\"ok\":true,\"version\":\"0.1.0\"}\n"
    let _ = http_respond_ct(c, 200, "application/json", body)
}

fn handle_create_user(c: int) {
    let body = http_body(c)
    // Parse and validate the request body
    let name = json_get_string(body, "name")
    if str_eq(name, "") {
        let _ = http_respond_ct(c, 400, "application/json",
            "{\"error\":\"name required\"}\n")
        return
    }
    let response = json_ss("name", name)
    let _ = http_respond_ct(c, 201, "application/json", response)
}

fn main() {
    let port = 8080
    let fd = http_bind(port)
    if fd < 0 {
        print("bind failed")
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
            handle_health(c)
        } else {
            if str_eq(method, "POST") and str_eq(path, "/users") {
                handle_create_user(c)
            } else {
                let _ = http_respond(c, 404, "{\"error\":\"not found\"}\n")
            }
        }
        let _ = http_close(c)
    }
    let _ = http_close_listener(fd)
}
```

Build and test:

```bash
mako build api.mko -o out/api
out/api &
curl -s http://127.0.0.1:8080/health
# {"ok":true,"version":"0.1.0"}
curl -s -X POST -d '{"name":"Ada"}' http://127.0.0.1:8080/users
# {"name":"Ada"}
```

---

## WebSocket Echo Server

A single-client WebSocket echo that upgrades HTTP and echoes text frames:

```mko
fn main() {
    let code = ws_echo_once(18092)
    print_int(code)
}
```

The `ws_echo_once` builtin handles the RFC 6455 upgrade handshake, reads one
text frame, echoes it back, and closes the connection. For a multi-client loop:

```mko
fn main() {
    let port = 18092
    let mut i = 0
    while i < 10 {
        let code = ws_echo_once(port)
        if code < 0 {
            break
        }
        i = i + 1
    }
    print("ws server done")
}
```

Test with a WebSocket client:

```bash
mako build ws_server.mko -o out/ws_server
out/ws_server &
echo "hello" | websocat ws://127.0.0.1:18092
# hello
```

---

## Reading and Writing Files

```mko
fn main() {
    // Write a file
    let path = "/tmp/mako_example.txt"
    let _ = write_file(path, "hello from mako\n")

    // Read it back
    let content = read_file(path)
    print(content)

    // Append to a file
    let log_path = "/tmp/mako_log.txt"
    let _ = append_file(log_path, "line 1\n")
    let _ = append_file(log_path, "line 2\n")

    // Check existence
    if file_exists(path) {
        print("file exists")
    }

    // Directory operations
    let _ = mkdir("/tmp/mako_dir")
    if is_dir("/tmp/mako_dir") {
        print("dir created")
    }

    // Clean up
    let _ = remove_file(path)
    let _ = remove_file(log_path)
}
```

### Error-Aware File Reading

```mko
fn load_config(path: string) -> Result[string, string] {
    if not file_exists(path) {
        return error("config file not found: " + path)
    }
    let content = read_file(path)
    if str_eq(content, "") {
        return error("config file is empty")
    }
    return Ok(content)
}

fn main() {
    match load_config("app.toml") {
        Ok(c) => print(c),
        Err(e) => {
            log_error(e)
        }
    }
}
```

---

## CLI Argument Parsing

```mko
fn main() {
    let a = args()
    let n = argc()

    if n < 2 {
        print("usage: app <command> [options]")
        exit(1)
    }

    let cmd = arg_get(1)

    if str_eq(cmd, "serve") {
        let mut port = 8080
        if n > 2 {
            match parse_int(arg_get(2)) {
                Ok(v) => { port = v }
                Err(_) => {
                    print("invalid port")
                    exit(1)
                }
            }
        }
        print("serving on port:")
        print_int(port)
    } else {
        if str_eq(cmd, "version") {
            print("app v0.1.0")
        } else {
            if str_eq(cmd, "help") {
                print("commands: serve, version, help")
            } else {
                print("unknown command: " + cmd)
                exit(1)
            }
        }
    }
}
```

```bash
mako run cli.mko -- serve 9090
# serving on port:
# 9090

mako run cli.mko -- version
# app v0.1.0
```

---

## Database CRUD with SQLite

```mko
fn main() {
    let db = "/tmp/mako_app.sqlite"

    // Create table
    let _ = sqlite_query_int(db, "create table if not exists users(id integer primary key, name text, age integer)")

    // Insert
    let _ = sqlite_query_int(db, "insert into users(name, age) values ('Ada', 36)")
    let _ = sqlite_query_int(db, "insert into users(name, age) values ('Bob', 28)")

    // Read
    let count = sqlite_query_int(db, "select count(*) from users")
    print("user count:")
    print_int(count)

    let name = sqlite_query_text(db, "select name from users where age > 30 limit 1")
    print("first user over 30: " + name)

    // Update
    let _ = sqlite_query_int(db, "update users set age = 37 where name = 'Ada'")

    // Delete
    let _ = sqlite_query_int(db, "delete from users where name = 'Bob'")

    // Verify
    let final_count = sqlite_query_int(db, "select count(*) from users")
    print("final count:")
    print_int(final_count)

    // Clean up
    let _ = remove_file(db)
}
```

### Postgres (when libpq is available)

```mko
fn main() {
    let handle = pg_connect("host=127.0.0.1 port=5432 dbname=mydb")
    if pg_ok(handle) == 0 {
        print("postgres not available")
        return
    }

    let _ = pg_exec(handle, "create table if not exists items(id serial, name text)")
    let _ = pg_exec(handle, "insert into items(name) values ('mako')")
    let rows = pg_exec_row_count(handle, "select * from items")
    print_int(rows)

    pg_close(handle)
}
```

---

## Concurrent Worker Pool

Process items in parallel using a crew with multiple kicked jobs:

```mko
fn process_item(id: int) -> int {
    // Simulate work
    sleep_ms(10)
    return id * id
}

fn main() {
    let items = [1, 2, 3, 4, 5, 6, 7, 8]
    let n = len(items)

    // Channel to collect results
    let results = chan_new(n)

    crew t {
        // Kick one job per item
        for i in range items {
            let _ = t.kick(worker(results, items[i]))
        }
    }

    // Collect results
    let mut total = 0
    for _ in range items {
        let v = results.recv()
        total = total + v
    }
    print("total:")
    print_int(total)
}

fn worker(out: chan[int], item: int) -> int {
    let result = process_item(item)
    let _ = out.send(result)
    return 0
}
```

### Using `fan` for Simple Data Parallelism

When each item maps independently to a result:

```mko
fn main() {
    let xs = [1, 2, 3, 4, 5, 6, 7, 8]
    let squares = fan(xs, |x| x * x)
    for v in squares {
        print_int(v)
    }
}
```

`fan` distributes work across available cores automatically.

---

## Arena-Scoped Request Handling

For server workloads, allocate all per-request memory from an arena:

```mko
struct Request {
    method: string
    path: string
    body: string
}

struct Response {
    status: int
    body: string
}

fn handle(req: Request) -> Response {
    if str_eq(req.path, "/health") {
        return Response { status: 200, body: "{\"ok\":true}\n" }
    }
    return Response { status: 404, body: "not found\n" }
}

fn main() {
    let fd = http_bind(8080)
    if fd < 0 {
        return
    }

    let mut n = 0
    while n < 100 {
        arena a {
            let c = http_accept(fd)
            if c >= 0 {
                // All temporary allocations live in the arena
                let mut headers = make([]string, 0, 16)
                let mut buf = make([]byte, 0, 4096)

                let req = Request {
                    method: http_method(c),
                    path: http_path(c),
                    body: http_body(c),
                }
                let resp = handle(req)
                let _ = http_respond(c, resp.status, resp.body)
                let _ = http_close(c)
                n = n + 1
            }
        }
        // Arena freed here -- one deallocation per request
    }
    let _ = http_close_listener(fd)
}
```

---

## Error Handling Patterns

### The `?` Operator for Propagation

```mko
fn read_config(path: string) -> Result[string, string] {
    if not file_exists(path) {
        return error("file not found: " + path)
    }
    return Ok(read_file(path))
}

fn parse_port(config: string) -> Result[int, string] {
    match parse_int(config) {
        Ok(v) => {
            if v < 1 or v > 65535 {
                return error("port out of range")
            }
            return Ok(v)
        }
        Err(e) => return error("invalid port number")
    }
}

fn load_port(path: string) -> Result[int, string] {
    let content = read_config(path)?      // propagates Err automatically
    let port = parse_port(content)?       // same here
    return Ok(port)
}

fn main() {
    match load_port("/tmp/port.txt") {
        Ok(p) => {
            print("port:")
            print_int(p)
        }
        Err(e) => {
            log_error(e)
            exit(1)
        }
    }
}
```

### Wrapping Errors with Context

```mko
fn connect_db(url: string) -> Result[int, string] {
    let handle = pg_connect(url)
    if pg_ok(handle) == 0 {
        return error("connection failed")
    }
    return Ok(handle)
}

fn init_app() -> Result[int, string] {
    let db = wrap_err(connect_db("host=localhost dbname=app"), "init_app")?
    return Ok(db)
}

fn main() {
    match init_app() {
        Ok(db) => print("connected"),
        Err(e) => {
            // e is "init_app: connection failed"
            log_error(e)
        }
    }
}
```

### Error Checking with `error_is`

```mko
fn main() {
    let e = errorf("missing %s", "config.toml")
    if error_is(e, "config.toml") {
        print("config error detected")
    }
    let msg = error_string(wrap_err(e, "startup"))
    print(msg)  // "startup: missing config.toml"
}
```

---

## Struct Serialization with `#[derive(json)]`

```mko
#[derive(json)]
struct User {
    name: string
    age: int
}

#[derive(json)]
struct Address {
    city: string
    zip: int
}

fn main() {
    // Serialize
    let user_json = User_to_json("Ada", 36)
    print(user_json)
    // {"name":"Ada","age":36}

    // Deserialize fields
    let name = User_name_from_json(user_json)
    let age = User_age_from_json(user_json)
    print(name)
    print_int(age)

    // Nested JSON
    let addr_json = Address_to_json("Paris", 75001)
    let nested = json_nest("address", addr_json)
    let doc = json_merge(user_json, nested)
    print(doc)

    // Extract nested field
    let city = json_path_string(doc, "address", "city")
    print(city)   // "Paris"
}
```

### JSON Arrays and Maps

```mko
fn main() {
    // Array of ints
    let nums = json_array_ints3(10, 20, 30)
    let more = json_array_push_int(nums, 40)
    print(more)

    // Array of strings
    let names = json_array_strings2("alice", "bob")
    let all = json_array_push_string(names, "carol")
    print(all)

    // Object from map
    let mut m = make(map[string]string, 4)
    m["host"] = "localhost"
    m["port"] = "8080"
    let obj = json_object_from_map_ss(m)
    print(obj)
}
```

---

## Channel Pipelines

Chain producers and consumers through channels:

```mko
fn generate(out: chan[int], n: int) -> int {
    for i in range n {
        let _ = out.send(i + 1)
    }
    out.close()
    return n
}

fn square(input: chan[int], out: chan[int]) -> int {
    let mut count = 0
    for v in range input {
        let _ = out.send(v * v)
        count = count + 1
    }
    out.close()
    return count
}

fn sum_all(input: chan[int]) -> int {
    let mut total = 0
    for v in range input {
        total = total + v
    }
    return total
}

fn main() {
    let ch1 = chan_new(8)
    let ch2 = chan_new(8)

    crew t {
        let _ = t.kick(generate(ch1, 5))
        let _ = t.kick(square(ch1, ch2))
        let result = t.kick(sum_all(ch2))
        let total = result.join()
        print("sum of squares 1..5:")
        print_int(total)   // 1+4+9+16+25 = 55
    }
}
```

---

## Select with Timeout and Default

Multiplexing multiple channels with a timeout:

```mko
fn delayed_send(ch: chan[int], val: int, ms: int) -> int {
    sleep_ms(ms)
    let _ = ch.send(val)
    return 0
}

fn main() {
    let fast = chan_new(2)
    let slow = chan_new(2)

    crew t {
        let _ = t.kick(delayed_send(fast, 1, 10))
        let _ = t.kick(delayed_send(slow, 2, 200))

        // Wait for first available, up to 100ms
        select timeout 100 {
            fast => {
                print("fast arrived:")
                print_int(chan_select_value())
            }
            slow => {
                print("slow arrived:")
                print_int(chan_select_value())
            }
            default => {
                print("nothing ready")
            }
        }
    }
}
```

---

## Redis Ping/Pong with Mock

```mko
fn main() {
    crew t {
        // Start a mock redis server
        let mock = t.kick(redis_mock_once(16379))
        sleep_ms(40)

        // Ping it
        let reply = redis_ping("127.0.0.1", 16379)
        print(reply)   // "PONG"

        let _ = mock.join()
    }
}
```

### Redis Key-Value Operations

```mko
fn main() {
    crew t {
        let mock = t.kick(redis_mock_kv(16380))
        sleep_ms(40)

        let _ = redis_set("127.0.0.1", 16380, "name", "mako")
        let val = redis_get("127.0.0.1", 16380, "name")
        print(val)   // "mako"

        let exists = redis_exists("127.0.0.1", 16380, "name")
        print_int(exists)   // 1

        let _ = redis_del("127.0.0.1", 16380, "name")
        let gone = redis_exists("127.0.0.1", 16380, "name")
        print_int(gone)     // 0

        let _ = mock.join()
    }
}
```

---

## Logging with Timestamps

```mko
fn main() {
    log_info("server starting")
    log_kv("info", "port", "8080")
    log_warn("no TLS configured")
    log_error("bind failed on :80")

    // Custom timestamp formatting
    let t = now_ms()
    print(time_format(t))   // RFC 3339 UTC
}
```

Output:

```text
[1720000000000 info] server starting
[1720000000000 info] port=8080
[1720000000001 warn] no TLS configured
[1720000000001 error] bind failed on :80
2026-07-10T00:00:00Z
```

---

## String Processing

```mko
fn main() {
    // Split and join
    let parts = str_split("a,b,c", ",")
    let joined = str_join(parts, " | ")
    print(joined)   // "a | b | c"

    // Builder for efficient concatenation
    let mut b = str_builder()
    builder_write(b, "hello")
    builder_write(b, " ")
    builder_write(b, "world")
    builder_write_byte(b, byte(33))
    print(builder_string(b))   // "hello world!"

    // Formatting
    let msg = fmt_sprintf("user %s has %d items", "ada")
    print(msg)

    // Regex
    if regex_match("^[a-z]+@[a-z]+\\.[a-z]+$", "user@example.com") {
        print("valid email pattern")
    }
    let captured = regex_capture("(\\d+)\\.(\\d+)", "version 3.14", 1)
    print(captured)   // "3"

    // Base64
    let encoded = base64_encode("hello mako")
    print(encoded)
    let decoded = base64_decode(encoded)
    print(decoded)
}
```

---

## Environment and Path Operations

```mko
fn main() {
    // Environment variables
    let _ = env_set("APP_MODE", "production")
    let mode = env_get("APP_MODE")
    print(mode)

    // Path operations
    let p = path_join("data", "users.db")
    print(p)   // "data/users.db"

    let clean = path_clean("/a/b/../c/./d")
    print(clean)   // "/a/c/d"

    // Timing
    let start = now_ms()
    sleep_ms(50)
    let elapsed = elapsed_ms(start)
    print("elapsed ms:")
    print_int(elapsed)
}
```

---

## Testing Patterns

### Table-Driven Tests

```mko
fn add(a: int, b: int) -> int {
    return a + b
}

fn TestAddTable() {
    let inputs_a = [1, 0, -1, 100]
    let inputs_b = [2, 0, -1, -100]
    let expected = [3, 0, -2, 0]

    for i in range 4 {
        t_run("case")
        assert_eq(add(inputs_a[i], inputs_b[i]), expected[i])
    }
}
```

### Testing with Setup and Cleanup

```mko
fn TestFileOperations() {
    let path = "/tmp/mako_test_file.txt"

    // Setup
    let _ = write_file(path, "test data")
    defer {
        let _ = remove_file(path)
    }

    // Test
    t_run("read")
    let content = read_file(path)
    assert_eq_str(content, "test data")

    t_run("exists")
    assert(file_exists(path))
}
```

```bash
mako test . -r TestFile -v
```

Next: [Appendix](ch15-appendix.md).
