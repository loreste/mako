#!/usr/bin/env bash
# CI helper: ephemeral Postgres via Docker, run mako live connect test, tear down.
# Requires Docker + cargo. Exit 0 on success.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="${MAKO_PG_CONTAINER:-mako-ci-pg}"
PORT="${MAKO_PG_PORT:-5433}"
IMAGE="${MAKO_PG_IMAGE:-postgres:16-alpine}"

cleanup() {
  docker stop "$NAME" >/dev/null 2>&1 || true
}
trap cleanup EXIT

docker rm -f "$NAME" >/dev/null 2>&1 || true
docker run -d --rm --name "$NAME" \
  -e POSTGRES_PASSWORD=mako \
  -e POSTGRES_USER=mako \
  -e POSTGRES_DB=mako \
  -p "${PORT}:5432" \
  "$IMAGE" >/dev/null

echo "waiting for postgres on :${PORT}..."
for i in $(seq 1 30); do
  if docker exec "$NAME" pg_isready -U mako -d mako >/dev/null 2>&1; then
    break
  fi
  sleep 1
  if [[ "$i" -eq 30 ]]; then
    echo "error: postgres not ready" >&2
    exit 1
  fi
done

export MAKO_PG_PORT="$PORT"
cd "$ROOT"
cargo run --quiet --bin mako -- run examples/ci/pg_live.mko
echo "ok: ci-postgres (port ${PORT})"
