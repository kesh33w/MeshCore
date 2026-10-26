#pragma once

#include <Packet.h>
#include <string.h>

namespace mesh {

class MeshTables {
 public:
  virtual ~MeshTables() {}
  virtual bool wasSeen(const Packet* packet) = 0;
  virtual void markSeen(const Packet* packet) = 0;
  virtual void clear(const Packet* packet) = 0;
};

class PacketManager {
 public:
  virtual ~PacketManager() {}
  virtual Packet* allocNew() = 0;
  virtual void free(Packet* packet) = 0;
  virtual int getFreeCount() const = 0;
  virtual void queueInbound(Packet* packet, uint32_t scheduled_for) = 0;
};

}  // namespace mesh
