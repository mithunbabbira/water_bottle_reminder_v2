#!/bin/sh
# Native firmware-core tests; no Arduino installation or bottle needed.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD_DIR="$ROOT/.build/native"
mkdir -p "$BUILD_DIR"
"${CXX:-clang++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$ROOT/tests/native/stubs" -I "$ROOT/bottle" \
  "$ROOT/tests/native/core_test.cpp" -o "$BUILD_DIR/bottle_core_tests"
"$BUILD_DIR/bottle_core_tests"
