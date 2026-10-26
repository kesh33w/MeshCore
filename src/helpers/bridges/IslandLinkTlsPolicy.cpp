#include "IslandLinkTlsPolicy.h"

#ifdef ESP32_PLATFORM
#include "IslandLinkIdentity.h"
#include <mbedtls/x509.h>

namespace island_link {
namespace {

int verifyPinnedLeaf(void* context, mbedtls_x509_crt* certificate,
                     int depth, uint32_t* flags) {
  const char* pin = static_cast<const char*>(context);
  if (!certificate || !flags || depth != 0 ||
      !verifyRelayCertificateSpki(*certificate, pin)) {
    // VERIFY_OPTIONAL suppresses the ordinary CERT_VERIFY_FAILED code. A
    // different fatal code makes pin and hostname failure end the handshake.
    return MBEDTLS_ERR_X509_FATAL_ERROR;
  }

  // Pinning supplies the trust anchor. A first-boot device may have no trusted
  // wall clock; all other flags, including algorithm and key-usage failures,
  // are fatal. The caller also checks the aggregate result after handshake.
  *flags &= ~(MBEDTLS_X509_BADCERT_NOT_TRUSTED |
              MBEDTLS_X509_BADCERT_EXPIRED |
              MBEDTLS_X509_BADCERT_FUTURE);
  return *flags == 0 ? 0 : MBEDTLS_ERR_X509_FATAL_ERROR;
}

}  // namespace

bool configurePinnedTls(mbedtls_ssl_config& config, const char* pin) {
  if (!isValidSpkiPin(pin)) return false;
  // mbedTLS 2.28.7 unconditionally returns CA_CHAIN_REQUIRED for a client in
  // VERIFY_REQUIRED mode with no CA, even when the callback accepts the pin.
  // OPTIONAL permits the no-CA setup; verifyPinnedLeaf returns a non-swallowed
  // fatal error on mismatch. A successful handshake must still have flags 0.
  mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_OPTIONAL);
  mbedtls_ssl_conf_verify(&config, verifyPinnedLeaf, const_cast<char*>(pin));
  return true;
}

}  // namespace island_link
#endif
