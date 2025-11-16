#!/usr/bin/env bash
set -euo pipefail

# Resolve repo root no matter where we’re called from
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
BIN_DIR="$REPO_ROOT/bin"
BUILD_DIR="$REPO_ROOT/build/tests"

mkdir -p "$BUILD_DIR"

# Sanity check
if ! [ -x "$BIN_DIR/uxnasm" ] || ! [ -x "$BIN_DIR/uxncli" ]; then
  echo "Missing $BIN_DIR/uxnasm or $BIN_DIR/uxncli. Run ./build.sh first." >&2
  exit 1
fi

pass=0
fail=0

for dir in "$SCRIPT_DIR"/*/; do
  [ -d "$dir" ] || continue
  name="$(basename "$dir")"
  tal="$dir/test.tal"
  expect="$dir/expected.txt"
  rom="$BUILD_DIR/$name.rom"

  if ! [ -f "$tal" ] || ! [ -f "$expect" ]; then
    echo "[skip] $name (missing test.tal or expected.txt)"
    continue
  fi

  echo "[assemble] $name"
  "$BIN_DIR/uxnasm" "$tal" "$rom" >/dev/null

  echo "[run] $name"
  out="$("$BIN_DIR/uxncli" "$rom" 2>&1 | tr -d '\r')"
  expected="$(tr -d '\r' < "$expect")"

  if [ "$out" = "$expected" ]; then
    echo "[ok] $name"
    pass=$((pass + 1))
  else
    echo "[fail] $name"
    echo " expected: $expected"
    echo "      got: $out"
    fail=$((fail + 1))
  fi
done

echo
echo "Passed: $pass"
echo "Failed: $fail"

if [ "$fail" -ne 0 ]; then
  exit 1
fi
# Only pause if running in an interactive terminal so ci systems don’t hang
if [ -t 0 ]; then
  echo
  read -rp "Press Enter to exit..."
fi