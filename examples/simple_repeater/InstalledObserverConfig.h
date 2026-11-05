#pragma once
#include "InstallCredentials.h"
namespace private_remote {
inline ObserverPeerConfig provisioned_observer_peer(){
  ObserverPeerConfig c;
  if(!installed_load())return c;
  auto& doc=installed_document();
  c.enabled=true;c.peer.enabled=true;
  c.peer.origin=doc["origin"].as<const char*>();c.peer.peer_id=install_trust_peer_id;c.peer.radio="remote-rf";
  c.peer.ttl_ms=15000;c.approved_remote_radio="host-rf";
  c.tls.enabled=true;c.tls.purpose=TlsPurpose::private_peer;
  c.tls.host=install_trust_host;c.tls.port=install_trust_port;c.tls.peer_id=install_trust_peer_id;
  c.tls.ca_pem=install_trust_ca_pem;c.tls.server_sha256=install_trust_server_sha256;
  c.tls.client_cert_pem=doc["certificate_pem"].as<const char*>();
  c.tls.client_key_pem=doc["private_key_pem"].as<const char*>();return c;
}
}
