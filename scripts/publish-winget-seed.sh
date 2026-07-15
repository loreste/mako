#!/usr/bin/env bash
# winget-pkgs publish seed. Validates YAML skeleton; does not submit PR.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
YAML="$ROOT/packaging/winget/mako.locale.en-US.yaml"
echo "mako publish-winget seed"
if [[ ! -f "$YAML" ]]; then
  echo "error: missing $YAML" >&2
  exit 1
fi
grep -q 'PackageIdentifier\|PackageVersion\|Installer' "$YAML" || grep -q 'Package' "$YAML"
echo "  yaml: ok ($YAML)"
if grep -q 'REPLACE' "$YAML"; then
  echo "  sha256: REPLACE placeholder still present — fill after release zip"
else
  echo "  sha256: looks filled"
fi
echo "  next: wingetcreate update / PR to microsoft/winget-pkgs (external)"
echo "publish-winget-seed: ok"
