# Building a Backend API

This tutorial builds a complete REST API with routing, JSON, validation,
in-memory storage, and health checks.

```bash
mako init notes-api --backend
cd notes-api
mako run main.mko
```

---

## Minimal Server

Every Mako HTTP server starts with `http_bind`, loops on `http_accept`,
inspects the request, and replies with `http_respond` or `http_respond_json`.

```mko
fn main() {
    let fd = http_bind(18300)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("listening on :18300")

    while 1 == 1 {
        let c = http_accept(fd)
        if c < 0 {
            // accept error — skip
        } else {
            let _ = http_respond(c, 200, "hello from mako\n")
            let _ = http_close(c)
        }
    }
}
```

Build and test:

```bash
mako build main.mko -o out/server
out/server &
curl -sS http://127.0.0.1:18300/
```

---

## Adding Routes

Use `http_path` and `http_method` to dispatch requests. String `==`
works for comparison in routing logic.

```mko
fn route(c: int) {
    let method = http_method(c)
    let path = http_path(c)

    if path == "/health" {
        let _ = http_respond_json(c, 200, "{\"ok\":true}\n")
    } else {
        if path == "/" {
            let _ = http_respond(c, 200, "notes-api v1\n")
        } else {
            let _ = http_respond_json(c, 404, json_object("error", "not found"))
        }
    }
}
```

---

## JSON Helpers

Mako provides several built-in JSON construction functions:

```mko
// Single key-value object
let obj = json_object("status", "ok")
// {"status":"ok"}

// Two key-value pairs (string values)
let resp = json_ss("status", "created", "title", "my-note")
// {"status":"created","title":"my-note"}

// Build from a map
let mut m = make(map[string]string)
m["name"] = "Ada"
m["role"] = "engineer"
let doc = json_object_from_map_ss(m)

// Extract fields from JSON strings
let name = json_get_string(doc, "name")
let count = json_get_int(some_json, "count")
```

---

## Structured JSON with derive

For structured data, use `#[derive(json)]` on a struct and attach
methods with `on`:

```mko
#[derive(json)]
struct Note {
    title: string
    body: string
}

on Note {
    fn to_json(self) -> string {
        return Note_to_json(self.title, self.body)
    }

    fn display(self) {
        print(self.title)
        print(self.body)
    }
}

fn main() {
    let j = Note_to_json("hello", "world")
    print(j)
    let t = Note_title_from_json(j)
    let b = Note_body_from_json(j)
    print(t)
    print(b)
}
```

---

## In-Memory Storage with CMap

`CMap` is a concurrent hashmap safe for use across `crew` tasks. For a
single-threaded server a regular `map` works fine, but `CMap` scales if
you later add concurrency.

```mko
let store = cmap_new()

// Store a note
cmap_set(store, "my-key", "my-value")

// Retrieve
let val = cmap_get(store, "my-key")

// Check existence
if cmap_has(store, "my-key") == 1 {
    print("found")
}

// Delete
let _ = cmap_del(store, "my-key")

// Count entries
print(cmap_len(store))
```

---

## Input Validation

Always validate incoming data before storing it. Use `json_get_string`
to extract fields and check for empty strings. Return a `400` status
with a JSON error body when validation fails. The complete example
below demonstrates this pattern.

---

## Complete Runnable Example

This is a full notes API with GET, POST, and DELETE support, health
checks, validation, and structured JSON responses.

```mko
fn main() {
    let mut max = 100
    if argc() > 1 {
        match parse_int(arg_get(1)) {
            Ok(v) => {
                if v > 0 {
                    max = v
                }
            }
            Err(_) => {}
        }
    }

    let fd = http_bind(18300)
    if fd < 0 {
        print("bind failed")
        return
    }
    print("notes-api on :18300")

    let mut notes = make(map[string]string)
    let mut n = 0

    while n < max {
        let c = http_accept(fd)
        if c < 0 {
            // skip
        } else {
            let method = http_method(c)
            let path = http_path(c)

            if path == "/health" {
                let _ = http_respond_json(
                    c, 200,
                    "{\"ok\":true,\"service\":\"notes-api\"}\n"
                )
            } else {
                if path == "/v1/notes" {
                    if method == "GET" {
                        let body = json_object_from_map_ss(notes)
                        let _ = http_respond_json(c, 200, body)
                    } else {
                        if method == "POST" {
                            let raw = http_body(c)
                            let title = json_get_string(raw, "title")
                            let body = json_get_string(raw, "body")
                            if title == "" {
                                let _ = http_respond_json(
                                    c, 400,
                                    json_object("error", "title required")
                                )
                            } else {
                                if body == "" {
                                    let _ = http_respond_json(
                                        c, 400,
                                        json_object("error", "body required")
                                    )
                                } else {
                                    notes[title] = body
                                    let resp = json_ss(
                                        "status", "created",
                                        "title", title
                                    )
                                    let _ = http_respond_json(c, 201, resp)
                                }
                            }
                        } else {
                            if method == "DELETE" {
                                let key = http_header(c, "X-Note-Key")
                                if key == "" {
                                    let _ = http_respond_json(
                                        c, 400,
                                        json_object("error", "X-Note-Key header required")
                                    )
                                } else {
                                    if has(notes, key) {
                                        delete(notes, key)
                                        let _ = http_respond_json(
                                            c, 200,
                                            json_object("status", "deleted")
                                        )
                                    } else {
                                        let _ = http_respond_json(
                                            c, 404,
                                            json_object("error", "note not found")
                                        )
                                    }
                                }
                            } else {
                                let _ = http_respond_json(
                                    c, 405,
                                    json_object("error", "method not allowed")
                                )
                            }
                        }
                    }
                } else {
                    let _ = http_respond_json(
                        c, 404,
                        json_object("error", "not found")
                    )
                }
            }
            let _ = http_close(c)
            n = n + 1
        }
    }
    let _ = http_close_listener(fd)
    print("notes-api done")
}
```

---

## Testing with curl

```bash
mako build main.mko -o out/notes-api
out/notes-api 20 &
curl -sS http://127.0.0.1:18300/health
curl -sS -X POST -H 'Content-Type: application/json' \
    -d '{"title":"hello","body":"world"}' http://127.0.0.1:18300/v1/notes
curl -sS http://127.0.0.1:18300/v1/notes
curl -sS -X DELETE -H 'X-Note-Key: hello' http://127.0.0.1:18300/v1/notes
```

---

## Key Takeaways

- `http_bind` / `http_accept` / `http_respond_json` / `http_close` form the
  server lifecycle
- `http_method`, `http_path`, `http_header`, `http_body` give access to
  request data
- `json_object`, `json_ss`, `json_object_from_map_ss` build JSON responses
- `json_get_string` and `json_get_int` parse incoming JSON
- Use `map[string]string` or `CMap` for in-memory storage
- Always validate input and return structured error responses
- `#[derive(json)]` generates serialization for structs
