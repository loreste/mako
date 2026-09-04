# Memory safety · no GC

String-slice COW detachment deep-clones owned string elements because the caller
retains responsibility for releasing the old header. This prevents detached
arrays from becoming duplicate owners of the same string allocation on macOS and
Linux. Nested `arr[i].field = append(arr[i].field, v)` dest-destroys that old
header (issue #53); skipping it leaked one full column copy per FayDB DML.

There is no garbage collector. Memory frees when ownership says so — scope
exit, move, drop — or when you use explicit share (RC) or arenas. Not when a
tracing collector eventually notices.

That isn’t a soft preference. **0.4.11+**. More detail in
[SOUNDNESS.md](SOUNDNESS.md), [SECURITY.md](SECURITY.md),
[MEMORY_MODEL.md](MEMORY_MODEL.md), [LONG_RUNNING.md](LONG_RUNNING.md),
[SPEED.md](SPEED.md).

---

## What that means day to day

No collector thread, no stop-the-world, no GC mode flag. Free lands at known
points: scope exit, reassign, `break` / `continue` / `return` / `?`, match
Own free. Index checks still abort on OOB under `-O3 -flto` (SAFE-001).
Use-after-move is a typecheck problem (CFG NLL + hold/move). Own drops once;
ASan watches for double-free. RC only shows up when you ask
(`share` / `ShareInt` / channel clones) — not as a heap-wide collector in
disguise.

This isn’t formally verified Rust. Safe Mako blocks whole classes of bugs by
construction; `unsafe` and FFI are outside that. CI’s ASan / UBSan / TSan runs
are evidence, not the whole defense.

---

## Memory tools (pick one cost model)

| Tool | Role | Free when |
|------|------|-----------|
| **Own** (default for heap strings/slices/maps) | Single owner | Scope exit / move / drop |
| **`hold`** | Move discipline | Same as Own after move rules |
| **`share` / Sync handles** | Explicit multi-owner / concurrent | RC hits zero or lock drop |
| **`arena`** | Bulk region | Arena scope exit (no per-object free tax) |
| **Views** (`cap==0`, `string_view`) | Zero-copy borrow | Never free backing storage |

There is **no** “let the GC clean it up later.”

Slice Views are non-retaining borrows. Safe Mako permits one live View per base;
a `mut` View is exclusive and may mutate its range. While a View is live, NLL prevents its
base from mutation, reassign, move, drop, competing borrow, or COW detachment. Atomic
slice refcounts protect backing lifetime only; they are not locks and do not
make slice payloads safe for concurrent access. Slices remain non-Send.

Retain uses a checked atomic increment. Zero-count retain, `UINT32_MAX`
overflow, and zero-count release abort instead of wrapping or resurrecting freed
storage. The ordinary unique-write path pays no refcount or locking cost.

---

## What is *not* GC

| Mechanism | Why it is not GC |
|-----------|------------------|
| Scope drops | Compiler-inserted free at known CFG points |
| Arena bulk free | Region free once; O(1) teardown |
| `ShareInt` / RC clones | Fixed refcount on a handle — not heap tracing |
| Channel clone/take | Message ownership transfer, not collection |

---

## Verification (how we know)

| Layer | Evidence |
|-------|----------|
| **Typecheck / NLL** | Use-after-move, kick Send, capture rules |
| **Codegen drops** | Free on all exits (SAFE-006 matrix) |
| **Loop scope cleanup** | All loop variants (string range, channel range, map range, iterator) release owned temporaries per iteration — not just integer range, slice, while, and cfor (SAFE-042) |
| **Append COW release** | `self_consuming_call` path releases old slice backing when append grows, preventing refcount leaks that cause `mako_rc_shared` to always return true and force exponential capacity growth (SAFE-042) |
| **JSON extractor safety** | Fixed use-after-free in `json_get_float`/`json_get_bool` where `strlen(pat)` was called after `free(pat)` |
| **PQC resource pairing** | Every `EVP_PKEY_CTX_new`/`EVP_PKEY`/`BIO_new`/`X509_new`/`EVP_MD_CTX_new` in `mako_pqc.h` is paired with its free on all paths (success and error) |
| **Owning struct params** | Non-mut and large-mut structs pass by pointer (no per-field RC clone at call, issue #46); native matches the ≥4 owned-field in-place threshold. Escaping a borrow clones so caller and destination destructors stay disjoint. Reassign (`db = result.db`) frees previous owned fields only when backing storage changed. Indexed owning-struct append (`append(dst, src[i])`) and `let t = src[i]` clone the element; owning-array field reassign (`db.tables = replace_table(db.tables, i, t)`), index assignment (`xs[i] = v`), and map overwrite (`m[k] = v`) free previous owned storage only when backing changed. Unique owning idents clone when reused and move (zeroed) on last use (issue #51). Two args cannot alias a `mut` parameter (`f(h, h)`). Kick deep-clones owned fields and drops the worker copy. Tests: `owning_struct_param_borrow_test.mko`, `owning_struct_param_borrow_adversarial_test.mko`, `owning_struct_exec_reassign_test.mko` |
| **Unit tests** | `own_*`, `double_free_guard`, `leak_detector`, `match_own_free`, … |
| **ASan CI** | Full suite + ownership fixtures (`--sanitize address`) |
| **UBSan / TSan** | Undefined behavior + races (opt-in / CI jobs) |
| **Years-up soaks** | `long-run-soak`, `http-long-run-soak` — no RSS creep under alloc/free |
| **Gate script** | `./scripts/memory-safety-gate.sh` |

```bash
# Ownership tests on C and native; native ASan/LSan when the toolchain supports it:
./scripts/memory-safety-gate.sh
MAKO_MS_SKIP_HTTP=1 ./scripts/memory-safety-gate.sh   # skip the slow HTTP soak
```

### Three rules that make the evidence mean something

Learned the hard way in 0.4.16, when a double-free shipped with the gate green.

1. **A fixture not in the gate is not covered.** The regression lived in a
   builder family whose fixture the gate never ran. Adding a builtin means
   adding its fixture to the list in `scripts/memory-safety-gate.sh`.
2. **A per-call leak is invisible to a run-once fixture.** It passes whether
   the result is reclaimed or not. Loop thousands of iterations with
   *let-bound* arguments — the shape a server actually uses — under
   LeakSanitizer, and require the leak total to be flat in the iteration count.
   Growth that scales with the loop is one leak per call, which a short-lived
   process will never show.
3. **Sanitizers run on Linux.** The ASan runtime deadlocks at startup on macOS,
   so results from a Mac host are not evidence. Note also that standalone LSan
   tolerates a double-free — use `--sanitize address`, or a plain build, to
   catch frees.

C-runtime builtins carry no ownership annotation across the ABI, so each is
classified owned or borrowed by the compiler. Misclassification is asymmetric:
borrowed-when-owned leaks every call, owned-when-borrowed is a double-free.
When uncertain, it stays borrowed.

---

Sanitizer sweeps pin `MAKO_RUNTIME` to the current checkout and default to the
C backend. This prevents a stale installed runtime or a backend without fully
instrumented loads and stores from producing misleading review evidence.

Owned runtime strings use plain `malloc`/`free`; refcount headers are reserved
for slice backings whose owning `cap > 0` representation supplies explicit
provenance. Borrowed string views never enter owned drop paths, and the runtime
never probes bytes before an arbitrary string pointer to guess its allocator.

## Unsafe boundary

| Safe Mako | Outside the guarantee |
|-----------|------------------------|
| Indexed access (bounds-checked) | `unsafe { … }`, `unsafe_index` |
| Own / hold / arena | Raw FFI pointers you free yourself |
| Kick Send checks | Data races if you break Sync rules in C |

---

## Long-running servers

No GC is why Mako targets **years-up** p99 stability. Combine:

1. Request-scoped owns / arenas  
2. `./scripts/long-run-soak.sh` and `./scripts/http-long-run-soak.sh`  
3. Optional `MAKO_ALLOCATOR=mimalloc|jemalloc` for fragmentation  

See [LONG_RUNNING.md](LONG_RUNNING.md).

---

## Nested struct-array ownership

Top-level arrays of structs clone by O(1) RC retain of the outer buffer, the
same as `[]int` and nested struct arrays. A deep copy of every element's owned
fields on clone is what OOM-killed FayDB pgwire on `COUNT(*)` of a 100K-row
table (issue #51: `ExecResult { db: db }` inside `db_exec`).
Nested refcounted slices use O(1) retains, while maps receive independent table
storage. Append detachment copies owned fields before releasing the old buffer.
Appending a new owning struct/enum element consumes an identifier source or
moves a fresh temporary; insertion does not add an unbalanced clone.

The final outer owner
recursively destroys every element's owned fields—including strings, nested
arrays, maps, builders, and nested structs—before releasing the backing allocation.
Element destructors therefore run exactly once.

Fresh local structs that contain owned fields are owners in their own right.
The compiler emits recursive field destruction at scope exit, including for
call-returned structs, and frees the old fields before whole-struct assignment.
An indexed struct value is a borrow into its container and never receives an
independent destructor unless explicitly cloned into owned storage.

Reading an owned field into an owning destination from a unique owning
struct local (call/method result, struct literal, or a nested struct moved
out of one) is a move, not a clone. Codegen copies the field header/value and
immediately zeros the source field, including nested paths
(`result.detail.label`). A nested struct moved out of that owner is itself a
unique owner, so further field extracts also move. The typechecker rejects a
second read of a moved field path. The parent destructor recursively visits
nested fields but skips the zeroed allocation, while the destination becomes
its sole owner. Cloning a unique owner's field aliases the parent destructor
and is not used. Fields reached through shared, indexed, or otherwise borrowed
values clone instead because mutating those sources would violate alias safety.
The native backend follows the same move-and-zero rule. Enum-typed owned
fields that must be duplicated (true aliases) clone through
`mako_enum_*_clone` so copies never alias payload storage.

Reassigning a unique struct local from a moved field (`db = result.db`)
destroys the destination's previous owned fields before overwrite. User
structs have no single free function; skipping that step leaked nested
arrays and strings on every query (issue #49).

Aggregate channel sends clone owned payload fields before handoff. The sender
keeps its original value; a successful send transfers the clone to the channel,
and a rejected normal, timeout, or try-send recursively destroys the clone.

Mutable owning parameters are borrowed aliases. The callee takes a balanced
retained header/reference on entry and releases it on every exit. Direct indexed
writes stay visible through the borrowed backing; growth and append detach when
required. Borrowed views may not outlive the owner or a detach boundary.

Built-in maps deep-copy table storage when ownership is duplicated. Occupied
string keys and values are cloned; empty and tombstone slots run no destructor.
`StrBuilder` is an atomic shared handle: clone uses a relaxed retain, final
release uses acquire/release ordering, overflow aborts, and `finish` steals only
from a unique owner.

Channel handles use overflow-checked atomic retain/release when copied into
owning structs or task state. The final release uses acquire/release ordering
before closing the channel and destroying synchronization state; a non-final
release never closes shared channel state. This protects blocked senders and
receivers from a sibling alias being destroyed. Channel payload transfer rules
remain unchanged.

Atomic reference counts protect lifetime only. They do not authorize concurrent
mutation of slice headers, maps, elements, or builders. Cross-task aliases must
be read-only or transferred through `share`/crew/channel ownership rules; a
writer requires exclusive ownership. Detachment happens before append/growth
when backing storage is shared, and borrowed views cannot survive that boundary.

Regression coverage: `examples/testing/struct_clone_nested_if_test.mko` stresses
repeated nested clone/drop cycles. The full sanitizer sweep and native memory
safety gate execute the ownership suite in CI.

For C pointer-backed owning-struct parameters, whole-value assignment writes
through the borrow, recursively destroys the destination's previous owned fields
before overwrite, and never registers the borrowed parameter for scope-exit
destruction. Storing that borrowed struct in an array or map deep-clones its
owned fields exactly once before the container takes ownership. Opaque `void*`
handles stay by-value and never enter this struct ABI path.

## Consolidation (v0.6.23 → v0.7)

The next phase freezes the COW slice representation as a normative spec,
adds property-based ownership fuzzing, model-checks the channel state machine,
runs official Unicode conformance suites, splits the C runtime into auditable
modules, and arranges an external review of ownership, channels, and native ABI.
See [CONSOLIDATION.md](CONSOLIDATION.md).

---

## What we’ll stand behind

No tracing GC. Ownership free. Bounds checks in release. ASan green on the
safe path.

We won’t claim “memory-safe for all FFI” or “proven like seL4.” And we won’t
add a collector later under the same product name without a major version and
an identity break.
