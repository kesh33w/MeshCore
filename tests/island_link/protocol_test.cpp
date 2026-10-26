#include "helpers/bridges/IslandLinkProtocol.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using island_link::LinkTimers;
using island_link::ReconnectBackoff;
using island_link::StreamDecoder;
using island_link::StreamEvent;
using island_link::writeWireFrame;
using island_link::intervalElapsed;

static void check(bool condition, const char* name) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", name);
    std::exit(1);
  }
}

int main() {
  const uint8_t frame[] = {1, 64, 0};
  uint8_t wire[256] = {};
  check(writeWireFrame(frame, sizeof(frame), wire, sizeof(wire)) == 4,
        "encode length plus unchanged frame");
  check(wire[0] == 3 && std::memcmp(wire + 1, frame, sizeof(frame)) == 0,
        "encoded bytes");
  check(writeWireFrame(frame, sizeof(frame), wire, 3) == 0,
        "short output rejected");
  check(writeWireFrame(frame, sizeof(frame), nullptr, sizeof(wire)) == 0,
        "null output rejected");
  check(writeWireFrame(frame, sizeof(frame) - 1, wire, sizeof(wire)) == 0,
        "invalid frame rejected");
  check(writeWireFrame(frame, std::numeric_limits<size_t>::max(), wire,
                       sizeof(wire)) == 0,
        "oversized size rejected before capacity arithmetic");

  StreamDecoder decoder;
  check(decoder.feed(0) == StreamEvent::Keepalive, "zero keepalive");
  check(decoder.feed(wire[0]) == StreamEvent::None, "fragmented prefix");
  check(decoder.feed(wire[1]) == StreamEvent::None, "first fragment");
  check(decoder.feed(wire[2]) == StreamEvent::None, "second fragment");
  check(decoder.feed(wire[3]) == StreamEvent::Frame, "full frame");
  check(decoder.size() == sizeof(frame) &&
            std::memcmp(decoder.data(), frame, sizeof(frame)) == 0,
        "validated data and actual size");
  check(decoder.feed(0) == StreamEvent::Keepalive, "coalesced keepalive");
  for (size_t i = 0; i < 4; ++i) {
    const StreamEvent event = decoder.feed(wire[i]);
    check(event == (i == 3 ? StreamEvent::Frame : StreamEvent::None),
          "coalesced second frame");
  }

  // The length byte can declare 255 bytes, but the validator must reject a
  // complete frame that exceeds MeshCore's payload and path limits.
  check(decoder.feed(255) == StreamEvent::None, "maximum wire length prefix");
  for (size_t i = 0; i < 254; ++i)
    check(decoder.feed(i < sizeof(frame) ? frame[i] : 0) == StreamEvent::None,
          "maximum wire length partial");
  check(decoder.feed(0) == StreamEvent::Invalid, "invalid complete record");
  check(decoder.size() == 0, "invalid frame data unavailable");
  check(decoder.feed(0) == StreamEvent::Invalid,
        "invalid decoder stays failed until reset");
  decoder.reset();
  check(decoder.feed(0) == StreamEvent::Keepalive, "reset resumes parsing");

  // A session reset also discards a partial frame.
  check(decoder.feed(3) == StreamEvent::None, "partial before reset");
  check(decoder.feed(1) == StreamEvent::None, "partial body before reset");
  decoder.reset();
  check(decoder.feed(0) == StreamEvent::Keepalive, "partial frame discarded");
  check(decoder.size() == 0, "reset clears frame size");

  // A 250-byte MeshCore frame is the largest the validator accepts.
  uint8_t long_frame[250] = {};
  long_frame[0] = 2;
  long_frame[1] = 96;  // One two-byte hash per hop, 32 hops.
  check(writeWireFrame(long_frame, sizeof(long_frame), wire, sizeof(wire)) == 251,
        "largest valid frame encoded");
  for (size_t i = 0; i < 251; ++i) {
    const StreamEvent event = decoder.feed(wire[i]);
    check(event == (i == 250 ? StreamEvent::Frame : StreamEvent::None),
          "largest valid frame decoded");
  }
  check(decoder.size() == 250 &&
            std::memcmp(decoder.data(), long_frame, sizeof(long_frame)) == 0,
        "largest valid frame retained");

  LinkTimers timers;
  check(!timers.keepaliveDue(30000) && !timers.receiveTimedOut(90000),
        "disconnected timers inactive");
  timers.onConnected(100);
  check(!timers.keepaliveDue(30099) && timers.keepaliveDue(30100),
        "keepalive exact boundary");
  timers.onKeepaliveSent(30100);
  check(!timers.keepaliveDue(30101) && timers.keepaliveDue(60100),
        "keepalive resets only when one is sent");
  check(!timers.receiveTimedOut(90099) && timers.receiveTimedOut(90100),
        "receive silence exact boundary");
  timers.onReceive(90100);
  check(!timers.receiveTimedOut(90101), "incoming data resets silence timer");
  check(timers.keepaliveDue(90101),
        "incoming frame traffic does not postpone keepalive");
  timers.onDisconnected();
  check(!timers.keepaliveDue(200000) && !timers.receiveTimedOut(200000),
        "disconnected timers stay inactive");

  const uint32_t near_wrap = std::numeric_limits<uint32_t>::max() - 999;
  timers.onConnected(near_wrap);
  check(!timers.keepaliveDue(near_wrap + 29999) &&
            timers.keepaliveDue(near_wrap + 30000),
        "keepalive across millis rollover");
  check(!timers.receiveTimedOut(near_wrap + 89999) &&
            timers.receiveTimedOut(near_wrap + 90000),
        "silence timeout across millis rollover");
  check(!intervalElapsed(near_wrap + 4999, near_wrap, 5000) &&
            intervalElapsed(near_wrap + 5000, near_wrap, 5000),
        "reconnect interval across millis rollover");

  ReconnectBackoff backoff;
  const uint32_t expected[] = {5000, 10000, 20000, 40000, 80000,
                               160000, 300000, 300000};
  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    check(backoff.nextDelayMs() == expected[i], "backoff sequence and cap");
  backoff.reset();
  check(backoff.nextDelayMs() == 5000, "backoff reset");

  std::puts("PASS: island link framing and timers");
}
