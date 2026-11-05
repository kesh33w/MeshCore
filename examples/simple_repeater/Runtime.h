#pragma once
#include "PrivateRfCapture.h"
namespace private_remote {
class ReceptionSink {
 public:
  virtual ~ReceptionSink()=default;
  // RF-thread contract: these methods only inspect state/copy bounded data.
  virtual bool ready() const=0;
  virtual bool offer(const Reception&)=0;
};
struct RuntimeStats { uint32_t expired=0, observer_accepted=0, observer_dropped=0, link_accepted=0, link_dropped=0; };
class Runtime {
 public:
  Capture capture;
  RuntimeStats stats;
  void sinks(ReceptionSink* observer,ReceptionSink* link) { observer_=observer;link_=link; }
  void tick(uint32_t now) {
    Reception r{}; if(!capture.pop(r)) return;
    if(uint32_t(now-r.received_ms)>5000) {++stats.expired;return;}
    // Each decision is independent; neither sink owns or consumes the other copy.
    if(r.observer) {
      if(observer_ && observer_->ready() && observer_->offer(r)) ++stats.observer_accepted;
      else ++stats.observer_dropped;
    }
    if(r.internet_candidate) {
      if(link_ && link_->ready() && link_->offer(r)) ++stats.link_accepted;
      else ++stats.link_dropped;
    }
  }
 private:
  ReceptionSink* observer_=nullptr;
  ReceptionSink* link_=nullptr;
};
#ifdef PRIVATE_REMOTE_CAPTURE
// Call from the RF setup task before its first MyMesh::loop. Lifetime must exceed runtime.
// Returns false after the loop starts; workers must not mutate sink registration.
class NativePeerIngress;
bool bind_standalone_runtime(ReceptionSink* observer, ReceptionSink* link, bool internet_enabled=false, NativePeerIngress* inbound=nullptr);
#endif

}
