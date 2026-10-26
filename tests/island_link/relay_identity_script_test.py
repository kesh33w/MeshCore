#!/usr/bin/env python3
"""Offline check for the relay certificate generator."""

import hashlib
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "generate_island_link_relay_identity.py"


class RelayIdentityScriptTest(unittest.TestCase):
    def test_generated_certificate_and_overwrite_guard(self):
        with tempfile.TemporaryDirectory() as parent:
            output = Path(parent) / "relay"
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(output)],
                capture_output=True,
                text=True,
                check=True,
            )
            match = re.search(r"Relay SPKI SHA-256: ([0-9a-f]{64})\n", result.stdout)
            self.assertIsNotNone(match)
            key = output / "relay-key.pem"
            cert = output / "relay-cert.pem"
            self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o700)
            self.assertEqual(stat.S_IMODE(key.stat().st_mode), 0o600)
            self.assertEqual(stat.S_IMODE(cert.stat().st_mode), 0o600)

            details = subprocess.check_output(
                ["openssl", "x509", "-in", str(cert), "-noout", "-text"],
                text=True,
            )
            self.assertIn("DNS:ilink.sacmesh.org", details)
            self.assertIn("CA:FALSE", details)
            self.assertIn("TLS Web Server Authentication", details)
            pubkey = subprocess.check_output(
                ["openssl", "x509", "-in", str(cert), "-pubkey", "-noout"]
            )
            spki = subprocess.check_output(
                ["openssl", "pkey", "-pubin", "-outform", "DER"],
                input=pubkey,
            )
            self.assertEqual(match.group(1), hashlib.sha256(spki).hexdigest())

            existing_key = key.read_bytes()
            retry = subprocess.run(
                [sys.executable, str(SCRIPT), str(output)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(retry.returncode, 0)
            self.assertEqual(key.read_bytes(), existing_key)


if __name__ == "__main__":
    unittest.main()
