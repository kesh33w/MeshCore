"""Package the Heltec V3 first-install image with an empty formatted SPIFFS."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def assemble(build, destination, boot_app0):
    build, destination = Path(build), Path(destination)
    table = (build / "partitions.bin").read_bytes()
    partitions = {}
    for pos in range(0, len(table), 32):
        magic, kind, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", table[pos:pos+32])
        if magic != 0x50AA:
            break
        name = label.rstrip(b"\0").decode()
        expected_type = {"nvs": (1, 2), "otadata": (1, 0), "app0": (0, 16),
                         "app1": (0, 17), "spiffs": (1, 130), "coredump": (1, 3)}
        if name in partitions or (kind, subtype) != expected_type.get(name) or flags != 0:
            raise ValueError("Unexpected partition type, duplicate label, or flags")
        partitions[name] = {"offset": offset, "size": size}
    expected = {
        "nvs": {"offset": 0x9000, "size": 0x5000},
        "otadata": {"offset": 0xE000, "size": 0x2000},
        "app0": {"offset": 0x10000, "size": 0x330000},
        "app1": {"offset": 0x340000, "size": 0x330000},
        "spiffs": {"offset": 0x670000, "size": 0x180000},
        "coredump": {"offset": 0x7F0000, "size": 0x10000},
    }
    if partitions != expected:
        raise ValueError("Unexpected V3 partition layout; refusing to package")
    app = (build / "firmware.bin").read_bytes()
    filesystem = (build / "spiffs.bin").read_bytes()
    merged = (build / "firmware-merged.bin").read_bytes()
    bootloader = (build / "bootloader.bin").read_bytes()
    ota_initializer = Path(boot_app0).read_bytes()
    if len(ota_initializer) != 0x2000 or merged[0xE000:0x10000] != ota_initializer:
        raise ValueError("Merged OTA initializer differs from the pinned framework")
    if not 0 < len(app) <= 0x330000 or len(filesystem) != 0x180000:
        raise ValueError("Application or filesystem does not fit the V3 partition")
    if merged[0x10000:0x10000+len(app)] != app or merged[0x8000:0x8000+len(table)] != table:
        raise ValueError("Merged application or partition differs from build products")
    if merged[:len(bootloader)] != bootloader:
        raise ValueError("Merged bootloader differs from build product")
    if merged[0x9000:0xE000] != b"\xff" * 0x5000 or len(merged) > 0x340000:
        raise ValueError("Merged input contains unexpected persistent state")
    image = bytearray(b"\xff" * 0x7F0000)
    image[:len(merged)] = merged
    image[0x670000:] = filesystem
    assert image[0x340000:0x670000] == b"\xff" * 0x330000
    destination.mkdir(parents=True, exist_ok=True)
    products = {"kesh-v3-merged.bin": image, "kesh-v3-app.bin": app}
    records = {}
    for name, data in products.items():
        (destination / name).write_bytes(data)
        records[name] = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    (destination / "SHA256SUMS.txt").write_text("".join(f"{v['sha256']}  {k}\n" for k, v in records.items()))
    return {"partitions": partitions, "artifacts": records, "nvs": "erased all 0xff", "unused_app1": "erased all 0xff"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default=".pio/build/kesh_v3")
    parser.add_argument("--output", required=True)
    parser.add_argument("--boot-app0", required=True, help="Pinned framework tools/partitions/boot_app0.bin")
    args = parser.parse_args()
    print(json.dumps(assemble(args.build, args.output, args.boot_app0), indent=2))
