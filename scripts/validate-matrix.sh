#!/usr/bin/env bash
# Multi-OS / multi-arch validation seed (P3).
# On the host: doctor + version + a tiny compile smoke.
# Optionally lists cross triples for zig/clang when present.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
MAKO="${MAKO:-./target/release/mako}"
if [[ ! -x "$MAKO" ]]; then
  MAKO="$(command -v mako || true)"
fi
if [[ -z "${MAKO}" || ! -x "$MAKO" ]]; then
  echo "error: mako binary not found (build or install first)" >&2
  exit 1
fi

echo "=== host ==="
uname -a || true
echo "=== mako version ==="
"$MAKO" version -v || "$MAKO" --version
echo "=== doctor ==="
if [[ "${DOCTOR_STRICT:-0}" == "1" ]]; then
  "$MAKO" doctor
else
  # Soft by default for source checkouts without install-manifest.
  "$MAKO" doctor || echo "doctor: non-zero (set DOCTOR_STRICT=1 to fail)"
fi

SMOKE="$(mktemp /tmp/mako_matrix_XXXX.mko)"
cat > "$SMOKE" <<'EOF'
fn main() {
    print("matrix-ok")
}
EOF
echo "=== smoke run ==="
"$MAKO" run "$SMOKE"
rm -f "$SMOKE"

echo "=== cross targets (hint; not built here) ==="
for t in \
  x86_64-unknown-linux-gnu \
  aarch64-unknown-linux-gnu \
  x86_64-apple-darwin \
  aarch64-apple-darwin \
  x86_64-pc-windows-gnu \
  wasm32-wasip1 \
  riscv64gc-unknown-linux-gnu \
  x86_64-unknown-freebsd \
  aarch64-unknown-freebsd
do
  echo "  - $t   (mako build --target $t …)"
done
if [[ -x "$ROOT/scripts/cross-target-seed.sh" ]]; then
  echo "=== cross-target-seed ==="
  bash "$ROOT/scripts/cross-target-seed.sh" || true
fi

if command -v zig >/dev/null 2>&1; then
  echo "zig: $(zig version 2>/dev/null || echo present)"
else
  echo "zig: missing (optional for cross)"
fi
echo "validate-matrix: ok"
