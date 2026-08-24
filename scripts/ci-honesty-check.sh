#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

if grep -R -nE 'continue-on-error:[[:space:]]*true' "$ROOT/.github/workflows"; then
  echo "ci-honesty-check: continue-on-error is not allowed in workflows; use a hard gate or explicit quarantine" >&2
  exit 1
fi

if [[ ! -s "$ROOT/ci/windows-quarantine.txt" ]]; then
  echo "ci-honesty-check: Windows quarantine file is missing or empty" >&2
  exit 1
fi

if ! grep -qE 'Windows quarantined full suite \(hard gate\)' "$ROOT/.github/workflows/ci.yml"; then
  echo "ci-honesty-check: Windows full suite must run through the hard quarantine gate" >&2
  exit 1
fi

bash "$ROOT/scripts/windows-quarantine-gate.sh" --self-test
echo "ci-honesty-check: workflow failure policy is explicit"
