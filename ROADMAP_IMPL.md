# Mako Implementation Roadmap

Detailed feature plan for Mako, organized by version. See
[docs/ROADMAP.md](docs/ROADMAP.md) for the summary view.

**Current version:** 0.2.3  
**Next milestone:** 0.2.4 (tooling) + soundness **residuals**  
**Last updated:** 2026-07-18

Soundness program of record: **[docs/SOUNDNESS.md](docs/SOUNDNESS.md)**.  
Concurrency model: **[docs/MEMORY_MODEL.md](docs/MEMORY_MODEL.md)**.  
Summary roadmap: **[docs/ROADMAP.md](docs/ROADMAP.md)**.

---

## Soundness program (SAFE / RT) — **core shipped; residuals active**

### Shipped on main (0.2.3+)

| ID | Work | Evidence |
|----|------|----------|
| SAFE-001 | Release bounds always on | `MAKO_SAFE_DEFAULT`; claims-gate `release-bounds` |
| SAFE-002 | Ownership categories | LANGUAGE_SPEC § Ownership categories |
| SAFE-003 | Core slice free + `cap==0` views + return transfer | `own_drop_slice_test`, `slice_return_own_test`; ASan |
| SAFE-003/007 | Slice view escape / view-return reject | `bad/slice_view_escape`, `slice_view_return` |
| SAFE-004 | Built-in map free | `mako_map_*_free`; own_drop for `make(map…)` |
| SAFE-007 | Arena return escape | `bad/arena_escape_return` |
| SAFE-009 | CMap RW gate | `mako_cmap.h` |
| SAFE-010 | Memory model doc | MEMORY_MODEL.md |
| RT-001 | Crew cancel_join | cancel_policy_test + docs |
| RT-005 | Select/channel stress seed | `chan_select_stress_test` |
| RT-006 | Census APIs | `runtime_census_test` |
| SAFE-003/004 full | free-on-reassign; monomorph map free; nested free | map_* suite + ASan |
| SAFE-006 break/continue | loop-exit own free | `cfg_drop_break_test` |
| SAFE-007 field store | arena store into outer fields | `bad/arena_store_field` |
| RT-004 seed | channel clone/try_send ownership | `channel_ownership_test` |
| (lang) | Field/index mut roots; temp lvalue reject; empty `[]` | bad examples + `empty_slice_test` |

### Residuals (feed 0.2.4 / later)

| ID | Work | Status |
|----|------|--------|
| SAFE-005 | String own/view surface + free once | Partial |
| SAFE-006 residual | `?` early-return free edges | Partial — break/continue/return done |
| SAFE-008 | Capture matrix + TSan soak | Partial |
| RT-002…003 | Bounded scheduler + blocking split | Planned |
| RT-004 residual | Take-send + monomorph channel matrix | Partial — seed tests shipped |
| RT-005 residual | Randomized longer soaks | Seed shipped |

### Implementation order (next)

1. String own/view (SAFE-005)  
2. `?` free residual (SAFE-006)  
3. Capture audit (SAFE-008)  
4. Channel ownership matrix (RT-004)  
5. Scheduler (RT-002/003)  
6. Longer stress soaks (RT-005)

---

## v0.2.1 — Safety & correctness — **shipped**

| Feature | Status |
|---------|--------|
| Match exhaustiveness | Done — enum, Option, Result, Bool, generic enums. Compiler error with missing variant names. |
| Match guards | Done — `pattern if condition => body`. Guard combined with pattern condition. |
| Use-after-move | Verified — 48 `hold_*.mko` bad examples. Compiler error on use of moved `hold` values. |
| Race detection | Verified — `race_mut_after_kick.mko`. Compiler flags mutation while kicked tasks may use the local. |
| Nested destructuring | AST supports `Vec<Pattern>` nesting. Codegen partial — `Some(struct)` binding needs Option inner type tracking. |

### Known limitations

- `Some(StructType)` match binding extracts as `int64_t` not the struct type (codegen doesn't track Option inner type in all paths)
- Match guard fallthrough across multiple arms with same pattern not supported (guard-failed arm skips to next `else if`)
- `return` inside multi-statement lambda bodies still uses enclosing fn return type

---

## v0.2.0 — Stdlib in Mako — **shipped**

Standard library packages rewritten in Mako instead of C runtime wrappers.

| Package | What shipped |
|---------|-------------|
| `std/io` | `StringReader` / `ByteWriter` with `mut self` read/write methods, `drain()` |
| `std/collections` | `IntStack` / `StrStack` / `IntQueue` with push/pop/enqueue/dequeue, Option returns |
| `std/context` | `Context` with `background()`, `with_timeout()`, `with_deadline()`, `with_cancel()`, `done()`, `err()` |
| `std/encoding/json` | `ObjectBuilder` / `ArrayBuilder` with `set_string`/`set_int`/`set_bool`/`set_raw`, JSON escaping |
| `std/net/http` | `Request`/`Response` types, `Router` with method+path matching, response constructors |
| `std/database/sql` | `Pool` with `acquire()`/`release()` slot management, `begin_tx`/`commit_tx`/`rollback_tx` |

Tests: `io_reader_writer_test`, `collections_mako_test`, `context_mako_test`,
`json_mako_test`, `http_mako_test`, `sql_pool_test`.

### Known limitations

- `io.copy(dst, src)` can't mutate caller's dst/src without reference types
- `Option[string]` match extracts wrong slot in some codegen paths
- `mut self` through `pull` imports doesn't always propagate the pointer flag
- Structs with opaque handle fields (`SqlDB`) can't generate equality

---

## v0.1.10 — Deepen generics + speed — **shipped**

Resolved the blockers that prevented the stdlib rewrite.

| Feature | Status |
|---------|--------|
| Multi-statement lambda bodies | Done — let, assign, if/else, while, nested loops in lambda bodies |
| `mut self` on methods | Done — pointer-based receiver, mutations persist, `->` field access |
| Generic enum variant disambiguation | Done — qualified lookup by return type context |
| Tuple channels, chan_len/cap | Done |
| Speed optimizations | Done — wyhash, stack f-strings, constant folding, zero-copy strings, select condvar |

---

## v0.1.9 — Generics & Iterators — **shipped**

Foundation for everything above. Generic structs, enums, interface bounds,
iterator protocol seed, mutable closure infrastructure.

### Generic structs

- `struct List[T] { data: []T, len: int }` must work
- Monomorphized at compile time (same strategy as generic functions today)
- Codegen emits one C struct per concrete instantiation
- Generic structs can appear in function signatures, return types, and as fields

### Generic enums

- `enum Tree[T] { Leaf(T), Node(Tree[T], Tree[T]) }` must work
- Same monomorphization strategy
- Pattern matching on generic enum variants must work

### Interface bounds on generics

- `fn sort[T: Orderable](a: []T)` — constrain type parameters
- The type checker must verify that concrete types satisfy the interface
- Interfaces remain structurally typed (no explicit `implements` declaration)
- Bounded generics work on both functions and types

### Iterator protocol

- Define a built-in `Iterator[T]` interface: `fn next(self) -> Option[T]`
- `for x in expr { }` works when `expr` implements `Iterator[T]`
- Slices, maps, and ranges get automatic `Iterator` implementations
- Users can implement `Iterator` on custom types via `on MyType { fn next(self) -> Option[T] }`
- Chaining methods (`map`, `filter`, `take`, `collect`) are not required in v0.1.9

### Closures with mutable captures

- Closures can capture and mutate outer variables by reference
- The compiler tracks which captures are mutable
- Mutable captures use a heap-boxed cell (not the current read-only snapshot)
- `fn(x) { counter = counter + 1 }` must work without `ShareInt` workarounds

---

## v0.2.0 — Stdlib in Mako

Move the standard library from C runtime wrappers to real Mako code. The stdlib
must be written in idiomatic Mako and serve as example code.

### io.Reader / io.Writer interfaces

- `interface Reader { fn read(self, buf: []byte) -> (int, error) }`
- `interface Writer { fn write(self, buf: []byte) -> (int, error) }`
- All I/O (files, sockets, buffers, compression) implements these interfaces
- `bufio.Reader` / `bufio.Writer` wrap any Reader/Writer

### Collections rewritten with generics

- `List[T]`, `Set[T]`, `Queue[T]`, `PriorityQueue[T]` in Mako
- `Map[K, V]` as a generic type (today maps are compiler-monomorphed built-ins)
- These use the iterator protocol from v0.1.9

### encoding/json in Mako

- Struct-aware marshal/unmarshal using reflect
- Streaming decoder for large payloads
- Written in Mako, not C glue

### net/http server with middleware

- Handler chains: `fn handle(req: Request, next: fn(Request) -> Response) -> Response`
- Request context carrying deadlines and values
- Streaming request/response bodies via Reader/Writer

### context with cancellation

- Deadline propagation through function call chains
- Cancel trees: parent cancellation propagates to children
- `context.with_timeout(ctx, duration)` / `context.with_cancel(ctx)`

### database/sql connection pool

- Pool with configurable min/max connections
- Prepared statement cache
- Transaction API: `db.begin()` / `tx.commit()` / `tx.rollback()`

---

## v0.2.3 — Safety & Correctness

Close the gap between safety claims and verified behavior.

### Ownership verification

- Static analysis that `hold` values are not used after move
- Compiler error on use-after-move, not just a runtime crash
- Works across function calls and control flow branches

### Lifetime tracking for references

- Prevent dangling pointers from sub-slices and borrowed views
- Track that a `mako_str_view` does not outlive the data it points into
- Compiler warning or error when a reference escapes its scope

### Race detection at compile time

- Shared mutable state across `kick` boundaries is a compile error
- Only `share`-wrapped or channel-passed values cross thread boundaries
- The Send/Sync model is enforced, not advisory

### Pattern match exhaustiveness

- Compiler error when match arms do not cover all enum variants
- Warning for missing `default` / `_` arm on non-enum matches
- Works with nested patterns

### Match guards

- `Ok(n) if n > 0 => handle_positive(n)`
- Guards are boolean expressions evaluated after pattern binding
- Guard failure falls through to the next arm

### Nested destructuring

- `Some(Point { x, y }) => use(x, y)`
- Destructure structs inside enum variants inside options
- Works with generic types

---

## v0.2.3 — JWT / HTTPS input hardening — **shipped**

| Feature | Status |
|---------|--------|
| Strict JWT JSON skip/parse | Done — numbers, true/false/null, depth limit, no trailing junk |
| JWKS fail-closed | Done — malformed keys JSON rejects verify |
| JWT sign/verify resource safety | Done — payload cap, HMAC length, free sig buffers |
| Dual-stack HTTP listen | Done — shared TCP backlog helper |
| TLS live tests | Done — free ports, assert SNI bind |

---

## v0.2.4 — Tooling

Production-grade developer experience.

### LSP improvements

- Find-all-references across files
- Rename refactoring (safe symbol rename across the project)
- Signature help (parameter hints while typing)
- Inlay hints (show inferred types inline)
- Type information on hover for arbitrary expressions

### Debugger integration

- Source-level breakpoints in `.mko` files via DAP protocol
- Step through Mako lines, not generated C
- Inspect Mako variables (translate C variable names back to Mako names)
- Works with VS Code and JetBrains

### Package registry

- `mako publish` uploads a package to the registry
- `mako install pkg@version` fetches from the registry
- Registry stores packages, versions, checksums, and documentation
- Web UI for browsing packages

### Dependency solver

- Proper version conflict resolution (not just first-match semver)
- Lock file with integrity hashes
- `mako update` respects semver constraints

---

## v0.3.0 — Cross-Platform

### Windows

- All tests pass on Windows in CI
- Native threading (`CreateThread` / `SRWLock` / `CONDITION_VARIABLE`)
- Native event loop (IOCP for networking)
- MSI installer and winget package

### WASM

- Browser target with DOM bindings
- No POSIX dependencies in WASM builds
- WASI Preview 2 (component model)
- wasm-opt integration for size

### ARM / RISC-V

- Tested in CI (QEMU or real hardware)
- Cross-compilation from x86 host works reliably

---

## v0.4.0 — Performance Ceiling

### IR layer

- Intermediate representation between AST and C emission
- Enables language-aware optimization passes
- Prerequisite for LLVM backend

### Dead code elimination

- Import-aware reachability analysis (handle `pull` / `import` correctly)
- Only emit reachable functions, types, and methods
- Reduces clang compile time and binary size

### Escape analysis

- Stack-allocate values that do not escape their declaring scope
- Avoids heap allocation for short-lived structs and closures

### Interface devirtualization

- When the concrete type is known at a call site, inline the call
- Eliminate fat-pointer indirection for monomorphic interface usage

### Closure inlining

- Inline small closures at their call sites
- Eliminate heap allocation for non-escaping closures

### LLVM backend (optional)

- Direct LLVM IR emission as alternative to C backend
- Better optimization for targets where clang is slow or unavailable
- Required for some advanced optimizations (vectorization, link-time devirt)

---

## v1.0 — Stability

- Language syntax frozen (no breaking changes)
- Stdlib API stable (semver guarantees on public symbols)
- Self-hosting compiler (compiler written in Mako)
- Formal memory model (documented guarantees for concurrent access)
- Ecosystem: registry with packages, IDE plugins, CI templates, tutorials

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.

Design principles that apply to all versions:

- Every feature must have tests in `examples/testing/`
- No garbage collection — memory uses `hold`, `share`, and `arena`
- Performance first — no silent hot-path allocations
- The compiler pipeline is `.mko` → parser → AST → type checker → codegen → C → clang → binary
