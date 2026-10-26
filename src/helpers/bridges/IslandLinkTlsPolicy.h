#pragma once

#ifdef ESP32_PLATFORM
#include <mbedtls/ssl.h>

namespace island_link {

// Configure verification for the relay's pinned self-signed leaf. The pin must
// remain valid until this TLS config is freed. Returns false for an absent or
// malformed pin, in which case the caller must not start a handshake.
// Set the expected hostname on each ssl context and require a zero final
// mbedtls_ssl_get_verify_result() after the handshake.
bool configurePinnedTls(mbedtls_ssl_config& config, const char* pin);

}  // namespace island_link
#endif
