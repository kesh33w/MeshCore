#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Only signatures used by the linked Identity/Utils objects. The simulation
// never reads or writes a Stream.
class Stream {
 public:
  virtual ~Stream() {}
  virtual size_t write(uint8_t) { return 1; }
  virtual size_t write(const uint8_t*, size_t length) { return length; }
  virtual size_t readBytes(uint8_t*, size_t) { return 0; }
  size_t print(char value) { return write(static_cast<uint8_t>(value)); }
  size_t print(const char* value) {
    return write(reinterpret_cast<const uint8_t*>(value), strlen(value));
  }
  size_t println() { return print('\n'); }
};
