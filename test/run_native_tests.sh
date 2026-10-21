#!/bin/sh
# Build and run the host-side unit tests (macOS: clang++, Linux: g++).
# Usage: test/run_native_tests.sh
#        TREADMILL_PREVIEW_DIR=docs/screens test/run_native_tests.sh   (also save screen previews)
set -e
cd "$(dirname "$0")/.."
CXX="${CXX:-c++}"
SANITIZE="${SANITIZE:-address,undefined}"
mkdir -p .pio/native-tests
"$CXX" -std=c++17 -O2 -Wall -Wextra -pthread -fsanitize="$SANITIZE" \
    -Isrc -Itest/native test/native/test_main.cpp src/plan.cpp src/belt.cpp src/app.cpp src/canvas.cpp src/screens.cpp \
    -o .pio/native-tests/run_tests
.pio/native-tests/run_tests "$@"
