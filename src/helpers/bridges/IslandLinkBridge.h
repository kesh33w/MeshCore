#pragma once

#if defined(WITH_ILINK) && defined(ESP32_PLATFORM)
#include "helpers/SimpleMeshTables.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// Packet pool and seen table belong exclusively to the radio task. The worker
// owns Wi-Fi fallback, TLS, identity, and persistence; only fixed records cross.
class IslandLinkBridge {
 public:
  explicit IslandLinkBridge(mesh::PacketManager* manager) : _manager(manager) {}
  void begin();
  void loop(uint32_t now);
  void sendPacket(mesh::Packet* packet); // completed logTx only
  bool takeReceivedOrigin(const mesh::Packet* packet);
  void setEnabled(bool enabled);
  void formatStatus(char* reply, size_t capacity) const;
  void configureNetwork(const char* ssid, const char* password, uint8_t power_save);
  // Must succeed before starting an observer task. Never waits on the radio loop.
  bool claimObserverNetwork();
  void allowNetworkFallback(bool allow) { _network_fallback.store(allow); }
  bool isEnabled() const { return (_request.load() & 1u) != 0; }
  bool isBusy() const { return _connected.load(); }

 private:
  friend class IslandLinkTestAccess;
  static constexpr size_t kQueueDepth = 4;
  struct Frame {
    uint32_t generation;
    uint32_t queued_at;
    size_t length;
    uint8_t bytes[MAX_TRANS_UNIT];
  };
  struct NetworkConfig {
    char ssid[32];
    char password[64];
    uint8_t power_save;
  };
  enum State { Starting, Off, NoPin, IdentityError, StorageError, NoWifi,
               Connecting, Connected, Retry, ResourceError };
  mesh::PacketManager* _manager;
  SimpleMeshTables _seen;
  const mesh::Packet* _admitted[kQueueDepth] = {};
  QueueHandle_t _rx = nullptr, _tx = nullptr, _config = nullptr;
  StaticQueue_t _rx_control, _tx_control, _config_control;
  uint8_t _rx_storage[kQueueDepth * sizeof(Frame)];
  uint8_t _tx_storage[kQueueDepth * sizeof(Frame)];
  uint8_t _config_storage[sizeof(NetworkConfig)];
  StaticSemaphore_t _network_gate_control;
  SemaphoreHandle_t _network_gate = nullptr;
  std::atomic<bool> _network_fallback{false};
  std::atomic<bool> _connected{false}, _identity_ready{false};
  // Low bit is requested on/off, bit 1 distinguishes boot-unloaded from off.
  // The remaining bits are a revision, published with the value atomically.
  std::atomic<uint32_t> _request{0}, _session_request{0}, _generation{1};
  std::atomic<uint32_t> _persisted_request{0};
  std::atomic<State> _state{Starting};
  std::atomic<uint32_t> _sent{0}, _received{0}, _dropped{0};
  std::atomic<uint32_t> _stack_free_min{0};
  char _fingerprint[65] = {};
  static void taskEntry(void* argument);
  void worker();
};
#endif
