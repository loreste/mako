#!/usr/bin/env bash
# Validate winget manifest; print PR steps for microsoft/winget-pkgs.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
YAML="$ROOT/packaging/winget/mako.locale.en-US.yaml"
echo "mako publish-winget"
if [[ ! -f "$YAML" ]]; then
  echo "error: missing $YAML" >&2
  exit 1
fi
grep -q 'PackageIdentifier' "$YAML"
grep -q 'InstallerUrl' "$YAML"
VER="$(grep -E '^PackageVersion:' "$YAML" | awk '{print $2}')"
SHA="$(grep -E 'InstallerSha256:' "$YAML" | awk '{print $2}')"
URL="$(grep -E 'InstallerUrl:' "$YAML" | awk '{print $2}')"
echo "  version: $VER"
echo "  url:     $URL"
if [[ -z "$SHA" || "$SHA" == REPLACE* ]]; then
  echo "  sha256:  MISSING — run ./scripts/fill-release-packaging.sh v${VER}"
  exit 1
fi
echo "  sha256:  $SHA"
# Live check: download header and compare sha if network ok
if command -v curl >/dev/null 2>&1; then
  TMP="$(mktemp)"
  if curl -fsSL -o "$TMP" "$URL" 2>/dev/null; then
    GOT="$(shasum -a 256 "$TMP" | awk '{print toupper($1)}')"
    rm -f "$TMP"
    if [[ "$GOT" == "$SHA" ]]; then
      echo "  verify:  ok (matches downloaded zip)"
    else
      echo "  verify:  MISMATCH got=$GOT expected=$SHA" >&2
      exit 1
    fi
  else
    echo "  verify:  skip (could not download installer)"
  fi
fi
echo "  next:"
echo "    1. Fork https://github.com/microsoft/winget-pkgs"
echo "    2. Copy packaging/winget/mako.locale.en-US.yaml →"
echo "       manifests/l/loreste/mako/${VER}/loreste.mako.yaml"
echo "    3. Open PR (winget bots will re-validate)"
if command -v wingetcreate >/dev/null 2>&1; then
  echo "  wingetcreate: present — you can run: wingetcreate new $URL"
else
  echo "  wingetcreate: not installed (optional)"
fi
echo "publish-winget: ok"
