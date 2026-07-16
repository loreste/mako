# Mako Implementation Roadmap

This is the authoritative feature plan for Mako. AI assistants working on this
codebase must follow this document when implementing new features or making
architectural decisions.

**Current version:** 0.1.9  
**Next milestone:** 0.2.0  
**Last updated:** 2026-07-16

---

## v0.1.9 — Generics & Iterators — **shipped**

Landed in product tip. Residual notes: iterator by-value `self` does not
advance state automatically; mutable multi-statement lambdas are seed-quality.
Everything downstream still depends on deepening these.

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

## v0.2.1 — Safety & Correctness

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

## v0.2.2 — Tooling

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

## Rules for AI assistants

1. When implementing a feature, check this document first to see which version
   it belongs to. Do not implement features from later versions before earlier
   ones are complete.

2. v0.1.9 features (generics, iterators, mutable closures) are the current
   priority. All other features wait.

3. The compiler architecture is: `.mko` source -> parser -> AST -> type checker
   -> codegen -> C source -> clang -> binary. All new features must work within
   this pipeline until v0.4.0 introduces an IR layer.

4. The runtime is C headers in `runtime/*.h`. New runtime features go there.
   Standard library Mako code goes in `std/`. Keep them separate.

5. Every feature must have tests in `examples/testing/`. No feature ships
   without tests.

6. Do not add garbage collection. Memory management uses ownership (`hold`),
   shared references (`share`), and arenas (`arena`). No GC.

7. Do not break existing tests. Run `make test` before declaring a feature
   complete.

8. Performance matters. Do not add features that silently allocate on the hot
   path. If a feature has a cost, make it visible and opt-in.

9. Do not mention any AI assistant in code, comments, commit messages, or
   documentation.
