#pragma once
#include <stdint.h>
namespace private_remote {
// Bounded reconnect delay. Unsigned monotonic time supports millis() rollover.
class ReconnectBackoff {
 public:
  bool due(uint32_t now) const {return !waiting_ || int32_t(now-next_)>=0;}
  void failed(uint32_t now) {next_=now+delay_;waiting_=true;delay_=delay_>=30000?60000:delay_*2;}
  void healthy() {delay_=1000;waiting_=false;}
 private:
  uint32_t next_=0,delay_=1000;bool waiting_=false;
};
}
