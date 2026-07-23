# Messaging · GraphQL · gRPC · OpenAPI

**Product tip:** 0.4.15 · **No GC** — payloads and responses are owned strings;
queues free on `*_free` / take transfer.

Related: [STDLIB.md](STDLIB.md) · [MEMORY_SAFETY.md](MEMORY_SAFETY.md) ·
[ADAPTIVE_OPT.md](ADAPTIVE_OPT.md) · [LANGUAGE.md](LANGUAGE.md).

---

## Language surface

### `queue[T]` (seed: `queue[string]`)

```mko
let q = make(queue[string], 64)
let _ = q.publish("job-1")
let job = q.try_take()
```

### `Graphql` type

```mko
let g = graphql_parse(body)
if g.has("health") == 1 {
    let resp = g.data("health", "{\"ok\":true}")
}
```

---

## Messaging: in-process + adapters

### In-process multi-queue (`mq_*` / `std/messaging`)

| Builtin | Role |
|---------|------|
| `mq_new` / `mq_free` | Broker handle |
| `mq_declare` / `mq_publish` / `mq_try_take` | Named FIFO |
| `mq_len` / `mq_purge` | Depth / clear |

### NATS adapter (`nats_*` / `std/messaging/nats`)

In-process **subject** bus (same shapes as NATS pub/sub). Wire frames for a
real `nats-server` when you add a TCP client.

| Builtin | Role |
|---------|------|
| `nats_new` / `nats_free` | Bus handle |
| `nats_sub(id, subject, cap)` | Create subject inbox |
| `nats_pub` / `nats_try_next` / `nats_len` | Publish / consume / depth |
| `nats_pub_frame` / `nats_sub_frame` / `nats_connect_frame` / `nats_ping_frame` | Text protocol seeds |

```mko
let n = nats_new()
let _ = nats_sub(n, "jobs.resize", 64)
let _ = nats_pub(n, "jobs.resize", "{\"w\":64}")
let msg = nats_try_next(n, "jobs.resize")
```

### Redis list adapter (`redis_mq_*` + live list ops)

| Surface | Role |
|---------|------|
| `redis_mq_new` / `declare` / `lpush` / `rpop` / `llen` | In-process LPUSH/RPOP (no redis-server) |
| `redis_conn_lpush` / `rpop` / `llen` | Live connection |
| `redis_lpush` / `rpop` / `llen` | One-shot host:port |

Package: `std/messaging/redis.mko` (`publish` = LPUSH, `try_take` = RPOP).

**Tests:** `examples/testing/adapters_api_test.mko` · `messaging_queue_test.mko`  
**Demo:** `examples/messaging_worker.mko`

| Adapter | Status |
|---------|--------|
| In-process queue | **Done** |
| NATS (in-process + frames) | **Done seed** |
| Redis lists | **Done seed** |
| Kafka / AMQP | Planned |

---

## GraphQL schema + resolvers

Beyond body/field helpers, schemas are first-class handles:

| Builtin | Role |
|---------|------|
| `graphql_schema_new` / `free` | Schema handle |
| `graphql_schema_add_type` / `add_field` | Type system seed |
| `graphql_schema_set_resolver(field, json)` | Root field → JSON value |
| `graphql_schema_resolve(id, query)` | Full `{"data":…}` or error |
| `graphql_schema_sdl(id)` | Emit SDL text |
| `graphql_schema_has_type` / `has_resolver` | Introspection seeds |

```mko
let s = graphql_schema_new()
let _ = graphql_schema_add_type(s, "Query")
let _ = graphql_schema_add_field(s, "Query", "health", "Boolean")
let _ = graphql_schema_set_resolver(s, "health", "true")
let resp = graphql_schema_resolve(s, "{ health }")
let sdl = graphql_schema_sdl(s)
```

Packages: `std/graphql` · `std/graphql/schema.mko`.

HTTP seeds still: `graphql_query_from_body`, `handle_http_body`, demo
`examples/graphql_http_server.mko`.

---

## gRPC

Existing **frame** helpers (`grpc_encode_message`, HTTP/2 unary seeds) plus a
**service registry**:

| Builtin | Role |
|---------|------|
| `grpc_service_new(name)` / `free` | Service handle |
| `grpc_service_add_method(id, method, response_payload)` | Unary map |
| `grpc_service_handle(id, method, request)` | Framed response |
| `grpc_service_has_method` / `methods` / `name` | Inspect |

Package: `std/grpc`. Tests: `grpc_frame_test` · `adapters_api_test`.

---

## OpenAPI 3.1

| Builtin | Role |
|---------|------|
| `openapi_route` / `openapi_doc` | Path item + document (existing) |
| `openapi_paths_merge` | Merge two path objects |
| `openapi_operation` / `openapi_response` | Operation / response fragments |
| `openapi_info` / `openapi_doc_full` | Info block + full doc |

Package: `std/openapi`. Test: `openapi_test` · `adapters_api_test`.

```mko
let paths = openapi_paths_merge(
    openapi_route("GET", "/health", "Liveness"),
    openapi_route("POST", "/v1/jobs", "Enqueue"),
)
let doc = openapi_doc_full(
    openapi_info("API", "1.0.0", "seed"),
    paths,
)
```

---

## Claims policy

- **Do** say: no GC; owned string payloads; in-process adapters work offline.
- **Do not** claim full NATS protocol client, Redis Streams, or protobuf codegen
  without a named harness.
- Live Redis/NATS require a server; seeds ship in-process mocks + wire frames.
