#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MAKO=${MAKO_BIN:-$ROOT/target/release/mako}
QUARANTINE=${MAKO_WINDOWS_QUARANTINE:-$ROOT/ci/windows-quarantine.txt}

normalize_quarantine() {
  sed -E 's/#.*$//; s/^[[:space:]]+//; s/[[:space:]]+$//' "$QUARANTINE" |
    awk 'NF { print }' |
    sed 's#\\#/#g' |
    sort -u
}

extract_failures() {
  grep -E '(FAIL[[:space:]]+.*examples/testing[\\/].*\.mko$|error: .*examples/testing[\\/].*\.mko: test process exited|examples/testing[\\/].*\.mko: [0-9]+$)' "$1" |
    sed -E 's#.*(examples/testing[\\/][^:[:space:]]+\.mko).*#\1#; s#\\#/#g' |
    sort -u
}

run_suite() {
  local log="$1"
  set +e
  "$MAKO" test "$ROOT/examples/testing" 2>&1 | tee "$log"
  local rc=${PIPESTATUS[0]}
  return "$rc"
}

self_test() {
  local tmp
  tmp=$(mktemp -d "${TMPDIR:-/tmp}/mako-win-quarantine.XXXXXX")
  trap 'rm -rf "$tmp"' RETURN

  cat >"$tmp/quarantine.txt" <<'EOF'
examples/testing/known_test.mko
examples/testing/recovered_test.mko
EOF
  cat >"$tmp/log.txt" <<'EOF'
FAIL examples/testing\known_test.mko
error: D:/a/mako/mako/examples/testing\known_test.mko: test process exited code 1
FAIL examples/testing\unknown_test.mko
D:/a/mako/mako/examples/testing\unknown_test.mko: 1
EOF
  QUARANTINE="$tmp/quarantine.txt"
  normalize_quarantine >"$tmp/expected.txt"
  extract_failures "$tmp/log.txt" >"$tmp/failed.txt"
  comm -23 "$tmp/failed.txt" "$tmp/expected.txt" >"$tmp/unknown.txt"
  comm -13 "$tmp/failed.txt" "$tmp/expected.txt" >"$tmp/recovered.txt"
  grep -q 'examples/testing/unknown_test.mko' "$tmp/unknown.txt"
  grep -q 'examples/testing/recovered_test.mko' "$tmp/recovered.txt"
  echo "windows-quarantine-gate: self-test passed"
}

if [[ "${1:-}" == "--self-test" ]]; then
  self_test
  exit 0
fi

if [[ ! -x "$MAKO" ]]; then
  echo "windows-quarantine-gate: missing executable $MAKO" >&2
  exit 1
fi
if [[ ! -f "$QUARANTINE" ]]; then
  echo "windows-quarantine-gate: missing quarantine file $QUARANTINE" >&2
  exit 1
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/mako-win-quarantine.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

normalize_quarantine >"$tmp/quarantine.txt"
set +e
run_suite "$tmp/suite.log"
suite_rc=$?
set -e
extract_failures "$tmp/suite.log" >"$tmp/failed.txt"

comm -23 "$tmp/failed.txt" "$tmp/quarantine.txt" >"$tmp/unknown.txt"
comm -12 "$tmp/failed.txt" "$tmp/quarantine.txt" >"$tmp/quarantined.txt"
comm -13 "$tmp/failed.txt" "$tmp/quarantine.txt" >"$tmp/recovered.txt"

if [[ -s "$tmp/unknown.txt" ]]; then
  echo "windows-quarantine-gate: unknown Windows failures:" >&2
  cat "$tmp/unknown.txt" >&2
  exit 1
fi

if [[ -s "$tmp/recovered.txt" ]]; then
  echo "windows-quarantine-gate: quarantined fixtures now pass; remove them from ci/windows-quarantine.txt:" >&2
  cat "$tmp/recovered.txt" >&2
  exit 1
fi

if [[ "$suite_rc" -eq 0 && -s "$tmp/quarantine.txt" ]]; then
  echo "windows-quarantine-gate: full suite passed but quarantine is not empty" >&2
  cat "$tmp/quarantine.txt" >&2
  exit 1
fi

if [[ "$suite_rc" -ne 0 && ! -s "$tmp/failed.txt" ]]; then
  echo "windows-quarantine-gate: suite failed without parsable fixture failures" >&2
  tail -80 "$tmp/suite.log" >&2
  exit 1
fi

echo "windows-quarantine-gate: quarantined failures:"
cat "$tmp/quarantined.txt"
echo "windows-quarantine-gate: no unknown Windows failures"
