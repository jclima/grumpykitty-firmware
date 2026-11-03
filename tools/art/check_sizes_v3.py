#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
"""Compare <dir>/ui_img_*.png sizes with src/ui/images/<name>.c headers.

Usage: check_sizes_v3.py <dir>   (e.g. tools/art/out/v3/alley)

Reads the PNG IHDR directly (no dependencies). Flags a PNG with no matching .c file and
any asset registered in draw_v3.swift that was not rendered. Exit code 1 on mismatch.
Read-only: nothing is written to src/.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
IMG = os.path.join(ROOT, "src", "ui", "images")


def png_size(path):
    with open(path, "rb") as f:
        d = f.read(24)
    if d[:8] != b"\x89PNG\r\n\x1a\n" or d[12:16] != b"IHDR":
        raise ValueError("not a PNG")
    return struct.unpack(">II", d[16:24])


def c_size(path):
    t = open(path, encoding="utf-8-sig").read()
    return (int(re.search(r"\.header\.w\s*=\s*(\d+)", t).group(1)),
            int(re.search(r"\.header\.h\s*=\s*(\d+)", t).group(1)))


def expected_names():
    src = open(os.path.join(HERE, "draw_v3.swift"), encoding="utf-8").read()
    return sorted(set(re.findall(r'Asset3\(name: "(ui_img_\w+)"', src)))


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    d = sys.argv[1]
    bad = 0
    files = sorted(f for f in os.listdir(d) if f.startswith("ui_img_") and f.endswith(".png")) if os.path.isdir(d) else []
    have = {f[:-4] for f in files}
    for n in expected_names():
        if n not in have:
            print("MISSING  %s (registered but not rendered)" % n); bad += 1
    checked = 0
    for f in files:
        name = f[:-4]
        cpath = os.path.join(IMG, name + ".c")
        if not os.path.exists(cpath):
            print("NO .c    %s (no src/ui/images/%s.c)" % (f, name)); bad += 1; continue
        a, b = png_size(os.path.join(d, f)), c_size(cpath)
        checked += 1
        if a != b:
            print("MISMATCH %s png=%dx%d c=%dx%d" % (name, a[0], a[1], b[0], b[1])); bad += 1
    print("%s: checked %d assets, %d mismatches" % (d, checked, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
