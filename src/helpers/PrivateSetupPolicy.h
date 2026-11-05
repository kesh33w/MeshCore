#pragma once
#include <stdint.h>
#include <string.h>
namespace private_remote {
// The preconfigured private setup AP is for Wi-Fi onboarding, not maintenance.
inline bool wifi_setup_key_allowed(const char* key) {
  return key && (strcmp(key, "wifi.ssid") == 0 || strcmp(key, "wifi.pwd") == 0);
}

// Private observer builds use Wi-Fi. A prior setup-complete marker cannot
// supply a network after the maintainer clears the saved SSID for handoff.
inline bool wifi_setup_required(bool recovery_requested, const char* ssid) {
  return recovery_requested || !ssid || ssid[0] == '\0';
}
// A deliberate software restart is the only way to enter recovery. Power
// loss, watchdog resets and normal Wi-Fi outages must not expose the portal.
constexpr uint32_t setup_cookie_value = 0x53545550;
inline bool consume_setup_cookie(uint32_t& cookie, bool software_reset) {
  const bool requested = software_reset && cookie == setup_cookie_value;
  cookie = 0;
  return requested;
}
inline bool setup_password_ready(const char* password) {
  return password && strlen(password) >= 8 && strcmp(password, "password") != 0;
}
inline const char* private_setup_blocker(bool provisioned_build, const char* saved_password) {
  if (!provisioned_build) return "private_build_not_provisioned";
  return setup_password_ready(saved_password) ? "" : "admin_password_not_provisioned";
}
}
