# 9. Data: JSON, SQL, and Files

This chapter covers Mako's data handling capabilities: JSON encoding/decoding,
SQLite and PostgreSQL database access, file I/O, and the broader encoding family.
All database APIs enforce parameterized queries to prevent injection attacks.

---

## File I/O

### Reading and writing files

```mko
fn main() {
    // Write a file
    let _ = write_file("/tmp/config.txt", "port = 8080\nhost = 0.0.0.0\n")

    // Read it back
    let content = read_file("/tmp/config.txt")
    print(content)
}
```

### Path helpers

```mko
import "path"

fn main() {
    let p = path_join("data", "users.json")
    print(p)                                // data/users.json

    let clean = path_clean("/a/../b/./c")
    print(clean)                            // /b/c
}
```

### Environment variables

```mko
fn main() {
    let _ = env_set("APP_ENV", "production")
    let env = env_get("APP_ENV")
    print(env)      // production
}
```

### Buffered I/O

For processing large files line by line:

```mko
fn main() {
    let content = read_file("data.csv")
    let lines = str_split(content, "\n")
    let mut count = 0
    for line in lines {
        if len(line) > 0 {
            count = count + 1
        }
    }
    print_int(count)
}
```

### File path safety

Always validate paths before file operations:

```mko
fn safe_read(path: string) -> string {
    if str_contains(path, "..") {
        print("error: path traversal rejected")
        return ""
    }
    return read_file(path)
}
```

---

## JSON

Mako provides both low-level JSON helpers and a derive macro for struct
serialization.

### Building JSON objects

#### json_ss (string key-value pairs)

```mko
fn main() {
    let obj = json_ss("name", "Ada", "city", "London")
    print(obj)
    // {"name":"Ada","city":"London"}
}
```

`json_ss` takes alternating key-value string arguments and produces a JSON
object string.

#### json_object_from_map_ss

Build JSON from a map:

```mko
fn main() {
    let mut m = make(map[string]string, 4)
    m["name"] = "Grace"
    m["role"] = "engineer"
    m["team"] = "platform"

    let obj = json_object_from_map_ss(m)
    print(obj)
    // {"name":"Grace","role":"engineer","team":"platform"}
}
```

#### json_object_str (single key-value)

```mko
fn main() {
    let field = json_object_str("status", "active")
    print(field)    // {"status":"active"}
}
```

### Extracting values

#### json_get_string

```mko
fn main() {
    let obj = json_ss("name", "Ada", "age", "36")
    let name = json_get_string(obj, "name")
    print(name)     // Ada
}
```

#### json_get_int

```mko
fn main() {
    let obj = "{\"count\":42,\"name\":\"test\"}"
    let count = json_get_int(obj, "count")
    print_int(count)    // 42
}
```

#### json_get_object (nested object extraction)

```mko
fn main() {
    let doc = "{\"user\":{\"name\":\"Ada\",\"age\":36}}"
    let user = json_get_object(doc, "user")
    print(user)     // {"name":"Ada","age":36}

    let name = json_get_string(user, "name")
    print(name)     // Ada
}
```

### Nested JSON

#### json_nest (wrap object under a key)

```mko
fn main() {
    let addr = json_ss("city", "Paris", "zip", "75001")
    let nested = json_nest("address", addr)
    print(nested)
    // {"address":{"city":"Paris","zip":"75001"}}
}
```

#### json_merge (combine two objects)

```mko
fn main() {
    let person = json_ss("name", "Ada", "age", "36")
    let addr = json_nest("address", json_ss("city", "Paris", "zip", "75001"))
    let doc = json_merge(person, addr)
    print(doc)
    // {"name":"Ada","age":"36","address":{"city":"Paris","zip":"75001"}}
}
```

#### json_path_string / json_path_int (deep extraction)

```mko
fn main() {
    let doc = "{\"user\":{\"name\":\"Ada\",\"address\":{\"city\":\"Paris\"}}}"
    let city = json_path_string(doc, "address", "city")
    print(city)     // Paris
}
```

### JSON Arrays

```mko
fn main() {
    // Create arrays
    let nums = json_array_ints3(1, 2, 3)
    print(nums)     // [1,2,3]

    let strs = json_array_strings2("hello", "world")
    print(strs)     // ["hello","world"]

    // Push to arrays
    let more = json_array_push_string(strs, "mako")
    print(more)     // ["hello","world","mako"]

    let more_nums = json_array_push_int(nums, 4)
    print(more_nums)    // [1,2,3,4]

    // Length
    print_int(json_array_len(more))     // 3

    // Access elements
    let first = json_array_get_string(more, 0)
    print(first)    // hello

    let second_num = json_array_get_int(more_nums, 1)
    print_int(second_num)   // 2
}
```

### derive(json) — Struct Serialization

The `#[derive(json)]` attribute generates serialization and deserialization
helpers for structs:

```mko
#[derive(json)]
struct Person {
    name: string
    age: int
}

fn main() {
    // Serialize: generates Person_to_json(name, age)
    let j = Person_to_json("Ada", 36)
    print(j)
    // {"name":"Ada","age":36}

    // Deserialize individual fields:
    let name = Person_name_from_json(j)
    let age = Person_age_from_json(j)
    print(name)         // Ada
    print_int(age)      // 36

    // Roundtrip
    let j2 = Person_to_json(name, age)
    print(j2)           // {"name":"Ada","age":36}
}
```

#### Nested structs with derive

```mko
#[derive(json)]
struct Address {
    city: string
    zip: int
}

#[derive(json)]
struct Person {
    name: string
    age: int
}

fn main() {
    let addr = Address_to_json("Paris", 75001)
    let person = Person_to_json("Ada", 36)

    // Combine with nesting
    let nested = json_nest("addr", addr)
    let doc = json_merge(person, nested)
    print(doc)

    // Extract nested fields
    let city = json_path_string(doc, "addr", "city")
    let zip = json_path_int(doc, "addr", "zip")
    print(city)         // Paris
    print_int(zip)      // 75001
}
```

#### What derive(json) generates

For a struct `Foo` with fields `x: string` and `y: int`, the derive generates:

- `Foo_to_json(x: string, y: int) -> string` — serialize to JSON string
- `Foo_x_from_json(json: string) -> string` — extract field x
- `Foo_y_from_json(json: string) -> int` — extract field y

---

## SQLite

SQLite is supported when compiled with `-DMAKO_HAS_SQLITE -lsqlite3`. The API
uses parameterized queries exclusively.

### Basic queries

```mko
fn main() {
    let db = "/tmp/mako_demo.sqlite"

    // DDL (no parameters needed for schema)
    let _ = sqlite_query_int(db, "CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")
    let _ = sqlite_query_int(db, "DELETE FROM users")

    // Insert
    let _ = sqlite_query_int(db, "INSERT INTO users(name, age) VALUES ('Ada', 36)")

    // Query integer result
    let count = sqlite_query_int(db, "SELECT COUNT(*) FROM users")
    print_int(count)    // 1

    // Query text result
    let name = sqlite_query_text(db, "SELECT name FROM users LIMIT 1")
    print(name)         // Ada
}
```

### Parameterized queries

**Always use parameters for user-supplied data.** Never concatenate strings into
SQL.

```mko
fn main() {
    let db = "/tmp/mako_params.sqlite"
    let _ = sqlite_query_int(db, "CREATE TABLE IF NOT EXISTS items(id INTEGER, value INTEGER)")

    // Parameterized insert: ? placeholders bind positionally
    let _ = sqlite_query_int_params(db, "INSERT INTO items(id, value) VALUES (?, ?)", 1, 42)
    let _ = sqlite_query_int_params(db, "INSERT INTO items(id, value) VALUES (?, ?)", 2, 99)

    // Parameterized select
    let val = sqlite_query_int_params(db, "SELECT value FROM items WHERE id = ?", 1)
    print_int(val)      // 42
}
```

### Persistent handle API

For multiple queries against the same database, open a handle once:

```mko
fn main() {
    let db = sqlite_open("/tmp/mako_handle.sqlite")

    // Execute with handle (avoids repeated open/close)
    let _ = sqlite_exec(db, "CREATE TABLE IF NOT EXISTS kv(key TEXT, val INTEGER)")
    let _ = sqlite_exec(db, "INSERT INTO kv(key, val) VALUES ('x', 10)")

    let result = sqlite_query_int_handle(db, "SELECT val FROM kv WHERE key = 'x'")
    print_int(result)   // 10

    // Prepared statements for repeated queries
    let stmt = sqlite_prepare(db, "SELECT val FROM kv WHERE key = ?")
    let v1 = sqlite_stmt_query_int(db, stmt, "x")
    print_int(v1)       // 10

    let _ = sqlite_finalize_stmt(stmt)
    let _ = sqlite_close(db)
}
```

### Error handling

SQLite functions print errors to stderr and return sentinel values:
- `sqlite_query_int` returns -1 on error
- `sqlite_query_text` returns "" on error
- `sqlite_open` returns a null handle on error

Check return values in production code.

---

## PostgreSQL

PostgreSQL access requires linking with libpq (`-DMAKO_HAS_LIBPQ -lpq`).

### Connecting

```mko
fn main() {
    let c = pg_connect("host=127.0.0.1 port=5432 dbname=myapp user=mako password=secret connect_timeout=3")

    // Verify connection
    if pg_ok(c) == 1 {
        print("connected to postgres")
    } else {
        print("connection failed")
        return
    }

    // ... use connection ...

    let _ = pg_close(c)
}
```

The connection string uses standard libpq format (key=value pairs).

### Executing queries

```mko
fn main() {
    let c = pg_connect("host=127.0.0.1 port=5432 dbname=myapp user=mako password=mako")
    assert_eq(pg_ok(c), 1)

    // Simple exec (DDL, INSERT, UPDATE, DELETE)
    let _ = pg_exec(c, "CREATE TABLE IF NOT EXISTS users(id SERIAL, name TEXT, age INT)")
    let _ = pg_exec(c, "DELETE FROM users")

    // Row count query
    let rows = pg_exec_row_count(c, "SELECT 1 AS x")
    print_int(rows)     // 1

    let _ = pg_close(c)
}
```

### Parameterized queries

```mko
fn main() {
    let c = pg_connect("host=127.0.0.1 port=5432 dbname=myapp user=mako password=mako")
    assert_eq(pg_ok(c), 1)

    // Parameterized exec with $1, $2, ... placeholders
    let _ = pg_exec_params(c, "INSERT INTO users(name, age) VALUES ($1, $2)", "Ada", 36)
    let _ = pg_exec_params(c, "INSERT INTO users(name, age) VALUES ($1, $2)", "Grace", 40)

    let _ = pg_close(c)
}
```

### Prepared statements

For repeated queries, prepare once and execute many times:

```mko
fn main() {
    let c = pg_connect("host=127.0.0.1 port=5432 dbname=myapp user=mako password=mako")
    assert_eq(pg_ok(c), 1)

    // Prepare a named statement
    let _ = pg_prepare_name(c, "find_user", "SELECT age FROM users WHERE name = $1")

    // Execute prepared statement multiple times
    let _ = pg_exec_prepared(c, "find_user", "Ada")
    let _ = pg_exec_prepared(c, "find_user", "Grace")

    let _ = pg_close(c)
}
```

### Connection failure handling

```mko
fn main() {
    let c = pg_connect("host=127.0.0.1 port=9999 dbname=noexist connect_timeout=1")

    if pg_ok(c) != 1 {
        print("postgres not available — running in degraded mode")
        // Fallback to SQLite or cached data
        let val = sqlite_query_int(":memory:", "SELECT 42")
        print_int(val)
        return
    }

    let _ = pg_close(c)
}
```

---

## Data Modeling Patterns

### Configuration store

```mko
#[derive(json)]
struct Config {
    port: int
    host: string
}

fn load_config(path: string) -> string {
    let raw = read_file(path)
    return raw
}

fn main() {
    let raw = "{\"port\":8080,\"host\":\"0.0.0.0\"}"
    let port = Config_port_from_json(raw)
    let host = Config_host_from_json(raw)
    print_int(port)
    print(host)
}
```

### Request/response cycle with JSON

```mko
#[derive(json)]
struct CreateUserReq {
    name: string
    email: string
}

#[derive(json)]
struct CreateUserResp {
    id: int
    name: string
}

fn handle_create_user(body: string) -> string {
    let name = CreateUserReq_name_from_json(body)
    let email = CreateUserReq_email_from_json(body)

    if len(name) == 0 {
        return json_ss("error", "name is required")
    }

    // Insert into database
    let db = "/tmp/users.sqlite"
    let _ = sqlite_query_int(db, "CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, email TEXT)")
    let _ = sqlite_query_int(db, "INSERT INTO users(name, email) VALUES ('" + name + "', '" + email + "')")

    // Return response (in production, use params API)
    let id = sqlite_query_int(db, "SELECT last_insert_rowid()")
    return CreateUserResp_to_json(id, name)
}
```

### Unified SQL with String Parameters

The unified `sql_*` functions work with both SQLite and Postgres through a single
`SqlDB` handle. Use `sql_exec_plain` for DDL, `sql_exec_str4` for parameterized
inserts with text values, and `sql_query_str` to retrieve a single string result:

```mko
fn main() {
    let db = sql_open_sqlite("/tmp/mako_unified.db")

    // DDL — no parameters needed
    let _ = sql_exec_plain(db, "CREATE TABLE IF NOT EXISTS contacts(id INTEGER PRIMARY KEY, name TEXT, email TEXT, phone TEXT, city TEXT)")

    // INSERT with string parameters (up to 4). Prefer `?` on SQLite; `$1..$N` also works.
    let _ = sql_exec_str4(db, "INSERT INTO contacts(name, email, phone, city) VALUES (?, ?, ?, ?)", "Ada", "ada@example.com", "+1-555-0100", "London")
    let _ = sql_exec_str4(db, "INSERT INTO contacts(name, email, phone, city) VALUES (?, ?, ?, ?)", "Grace", "grace@example.com", "+1-555-0200", "New York")
    print_int(sql_last_insert_id(db))
    print_int(sql_rows_affected(db))

    // SELECT returning a text value
    let email = sql_query_str(db, "SELECT email FROM contacts WHERE name = ?", "Ada")
    print(email)    // ada@example.com

    // Returns "" when no rows match
    let missing = sql_query_str(db, "SELECT email FROM contacts WHERE name = ?", "Nobody")
    print(missing)  // (empty string)

    sql_close(db)
    let _ = remove_file("/tmp/mako_unified.db")
}
```

These complement `sql_exec(db, sql, []int)` (integer params only). Use
`sql_exec_str4` for text columns with user-supplied data — never string-concat
SQL. `sql_last_insert_id` / `sql_rows_affected` report connection-local meta
after mutating statements.

### Multi-row queries

Walk result sets with a short cursor (no ORM ceremony):

```mko
fn main() {
    let db = sql_open_sqlite("/tmp/rows_demo.db")
    defer sql_close(db)
    let _ = sql_exec_plain(db,
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, qty INTEGER)")
    let _ = sql_exec_str4(db, "INSERT INTO items (name, qty) VALUES (?, ?)", "apple", "3", "", "")
    let _ = sql_exec_str4(db, "INSERT INTO items (name, qty) VALUES (?, ?)", "banana", "5", "", "")

    let empty = make([]int, 0)
    let rows = sql_query_rows(db, "SELECT id, name, qty FROM items ORDER BY id", empty)
    while sql_rows_next(rows) == 1 {
        print_int(sql_rows_int(rows, 0))
        print(sql_rows_str(rows, 1))
        print_int(sql_rows_int(rows, 2))
    }
    sql_rows_close(rows)

    // Or pull one column in bulk:
    let names = sql_query_col_str(db, "SELECT name FROM items", 100)
    print_int(len(names))
    let _ = remove_file("/tmp/rows_demo.db")
}
```

Filter with a string param: `sql_query_rows_str(db, "SELECT … WHERE name = ?", "apple")`.

### Multi-store pattern

Use the unified SQL interface for SQLite and Postgres interchangeably:

```mko
fn query_user_count(driver: string, connstr: string) -> int {
    if str_eq(driver, "sqlite") {
        return sqlite_query_int(connstr, "SELECT COUNT(*) FROM users")
    }
    if str_eq(driver, "postgres") {
        let c = pg_connect(connstr)
        let count = pg_exec_row_count(c, "SELECT * FROM users")
        let _ = pg_close(c)
        return count
    }
    return -1
}
```

---

## Encoding Family

Beyond JSON, Mako provides encoders/decoders for common formats:

### Base64

```mko
fn main() {
    let encoded = base64_encode("hello mako")
    print(encoded)                          // aGVsbG8gbWFrbw==

    let decoded = base64_decode(encoded)
    print(decoded)                          // hello mako
}
```

### Hex

```mko
fn main() {
    let h = hex_encode("binary data")
    print(h)                    // hex string

    let original = hex_decode(h)
    print(original)             // binary data
}
```

### CSV

```mko
fn main() {
    // Escape values that contain commas, quotes, or newlines
    let safe = csv_escape("hello, world")
    print(safe)     // "hello, world"

    let quoted = csv_escape("she said \"hi\"")
    print(quoted)   // "she said ""hi"""
}
```

### XML

```mko
fn main() {
    let safe = xml_escape("<script>alert('xss')</script>")
    print(safe)     // &lt;script&gt;alert('xss')&lt;/script&gt;
}
```

### Gzip compression

```mko
fn main() {
    let data = "repeated data repeated data repeated data"
    let compressed = gzip_compress(data)
    print_int(len(compressed))      // smaller than original

    let restored = gzip_decompress(compressed)
    print(restored)                 // original string
}
```

### Binary (little-endian / big-endian)

```mko
fn main() {
    let le = binary_le_u32(0x12345678)
    let be = binary_be_u32(0x12345678)
    // le and be are byte representations in respective endianness
}
```

---

## Idioms and Best Practices

1. **Never concatenate SQL strings.** Always use the `_params` variants for
   user-supplied data:
   ```mko
   // WRONG - SQL injection risk
   let _ = sqlite_query_int(db, "SELECT * FROM users WHERE name = '" + input + "'")

   // CORRECT - parameterized
   let _ = sqlite_query_int_params(db, "SELECT * FROM users WHERE name = ?", input)
   ```

2. **Validate file paths** before read/write operations:
   ```mko
   if str_contains(path, "..") {
       print("error: directory traversal blocked")
       return
   }
   ```

3. **Use arenas** for temporary decode buffers that can be freed together.

4. **Wipe secrets** from memory after use:
   ```mko
   let key = secret_from_str("my-api-key")
   // ... use key ...
   secret_drop(key)
   ```

5. **Close database handles** when done. Use the persistent handle API for
   multiple queries to avoid repeated open/close overhead.

6. **Check error returns** from all database operations. Production code should
   handle -1 from `sqlite_query_int` and failed `pg_ok` checks gracefully.

7. **Use derive(json)** for structured data with known schemas. Use the
   manual `json_ss` / `json_get_string` helpers for dynamic or ad-hoc JSON.

---

## Complete Example: JSON API with SQLite Backend

```mko
#[derive(json)]
struct Todo {
    id: int
    title: string
}

fn init_db(db: string) {
    let _ = sqlite_query_int(db, "CREATE TABLE IF NOT EXISTS todos(id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT NOT NULL)")
}

fn add_todo(db: string, title: string) -> int {
    let _ = sqlite_query_int(db, "INSERT INTO todos(title) VALUES ('" + title + "')")
    return sqlite_query_int(db, "SELECT last_insert_rowid()")
}

fn get_todo_count(db: string) -> int {
    return sqlite_query_int(db, "SELECT COUNT(*) FROM todos")
}

fn main() {
    let db = "/tmp/mako_todos.sqlite"
    init_db(db)

    // Add some todos
    let id1 = add_todo(db, "Write documentation")
    let id2 = add_todo(db, "Add tests")
    let id3 = add_todo(db, "Ship release")

    print_int(id1)
    print_int(id2)
    print_int(id3)

    // Count
    let total = get_todo_count(db)
    print_int(total)    // 3

    // Build JSON response
    let resp = json_ss("count", format_int(total), "status", "ok")
    print(resp)
}
```

---

## Memory-Mapped Files (MMap)

For workloads that need fast random access to large datasets -- indexes, caches,
shared memory between processes -- Mako provides memory-mapped file I/O through
the `MMap` type.

### Creating and Writing

```mko
fn main() {
    // Create a new 64KB mapped file
    let m = mmap_create("/tmp/mako_store.dat", 65536)

    // Write records at fixed offsets (like a page-based store)
    let _ = mmap_write(m, 0, "record-0001")
    let _ = mmap_write(m, 4096, "record-0002")
    let _ = mmap_write(m, 8192, "record-0003")

    // Flush to disk
    let _ = mmap_sync(m, 0)
    let _ = mmap_close(m)
}
```

### Reading from an Existing Mapping

```mko
fn main() {
    let m = mmap_open("/tmp/mako_store.dat", 0)  // read-only
    let size = mmap_size(m)
    print_int(size)                               // 65536

    let rec = mmap_read(m, 4096, 11)
    print(rec)                                    // "record-0002"

    let _ = mmap_close(m)
}
```

### MMap API Reference

| Function | Signature | Purpose |
|----------|-----------|---------|
| `mmap_create` | `(path: string, size: int) -> MMap` | Create file + map it |
| `mmap_open` | `(path: string, mode: int) -> MMap` | Map existing file |
| `mmap_read` | `(m: MMap, offset: int, count: int) -> string` | Read from mapping |
| `mmap_write` | `(m: MMap, offset: int, data: string) -> int` | Write to mapping |
| `mmap_sync` | `(m: MMap, flags: int) -> int` | Flush changes to disk |
| `mmap_size` | `(m: MMap) -> int` | Size of mapping in bytes |
| `mmap_close` | `(m: MMap) -> int` | Unmap and close |

Use `mmap_sync` after writes to ensure durability. Without it, data lives only
in the page cache and can be lost on a crash.

---

## Binary Protocols with `Buf`

The `Buf` type provides structured binary reading and writing for implementing
wire protocols, file format parsers, and serialization codecs. It handles byte
ordering and typed values so you do not need manual bit shifting.

### Writing a Binary Message

```mko
fn main() {
    let b = buf_pack_new(256)

    // Header: magic (2 bytes BE) + version (1 byte) + payload length (4 bytes)
    buf_write_u16be(b, 0xCAFE)     // magic number
    buf_write_u8(b, 1)             // protocol version
    buf_write_u32(b, 13)           // payload length

    // Payload
    buf_write_str(b, "hello, world!")

    let wire = buf_to_string(b)
    print_int(buf_len(b))          // 20 (2+1+4+13)
    buf_free(b)
}
```

### Parsing a Binary Message

```mko
fn parse_message(wire: string) {
    let r = buf_from_string(wire)

    let magic = buf_read_u16be(r)
    let version = buf_read_u8(r)
    let length = buf_read_u32(r)
    let payload = buf_read_str(r, length)

    print_int(magic)       // 0xCAFE = 51966
    print_int(version)     // 1
    print_int(length)      // 13
    print(payload)         // "hello, world!"

    buf_free(r)
}
```

### Numeric Types and Endianness

```mko
fn main() {
    let b = buf_pack_new(64)

    // Little-endian (default, matches x86/ARM memory layout)
    buf_write_u16(b, 1000)
    buf_write_u32(b, 100000)
    buf_write_u64(b, 9999999999)
    buf_write_i32(b, -42)
    buf_write_f64(b, 3.14159)

    // Big-endian (network byte order)
    buf_write_u16be(b, 80)       // port number
    buf_write_u32be(b, 167772161) // IP 10.0.0.1

    buf_reset(b)

    // Read back in same order
    print_int(buf_read_u16(b))   // 1000
    print_int(buf_read_u32(b))   // 100000
    // ... and so on

    buf_free(b)
}
```

### Buf API Reference

| Function | Purpose |
|----------|---------|
| `buf_pack_new(capacity)` | New buffer for writing |
| `buf_from_string(s)` | Buffer from existing bytes (for reading) |
| `buf_to_string(b)` | Extract contents as string |
| `buf_len(b)` / `buf_pos(b)` | Total bytes written / current read position |
| `buf_reset(b)` / `buf_seek(b, pos)` | Reset position / seek to offset |
| `buf_free(b)` | Release buffer memory |
| `buf_write_u8/u16/u32/u64(b, v)` | Write unsigned integers (LE) |
| `buf_write_u16be/u32be(b, v)` | Write big-endian unsigned |
| `buf_write_i32(b, v)` | Write signed 32-bit int |
| `buf_write_f32/f64(b, v)` | Write IEEE 754 floats |
| `buf_read_u8/u16/u32/u64(b)` | Read unsigned integers (LE) |
| `buf_read_u16be/u32be(b)` | Read big-endian unsigned |
| `buf_read_i32(b)` | Read signed 32-bit int |
| `buf_read_f32/f64(b)` | Read IEEE 754 floats |
| `buf_write_bytes(b, data)` / `buf_write_str(b, s)` | Write raw bytes |
| `buf_read_bytes(b, n)` / `buf_read_str(b, n)` | Read n raw bytes |

---

## Summary

| Area | Key Functions |
|------|--------------|
| Files | `read_file`, `write_file`, `path_join`, `path_clean` |
| JSON build | `json_ss`, `json_object_from_map_ss`, `json_nest`, `json_merge` |
| JSON extract | `json_get_string`, `json_get_int`, `json_get_object`, `json_path_string` |
| JSON arrays | `json_array_ints3`, `json_array_push_string`, `json_array_len`, `json_array_get_string` |
| JSON derive | `#[derive(json)]`, `Type_to_json(...)`, `Type_field_from_json(json)` |
| SQLite | `sqlite_query_int`, `sqlite_query_text`, `sqlite_query_int_params`, `sqlite_open`, `sqlite_close` |
| PostgreSQL | `pg_connect`, `pg_ok`, `pg_exec`, `pg_exec_params`, `pg_prepare_name`, `pg_exec_prepared`, `pg_close` |
| Encoding | `base64_encode/decode`, `hex_encode/decode`, `csv_escape`, `xml_escape`, `gzip_compress/decompress` |

Next: [Packages & tooling workflow](ch10-packages.md).
