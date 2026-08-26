#!/usr/bin/env bash
# Smoke-test the normal-user tooling surface for release candidates.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

MAKO="${MAKO:-$ROOT/target/release/mako}"
if [[ ! -x "$MAKO" && -x "$MAKO.exe" ]]; then
  MAKO="$MAKO.exe"
fi
if [[ ! -x "$MAKO" ]]; then
  echo "tooling-smoke: missing $MAKO; run cargo build --release first" >&2
  exit 1
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/mako-tooling-smoke.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

echo "tooling-smoke: version"
"$MAKO" version | grep -E '^makori version makori[0-9]+\.[0-9]+\.[0-9]+ [^/]+/[^/]+$' >/dev/null
"$MAKO" --version | grep -E '^makori version makori[0-9]+\.[0-9]+\.[0-9]+ [^/]+/[^/]+$' >/dev/null

echo "tooling-smoke: doctor"
MAKO_RUNTIME="$ROOT/runtime" MAKO_STD="$ROOT/std" "$MAKO" doctor

echo "tooling-smoke: fmt"
cat >"$TMP/formatted.mko" <<'MKO'
fn main() {
    let x = 1
    assert_eq(x, 1)
}
MKO
"$MAKO" fmt --check "$TMP/formatted.mko"
cat >"$TMP/unformatted.mko" <<'MKO'
fn main() {
let x = 1
assert_eq(x, 1)
}
MKO
if "$MAKO" fmt --check "$TMP/unformatted.mko" >/dev/null 2>&1; then
  echo "tooling-smoke: fmt --check unexpectedly accepted unformatted input" >&2
  exit 1
fi
"$MAKO" fmt -w "$TMP/unformatted.mko"
"$MAKO" fmt --check "$TMP/unformatted.mko"

echo "tooling-smoke: lint"
"$MAKO" lint --identity examples/testing/tooling_quality_test.mko
"$MAKO" lint --security examples/testing/tooling_quality_test.mko
mkdir -p "$TMP/empty"
if "$MAKO" lint "$TMP/empty" >/dev/null 2>&1; then
  echo "tooling-smoke: lint unexpectedly accepted an empty source tree" >&2
  exit 1
fi

echo "tooling-smoke: doc"
"$MAKO" doc examples/testing/tooling_quality_test.mko --out "$TMP/api"
test -f "$TMP/api/index.md"
test -f "$TMP/api/examples.md"
test -f "$TMP/api/search-index.json"

echo "tooling-smoke: test"
"$MAKO" test examples/testing/tooling_quality_test.mko

echo "tooling-smoke: ok"
