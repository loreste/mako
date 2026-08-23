# Standard Library Safety Contracts

This file defines the safety contract families enforced by
`scripts/stdlib-safety-audit.sh`. The audit owns the exact package-to-family
mapping and fails if any `checked-native` or `unsafe-boundary` row in
[STDLIB_SAFETY_MATRIX.md](STDLIB_SAFETY_MATRIX.md) is unclassified or ambiguous.

These contracts do not replace package tests. They define the minimum release
bar that lets Mako talk about safe stdlib coverage honestly while native-backed
packages continue to use C, OpenSSL, zlib, OS handles, and platform APIs behind
checked wrappers.

### `codec-parser`

Applies to archive, compression, encoding, parser, template, protocol-frame,
and text/binary codec packages.

Required behavior:

- malformed input returns an error or empty result instead of reading past
  bounds;
- decoded sizes are capped before allocation or decompression;
- integer length math checks overflow before allocating or copying;
- parser state is released on every success and failure path;
- generated strings and slices are owned by the caller and safe to drop once.

Required evidence:

- adversarial stdlib parity tests for compression, ASN.1, cookiejar isolation,
  and JSON-RPC/textproto escape behavior;
- codec tests for CBOR, MessagePack, Avro, YAML, TOML, JSON, CSV, protobuf,
  GraphQL, gRPC, and SIP framing;
- package-local malformed-input tests for archive, image, binary codec, mail,
  XML, GraphQL, and gRPC fail-closed behavior;
- full `examples/testing` and `memory-safety-gate.sh` coverage for release
  claims.

### `crypto-secret`

Applies to crypto, hash, TLS, x509, random, password, and UUID packages.

Required behavior:

- invalid key, nonce, DER, PEM, signature, hash, and UUID lengths fail closed;
- verification helpers do not become signing helpers unless explicitly
  designed;
- secret material is not copied into long-lived logs or unchecked global state;
- constant-time helpers handle length mismatch without leaking or crashing;
- TLS helpers verify by default, while insecure helpers stay loud and opt-in.

Required evidence:

- crypto vector tests for HKDF, hashes, CRC64, RSA verify-only behavior, and
  constant-time comparisons;
- password/hash malformed-input tests;
- TLS pool invalid argument, timeout, host-injection, and close tests;
- UUID/ULID parse and byte-length parity tests.

### `handle-lifecycle`

Applies to database, IO, network, HTTP, messaging, OS, sync, timer, plugin,
runtime, syscall, and other handle-backed packages.

Required behavior:

- close/free/drop operations are idempotent or fail closed for invalid IDs;
- allocation, registration, and initialization unwind every acquired resource on
  each error path;
- timeout and cancel paths do not orphan tasks, fds, sockets, statements, or
  rows;
- unsafe boundaries stay explicit and outside the default safe claim until
  checked-handle hardening covers them.

Required evidence:

- SQL rows/statements close and invalid-handle tests;
- adapter/messaging double-free and leak-scope tests;
- file IO adversarial leak tests;
- socket, UDP, TLS, watch, syscall, plugin, runtime stats, timer, and queue
  lifecycle tests;
- long-run ownership/RSS soak in `memory-safety-gate.sh`.

## Release Rule

For a release to claim stdlib safety coverage, all of the following must pass:

- `scripts/stdlib-safety-audit.sh`;
- `scripts/memory-safety-gate.sh`;
- full `examples/testing` on the default backend;
- full `examples/testing --backend c`;
- strict speed gate when release notes claim faster-than-Rust kernels.
