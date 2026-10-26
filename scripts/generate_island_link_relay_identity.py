#!/usr/bin/env python3
"""Create one offline IslandLink relay key and self-signed certificate.

Run with a new, private output directory. Only the SPKI pin goes in firmware.
The key and certificate are server-side material and must never enter a
firmware image or source control.
"""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


HOSTNAME = "ilink.sacmesh.org"
DAYS = 7305  # Approximately 20 years; pin rotation is a separate operation.


def run_openssl(*args, input_bytes=None):
    result = subprocess.run(
        ["openssl", *args],
        input=input_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode:
        raise RuntimeError("OpenSSL failed while generating the relay identity")
    return result.stdout


def create(output_dir: Path):
    output_dir = output_dir.expanduser().absolute()
    if output_dir.exists():
        raise FileExistsError("output directory already exists; choose a new one")
    if not output_dir.parent.is_dir():
        raise FileNotFoundError("output parent directory does not exist")

    old_umask = os.umask(0o077)
    try:
        with tempfile.TemporaryDirectory(prefix=".island-link-", dir=output_dir.parent) as tmp:
            temp_dir = Path(tmp)
            key = temp_dir / "relay-key.pem"
            cert = temp_dir / "relay-cert.pem"
            config = temp_dir / "openssl.cnf"
            config.write_text(
                "[req]\n"
                "distinguished_name = dn\n"
                "x509_extensions = extensions\n"
                "prompt = no\n"
                "[dn]\n"
                f"CN = {HOSTNAME}\n"
                "[extensions]\n"
                f"subjectAltName = DNS:{HOSTNAME}\n"
                "basicConstraints = critical,CA:FALSE\n"
                "keyUsage = critical,digitalSignature\n"
                "extendedKeyUsage = serverAuth\n",
                encoding="ascii",
            )
            run_openssl("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(key))
            run_openssl(
                "req", "-new", "-x509", "-sha256", "-days", str(DAYS),
                "-key", str(key), "-out", str(cert), "-config", str(config),
            )
            pubkey_pem = run_openssl("x509", "-in", str(cert), "-pubkey", "-noout")
            spki_der = run_openssl("pkey", "-pubin", "-outform", "DER", input_bytes=pubkey_pem)
            pin = hashlib.sha256(spki_der).hexdigest()
            config.unlink()
            key.chmod(0o600)
            cert.chmod(0o600)
            temp_dir.rename(output_dir)
    finally:
        os.umask(old_umask)

    print(f"Relay SPKI SHA-256: {pin}")
    print(f"Relay key and certificate: {output_dir}")
    print("Keep the private key on the relay. Put only the 64-character pin in firmware.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_dir", type=Path, help="new private directory")
    args = parser.parse_args()
    try:
        if shutil.which("openssl") is None:
            raise FileNotFoundError("openssl is not installed")
        create(args.output_dir)
    except (FileExistsError, FileNotFoundError, RuntimeError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
