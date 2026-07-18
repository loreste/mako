#!/usr/bin/env bash
# MSI packaging seed (dry-run friendly). Does not require WiX for validation.
# Full build: STAGE=... candle/light against packaging/windows/mako.wxs
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WXS="$ROOT/packaging/windows/mako.wxs"
echo "mako package-msi seed"
echo "  wxs: $WXS"
if [[ ! -f "$WXS" ]]; then
  echo "error: missing $WXS" >&2
  exit 1
fi
# Structural checks
grep -q 'Product' "$WXS"
grep -q 'INSTALLFOLDER' "$WXS"
grep -q 'mako.exe' "$WXS" || grep -q 'MakoBin' "$WXS"
echo "  wxs: ok (schema present)"
if command -v candle >/dev/null 2>&1 && command -v light >/dev/null 2>&1; then
  echo "  wix: candle/light on PATH (real MSI build possible)"
else
  echo "  wix: missing (install WiX to build MSI; notes in package-msi-notes.md)"
fi
echo "  sign: external (Authenticode via CI secrets — not automated here)"
echo "package-msi-seed: ok"
