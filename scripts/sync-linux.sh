#!/usr/bin/env bash
# Sync this checkout to the Linux verification box.
#
# Use this rather than an ad-hoc rsync. A narrow `rsync runtime/ host:runtime/`
# with no excludes copies the host-built third_party archives — including
# runtime/third_party/quiche/target/release/libquiche.a — over the remote ones,
# which produces "archive has no index" at link time and takes the whole
# memory-safety gate down. The excludes below are the reason this script exists.
#
#   ./scripts/sync-linux.sh                # sync only
#   ./scripts/sync-linux.sh --build        # sync, then rebuild release remotely
#
# Host: MAKO_LINUX_HOST (default root@169.58.67.132), path: MAKO_LINUX_PATH.
set -euo pipefail

HOST="${MAKO_LINUX_HOST:-root@169.58.67.132}"
DEST="${MAKO_LINUX_PATH:-/root/mako}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

rsync -az --delete \
  --exclude 'target/' \
  --exclude '.git/' \
  --exclude '*.dSYM/' \
  --exclude 'out/' \
  --exclude 'dist/' \
  --exclude '.tokensave/' \
  --exclude '.mako/' \
  --exclude 'node_modules/' \
  "$ROOT/" "$HOST:$DEST/"
echo "synced $ROOT -> $HOST:$DEST"

if [[ "${1:-}" == "--build" ]]; then
  # rsync preserves mtimes, so cargo would otherwise consider the tree unchanged.
  ssh "$HOST" "cd $DEST && find src runtime build.rs Cargo.toml -type f -exec touch {} + \
    && PATH=/root/.cargo/bin:\$PATH cargo build --release"
  echo "remote release build complete"
fi
