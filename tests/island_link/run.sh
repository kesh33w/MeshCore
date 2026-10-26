#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/island-link-tests.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
cxx=${CXX:-clang++}

# Packet.h includes upstream virtual methods with unused parameters.
set -- -std=c++11 -Wall -Wextra -Werror -Wno-unused-parameter -I "$repo/src"
"$cxx" "$@" -O0 -fno-stack-protector -c \
  "$repo/tests/island_link/allocation_probe.cpp" -o "$build/allocation_probe.o"
nm -u "$build/allocation_probe.o" > "$build/undefined.txt"
if [ -s "$build/undefined.txt" ]; then
  cat "$build/undefined.txt"
  echo "FAIL: validator has external symbol dependencies" >&2
  exit 1
fi
echo "PASS: unoptimized validator object has no external symbol dependencies"

set -- "$@" -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined \
  -fno-sanitize-recover=all
"$cxx" "$@" "$repo/tests/island_link/frame_test.cpp" -o "$build/frame_test"
"$build/frame_test"
"$cxx" "$@" -DISLAND_LINK_STANDALONE_FUZZ \
  "$repo/tests/island_link/frame_fuzz.cpp" -o "$build/frame_fuzz"
"$build/frame_fuzz"
