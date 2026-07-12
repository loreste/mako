# Mako standard library

Batteries for **web and backends**, with naming conventions adapted to Mako.

Call builtins directly (`str_split`, `path_join`, …) **or** import std packages:

```mko
import "strings"
import "path"
import "sync"

let s = strings.concat(strings.split("a,b", ","), "-")
let p = path.clean("/a/../b")
let m = sync.rwmutex()
```

Bare names like `import "strings"` resolve under `std/` (override with `MAKO_STD`)
and auto-alias so `strings.split` works. Relative `import "./x.mko"` unchanged.
Note: method names that are keywords (`join`, `match`, …) use aliases
(`concat`, `matches`, `join_path`).

Performance bar: **fast and lean** on the same
hardware — no *mandatory* GC, arena-per-request, few copies, structured
concurrency.

**Book (stdlib chapter):** [book/src/ch07-stdlib.md](book/src/ch07-stdlib.md) ·
**Working APIs with syntax:** [GUIDE.md](GUIDE.md) · **How-tos:** [howto/](howto/) ·
North star: [VISION.md](VISION.md) · Honest matrix: [STATUS.md](STATUS.md) ·
Queue: [ROADMAP.md](ROADMAP.md).

Runtime: `runtime/mako_rt.h`, `runtime/mako_stdlib.h`, `runtime/mako_std.h`,
`runtime/mako_http.h`, `runtime/mako_db.h`, `runtime/mako_security.h`.

Tests: `examples/testing/stdlib_*`, plus area tests (`base64_test`, `regex_*`,
`errors_test`, `path_join_test`, …). Demo: `examples/stdlib/demo.mko`.

---

## Package index (2026-07-10 · Wave 9)

| Package | Status | Role |
|---------|--------|------|
| `strings` / `bytes` | **Done** | split/join/trim/replace/builder + `bytes.Buffer` |
| `strconv` / `fmt` | **Done** | parse/format; `fmt_sprintf*` |
| `io` / `fs` / `path` / `filepath` | **Done** | files + recursive walk |
| `bufio` | **Done** | buffered reader/writer |
| `os` / `env` / `args` / `os/exec` / `os/signal` | **Done** | env/args/exec; signal Unix |
| `flag` | **Done** | CLI flags |
| `net` / `http` / `net/url` / `net/mail` / `net/smtp` | **Done** | + AUTH PLAIN / STARTTLS probe |
| `encoding/*` + `gob` / `binary` | **Done** | LE+BE; gob map/ss/struct/`[]string` |
| `compress/gzip` · `archive/tar` · `archive/zip` | **Done** | multi-file zip + deflate |
| `mime` / `multipart` · `context` · `crypto` | **Done** | |
| `math` / `rand` · `text/template` / `html/template` | **Done** | if/range/with/nested |
| `html` · `utf8` · `sync` / `atomic` · `slices` / `maps` | **Done** | |
| `errors` / `testing` / `httptest` / `regexp` / `log` / `slog` / `sql` | **Done** | RE2-ish + `\x` `(?:)` `\p{...}` lookahead |
| `image/png` / `gif` / `jpeg` | **Done** | LZW dict; DCT + Huffman block |
| `reflect` | **Done** | value bag + clone/equal |
| `plugin` / `syscall` | skip | VISION |
| `embed` | **Done** | helper (not compile-time) |

Runtime: `mako_rt.h` + `mako_goext.h` (Waves 1–9). Tests: `goext_wave{,3,4,5,6,7,8,9}_test.mko`.

---

## Event Loop (`mako_evloop.h`)

Non-blocking I/O multiplexing (epoll/kqueue):

| Builtin | Role |
|---------|------|
| `evloop_new` / `evloop_close` | Create / destroy event loop |
| `evloop_add` / `evloop_mod` / `evloop_del` | Register / modify / remove fd |
| `evloop_wait(el, timeout_ms)` | Wait for events, returns count |
| `evloop_event_fd` / `evloop_event_flags` | Inspect ready events by index |
| `nb_listen` / `nb_accept` / `nb_read` / `nb_write` / `nb_close` | Non-blocking TCP helpers |
| `nb_udp_bind` / `nb_udp_recv` | Non-blocking UDP helpers |

---

## Game UDP (`mako_game.h`)

High-performance UDP networking for game servers:

| Builtin | Role |
|---------|------|
| `game_udp_bind` / `game_udp_close` | Bind / close game UDP socket |
| `game_udp_recv` / `game_udp_sender` | Receive packet, get sender peer ID |
| `game_udp_send` / `game_udp_broadcast` | Send to peer / broadcast to all |
| `game_udp_kick` / `game_udp_peers` | Disconnect peer / count peers |
| `game_udp_fd` | Raw fd for event loop integration |
| `tick_now_us` / `tick_sleep_us` | Microsecond tick timing |

---

## Cloud / Distributed (`mako_cloud.h`)

Primitives for distributed services:

| Builtin | Role |
|---------|------|
| `chash_new` / `chash_get` / `chash_add_node` / `chash_remove_node` / `chash_node_count` / `chash_free` | Consistent hash ring |
| `ratelimit_new` / `ratelimit_allow` / `ratelimit_remaining` / `ratelimit_free` | Token-bucket rate limiter |
| `breaker_new` / `breaker_allow` / `breaker_success` / `breaker_failure` / `breaker_state` / `breaker_reset` / `breaker_free` | Circuit breaker |

---

## HTTP Engine (`mako_httpengine.h`)

High-level HTTP server with declarative routing:

| Builtin | Role |
|---------|------|
| `httpengine_new` / `httpengine_free` | Create / destroy engine |
| `httpengine_route(e, method, path, handler_id)` | Register route |
| `httpengine_start(e, port)` | Start listening |
| `httpengine_stop(e)` | Stop engine |

---

## Core APIs (Wave 1–9)

| Builtin | Package |
|---------|---------|
| `flag_string` / `flag_int` / `flag_bool` | flag |
| `exec_output` / `exec_run` | os/exec |
| `url_scheme` / `host` / `path` / `query` / `query_escape` | net/url |
| `csv_split_line` / `csv_join_row` | encoding/csv |
| `xml_escape` / `xml_tag_text` / `html_escape` | encoding/xml · html |
| `gzip_compress` / `decompress` / `available` | compress/gzip |
| `tar_write_file` / `tar_first_name` | archive/tar |
| `mime_type` | mime |
| `context_with_timeout` / `expired` / `remaining` | context |
| `bytes_buffer*` | bytes |
| `rand_seed` / `rand_intn` / `rand_float` | math/rand |
| `template_execute` | text/template |
| `base32_encode` · `sha1` · `sha512` | encoding/base32 · crypto |
| `lookup_host` / `parse_ip_ok` / `dns_*` | net / DNS and address helpers |
| `signal_notify` / `signal_received` | os/signal |
| `atomic_*` | sync/atomic |
| `utf8_valid` / `utf8_rune_len` | unicode/utf8 |
| `filepath_walk` / `filepath_walk_n` | path/filepath |
| `slices_reverse` / `slices_unique` | slices |
| `embed_file` | embed helper |
| `zip_write_file` / `zip_first_name` / `zip_read_file` / `zip_deflate_available` | archive/zip |
| `png_*` / `gif_*` / `jpeg_*` | image |
| `maps_keys` / `values` / `clear` / `clone` / `equal` / `copy` | maps |
| `reflect_*` / `reflect_struct_*` | reflect |
| `httptest_serve_once` / `get` / `status` / `header` | testing/httptest |
| `aead_available` / `aes_gcm_*` / `chacha20_poly1305_*` | crypto |
| `multipart_boundary` / `multipart_form_value` / `multipart_file_*` | mime/multipart |
| `regex_find_all` / `replace*` / `valid` / `quote_meta` | regexp |
| `html_template_execute` / `execute2` / `execute3` / `if` | html/template |
| `gob_encode_*` / `gob_decode_*` / `gob_*_map_ss` | encoding/gob |
| `mail_parse_address` / `mail_header_get` / `mail_address_ok` | net/mail |
| `smtp_format_message` / `smtp_send_soft` | net/smtp |
| `binary_put_u*le` / `binary_u*le` | encoding/binary |
| `zip_create` / `zip_add` / `zip_write_to` / `zip_list` | archive/zip |
| `reflect_value_*` | reflect |
| `jpeg_encode_gray_dct` / `gif_encode_rgb_lzw` | image |
| `slog_*` | log/slog |
| `slog_redact` / `slog_with_redacted` | structured logging redaction controls |
| `metric_*` / `gauge_*` / `hist_*` / `metrics_export` | process-local observability metrics |
| `validate_required` / `validate_*_len` / `validate_int_range` / `validate_email` | backend request validation |
| `game_fixed_steps` / `game_fixed_remainder` / `game_alpha` / `game_frame_budget_ok` | fixed-timestep game-loop helpers |
| `fx_*` / `det_rng_*` / `replay_*` | deterministic simulation math, RNG, and replay streams |
| `frame_*` / `obj_*` / `alloc_*` / `leak_*` | game frame allocators, object pools, allocation tracking, scoped leak reporting |
| `ecs_*` | ECS seed: entities, components, queries, archetype masks, system updates |
| `ring_*` / `lfq_*` / `sg_*` | fixed-capacity rings, SPSC queue seed, scatter/gather string helpers |
| `fsm_rule` / `fsm_can` / `fsm_transition` / `fsm_is` | finite-state-machine helpers for session systems |
| `cookie_get` / `cookie_make` / `session_id_new` / `csrf_*` / `auth_*` / `authz_*` | cookies, sessions, CSRF, authentication, authorization (see Session/Auth section below) |
| `rate_allow` / `rate_remaining` / `cache_*` / `http_compress_if_accepted` | backend rate limiting, TTL cache, compression negotiation |
| `job_schedule` / `job_due` / `job_delay_ms` / `job_cancel` | background job scheduling primitives |
| `conn_pool_slot` / `conn_pool_next` / `lb_pick2` / `lb_pick3` | connection-pool slotting and load balancing |
| `openapi_route` / `openapi_doc` | OpenAPI 3.1 route and document generation |
| `graphql_field` / `graphql_arg` / `graphql_data` / `graphql_error` / `graphql_request` / `graphql_is_mutation` | GraphQL request parsing and response seed helpers |
| `sse_event` / `sse_retry` / `rpc_frame` / `rpc_method` / `rpc_payload` | SSE and streaming RPC wire helpers |
| `io_read_ready` / `io_write_ready` / `io_set_nonblocking` / `io_try_write` / `io_backoff_ms` / `io_should_pause` | backpressure-aware readiness, nonblocking write, and retry policy helpers |

---

## `dio` (Direct I/O)

Low-level unbuffered file operations and memory-mapped files (`runtime/mako_dio.h`):

| Builtin | Role |
|---------|------|
| `file_open` / `file_close` | open/close file descriptors |
| `pread` / `pwrite` | positional read/write (no seek side-effect) |
| `file_append` | append to fd |
| `fsync` / `fdatasync` | flush to disk (data+meta / data-only) |
| `fallocate` / `file_truncate` | pre-allocate / truncate |
| `file_size` / `file_seek` / `file_read_exact` | size, seek, exact read |
| `mmap_open` / `mmap_create` | map existing or new file |
| `mmap_read` / `mmap_write` / `mmap_sync` | read/write/flush mapping |
| `mmap_size` / `mmap_close` | size / unmap |

Tests: `examples/testing/dio_test.mko`. Header: `runtime/mako_dio.h`.

---

## `buf` (Binary Buffer)

Structured binary read/write for protocols and file formats (`runtime/mako_buf.h`):

| Builtin | Role |
|---------|------|
| `buf_pack_new(cap)` / `buf_from_string(s)` | create buffer |
| `buf_to_string(b)` | extract contents |
| `buf_len` / `buf_pos` / `buf_reset` / `buf_seek` / `buf_free` | navigation/lifecycle |
| `buf_write_u8/u16/u32/u64/i32/f32/f64` | write typed values (LE) |
| `buf_write_u16be/u32be` | write big-endian |
| `buf_read_u8/u16/u32/u64/i32/f32/f64` | read typed values (LE) |
| `buf_read_u16be/u32be` | read big-endian |
| `buf_write_bytes/str` / `buf_read_bytes/str` | raw byte/string I/O |

Tests: `examples/testing/buf_test.mko`. Header: `runtime/mako_buf.h`.

---

## `strings` / `bytes`

String manipulation and byte buffer utilities.

```mko
import "strings"
```

| Builtin | Role |
|---------|------|
| `str_len` / `str_eq` / `str_contains` | length, equality, substring |
| `str_has_prefix` / `str_has_suffix` | prefix/suffix |
| `str_index` / `str_last_index` | find (−1 if missing) |
| `str_trim` / `str_trim_space` / `str_trim_left` / `str_trim_right` | trim |
| `str_to_lower` / `str_to_upper` / `str_repeat` | case / repeat |
| `str_replace` | replace all |
| `str_split` / `str_fields` / `str_join` | split/join |
| `str_builder` + `builder_write*` / `builder_string` / `builder_len` | builder |
| `rune_count` | UTF-8 rune count |
| `as_bytes` / `bytes_as_str` / `bytes_view` / `bytes_is_view` | zero-copy views |
| `buf_get` / `buf_put` | process-local reusable byte buffers |

### Usage examples

```mko
fn main() {
    // Split and join
    let parts = str_split("alice,bob,carol", ",")
    print_int(len(parts))                           // 3
    let joined = str_join(parts, " | ")
    print(joined)                                    // alice | bob | carol

    // Search and check
    print_int(str_contains("hello mako", "mako"))    // 1
    print_int(str_has_prefix("/api/users", "/api"))   // 1
    print_int(str_index("abcdef", "cd"))              // 2

    // Trim and case
    let trimmed = str_trim_space("  hello  ")
    print(trimmed)                                   // hello
    print(str_to_upper("mako"))                      // MAKO

    // Builder for efficient concatenation
    let b = str_builder()
    builder_write(b, "hello")
    builder_write(b, " ")
    builder_write(b, "world")
    print(builder_string(b))                         // hello world

    // String <-> bytes
    let raw = bytes("mako")
    print_int(len(raw))                              // 4
    print(string(raw))                               // mako
}
```

---

## `strconv` / `fmt` / `print`

String conversion and formatted output.

```mko
import "strconv"
import "fmt"
```

| Builtin | Role |
|---------|------|
| `parse_int` → `Result[int,string]` | base-10 int |
| `parse_float` | float (0.0 on failure) |
| `parse_bool` → `Result[int,string]` | true/false/1/0 |
| `format_int` / `int_to_string` | int → string |
| `format_float(v, prec)` / `format_bool` | format |
| `fmt_sprintf` / `fmt_sprintf_d` | first `%s`/`%v` or `%d`/`%i` |
| `print` / `print_int*` / `print_float` | stdout |

### Usage examples

```mko
fn main() {
    // Parse strings to numbers
    match parse_int("42") {
        Ok(n) => print_int(n)               // 42
        Err(e) => print(e)
    }

    // parse_int returns Err on bad input
    match parse_int("not_a_number") {
        Ok(_) => print("unexpected")
        Err(e) => print(e)                  // parse error
    }

    // Format numbers to strings
    let s = int_to_string(100)
    print(s)                                // 100

    let f = format_float(3.14159, 2)
    print(f)                                // 3.14

    // Sprintf-style formatting
    let msg = fmt_sprintf("hello, %s!", "mako")
    print(msg)                              // hello, mako!

    let report = fmt_sprintf_d("processed %d items", 42)
    print(report)                           // processed 42 items
}
```

---

## `path` / `fs` / `io` / `os`

File system operations, path manipulation, environment, and process control.

```mko
import "path"
import "os"
```

| Builtin | Role |
|---------|------|
| `path_join` / `path_clean` / `path_base` / `path_dir` / `path_ext` / `path_is_abs` | path |
| `read_file` / `write_file` / `append_file` / `remove_file` | file I/O |
| `mkdir` / `file_exists` / `is_dir` / `read_dir` | FS |
| `getcwd` / `chdir` | working directory |
| `env_get` / `env_set` | environment |
| `argc` / `args` / `arg_get` | process args |
| `exit` | process exit |

Cross-platform: Win/Mac/Linux separators and dir APIs in `mako_stdlib.h` /
`mako_platform.h`.

### Usage examples

```mko
fn main() {
    // Path manipulation
    let p = path_join("/usr", "local/bin")
    print(p)                                    // /usr/local/bin
    print(path_base("/home/user/file.mko"))     // file.mko
    print(path_dir("/home/user/file.mko"))      // /home/user
    print(path_ext("archive.tar.gz"))           // .gz

    // File I/O
    let _ = write_file("/tmp/demo.txt", "hello mako\n")
    match read_file("/tmp/demo.txt") {
        Ok(data) => print(data)
        Err(e) => print(e)
    }
    let _ = append_file("/tmp/demo.txt", "second line\n")
    let _ = remove_file("/tmp/demo.txt")

    // File system queries
    print_int(file_exists("/tmp"))              // 1
    print_int(is_dir("/tmp"))                   // 1

    // Environment
    let home = env_get("HOME")
    print(home)

    // Working directory
    let cwd = getcwd()
    print(cwd)

    // Process arguments
    print_int(argc())
    print(arg_get(0))
}
```

---

## `bufio`

Buffered readers and writers for efficient I/O.

```mko
import "bufio"
```

| Builtin | Role |
|---------|------|
| `buf_reader_new(path)` / `buf_reader_from_string(s)` | buffered reader |
| `buf_read_line` / `buf_read(n)` | read line / up to n bytes |
| `buf_reader_close` | close |
| `buf_writer_new(path)` | buffered writer |
| `buf_write` / `buf_write_byte` / `buf_flush` | write |
| `buf_writer_close` | flush + close |

Tests: `examples/testing/bufio_test.mko`.

### Usage examples

```mko
fn main() {
    // Buffered writer: batches writes for efficiency
    let w = buf_writer_new("/tmp/bufio_demo.txt")
    buf_write(w, "line one\n")
    buf_write(w, "line two\n")
    buf_write(w, "line three\n")
    buf_flush(w)
    buf_writer_close(w)

    // Buffered reader: read line by line
    let r = buf_reader_new("/tmp/bufio_demo.txt")
    let l1 = buf_read_line(r)
    print(l1)                           // line one
    let l2 = buf_read_line(r)
    print(l2)                           // line two
    buf_reader_close(r)

    // Reader from a string (useful for parsing)
    let sr = buf_reader_from_string("hello\nworld\n")
    print(buf_read_line(sr))            // hello
    print(buf_read_line(sr))            // world

    let _ = remove_file("/tmp/bufio_demo.txt")
}
```

---

## `net` / `http`

TCP: `tcp_listen` / `tcp_accept` / `tcp_connect` / `tcp_write` / `tcp_close`,
plus session controls (`tcp_set_timeout`, `tcp_keepalive`, `tcp_nodelay`,
`tcp_listen_backlog` / `tcp_listen_reuseport`, buffer sizing, `tcp_accept4`).

### Upstream pool & reverse proxy

| Builtin | Role |
|---------|------|
| `tcp_pool_open` / `acquire` / `release` / `close` | Per host:port connection pool; nonblocking reuse check |
| `tcp_connect_nb` / `connect_check` / `connect_wait` | Nonblocking connect for slow/unhealthy backends |
| `tcp_fd_copy` / `tcp_splice` / `tcp_proxy_pump` | Efficient stream copy (Linux `splice` when available) |
| `http_forward` | Simple upstream forward → body only |
| `http_forward_full` / `http_forward_fd` | Status + body + headers (`HttpForwardResult`); chunked OK |
| `http_proxy_raw` | Raw request → backend → raw response → client |
| `http_parse` / `http_parsed_*` | C hot-path request parse (`HttpParsed`) |
| `http_decode_chunked` | Standalone chunked body decode |

Edge cases (duplicate Host/CL, incomplete chunked, 204/304, bad args) are
documented under **Reverse-proxy notes** in [BUILTINS.md](BUILTINS.md).
Tests: `examples/testing/proxy_pool_test.mko`, `proxy_edge_test.mko`.

### HTTP server

| Builtin | Role |
|---------|------|
| `http_bind` / `http_accept` | listen / accept |
| `http_method` / `http_path` / `http_body` / `http_header` | request |
| `http_req_method` / `http_req_path` / `http_req_body` | request helpers |
| `http_respond` / `http_respond_ct` / `http_respond_json` | response |
| `http_health_json` / `http_respond_health` | health/readiness JSON helpers |
| `http_next` / `http_keepalive` / `http_close*` / `http_shutdown_*` | lifecycle and graceful shutdown |
| `http_serve` / `http_echo` / `http_listen` | demos |
| `http_header_ok` | header validation |

### HTTP client

| Builtin | Role |
|---------|------|
| `http_get` / `http_post` / `http_request` | client |
| `http_get_timeout` / `http_post_timeout` | timeouts |
| `http_last_status` / `http_last_header` | last response |

HTTPS/H2/gRPC/WS: `tls_*`, `nghttp2_*`, `ws_*` (handshake, server/client frames, echo helpers).

UDP/Unix sockets: `udp_bind`, `udp_send_to`, `udp_recv`, `udp_local_port`,
`udp_close`, `unix_socket_pair`, `unix_socket_pair_peer`, `unix_write`,
`unix_read`, `unix_close`.

### Typed `HttpRequest`

| Builtin | Role |
|---------|------|
| `http_request_parse(raw)` | parse HTTP/1.1 request bytes → `HttpRequest` |
| `http_request_from_conn(conn)` | snapshot from accepted connection |
| `http_request_method` / `path` / `body` | accessors |
| `http_route_match(req, method, pattern)` | match `HttpRequest` against a method and Mako route pattern |
| `http_route_param(req, pattern, name)` | extract `{name}` from a matched route pattern |
| `router_new` / `router_group` / `router_add` / `router_match` / `router_param` / `router_count` | grouped router table and handler-name lookup |
| `reqctx_*` / `middleware_*` | per-request context store and middleware chain/policy helpers |

Same parse shape applies after TLS decrypt (feed plaintext to `http_request_parse`).
Route patterns use Mako's compact `{name}` segment capture form:
`/users/{user}/posts/{post}`.
Example: `examples/http_lib/request_type.mko` · test: `http_request_type_test.mko`.

Examples: `examples/http_lib/`, `examples/api_backend/`. Smoke: `./scripts/http-lib-smoke.sh`.

### HTTP server usage example

```mko
fn main() {
    let fd = http_bind(8080)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("listening on :8080")

    let mut n = 0
    while n < 3 {
        let c = http_accept(fd)
        if c < 0 {
            continue
        }
        let path = http_path(c)
        if str_eq(path, "/health") {
            let _ = http_respond_json(c, 200, "{\"ok\":true}\n")
        } else {
            let _ = http_respond(c, 404, "not found\n")
        }
        let _ = http_close(c)
        n = n + 1
    }
    let _ = http_close_listener(fd)
}
```

### HTTP client usage example

```mko
fn main() {
    let body = http_get("http://example.com/api")
    print(body)
    let status = http_last_status()
    print_int(status)

    // POST with timeout
    let resp = http_post_timeout("http://example.com/data",
        "{\"key\":\"value\"}", 5000)
    print(resp)
}
```

### URL parsing

```mko
fn main() {
    let raw = "https://example.com:8080/path?q=mako&page=1"
    print(url_scheme(raw))          // https
    print(url_host(raw))            // example.com:8080
    print(url_path(raw))            // /path
    print(url_query(raw))           // q=mako&page=1

    let encoded = query_escape("hello world & more")
    print(encoded)                  // hello+world+%26+more
}
```

---

## `encoding/json` · `base64` · `hex`

Data encoding and decoding in multiple formats.

```mko
import "encoding/json"
import "encoding/base64"
```

| Builtin | Role |
|---------|------|
| `json_*` (object/array/path/merge/…) | encode/decode into arenas |
| `#[derive(json)]` | compile-time struct marshal/unmarshal codegen for scalar fields |
| `yaml_get_string` / `toml_get_*` | config extraction seeds |
| `msgpack_int_hex` / `cbor_int_hex` / `avro_long_hex` | binary format integer encoding seeds |
| `base64_encode` / `base64_decode` | base64 |
| `hex_encode` / `hex_decode` | hex |

### JSON usage examples

```mko
fn main() {
    // Build a JSON object
    let obj = json_new()
    let obj = json_set_string(obj, "name", "Ada")
    let obj = json_set_int(obj, "age", 36)
    print(obj)                              // {"name":"Ada","age":36}

    // Read fields back
    let name = json_get_string(obj, "name")
    let age = json_get_int(obj, "age")
    print(name)                             // Ada
    print_int(age)                          // 36

    // derive(json) for structs (see EXAMPLES.md for full example)
    // #[derive(json)]
    // struct Config { host: string, port: int }
    // let j = Config_to_json("localhost", 8080)
}
```

### Base64 and hex usage examples

```mko
fn main() {
    // Base64 encode/decode
    let encoded = base64_encode("hello mako")
    print(encoded)                          // aGVsbG8gbWFrbw==
    let decoded = base64_decode(encoded)
    print(decoded)                          // hello mako

    // Hex encode/decode
    let h = hex_encode("AB")
    print(h)                                // 4142
    let raw = hex_decode(h)
    print(raw)                              // AB
}
```

### CSV usage examples

```mko
fn main() {
    // Split a CSV line into fields
    let fields = csv_split_line("name,age,city")
    print(fields[0])                        // name
    print(fields[1])                        // age

    // Join fields into a CSV row
    let row = csv_join_row(["Alice", "30", "NYC"])
    print(row)                              // Alice,30,NYC
}
```

### XML and HTML escaping

```mko
fn main() {
    let safe = xml_escape("<script>alert('xss')</script>")
    print(safe)         // &lt;script&gt;alert('xss')&lt;/script&gt;

    let html_safe = html_escape("5 > 3 & 2 < 4")
    print(html_safe)    // 5 &gt; 3 &amp; 2 &lt; 4
}
```

---

## `crypto`

Cryptographic hashing, random number generation, and secure comparison.

```mko
import "crypto"
```

| Builtin | Role |
|---------|------|
| `sha256` / `hmac_sha256` | digests (hex) |
| `random_bytes` / `random_int` | CSPRNG |
| `const_eq` / `crypto_eq` | timing-safe compare |
| `secret_from_str` / `secret_drop` | zero-on-drop |

### Usage examples

```mko
fn main() {
    // SHA-256 hash (returns hex string)
    let hash = sha256("hello mako")
    print(hash)                         // 64-char hex digest

    // HMAC-SHA256 for message authentication
    let mac = hmac_sha256("secret-key", "message data")
    print(mac)

    // Cryptographically secure random bytes
    let token = random_bytes(16)
    print(hex_encode(token))            // 32-char random hex

    // Timing-safe comparison (prevents timing attacks)
    let a = sha256("password1")
    let b = sha256("password1")
    print_int(crypto_eq(a, b))          // 1

    // Secure secret handling
    let s = secret_from_str("my-api-key")
    // ... use s ...
    secret_drop(s)                      // zeroes memory
}
```

---

## `time`

Clocks, timestamps, sleeping, and time formatting.

```mko
import "time"
```

| Builtin | Role |
|---------|------|
| `now_ms` / `now_ns` / `time_unix` | clocks |
| `sleep_ms` / `time_sleep_ms` | sleep |
| `elapsed_ms` | delta from start ms |
| `time_format` | RFC3339 UTC from unix ms |

### Usage examples

```mko
fn main() {
    // Current timestamp
    let start = now_ms()
    print_int(start)                    // e.g. 1720612345678

    // Unix seconds
    let unix = time_unix()
    print_int(unix)                     // e.g. 1720612345

    // RFC 3339 formatting
    let formatted = time_format(start)
    print(formatted)                    // e.g. 2026-07-10T12:34:05Z

    // Measure elapsed time
    sleep_ms(100)
    let elapsed = elapsed_ms(start)
    print_int(elapsed)                  // ~100

    // High-precision nanosecond clock (for benchmarks)
    let t0 = now_ns()
    // ... do work ...
    let t1 = now_ns()
    print_int(t1 - t0)                 // nanoseconds elapsed
}
```

---

## `sync` / `conc`

Concurrency primitives: mutexes, wait groups, channels, crews, and actors.

```mko
import "sync"
```

| Builtin | Role |
|---------|------|
| `mutex_new` / `mutex_lock` / `mutex_unlock` | mutex |
| `rwmutex_new` / `rwmutex_rlock` / `runlock` / `lock` / `unlock` | RWMutex |
| `wait_group_new` / `wait_group_add` / `done` / `wait` | WaitGroup |
| `cmap_new` / `cmap_set` / `cmap_get` / `cmap_has` / `cmap_del` / `cmap_len` / `cmap_incr` | CMap (concurrent hashmap with lock-free reads) |
| `chan_new` / `chan_try_send` / `chan_len` / `chan_cap` / `chan_select*` | bounded channels and backpressure |
| `runtime_stats_json` / `runtime_stats_reset` | runtime scheduler/channel introspection |
| `crew` / `go` / cancel policy | structured concurrency |
| `actor_spawn` / `actor_send` / `actor_recv` / `actor_stop` | actors |
| `actor` / `receive` syntax | desugar (see GUIDE) |

### Mutex usage example

```mko
fn main() {
    let m = mutex_new()
    let mut counter = 0

    mutex_lock(m)
    counter = counter + 1
    mutex_unlock(m)

    print_int(counter)                  // 1
}
```

### Channel and crew usage example

```mko
fn worker(ch: chan[int], id: int) -> int {
    let _ = ch.send(id * 10)
    return id
}

fn main() {
    let ch = chan_new(4)

    crew t {
        let w1 = t.kick(worker(ch, 1))
        let w2 = t.kick(worker(ch, 2))
        let _ = w1.join()
        let _ = w2.join()
    }

    print_int(ch.recv())                // 10
    print_int(ch.recv())                // 20
}
```

### WaitGroup usage example

```mko
fn main() {
    let wg = wait_group_new()
    wait_group_add(wg, 3)

    // Simulate 3 tasks finishing
    done(wg)
    done(wg)
    done(wg)

    wait(wg)                            // blocks until count reaches 0
    print("all done")
}
```

### Concurrent map usage example

```mko
fn main() {
    let m = cmap_new()
    cmap_set(m, "hits", "0")
    cmap_incr(m, "hits")
    cmap_incr(m, "hits")
    print(cmap_get(m, "hits"))          // 2
    print_int(cmap_len(m))              // 1
    print_int(cmap_has(m, "hits"))      // 1
}
```

---

## `errors` · `testing` · `regexp` · `log` · `math` · `collections`

| Area | Builtins |
|------|----------|
| errors | `error` / `errorf` / `wrap_err` / `error_is` / `error_string` / `?` |
| testing | `mako test`, `assert` / `assert_eq` / `assert_eq_str`, `t_run`, `--race` / `--sanitize` |
| regexp | `regex_match` / `regex_find` / `regex_capture` |
| log | `log_info` / `log_warn` / `log_error` / `log_debug` / `log_kv` |
| math | `abs` / `min` / `max` / `clamp`, `math_sqrt` / `pow` / `floor` / `ceil` / `sin` / `cos` / `log` / `exp` / `math_abs` |
| collections | `sort_ints` / `sort_strings`, `ints_contains` / `strings_contains`, `ints_copy` / `ints_index` |

### Errors usage examples

```mko
fn load_config(path: string) -> Result[string, string] {
    if str_eq(path, "") {
        return error("empty path")
    }
    Ok("config_data")
}

fn main() {
    // The ? operator propagates errors up the call chain
    // wrap_err adds context
    let r = load_config("")
    let r = wrap_err(r, "startup")
    match r {
        Ok(data) => print(data)
        Err(e) => {
            print(error_string(r))          // startup: empty path
            print_int(error_is(r, "empty")) // 1
        }
    }
}
```

### Regexp usage examples

```mko
fn main() {
    // Match: does the string match the pattern?
    print_int(regex_match("hello123", "\\d+"))       // 1

    // Find: extract the first match
    let found = regex_find("order-42-confirmed", "\\d+")
    print(found)                                      // 42

    // Capture: extract groups
    let caps = regex_capture("2026-07-10", "(\\d{4})-(\\d{2})-(\\d{2})")
    print(caps[1])                                    // 2026
    print(caps[2])                                    // 07
    print(caps[3])                                    // 10

    // Find all matches
    let all = regex_find_all("a1b2c3", "\\d")
    print_int(len(all))                               // 3
}
```

### Log usage examples

```mko
fn main() {
    log_info("server started")
    log_warn("disk usage high")
    log_error("connection refused")
    log_debug("request payload received")

    // Structured key-value logging
    log_kv("request", "method", "GET", "path", "/api/users")
}
```

### Math usage examples

```mko
fn main() {
    print_int(abs(-42))                     // 42
    print_int(min(3, 7))                    // 3
    print_int(max(3, 7))                    // 7
    print_int(clamp(15, 0, 10))             // 10

    print_float(math_sqrt(144.0))           // 12.0
    print_float(pow(2.0, 10.0))             // 1024.0
    print_float(floor(3.7))                 // 3.0
    print_float(ceil(3.2))                  // 4.0
}
```

### Collections (slices / maps) usage examples

```mko
fn main() {
    // Sort
    let mut nums = [5, 3, 1, 4, 2]
    sort_ints(nums)
    // nums is now [1, 2, 3, 4, 5]
    print_int(nums[0])                      // 1

    let mut names = ["carol", "alice", "bob"]
    sort_strings(names)
    print(names[0])                         // alice

    // Search
    print_int(ints_contains([10, 20, 30], 20))      // 1
    print_int(strings_contains(["a", "b"], "c"))     // 0
    print_int(ints_index([10, 20, 30], 20))          // 1

    // Slice utilities
    let mut src = [1, 2, 3]
    let dst = ints_copy(src)
    print_int(len(dst))                     // 3

    // Reverse and unique
    let mut vals = [3, 1, 2, 1, 3]
    slices_reverse(vals)
    // vals is now [3, 1, 2, 1, 3] reversed

    // Maps
    let keys = maps_keys(m)
    let vals = maps_values(m)
}
```

### Testing usage examples

```mko
// In a *_test.mko file:
fn TestExample() {
    assert(true)
    assert_eq(2 + 2, 4)
    assert_eq_str("hello", "hello")
}

fn TestSubtests() {
    t_run("case one", fn() {
        assert_eq(1 + 1, 2)
    })
    t_run("case two", fn() {
        assert_eq(2 * 3, 6)
    })
}
```

Run tests:

```bash
mako test .                     # run all tests
mako test . -v                  # verbose
mako test . -run TestExample    # run one test
mako test . --race              # with thread sanitizer
```

### Context usage example

```mko
fn main() {
    // Create a context with a 5-second timeout
    let ctx = context_with_timeout(5000)

    // Check if expired
    if expired(ctx) == 0 {
        print("still active")
    }

    // Check remaining time
    let ms = remaining(ctx)
    print_int(ms)                   // ~5000
}
```

### Reflect usage example

```mko
fn main() {
    // Reflect provides runtime type inspection
    let v = reflect_value_of(42)
    let cloned = reflect_clone(v)
    print_int(reflect_equal(v, cloned))     // 1
}
```

### Image usage example

```mko
fn main() {
    // PNG encoding
    let data = png_encode_gray(pixels, width, height)
    let _ = write_file("/tmp/output.png", data)

    // JPEG encoding with DCT
    let jpg = jpeg_encode_gray_dct(pixels, width, height)
    let _ = write_file("/tmp/output.jpg", jpg)
}
```

### Archive usage examples

```mko
fn main() {
    // Create a zip file
    let z = zip_create()
    zip_add(z, "hello.txt", "hello from mako\n")
    zip_add(z, "data.txt", "some data\n")
    zip_write_to(z, "/tmp/demo.zip")

    // List files in a zip
    let names = zip_list("/tmp/demo.zip")
    print(names[0])                         // hello.txt

    // Read a file from a zip
    let content = zip_read_file("/tmp/demo.zip", "hello.txt")
    print(content)                          // hello from mako

    let _ = remove_file("/tmp/demo.zip")
}
```

### Flag (CLI argument parsing) usage example

```mko
fn main() {
    let host = flag_string("host", "localhost", "server hostname")
    let port = flag_int("port", 8080, "server port")
    let verbose = flag_bool("verbose", false, "enable verbose output")

    print(host)
    print_int(port)
}
```

Run: `mako run server.mko -- --host 0.0.0.0 --port 9090 --verbose`

### Unicode/UTF-8 usage example

```mko
fn main() {
    print_int(utf8_valid("hello"))          // 1
    print_int(utf8_rune_len("hello"))       // 5 (ASCII: 1 byte per rune)
    print_int(rune_count("hello"))          // 5
}
```

---

## `database/sql` -- unified facade

Same call shape for sqlite and postgres (`?` placeholders; postgres rewritten to `$N`):

| Builtin | Role |
|---------|------|
| `sql_open_sqlite(path)` / `sql_open_postgres(url)` | open → `SqlDB` |
| `sql_ok` / `sql_close` | status / close |
| `sql_query_int(db, sql, []int)` | parameterized query → int |
| `sql_exec(db, sql, []int)` | parameterized exec (integer params) |
| `sql_exec_plain(db, sql)` | exec with no parameters (DDL, simple statements); returns 0 on success |
| `sql_exec_str4(db, sql, p1, p2, p3, p4)` | exec with up to 4 string parameters ($1..$4); returns 0 on success |
| `sql_query_str(db, sql, p1)` | query single string value (first col, first row) with one string param; returns "" if empty |
| `sql_begin` / `sql_commit` / `sql_rollback` | transaction boundaries |
| `sql_prepare` / `sql_stmt_query_int` / `sql_stmt_exec` / `sql_stmt_close` | prepared statement handles |
| `sql_migration_applied` / `sql_migrate` | numeric-version migration table and transactional apply |
| `sql_check_typed(schema, sql, params, result)` | static table/column/param/nullability/result checker |
| `sql_pool_open_*` / `sql_pool_query_int` / `sql_pool_exec` / `sql_pool_close` | bounded lazy connection pools with round-robin slots |
| `mysql_connect_url` / `mysql_ok` / `mysql_is_mariadb` / `mysql_driver_name` | MySQL/MariaDB DSN validation and driver metadata |
| `redis_connect_url` / `redis_connect` / `redis_conn_*` / `redis_close` | Redis URL parsing and reusable RESP connection handles |
| `mongo_connect_url` / `mongo_find_one_request` | MongoDB-compatible URL parsing and find-one command request construction |
| `cassandra_connect_url` / `cassandra_select` | Cassandra-compatible URL parsing and CQL select construction |
| `clickhouse_connect_url` / `clickhouse_select` | ClickHouse-compatible URL parsing and SQL select construction |
| `elastic_connect_url` / `elastic_search_request` | Elasticsearch-compatible URL parsing and search request construction |

Legacy drivers still available: `sqlite_query_*`, `pg_connect` / `pg_exec*`.
Parameterized queries only for untrusted input -- [SECURITY.md](SECURITY.md).
Tests: `examples/testing/sql_unify_test.mko`, `sql_pool_test.mko`,
`sql_tx_stmt_test.mko`, `sql_migration_test.mko`, `sql_typed_check_test.mko`,
`mysql_redis_polish_test.mko`, `multistore_compat_test.mko`,
`derive_json_codegen_test.mko`.

### SQLite usage example

```mko
fn main() {
    let db = sql_open_sqlite("/tmp/app.db")
    if sql_ok(db) == 0 {
        print("open failed")
        return
    }
    defer sql_close(db)

    // Create a table
    let _ = sql_exec_plain(db,
        "CREATE TABLE IF NOT EXISTS notes (id INTEGER PRIMARY KEY, text TEXT)")

    // Insert with parameterized query
    let _ = sql_exec_str4(db,
        "INSERT INTO notes (text) VALUES ($1)", "buy groceries", "", "", "")

    // Query
    let count = sql_query_int(db, "SELECT COUNT(*) FROM notes", [])
    print_int(count)                    // 1

    let text = sql_query_str(db, "SELECT text FROM notes WHERE id = $1", "1")
    print(text)                         // buy groceries

    // Transactions
    sql_begin(db)
    let _ = sql_exec_str4(db,
        "INSERT INTO notes (text) VALUES ($1)", "walk the dog", "", "", "")
    sql_commit(db)

    let _ = remove_file("/tmp/app.db")
}
```

### Prepared statements

```mko
fn main() {
    let db = sql_open_sqlite("/tmp/prep.db")
    defer sql_close(db)

    let _ = sql_exec_plain(db, "CREATE TABLE kv (k TEXT, v INTEGER)")

    // Prepare once, execute many times
    let stmt = sql_prepare(db, "INSERT INTO kv (k, v) VALUES (?, ?)")
    let _ = sql_stmt_exec(stmt, [1])    // execute with params
    sql_stmt_close(stmt)

    let _ = remove_file("/tmp/prep.db")
}
```

### Migrations

```mko
fn main() {
    let db = sql_open_sqlite("/tmp/migrate.db")
    defer sql_close(db)

    // Apply migration version 1 if not already applied
    if sql_migration_applied(db, 1) == 0 {
        sql_migrate(db, 1,
            "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)")
    }

    print("migration done")
    let _ = remove_file("/tmp/migrate.db")
}
```

---

## Local packages (`mako.toml` / `mako pkg`)

```bash
mako pkg init
mako pkg add util path=../util
mako pkg publish          # → .mako/registry (or $MAKO_REGISTRY)
mako pkg install         # resolve SemVer + write mako.lock
mako pkg update
```

See [howto/04-packages.md](howto/04-packages.md).

---

## Performance principles

1. **No mandatory GC** — backends stay predictable; optional GC is app opt-in only.
2. **Arena per request** — allocate freely; free the region once.
3. **Dense values** — less pointer chasing.
4. **Explicit buffers** — fewer hidden copies (`bytes_view`, pools).
5. **Crew concurrency** — no orphan tasks; cancel/timeouts.
6. **Future I/O** — io_uring / kqueue, pooling, HTTP/2–3 depth.

---

## Known gaps (honest)

| Area | Mako today |
|------|------------|
| `strings` package import | Done — `import "strings"` → `std/strings/` (also path, fmt, sync, …) |
| `bufio.Reader/Writer` | Done |
| `net/http.Request` typed | Done (`HttpRequest`) |
| `database/sql` one API | Done (`sql_*`; legacy sqlite_/pg_ remain) |
| Full `regexp` engine | RE2-ish (`\d\w\s`, `{n,m}`, `\b`, find_all/replace); not full RE2/PCRE |
| `sync.WaitGroup` / RWMutex / atomic | Done |
| Generics collections | slices + maps helpers Done; `List<T>` Later |
| zip multi-file · png/gif/jpeg · reflect · httptest · gob/mail/smtp/slog/binary | Done (area-level; not every symbol) |
| Full stdlib symbol parity | **Not claimed** (~98% major *areas*; not every symbol) |

---

## Session Management, Authentication & Authorization

Cookies, sessions, CSRF, auth, signed tokens, and RBAC (`runtime/mako_security.h`).

### Cookies

| Builtin | Purpose |
|---------|---------|
| `cookie_get(header, name) -> string` | Parse a cookie value from a `Cookie:` header |
| `cookie_make(name, value, max_age) -> string` | Create a `Set-Cookie` header (HttpOnly, SameSite=Lax, Path=/) |

### Sessions

| Builtin | Purpose |
|---------|---------|
| `session_id_new() -> string` | Generate a 32-char random hex session ID (16 bytes of cryptographic randomness) |
| `auth_session_cookie(cookie_header, cookie_name, expected) -> int` | Constant-time session cookie check (1=match, 0=no) |

### CSRF

| Builtin | Purpose |
|---------|---------|
| `csrf_token() -> string` | Generate a random CSRF token |
| `csrf_check(expected, submitted) -> int` | Constant-time comparison (1=match, 0=no) |

### Authentication

| Builtin | Purpose |
|---------|---------|
| `auth_bearer(authorization) -> string` | Extract token from "Bearer \<token\>" header |
| `auth_check_bearer(authorization, expected_token) -> int` | Constant-time bearer token verification |
| `auth_basic_header(user, pass) -> string` | Build a "Basic \<base64\>" authorization header |
| `auth_check_basic(authorization, user, pass) -> int` | Verify Basic auth credentials |

### Signed Tokens (HMAC-SHA256)

| Builtin | Purpose |
|---------|---------|
| `auth_token_sign(subject, secret) -> string` | Sign a subject, returns "subject.hmac_signature" |
| `auth_token_check(token, secret) -> int` | Verify a signed token (1=valid, 0=invalid) |
| `auth_token_subject(token) -> string` | Extract subject from "subject.signature" token |

### Role-Based Access Control

| Builtin | Purpose |
|---------|---------|
| `auth_role_has(roles_csv, role) -> int` | Check if a CSV roles string contains a role |
| `authz_allow_role(user_roles_csv, required_roles_csv) -> int` | Check if user has any required role |

---

## Security

TLS for production listeners, parameterized DB, no ignored `Result`s —
[SECURITY.md](SECURITY.md).
