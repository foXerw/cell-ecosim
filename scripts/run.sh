#!/usr/bin/env bash
# Launch the desktop app. Builds first if the native engine is missing.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ ! -f build/native/ecosim.dll ]; then
  scripts/build.sh
fi

exec dotnet run --project app/Ecosim.App --no-build "$@"
