# Island link frame validation

Stage 2 only. Run from the repository root with a host Clang installation:

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

## Caller contract and limits

The caller supplies existing bytes and their actual length as `size_t`. For an
in-range length, the pointer must cover that many readable bytes. Call
`validateFrame` before allocating a packet or a frame-copy buffer. Only on
`FrameError::None` may the caller allocate and pass these unchanged bytes to
`Packet::readFrom`. Do not narrow the length to `uint8_t` before validation.

The fixed storage that receives network bytes and TLS connection buffers are
outside this validator. Transport framing, firmware admission, echo suppression,
DM routing, TLS, enrollment, and persistence are not implemented or tested here.
