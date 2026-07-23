# Messaging queues · GraphQL

**Product tip:** 0.4.12 · **No GC** — payloads and responses are owned strings;
queues free on `mq_free` / `mq_purge` / take transfer.

Related: [STDLIB.md](STDLIB.md) · [MEMORY_SAFETY.md](MEMORY_SAFETY.md) ·
[GENERAL_PURPOSE_PLAN.md](GENERAL_PURPOSE_PLAN.md) wave 2.

---

## Messaging (`std/messaging` + builtins)

In-process **message queues** for work distribution and pub/sub seeds. Not a
Kafka client yet — same API shapes can wrap external brokers later.

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
