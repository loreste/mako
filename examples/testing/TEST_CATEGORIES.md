# Test Categories

Tests in this directory are organized by category. All tests MUST pass
in the default `mako test examples/testing` run. Tests that require
optional external dependencies use **soft-skip** — they detect the
missing dep at runtime and return early with PASS (API shape verified,
not full integration).

## Categories

### Core language (always required, no external deps)

All tests not listed below. These test the compiler, type system,
ownership model, codegen, concurrency primitives, and standard library.
Failures here block the release.

### Soft-fallback (API shape only without optional deps)

These verify API surface and error paths when the optional dep is absent.
When the dep IS present, they perform real integration testing.

| Test file | Optional dep | Behavior without dep |
|-----------|-------------|---------------------|
| sql_programming_test.mko | libsqlite3 | Returns -1/empty, verifies param shape |
| quiche_link_test.mko | libquiche | Soft-skip, verifies ABI surface |
| h3_server_test.mko | libquiche | Soft-skip unit tests only |
| crypto_srtp_blocks_test.mko | - | Stub detection, API shape |
| model_weights_test.mko | - | Stub detection, surface |

### Network (require loopback, may soft-skip on restricted envs)

| Test file | Requirement |
|-----------|------------|
| net_ipv6_he_test.mko | IPv6 loopback (::1) |
| net_lowlevel_test.mko | Loopback networking |
| ws_api_test.mko | Loopback networking |

### Live integration (opt-in via env vars)

These perform real network I/O against external or local services.
NOT run by default — require explicit env vars.

| Test file | Env var | What it tests |
|-----------|---------|--------------|
| (TLS tests) | MAKO_LIVE_TLS=1 | Real TLS handshake |
| (QUIC tests) | MAKO_LIVE_QUIC=1 | Real QUIC/HTTP3 |
| (nghttp2 tests) | MAKO_LIVE_NGHTTP2=1 | Real HTTP/2 server |

## Convention

Tests that soft-skip MUST print a diagnostic indicating why:
```
sqlite: not linked (rebuild with -DMAKO_HAS_SQLITE -lsqlite3)
```

This allows CI logs to distinguish "test passed because feature works"
from "test passed because feature was not tested."
