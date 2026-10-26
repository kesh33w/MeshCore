#pragma once

#include <cstddef>
#include <cstdint>

// Independent wire-spec oracle. Deliberately does not use firmware constants.
inline bool expectedValid(const uint8_t* data, size_t size) {
  if (!data || size < 3 || size > 255) return false;
  if (data[0] >= 64 || data[0] / 4 == 9) return false;
  if (data[0] % 4 != 1 && data[0] % 4 != 2) return false;

  size_t path_bytes;
  if (data[1] >= 64 && data[1] <= 96) {
    path_bytes = (data[1] - 64) * 2;
  } else if (data[1] >= 128 && data[1] <= 149) {
    path_bytes = (data[1] - 128) * 3;
  } else {
    return false;
  }
  if (data[0] % 4 == 2 && path_bytes == 0) return false;
  return size >= path_bytes + 3 && size <= path_bytes + 186;
}
