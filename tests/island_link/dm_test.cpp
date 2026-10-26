#include "helpers/bridges/IslandLinkBridge.h"
#include "helpers/bridges/IslandLinkFrame.h"
#include "helpers/StaticPoolPacketManager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

uint32_t dm_test_millis = 1000;

static void check(bool okay, const char* name) {
  if (!okay) {
    std::fprintf(stderr, "FAIL: %s\n", name);
    std::exit(1);
  }
}

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

  static unsigned transfer(IslandLinkBridge& source, IslandLinkBridge& dest) {
    unsigned moved = 0;
    IslandLinkBridge::Frame frame = {};
    while (xQueueReceive(source._tx, &frame, 0) == pdTRUE) {
      check(island_link::validateFrame(frame.bytes, frame.length) ==
                island_link::FrameError::None, "bridged frame validates");
      check(xQueueSend(dest._rx, &frame, 0) == pdTRUE,
            "relay receive queue has capacity");
      ++moved;
    }
    return moved;
  }
  static unsigned dropped(const IslandLinkBridge& bridge) {
    return bridge._dropped.load();
  }
};

class FakeRadio : public mesh::Radio {
  struct Frame { uint8_t bytes[MAX_TRANS_UNIT]; int length; };
  Frame incoming_[64] = {};
  size_t head_ = 0, count_ = 0;
 public:
  FakeRadio* peer = nullptr;
  unsigned sent = 0;
  void deliver(const uint8_t* bytes, int length) {
    check(length > 0 && length <= MAX_TRANS_UNIT && count_ < 64,
          "fake radio receive bounds");
    Frame& frame = incoming_[(head_ + count_) % 64];
    frame.length = length;
    std::memcpy(frame.bytes, bytes, length);
    ++count_;
  }
  int recvRaw(uint8_t* bytes, int capacity) override {
    if (!count_) return 0;
    Frame& frame = incoming_[head_];
    check(frame.length <= capacity, "fake radio caller capacity");
    std::memcpy(bytes, frame.bytes, frame.length);
    const int length = frame.length;
    head_ = (head_ + 1) % 64;
    --count_;
    return length;
  }
  uint32_t getEstAirtimeFor(int) override { return 10; }
  float packetScore(float, int) override { return 1.0f; }
  bool startSendRaw(const uint8_t* bytes, int length) override {
    check(peer != nullptr, "fake radio has island peer");
    peer->deliver(bytes, length);
    ++sent;
    return true;
  }
  bool isSendComplete() override { return true; }
  void onSendFinished() override {}
  bool isInRecvMode() const override { return true; }
  float getLastSNR() const override { return 8.0f; }
};

class FakeClock : public mesh::MillisecondClock {
 public:
  unsigned long getMillis() override { return dm_test_millis; }
};
class FakeRtc : public mesh::RTCClock {
 public:
  uint32_t getCurrentTime() override { return 1800000000; }
  void setCurrentTime(uint32_t) override {}
};
class FakeRng : public mesh::RNG {
  uint32_t state_;
 public:
  explicit FakeRng(uint32_t seed) : state_(seed) {}
  void random(uint8_t* dest, size_t length) override {
    for (size_t i = 0; i < length; ++i) {
      state_ ^= state_ << 13;
      state_ ^= state_ >> 17;
      state_ ^= state_ << 5;
      dest[i] = static_cast<uint8_t>(state_);
    }
  }
};

struct NodeParts {
  FakeRadio radio;
  FakeClock clock;
  FakeRtc rtc;
  FakeRng rng;
  StaticPoolPacketManager manager;
  SimpleMeshTables tables;
  explicit NodeParts(uint32_t seed) : rng(seed), manager(32) {}
};

class TestMesh : public mesh::Mesh {
  NodeParts& parts_;
  bool forward_;
  bool client_a_;
  uint8_t peer_pub_[PUB_KEY_SIZE] = {};
  uint8_t known_path_[MAX_PATH_SIZE] = {};
  uint8_t known_path_len_ = 0;
  uint8_t hash_size_;
  bool sent_path_ = false, sent_reply_ = false;

  static uint32_t packetHash32(const mesh::Packet* packet) {
    uint8_t hash[MAX_HASH_SIZE];
    packet->calculatePacketHash(hash);
    uint32_t result;
    std::memcpy(&result, hash, sizeof(result));
    return result;
  }

  void reversePath(const mesh::Packet* packet, uint8_t output[MAX_PATH_SIZE]) {
    const uint8_t size = packet->getPathHashSize();
    const uint8_t count = packet->getPathHashCount();
    for (uint8_t i = 0; i < count; ++i)
      std::memcpy(output + i * size, packet->path + (count - 1 - i) * size,
                  size);
  }
  void ackWithPath(mesh::Packet* source, const uint8_t* path,
                   uint8_t path_len) {
    uint8_t hash[MAX_HASH_SIZE];
    source->calculatePacketHash(hash);
    mesh::Packet* ack = createAck(hash, 4);
    check(ack != nullptr, "ACK packet allocation");
    sendDirect(ack, path, path_len);
  }

 protected:
  bool allowPacketForward(const mesh::Packet*) override { return forward_; }
  int searchPeersByHash(const uint8_t* hash) override {
    return std::memcmp(hash, peer_pub_, PATH_HASH_SIZE) == 0 ? 1 : 0;
  }
  void getPeerSharedSecret(uint8_t* secret, int index) override {
    check(index == 0, "known peer index");
    self_id.calcSharedSecret(secret, peer_pub_);
  }
  mesh::DispatcherAction onRecvPacket(mesh::Packet* packet) override {
    if (bridge) bridge->takeReceivedOrigin(packet);
    return mesh::Mesh::onRecvPacket(packet);
  }
  void logTx(mesh::Packet* packet, int) override {
    if (bridge) bridge->sendPacket(packet);
  }
  void onPeerDataRecv(mesh::Packet* packet, uint8_t type, int,
                      const uint8_t* secret, uint8_t* data, size_t length) override {
    check(type == PAYLOAD_TYPE_TXT_MSG, "real text message decryption");
    if (!client_a_ && length >= 5 && std::memcmp(data, "hello", 5) == 0) {
      ++hello_count;
      uint8_t return_path[MAX_PATH_SIZE] = {};
      reversePath(packet, return_path);
      ackWithPath(packet, return_path, packet->path_len);
      if (!sent_path_) {
        sent_path_ = true;
        // The PATH content names the forward path A->B, while its flood
        // transmission discovers the return path B->A.
        mesh::Packet* path = createPathReturn(peer_pub_, secret, packet->path,
                                               packet->path_len, 0, nullptr, 0);
        check(path != nullptr, "flood PATH allocation");
        expected_path_ack = packetHash32(path);
        sendFlood(path, 0, hash_size_);
      }
    } else if (client_a_ && length >= 5 &&
               std::memcmp(data, "reply", 5) == 0) {
      ++reply_count;
      check(known_path_len_ != 0, "known forward path for reply ACK");
      ackWithPath(packet, known_path_, known_path_len_);
    }
  }
  bool onPeerPathRecv(mesh::Packet* packet, int, const uint8_t*, uint8_t* path,
                      uint8_t path_len, uint8_t, uint8_t*, uint8_t) override {
    ++path_count;
    known_path_len_ = path_len;
    const size_t bytes = (path_len & 63) * ((path_len >> 6) + 1);
    std::memcpy(known_path_, path, bytes);
    if (client_a_) {
      ackWithPath(packet, known_path_, known_path_len_);
      return true; // real Mesh sends reciprocal DIRECT PATH
    }
    if (!sent_reply_) {
      sent_reply_ = true;
      uint8_t secret[PUB_KEY_SIZE];
      self_id.calcSharedSecret(secret, peer_pub_);
      mesh::Identity destination(peer_pub_);
      const uint8_t message[] = {'r', 'e', 'p', 'l', 'y'};
      mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_TXT_MSG, destination,
                                           secret, message, sizeof(message));
      check(reply != nullptr, "direct reply allocation");
      expected_reply_ack = packetHash32(reply);
      sendDirect(reply, known_path_, known_path_len_);
    }
    return false;
  }
  void onAckRecv(mesh::Packet*, uint32_t value) override {
    ++ack_count;
    if (value == expected_initial_ack) initial_acked = true;
    if (value == expected_path_ack) path_acked = true;
    if (value == expected_reply_ack) reply_acked = true;
  }

 public:
  IslandLinkBridge* bridge = nullptr;
  unsigned hello_count = 0, reply_count = 0, path_count = 0, ack_count = 0;
  uint32_t expected_initial_ack = 0, expected_path_ack = 0,
           expected_reply_ack = 0;
  bool initial_acked = false, path_acked = false, reply_acked = false;
  TestMesh(NodeParts& parts, bool forward, bool client_a, uint8_t hash_size)
      : mesh::Mesh(parts.radio, parts.clock, parts.rng, parts.rtc,
                   parts.manager, parts.tables),
        parts_(parts), forward_(forward), client_a_(client_a),
        hash_size_(hash_size) {
    self_id = mesh::LocalIdentity(&parts_.rng);
  }
  void setPeer(const TestMesh& peer) {
    std::memcpy(peer_pub_, peer.self_id.pub_key, PUB_KEY_SIZE);
  }
  void sendInitial(const TestMesh& destination) {
    uint8_t secret[PUB_KEY_SIZE];
    self_id.calcSharedSecret(secret, destination.self_id.pub_key);
    mesh::Identity recipient(destination.self_id.pub_key);
    const uint8_t message[] = {'h', 'e', 'l', 'l', 'o'};
    mesh::Packet* packet = createDatagram(PAYLOAD_TYPE_TXT_MSG, recipient,
                                          secret, message, sizeof(message));
    check(packet != nullptr, "initial flood allocation");
    expected_initial_ack = packetHash32(packet);
    sendFlood(packet, 0, hash_size_);
  }
};

static void runScenario(uint8_t hash_size) {
  dm_test_millis = 1000;
  NodeParts a_parts(0x11112222), h_parts(0x33334444),
            i_parts(0x55556666), b_parts(0x77778888);
  a_parts.radio.peer = &h_parts.radio;
  h_parts.radio.peer = &a_parts.radio;
  i_parts.radio.peer = &b_parts.radio;
  b_parts.radio.peer = &i_parts.radio;
  TestMesh a(a_parts, false, true, hash_size);
  TestMesh h(h_parts, true, false, hash_size);
  TestMesh i(i_parts, true, false, hash_size);
  TestMesh b(b_parts, false, false, hash_size);
  a.setPeer(b);
  b.setPeer(a);
  IslandLinkBridge h_link(&h_parts.manager), i_link(&i_parts.manager);
  IslandLinkTestAccess::ready(h_link);
  IslandLinkTestAccess::ready(i_link);
  h.bridge = &h_link;
  i.bridge = &i_link;
  a.begin(); h.begin(); i.begin(); b.begin();
  a.sendInitial(b);

  unsigned h_to_i = 0, i_to_h = 0;
  unsigned completed_at = 2000;
  for (unsigned step = 0; step < 2000; ++step) {
    dm_test_millis += 10;
    a.loop(); h.loop(); i.loop(); b.loop();
    h_to_i += IslandLinkTestAccess::transfer(h_link, i_link);
    i_to_h += IslandLinkTestAccess::transfer(i_link, h_link);
    h_link.loop(dm_test_millis);
    i_link.loop(dm_test_millis);
    if (completed_at == 2000 && b.hello_count == 1 && a.reply_count == 1 &&
        a.path_count >= 1 && b.path_count >= 1 && a.initial_acked &&
        b.path_acked && b.reply_acked) completed_at = step;
  }
  check(completed_at < 1800, "simulation continued after message completion");
  check(b.hello_count == 1 && a.reply_count == 1,
        "flood DM and direct reply decrypted at opposite island clients");
  check(a.path_count >= 1 && b.path_count >= 1,
        "flood PATH and reciprocal direct PATH processed");
  check(a.initial_acked && b.path_acked && b.reply_acked,
        "initial, PATH, and direct reply ACK hashes crossed islands");
  check(h_to_i == 4 && i_to_h == 3,
        "exactly seven bridge transfers after extended no-echo run");
  // Each link ingress is later emitted over local RF and offered to the
  // bridge by logTx. The bridge rejects it through seen or wire validation.
  check(IslandLinkTestAccess::dropped(h_link) == 3 &&
        IslandLinkTestAccess::dropped(i_link) == 4,
        "all seven reflected RF packets rejected by real bridge policy");
  std::printf("PASS: hash%u real Mesh/Dispatcher two-island DM, H->I=%u I->H=%u\n",
              hash_size, h_to_i, i_to_h);
}

int main() {
  runScenario(2);
  runScenario(3);
}
