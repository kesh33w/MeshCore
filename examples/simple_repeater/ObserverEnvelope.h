#pragma once
#include "PrivateRfCapture.h"
#include "ObserverAuth.h"
#include <stdio.h>
#include <cmath>
namespace private_remote {
// Actual RF observations; optional identity binds the public broker envelope.
inline bool safe_label(const char* s) {
  if (!s || !*s) return false;
  size_t n=0;
  for (;s[n];++n) if(n>=64 || !((s[n]>='a'&&s[n]<='z') || (s[n]>='A'&&s[n]<='Z') ||
      (s[n]>='0'&&s[n]<='9') || s[n]=='-' || s[n]=='_')) return false;
  return true;
}
inline size_t observer_json(const Reception& r, const char* origin, char* out, size_t capacity, const char* identity=nullptr) {
  if (!r.observer || r.length>max_raw || !safe_label(origin)) return 0;
  char hex[max_raw*2+1]; static const char digits[]="0123456789abcdef";
  for(size_t i=0;i<r.length;++i) {hex[2*i]=digits[r.raw[i]>>4];hex[2*i+1]=digits[r.raw[i]&15];}
  hex[r.length*2]=0;
  char snr[32]="null",rssi[32]="null";
  if(std::isfinite(r.snr)) snprintf(snr,sizeof(snr),"%.2f",double(r.snr));
  if(std::isfinite(r.rssi)) snprintf(rssi,sizeof(rssi),"%.2f",double(r.rssi));
  char id[96]="";
  if(identity){if(!observer_public_key(identity))return 0;snprintf(id,sizeof(id),"\"origin_id\":\"%s\",",identity);}
  const int n=snprintf(out,capacity,"{%s\"raw\":\"%s\",\"origin\":\"%s\",\"direction\":\"rx\",\"SNR\":%s,\"RSSI\":%s}",id,hex,origin,snr,rssi);
  return n>0 && size_t(n)<capacity ? size_t(n) : 0;
}
} // namespace private_remote
