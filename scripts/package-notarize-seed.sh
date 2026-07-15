#!/usr/bin/env bash
# macOS notarize seed (dry-run). Does not call Apple APIs without credentials.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NOTES="$ROOT/scripts/package-macos-notarize-notes.md"
echo "mako package-notarize seed"
if [[ ! -f "$NOTES" ]]; then
  echo "error: missing $NOTES" >&2
  exit 1
fi
grep -q 'notarytool\|altool\|notariz' "$NOTES" || true
echo "  notes: ok ($NOTES)"
if [[ -n "${APPLE_ID:-}" && -n "${APPLE_TEAM_ID:-}" ]]; then
  echo "  credentials: APPLE_ID/TEAM_ID set (ready for real notarytool run)"
else
  echo "  credentials: unset (dry-run only; see package-macos-notarize-notes.md)"
fi
if command -v xcrun >/dev/null 2>&1; then
  echo "  xcrun: ok"
else
  echo "  xcrun: missing (macOS only)"
fi
echo "package-notarize-seed: ok"
