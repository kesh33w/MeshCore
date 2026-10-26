#include "IslandLinkBridge.h"

#if defined(WITH_ILINK) && defined(ESP32_PLATFORM)
#include "IslandLinkIdentity.h"
#include "IslandLinkTlsPolicy.h"
#include "IslandLinkProtocol.h"
#include "helpers/NetworkLink.h"
#include <Preferences.h>
#include <WiFi.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#ifndef ILINK_RELAY_SPKI_SHA256
#define ILINK_RELAY_SPKI_SHA256 ""
#endif

namespace {
constexpr uint32_t kHandshakeTimeout = 15000;
constexpr uint32_t kConnectTimeout = 5000;
constexpr uint32_t kQueueMaxAge = 2000;
// ESP32 mbedTLS PEM writers use large stack buffers during first-boot key
// creation. 16 KiB overflowed on V4.3; retain room for the nested crypto calls.
constexpr uint32_t kWorkerStack = 32768;

struct TlsSession {
  mbedtls_net_context socket;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config config;
  mbedtls_ctr_drbg_context random;
  mbedtls_entropy_context entropy;
  mbedtls_x509_crt certificate;
  mbedtls_pk_context key;
  bool ready = false;

  TlsSession() {
    mbedtls_net_init(&socket); mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&config); mbedtls_ctr_drbg_init(&random);
    mbedtls_entropy_init(&entropy); mbedtls_x509_crt_init(&certificate);
    mbedtls_pk_init(&key);
  }
  ~TlsSession() {
    mbedtls_net_free(&socket); mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config); mbedtls_ctr_drbg_free(&random);
    mbedtls_entropy_free(&entropy); mbedtls_x509_crt_free(&certificate);
    mbedtls_pk_free(&key);
  }
  bool initialize(const island_link::Identity& identity) {
    const unsigned char seed[] = "MeshCore island link";
    if (mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy, seed, sizeof(seed)) != 0 ||
        mbedtls_x509_crt_parse(&certificate,
          reinterpret_cast<const unsigned char*>(identity.client_cert_pem),
          strlen(identity.client_cert_pem) + 1) != 0 ||
        mbedtls_pk_parse_key(&key,
          reinterpret_cast<const unsigned char*>(identity.private_key_pem),
          strlen(identity.private_key_pem) + 1, nullptr, 0) != 0 ||
        mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
          MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) return false;
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &random);
    if (!island_link::configurePinnedTls(config, ILINK_RELAY_SPKI_SHA256)) return false;
    mbedtls_ssl_conf_min_version(&config, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    if (mbedtls_ssl_conf_own_cert(&config, &certificate, &key) != 0 ||
        mbedtls_ssl_setup(&ssl, &config) != 0 ||
        mbedtls_ssl_set_hostname(&ssl, island_link::kRelayHostname) != 0) return false;
    mbedtls_ssl_set_bio(&ssl, &socket, mbedtls_net_send, mbedtls_net_recv, nullptr);
    ready = true;
    return true;
  }
  void close() {
    mbedtls_net_free(&socket);
    if (ready) mbedtls_ssl_session_reset(&ssl);
  }
  bool connect(const std::atomic<uint32_t>& revision,
               uint32_t expected_revision) {
    close();
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    // DNS may block this worker, never the radio task. Recheck cancellation
    // before opening a socket after DNS returns.
    if (getaddrinfo(island_link::kRelayHostname, "8443", &hints, &addresses) != 0) return false;
    const auto cancelled = [&]() { return !(expected_revision & 1u) || revision.load() != expected_revision; };
    bool connected = false;
    for (auto a = addresses; a && !cancelled(); a = a->ai_next) {
      socket.fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (socket.fd < 0) continue;
      if (fcntl(socket.fd, F_SETFL, O_NONBLOCK) < 0) { close(); continue; }
      int result = ::connect(socket.fd, a->ai_addr, a->ai_addrlen);
      connected = result == 0;
      uint32_t start = millis();
      while (result != 0 && errno == EINPROGRESS && !cancelled() &&
             !island_link::intervalElapsed(millis(), start, kConnectTimeout)) {
        fd_set write_fds;
        FD_ZERO(&write_fds); FD_SET(socket.fd, &write_fds);
        timeval timeout = {0, 100000};
        int selected = select(socket.fd + 1, nullptr, &write_fds, nullptr, &timeout);
        if (selected > 0) {
          int error = 0; socklen_t size = sizeof(error);
          result = getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &error, &size);
          if (result == 0 && error == 0) { connected = true; break; }
          result = -1;
          break;
        }
        if (selected < 0) break;
        errno = EINPROGRESS;
      }
      if (connected && !cancelled()) break;
      close(); connected = false;
    }
    freeaddrinfo(addresses);
    if (!connected || cancelled()) { close(); return false; }
    const uint32_t start = millis();
    while (!cancelled() && !island_link::intervalElapsed(millis(), start, kHandshakeTimeout)) {
      int result = mbedtls_ssl_handshake(&ssl);
      if (result == 0) return mbedtls_ssl_get_peer_cert(&ssl) != nullptr &&
        mbedtls_ssl_get_verify_result(&ssl) == 0;
      if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) break;
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    close();
    return false;
  }
};
} // namespace

void IslandLinkBridge::begin() {
  if (_rx) return;
  _rx = xQueueCreateStatic(kQueueDepth, sizeof(Frame), _rx_storage, &_rx_control);
  _tx = xQueueCreateStatic(kQueueDepth, sizeof(Frame), _tx_storage, &_tx_control);
  _config = xQueueCreateStatic(1, sizeof(NetworkConfig), _config_storage, &_config_control);
  _network_gate = xSemaphoreCreateMutexStatic(&_network_gate_control);
  if (!_rx || !_tx || !_config || !_network_gate ||
      xTaskCreatePinnedToCore(taskEntry, "IslandLink", kWorkerStack, this, 1, nullptr, 0) != pdPASS)
    _state.store(ResourceError);
}

bool IslandLinkBridge::claimObserverNetwork() {
  if (!_network_gate) return true;
  _network_fallback.store(false);
  if (xSemaphoreTake(_network_gate, 0) != pdTRUE) return false;
  xSemaphoreGive(_network_gate);
  return true;
}

void IslandLinkBridge::configureNetwork(const char* ssid, const char* password, uint8_t power_save) {
  if (!_config) return;
  NetworkConfig config = {};
  strlcpy(config.ssid, ssid, sizeof(config.ssid));
  strlcpy(config.password, password, sizeof(config.password));
  config.power_save = power_save;
  xQueueOverwrite(_config, &config);
}

void IslandLinkBridge::setEnabled(bool enabled) {
  uint32_t previous = _request.load();
  uint32_t next;
  do {
    next = ((previous + 4u) & ~3u) | 2u | (enabled ? 1u : 0u);
  } while (!_request.compare_exchange_weak(previous, next));
  _connected.store(false);
  _generation.fetch_add(1);
  // Persistence and socket teardown happen on the worker; stale records are
  // rejected by generation on both sides, even through a rapid off/on toggle.
}

void IslandLinkBridge::formatStatus(char* reply, size_t capacity) const {
  static const char* names[] = {"starting", "off", "relay pin not configured", "identity error",
    "storage error", "waiting for Wi-Fi", "connecting", "connected", "retrying", "resource error"};
  const State current = _state.load();
  const bool fatal = current == IdentityError || current == StorageError || current == ResourceError;
  const bool pending = _request.load() != _persisted_request.load();
  const char* detail = fatal ? names[current] : pending ? "saving" : "";
  char state[64];
  if (!island_link::isValidSpkiPin(ILINK_RELAY_SPKI_SHA256))
    snprintf(state, sizeof(state), "%s%srelay pin not configured", detail,
             detail[0] ? "; " : "");
  else strlcpy(state, pending && !fatal ? "saving" : names[current], sizeof(state));
  snprintf(reply, capacity, "%s %s\nkey:%s\ntx:%lu rx:%lu drop:%lu\nstack_free_min:%lu", isEnabled() ? "on" : "off",
    state, _identity_ready.load() ? _fingerprint : "pending",
    (unsigned long)_sent.load(), (unsigned long)_received.load(), (unsigned long)_dropped.load(),
    (unsigned long)_stack_free_min.load());
}

void IslandLinkBridge::taskEntry(void* argument) {
  static_cast<IslandLinkBridge*>(argument)->worker();
  vTaskDelete(nullptr);
}

void IslandLinkBridge::worker() {
  island_link::Identity identity;
  auto identity_status = island_link::loadOrCreateIdentity(identity, false);
  if (identity_status != island_link::IdentityStatus::Loaded &&
      identity_status != island_link::IdentityStatus::WaitingForEntropy) {
    _state.store(IdentityError); return;
  }
  if (identity_status == island_link::IdentityStatus::Loaded) {
    strlcpy(_fingerprint, identity.spki_sha256_hex, sizeof(_fingerprint));
    _identity_ready.store(true);
  }
  Preferences prefs;
  if (!prefs.begin("ilink", false)) { _state.store(StorageError); return; }
  uint32_t initial = 0;
  const uint32_t restored = 2u | (prefs.getBool("enabled", false) ? 1u : 0u);
  const bool loaded_setting = _request.compare_exchange_strong(initial, restored);
  uint32_t saved_revision = loaded_setting ? restored : 0;
  if (loaded_setting) _persisted_request.store(restored);
  bool storage_ok = true;
  TlsSession tls;
  island_link::StreamDecoder decoder;
  island_link::LinkTimers timers;
  island_link::ReconnectBackoff backoff;
  NetworkConfig network_config = {};
  bool network_started = false;
  uint32_t retry_since = millis(), retry_delay = 0;
  uint32_t active_revision = _request.load();
  uint32_t stack_sample_at = 0;
  uint32_t session_generation = 0, connected_at = 0;
  bool backoff_reset = false;
  uint8_t wire[MAX_TRANS_UNIT + 1];
  size_t wire_length = 0, wire_offset = 0;
  bool wire_keepalive = false;
  uint32_t write_started = 0;
  const auto disconnect = [&]() {
    _connected.store(false);
    _generation.fetch_add(1);
    tls.close(); decoder.reset(); timers.onDisconnected();
    wire_length = wire_offset = 0;
    Frame discarded;
    while (xQueueReceive(_tx, &discarded, 0) == pdTRUE) ++_dropped;
    while (xQueueReceive(_rx, &discarded, 0) == pdTRUE) ++_dropped;
  };

  for (;;) {
    if (uint32_t(millis() - stack_sample_at) >= 1000) {
      stack_sample_at = millis();
      _stack_free_min.store(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t));
    }
    const uint32_t revision = _request.load();
    if (revision != saved_revision) {
      storage_ok = prefs.putBool("enabled", (revision & 1u) != 0) == 1;
      if (!storage_ok) {
        disconnect(); _state.store(StorageError);
        // Do not claim the requested setting was persisted. Retry on next command.
        saved_revision = revision;
        vTaskDelay(pdMS_TO_TICKS(50)); continue;
      }
      saved_revision = revision;
      _persisted_request.store(revision);
    }
    if (revision != active_revision) {
      disconnect(); active_revision = revision;
      retry_since = millis(); retry_delay = 0; backoff.reset();
    }
    NetworkConfig incoming;
    if (xQueueReceive(_config, &incoming, 0) == pdTRUE) {
      if (strcmp(incoming.ssid, network_config.ssid) || strcmp(incoming.password, network_config.password))
        network_started = false;
      network_config = incoming;
    }
    NetworkLink& network = activeNetworkLink();
    if (_network_fallback.load() && xSemaphoreTake(_network_gate, 0) == pdTRUE) {
      if (_network_fallback.load()) {
        if (!network_started && network_config.ssid[0])
          network_started = network.begin(network_config.ssid, network_config.password);
        if (network_started) {
          network.updateWifiCredentials(network_config.ssid, network_config.password);
          network.maintain(millis(), network_config.power_save);
        }
      }
      xSemaphoreGive(_network_gate);
    } else {
      network_started = false; // reestablish ownership only after observer releases it
    }

    if (!_identity_ready.load() && WiFi.status() == WL_CONNECTED) {
      // The ESP32 hardware entropy source is active with associated Wi-Fi.
      // Never create a permanent key before RF entropy is available.
      identity_status = island_link::loadOrCreateIdentity(identity, true);
      if (identity_status != island_link::IdentityStatus::Created &&
          identity_status != island_link::IdentityStatus::Loaded) {
        _state.store(IdentityError); return;
      }
      strlcpy(_fingerprint, identity.spki_sha256_hex, sizeof(_fingerprint));
      _identity_ready.store(true);
    }

    const bool pin_configured = island_link::isValidSpkiPin(ILINK_RELAY_SPKI_SHA256);
    const bool usable = storage_ok && revision == saved_revision && revision == _request.load() &&
      isEnabled() && _identity_ready.load() && pin_configured && network.isConnected();
    if (!usable) {
      if (tls.socket.fd >= 0 || _connected.load()) {
        disconnect(); retry_since = millis(); retry_delay = backoff.nextDelayMs();
      }
      _state.store(!storage_ok ? StorageError : !pin_configured ? NoPin : !isEnabled() ? Off : NoWifi);
      vTaskDelay(pdMS_TO_TICKS(50)); continue;
    }
    uint32_t now = millis();
    if (!_connected.load()) {
      if (!island_link::intervalElapsed(now, retry_since, retry_delay)) {
        _state.store(Retry); vTaskDelay(pdMS_TO_TICKS(50)); continue;
      }
      _state.store(Connecting);
      if (!tls.ready && !tls.initialize(identity)) { _state.store(ResourceError); return; }
      if (!tls.connect(_request, active_revision)) {
        disconnect(); retry_since = millis(); retry_delay = backoff.nextDelayMs();
        _state.store(Retry); vTaskDelay(pdMS_TO_TICKS(50)); continue;
      }
      if (!isEnabled() || _request.load() != active_revision) { disconnect(); continue; }
      decoder.reset(); timers.onConnected(millis());
      session_generation = _generation.fetch_add(1) + 1;
      _session_request.store(active_revision);
      connected_at = millis(); backoff_reset = false;
      _connected.store(true); _state.store(Connected);
    }

    bool failed = false;
    uint8_t input[256];
    // Bound work per tick even if the peer continually supplies bytes.
    for (unsigned chunk = 0; chunk < 4 && !failed; ++chunk) {
      int received = mbedtls_ssl_read(&tls.ssl, input, sizeof(input));
      if (received == MBEDTLS_ERR_SSL_WANT_READ || received == MBEDTLS_ERR_SSL_WANT_WRITE) break;
      if (received <= 0) { failed = true; break; }
      timers.onReceive(millis());
      for (size_t i = 0; i < static_cast<size_t>(received); ++i) {
        const auto event = decoder.feed(input[i]);
        if (event == island_link::StreamEvent::Invalid) { ++_dropped; failed = true; break; }
        if (event == island_link::StreamEvent::Frame) {
          Frame frame = {};
          frame.generation = session_generation; frame.queued_at = millis();
          frame.length = decoder.size(); // actual size_t count, already validated
          memcpy(frame.bytes, decoder.data(), frame.length);
          if (!isEnabled() || _request.load() != active_revision ||
              xQueueSend(_rx, &frame, 0) != pdTRUE) ++_dropped;
        }
      }
    }
    now = millis();
    if (timers.receiveTimedOut(now)) failed = true;
    if (!backoff_reset && island_link::intervalElapsed(now, connected_at, 90000)) {
      backoff.reset(); backoff_reset = true;
    }
    if (!failed && wire_length == 0) {
      if (timers.keepaliveDue(now)) {
        wire[0] = 0; wire_length = 1; wire_keepalive = true;
      } else {
        Frame frame;
        if (xQueueReceive(_tx, &frame, 0) == pdTRUE) {
          if (frame.generation != session_generation ||
              island_link::intervalElapsed(now, frame.queued_at, kQueueMaxAge)) ++_dropped;
          else wire_length = island_link::writeWireFrame(frame.bytes, frame.length, wire, sizeof(wire));
          wire_keepalive = false;
        }
      }
      wire_offset = 0; write_started = now;
    }
    if (!failed && wire_length && isEnabled() && _request.load() == active_revision) {
      int written = mbedtls_ssl_write(&tls.ssl, wire + wire_offset, wire_length - wire_offset);
      if (written > 0) {
        wire_offset += static_cast<size_t>(written);
        if (wire_offset == wire_length) {
          if (wire_keepalive) timers.onKeepaliveSent(millis());
          else ++_sent;
          wire_length = wire_offset = 0;
        }
      } else if (written != MBEDTLS_ERR_SSL_WANT_READ && written != MBEDTLS_ERR_SSL_WANT_WRITE) failed = true;
      if (wire_length && island_link::intervalElapsed(millis(), write_started, kQueueMaxAge)) failed = true;
    }
    if (failed) {
      disconnect(); retry_since = millis(); retry_delay = backoff.nextDelayMs(); _state.store(Retry);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
#endif
