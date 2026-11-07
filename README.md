# Kesh's MeshCore fork

This repository contains Kesh's Heltec V4.3 iLink firmware for [Kesh's OpenHop bridge](https://github.com/kesh33w/kesh-openhop-bridge). The project builds on the [MeshCore Observer fork](https://github.com/agessaman/MeshCore) and the original [MeshCore](https://github.com/meshcore-dev/MeshCore) firmware.

The default branch is `dev`. It retains the general MeshCore source and is not the iLink build. Select a project branch explicitly.

## Choose a project branch

| Hardware | Branch | Setup and build guide |
| --- | --- | --- |
| Heltec V4.3 with TFT touchscreen | [kesh/v43-tft-ilink](https://github.com/kesh33w/MeshCore/tree/kesh/v43-tft-ilink) | [TFT guide](https://github.com/kesh33w/MeshCore/blob/kesh/v43-tft-ilink/OPENHOP-BRIDGE-TFT.md) |
| Standard Heltec V4.3 with SSD1306 OLED | [kesh/v43-oled-ilink](https://github.com/kesh33w/MeshCore/tree/kesh/v43-oled-ilink) | [OLED guide](https://github.com/kesh33w/MeshCore/blob/kesh/v43-oled-ilink/OPENHOP-BRIDGE-OLED.md) |

For a new checkout, choose one:

```sh
git clone --branch kesh/v43-tft-ilink --single-branch https://github.com/kesh33w/MeshCore.git MeshCore-tft
git clone --branch kesh/v43-oled-ilink --single-branch https://github.com/kesh33w/MeshCore.git MeshCore-oled
```

Follow that branch's guide for the exact PlatformIO environment, hardware and flashing procedure. TFT and OLED images are not interchangeable. The OpenHop USB bench modem is a [separate repository](https://github.com/kesh33w/openhop_modem/tree/kesh/heltec-bench).

## What iLink does

The firmware runs a MeshCore repeater and observer with an optional TLS link to the Pi. Eligible traffic with two- or three-byte path hashes can use approved iLink routes. One-byte traffic stays on normal RF. Link failure does not prevent local RF startup.

Each installation generates its own identity and enrollment credentials. The TFT uses touchscreen Wi-Fi setup; the OLED uses a phone setup portal and USER-button recovery. A connected TLS session does not prove RF transmission or recipient delivery.

The hub's [releases](https://github.com/kesh33w/kesh-openhop-bridge/releases) include images, checksums and source manifests. Historical release names, tags and pins remain unchanged. The [branch-history guide](https://github.com/kesh33w/kesh-openhop-bridge/blob/kesh/openhop-bridge/docs/branch-history.md) explains the current Kesh branches and their original counterparts.

## Verification limits

The TFT has a recorded test in which one encrypted recipient message and its return acknowledgment passed through the Pi. This does not establish endurance or field reliability.

The OLED trial release passed compilation, focused native tests and image checks. It has not been flashed to a non-TFT device. OLED setup, enrollment and RF delivery remain unverified on hardware. Browser full-erase flashing also remains unverified. Use the hardware-specific guide for the latest recorded scope.

A merged first-install image requires a full erase and replaces firmware, identity and saved settings. Check the board and release instructions before flashing.

## General MeshCore development

The [preserved upstream README](UPSTREAM-README.md) covers the general library, companion firmware, examples, clients and contribution guidelines. The [upstream documentation](https://docs.meshcore.io) describes MeshCore itself. Those instructions do not select or install Kesh's iLink branches.

The `dev` branch includes the [companion](examples/companion_radio), [repeater](examples/simple_repeater), [room server](examples/simple_room_server) and other examples. Its native unit tests run with:

```sh
pio test --environment native --verbose
```

Kesh uses sacmesh.org to host project downloads. The project remains Kesh's personal work on top of MeshCore and the Observer fork. Original copyright notices and the [MIT license](license.txt) remain unchanged.
