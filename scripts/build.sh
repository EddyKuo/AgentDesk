#!/bin/sh
# macOS build (ADR-30): scripts/build.sh [preset] [configure|build|test|all]
# Dependencies come from Homebrew for now (googletest, nlohmann-json).
set -eu
preset="${1:-clang-macos-arm64-debug}"
step="${2:-all}"
cd "$(dirname "$0")/.."
case "$step" in
  configure) cmake --preset "$preset" ;;
  build) cmake --build --preset "$preset" ;;
  test) ctest --preset "$preset" ;;
  all)
    [ -f "build/$preset/CMakeCache.txt" ] || cmake --preset "$preset"
    cmake --build --preset "$preset"
    ctest --preset "$preset"
    ;;
  *) echo "unknown step: $step" >&2; exit 2 ;;
esac
