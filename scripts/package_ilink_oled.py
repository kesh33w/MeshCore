"""Package the built OLED first-install image, including formatted empty SPIFFS."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct


def assemble(build, destination):
    build, destination = Path(build), Path(destination)
    partitions = {}
    table = (build / "partitions.bin").read_bytes()
    for pos in range(0, len(table), 32):
        magic, kind, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", table[pos:pos+32])
        if magic != 0x50AA:
            break
        partitions[label.rstrip(b"\0").decode()] = {"offset": offset, "size": size}
    expected = {
        "nvs": {"offset": 0x9000, "size": 0x5000},
        "app0": {"offset": 0x10000, "size": 0x640000},
        "spiffs": {"offset": 0xC90000, "size": 0x360000},
    }
    if any(partitions.get(key) != value for key, value in expected.items()):
        raise ValueError("Unexpected partition layout. Review before packaging.")
    app = (build / "firmware.bin").read_bytes()
    filesystem = (build / "spiffs.bin").read_bytes()
    merged = (build / "firmware-merged.bin").read_bytes()
    if len(app) > partitions["app0"]["size"] or len(filesystem) != partitions["spiffs"]["size"]:
        raise ValueError("Application or filesystem does not fit its partition")
    if merged[0x10000:0x10000+len(app)] != app or merged[0x8000:0x8000+len(table)] != table:
        raise ValueError("Merged application/partition bytes differ from build products")
    if merged[0x9000:0xE000] != b"\xff" * 0x5000 or len(merged) > 0xC90000:
        raise ValueError("Merged input contains unexpected persistent state")
    end = 0xC90000 + len(filesystem)
    image = bytearray(b"\xff" * end)
    image[:len(merged)] = merged
    image[0xC90000:] = filesystem
    destination.mkdir(parents=True, exist_ok=True)
    output = destination / "kesh-v43-oled-merged.bin"
    output.write_bytes(image)
    shutil.copyfile(build / "firmware.bin", destination / "kesh-v43-oled-app.bin")
    records = {}
    for path in (output, destination / "kesh-v43-oled-app.bin"):
        records[path.name] = {"size": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    (destination / "SHA256SUMS.txt").write_text("".join(f"{v['sha256']}  {k}\n" for k, v in records.items()))
    return {"partitions": partitions, "artifacts": records, "nvs": "erased, all 0xff"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default=".pio/build/kesh_v43_oled")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    print(json.dumps(assemble(args.build, args.output), indent=2))
