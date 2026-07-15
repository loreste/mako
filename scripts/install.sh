#!/usr/bin/env bash
# Install mako binary + runtime headers under PREFIX.
# Usage:
#   ./scripts/install.sh              # PREFIX=$HOME/.local
#   PREFIX=/usr/local ./scripts/install.sh
#   ./scripts/install.sh --skip-build
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
BIN_DIR="${BIN_DIR:-$PREFIX/bin}"
SHARE_DIR="${SHARE_DIR:-$PREFIX/share/mako}"
RUNTIME_DST="$SHARE_DIR/runtime"
STD_DST="$SHARE_DIR/std"
EDITORS_DST="$SHARE_DIR/editors"
SKIP_BUILD=0
if [[ "${1:-}" == "--skip-build" ]]; then SKIP_BUILD=1; fi

echo "mako install"
echo "  prefix:  $PREFIX"
echo "  bin:     $BIN_DIR"
echo "  runtime: $RUNTIME_DST"
echo "  std:     $STD_DST"

if [[ "$SKIP_BUILD" -ne 1 ]]; then
  echo "Building release binary…"
  (cd "$ROOT" && cargo build --release --quiet)
fi

TARGET_DIR="$(
  cd "$ROOT" && cargo metadata --format-version 1 --no-deps 2>/dev/null \
    | python3 -c 'import json,sys; print(json.load(sys.stdin)["target_directory"])' 2>/dev/null \
    || true
)"
if [[ -z "$TARGET_DIR" ]]; then
  TARGET_DIR="$ROOT/target"
fi
BIN="$TARGET_DIR/release/mako"
if [[ ! -x "$BIN" && -x "$ROOT/bin/mako" ]]; then
  BIN="$ROOT/bin/mako"
fi
if [[ ! -x "$BIN" ]]; then
  echo "error: missing $BIN — run cargo build --release first" >&2
  exit 1
fi
RUNTIME_SRC="$ROOT/runtime"
if [[ ! -f "$RUNTIME_SRC/mako_rt.h" && -f "$ROOT/share/mako/runtime/mako_rt.h" ]]; then
  RUNTIME_SRC="$ROOT/share/mako/runtime"
fi
STD_SRC="$ROOT/std"
if [[ ! -d "$STD_SRC" && -d "$ROOT/share/mako/std" ]]; then
  STD_SRC="$ROOT/share/mako/std"
fi
VSCODE_SRC="$ROOT/editors/vscode"
if [[ ! -d "$VSCODE_SRC" && -d "$ROOT/share/mako/editors/vscode" ]]; then
  VSCODE_SRC="$ROOT/share/mako/editors/vscode"
fi
if [[ ! -f "$RUNTIME_SRC/mako_rt.h" ]]; then
  echo "error: missing runtime/mako_rt.h (looked under checkout and release artifact layout)" >&2
  exit 1
fi

mkdir -p "$BIN_DIR" "$RUNTIME_DST" "$STD_DST"
install -m 755 "$BIN" "$BIN_DIR/mako"

# Headers only (+ certs). Skip vendored quiche trees.
shopt -s nullglob
for h in "$RUNTIME_SRC"/*.h; do
  install -m 644 "$h" "$RUNTIME_DST/"
done
if [[ -d "$RUNTIME_SRC/certs" ]]; then
  mkdir -p "$RUNTIME_DST/certs"
  cp -R "$RUNTIME_SRC/certs/." "$RUNTIME_DST/certs/"
fi
mkdir -p "$RUNTIME_DST/third_party"
if [[ -f "$RUNTIME_SRC/third_party/README.md" ]]; then
  install -m 644 "$RUNTIME_SRC/third_party/README.md" "$RUNTIME_DST/third_party/"
fi
echo "$RUNTIME_DST" > "$SHARE_DIR/runtime_path.txt"

if [[ -d "$STD_SRC" ]]; then
  rm -rf "$STD_DST"
  mkdir -p "$STD_DST"
  cp -R "$STD_SRC/." "$STD_DST/"
fi

if [[ -d "$VSCODE_SRC" ]]; then
  mkdir -p "$EDITORS_DST"
  rm -rf "$EDITORS_DST/vscode"
  cp -R "$VSCODE_SRC" "$EDITORS_DST/vscode"
fi

# Install manifest for doctor / support (P3 polish).
VER_LINE="$("$BIN_DIR/mako" version 2>/dev/null || "$BIN_DIR/mako" --version 2>/dev/null || echo unknown)"
HOST="$(uname -s 2>/dev/null || echo unknown)-$(uname -m 2>/dev/null || echo unknown)"
TS="$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date)"
cat > "$SHARE_DIR/install-manifest.json" <<EOF
{
  "schema": "mako.install.v1",
  "version": "$(printf '%s' "$VER_LINE" | tr -d '\n' | sed 's/"/\\"/g')",
  "prefix": "$(printf '%s' "$PREFIX" | sed 's/"/\\"/g')",
  "host": "$(printf '%s' "$HOST" | sed 's/"/\\"/g')",
  "installedAt": "$TS",
  "runtime": "$(printf '%s' "$RUNTIME_DST" | sed 's/"/\\"/g')",
  "std": "$(printf '%s' "$STD_DST" | sed 's/"/\\"/g')"
}
EOF

echo "Installed $BIN_DIR/mako ($VER_LINE)"
echo "Installed runtime → $RUNTIME_DST"
echo "Installed stdlib  → $STD_DST"
if [[ -d "$EDITORS_DST/vscode" ]]; then
  echo "Installed VS Code scaffold → $EDITORS_DST/vscode"
fi
echo "Manifest: $SHARE_DIR/install-manifest.json"
echo "Discovery: MAKO_RUNTIME → $PREFIX/share/mako/runtime (via binary) → checkout"
echo "Optional: export MAKO_RUNTIME=$RUNTIME_DST"
echo "Verify: $BIN_DIR/mako doctor"
echo "Docs: $ROOT/docs/RELEASE.md"
echo "Done. Ensure $BIN_DIR is on PATH."
