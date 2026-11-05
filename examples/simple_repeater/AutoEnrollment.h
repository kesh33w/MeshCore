#pragma once
#ifdef PRIVATE_INSTALL_IDENTITY
#include "InstallCredentials.h"
#include "Esp32TlsTransport.h"
#include <helpers/PrivateLinkState.h>
#include <WiFi.h>
#include <esp_system.h>
#include <mbedtls/x509_csr.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/oid.h>
#include <memory>

namespace private_remote {
static const char bootstrap_path[] = "/enrollment-key.json";
static const char bootstrap_pending[] = "/enrollment-key.pending";
inline int enrollment_random(void*, unsigned char* out, size_t n) { esp_fill_random(out,n); return 0; }
inline DynamicJsonDocument& enrollment_bootstrap() { static DynamicJsonDocument doc(4096); return doc; }
inline bool enrollment_prepare() {
  auto& doc=enrollment_bootstrap();
  if (SPIFFS.exists(bootstrap_path)) {
    File f=SPIFFS.open(bootstrap_path,"r");
    if (!f || f.isDirectory() || f.size()>4096 || deserializeJson(doc,f)) return false;
    f.close();
    if (!doc["private_key_pem"].is<const char*>() || !doc["admin_password"].is<const char*>() ||
        strlen(doc["admin_password"].as<const char*>())!=15) return false;
    const char* saved=doc["private_key_pem"]; mbedtls_pk_context saved_key; mbedtls_pk_init(&saved_key);
    bool valid=mbedtls_pk_parse_key(&saved_key,(const unsigned char*)saved,strlen(saved)+1,nullptr,0)==0 &&
      mbedtls_pk_can_do(&saved_key,MBEDTLS_PK_ECKEY) && mbedtls_pk_ec(saved_key)->grp.id==MBEDTLS_ECP_DP_SECP256R1;
    mbedtls_pk_free(&saved_key); return valid;
  }
  if (SPIFFS.exists(install_path)) return false;
  // Keep the driver initialized for the imminent setup AP or station startup.
  // Tearing it down here forces a second initialization during boot.
  const wifi_mode_t previous_mode=WiFi.getMode();
  const bool started_wifi=previous_mode==WIFI_MODE_NULL;
  if (started_wifi && !WiFi.mode(WIFI_MODE_STA)) return false;
  mbedtls_pk_context key; mbedtls_pk_init(&key);
  char pem[2048]={}; char admin[16]; unsigned char random[15];
  bool ok=mbedtls_pk_setup(&key,mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))==0 &&
    mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,mbedtls_pk_ec(key),enrollment_random,nullptr)==0 &&
    mbedtls_pk_write_key_pem(&key,(unsigned char*)pem,sizeof(pem))==0;
  mbedtls_pk_free(&key);
  if (!ok) return false;
  esp_fill_random(random,sizeof(random));
  static const char alphabet[]="ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  for (unsigned i=0;i<15;i++) admin[i]=alphabet[random[i]%(sizeof(alphabet)-1)]; admin[15]=0;
  doc["private_key_pem"]=pem; doc["admin_password"]=admin;
  memset(pem,0,sizeof(pem)); memset(admin,0,sizeof(admin));
  SPIFFS.remove(bootstrap_pending); File f=SPIFFS.open(bootstrap_pending,"w");
  if (!f || serializeJson(doc,f)!=measureJson(doc)) { f.close(); SPIFFS.remove(bootstrap_pending); doc.clear(); return false; }
  f.flush(); f.close();
  DynamicJsonDocument check(4096); f=SPIFFS.open(bootstrap_pending,"r");
  bool intact=f && !deserializeJson(check,f) && !strcmp(check["private_key_pem"]|"",doc["private_key_pem"]|"") && !strcmp(check["admin_password"]|"",doc["admin_password"]|""); f.close();
  if (!intact || !SPIFFS.rename(bootstrap_pending,bootstrap_path)) { SPIFFS.remove(bootstrap_pending); doc.clear(); return false; }
  return true;
}
inline const char* enrollment_admin() {
  return enrollment_bootstrap()["admin_password"].as<const char*>();
}
struct EnrollmentBootstrapJob { TaskHandle_t caller; bool success; };
inline void enrollment_bootstrap_worker(void* arg) {
  auto* job=static_cast<EnrollmentBootstrapJob*>(arg);
  job->success=enrollment_prepare();
  xTaskNotifyGive(job->caller);
  vTaskDelete(nullptr);
}
inline bool enrollment_prepare_at_boot() {
  if(SPIFFS.exists(install_path)) return true;
  EnrollmentBootstrapJob job{xTaskGetCurrentTaskHandle(),false};
  if(xTaskCreate(enrollment_bootstrap_worker,"enroll-key",24576,&job,1,nullptr)!=pdPASS) return false;
  ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
  return job.success;
}
inline bool enrollment_attempt(unsigned generation) {
  auto allowed=[generation](){ return network_allowed().load() && network_generation().load()==generation && WiFi.status()==WL_CONNECTED && time_ready(); };
  if(!allowed()) return false;
  if (!enrollment_prepare()) return false;
  auto& boot=enrollment_bootstrap(); const char* key_pem=boot["private_key_pem"];
  mbedtls_pk_context key; mbedtls_pk_init(&key);
  bool ok=mbedtls_pk_parse_key(&key,(const unsigned char*)key_pem,strlen(key_pem)+1,nullptr,0)==0;
  unsigned char der[256],digest[32]; int len=ok?mbedtls_pk_write_pubkey_der(&key,der,sizeof(der)):-1;
  char origin[32]="device-";
  if (len<=0) ok=false;
  else { mbedtls_sha256_ret(der+sizeof(der)-len,len,digest,0); for(unsigned i=0;i<12;i++) snprintf(origin+7+2*i,3,"%02x",digest[i]); }
  char subject[40]; snprintf(subject,sizeof(subject),"CN=%s",origin);
  char csr[2048]={}; mbedtls_x509write_csr req; mbedtls_x509write_csr_init(&req);
  if(ok) { mbedtls_x509write_csr_set_key(&req,&key); mbedtls_x509write_csr_set_md_alg(&req,MBEDTLS_MD_SHA256);
    ok=mbedtls_x509write_csr_set_subject_name(&req,subject)==0 &&
      mbedtls_x509write_csr_pem(&req,(unsigned char*)csr,sizeof(csr),enrollment_random,nullptr)==0; }
  mbedtls_x509write_csr_free(&req); mbedtls_pk_free(&key);
  if(!ok) return false;
  DynamicJsonDocument request(4096); request["version"]=1; request["origin"]=origin; request["csr_pem"]=csr;
  String body; serializeJson(request,body); request.clear();
  VerifiedTlsClient client; client.setCACert(install_trust_ca_pem); client.setHandshakeTimeout(5); client.setTimeout(5);
  static const char* alpn[]={"http/1.1",nullptr}; client.setAlpnProtocols(alpn);
  if(client.connect(install_trust_host,install_trust_port,5000)!=1 || !client.supported_version()) { client.stop(); return false; }
  uint8_t fingerprint[32]; char hex[65];
  if(!client.getFingerprintSHA256(fingerprint)) { client.stop(); return false; }
  for(unsigned i=0;i<32;i++) snprintf(hex+2*i,3,"%02x",fingerprint[i]);
  if(strcmp(hex,install_trust_server_sha256)) {client.stop();return false;}
  client.printf("POST /enroll HTTP/1.1\r\nHost: %s:%u\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",install_trust_host,install_trust_port,unsigned(body.length()));
  if(client.write((const uint8_t*)body.c_str(),body.length())!=body.length()) { client.stop();return false; }
  String status=client.readStringUntil('\n'); if(!status.startsWith("HTTP/1.1 200 ")) {client.stop();return false;}
  int content_length=-1; unsigned header_bytes=0;
  while(client.connected()) { String line=client.readStringUntil('\n'); header_bytes+=line.length(); if(header_bytes>4096){client.stop();return false;} line.trim(); if(!line.length())break;
    if(line.startsWith("Content-Length:")) content_length=line.substring(15).toInt(); }
  if(content_length<=0 || content_length>8192){client.stop();return false;}
  std::unique_ptr<char[]> response(new char[content_length+1]);
  size_t received=client.readBytes(response.get(),content_length); client.stop(); if(received!=size_t(content_length))return false; response[content_length]=0;
  DynamicJsonDocument doc(install_max_bytes*2);
  if(deserializeJson(doc,response.get()) || doc["version"].as<int>()!=1 || strcmp(doc["origin"]|"",origin) || !doc["certificate_pem"].is<const char*>()) return false;
  const char* cert=doc["certificate_pem"];
  mbedtls_x509_crt crt,ca; mbedtls_x509_crt_init(&crt);mbedtls_x509_crt_init(&ca); uint32_t flags=0;
  ok=mbedtls_x509_crt_parse(&crt,(const unsigned char*)cert,strlen(cert)+1)==0 &&
    mbedtls_x509_crt_parse(&ca,(const unsigned char*)install_trust_ca_pem,strlen(install_trust_ca_pem)+1)==0 &&
    mbedtls_x509_crt_verify(&crt,&ca,nullptr,nullptr,&flags,nullptr,nullptr)==0 &&
    mbedtls_x509_crt_check_extended_key_usage(&crt,MBEDTLS_OID_CLIENT_AUTH,MBEDTLS_OID_SIZE(MBEDTLS_OID_CLIENT_AUTH))==0;
  mbedtls_x509_crt_free(&crt);mbedtls_x509_crt_free(&ca);
  doc["private_key_pem"]=key_pem;doc["admin_password"]=boot["admin_password"];
  if(!ok || !install_document_valid(doc))return false;
  if(!allowed()) return false;
  SPIFFS.remove(install_pending); File f=SPIFFS.open(install_pending,"w");
  if(!f || serializeJson(doc,f)!=measureJson(doc)){f.close();SPIFFS.remove(install_pending);return false;} f.flush();f.close();
  DynamicJsonDocument check(install_max_bytes*2); f=SPIFFS.open(install_pending,"r");
  bool intact=f && !deserializeJson(check,f) && install_document_valid(check); f.close();
  if(!allowed() || !intact || !SPIFFS.rename(install_pending,install_path)){SPIFFS.remove(install_pending);return false;}
  return true;
}
inline void enrollment_worker(void*) {
  for(;;) {
    if(network_allowed().load() && time_ready() && WiFi.status()==WL_CONNECTED) {
      Serial.println("Enrollment: attempting");
      unsigned generation=network_generation().load();
      if(enrollment_attempt(generation)) { Serial.println("Enrollment: saved; restarting"); delay(1000); ESP.restart(); }
      Serial.println("Enrollment: retry scheduled");
    }
    vTaskDelay(pdMS_TO_TICKS(30000));
  }
}
inline void start_enrollment() {
  static bool started=false; static uint32_t last_attempt=0;
  if(started || SPIFFS.exists(install_path)) return;
  if(last_attempt && uint32_t(millis()-last_attempt)<30000) return;
  last_attempt=millis();
  if(xTaskCreate(enrollment_worker,"enrollment",16384,nullptr,1,nullptr)!=pdPASS) Serial.println("Enrollment: worker allocation failed; retry scheduled");
  else started=true;
}
}
#endif
