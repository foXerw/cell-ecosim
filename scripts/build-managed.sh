#!/usr/bin/env bash
# Build the managed side. The native DLL must exist first because it is copied
# into each project's output directory.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ ! -f build/native/ecosim.dll ]; then
  scripts/build-native.sh
fi

exec dotnet build app/Ecosim.slnx "$@"
