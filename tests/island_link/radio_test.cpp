#include "helpers/bridges/IslandLinkBridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

uint32_t island_link_test_millis = 0;

class IslandLinkTestAccess {
 public:
  static void ready(IslandLinkBridge& bridge) {
    bridge._rx = xQueueCreateStatic(bridge.kQueueDepth, sizeof(IslandLinkBridge::Frame),
                                    bridge._rx_storage, &bridge._rx_control);
    bridge._tx = xQueueCreateStatic(bridge.kQueueDepth, sizeof(IslandLinkBridge::Frame),
                                    bridge._tx_storage, &bridge._tx_control);
    bridge._request.store(3);
    bridge._session_request.store(3);
    bridge._connected.store(true);
  }

  static bool enqueue(IslandLinkBridge& bridge, const uint8_t* bytes,
                      size_t actual_length, uint32_t queued_at,
                      uint32_t generation = 1) {
    IslandLinkBridge::Frame frame = {};
    frame.generation = generation;
    frame.queued_at = queued_at;
    frame.length = actual_length;
    if (bytes) std::memcpy(frame.bytes, bytes, actual_length > MAX_TRANS_UNIT
                                         ? MAX_TRANS_UNIT : actual_length);
    return xQueueSend(bridge._rx, &frame, 0) == pdTRUE;
  }

  static bool popTx(IslandLinkBridge& bridge, uint8_t* bytes, size_t* length) {
    IslandLinkBridge::Frame frame = {};
    if (xQueueReceive(bridge._tx, &frame, 0) != pdTRUE) return false;
    *length = frame.length;
    std::memcpy(bytes, frame.bytes, frame.length);
    return true;
  }

  static size_t txCount(const IslandLinkBridge& bridge) {
    return uxQueueMessagesWaiting(bridge._tx);
  }
  static uint32_t received(const IslandLinkBridge& bridge) {
    return bridge._received.load();
  }
  static uint32_t dropped(const IslandLinkBridge& bridge) {
    return bridge._dropped.load();
  }
  static void connected(IslandLinkBridge& bridge, bool value) {
    bridge._connected.store(value);
  }
  static void enabled(IslandLinkBridge& bridge, bool value) {
    bridge._request.store(value ? 3 : 2);
  }
  static void request(IslandLinkBridge& bridge, uint32_t value) {
    bridge._request.store(value);
  }
  static void sessionRequest(IslandLinkBridge& bridge, uint32_t value) {
    bridge._session_request.store(value);
  }
  static void generation(IslandLinkBridge& bridge, uint32_t value) {
    bridge._generation.store(value);
  }
};

class TestManager : public mesh::PacketManager {
 public:
  mesh::Packet packets[12];
  bool in_use[12] = {};
  mesh::Packet* inbound[12] = {};
  size_t inbound_count = 0;
  int alloc_calls = 0;
  int free_calls = 0;
  int free_override = -1;

  mesh::Packet* allocNew() override {
    ++alloc_calls;
    for (size_t i = 0; i < 12; ++i) {
      if (!in_use[i]) {
        in_use[i] = true;
        return &packets[i];
      }
    }
    return nullptr;
  }
  void free(mesh::Packet* packet) override {
    ++free_calls;
    for (size_t i = 0; i < 12; ++i) {
      if (&packets[i] == packet) {
        in_use[i] = false;
        return;
      }
    }
    std::abort();
  }
  int getFreeCount() const override {
    if (free_override >= 0) return free_override;
    int count = 0;
    for (bool used : in_use) if (!used) ++count;
    return count;
  }
  void queueInbound(mesh::Packet* packet, uint32_t) override {
    if (inbound_count == 12) std::abort();
    inbound[inbound_count++] = packet;
  }
};

static void check(bool result, const char* name) {
  if (!result) {
    std::fprintf(stderr, "FAIL: %s\n", name);
    std::exit(1);
  }
}

static mesh::Packet makePacket(uint8_t seed, bool direct = false) {
  mesh::Packet packet;
  packet.header = direct ? ROUTE_TYPE_DIRECT : ROUTE_TYPE_FLOOD;
  packet.path_len = direct ? 65 : 64; // two-byte hashes, one hop or no hops
  packet.path[0] = 0x12;
  packet.path[1] = 0x34;
  packet.payload_len = 1;
  packet.payload[0] = seed;
  return packet;
}

static void enqueuePacket(IslandLinkBridge& bridge, const mesh::Packet& packet,
                          uint32_t queued_at, uint32_t generation = 1) {
  uint8_t raw[MAX_TRANS_UNIT] = {};
  const size_t length = packet.writeTo(raw);
  check(IslandLinkTestAccess::enqueue(bridge, raw, length, queued_at, generation),
        "enqueue test receive frame");
}

static void testSendAndEcho() {
  TestManager manager;
  IslandLinkBridge bridge(&manager);
  IslandLinkTestAccess::ready(bridge);
  island_link_test_millis = 100;
  mesh::Packet outbound = makePacket(1, true);
  bridge.sendPacket(&outbound);
  check(IslandLinkTestAccess::txCount(bridge) == 1, "valid direct sent to queue");
  uint8_t raw[MAX_TRANS_UNIT] = {};
  size_t length = 0;
  check(IslandLinkTestAccess::popTx(bridge, raw, &length), "take queued frame");
  check(length == static_cast<size_t>(outbound.getRawLength()) &&
            raw[0] == ROUTE_TYPE_DIRECT && raw[1] == 65,
        "real Packet::writeTo wire bytes");
  check(IslandLinkTestAccess::enqueue(bridge, raw, length, 100), "enqueue echo");
  bridge.loop(100);
  check(IslandLinkTestAccess::received(bridge) == 0 && manager.inbound_count == 0 &&
            manager.alloc_calls == 1 && manager.free_calls == 1,
        "outbound seen table suppresses inbound echo");

  mesh::Packet inbound = makePacket(2);
  enqueuePacket(bridge, inbound, 101);
  bridge.loop(101);
  check(IslandLinkTestAccess::received(bridge) == 1 && manager.inbound_count == 1,
        "valid inbound queued to actual packet manager");
  check(bridge.takeReceivedOrigin(manager.inbound[0]) &&
            !bridge.takeReceivedOrigin(manager.inbound[0]), "origin taken once");
  bridge.sendPacket(manager.inbound[0]);
  check(IslandLinkTestAccess::txCount(bridge) == 0,
        "inbound seen table suppresses outbound echo");
}

static void testAdmissionGuards() {
  TestManager manager;
  IslandLinkBridge bridge(&manager);
  IslandLinkTestAccess::ready(bridge);
  mesh::Packet packet = makePacket(3);

  enqueuePacket(bridge, packet, 100);
  IslandLinkTestAccess::enabled(bridge, false);
  bridge.loop(100);
  IslandLinkTestAccess::enabled(bridge, true);
  enqueuePacket(bridge, packet, 101);
  IslandLinkTestAccess::connected(bridge, false);
  bridge.loop(101);
  IslandLinkTestAccess::connected(bridge, true);
  enqueuePacket(bridge, packet, 102, 1);
  IslandLinkTestAccess::generation(bridge, 2);
  bridge.loop(102);
  check(manager.alloc_calls == 0 && IslandLinkTestAccess::dropped(bridge) == 3,
        "off, disconnected, and old generation drop before allocation");

  uint8_t raw[MAX_TRANS_UNIT] = {};
  packet.writeTo(raw);
  check(IslandLinkTestAccess::enqueue(bridge, raw,
        std::numeric_limits<size_t>::max(), 200, 2), "enqueue oversized length");
  bridge.loop(200);
  check(manager.alloc_calls == 0 && IslandLinkTestAccess::dropped(bridge) == 4,
        "oversized actual size_t rejected before allocation or narrowing");
  check(IslandLinkTestAccess::enqueue(bridge, raw, 2, 201, 2),
        "enqueue malformed frame");
  bridge.loop(201);
  check(manager.alloc_calls == 0 && IslandLinkTestAccess::dropped(bridge) == 5,
        "malformed frame rejected before allocation");

  enqueuePacket(bridge, packet, 300, 2);
  bridge.loop(2300);
  check(manager.alloc_calls == 0 && IslandLinkTestAccess::dropped(bridge) == 6,
        "2000 millisecond boundary expires");
  enqueuePacket(bridge, packet, 300, 2);
  bridge.loop(2299);
  check(manager.alloc_calls == 1 && manager.inbound_count == 1,
        "1999 millisecond frame admitted");
}

static void testStaleSessionAfterRapidToggle() {
  TestManager manager;
  IslandLinkBridge bridge(&manager);
  IslandLinkTestAccess::ready(bridge);
  mesh::Packet packet = makePacket(7);

  // Simulate a stale worker leaving _connected true while the requested state
  // moves from on (3), through off (6), back to on (11).
  IslandLinkTestAccess::request(bridge, 6);
  IslandLinkTestAccess::request(bridge, 11);
  enqueuePacket(bridge, packet, 100);
  bridge.loop(100);
  bridge.sendPacket(&packet);
  check(manager.alloc_calls == 0 && manager.inbound_count == 0 &&
            IslandLinkTestAccess::txCount(bridge) == 0,
        "stale session revision blocks both admission and transmit");
  IslandLinkTestAccess::sessionRequest(bridge, 11);
  enqueuePacket(bridge, packet, 101);
  bridge.loop(101);
  check(manager.alloc_calls == 1 && manager.inbound_count == 1,
        "matching new session revision resumes admission");
}

static void testReserveAndCapacity() {
  TestManager manager;
  IslandLinkBridge bridge(&manager);
  IslandLinkTestAccess::ready(bridge);
  mesh::Packet packet = makePacket(10);
  manager.free_override = 4;
  enqueuePacket(bridge, packet, 100);
  bridge.loop(100);
  check(manager.alloc_calls == 0 && manager.inbound_count == 0,
        "four packets reserved for RF before allocation");
  manager.free_override = 5;
  enqueuePacket(bridge, packet, 101);
  bridge.loop(101);
  check(manager.alloc_calls == 1 && manager.inbound_count == 1,
        "fifth free packet permits admission");

  for (uint8_t seed = 11; seed <= 13; ++seed) {
    mesh::Packet other = makePacket(seed);
    enqueuePacket(bridge, other, 101);
    bridge.loop(101);
  }
  check(manager.inbound_count == 4, "four admission slots fill");
  mesh::Packet fifth = makePacket(14);
  enqueuePacket(bridge, fifth, 101);
  bridge.loop(101);
  check(manager.alloc_calls == 4 && manager.inbound_count == 4,
        "full admission slots drop before allocation");
}

static void testOutboundQueueFullAndOff() {
  TestManager manager;
  IslandLinkBridge bridge(&manager);
  IslandLinkTestAccess::ready(bridge);
  island_link_test_millis = 500;
  for (uint8_t seed = 20; seed <= 24; ++seed) {
    mesh::Packet packet = makePacket(seed);
    bridge.sendPacket(&packet);
  }
  check(IslandLinkTestAccess::txCount(bridge) == 4 &&
            IslandLinkTestAccess::dropped(bridge) == 1,
        "bounded transmit queue drops fifth frame");
  uint8_t raw[MAX_TRANS_UNIT] = {};
  size_t length = 0;
  check(IslandLinkTestAccess::popTx(bridge, raw, &length),
        "free a transmit queue slot");
  mesh::Packet retry = makePacket(24);
  bridge.sendPacket(&retry);
  check(IslandLinkTestAccess::txCount(bridge) == 4,
        "queue-full rejection does not mark frame seen before retry");

  mesh::Packet invalid = makePacket(26);
  invalid.payload_len = MAX_PACKET_PAYLOAD + 1;
  bridge.sendPacket(&invalid);
  check(IslandLinkTestAccess::dropped(bridge) == 2,
        "oversized packet rejected before writeTo");
  IslandLinkTestAccess::connected(bridge, false);
  mesh::Packet packet = makePacket(25);
  bridge.sendPacket(&packet);
  check(IslandLinkTestAccess::txCount(bridge) == 4,
        "disconnected send does not enqueue");
}

int main() {
  testSendAndEcho();
  testAdmissionGuards();
  testStaleSessionAfterRapidToggle();
  testReserveAndCapacity();
  testOutboundQueueFullAndOff();
  std::puts("PASS: real IslandLinkRadio send and admission with host queues");
}
