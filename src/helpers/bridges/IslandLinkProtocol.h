#pragma once

#include <stddef.h>
#include <stdint.h>

#include "IslandLinkFrame.h"

namespace island_link {

// Each wire record starts with one length byte. Zero is a keepalive; a
// nonzero length is followed by exactly that many MeshCore frame bytes.
enum class StreamEvent { None, Keepalive, Frame, Invalid };

class StreamDecoder {
 public:
  StreamDecoder() : expected_(0), received_(0), failed_(false) {}

  void reset() {
    expected_ = 0;
    received_ = 0;
    frame_size_ = 0;
    failed_ = false;
  }

  StreamEvent feed(uint8_t byte) {
    if (failed_) return StreamEvent::Invalid;
    if (expected_ == 0) {
      if (byte == 0) return StreamEvent::Keepalive;
      expected_ = byte;
      received_ = 0;
      return StreamEvent::None;
    }

    bytes_[received_++] = byte;
    if (received_ != expected_) return StreamEvent::None;

    // Keep the actual size_t receive count through validation. A rejected
    // record poisons this decoder until the caller resets it for a new session.
    const size_t length = received_;
    expected_ = 0;
    received_ = 0;
    if (validateFrame(bytes_, length) != FrameError::None) {
      frame_size_ = 0;
      failed_ = true;
      return StreamEvent::Invalid;
    }
    frame_size_ = length;
    return StreamEvent::Frame;
  }

  const uint8_t* data() const { return bytes_; }
  size_t size() const { return frame_size_; }

 private:
  uint8_t bytes_[MAX_TRANS_UNIT];
  uint8_t expected_;
  size_t received_;
  size_t frame_size_ = 0;
  bool failed_;
};

// Returns the number of output bytes, or zero for an invalid frame or a short
// output buffer. The caller must own capacity bytes at out when capacity > 0.
inline size_t writeWireFrame(const uint8_t* frame, size_t length,
                             uint8_t* out, size_t capacity) {
  if (validateFrame(frame, length) != FrameError::None || out == nullptr ||
      capacity < length + 1) return 0;
  out[0] = static_cast<uint8_t>(length);
  for (size_t i = 0; i < length; ++i) out[i + 1] = frame[i];
  return length + 1;
}

// Use this for scheduled reconnect attempts too. Intervals must be less than
// one uint32_t millis cycle, and callers must check at least once per cycle.
inline bool intervalElapsed(uint32_t now, uint32_t since, uint32_t interval) {
  return uint32_t(now - since) >= interval;
}

class LinkTimers {
 public:
  static const uint32_t kKeepaliveMs = 30000;
  static const uint32_t kReceiveTimeoutMs = 90000;

  LinkTimers() : last_receive_(0), last_keepalive_(0), connected_(false) {}

  void onConnected(uint32_t now) {
    last_receive_ = now;
    last_keepalive_ = now;
    connected_ = true;
  }
  void onDisconnected() { connected_ = false; }
  void onReceive(uint32_t now) { last_receive_ = now; }
  void onKeepaliveSent(uint32_t now) { last_keepalive_ = now; }

  bool keepaliveDue(uint32_t now) const {
    return connected_ && intervalElapsed(now, last_keepalive_, kKeepaliveMs);
  }
  bool receiveTimedOut(uint32_t now) const {
    return connected_ && intervalElapsed(now, last_receive_, kReceiveTimeoutMs);
  }

 private:
  uint32_t last_receive_;
  uint32_t last_keepalive_;
  bool connected_;
};

class ReconnectBackoff {
 public:
  static const uint32_t kInitialMs = 5000;
  static const uint32_t kMaximumMs = 300000;

  ReconnectBackoff() : next_ms_(kInitialMs) {}
  void reset() { next_ms_ = kInitialMs; }
  uint32_t nextDelayMs() {
    const uint32_t delay = next_ms_;
    next_ms_ = next_ms_ >= kMaximumMs / 2 ? kMaximumMs : next_ms_ * 2;
    return delay;
  }

 private:
  uint32_t next_ms_;
};

}  // namespace island_link
