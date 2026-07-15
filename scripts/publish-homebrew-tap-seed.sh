#!/usr/bin/env bash
# homebrew-core / private-tap publish seed. Prints the steps; does not PR.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FORMULA="$ROOT/Formula/mako.rb"
echo "mako publish-homebrew seed"
if [[ ! -f "$FORMULA" ]]; then
  echo "error: missing $FORMULA" >&2
  exit 1
fi
grep -q 'class Mako' "$FORMULA"
VER=$(grep -E 'version |url ' "$FORMULA" | head -5 || true)
echo "  formula: ok"
echo "  next steps:"
echo "    1. Tag a release and publish source tarball / bottle"
echo "    2. Update url/sha256 in Formula/mako.rb or homebrew-core formula"
echo "    3. brew audit --strict mako && brew test mako"
echo "    4. Open PR to homebrew-core (external; requires maintainer review)"
echo "publish-homebrew-tap-seed: ok"
