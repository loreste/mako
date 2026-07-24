#!/usr/bin/env bash
# Anneal — offline adaptive-optimization cycle (docs/ADAPTIVE_OPT.md).
#
# Product-named entry point for the adaptive-opt cycle: ship a release build,
# read hot-site guidance from a train run, rebuild offline with PGO, swap. Never
# rewrites a running binary. Same arguments and environment as the underlying
# scripts/adaptive-opt-cycle.sh.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
exec "$here/adaptive-opt-cycle.sh" "$@"
