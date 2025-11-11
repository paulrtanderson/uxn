#!/usr/bin/env bash
set -euo pipefail

# Expected exact output (replace this with your real expected output)
EXPECTED="4342"

# Ensure tools exist
if ! [ -x bin/uxnasm ] || ! [ -x bin/uxncli ]; then
  echo "Missing bin/uxnasm or bin/uxncli. Run ./build.sh first." >&2
  exit 1
fi

# Assemble
mkdir -p build/tests
bin/uxnasm tests/test1.tal build/tests/test1.rom

# Run and capture output (strip CR for cross-platform consistency)
OUT="$(bin/uxncli build/tests/test1.rom 2>&1 | tr -d '\r')"

# Compare
if [ "$OUT" = "$EXPECTED" ]; then
  echo "[ok] test1.tal output matched"
else
  echo "[fail] test1.tal output mismatch"
  echo " expected: $EXPECTED"
  echo "      got: $OUT"
  exit 1
fi