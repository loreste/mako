# Database Applications

This tutorial covers SQLite, PostgreSQL, the unified SQL API, data
modeling, and error handling for database operations.

---

## SQLite: Embedded Database

Mako links against `libsqlite3` at compile time. The simplest API uses
`sqlite_query_int` and `sqlite_query_text` with a database file path
and a SQL statement.

```mko
fn main() {
    let db = "/tmp/mako_app.sqlite"

    // Create a table
    let _ = sqlite_query_int(db, "CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY, name TEXT, email TEXT)")

    // Insert rows
    let _ = sqlite_query_int(db, "INSERT INTO users(name, email) VALUES ('Ada', 'ada@example.com')")
    let _ = sqlite_query_int(db, "INSERT INTO users(name, email) VALUES ('Grace', 'grace@example.com')")

    // Query a single integer value
    let count = sqlite_query_int(db, "SELECT COUNT(*) FROM users")
    print("user count:")
    print(count)

    // Query a single text value
    let name = sqlite_query_text(db, "SELECT name FROM users WHERE id = 1")
    print("first user:")
    print(name)

    // Clean up
    let _ = sqlite_query_int(db, "DELETE FROM users")
}
```

---

## Unified SQL API

The unified SQL API works with both SQLite and PostgreSQL through a
common `SqlDB` handle. This lets you write database code once and switch
backends by changing the open call.

```mko
let db = sql_open_sqlite("/tmp/app.db")       // SQLite
let db = sql_open_postgres("host=localhost dbname=myapp")  // PostgreSQL
```

### Creating Tables

Use `sql_exec_plain` for DDL statements that take no parameters.

```mko
fn setup_schema(db: SqlDB) -> int {
    let _ = sql_exec_plain(db,
        "CREATE TABLE IF NOT EXISTS notes(id INTEGER PRIMARY KEY, title TEXT, body TEXT, created_at TEXT)"
    )
    let _ = sql_exec_plain(db,
        "CREATE TABLE IF NOT EXISTS tags(id INTEGER PRIMARY KEY, note_id INTEGER, tag TEXT)"
    )
    return 0
}
```

### Inserting Data

`sql_exec_str4` supports up to four string parameters. Use `$1` through
`$4` as placeholders (works with both SQLite and PostgreSQL).

```mko
fn insert_note(db: SqlDB, title: string, body: string) -> int {
    let result = sql_exec_str4(db,
        "INSERT INTO notes(title, body, created_at) VALUES ($1, $2, $3, $4)",
        title, body, "2026-01-15", ""
    )
    return result  // 0 on success
}
```

For integer-only parameters, use `sql_exec`:

```mko
let args = [42, 100]
let _ = sql_exec(db, "INSERT INTO scores(user_id, score) VALUES ($1, $2)", args)
```

### Querying Data

`sql_query_str` returns the first column of the first row as a string.
Returns `""` if no rows match.

```mko
fn find_note(db: SqlDB, title: string) -> string {
    let body = sql_query_str(db,
        "SELECT body FROM notes WHERE title = $1",
        title
    )
    return body
}
```

For integer results, use `sql_query_int`:

```mko
fn count_notes(db: SqlDB) -> int {
    let args: []int = []
    return sql_query_int(db, "SELECT COUNT(*) FROM notes", args)
}
```

### Closing

Always close the database when done:

```mko
sql_close(db)
```

---

## PostgreSQL

PostgreSQL connectivity uses `libpq`. The connection gracefully fails
when the server is unreachable.

```mko
fn main() {
    let handle = pg_connect("host=localhost port=5432 dbname=myapp")

    // Check connection status
    if pg_ok(handle) == 0 {
        print("cannot connect to postgres")
        return
    }

    // Execute a statement
    let _ = pg_exec(handle,
        "CREATE TABLE IF NOT EXISTS events(id SERIAL, name TEXT, ts TIMESTAMPTZ DEFAULT NOW())"
    )

    // Insert data
    let _ = pg_exec(handle, "INSERT INTO events(name) VALUES ('startup')")

    // Query with row count
    let rows = pg_exec_row_count(handle,
        "SELECT * FROM events"
    )
    print("event rows:")
    print(rows)

    // Close connection
    pg_close(handle)
}
```

### Connection URLs

`pg_connect_url` parses a PostgreSQL URL into libpq connection keywords:

```mko
let handle = pg_connect("postgres://user:pass@localhost:5432/mydb")
if pg_ok(handle) == 0 {
    print("connection failed")
    pg_close(handle)
    return
}
```

When the server goes down, `pg_exec` returns `-1` and `pg_ok` returns
`0`. Always check `pg_ok` before executing queries.

---

## Data Modeling Patterns

### Notes Application Schema

```mko
fn create_schema(db: SqlDB) -> int {
    let _ = sql_exec_plain(db,
        "CREATE TABLE IF NOT EXISTS notes(id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT NOT NULL, body TEXT, created TEXT)"
    )
    return 0
}

fn add_note(db: SqlDB, title: string, body: string) -> int {
    return sql_exec_str4(db,
        "INSERT INTO notes(title, body, created) VALUES ($1, $2, $3, $4)",
        title, body, "2026-01-15T10:00:00Z", ""
    )
}

fn get_note_body(db: SqlDB, title: string) -> string {
    return sql_query_str(db,
        "SELECT body FROM notes WHERE title = $1",
        title
    )
}

fn delete_note(db: SqlDB, title: string) -> int {
    return sql_exec_str4(db,
        "DELETE FROM notes WHERE title = $1",
        title, "", "", ""
    )
}
```

---

## Error Handling for Database Operations

Wrap database calls with proper error checking using `Result`:

```mko
fn safe_insert(db: SqlDB, title: string, body: string) -> Result[int, string] {
    if title == "" {
        return error("title cannot be empty")
    }
    let result = sql_exec_str4(db,
        "INSERT INTO notes(title, body, created) VALUES ($1, $2, $3, $4)",
        title, body, "2026-01-15", ""
    )
    if result < 0 {
        return error("insert failed")
    }
    return Ok(result)
}

fn main() {
    let db = sql_open_sqlite("/tmp/mako_notes.db")
    let _ = sql_exec_plain(db,
        "CREATE TABLE IF NOT EXISTS notes(id INTEGER PRIMARY KEY, title TEXT, body TEXT, created TEXT)"
    )

    match safe_insert(db, "hello", "world") {
        Ok(_) => print("inserted successfully")
        Err(e) => print(e)
    }

    match safe_insert(db, "", "no title") {
        Ok(_) => print("unexpected success")
        Err(e) => print(e)  // "title cannot be empty"
    }

    sql_close(db)
}
```

---

## Complete Example: Notes Database

```mko
fn main() {
    let db = sql_open_sqlite("/tmp/mako_tutorial_notes.db")
    let _ = sql_exec_plain(db,
        "CREATE TABLE IF NOT EXISTS notes(id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT NOT NULL, body TEXT)")
    let _ = sql_exec_plain(db, "DELETE FROM notes")

    // Insert
    let _ = sql_exec_str4(db,
        "INSERT INTO notes(title, body) VALUES ($1, $2, $3, $4)",
        "welcome", "Hello from Mako", "", "")
    let _ = sql_exec_str4(db,
        "INSERT INTO notes(title, body) VALUES ($1, $2, $3, $4)",
        "guide", "See the tutorial", "", "")

    // Read
    let body = sql_query_str(db, "SELECT body FROM notes WHERE title = $1", "welcome")
    print(body)

    // Count
    let args: []int = []
    let count = sql_query_int(db, "SELECT COUNT(*) FROM notes", args)
    print("total notes:")
    print(count)

    sql_close(db)
}
```

---

## API Reference

| Function | Purpose |
|----------|---------|
| `sqlite_query_int(path, sql)` | SQLite: query returning int |
| `sqlite_query_text(path, sql)` | SQLite: query returning text |
| `sql_open_sqlite(path)` | Open SQLite via unified API |
| `sql_open_postgres(connstr)` | Open PostgreSQL via unified API |
| `sql_exec_plain(db, sql)` | Execute DDL/DML (no params) |
| `sql_exec_str4(db, sql, a, b, c, d)` | Execute with up to 4 string params |
| `sql_exec(db, sql, args)` | Execute with int array params |
| `sql_query_str(db, sql, param)` | Query single string result |
| `sql_query_int(db, sql, args)` | Query single int result |
| `sql_close(db)` | Close database connection |
| `pg_connect(connstr)` | Direct PostgreSQL connection |
| `pg_ok(handle)` | Check connection status |
| `pg_exec(handle, sql)` | Execute PostgreSQL statement |
| `pg_exec_row_count(handle, sql)` | Execute and return row count |
| `pg_close(handle)` | Close PostgreSQL connection |
