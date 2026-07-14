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

### Unified SQL CRUD with String Data

Use `sql_exec_str4` for text inserts and `sql_query_str` for text lookups via
the unified `SqlDB` handle:

```mko
fn main() {
    let db = sql_open_sqlite("/tmp/mako_crud.db")

    // Create table (no parameters needed)
    let _ = sql_exec_plain(db, "CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY, name TEXT, email TEXT, role TEXT, team TEXT)")

    // Insert rows with string parameters
    let _ = sql_exec_str4(db, "INSERT INTO users(name, email, role, team) VALUES ($1, $2, $3, $4)", "Ada", "ada@example.com", "engineer", "platform")
    let _ = sql_exec_str4(db, "INSERT INTO users(name, email, role, team) VALUES ($1, $2, $3, $4)", "Grace", "grace@example.com", "lead", "infra")

    // Query a single text value
    let role = sql_query_str(db, "SELECT role FROM users WHERE name = $1", "Ada")
    print(role)   // engineer

    // Update with string params (unused params can be empty strings)
    let _ = sql_exec_str4(db, "UPDATE users SET role = $1 WHERE name = $2", "senior engineer", "Ada", "", "")

    // Verify update
    let updated = sql_query_str(db, "SELECT role FROM users WHERE name = $1", "Ada")
    print(updated)   // senior engineer

    // Delete
    let _ = sql_exec_plain(db, "DELETE FROM users WHERE name = 'Grace'")

    sql_close(db)
    let _ = remove_file("/tmp/mako_crud.db")
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

## Concurrent Cache with CMap

Use `CMap` as a shared cache across worker tasks -- no channels or mutexes needed:

```mko
fn fetch_and_cache(cache: CMap, key: string) -> int {
    // Check cache first
    if cmap_has(cache, key) == 1 {
        return 0
    }
    // Simulate expensive computation
    sleep_ms(10)
    let value = "result_for_" + key
    cmap_set(cache, key, value)
    let _ = cmap_incr(cache, "misses", 1)
    return 1
}

fn main() {
    let cache = cmap_new()
    let keys = ["user:1", "user:2", "user:3", "user:1", "user:2"]

    crew t {
        for i in range keys {
            let _ = t.kick(fetch_and_cache(cache, keys[i]))
        }
    }

    // After all workers complete:
    print(cmap_get(cache, "user:1"))   // "result_for_user:1"
    print(cmap_get(cache, "user:2"))   // "result_for_user:2"
    print(cmap_get(cache, "user:3"))   // "result_for_user:3"
    print_int(cmap_len(cache))         // 4 (3 keys + "misses" counter)
    print_int(cmap_incr(cache, "misses", 0))  // 3 (read counter)
}
```

CMap handles all synchronization internally. Multiple tasks can read and write
the same keys concurrently. Use `cmap_incr` for atomic counters (e.g., cache
hit/miss stats).

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

---

## MMap-Based Data Storage

A simple key-value store using memory-mapped files for persistence. Records are
stored at fixed-size slots, enabling O(1) access by index:

```mko
fn slot_offset(index: int) -> int {
    // Each slot is 256 bytes (fixed-width record)
    return index * 256
}

fn store_put(m: MMap, index: int, key: string, value: string) {
    let record = key + "=" + value
    let _ = mmap_write(m, slot_offset(index), record)
}

fn store_get(m: MMap, index: int, max_len: int) -> string {
    return mmap_read(m, slot_offset(index), max_len)
}

fn main() {
    let path = "/tmp/mako_kvstore.dat"
    let slot_size = 256
    let max_slots = 1024
    let total_size = slot_size * max_slots   // 256 KB

    // Create the store
    let m = mmap_create(path, total_size)

    // Write some records
    store_put(m, 0, "user:1", "Alice")
    store_put(m, 1, "user:2", "Bob")
    store_put(m, 2, "user:3", "Carol")

    // Flush to disk for durability
    let _ = mmap_sync(m, 0)

    // Read back
    let rec = store_get(m, 1, 64)
    print(rec)   // "user:2=Bob"

    let _ = mmap_close(m)

    // Re-open and verify persistence
    let m2 = mmap_open(path, 0)
    let rec2 = store_get(m2, 0, 64)
    print(rec2)   // "user:1=Alice"
    let _ = mmap_close(m2)

    // Clean up
    let _ = remove_file(path)
}
```

This pattern forms the basis of embedded storage engines: fixed-size pages with
known offsets enable lock-free concurrent reads (each reader maps the file
independently). For production use, add a free-list or append-only log for
crash recovery.

---

## Binary Protocol Parsing

Parse and build a custom binary protocol (e.g., for a message queue or RPC
frame). This recipe demonstrates the `Buf` type for structured binary I/O:

```mko
// Protocol: [magic:u16be][version:u8][type:u8][length:u32be][payload:bytes][checksum:u32]

fn encode_message(msg_type: int, payload: string) -> string {
    let b = buf_pack_new(256)

    // Header
    buf_write_u16be(b, 0x4D4B)     // "MK" magic
    buf_write_u8(b, 1)             // version 1
    buf_write_u8(b, msg_type)      // message type
    buf_write_u32be(b, len(payload))  // payload length (network order)

    // Payload
    buf_write_str(b, payload)

    // Simple checksum (sum of payload bytes mod 2^32)
    let mut sum = 0
    for i in range len(payload) {
        sum = sum + int(payload[i])
    }
    buf_write_u32(b, sum)

    let wire = buf_to_string(b)
    buf_free(b)
    return wire
}

fn decode_message(wire: string) -> string {
    let r = buf_from_string(wire)

    // Parse header
    let magic = buf_read_u16be(r)
    if magic != 0x4D4B {
        buf_free(r)
        print("error: invalid magic")
        return ""
    }

    let version = buf_read_u8(r)
    let msg_type = buf_read_u8(r)
    let length = buf_read_u32be(r)

    // Read payload
    let payload = buf_read_str(r, length)

    // Verify checksum
    let checksum = buf_read_u32(r)
    let mut sum = 0
    for i in range len(payload) {
        sum = sum + int(payload[i])
    }
    if sum != checksum {
        buf_free(r)
        print("error: checksum mismatch")
        return ""
    }

    buf_free(r)
    print_int(version)     // 1
    print_int(msg_type)    // message type
    return payload
}

fn main() {
    // Encode a request message (type=1)
    let wire = encode_message(1, "{\"action\":\"ping\"}")

    // Decode it
    let payload = decode_message(wire)
    print(payload)   // {"action":"ping"}

    // Encode a response (type=2)
    let resp = encode_message(2, "{\"status\":\"ok\"}")
    let resp_payload = decode_message(resp)
    print(resp_payload)   // {"status":"ok"}
}
```

This pattern applies to any length-prefixed binary protocol: database wire
formats, game networking packets, sensor data streams, or custom RPC frames.
The `Buf` type handles byte ordering so you can focus on the protocol logic.

---

## Event-Driven Server (Event Loop)

A non-blocking server using the event loop that handles many connections without
one thread per client:

```mko
fn main() {
    let el = evloop_new()
    let server = nb_listen(8080)
    let _ = evloop_add(el, server, 1)
    print("event-driven server on :8080")

    let mut served = 0
    while served < 100 {
        let n = evloop_wait(el, 1000)
        let mut i = 0
        while i < n {
            let fd = evloop_event_fd(el, i)
            if fd == server {
                let client = nb_accept(server)
                if client >= 0 {
                    let _ = evloop_add(el, client, 1)
                }
            } else {
                let data = nb_read(fd)
                if len(data) > 0 {
                    let _ = nb_write(fd, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nok\n")
                }
                let _ = evloop_del(el, fd)
                let _ = nb_close(fd)
                served = served + 1
            }
            i = i + 1
        }
    }
    let _ = nb_close(server)
    let _ = evloop_close(el)
}
```

Build and test:

```bash
mako build evserver.mko -o out/evserver
out/evserver &
curl http://127.0.0.1:8080/
# ok
```

---

## Game Loop with Tick Rate

A fixed-timestep game loop using `tick_now_us` and `tick_sleep_us` to maintain
a consistent tick rate:

```mko
fn main() {
    let u = game_udp_bind(27015)
    let tick_rate_us = 16666  // ~60 ticks per second
    let max_ticks = 600       // run for ~10 seconds

    let mut tick = 0
    while tick < max_ticks {
        let start = tick_now_us()

        // Process incoming packets
        let data = game_udp_recv(u)
        if len(data) > 0 {
            let peer = game_udp_sender(u)
            // Echo back with tick number
            let _ = game_udp_send(u, peer, "tick:" + string(tick))
        }

        // Broadcast world state to all connected peers
        if game_udp_peers(u) > 0 {
            let _ = game_udp_broadcast(u, "state:" + string(tick))
        }

        tick = tick + 1
        // Sleep the remainder of the tick interval
        let _ = tick_sleep_us(start, tick_rate_us)
    }

    print("game server done, ticks:")
    print_int(tick)
    game_udp_close(u)
}
```

The `tick_sleep_us` function calculates how long has elapsed since `start` and
sleeps only the remaining time to hit the target interval. If the tick took
longer than the interval, it returns immediately (no negative sleep).

---

## Rate-Limited API

Protect an HTTP endpoint with a token-bucket rate limiter:

```mko
fn main() {
    let fd = http_bind(8080)
    if fd < 0 {
        print("bind failed")
        return
    }

    // Allow 10 requests/second with a burst of 5
    let rl = ratelimit_new(10, 5)

    let mut n = 0
    while n < 50 {
        let c = http_accept(fd)
        if c < 0 {
            continue
        }

        if ratelimit_allow(rl) == 1 {
            let remaining = ratelimit_remaining(rl)
            let body = "{\"ok\":true,\"remaining\":" + string(remaining) + "}\n"
            let _ = http_respond_json(c, 200, body)
        } else {
            let _ = http_respond_json(c, 429, "{\"error\":\"rate limit exceeded\"}\n")
        }
        let _ = http_close(c)
        n = n + 1
    }

    ratelimit_free(rl)
    let _ = http_close_listener(fd)
}
```

Build and test:

```bash
mako build ratelimit_api.mko -o out/ratelimit_api
out/ratelimit_api &
# First 5 requests succeed (burst), then throttled to 10/sec
for i in $(seq 1 8); do curl -s http://127.0.0.1:8080/; done
```

---

## Circuit Breaker Pattern

Wrap calls to an unreliable downstream service with a circuit breaker to
fail fast when the service is unhealthy:

```mko
fn call_service(cb: CircuitBreaker) -> int {
    if breaker_allow(cb) == 0 {
        // Circuit is open -- fail fast
        return -1
    }

    // Simulate calling a downstream service
    // In real code, this would be http_get or similar
    let success = 0  // simulate failure for demo

    if success == 1 {
        breaker_success(cb)
        return 1
    } else {
        breaker_failure(cb)
        return 0
    }
}

fn main() {
    // Open after 3 failures, 5-second timeout, 2 half-open probes
    let cb = breaker_new(3, 5000, 2)

    // Simulate requests
    let mut i = 0
    while i < 10 {
        let result = call_service(cb)
        let state = breaker_state(cb)

        if state == 0 {
            print("closed - attempting request")
        } else {
            if state == 1 {
                print("open - failing fast")
            } else {
                print("half-open - probing")
            }
        }
        print_int(result)

        i = i + 1
        sleep_ms(100)
    }

    // Manual reset for recovery
    breaker_reset(cb)
    print_int(breaker_state(cb))  // 0 (closed)
    breaker_free(cb)
}
```

The circuit breaker transitions:
1. **Closed** (normal): requests pass through, failures counted
2. **Open** (protecting): after threshold failures, all requests rejected
3. **Half-open** (probing): after timeout, limited requests allowed to test recovery

This pattern prevents cascading failures in microservice architectures.

## Authenticated API Server

A complete server with login, session creation, cookie management, protected
routes, CSRF tokens, role checking, and logout. Uses `CMap` as a thread-safe
session store.

```mko
let sessions = cmap_new()    // sid -> user
let roles = cmap_new()       // user -> comma-separated roles
let csrf_store = cmap_new()  // sid -> csrf_token

fn handle_login(c: int) {
    let body = http_body(c)
    let user = json_get_string(body, "user")
    let pass = json_get_string(body, "pass")

    // In production, verify credentials against a database.
    // Here we accept a hardcoded demo user.
    if str_eq(user, "admin") == 0 or str_eq(pass, "s3cret") == 0 {
        let _ = http_respond_json(c, 401, "{\"error\":\"bad credentials\"}")
        return
    }

    // Create session
    let sid = session_id_new()
    cmap_set(sessions, sid, user)
    cmap_set(roles, user, "admin,editor")

    // Generate CSRF token and store it
    let token = csrf_token()
    cmap_set(csrf_store, sid, token)

    // Set HttpOnly cookie (SameSite=Lax, Path=/, 24h expiry)
    let cookie = cookie_make("sid", sid, 86400)
    let resp = json_ss("csrf_token", token)
    let _ = http_respond_ct(c, 200, "application/json", resp, cookie)
}

fn require_session(c: int) -> string {
    let cookie_hdr = http_header(c, "Cookie")
    let sid = cookie_get(cookie_hdr, "sid")
    if cmap_has(sessions, sid) == 0 {
        return ""
    }
    return sid
}

fn handle_profile(c: int) {
    let sid = require_session(c)
    if str_eq(sid, "") {
        let _ = http_respond_json(c, 401, "{\"error\":\"unauthorized\"}")
        return
    }
    let user = cmap_get(sessions, sid)
    let user_roles = cmap_get(roles, user)
    let resp = json_ss("user", user, "roles", user_roles)
    let _ = http_respond_json(c, 200, resp)
}

fn handle_admin_action(c: int) {
    let sid = require_session(c)
    if str_eq(sid, "") {
        let _ = http_respond_json(c, 401, "{\"error\":\"unauthorized\"}")
        return
    }

    // Check CSRF token (constant-time)
    let body = http_body(c)
    let submitted_csrf = json_get_string(body, "csrf_token")
    let expected_csrf = cmap_get(csrf_store, sid)
    if csrf_check(expected_csrf, submitted_csrf) == 0 {
        let _ = http_respond_json(c, 403, "{\"error\":\"CSRF token mismatch\"}")
        return
    }

    // Check role
    let user = cmap_get(sessions, sid)
    let user_roles = cmap_get(roles, user)
    if authz_allow_role(user_roles, "admin") == 0 {
        let _ = http_respond_json(c, 403, "{\"error\":\"admin role required\"}")
        return
    }

    let _ = http_respond_json(c, 200, "{\"action\":\"completed\"}")
}

fn handle_logout(c: int) {
    let sid = require_session(c)
    if str_eq(sid, "") == 0 {
        let _ = cmap_del(csrf_store, sid)
        let _ = cmap_del(sessions, sid)
    }
    let expired = cookie_make("sid", "", 0)   // expire the cookie
    let _ = http_respond_ct(c, 200, "application/json", "{\"ok\":true}", expired)
}

fn route(c: int) {
    let method = http_method(c)
    let path = http_path(c)

    if str_eq(method, "POST") and str_eq(path, "/login") {
        handle_login(c)
    } else {
        if str_eq(method, "GET") and str_eq(path, "/profile") {
            handle_profile(c)
        } else {
            if str_eq(method, "POST") and str_eq(path, "/admin/action") {
                handle_admin_action(c)
            } else {
                if str_eq(method, "POST") and str_eq(path, "/logout") {
                    handle_logout(c)
                } else {
                    let _ = http_respond_json(c, 404, "{\"error\":\"not found\"}")
                }
            }
        }
    }
}

fn main() {
    let fd = http_bind(8080)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("auth server on :8080")

    while 1 == 1 {
        let c = http_accept(fd)
        if c >= 0 {
            route(c)
            let _ = http_close(c)
        }
    }
    let _ = http_close_listener(fd)
}
```

Build and test:

```bash
mako build auth_server.mko -o out/auth_server
out/auth_server &

# Login and get session cookie + CSRF token
curl -s -c cookies.txt -X POST http://127.0.0.1:8080/login \
  -d '{"user":"admin","pass":"s3cret"}'
# {"csrf_token":"..."}

# Access protected route with session cookie
curl -s -b cookies.txt http://127.0.0.1:8080/profile
# {"user":"admin","roles":"admin,editor"}

# Admin action with CSRF token
curl -s -b cookies.txt -X POST http://127.0.0.1:8080/admin/action \
  -d '{"csrf_token":"<token from login response>"}'
# {"action":"completed"}

# Logout (expires cookie, removes session)
curl -s -b cookies.txt -X POST http://127.0.0.1:8080/logout
# {"ok":true}
```

This recipe demonstrates:
- **Session creation** with cryptographic session IDs (`session_id_new`)
- **Cookie management** with secure defaults (`cookie_make`: HttpOnly, SameSite=Lax, Path=/)
- **CSRF protection** with per-session tokens and constant-time verification (`csrf_token`, `csrf_check`)
- **Role-based access control** checking user roles against required roles (`authz_allow_role`)
- **Logout** by deleting session data and expiring the cookie
- **Thread-safe state** using `CMap` for concurrent session storage

For bearer-token APIs (mobile clients, service-to-service), replace the cookie
flow with `auth_check_bearer`. For signed stateless tokens, use
`auth_token_sign` / `auth_token_check`.

---

## Checked Arithmetic

Use checked arithmetic when overflow is a possible error condition rather than
a bug. The `checked_*` functions return `Result[int, string]`, so overflow
becomes a normal error you can handle with `match` or `?`:

```mko
fn compute_total(prices: []int) -> Result[int, string] {
    let mut total = 0
    for i in range prices {
        match checked_add(total, prices[i]) {
            Ok(v) => { total = v }
            Err(e) => return error("overflow computing total")
        }
    }
    return Ok(total)
}

fn main() {
    let prices = [1000000000, 2000000000, 3000000000]
    match compute_total(prices) {
        Ok(t) => print_int(t),
        Err(e) => log_error(e),
    }

    // Quick check without performing the operation
    let a = 9223372036854775800
    let b = 100
    if would_overflow_add(a, b) == 1 {
        print("would overflow, using fallback")
    }
}
```

---

## Graceful HTTP Shutdown

A production HTTP server that drains in-flight requests on SIGTERM/SIGINT:

```mko
fn handle(c: int) {
    let path = http_path(c)
    if str_eq(path, "/health") {
        let _ = http_respond_json(c, 200, "{\"ok\":true}\n")
    } else {
        let _ = http_respond_json(c, 200, "{\"hello\":\"world\"}\n")
    }
}

fn main() {
    install_graceful_shutdown()
    let fd = http_bind(8080)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("listening on :8080")

    // Accept loop — exits when signal arrives
    while shutdown_requested() == 0 {
        let c = http_accept(fd)
        if c < 0 { continue }
        handle(c)
        let _ = http_close(c)
    }

    // Drain phase: give in-flight work 5 seconds to finish
    server_shutdown_begin(5000)
    server_drain(5000)
    let _ = http_close_listener(fd)
    print("graceful exit complete")
}
```

Build and test:

```bash
mako build server.mko -o out/server
out/server &
curl http://127.0.0.1:8080/health
kill -TERM $!    # triggers graceful drain
```

---

## Distributed Tracing

Propagate trace IDs across HTTP services for request correlation:

```mko
fn handle_request(c: int) {
    // Extract or generate trace ID from incoming request
    let tid = middleware_trace(c)

    trace_begin("handle_request")
    trace_log("processing path=" + http_path(c))

    // ... do work ...
    sleep_ms(5)

    trace_end()

    // Include trace ID in response for client correlation
    let body = "{\"trace_id\":\"" + tid + "\"}\n"
    let _ = http_respond_json(c, 200, body)
}

fn main() {
    let fd = http_bind(8080)
    if fd < 0 { return }

    let mut n = 0
    while n < 100 {
        let c = http_accept(fd)
        if c < 0 { continue }
        handle_request(c)
        let _ = http_close(c)
        n = n + 1
    }
    let _ = http_close_listener(fd)
}
```

For outgoing calls, propagate the trace ID via headers:

```mko
fn call_downstream(url: string) -> string {
    let tid = trace_id()
    // Pass trace ID as X-Trace-Id header to downstream
    trace_begin("call_downstream")
    let resp = http_get(url)
    trace_end()
    return resp
}
```

---

## Collections recipes

Practical map and slice patterns. Full grid: [howto/10-collections](../../howto/10-collections.md)
· language tour [ch03](ch03-language-tour.md).

### Set membership

```mko
fn is_known(seen: map[string]bool, name: string) -> bool {
    return has(seen, name)
}

fn main() {
    let mut seen = make(map[string]bool)
    seen["alice"] = true
    seen["bob"] = true
    if is_known(seen, "alice") {
        print("known")
    }
}
```

### Group by key

```mko
fn main() {
    let mut groups = make(map[string][]int)
    groups["even"] = [2, 4, 6]
    groups["odd"] = [1, 3, 5]
    for k, row in range groups {
        print(k)
        print_int(len(row))
    }
}
```

### Nested config table (depth 2)

```mko
fn main() {
    let mut cfg = make(map[string]map[string]int)
    let mut db = make(map[string]int)
    db["port"] = 5432
    db["pool"] = 16
    cfg["database"] = db
    print_int(cfg["database"]["port"])
}
```

### Nested table depth 3

```mko
fn main() {
    let mut grid = make(map[string]map[string]map[string]int)
    let mut mid = make(map[string]map[string]int)
    let mut leaf = make(map[string]int)
    leaf["n"] = 1
    mid["row"] = leaf
    grid["a"] = mid
    print_int(grid["a"]["row"]["n"])
}
```

### Named mailboxes (`map[K]chan[T]`)

```mko
fn main() {
    let mut inbox = make(map[string]chan[int])
    let ch = chan_open[int](4)
    inbox["worker"] = ch
    let _ = inbox["worker"].send(42)
    print_int(inbox["worker"].recv())
    ch.close()
}
```

### Worker pools (`map[K][]chan[T]`)

```mko
fn main() {
    let mut pools = make(map[string][]chan[int])
    let a = chan_open[int](1)
    let b = chan_open[int](1)
    pools["team"] = [a, b]
    let _ = pools["team"][0].send(1)
    print_int(pools["team"][0].recv())
    a.close()
    b.close()
}
```

### Optional and fallible values per key

```mko
fn main() {
    let mut cache = make(map[string]Option[int])
    cache["hits"] = Some(10)
    cache["misses"] = None
    match cache["hits"] {
        Some(n) => print_int(n),
        None => print("empty"),
    }

    let mut jobs = make(map[int]Result[string, string])
    jobs[1] = Ok("done")
    jobs[2] = Err("timeout")
    match jobs[2] {
        Ok(s) => print(s),
        Err(e) => print(e),
    }
}
```

### Optional whole map

```mko
fn load_scores() -> Option[map[string]int] {
    let mut m = make(map[string]int)
    m["a"] = 100
    return Some(m)
}

fn main() {
    match load_scores() {
        Some(m) => print_int(m["a"]),
        None => print("no scores"),
    }
}
```

### Struct keys without hand-rolled hash

```mko
struct Point { x: int, y: int }

fn main() {
    let mut cells = make(map[Point]string)
    cells[Point { x: 0, y: 0 }] = "origin"
    cells[Point { x: 1, y: 2 }] = "cell"
    print(cells[Point { x: 0, y: 0 }])
}
```

### Bulk helpers

```mko
fn main() {
    let mut m = make(map[string]int)
    m["a"] = 1
    m["b"] = 2
    let ks = maps_keys(m)
    let vs = maps_values(m)
    let c = maps_clone(m)
    assert_eq(maps_equal(m, c), 1)
    maps_clear(c)
    print_int(len(c))   // 0
}
```

---

Next: [Appendix](ch15-appendix.md).
