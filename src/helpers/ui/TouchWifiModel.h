#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace TouchWifiModel {
inline void wipe(char* p, size_t n) { volatile char* v = p; while (n--) *v++ = 0; }
// Match the existing observer preference capacities, including terminators.
inline bool validCredentials(const char* ssid, const char* password) {
  return ssid && password && strlen(ssid) > 0 && strlen(ssid) < 32 &&
         (strlen(password) == 0 || (strlen(password) >= 8 && strlen(password) < 64));
}
// CHSC6xxx coordinate report. Ignore short, multi-touch and out-of-range reports.
// Measured on TFT 9F:40: panel coordinates run opposite the display axes.
// With display rotation 3, (native 82,158) hits the drawn circle near (160,160).
// The saved display flip adds another 180 degrees.
inline bool point(const uint8_t* b, size_t n, bool flip, uint16_t& x, uint16_t& y) {
  if (n != 16 || b[2] != 1 || (b[3] >> 6) == 1 || (b[3] >> 6) == 3) return false;
  uint16_t native_x = ((b[3] & 15) << 8) | b[4];
  uint16_t native_y = ((b[5] & 15) << 8) | b[6];
  if (native_x >= 240 || native_y >= 320) return false;
  x = native_y; y = 239 - native_x;
  if (flip) { x = 319 - x; y = 239 - y; }
  return true;
}
class Entry {
 public:
  char text[64] = {};
  void clear() { wipe(text, sizeof(text)); }
  bool append(char c, size_t limit) {
    size_t n = strlen(text);
    if (c < 32 || c > 126 || n >= limit || n >= sizeof(text)-1) return false;
    text[n] = c; text[n+1] = 0; return true;
  }
  void backspace() { size_t n = strlen(text); if (n) text[n-1] = 0; }
  ~Entry() { clear(); }
};
}
class TouchWifiCallbacks {
 public:
  virtual bool touchSetupAllowed() = 0;
  // True only after the unauthenticated portal has completely stopped.
  virtual bool touchPrepareConnection() = 0;
  virtual bool touchStartConnection(const char*, const char*) = 0;
  virtual bool touchConnectionReady() = 0;
  virtual void touchCancelConnection() = 0;
  virtual bool touchSaveCredentials(const char*, const char*) = 0;
  virtual void touchRestart(bool setup) = 0;
};
