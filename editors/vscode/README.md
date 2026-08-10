# Mako for VS Code

VS Code support for `.mko` files.

## Features

- Syntax highlighting for Mako.
- Language configuration for comments, brackets, and auto-closing pairs.
- Snippets for `main`, tests, `crew`, `arena`, and HTTP route helpers.
- Commands:
  - `Mako: Check`
  - `Mako: Build`
  - `Mako: Run`
  - `Mako: Test`
  - `Mako: Format Current File`
  - `Mako: Initialize Project`
  - `Mako: Debug Active File`
  - `Mako: Restart Language Server`
- Problem matcher for `file:line:col: error: message` diagnostics.
- Built-in `mako lsp` stdio client for diagnostics, hover, completion,
  definitions, references, rename, code actions, document/workspace symbols,
  and signature help.
- Native debug launch: the `mako-native` debug type spawns `mako dap` (the
  built-in DAP adapter) directly — no CodeLLDB or Microsoft C/C++ extension
  needed, and no `preLaunchTask` (the adapter builds the program on launch).

## Development

Open this directory in VS Code and run the extension host.

The extension expects `mako` on `PATH`. Override with:

```json
{
  "mako.path": "/path/to/mako",
  "mako.lsp.enabled": true
}
```

Use **Mako: Debug Active File** or add a launch config:

```json
{
  "type": "mako-native",
  "request": "launch",
  "name": "Mako: Debug active file",
  "program": "${file}",
  "args": []
}
```

`program` may point at a `.mko` file (the adapter builds it with debug info,
C backend) or at an already-built binary. The adapter locates `lldb-dap` via
`$MAKO_LLDB_DAP`, `xcrun -f lldb-dap` (macOS), or `PATH`. See
[docs/DEBUG.md](../../docs/DEBUG.md).

## Packaging

This scaffold intentionally has no runtime npm dependencies. Package it with
`vsce package` once publisher metadata is finalized.
