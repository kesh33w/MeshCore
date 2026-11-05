#pragma once
#include "Runtime.h"
#include "LinkEnvelope.h"
#include "LinkIngress.h"
#include "MeshCoreDigest.h"
#include <atomic>
namespace private_remote {
struct PrivatePeerConfig {
  bool enabled=false;
  const char* origin=nullptr;
  const char* peer_id=nullptr;
  const char* radio=nullptr;
  uint32_t ttl_ms=15000;
};
// One RF producer, one network worker consumer. No network operations in offer().
class PrivatePeerOutbox final : public ReceptionSink {
 public:
  explicit PrivatePeerOutbox(PrivatePeerConfig config):config_(config) {}
  bool configuration_valid() const {return valid_config();}
  const char* configured_peer() const {return config_.peer_id;}
  uint32_t configured_ttl() const {return config_.ttl_ms;}
  bool ready() const override {return (session_.load(std::memory_order_acquire)&1)!=0;}
  bool offer(const Reception& r) override {
    const unsigned epoch=session_.load(std::memory_order_acquire);
    if(!(epoch&1) || !r.internet_candidate || !native_peer_payload(r.raw,r.length)) return false;
    const unsigned w=write_.load(std::memory_order_relaxed),rd=read_.load(std::memory_order_acquire);
    if(w-rd>=4) return false;
    queue_[w%4]=r; generations_[w%4]=epoch;write_.store(w+1,std::memory_order_release);return true;
  }
  uint32_t staged=0, frames_written=0, dropped=0;
  // Network worker only. Use this SAME cache when admitting inbound peer packets.
  IngressPolicy& replay_policy() {return replay_;}
  template<class Stream> void tick(Stream& stream,uint32_t now,uint64_t unix_ms,bool trusted_time) {
    if(!valid_config() || !trusted_time || unix_ms<1704067200000ULL) {stream.close();reset();return;}
    if(!stream.connected()) {reset();return;}
    const char* peer=stream.authenticated_peer_id();
    if(!peer || strcmp(peer,config_.peer_id)) {stream.close();reset();return;}
    session_.fetch_or(1,std::memory_order_release);
    if(txlen_) {
      if(uint32_t(now-captured_)>=config_.ttl_ms || unix_ms>=expires_) {stream.close();reset();return;}
      const size_t remaining=txlen_-txpos_,sent=stream.write(tx_+txpos_,remaining);
      if(sent>remaining) {stream.close();reset();return;}
      txpos_+=sent;
      if(txpos_==txlen_) {++frames_written;txlen_=txpos_=0;}
      return;
    }
    Reception r{};if(!pop(r))return;
    const uint32_t age=uint32_t(now-r.received_ms);
    if(age>5000 || age>=config_.ttl_ms || unix_ms<age) {++dropped;return;}
    const uint64_t issued=unix_ms-age;
    if(issued>UINT64_MAX-config_.ttl_ms) {++dropped;return;}
    uint8_t digest[32];
    expires_=issued+config_.ttl_ms;
    if(!link_digest(r.raw,r.length,digest) || !replay_.remember(digest,expires_,unix_ms)) {++dropped;return;}
    txlen_=link_frame(r.raw,r.length,digest,config_.origin,config_.radio,issued,expires_,tx_,sizeof(tx_));
    if(!txlen_) {++dropped;return;}
    captured_=r.received_ms;txpos_=0;++staged;
  }
 private:
  PrivatePeerConfig config_;
  IngressPolicy replay_;
  std::atomic<unsigned> session_{0},write_{0},read_{0};
  Reception queue_[4]{};unsigned generations_[4]{};
  uint8_t tx_[2053]{};size_t txlen_=0,txpos_=0;uint32_t captured_=0;uint64_t expires_=0;
  bool valid_config() const {return config_.enabled && safe_label(config_.peer_id) && safe_label(config_.origin) && strcmp(config_.peer_id,config_.origin)!=0 && safe_label(config_.radio) && config_.ttl_ms>=100 && config_.ttl_ms<=60000;}
  bool pop(Reception& r) {
    const unsigned rd=read_.load(std::memory_order_relaxed),w=write_.load(std::memory_order_acquire);
    if(rd==w)return false;
    r=queue_[rd%4];const unsigned epoch=generations_[rd%4];read_.store(rd+1,std::memory_order_release);
    if(epoch!=session_.load(std::memory_order_acquire)){++dropped;return false;}return true;
  }
  void reset() {
    const unsigned previous=session_.load(std::memory_order_acquire);
    if(previous&1) session_.store((previous+2)&~1u,std::memory_order_release);
    // A concurrent RF offer with the old epoch remains tagged and cannot replay.
    Reception ignored{};
    for(unsigned i=0;i<4;++i) {unsigned before=read_.load();if(pop(ignored))++dropped;if(read_.load()==before)break;}
    if(txlen_)++dropped;
    txlen_=txpos_=0;
  }
};
} // namespace private_remote
