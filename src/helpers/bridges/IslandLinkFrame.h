#pragma once

#include <Packet.h>

namespace island_link {

enum class FrameError {
  None,
  Length,
  NullData,
  Version,
  Route,
  Trace,
  HashSize,
  PathSize,
  TruncatedPath,
  EmptyDirectPath,
  PayloadSize,
};

// Inspect caller-owned bytes before allocating a Packet or copying a frame.
// No parsing, allocation, mutation, or payload interpretation happens here.
// data must cover length bytes when length is in range. Keep length as size_t:
// narrowing an untrusted length to Packet::readFrom's uint8_t could hide overflow.
inline FrameError validateFrame(const uint8_t* data, size_t length) {
  if (length < 2 || length > MAX_TRANS_UNIT) return FrameError::Length;
  if (data == nullptr) return FrameError::NullData;

  const uint8_t header = data[0];
  if (((header >> PH_VER_SHIFT) & PH_VER_MASK) != PAYLOAD_VER_1)
    return FrameError::Version;
  const uint8_t route = header & PH_ROUTE_MASK;
  if (route != ROUTE_TYPE_FLOOD && route != ROUTE_TYPE_DIRECT)
    return FrameError::Route;
  if (((header >> PH_TYPE_SHIFT) & PH_TYPE_MASK) == PAYLOAD_TYPE_TRACE)
    return FrameError::Trace;

  const size_t hash_size = (data[1] >> 6) + 1;
  const size_t hops = data[1] & 63;
  if (hash_size != 2 && hash_size != 3) return FrameError::HashSize;
  const size_t path_bytes = hash_size * hops;
  if (path_bytes > MAX_PATH_SIZE) return FrameError::PathSize;
  if (path_bytes > length - 2) return FrameError::TruncatedPath;
  if (route == ROUTE_TYPE_DIRECT && hops == 0)
    return FrameError::EmptyDirectPath;

  const size_t payload_bytes = length - 2 - path_bytes;
  if (payload_bytes == 0 || payload_bytes > MAX_PACKET_PAYLOAD)
    return FrameError::PayloadSize;
  return FrameError::None;
}

}  // namespace island_link
