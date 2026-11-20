#!/usr/bin/env bash
# One command from a clean checkout to a runnable app: native engine, then the
# managed solution that copies ecosim.dll into each output directory.
set -euo pipefail
cd "$(dirname "$0")/.."

scripts/build-native.sh
scripts/build-managed.sh "$@"

echo
echo "done.  run it with:  scripts/run.sh"
