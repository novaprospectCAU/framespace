#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [[ ! -f "$PROJECT_ROOT/build/framespace.html" ]]; then
  echo "No build output found. Run scripts/build.sh first."
  exit 1
fi

cd "$PROJECT_ROOT/build"
python3 -m http.server 8080
