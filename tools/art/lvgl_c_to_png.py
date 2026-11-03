#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
"""Decode LVGL 8 image .c files to PNG (standard library only).

Supports LV_IMG_CF_TRUE_COLOR_ALPHA (3 B/px: RGB565 LE + alpha) and
LV_IMG_CF_TRUE_COLOR (2 B/px RGB565 LE), LV_COLOR_16_SWAP 0.

Usage:  python3 lvgl_c_to_png.py file.c [...] -o outdir
        python3 lvgl_c_to_png.py --reference      # dump the reference set
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngio import write_png  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
REFS = ["background", "mascot", "message_box_bg", "message_box_mascot", "button_bg",
        "dino", "air_mouse", "badusb", "infrared", "header", "clock_bg", "mascot_update"]


def e5(v): return (v << 3) | (v >> 2)
def e6(v): return (v << 2) | (v >> 4)


def decode_c(path):
    txt = open(path, encoding="utf-8-sig").read()
    w = int(re.search(r"\.header\.w\s*=\s*(\d+)", txt).group(1))
    h = int(re.search(r"\.header\.h\s*=\s*(\d+)", txt).group(1))
    cf = re.search(r"\.header\.cf\s*=\s*(\w+)", txt).group(1)
    body = txt[txt.index("_data[] = {") + 11:]
    body = body[:body.index("};")]
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    bpp = {"LV_IMG_CF_TRUE_COLOR_ALPHA": 3, "LV_IMG_CF_TRUE_COLOR": 2}.get(cf)
    if bpp is None:
        raise ValueError("unsupported cf %s" % cf)
    if len(data) != w * h * bpp:
        raise ValueError("%s: size %d != %d" % (path, len(data), w * h * bpp))
    out = bytearray()
    for i in range(0, len(data), bpp):
        v = data[i] | (data[i + 1] << 8)
        out += bytes((e5(v >> 11), e6((v >> 5) & 63), e5(v & 31), data[i + 2] if bpp == 3 else 255))
    return w, h, bytes(out)


def main(argv):
    if "--reference" in argv:
        od = os.path.join(ROOT, "tools", "art", "out", "reference")
        os.makedirs(od, exist_ok=True)
        files = [os.path.join(ROOT, "src", "ui", "images", "ui_img_%s_png.c" % n) for n in REFS]
        gif = os.path.join(ROOT, "src", "system", "assets", "boot_animation.gif")
        subprocess.run(["/usr/bin/sips", "-s", "format", "png", gif, "--out",
                        os.path.join(od, "boot_animation_frame0.png")], check=True, capture_output=True)
        print("wrote boot_animation_frame0.png")
    else:
        files = [a for a in argv if not a.startswith("-") and a.endswith(".c")]
        od = argv[argv.index("-o") + 1] if "-o" in argv else "."
        os.makedirs(od, exist_ok=True)
    for f in files:
        w, h, rgba = decode_c(f)
        name = os.path.basename(f)[:-2] + ".png"
        write_png(os.path.join(od, name), w, h, rgba)
        print("wrote %s (%dx%d)" % (name, w, h))


if __name__ == "__main__":
    main(sys.argv[1:])
