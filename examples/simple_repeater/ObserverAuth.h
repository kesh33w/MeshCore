#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
namespace private_remote {
using ObserverSigner=bool (*)(void*,const uint8_t*,size_t,uint8_t*);
inline bool observer_public_key(const char* key){
 if(!key||strlen(key)!=64)return false;
 for(unsigned i=0;i<64;++i)if(!((key[i]>='0'&&key[i]<='9')||(key[i]>='A'&&key[i]<='F')))return false;
 return true;
}
inline size_t observer_base64url(const char* input,char* out,size_t cap){
 static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
 size_t n=0;uint32_t bits=0;unsigned count=0;
 for(const unsigned char* p=reinterpret_cast<const unsigned char*>(input);*p;++p){
  bits=(bits<<8)|*p;count+=8;while(count>=6){count-=6;if(n+1>=cap)return 0;out[n++]=alphabet[(bits>>count)&63];}
 }
 if(count){if(n+1>=cap)return 0;out[n++]=alphabet[(bits<<(6-count))&63];}
 if(n>=cap)return 0;
 out[n]=0;return n;
}
// meshcore-decoder 0.2.1 auth-token.ts: base64url JSON, ordinary Ed25519,
// hexadecimal signature. This client issues 7200-second tokens; this is not a live SACMesh broker limit.
inline size_t observer_token(const char* key,uint64_t now,ObserverSigner signer,void* context,char* out,size_t cap){
 if(!observer_public_key(key)||!signer||now<1704067200ULL||now>9007199254733791ULL)return 0;
 char payload[256],encoded[384],header[64];
 int n=snprintf(payload,sizeof(payload),"{\"publicKey\":\"%s\",\"iat\":%llu,\"exp\":%llu,\"aud\":\"mqtt.sacmesh.org\"}",key,
  static_cast<unsigned long long>(now),static_cast<unsigned long long>(now+7200));
 if(n<0||size_t(n)>=sizeof(payload)||!observer_base64url(payload,encoded,sizeof(encoded))||
  !observer_base64url("{\"alg\":\"Ed25519\",\"typ\":\"JWT\"}",header,sizeof(header)))return 0;
 n=snprintf(out,cap,"%s.%s",header,encoded);if(n<0||size_t(n)+130>cap)return 0;
 uint8_t signature[64];if(!signer(context,reinterpret_cast<const uint8_t*>(out),n,signature))return 0;
 size_t pos=n;out[pos++]='.';static const char hex[]="0123456789abcdef";
 for(uint8_t b:signature){out[pos++]=hex[b>>4];out[pos++]=hex[b&15];}out[pos]=0;return pos;
}
}
