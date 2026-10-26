#pragma once

#include "fake_nvs.h"
#include <stddef.h>
#include <string.h>

class Preferences {
  bool opened_ = false;
 public:
  bool begin(const char* name, bool read_only) {
    opened_ = strcmp(name, "ilink") == 0 && !read_only;
    return opened_;
  }
  size_t putBytes(const char* key, const void* data, size_t length) {
    if (!opened_ || strcmp(key, "identity") != 0 || fake_nvs.write_fails)
      return 0;
    fake_nvs.blob.assign(static_cast<const uint8_t*>(data),
                         static_cast<const uint8_t*>(data) + length);
    fake_nvs.exists = true;
    ++fake_nvs.writes;
    return length;
  }
  void end() { opened_ = false; }
};
