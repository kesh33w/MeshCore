# Kesh's Heltec V4.3 OLED iLink firmware

This build is for the standard Heltec WiFi LoRa 32 V4.3 with the small SSD1306 OLED display. It uses the `heltec_v4` board and the same repeater, MQTT observer, iLink, automatic enrollment and per-install identity code as the published TFT local60 firmware. It is not a companion or USB modem build. Do not select the `heltec_v4_r8` board or use this image on the TFT model.

New installs default to 909.875 MHz, 62.5 kHz bandwidth, SF9, coding rate 4/5, a 10 dBm chip transmit setting, two-byte paths, a 60-minute local advert and a 47-hour flood advert. Heltec's front-end amplifier affects actual RF output. The normal zero-hop startup advert is scheduled after 16 seconds. Saved preferences override the fresh-install defaults.

Eligible two- and three-byte traffic uses the same iLink admission rules as the TFT build. One-byte traffic follows normal RF handling. Local repeating and MQTT observation retain their existing behavior when iLink is unavailable. The service endpoint, server certificate pin, CA trust and `host-rf` route are unchanged. Each installation generates its own device identity, admin password and enrollment key. No installed identity or Wi-Fi credentials are supplied.

## Install

The merged image is a full first installation. A full erase replaces the existing firmware, identity and settings. Record any settings you need first.

1. Download `kesh-v43-oled-merged.bin` and verify it against `SHA256SUMS.txt`.
2. Attach the antenna, then connect the matching V4.3 OLED radio with a USB data cable.
3. Open https://flasher.meshcore.io/ in desktop Chrome or Edge. Choose **Custom Firmware**, then select the merged file with its filename unchanged.
4. Accept the full erase only when ready to replace the identity and settings. Flash the image and keep the cable connected until complete.
5. Join the `MeshCore-Setup-XXXX` Wi-Fi network shown on the OLED. Stay connected if your phone reports no internet. Open `http://192.168.4.1/` in a browser.
6. Complete the setup wizard. Select your Wi-Fi and use **Keep current settings** for the supplied Sacramento radio profile, or choose the exact profile required by your local mesh. Save, allow the node to restart, then allow another restart after enrollment succeeds.
7. The OLED and USB serial log show Wi-Fi and link state. An authenticated link indicates a TLS session, not proof of message delivery or a recipient acknowledgment.

The `-merged.bin` filename tells the custom flasher to write at `0x0` after a full erase. Do not write the merged image at the application offset `0x10000`. The release also includes an application-only image for maintainers. It does not initialize a new board's filesystem and is not the first-install file.

To reopen Wi-Fi setup later, long-press USER until the setup instruction appears, then release it. Release before the automatic restart because USER is also the boot strap pin. Normal power cycles and network outages do not reopen the setup portal. The serial console can retrieve the installation's admin password with `install admin`; keep that value private.

## Build

Use PlatformIO 6.2.0 and a dedicated PlatformIO core directory. The pinned Espressif32 platform is 6.11.0 with Arduino ESP32 2.0.17. The OLED target applies and verifies a TLS 1.2 minimum patch to that directory's `WiFiClientSecure` source. Unknown source hashes stop the build. Use a separate core directory to avoid changing other projects' framework cache.

```sh
export PLATFORMIO_CORE_DIR="$PWD/.pio-core"
cp platformio.sacmesh.example.ini platformio.local.ini
pio run -e kesh_v43_oled
mkdir -p data
pio run -e kesh_v43_oled -t buildfs -t mergebin
python scripts/package_ilink_oled.py --output out/v43-oled
pio test -e native -f test_private_link -f test_routing_policy -f test_webconfig_keys
```

The `data` directory must be empty for a public first-install release. Verify the built SPIFFS with `mkspiffs -l` and unpack it to confirm no files. The package script includes that filesystem and checks the app, partition layout and erased NVS. Review images for credentials before publication. Generated CA bundles, WebConfig HTML and local build outputs remain ignored.

## Verification limits

This OLED release has passed compilation, resolved-target preprocessing, focused native tests and image checks. It has not been flashed to a non-TFT radio. OLED first boot, phone setup, enrollment, remote RF delivery, recipient acknowledgments and the complete browser USB flashing procedure remain unverified on this hardware. Prior TFT hardware results are not validation of this OLED release.

MeshCore's MIT license is in `license.txt`. The release notices retain the framework and dependency licenses. This is Kesh's personal OpenHop bridge project; sacmesh.org hosts its downloads.
