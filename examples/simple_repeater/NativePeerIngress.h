#pragma once
#include "LinkIngress.h"
#include <atomic>
namespace private_remote {
// One network producer and one RF-loop consumer. Never call radio routing on worker.
class NativePeerIngress {
 struct Item {uint8_t raw[max_raw]{};size_t length=0;uint32_t at=0,ttl=0,epoch=0;};
 Item items_[4]{};
 std::atomic<unsigned> read_{0},write_{0};std::atomic<uint32_t> epoch_{1};
 public:
 bool from_peer(const InboundPacket& packet,IngressPolicy& shared_policy,
                const char* peer,const char* radio,bool trusted_time,uint64_t unix_ms,uint32_t mono_ms,uint32_t ttl_ms=15000) {
  if(!native_peer_payload(packet.raw,packet.length))return false;
  uint8_t digest[32];PacketView view{};
  if(!inspect(packet.raw,packet.length,view)||view.width!=packet.width||
     !link_digest(packet.raw,packet.length,digest)||memcmp(digest,packet.digest,32))return false;
  const unsigned w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
  if(w-r>=4||!shared_policy.admit(packet,peer,radio,trusted_time,unix_ms,ttl_ms))return false;
  auto& item=items_[w%4];memcpy(item.raw,packet.raw,packet.length);item.length=packet.length;
  item.at=mono_ms;const uint64_t remaining=packet.expires-unix_ms;
  item.ttl=remaining>5000?5000:static_cast<uint32_t>(remaining);item.epoch=epoch_.load(std::memory_order_relaxed);
  write_.store(w+1,std::memory_order_release);return true;
 }
 void disconnect(){epoch_.fetch_add(1,std::memory_order_release);}
 template<class Dispatcher> bool tick(Dispatcher& dispatcher,uint32_t now) {
  const unsigned r=read_.load(std::memory_order_relaxed),w=write_.load(std::memory_order_acquire);
  if(r==w)return false;
  const Item item=items_[r%4];read_.store(r+1,std::memory_order_release);
  if(item.epoch!=epoch_.load(std::memory_order_acquire)||uint32_t(now-item.at)>=item.ttl)return false;
  return dispatcher.receivePeerPacket(item.raw,item.length,item.ttl-uint32_t(now-item.at));
 }
};
}
