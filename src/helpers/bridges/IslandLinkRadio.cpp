#include "IslandLinkBridge.h"

#if defined(WITH_ILINK) && defined(ESP32_PLATFORM)
#include "IslandLinkProtocol.h"
#include <Arduino.h>

namespace { constexpr uint32_t kQueueMaxAge = 2000; }

void IslandLinkBridge::sendPacket(mesh::Packet* packet) {
  if (!_connected.load() || !isEnabled() || _session_request.load() != _request.load() || !_tx || !packet) return;
  // Check internal lengths before writeTo() touches fixed output storage.
  if (packet->path_len > 255 || !mesh::Packet::isValidPathLen(packet->path_len) ||
      packet->payload_len > MAX_PACKET_PAYLOAD || packet->getRawLength() > MAX_TRANS_UNIT) {
    ++_dropped; return;
  }
  Frame frame = {};
  frame.generation = _generation.load();
  frame.queued_at = millis();
  frame.length = packet->writeTo(frame.bytes);
  if (island_link::validateFrame(frame.bytes, frame.length) != island_link::FrameError::None ||
      _seen.wasSeen(packet)) { ++_dropped; return; }
  if (xQueueSend(_tx, &frame, 0) == pdTRUE) _seen.markSeen(packet);
  else ++_dropped;
}

void IslandLinkBridge::loop(uint32_t now) {
  if (!_rx) return;
  Frame frame;
  // At most one admission per loop; RF processing keeps priority.
  if (xQueueReceive(_rx, &frame, 0) != pdTRUE) return;
  if (!isEnabled() || !_connected.load() || _session_request.load() != _request.load() ||
      frame.generation != _generation.load() ||
      island_link::intervalElapsed(now, frame.queued_at, kQueueMaxAge) ||
      island_link::validateFrame(frame.bytes, frame.length) != island_link::FrameError::None) {
    ++_dropped; return;
  }
  size_t slot = 0;
  while (slot < kQueueDepth && _admitted[slot]) ++slot;
  if (slot == kQueueDepth || _manager->getFreeCount() <= 4) { ++_dropped; return; }
  auto packet = _manager->allocNew();
  if (!packet) { ++_dropped; return; }
  // Only this call narrows the already validated length to the upstream API.
  if (!packet->readFrom(frame.bytes, static_cast<uint8_t>(frame.length)) || _seen.wasSeen(packet)) {
    _manager->free(packet); ++_dropped; return;
  }
  packet->_snr = 0; // never exposed as a measurement; origin guard suppresses RF records
  _seen.markSeen(packet);
  _admitted[slot] = packet;
  // The production packet pool and inbound queue both hold 32 packets. Having
  // more than four free pool entries above guarantees inbound capacity here.
  _manager->queueInbound(packet, now); // normal mesh duplicate/path/flood processing
  ++_received;
}

bool IslandLinkBridge::takeReceivedOrigin(const mesh::Packet* packet) {
  for (auto& entry : _admitted) {
    if (entry == packet) { entry = nullptr; return true; }
  }
  return false;
}

#endif
