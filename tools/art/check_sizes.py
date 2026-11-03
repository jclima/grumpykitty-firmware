#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
"""Compare tools/art/out/v2/ui_img_*.png sizes with src/ui/images/<name>.c headers.

Reads the PNG IHDR directly (no dependencies). Also flags a PNG with no matching .c file
and any asset the v2 set is expected to produce but is missing. Exit code 1 on mismatch.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
V2 = os.path.join(HERE, "out", "v2")
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
    """Asset names registered in draw_v2.swift (so a missing render is a mismatch too)."""
    src = open(os.path.join(HERE, "draw_v2.swift"), encoding="utf-8").read()
    return sorted(set(re.findall(r'Asset\(name: "(ui_img_\w+)"', src)))


def main():
    bad = 0
    files = sorted(f for f in os.listdir(V2) if f.startswith("ui_img_") and f.endswith(".png")) if os.path.isdir(V2) else []
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
        a, b = png_size(os.path.join(V2, f)), c_size(cpath)
        checked += 1
        if a != b:
            print("MISMATCH %s png=%dx%d c=%dx%d" % (name, a[0], a[1], b[0], b[1])); bad += 1
    print("checked %d assets, %d mismatches" % (checked, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
