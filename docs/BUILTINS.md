# Mako Built-in Functions Reference

Complete reference for every built-in function available in Mako.
Signatures use the form `function_name(param: type, ...) -> return_type`.

---

## 1. Output

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `print(s: string) -> void` | Print a string to stdout |
| `print_int` | `print_int(n: int) -> void` | Print an integer to stdout |
| `print_int64` | `print_int64(n: int64) -> void` | Print a 64-bit integer to stdout |
| `print_int32` | `print_int32(n: int32) -> void` | Print a 32-bit integer to stdout |
| `print_int8` | `print_int8(n: int8) -> void` | Print an 8-bit integer to stdout |
| `print_uint64` | `print_uint64(n: uint64) -> void` | Print an unsigned 64-bit integer to stdout |
| `print_float` | `print_float(f: float) -> void` | Print a float to stdout |
| `print_bool` | `print_bool(b: bool) -> void` | Print a boolean to stdout |
| `dbg` | `dbg(n: int) -> int` | Debug-print an integer and return it |
| `dbg_str` | `dbg_str(s: string) -> string` | Debug-print a string and return it |
| `format_int` | `format_int(n: int) -> string` | Convert an integer to its string representation |
| `format_float` | `format_float(f: float, prec: int) -> string` | Convert a float to string with given decimal precision |
| `format_bool` | `format_bool(b: bool) -> string` | Convert a boolean to "true" or "false" |
| `int_to_string` | `int_to_string(n: int) -> string` | Convert an integer to its string representation |
| `fmt_sprintf` | `fmt_sprintf(fmt: string, arg: string) -> string` | Sprintf-style formatting with a string argument |
| `fmt_sprintf_d` | `fmt_sprintf_d(fmt: string, arg: int) -> string` | Sprintf-style formatting with an integer argument |

---

## 2. Strings

| Function | Signature | Description |
|----------|-----------|-------------|
| `str_len` | `str_len(s: string) -> int` | Return byte length of a string |
| `str_eq` | `str_eq(a: string, b: string) -> bool` | Test two strings for equality |
| `str_contains` | `str_contains(s: string, substr: string) -> bool` | Check if string contains a substring |
| `str_has_prefix` | `str_has_prefix(s: string, prefix: string) -> bool` | Check if string starts with prefix |
| `str_has_suffix` | `str_has_suffix(s: string, suffix: string) -> bool` | Check if string ends with suffix |
| `str_index` | `str_index(s: string, substr: string) -> int` | Return index of first occurrence of substr, or -1 |
| `str_last_index` | `str_last_index(s: string, substr: string) -> int` | Return index of last occurrence of substr, or -1 |
| `str_trim` | `str_trim(s: string, cutset: string) -> string` | Trim characters in cutset from both ends |
| `str_trim_space` | `str_trim_space(s: string) -> string` | Trim whitespace from both ends |
| `str_trim_left` | `str_trim_left(s: string, cutset: string) -> string` | Trim characters in cutset from the left |
| `str_trim_right` | `str_trim_right(s: string, cutset: string) -> string` | Trim characters in cutset from the right |
| `str_to_lower` | `str_to_lower(s: string) -> string` | Convert string to lowercase |
| `str_to_upper` | `str_to_upper(s: string) -> string` | Convert string to uppercase |
| `str_repeat` | `str_repeat(s: string, count: int) -> string` | Repeat a string count times |
| `str_replace` | `str_replace(s: string, old: string, new: string) -> string` | Replace all occurrences of old with new |
| `str_split` | `str_split(s: string, sep: string) -> []string` | Split string by separator into an array |
| `str_fields` | `str_fields(s: string) -> []string` | Split string on whitespace into an array |
| `str_join` | `str_join(parts: []string, sep: string) -> string` | Join string array with separator |
| `str_count` | `str_count(s: string, substr: string) -> int` | Count non-overlapping occurrences of substr |
| `str_cut` | `str_cut(s: string, sep: string) -> []string` | Cut string at first occurrence of sep, returning [before, after] |
| `rune_count` | `rune_count(s: string) -> int` | Count the number of Unicode code points |
| `str_builder` | `str_builder() -> StrBuilder` | Create a new growable string buffer |
| `builder_write` | `builder_write(sb: StrBuilder, s: string) -> void` | Append a string to the builder |
| `builder_write_byte` | `builder_write_byte(sb: StrBuilder, b: byte) -> void` | Append a single byte to the builder |
| `builder_string` | `builder_string(sb: StrBuilder) -> string` | Finalize and return the built string |
| `builder_len` | `builder_len(sb: StrBuilder) -> int` | Return current length of the builder |

---

## 3. Math

| Function | Signature | Description |
|----------|-----------|-------------|
| `abs` | `abs(n: int) -> int` | Absolute value of an integer |
| `abs_f` | `abs_f(f: float) -> float` | Absolute value of a float |
| `min` | `min(a: int, b: int) -> int` | Return the smaller of two integers |
| `max` | `max(a: int, b: int) -> int` | Return the larger of two integers |
| `clamp` | `clamp(val: int, lo: int, hi: int) -> int` | Clamp an integer to [lo, hi] |
| `clamp_f` | `clamp_f(val: float, lo: float, hi: float) -> float` | Clamp a float to [lo, hi] |
| `sqrt` | `sqrt(f: float) -> float` | Square root |
| `sin` | `sin(f: float) -> float` | Sine (radians) |
| `cos` | `cos(f: float) -> float` | Cosine (radians) |
| `atan2` | `atan2(y: float, x: float) -> float` | Two-argument arctangent |
| `floor_f` | `floor_f(f: float) -> float` | Floor of a float |
| `ceil_f` | `ceil_f(f: float) -> float` | Ceiling of a float |
| `lerp` | `lerp(a: float, b: float, t: float) -> float` | Linear interpolation between a and b |
| `dist2d` | `dist2d(x1: float, y1: float, x2: float, y2: float) -> float` | Euclidean distance between two 2D points |
| `math_abs` | `math_abs(f: float) -> float` | Absolute value of a float |
| `math_sqrt` | `math_sqrt(f: float) -> float` | Square root |
| `math_pow` | `math_pow(base: float, exp: float) -> float` | Raise base to the power of exp |
| `math_floor` | `math_floor(f: float) -> float` | Floor of a float |
| `math_ceil` | `math_ceil(f: float) -> float` | Ceiling of a float |
| `math_sin` | `math_sin(f: float) -> float` | Sine (radians) |
| `math_cos` | `math_cos(f: float) -> float` | Cosine (radians) |
| `math_log` | `math_log(f: float) -> float` | Natural logarithm |
| `math_exp` | `math_exp(f: float) -> float` | Exponential (e^x) |
| `rand_seed` | `rand_seed(seed: int) -> void` | Seed the random number generator |
| `rand_intn` | `rand_intn(n: int) -> int` | Random integer in [0, n) |
| `rand_float` | `rand_float() -> float` | Random float in [0.0, 1.0) |
| `random_int` | `random_int(lo: int, hi: int) -> int` | Cryptographically random integer in [lo, hi] |
| `safe_add` | `safe_add(a: int, b: int) -> int` | Overflow-checked integer addition |

---

## 4. Parsing & Conversion

| Function | Signature | Description |
|----------|-----------|-------------|
| `parse_int` | `parse_int(s: string) -> Result[int, string]` | Parse a string as an integer |
| `parse_float` | `parse_float(s: string) -> float` | Parse a string as a float |
| `parse_bool` | `parse_bool(s: string) -> Result[int, string]` | Parse a string as a boolean (1/0) |

---

## 5. Slices & Arrays

| Function | Signature | Description |
|----------|-----------|-------------|
| `len` | `len(arr: []int) -> int` | Return the length of an array |
| `cap` | `cap(arr: []int) -> int` | Return the capacity of an array |
| `append` | `append(arr: []int, val: int) -> []int` | Append a value to an array, returning the new array |
| `copy` | `copy(dst: []int, src: []int) -> int` | Copy elements from src to dst, return count copied |
| `ints_contains` | `ints_contains(arr: []int, val: int) -> bool` | Check if an int array contains a value |
| `strings_contains` | `strings_contains(arr: []string, val: string) -> bool` | Check if a string array contains a value |
| `ints_index` | `ints_index(arr: []int, val: int) -> int` | Return index of value in int array, or -1 |
| `ints_copy` | `ints_copy(arr: []int) -> []int` | Return a shallow copy of an int array |
| `sort_ints` | `sort_ints(arr: []int) -> []int` | Sort an integer array in ascending order |
| `sort_strings` | `sort_strings(arr: []string) -> []string` | Sort a string array in ascending order |
| `slices_reverse` | `slices_reverse(arr: []int) -> []int` | Reverse an integer array |
| `slices_unique` | `slices_unique(arr: []int) -> []int` | Remove duplicates from an integer array |
| `slice_ints` | `slice_ints(arr: []int, start: int, end: int) -> Slice` | Create a borrowed slice view of an int array |
| `slice_len` | `slice_len(s: Slice) -> int` | Return the length of a slice |
| `slice_get` | `slice_get(s: Slice, idx: int) -> int` | Get an element from a slice by index |
| `unsafe_index` | `unsafe_index(arr: []int, idx: int) -> int` | Unchecked array index (no bounds check) |

---

## 6. Maps

| Function | Signature | Description |
|----------|-----------|-------------|
| `maps_keys` | `maps_keys(m: map[string]int) -> []string` | Return all keys from a map |
| `maps_values` | `maps_values(m: map[string]int) -> []int` | Return all values from a map |
| `maps_clear` | `maps_clear(m: map[string]int) -> void` | Remove all entries from a map |
| `maps_clone` | `maps_clone(m: map[string]int) -> map[string]int` | Create a shallow copy of a map |
| `maps_equal` | `maps_equal(a: map[string]int, b: map[string]int) -> int` | Check if two maps have identical entries |
| `maps_copy` | `maps_copy(dst: map[string]int, src: map[string]int) -> void` | Copy all entries from src into dst |

---

## 7. File I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `read_file` | `read_file(path: string) -> string` | Read entire file contents as a string |
| `write_file` | `write_file(path: string, data: string) -> int` | Write string data to a file (overwrite) |
| `append_file` | `append_file(path: string, data: string) -> int` | Append string data to a file |
| `remove_file` | `remove_file(path: string) -> int` | Delete a file |
| `file_exists` | `file_exists(path: string) -> bool` | Check if a file exists |
| `is_dir` | `is_dir(path: string) -> bool` | Check if a path is a directory |
| `read_dir` | `read_dir(path: string) -> []string` | List entries in a directory |
| `mkdir` | `mkdir(path: string) -> int` | Create a directory |
| `getcwd` | `getcwd() -> string` | Return the current working directory |
| `chdir` | `chdir(path: string) -> int` | Change the current working directory |
| `path_join` | `path_join(a: string, b: string) -> string` | Join two path segments |
| `path_base` | `path_base(path: string) -> string` | Return the last element of a path |
| `path_dir` | `path_dir(path: string) -> string` | Return the directory portion of a path |
| `path_ext` | `path_ext(path: string) -> string` | Return the file extension |
| `path_clean` | `path_clean(path: string) -> string` | Clean and normalize a path |
| `path_is_abs` | `path_is_abs(path: string) -> bool` | Check if a path is absolute |
| `filepath_walk` | `filepath_walk(root: string) -> []string` | Recursively list all files under root |
| `filepath_walk_n` | `filepath_walk_n(root: string, max: int) -> []string` | Recursively list up to max files under root |
| `embed_file` | `embed_file(path: string) -> string` | Embed file contents at compile time |

---

## 8. Direct I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `file_open` | `file_open(path: string, flags: int, mode: int) -> int` | Open a file descriptor with flags and mode |
| `file_close` | `file_close(fd: int) -> int` | Close a file descriptor |
| `pread` | `pread(fd: int, count: int, offset: int) -> string` | Read count bytes at offset without seeking |
| `pwrite` | `pwrite(fd: int, data: string, offset: int) -> int` | Write data at offset without seeking |
| `file_append` | `file_append(fd: int, data: string) -> int` | Append data to a file descriptor |
| `fsync` | `fsync(fd: int) -> int` | Flush file data and metadata to disk |
| `fdatasync` | `fdatasync(fd: int) -> int` | Flush file data (not metadata) to disk |
| `fallocate` | `fallocate(fd: int, size: int) -> int` | Pre-allocate disk space for a file |
| `file_size` | `file_size(fd: int) -> int` | Return the size of an open file |
| `file_truncate` | `file_truncate(fd: int, size: int) -> int` | Truncate or extend a file to given size |
| `file_seek` | `file_seek(fd: int, offset: int, whence: int) -> int` | Seek to a position in a file |
| `file_read_exact` | `file_read_exact(fd: int, count: int) -> string` | Read exactly count bytes from current position |

---

## 9. Memory-Mapped Files

| Function | Signature | Description |
|----------|-----------|-------------|
| `mmap_open` | `mmap_open(path: string, size: int) -> MMap` | Open an existing file as a memory-mapped region |
| `mmap_create` | `mmap_create(path: string, size: int) -> MMap` | Create a new memory-mapped file |
| `mmap_read` | `mmap_read(m: MMap, offset: int, len: int) -> string` | Read bytes from a mapped region |
| `mmap_write` | `mmap_write(m: MMap, offset: int, data: string) -> int` | Write bytes to a mapped region |
| `mmap_sync` | `mmap_sync(m: MMap, flags: int) -> int` | Flush mapped pages to disk |
| `mmap_size` | `mmap_size(m: MMap) -> int` | Return the size of the mapped region |
| `mmap_close` | `mmap_close(m: MMap) -> int` | Unmap and close the memory-mapped file |

---

## 10. Environment

| Function | Signature | Description |
|----------|-----------|-------------|
| `env_get` | `env_get(key: string) -> string` | Get an environment variable value |
| `env_set` | `env_set(key: string, val: string) -> int` | Set an environment variable |
| `env_get_or` | `env_get_or(key: string, fallback: string) -> string` | Get an environment variable or return fallback |
| `env_has` | `env_has(key: string) -> int` | Check if an environment variable is set |
| `argc` | `argc() -> int` | Return the number of command-line arguments |
| `args` | `args() -> []string` | Return all command-line arguments |
| `arg_get` | `arg_get(idx: int) -> string` | Get a command-line argument by index |
| `flag_string` | `flag_string(name: string, default: string) -> string` | Parse a string flag from command-line arguments |
| `flag_int` | `flag_int(name: string, default: int) -> int` | Parse an integer flag from command-line arguments |
| `flag_bool` | `flag_bool(name: string, default: int) -> int` | Parse a boolean flag from command-line arguments |

---

## 11. Time

| Function | Signature | Description |
|----------|-----------|-------------|
| `now_ns` | `now_ns() -> int` | Current time in nanoseconds since epoch |
| `now_ms` | `now_ms() -> int` | Current time in milliseconds since epoch |
| `time_unix` | `time_unix() -> int` | Current time as Unix timestamp (seconds) |
| `time_format` | `time_format(unix: int) -> string` | Format a Unix timestamp as a human-readable string |
| `sleep_ms` | `sleep_ms(ms: int) -> void` | Sleep for the given number of milliseconds |
| `time_sleep_ms` | `time_sleep_ms(ms: int) -> void` | Sleep for the given number of milliseconds |
| `elapsed_ms` | `elapsed_ms(start: int) -> int` | Milliseconds elapsed since the given start time |

---

## 12. HTTP Server

| Function | Signature | Description |
|----------|-----------|-------------|
| `http_bind` | `http_bind(port: int) -> int` | Bind an HTTP listener on a port |
| `http_accept` | `http_accept(listener: int) -> int` | Accept an incoming HTTP connection |
| `http_method` | `http_method(conn: int) -> string` | Get the HTTP method of a request |
| `http_path` | `http_path(conn: int) -> string` | Get the URL path of a request |
| `http_body` | `http_body(conn: int) -> string` | Get the body of a request |
| `http_header` | `http_header(conn: int, name: string) -> string` | Get a request header value by name |
| `http_respond` | `http_respond(conn: int, status: int, body: string) -> int` | Send an HTTP response with status and body |
| `http_respond_json` | `http_respond_json(conn: int, status: int, json: string) -> int` | Send a JSON HTTP response |
| `http_respond_ct` | `http_respond_ct(conn: int, status: int, body: string, content_type: string) -> int` | Send a response with a custom content type |
| `http_close` | `http_close(conn: int) -> int` | Close an HTTP connection |
| `http_close_listener` | `http_close_listener(listener: int) -> int` | Close an HTTP listener |
| `http_next` | `http_next(conn: int) -> int` | Advance to the next request on a keep-alive connection |
| `http_keepalive` | `http_keepalive(conn: int) -> int` | Check if the connection supports keep-alive |
| `http_serve` | `http_serve(port: int, handler: string) -> int` | Start a simple HTTP server |
| `http_echo` | `http_echo(port: int) -> int` | Start an echo HTTP server |
| `http_listen` | `http_listen(port: int, handler: string) -> int` | Listen and serve HTTP |
| `http_header_ok` | `http_header_ok(name: string, value: string) -> int` | Validate an HTTP header name/value pair |
| `http_content_encoding` | `http_content_encoding(accept: string) -> string` | Determine content encoding from Accept-Encoding |
| `http_compress_if_accepted` | `http_compress_if_accepted(accept: string, body: string) -> string` | Compress body if client accepts compression |
| `http_health_json` | `http_health_json(name: string, status: int) -> string` | Generate a JSON health check response |
| `http_respond_health` | `http_respond_health(conn: int, name: string, status: int) -> int` | Respond with a health check status |
| `http_active_connections` | `http_active_connections() -> int` | Return the number of active HTTP connections |

### HTTP Shutdown

| Function | Signature | Description |
|----------|-----------|-------------|
| `http_shutdown_begin` | `http_shutdown_begin(timeout: int) -> int` | Begin graceful shutdown with timeout |
| `http_shutdown_reset` | `http_shutdown_reset() -> int` | Reset shutdown state |
| `http_shutdown_requested` | `http_shutdown_requested() -> int` | Check if shutdown has been requested |
| `http_shutdown_ready` | `http_shutdown_ready() -> int` | Check if shutdown is ready (all connections drained) |
| `http_shutdown_deadline` | `http_shutdown_deadline() -> int` | Get the shutdown deadline timestamp |
| `http_shutdown_remaining` | `http_shutdown_remaining() -> int` | Milliseconds remaining until shutdown deadline |
| `http_shutdown_expired` | `http_shutdown_expired() -> int` | Check if shutdown deadline has passed |
| `http_shutdown_drain_conn` | `http_shutdown_drain_conn(conn: int) -> int` | Drain a connection during shutdown |
| `http_shutdown_from_signal` | `http_shutdown_from_signal(listener: int, timeout: int) -> int` | Begin shutdown from a signal |

---

## 13. HTTP Client

| Function | Signature | Description |
|----------|-----------|-------------|
| `http_get` | `http_get(url: string) -> string` | Perform an HTTP GET request |
| `http_post` | `http_post(url: string, body: string) -> string` | Perform an HTTP POST request |
| `http_request` | `http_request(method: string, url: string, body: string, timeout: int) -> string` | Perform a custom HTTP request |
| `http_get_timeout` | `http_get_timeout(url: string, timeout_ms: int) -> string` | HTTP GET with timeout |
| `http_post_timeout` | `http_post_timeout(url: string, body: string, timeout_ms: int) -> string` | HTTP POST with timeout |
| `http_last_status` | `http_last_status() -> int` | Get the status code of the last HTTP response |
| `http_last_header` | `http_last_header(name: string) -> string` | Get a header from the last HTTP response |

### HTTP Request Parsing

| Function | Signature | Description |
|----------|-----------|-------------|
| `http_request_parse` | `http_request_parse(raw: string) -> HttpRequest` | Parse a raw HTTP request string |
| `http_request_from_conn` | `http_request_from_conn(conn: int) -> HttpRequest` | Parse an HTTP request from a connection fd |
| `http_request_method` | `http_request_method(req: HttpRequest) -> string` | Get the method from a parsed request |
| `http_request_path` | `http_request_path(req: HttpRequest) -> string` | Get the path from a parsed request |
| `http_request_body` | `http_request_body(req: HttpRequest) -> string` | Get the body from a parsed request |
| `http_route_match` | `http_route_match(req: HttpRequest, method: string, pattern: string) -> bool` | Match a request against a method and route pattern |
| `http_route_param` | `http_route_param(req: HttpRequest, pattern: string, name: string) -> string` | Extract a route parameter from a matched pattern |
| `http_req_method` | `http_req_method(conn: int) -> string` | Get request method from connection |
| `http_req_path` | `http_req_path(conn: int) -> string` | Get request path from connection |
| `http_req_body` | `http_req_body(conn: int) -> string` | Get request body from connection |

---

## 14. JSON

| Function | Signature | Description |
|----------|-----------|-------------|
| `json_object` | `json_object(key: string, value: string) -> string` | Create a JSON object with one key-value pair |
| `json_get_string` | `json_get_string(json: string, key: string) -> string` | Extract a string value by key from JSON |
| `json_get_int` | `json_get_int(json: string, key: string) -> int` | Extract an integer value by key from JSON |
| `json_get_object` | `json_get_object(json: string, key: string) -> string` | Extract a nested JSON object by key |
| `json_has` | `json_has(json: string, key: string, value: string) -> int` | Check if JSON has a key with a given value |
| `json_nest` | `json_nest(key: string, inner: string) -> string` | Nest a JSON value under a key |
| `json_merge` | `json_merge(a: string, b: string) -> string` | Merge two JSON objects |
| `json_path_string` | `json_path_string(json: string, path: string, key: string) -> string` | Extract a string via dot-path navigation |
| `json_path_int` | `json_path_int(json: string, path: string, key: string) -> int` | Extract an integer via dot-path navigation |
| `json_ss` | `json_ss(k1: string, v1: string, k2: string, v2: string) -> string` | Create a JSON object with two string key-value pairs |
| `json_si` | `json_si(k1: string, v1: string, k2: string, v2: int) -> string` | Create a JSON object with a string and an int key-value pair |
| `json_i` | `json_i(key: string, value: int) -> string` | Create a JSON object with one int key-value pair |
| `json_object_from_map_ss` | `json_object_from_map_ss(m: map[string]string) -> string` | Convert a map[string]string to a JSON object |
| `json_array_len` | `json_array_len(json: string) -> int` | Return the length of a JSON array |
| `json_array_get_int` | `json_array_get_int(json: string, idx: int) -> int` | Get an integer element from a JSON array |
| `json_array_get_string` | `json_array_get_string(json: string, idx: int) -> string` | Get a string element from a JSON array |
| `json_array_push_string` | `json_array_push_string(json: string, val: string) -> string` | Push a string onto a JSON array |
| `json_array_push_int` | `json_array_push_int(json: string, val: int) -> string` | Push an integer onto a JSON array |
| `json_array_ints3` | `json_array_ints3(a: int, b: int, c: int) -> string` | Create a JSON array from three integers |
| `json_array_strings2` | `json_array_strings2(a: string, b: string) -> string` | Create a JSON array from two strings |

---

## 15. Database

### SQLite (Legacy)

| Function | Signature | Description |
|----------|-----------|-------------|
| `sqlite_query_int` | `sqlite_query_int(db: string, sql: string) -> int` | Query an integer from SQLite |
| `sqlite_query_text` | `sqlite_query_text(db: string, sql: string) -> string` | Query a text value from SQLite |
| `sqlite_query_int_params` | `sqlite_query_int_params(db: string, sql: string, params: []int) -> int` | Query an integer from SQLite with params |

### PostgreSQL (Legacy)

| Function | Signature | Description |
|----------|-----------|-------------|
| `pg_connect` | `pg_connect(connstr: string) -> PgConn` | Connect to a PostgreSQL database |
| `pg_ok` | `pg_ok(conn: PgConn) -> int` | Check if a Postgres connection is valid |
| `pg_exec` | `pg_exec(conn: PgConn, sql: string) -> int` | Execute SQL on a Postgres connection |
| `pg_exec_row_count` | `pg_exec_row_count(conn: PgConn, sql: string) -> int` | Execute SQL and return affected row count |
| `pg_close` | `pg_close(conn: PgConn) -> int` | Close a Postgres connection |
| `pg_connect_url` | `pg_connect_url(url: string) -> string` | Parse a Postgres connection URL |

### Unified SQL

| Function | Signature | Description |
|----------|-----------|-------------|
| `sql_open_sqlite` | `sql_open_sqlite(path: string) -> SqlDB` | Open a SQLite database |
| `sql_open_postgres` | `sql_open_postgres(connstr: string) -> SqlDB` | Open a PostgreSQL database |
| `sql_ok` | `sql_ok(db: SqlDB) -> int` | Check if a database connection is valid |
| `sql_close` | `sql_close(db: SqlDB) -> int` | Close a database connection |
| `sql_exec` | `sql_exec(db: SqlDB, sql: string, params: []int) -> int` | Execute SQL with integer params |
| `sql_exec_plain` | `sql_exec_plain(db: SqlDB, sql: string) -> int` | Execute SQL without params |
| `sql_exec_str4` | `sql_exec_str4(db: SqlDB, sql: string, a: string, b: string, c: string, d: string) -> int` | Execute SQL with four string params |
| `sql_query_int` | `sql_query_int(db: SqlDB, sql: string, params: []int) -> int` | Query a single integer result |
| `sql_query_str` | `sql_query_str(db: SqlDB, sql: string, param: string) -> string` | Query a single string result |
| `sql_begin` | `sql_begin(db: SqlDB) -> int` | Begin a transaction |
| `sql_commit` | `sql_commit(db: SqlDB) -> int` | Commit a transaction |
| `sql_rollback` | `sql_rollback(db: SqlDB) -> int` | Roll back a transaction |
| `sql_prepare` | `sql_prepare(db: SqlDB, sql: string) -> int` | Prepare a SQL statement |
| `sql_stmt_query_int` | `sql_stmt_query_int(stmt: int, params: []int) -> int` | Query integer from a prepared statement |
| `sql_stmt_exec` | `sql_stmt_exec(stmt: int, params: []int) -> int` | Execute a prepared statement |
| `sql_stmt_close` | `sql_stmt_close(stmt: int) -> int` | Close a prepared statement |
| `sql_migration_applied` | `sql_migration_applied(db: SqlDB, version: int) -> int` | Check if a migration has been applied |
| `sql_migrate` | `sql_migrate(db: SqlDB, version: int, sql: string) -> int` | Apply a migration |
| `sql_check_typed` | `sql_check_typed(table: string, col: string, type_name: string, constraint: string) -> int` | Validate column type constraints |
| `sql_query` | `sql_query(sql: string) -> string` | Execute a raw SQL query string |

### SQL Connection Pool

| Function | Signature | Description |
|----------|-----------|-------------|
| `sql_pool_open_sqlite` | `sql_pool_open_sqlite(path: string, size: int) -> int` | Open a SQLite connection pool |
| `sql_pool_open_postgres` | `sql_pool_open_postgres(connstr: string, size: int) -> int` | Open a PostgreSQL connection pool |
| `sql_pool_ok` | `sql_pool_ok(pool: int) -> int` | Check if a pool is valid |
| `sql_pool_size` | `sql_pool_size(pool: int) -> int` | Return the pool size |
| `sql_pool_opened` | `sql_pool_opened(pool: int) -> int` | Return number of opened connections |
| `sql_pool_next_slot` | `sql_pool_next_slot(pool: int) -> int` | Get the next available pool slot |
| `sql_pool_query_int` | `sql_pool_query_int(pool: int, sql: string, params: []int) -> int` | Query integer via pool |
| `sql_pool_exec` | `sql_pool_exec(pool: int, sql: string, params: []int) -> int` | Execute SQL via pool |
| `sql_pool_close` | `sql_pool_close(pool: int) -> int` | Close the connection pool |

### MySQL

| Function | Signature | Description |
|----------|-----------|-------------|
| `mysql_connect` | `mysql_connect(connstr: string) -> MysqlConn` | Connect to a MySQL database |
| `mysql_connect_url` | `mysql_connect_url(url: string) -> string` | Parse a MySQL connection URL |
| `mysql_ok` | `mysql_ok(conn: MysqlConn) -> int` | Check if a MySQL connection is valid |
| `mysql_close` | `mysql_close(conn: MysqlConn) -> int` | Close a MySQL connection |
| `mysql_is_mariadb` | `mysql_is_mariadb(version: string) -> int` | Check if the server is MariaDB |
| `mysql_driver_name` | `mysql_driver_name(version: string) -> string` | Return the driver name for a version string |

---

## 16. Redis

| Function | Signature | Description |
|----------|-----------|-------------|
| `redis_connect` | `redis_connect(addr: string) -> RedisConn` | Connect to a Redis server |
| `redis_connect_url` | `redis_connect_url(url: string) -> string` | Parse a Redis connection URL |
| `redis_ok` | `redis_ok(conn: RedisConn) -> int` | Check if a Redis connection is valid |
| `redis_close` | `redis_close(conn: RedisConn) -> int` | Close a Redis connection |
| `redis_conn_ping` | `redis_conn_ping(conn: RedisConn) -> string` | Ping the Redis server |
| `redis_conn_set` | `redis_conn_set(conn: RedisConn, key: string, val: string) -> string` | Set a key-value pair |
| `redis_conn_get` | `redis_conn_get(conn: RedisConn, key: string) -> string` | Get a value by key |
| `redis_conn_del` | `redis_conn_del(conn: RedisConn, key: string) -> string` | Delete a key |
| `redis_conn_exists` | `redis_conn_exists(conn: RedisConn, key: string) -> string` | Check if a key exists |
| `redis_ping` | `redis_ping(addr: string, port: int) -> string` | Ping a Redis server by address and port |
| `redis_set` | `redis_set(addr: string, port: int, key: string, val: string) -> string` | Set a value on a Redis server |
| `redis_get` | `redis_get(addr: string, port: int, key: string) -> string` | Get a value from a Redis server |
| `redis_del` | `redis_del(addr: string, port: int, key: string) -> string` | Delete a key from a Redis server |
| `redis_exists` | `redis_exists(addr: string, port: int, key: string) -> string` | Check if a key exists on a Redis server |
| `redis_mock_once` | `redis_mock_once(port: int) -> int` | Start a single-request Redis mock |
| `redis_mock_kv` | `redis_mock_kv(port: int, count: int) -> int` | Start a Redis mock with key-value support |

---

## 17. Crypto & Security

| Function | Signature | Description |
|----------|-----------|-------------|
| `sha1` | `sha1(data: string) -> string` | Compute SHA-1 hash (raw bytes) |
| `sha256` | `sha256(data: string) -> string` | Compute SHA-256 hash (raw bytes) |
| `sha512` | `sha512(data: string) -> string` | Compute SHA-512 hash (raw bytes) |
| `hmac_sha256` | `hmac_sha256(key: string, data: string) -> string` | Compute HMAC-SHA256 (hex) |
| `sha256_raw` | `sha256_raw(data: string) -> string` | Compute SHA-256 (raw 32 bytes) |
| `hmac_sha256_raw` | `hmac_sha256_raw(key: string, data: string) -> string` | Compute HMAC-SHA256 (raw 32 bytes) |
| `xor_bytes` | `xor_bytes(a: string, b: string) -> string` | Pairwise XOR of two equal-length byte strings |
| `random_bytes` | `random_bytes(n: int) -> string` | Generate n cryptographically random bytes |

### Password Hashing

| Function | Signature | Description |
|----------|-----------|-------------|
| `argon2id_hash` | `argon2id_hash(password: string) -> string` | Hash a password with Argon2id (PHC string). Preferred for new systems |
| `argon2id_verify` | `argon2id_verify(phc: string, password: string) -> int` | Verify a password against an Argon2id PHC string (1/0) |
| `bcrypt_hash` | `bcrypt_hash(password: string, cost: int) -> string` | Hash a password with bcrypt (`$2b$`). Cost 4–31; needs libxcrypt (Linux) |
| `bcrypt_verify` | `bcrypt_verify(hash: string, password: string) -> int` | Verify a password against a bcrypt hash (1/0) |
| `bcrypt_available` | `bcrypt_available() -> int` | Whether bcrypt is available in this build (1/0) |
| `pbkdf2_sha256` | `pbkdf2_sha256(password: string, salt: string, iterations: int, dklen: int) -> string` | PBKDF2-HMAC-SHA256 derived key (raw bytes) |

### SCRAM-SHA-256

Crypto core for SCRAM-SHA-256 (RFC 5802 / RFC 7677) challenge-response auth,
exposed via the `crypto` package (`crypto.scram_*`). Compose the `AuthMessage`
from the protocol strings yourself. See `examples/testing/scram_test.mko`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `crypto.scram_salted_password` | `(password, salt, iterations) -> string` | PBKDF2 salted password (salt is raw bytes) |
| `crypto.scram_client_key` | `(salted) -> string` | `HMAC(salted, "Client Key")` |
| `crypto.scram_server_key` | `(salted) -> string` | `HMAC(salted, "Server Key")` |
| `crypto.scram_stored_key` | `(client_key) -> string` | `SHA256(client_key)` |
| `crypto.scram_client_signature` | `(stored_key, auth) -> string` | `HMAC(stored_key, auth)` |
| `crypto.scram_server_signature` | `(server_key, auth) -> string` | `HMAC(server_key, auth)` |
| `crypto.scram_client_proof` | `(client_key, client_sig) -> string` | `client_key XOR client_sig` |
| `crypto.scram_verify_proof` | `(stored_key, auth, proof) -> int` | Server-side: validate a client proof (1/0) |
| `const_eq` | `const_eq(a: string, b: string) -> int` | Constant-time string comparison |
| `crypto_eq` | `crypto_eq(a: string, b: string) -> int` | Constant-time byte comparison |
| `secret_from_str` | `secret_from_str(s: string) -> Secret` | Wrap a string as a secret (zeroized on drop) |
| `secret_drop` | `secret_drop(s: Secret) -> void` | Securely erase and drop a secret |
| `aead_available` | `aead_available() -> int` | Check if AEAD ciphers are available |
| `aes_gcm_seal` | `aes_gcm_seal(key: string, nonce: string, plaintext: string, aad: string) -> string` | Encrypt with AES-GCM |
| `aes_gcm_open` | `aes_gcm_open(key: string, nonce: string, ciphertext: string, aad: string) -> string` | Decrypt with AES-GCM |
| `chacha20_poly1305_seal` | `chacha20_poly1305_seal(key: string, nonce: string, plaintext: string, aad: string) -> string` | Encrypt with ChaCha20-Poly1305 |
| `chacha20_poly1305_open` | `chacha20_poly1305_open(key: string, nonce: string, ciphertext: string, aad: string) -> string` | Decrypt with ChaCha20-Poly1305 |

---

## 18. Encoding

| Function | Signature | Description |
|----------|-----------|-------------|
| `base64_encode` | `base64_encode(data: string) -> string` | Encode data as base64 |
| `base64_decode` | `base64_decode(data: string) -> string` | Decode base64 data |
| `base32_encode` | `base32_encode(data: string) -> string` | Encode data as base32 |
| `hex_encode` | `hex_encode(data: string) -> string` | Encode data as hexadecimal |
| `hex_decode` | `hex_decode(data: string) -> string` | Decode hexadecimal data |
| `url_query_escape` | `url_query_escape(s: string) -> string` | Escape a string for use in URL query parameters |
| `gzip_compress` | `gzip_compress(data: string) -> string` | Compress data with gzip |
| `gzip_decompress` | `gzip_decompress(data: string) -> string` | Decompress gzip data |
| `gzip_available` | `gzip_available() -> int` | Check if gzip is available |
| `bin_encode_int` | `bin_encode_int(n: int) -> string` | Encode an integer in binary format |

### Binary Encoding (Little-Endian)

| Function | Signature | Description |
|----------|-----------|-------------|
| `binary_put_u16le` | `binary_put_u16le(n: int) -> string` | Encode uint16 as little-endian bytes |
| `binary_put_u32le` | `binary_put_u32le(n: int) -> string` | Encode uint32 as little-endian bytes |
| `binary_put_u64le` | `binary_put_u64le(n: int) -> string` | Encode uint64 as little-endian bytes |
| `binary_u16le` | `binary_u16le(data: string) -> int` | Decode little-endian bytes as uint16 |
| `binary_u32le` | `binary_u32le(data: string) -> int` | Decode little-endian bytes as uint32 |
| `binary_u64le` | `binary_u64le(data: string) -> int` | Decode little-endian bytes as uint64 |

### Binary Encoding (Big-Endian)

| Function | Signature | Description |
|----------|-----------|-------------|
| `binary_put_u16be` | `binary_put_u16be(n: int) -> string` | Encode uint16 as big-endian bytes |
| `binary_put_u32be` | `binary_put_u32be(n: int) -> string` | Encode uint32 as big-endian bytes |
| `binary_put_u64be` | `binary_put_u64be(n: int) -> string` | Encode uint64 as big-endian bytes |
| `binary_u16be` | `binary_u16be(data: string) -> int` | Decode big-endian bytes as uint16 |
| `binary_u32be` | `binary_u32be(data: string) -> int` | Decode big-endian bytes as uint32 |
| `binary_u64be` | `binary_u64be(data: string) -> int` | Decode big-endian bytes as uint64 |

### Gob Encoding

| Function | Signature | Description |
|----------|-----------|-------------|
| `gob_encode_string` | `gob_encode_string(s: string) -> string` | Encode a string in gob format |
| `gob_decode_string` | `gob_decode_string(data: string) -> string` | Decode a gob-encoded string |
| `gob_encode_int` | `gob_encode_int(n: int) -> string` | Encode an integer in gob format |
| `gob_decode_int` | `gob_decode_int(data: string) -> int` | Decode a gob-encoded integer |
| `gob_encode_map_ss` | `gob_encode_map_ss(m: map[string]string) -> string` | Encode a map[string]string in gob format |
| `gob_decode_map_ss` | `gob_decode_map_ss(data: string) -> map[string]string` | Decode a gob-encoded map[string]string |
| `gob_encode_strs` | `gob_encode_strs(arr: []string) -> string` | Encode a string array in gob format |
| `gob_decode_strs` | `gob_decode_strs(data: string) -> []string` | Decode a gob-encoded string array |
| `gob_encode_struct` | `gob_encode_struct(val: ReflectValue) -> string` | Encode a reflected struct in gob format |
| `gob_decode_struct` | `gob_decode_struct(data: string) -> ReflectValue` | Decode a gob-encoded struct |

### Serialization Formats

| Function | Signature | Description |
|----------|-----------|-------------|
| `yaml_get_string` | `yaml_get_string(yaml: string, key: string) -> string` | Extract a string value from YAML by key |
| `toml_get_string` | `toml_get_string(toml: string, key: string) -> string` | Extract a string value from TOML by key |
| `toml_get_int` | `toml_get_int(toml: string, key: string) -> int` | Extract an integer value from TOML by key |
| `msgpack_int_hex` | `msgpack_int_hex(n: int) -> string` | Encode an integer as MessagePack hex |
| `cbor_int_hex` | `cbor_int_hex(n: int) -> string` | Encode an integer as CBOR hex |
| `avro_long_hex` | `avro_long_hex(n: int) -> string` | Encode a long as Avro hex |
| `csv_split_line` | `csv_split_line(line: string) -> []string` | Split a CSV line into fields |
| `csv_join_row` | `csv_join_row(fields: []string) -> string` | Join fields into a CSV row |
| `xml_escape` | `xml_escape(s: string) -> string` | Escape special characters for XML |
| `html_escape` | `html_escape(s: string) -> string` | Escape special characters for HTML |
| `xml_tag_text` | `xml_tag_text(tag: string, text: string) -> string` | Create an XML element with text content |

---

## 19. Regular Expressions

| Function | Signature | Description |
|----------|-----------|-------------|
| `regex_match` | `regex_match(pattern: string, s: string) -> bool` | Check if string matches a regex pattern |
| `regex_find` | `regex_find(pattern: string, s: string) -> string` | Find the first match of a pattern |
| `regex_find_all` | `regex_find_all(pattern: string, s: string, max: int) -> []string` | Find all matches up to max count |
| `regex_replace` | `regex_replace(pattern: string, s: string, replacement: string) -> string` | Replace the first match |
| `regex_replace_all` | `regex_replace_all(pattern: string, s: string, replacement: string) -> string` | Replace all matches |
| `regex_capture` | `regex_capture(pattern: string, s: string, group: int) -> string` | Extract a capture group from a match |
| `regex_valid` | `regex_valid(pattern: string) -> int` | Check if a regex pattern is valid |
| `regex_quote_meta` | `regex_quote_meta(s: string) -> string` | Escape regex metacharacters in a string |

---

## 20. UUID

| Function | Signature | Description |
|----------|-----------|-------------|
| `uuid_v4` | `uuid_v4() -> Uuid` | Generate a random v4 UUID |
| `uuid_nil` | `uuid_nil() -> Uuid` | Return the nil UUID (all zeros) |
| `uuid_string` | `uuid_string(u: Uuid) -> string` | Convert a UUID to its string representation |
| `uuid_parse` | `uuid_parse(s: string) -> Uuid` | Parse a string as a UUID |
| `uuid_parse_ok` | `uuid_parse_ok(s: string) -> bool` | Check if a string is a valid UUID |
| `uuid_eq` | `uuid_eq(a: Uuid, b: Uuid) -> bool` | Compare two UUIDs for equality |
| `uuid_is_nil` | `uuid_is_nil(u: Uuid) -> bool` | Check if a UUID is nil |
| `uuid_check` | `uuid_check(s: string) -> Result[int, string]` | Parse and validate a UUID string |

---

## 21. Cookies & Sessions

| Function | Signature | Description |
|----------|-----------|-------------|
| `cookie_get` | `cookie_get(header: string, name: string) -> string` | Extract a cookie value from a Cookie header |
| `cookie_make` | `cookie_make(name: string, value: string, max_age: int) -> string` | Create a Set-Cookie header string |
| `session_id_new` | `session_id_new() -> string` | Generate a new cryptographically random session ID |
| `csrf_token` | `csrf_token() -> string` | Generate a new CSRF token |
| `csrf_check` | `csrf_check(token: string, expected: string) -> int` | Validate a CSRF token |

---

## 22. Authentication

| Function | Signature | Description |
|----------|-----------|-------------|
| `auth_bearer` | `auth_bearer(header: string) -> string` | Extract bearer token from Authorization header |
| `auth_check_bearer` | `auth_check_bearer(header: string, expected: string) -> int` | Validate a bearer token |
| `auth_basic_header` | `auth_basic_header(user: string, pass: string) -> string` | Create a Basic auth header |
| `auth_check_basic` | `auth_check_basic(header: string, user: string, pass: string) -> int` | Validate Basic auth credentials |
| `auth_token_sign` | `auth_token_sign(subject: string, secret: string) -> string` | Sign an auth token with a subject |
| `auth_token_check` | `auth_token_check(token: string, secret: string) -> int` | Verify an auth token |
| `auth_token_subject` | `auth_token_subject(token: string) -> string` | Extract the subject from an auth token |
| `auth_session_cookie` | `auth_session_cookie(user: string, secret: string, name: string) -> int` | Create a session cookie for a user |
| `auth_role_has` | `auth_role_has(roles: string, role: string) -> int` | Check if a role list contains a specific role |
| `authz_allow_role` | `authz_allow_role(roles: string, required: string) -> int` | Authorize based on required role |

---

## 23. Channels

| Function | Signature | Description |
|----------|-----------|-------------|
| `chan_new` | `chan_new(capacity: int) -> chan[int]` | Create a new buffered int channel |
| `chan_open[T]` | `chan_open[T](capacity: int) -> chan[T]` | Typed channel (see element types below) |
| `chan_try_send` | `chan_try_send(ch: chan[int], val: int) -> int` | Non-blocking send; returns 0 on success |
| `chan_len` | `chan_len(ch: chan[int]) -> int` | Return the number of items in the channel |
| `chan_cap` | `chan_cap(ch: chan[int]) -> int` | Return the capacity of the channel |
| `chan_select2` | `chan_select2(a: chan[int], b: chan[int], timeout_ms: int) -> int` | Select from two **int** channels with timeout |
| `chan_select3` | `chan_select3(a: chan[int], b: chan[int], c: chan[int], timeout_ms: int) -> int` | Select from three int channels with timeout |
| `chan_select4` | `chan_select4(a: chan[int], b: chan[int], c: chan[int], d: chan[int], timeout_ms: int) -> int` | Select from four int channels with timeout |
| `chan_select_value` | `chan_select_value() -> int` | Value from last **int** select |
| `chan_str_select2` | `chan_str_select2(a, b, timeout_ms) -> int` | Select between two string channels |
| `chan_select_value_str` | `chan_select_value_str() -> string` | Value from last **string** select |

**Methods on `ch`:** `ch.send(v)`, `ch.recv()`, `ch.close()`, `job.join_timeout(ms)` (returns 0 on timeout).

**`chan_open[T]` element types**

| `T` | Runtime | Notes |
|-----|---------|--------|
| int family / bool | `MakoChan` | Default int ring |
| float | `MakoChan` | Bitcast via `mako_f64_to_bits` / `mako_bits_to_f64` |
| string | `MakoChanStr` | Owned strings; `chan_str_select2` / select syntax |
| named struct | `MakoChanPtr` | Heap-box on send; free on recv; **select takes the message** (do not `recv` again in the arm) |

`select timeout … { }` uses int, string, or **struct/ptr** select when all arms match.
Helpers: `chan_select_value` / `chan_select_value_str` / `mako_chan_select_value_ptr`.
Tests: `chan_struct_test`, `chan_float_test`, `wave8_queue_test`, `wave9_queue_test`.

---

## 24. Concurrency

### Crew / kick / join / fan (language)

| Construct | Meaning |
|-----------|---------|
| `crew t { … }` | Structured scope; cancel+join on exit (no orphan tasks) |
| `t.kick(f(args…))` | Spawn on crew; returns `Job[R]` |
| `job.join()` / `join(job)` | Wait for result of type `R` |
| `job.join_timeout(ms)` | Timed join → **`Result[R, string]`** (`Ok`/`Err("timeout")`). If `R` is already `Result[T, string]`, **flattens** (no nest). |
| `t.drain(ms)` / `crew_drain` | Cancel + join with timeout budget |
| `t.cancel()` / `t.cancelled()` | Cooperative cancel flag |
| `fan(xs, \|x\| …)` | Parallel map over array |

**Job return types (`join`)**

| `R` | Behavior |
|-----|----------|
| int / bool | Packed in `intptr_t` |
| string | Heap-boxed across pthread; join unboxes |
| `Result[T, E]` | Heap-boxed `MakoResultInt`; join unboxes |
| float | Bitcast through `intptr_t` |

Kick **args** that are sendable: Copy scalars, **POD structs** (int/float/bool/**string** fields, heap-boxed; strings cloned), string (cloned), chan handles, ShareInt/sync handles. Arrays/maps/non-POD structs remain rejected (`examples/bad/kick_non_pod.mko`).

`reflect_value_of(s)` snapshots POD struct fields (any field count) into a reflect bag,
flattening nested POD structs leaf-first. Structs with maps/slices remain rejected
(`examples/bad/reflect_non_pod.mko`).
`Result[[]int|[]string|[]float|[]Struct, E]` and
`Result[map[string]int|map[int]int|map[string]string, E]` Ok are supported
(heap-boxed arrays / map pointers).

**`fan` element types:** `[]int`, `[]float`, `[]string`, `[]Struct` (POD named structs via `mako_par_map_bytes`).

Tests: `crew_fan_test.mko`, `job_join_typed_test.mko`, `fan_struct_test.mko`, `fan_float_test.mko`, `fan_string_test.mko`, `crew_drain_test.mko`.

### Mutexes

| Function | Signature | Description |
|----------|-----------|-------------|
| `mutex_new` | `mutex_new() -> Mutex` | Create a new mutex |
| `mutex_lock` | `mutex_lock(m: Mutex) -> void` | Acquire the mutex lock |
| `mutex_unlock` | `mutex_unlock(m: Mutex) -> void` | Release the mutex lock |
| `rwmutex_new` | `rwmutex_new() -> RWMutex` | Create a new readers-writer mutex |
| `rwmutex_rlock` | `rwmutex_rlock(m: RWMutex) -> void` | Acquire a read lock |
| `rwmutex_runlock` | `rwmutex_runlock(m: RWMutex) -> void` | Release a read lock |
| `rwmutex_lock` | `rwmutex_lock(m: RWMutex) -> void` | Acquire a write lock |
| `rwmutex_unlock` | `rwmutex_unlock(m: RWMutex) -> void` | Release a write lock |

### Atomics

| Function | Signature | Description |
|----------|-----------|-------------|
| `atomic_new` | `atomic_new(val: int) -> AtomicInt` | Create a new atomic integer |
| `atomic_load` | `atomic_load(a: AtomicInt) -> int` | Atomically load the value |
| `atomic_store` | `atomic_store(a: AtomicInt, val: int) -> void` | Atomically store a value |
| `atomic_add` | `atomic_add(a: AtomicInt, delta: int) -> int` | Atomically add and return the new value |
| `atomic_cas` | `atomic_cas(a: AtomicInt, expected: int, desired: int) -> int` | Compare-and-swap; returns 1 on success |

### WaitGroup

| Function | Signature | Description |
|----------|-----------|-------------|
| `wait_group_new` | `wait_group_new() -> WaitGroup` | Create a new wait group |
| `wait_group_add` | `wait_group_add(wg: WaitGroup, delta: int) -> void` | Add to the wait group counter |
| `wait_group_done` | `wait_group_done(wg: WaitGroup) -> void` | Decrement the wait group counter |
| `wait_group_wait` | `wait_group_wait(wg: WaitGroup) -> void` | Block until the counter reaches zero |

### Shared References

| Function | Signature | Description |
|----------|-----------|-------------|
| `share_int` | `share_int(val: int) -> ShareInt` | Create a shared reference-counted integer |
| `share_clone` | `share_clone(s: ShareInt) -> ShareInt` | Clone a shared integer (increment ref count) |
| `share_get` | `share_get(s: ShareInt) -> int` | Read the value of a shared integer |
| `share_drop` | `share_drop(s: ShareInt) -> void` | Drop a shared integer reference |

---

## 25. Concurrent Map (CMap)

| Function | Signature | Description |
|----------|-----------|-------------|
| `cmap_new` | `cmap_new() -> CMap` | Create a new concurrent hash map |
| `cmap_set` | `cmap_set(m: CMap, key: string, val: string) -> void` | Set a key-value pair |
| `cmap_get` | `cmap_get(m: CMap, key: string) -> string` | Get a value by key |
| `cmap_has` | `cmap_has(m: CMap, key: string) -> int` | Check if a key exists |
| `cmap_del` | `cmap_del(m: CMap, key: string) -> int` | Delete a key |
| `cmap_len` | `cmap_len(m: CMap) -> int` | Return the number of entries |
| `cmap_incr` | `cmap_incr(m: CMap, key: string, delta: int) -> int` | Atomically increment a numeric value |

---

## 26. Event Loop

| Function | Signature | Description |
|----------|-----------|-------------|
| `evloop_new` | `evloop_new() -> EvLoop` | Create a new event loop |
| `evloop_add` | `evloop_add(el: EvLoop, fd: int, flags: int) -> int` | Add a file descriptor to the event loop |
| `evloop_mod` | `evloop_mod(el: EvLoop, fd: int, flags: int) -> int` | Modify events for a file descriptor |
| `evloop_del` | `evloop_del(el: EvLoop, fd: int) -> int` | Remove a file descriptor from the event loop |
| `evloop_wait` | `evloop_wait(el: EvLoop, timeout_ms: int) -> int` | Wait for events with timeout |
| `evloop_event_fd` | `evloop_event_fd(el: EvLoop, idx: int) -> int` | Get the fd of the i-th ready event |
| `evloop_event_flags` | `evloop_event_flags(el: EvLoop, idx: int) -> int` | Get the flags of the i-th ready event |
| `evloop_close` | `evloop_close(el: EvLoop) -> int` | Close the event loop |

### Non-Blocking I/O Helpers

| Function | Signature | Description |
|----------|-----------|-------------|
| `nb_listen` | `nb_listen(port: int) -> int` | Create a non-blocking listening socket |
| `nb_accept` | `nb_accept(fd: int) -> int` | Non-blocking accept |
| `nb_read` | `nb_read(fd: int) -> string` | Non-blocking read |
| `nb_write` | `nb_write(fd: int, data: string) -> int` | Non-blocking write |
| `nb_udp_bind` | `nb_udp_bind(port: int) -> int` | Bind a non-blocking UDP socket |
| `nb_udp_recv` | `nb_udp_recv(fd: int) -> string` | Non-blocking UDP receive |
| `nb_close` | `nb_close(fd: int) -> int` | Close a non-blocking fd |

---

## 27. Binary Buffer

| Function | Signature | Description |
|----------|-----------|-------------|
| `buf_pack_new` | `buf_pack_new(capacity: int) -> Buf` | Create a new binary buffer with given capacity |
| `buf_from_string` | `buf_from_string(s: string) -> Buf` | Create a buffer from a string |
| `buf_to_string` | `buf_to_string(b: Buf) -> string` | Convert buffer contents to a string |
| `buf_len` | `buf_len(b: Buf) -> int` | Return the length of the buffer |
| `buf_pos` | `buf_pos(b: Buf) -> int` | Return the current read/write position |
| `buf_reset` | `buf_reset(b: Buf) -> void` | Reset buffer position to zero |
| `buf_seek` | `buf_seek(b: Buf, pos: int) -> void` | Seek to a specific position |
| `buf_free` | `buf_free(b: Buf) -> void` | Free the buffer memory |

### Write Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `buf_write_u8` | `buf_write_u8(b: Buf, val: int) -> void` | Write a uint8 |
| `buf_write_u16` | `buf_write_u16(b: Buf, val: int) -> void` | Write a uint16 (little-endian) |
| `buf_write_u32` | `buf_write_u32(b: Buf, val: int) -> void` | Write a uint32 (little-endian) |
| `buf_write_u64` | `buf_write_u64(b: Buf, val: int) -> void` | Write a uint64 (little-endian) |
| `buf_write_i32` | `buf_write_i32(b: Buf, val: int) -> void` | Write a signed int32 |
| `buf_write_f32` | `buf_write_f32(b: Buf, val: float) -> void` | Write a 32-bit float |
| `buf_write_f64` | `buf_write_f64(b: Buf, val: float) -> void` | Write a 64-bit float |
| `buf_write_bytes` | `buf_write_bytes(b: Buf, data: string) -> void` | Write raw bytes |
| `buf_write_str` | `buf_write_str(b: Buf, s: string) -> void` | Write a length-prefixed string |
| `buf_write_u16be` | `buf_write_u16be(b: Buf, val: int) -> void` | Write a uint16 (big-endian) |
| `buf_write_u32be` | `buf_write_u32be(b: Buf, val: int) -> void` | Write a uint32 (big-endian) |

### Read Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `buf_read_u8` | `buf_read_u8(b: Buf) -> int` | Read a uint8 |
| `buf_read_u16` | `buf_read_u16(b: Buf) -> int` | Read a uint16 (little-endian) |
| `buf_read_u32` | `buf_read_u32(b: Buf) -> int` | Read a uint32 (little-endian) |
| `buf_read_u64` | `buf_read_u64(b: Buf) -> int` | Read a uint64 (little-endian) |
| `buf_read_i32` | `buf_read_i32(b: Buf) -> int` | Read a signed int32 |
| `buf_read_f32` | `buf_read_f32(b: Buf) -> float` | Read a 32-bit float |
| `buf_read_f64` | `buf_read_f64(b: Buf) -> float` | Read a 64-bit float |
| `buf_read_bytes` | `buf_read_bytes(b: Buf, n: int) -> string` | Read n raw bytes |
| `buf_read_str` | `buf_read_str(b: Buf) -> string` | Read a length-prefixed string |
| `buf_read_u16be` | `buf_read_u16be(b: Buf) -> int` | Read a uint16 (big-endian) |
| `buf_read_u32be` | `buf_read_u32be(b: Buf) -> int` | Read a uint32 (big-endian) |

### Byte Buffer Pool

| Function | Signature | Description |
|----------|-----------|-------------|
| `buf_get` | `buf_get(size: int) -> []byte` | Get a byte buffer from the pool |
| `buf_put` | `buf_put(b: []byte) -> void` | Return a byte buffer to the pool |

---

## 28. Game Networking

| Function | Signature | Description |
|----------|-----------|-------------|
| `game_udp_bind` | `game_udp_bind(port: int) -> GameUDP` | Bind a game UDP socket |
| `game_udp_recv` | `game_udp_recv(sock: GameUDP) -> string` | Receive a packet |
| `game_udp_sender` | `game_udp_sender(sock: GameUDP) -> int` | Get the sender peer ID of the last received packet |
| `game_udp_send` | `game_udp_send(sock: GameUDP, peer: int, data: string) -> int` | Send a packet to a specific peer |
| `game_udp_broadcast` | `game_udp_broadcast(sock: GameUDP, data: string) -> int` | Broadcast a packet to all peers |
| `game_udp_kick` | `game_udp_kick(sock: GameUDP, peer: int) -> void` | Disconnect a peer |
| `game_udp_peers` | `game_udp_peers(sock: GameUDP) -> int` | Return the number of connected peers |
| `game_udp_fd` | `game_udp_fd(sock: GameUDP) -> int` | Get the underlying file descriptor |
| `game_udp_close` | `game_udp_close(sock: GameUDP) -> void` | Close the game UDP socket |
| `tick_now_us` | `tick_now_us() -> int` | Current time in microseconds (monotonic) |
| `tick_sleep_us` | `tick_sleep_us(target: int, period: int) -> int` | Sleep until next tick period |

### Game Loop

| Function | Signature | Description |
|----------|-----------|-------------|
| `game_fixed_steps` | `game_fixed_steps(dt: int, step: int, accum: int) -> int` | Calculate fixed-step update count |
| `game_fixed_remainder` | `game_fixed_remainder(dt: int, step: int, accum: int) -> int` | Remaining accumulator after fixed steps |
| `game_alpha` | `game_alpha(dt: int, step: int, accum: int) -> int` | Interpolation alpha for rendering |
| `game_frame_budget_ok` | `game_frame_budget_ok(elapsed: int, budget: int) -> int` | Check if frame completed within budget |

### Fixed-Point Math

| Function | Signature | Description |
|----------|-----------|-------------|
| `fx_from_int` | `fx_from_int(val: int, shift: int) -> int` | Convert integer to fixed-point |
| `fx_to_int` | `fx_to_int(val: int, shift: int) -> int` | Convert fixed-point to integer |
| `fx_mul` | `fx_mul(a: int, b: int, shift: int) -> int` | Multiply two fixed-point values |
| `fx_div` | `fx_div(a: int, b: int, shift: int) -> int` | Divide two fixed-point values |

### Deterministic RNG

| Function | Signature | Description |
|----------|-----------|-------------|
| `det_rng_next` | `det_rng_next(state: int) -> int` | Advance deterministic RNG state |
| `det_rng_range` | `det_rng_range(state: int, max: int) -> int` | Get a deterministic random value in range |

### Replay

| Function | Signature | Description |
|----------|-----------|-------------|
| `replay_append` | `replay_append(buf: string, frame: int, input: int) -> string` | Append an input to a replay buffer |
| `replay_input` | `replay_input(buf: string, frame: int) -> int` | Get the input at a specific frame |

---

## 29. Cloud / Distributed

### Consistent Hashing

| Function | Signature | Description |
|----------|-----------|-------------|
| `chash_new` | `chash_new(replicas: int, nodes: int) -> CHash` | Create a consistent hash ring |
| `chash_get` | `chash_get(ring: CHash, key: string) -> int` | Get the node for a key |
| `chash_add_node` | `chash_add_node(ring: CHash) -> int` | Add a node to the ring |
| `chash_remove_node` | `chash_remove_node(ring: CHash, node: int) -> void` | Remove a node from the ring |
| `chash_node_count` | `chash_node_count(ring: CHash) -> int` | Return the number of nodes |
| `chash_free` | `chash_free(ring: CHash) -> void` | Free the hash ring |

### Rate Limiting

| Function | Signature | Description |
|----------|-----------|-------------|
| `ratelimit_new` | `ratelimit_new(max: int, window_ms: int) -> RateLimiter` | Create a new rate limiter |
| `ratelimit_allow` | `ratelimit_allow(rl: RateLimiter) -> int` | Check if a request is allowed |
| `ratelimit_remaining` | `ratelimit_remaining(rl: RateLimiter) -> int` | Return remaining allowed requests |
| `ratelimit_free` | `ratelimit_free(rl: RateLimiter) -> void` | Free the rate limiter |
| `rate_allow` | `rate_allow(key: string, max: int, window_ms: int) -> int` | Key-based rate limiting check |
| `rate_remaining` | `rate_remaining(key: string, max: int, window_ms: int) -> int` | Key-based remaining requests |

### Circuit Breaker

| Function | Signature | Description |
|----------|-----------|-------------|
| `breaker_new` | `breaker_new(threshold: int, timeout_ms: int, half_max: int) -> CircuitBreaker` | Create a new circuit breaker |
| `breaker_allow` | `breaker_allow(cb: CircuitBreaker) -> int` | Check if a request is allowed |
| `breaker_success` | `breaker_success(cb: CircuitBreaker) -> void` | Record a successful operation |
| `breaker_failure` | `breaker_failure(cb: CircuitBreaker) -> void` | Record a failed operation |
| `breaker_state` | `breaker_state(cb: CircuitBreaker) -> int` | Get the current state (closed/open/half-open) |
| `breaker_reset` | `breaker_reset(cb: CircuitBreaker) -> void` | Reset to closed state |
| `breaker_free` | `breaker_free(cb: CircuitBreaker) -> void` | Free the circuit breaker |

### JWT

| Function | Signature | Description |
|----------|-----------|-------------|
| `jwt_sign` | `jwt_sign(payload: string, secret: string) -> string` | Sign a JWT payload |
| `jwt_verify` | `jwt_verify(token: string, secret: string) -> int` | Verify a JWT signature |
| `jwt_payload` | `jwt_payload(token: string) -> string` | Extract the payload from a JWT |

### Backoff

| Function | Signature | Description |
|----------|-----------|-------------|
| `backoff_ms` | `backoff_ms(attempt: int, base_ms: int, max_ms: int) -> int` | Calculate exponential backoff delay |

---

## 30. HTTP Engine

| Function | Signature | Description |
|----------|-----------|-------------|
| `httpengine_new` | `httpengine_new(port: int, max_conns: int) -> HttpEngine` | Create a new HTTP engine |
| `httpengine_route` | `httpengine_route(engine: HttpEngine, pattern: string, method: int, handler: string, name: string) -> void` | Register a route |
| `httpengine_serve` | `httpengine_serve(engine: HttpEngine) -> int` | Start serving |

---

## 31. Router

| Function | Signature | Description |
|----------|-----------|-------------|
| `router_new` | `router_new() -> int` | Create a new router |
| `router_group` | `router_group(router: int, prefix: string) -> int` | Create a route group with a prefix |
| `router_add` | `router_add(router: int, method: string, pattern: string, handler: string) -> int` | Add a route |
| `router_match` | `router_match(router: int, req: HttpRequest) -> string` | Match a request to a route handler |
| `router_match_path` | `router_match_path(router: int, method: string, path: string) -> string` | Match a method and path to a handler |
| `router_param` | `router_param(router: int, req: HttpRequest, name: string) -> string` | Extract a route parameter |
| `router_count` | `router_count(router: int) -> int` | Return the number of registered routes |

---

## 32. Request Context & Middleware

| Function | Signature | Description |
|----------|-----------|-------------|
| `reqctx_new` | `reqctx_new() -> int` | Create a new request context |
| `reqctx_set` | `reqctx_set(ctx: int, key: string, val: string) -> int` | Set a context value |
| `reqctx_get` | `reqctx_get(ctx: int, key: string) -> string` | Get a context value |
| `reqctx_has` | `reqctx_has(ctx: int, key: string) -> int` | Check if a context key exists |
| `reqctx_count` | `reqctx_count(ctx: int) -> int` | Return the number of context entries |
| `middleware_allow_methods` | `middleware_allow_methods(req: HttpRequest, methods: string) -> int` | Check if request method is allowed |
| `middleware_next` | `middleware_next(ctx: int, name: string) -> int` | Advance to the next middleware |
| `middleware_ran` | `middleware_ran(ctx: int, name: string) -> int` | Check if a middleware has run |
| `middleware_trace` | `middleware_trace(ctx: int) -> string` | Get the middleware execution trace |
| `middleware_require_context` | `middleware_require_context(ctx: int, key: string) -> int` | Require a context key to be set |

---

## 33. Testing

| Function | Signature | Description |
|----------|-----------|-------------|
| `assert` | `assert(cond: bool) -> void` | Assert a condition is true; panic if false |
| `assert_eq` | `assert_eq(a: int, b: int) -> void` | Assert two integers are equal |
| `assert_eq_str` | `assert_eq_str(a: string, b: string) -> void` | Assert two strings are equal |
| `fail` | `fail(msg: string) -> void` | Unconditionally fail with a message |
| `t_run` | `t_run(name: string) -> void` | Run a named test case |
| `t_run_nested` | `t_run_nested(name: string) -> void` | Run a nested test case |
| `black_box` | `black_box(n: int) -> int` | Prevent compiler optimization (benchmarking) |
| `httptest_serve_once` | `httptest_serve_once(port: int, response: string) -> int` | Start a test HTTP server for one request |
| `httptest_get` | `httptest_get(url: string) -> string` | Perform a test HTTP GET |
| `httptest_status` | `httptest_status() -> int` | Get the status of the last test request |
| `httptest_header` | `httptest_header(name: string) -> string` | Get a header from the last test response |

---

## 34. Runtime

| Function | Signature | Description |
|----------|-----------|-------------|
| `runtime_stats_json` | `runtime_stats_json() -> string` | Return runtime statistics as JSON |
| `runtime_stats_reset` | `runtime_stats_reset() -> void` | Reset runtime statistics counters |
| `exit` | `exit(code: int) -> void` | Exit the process with a status code |
| `leak_mark` | `leak_mark() -> int` | Mark current allocation state for leak detection |
| `leak_bytes_since` | `leak_bytes_since(mark: int) -> int` | Bytes allocated since a leak mark |
| `leak_detected` | `leak_detected(mark: int) -> int` | Check if any leak detected since mark |
| `leak_assert_clear` | `leak_assert_clear(mark: int) -> int` | Assert no leaks since mark |
| `leak_report_json` | `leak_report_json(mark: int) -> string` | Leak report as JSON |
| `gc_arena_new` | `gc_arena_new() -> Arena` | Create a new GC arena |
| `arena_text` | `arena_text(a: Arena, s: string) -> string` | Allocate a string in the arena |
| `arena_ints` | `arena_ints(a: Arena, n: int) -> []int` | Allocate an int array in the arena |
| `arena_stamp` | `arena_stamp(a: Arena, n: int) -> int` | Stamp an arena allocation |
| `dlopen_probe` | `dlopen_probe(lib: string) -> int` | Check if a dynamic library can be loaded |
| `await_timeout` | `await_timeout(job: Job[int], timeout_ms: int) -> int` | Await a job with timeout |

---

## 35. Allocation Tracking

| Function | Signature | Description |
|----------|-----------|-------------|
| `alloc_track_reset` | `alloc_track_reset() -> int` | Reset the allocation tracker |
| `alloc_track_alloc` | `alloc_track_alloc(bytes: int) -> int` | Record an allocation |
| `alloc_track_free` | `alloc_track_free(bytes: int) -> int` | Record a free |
| `alloc_live_bytes` | `alloc_live_bytes() -> int` | Return currently live allocated bytes |
| `alloc_high_bytes` | `alloc_high_bytes() -> int` | Return peak allocated bytes |
| `alloc_report_json` | `alloc_report_json() -> string` | Allocation report as JSON |

---

## 36. TCP & UDP Networking

### TCP

| Function | Signature | Description |
|----------|-----------|-------------|
| `tcp_listen` | `tcp_listen(port: int) -> int` | Listen on a TCP port (all interfaces) |
| `tcp_listen_addr` | `tcp_listen_addr(host: string, port: int) -> int` | Listen bound to a specific address (`"127.0.0.1"`, `"*"` for all) |
| `tcp_listen_backlog` | `tcp_listen_backlog(host: string, port: int, backlog: int) -> int` | Listen with an explicit accept backlog (bounds inbound queue) |
| `tcp_accept` | `tcp_accept(listener: int) -> int` | Accept a TCP connection |
| `tcp_accept_nb` | `tcp_accept_nb(listener: int) -> int` | Non-blocking TCP accept |
| `tcp_connect` | `tcp_connect(host: string, port: int) -> int` | Connect to a TCP server |
| `tcp_connect_nb` | `tcp_connect_nb(host: string, port: int) -> int` | Nonblocking connect; returns fd while still connecting |
| `tcp_connect_check` | `tcp_connect_check(fd: int) -> int` | `1` connected, `0` pending, `-1` failed |
| `tcp_connect_wait` | `tcp_connect_wait(fd: int, timeout_ms: int) -> int` | Poll until connect completes (`1`/`0`/`-1`) |
| `tcp_pool_open` | `tcp_pool_open(host: string, port: int, max: int, timeout_ms: int) -> int` | Upstream connection pool handle |
| `tcp_pool_acquire` | `tcp_pool_acquire(pool: int) -> int` | Borrow a live fd (validates reuse) |
| `tcp_pool_release` | `tcp_pool_release(pool: int, fd: int, reusable: int) -> int` | Return fd; close if not reusable |
| `tcp_pool_close` | `tcp_pool_close(pool: int) -> int` | Close pool and all idle fds |
| `tcp_pool_idle` / `tcp_pool_open_count` | `…(pool) -> int` | Idle / total open connection counts |
| `tcp_fd_copy` / `tcp_splice` | `tcp_fd_copy(src, dst, max) -> int` | Efficient fd-to-fd copy (`splice` on Linux) |
| `tcp_proxy_pump` | `tcp_proxy_pump(a, b, timeout_ms, max) -> int` | Bidirectional stream pump |
| `tcp_write` | `tcp_write(conn: int, data: string) -> int` | Write data to a TCP connection |
| `tcp_read` | `tcp_read(conn: int) -> string` | Read data from a TCP connection |
| `tcp_read_print` | `tcp_read_print(conn: int) -> int` | Read and print TCP data |
| `tcp_nodelay` | `tcp_nodelay(conn: int) -> int` | Set TCP_NODELAY on a connection |
| `tcp_set_timeout` | `tcp_set_timeout(conn: int, ms: int) -> int` | Set recv+send timeout in ms (0 = block forever) |
| `tcp_keepalive` | `tcp_keepalive(conn: int, idle: int, interval: int, count: int) -> int` | Enable TCP keepalive; tune idle/interval (s) and probe count |
| `tcp_set_recv_buf` / `tcp_set_send_buf` | `…(fd, size) -> int` | Socket buffer sizing |
| `tcp_reuseport` | `tcp_reuseport(fd: int) -> int` | Enable `SO_REUSEPORT` (before bind) |
| `tcp_listen_reuseport` | `tcp_listen_reuseport(host, port, backlog) -> int` | Listen with reuseport |
| `tcp_accept4` | `tcp_accept4(listener: int) -> int` | Accept with `NONBLOCK\|CLOEXEC` |
| `tcp_close` | `tcp_close(conn: int) -> int` | Close a TCP connection |
| `http_forward` | `http_forward(host, port, method, path, body) -> string` | Forward to HTTP/1.1 backend; returns body only |
| `http_forward_full` | `http_forward_full(host, port, method, path, headers, body, timeout_ms) -> HttpForwardResult` | Status + body + byte counts (chunked OK) |
| `http_forward_fd` | `http_forward_fd(fd, method, path, host, headers, body, timeout_ms) -> HttpForwardResult` | Forward on pooled fd (`Connection: keep-alive`) |
| `http_forward_ok` | `http_forward_ok(r: HttpForwardResult) -> int` | `1` if response fully read |
| `http_forward_status` | `http_forward_status(r) -> int` | HTTP status (0 if fail) |
| `http_forward_body` | `http_forward_body(r) -> string` | Decoded body (chunked already decoded) |
| `http_forward_body_len` | `http_forward_body_len(r) -> int` | Body length |
| `http_forward_total_bytes` | `http_forward_total_bytes(r) -> int` | Raw response size (headers+body wire) |
| `http_forward_headers` | `http_forward_headers(r) -> string` | Raw header block after status line |
| `http_proxy_raw` | `http_proxy_raw(client_fd, backend_fd, raw_request, timeout_ms) -> ProxyIoResult` | Raw request/response byte pump (no rebuild) |
| `proxy_io_ok` / `proxy_io_bytes_written` / `proxy_io_bytes_read` | accessors on `ProxyIoResult` | |
| `http_parse` | `http_parse(raw: string) -> HttpParsed` | C hot-path request parse (method/path/host/headers/body) |
| `http_parsed_ok` | `http_parsed_ok(r) -> int` | `1` if method+path parsed |
| `http_parsed_method` / `path` / `host` / `headers` / `body` | accessors | |
| `http_parsed_content_length` | `…(r) -> int` | `-1` if absent / invalid |
| `http_parsed_chunked` | `…(r) -> int` | `1` if `Transfer-Encoding: chunked` |
| `http_parsed_header` | `http_parsed_header(r, name) -> string` | Case-insensitive single header |
| `http_decode_chunked` | `http_decode_chunked(chunked_body: string) -> string` | Decode a chunked body; incomplete/malformed → `""` |

### Reverse-proxy notes (edge cases)

**Pool (`tcp_pool_*`)**

- Global pool table is **mutex-protected** (`pthread_mutex`) for multi-crew / multi-kick use.
- `release(..., reusable=1)` validates the fd with a **nonblocking** probe (never waits on `SO_RCVTIMEO`); probe runs outside the pool lock.
- Closed peer / unexpected buffered data → fd is closed, not returned to idle.
- Bad host/port, empty host, or CR/LF in host → `open` returns `-1`.
- `max` connections: further `acquire` returns `-1` until a fd is released.
- Double `close` is safe (`0` on already-closed).

**`http_forward_full` / `http_forward_fd`**

- Builds the request with Host + Content-Length unless the caller already supplied them.
- Normalizes caller header blocks that omit a trailing `\r\n`.
- Rejects method/path/host containing CR/LF (request-smuggling seed).
- Reads body by **Content-Length**, **chunked** (extensions + trailers), or connection close.
- Statuses **1xx / 204 / 304** → empty body.
- Chunked incomplete on EOF → failure (`ok=0`).
- Max response size 16 MiB.

**`http_parse` / `http_decode_chunked`**

- Accepts CRLF or bare LF headers.
- Truncates body to Content-Length when the buffer is longer.
- Case-insensitive header names; trims trailing SP/HTAB on values.
- Incomplete headers still yield method/path with `ok=1` and empty body.
- Incomplete/malformed chunked → empty body string.

**`http_proxy_raw`**

- Same-fd client/backend is refused.
- Empty request → `ok=0`.

Tests: `examples/testing/proxy_pool_test.mko`, `examples/testing/proxy_edge_test.mko`.

### UDP

| Function | Signature | Description |
|----------|-----------|-------------|
| `udp_bind` | `udp_bind(port: int) -> int` | Bind a UDP socket |
| `udp_send_to` | `udp_send_to(fd: int, host: string, port: int, data: string) -> int` | Send UDP data to a host:port |
| `udp_recv` | `udp_recv(fd: int, max_bytes: int) -> string` | Receive UDP data |
| `udp_local_port` | `udp_local_port(fd: int) -> int` | Get the local port of a UDP socket |
| `udp_close` | `udp_close(fd: int) -> int` | Close a UDP socket |

### Unix Sockets

| Function | Signature | Description |
|----------|-----------|-------------|
| `unix_socket_pair` | `unix_socket_pair() -> int` | Create a Unix socket pair (returns first fd) |
| `unix_socket_pair_peer` | `unix_socket_pair_peer() -> int` | Get the peer fd of the last socket pair |
| `unix_write` | `unix_write(fd: int, data: string) -> int` | Write to a Unix socket |
| `unix_read` | `unix_read(fd: int, max: int) -> string` | Read from a Unix socket |
| `unix_close` | `unix_close(fd: int) -> int` | Close a Unix socket |

---

## 37. TLS

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_hs_reset` | `tls_hs_reset() -> int` | Reset the TLS handshake state machine |
| `tls_hs_state` | `tls_hs_state() -> int` | Get the current TLS handshake state |
| `tls_hs_advance` | `tls_hs_advance(msg: string) -> int` | Advance the handshake with a message |
| `tls_hs_is_app` | `tls_hs_is_app() -> int` | Check if handshake is complete (app data ready) |
| `tls_serve` | `tls_serve(port: int, cert: string, key: string, handler: string) -> int` | Start a TLS server |
| `tls_serve_once` | `tls_serve_once(port: int, cert: string, key: string, response: string) -> int` | Serve one TLS request |
| `tls_serve_n` | `tls_serve_n(port: int, cert: string, key: string, response: string, n: int) -> int` | Serve n TLS requests |
| `tls_listen_stub` | `tls_listen_stub(port: int) -> int` | Stub TLS listener |
| `tls_get_insecure` | `tls_get_insecure(host: string, port: int, path: string) -> string` | TLS GET without certificate verification |
| `tls_get` | `tls_get(host: string, port: int, path: string, ca: string) -> string` | TLS GET with CA certificate |
| `tls_post` | `tls_post(host: string, port: int, path: string, ca: string, body: string) -> string` | TLS POST with CA certificate |
| `tls_handshake_ok` | `tls_handshake_ok(host: string, port: int, ca: string) -> string` | Test TLS handshake |
| `tls_handshake_version` | `tls_handshake_version(host: string, port: int, ca: string) -> string` | Get negotiated TLS version |

### Socket-style TLS server

A blocking, socket-style API for terminating TLS on an accepted TCP fd (also
supports STARTTLS-style upgrades). ALPN advertises `h2`. Requires an OpenSSL
build; `tls_server_available()` reports 1 when present.

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_server_available` | `tls_server_available() -> int` | Whether the TLS server backend is available (1/0) |
| `tls_server_new` | `tls_server_new(cert: string, key: string) -> TlsServer` | Create a TLS server context (min TLS 1.2) |
| `tls_server_new_tls13` | `tls_server_new_tls13(cert: string, key: string) -> TlsServer` | Create a TLS server that requires TLS 1.3 (rejects older clients) |
| `tls_accept` | `tls_accept(srv: TlsServer, fd: int) -> TlsConn` | Blocking TLS handshake on an accepted TCP fd |
| `tls_accept_start` | `tls_accept_start(srv: TlsServer, fd: int) -> TlsConn` | Nonblocking TLS accept start (handshake may be incomplete) |
| `tls_handshake_step` | `tls_handshake_step(conn: TlsConn) -> int` | Drive handshake: `1` done, `0` want-read, `2` want-write, `-1` error |
| `tls_is_init_finished` | `tls_is_init_finished(conn: TlsConn) -> int` | Handshake complete? |
| `tls_want_read` / `tls_want_write` | `…(conn) -> int` | Event-loop interest flags |
| `tls_conn_fd` | `tls_conn_fd(conn: TlsConn) -> int` | Underlying TCP fd for poll/epoll |
| `tls_read_nb` / `tls_write_nb` | nonblocking TLS I/O | Empty / `0` on want-read/write |

Use `tls_accept_start` + `tls_handshake_step` (or poll on `tls_conn_fd` with want-read/write) so the accept loop is not blocked by slow handshakes. Requires OpenSSL (`tls_server_available()`).
| `tls_read` | `tls_read(conn: TlsConn, max: int) -> string` | Read decrypted bytes (empty on close) |
| `tls_write` | `tls_write(conn: TlsConn, data: string) -> int` | Write plaintext (encrypted on the wire); bytes written or -1 |
| `tls_conn_alpn` | `tls_conn_alpn(conn: TlsConn) -> string` | Negotiated ALPN protocol (e.g. `"h2"`) |
| `tls_conn_close` | `tls_conn_close(conn: TlsConn) -> int` | Close a TLS connection |
| `tls_server_free` | `tls_server_free(srv: TlsServer) -> int` | Free a TLS server context |

### TLS Crypto Primitives

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_aead_seal` | `tls_aead_seal(key: string, nonce: string, plaintext: string, aad: string) -> string` | AEAD encrypt |
| `tls_aead_open` | `tls_aead_open(key: string, nonce: string, ciphertext: string, aad: string) -> string` | AEAD decrypt |
| `tls_record_type` | `tls_record_type(record: string) -> int` | Get TLS record type |
| `tls_record_version` | `tls_record_version(record: string) -> int` | Get TLS record version |
| `tls_record_len` | `tls_record_len(record: string) -> int` | Get TLS record length |
| `tls_record_appdata_seal` | `tls_record_appdata_seal(key: string, iv: string, plaintext: string) -> string` | Seal app data record |
| `tls_record_appdata_open` | `tls_record_appdata_open(key: string, iv: string, ciphertext: string) -> string` | Open app data record |
| `tls_record_seq_reset` | `tls_record_seq_reset() -> int` | Reset sequence numbers |
| `tls_record_write_seq` | `tls_record_write_seq() -> int` | Get write sequence number |
| `tls_record_read_seq` | `tls_record_read_seq() -> int` | Get read sequence number |
| `tls_record_appdata_seal_seq` | `tls_record_appdata_seal_seq(key: string, iv: string, plaintext: string) -> string` | Seal with auto-incrementing sequence |
| `tls_record_appdata_open_seq` | `tls_record_appdata_open_seq(key: string, iv: string, ciphertext: string) -> string` | Open with auto-incrementing sequence |

### TLS 1.3 Handshake Messages

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_client_hello` | `tls_client_hello(random: string) -> string` | Build a ClientHello message |
| `tls_client_hello_legacy_version` | `tls_client_hello_legacy_version(ch: string) -> int` | Parse legacy version from ClientHello |
| `tls_client_hello_random` | `tls_client_hello_random(ch: string) -> string` | Extract random from ClientHello |
| `tls_client_hello_has_aes128_gcm` | `tls_client_hello_has_aes128_gcm(ch: string) -> int` | Check for AES-128-GCM cipher suite |
| `tls_server_hello` | `tls_server_hello(random: string) -> string` | Build a ServerHello message |
| `tls_server_hello_random` | `tls_server_hello_random(sh: string) -> string` | Extract random from ServerHello |
| `tls_certificate` | `tls_certificate(cert: string) -> string` | Build a Certificate message |
| `tls_certificate_der` | `tls_certificate_der(msg: string) -> string` | Extract DER from Certificate message |
| `tls_certificate_verify` | `tls_certificate_verify(scheme: int, sig: string) -> string` | Build a CertificateVerify message |
| `tls_certificate_verify_scheme` | `tls_certificate_verify_scheme(msg: string) -> int` | Get signature scheme |
| `tls_certificate_verify_sig` | `tls_certificate_verify_sig(msg: string) -> string` | Get signature bytes |
| `tls_encrypted_extensions` | `tls_encrypted_extensions() -> string` | Build an EncryptedExtensions message |
| `tls_finished` | `tls_finished(verify_data: string) -> string` | Build a Finished message |
| `tls_hs_msg_type` | `tls_hs_msg_type(msg: string) -> int` | Get handshake message type |
| `tls_finished_verify_data` | `tls_finished_verify_data(secret: string, transcript: string) -> string` | Compute verify data |
| `tls_finished_verify_data_hex` | `tls_finished_verify_data_hex(secret: string, transcript: string) -> string` | Compute verify data as hex |

### TLS Key Derivation

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_derive_secret` | `tls_derive_secret(secret: string, label: string, hash: string) -> string` | Derive a TLS 1.3 secret |
| `tls_derive_secret_hex` | `tls_derive_secret_hex(secret: string, label: string, hash: string) -> string` | Derive secret as hex |
| `tls_client_handshake_traffic_secret` | `tls_client_handshake_traffic_secret(shared: string, hash: string) -> string` | Derive client handshake traffic secret |
| `tls_server_handshake_traffic_secret` | `tls_server_handshake_traffic_secret(shared: string, hash: string) -> string` | Derive server handshake traffic secret |
| `tls_client_handshake_traffic_secret_hex` | `tls_client_handshake_traffic_secret_hex(shared: string, hash: string) -> string` | Derive client handshake secret as hex |
| `tls_server_handshake_traffic_secret_hex` | `tls_server_handshake_traffic_secret_hex(shared: string, hash: string) -> string` | Derive server handshake secret as hex |
| `tls_client_application_traffic_secret` | `tls_client_application_traffic_secret(shared: string, hash: string) -> string` | Derive client app traffic secret |
| `tls_server_application_traffic_secret` | `tls_server_application_traffic_secret(shared: string, hash: string) -> string` | Derive server app traffic secret |
| `tls_client_application_traffic_secret_hex` | `tls_client_application_traffic_secret_hex(shared: string, hash: string) -> string` | Derive client app secret as hex |
| `tls_server_application_traffic_secret_hex` | `tls_server_application_traffic_secret_hex(shared: string, hash: string) -> string` | Derive server app secret as hex |

### TLS Transcript

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_transcript_reset` | `tls_transcript_reset() -> int` | Reset transcript hash |
| `tls_transcript_append` | `tls_transcript_append(msg: string) -> int` | Append a message to transcript |
| `tls_transcript_len` | `tls_transcript_len() -> int` | Get transcript length |
| `tls_transcript_finished_hex` | `tls_transcript_finished_hex(secret: string) -> string` | Compute finished verify data from transcript |

### TLS Handshake Session

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_hs_session_reset` | `tls_hs_session_reset() -> int` | Reset handshake session |
| `tls_hs_session_feed` | `tls_hs_session_feed(data: string) -> int` | Feed data to handshake session |
| `tls_hs_session_client_hello` | `tls_hs_session_client_hello(random: string) -> int` | Process ClientHello |
| `tls_hs_session_server_hello` | `tls_hs_session_server_hello(random: string) -> int` | Process ServerHello |
| `tls_hs_session_finished_hex` | `tls_hs_session_finished_hex(secret: string) -> string` | Compute finished data for session |
| `tls_hs_session_encrypted_extensions` | `tls_hs_session_encrypted_extensions() -> int` | Process EncryptedExtensions |
| `tls_hs_session_certificate` | `tls_hs_session_certificate(cert: string) -> int` | Process Certificate |
| `tls_hs_session_certificate_verify` | `tls_hs_session_certificate_verify(scheme: int, sig: string) -> int` | Process CertificateVerify |
| `tls_hs_session_finished` | `tls_hs_session_finished(verify: string, secret: string) -> int` | Process Finished |

---

## 38. HTTP/2

### Frame Construction

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_detect` | `http2_detect(data: string) -> bool` | Detect HTTP/2 preface |
| `http2_headers_frame` | `http2_headers_frame(stream: int, block: string, flags: int) -> string` | Build a HEADERS frame |
| `http2_data_frame` | `http2_data_frame(stream: int, payload: string, flags: int) -> string` | Build a DATA frame |
| `http2_continuation_frame` | `http2_continuation_frame(stream: int, block: string, flags: int) -> string` | Build a CONTINUATION frame |
| `http2_goaway_frame` | `http2_goaway_frame(last_stream: int, error_code: int) -> string` | Build a GOAWAY frame |
| `http2_ping_frame` | `http2_ping_frame(data: string, ack: int) -> string` | Build a PING frame |
| `http2_window_update_frame` | `http2_window_update_frame(stream: int, increment: int) -> string` | Build a WINDOW_UPDATE frame |
| `http2_rst_stream_frame` | `http2_rst_stream_frame(stream: int, error_code: int) -> string` | Build a RST_STREAM frame |
| `http2_priority_frame` | `http2_priority_frame(stream: int, dep: int, weight: int, exclusive: int) -> string` | Build a PRIORITY frame |
| `http2_push_promise_frame` | `http2_push_promise_frame(stream: int, promised: int, block: string, flags: int) -> string` | Build a PUSH_PROMISE frame |

### Frame Parsing

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_frame_type` | `http2_frame_type(frame: string) -> int` | Get frame type |
| `http2_frame_stream` | `http2_frame_stream(frame: string) -> int` | Get frame stream ID |
| `http2_frame_len` | `http2_frame_len(frame: string) -> int` | Get frame payload length |
| `http2_frame_flags` | `http2_frame_flags(frame: string) -> int` | Get frame flags |
| `http2_frame_payload` | `http2_frame_payload(frame: string) -> string` | Extract frame payload |
| `http2_frame_at` | `http2_frame_at(data: string, idx: int) -> string` | Get the i-th frame from a buffer |
| `http2_concat_frames` | `http2_concat_frames(a: string, b: string) -> string` | Concatenate two frames |
| `http2_header_block` | `http2_header_block(frame: string, flags: int) -> string` | Extract header block from frame |
| `http2_is_goaway` | `http2_is_goaway(frame: string) -> bool` | Check if frame is GOAWAY |
| `http2_is_ping` | `http2_is_ping(frame: string) -> bool` | Check if frame is PING |
| `http2_is_window_update` | `http2_is_window_update(frame: string) -> bool` | Check if frame is WINDOW_UPDATE |
| `http2_is_rst_stream` | `http2_is_rst_stream(frame: string) -> bool` | Check if frame is RST_STREAM |
| `http2_is_priority` | `http2_is_priority(frame: string) -> bool` | Check if frame is PRIORITY |
| `http2_is_push_promise` | `http2_is_push_promise(frame: string) -> bool` | Check if frame is PUSH_PROMISE |
| `http2_is_settings_ack` | `http2_is_settings_ack(frame: string) -> bool` | Check if frame is a SETTINGS ACK |
| `http2_goaway_last_stream` | `http2_goaway_last_stream(frame: string) -> int` | Get last stream ID from GOAWAY |
| `http2_goaway_error` | `http2_goaway_error(frame: string) -> int` | Get error code from GOAWAY |
| `http2_window_update_increment` | `http2_window_update_increment(frame: string) -> int` | Get increment from WINDOW_UPDATE |
| `http2_rst_stream_error` | `http2_rst_stream_error(frame: string) -> int` | Get error code from RST_STREAM |
| `http2_push_promise_stream` | `http2_push_promise_stream(frame: string) -> int` | Get promised stream from PUSH_PROMISE |

### Settings & Connection

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_settings_len` | `http2_settings_len(frame: string) -> int` | Get length of settings frame |
| `http2_empty_settings` | `http2_empty_settings() -> string` | Build an empty SETTINGS frame |
| `http2_settings_max_concurrent` | `http2_settings_max_concurrent(max: int) -> string` | Build SETTINGS with max concurrent streams |
| `http2_settings_ack` | `http2_settings_ack() -> string` | Build a SETTINGS ACK frame |
| `http2_client_preface` | `http2_client_preface() -> string` | Get the HTTP/2 client connection preface |
| `http2_server_preface` | `http2_server_preface() -> string` | Get the HTTP/2 server connection preface |

### Connection State Machine

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_conn_reset` | `http2_conn_reset() -> int` | Reset connection state |
| `http2_conn_recv` | `http2_conn_recv(data: string) -> int` | Process received data |
| `http2_conn_send_settings` | `http2_conn_send_settings() -> int` | Send SETTINGS frame |
| `http2_conn_send_settings_ack` | `http2_conn_send_settings_ack() -> int` | Send SETTINGS ACK |
| `http2_conn_settings_ack_needed` | `http2_conn_settings_ack_needed() -> int` | Check if SETTINGS ACK is needed |
| `http2_conn_auto_settings_ack` | `http2_conn_auto_settings_ack() -> string` | Auto-generate SETTINGS ACK if needed |
| `http2_conn_pump` | `http2_conn_pump(data: string) -> string` | Process data and generate response |
| `http2_conn_goaway_last` | `http2_conn_goaway_last() -> int` | Get last stream from received GOAWAY |
| `http2_conn_max_concurrent` | `http2_conn_max_concurrent() -> int` | Get max concurrent streams |
| `http2_conn_active_streams` | `http2_conn_active_streams() -> int` | Get count of active streams |
| `http2_conn_set_server` | `http2_conn_set_server(is_server: int) -> int` | Set connection role |
| `http2_conn_is_server` | `http2_conn_is_server() -> int` | Check if connection is server |
| `http2_conn_preface_received` | `http2_conn_preface_received() -> int` | Check if preface was received |
| `http2_conn_settings_exchanged` | `http2_conn_settings_exchanged() -> int` | Check if settings exchange is complete |
| `http2_conn_closing` | `http2_conn_closing() -> int` | Check if connection is closing |
| `http2_conn_header_block` | `http2_conn_header_block(stream: int) -> string` | Get header block for a stream |
| `http2_conn_header_stream` | `http2_conn_header_stream() -> int` | Get the stream with pending headers |
| `http2_conn_header_assembling` | `http2_conn_header_assembling() -> int` | Check if headers are being assembled |
| `http2_conn_send_goaway` | `http2_conn_send_goaway() -> int` | Send GOAWAY frame |

### Flow Control

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_window_of` | `http2_window_of(stream: int) -> int` | Get window size for a stream |
| `http2_window_conn` | `http2_window_conn() -> int` | Get connection-level window size |
| `http2_window_blocked` | `http2_window_blocked(stream: int) -> int` | Check if stream is flow-control blocked |
| `http2_window_consume` | `http2_window_consume(stream: int, amount: int) -> int` | Consume window for a stream |
| `http2_window_increment` | `http2_window_increment(stream: int, amount: int) -> int` | Increment window for a stream |

### Stream State

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_stream_reset` | `http2_stream_reset() -> int` | Reset stream state |
| `http2_stream_id` | `http2_stream_id() -> int` | Get current stream ID |
| `http2_stream_state` | `http2_stream_state() -> int` | Get current stream state |
| `http2_stream_state_of` | `http2_stream_state_of(stream: int) -> int` | Get state of a specific stream |
| `http2_stream_apply` | `http2_stream_apply(frame: string) -> int` | Apply a frame to stream state |
| `http2_stream_apply_local` | `http2_stream_apply_local(frame: string) -> int` | Apply a locally-sent frame |
| `http2_stream_half_closed_remote` | `http2_stream_half_closed_remote(stream: int) -> int` | Check if remote half is closed |
| `http2_stream_half_closed_local` | `http2_stream_half_closed_local(stream: int) -> int` | Check if local half is closed |
| `http2_ready_streams` | `http2_ready_streams() -> int` | Count streams with complete HEADERS not yet taken |
| `http2_next_ready_stream` | `http2_next_ready_stream() -> int` | Next untaken ready stream id, or `-1` |
| `http2_stream_take` | `http2_stream_take(stream: int) -> int` | Mark stream taken by a worker (`1` if found) |
| `http2_stream_body` | `http2_stream_body(stream: int) -> string` | Accumulated DATA body for stream |
| `http2_stream_body_len` | `http2_stream_body_len(stream: int) -> int` | Body byte count (`-1` if unknown stream) |
| `http2_stream_body_done` | `http2_stream_body_done(stream: int) -> int` | `1` if END_STREAM seen on DATA |

Up to **32 concurrent stream slots** per connection; completed HEADERS push into a
ready queue so workers can multiplex without one-request-at-a-time stalls.

### Priority

| Function | Signature | Description |
|----------|-----------|-------------|
| `http2_priority_dep` | `http2_priority_dep(frame: string) -> int` | Get dependency stream from PRIORITY |
| `http2_priority_weight` | `http2_priority_weight(frame: string) -> int` | Get weight from PRIORITY |
| `http2_priority_exclusive` | `http2_priority_exclusive(frame: string) -> int` | Get exclusive flag from PRIORITY |
| `http2_priority_apply` | `http2_priority_apply(frame: string) -> int` | Apply priority to the tree |
| `http2_stream_priority_dep` | `http2_stream_priority_dep(stream: int) -> int` | Get dependency of a stream |
| `http2_stream_priority_weight` | `http2_stream_priority_weight(stream: int) -> int` | Get weight of a stream |
| `http2_stream_priority_exclusive` | `http2_stream_priority_exclusive(stream: int) -> int` | Get exclusive flag of a stream |
| `http2_stream_priority_child_count` | `http2_stream_priority_child_count(stream: int) -> int` | Get number of child streams |
| `http2_schedule_next` | `http2_schedule_next() -> int` | Schedule next stream for sending |

### TLS + HTTP/2

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_serve_once_h2` | `tls_serve_once_h2(port: int, cert: string, key: string, response: string) -> int` | Serve one TLS+HTTP/2 request |
| `tls_h2_settings_exchange` | `tls_h2_settings_exchange(host: string, port: int, ca: string) -> string` | Perform H2 settings exchange over TLS |
| `tls_h2_get` | `tls_h2_get(host: string, port: int, ca: string, path: string) -> string` | HTTP/2 GET over TLS |
| `tls_h2_post` | `tls_h2_post(host: string, port: int, ca: string, path: string, body: string) -> string` | HTTP/2 POST over TLS |
| `tls_h2_get_twice` | `tls_h2_get_twice(host: string, port: int, ca: string, path: string) -> string` | Two multiplexed HTTP/2 GETs over TLS |
| `tls_h2_mux` | `tls_h2_mux(host: string, port: int, ca: string) -> string` | HTTP/2 multiplexing test over TLS |
| `tls_serve_h2_wu` | `tls_serve_h2_wu(port: int, cert: string, key: string, response: string, window: int) -> int` | Serve H2 with window update |
| `tls_h2_window_get` | `tls_h2_window_get(host: string, port: int, ca: string, path: string) -> string` | H2 GET with flow control |
| `tls_serve_h2_n` | `tls_serve_h2_n(port: int, cert: string, key: string, response: string, n: int) -> int` | Serve n HTTP/2 requests over TLS |
| `tls_serve_h2_routes` | `tls_serve_h2_routes(port: int, cert: string, key: string, routes: string, responses: string, n: int) -> int` | Serve HTTP/2 with multiple routes |

---

## 39. HPACK (HTTP/2 Header Compression)

| Function | Signature | Description |
|----------|-----------|-------------|
| `hpack_encode_indexed` | `hpack_encode_indexed(idx: int) -> string` | Encode an indexed header |
| `hpack_decode_indexed` | `hpack_decode_indexed(data: string) -> int` | Decode an indexed header |
| `hpack_static_name` | `hpack_static_name(idx: int) -> string` | Get name from static table |
| `hpack_static_value` | `hpack_static_value(idx: int) -> string` | Get value from static table |
| `hpack_encode_literal` | `hpack_encode_literal(name: string, value: string) -> string` | Encode a literal header |
| `hpack_literal_name` | `hpack_literal_name(data: string) -> string` | Decode literal header name |
| `hpack_literal_value` | `hpack_literal_value(data: string) -> string` | Decode literal header value |
| `hpack_decode_stub` | `hpack_decode_stub(data: string) -> int` | Stub HPACK decoder |
| `hpack_decode_block` | `hpack_decode_block(block: string) -> int` | Decode a complete header block |
| `hpack_decoded_count` | `hpack_decoded_count() -> int` | Number of decoded headers |
| `hpack_decoded_name` | `hpack_decoded_name(idx: int) -> string` | Get decoded header name |
| `hpack_decoded_value` | `hpack_decoded_value(idx: int) -> string` | Get decoded header value |
| `hpack_decode_clear` | `hpack_decode_clear() -> int` | Clear decoded headers |
| `hpack_dyn_insert` | `hpack_dyn_insert(name: string, value: string) -> int` | Insert into dynamic table |
| `hpack_dyn_name` | `hpack_dyn_name() -> string` | Get last inserted dynamic name |
| `hpack_dyn_value` | `hpack_dyn_value() -> string` | Get last inserted dynamic value |
| `hpack_dyn_clear` | `hpack_dyn_clear() -> int` | Clear dynamic table |
| `hpack_dyn_len` | `hpack_dyn_len() -> int` | Get dynamic table size |
| `hpack_dyn_name_at` | `hpack_dyn_name_at(idx: int) -> string` | Get name at dynamic table index |
| `hpack_dyn_value_at` | `hpack_dyn_value_at(idx: int) -> string` | Get value at dynamic table index |
| `hpack_huffman_encode` | `hpack_huffman_encode(data: string) -> string` | Huffman-encode a string |
| `hpack_huffman_decode` | `hpack_huffman_decode(data: string) -> string` | Huffman-decode a string |

---

## 40. QUIC

### Packet Parsing

| Function | Signature | Description |
|----------|-----------|-------------|
| `quic_detect` | `quic_detect(data: string) -> bool` | Detect a QUIC packet |
| `quic_long_header` | `quic_long_header(data: string) -> bool` | Check for long header |
| `quic_short_header` | `quic_short_header(data: string) -> bool` | Check for short header |
| `quic_version` | `quic_version(pkt: string) -> int` | Get QUIC version |
| `quic_dcid_len` | `quic_dcid_len(pkt: string) -> int` | Get destination connection ID length |
| `quic_dcid` | `quic_dcid(pkt: string) -> string` | Get destination connection ID |
| `quic_scid_len` | `quic_scid_len(pkt: string) -> int` | Get source connection ID length |
| `quic_scid` | `quic_scid(pkt: string) -> string` | Get source connection ID |
| `quic_payload_offset` | `quic_payload_offset(pkt: string) -> int` | Get offset of payload data |
| `quic_spin_bit` | `quic_spin_bit(pkt: string) -> int` | Get the spin bit |
| `quic_key_phase` | `quic_key_phase(pkt: string) -> int` | Get the key phase bit |
| `quic_long_type` | `quic_long_type(pkt: string) -> int` | Get long header packet type |
| `quic_is_retry` | `quic_is_retry(pkt: string) -> bool` | Check if packet is a Retry |
| `quic_is_version_negotiation` | `quic_is_version_negotiation(pkt: string) -> bool` | Check if packet is Version Negotiation |
| `quic_vn_version_count` | `quic_vn_version_count(pkt: string) -> int` | Count offered versions |
| `quic_vn_version_at` | `quic_vn_version_at(pkt: string, idx: int) -> int` | Get a specific offered version |
| `quic_vn_select` | `quic_vn_select(pkt: string, preferred: int) -> int` | Select a preferred version |
| `quic_stub` | `quic_stub(port: int) -> int` | Stub QUIC endpoint |

### QUIC Crypto Frames

| Function | Signature | Description |
|----------|-----------|-------------|
| `quic_has_crypto` | `quic_has_crypto(pkt: string) -> bool` | Check if packet has CRYPTO frame |
| `quic_crypto_offset` | `quic_crypto_offset(pkt: string) -> int` | Get CRYPTO frame offset |
| `quic_crypto_data_offset` | `quic_crypto_data_offset(pkt: string) -> int` | Get CRYPTO data offset |
| `quic_crypto_data_len` | `quic_crypto_data_len(pkt: string) -> int` | Get CRYPTO data length |
| `quic_crypto_data` | `quic_crypto_data(pkt: string) -> string` | Extract CRYPTO data |
| `quic_crypto_frame` | `quic_crypto_frame(data: string, offset: int) -> string` | Build a CRYPTO frame |
| `quic_crypto_payload` | `quic_crypto_payload(data: string, offset: int, length: int) -> string` | Extract CRYPTO payload |
| `quic_payload_crypto_data` | `quic_payload_crypto_data(payload: string) -> string` | Extract CRYPTO data from payload |
| `quic_payload_crypto_data_len` | `quic_payload_crypto_data_len(payload: string) -> int` | Get CRYPTO data length from payload |

### QUIC ACK & Stream Frames

| Function | Signature | Description |
|----------|-----------|-------------|
| `quic_ack_frame` | `quic_ack_frame(largest: int, delay: int, first_range: int) -> string` | Build an ACK frame |
| `quic_ack_largest` | `quic_ack_largest(frame: string) -> int` | Get largest acknowledged packet |
| `quic_ack_delay` | `quic_ack_delay(frame: string) -> int` | Get ACK delay |
| `quic_ack_range_count` | `quic_ack_range_count(frame: string) -> int` | Get ACK range count |
| `quic_ack_first_range` | `quic_ack_first_range(frame: string) -> int` | Get first ACK range |
| `quic_ack_smallest` | `quic_ack_smallest(frame: string) -> int` | Get smallest acknowledged packet |
| `quic_is_ack` | `quic_is_ack(frame: string) -> int` | Check if frame is ACK |
| `quic_stream_frame` | `quic_stream_frame(id: int, offset: int, data: string, fin: int) -> string` | Build a STREAM frame |
| `quic_is_stream` | `quic_is_stream(frame: string) -> int` | Check if frame is STREAM |
| `quic_stream_fin` | `quic_stream_fin(frame: string) -> int` | Check FIN flag |
| `quic_stream_id_of` | `quic_stream_id_of(frame: string) -> int` | Get stream ID |
| `quic_stream_offset` | `quic_stream_offset(frame: string) -> int` | Get stream offset |
| `quic_stream_data_len` | `quic_stream_data_len(frame: string) -> int` | Get stream data length |
| `quic_stream_data` | `quic_stream_data(frame: string) -> string` | Get stream data |

### QUIC Initial Protection

| Function | Signature | Description |
|----------|-----------|-------------|
| `quic_hkdf_expand_label` | `quic_hkdf_expand_label(secret: string, label: string, length: int) -> string` | HKDF-Expand-Label |
| `quic_hkdf_expand_label_hex` | `quic_hkdf_expand_label_hex(secret: string, label: string, length: int) -> string` | HKDF-Expand-Label as hex |
| `quic_initial_client_secret` | `quic_initial_client_secret(dcid: string) -> string` | Derive client initial secret |
| `quic_initial_client_secret_hex` | `quic_initial_client_secret_hex(dcid: string) -> string` | Derive client initial secret as hex |
| `quic_initial_client_key` | `quic_initial_client_key(dcid: string) -> string` | Derive client initial key |
| `quic_initial_client_iv` | `quic_initial_client_iv(dcid: string) -> string` | Derive client initial IV |
| `quic_initial_client_key_hex` | `quic_initial_client_key_hex(dcid: string) -> string` | Derive client initial key as hex |
| `quic_initial_client_iv_hex` | `quic_initial_client_iv_hex(dcid: string) -> string` | Derive client initial IV as hex |
| `quic_initial_client_hp` | `quic_initial_client_hp(dcid: string) -> string` | Derive client header protection key |
| `quic_initial_client_hp_hex` | `quic_initial_client_hp_hex(dcid: string) -> string` | Derive client HP key as hex |
| `quic_initial_protect` | `quic_initial_protect(pkt: string, pn_len: int, key: string, iv: string) -> string` | Apply initial protection |
| `quic_initial_unprotect` | `quic_initial_unprotect(pkt: string, pn_len: int, key: string, iv: string) -> string` | Remove initial protection |
| `quic_header_protection_mask` | `quic_header_protection_mask(hp_key: string, sample: string) -> string` | Compute HP mask |
| `quic_header_protection_mask_hex` | `quic_header_protection_mask_hex(hp_key: string, sample: string) -> string` | Compute HP mask as hex |
| `quic_initial_hp_mask` | `quic_initial_hp_mask(hp_key: string, sample: string) -> string` | Compute initial HP mask |
| `quic_initial_hp_mask_hex` | `quic_initial_hp_mask_hex(hp_key: string, sample: string) -> string` | Compute initial HP mask as hex |
| `quic_header_protect_apply` | `quic_header_protect_apply(header: string, pn_offset: int, mask: string) -> string` | Apply header protection |
| `quic_header_protect_remove` | `quic_header_protect_remove(header: string, pn_offset: int, mask: string) -> string` | Remove header protection |
| `quic_initial_packet_protect` | `quic_initial_packet_protect(pkt: string, pn_offset: int, key: string, pn: int, hp_key: string) -> string` | Full packet protection |
| `quic_initial_packet_unprotect` | `quic_initial_packet_unprotect(pkt: string, key: string, pn_offset: int, pn_len: int) -> string` | Full packet unprotection |

### QUIC Libraries

| Function | Signature | Description |
|----------|-----------|-------------|
| `quiche_available` | `quiche_available() -> int` | Check if quiche is available |
| `quiche_version` | `quiche_version() -> string` | Get quiche version |
| `quiche_handshake` | `quiche_handshake(host: string, port: int, ca: string, timeout: int) -> string` | Perform QUIC handshake via quiche |
| `quiche_h3_get` | `quiche_h3_get(host: string, port: int, ca: string, path: string, timeout: int) -> string` | HTTP/3 GET via quiche |
| `quiche_h3_post` | `quiche_h3_post(host: string, port: int, ca: string, path: string, body: string, timeout: int) -> string` | HTTP/3 POST via quiche |
| `quiche_h3_get_two` | `quiche_h3_get_two(host: string, port: int, ca: string, path1: string, path2: string, timeout: int) -> string` | Two multiplexed HTTP/3 GETs |
| `quiche_start_server` | `quiche_start_server(port: int, cert: string, key: string, ca: string, response: string) -> int` | Start a QUIC server |
| `quiche_stop_server` | `quiche_stop_server(handle: int) -> int` | Stop a QUIC server |
| `h3_server_available` | `h3_server_available() -> int` | H3/UDP surface available (`1` when quiche linked) |
| `h3_server_new` | `h3_server_new(cert: string, key: string) -> int` | Create H3 server handle (cert/key paths) |
| `h3_server_bind` | `h3_server_bind(handle, host, port) -> int` | Bind UDP for QUIC |
| `h3_server_fd` | `h3_server_fd(handle) -> int` | UDP fd for event-loop registration |
| `h3_server_poll` | `h3_server_poll(handle, timeout_ms) -> int` | `1` readable, `0` timeout, `-1` error |
| `h3_accept_stream` | `h3_accept_stream(handle) -> int` | Next stream id marker (or `-1`) |
| `h3_stream_read` / `h3_stream_write` | stream I/O | Read last datagram / write (surface; crypto depth via quiche) |
| `h3_server_close` | `h3_server_close(handle) -> int` | Close server and UDP fd |
| `nghttp2_available` | `nghttp2_available() -> int` | Check if nghttp2 is available |
| `nghttp2_get` | `nghttp2_get(host: string, port: int, ca: string, path: string) -> string` | HTTP/2 GET via nghttp2 |
| `nghttp2_post` | `nghttp2_post(host: string, port: int, ca: string, path: string, body: string) -> string` | HTTP/2 POST via nghttp2 |
| `nghttp2_get_two` | `nghttp2_get_two(host: string, port: int, ca: string, path1: string, path2: string) -> string` | Two multiplexed HTTP/2 GETs via nghttp2 |

---

## 41. Protobuf

| Function | Signature | Description |
|----------|-----------|-------------|
| `pb_encode_varint` | `pb_encode_varint(val: int) -> string` | Encode a varint |
| `pb_decode_varint` | `pb_decode_varint(data: string) -> int` | Decode a varint |
| `pb_varint_len` | `pb_varint_len(data: string) -> int` | Get encoded varint length |
| `pb_encode_key` | `pb_encode_key(field: int, wire_type: int) -> string` | Encode a field key |
| `pb_key_field` | `pb_key_field(data: string) -> int` | Get field number from key |
| `pb_key_wire` | `pb_key_wire(data: string) -> int` | Get wire type from key |
| `pb_zigzag_encode` | `pb_zigzag_encode(val: int) -> int` | ZigZag encode a signed integer |
| `pb_zigzag_decode` | `pb_zigzag_decode(val: int) -> int` | ZigZag decode to signed integer |
| `pb_encode_sint` | `pb_encode_sint(val: int) -> string` | Encode a signed integer |
| `pb_decode_sint` | `pb_decode_sint(data: string) -> int` | Decode a signed integer |
| `pb_encode_bytes` | `pb_encode_bytes(data: string) -> string` | Encode length-delimited bytes |
| `pb_bytes_len` | `pb_bytes_len(data: string) -> int` | Get length of encoded bytes |
| `pb_encode_field_varint` | `pb_encode_field_varint(field: int, val: int) -> string` | Encode a complete varint field |
| `pb_encode_simple` | `pb_encode_simple(name: string, id: int) -> string` | Encode a simple message |
| `pb_simple_name` | `pb_simple_name(data: string) -> string` | Get name from simple message |
| `pb_simple_id` | `pb_simple_id(data: string) -> int` | Get ID from simple message |
| `pb_encode_nested` | `pb_encode_nested(name: string, id: int, inner: string, inner_id: int) -> string` | Encode a nested message |
| `pb_nested_inner` | `pb_nested_inner(data: string) -> string` | Extract inner message |
| `pb_encode_repeated_varint` | `pb_encode_repeated_varint(field: int, val1: int, val2: int) -> string` | Encode repeated varint field |
| `pb_repeated_count` | `pb_repeated_count(data: string, field: int) -> int` | Count repeated field occurrences |
| `pb_repeated_at` | `pb_repeated_at(data: string, field: int, idx: int) -> int` | Get repeated field value at index |

---

## 42. WebSocket

| Function | Signature | Description |
|----------|-----------|-------------|
| `ws_accept_key` | `ws_accept_key(client_key: string) -> string` | Compute WebSocket accept key |
| `ws_upgrade_request_ok` | `ws_upgrade_request_ok(request: string) -> int` | Validate a WebSocket upgrade request |
| `ws_client_request` | `ws_client_request(host: string, path: string, key: string) -> string` | Build a WebSocket upgrade request |
| `ws_client_accept_ok` | `ws_client_accept_ok(response: string, key: string) -> int` | Validate upgrade response |
| `ws_accept` | `ws_accept(conn: int) -> int` | Accept a WebSocket connection |
| `ws_recv` | `ws_recv(conn: int, max_bytes: int) -> string` | Receive a WebSocket message |
| `ws_last_opcode` | `ws_last_opcode() -> int` | Get the opcode of the last received frame |
| `ws_send_text` | `ws_send_text(conn: int, data: string) -> int` | Send a text frame |
| `ws_send_binary` | `ws_send_binary(conn: int, data: string) -> int` | Send a binary frame |
| `ws_send_ping` | `ws_send_ping(conn: int, data: string) -> int` | Send a ping frame |
| `ws_send_close` | `ws_send_close(conn: int, code: int, reason: string) -> int` | Send a close frame |
| `ws_close` | `ws_close(conn: int) -> int` | Close a WebSocket connection |
| `ws_echo_stub` | `ws_echo_stub(conn: int) -> int` | WebSocket echo stub |
| `ws_echo_once` | `ws_echo_once(conn: int) -> int` | Echo one WebSocket message |
| `ws_echo` | `ws_echo(conn: int) -> int` | Echo WebSocket messages in a loop |
| `ws_client_connect` | `ws_client_connect(host: string, port: int, path: string, key: string) -> int` | Connect to a WebSocket server |
| `ws_client_send_text` | `ws_client_send_text(conn: int, data: string) -> int` | Client: send text frame |
| `ws_client_send_binary` | `ws_client_send_binary(conn: int, data: string) -> int` | Client: send binary frame |
| `ws_client_send_ping` | `ws_client_send_ping(conn: int, data: string) -> int` | Client: send ping frame |

---

## 43. gRPC

| Function | Signature | Description |
|----------|-----------|-------------|
| `grpc_encode_message` | `grpc_encode_message(payload: string) -> string` | Encode a gRPC message with length prefix |
| `grpc_message_len` | `grpc_message_len(msg: string) -> int` | Get gRPC message length |
| `grpc_message_within_limit` | `grpc_message_within_limit(msg: string, limit: int) -> int` | Check if message is within size limit |
| `grpc_default_max_message` | `grpc_default_max_message() -> int` | Get default max message size |
| `grpc_message_payload` | `grpc_message_payload(msg: string) -> string` | Extract gRPC message payload |
| `grpc_unary_request` | `grpc_unary_request(method: string, id: int) -> string` | Build a unary gRPC request |
| `grpc_unary_name` | `grpc_unary_name(req: string) -> string` | Get method name from unary request |
| `grpc_unary_id` | `grpc_unary_id(req: string) -> int` | Get ID from unary request |
| `grpc_content_type` | `grpc_content_type() -> string` | Get the gRPC content type string |
| `grpc_status_trailer` | `grpc_status_trailer(code: int) -> string` | Build a gRPC status trailer |
| `grpc_status_code` | `grpc_status_code(trailer: string) -> int` | Parse gRPC status code |
| `grpc_http2_unary` | `grpc_http2_unary(stream: int, method: string, id: int) -> string` | Build gRPC unary over HTTP/2 |
| `grpc_http2_unary_payload` | `grpc_http2_unary_payload(frames: string) -> string` | Extract payload from gRPC/H2 frames |
| `grpc_http2_unary_response` | `grpc_http2_unary_response(stream: int, payload: string, status: int) -> string` | Build gRPC unary response over HTTP/2 |
| `grpc_http2_unary_response_status` | `grpc_http2_unary_response_status(stream: int, payload: string, status: int, grpc_status: int) -> string` | Build gRPC response with status |
| `grpc_http2_response_payload` | `grpc_http2_response_payload(frames: string) -> string` | Extract payload from gRPC/H2 response |
| `grpc_http2_response_status` | `grpc_http2_response_status(frames: string) -> int` | Get gRPC status from response |
| `grpc_http2_stream_data` | `grpc_http2_stream_data(stream: int, payload: string, id: int, fin: int) -> string` | Build gRPC stream data |
| `grpc_http2_stream_two` | `grpc_http2_stream_two(stream: int, payload1: string, id1: int, payload2: string, id2: int) -> string` | Build two gRPC stream messages |
| `grpc_http2_stream_data_count` | `grpc_http2_stream_data_count(frames: string, stream: int) -> int` | Count gRPC stream data frames |
| `grpc_http2_client_stream_flow` | `grpc_http2_client_stream_flow(stream: int, p1: string, id1: int, p2: string, id2: int, p3: string, id3: int, window: int) -> string` | Build gRPC client stream with flow control |
| `grpc_stub_ping` | `grpc_stub_ping() -> int` | gRPC stub ping |

### TLS + gRPC

| Function | Signature | Description |
|----------|-----------|-------------|
| `tls_serve_grpc_once` | `tls_serve_grpc_once(port: int, cert: string, key: string) -> int` | Serve one gRPC request over TLS |
| `tls_grpc_unary` | `tls_grpc_unary(host: string, port: int, ca: string, method: string, id: int, payload: string) -> string` | gRPC unary call over TLS |
| `tls_serve_grpc_stream` | `tls_serve_grpc_stream(port: int, cert: string, key: string) -> int` | Serve gRPC stream over TLS |
| `tls_grpc_stream` | `tls_grpc_stream(host: string, port: int, ca: string, method: string, id: int, p1: string, count: int, p2: string) -> string` | gRPC streaming call over TLS |

---

## 44. SMTP

| Function | Signature | Description |
|----------|-----------|-------------|
| `smtp_format_message` | `smtp_format_message(from: string, to: string, subject: string, body: string) -> string` | Format an email message |
| `smtp_send_soft` | `smtp_send_soft(host: string, port: int, message: string) -> int` | Send email without auth |
| `smtp_send_dialog` | `smtp_send_dialog(host: string, port: int, from: string, to: string, message: string) -> int` | Send email with SMTP dialog |
| `smtp_auth_plain` | `smtp_auth_plain(user: string, pass: string) -> string` | Create PLAIN auth string |
| `smtp_send_auth` | `smtp_send_auth(host: string, port: int, from: string, to: string, message: string, user: string, pass: string) -> int` | Send email with authentication |
| `smtp_starttls_available` | `smtp_starttls_available() -> int` | Check if STARTTLS is available |
| `smtp_send_starttls` | `smtp_send_starttls(host: string, port: int, from: string, to: string, message: string, user: string, pass: string) -> int` | Send email with STARTTLS |
| `mail_parse_address` | `mail_parse_address(addr: string) -> string` | Parse an email address |
| `mail_header_get` | `mail_header_get(headers: string, name: string) -> string` | Get a mail header value |
| `mail_address_ok` | `mail_address_ok(addr: string) -> int` | Validate an email address |

---

## 45. Logging

| Function | Signature | Description |
|----------|-----------|-------------|
| `log_debug` | `log_debug(msg: string) -> void` | Log a debug message |
| `log_info` | `log_info(msg: string) -> void` | Log an info message |
| `log_warn` | `log_warn(msg: string) -> void` | Log a warning message |
| `log_error` | `log_error(msg: string) -> void` | Log an error message |
| `log_kv` | `log_kv(level: string, key: string, value: string) -> void` | Log a key-value message |
| `slog_set_level` | `slog_set_level(level: string) -> void` | Set structured log level |
| `slog_debug` | `slog_debug(msg: string) -> void` | Structured debug log |
| `slog_info` | `slog_info(msg: string) -> void` | Structured info log |
| `slog_warn` | `slog_warn(msg: string) -> void` | Structured warning log |
| `slog_error` | `slog_error(msg: string) -> void` | Structured error log |
| `slog_with` | `slog_with(level: string, msg: string, key: string, value: string) -> void` | Structured log with key-value |
| `slog_redact` | `slog_redact(value: string) -> string` | Redact sensitive data for logging |
| `slog_with_redacted` | `slog_with_redacted(level: string, key: string, value: string) -> void` | Log with auto-redacted value |

---

## 46. Metrics

| Function | Signature | Description |
|----------|-----------|-------------|
| `metric_inc` | `metric_inc(id: int) -> void` | Increment a counter metric |
| `metric_add` | `metric_add(id: int, val: int) -> void` | Add to a counter metric |
| `metric_get` | `metric_get(id: int) -> int` | Get a counter metric value |
| `gauge_set` | `gauge_set(id: int, val: int) -> void` | Set a gauge value |
| `gauge_add` | `gauge_add(id: int, val: int) -> void` | Add to a gauge |
| `gauge_get` | `gauge_get(id: int) -> int` | Get a gauge value |
| `hist_observe` | `hist_observe(id: int, val: int) -> void` | Record a histogram observation |
| `hist_count` | `hist_count(id: int) -> int` | Get histogram observation count |
| `hist_sum` | `hist_sum(id: int) -> int` | Get histogram sum |
| `hist_avg` | `hist_avg(id: int) -> int` | Get histogram average |
| `metrics_export` | `metrics_export() -> string` | Export all metrics as text |

---

## 47. Validation

| Function | Signature | Description |
|----------|-----------|-------------|
| `validate_required` | `validate_required(val: string) -> int` | Check that a value is non-empty |
| `validate_min_len` | `validate_min_len(val: string, min: int) -> int` | Check minimum string length |
| `validate_max_len` | `validate_max_len(val: string, max: int) -> int` | Check maximum string length |
| `validate_int_range` | `validate_int_range(val: int, min: int, max: int) -> int` | Check integer is in range |
| `validate_email` | `validate_email(val: string) -> int` | Validate email format |

---

## 48. Caching & Scheduling

| Function | Signature | Description |
|----------|-----------|-------------|
| `cache_put` | `cache_put(key: string, val: string, ttl_ms: int) -> int` | Store a value in cache with TTL |
| `cache_get` | `cache_get(key: string) -> string` | Get a cached value |
| `cache_has` | `cache_has(key: string) -> int` | Check if a cache key exists |
| `job_schedule` | `job_schedule(name: string, delay_ms: int) -> int` | Schedule a job |
| `job_due` | `job_due(name: string) -> int` | Check if a job is due |
| `job_delay_ms` | `job_delay_ms(name: string) -> int` | Get remaining delay for a job |
| `job_cancel` | `job_cancel(name: string) -> int` | Cancel a scheduled job |
| `conn_pool_slot` | `conn_pool_slot(key: string, max: int) -> int` | Get a connection pool slot |
| `conn_pool_next` | `conn_pool_next(pool_size: int) -> int` | Get next available pool slot |
| `lb_pick2` | `lb_pick2(key: string, a: string, b: string) -> string` | Pick from two load-balance targets |
| `lb_pick3` | `lb_pick3(key: string, a: string, b: string, c: string) -> string` | Pick from three load-balance targets |

---

## 49. Data Structures

### Ring Buffer

| Function | Signature | Description |
|----------|-----------|-------------|
| `ring_new` | `ring_new(capacity: int) -> int` | Create a ring buffer |
| `ring_push` | `ring_push(ring: int, val: int) -> int` | Push a value |
| `ring_pop` | `ring_pop(ring: int) -> int` | Pop a value |
| `ring_peek` | `ring_peek(ring: int) -> int` | Peek at the front value |
| `ring_len` | `ring_len(ring: int) -> int` | Get current length |
| `ring_cap` | `ring_cap(ring: int) -> int` | Get capacity |

### Lock-Free Queue

| Function | Signature | Description |
|----------|-----------|-------------|
| `lfq_new` | `lfq_new(capacity: int) -> int` | Create a lock-free queue |
| `lfq_try_push` | `lfq_try_push(q: int, val: int) -> int` | Try to push a value |
| `lfq_try_pop` | `lfq_try_pop(q: int) -> int` | Try to pop a value |
| `lfq_len` | `lfq_len(q: int) -> int` | Get current length |

### Scatter-Gather

| Function | Signature | Description |
|----------|-----------|-------------|
| `sg_gather2` | `sg_gather2(a: string, b: string) -> string` | Gather two buffers |
| `sg_gather3` | `sg_gather3(a: string, b: string, c: string) -> string` | Gather three buffers |
| `sg_slice` | `sg_slice(buf: string, start: int, end: int) -> string` | Slice a gathered buffer |

### FSM (Finite State Machine)

| Function | Signature | Description |
|----------|-----------|-------------|
| `fsm_rule` | `fsm_rule(state: string, event: string, next: string) -> string` | Define a state transition rule |
| `fsm_is` | `fsm_is(rules: string, state: string) -> int` | Check current state |
| `fsm_can` | `fsm_can(rules: string, state: string, event: string) -> int` | Check if transition is possible |
| `fsm_transition` | `fsm_transition(rules: string, state: string, event: string) -> string` | Perform a state transition |

### Frame Allocator & Object Pool

| Function | Signature | Description |
|----------|-----------|-------------|
| `frame_alloc_new` | `frame_alloc_new(size: int) -> int` | Create a frame allocator |
| `frame_alloc` | `frame_alloc(fa: int, bytes: int) -> int` | Allocate from frame allocator |
| `frame_reset` | `frame_reset(fa: int) -> int` | Reset frame allocator |
| `frame_used` | `frame_used(fa: int) -> int` | Get used bytes |
| `frame_cap` | `frame_cap(fa: int) -> int` | Get capacity |
| `obj_pool_new` | `obj_pool_new(count: int) -> int` | Create an object pool |
| `obj_acquire` | `obj_acquire(pool: int) -> int` | Acquire an object |
| `obj_release` | `obj_release(pool: int, id: int) -> int` | Release an object |
| `obj_available` | `obj_available(pool: int) -> int` | Get available objects |
| `obj_pool_cap` | `obj_pool_cap(pool: int) -> int` | Get pool capacity |

---

## 50. ECS (Entity Component System)

| Function | Signature | Description |
|----------|-----------|-------------|
| `ecs_world_new` | `ecs_world_new(max_entities: int) -> int` | Create a new ECS world |
| `ecs_spawn` | `ecs_spawn(world: int) -> int` | Spawn a new entity |
| `ecs_alive` | `ecs_alive(world: int, entity: int) -> int` | Check if an entity is alive |
| `ecs_despawn` | `ecs_despawn(world: int, entity: int) -> int` | Despawn an entity |
| `ecs_add` | `ecs_add(world: int, entity: int, component: int, value: int) -> int` | Add a component to an entity |
| `ecs_set` | `ecs_set(world: int, entity: int, component: int, value: int) -> int` | Set a component value |
| `ecs_has` | `ecs_has(world: int, entity: int, component: int) -> int` | Check if entity has component |
| `ecs_get` | `ecs_get(world: int, entity: int, component: int) -> int` | Get a component value |
| `ecs_remove` | `ecs_remove(world: int, entity: int, component: int) -> int` | Remove a component |
| `ecs_query_count` | `ecs_query_count(world: int, mask: int) -> int` | Count entities matching component mask |
| `ecs_query_first` | `ecs_query_first(world: int, mask: int) -> int` | Get first entity matching mask |
| `ecs_archetype` | `ecs_archetype(world: int, entity: int) -> int` | Get archetype of an entity |
| `ecs_system_add` | `ecs_system_add(world: int, mask: int, callback: int) -> int` | Register a system |

---

## 51. Actors

| Function | Signature | Description |
|----------|-----------|-------------|
| `actor_spawn` | `actor_spawn(mailbox_size: int) -> chan[int]` | Spawn an actor with a mailbox |
| `actor_send` | `actor_send(mailbox: chan[int], msg: int) -> bool` | Send a message to an actor |
| `actor_recv` | `actor_recv(mailbox: chan[int]) -> int` | Receive a message from a mailbox |
| `actor_stop` | `actor_stop(mailbox: chan[int]) -> void` | Stop an actor |

---

## 52. Bytes & UTF-8

| Function | Signature | Description |
|----------|-----------|-------------|
| `as_bytes` | `as_bytes(s: string) -> []byte` | Convert a string to a byte array |
| `bytes_as_str` | `bytes_as_str(b: []byte) -> string` | Convert a byte array to a string |
| `bytes_is_view` | `bytes_is_view(b: []byte) -> int` | Check if byte array is a view |
| `bytes_view` | `bytes_view(s: string, start: int, end: int) -> []byte` | Create a byte view into a string |
| `simd_xor_bytes` | `simd_xor_bytes(b: []byte) -> int` | XOR all bytes (SIMD-accelerated) |
| `utf8_valid` | `utf8_valid(s: string) -> int` | Check if string is valid UTF-8 |
| `utf8_rune_len` | `utf8_rune_len(codepoint: int) -> int` | Get byte length of a rune |

---

## 53. Buffered I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `buf_reader_new` | `buf_reader_new(path: string) -> BufReader` | Create a buffered reader from a file |
| `buf_reader_from_string` | `buf_reader_from_string(data: string) -> BufReader` | Create a buffered reader from a string |
| `buf_read_line` | `buf_read_line(r: BufReader) -> string` | Read the next line |
| `buf_read` | `buf_read(r: BufReader, n: int) -> string` | Read n bytes |
| `buf_reader_close` | `buf_reader_close(r: BufReader) -> int` | Close the reader |
| `buf_writer_new` | `buf_writer_new(path: string) -> BufWriter` | Create a buffered writer to a file |
| `buf_write` | `buf_write(w: BufWriter, data: string) -> int` | Write data to buffer |
| `buf_write_byte` | `buf_write_byte(w: BufWriter, b: int) -> int` | Write a single byte |
| `buf_flush` | `buf_flush(w: BufWriter) -> int` | Flush the buffer to disk |
| `buf_writer_close` | `buf_writer_close(w: BufWriter) -> int` | Close the writer |

---

## 54. Async I/O

| Function | Signature | Description |
|----------|-----------|-------------|
| `io_wait` | `io_wait(fd: int, timeout_ms: int) -> int` | Wait for I/O readiness |
| `io_poll2` | `io_poll2(fd1: int, fd2: int, timeout_ms: int) -> int` | Poll two fds |
| `io_poll3` | `io_poll3(fd1: int, fd2: int, fd3: int, timeout_ms: int) -> int` | Poll three fds |
| `io_poll4` | `io_poll4(fd1: int, fd2: int, fd3: int, fd4: int, timeout_ms: int) -> int` | Poll four fds |
| `io_kq_poll2` | `io_kq_poll2(fd1: int, fd2: int, timeout_ms: int) -> int` | Kqueue-based poll of two fds |
| `io_epoll_poll2` | `io_epoll_poll2(fd1: int, fd2: int, timeout_ms: int) -> int` | Epoll-based poll of two fds |
| `io_native_poll2` | `io_native_poll2(fd1: int, fd2: int, timeout_ms: int) -> int` | Native poll of two fds |
| `io_read_ready` | `io_read_ready(fd: int, timeout_ms: int) -> int` | Check if fd is read-ready |
| `io_write_ready` | `io_write_ready(fd: int, timeout_ms: int) -> int` | Check if fd is write-ready |
| `io_set_nonblocking` | `io_set_nonblocking(fd: int, nonblocking: int) -> int` | Set fd blocking mode |
| `io_try_write` | `io_try_write(fd: int, data: string) -> int` | Non-blocking write attempt |
| `io_backoff_ms` | `io_backoff_ms(attempt: int, base: int, max: int) -> int` | Calculate I/O backoff delay |
| `io_should_pause` | `io_should_pause(pending: int, threshold: int, timeout: int) -> int` | Check if I/O should be paused |

---

## 55. Signals

| Function | Signature | Description |
|----------|-----------|-------------|
| `signal_notify` | `signal_notify(signum: int) -> int` | Register for signal notification |
| `signal_received` | `signal_received() -> int` | Check if a signal was received |

---

## 56. DNS & Networking

| Function | Signature | Description |
|----------|-----------|-------------|
| `lookup_host` | `lookup_host(host: string) -> string` | Look up the first IP for a hostname |
| `dns_lookup_count` | `dns_lookup_count(host: string) -> int` | Count DNS results |
| `dns_lookup_all` | `dns_lookup_all(host: string) -> string` | Get all DNS results |
| `dns_lookup_ipv4` | `dns_lookup_ipv4(host: string) -> string` | Look up IPv4 address |
| `dns_lookup_ipv6` | `dns_lookup_ipv6(host: string) -> string` | Look up IPv6 address |
| `parse_ip_ok` | `parse_ip_ok(addr: string) -> int` | Validate an IP address |
| `dns_ip_family` | `dns_ip_family(addr: string) -> int` | Get IP address family (4 or 6) |
| `dns_is_loopback` | `dns_is_loopback(addr: string) -> int` | Check if address is loopback |
| `dns_is_private` | `dns_is_private(addr: string) -> int` | Check if address is private |
| `dns_normalize_host` | `dns_normalize_host(host: string) -> string` | Normalize a hostname |
| `dns_join_host_port` | `dns_join_host_port(host: string, port: int) -> string` | Join host and port |
| `dns_split_host` | `dns_split_host(hostport: string) -> string` | Extract host from host:port |
| `dns_split_port` | `dns_split_port(hostport: string) -> int` | Extract port from host:port |

---

## 57. URL Parsing

| Function | Signature | Description |
|----------|-----------|-------------|
| `url_scheme` | `url_scheme(url: string) -> string` | Extract the scheme from a URL |
| `url_host` | `url_host(url: string) -> string` | Extract the host from a URL |
| `url_path` | `url_path(url: string) -> string` | Extract the path from a URL |
| `url_query` | `url_query(url: string) -> string` | Extract the query string from a URL |

---

## 58. Templates

| Function | Signature | Description |
|----------|-----------|-------------|
| `template_execute` | `template_execute(tmpl: string, key: string, val: string) -> string` | Execute a template with one variable |
| `html_template_execute` | `html_template_execute(tmpl: string, key: string, val: string) -> string` | Execute an HTML template with one variable |
| `html_template_execute2` | `html_template_execute2(tmpl: string, k1: string, v1: string, k2: string, v2: string) -> string` | Execute an HTML template with two variables |
| `html_template_execute3` | `html_template_execute3(tmpl: string, k1: string, v1: string, k2: string, v2: string, k3: string, v3: string) -> string` | Execute an HTML template with three variables |
| `html_template_if` | `html_template_if(tmpl: string, key: string, cond: int, val: string) -> string` | Execute an HTML template with conditional |
| `html_template_range` | `html_template_range(tmpl: string, key: string, items: string) -> string` | Execute an HTML template with range iteration |
| `html_template_with` | `html_template_with(tmpl: string, key: string, val: string) -> string` | Execute an HTML template with context |
| `html_template_nested` | `html_template_nested(outer: string, key: string, cond: int, inner: string, val: string) -> string` | Execute nested HTML templates |

---

## 59. Image Processing

### PNG

| Function | Signature | Description |
|----------|-----------|-------------|
| `png_available` | `png_available() -> int` | Check if PNG support is available |
| `png_encode_gray` | `png_encode_gray(width: int, height: int, pixels: string) -> string` | Encode grayscale PNG |
| `png_encode_rgb` | `png_encode_rgb(width: int, height: int, pixels: string) -> string` | Encode RGB PNG |
| `png_decode_gray` | `png_decode_gray(data: string) -> string` | Decode grayscale PNG |
| `png_width` | `png_width(data: string) -> int` | Get PNG width |
| `png_height` | `png_height(data: string) -> int` | Get PNG height |

### GIF

| Function | Signature | Description |
|----------|-----------|-------------|
| `gif_encode_rgb` | `gif_encode_rgb(width: int, height: int, pixels: string) -> string` | Encode RGB GIF |
| `gif_decode_rgb` | `gif_decode_rgb(data: string) -> string` | Decode RGB GIF |
| `gif_width` | `gif_width(data: string) -> int` | Get GIF width |
| `gif_height` | `gif_height(data: string) -> int` | Get GIF height |
| `gif_encode_rgb_lzw` | `gif_encode_rgb_lzw(width: int, height: int, pixels: string) -> string` | Encode RGB GIF with LZW |
| `gif_decode_rgb_lzw` | `gif_decode_rgb_lzw(data: string) -> string` | Decode LZW GIF |

### JPEG

| Function | Signature | Description |
|----------|-----------|-------------|
| `jpeg_encode_gray` | `jpeg_encode_gray(width: int, height: int, pixels: string) -> string` | Encode grayscale JPEG |
| `jpeg_decode_gray` | `jpeg_decode_gray(data: string) -> string` | Decode grayscale JPEG |
| `jpeg_width` | `jpeg_width(data: string) -> int` | Get JPEG width |
| `jpeg_height` | `jpeg_height(data: string) -> int` | Get JPEG height |
| `jpeg_encode_gray_dct` | `jpeg_encode_gray_dct(width: int, height: int, pixels: string) -> string` | Encode JPEG with DCT |
| `jpeg_dct_dc` | `jpeg_dct_dc(data: string) -> int` | Get DCT DC coefficient |
| `jpeg_encode_gray_huff` | `jpeg_encode_gray_huff(width: int, height: int, pixels: string) -> string` | Encode JPEG with Huffman |
| `jpeg_huff_block` | `jpeg_huff_block(data: string) -> string` | Get Huffman-encoded block |
| `jpeg_encode_gray_jfif` | `jpeg_encode_gray_jfif(width: int, height: int, pixels: string) -> string` | Encode grayscale with SOI+APP0(JFIF)+SOF0 headers; pixels in APP7 (`MAKOJPG`) for `jpeg_decode_gray` roundtrip. External viewers see a JFIF shell, not a full Huffman bitstream. |
| `jpeg_is_jfif` | `jpeg_is_jfif(data: string) -> int` | Check if data has JFIF APP0 marker |
| `jpeg_has_sof0` | `jpeg_has_sof0(data: string) -> int` | Scan markers for SOF0 (baseline DCT header) |
| `jpeg_sof0_width` | `jpeg_sof0_width(data: string) -> int` | Width from SOF0 (0 if missing) |
| `jpeg_sof0_height` | `jpeg_sof0_height(data: string) -> int` | Height from SOF0 (0 if missing) |
| `jpeg_sof0_precision` | `jpeg_sof0_precision(data: string) -> int` | Sample precision from SOF0 (0 if missing; Mako JFIF uses 8) |
| `jpeg_sof0_components` | `jpeg_sof0_components(data: string) -> int` | Component count Nf from SOF0 (grayscale JFIF uses 1) |
| `jpeg_is_baseline_gray` | `jpeg_is_baseline_gray(data: string) -> int` | 1 if JFIF + SOF0 + 8-bit + Nf==1 (Mako grayscale shell) |
| `jpeg_jfif_major` | `jpeg_jfif_major(data: string) -> int` | JFIF major version from APP0 (0 if missing; Mako shell uses 1) |
| `jpeg_jfif_minor` | `jpeg_jfif_minor(data: string) -> int` | JFIF minor version from APP0 (0 if missing; Mako shell uses 1) |
| `jpeg_sof0_sampling` | `jpeg_sof0_sampling(data: string) -> int` | First component Hi/Vi packed byte from SOF0 (grayscale uses `0x11`) |
| `jpeg_sof0_component_id` | `jpeg_sof0_component_id(data: string) -> int` | First component id Ci from SOF0 (grayscale JFIF uses 1) |
| `jpeg_jfif_density_units` | `jpeg_jfif_density_units(data: string) -> int` | JFIF density units from APP0 (`-1` if missing; Mako shell uses 0) |
| `jpeg_jfif_x_density` | `jpeg_jfif_x_density(data: string) -> int` | JFIF X density from APP0 (0 if missing; Mako shell uses 1) |
| `jpeg_jfif_y_density` | `jpeg_jfif_y_density(data: string) -> int` | JFIF Y density from APP0 (0 if missing; Mako shell uses 1) |
| `jpeg_has_app7` | `jpeg_has_app7(data: string) -> int` | 1 if APP7 carries `MAKOJPG` roundtrip payload |
| `jpeg_sof0_quant_table` | `jpeg_sof0_quant_table(data: string) -> int` | First component quant table selector Tqi (`-1` if missing; Mako gray uses 0) |
| `jpeg_jfif_thumb_width` | `jpeg_jfif_thumb_width(data: string) -> int` | JFIF embedded thumbnail width (`-1` if no APP0; Mako shell uses 0) |
| `jpeg_jfif_thumb_height` | `jpeg_jfif_thumb_height(data: string) -> int` | JFIF embedded thumbnail height (`-1` if no APP0; Mako shell uses 0) |
| `jpeg_is_mako_jfif` | `jpeg_is_mako_jfif(data: string) -> int` | 1 if baseline-gray JFIF shell **and** MAKOJPG APP7 payload |
| `jpeg_has_eoi` | `jpeg_has_eoi(data: string) -> int` | 1 if JPEG has EOI (`FF D9`) after SOI |
| `jpeg_sof0_matches_app7` | `jpeg_sof0_matches_app7(data: string) -> int` | 1 if SOF0 width/height match MAKOJPG APP7 payload dims |
| `jpeg_is_mako_complete` | `jpeg_is_mako_complete(data: string) -> int` | 1 if `jpeg_is_mako_jfif` + dim match + EOI |
| `jpeg_is_mako_raw` | `jpeg_is_mako_raw(data: string) -> int` | 1 if APP7 MAKOJPG + EOI + positive dims (JFIF optional; covers `jpeg_encode_gray`) |
| `jpeg_jfif_app0_length` | `jpeg_jfif_app0_length(data: string) -> int` | APP0 segment length field (0 if missing; Mako JFIF uses 16) |
| `jpeg_app7_payload_len` | `jpeg_app7_payload_len(data: string) -> int` | APP7 pixel payload byte count (`width * height` from MAKOJPG dims) |
| `jpeg_has_app8` | `jpeg_has_app8(data: string) -> int` | 1 if APP8 DCT-DC evidence marker present (`jpeg_encode_gray_dct`) |
| `jpeg_has_app9` | `jpeg_has_app9(data: string) -> int` | 1 if APP9 Huffman-ish block present (`jpeg_encode_gray_huff`) |
| `jpeg_is_mako_dct` | `jpeg_is_mako_dct(data: string) -> int` | 1 if `jpeg_is_mako_raw` + APP8 |
| `jpeg_is_mako_huff` | `jpeg_is_mako_huff(data: string) -> int` | 1 if `jpeg_is_mako_dct` + APP9 |
| `jpeg_app8_length` | `jpeg_app8_length(data: string) -> int` | APP8 segment length field (0 if missing; Mako DCT uses 6) |
| `jpeg_app9_length` | `jpeg_app9_length(data: string) -> int` | APP9 segment length field (0 if missing; Mako huff uses 66) |
| `jpeg_roundtrip_ok` | `jpeg_roundtrip_ok(data: string) -> int` | 1 if APP7 decode length equals `width*height` payload |
| `jpeg_app7_length` | `jpeg_app7_length(data: string) -> int` | APP7 MAKOJPG segment length field (0 if missing; 8×8 gray uses 77) |
| `jpeg_has_soi` | `jpeg_has_soi(data: string) -> int` | 1 if buffer starts with SOI (`FF D8`) |
| `jpeg_app7_len_matches_payload` | `jpeg_app7_len_matches_payload(data: string) -> int` | 1 if APP7 length field equals `2+7+4+width*height` |

---

## 60. Archive Formats

### Tar

| Function | Signature | Description |
|----------|-----------|-------------|
| `tar_write_file` | `tar_write_file(name: string, data: string, archive: string) -> int` | Write a file to a tar archive |
| `tar_first_name` | `tar_first_name(archive: string) -> string` | Get the name of the first file in a tar |

### Zip

| Function | Signature | Description |
|----------|-----------|-------------|
| `zip_write_file` | `zip_write_file(name: string, data: string, archive: string) -> int` | Write a file to a zip archive |
| `zip_first_name` | `zip_first_name(archive: string) -> string` | Get the name of the first file |
| `zip_read_file` | `zip_read_file(archive: string, name: string) -> string` | Read a file from a zip archive |
| `zip_list` | `zip_list(archive: string) -> []string` | List files in a zip archive |
| `zip_create` | `zip_create() -> ZipWriter` | Create a new zip writer |
| `zip_add` | `zip_add(zw: ZipWriter, name: string, data: string) -> int` | Add a file to a zip writer |
| `zip_write_to` | `zip_write_to(zw: ZipWriter, path: string) -> int` | Write zip to a file |
| `zip_close` | `zip_close(zw: ZipWriter) -> void` | Close the zip writer |
| `zip_deflate_available` | `zip_deflate_available() -> int` | Check if zip deflate is available |

---

## 61. Multipart Forms

| Function | Signature | Description |
|----------|-----------|-------------|
| `multipart_boundary` | `multipart_boundary(content_type: string) -> string` | Extract multipart boundary from Content-Type |
| `multipart_form_value` | `multipart_form_value(body: string, boundary: string, field: string) -> string` | Get a form field value |
| `multipart_file_name` | `multipart_file_name(body: string, boundary: string, field: string) -> string` | Get uploaded file name |
| `multipart_file_content_type` | `multipart_file_content_type(body: string, boundary: string, field: string) -> string` | Get uploaded file content type |
| `multipart_file_value` | `multipart_file_value(body: string, boundary: string, field: string) -> string` | Get uploaded file contents |
| `multipart_file_size` | `multipart_file_size(body: string, boundary: string, field: string) -> int` | Get uploaded file size |
| `multipart_file_allowed` | `multipart_file_allowed(body: string, boundary: string, field: string, max_size: int, allowed_types: string) -> int` | Validate uploaded file |

---

## 62. Reflect

| Function | Signature | Description |
|----------|-----------|-------------|
| `reflect_type_of_int` | `reflect_type_of_int(val: int) -> string` | Get type name of an int value |
| `reflect_type_of_string` | `reflect_type_of_string(val: string) -> string` | Get type name of a string value |
| `reflect_kind_of_int` | `reflect_kind_of_int(val: int) -> string` | Get kind of an int value |
| `reflect_kind_of_string` | `reflect_kind_of_string(val: string) -> string` | Get kind of a string value |
| `reflect_value_string_int` | `reflect_value_string_int(val: int) -> string` | Convert int to reflect string |
| `reflect_value_string_str` | `reflect_value_string_str(val: string) -> string` | Convert string to reflect string |
| `reflect_len_string` | `reflect_len_string(val: string) -> int` | Get length via reflect |
| `reflect_struct_num_fields` | `reflect_struct_num_fields(type_name: string) -> int` | Get number of struct fields |
| `reflect_struct_field_name` | `reflect_struct_field_name(type_name: string, idx: int) -> string` | Get struct field name |
| `reflect_struct_field_type` | `reflect_struct_field_type(type_name: string, idx: int) -> string` | Get struct field type |
| `reflect_struct_has_field` | `reflect_struct_has_field(type_name: string, field: string) -> int` | Check if struct has a field |
| `reflect_value_new` | `reflect_value_new(type_name: string) -> ReflectValue` | Create a new reflect value |
| `reflect_value_set` | `reflect_value_set(rv: ReflectValue, field: string, val: string) -> int` | Set a field on a reflect value |
| `reflect_value_get` | `reflect_value_get(rv: ReflectValue, field: string) -> string` | Get a field from a reflect value |
| `reflect_value_num_fields` | `reflect_value_num_fields(rv: ReflectValue) -> int` | Get number of fields |
| `reflect_value_field_at` | `reflect_value_field_at(rv: ReflectValue, idx: int) -> string` | Get field value at index |
| `reflect_value_set_at` | `reflect_value_set_at(rv: ReflectValue, idx: int, val: string) -> int` | Set field value at index |
| `reflect_value_schema` | `reflect_value_schema(rv: ReflectValue) -> string` | Get value schema as string |
| `reflect_value_clone` | `reflect_value_clone(rv: ReflectValue) -> ReflectValue` | Clone a reflect value |
| `reflect_value_equal` | `reflect_value_equal(a: ReflectValue, b: ReflectValue) -> int` | Compare two reflect values |
| `reflect_value_of_type` | `reflect_value_of_type(type_name: string) -> ReflectValue` | Create a value of a named type |
| `reflect_type_schema` | `reflect_type_schema(type_name: string) -> string` | Get type schema |
| `reflect_type_count` | `reflect_type_count() -> int` | Count registered types |
| `reflect_type_name_at` | `reflect_type_name_at(idx: int) -> string` | Get type name at index |

---

## 63. Context & Timeout

| Function | Signature | Description |
|----------|-----------|-------------|
| `context_with_timeout` | `context_with_timeout(timeout_ms: int) -> int` | Create a context with timeout |
| `context_expired` | `context_expired(ctx: int) -> int` | Check if context has expired |
| `context_remaining` | `context_remaining(ctx: int) -> int` | Milliseconds remaining |

---

## 64. BytesBuffer

| Function | Signature | Description |
|----------|-----------|-------------|
| `bytes_buffer` | `bytes_buffer() -> BytesBuffer` | Create a new bytes buffer |
| `bytes_buffer_write` | `bytes_buffer_write(bb: BytesBuffer, data: string) -> void` | Write to the buffer |
| `bytes_buffer_string` | `bytes_buffer_string(bb: BytesBuffer) -> string` | Get buffer contents as string |
| `bytes_buffer_len` | `bytes_buffer_len(bb: BytesBuffer) -> int` | Get buffer length |
| `bytes_buffer_reset` | `bytes_buffer_reset(bb: BytesBuffer) -> void` | Reset the buffer |

---

## 65. Error Handling

| Function | Signature | Description |
|----------|-----------|-------------|
| `result_unwrap_or` | `result_unwrap_or(r: Result[int, string], default: int) -> int` | Unwrap Result or return default |
| `wrap_err` | `wrap_err(r: Result[int, string], msg: string) -> Result[int, string]` | Wrap an error with additional context |
| `errorf` | `errorf(fmt: string, arg: string) -> Result[int, string]` | Create a formatted error Result |
| `error_is` | `error_is(r: Result[int, string], target: string) -> bool` | Check if error matches a target string |
| `error_string` | `error_string(r: Result[int, string]) -> string` | Extract error message from a Result |

---

## 66. OpenAPI & GraphQL

| Function | Signature | Description |
|----------|-----------|-------------|
| `openapi_route` | `openapi_route(method: string, path: string, summary: string) -> string` | Define an OpenAPI route |
| `openapi_doc` | `openapi_doc(title: string, version: string, routes: string) -> string` | Generate an OpenAPI document |
| `graphql_field` | `graphql_field(name: string) -> string` | Create a GraphQL field |
| `graphql_arg` | `graphql_arg(name: string, value: string) -> string` | Create a GraphQL argument |
| `graphql_data` | `graphql_data(key: string, value: string) -> string` | Create a GraphQL data response |
| `graphql_error` | `graphql_error(msg: string) -> string` | Create a GraphQL error response |
| `graphql_request` | `graphql_request(query: string) -> string` | Build a GraphQL request |
| `graphql_is_mutation` | `graphql_is_mutation(query: string) -> int` | Check if query is a mutation |

---

## 67. SSE & RPC

| Function | Signature | Description |
|----------|-----------|-------------|
| `sse_event` | `sse_event(event: string, data: string) -> string` | Format a Server-Sent Event |
| `sse_retry` | `sse_retry(ms: int) -> string` | Format an SSE retry directive |
| `rpc_frame` | `rpc_frame(method: string, payload: string) -> string` | Build an RPC frame |
| `rpc_method` | `rpc_method(frame: string) -> string` | Extract method from RPC frame |
| `rpc_payload` | `rpc_payload(frame: string) -> string` | Extract payload from RPC frame |

---

## 68. NoSQL Databases

| Function | Signature | Description |
|----------|-----------|-------------|
| `mongo_connect_url` | `mongo_connect_url(url: string) -> string` | Parse a MongoDB connection URL |
| `mongo_find_one_request` | `mongo_find_one_request(collection: string, filter: string) -> string` | Build a findOne request |
| `cassandra_connect_url` | `cassandra_connect_url(url: string) -> string` | Parse a Cassandra connection URL |
| `cassandra_select` | `cassandra_select(keyspace: string, table: string, where_clause: string) -> string` | Build a Cassandra SELECT |
| `clickhouse_connect_url` | `clickhouse_connect_url(url: string) -> string` | Parse a ClickHouse connection URL |
| `clickhouse_select` | `clickhouse_select(db: string, table: string, where_clause: string) -> string` | Build a ClickHouse SELECT |
| `elastic_connect_url` | `elastic_connect_url(url: string) -> string` | Parse an Elasticsearch connection URL |
| `elastic_search_request` | `elastic_search_request(index: string, query: string) -> string` | Build an Elasticsearch search request |
| `queue_stub_ping` | `queue_stub_ping() -> int` | Queue stub ping |

---

## 69. Process Execution

| Function | Signature | Description |
|----------|-----------|-------------|
| `exec_output` | `exec_output(cmd: string) -> string` | Run a command and return its output |
| `exec_run` | `exec_run(cmd: string) -> int` | Run a command and return exit code |

---

## 70. MIME

| Function | Signature | Description |
|----------|-----------|-------------|
| `mime_type` | `mime_type(ext: string) -> string` | Get MIME type for a file extension |


---

## 71. Checked arithmetic & overflow

CLI: `mako build --overflow trap|wrap|ignore` (also on `mako run`).  
Trap mode rewrites integer `+ - *` to `mako_add_i64` / `sub` / `mul` (abort on overflow).

| Function | Signature | Description |
|----------|-----------|-------------|
| `checked_add` | `checked_add(a: int, b: int) -> int` | Add; **abort** on overflow |
| `checked_sub` | `checked_sub(a: int, b: int) -> int` | Subtract; abort on overflow |
| `checked_mul` | `checked_mul(a: int, b: int) -> int` | Multiply; abort on overflow |
| `would_overflow_add` | `would_overflow_add(a: int, b: int) -> int` | `1` if `a+b` would overflow |
| `would_overflow_sub` | `would_overflow_sub(a: int, b: int) -> int` | `1` if `a-b` would overflow |
| `would_overflow_mul` | `would_overflow_mul(a: int, b: int) -> int` | `1` if `a*b` would overflow |

Runtime: `runtime/mako_overflow.h`.

---

## 72. Graceful shutdown

| Function | Signature | Description |
|----------|-----------|-------------|
| `install_graceful_shutdown` | `install_graceful_shutdown(grace_ms: int) -> int` | `signal_on_term` + store preferred grace |
| `shutdown_requested` | `shutdown_requested() -> int` | `1` if shutdown was signaled |
| `signal_on_term` | `signal_on_term() -> int` | Install SIGTERM/SIGINT → set shutdown flag |
| `register_listener` | `register_listener(fd: int) -> int` | Track listen fd for close on drain |
| `close_listeners` | `close_listeners() -> int` | Close registered listeners |
| `server_shutdown_begin` | `server_shutdown_begin(grace_ms: int) -> int` | Flag drain + close listeners |
| `server_drain` | `server_drain(timeout_ms: int) -> int` | Wait up to timeout (10ms slices) |
| `should_stop_accepting` | `should_stop_accepting() -> int` | Accept-loop stop flag |
| `http_shutdown_begin` | `http_shutdown_begin() -> int` | Begin HTTP server shutdown |
| `http_shutdown_requested` | `http_shutdown_requested() -> int` | Check if HTTP shutdown in progress |
| `http_shutdown_drain_conn` | `http_shutdown_drain_conn(fd: int) -> int` | Drain a single HTTP connection |
| `http_shutdown_expired` | `http_shutdown_expired() -> int` | Check if grace period expired |
| `http_shutdown_remaining` | `http_shutdown_remaining() -> int` | Remaining ms in grace window |
| `http_shutdown_ready` | `http_shutdown_ready() -> int` | `1` if all connections drained |
| `http_shutdown_reset` | `http_shutdown_reset() -> int` | Reset shutdown state |
| `http_shutdown_deadline` | `http_shutdown_deadline(ms: int) -> int` | Set deadline for drain |
| `http_shutdown_from_signal` | `http_shutdown_from_signal() -> int` | Trigger shutdown from signal handler |

Runtime: `runtime/mako_shutdown.h`. Pairs with `signal_watch`.

---

## 73. Leak detector (scopes)

Builds on `leak_mark` / `alloc_track_*`. Nestable scopes:

| Function | Signature | Description |
|----------|-----------|-------------|
| `leak_mark` | `leak_mark() -> int` | Snapshot live alloc bytes |
| `leak_bytes_since` | `leak_bytes_since(mark: int) -> int` | Bytes still live since mark |
| `leak_detected` | `leak_detected(mark: int) -> int` | `1` if bytes grew since mark |
| `leak_assert_clear` | `leak_assert_clear(mark: int) -> int` | Assert no growth since mark |
| `leak_report_json` | `leak_report_json(mark: int) -> string` | JSON report since mark |
| `leak_scope_enter` | `leak_scope_enter() -> int` | Push nestable scope mark |
| `leak_scope_exit` | `leak_scope_exit() -> int` | Pop; returns leaked bytes; warns on stderr if >0 |
| `leak_check` | `leak_check() -> int` | Bytes over current scope mark (or live if none) |
| `leak_assert_scope` | `leak_assert_scope() -> int` | `1` if no leak in current scope |

Runtime: `runtime/mako_leak.h` (builds on `alloc_track_*` in `mako_std.h`).

---

## 74. Distributed tracing + logs

| Function | Signature | Description |
|----------|-----------|-------------|
| `trace_begin` | `trace_begin(name: string) -> int` | Start a named span under current (or new) trace |
| `trace_end` | `trace_end() -> int` | End span; JSON line to stderr; returns duration ms |
| `trace_id` | `trace_id() -> string` | Install new 128-bit id; return hex (32 chars) |
| `trace_set` | `trace_set(id: string) -> int` | Install existing 32-hex id |
| `trace_current` | `trace_current() -> string` | Current **trace id hex**, or empty |
| `trace_log` | `trace_log(msg: string) -> int` | JSON log line with trace context |
| `trace_clear` | `trace_clear() -> int` | Clear TLS trace state |
| `middleware_trace` | `middleware_trace(c: int) -> string` | Extract/generate trace ID from HTTP request |
| `log_info` / `log_warn` / `log_error` / `log_debug` | `log_*(msg: string)` | Stderr logs; when a trace is active, include `trace=<hex>` |
| `slog_with` | `slog_with(level, msg, key, val)` | Structured log; also emits `trace=` when active |

Runtime: `runtime/mako_trace.h` · log helpers in `mako_std.h` / `mako_goext.h` (include order: trace before std).
Test: `examples/testing/trace_log_test.mko`.

---

## CLI notes

| Flag / command | Meaning |
|----------------|---------|
| `--overflow trap\|wrap\|ignore` | Integer overflow codegen (build/run) |
| `--bounds always` | Keep bounds checks in release |
| `mako test --race` | ThreadSanitizer (also `mako build --sanitize thread`) |
| `mako dev [file]` | Watch mtime → rebuild + rerun (hot reload seed) |
| `./scripts/bench-gate.sh [2.0\|1.5]` | Microbench vs Rust (CI job on ubuntu) |

Tests: `examples/testing/overflow_shutdown_test.mko`. Multi-error recovery:
`examples/bad/multi_error.mko` (`mako check` reports all top-level parse errors).

**CI** (`.github/workflows/ci.yml`): native matrix · cross-smoke · **bench-gate** · **TSan** concurrency smoke (`crew_fan`, `kick_send`, `chan_struct`, `crew_drain`).

---

## 75. Result[T, E] typed errors

| Ok type `T` | Encoding |
|-------------|----------|
| int family / bool | `MakoResultInt.value` via `mako_ok_int` |
| string | `MakoResultInt.ok_s` via `mako_ok_str` |
| float | `MakoResultInt.ok_f` via `mako_ok_float_res` |
| named struct | heap box via `mako_ok_ptr`; match Ok unboxes and frees |
| `[]int` | heap-boxed `MakoIntArray` via `mako_ok_ptr` (`slice`) |
| `[]string` | heap-boxed `MakoStrArray` via `mako_ok_ptr` (`slice_str`) |
| `[]float` | heap-boxed `MakoFloatArray` via `mako_ok_ptr` (`slice_float`) |
| `[]Struct` | heap-boxed `MakoArr_{Name}` via `mako_ok_ptr` (`slice_struct`) |
| `map[string]int` | map pointer via `mako_ok_ptr` (`map_si`) |
| `map[int]int` | map pointer via `mako_ok_ptr` (`map_ii`) |
| `map[string]string` | map pointer via `mako_ok_ptr` (`map_ss`) |
| `Option[U]` | heap-boxed `MakoOptionInt` via `mako_ok_ptr` (`option`) |
| `Result[U, E]` | heap-boxed nested `MakoResultInt` via `mako_ok_ptr` (`result`) |

`Option[T]` uses the same payload slots (`value` / `ok_s` / `ok_f` / ptr). Generic
`Some(x)` / `None` and match `Some(v)` work for int/string/float, boxed containers,
and multi-layer Option nests via kind chains. Nested `Ok(Ok(x))` /
`Result[Result[T, E], E2]` is supported (typecheck pushes expected Result for
inner Ok/Err; codegen boxes the inner Result). Mixed nests work via expected-type context plus **general successive nest-kind
chains** for alternating Result/Option layers (including 5-layer
`Result[Option[Result[Option[Result[T]]]]]`).

| Err type `E` | Encoding |
|--------------|----------|
| string | `err_kind=0`, `err` string (`mako_err_int`) |
| user `enum` | `err_kind=1`, tag + i0..i2 + s0..s1 (`mako_err_enum_ex`); `match Err(e)` reconstructs |

```mko
enum IoError { NotFound, Permission(int), Other(string) }

fn open(code: int) -> Result[int, IoError] {
    if code == 0 { return Ok(1) }
    return Err(NotFound)
}

fn name() -> Result[string, string] {
    return Ok("mako")
}

fn scores() -> Result[map[string]int, string] {
    let mut m = make(map[string]int)
    m["a"] = 1
    return Ok(m)
}

match open(1) {
    Ok(v) => print_int(v),
    Err(e) => match e {
        NotFound => print("missing"),
        Permission(c) => print_int(c),
        Other(msg) => print(msg),
    },
}
```

Tests: `result_enum_test.mko`, `job_join_typed_test.mko` (Result across kick/join),
`wave11_queue_test.mko` (`[]int` Ok), `wave12_queue_test.mko` (`map[string]int` Ok),
`wave13_queue_test.mko` (`map[int]int` / `map[string]string` Ok),
`wave14_queue_test.mko` (`[]string` / `[]float` Ok),
`wave15_queue_test.mko` (`[]Struct` Ok),
`wave16_queue_test.mko` (generic `Result[T, string]` Ok mono scalars),
`wave17_queue_test.mko` (generic mono for `[]int`/`[]string`/`[]Struct`/maps),
`wave18_queue_test.mko` (generic `Option[T]`, nested `Result[Option[T]]`),
`wave19_queue_test.mko` (Option containers, `Option[Option[T]]`, `jpeg_has_sof0`),
`wave20_queue_test.mko` (triple Option nest string/int, Option struct),
`wave21_queue_test.mko` (`Result[Result[T]]`, `wrap_ok(Ok(...))`),
`wave22_queue_test.mko` (`Option[Result[T]]`, `Result[Option[Result[T]]]`),
`wave23_queue_test.mko` (`Option[Result[Option[T]]]` deep mixed nests),
`wave24_queue_test.mko` (5-layer alternating Result/Option),
`wave25_queue_test.mko` (bare None, nested Err, mono either, SOF0 dims),
`wave26_queue_test.mko` (None/Err nest edges, nest3 deep Err, SOF0 precision),
`wave27_queue_test.mko` (nested None edges, SOF0 components),
`wave28_queue_test.mko` (deep None/Err, baseline gray, Tai scripts),
`wave29_queue_test.mko` (4-layer Option/Result, JFIF version, SOF0 sampling),
`wave30_queue_test.mko` (5-layer Result nests, JFIF density, APP7, SOF0 Ci),
`wave31_queue_test.mko` (Option 5-layer, SOF0 Tqi, JFIF thumb, `jpeg_is_mako_jfif`),
`wave32_queue_test.mko` (string nests, SOF0↔APP7 match, EOI, `jpeg_is_mako_complete`),
`wave33_queue_test.mko` (bool deep nests, `jpeg_is_mako_raw`, APP0/APP7 lengths),
`wave34_queue_test.mko` (`Result[Result[string]]`, APP8/APP9, `jpeg_is_mako_dct`/`huff`),
`wave35_queue_test.mko` (float nests, `jpeg_roundtrip_ok`, APP8/9 lengths, Common/Mn/Mc),
`wave36_queue_test.mko` (bool/string nests, APP7 length/SOI layout, Sm/Sk/Pc),
`wave37_queue_test.mko` (Option/Result `?` int/string/float unwrap + early return).

---

## 76. const fn

```mko
const fn double(x: int) -> int { return x * 2 }
const SIZE = double(512)   // folded at compile time to 1024
```

Rules:
- Body may use `let` of const expressions and a final `return` / trailing expr.
- Only integer ops (`+ - * / % & | ^ << >>`).
- Calls with const args are folded to integer literals in generated C.

---

## 77. crew_drain / evloop_shutdown

| Function | Signature | Description |
|----------|-----------|-------------|
| `crew_drain` | `crew_drain(timeout_ms: int) -> int` | Drain all crew tasks with timeout |
| `evloop_shutdown` | `evloop_shutdown(el: EvLoop) -> int` | Close event-loop backend and free |

