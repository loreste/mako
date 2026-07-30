#!/usr/bin/env bash
# DTLS 1.2 echo smoke: Mako server (examples/dtls_echo_server.mko) against
# `openssl s_client -dtls1_2`. Skips cleanly when OpenSSL is absent — either
# not linked into the built binary or no `openssl` CLI for the interop client.
# Usage: ./scripts/dtls-smoke.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
# Resolve `pull "dtls"` against this repo's std, not any installed copy.
export MAKO_STD="$ROOT/std"
PORT=19499
OUT="${TMPDIR:-/tmp}/mako_dtls_smoke_$$"
BIN="$OUT.bin"
LOG="$OUT.log"
CLOG="$OUT.client.log"

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

openssl_linked() {
  local bin="$1"
  if command -v otool >/dev/null 2>&1; then
    otool -L "$bin" 2>/dev/null | grep -q '[Ll]ibssl' && return 0
  fi
  if command -v ldd >/dev/null 2>&1; then
    ldd "$bin" 2>/dev/null | grep -q 'libssl' && return 0
  fi
  # Fallback: binary strings / nm
  if strings "$bin" 2>/dev/null | grep -q 'DTLSv1_listen'; then
    return 0
  fi
  return 1
}

openssl_cli() {
  if command -v openssl >/dev/null 2>&1; then
    command -v openssl
    return 0
  fi
  local cand
  for cand in /opt/homebrew/opt/openssl@3/bin/openssl \
              /usr/local/opt/openssl@3/bin/openssl \
              /usr/bin/openssl; do
    if [[ -x "$cand" ]]; then
      echo "$cand"
      return 0
    fi
  done
  return 1
}

cleanup() {
  if [[ -n "${SPID:-}" ]]; then
    kill "$SPID" 2>/dev/null || true
    wait "$SPID" 2>/dev/null || true
  fi
  if [[ -n "${CPID:-}" ]]; then
    kill "$CPID" 2>/dev/null || true
    wait "$CPID" 2>/dev/null || true
  fi
  rm -f "$BIN" "$LOG" "$CLOG"
}
trap cleanup EXIT

if ! MAKO="$(mako_bin)"; then
  cargo build --release
  MAKO="$(mako_bin)" || { echo "error: mako binary not found"; exit 1; }
fi

"$MAKO" build examples/dtls_echo_server.mko -o "$BIN"

if ! openssl_linked "$BIN"; then
  echo "skip: OpenSSL not linked into $BIN (install openssl + rebuild mako)"
  echo "ok: dtls smoke skipped"
  exit 0
fi

if ! OSSL="$(openssl_cli)"; then
  echo "skip: no openssl CLI for the s_client interop half"
  echo "ok: dtls smoke skipped"
  exit 0
fi

if [[ ! -f runtime/certs/dev.crt || ! -f runtime/certs/dev.key ]]; then
  echo "skip: runtime/certs/dev.{crt,key} missing"
  echo "ok: dtls smoke skipped"
  exit 0
fi

"$BIN" >"$LOG" 2>&1 &
SPID=$!

for _ in $(seq 1 50); do
  if grep -q "listening" "$LOG" 2>/dev/null; then
    break
  fi
  if ! kill -0 "$SPID" 2>/dev/null; then
    echo "error: dtls echo server exited early"
    cat "$LOG" || true
    exit 1
  fi
  sleep 0.05
done
grep -q "listening" "$LOG" || { echo "error: server never reported listening"; cat "$LOG" || true; exit 1; }

# Keep stdin open briefly after the ping so s_client waits for the echo.
(printf 'dtls-smoke-ping\n'; sleep 3) | "$OSSL" s_client -dtls1_2 -connect "127.0.0.1:$PORT" -quiet >"$CLOG" 2>&1 &
CPID=$!

found=0
for _ in $(seq 1 100); do
  if grep -q "dtls-smoke-ping" "$CLOG" 2>/dev/null; then
    found=1
    break
  fi
  if ! kill -0 "$CPID" 2>/dev/null; then
    break
  fi
  sleep 0.05
done
kill "$CPID" 2>/dev/null || true
wait "$CPID" 2>/dev/null || true
CPID=

if [[ "$found" != "1" ]]; then
  echo "error: no DTLS echo from openssl s_client"
  echo "--- server log ---"; cat "$LOG" || true
  echo "--- client log ---"; cat "$CLOG" || true
  exit 1
fi
echo "s_client -dtls1_2 echo → dtls-smoke-ping"

for _ in $(seq 1 100); do
  if ! kill -0 "$SPID" 2>/dev/null; then
    break
  fi
  sleep 0.05
done
if kill -0 "$SPID" 2>/dev/null; then
  echo "error: dtls echo server still running"
  exit 1
fi
wait "$SPID" || true
SPID=
grep -q "dtls_smoke: done" "$LOG"
echo "ok: dtls smoke"
