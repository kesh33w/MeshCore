#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/island-link-dm.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
cc=${CC:-clang}
cxx=${CXX:-clang++}
crypto="$repo/.pio/libdeps/heltec_v4_repeater_observer_mqtt/Crypto"
if [ ! -f "$crypto/SHA256.h" ]; then
  echo "The existing PlatformIO rweather/Crypto dependency is required." >&2
  exit 1
fi

for source in "$repo"/lib/ed25519/*.c; do
  name=$(basename "$source" .c)
  "$cc" -O1 -g -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -fno-sanitize=shift \
    -fno-sanitize-recover=all \
    -I "$repo/lib/ed25519" -c "$source" -o "$build/ed_$name.o"
done

"$cxx" -std=c++11 -O1 -g -ffunction-sections -fdata-sections \
  -Wl,-dead_strip -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare -Wno-reorder-ctor \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -DWITH_ILINK -DESP32_PLATFORM \
  -I "$repo/src" -I "$repo/tests/island_link/dm_mocks" \
  -I "$repo/tests/island_link/mocks" \
  -I "$crypto" -I "$repo/lib/ed25519" \
  "$repo/tests/island_link/dm_test.cpp" \
  "$repo/src/Mesh.cpp" "$repo/src/Dispatcher.cpp" \
  "$repo/src/Packet.cpp" "$repo/src/Utils.cpp" "$repo/src/Identity.cpp" \
  "$repo/src/helpers/StaticPoolPacketManager.cpp" \
  "$repo/src/helpers/bridges/IslandLinkRadio.cpp" \
  "$crypto/AES128.cpp" "$crypto/AESCommon.cpp" \
  "$crypto/Cipher.cpp" "$crypto/BlockCipher.cpp" \
  "$crypto/SHA256.cpp" "$crypto/SHA512.cpp" "$crypto/Hash.cpp" \
  "$crypto/Crypto.cpp" "$crypto/Ed25519.cpp" \
  "$crypto/Curve25519.cpp" "$crypto/BigNumberUtil.cpp" \
  "$build"/ed_*.o -o "$build/dm_test"
"$build/dm_test"
