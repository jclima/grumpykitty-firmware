<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 JC Lima -->
# FancyKitty art pipeline

Procedural kawaii-kitty art plus converters between PNG and LVGL `.c` images.
Standard-library Python 3 and macOS `swiftc`/`sips` only; no external art.

## Regenerate the style sampler

    tools/art/make_preview.sh

Writes to `tools/art/out/` (git-ignored): `reference/` (current look decoded
from `src/ui/images/*.c` and the boot GIF's first frame), `mascot_*.png`,
`icon_*.png`, `background.png`, `message_box_bg.png`, `contact_sheet.png`.

## Converters

    python3 tools/art/lvgl_c_to_png.py --reference        # dump reference set
    python3 tools/art/lvgl_c_to_png.py file.c [...] -o dir
    python3 tools/art/png_to_lvgl_c.py in.png out.c [var]  # dino layout, 3 B/px RGB565 LE + alpha

Input PNGs must be 8-bit RGB/RGBA non-interlaced (otherwise run
`sips -s format png in out.png` first). Nothing here writes into `src/`;
copying a generated `.c` there is a deliberate manual step.

## Adding an asset

1. Add a `drawFoo() -> CGContext` function in `draw.swift` (use `makeCtx(w, h)`,
   top-left origin; reuse `drawHead`, `shape`, `tube`, `heart`, `sparkle`, `fish`).
2. Call `save(...)` for it in the `main` section (and add it to `drawSheet` if it
   should appear on the contact sheet).
3. Keep text out of the images; the firmware renders labels.
4. Every new file starts with `SPDX-License-Identifier: MIT` and
   `Copyright (c) 2026 JC Lima`.

Palette: pink F8B4C8, peach FFD3B0, cream FFF3DC, lavender CDB8F0, mint B8EBD0,
baby blue B5DCF5, charcoal outline 4A3F55, coral FF7F7F (plus a gold bell accent).
