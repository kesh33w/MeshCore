#pragma once
// Private prototype: bounded hardware capture. No Wi-Fi, socket, credentials or RF transmit.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace private_remote {
constexpr size_t max_raw = 254;
struct PacketView { uint8_t width, encoded_path, type; size_t payload_offset; };
inline bool inspect(const uint8_t* raw, size_t len, PacketView& out) {
  if (!raw || len < 3 || len > max_raw || (raw[0] >> 6) != 0) return false;
  const unsigned route = raw[0] & 3;
  const size_t path_index = (route == 0 || route == 3) ? 5 : 1;
  if (len <= path_index) return false;
  const uint8_t encoded = raw[path_index];
  const unsigned width = (encoded >> 6) + 1;
  const size_t path_bytes = (encoded & 63) * width;
  const size_t payload_offset = path_index + 1 + path_bytes;
  if (width == 4 || path_bytes > 64 || payload_offset >= len || len - payload_offset > 184) return false;
  out = {static_cast<uint8_t>(width), encoded, static_cast<uint8_t>((raw[0] >> 2) & 15), payload_offset};
  return true;
}
// Shared internet admission policy. Observer capture remains independent.
inline bool native_peer_payload(const uint8_t* raw,size_t len) {
  PacketView v{};if(!inspect(raw,len,v)||v.width<2)return false;
  const size_t n=len-v.payload_offset;
  switch(v.type) {
   case 5:case 6:return n>=19 && (n-3)%16==0;
   case 2:case 8:return n>=20 && (n-4)%16==0;
   case 3:return n==4 || n==6; // MeshCore legacy/signed ACK or plain-text extended ACK.
   default:return false;
  }
}
struct Config { bool observer_enabled = true; bool internet_enabled = false; };
struct Reception {
  uint8_t raw[max_raw]; uint16_t length; float snr, rssi;
  uint32_t received_ms; bool observer, internet_candidate;
};
// Single-threaded producer/consumer: call only on the repeater loop thread.
// Saturation drops capture, never blocks or changes normal Mesh::onRecvPacket.
class Capture {
 public:
  Config config;
  uint32_t dropped = 0;
  bool hardware_received(const uint8_t* raw, size_t len, float snr, float rssi, uint32_t now) {
    if (!raw || len == 0 || len > max_raw) return false;
    const bool candidate = config.internet_enabled && native_peer_payload(raw, len);
    if (!config.observer_enabled && !candidate) return false;
    if (count_ == capacity) { ++dropped; return false; }
    auto& r = items_[(head_ + count_) % capacity];
    memcpy(r.raw, raw, len); r.length = len; r.snr = snr; r.rssi = rssi;
    r.received_ms = now; r.observer = config.observer_enabled; r.internet_candidate = candidate;
    ++count_; return true;
  }
  bool pop(Reception& r) {
    if (!count_) return false;
    r = items_[head_]; head_ = (head_ + 1) % capacity; --count_; return true;
  }
  void discard() { head_ = count_ = 0; }
 private:
  static constexpr size_t capacity = 4;
  Reception items_[capacity]{};
  size_t head_ = 0, count_ = 0;
};
} // namespace private_remote
