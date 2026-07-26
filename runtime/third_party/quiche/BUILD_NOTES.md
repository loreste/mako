# Quiche FFI + HTTP/3 mux (2026-07-09)

## Verified

| Builtin | Result |
|---------|--------|
| `quiche_h3_get` | `h3:200;mako-quic-ok` |
| `quiche_h3_post` | `h3:405;` (stock GET-only app) |
| **`quiche_h3_get_two(host, port, path1, path2, sni, verify)`** | `h3:200;mako-quic-ok\|200;health` |

Mux submits two `quiche_h3_send_request` calls before pumping UDP; responses keyed by stream id.

```bash
MAKO_LIVE_QUIC=1 cargo run --quiet -- test examples/testing/quiche_link_test.mko -v
```

## Milestone note

Client-side TLS/h2/h3 **seeds are sufficient at ~99%**. Next highest leverage is packaging/release and product APIs, not more one-off FFI probes (push, echo-POST servers, etc.).

## Rebuilding libquiche — symbol collision with OpenSSL

`libquiche.a` statically contains BoringSSL. BoringSSL defines `SSL_CTX_new`,
`SSL_new`, `SSL_free`, `BIO_new_file` and roughly twelve thousand other symbols
under the **same names as OpenSSL**, which Mako links for its own TLS.

A naive rebuild:

```bash
cd runtime/third_party/quiche/src
cargo build --release --package quiche --features ffi
```

produces an archive that exports all of them. The linker then binds Mako's
OpenSSL calls to BoringSSL's implementations, and passing an OpenSSL
`TLS_server_method()` pointer into BoringSSL's `SSL_CTX_new` segfaults inside
`ssl_lib.cc`. It presents as a crash in `mako_tls_server_new` with no
diagnostic, and it takes out every TLS-touching fixture — `proxy_pool_test`,
`h3_server_test` — while `quiche_link_test` still passes, because that path
only calls `quiche_*`.

Only the 169 `quiche_*` symbols are needed from this archive.

Things that do **not** fix it:

- `-Wl,--exclude-libs,libquiche.a` — stops the symbols being re-exported, but
  the static archive has already satisfied the reference at link time. `nm`
  shows `t SSL_CTX_new` (local) and no undefined OpenSSL reference: the wrong
  binding is already baked in.
- Partial-link then `objcopy --keep-global-symbols` — the archive contains
  duplicate object members, so `ld -r --whole-archive` fails with multiple
  definitions before `objcopy` runs.

The fix belongs at BoringSSL build time: build with `BORINGSSL_PREFIX` so its
symbols are namespaced and cannot collide. The archive that shipped with this
tree did not have this problem, so it was produced that way.

Until a prefixed archive is rebuilt, removing `libquiche.a` is a valid state —
`build.rs` treats quiche as optional and falls back to H3 header stubs.
