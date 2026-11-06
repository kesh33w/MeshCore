"""Enforce the iLink TLS floor in the pinned Arduino framework before compiling.

Use a dedicated PLATFORMIO_CORE_DIR for this build. Unknown framework revisions
fail closed rather than claiming the TLS floor was applied.
"""
from hashlib import sha256
from pathlib import Path

ORIGINAL_SHA256 = "10d70932e1bee30a67a244d47055651f2b08795a89f3990b53a1ee0ec5e6e444"
ANCHOR = b"    if (alpn_protos != NULL) {"
INSERT = (b"    // Kesh iLink: negotiate TLS 1.2 or newer, never an older protocol.\n"
          b"    mbedtls_ssl_conf_min_version(&ssl_client->ssl_conf,\n"
          b"                                 MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);\n\n")


def patch_framework(path):
    path = Path(path)
    current = path.read_bytes()
    original = current.replace(INSERT, b"", 1) if current.count(INSERT) == 1 else current
    if sha256(original).hexdigest() != ORIGINAL_SHA256 or original.count(ANCHOR) != 1:
        raise RuntimeError("Unrecognized WiFiClientSecure source. Review the TLS patch before building.")
    expected = original.replace(ANCHOR, INSERT + ANCHOR, 1)
    if current != expected:
        path.write_bytes(expected)
    return sha256(expected).hexdigest()


if "Import" in globals():
    Import("env")
    framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    digest = patch_framework(framework / "libraries/WiFiClientSecure/src/ssl_client.cpp")
    env.Append(CPPDEFINES=[("PRIVATE_REMOTE_TLS12_MINIMUM", 1)])
    print("iLink TLS 1.2 framework SHA-256:", digest)
