#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef ESP32_PLATFORM
#include <mbedtls/x509_crt.h>

namespace island_link {

constexpr const char* kRelayHostname = "ilink.sacmesh.org";
constexpr size_t kClientCertPemSize = 1536;
constexpr size_t kPrivateKeyPemSize = 512;

// Keep the secret in the worker task's memory. Do not copy, log, or expose it
// through configuration or CLI replies. The owner must outlive the TLS client.
struct Identity {
  char client_cert_pem[kClientCertPemSize] = {};
  char private_key_pem[kPrivateKeyPemSize] = {};
  char spki_sha256_hex[65] = {};

  Identity() = default;
  Identity(const Identity&) = delete;
  Identity& operator=(const Identity&) = delete;
  ~Identity();
  void clear();
};

enum class IdentityStatus {
  Loaded,
  Created,
  StorageError,
  InvalidStoredIdentity,
  CryptoError,
  WaitingForEntropy,
};

// Synchronous, potentially slow operation. Call from the IslandLink worker,
// never from a radio callback or loop. A malformed existing record is never
// overwritten with a new identity. Pass allow_generation=true only while the
// ESP32 Wi-Fi STA RF subsystem is enabled, so its hardware RNG receives fresh
// physical entropy before seeding the P-256 key generator. Existing records
// load regardless of allow_generation.
IdentityStatus loadOrCreateIdentity(Identity& out, bool allow_generation);

// Certificate display name only. Refreshes in memory using the existing key;
// never writes NVS or generates a replacement key. Call with Wi-Fi entropy ready.
// ASCII letters, digits, spaces, '.', '_' and '-' are retained, other bytes
// become '_'. The name is bounded to 32 bytes and empty names use Island Link.
bool refreshClientCertificate(Identity& identity, const char* advert_name);

// True only for 64 hexadecimal digits. An absent pin keeps the link disabled.
bool isValidSpkiPin(const char* pin);

// Check the exact DNS SAN and SHA-256 of the DER SubjectPublicKeyInfo.
// Use on the leaf certificate in the TLS verify callback. The callback must
// still reject other TLS verification errors, except the explicitly accepted
// self-signed/untrusted-root and clock-unavailable flags.
bool verifyRelayCertificateSpki(const mbedtls_x509_crt& leaf,
                                const char* expected_spki_sha256_hex);

}  // namespace island_link
#endif
