#include "IslandLinkIdentity.h"

#ifdef ESP32_PLATFORM
#include <Preferences.h>
#include <esp_err.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/md.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include <mbedtls/x509.h>
#include <mbedtls/x509_crt.h>
#include <nvs.h>
#include <string.h>
#include <stdio.h>

namespace island_link {
namespace {

constexpr char kNamespace[] = "ilink";
constexpr char kIdentityKey[] = "identity";
constexpr uint8_t kMagic[8] = {'I', 'L', 'I', 'D', 'E', 'N', 'T', '1'};
constexpr uint8_t kFormatVersion = 1;

// Byte arrays avoid padding and preserve the blob layout across compilers.
struct StoredIdentity {
  uint8_t magic[8];
  uint8_t version;
  uint8_t key_len_le[2];
  uint8_t cert_len_le[2];
  char private_key_pem[kPrivateKeyPemSize];
  char client_cert_pem[kClientCertPemSize];
  uint8_t checksum[32];
};

static_assert(sizeof(StoredIdentity) == 8 + 1 + 2 + 2 +
                                      kPrivateKeyPemSize + kClientCertPemSize + 32,
              "Identity NVS layout must be packed");

void encode16(uint8_t dst[2], size_t value) {
  dst[0] = static_cast<uint8_t>(value);
  dst[1] = static_cast<uint8_t>(value >> 8);
}

size_t decode16(const uint8_t src[2]) {
  return static_cast<size_t>(src[0]) | (static_cast<size_t>(src[1]) << 8);
}

bool sha256(const uint8_t* data, size_t length, uint8_t output[32]) {
  return mbedtls_sha256_ret(data, length, output, 0) == 0;
}

bool equalConstantTime(const uint8_t* a, const uint8_t* b, size_t length) {
  uint8_t diff = 0;
  for (size_t i = 0; i < length; ++i) diff |= a[i] ^ b[i];
  return diff == 0;
}

uint8_t hexValue(char c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
  return 0xff;
}

bool decodePin(const char* text, uint8_t bytes[32]) {
  if (!text) return false;
  for (size_t i = 0; i < 32; ++i) {
    if (!text[2 * i] || !text[2 * i + 1]) return false;
    uint8_t high = hexValue(text[2 * i]);
    uint8_t low = hexValue(text[2 * i + 1]);
    if (high > 15 || low > 15) return false;
    bytes[i] = static_cast<uint8_t>((high << 4) | low);
  }
  return text[64] == '\0';
}

void hexEncode(const uint8_t bytes[32], char output[65]) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < 32; ++i) {
    output[2 * i] = digits[bytes[i] >> 4];
    output[2 * i + 1] = digits[bytes[i] & 15];
  }
  output[64] = '\0';
}

bool fingerprint(const mbedtls_pk_context& key, uint8_t output[32]) {
  uint8_t der[256];
  int size = mbedtls_pk_write_pubkey_der(const_cast<mbedtls_pk_context*>(&key),
                                        der, sizeof(der));
  bool okay = size > 0 && sha256(der + sizeof(der) - size, size, output);
  mbedtls_platform_zeroize(der, sizeof(der));
  return okay;
}

bool parsePrivateKey(mbedtls_pk_context& key, const char* pem, size_t length) {
#if MBEDTLS_VERSION_MAJOR >= 3
  return mbedtls_pk_parse_key(&key, reinterpret_cast<const uint8_t*>(pem),
                              length, nullptr, 0, nullptr, nullptr) == 0;
#else
  return mbedtls_pk_parse_key(&key, reinterpret_cast<const uint8_t*>(pem),
                              length, nullptr, 0) == 0;
#endif
}

bool checkKeyPair(const mbedtls_pk_context& pub, const mbedtls_pk_context& priv) {
#if MBEDTLS_VERSION_MAJOR >= 3
  return mbedtls_pk_check_pair(&pub, &priv, nullptr, nullptr) == 0;
#else
  return mbedtls_pk_check_pair(&pub, &priv) == 0;
#endif
}

bool validateStored(const StoredIdentity& blob, Identity& out) {
  if (memcmp(blob.magic, kMagic, sizeof(kMagic)) != 0 ||
      blob.version != kFormatVersion) return false;
  const size_t key_length = decode16(blob.key_len_le);
  const size_t cert_length = decode16(blob.cert_len_le);
  if (key_length < 2 || key_length > sizeof(blob.private_key_pem) ||
      cert_length < 2 || cert_length > sizeof(blob.client_cert_pem) ||
      blob.private_key_pem[key_length - 1] != '\0' ||
      blob.client_cert_pem[cert_length - 1] != '\0' ||
      strnlen(blob.private_key_pem, key_length) != key_length - 1 ||
      strnlen(blob.client_cert_pem, cert_length) != cert_length - 1) return false;

  uint8_t digest[32];
  if (!sha256(reinterpret_cast<const uint8_t*>(&blob),
              offsetof(StoredIdentity, checksum), digest) ||
      !equalConstantTime(digest, blob.checksum, sizeof(digest))) return false;

  mbedtls_pk_context key;
  mbedtls_x509_crt cert;
  mbedtls_pk_init(&key);
  mbedtls_x509_crt_init(&cert);
  bool valid = parsePrivateKey(key, blob.private_key_pem, key_length) &&
      mbedtls_x509_crt_parse(&cert,
          reinterpret_cast<const uint8_t*>(blob.client_cert_pem), cert_length) == 0 &&
      mbedtls_pk_get_type(&key) == MBEDTLS_PK_ECKEY &&
      mbedtls_pk_get_type(&cert.pk) == MBEDTLS_PK_ECKEY &&
      mbedtls_pk_ec(key)->grp.id == MBEDTLS_ECP_DP_SECP256R1 &&
      checkKeyPair(cert.pk, key) && fingerprint(cert.pk, digest);
  if (valid) {
    memcpy(out.private_key_pem, blob.private_key_pem, key_length);
    memcpy(out.client_cert_pem, blob.client_cert_pem, cert_length);
    hexEncode(digest, out.spki_sha256_hex);
  }
  mbedtls_pk_free(&key);
  mbedtls_x509_crt_free(&cert);
  mbedtls_platform_zeroize(digest, sizeof(digest));
  return valid;
}

bool createIdentity(Identity& out, const char* subject = "CN=IslandLink device",
                    bool reuse_key = false) {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context rng;
  mbedtls_pk_context key;
  mbedtls_x509write_cert cert;
  mbedtls_mpi serial;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&rng);
  mbedtls_pk_init(&key);
  mbedtls_x509write_crt_init(&cert);
  mbedtls_mpi_init(&serial);

  static const uint8_t personalisation[] = "IslandLink device identity v1";
  bool okay = mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy,
                                    personalisation, sizeof(personalisation) - 1) == 0;
  if (reuse_key) {
    if (okay) okay = parsePrivateKey(key, out.private_key_pem,
                                     strlen(out.private_key_pem) + 1);
  } else {
    if (okay) okay = mbedtls_pk_setup(&key,
                     mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) == 0;
    if (okay) okay = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,
                     mbedtls_pk_ec(key), mbedtls_ctr_drbg_random, &rng) == 0;
    if (okay) okay = mbedtls_pk_write_key_pem(&key,
                     reinterpret_cast<uint8_t*>(out.private_key_pem),
                     sizeof(out.private_key_pem)) == 0;
  }

  uint8_t serial_bytes[16];
  if (okay) okay = mbedtls_ctr_drbg_random(&rng, serial_bytes,
                                            sizeof(serial_bytes)) == 0;
  serial_bytes[0] &= 0x7f;
  serial_bytes[sizeof(serial_bytes) - 1] |= 1;
  if (okay) okay = mbedtls_mpi_read_binary(&serial, serial_bytes,
                                           sizeof(serial_bytes)) == 0;
  mbedtls_platform_zeroize(serial_bytes, sizeof(serial_bytes));

  if (okay) {
    mbedtls_x509write_crt_set_subject_key(&cert, &key);
    mbedtls_x509write_crt_set_issuer_key(&cert, &key);
    mbedtls_x509write_crt_set_md_alg(&cert, MBEDTLS_MD_SHA256);
    okay = mbedtls_x509write_crt_set_subject_name(&cert,
                  subject) == 0 &&
           mbedtls_x509write_crt_set_issuer_name(&cert,
                  subject) == 0 &&
           mbedtls_x509write_crt_set_serial(&cert, &serial) == 0 &&
           mbedtls_x509write_crt_set_validity(&cert,
                  "20200101000000", "20991231235959") == 0 &&
           mbedtls_x509write_crt_set_basic_constraints(&cert, 0, -1) == 0 &&
           mbedtls_x509write_crt_set_key_usage(&cert,
                  MBEDTLS_X509_KU_DIGITAL_SIGNATURE) == 0;
    // DER SEQUENCE containing id-kp-clientAuth. This mbedTLS 2.x release
    // exposes only the generic certificate extension writer for EKU.
    static const uint8_t client_auth_eku[] = {
        0x30, 0x0a, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x05,
        0x05, 0x07, 0x03, 0x02};
    if (okay) okay = mbedtls_x509write_crt_set_extension(
        &cert, MBEDTLS_OID_EXTENDED_KEY_USAGE,
        sizeof(MBEDTLS_OID_EXTENDED_KEY_USAGE) - 1, 0,
        client_auth_eku, sizeof(client_auth_eku)) == 0;
  }
  if (okay) okay = mbedtls_x509write_crt_pem(&cert,
                   reinterpret_cast<uint8_t*>(out.client_cert_pem),
                   sizeof(out.client_cert_pem), mbedtls_ctr_drbg_random, &rng) == 0;
  uint8_t digest[32];
  if (okay) okay = fingerprint(key, digest);
  if (okay) hexEncode(digest, out.spki_sha256_hex);
  mbedtls_platform_zeroize(digest, sizeof(digest));

  mbedtls_mpi_free(&serial);
  mbedtls_x509write_crt_free(&cert);
  mbedtls_pk_free(&key);
  mbedtls_ctr_drbg_free(&rng);
  mbedtls_entropy_free(&entropy);
  if (!okay) out.clear();
  return okay;
}

// Read with NVS directly so a missing record is distinct from a wrong type or
// an I/O error. Preferences performs the single-blob write and commit.
esp_err_t readStored(StoredIdentity& blob) {
  nvs_handle_t handle;
  esp_err_t result = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (result != ESP_OK) return result;
  size_t length = sizeof(blob);
  result = nvs_get_blob(handle, kIdentityKey, &blob, &length);
  nvs_close(handle);
  if (result == ESP_OK && length != sizeof(blob)) return ESP_ERR_NVS_INVALID_LENGTH;
  return result;
}

bool hasExactRelayDnsSan(const mbedtls_x509_crt& leaf) {
  constexpr size_t hostname_length = sizeof("ilink.sacmesh.org") - 1;
  for (const mbedtls_x509_sequence* name = &leaf.subject_alt_names;
       name != nullptr && name->buf.p != nullptr; name = name->next) {
    mbedtls_x509_subject_alternative_name parsed = {};
    if (mbedtls_x509_parse_subject_alt_name(&name->buf, &parsed) == 0 &&
        parsed.type == MBEDTLS_X509_SAN_DNS_NAME &&
        parsed.san.unstructured_name.len == hostname_length &&
        memcmp(parsed.san.unstructured_name.p, kRelayHostname,
               hostname_length) == 0) return true;
  }
  return false;
}

}  // namespace

Identity::~Identity() { clear(); }

void Identity::clear() {
  mbedtls_platform_zeroize(client_cert_pem, sizeof(client_cert_pem));
  mbedtls_platform_zeroize(private_key_pem, sizeof(private_key_pem));
  mbedtls_platform_zeroize(spki_sha256_hex, sizeof(spki_sha256_hex));
}

IdentityStatus loadOrCreateIdentity(Identity& out, bool allow_generation) {
  out.clear();
  StoredIdentity blob = {};
  esp_err_t read_result = readStored(blob);
  if (read_result == ESP_OK) {
    bool valid = validateStored(blob, out);
    mbedtls_platform_zeroize(&blob, sizeof(blob));
    return valid ? IdentityStatus::Loaded : IdentityStatus::InvalidStoredIdentity;
  }
  if (read_result != ESP_ERR_NVS_NOT_FOUND) {
    mbedtls_platform_zeroize(&blob, sizeof(blob));
    return read_result == ESP_ERR_NVS_INVALID_LENGTH ||
                   read_result == ESP_ERR_NVS_TYPE_MISMATCH
               ? IdentityStatus::InvalidStoredIdentity
               : IdentityStatus::StorageError;
  }

  // The bootloader's RNG seed is not a continuous entropy source once the app
  // starts. The caller enables Wi-Fi STA RF before authorizing first key creation.
  if (!allow_generation) return IdentityStatus::WaitingForEntropy;
  if (!createIdentity(out)) return IdentityStatus::CryptoError;
  memcpy(blob.magic, kMagic, sizeof(kMagic));
  blob.version = kFormatVersion;
  size_t key_length = strlen(out.private_key_pem) + 1;
  size_t cert_length = strlen(out.client_cert_pem) + 1;
  encode16(blob.key_len_le, key_length);
  encode16(blob.cert_len_le, cert_length);
  memcpy(blob.private_key_pem, out.private_key_pem, key_length);
  memcpy(blob.client_cert_pem, out.client_cert_pem, cert_length);
  if (!sha256(reinterpret_cast<const uint8_t*>(&blob),
              offsetof(StoredIdentity, checksum), blob.checksum)) {
    out.clear();
    mbedtls_platform_zeroize(&blob, sizeof(blob));
    return IdentityStatus::CryptoError;
  }

  Preferences prefs;
  bool opened = prefs.begin(kNamespace, false);
  bool stored = opened && prefs.putBytes(kIdentityKey, &blob, sizeof(blob)) == sizeof(blob);
  if (opened) prefs.end();
  mbedtls_platform_zeroize(&blob, sizeof(blob));
  if (!stored) {
    out.clear();
    return IdentityStatus::StorageError;
  }

  // Do not use an identity until the committed record can be read and parsed.
  StoredIdentity verified = {};
  read_result = readStored(verified);
  out.clear();
  bool valid = read_result == ESP_OK && validateStored(verified, out);
  mbedtls_platform_zeroize(&verified, sizeof(verified));
  if (!valid) {
    out.clear();
    return IdentityStatus::StorageError;
  }
  return IdentityStatus::Created;
}

bool refreshClientCertificate(Identity& identity, const char* advert_name) {
  char name[33] = {};
  size_t length = 0;
  if (advert_name) {
    while (*advert_name == ' ') ++advert_name;
    while (*advert_name && length < sizeof(name) - 1) {
      const unsigned char c = static_cast<unsigned char>(*advert_name++);
      name[length++] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == ' ' || c == '.' ||
                       c == '_' || c == '-' ? c : '_';
    }
  }
  while (length && name[length - 1] == ' ') name[--length] = 0;
  if (!length) memcpy(name, "Island Link", sizeof("Island Link"));

  mbedtls_x509_crt current;
  mbedtls_x509_crt_init(&current);
  bool matches = false;
  if (mbedtls_x509_crt_parse(&current,
          reinterpret_cast<const uint8_t*>(identity.client_cert_pem),
          strlen(identity.client_cert_pem) + 1) == 0) {
    const mbedtls_x509_name& cn = current.subject;
    matches = MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &cn.oid) == 0 &&
              !cn.next && cn.val.len == strlen(name) &&
              memcmp(cn.val.p, name, cn.val.len) == 0;
  }
  mbedtls_x509_crt_free(&current);
  if (matches) return true;
  char subject[36];
  snprintf(subject, sizeof(subject), "CN=%s", name);
  return createIdentity(identity, subject, true);
}

bool isValidSpkiPin(const char* pin) {
  uint8_t decoded[32];
  bool valid = decodePin(pin, decoded);
  mbedtls_platform_zeroize(decoded, sizeof(decoded));
  return valid;
}

bool verifyRelayCertificateSpki(const mbedtls_x509_crt& leaf,
                                const char* expected_spki_sha256_hex) {
  uint8_t expected[32];
  if (!decodePin(expected_spki_sha256_hex, expected)) return false;
  uint8_t actual[32];
  bool matches = hasExactRelayDnsSan(leaf) &&
                 fingerprint(leaf.pk, actual) &&
                 equalConstantTime(actual, expected, sizeof(actual));
  mbedtls_platform_zeroize(expected, sizeof(expected));
  mbedtls_platform_zeroize(actual, sizeof(actual));
  return matches;
}

}  // namespace island_link
#endif
