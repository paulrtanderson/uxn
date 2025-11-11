#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

cd "$REPO_ROOT"

echo "=== Building ==="
chmod +x build.sh
./build.sh --no-run

echo "=== Running tests ==="
chmod +x tests/run.sh
./tests/run.sh

echo
echo "=== All tests finished successfully ==="