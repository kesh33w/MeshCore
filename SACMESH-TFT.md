# SACMesh V4.3 TFT source

This branch starts at `d664d89df66bdacc1319900a2e2d152fa31b9dc9` in the MeshCore observer-firmware lineage. It contains the SACMesh private link, enrollment, screen and Wi-Fi setup source used for the Heltec V4.3 TFT work. MeshCore's MIT license remains in `license.txt`.

The checked-in `platformio.sacmesh.example.ini` selects a build that generates its device identity during installation. Copy it to the ignored `platformio.local.ini` before building. No Wi-Fi password, device private key, client certificate, admin password or installed identity is included in this branch.

The published `sacmesh-v43-tft-merged.bin` has SHA-256 `ff6be9a07b78e3edf9558e7cea6bb19cee5568ac4cf9302b757a7027e65564bd` and is available at https://sacmesh.org/downloads/firmware/heltec-v43-tft/2026-11-05/sacmesh-v43-tft-merged.bin . It was a commissioned build. Its embedded device configuration is deliberately excluded, so this branch does not claim a bit-identical public rebuild of that image. The binary is flashed at offset `0x0` after a full erase. Browser USB flashing has not been verified.

The Pi-to-V4 test passed one encrypted recipient message and an independently received recipient-generated acknowledgment. This is a bounded test, not an endurance result.
