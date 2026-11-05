#pragma once
// No transport is created here. Call only from a dedicated network worker.
#include "ObserverEnvelope.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace private_remote {
constexpr size_t tls_max_io = 2052;
enum class TlsPurpose { observer, private_peer };
struct SecureConfig {
  bool enabled = false;
  TlsPurpose purpose = TlsPurpose::private_peer;
  const char* host = nullptr;
  uint16_t port = 0;
  const char* peer_id = nullptr;
  const char* ca_pem = nullptr;
  const char* client_cert_pem = nullptr;
  const char* client_key_pem = nullptr;
  const char* server_sha256 = nullptr; // Private-peer leaf certificate pin, 64 hex characters.
  bool trusted_clock = false;
  uint64_t unix_seconds = 0;
};
// mbedTLS uses the OS clock, not the provider timestamp in SecureConfig.
// Keep them consistent without setting either clock or weakening certificate checks.
constexpr uint64_t tls_max_clock_skew_seconds = 5;
inline bool tls_clock_consistent(const SecureConfig& c, int64_t system_seconds,
                                 uint32_t elapsed_ms=0) {
  if(!c.trusted_clock || c.unix_seconds<1704067200ULL || system_seconds<1704067200LL) return false;
  const uint64_t elapsed_seconds=elapsed_ms/1000;
  if(c.unix_seconds>UINT64_MAX-elapsed_seconds) return false;
  const uint64_t expected=c.unix_seconds+elapsed_seconds;
  const uint64_t actual=static_cast<uint64_t>(system_seconds);
  const uint64_t difference=actual>expected?actual-expected:expected-actual;
  return difference<=tls_max_clock_skew_seconds;
}
inline bool bounded_text(const char* s, size_t max) {
  if (!s || !*s) return false;
  for (size_t i=0;i<=max;++i) if (!s[i]) return true;
  return false;
}
inline bool dns_name(const char* s) {
  if (!bounded_text(s,253)) return false;
  size_t label=0; bool numeric=true; char previous=0;
  for (size_t i=0;s[i];++i) {
    char c=s[i];
    if(c=='.') { if(!label || previous=='-') return false; label=0; }
    else {
      if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-')) return false;
      if(!label && c=='-') return false;
      if(++label>63) return false;
      if(!(c>='0'&&c<='9')) numeric=false;
    }
    previous=c;
  }
  return label && previous!='-' && !numeric;
}
inline int hex_digit(char c) {
  if(c>='0'&&c<='9') return c-'0';
  if(c>='a'&&c<='f') return c-'a'+10;
  if(c>='A'&&c<='F') return c-'A'+10;
  return -1;
}
inline bool digest_pin(const char* s, uint8_t out[32]) {
  if(!s) return false;
  for(size_t i=0;i<32;++i) {
    if(!s[2*i] || !s[2*i+1]) return false;
    int a=hex_digit(s[2*i]),b=hex_digit(s[2*i+1]);
    if(a<0 || b<0) return false;
    out[i]=uint8_t(a*16+b);
  }
  return s[64]==0;
}
inline bool secure_config_valid(const SecureConfig& c) {
  if(!c.enabled || !dns_name(c.host) || !c.port || !safe_label(c.peer_id) ||
     !bounded_text(c.ca_pem,16384) || !c.trusted_clock || c.unix_seconds<1704067200ULL) return false;
  const bool cert=bounded_text(c.client_cert_pem,16384), key=bounded_text(c.client_key_pem,16384);
  if(cert!=key || (c.client_cert_pem && !cert) || (c.client_key_pem && !key)) return false;
  uint8_t pin[32];
  if(c.server_sha256 && !digest_pin(c.server_sha256,pin)) return false;
  if(c.purpose==TlsPurpose::private_peer) return cert && key && digest_pin(c.server_sha256,pin);
  return c.purpose==TlsPurpose::observer;
}
// Driver must validate CA chain + DNS hostname and enforce worker ownership.
// There is deliberately no production mock, insecure driver, or default driver.
class TlsDriver {
 public:
  virtual ~TlsDriver()=default;
  virtual bool handshake(const SecureConfig&)=0;
  virtual bool fingerprint(uint8_t[32])=0;
  virtual bool connected()=0;
  virtual size_t write(const uint8_t*,size_t)=0;
  virtual size_t read(uint8_t*,size_t)=0;
  virtual void close()=0;
};
class SecureSession {
 public:
  explicit SecureSession(TlsDriver& driver): driver_(driver) {}
  bool connect(const SecureConfig& c) {
    close();
    if(!secure_config_valid(c) || !driver_.handshake(c)) { close(); return false; }
    if(c.server_sha256) {
      uint8_t expected[32],actual[32];
      if(!digest_pin(c.server_sha256,expected) || !driver_.fingerprint(actual)) {close();return false;}
      uint8_t mismatch=0;
      for(size_t i=0;i<32;++i) mismatch|=expected[i]^actual[i];
      if(mismatch) {close();return false;}
    }
    if(!driver_.connected()) { close();return false; }
    memcpy(peer_,c.peer_id,strlen(c.peer_id)+1); authenticated_=true; return true;
  }
  bool connected() {
    if(!authenticated_) return false;
    if(!driver_.connected()) {close();return false;}
    return true;
  }
  const char* authenticated_peer_id() { return connected()?peer_:nullptr; }
  size_t write(const uint8_t* data,size_t length) {
    if(!data || !length || length>tls_max_io || !connected()) return 0;
    const size_t n=driver_.write(data,length);
    if(n>length) {close();return 0;}
    // Partial write is uncertain. Caller must finish this frame or close, never replay it.
    return n;
  }
  size_t read(uint8_t* data,size_t length) {
    if(!data || !length || length>tls_max_io || !connected()) return 0;
    const size_t n=driver_.read(data,length);
    if(n>length) {close();return 0;}
    return n;
  }
  void close() { authenticated_=false; peer_[0]=0; driver_.close(); }
 private:
  TlsDriver& driver_;
  bool authenticated_=false;
  char peer_[65]{};
};
} // namespace private_remote
