#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
# Regenerate the FancyKitty style-sampler previews into tools/art/out/.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out="$here/out"
mkdir -p "$out/.build"
python3 "$here/lvgl_c_to_png.py" --reference >/dev/null
swiftc -O -swift-version 5 "$here/draw.swift" -o "$out/.build/draw"
"$out/.build/draw" "$out" "$out/reference"
