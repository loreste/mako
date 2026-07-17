# Mako ABI And Plugin Seed

**Product tip:** **0.2.0**.

Mako's current FFI is `extern "C"` plus the runtime C headers. The plugin ABI
seed gives native dynamic plugins and future WASM plugins one stable handshake.

## Native ABI v1

Header: `runtime/mako_plugin.h`

- API string: `mako.plugin.v1`
- ABI version: `1`
- Native entrypoint: `mako_plugin_entry`
- Required exported symbol type: `const MakoPluginVTable *mako_plugin_entry(void)`
- String ownership: plugin-returned strings are released with `free_string` when
  the plugin provides it

The ABI surface is intentionally small:

- `MakoPluginInfo`: name, version, kind, ABI version
- `MakoPluginHost`: host callbacks, currently logging and opaque user data
- `MakoPluginVTable`: init, shutdown, call, string free callback

Generate a native skeleton:

```bash
mako deploy plugin my-plugin --name my-plugin --kind native
cd my-plugin
./build-plugin.sh
```

## WASM Plugin Seed

Generate a WASM plugin manifest/skeleton:

```bash
mako deploy plugin my-wasm-plugin --name my-wasm-plugin --kind wasm
```

The WASM skeleton documents exported ABI-version and call functions. Full WASM
component-model adapters, WIT generation, capability negotiation, and host-side
dynamic loading remain roadmap work.

## Boundary

Done now:

- Stable ABI header seed
- Native plugin skeleton generator
- WASM plugin manifest/skeleton generator
- Release archives include the ABI docs/header

Not done yet:

- Runtime `dlopen` / `LoadLibrary` host loader
- Plugin registry, signing, sandboxing, or permission prompts
- WASM component-model host adapter
- Language-level `plugin import` syntax
