#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
"""Merge a PlatformIO build into one flashable image plus an ESP Web Tools manifest.

Usage: merge_bin.py [--build-dir .pio/build/esp32s3box] [--version X.Y.Z] [--out-dir dist]

Offsets match the PlatformIO espressif32 upload layout and partitions_16mb.csv
(nvs 0x9000, otadata 0xe000, app0 0x10000):
  0x0 bootloader.bin, 0x8000 partitions.bin, 0xe000 boot_app0.bin, 0x10000 firmware.bin
"""
import argparse, glob, hashlib, json, os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HOME = os.path.expanduser("~")


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

    os.makedirs(a.out_dir, exist_ok=True)
    merged_name = f"grumpykitty-{version}-merged.bin"
    merged = os.path.join(a.out_dir, merged_name)
    cmd = esptool_cmd() + ["--chip", "esp32s3", "merge_bin", "-o", merged,
                           "--flash_mode", "qio", "--flash_freq", "80m", "--flash_size", "16MB"]
    for off in sorted(files):
        cmd += [hex(off), files[off]]
    print("+", " ".join(cmd))
    subprocess.check_call(cmd)

    shutil.copyfile(files[0x10000], os.path.join(a.out_dir, f"grumpykitty-{version}-firmware.bin"))
    manifest = {
        "name": "GrumpyKitty",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32-S3",
                    "parts": [{"path": merged_name, "offset": 0}]}],
    }
    with open(os.path.join(a.out_dir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    h = hashlib.sha256(open(merged, "rb").read()).hexdigest()
    print(f"merged: {merged} ({os.path.getsize(merged)} bytes) sha256={h}")


if __name__ == "__main__":
    main()
