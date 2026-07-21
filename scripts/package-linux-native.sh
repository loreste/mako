#!/usr/bin/env bash
# Package an already-built Linux Mako binary for simple installation.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-$ROOT/target/release/mako}"
OUT="${2:-$ROOT/dist}"
VERSION="$(sed -n 's/^version = "\([^"]*\)"/\1/p' "$ROOT/Cargo.toml" | head -1)"
ARTIFACT="mako-${VERSION}-x86_64-unknown-linux-gnu"
STAGE="$OUT/$ARTIFACT"
[[ -x "$BIN" ]] || { echo "error: executable not found: $BIN" >&2; exit 1; }
command -v readelf >/dev/null 2>&1 || { echo "error: readelf is required" >&2; exit 1; }
readelf -h "$BIN" 2>/dev/null | grep -q 'Class:.*ELF64' || {
  echo "error: $BIN is not an ELF64 Linux executable" >&2; exit 1;
}
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/share/mako/runtime" "$STAGE/share/mako/std"
install -m 0755 "$BIN" "$STAGE/bin/mako"
cp "$ROOT"/runtime/*.h "$STAGE/share/mako/runtime/"
cp -R "$ROOT/std/." "$STAGE/share/mako/std/"
cp "$ROOT/README.md" "$STAGE/README.md"
cp "$ROOT/scripts/install.sh" "$STAGE/install.sh"
chmod +x "$STAGE/install.sh"
mkdir -p "$OUT"
tar --owner=0 --group=0 -C "$OUT" -czf "$OUT/$ARTIFACT.tar.gz" "$ARTIFACT"
if command -v sha256sum >/dev/null 2>&1; then sha256sum "$OUT/$ARTIFACT.tar.gz" > "$OUT/$ARTIFACT.tar.gz.sha256"; else shasum -a 256 "$OUT/$ARTIFACT.tar.gz" > "$OUT/$ARTIFACT.tar.gz.sha256"; fi
rm -rf "$STAGE"
echo "created $OUT/$ARTIFACT.tar.gz"
