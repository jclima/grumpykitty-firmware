#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
# Render the GrumpyKitty art v2 set to tools/art/out/v2/ (<ui_img_name>.png at the exact
# LVGL sizes plus contact_sheet_v2_*.png), then verify sizes against src/ui/images/*.c.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out="$here/out"
mkdir -p "$out/.build" "$out/v2"
swiftc -O -swift-version 5 "$here/draw.swift" "$here/draw_v2.swift" "$here/main.swift" -o "$out/.build/draw"
"$out/.build/draw" v2 "$out/v2"
python3 "$here/check_sizes.py"
