#!/bin/bash
# Build libghostty as a static library for linking into Ptyxis.
# Requires: zig >= 0.15.2
set -euo pipefail

GHOSTTY_DIR="$(cd "$(dirname "$0")/../subprojects/ghostty" && pwd)"
BUILD_DIR="${GHOSTTY_DIR}/zig-out"
OUTPUT_DIR="${1:-${BUILD_DIR}}"

cd "$GHOSTTY_DIR"

echo "==> Building libghostty..."
zig build \
  -Doptimize=ReleaseFast \
  -Dtarget=native \
  --prefix "$OUTPUT_DIR"

echo "==> Copying artifacts..."
# The static library
cp -v "$OUTPUT_DIR/lib/ghostty-internal.a" "$OUTPUT_DIR/" 2>/dev/null || true

# The header
cp -v include/ghostty.h "$OUTPUT_DIR/include/ghostty.h" 2>/dev/null || true

echo "==> Done. Library at $OUTPUT_DIR/"
ls -la "$OUTPUT_DIR"/ghostty-internal.a "$OUTPUT_DIR"/include/ghostty.h 2>/dev/null || echo "Check zig-out/ for outputs"
