#!/usr/bin/env bash
# Run both test suites: native (ctest) and managed (dotnet test).
set -euo pipefail
cd "$(dirname "$0")/.."

scripts/build-native.sh

if [ ! -f build/native/ecosim.dll ]; then
  echo "build/native/ecosim.dll missing after native build" >&2
  exit 1
fi

dotnet test app/Ecosim.Tests "$@"
