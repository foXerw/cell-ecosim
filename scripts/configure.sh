#!/usr/bin/env bash
# Configure the native build.
#
# This machine has MinGW g++ but neither mingw32-make nor ninja, so the
# generator is picked from whatever build tool actually exists:
#   ninja  >  MinGW Makefiles  >  MSYS Makefiles  >  Unix Makefiles
# Override by exporting ECOSIM_GENERATOR / ECOSIM_MAKE_PROGRAM before running.
set -euo pipefail
cd "$(dirname "$0")/.."

pick_generator() {
  if command -v ninja >/dev/null 2>&1; then echo "Ninja"; return; fi
  if command -v mingw32-make >/dev/null 2>&1; then echo "MinGW Makefiles"; return; fi
  for candidate in /c/msys64/usr/bin/make.exe /usr/bin/make make; do
    if command -v "$candidate" >/dev/null 2>&1 || [ -x "$candidate" ]; then
      echo "MSYS Makefiles"; return
    fi
  done
  echo "Unix Makefiles"
}

GEN="${ECOSIM_GENERATOR:-$(pick_generator)}"
ARGS=(-S native -B build/native -G "$GEN" -DCMAKE_BUILD_TYPE=Release)

if [ "$GEN" = "MSYS Makefiles" ] && [ -n "${ECOSIM_MAKE_PROGRAM:-}" ]; then
  ARGS+=(-DCMAKE_MAKE_PROGRAM="$ECOSIM_MAKE_PROGRAM")
elif [ "$GEN" = "MSYS Makefiles" ] && [ -x /c/msys64/usr/bin/make.exe ]; then
  ARGS+=(-DCMAKE_MAKE_PROGRAM=C:/msys64/usr/bin/make.exe)
fi

if command -v g++ >/dev/null 2>&1; then
  ARGS+=(-DCMAKE_CXX_COMPILER="$(cygpath -m "$(command -v g++)")")
fi

echo "cmake ${ARGS[*]} $*"
exec cmake "${ARGS[@]}" "$@"
