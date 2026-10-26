#include "helpers/bridges/IslandLinkIdentity.h"
#include "fake_nvs.h"

#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

FakeNvs fake_nvs;

static void check(bool okay, const char* name) {
  if (!okay) {
    std::fprintf(stderr, "FAIL: %s\n", name);
    std::exit(1);
  }
}

static std::string pinFor(const mbedtls_x509_crt& cert) {
  uint8_t der[256];
  const int length = mbedtls_pk_write_pubkey_der(
      const_cast<mbedtls_pk_context*>(&cert.pk), der, sizeof(der));
  check(length > 0, "fixture SPKI DER");
  uint8_t digest[32];
  check(mbedtls_sha256_ret(der + sizeof(der) - length, length, digest, 0) == 0,
        "fixture SPKI digest");
  static const char hex[] = "0123456789abcdef";
  char output[65];
  for (size_t i = 0; i < 32; ++i) {
    output[2 * i] = hex[digest[i] >> 4];
    output[2 * i + 1] = hex[digest[i] & 15];
  }
  output[64] = 0;
  return output;
}

static void testPinAndSan(const char* right_path, const char* wrong_path) {
  mbedtls_x509_crt right, wrong;
  mbedtls_x509_crt_init(&right);
  mbedtls_x509_crt_init(&wrong);
  check(mbedtls_x509_crt_parse_file(&right, right_path) == 0,
        "parse right SAN fixture");
  check(mbedtls_x509_crt_parse_file(&wrong, wrong_path) == 0,
        "parse wrong SAN fixture");
  const std::string right_pin = pinFor(right);
  const std::string wrong_pin = pinFor(wrong);
  check(island_link::isValidSpkiPin(right_pin.c_str()), "valid pin format");
  check(!island_link::isValidSpkiPin(nullptr) &&
            !island_link::isValidSpkiPin("1234") &&
            !island_link::isValidSpkiPin((right_pin + "0").c_str()),
        "absent, short, and long pins rejected");
  std::string nonhex = right_pin;
  nonhex[5] = 'z';
  check(!island_link::isValidSpkiPin(nonhex.c_str()), "nonhex pin rejected");
  check(island_link::verifyRelayCertificateSpki(right, right_pin.c_str()),
        "exact relay DNS SAN and SPKI pin accepted");
  check(!island_link::verifyRelayCertificateSpki(right, wrong_pin.c_str()),
        "wrong SPKI pin rejected");
  check(!island_link::verifyRelayCertificateSpki(wrong, wrong_pin.c_str()),
        "wrong DNS SAN rejected even with correct SPKI pin");
  mbedtls_x509_crt_free(&right);
  mbedtls_x509_crt_free(&wrong);
}

static void recalculateChecksum(std::vector<uint8_t>& blob) {
  check(blob.size() > 32, "stored blob length for test mutation");
  check(mbedtls_sha256_ret(blob.data(), blob.size() - 32,
                           blob.data() + blob.size() - 32, 0) == 0,
        "test mutation checksum");
}

static void testPersistence() {
  fake_nvs.reset();
  island_link::Identity deferred;
  check(island_link::loadOrCreateIdentity(deferred, false) ==
            island_link::IdentityStatus::WaitingForEntropy &&
            !fake_nvs.exists && fake_nvs.writes == 0,
        "missing identity waits for caller-approved entropy without writing");
  island_link::Identity first;
  check(island_link::loadOrCreateIdentity(first, true) ==
            island_link::IdentityStatus::Created,
        "missing identity creates and verifies stored record");
  check(fake_nvs.exists && fake_nvs.writes == 1 &&
            island_link::isValidSpkiPin(first.spki_sha256_hex),
        "new identity persisted with valid fingerprint");
  const std::string first_pin(first.spki_sha256_hex);
  const std::string first_cert(first.client_cert_pem);
  const std::vector<uint8_t> first_blob = fake_nvs.blob;

  island_link::Identity restarted;
  check(island_link::loadOrCreateIdentity(restarted, false) ==
            island_link::IdentityStatus::Loaded,
        "fake restart loads persisted identity");
  check(first_pin == restarted.spki_sha256_hex &&
            first_cert == restarted.client_cert_pem && fake_nvs.writes == 1,
        "restart preserves exact identity without rewrite");

  fake_nvs.blob = first_blob;
  fake_nvs.blob[20] ^= 1;
  island_link::Identity rejected;
  check(island_link::loadOrCreateIdentity(rejected, false) ==
            island_link::IdentityStatus::InvalidStoredIdentity &&
            fake_nvs.writes == 1 && fake_nvs.blob[20] != first_blob[20] &&
            rejected.spki_sha256_hex[0] == 0,
        "corrupt stored blob refused without replacement");

  fake_nvs.blob = first_blob;
  fake_nvs.wrong_type = true;
  check(island_link::loadOrCreateIdentity(rejected, false) ==
            island_link::IdentityStatus::InvalidStoredIdentity &&
            fake_nvs.writes == 1 && fake_nvs.blob == first_blob,
        "wrong NVS type refused without replacement");
  fake_nvs.wrong_type = false;

  fake_nvs.blob = first_blob;
  fake_nvs.blob.pop_back();
  check(island_link::loadOrCreateIdentity(rejected, false) ==
            island_link::IdentityStatus::InvalidStoredIdentity &&
            fake_nvs.writes == 1,
        "truncated NVS blob refused without replacement");

  fake_nvs.reset();
  island_link::Identity second;
  check(island_link::loadOrCreateIdentity(second, true) ==
            island_link::IdentityStatus::Created,
        "create second synthetic identity");
  const std::vector<uint8_t> second_blob = fake_nvs.blob;
  check(first_pin != second.spki_sha256_hex, "synthetic identities differ");

  // Keep the first private key, substitute the second certificate, and make
  // the checksum valid. This reaches the actual key-pair consistency check.
  std::vector<uint8_t> mismatch = first_blob;
  const size_t cert_offset = 8 + 1 + 2 + 2 + island_link::kPrivateKeyPemSize;
  check(mismatch.size() == cert_offset + island_link::kClientCertPemSize + 32,
        "stored blob layout for key-pair mutation");
  mismatch[11] = second_blob[11];
  mismatch[12] = second_blob[12];
  std::memcpy(mismatch.data() + cert_offset,
              second_blob.data() + cert_offset, island_link::kClientCertPemSize);
  recalculateChecksum(mismatch);
  fake_nvs.blob = mismatch;
  const unsigned writes_before = fake_nvs.writes;
  check(island_link::loadOrCreateIdentity(rejected, false) ==
            island_link::IdentityStatus::InvalidStoredIdentity &&
            fake_nvs.writes == writes_before && fake_nvs.blob == mismatch,
        "checksum-correct mismatched key and cert refused without replacement");
}

int main(int argc, char** argv) {
  check(argc == 3, "two certificate fixture paths provided");
  testPersistence();
  testPinAndSan(argv[1], argv[2]);
  std::puts("PASS: actual IslandLinkIdentity persistence, key pair, SAN and pin");
}
