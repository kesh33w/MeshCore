# Kesh's OpenHop bridge: V4.3 TFT source

This branch starts at `d664d89df66bdacc1319900a2e2d152fa31b9dc9` in the MeshCore observer-firmware lineage. It contains the private link, enrollment, screen and Wi-Fi setup source for the Heltec V4.3 TFT in Kesh's personal OpenHop bridge project. Kesh uses sacmesh.org to host project downloads. MeshCore's MIT license remains in `license.txt`.

The checked-in `platformio.sacmesh.example.ini` selects a build that generates its device identity during installation. Copy it to the ignored `platformio.local.ini` before building. No Wi-Fi password, device private key, client certificate, admin password or installed identity is included in this branch.

The published `sacmesh-v43-tft-merged.bin` has SHA-256 `ff6be9a07b78e3edf9558e7cea6bb19cee5568ac4cf9302b757a7027e65564bd` and is available at https://sacmesh.org/downloads/firmware/heltec-v43-tft/2026-11-05/sacmesh-v43-tft-merged.bin . The image generates its device key, admin password and enrollment identity during installation. Its NVS is erased and its SPIFFS filesystem is empty. Embedded certificates are public trust roots; no device private key or client certificate is included. The `PRIVATE_TESTER_FULL_SETUP` flag enables the full radio setup wizard and does not supply credentials. Dependency and generated trust-bundle differences mean a bit-identical rebuild is not claimed. The binary is flashed at offset `0x0` after a full erase. Browser USB flashing has not been verified.

The Pi-to-V4 test passed one encrypted recipient message and an independently received recipient-generated acknowledgment. This is a bounded test, not an endurance result.

## Build

Use PlatformIO 6.2.0 and the pinned Espressif32 6.11.0 platform. From this branch:

```sh
cp platformio.sacmesh.example.ini platformio.local.ini
pio run -e sacmesh_v43_tft_touch
```

PlatformIO downloads public dependencies and the Adafruit CA bundle. Generated certificate bundles, WebConfig HTML and local build products remain ignored. The public checkout built successfully on November 5 with the existing resolved dependency cache and a freshly downloaded public CA bundle. This verifies the source build contract, but does not establish a fresh dependency installation or bit-identical firmware.
