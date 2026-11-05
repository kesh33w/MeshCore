#pragma once
#include "MeshCoreDigest.h"
#include "ObserverEnvelope.h"
#include <limits.h>

namespace private_remote {
// Strict flat v1 schema. No dynamic JSON allocations or recursive parser.
// Strings use the encoder's ASCII subset; escaped/unknown/duplicate fields fail closed.
struct InboundPacket {
  uint8_t raw[max_raw]{}; size_t length=0; uint8_t width=0;
  char origin[65]{}, radio[65]{}; uint8_t digest[32]{};
  uint64_t issued=0, expires=0;
};
class EnvelopeReader {
 const char* p_; const char* end_;
 void space(){while(p_<end_ && (*p_==' '||*p_=='\n'||*p_=='\r'||*p_=='\t'))++p_;}
 bool take(char c){space();if(p_==end_||*p_!=c)return false;++p_;return true;}
 bool string(char* out,size_t size){
  if(!take('"'))return false;
  size_t n=0;
  while(p_<end_ && *p_!='"') {
   const unsigned char c=*p_++;
   if(c<32||c>126||c=='\\'||n+1>=size)return false;
   out[n++]=char(c);
  }
  if(p_==end_)return false;
  ++p_;out[n]=0;return true;
 }
 bool number(uint64_t& out){
  space();if(p_==end_||*p_<'0'||*p_>'9')return false;
  out=0;const bool zero=*p_=='0';unsigned digits=0;
  while(p_<end_&&*p_>='0'&&*p_<='9'){
   const unsigned d=*p_++-'0';
   if((zero&&digits)||out>(UINT64_MAX-d)/10)return false;
   out=out*10+d;++digits;
  }
  return true;
 }
 static int nibble(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
 static bool hex(const char* text,uint8_t* out,size_t capacity,size_t& length){
  const size_t n=strlen(text);if(n%2||n/2>capacity)return false;
  length=n/2;for(size_t i=0;i<length;++i){const int a=nibble(text[i*2]),b=nibble(text[i*2+1]);
   if(a<0||b<0)return false;
   out[i]=uint8_t(a*16+b);
  }return true;
 }
 public:
 EnvelopeReader(const char* p,size_t n):p_(p),end_(p+n){}
 bool read(InboundPacket& result){
  InboundPacket e{}; unsigned seen=0; uint64_t version=0,hops=0,width=0;
  if(!take('{'))return false;
  for(unsigned item=0;item<9;++item){
   char key[16];if(item&&!take(','))return false;
   if(!string(key,sizeof(key))||!take(':'))return false;
   const char* names[]={"version","origin","radio","packet_id","issued_ms","expires_ms","hops","width","raw"};
   unsigned index=0;while(index<9&&strcmp(key,names[index]))++index;
   if(index==9||(seen&(1u<<index)))return false;
   seen|=1u<<index;
   bool ok=false;
   switch(index){
    case 0:ok=number(version);break;
    case 1:ok=string(e.origin,sizeof(e.origin))&&safe_label(e.origin);break;
    case 2:ok=string(e.radio,sizeof(e.radio))&&safe_label(e.radio);break;
    case 3:{char value[65];size_t length=0;ok=string(value,sizeof(value))&&hex(value,e.digest,32,length)&&length==32;break;}
    case 4:ok=number(e.issued);break;
    case 5:ok=number(e.expires);break;
    case 6:ok=number(hops);break;
    case 7:ok=number(width);break;
    case 8:{char value[max_raw*2+1];ok=string(value,sizeof(value))&&hex(value,e.raw,max_raw,e.length);break;}
   }
   if(!ok)return false;
  }
  if(!take('}'))return false;
  space();if(p_!=end_||seen!=511||version!=1||hops!=1||(width!=2&&width!=3))return false;
  PacketView view{};uint8_t digest[32];
  if(!inspect(e.raw,e.length,view)||view.width!=width||!link_digest(e.raw,e.length,digest)||memcmp(digest,e.digest,32))return false;
  e.width=uint8_t(width);result=e;return true;
 }
};

// One instance per authenticated session, worker thread only. Reset on disconnect.
// Caller closes the session on malformed/oversized frames; no resynchronization.
class FrameReader {
 uint8_t data_[2052]{}; size_t used_=0,expected_=0;uint32_t started_=0;
 public:
 enum class Result { pending, complete, rejected };
 void reset(){used_=expected_=0;started_=0;}
 bool timed_out(uint32_t now) const {return used_ && uint32_t(now-started_)>5000;}
 Result byte(uint8_t value,uint32_t now,InboundPacket& out){
  if(timed_out(now)){reset();return Result::rejected;}
  if(!used_)started_=now;
  if(used_>=sizeof(data_)){reset();return Result::rejected;}
  data_[used_++]=value;
  if(used_==4){
   const uint32_t n=(uint32_t(data_[0])<<24)|(uint32_t(data_[1])<<16)|(uint32_t(data_[2])<<8)|data_[3];
   if(!n||n>2048){reset();return Result::rejected;}expected_=n+4;
  }
  if(expected_&&used_==expected_){
   const bool ok=EnvelopeReader(reinterpret_cast<const char*>(data_+4),expected_-4).read(out);
   reset();return ok?Result::complete:Result::rejected;
  }
  return Result::pending;
 }
};

// Independent network replay ledger. Never use its result to suppress local RF.
// Retain across connection resets, not durable across device restart yet.
class IngressPolicy {
 struct Seen {uint8_t digest[32]{};uint64_t expires=0;};
 Seen seen_[32]{};
 public:
 bool admit(const InboundPacket& e,const char* authenticated_peer,const char* approved_radio,
            bool trusted_time,uint64_t now,uint32_t ttl=15000){
  if(!trusted_time||!safe_label(authenticated_peer)||!safe_label(approved_radio)||
     strcmp(e.origin,authenticated_peer)||strcmp(e.radio,approved_radio)||ttl<100||ttl>60000||
     e.expires<=e.issued||e.expires-e.issued>ttl||e.expires<=now||
     (e.issued>now&&e.issued-now>2000))return false;
  // RF eligibility still uses e.expires. Hold replay suppression for a full local TTL.
  if(now>UINT64_MAX-ttl)return false;
  const uint64_t local_end=now+ttl;
  return remember(e.digest,e.expires>local_end?e.expires:local_end,now);
 }
 bool remember(const uint8_t digest[32],uint64_t expires,uint64_t now){
  if(!digest||expires<=now)return false;
  Seen* free=nullptr;
  for(auto& s:seen_){if(s.expires<=now){if(!free)free=&s;}else if(!memcmp(s.digest,digest,32))return false;}
  if(!free)return false;
  memcpy(free->digest,digest,32);free->expires=expires;return true;
 }
};
} // namespace private_remote
