#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/island-link-crypto.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
cc=${CC:-clang}
cxx=${CXX:-clang++}

# Match the ESP32 framework's mbedTLS major/minor and patch release. Source
# and synthetic keys/certificates stay in this disposable test directory.
if [ -n "${MBEDTLS_SOURCE:-}" ]; then
  source_dir=$MBEDTLS_SOURCE
else
  curl -fsSL --retry 2 --connect-timeout 10 \
    https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v2.28.7.tar.gz \
    -o "$build/mbedtls.tar.gz"
  tar -xzf "$build/mbedtls.tar.gz" -C "$build"
  source_dir=$build/mbedtls-2.28.7
fi
grep -q 'MBEDTLS_VERSION_STRING         "2.28.7"' \
  "$source_dir/include/mbedtls/version.h"

mkdir "$build/objects"
for source in "$source_dir"/library/*.c; do
  name=$(basename "$source" .c)
  "$cc" -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I "$source_dir/include" -c "$source" -o "$build/objects/$name.o"
done
ar rcs "$build/libmbedtls-host.a" "$build"/objects/*.o

make_fixture() {
  name=$1
  san=$2
  cat > "$build/$name.cnf" <<EOF
[req]
distinguished_name=dn
x509_extensions=v3
prompt=no
[dn]
CN=synthetic island link host test
[v3]
subjectAltName=DNS:$san
EOF
  openssl ecparam -genkey -name prime256v1 -noout -out "$build/$name.key"
  openssl req -x509 -new -key "$build/$name.key" -config "$build/$name.cnf" \
    -days 1 -out "$build/$name.crt"
}
make_fixture right ilink.sacmesh.org
make_fixture wrong wrong.sacmesh.org

"$cxx" -std=c++11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I "$repo/tests/island_link/crypto_mocks" -I "$source_dir/include" \
  -I "$repo/src" -DESP32_PLATFORM \
  "$repo/tests/island_link/crypto_test.cpp" \
  "$repo/src/helpers/bridges/IslandLinkIdentity.cpp" \
  "$build/libmbedtls-host.a" -o "$build/crypto_test"
"$build/crypto_test" "$build/right.crt" "$build/wrong.crt"

"$cxx" -std=c++11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I "$repo/tests/island_link/crypto_mocks" -I "$source_dir/include" \
  -I "$repo/src" -DESP32_PLATFORM \
  "$repo/tests/island_link/tls_test.cpp" \
  "$repo/src/helpers/bridges/IslandLinkIdentity.cpp" \
  "$repo/src/helpers/bridges/IslandLinkTlsPolicy.cpp" \
  "$build/libmbedtls-host.a" -o "$build/tls_test"
"$build/tls_test" "$build/right.crt" "$build/right.key" \
  "$build/wrong.crt" "$build/wrong.key"
