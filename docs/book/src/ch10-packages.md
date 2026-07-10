# 10. Packages, workspaces, fmt, test

## `mako.toml`

```toml
[package]
name = "hello"
version = "0.1.0"

[dependencies]
"helper" = { path = "../helper", version = "0.1.0" }
```

Path deps merge at compile time (transitive). Symbols are namespaced by the
dependency key: `"helper" = { path = "…" }` → `helper.add(...)`.

```bash
mako pkg init mylib
mako pkg add helper ../helper
mako pkg list
mako pkg lock
mako pkg audit
mako pkg fetch          # git deps → .mako/deps/ (needs network)
```

Registry-style SemVer resolves from `.mako/registry/<name>/<ver>/` — see
`examples/pkg_manager/`.
Audits are local and reproducible: `mako-cve.toml` supplies advisory ranges, and
`mako-license.toml` supplies allow/deny license policy for locked packages.

## Workspaces

```bash
mako init myws --workspace
cd myws
mako check .
mako run -p app
mako test .
mako fmt .
```

Root:

```toml
[workspace]
members = ["lib", "app"]
```

## Format

```bash
mako fmt path.mko          # stdout
mako fmt -w .              # write
mako fmt -l .              # list dirty
mako fmt -d .              # diff
```

Two or more imports are rewritten as a grouped `import ( … )` block.

## Test

```bash
mako test examples/testing
mako test examples/testing -r TestAdd -v
mako test examples/testing --count 3
```

```mko
fn TestAdd() {
    assert_eq(1 + 1, 2)
}
```

Default suite stays green without live TLS/QUIC. Optional:

```bash
MAKO_LIVE_TLS=1 mako test examples/testing
```

How-tos: [04-packages](../../howto/04-packages.md) ·
[08-testing](../../howto/08-testing.md).

Next: [Speed & memory safety](ch11-speed-safety.md).
