#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
# Render the GrumpyKitty boot animation (alley style) to
#   tools/art/out/v3/alley/boot_animation.gif       (320x240, full opaque frames)
#   tools/art/out/v3/alley/boot_contact_sheet.png   (12 evenly spaced frames read back from the GIF)
# Copying the GIF to src/system/assets/boot_animation.gif is a deliberate manual step:
#   cp tools/art/out/v3/alley/boot_animation.gif src/system/assets/boot_animation.gif
set -e
here=$(cd "$(dirname "$0")" && pwd)
out="$here/out"
mkdir -p "$out/.build" "$out/v3/alley"
swiftc -O -swift-version 5 "$here/draw.swift" "$here/draw_v3.swift" "$here"/draw_v3_*.swift "$here/boot_gif.swift" -o "$out/.build/boot_gif"
"$out/.build/boot_gif" "$out/v3/alley/boot_animation.gif" "$out/v3/alley/boot_contact_sheet.png"
ls -l "$out/v3/alley/boot_animation.gif"
