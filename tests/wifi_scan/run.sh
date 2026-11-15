#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$ROOT/.build/native"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I "$ROOT/tests/wifi_scan/stubs" \
  "$ROOT/tests/wifi_scan/scan_test.cpp" -o "$ROOT/.build/native/wifi_scan_tests"
"$ROOT/.build/native/wifi_scan_tests"
