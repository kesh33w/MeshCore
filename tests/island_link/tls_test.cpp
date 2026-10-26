#include "helpers/bridges/IslandLinkIdentity.h"
#include "helpers/bridges/IslandLinkTlsPolicy.h"
#include "fake_nvs.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>

FakeNvs fake_nvs;

static void check(bool okay, const char* message) {
  if (!okay) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

static std::string pinFor(mbedtls_pk_context& key) {
  uint8_t der[256];
  int length = mbedtls_pk_write_pubkey_der(&key, der, sizeof(der));
  check(length > 0, "SPKI encoding");
  uint8_t hash[32];
  check(mbedtls_sha256_ret(der + sizeof(der) - length, length, hash, 0) == 0,
        "SPKI hashing");
  static const char digits[] = "0123456789abcdef";
  char text[65];
  for (size_t i = 0; i < sizeof(hash); ++i) {
    text[2 * i] = digits[hash[i] >> 4];
    text[2 * i + 1] = digits[hash[i] & 15];
  }
  text[64] = 0;
  return text;
}

static int verifyClientPin(void* context, mbedtls_x509_crt* certificate,
                           int depth, uint32_t* flags) {
  if (!context || !certificate || !flags || depth != 0 ||
      pinFor(certificate->pk) != static_cast<const char*>(context))
    return MBEDTLS_ERR_X509_FATAL_ERROR;
  *flags &= ~(MBEDTLS_X509_BADCERT_NOT_TRUSTED |
              MBEDTLS_X509_BADCERT_EXPIRED |
              MBEDTLS_X509_BADCERT_FUTURE);
  return *flags == 0 ? 0 : MBEDTLS_ERR_X509_FATAL_ERROR;
}

struct Endpoint {
  std::deque<uint8_t> incoming;
  Endpoint* peer = nullptr;
};

static int writeMemory(void* context, const unsigned char* data, size_t length) {
  Endpoint* endpoint = static_cast<Endpoint*>(context);
  endpoint->peer->incoming.insert(endpoint->peer->incoming.end(), data, data + length);
  return static_cast<int>(length);
}

static int readMemory(void* context, unsigned char* data, size_t capacity) {
  Endpoint* endpoint = static_cast<Endpoint*>(context);
  if (endpoint->incoming.empty()) return MBEDTLS_ERR_SSL_WANT_READ;
  size_t length = std::min(capacity, endpoint->incoming.size());
  for (size_t i = 0; i < length; ++i) {
    data[i] = endpoint->incoming.front();
    endpoint->incoming.pop_front();
  }
  return static_cast<int>(length);
}

struct HandshakeResult {
  bool client_complete = false;
  bool server_complete = false;
  int client_error = 0;
  int server_error = 0;
  uint32_t client_flags = 0xffffffff;
  uint32_t server_flags = 0xffffffff;
  bool server_saw_client_cert = false;
};

// This models the relay application's mandatory post-handshake admission
// gate. VERIFY_OPTIONAL allows an absent client certificate through TLS, so
// callback verification alone cannot establish an enrolled device identity.
static bool relayAccepts(const HandshakeResult& result) {
  return result.server_complete && result.server_flags == 0 &&
         result.server_saw_client_cert;
}

static HandshakeResult handshake(const char* server_cert_path,
                                 const char* server_key_path,
                                 const island_link::Identity& client_identity,
                                 const std::string& relay_pin,
                                 const std::string& enrolled_client_pin,
                                 bool present_client_certificate = true) {
  mbedtls_ssl_context client, server;
  mbedtls_ssl_config client_config, server_config;
  mbedtls_ctr_drbg_context random;
  mbedtls_entropy_context entropy;
  mbedtls_x509_crt server_cert, client_cert;
  mbedtls_pk_context server_key, client_key;
  mbedtls_ssl_init(&client); mbedtls_ssl_init(&server);
  mbedtls_ssl_config_init(&client_config); mbedtls_ssl_config_init(&server_config);
  mbedtls_ctr_drbg_init(&random); mbedtls_entropy_init(&entropy);
  mbedtls_x509_crt_init(&server_cert); mbedtls_x509_crt_init(&client_cert);
  mbedtls_pk_init(&server_key); mbedtls_pk_init(&client_key);

  static const unsigned char seed[] = "IslandLink synthetic TLS test";
  check(mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
                              seed, sizeof(seed)) == 0, "TLS test RNG");
  check(mbedtls_x509_crt_parse_file(&server_cert, server_cert_path) == 0,
        "parse relay fixture");
  check(mbedtls_pk_parse_keyfile(&server_key, server_key_path, nullptr) == 0,
        "parse relay fixture key");
  check(mbedtls_x509_crt_parse(&client_cert,
          reinterpret_cast<const unsigned char*>(client_identity.client_cert_pem),
          std::strlen(client_identity.client_cert_pem) + 1) == 0,
        "parse generated client certificate");
  check(mbedtls_pk_parse_key(&client_key,
          reinterpret_cast<const unsigned char*>(client_identity.private_key_pem),
          std::strlen(client_identity.private_key_pem) + 1, nullptr, 0) == 0,
        "parse generated client key");
  check(mbedtls_ssl_config_defaults(&client_config, MBEDTLS_SSL_IS_CLIENT,
          MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0,
        "client defaults");
  check(mbedtls_ssl_config_defaults(&server_config, MBEDTLS_SSL_IS_SERVER,
          MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0,
        "server defaults");
  check(island_link::configurePinnedTls(client_config, relay_pin.c_str()),
        "production pinned policy configured");
  mbedtls_ssl_conf_authmode(&server_config, MBEDTLS_SSL_VERIFY_OPTIONAL);
  mbedtls_ssl_conf_verify(&server_config, verifyClientPin,
                           const_cast<char*>(enrolled_client_pin.c_str()));
  mbedtls_ssl_conf_rng(&client_config, mbedtls_ctr_drbg_random, &random);
  mbedtls_ssl_conf_rng(&server_config, mbedtls_ctr_drbg_random, &random);
  if (present_client_certificate) {
    check(mbedtls_ssl_conf_own_cert(&client_config, &client_cert, &client_key) == 0,
          "client presents production-generated identity");
  }
  check(mbedtls_ssl_conf_own_cert(&server_config, &server_cert, &server_key) == 0,
        "server presents fixture identity");
  check(mbedtls_ssl_setup(&client, &client_config) == 0 &&
        mbedtls_ssl_setup(&server, &server_config) == 0,
        "TLS contexts setup");
  check(mbedtls_ssl_set_hostname(&client, island_link::kRelayHostname) == 0,
        "TLS hostname set");

  Endpoint a, b;
  a.peer = &b; b.peer = &a;
  mbedtls_ssl_set_bio(&client, &a, writeMemory, readMemory, nullptr);
  mbedtls_ssl_set_bio(&server, &b, writeMemory, readMemory, nullptr);
  HandshakeResult result;
  for (int step = 0; step < 10000; ++step) {
    if (!result.client_complete && result.client_error == 0) {
      int status = mbedtls_ssl_handshake(&client);
      if (status == 0) result.client_complete = true;
      else if (status != MBEDTLS_ERR_SSL_WANT_READ && status != MBEDTLS_ERR_SSL_WANT_WRITE)
        result.client_error = status;
    }
    if (!result.server_complete && result.server_error == 0) {
      int status = mbedtls_ssl_handshake(&server);
      if (status == 0) result.server_complete = true;
      else if (status != MBEDTLS_ERR_SSL_WANT_READ && status != MBEDTLS_ERR_SSL_WANT_WRITE)
        result.server_error = status;
    }
    if ((result.client_complete && result.server_complete) ||
        result.client_error || result.server_error) break;
  }
  result.client_flags = mbedtls_ssl_get_verify_result(&client);
  result.server_flags = mbedtls_ssl_get_verify_result(&server);
  result.server_saw_client_cert = mbedtls_ssl_get_peer_cert(&server) != nullptr;

  mbedtls_ssl_free(&client); mbedtls_ssl_free(&server);
  mbedtls_ssl_config_free(&client_config); mbedtls_ssl_config_free(&server_config);
  mbedtls_x509_crt_free(&client_cert); mbedtls_x509_crt_free(&server_cert);
  mbedtls_pk_free(&client_key); mbedtls_pk_free(&server_key);
  mbedtls_ctr_drbg_free(&random); mbedtls_entropy_free(&entropy);
  return result;
}

int main(int argc, char** argv) {
  check(argc == 5, "right and wrong relay certificate/key fixtures supplied");
  fake_nvs.reset();
  island_link::Identity authorized;
  check(island_link::loadOrCreateIdentity(authorized, true) ==
        island_link::IdentityStatus::Created, "authorized device identity");
  mbedtls_x509_crt relay;
  mbedtls_x509_crt_init(&relay);
  check(mbedtls_x509_crt_parse_file(&relay, argv[1]) == 0,
        "right relay certificate");
  std::string relay_pin = pinFor(relay.pk);
  mbedtls_x509_crt_free(&relay);
  mbedtls_x509_crt_init(&relay);
  check(mbedtls_x509_crt_parse_file(&relay, argv[3]) == 0,
        "wrong SAN relay certificate");
  std::string wrong_san_pin = pinFor(relay.pk);
  mbedtls_x509_crt_free(&relay);

  mbedtls_ssl_config unconfigured;
  mbedtls_ssl_config_init(&unconfigured);
  check(!island_link::configurePinnedTls(unconfigured, nullptr) &&
        !island_link::configurePinnedTls(unconfigured, ""),
        "missing pin cannot configure TLS");
  mbedtls_ssl_config_free(&unconfigured);

  HandshakeResult accepted = handshake(argv[1], argv[2], authorized,
      relay_pin, authorized.spki_sha256_hex);
  check(accepted.client_complete && accepted.server_complete &&
        accepted.client_flags == 0 && accepted.server_flags == 0 &&
        relayAccepts(accepted),
        "pinned self-signed TLS and authorized client certificate succeed");

  std::string wrong_pin(64, '0');
  if (wrong_pin == relay_pin) wrong_pin[0] = '1';
  HandshakeResult rejected_pin = handshake(argv[1], argv[2], authorized,
      wrong_pin, authorized.spki_sha256_hex);
  check(!rejected_pin.client_complete && rejected_pin.client_error != 0,
        "wrong relay pin aborts handshake");
  HandshakeResult rejected_san = handshake(argv[3], argv[4], authorized,
      wrong_san_pin, authorized.spki_sha256_hex);
  check(!rejected_san.client_complete && rejected_san.client_error != 0,
        "wrong relay SAN aborts handshake");

  std::string not_enrolled(64, '0');
  if (not_enrolled == authorized.spki_sha256_hex) not_enrolled[0] = '1';
  HandshakeResult rejected_client = handshake(argv[1], argv[2], authorized,
      relay_pin, not_enrolled);
  check(!rejected_client.server_complete && rejected_client.server_error != 0,
        "unapproved client fingerprint aborts server handshake");
  HandshakeResult missing_client = handshake(argv[1], argv[2], authorized,
      relay_pin, authorized.spki_sha256_hex, false);
  check(!relayAccepts(missing_client),
        "missing client certificate fails required relay peer-cert gate");
  std::puts("PASS: pinned self-signed in-memory TLS handshake and rejection paths");
}
