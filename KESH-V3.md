# Kesh's Heltec V3 OLED iLink firmware

This build is for the standard Heltec WiFi LoRa 32 V3 with the small SSD1306 OLED display. It uses the `esp32-s3-devkitc-1` board with the Heltec V3 pin map, 8 MB flash and no PSRAM and the same repeater, MQTT observer, iLink, automatic enrollment and per-install identity code as the published TFT local60 firmware. It is not a companion or USB modem build. Do not use a V4, V4.3, TFT, or R8 image on the V3. This build does not require PSRAM. It retains the upstream V3 memory limits: up to two active MQTT slots, a six-packet queue, and reduced neighbor detail, rather than the V4 capacities. The default enables only the Sacramento slot.

New installs default to 909.875 MHz, 62.5 kHz bandwidth, SF9, coding rate 4/5, a 10 dBm chip transmit setting, two-byte paths, a 60-minute local advert and a 47-hour flood advert. The normal zero-hop startup advert is scheduled after 16 seconds. Saved preferences override the fresh-install defaults.

Eligible two- and three-byte traffic uses the same iLink admission rules as the TFT build. One-byte traffic follows normal RF handling. Local repeating and MQTT observation retain their existing behavior when iLink is unavailable. The service endpoint, server certificate pin, CA trust and `host-rf` route are unchanged. Each installation generates its own device identity, admin password and enrollment key. No installed identity or Wi-Fi credentials are supplied.

## Install

The merged image is a full first installation. A full erase replaces the existing firmware, identity and settings. Record any settings you need first.

1. Download `kesh-v3-merged.bin` and verify it against `SHA256SUMS.txt`.
2. Attach the antenna, then connect the matching V3 OLED radio with a USB data cable.
3. Open https://flasher.meshcore.io/ in desktop Chrome or Edge. Choose **Custom Firmware**, then select the merged file with its filename unchanged.
4. Accept the full erase only when ready to replace the identity and settings. Flash the image and keep the cable connected until complete.
5. Join the `MeshCore-Setup-XXXX` Wi-Fi network shown on the OLED. Stay connected if your phone reports no internet. Open `http://192.168.4.1/` in a browser.
6. Complete the setup wizard. Select your Wi-Fi and use **Keep current settings** for the supplied Sacramento radio profile, or choose the exact profile required by your local mesh. Save, allow the node to restart, then allow another restart after enrollment succeeds.
7. The OLED and USB serial log show Wi-Fi and link state. An authenticated link indicates a TLS session, not proof of message delivery or a recipient acknowledgment.

The `-merged.bin` filename tells the custom flasher to write at `0x0` after a full erase. Do not write the merged image at the application offset `0x10000`. The release also includes an application-only image for maintainers. It does not initialize a new board's filesystem and is not the first-install file.

To reopen Wi-Fi setup later, long-press USER until the setup instruction appears, then release it. Release before the automatic restart because USER is also the boot strap pin. Normal power cycles and network outages do not reopen the setup portal. The serial console can retrieve the installation's admin password with `install admin`; keep that value private. Confirm it is generated and not the upstream default `password`. If the serial console reports `Enrollment: bootstrap unavailable`, do not deploy the node: the inherited startup path can continue with the default admin password after a provisioning failure. Resolve that failure before use.

## Build

Use PlatformIO 6.2.0 and a dedicated PlatformIO core directory. The pinned Espressif32 platform is 6.11.0 with Arduino ESP32 2.0.17. The OLED target applies and verifies a TLS 1.2 minimum patch to that directory's `WiFiClientSecure` source. Unknown source hashes stop the build. Use a separate core directory to avoid changing other projects' framework cache.

```sh
export PLATFORMIO_CORE_DIR="$PWD/.pio-core"
cp platformio.kesh-v3.ini platformio.local.ini
pio run -e kesh_v3
mkdir -p data
pio run -e kesh_v3 -t buildfs -t mergebin
python scripts/package_ilink_v3.py --output out/v3 \
  --boot-app0 "$PLATFORMIO_CORE_DIR/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
pio test -e native -f test_private_link -f test_routing_policy -f test_webconfig_keys
```

The `data` directory must be empty for a public first-install release. The V3 uses the standard 8 MB partition layout, with the application at 0x10000 and SPIFFS at 0x670000. Verify the built SPIFFS with `mkspiffs -l` and unpack it to confirm no files. The package script includes that filesystem and checks the app, partition layout and erased NVS. Review images for credentials before publication. Generated CA bundles, WebConfig HTML and local build outputs remain ignored.

## Verification limits

This V3 release passed compilation, resolved-target preprocessing, 37 focused native tests and image checks. On 7 November 2026, the published merged image was flashed to an 8 MB Heltec V3 with esptool, which verified the written data hash. The image SHA-256 was `dd5ef3d3a925eaae5f205744259793df939562af768ac3bb68cc34d1f7568585`.

USB observations confirmed normal boot, SPIFFS initialization, fresh installation bootstrap, a generated non-default admin password and the documented radio defaults. Display initialization appeared in the serial log, but the physical OLED was not visually checked. The radio received live packets and sent its scheduled startup advert.

The published image starts with repeating enabled. The test board's repeating setting was later disabled and survived a reboot; this did not change the release defaults. A subsequent 127-second setup-mode observation recorded nine received packets, zero forwarded floods, zero core errors and two RF read errors of unknown type. Minimum free heap was 148,644 bytes. This was not a five-minute soak or a network/TLS-load test.

Phone setup, Wi-Fi association, enrollment, MQTT, iLink TLS, remote RF delivery, recipient acknowledgments and browser USB flashing remain unverified on V3 hardware. Runtime heap margin under simultaneous MQTT, TLS enrollment and iLink load has not been measured. Prior V4.3 OLED/TFT results do not validate these V3 behaviors.

MeshCore's MIT license is in `license.txt`. The release notices retain the framework and dependency licenses. This is Kesh's personal OpenHop bridge project; sacmesh.org hosts its downloads.

## Source provenance

The original source archive contains the public MeshCore fork at commit `ddc27fb99cb34b21e6f8a0943bf4e64237961012` plus the V3 build profile, V3 packaging script and an earlier version of this guide. Its checksum is recorded in the release's `source-manifest.json`.

The V3 source is maintained on the [`kesh/v3-oled-ilink` branch](https://github.com/kesh33w/MeshCore/tree/kesh/v3-oled-ilink). The build profile and packaging script match the original archive. This guide adds the subsequent hardware observations. Existing V4.3 runtime code, OLED/TFT defaults and service configuration are unchanged.
