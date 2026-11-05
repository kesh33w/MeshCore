#pragma once
#include "SecureTransport.h"
#ifdef ARDUINO_ARCH_ESP32
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>
#ifndef PRIVATE_REMOTE_TLS12_MINIMUM
#error "Apply the pinned private TLS 1.2 minimum framework patch before building"
#endif
namespace private_remote {
class VerifiedTlsClient final : public WiFiClientSecure {
 public:
  bool supported_version() const {
    return strcmp(mbedtls_ssl_get_version(&sslclient->ssl_ctx),"TLSv1.2")==0;
  }
};
// Construct/use/destroy on the same dedicated worker, never the RF loop task.
// Caller must keep PEM pointers valid for the session lifetime. No credential storage.
class Esp32TlsDriver final : public TlsDriver {
 public:
  Esp32TlsDriver(TaskHandle_t worker,TaskHandle_t rf):worker_(worker),rf_(rf) {}
  bool handshake(const SecureConfig& c) override {
    if(!owned() || !secure_config_valid(c)) return false;
    client_.stop();
    // Certificate validity uses system time. Refuse a different provider epoch.
    const uint32_t clock_started=millis();
    if(!tls_clock_consistent(c,static_cast<int64_t>(time(nullptr)))) return false;
    client_.setCACert(c.ca_pem);
    client_.setCertificate(c.client_cert_pem);
    client_.setPrivateKey(c.client_key_pem);
    client_.setHandshakeTimeout(5); // API uses seconds.
    client_.setTimeout(2);         // API uses seconds, unlike many Arduino Streams.
    const bool ok=client_.connect(c.host,c.port,2000)==1 && client_.supported_version() &&
      tls_clock_consistent(c,static_cast<int64_t>(time(nullptr)),uint32_t(millis()-clock_started)); // TCP timeout milliseconds.
    if(!ok) client_.stop();
    return ok;
  }
  bool fingerprint(uint8_t out[32]) override {return owned() && out && client_.getFingerprintSHA256(out);}
  bool connected() override {return owned() && client_.connected();}
  size_t write(const uint8_t* p,size_t n) override {
    if(!owned() || !p || !n || n>tls_max_io || !client_.connected()) return 0;
    return client_.write(p,n);
  }
  size_t read(uint8_t* p,size_t n) override {
    if(!owned() || !p || !n || n>tls_max_io || !client_.connected()) return 0;
    int available=client_.available(); if(available<=0) return 0;
    if(n>size_t(available)) n=size_t(available);
    const int got=client_.read(p,n); return got>0?size_t(got):0;
  }
  void close() override {if(owned()) client_.stop();}
 private:
  bool owned() const {return worker_ && rf_ && worker_!=rf_ && xTaskGetCurrentTaskHandle()==worker_;}
  TaskHandle_t worker_,rf_;
  VerifiedTlsClient client_;
};
} // namespace private_remote
#endif
