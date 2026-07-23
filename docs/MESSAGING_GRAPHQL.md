# Messaging queues · GraphQL

**Product tip:** 0.4.13 · **No GC** — payloads and responses are owned strings;
queues free on `mq_free` / `mq_purge` / take transfer.

Related: [STDLIB.md](STDLIB.md) · [MEMORY_SAFETY.md](MEMORY_SAFETY.md) ·
[LANGUAGE.md](LANGUAGE.md) · [GENERAL_PURPOSE_PLAN.md](GENERAL_PURPOSE_PLAN.md) wave 2.

---

## Language surface (first-class)

### `queue[T]` (seed: `queue[string]`)

Like `chan[T]`, queues are a **language type** with `make` and methods:

```mko
let q = make(queue[string], 64)
let _ = q.publish("job-1")   // or q.push(...)
let job = q.try_take()       // or q.take() — empty string if none
print_int(q.len())
let _ = q.purge()
let _ = q.free()             // or q.close()
```

| Construct | Meaning |
|-----------|---------|
| `queue[string]` | FIFO message queue type |
| `make(queue[string], n)` | Create with capacity `n` |
| `.publish` / `.push` | Enqueue (returns 1/0) |
| `.try_take` / `.take` | Dequeue (owned string) |
| `.len` / `.purge` / `.free` | Depth / clear / destroy |

**Tests:** `examples/testing/lang_queue_graphql_test.mko`

### `Graphql` type

```mko
let g = graphql_parse(body)   // HTTP JSON body *or* raw query text
if g.has("health") == 1 {
    let resp = g.data("health", "{\"ok\":true}")
}
let fs = g.fields()
let _ = g.is_query()
```

| Construct | Meaning |
|-----------|---------|
| `Graphql` | Document type (query payload) |
| `graphql_parse(s)` | From POST body or raw query |
| `.query` / `.fields` / `.has` / `.arg` | Inspect |
| `.data` / `.error` | Build JSON response |
| `.is_query` / `.is_mutation` | Operation class |

---

## Messaging builtins + `std/messaging`

In-process **named** multi-queue broker (for multi-topic brokers and workers).
The language `queue[T]` is a single-queue handle on top of the same runtime.

| Builtin | Role |
|---------|------|
| `mq_new() -> int` | Create broker handle (`0` = fail) |
| `mq_free(id) -> int` | Destroy broker; frees all pending messages |
| `mq_declare(id, name, cap) -> int` | Create named queue (idempotent) |
| `mq_publish(id, name, body) -> int` | Enqueue **clone** of body (`0` if full/missing) |
| `mq_try_take(id, name) -> string` | Dequeue; **empty string** if none (ownership transfer) |
| `mq_len(id, name) -> int` | Pending messages |
| `mq_purge(id, name) -> int` | Drop all pending (frees bodies) |

```mko
let b = mq_new()
let _ = mq_declare(b, "jobs", 1024)
let _ = mq_publish(b, "jobs", "{\"task\":\"resize\"}")
let job = mq_try_take(b, "jobs")
// … process job …
let _ = mq_free(b)
```

Package wrappers: `import "messaging"` → `messaging.broker()`, `.publish`, etc.

**Concurrency:** publish/take are mutex-safe across kicks. Prefer bounded `cap`
so backpressure is visible (no unbounded GC heap growth).

**Tests:** `examples/testing/messaging_queue_test.mko`  
**Demo:** `examples/messaging_worker.mko`

### Roadmap (external brokers)

| Adapter | Status |
|---------|--------|
| In-process queue | **Done seed** |
| Redis streams / NATS / AMQP | Planned (FFI + same `publish`/`take` shapes) |
| Kafka | Planned after NATS seed |

---

## GraphQL (`std/graphql` + builtins)

Seed for **GraphQL-over-HTTP** without a heavy runtime.

| Builtin | Role |
|---------|------|
| `graphql_query_from_body(json)` | Extract `"query"` from POST body |
| `graphql_variables_from_body(json)` | Extract `"variables"` object (or `{}`) |
| `graphql_fields(query)` | Comma-separated root field names |
| `graphql_field` / `graphql_arg` / `graphql_has_field` | Parse seeds |
| `graphql_data` / `graphql_data2` / `graphql_error` | JSON responses |
| `graphql_is_query` / `graphql_is_mutation` / `operation_name` | Operation class |

```mko
// Inside HTTP handler for POST /graphql:
let q = graphql_query_from_body(http_body(conn))
if graphql_has_field(q, "health") {
    let _ = http_respond_ct(conn, 200, "application/json",
        graphql_data("health", "{\"ok\":true}"))
} else {
    let _ = http_respond_ct(conn, 200, "application/json",
        graphql_error("unknown field"))
}
```

**Tests:** `examples/testing/graphql_seed_test.mko`, `graphql_http_test.mko`  
**Demo:** `examples/graphql_http_server.mko`

### Roadmap

| Piece | Status |
|-------|--------|
| Query/mutation detect, field/arg, JSON response | **Done seed** |
| HTTP body extract + multi-field list | **Done seed** |
| Typed schema / resolvers codegen | Planned |
| Subscriptions (SSE/WebSocket) | Planned (SSE seed exists) |

---

## Memory safety notes

- Queue bodies are **cloned on publish** and **owned by the takee** — no shared
  mutable GC heap.
- Destroy brokers with `mq_free` on shutdown paths (crew exit / main return).
- GraphQL helpers allocate response strings; normal string drop frees them.
