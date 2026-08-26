#!/usr/bin/env bash
# CI helper: ephemeral Redis via Docker, run mako live ping/set/get, tear down.
# Requires Docker + cargo. Exit 0 on success.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="${MAKO_REDIS_CONTAINER:-mako-ci-redis}"
PORT="${MAKO_REDIS_PORT:-6380}"
IMAGE="${MAKO_REDIS_IMAGE:-redis:7-alpine}"

cleanup() {
  docker stop "$NAME" >/dev/null 2>&1 || true
}
trap cleanup EXIT

docker rm -f "$NAME" >/dev/null 2>&1 || true
docker run -d --rm --name "$NAME" -p "${PORT}:6379" "$IMAGE" >/dev/null

echo "waiting for redis on :${PORT}..."
for i in $(seq 1 20); do
  if docker exec "$NAME" redis-cli ping 2>/dev/null | grep -q PONG; then
    break
  fi
  sleep 1
  if [[ "$i" -eq 20 ]]; then
    echo "error: redis not ready" >&2
    exit 1
  fi
done

export MAKO_REDIS_PORT="$PORT"
cd "$ROOT"
cargo run --quiet --bin mako -- run examples/ci/redis_live.mko
echo "ok: ci-redis (port ${PORT})"
