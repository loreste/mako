#!/usr/bin/env bash
# Package a self-contained Mako release artifact (slim by default).
# Usage:
#   ./scripts/package-release.sh [artifact-name]
#   ./scripts/package-release.sh --full mako-x86_64-unknown-linux-gnu
#
# Slim (default): stripped binary + runtime headers + std + install scripts.
# Full: also ships editors + docs snapshot (larger download).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

FULL=0
NAME=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --full) FULL=1; shift ;;
    --slim) FULL=0; shift ;;
    -h|--help)
      echo "Usage: $0 [--slim|--full] [artifact-name]"
      exit 0
      ;;
    *)
      NAME="$1"
      shift
      ;;
  esac
done

if [[ -z "$NAME" ]]; then
  NAME="${ARTIFACT_NAME:-}"
fi
if [[ -z "$NAME" ]]; then
  ARCH="$(uname -m)"
  OS="$(uname -s)"
  case "$OS" in
    Darwin)
      case "$ARCH" in
        arm64|aarch64) NAME="mako-aarch64-apple-darwin" ;;
        *) NAME="mako-x86_64-apple-darwin" ;;
      esac
      ;;
    Linux)
      case "$ARCH" in
        aarch64|arm64) NAME="mako-aarch64-unknown-linux-gnu" ;;
        *) NAME="mako-x86_64-unknown-linux-gnu" ;;
      esac
      ;;
    *)
      echo "error: set ARTIFACT_NAME or pass name as arg" >&2
      exit 1
      ;;
  esac
fi

BIN="$ROOT/target/release/mako"
if [[ ! -x "$BIN" ]]; then
  echo "Building release…"
  cargo build --release --quiet
fi
if [[ ! -x "$BIN" ]]; then
  echo "error: missing $BIN" >&2
  exit 1
fi

DIST="$ROOT/dist"
STAGE="$DIST/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/share/mako/runtime" "$STAGE/share/mako/std" "$STAGE/scripts"

# Strip debug symbols for a smaller download (best-effort).
cp "$BIN" "$STAGE/bin/mako"
chmod +x "$STAGE/bin/mako"
if command -v strip >/dev/null 2>&1; then
  strip "$STAGE/bin/mako" 2>/dev/null || true
fi

for h in "$ROOT"/runtime/*.h; do
  cp "$h" "$STAGE/share/mako/runtime/"
done
if [[ -d "$ROOT/runtime/certs" ]]; then
  mkdir -p "$STAGE/share/mako/runtime/certs"
  cp -R "$ROOT/runtime/certs/." "$STAGE/share/mako/runtime/certs/"
fi
if [[ -d "$ROOT/std" ]]; then
  cp -R "$ROOT/std/." "$STAGE/share/mako/std/"
fi

# Always ship a tiny docs stub (not the whole docs/ tree).
mkdir -p "$STAGE/share/mako/docs"
if [[ -f "$ROOT/README.md" ]]; then
  cp "$ROOT/README.md" "$STAGE/share/mako/docs/README.md"
fi

if [[ "$FULL" -eq 1 ]]; then
  if [[ -d "$ROOT/editors/vscode" ]]; then
    mkdir -p "$STAGE/share/mako/editors"
    cp -R "$ROOT/editors/vscode" "$STAGE/share/mako/editors/vscode"
  fi
  if [[ -f "$ROOT/CHANGELOG.md" ]]; then
    cp "$ROOT/CHANGELOG.md" "$STAGE/share/mako/docs/CHANGELOG.md"
  fi
  if [[ -d "$ROOT/docs" ]]; then
    mkdir -p "$STAGE/share/mako/docs/docs"
    cp -R "$ROOT/docs/." "$STAGE/share/mako/docs/docs/"
  fi
fi

cp "$ROOT/scripts/install.sh" "$STAGE/scripts/install.sh"
cp "$ROOT/scripts/uninstall.sh" "$STAGE/scripts/uninstall.sh"
cp "$ROOT/scripts/install.ps1" "$STAGE/scripts/install.ps1"
cp "$ROOT/scripts/uninstall.ps1" "$STAGE/scripts/uninstall.ps1"
cp "$ROOT/scripts/install-release.sh" "$STAGE/scripts/install-release.sh"
cp "$ROOT/scripts/install-linux.sh" "$STAGE/scripts/install-linux.sh"
chmod +x "$STAGE/scripts/install.sh" "$STAGE/scripts/uninstall.sh" \
  "$STAGE/scripts/install-release.sh" "$STAGE/scripts/install-linux.sh"

MODE_LABEL="slim"
[[ "$FULL" -eq 1 ]] && MODE_LABEL="full"

cat > "$STAGE/README.txt" << EOF
Mako release layout ($NAME) — $MODE_LABEL package

  bin/mako                 — compiler CLI (stripped)
  share/mako/runtime/      — C runtime headers (required to compile .mko)
  share/mako/std/          — standard library sources
  scripts/install.sh       — install this artifact into PREFIX
  scripts/install-release.sh — one-shot download + verify + install
  scripts/install-linux.sh — Linux alias for install-release.sh
  scripts/uninstall.sh     — remove files installed under PREFIX

One-shot install (from GitHub Releases):
  curl -fsSL https://github.com/loreste/mako/releases/latest/download/install-linux.sh | bash

From this unpacked tree:
  PREFIX=\$HOME/.local ./scripts/install.sh --skip-build

Requires a C compiler on the target machine (clang). No Rust toolchain needed.
Docs: https://github.com/loreste/mako/blob/main/docs/RELEASE.md
EOF

tar -C "$DIST" -czf "$DIST/$NAME.tar.gz" "$NAME"
(
  cd "$DIST"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$NAME.tar.gz" "$NAME/bin/mako" > "$NAME.sha256"
  else
    shasum -a 256 "$NAME.tar.gz" "$NAME/bin/mako" > "$NAME.sha256"
  fi
)
# Convenience copies for GitHub Releases
cp "$STAGE/bin/mako" "$DIST/$NAME"
cp "$ROOT/scripts/install-release.sh" "$DIST/install-release.sh"
cp "$ROOT/scripts/install-linux.sh" "$DIST/install-linux.sh"
chmod +x "$DIST/install-release.sh" "$DIST/install-linux.sh"

BYTES="$(wc -c < "$DIST/$NAME.tar.gz" | tr -d ' ')"
echo "Packed $DIST/$NAME.tar.gz ($BYTES bytes, $MODE_LABEL)"
echo "Also: $DIST/$NAME, $DIST/$NAME.sha256, install-release.sh, install-linux.sh"
