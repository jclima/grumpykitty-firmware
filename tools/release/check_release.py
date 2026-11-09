#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
"""Verify an assembled web-flasher site before it is published.

Usage:
  check_release.py --site _site --version-file VERSION [--sums dist/SHA256SUMS]
  check_release.py --self-test

Checks (all must pass):
  * manifest.json lists exactly the parts 0x0 bootloader, 0x8000 partitions,
    0xe000 boot_app0, 0x10000 firmware, and new_install_prompt_erase is true
  * every manifest path exists in <site>/flash, is a plain same-origin file name,
    and its sha256 matches SHA256SUMS where listed
  * the shipped partitions.bin (32-byte entries: magic 0xAA50 as bytes AA 50,
    type, subtype, offset, size, label; ends at an 0xEBEB md5 or 0xFFFF entry)
    has exactly app0 ota_0 at 0x10000 and app1 ota_1 at 0x790000 (both app
    partitions, non-overlapping), NVS at 0x9000 size 0x5000 and otadata at 0xe000
  * firmware.bin fits the smallest OTA app slot (warns at >=90%)
  * no part's sector-rounded write range overlaps the NVS partition, as parsed
    from the shipped partitions.bin (parts must also not overlap each other)
  * the merged image is all 0xFF across 0x9000-0xE000 (NVS region)
  * firmware.bin contains the VERSION string
"""
import argparse, hashlib, json, os, struct, subprocess, sys, tempfile

SECTOR = 0x1000
EXPECTED = {0x0: "bootloader", 0x8000: "partitions", 0xE000: "boot_app0", 0x10000: "firmware"}
APP0_OFFSET, APP1_OFFSET = 0x10000, 0x790000
NVS_EXPECT = (0x9000, 0x5000)
OTADATA_OFFSET = 0xE000


class CheckError(Exception):
    pass


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_sums(path):
    sums = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            digest, _, name = line.partition(" ")
            sums[os.path.basename(name.strip().lstrip("*"))] = digest
    return sums


def parse_table(partitions_bin):
    """Return [(label, type, subtype, offset, size)] from an ESP32 partition table binary."""
    with open(partitions_bin, "rb") as f:
        data = f.read()
    out = []
    for i in range(0, len(data) - 31, 32):
        entry = data[i:i + 32]
        if entry[:2] in (b"\xeb\xeb", b"\xff\xff"):  # md5 / end marker
            break
        if entry[:2] != b"\xaa\x50":
            raise CheckError(f"{os.path.basename(partitions_bin)}: bad entry magic at {i:#x}")
        ptype, subtype, offset, size = struct.unpack("<BBII", entry[2:12])
        label = entry[12:28].split(b"\0")[0].decode("ascii", "replace")
        out.append((label, ptype, subtype, offset, size))
    return out


def check_table(partitions_bin):
    """Validate the dual-slot layout; return (nvs_start, nvs_end, smallest_slot_size)."""
    parts = parse_table(partitions_bin)
    name = os.path.basename(partitions_bin)
    nvs = [p for p in parts if p[1] == 1 and p[2] == 2]
    if len(nvs) != 1:
        raise CheckError(f"{name}: expected exactly one nvs partition, found {len(nvs)}")
    if (nvs[0][3], nvs[0][4]) != NVS_EXPECT:
        raise CheckError(f"{name}: nvs at {nvs[0][3]:#x} size {nvs[0][4]:#x}, "
                         f"expected {NVS_EXPECT[0]:#x} size {NVS_EXPECT[1]:#x}")
    otadata = [p for p in parts if p[1] == 1 and p[2] == 0]
    if len(otadata) != 1 or otadata[0][3] != OTADATA_OFFSET:
        raise CheckError(f"{name}: otadata must be a single partition at {OTADATA_OFFSET:#x}")
    apps = [p for p in parts if p[1] == 0]
    want = {"app0": (0x10, APP0_OFFSET), "app1": (0x11, APP1_OFFSET)}
    if sorted(p[0] for p in apps) != sorted(want):
        raise CheckError(f"{name}: expected exactly app partitions app0 and app1, "
                         f"found {sorted(p[0] for p in apps)}")
    for label, subtype, offset, size in [(p[0], p[2], p[3], p[4]) for p in apps]:
        if (subtype, offset) != want[label]:
            raise CheckError(f"{name}: {label} must be ota_{want[label][0] - 0x10} at {want[label][1]:#x}, "
                             f"found subtype {subtype:#x} at {offset:#x}")
    app0, app1 = sorted(apps, key=lambda p: p[3])
    if app0[3] + app0[4] > app1[3]:
        raise CheckError(f"{name}: app0 overlaps app1")
    return nvs[0][3], nvs[0][3] + nvs[0][4], min(app0[4], app1[4])


def check(site, version, sums_path=None):
    flash = os.path.join(site, "flash")
    manifest_path = os.path.join(flash, "manifest.json")
    if not os.path.isfile(manifest_path):
        raise CheckError(f"missing {manifest_path}")
    with open(manifest_path) as f:
        manifest = json.load(f)
    if manifest.get("new_install_prompt_erase") is not True:
        raise CheckError("manifest: new_install_prompt_erase must be true")
    if manifest.get("version") != version:
        raise CheckError(f"manifest version {manifest.get('version')!r} != VERSION {version!r}")
    builds = manifest.get("builds") or []
    if len(builds) != 1:
        raise CheckError("manifest: expected exactly one build")
    parts = builds[0].get("parts") or []
    offsets = sorted(p.get("offset") for p in parts)
    if offsets != sorted(EXPECTED) or len(parts) != len(EXPECTED):
        raise CheckError(f"manifest offsets {[hex(o) if isinstance(o, int) else o for o in offsets]} "
                         f"!= {[hex(o) for o in sorted(EXPECTED)]}")

    sums = read_sums(sums_path) if sums_path else {}
    files = {}
    for p in parts:
        name, off = p["path"], p["offset"]
        if name != os.path.basename(name) or "/" in name or "\\" in name or ":" in name:
            raise CheckError(f"manifest path {name!r} is not a same-origin file name")
        if name != f"grumpykitty-{version}-{EXPECTED[off]}.bin":
            raise CheckError(f"offset {off:#x}: unexpected file {name!r}")
        path = os.path.join(flash, name)
        if not os.path.isfile(path):
            raise CheckError(f"manifest path {name} missing from {flash}")
        if name in sums and sums[name] != sha256(path):
            raise CheckError(f"{name}: sha256 differs from SHA256SUMS")
        files[off] = path
    if sums_path:
        for name in [f"grumpykitty-{version}-merged.bin", "manifest.json"]:
            path = os.path.join(flash, name)
            if name not in sums:
                raise CheckError(f"{name} not listed in SHA256SUMS")
            if os.path.isfile(path) and sums[name] != sha256(path):
                raise CheckError(f"{name}: sha256 differs from SHA256SUMS")

    nvs_start, nvs_end, slot = check_table(files[0x8000])
    fw_size = os.path.getsize(files[0x10000])
    if fw_size > slot:
        raise CheckError(f"firmware.bin {fw_size} bytes exceeds smallest OTA slot {slot} bytes")
    if fw_size >= 0.9 * slot:
        print(f"check_release: WARNING: firmware.bin is {100.0 * fw_size / slot:.1f}% of the OTA slot",
              file=sys.stderr)
    ranges = []
    for off, path in sorted(files.items()):
        end = off + os.path.getsize(path)
        end = (end + SECTOR - 1) // SECTOR * SECTOR
        if off < nvs_end and end > nvs_start:
            raise CheckError(f"{os.path.basename(path)} write range {off:#x}-{end:#x} "
                             f"overlaps NVS {nvs_start:#x}-{nvs_end:#x}")
        ranges.append((off, end, path))
    for (_, e, lp), (s, _, rp) in zip(ranges, ranges[1:]):
        if e > s:
            raise CheckError(f"{os.path.basename(lp)} overlaps {os.path.basename(rp)}")

    merged = os.path.join(flash, f"grumpykitty-{version}-merged.bin")
    if not os.path.isfile(merged):
        raise CheckError(f"missing {merged}")
    with open(merged, "rb") as f:
        f.seek(nvs_start)
        region = f.read(nvs_end - nvs_start)
    if len(region) != nvs_end - nvs_start or region.count(b"\xff") != len(region):
        raise CheckError(f"merged image NVS region {nvs_start:#x}-{nvs_end:#x} is not all 0xFF")

    with open(files[0x10000], "rb") as f:
        if version.encode() not in f.read():
            raise CheckError(f"firmware.bin does not contain version string {version!r}")
    return nvs_start, nvs_end


# ----------------------------------------------------------------- self-test
def _entry(ptype, subtype, offset, size, name):
    return b"\xaa\x50" + struct.pack("<BBII", ptype, subtype, offset, size) + name.ljust(16, b"\0") + b"\0" * 4


def _make_site(root, version, fw_size=0x2000, boot_size=0x5000, nvs_clean=True,
               slot_size=0x780000, nvs_offset=0x9000, with_app1=True):
    flash = os.path.join(root, "flash")
    os.makedirs(flash)
    table = _entry(1, 2, nvs_offset, 0x5000, b"nvs") + _entry(1, 0, 0xE000, 0x2000, b"otadata") \
        + _entry(0, 0x10, APP0_OFFSET, slot_size, b"app0")
    if with_app1:
        table += _entry(0, 0x11, APP1_OFFSET, slot_size, b"app1")
    blobs = {
        "bootloader": b"\x11" * boot_size,
        "partitions": table.ljust(0xC00, b"\xff"),
        "boot_app0": b"\x22" * 0x2000,
        "firmware": (b"fw " + version.encode() + b"\0").ljust(fw_size, b"\x33"),
    }
    parts = []
    for off, key in EXPECTED.items():
        name = f"grumpykitty-{version}-{key}.bin"
        with open(os.path.join(flash, name), "wb") as f:
            f.write(blobs[key])
        parts.append({"path": name, "offset": off})
    merged = bytearray(b"\xff" * 0x20000)
    if not nvs_clean:
        merged[0x9100] = 0
    with open(os.path.join(flash, f"grumpykitty-{version}-merged.bin"), "wb") as f:
        f.write(merged)
    with open(os.path.join(flash, "manifest.json"), "w") as f:
        json.dump({"name": "GrumpyKitty", "version": version, "new_install_prompt_erase": True,
                   "builds": [{"chipFamily": "ESP32-S3", "parts": parts}]}, f)
    return flash


def self_test():
    version = "9.9.9-test"
    cases = [
        ("pass", {}, None),
        ("nvs-overlap (bootloader reaches 0x9000)", {"boot_size": 0x9100}, "overlaps NVS"),
        ("merged image NVS not blank", {"nvs_clean": False}, "not all 0xFF"),
        ("firmware larger than OTA slot", {"slot_size": 0x1000, "fw_size": 0x2000}, "exceeds smallest OTA slot"),
        ("missing app1", {"with_app1": False}, "app0 and app1"),
        ("altered NVS offset", {"nvs_offset": 0xA000}, "nvs at 0xa000"),
    ]
    failures = 0
    for label, kwargs, expect in cases:
        with tempfile.TemporaryDirectory() as tmp:
            _make_site(tmp, version, **kwargs)
            try:
                check(tmp, version)
                got = None
            except CheckError as e:
                got = str(e)
        ok = (got is None) if expect is None else (got is not None and expect in got)
        print(f"self-test {'ok  ' if ok else 'FAIL'} {label}" + (f" -> {got}" if got else ""))
        failures += not ok
    # bad offset set and tampered checksum
    with tempfile.TemporaryDirectory() as tmp:
        flash = _make_site(tmp, version)
        mp = os.path.join(flash, "manifest.json")
        m = json.load(open(mp))
        m["builds"][0]["parts"][1]["offset"] = 0x9000
        json.dump(m, open(mp, "w"))
        try:
            check(tmp, version)
            ok = False
        except CheckError as e:
            ok = "offsets" in str(e)
        print(f"self-test {'ok  ' if ok else 'FAIL'} wrong offsets rejected")
        failures += not ok
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--site", default="_site")
    ap.add_argument("--version-file", default="VERSION")
    ap.add_argument("--sums", default=None, help="SHA256SUMS to compare against")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        sys.exit(1 if self_test() else 0)
    with open(a.version_file) as f:
        version = f.read().strip()
    try:
        s, e = check(a.site, version, a.sums)
    except CheckError as ex:
        sys.exit(f"check_release: FAIL: {ex}")
    print(f"check_release: OK (version {version}, NVS {s:#x}-{e:#x} untouched by split parts)")


if __name__ == "__main__":
    main()
