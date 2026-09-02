# Consolidation plan: v0.6.24 → v0.7

**Product tip:** **0.6.23**. This document tracks the hardening and specification
work required before v0.7.

The goal is **no new features** — only freeze, specify, verify, and audit what
exists. Every item here must be memory-safe and CPU-efficient by construction.

---

## 1. COW slice specification

**Status:** unfrozen

Freeze the copy-on-write slice representation and write a normative spec:

- [ ] Document the exact struct layout: `{ T *data, size_t len, size_t cap }`
      with the 8-byte refcount header at `data - MAKO_RC_HEADER`.
- [ ] Specify retain/release semantics: atomic increment on clone, atomic
      decrement on free, abort on zero-count retain or `UINT32_MAX` overflow.
- [ ] Specify COW detach: append/mutate when `mako_rc_shared(data)` is true
      allocates new backing, copies, releases old.
- [ ] Specify view semantics: `cap == 0` means non-owning view; never freed.
- [ ] Specify the `self_consuming_call` ownership transfer contract for
      append/push/pop/insert/remove.
- [ ] Write conformance tests that exercise every edge of the spec.

## 2. Property-based ownership testing

**Status:** not started

- [ ] Generate random ownership programs (let/move/clone/drop/reassign/return
      sequences) and verify ASan-clean execution on both C and native backends.
- [ ] Generate adversarial append/clone/slice/view interleavings.
- [ ] Add a fuzzer harness for the C codegen's own-drop tracking.

## 3. Channel state machine model-check

**Status:** not started

- [ ] Model the unbuffered channel rendezvous protocol (send/recv/close/select)
      as a finite state machine.
- [ ] Verify no deadlock, no lost wakeup, no double-free of buffered messages.
- [ ] Verify the SAFE-042 loop-scope cleanup interacts correctly with channel
      range iteration.
- [ ] Write stress tests: multi-producer/multi-consumer, close-during-send,
      select with timeout, channel of channels.

## 4. Unicode conformance

**Status:** partial (Unicode 17 UCD tables + normalization implemented)

- [ ] Run the official Unicode NormalizationTest.txt suite against
      `unicode_nfc`/`unicode_nfd`/`unicode_nfkc`/`unicode_nfkd`.
- [ ] Run the official GraphemeBreakTest.txt and WordBreakTest.txt suites.
- [ ] Verify XID_Start/XID_Continue tables against the official DerivedCoreProperties.txt.
- [ ] Document any intentional deviations (e.g., no BIDI controls in identifiers).

## 5. Windows quarantine reduction

**Status:** quarantined

- [ ] Audit and fix each quarantined Windows test.
- [ ] Remove stale README performance numbers that reference old benchmarks.
- [ ] Verify native backend on Windows (Cranelift ABI, stack alignment).

## 6. C runtime modularization

**Status:** monolithic headers

Split the C runtime into auditable, independently reviewable modules:

- [ ] `mako_rt.h` — core types, refcount, allocator, scheduler (< 2000 lines)
- [ ] `mako_str.h` — string operations
- [ ] `mako_slice.h` — slice/array operations with COW
- [ ] `mako_chan.h` — channel operations (int, string, ptr)
- [ ] `mako_map.h` — hash map operations
- [ ] Each module must compile independently with only `mako_rt.h` as dependency.
- [ ] Each module must pass ASan/UBSan independently.

## 7. Application benchmarks

**Status:** micro benchmarks only

- [ ] SIP proxy throughput (messages/sec, p99 latency) vs Kamailio.
- [ ] HTTP/1.1 + HTTP/2 server throughput vs Go net/http.
- [ ] HEP/EEP capture agent throughput vs heplify.
- [ ] Publish results with methodology, hardware, and reproduction steps.
- [ ] All benchmarks must be deterministic and reproducible in CI.

## 8. CI release automation

**Status:** manual

- [ ] CI generates all release facts (test counts, ASan results, benchmark
      numbers, binary sizes) automatically on tag push.
- [ ] CI produces release binaries for darwin/arm64, darwin/amd64, linux/amd64.
- [ ] CI validates the installed binary matches the checkout (hash comparison).
- [ ] CI runs FayDB durable WAL/crash suite as a regression gate.

## 9. Self-hosted IR verifier

**Status:** in progress

- [ ] Continue the CFG/SSA verifier for the native IR.
- [ ] Verify dominance, use-before-def, type consistency at every IR node.
- [ ] Verify that every IR instruction's operands are reachable from the
      function entry.
- [ ] Run the verifier on every test in the suite; any violation is a compiler bug.

## 10. External review

**Status:** not started

Arrange an external review covering:

- [ ] COW ownership model: retain/release pairing, scope-exit drops, append
      detach, struct field ownership transfer.
- [ ] Channel wakeup protocol: rendezvous, buffered, select, close semantics.
- [ ] Native ABI handling: struct return conventions, parameter passing,
      caller/callee-saved registers, stack alignment.
- [ ] ML-DSA integration: key material lifetime, OpenSSL object cleanup,
      signature verification correctness.

---

## Memory safety invariants (must hold for v0.7)

1. Every `malloc`/`mako_rc_alloc` has exactly one matching `free`/`mako_rc_release`
   on all paths (success, error, early return, break, continue, `?`).
2. No use-after-free: freed pointers are never dereferenced.
3. No double-free: ownership is unique or refcounted; never both.
4. No buffer overflows: all index operations are bounds-checked (SAFE-001).
5. No data races on shared state: channels, atomics, and mutexes are the only
   concurrency primitives; slices are non-Send.
6. ASan + UBSan clean on the full test suite (C and native backends).
7. TSan clean on all concurrency tests.
8. No signed integer overflow UB: all arithmetic uses defined wrap semantics.
