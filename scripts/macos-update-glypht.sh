#!/usr/bin/env bash
# Copies the glypht.app built by build.sh to the Desktop (macOS only).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_APP="${SCRIPT_DIR}/../.build-release/glypht.app"
DEST_APP="${HOME}/Desktop/glypht.app"

if [ "$(uname -s)" != "Darwin" ]; then
  echo "ERROR: this script works on macOS only" >&2
  exit 1
fi

if pgrep -x glypht >/dev/null; then
  echo "ERROR: glypht is running. Cannot update. Close it and try again." >&2
  exit 1
fi

if [ ! -d "${SRC_APP}" ]; then
  echo "ERROR: ${SRC_APP} not found. Run ./build.sh first." >&2
  exit 1
fi

if [ -e "${DEST_APP}" ]; then
  echo "INFO removing old ${DEST_APP}"
  rm -rf "${DEST_APP}"
fi

echo "INFO copying ${SRC_APP} to ${DEST_APP}"
ditto "${SRC_APP}" "${DEST_APP}"

echo "To open it, run:"
echo "open ~/Desktop/glypht.app/"
