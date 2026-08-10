#!/usr/bin/env bash
# Integration test for the Mako DAP adapter (`mako dap`).
#
# Drives a scripted DAP session (scripts/dap_drive.py) against
# examples/testing/debug_probe.mko: build-on-launch, source breakpoint at the
# `bp-line` marker, stack frames in .mko source, MakoString/MakoIntArray
# formatting, clean disconnect with no orphan lldb-dap/inferior processes.
#
# Skips (exit 0 with a note) when lldb-dap is unavailable.
set -euo pipefail
cd "$(dirname "$0")/.."

MAKO_BIN="${MAKO_BIN:-./target/release/mako}"
PROBE="examples/testing/debug_probe.mko"

if [[ ! -x "$MAKO_BIN" ]]; then
    echo "test-dap: $MAKO_BIN not found (run cargo build --release first)" >&2
    exit 1
fi

# Locate lldb-dap the same way the adapter does.
find_lldb_dap() {
    if [[ -n "${MAKO_LLDB_DAP:-}" && -f "$MAKO_LLDB_DAP" ]]; then
        echo "$MAKO_LLDB_DAP"
        return 0
    fi
    if [[ "$(uname)" == "Darwin" ]] && command -v xcrun >/dev/null; then
        xcrun -f lldb-dap 2>/dev/null && return 0
    fi
    for n in lldb-dap lldb-dap-21 lldb-dap-20 lldb-dap-19 lldb-dap-18 lldb-dap-17 lldb-dap-16; do
        if command -v "$n" >/dev/null; then
            command -v "$n"
            return 0
        fi
    done
    return 1
}

if ! find_lldb_dap >/dev/null; then
    echo "test-dap: SKIP (lldb-dap not found)"
    exit 0
fi

BP_LINE=$(awk '/bp-line/{print NR}' "$PROBE")
if [[ -z "$BP_LINE" ]]; then
    echo "test-dap: FAIL (no bp-line marker in $PROBE)" >&2
    exit 1
fi

python3 scripts/dap_drive.py "$MAKO_BIN" "$(pwd)/$PROBE" "$BP_LINE"

# Negative: launching a source with a type error must produce a DAP error
# response (not a hang, not a silent success).
python3 scripts/dap_drive.py "$MAKO_BIN" \
    "$(pwd)/examples/bad/assign_string_to_int.mko" 0 --expect-build-fail

if pgrep -f "lldb-dap.*mako_dbg" >/dev/null 2>&1; then
    echo "test-dap: FAIL (orphan lldb-dap process)" >&2
    exit 1
fi
echo "test-dap: OK"
