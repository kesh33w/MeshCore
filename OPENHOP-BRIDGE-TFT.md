# Kesh's V4.3 TFT iLink firmware

iLink means Internet Link. This is Kesh's Heltec V4.3 TFT firmware, based on the MeshCore Observer lineage, with an optional TLS Internet Link.

This branch starts at `d664d89df66bdacc1319900a2e2d152fa31b9dc9` in the MeshCore observer-firmware lineage. It contains the private link, enrollment, screen and Wi-Fi setup source for the Heltec V4.3 TFT in [Kesh's personal OpenHop bridge project](https://github.com/kesh33w/kesh-openhop-bridge). Kesh uses sacmesh.org to host project downloads. MeshCore's MIT license remains in `license.txt`.

The active project branch is `kesh/v43-tft-ilink`. It includes the automatic-advert and hourly-default behavior from the published local60 source `ddd2b54d742bf4f674bb6bc1e75cef167bb4b828`. Existing release tags and historical source pins, including `85af7e596bde8e03b09e29f47cde40187972740e`, remain unchanged. The original `sacmesh/v43-tft-internet-link-20261105` branch remains available. The [branch history and commit mapping](https://github.com/kesh33w/kesh-openhop-bridge/blob/kesh/openhop-bridge/docs/branch-history.md) document the parallel history and how to switch an existing clone.

The checked-in `platformio.sacmesh.example.ini` selects a build that generates its device identity during installation. Copy it to the ignored `platformio.local.ini` before building. No Wi-Fi password, device private key, client certificate, admin password or installed identity is included in this branch.

The original November 5 `sacmesh-v43-tft-merged.bin` has SHA-256 `ff6be9a07b78e3edf9558e7cea6bb19cee5568ac4cf9302b757a7027e65564bd` and is available at https://sacmesh.org/downloads/firmware/heltec-v43-tft/2026-11-05/sacmesh-v43-tft-merged.bin . The image generates its device key, admin password and enrollment identity during installation. Its NVS is erased and its SPIFFS filesystem is empty. Embedded certificates are public trust roots; no device private key or client certificate is included. The `PRIVATE_TESTER_FULL_SETUP` flag enables the full radio setup wizard and does not supply credentials. Dependency and generated trust-bundle differences mean a bit-identical rebuild is not claimed. The binary is flashed at offset `0x0` after a full erase. Browser USB flashing has not been verified.

The Pi-to-V4 test passed one encrypted recipient message and an independently received recipient-generated acknowledgment. This is a bounded test, not an endurance result.

## Build

Use PlatformIO 6.2.0 and the pinned Espressif32 6.11.0 platform. From this branch:

```sh
cp platformio.sacmesh.example.ini platformio.local.ini
pio run -e sacmesh_v43_tft_touch
```

PlatformIO downloads public dependencies and the Adafruit CA bundle. Generated certificate bundles, WebConfig HTML and local build products remain ignored. The public checkout built successfully on November 5 with the existing resolved dependency cache and a freshly downloaded public CA bundle. This verifies the source build contract, but does not establish a fresh dependency installation or bit-identical firmware.

## Automatic adverts, November 6, 2026

The automatic-advert fix restores the existing MeshCore timers and saved interval settings. The TFT sends the standard zero-hop boot advert after 16 seconds. New TFT installations start with a 60-minute local interval and the unchanged 47-hour flood interval. The existing CLI save policy disables a local interval below 60 minutes when settings are saved. For a configured installation, use `set advert.interval 60` and `set flood.advert.interval 47` to persist hourly local adverts and the standard flood interval.

Saved intervals, including explicit zero, retain their meaning after reboot. An existing installation with both intervals saved as zero needs explicit nonzero settings to enable recurring adverts. This release does not silently replace those preferences.

The new merged image remains a full-erase installation at `0x0`; the previous November 5 download remains available at its original URL. The endpoint, trust pin, RF defaults and host-rf route are unchanged. Validation covers a full TFT build, exact release-target preprocessing and image/partition checks. An attached Heltec V4.3 TFT has also received an application-only update with the flash hash verified. Its existing identity, name, RF profile and MQTT origin were preserved. The advert intervals survived a reboot, and the Pi received its normal startup advert. The analyzer recognized the configured repeater name. Browser full-erase flashing remains unverified.

## Path hash width

New TFT installations default to two-byte advert paths. `PRIVATE_PATH_HASH_BYTES=2` initializes `path.hash.mode=1`. The CLI mode is zero-based: mode0 means one byte, mode1 means two bytes, and mode2 means three bytes. Saved preferences remain adjustable and take precedence over the new-install default.

The attached TFT reported mode1 after reboot, confirming two-byte advert paths. Standard zero-hop startup adverts contain no path hashes. Their empty-path encoding can still be shown as one-byte by packet inspectors; that does not change the configured width of flood advert paths.

## Hourly local default update

The [published local60 release](https://github.com/kesh33w/kesh-openhop-bridge/releases/tag/sacmesh-v43-tft-20261106-local60) records its immutable source and binary checksums. The active Kesh branch carries the same runtime source while retaining its own project attribution and branch history.

The TFT constructor now initializes the local interval to 60 minutes. This applies only before saved preferences load. Existing configured values, including intentional zero, remain unchanged. The flood default and saved flood intervals are unchanged. The attached TFT has a verified 60-minute local and 47-hour flood interval.
