#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <path-to-emsdk-root>"
  echo "Example: $0 $HOME/dev/emsdk"
  exit 1
fi

EMSDK_ROOT="$1"
if [[ ! -f "$EMSDK_ROOT/emsdk_env.sh" ]]; then
  echo "emsdk_env.sh not found in: $EMSDK_ROOT"
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=/dev/null
source "$EMSDK_ROOT/emsdk_env.sh"

emcmake cmake -S "$PROJECT_ROOT" -B "$PROJECT_ROOT/build" -G Ninja
cmake --build "$PROJECT_ROOT/build"

echo "Build complete: $PROJECT_ROOT/build/framespace.html"
