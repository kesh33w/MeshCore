#pragma once
#include "Esp32TlsTransport.h"
#include "PrivatePeerOutbox.h"
#include "NativePeerIngress.h"
#include "ReconnectBackoff.h"
#include "helpers/PrivateLinkState.h"
#include <sys/time.h>
#include <WiFi.h>
namespace private_remote {
struct ObserverPeerConfig {
 bool enabled=false;
 PrivatePeerConfig peer;
 SecureConfig tls;
 const char* approved_remote_radio=nullptr;
};
}
#ifdef PRIVATE_INSTALL_IDENTITY
#include "InstalledObserverConfig.h"
#elif __has_include("PrivateObserverGenerated.h")
#include "PrivateObserverGenerated.h"
#else
namespace private_remote {inline ObserverPeerConfig provisioned_observer_peer(){return {};}}
#endif
namespace private_remote {
class ObserverPrivateLink {
 ObserverPeerConfig config_=provisioned_observer_peer();
 PrivatePeerOutbox outbox_{config_.peer}; NativePeerIngress ingress_; Runtime runtime_;
 std::atomic<bool> running_{false}; unsigned generation_=0; TaskHandle_t rf_task_=nullptr;
 bool prepared_=false;
 ReconnectBackoff worker_start_backoff_;
 static bool clock(uint64_t& ms){
  timeval tv{};
  if(!time_ready() || gettimeofday(&tv,nullptr) || tv.tv_sec<1704067200)return false;
  ms=uint64_t(tv.tv_sec)*1000+tv.tv_usec/1000;return true;
 }
 bool allowed()const{return network_allowed().load() && network_generation().load()==generation_;}
 static void entry(void* ctx){
  auto* self=static_cast<ObserverPrivateLink*>(ctx);self->run();
  self->running_.store(false);vTaskDelete(nullptr);
 }
 void run(){
  Esp32TlsDriver driver(xTaskGetCurrentTaskHandle(),rf_task_);SecureSession stream(driver);
  FrameReader reader;ReconnectBackoff backoff;uint32_t last_report=0,up_since=0;bool was_up=false;
  while(allowed()){
   uint64_t epoch=0;uint32_t now=millis();bool ready=clock(epoch)&&WiFi.status()==WL_CONNECTED;
   if(!ready || !stream.connected()){
    clear_peer_authentication();
    if(was_up){stream.close();reader.reset();ingress_.disconnect();outbox_.tick(stream,now,0,false);was_up=false;backoff.failed(now);}
    if(!ready || !backoff.due(now)){vTaskDelay(pdMS_TO_TICKS(20));continue;}
    auto tls=config_.tls;tls.trusted_clock=true;tls.unix_seconds=epoch/1000;
    if(!stream.connect(tls)){backoff.failed(millis());Serial.println("Private link: TLS connection failed; retry scheduled");continue;}
    mark_peer_authenticated(generation_);
    Serial.println("Private link: TLS authenticated");was_up=true;up_since=millis();continue;
   }
   if(uint32_t(now-up_since)>=30000)backoff.healthy();
   if(uint32_t(now-last_report)>=30000){
    last_report=now;Serial.printf("Private link: ready=%u frames_written=%lu dropped=%lu heap=%lu\n",unsigned(outbox_.ready()),static_cast<unsigned long>(outbox_.frames_written),static_cast<unsigned long>(outbox_.dropped),static_cast<unsigned long>(ESP.getFreeHeap()));
   }
   bool bad=reader.timed_out(now);uint8_t bytes[256];size_t n=bad?0:stream.read(bytes,sizeof(bytes));
   for(size_t i=0;i<n && allowed();++i){
    InboundPacket packet{};auto result=reader.byte(bytes[i],now,packet);
    if(result==FrameReader::Result::rejected){bad=true;break;}
    if(result==FrameReader::Result::complete)ingress_.from_peer(packet,outbox_.replay_policy(),stream.authenticated_peer_id(),config_.approved_remote_radio,true,epoch,now,outbox_.configured_ttl());
   }
   if(bad){clear_peer_authentication();stream.close();continue;}
   if(allowed())outbox_.tick(stream,now,epoch,true);
   if(!allowed() || !stream.connected())clear_peer_authentication();
   vTaskDelay(pdMS_TO_TICKS(10));
  }
  clear_peer_authentication();
  stream.close();ingress_.disconnect();outbox_.tick(stream,millis(),0,false);
 }
 public:
 // Process-lifetime singleton: config and queues remain valid until the worker exits.
 // Only the RF task calls setup/capture/loop. MQTTBridge owns network state and time.
 void setup(){
  rf_task_=xTaskGetCurrentTaskHandle();auto tls=config_.tls;tls.trusted_clock=true;tls.unix_seconds=1704067200;
  prepared_=config_.enabled&&outbox_.configuration_valid()&&safe_label(config_.approved_remote_radio)&&secure_config_valid(tls)&&tls.purpose==TlsPurpose::private_peer&&!strcmp(tls.peer_id,outbox_.configured_peer());
  private_link_available().store(prepared_);clear_peer_authentication();
  if(config_.enabled&&!prepared_)Serial.println("Private link: invalid provisioning; disabled");
  runtime_.capture.config.observer_enabled=false;runtime_.sinks(nullptr,&outbox_);runtime_.capture.config.internet_enabled=prepared_;
 }
 void capture(float snr,float rssi,const uint8_t* raw,int len){
  if(prepared_&&allowed())runtime_.capture.hardware_received(raw,len,snr,rssi,millis());
 }
 template<class Mesh> void loop(Mesh& mesh){
  if(!prepared_)return;
  if(!allowed())runtime_.capture.discard();
  uint64_t epoch=0;
  if(!running_.load()&&network_allowed().load()&&WiFi.status()==WL_CONNECTED&&clock(epoch)&&worker_start_backoff_.due(millis())){
   generation_=network_generation().load();running_.store(true);
   if(xTaskCreate(entry,"private-peer",12288,this,1,nullptr)!=pdPASS){running_.store(false);Serial.println("Private link: task allocation failed");worker_start_backoff_.failed(millis());clear_peer_authentication();}
   else worker_start_backoff_.healthy();
  }
  if(allowed()){runtime_.tick(millis());ingress_.tick(mesh,millis());}
 }
};
inline ObserverPrivateLink& observer_private_link(){static ObserverPrivateLink link;return link;}
}
