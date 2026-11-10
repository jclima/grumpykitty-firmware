#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
"""Package a PlatformIO build as merged and split flash images.

Usage: merge_bin.py [--build-dir .pio/build/esp32s3box] [--version X.Y.Z] [--out-dir dist]

Offsets match the PlatformIO espressif32 upload layout and partitions_16mb.csv
(nvs 0x9000, otadata 0xe000, app0 0x10000, app1 0x790000):
  0x0 bootloader.bin, 0x8000 partitions.bin, 0xe000 boot_app0.bin, 0x10000 firmware.bin.
Real slot, NVS and otadata bounds are read from the built partitions.bin, and
firmware.bin must fit the smallest OTA app slot (PlatformIO's checkprogsize only
compares against the largest app partition, so this is the real guard).

partitions.bin binary format (ESP-IDF): consecutive 32-byte little-endian entries
  magic u16 = 0x50AA, type u8, subtype u8, offset u32, size u32, label char[16], flags u32
terminated by an MD5 entry (magic 0xEBEB) or an erased entry (0xFFFF).
type 0 = app (subtype 0x10 ota_0, 0x11 ota_1, ...), type 1 = data (subtype 0 ota, 2 nvs).
The merged image is for esptool clean installs only (it covers the NVS region
with 0xFF, so flashing it wipes saved settings); it is not listed in the manifest.
The split images update only these regions and preserve the settings/NVS partition.
"""
import argparse, glob, hashlib, json, os, shutil, struct, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HOME = os.path.expanduser("~")
SECTOR = 0x1000
PART_MAGIC, MD5_MAGIC = 0x50AA, 0xEBEB
TYPE_APP, TYPE_DATA = 0, 1
SUB_OTA_0, SUB_OTA_1 = 0x10, 0x11
SUB_DATA_OTA, SUB_DATA_NVS = 0, 2
WARN_FRACTION = 0.90
EXPECT_NVS = (0x9000, 0x5000)
EXPECT_OTADATA_OFFSET = 0xE000


def parse_partitions(path):
    """Return a list of (label, type, subtype, offset, size) from partitions.bin."""
    with open(path, "rb") as f:
        data = f.read()
    out = []
    for i in range(0, len(data) - 31, 32):
        entry = data[i:i + 32]
        magic = struct.unpack("<H", entry[:2])[0]
        if magic == MD5_MAGIC or magic == 0xFFFF:
            break
        if magic != PART_MAGIC:
            sys.exit(f"{path}: bad partition entry magic {magic:#x} at {i:#x}")
        ptype, subtype, offset, size = struct.unpack("<BBII", entry[2:12])
        label = entry[12:28].split(b"\0")[0].decode("ascii", "replace")
        out.append((label, ptype, subtype, offset, size))
    return out


def layout_bounds(parts):
    """Validate NVS/otadata/OTA slots; return (nvs_start, nvs_end, smallest_slot_size)."""
    def find(ptype, subtype):
        return [p for p in parts if p[1] == ptype and p[2] == subtype]
    nvs = find(TYPE_DATA, SUB_DATA_NVS)
    if len(nvs) != 1 or (nvs[0][3], nvs[0][4]) != EXPECT_NVS:
        sys.exit(f"partition table: NVS must be at {EXPECT_NVS[0]:#x} size {EXPECT_NVS[1]:#x}, got "
                 f"{[(hex(p[3]), hex(p[4])) for p in nvs]}")
    otadata = find(TYPE_DATA, SUB_DATA_OTA)
    if len(otadata) != 1 or otadata[0][3] != EXPECT_OTADATA_OFFSET:
        sys.exit(f"partition table: otadata must be at {EXPECT_OTADATA_OFFSET:#x}, got "
                 f"{[hex(p[3]) for p in otadata]}")
    slots = [p for p in parts if p[1] == TYPE_APP and p[2] in (SUB_OTA_0, SUB_OTA_1)]
    if sorted(p[2] for p in slots) != [SUB_OTA_0, SUB_OTA_1]:
        sys.exit("partition table: expected exactly one ota_0 and one ota_1 app slot")
    return nvs[0][3], nvs[0][3] + nvs[0][4], min(p[4] for p in slots)


def check_firmware_fits(fw_path, slot_size):
    size = os.path.getsize(fw_path)
    pct = 100.0 * size / slot_size
    if size > slot_size:
        sys.exit(f"firmware.bin {size} bytes exceeds smallest OTA slot {slot_size} bytes ({pct:.1f}%)")
    if size >= WARN_FRACTION * slot_size:
        print(f"WARNING: firmware.bin is {pct:.1f}% of the smallest OTA slot ({size}/{slot_size} bytes)",
              file=sys.stderr)
    return size, pct


def find_boot_app0():
    pats = [os.path.join(HOME, ".platformio/packages/framework-arduinoespressif32*/tools/partitions/boot_app0.bin")]
    for p in pats:
        hits = sorted(glob.glob(p))
        if hits:
            return hits[0]
    sys.exit("boot_app0.bin not found; pass --boot-app0")


def esptool_cmd():
    env = os.environ.get("ESPTOOL")
    if env:
        return env.split()
    cands = [os.path.join(HOME, ".platformio/penv/bin/python")]
    for py in cands:
        if os.path.exists(py) and subprocess.call([py, "-m", "esptool", "version"],
                                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0:
            return [py, "-m", "esptool"]
    tool = os.path.join(HOME, ".platformio/packages/tool-esptoolpy/esptool.py")
    if os.path.exists(tool):
        return [sys.executable, tool]
    if subprocess.call([sys.executable, "-m", "esptool", "version"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0:
        return [sys.executable, "-m", "esptool"]
    sys.exit("esptool not found (set ESPTOOL='python -m esptool' or install PlatformIO)")


def rounded_range(offset, path):
    end = offset + os.path.getsize(path)
    return offset, ((end + SECTOR - 1) // SECTOR) * SECTOR


def validate_ranges(files, nvs_start, nvs_end):
    ranges = []
    for offset, path in sorted(files.items()):
        start, end = rounded_range(offset, path)
        if start < nvs_end and end > nvs_start:
            sys.exit(f"{os.path.basename(path)} write range {start:#x}-{end:#x} overlaps NVS {nvs_start:#x}-{nvs_end:#x}")
        ranges.append((start, end, path))
    for (_, left_end, left_path), (right_start, _, right_path) in zip(ranges, ranges[1:]):
        if left_end > right_start:
            sys.exit(f"split write ranges overlap: {left_path} and {right_path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=os.path.join(ROOT, ".pio/build/esp32s3box"))
    ap.add_argument("--version", default=None, help="default: contents of VERSION")
    ap.add_argument("--out-dir", default=os.path.join(ROOT, "dist"))
    ap.add_argument("--boot-app0", default=None)
    a = ap.parse_args()

    version = a.version
    if not version:
        with open(os.path.join(ROOT, "VERSION")) as f:
            version = f.read().strip()
    bd = a.build_dir
    parts = [(0x0, "bootloader.bin"), (0x8000, "partitions.bin"), (0x10000, "firmware.bin")]
    files = {}
    for off, name in parts:
        p = os.path.join(bd, name)
        if not os.path.isfile(p):
            sys.exit(f"missing {p}; build first (make build)")
        files[off] = p
    files[0xE000] = a.boot_app0 or find_boot_app0()
    nvs_start, nvs_end, slot = layout_bounds(parse_partitions(files[0x8000]))
    size, pct = check_firmware_fits(files[0x10000], slot)
    print(f"firmware.bin {size} bytes = {pct:.1f}% of smallest OTA slot ({slot:#x})")
    validate_ranges(files, nvs_start, nvs_end)

    os.makedirs(a.out_dir, exist_ok=True)
    merged_name = f"grumpykitty-{version}-merged.bin"
    merged = os.path.join(a.out_dir, merged_name)
    cmd = esptool_cmd() + ["--chip", "esp32s3", "merge_bin", "-o", merged,
                           "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"]
    for off in sorted(files):
        cmd += [hex(off), files[off]]
    print("+", " ".join(cmd))
    subprocess.check_call(cmd)

    split_names = {
        0x0: f"grumpykitty-{version}-bootloader.bin",
        0x8000: f"grumpykitty-{version}-partitions.bin",
        0xE000: f"grumpykitty-{version}-boot_app0.bin",
        0x10000: f"grumpykitty-{version}-firmware.bin",
    }
    for offset, name in split_names.items():
        shutil.copyfile(files[offset], os.path.join(a.out_dir, name))
    manifest = {
        "name": "GrumpyKitty",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32-S3",
                    "parts": [{"path": split_names[offset], "offset": offset}
                              for offset in sorted(split_names)]}],
    }
    with open(os.path.join(a.out_dir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    # The device derives the same-origin payload name from VERSION. This feed
    # describes only the app binary, never the USB flash image or partition table.
    with open(files[0x10000], "rb") as fw:
        app_digest = hashlib.sha256(fw.read()).hexdigest()
    with open(os.path.join(a.out_dir, "ota.json"), "w") as f:
        json.dump({"schema": 1, "device": "meowkit-s3", "version": version,
                   "size": size, "sha256": app_digest}, f, indent=2)
        f.write("\n")
    h = hashlib.sha256(open(merged, "rb").read()).hexdigest()
    print(f"merged: {merged} ({os.path.getsize(merged)} bytes) sha256={h}")


if __name__ == "__main__":
    main()
