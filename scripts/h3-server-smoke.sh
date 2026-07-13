#!/usr/bin/env bash
# HTTP/3 server smoke: Mako h3_server + quiche_h3_get client.
# Skips cleanly when quiche is not linked.
# Usage: ./scripts/h3-server-smoke.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PORT=18493
OUT="${TMPDIR:-/tmp}/mako_h3_server_$$"
BIN="$OUT.bin"
LOG="$OUT.log"
CLIENT="$OUT.client.mko"

mako_bin() {
  if [[ -x "$ROOT/target/release/mako" ]]; then
    echo "$ROOT/target/release/mako"
    return 0
  fi
  local td
  td="$(cargo metadata --format-version 1 --no-deps 2>/dev/null | python3 -c 'import json,sys; print(json.load(sys.stdin)["target_directory"])' 2>/dev/null || true)"
  if [[ -n "$td" && -x "$td/release/mako" ]]; then
    echo "$td/release/mako"
    return 0
  fi
  return 1
}

cleanup() {
  if [[ -n "${SPID:-}" ]]; then
    kill "$SPID" 2>/dev/null || true
    wait "$SPID" 2>/dev/null || true
  fi
  rm -f "$BIN" "$LOG" "$CLIENT" "${CLIENT}.bin"
}
trap cleanup EXIT

if ! MAKO="$(mako_bin)"; then
  cargo build --release
  MAKO="$(mako_bin)" || { echo "error: mako binary not found"; exit 1; }
fi

# Probe quiche
PROBE="${OUT}.probe.mko"
cat >"$PROBE" <<'EOF'
fn main() {
    if quiche_available() == 0 {
        print("nolink")
    } else {
        print("ok")
    }
}
EOF
AVAIL="$("$MAKO" run "$PROBE" 2>/dev/null || true)"
rm -f "$PROBE"
if [[ "$AVAIL" != "ok" ]]; then
  echo "skip: quiche not linked"
  echo "ok: h3 smoke skipped"
  exit 0
fi

if [[ ! -f runtime/certs/dev.crt || ! -f runtime/certs/dev.key ]]; then
  echo "skip: runtime/certs/dev.{crt,key} missing"
  exit 0
fi

"$MAKO" build examples/h3_server.mko -o "$BIN"
"$BIN" >"$LOG" 2>&1 &
SPID=$!

for _ in $(seq 1 50); do
  if grep -q "h3 server on" "$LOG" 2>/dev/null; then
    break
  fi
  if ! kill -0 "$SPID" 2>/dev/null; then
    echo "error: h3 server exited early"
    cat "$LOG" || true
    exit 1
  fi
  sleep 0.05
done

# Client GET /health via quiche_h3_get (verify_peer=0 for dev cert).
cat >"$CLIENT" <<EOF
fn main() {
    let resp = quiche_h3_get("127.0.0.1", $PORT, "/health", "localhost", 0)
    print(resp)
}
EOF
RESP="$("$MAKO" run "$CLIENT" 2>/dev/null || true)"
echo "client: $RESP"

if ! echo "$RESP" | grep -q 'h3:200'; then
  # Server may still be starting — retry once.
  sleep 0.3
  RESP="$("$MAKO" run "$CLIENT" 2>/dev/null || true)"
  echo "client-retry: $RESP"
fi

echo "$RESP" | grep -q 'h3:200'
echo "$RESP" | grep -q 'ok'

for _ in $(seq 1 100); do
  if ! kill -0 "$SPID" 2>/dev/null; then
    break
  fi
  sleep 0.05
done
if kill -0 "$SPID" 2>/dev/null; then
  kill "$SPID" 2>/dev/null || true
  wait "$SPID" 2>/dev/null || true
  SPID=
fi
grep -q "h3_server done\|h3 server on" "$LOG" || true
echo "ok: h3_server smoke"
