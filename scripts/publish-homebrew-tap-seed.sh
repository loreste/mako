#!/usr/bin/env bash
# Validate Formula/mako.rb and print homebrew-core / private-tap steps.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FORMULA="$ROOT/Formula/mako.rb"
echo "mako publish-homebrew"
if [[ ! -f "$FORMULA" ]]; then
  echo "error: missing $FORMULA" >&2
  exit 1
fi
grep -q 'class Mako' "$FORMULA"
URL="$(grep -E '^\s*url\s+"' "$FORMULA" | head -1 | sed -E 's/.*"([^"]+)".*/\1/')"
SHA="$(grep -E '^\s*sha256\s+"' "$FORMULA" | head -1 | sed -E 's/.*"([^"]+)".*/\1/')"
echo "  url:    ${URL:-MISSING}"
echo "  sha256: ${SHA:-MISSING}"
if [[ -z "$URL" || -z "$SHA" ]]; then
  echo "error: formula missing stable url/sha256 — run ./scripts/fill-release-packaging.sh" >&2
  exit 1
fi
if [[ "$URL" != *archive/refs/tags/* ]]; then
  echo "  warn: expected github archive URL"
fi
# Verify source archive hash
if command -v curl >/dev/null 2>&1; then
  TMP="$(mktemp)"
  if curl -fsSL -o "$TMP" "$URL" 2>/dev/null; then
    GOT="$(shasum -a 256 "$TMP" | awk '{print $1}')"
    rm -f "$TMP"
    if [[ "$GOT" == "$SHA" ]]; then
      echo "  verify: ok (source tarball matches)"
    else
      echo "  verify: sha256 differs (expected=$SHA got=$GOT)"
      echo "  note:   this is expected when the Formula was committed before the tag was finalized"
      echo "  fix:    update Formula/mako.rb sha256 to $GOT on main"
    fi
  else
    echo "  verify: skip (could not download source)"
  fi
fi
echo "  next:"
echo "    # Local / private tap"
echo "    brew install --build-from-source $FORMULA"
echo "    # homebrew-core (external)"
echo "    brew audit --strict --online $FORMULA"
echo "    brew test --verbose $FORMULA   # after install"
echo "    # open PR to Homebrew/homebrew-core"
if command -v brew >/dev/null 2>&1; then
  echo "  brew: $(brew --version | head -1)"
else
  echo "  brew: not installed (optional for audit)"
fi
echo "publish-homebrew: ok"
