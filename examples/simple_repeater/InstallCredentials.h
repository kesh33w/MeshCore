#pragma once
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include "InstallTrust.h"
namespace private_remote {
constexpr size_t install_max_bytes=12288;
static const char install_path[]="/private-link.json";
static const char install_pending[]="/private-link.pending";
inline bool install_document_valid(JsonDocument& doc) {
  if(doc["version"].as<int>()!=1) return false;
  for(const char* field:{"origin","certificate_pem","private_key_pem","admin_password"})
    if(!doc[field].is<const char*>()) return false;
  const char* origin=doc["origin"],*admin=doc["admin_password"];
  if(strncmp(origin,"device-",7)||strlen(origin)!=31) return false;
  for(unsigned i=7;i<31;++i) if(!((origin[i]>='0'&&origin[i]<='9')||(origin[i]>='a'&&origin[i]<='f')))return false;
  if(strlen(admin)<8||strlen(admin)>15||!strcmp(admin,"password"))return false;
  const char* cert=doc["certificate_pem"],*key=doc["private_key_pem"];
  if(strlen(cert)>4096||strlen(key)>2048)return false;
  mbedtls_x509_crt crt;mbedtls_pk_context pk;
  mbedtls_x509_crt_init(&crt);mbedtls_pk_init(&pk);
  bool ok=mbedtls_x509_crt_parse(&crt,(const unsigned char*)cert,strlen(cert)+1)==0 &&
    mbedtls_pk_parse_key(&pk,(const unsigned char*)key,strlen(key)+1,nullptr,0)==0 &&
    mbedtls_pk_check_pair(&crt.pk,&pk)==0;
  // Bind the label to the key, not merely to a caller-supplied certificate CN.
  unsigned char der[256],digest[32];
  int der_len=ok?mbedtls_pk_write_pubkey_der(&pk,der,sizeof(der)):-1;
  char expected[32]="device-";
  if(der_len<=0)ok=false;
  else {
    mbedtls_sha256_ret(der+sizeof(der)-der_len,der_len,digest,0);
    for(unsigned i=0;i<12;++i)snprintf(expected+7+2*i,3,"%02x",digest[i]);
    ok=ok&&!strcmp(expected,origin);
  }
  bool named=false;
  for(auto* dn=&crt.subject;dn;dn=dn->next)
    if(dn->oid.len==3&&!memcmp(dn->oid.p,"\x55\x04\x03",3)&&dn->val.len==strlen(origin)&&!memcmp(dn->val.p,origin,dn->val.len))named=true;
  mbedtls_pk_free(&pk);mbedtls_x509_crt_free(&crt);
  return ok&&named;
}
inline DynamicJsonDocument& installed_document(){static DynamicJsonDocument doc(install_max_bytes*2);return doc;}
inline bool installed_load(){
  static bool attempted=false,valid=false;
  if(attempted)return valid;
  attempted=true;
  if(!SPIFFS.exists(install_path))return false;
  File f=SPIFFS.open(install_path,"r");
  if(!f||f.isDirectory()||f.size()>install_max_bytes)return false;
  auto& doc=installed_document();
  valid=!deserializeJson(doc,f)&&install_document_valid(doc);
  f.close();if(!valid)doc.clear();return valid;
}
inline const char* installed_admin(){return installed_load()?installed_document()["admin_password"].as<const char*>():nullptr;}
// This handler is called ONLY by the physical USB serial command dispatcher.
// It is never registered with web configuration, RF or network CLI handlers.
inline bool install_usb_command(const char* command,char* reply){
  if(strncmp(command,"install ",8))return false;
  static File pending;static size_t written=0;
  const char* cmd=command+8;
  if(!strcmp(cmd,"status")){
    // SPIFFS may return a directory handle when opening a missing path.
    if(!SPIFFS.exists(install_path)){strcpy(reply,"INSTALL EMPTY");return true;}
    File f=SPIFFS.open(install_path,"r");
    if(!f||f.isDirectory()){strcpy(reply,"INSTALL ERROR");return true;}
    DynamicJsonDocument doc(install_max_bytes*2);
    bool valid=f.size()<=install_max_bytes&&!deserializeJson(doc,f)&&install_document_valid(doc);
    f.close();
    if(valid)snprintf(reply,160,"INSTALL READY %s",doc["origin"].as<const char*>());
    else strcpy(reply,"INSTALL ERROR");
    return true;
  }
  if(SPIFFS.exists(install_path)){strcpy(reply,"INSTALL LOCKED");return true;}
  if(!strcmp(cmd,"begin")){
    pending.close();SPIFFS.remove(install_pending);pending=SPIFFS.open(install_pending,"w");written=0;
    strcpy(reply,pending?"INSTALL OK 0":"INSTALL ERROR");return true;
  }
  if(!strncmp(cmd,"data ",5)){
    char* end=nullptr;unsigned long offset=strtoul(cmd+5,&end,10);
    if(!pending||end==cmd+5||*end!=' '||offset!=written){strcpy(reply,"INSTALL ERROR");return true;}
    const char* hex=end+1;size_t n=strlen(hex);uint8_t bytes[48];
    if(!n||n%2||n>96||written+n/2>install_max_bytes){strcpy(reply,"INSTALL ERROR");return true;}
    auto nibble=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1;};
    for(size_t i=0;i<n/2;++i){int a=nibble(hex[2*i]),b=nibble(hex[2*i+1]);if(a<0||b<0){strcpy(reply,"INSTALL ERROR");return true;}bytes[i]=uint8_t(a*16+b);}
    if(pending.write(bytes,n/2)!=n/2){pending.close();SPIFFS.remove(install_pending);strcpy(reply,"INSTALL ERROR");return true;}
    written+=n/2;snprintf(reply,160,"INSTALL OK %u",unsigned(written));return true;
  }
  if(!strncmp(cmd,"commit ",7)){
    if(!pending||strlen(cmd+7)!=64){strcpy(reply,"INSTALL ERROR");return true;}
    pending.close();File file=SPIFFS.open(install_pending,"r");
    mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);mbedtls_sha256_starts_ret(&hash,0);
    uint8_t block[256],digest[32];size_t n;
    while((n=file.read(block,sizeof(block))))mbedtls_sha256_update_ret(&hash,block,n);
    mbedtls_sha256_finish_ret(&hash,digest);mbedtls_sha256_free(&hash);file.close();
    char actual[65];for(unsigned i=0;i<32;++i)snprintf(actual+2*i,3,"%02x",digest[i]);
    bool ok=!strcmp(actual,cmd+7);DynamicJsonDocument doc(install_max_bytes*2);
    file=SPIFFS.open(install_pending,"r");ok=ok&&!deserializeJson(doc,file)&&install_document_valid(doc);file.close();doc.clear();
    ok=ok&&SPIFFS.rename(install_pending,install_path);
    if(!ok)SPIFFS.remove(install_pending);
    strcpy(reply,ok?"INSTALL SAVED":"INSTALL ERROR");return true;
  }
  strcpy(reply,"INSTALL ERROR");return true;
}
}
