#include "helpers/bridges/IslandLinkFrame.h"
#include "frame_oracle.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using island_link::FrameError;
using island_link::validateFrame;

static size_t checks = 0;

static void check(bool condition, const char* name) {
  ++checks;
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", name);
    std::exit(1);
  }
}

int main() {
  uint8_t frame[256] = {};
  frame[0] = 1;
  frame[1] = 64;
  check(validateFrame(nullptr, 0) == FrameError::Length, "empty input");
  check(validateFrame(frame, 1) == FrameError::Length, "one-byte input");
  check(validateFrame(nullptr, 2) == FrameError::NullData, "null input");
  check(validateFrame(frame, 2) == FrameError::PayloadSize, "missing payload");
  check(validateFrame(frame, 256) == FrameError::Length, "oversized input");
  check(validateFrame(nullptr, std::numeric_limits<size_t>::max()) == FrameError::Length,
        "large length rejected before access or narrowing");
  check(validateFrame(frame, 3) == FrameError::None, "two-byte zero-hop flood");
  check(validateFrame(frame, 186) == FrameError::None, "184-byte payload");
  check(validateFrame(frame, 187) == FrameError::PayloadSize, "185-byte payload");

  frame[0] = 65;
  check(validateFrame(frame, 3) == FrameError::Version, "unsupported version");
  frame[0] = 0;
  check(validateFrame(frame, 3) == FrameError::Route, "transport flood");
  frame[0] = 3;
  check(validateFrame(frame, 3) == FrameError::Route, "transport direct");
  frame[0] = 37;
  check(validateFrame(frame, 3) == FrameError::Trace, "trace");
  frame[0] = 2;
  check(validateFrame(frame, 3) == FrameError::EmptyDirectPath, "zero-hop direct");
  frame[1] = 65;
  check(validateFrame(frame, 3) == FrameError::TruncatedPath, "truncated path");
  check(validateFrame(frame, 4) == FrameError::PayloadSize, "path without payload");
  check(validateFrame(frame, 5) == FrameError::None, "one-hop direct");
  frame[1] = 96;
  check(validateFrame(frame, 250) == FrameError::None, "64-byte path and max payload");
  check(validateFrame(frame, 251) == FrameError::PayloadSize, "payload over max with path");
  check(validateFrame(frame, 255) == FrameError::PayloadSize, "255-byte frame cannot fit limits");
  frame[1] = 97;
  check(validateFrame(frame, 100) == FrameError::PathSize, "66-byte path");
  frame[1] = 149;
  check(validateFrame(frame, 249) == FrameError::None, "63-byte three-byte path");
  frame[1] = 150;
  check(validateFrame(frame, 100) == FrameError::PathSize, "three-byte path over limit");
  frame[0] = 1;
  frame[1] = 128;
  check(validateFrame(frame, 3) == FrameError::None, "three-byte zero-hop flood");
  frame[1] = 0;
  check(validateFrame(frame, 3) == FrameError::HashSize, "single-byte hashes");
  frame[1] = 192;
  check(validateFrame(frame, 3) == FrameError::HashSize, "reserved four-byte hashes");

  // Every header and path byte at every length through the upper boundary.
  // This also checks all payload types, including unspecified non-TRACE types:
  // the requested validator does not impose a payload-type allowlist.
  size_t accepted = 0;
  for (unsigned header = 0; header < 256; ++header) {
    for (unsigned path = 0; path < 256; ++path) {
      frame[0] = static_cast<uint8_t>(header);
      frame[1] = static_cast<uint8_t>(path);
      uint8_t original[sizeof(frame)];
      std::memcpy(original, frame, sizeof(frame));
      for (size_t length = 0; length <= sizeof(frame); ++length) {
        const bool valid = validateFrame(frame, length) == FrameError::None;
        check(valid == expectedValid(frame, length), "exhaustive wire-spec acceptance");
        accepted += valid;
      }
      check(std::memcmp(original, frame, sizeof(frame)) == 0, "input unchanged");
    }
  }
  // 15 allowed payload types, 55 FLOOD and 53 DIRECT path encodings,
  // and 184 possible payload lengths for each accepted header/path pair.
  check(accepted == 15u * (55u + 53u) * 184u, "accepted combination count");
  std::printf("PASS: %zu checks; %zu accepted header/path/length combinations\n",
              checks, accepted);
}
