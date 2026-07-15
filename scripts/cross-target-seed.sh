#!/usr/bin/env bash
# Cross-target validation seed (FreeBSD / RISC-V / multi-arch).
# Does not require foreign hosts: lists triples, checks toolchain hints.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
MAKO="${MAKO:-./target/release/mako}"
if [[ ! -x "$MAKO" ]]; then
  MAKO="$(command -v mako || true)"
fi

echo "mako cross-target seed"
echo "  host: $(uname -s 2>/dev/null || echo unknown)-$(uname -m 2>/dev/null || echo unknown)"

TARGETS=(
  x86_64-unknown-linux-gnu
  aarch64-unknown-linux-gnu
  x86_64-apple-darwin
  aarch64-apple-darwin
  x86_64-pc-windows-gnu
  wasm32-wasip1
  riscv64gc-unknown-linux-gnu
  x86_64-unknown-freebsd
  aarch64-unknown-freebsd
)

echo "=== target triples (hint) ==="
for t in "${TARGETS[@]}"; do
  echo "  - $t"
done

if [[ -n "${MAKO}" && -x "$MAKO" ]]; then
  echo "=== mako version ==="
  "$MAKO" version -v 2>/dev/null || "$MAKO" --version || true
  echo "=== dry cross help ==="
  echo "  example: mako build --target riscv64gc-unknown-linux-gnu main.mko"
  echo "  example: mako build --target x86_64-unknown-freebsd main.mko"
else
  echo "  mako: not built (optional for this seed)"
fi

if command -v rustc >/dev/null 2>&1; then
  echo "=== rustc known targets (filter) ==="
  rustc --print target-list 2>/dev/null | grep -E 'riscv64|freebsd|wasm32-wasip1|aarch64-unknown-linux' | head -20 || true
else
  echo "  rustc: missing"
fi

if command -v zig >/dev/null 2>&1; then
  echo "  zig: $(zig version 2>/dev/null || echo present) (useful for --target cross)"
else
  echo "  zig: missing (optional for cross-compile)"
fi

echo "cross-target-seed: ok (no foreign host required)"
