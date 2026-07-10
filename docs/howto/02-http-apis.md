# Building HTTP JSON APIs

This guide builds a complete JSON API server with routing, request parsing, and
response handling. You will also make client requests to test it.

## Minimal server

A Mako HTTP server uses four steps: bind, accept, respond, close.

```mko
fn main() {
    let fd = http_bind(8080)
    let mut n = 0
    while n < 100 {
        let c = http_accept(fd)
        if c >= 0 {
            let _ = http_respond(c, 200, "hello from mako\n")
            let _ = http_close(c)
            n = n + 1
        }
    }
    let _ = http_close_listener(fd)
}
```

Build and run:

```bash
mako build main.mko -o server
./server &
curl http://127.0.0.1:8080/
# hello from mako
kill %1
```

## Adding routes

Use `http_path` and `http_method` to dispatch requests:

```mko
fn main() {
    let fd = http_bind(8080)
    let mut running = true
    let mut count = 0
    while running {
        let c = http_accept(fd)
        if c < 0 { continue }

        let method = http_method(c)
        let path = http_path(c)

        if str_eq(path, "/health") {
            let _ = http_respond_json(c, 200, "{\"ok\":true}\n")
        } else if str_eq(path, "/echo") {
            let body = http_body(c)
            let _ = http_respond_ct(c, 200, "text/plain", body)
        } else {
            let _ = http_respond(c, 404, "not found\n")
        }

        let _ = http_close(c)
        count = count + 1
        if count >= 50 {
            running = false
        }
    }
    let _ = http_close_listener(fd)
}
```

## JSON responses with derive

Use `#[derive(json)]` to generate serializers for your types:

```mko
#[derive(json)]
struct User {
    name: string
    age: int
}

fn handle_user(c: int) {
    let json = User_to_json("Ada", 36)
    let _ = http_respond_json(c, 200, json)
}
```

For manual JSON construction:

```mko
fn health_json() -> string {
    return json_object(json_si("status", "ok") + "," + json_i("uptime", 42))
}
```

## Reading request headers and body

```mko
let c = http_accept(fd)
let content_type = http_header(c, "Content-Type")
let auth = http_header(c, "Authorization")
let body = http_body(c)
```

## Keep-alive connections

For clients that reuse connections, call `http_next` instead of closing:

```mko
let c = http_accept(fd)
let _ = http_respond(c, 200, "first\n")
let ok = http_next(c)
if ok > 0 {
    let _ = http_respond(c, 200, "second\n")
}
let _ = http_close(c)
```

## HTTP client

Make outgoing requests from your program:

```mko
fn main() {
    let body = http_get("http://127.0.0.1:8080/health")
    let status = http_last_status()
    print_int(status)
    print(body)

    let resp = http_post("http://127.0.0.1:8080/echo", "ping")
    print(resp)

    // With timeout (milliseconds)
    let data = http_get_timeout("http://example.com/api", 3000)
}
```

## Complete working example

Save as `api.mko`:

```mko
#[derive(json)]
struct Status {
    ok: bool
    version: string
}

fn main() {
    let fd = http_bind(18100)
    print("listening on :18100")
    let mut n = 0
    while n < 20 {
        let c = http_accept(fd)
        if c < 0 { continue }

        let path = http_path(c)
        if str_eq(path, "/health") {
            let _ = http_respond_json(c, 200, "{\"ok\":true,\"version\":\"0.1.0\"}\n")
        } else if str_eq(path, "/echo") {
            let body = http_body(c)
            let _ = http_respond_ct(c, 200, "application/json", body)
        } else {
            let _ = http_respond(c, 404, "{\"error\":\"not found\"}\n")
        }
        let _ = http_close(c)
        n = n + 1
    }
    let _ = http_close_listener(fd)
}
```

Test it:

```bash
mako run api.mko &
curl -s http://127.0.0.1:18100/health | cat
# {"ok":true,"version":"0.1.0"}

curl -s -X POST -d '{"msg":"hi"}' http://127.0.0.1:18100/echo
# {"msg":"hi"}

curl -s http://127.0.0.1:18100/unknown
# {"error":"not found"}
```

## API reference

| Function | Purpose |
|----------|---------|
| `http_bind(port)` | Start listening, returns fd |
| `http_accept(fd)` | Accept and parse one request, returns conn id |
| `http_method(c)` | Request method string |
| `http_path(c)` | Request path string |
| `http_body(c)` | Request body string |
| `http_header(c, name)` | Get a request header value |
| `http_respond(c, status, body)` | Send plain text response |
| `http_respond_ct(c, status, ct, body)` | Send response with Content-Type |
| `http_respond_json(c, status, json)` | Send JSON response |
| `http_close(c)` | Close connection |
| `http_close_listener(fd)` | Stop listening |
| `http_get(url)` | GET request, returns body |
| `http_post(url, body)` | POST request, returns body |
| `http_last_status()` | Status code of last client call |
| `http_last_header(name)` | Response header of last client call |

## Next steps

- [Handle errors in your API](03-errors-debugging.md)
- [HTTPS and HTTP/2](../GUIDE.md) (see Networking section)
- Scaffold a full service: `mako init mysvc --backend`
