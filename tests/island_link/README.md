# Island link host tests

Stages 2 and 3. Run the core tests from the repository root with host Clang:

```sh
sh tests/island_link/run.sh
```

No PlatformIO, ESP32 toolchain, downloaded test dependencies, network connection,
or hardware is required. The script builds in a temporary directory and removes
its artifacts on exit. Set `CXX` to choose another Clang installation.

## Verified in source

`src/helpers/bridges/IslandLinkFrame.h` uses the constants from this checkout's
`Packet.h` and `MeshCore.h`. It accepts only version 0 FLOOD or DIRECT frames,
two- or three-byte path hashes, at most 64 path bytes, and 1 through 184 payload
bytes. It rejects TRACE and zero-hop DIRECT, and accepts zero-hop FLOOD. The
outer length gate is 2 through 255 bytes. The other limits make 250 bytes the
largest accepted frame, or 249 with three-byte hashes.

The validator returns an error enum and does not allocate, copy bytes, construct
a packet, or invoke `Packet::readFrom`. It does not validate payload semantics
or add restrictions on other payload types.

## Tests

The unit executable checks named boundaries and all 256 header values, all 256
path encodings, and lengths 0 through 256 against an independent wire-spec
oracle. A separate object-symbol check verifies that the unoptimized validator
has no external dependencies through which it could allocate or invoke a parser.

The standalone fuzz executable runs 1,000,000 reproducible arbitrary and mutated
inputs with AddressSanitizer and UndefinedBehaviorSanitizer. Its fixed corpus
buffer has bytes beyond the input length poisoned to detect over-reads. Both
executables check that the validator leaves input unchanged. Valid fuzz inputs
are accepted; malformed ones must match the oracle's rejection.

`frame_fuzz.cpp` also exports `LLVMFuzzerTestOneInput` for coverage-guided fuzzing
on installations with libFuzzer. The default script runs the deterministic
driver, not libFuzzer.

The same runner tests fragmented/coalesced length-prefixed records, keepalives,
90-second silence detection, reconnect backoff and timer rollover. It also
compiles the actual `IslandLinkRadio.cpp` admission and transmit methods with
host queues. Those tests cover validate-before-allocation, both echo directions,
queue pressure, packet reserve, origin tracking, stale frames and rapid off/on
session changes. That target uses a deterministic hash mock, not cryptography.

Additional tests:

```sh
python3 tests/island_link/relay_identity_script_test.py
sh tests/island_link/run_crypto.sh
sh tests/island_link/run_dm.sh
```

The Python test requires OpenSSL and creates only temporary synthetic keys. The
crypto runner downloads official mbedTLS 2.28.7, matching the ESP32 SDK version,
or uses an existing source directory supplied as `MBEDTLS_SOURCE`. It tests the
actual identity and pin-policy code with fake NVS and real cryptography, including
in-memory TLS handshakes. It creates no sockets or deployment credentials.

The DM runner uses the Crypto dependency installed by the observer PlatformIO
build. It links real Mesh, Dispatcher, Packet, identity, crypto, packet-manager,
seen-table and bridge radio code. Fake radios and small application callbacks
exercise two-island flood delivery, PATH exchange, a direct reply and ACKs for
both hash widths. It does not instantiate BaseChatMesh or the full repeater app.
The runner currently uses the macOS linker option `-dead_strip`.

The DM run initially found an existing negative left shift in
`lib/ed25519/ge.c:359`. Shift sanitization is disabled only for the vendored
ed25519 C sources. AddressSanitizer and other UndefinedBehaviorSanitizer checks
remain enabled. Upstream crypto was not changed.

## Caller contract and limits

The caller supplies existing bytes and their actual length as `size_t`. For an
in-range length, the pointer must cover that many readable bytes. Call
`validateFrame` before allocating a packet or a frame-copy buffer. Only on
`FrameError::None` may the caller allocate and pass these unchanged bytes to
`Packet::readFrom`. Do not narrow the length to `uint8_t` before validation.

The fixed storage that receives network bytes and TLS connection buffers are
outside the validator. Host tests do not establish actual ESP32 entropy, Wi-Fi
and MQTT coexistence, watchdog behavior, flash persistence, or RF performance.
See `STAGE3.md` for implementation boundaries and build measurements.
