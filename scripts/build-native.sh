#!/usr/bin/env bash
# Build the native engine and run the native test suite.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ ! -f build/native/CMakeCache.txt ]; then
  scripts/configure.sh
fi

cmake --build build/native -j
ctest --test-dir build/native --output-on-failure
