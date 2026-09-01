# Memory safety · no GC

String-slice COW detachment deep-clones owned string elements because the caller
retains responsibility for releasing the old header. This prevents detached
arrays from becoming duplicate owners of the same string allocation on macOS and
Linux.

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

Top-level arrays of structs use independent outer buffers. A by-value clone copies the
outer buffer and clones or retains every owned element field.
Nested refcounted slices use O(1) retains, while maps receive independent table
storage. Append detachment copies owned fields before releasing the old buffer.
Appending a new owning struct/enum element consumes an identifier source or
moves a fresh temporary; insertion does not add an unbalanced clone.

The final outer owner
recursively destroys every element's owned fields—including strings, nested
arrays, maps, builders, and nested structs—before releasing the backing allocation.
Element destructors therefore run exactly once.

Mutable owning parameters are borrowed aliases. The callee takes a balanced
retained header/reference on entry and releases it on every exit. Direct indexed
writes stay visible through the borrowed backing; growth and append detach when
required. Borrowed views may not outlive the owner or a detach boundary.

Built-in maps deep-copy table storage when ownership is duplicated. Occupied
string keys and values are cloned; empty and tombstone slots run no destructor.
`StrBuilder` is an atomic shared handle: clone uses a relaxed retain, final
release uses acquire/release ordering, overflow aborts, and `finish` steals only
from a unique owner.

Atomic reference counts protect lifetime only. They do not authorize concurrent
mutation of slice headers, maps, elements, or builders. Cross-task aliases must
be read-only or transferred through `share`/crew/channel ownership rules; a
writer requires exclusive ownership. Detachment happens before append/growth
when backing storage is shared, and borrowed views cannot survive that boundary.

Regression coverage: `examples/testing/struct_clone_nested_if_test.mko` stresses
repeated nested clone/drop cycles. The full sanitizer sweep and native memory
safety gate execute the ownership suite in CI.

## Consolidation (v0.6.18 → v0.7)

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
