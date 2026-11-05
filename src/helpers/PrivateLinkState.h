#pragma once
#include <atomic>
namespace private_remote {
inline std::atomic<bool>& fresh_time(){static std::atomic<bool> value{false};return value;}
inline std::atomic<bool>& network_allowed(){static std::atomic<bool> value{false};return value;}
inline std::atomic<unsigned>& network_generation(){static std::atomic<unsigned> value{0};return value;}
inline std::atomic<bool>& tls_allocator_ready(){static std::atomic<bool> value{false};return value;}
inline std::atomic<bool>& private_link_available(){static std::atomic<bool> value{false};return value;}
inline std::atomic<bool>& peer_authenticated(){static std::atomic<bool> value{false};return value;}
inline std::atomic<unsigned>& peer_auth_generation(){static std::atomic<unsigned> value{0};return value;}
inline void clear_peer_authentication(){peer_authenticated().store(false);}
inline void mark_peer_authenticated(unsigned generation){
 peer_auth_generation().store(generation);peer_authenticated().store(true);
}
inline bool time_ready(){return fresh_time().load()&&tls_allocator_ready().load();}
inline void accept_time(bool successful_probe,int clock_result){if(successful_probe&&clock_result==0)fresh_time().store(true);}
inline void stop_network(){network_allowed().store(false);network_generation().fetch_add(1);clear_peer_authentication();}
// These labels describe observed state, not a delivery or recipient-ACK claim.
inline const char* private_link_status(bool wifi_connected){
 if(!private_link_available().load())return "Link: unavailable";
 if(!network_allowed().load())return "Link: stopped";
 if(!wifi_connected)return "Link: waiting WiFi";
 if(!time_ready())return "Link: syncing time";
 if(peer_authenticated().load() && peer_auth_generation().load()==network_generation().load())
  return "Link: authenticated";
 return "Link: connecting";
}
}
