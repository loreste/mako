#!/usr/bin/env bash
# Linux one-shot installer entrypoint.
# Same as install-release.sh; kept so docs/mirrors can link a Linux-specific name.
#
#   curl -fsSL https://github.com/loreste/mako/releases/latest/download/install-linux.sh | bash
#
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f "$HERE/install-release.sh" ]]; then
  exec bash "$HERE/install-release.sh" "$@"
fi
# When piped from GitHub releases without sibling scripts, re-fetch the full installer.
curl -fsSL "${MAKO_RELEASE_BASE_URL:-https://github.com/loreste/mako/releases/latest/download}/install-release.sh" \
  | bash -s -- "$@"
