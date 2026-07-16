#!/usr/bin/env bash
# Validate multi-file winget manifests; print PR steps for microsoft/winget-pkgs.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WDIR="$ROOT/packaging/winget"
VER_FILE="$WDIR/loreste.mako.yaml"
INST_FILE="$WDIR/loreste.mako.installer.yaml"
LOC_FILE="$WDIR/loreste.mako.locale.en-US.yaml"
echo "mako publish-winget"
for f in "$VER_FILE" "$INST_FILE" "$LOC_FILE"; do
  if [[ ! -f "$f" ]]; then
    echo "error: missing $f" >&2
    exit 1
  fi
done
# Reject deprecated singleton if still present
if grep -q 'ManifestType: singleton' "$WDIR"/*.yaml 2>/dev/null; then
  echo "error: singleton manifests are rejected by winget-pkgs — use multi-file 1.12.0" >&2
  exit 1
fi
VER="$(grep -E '^PackageVersion:' "$VER_FILE" | awk '{print $2}')"
SHA="$(grep -E 'InstallerSha256:' "$INST_FILE" | awk '{print $2}')"
URL="$(grep -E 'InstallerUrl:' "$INST_FILE" | awk '{print $2}')"
echo "  version: $VER"
echo "  url:     $URL"
if [[ -z "$SHA" || "$SHA" == REPLACE* ]]; then
  echo "  sha256:  MISSING — run ./scripts/fill-release-packaging.sh v${VER}"
  exit 1
fi
echo "  sha256:  $SHA"
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
echo "    2. Copy all three packaging/winget/loreste.mako*.yaml files to:"
echo "       manifests/l/loreste/mako/${VER}/"
echo "    3. Open PR (CLA: @microsoft-github-policy-service agree)"
if command -v wingetcreate >/dev/null 2>&1; then
  echo "  wingetcreate: present — optional: wingetcreate new $URL"
else
  echo "  wingetcreate: not installed (optional)"
fi
echo "publish-winget: ok"
