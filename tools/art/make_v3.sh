#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
# Render the GrumpyKitty art v3 sampler (mean grumpy alley cat, 3 palettes) to
# tools/art/out/v3/<alley|toxic|bloodmoon>/ plus tools/art/out/v3/overview.png,
# then verify every asset size against src/ui/images/*.c. Nothing here writes to src/.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out="$here/out"
mkdir -p "$out/.build" "$out/v3"
swiftc -O -swift-version 5 "$here/draw.swift" "$here/draw_v3.swift" "$here"/draw_v3_*.swift "$here/main_v3.swift" -o "$out/.build/draw_v3"
"$out/.build/draw_v3" "$out/v3"
for d in alley; do
  python3 "$here/check_sizes_v3.py" "$out/v3/$d"
done
