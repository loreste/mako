# 15. Appendix

Complete reference tables for keywords, operators, types, built-in functions,
compiler flags, and environment variables.

---

## A. Keyword Reference

Mako has **38 reserved words**. These are always keywords and can never be used
as identifiers. Source of truth: `src/lexer/mod.rs`.

### Declarations

| Keyword     | Purpose                                          |
|-------------|--------------------------------------------------|
| `fn`        | Function declaration                             |
| `struct`    | Product type with named fields                   |
| `enum`      | Sum type with variants                           |
| `actor`     | Actor type with receive arms                     |
| `receive`   | Message handler arm inside an actor              |
| `interface` | Named method set (trait-like)                    |
| `extern`    | Foreign function declaration (`extern "C" fn`)   |
| `const`     | Compile-time constant binding                    |
| `import`    | Module import (file, std package, or alias)      |
| `let`       | Local immutable binding                          |
| `mut`       | Mutable marker for bindings or parameters        |

### Control Flow

| Keyword    | Purpose                                           |
|------------|---------------------------------------------------|
| `if`       | Conditional branch                                |
| `else`     | Alternative branch                                |
| `while`    | Loop while condition is true                      |
| `for`      | Iteration over ranges, slices, maps, channels     |
| `in`       | Separator in `for` loops                          |
| `range`    | Range expression (slice, integer, map, channel)   |
| `break`    | Exit the innermost (or labeled) loop              |
| `continue` | Skip to next iteration of innermost (or labeled) loop |
| `return`   | Return from function                              |
| `defer`    | Run on function/scope exit (LIFO order)           |
| `match`    | Pattern match on enums, Option, Result, integers  |

### Literals and Logic

| Keyword | Purpose                                   |
|---------|-------------------------------------------|
| `true`  | Boolean literal true                      |
| `false` | Boolean literal false                     |
| `and`   | Logical AND (same as `&&`)                |
| `or`    | Logical OR (same as `\|\|`)              |
| `not`   | Logical NOT (same as `!`)                 |

### Concurrency

| Keyword   | Purpose                                        |
|-----------|------------------------------------------------|
| `crew`    | Structured concurrency scope                   |
| `kick`    | Spawn work on a crew                           |
| `join`    | Wait for a kicked job to complete              |
| `fan`     | Data-parallel map over a collection            |
| `select`  | Multi-way channel wait                         |
| `timeout` | Select arm: wait up to N milliseconds          |
| `default` | Select arm: non-blocking fallback              |

### Memory and Ownership

| Keyword | Purpose                                         |
|---------|-------------------------------------------------|
| `arena` | Bump-allocation region (freed on scope exit)    |
| `hold`  | Move-on-rebind ownership binding                |
| `share` | Shared/borrowed binding                         |
| `as`    | Type cast or alias in imports                   |

### Alphabetical (Complete List)

```
actor and arena as break const continue crew default defer else enum extern
false fan fn for hold if import in interface join kick let match mut not or
range receive return select share struct timeout true while
```

---

## B. Operator Table

### Assignment

| Operator | Meaning            |
|----------|--------------------|
| `=`      | Assignment (never equality) |

### Comparison

| Operator | Meaning              |
|----------|----------------------|
| `==`     | Equal                |
| `!=`     | Not equal            |
| `<`      | Less than            |
| `>`      | Greater than         |
| `<=`     | Less than or equal   |
| `>=`     | Greater than or equal|

### Logical (Short-Circuit)

| Operator   | Keyword Form | Meaning         |
|------------|--------------|-----------------|
| `&&`       | `and`        | Logical AND     |
| `\|\|`     | `or`         | Logical OR      |
| `!`        | `not`        | Logical NOT     |

`&&` and `||` short-circuit: the right-hand side is not evaluated when
the result is already determined by the left.

### Arithmetic

| Operator | Meaning        |
|----------|----------------|
| `+`      | Addition (also string concatenation) |
| `-`      | Subtraction    |
| `*`      | Multiplication |
| `/`      | Division       |
| `%`      | Modulo         |

### Bitwise

| Operator | Meaning                |
|----------|------------------------|
| `&`      | Bitwise AND            |
| `\|`     | Bitwise OR             |
| `^`      | Bitwise XOR            |
| `&^`     | Bit clear (AND NOT)    |
| `<<`     | Left shift             |
| `>>`     | Right shift            |
| `^x`     | Unary bitwise complement |

### Special

| Syntax       | Meaning                                |
|--------------|----------------------------------------|
| `?`          | Result propagation (early return on Err) |
| `\|x\| expr` | Lambda / closure                       |
| `s[i]`      | Index access (bounds-checked)           |
| `s[i:j]`    | Slice expression                        |
| `.`          | Field access or method call             |

---

## C. Type Reference

### Primitive Types

| Type      | Description                    | Size         |
|-----------|--------------------------------|--------------|
| `int`     | Platform integer               | 64 bits      |
| `int64`   | Signed 64-bit integer          | 64 bits      |
| `int32`   | Signed 32-bit integer          | 32 bits      |
| `int8`    | Signed 8-bit integer           | 8 bits       |
| `uint64`  | Unsigned 64-bit integer        | 64 bits      |
| `byte`    | Unsigned 8-bit integer         | 8 bits       |
| `float64` | 64-bit floating point          | 64 bits      |
| `float`   | Alias for `float64`            | 64 bits      |
| `bool`    | Boolean (`true` / `false`)     | 1 byte       |
| `string`  | UTF-8 text (ptr + length)      | 16 bytes     |

### Composite Types

| Type              | Description                              |
|-------------------|------------------------------------------|
| `[]T`             | Slice of T (ptr, len, cap)               |
| `[]byte`          | Byte slice                               |
| `[]string`        | String slice                             |
| `[]float`         | Float slice                              |
| `map[K]V`         | Hash map (K: string or int, V: any)      |
| `chan[T]`          | Typed channel                            |
| `Option[T]`       | Some(T) or None                          |
| `Result[T, E]`    | Ok(T) or Err(E)                          |
| `struct Name { }` | Named product type                       |
| `enum Name { }`   | Named sum type (with variants)           |

### Type Conversions

| Conversion       | Syntax            | Notes                              |
|------------------|-------------------|------------------------------------|
| int -> int64     | `int64(x)`        | Always valid                       |
| int -> int32     | `int32(x)`        | Runtime check (range)              |
| int -> int8      | `int8(x)`         | Runtime check (-128..127)          |
| int -> uint64    | `uint64(x)`       | Aborts if negative                 |
| int -> byte      | `byte(x)`         | Runtime check (0..255)             |
| int -> float64   | `float64(x)`      | Always valid                       |
| int -> string    | `string(x)`       | Decimal representation             |
| string -> []byte | `bytes(s)` or `[]byte(s)` | Copies bytes              |
| []byte -> string | `string(b)`       | Copies bytes into string           |
| float64 -> int   | `int(f)`          | Truncates toward zero              |

---

## D. Built-in Functions

### Output

| Function          | Signature                       | Purpose              |
|-------------------|---------------------------------|----------------------|
| `print`           | `(string)`                      | Print string + newline |
| `print_int`       | `(int)`                         | Print integer        |
| `print_int64`     | `(int64)`                       | Print int64          |
| `print_int8`      | `(int8)`                        | Print int8           |
| `print_float`     | `(float64)`                     | Print float          |
| `dbg`             | `(int)`                         | Debug print with file:line |
| `dbg_str`         | `(string)`                      | Debug print string with file:line |

### Slice Operations

| Function          | Signature                        | Purpose              |
|-------------------|----------------------------------|----------------------|
| `len`             | `([]T) -> int`                   | Slice/map/string length |
| `cap`             | `([]T) -> int`                   | Slice capacity       |
| `append`          | `([]T, T) -> []T`               | Append element       |
| `copy`            | `([]T, []T) -> int`             | Copy elements, return count |
| `make`            | `([]T, len[, cap]) -> []T`      | Allocate slice       |
| `sort_ints`       | `([]int) -> []int`              | Return sorted copy   |
| `sort_strings`    | `([]string) -> []string`        | Return sorted copy   |
| `ints_contains`   | `([]int, int) -> bool`          | Check membership     |

### Map Operations

| Function          | Signature                        | Purpose              |
|-------------------|----------------------------------|----------------------|
| `make`            | `(map[K]V[, hint]) -> map[K]V`  | Allocate map         |
| `has`             | `(map[K]V, K) -> bool`          | Key presence check   |
| `delete`          | `(map[K]V, K)`                  | Remove key           |
| `len`             | `(map[K]V) -> int`              | Entry count          |

### String Operations

| Function          | Signature                        | Purpose              |
|-------------------|----------------------------------|----------------------|
| `str_len`         | `(string) -> int`               | Byte length (same as `len`) |
| `rune_count`      | `(string) -> int`               | Unicode code point count |
| `str_eq`          | `(string, string) -> bool`      | String equality      |
| `str_contains`    | `(string, string) -> bool`      | Substring check      |
| `str_split`       | `(string, string) -> []string`  | Split by delimiter   |
| `str_join`        | `([]string, string) -> string`  | Join with separator  |
| `str_builder`     | `() -> Builder`                 | Create string builder|
| `builder_write`   | `(Builder, string)`             | Append to builder    |
| `builder_write_byte` | `(Builder, byte)`            | Append byte          |
| `builder_string`  | `(Builder) -> string`           | Finalize builder     |
| `builder_len`     | `(Builder) -> int`              | Current length       |
| `fmt_sprintf`     | `(string, ...) -> string`       | Format string        |

### Math

| Function          | Signature                        | Purpose              |
|-------------------|----------------------------------|----------------------|
| `abs`             | `(int) -> int`                  | Absolute value       |
| `parse_int`       | `(string) -> Result[int, string]` | Parse decimal integer |
| `parse_float`     | `(string) -> float64`           | Parse float          |

### File System

| Function          | Signature                        | Purpose              |
|-------------------|----------------------------------|----------------------|
| `read_file`       | `(string) -> string`            | Read entire file     |
| `write_file`      | `(string, string) -> int`       | Write file (create/overwrite) |
| `append_file`     | `(string, string) -> int`       | Append to file       |
| `file_exists`     | `(string) -> bool`              | Check file existence |
| `is_dir`          | `(string) -> bool`              | Check if path is directory |
| `mkdir`           | `(string) -> int`               | Create directory     |
| `remove_file`     | `(string) -> int`               | Delete file          |

### Environment and Arguments

| Function          | Signature                        | Purpose              |
|-------------------|----------------------------------|----------------------|
| `env_get`         | `(string) -> string`            | Get environment variable |
| `env_set`         | `(string, string) -> int`       | Set environment variable |
| `argc`            | `() -> int`                     | Argument count       |
| `arg_get`         | `(int) -> string`               | Get argument by index|
| `args`            | `() -> []string`                | All arguments as slice |
| `exit`            | `(int)`                         | Exit with status code|

### Time

| Function          | Signature                        | Purpose              |
|-------------------|----------------------------------|----------------------|
| `now_ms`          | `() -> int`                     | Wall clock milliseconds |
| `now_ns`          | `() -> int`                     | Monotonic nanoseconds (for benchmarks) |
| `sleep_ms`        | `(int)`                         | Sleep N milliseconds |
| `time_sleep_ms`   | `(int)`                         | Alias for sleep_ms   |
| `time_format`     | `(int) -> string`               | Format ms as RFC 3339 UTC |
| `elapsed_ms`      | `(int) -> int`                  | Milliseconds since timestamp |

### Path

| Function          | Signature                        | Purpose              |
|-------------------|----------------------------------|----------------------|
| `path_join`       | `(string, string) -> string`    | Join path segments   |
| `path_clean`      | `(string) -> string`            | Normalize path       |

### Logging

| Function          | Signature                        | Purpose              |
|-------------------|----------------------------------|----------------------|
| `log_info`        | `(string)`                      | Info-level log line  |
| `log_warn`        | `(string)`                      | Warning-level log    |
| `log_error`       | `(string)`                      | Error-level log      |
| `log_kv`          | `(string, string, string)`      | Key-value log entry  |

### Encoding

| Function          | Signature                        | Purpose              |
|-------------------|----------------------------------|----------------------|
| `base64_encode`   | `(string) -> string`            | Base64 encode        |
| `base64_decode`   | `(string) -> string`            | Base64 decode        |
| `hex_encode`      | `(string) -> string`            | Hex encode           |
| `hex_decode`      | `(string) -> string`            | Hex decode           |

### Regex

| Function          | Signature                                | Purpose              |
|-------------------|------------------------------------------|----------------------|
| `regex_match`     | `(pattern: string, text: string) -> bool` | Full match test     |
| `regex_find`      | `(pattern: string, text: string) -> string` | First match substring |
| `regex_capture`   | `(pattern: string, text: string, group: int) -> string` | Capture group |

Supported patterns: literals, `.`, `*`, `+`, `?`, `|`, `[abc]`, `[a-z]`,
`[^...]`, `(...)`, `^`, `$`.

### JSON

| Function                 | Purpose                               |
|--------------------------|---------------------------------------|
| `json_object`            | Create empty JSON object              |
| `json_si(key, int)`      | Object with string key, int value     |
| `json_ss(key, string)`   | Object with string key, string value  |
| `json_i(key, int)`       | Integer field                         |
| `json_get_string(json, key)` | Extract string field              |
| `json_get_int(json, key)`    | Extract int field                 |
| `json_get_object(json, key)` | Extract nested object             |
| `json_nest(key, json)`   | Wrap JSON as nested field             |
| `json_merge(a, b)`       | Merge two JSON objects                |
| `json_path_string(json, ...keys)` | Deep string extraction       |
| `json_array_ints3(a,b,c)` | Create int array                    |
| `json_array_push_int`    | Append to int array                   |
| `json_array_strings2(a,b)` | Create string array                 |
| `json_array_push_string` | Append to string array                |
| `json_array_len`         | Array element count                   |
| `json_object_from_map_ss` | Map[string]string to JSON object     |

### Database (Unified SQL)

| Function          | Signature                                                        | Purpose              |
|-------------------|------------------------------------------------------------------|----------------------|
| `sql_exec_plain`  | `(db: SqlDB, sql: string) -> int`                                | Execute SQL with no parameters (DDL, simple statements); returns 0 on success |
| `sql_exec_str4`   | `(db: SqlDB, sql: string, p1: string, p2: string, p3: string, p4: string) -> int` | Execute SQL with up to 4 string parameters ($1..$4); returns 0 on success |
| `sql_query_str`   | `(db: SqlDB, sql: string, p1: string) -> string`                 | Query single string value (first column, first row) with one string param; returns "" if no result |

### UUID

| Function          | Signature                        | Purpose              |
|-------------------|----------------------------------|----------------------|
| `uuid_v4`         | `() -> Uuid`                    | Generate random UUID |
| `uuid_string`     | `(Uuid) -> string`              | Format as string     |
| `uuid_parse`      | `(string) -> Uuid`              | Parse (nil on fail)  |
| `uuid_parse_ok`   | `(string) -> bool`              | Validate format      |
| `uuid_eq`         | `(Uuid, Uuid) -> bool`          | Compare              |
| `uuid_nil`        | `() -> Uuid`                    | Nil UUID             |
| `uuid_is_nil`     | `(Uuid) -> bool`                | Check nil            |
| `uuid_check`      | `(string) -> Result[int, string]` | Validate with error |

### Concurrent Map (CMap)

| Function          | Signature                               | Purpose              |
|-------------------|-----------------------------------------|----------------------|
| `cmap_new`        | `() -> CMap`                            | Create concurrent map |
| `cmap_set`        | `(CMap, string, string)`                | Set key-value pair   |
| `cmap_get`        | `(CMap, string) -> string`              | Get value (`""` if missing) |
| `cmap_has`        | `(CMap, string) -> int`                 | Key exists (1/0)     |
| `cmap_del`        | `(CMap, string) -> int`                 | Delete (1 if existed)|
| `cmap_len`        | `(CMap) -> int`                         | Entry count          |
| `cmap_incr`       | `(CMap, string, int) -> int`            | Atomic increment, returns new value |

### Channels

| Function          | Signature                        | Purpose              |
|-------------------|----------------------------------|----------------------|
| `chan_new`         | `(int) -> chan[T]`              | Create buffered channel |
| `.send`           | `(T) -> int`                    | Send value           |
| `.recv`           | `() -> T`                       | Receive value        |
| `.close`          | `()`                            | Close channel        |
| `chan_select2`     | `(a, b, timeout) -> int`       | Select over 2 channels |
| `chan_select3`     | `(a, b, c, timeout) -> int`    | Select over 3 channels |
| `chan_select4`     | `(a, b, c, d, timeout) -> int` | Select over 4 channels |
| `chan_select_value` | `() -> int`                   | Value from last select |

### Security

| Function          | Signature                        | Purpose              |
|-------------------|----------------------------------|----------------------|
| `const_eq`        | `(string, string) -> int`       | Constant-time compare (1=equal) |
| `secret_from_str` | `(string) -> Secret`            | Create secret handle |
| `secret_drop`     | `(Secret)`                      | Zero and free secret |
| `http_header_ok`  | `(name, val) -> int`            | Reject CR/LF injection |

### Cookies

| Function          | Signature                                                    | Purpose              |
|-------------------|--------------------------------------------------------------|----------------------|
| `cookie_get`      | `(header: string, name: string) -> string`                   | Parse cookie value from Cookie header |
| `cookie_make`     | `(name: string, value: string, max_age: int) -> string`      | Create Set-Cookie string (HttpOnly, SameSite=Lax, Path=/) |

### Sessions

| Function               | Signature                                                              | Purpose              |
|------------------------|------------------------------------------------------------------------|----------------------|
| `session_id_new`       | `() -> string`                                                         | Generate 32-char crypto-random hex session ID |
| `auth_session_cookie`  | `(cookie_header: string, cookie_name: string, expected: string) -> int` | Constant-time session cookie check (1=match, 0=no) |

### CSRF Protection

| Function          | Signature                                          | Purpose              |
|-------------------|----------------------------------------------------|----------------------|
| `csrf_token`      | `() -> string`                                     | Generate random CSRF token |
| `csrf_check`      | `(expected: string, submitted: string) -> int`     | Constant-time comparison (1=match, 0=no) |

### Authentication

| Function              | Signature                                                           | Purpose              |
|-----------------------|---------------------------------------------------------------------|----------------------|
| `auth_bearer`         | `(authorization: string) -> string`                                 | Extract token from "Bearer \<token\>" header |
| `auth_check_bearer`   | `(authorization: string, expected_token: string) -> int`            | Constant-time bearer verification (1=match, 0=no) |
| `auth_basic_header`   | `(user: string, pass: string) -> string`                            | Build "Basic \<base64\>" authorization header |
| `auth_check_basic`    | `(authorization: string, user: string, pass: string) -> int`       | Verify Basic auth credentials (1=match, 0=no) |

### Signed Tokens (HMAC-SHA256)

| Function              | Signature                                          | Purpose              |
|-----------------------|----------------------------------------------------|----------------------|
| `auth_token_sign`     | `(subject: string, secret: string) -> string`      | Sign subject, returns "subject.hmac_signature" |
| `auth_token_check`    | `(token: string, secret: string) -> int`           | Verify signed token (1=valid, 0=invalid) |
| `auth_token_subject`  | `(token: string) -> string`                        | Extract subject from "subject.signature" token |

### Role-Based Access Control

| Function              | Signature                                                          | Purpose              |
|-----------------------|--------------------------------------------------------------------|----------------------|
| `auth_role_has`       | `(roles_csv: string, role: string) -> int`                         | Check if CSV roles contains a role |
| `authz_allow_role`    | `(user_roles_csv: string, required_roles_csv: string) -> int`      | Check if user has any required role |

### Error Handling

| Function          | Signature                               | Purpose              |
|-------------------|-----------------------------------------|----------------------|
| `Ok(v)`           | `-> Result[T, E]`                       | Success value        |
| `error(msg)`      | `-> Result[T, string]`                  | Error value          |
| `errorf(fmt, ...)` | `-> Result[T, string]`                 | Formatted error      |
| `wrap_err(r, ctx)` | `-> Result[T, string]`                 | Add context to error |
| `error_is(r, substr)` | `-> bool`                           | Check error contains |
| `error_string(r)` | `-> string`                             | Extract error message|

### Testing

| Function           | Signature            | Purpose                      |
|--------------------|----------------------|------------------------------|
| `assert`           | `(bool)`             | Fail if false                |
| `assert_eq`        | `(int, int)`         | Fail if not equal            |
| `assert_eq_str`    | `(string, string)`   | Fail if strings differ       |
| `fail`             | `(string)`           | Unconditional failure        |
| `t_run`            | `(string)`           | Start named subtest          |
| `t_run_nested`     | `(string)`           | Start nested subtest         |

### Benchmarking

| Function          | Signature            | Purpose                       |
|-------------------|----------------------|-------------------------------|
| `black_box`       | `(T) -> T`           | Prevent optimizer elimination |
| `now_ns`          | `() -> int`          | Monotonic clock for timing    |

### Direct I/O

| Function          | Signature                                     | Purpose                          |
|-------------------|-----------------------------------------------|----------------------------------|
| `file_open`       | `(path: string, mode: int, flags: int) -> int` | Open file descriptor            |
| `file_close`      | `(fd: int) -> int`                            | Close file descriptor            |
| `pread`           | `(fd: int, count: int, offset: int) -> string` | Positional read                 |
| `pwrite`          | `(fd: int, data: string, offset: int) -> int` | Positional write                 |
| `file_append`     | `(fd: int, data: string) -> int`              | Append to file                   |
| `fsync`           | `(fd: int) -> int`                            | Flush data + metadata to disk    |
| `fdatasync`       | `(fd: int) -> int`                            | Flush data only to disk          |
| `fallocate`       | `(fd: int, size: int) -> int`                 | Pre-allocate disk space          |
| `file_size`       | `(fd: int) -> int`                            | Get file size                    |
| `file_truncate`   | `(fd: int, size: int) -> int`                 | Truncate file                    |
| `file_seek`       | `(fd: int, offset: int, whence: int) -> int`  | Seek position (0=SET,1=CUR,2=END) |
| `file_read_exact` | `(fd: int, n: int) -> string`                 | Read exactly n bytes             |

### Memory-Mapped Files (MMap)

| Function          | Signature                                      | Purpose                          |
|-------------------|------------------------------------------------|----------------------------------|
| `mmap_open`       | `(path: string, mode: int) -> MMap`            | Map existing file                |
| `mmap_create`     | `(path: string, size: int) -> MMap`            | Create and map new file          |
| `mmap_read`       | `(m: MMap, offset: int, count: int) -> string` | Read from mapping                |
| `mmap_write`      | `(m: MMap, offset: int, data: string) -> int`  | Write to mapping                 |
| `mmap_sync`       | `(m: MMap, flags: int) -> int`                 | Flush mapping to disk            |
| `mmap_size`       | `(m: MMap) -> int`                             | Size of mapping                  |
| `mmap_close`      | `(m: MMap) -> int`                             | Unmap and close                  |

### Binary Buffer (Buf)

| Function          | Signature                          | Purpose                          |
|-------------------|------------------------------------|----------------------------------|
| `buf_pack_new`    | `(capacity: int) -> Buf`           | Create new write buffer          |
| `buf_from_string` | `(s: string) -> Buf`              | Buffer from existing bytes       |
| `buf_to_string`   | `(b: Buf) -> string`              | Extract contents as string       |
| `buf_len`         | `(b: Buf) -> int`                  | Total bytes in buffer            |
| `buf_pos`         | `(b: Buf) -> int`                  | Current read position            |
| `buf_reset`       | `(b: Buf)`                         | Reset position to start          |
| `buf_seek`        | `(b: Buf, pos: int)`              | Seek to position                 |
| `buf_free`        | `(b: Buf)`                         | Release buffer memory            |
| `buf_write_u8`    | `(b: Buf, v: int)`                | Write unsigned 8-bit             |
| `buf_write_u16`   | `(b: Buf, v: int)`                | Write unsigned 16-bit (LE)       |
| `buf_write_u32`   | `(b: Buf, v: int)`                | Write unsigned 32-bit (LE)       |
| `buf_write_u64`   | `(b: Buf, v: int)`                | Write unsigned 64-bit (LE)       |
| `buf_write_u16be` | `(b: Buf, v: int)`                | Write unsigned 16-bit (BE)       |
| `buf_write_u32be` | `(b: Buf, v: int)`                | Write unsigned 32-bit (BE)       |
| `buf_write_i32`   | `(b: Buf, v: int)`                | Write signed 32-bit (LE)         |
| `buf_write_f32`   | `(b: Buf, v: float)`              | Write 32-bit float               |
| `buf_write_f64`   | `(b: Buf, v: float)`              | Write 64-bit float               |
| `buf_read_u8`     | `(b: Buf) -> int`                  | Read unsigned 8-bit              |
| `buf_read_u16`    | `(b: Buf) -> int`                  | Read unsigned 16-bit (LE)        |
| `buf_read_u32`    | `(b: Buf) -> int`                  | Read unsigned 32-bit (LE)        |
| `buf_read_u64`    | `(b: Buf) -> int`                  | Read unsigned 64-bit (LE)        |
| `buf_read_u16be`  | `(b: Buf) -> int`                  | Read unsigned 16-bit (BE)        |
| `buf_read_u32be`  | `(b: Buf) -> int`                  | Read unsigned 32-bit (BE)        |
| `buf_read_i32`    | `(b: Buf) -> int`                  | Read signed 32-bit (LE)          |
| `buf_read_f32`    | `(b: Buf) -> float`               | Read 32-bit float                |
| `buf_read_f64`    | `(b: Buf) -> float`               | Read 64-bit float                |
| `buf_write_bytes` | `(b: Buf, data: string)`          | Write raw bytes                  |
| `buf_write_str`   | `(b: Buf, s: string)`             | Write string (same as write_bytes) |
| `buf_read_bytes`  | `(b: Buf, n: int) -> string`      | Read n raw bytes                 |
| `buf_read_str`    | `(b: Buf, n: int) -> string`      | Read n bytes as string           |

### Event Loop (mako_evloop.h)

| Function                | Signature                              | Purpose                       |
|-------------------------|----------------------------------------|-------------------------------|
| `evloop_new`            | `() -> EvLoop`                         | Create event loop             |
| `evloop_add`            | `(EvLoop, int, int) -> int`            | Register fd with flags        |
| `evloop_mod`            | `(EvLoop, int, int) -> int`            | Modify fd interest flags      |
| `evloop_del`            | `(EvLoop, int) -> int`                 | Remove fd                     |
| `evloop_wait`           | `(EvLoop, int) -> int`                 | Wait for events, returns count|
| `evloop_event_fd`       | `(EvLoop, int) -> int`                 | Get fd from event at index    |
| `evloop_event_flags`    | `(EvLoop, int) -> int`                 | Get flags from event at index |
| `evloop_close`          | `(EvLoop) -> int`                      | Destroy event loop            |
| `nb_listen`             | `(int) -> int`                         | Non-blocking TCP listener     |
| `nb_accept`             | `(int) -> int`                         | Non-blocking accept           |
| `nb_read`               | `(int) -> string`                      | Non-blocking read             |
| `nb_write`              | `(int, string) -> int`                 | Non-blocking write            |
| `nb_udp_bind`           | `(int) -> int`                         | Non-blocking UDP socket       |
| `nb_udp_recv`           | `(int) -> string`                      | Non-blocking UDP receive      |
| `nb_close`              | `(int) -> int`                         | Close non-blocking socket     |

### Game UDP (mako_game.h)

| Function                | Signature                              | Purpose                       |
|-------------------------|----------------------------------------|-------------------------------|
| `game_udp_bind`         | `(int) -> GameUDP`                     | Bind UDP game socket          |
| `game_udp_recv`         | `(GameUDP) -> string`                  | Receive packet (tracks sender)|
| `game_udp_sender`       | `(GameUDP) -> int`                     | Peer ID of last sender        |
| `game_udp_send`         | `(GameUDP, int, string) -> int`        | Send to specific peer         |
| `game_udp_broadcast`    | `(GameUDP, string) -> int`             | Send to all peers             |
| `game_udp_kick`         | `(GameUDP, int)`                       | Disconnect a peer             |
| `game_udp_peers`        | `(GameUDP) -> int`                     | Number of connected peers     |
| `game_udp_fd`           | `(GameUDP) -> int`                     | Raw fd for event loop         |
| `game_udp_close`        | `(GameUDP)`                            | Close socket                  |
| `tick_now_us`           | `() -> int`                            | Microsecond timestamp         |
| `tick_sleep_us`         | `(int, int) -> int`                    | Sleep to maintain tick rate   |

### Cloud / Distributed (mako_cloud.h)

| Function                | Signature                              | Purpose                       |
|-------------------------|----------------------------------------|-------------------------------|
| `chash_new`             | `(int, int) -> CHash`                  | Create hash ring (nodes, vnodes) |
| `chash_get`             | `(CHash, string) -> int`               | Get node for key              |
| `chash_add_node`        | `(CHash) -> int`                       | Add node, returns ID          |
| `chash_remove_node`     | `(CHash, int)`                         | Remove a node                 |
| `chash_node_count`      | `(CHash) -> int`                       | Active node count             |
| `chash_free`            | `(CHash)`                              | Destroy ring                  |
| `ratelimit_new`         | `(int, int) -> RateLimiter`            | Token bucket (rate, burst)    |
| `ratelimit_allow`       | `(RateLimiter) -> int`                 | Consume token (1/0)           |
| `ratelimit_remaining`   | `(RateLimiter) -> int`                 | Tokens remaining              |
| `ratelimit_free`        | `(RateLimiter)`                        | Destroy limiter               |
| `breaker_new`           | `(int, int, int) -> CircuitBreaker`    | Create (threshold, timeout, half_open_max) |
| `breaker_allow`         | `(CircuitBreaker) -> int`              | Check if request proceeds     |
| `breaker_success`       | `(CircuitBreaker)`                     | Record success                |
| `breaker_failure`       | `(CircuitBreaker)`                     | Record failure                |
| `breaker_state`         | `(CircuitBreaker) -> int`              | 0=closed, 1=open, 2=half-open|
| `breaker_reset`         | `(CircuitBreaker)`                     | Reset to closed               |
| `breaker_free`          | `(CircuitBreaker)`                     | Destroy breaker               |

### HTTP Engine (mako_httpengine.h)

| Function                | Signature                              | Purpose                       |
|-------------------------|----------------------------------------|-------------------------------|
| `httpengine_new`        | `() -> HttpEngine`                     | Create HTTP engine            |
| `httpengine_route`      | `(HttpEngine, string, string, int) -> int` | Register route            |
| `httpengine_start`      | `(HttpEngine, int) -> int`             | Start listening               |
| `httpengine_stop`       | `(HttpEngine)`                         | Stop engine                   |
| `httpengine_free`       | `(HttpEngine)`                         | Destroy engine                |

### Checked Arithmetic

| Function                | Signature                              | Purpose                       |
|-------------------------|----------------------------------------|-------------------------------|
| `checked_add`           | `(int, int) -> Result[int, string]`    | Add with overflow detection   |
| `checked_sub`           | `(int, int) -> Result[int, string]`    | Subtract with overflow detection |
| `checked_mul`           | `(int, int) -> Result[int, string]`    | Multiply with overflow detection |
| `would_overflow_add`    | `(int, int) -> int`                    | 1 if add would overflow       |
| `would_overflow_sub`    | `(int, int) -> int`                    | 1 if sub would overflow       |
| `would_overflow_mul`    | `(int, int) -> int`                    | 1 if mul would overflow       |

### Graceful Shutdown

| Function                    | Signature                          | Purpose                       |
|-----------------------------|-------------------------------------|-------------------------------|
| `install_graceful_shutdown`  | `() -> int`                        | Register SIGTERM/SIGINT handler |
| `shutdown_requested`         | `() -> int`                        | 1 if shutdown was signaled    |
| `server_shutdown_begin`      | `(int) -> int`                     | Begin drain, close listeners  |
| `server_drain`               | `(int) -> int`                     | Wait for connections to drain |
| `http_shutdown_begin`        | `() -> int`                        | Begin HTTP server shutdown    |
| `http_shutdown_ready`        | `() -> int`                        | 1 if all connections drained  |
| `http_shutdown_from_signal`  | `() -> int`                        | Trigger from signal handler   |

### Distributed Tracing

| Function                | Signature                              | Purpose                       |
|-------------------------|----------------------------------------|-------------------------------|
| `trace_begin`           | `(string) -> int`                      | Start a named trace span      |
| `trace_end`             | `() -> int`                            | End current span              |
| `trace_id`              | `() -> string`                         | Get current trace ID (hex)    |
| `trace_set`             | `(string) -> int`                      | Set trace ID for propagation  |
| `trace_current`         | `() -> string`                         | Get current span name         |
| `trace_log`             | `(string) -> int`                      | Log with trace context        |
| `trace_clear`           | `() -> int`                            | Reset trace state             |
| `middleware_trace`      | `(int) -> string`                      | Extract/generate trace from request |

### Leak Detection

| Function                | Signature                              | Purpose                       |
|-------------------------|----------------------------------------|-------------------------------|
| `leak_mark`             | `() -> int`                            | Snapshot allocation count     |
| `leak_check`            | `(int) -> int`                         | Check for leaks since mark    |
| `leak_detected`         | `() -> int`                            | Whether last check found leak |
| `leak_scope_enter`      | `() -> int`                            | Enter nestable leak scope     |
| `leak_scope_exit`       | `() -> int`                            | Exit scope, returns leaked bytes |
| `leak_assert_scope`     | `() -> int`                            | 1 if no leak in scope         |
| `leak_bytes_since`      | `(int) -> int`                         | Bytes allocated since mark    |
| `leak_report_json`      | `() -> string`                         | JSON leak report              |

### Crew Drain

| Function                | Signature                              | Purpose                       |
|-------------------------|----------------------------------------|-------------------------------|
| `crew_drain`            | `(int) -> int`                         | Drain all crew tasks with timeout |

---

## E. Compiler Flags

### `mako build` Flags

| Flag                  | Default    | Purpose                              |
|-----------------------|------------|--------------------------------------|
| `-o PATH`             | —          | Output binary path                   |
| `--release`           | off        | Optimize with -O3 -flto             |
| `-j N`                | CPU count  | Parallel clang jobs                  |
| `--no-incremental`    | off        | Skip object cache                    |
| `--time`              | off        | Print timing breakdown               |
| `--emit-c`            | off        | Write generated C file               |
| `--target TRIPLE`     | host       | Cross-compilation target             |
| `--sanitize=MODE`     | off        | address or thread sanitizer          |
| `--static-link`       | varies     | Force static linking                 |
| `--no-static-link`    | varies     | Force dynamic linking                |
| `-p NAME`             | —          | Target workspace member              |

### `mako check` Flags

| Flag         | Purpose                                |
|--------------|----------------------------------------|
| `--json`     | Emit JSON diagnostics                  |
| `-p NAME`    | Check one workspace member             |

### `mako test` Flags

| Flag            | Purpose                                  |
|-----------------|------------------------------------------|
| `-r PATTERN`    | Filter tests by name (substring/glob/regex) |
| `--run PATTERN` | Same as `-r`                             |
| `-v` / `--verbose` | List matched functions, verbose output |
| `--count N`     | Repeat tests N times                     |
| `--coverage`    | Instrument for coverage reporting        |
| `-p NAME`       | Test one workspace member                |

### `mako fmt` Flags

| Flag    | Purpose                              |
|---------|--------------------------------------|
| `-w`    | Write formatted output back to file  |
| `-l`    | List files that would change         |
| `-d`    | Show diff of formatting changes      |
| `-p NAME` | Format one workspace member        |

### `mako profile` Flags

| Flag          | Purpose                              |
|---------------|--------------------------------------|
| `--json`      | Structured JSON timing output        |
| `--release`   | Profile with optimizations enabled   |
| `-p NAME`     | Profile one workspace member         |

---

## F. Environment Variables

| Variable          | Purpose                                              | Default                |
|-------------------|------------------------------------------------------|------------------------|
| `MAKO_RUNTIME`    | Path to runtime headers and sources                  | Binary-relative        |
| `MAKO_CC`         | C compiler override (e.g., `zig cc`, `clang-17`)     | System `clang`         |
| `MAKO_JOBS`       | Parallel compilation jobs (same as `-j`)             | CPU count              |
| `MAKO_CACHE`      | Object cache directory                               | `.mako/cache/`         |
| `MAKO_GIT_HASH`   | Git hash for `mako version -v` output               | From build.rs          |
| `MAKO_HAS_OPENSSL`| Enable OpenSSL-linked TLS features                   | Auto-detected          |
| `MAKO_LIVE_TLS`   | Enable live TLS integration tests                    | Off (set to `1`)       |
| `MAKO_WASI_GREET` | Example env var for WASI programs                    | —                      |
| `WASI_SDK_PATH`   | Path to wasi-sdk installation                        | Common paths searched  |

---

## G. File Extensions and Conventions

| Extension    | Purpose                            |
|--------------|------------------------------------|
| `.mko`       | Mako source file                   |
| `_test.mko`  | Test file (discovered by `mako test`) |
| `bench_*.mko`| Benchmark file (discovered by `mako bench`) |
| `lib.mko`    | Package library entry point        |
| `main.mko`   | Application entry point            |
| `mako.toml`  | Package manifest                   |
| `mako.lock`  | Dependency lock file               |

---

## H. `mako.toml` Reference

```toml
[package]
name = "myapp"
version = "0.1.0"
systems = false          # true = strict ownership, no future GC weakening

[dependencies]
"helper" = { path = "../helper", version = "0.1.0" }
"crypto" = { git = "https://example.com/crypto.git", tag = "v1.0.0", version = "1.0.0" }
"util"   = { version = "^1.0.0" }   # registry-only (from .mako/registry/)

[workspace]
members = ["lib", "app", "cli"]
```

### Dependency Formats

| Form                                    | Source     |
|-----------------------------------------|------------|
| `{ path = "../lib" }`                   | Local path |
| `{ path = "../lib", version = "0.1.0" }` | Path + SemVer check |
| `{ git = "url", tag = "v1.0" }`        | Git repository |
| `{ version = "^1.0.0" }`               | Registry   |

Version constraints: `^1.0` (compatible), `~1.0` (patch only), `1.0.0` (exact).

---

## I. Exit Codes

| Code | Meaning                        |
|------|--------------------------------|
| 0    | Success                        |
| 1    | Compilation error or test failure |
| 2    | Missing file or invalid arguments |
| 101  | Runtime abort (bounds check, assertion, OOR conversion) |

---

## J. Doc Map

| Document                        | Purpose                              |
|---------------------------------|--------------------------------------|
| This Book                       | Guided tour of Mako                  |
| `docs/GUIDE.md`                 | Exhaustive verified syntax reference |
| `docs/LANGUAGE.md`              | Language design overview             |
| `docs/STDLIB.md`                | Standard library surface             |
| `docs/BUILD.md`                 | Incremental compilation details      |
| `docs/PERFORMANCE.md`           | Release optimization and benchmarks  |
| `docs/DEBUG.md`                 | Debugging with lldb, dbg, sanitizers |
| `docs/SECURITY.md`             | Memory safety and security model     |
| `docs/WASM.md`                  | WebAssembly/WASI details             |
| `docs/KEYWORDS.md`             | Full keyword table                   |
| `docs/RELEASE.md`              | Packaging and distribution           |
| `docs/howto/`                   | Task-oriented how-to guides          |
| `CHANGELOG.md`                  | Release notes                        |

---

## K. Checkable Book Examples

All code examples in this book are valid Mako that passes `mako check`:

```bash
mako check docs/book/examples/*.mko
mako run docs/book/examples/book_hello.mko
```

| File                              | Topic                  |
|-----------------------------------|------------------------|
| `docs/book/examples/book_hello.mko` | Hello world + fib    |
| `docs/book/examples/book_ops.mko`   | Operators            |
| `docs/book/examples/book_errors.mko`| Result and error wrapping |
| `docs/book/examples/book_imports.mko`| Grouped imports      |
