#!/usr/bin/env bash
set -euo pipefail

# Check every checked-in Mako stdlib package independently. This catches stale
# wrappers, reserved-name collisions, and references to removed builtins even
# when no application imports that package in the main test suite.
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MAKO=${MAKO_BIN:-$ROOT/target/release/mako}
CACHE=${MAKO_CACHE:-/tmp/mako-stdlib-gate-cache-$(date +%s)}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/mako-stdlib-gate.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

if [[ ! -x "$MAKO" ]]; then
    echo "stdlib-gate: missing executable $MAKO (run cargo build --release first)" >&2
    exit 1
fi

count=0
failures=0
while IFS= read -r file; do
    count=$((count + 1))
    if ! MAKO_CACHE="$CACHE/$count" "$MAKO" check "$ROOT/$file" \
        >"$TMP/$count.out" 2>&1; then
        echo "stdlib-gate: FAIL $file" >&2
        sed -n '1,40p' "$TMP/$count.out" >&2
        failures=$((failures + 1))
    fi
done < <(cd "$ROOT" && find std -type f -name '*.mko' -print | sort)

if [[ $count -eq 0 || $failures -ne 0 ]]; then
    echo "stdlib-gate: checked $count file(s), $failures failure(s)" >&2
    exit 1
fi
echo "stdlib-gate: checked $count file(s), 0 failures"
