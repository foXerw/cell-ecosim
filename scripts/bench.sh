#!/usr/bin/env bash
# Performance baseline. Numbers get copied into docs/building.md.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ ! -x build/native/ecosim-cli.exe ]; then
  scripts/build-native.sh
fi

echo "== 1024x1024 random soup, 200 gens =="
build/native/ecosim-cli.exe bench --w 1024 --h 1024 --gens 200 --density 300 --threads 0
echo
echo "== 2048x2048 random soup, 50 gens =="
build/native/ecosim-cli.exe bench --w 2048 --h 2048 --gens 50 --density 300 --threads 0
