#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/observer-defaults-tests.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
cxx=${CXX:-clang++}

# Ignore warnings in the existing Arduino mocks and serializer switch parser.
set -- -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter \
  -Wno-sign-compare -Wno-implicit-fallthrough -Wno-unused-private-field \
  -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined \
  -fno-sanitize-recover=all -I "$repo/src" -I "$repo/test/mocks" \
  -DWITH_MQTT_BRIDGE=1 -DPROGMEM= \
  "$repo/tests/observer_defaults/defaults_test.cpp" \
  "$repo/src/helpers/ConfigSerializer.cpp" "$repo/src/Utils.cpp"

"$cxx" "$@" -DWITH_ILINK=1 '-DEXPECT_SLOT1_PRESET="sacmesh"' \
  '-DEXPECT_IATA="SMF"' '-DEXPECT_SLOT2_PRESET="none"' -o "$build/ilink"
"$build/ilink"

"$cxx" "$@" '-DEXPECT_SLOT1_PRESET="analyzer-us"' \
  '-DEXPECT_IATA=""' '-DEXPECT_SLOT2_PRESET="analyzer-eu"' -o "$build/ordinary"
"$build/ordinary"

"$cxx" "$@" -DWITH_ILINK=1 '-DMQTT_DEFAULT_SLOT1_PRESET="none"' \
  '-DMQTT_DEFAULT_IATA="PDX"' '-DEXPECT_SLOT1_PRESET="none"' \
  '-DEXPECT_IATA="PDX"' '-DEXPECT_SLOT2_PRESET="none"' -o "$build/override"
"$build/override"
