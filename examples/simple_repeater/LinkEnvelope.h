#pragma once
#include "ObserverEnvelope.h"
#include <inttypes.h>
namespace private_remote {
// Compact JSON v1 compatibility encoder only. No unauthenticated transport.
// hash32 is full SHA-256(type [+ encoded path_len for TRACE] + payload).
// Native MeshCore dedup hash is only 8 bytes and MUST NOT be supplied here.
inline size_t link_frame(const uint8_t* raw,size_t length,const uint8_t hash32[32],
 const char* origin,const char* radio,uint64_t issued,uint64_t expires,uint8_t* out,size_t capacity) {
  PacketView v{};
  if(!inspect(raw,length,v) || v.width<2 || !safe_label(origin) || !safe_label(radio) ||
     !hash32 || !out || capacity<5 || expires<=issued || expires-issued>60000) return 0;
  char hex[max_raw*2+1],digest[65]; const char digits[]="0123456789abcdef";
  for(size_t i=0;i<length;++i){hex[i*2]=digits[raw[i]>>4];hex[i*2+1]=digits[raw[i]&15];} hex[length*2]=0;
  for(size_t i=0;i<32;++i){digest[i*2]=digits[hash32[i]>>4];digest[i*2+1]=digits[hash32[i]&15];} digest[64]=0;
  int n=snprintf(reinterpret_cast<char*>(out+4),capacity-4,
    "{\"expires_ms\":%" PRIu64 ",\"hops\":1,\"issued_ms\":%" PRIu64 ",\"origin\":\"%s\",\"packet_id\":\"%s\",\"radio\":\"%s\",\"raw\":\"%s\",\"version\":1,\"width\":%u}",
    expires,issued,origin,digest,radio,hex,unsigned(v.width));
  if(n<=0 || n>2048 || size_t(n)>=capacity-4) return 0;
  out[0]=(uint32_t(n)>>24)&255;out[1]=(uint32_t(n)>>16)&255;out[2]=(uint32_t(n)>>8)&255;out[3]=n&255;
  return n+4;
}
} // namespace private_remote
