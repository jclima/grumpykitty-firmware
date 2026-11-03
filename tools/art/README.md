<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 JC Lima -->
# GrumpyKitty art pipeline

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

## Art v2 (grumpy witty kitty, midnight plum theme)

    tools/art/make_v2.sh

Builds `draw.swift` + `draw_v2.swift` + `main.swift`, renders every v2 asset to
`tools/art/out/v2/<ui_img_variable_name>.png` at the exact LVGL size (45 assets:
6 mascots, 13 app icons in APP_REGISTRY order, 14 small UI icons, 12 backgrounds/panels),
writes `contact_sheet_v2_1_mascots.png`, `contact_sheet_v2_2_icons.png`,
`contact_sheet_v2_3_backgrounds_panels.png`, then runs `check_sizes.py`
(PNG IHDR vs `.header.w/.h` in `src/ui/images/<name>.c`; exits 1 on any mismatch or missing asset).

Each asset is one function in `draw_v2.swift`, registered in `mascotAssets` / `iconAssets` /
`smallAssets` / `panelAssets`. Convert to LVGL with `png_to_lvgl_c.py` (nothing here writes to `src/`).
Opaque backgrounds are Bayer-dithered before RGB565 quantisation to avoid banding.
Not redone (still the old look): keyboard/key backgrounds, t9_bg, alphanumeric, functional keys,
key_a/key_b, text_input, pickers, backspace, capital, send, unit, week_bg, date, time.

## Art v3 sampler (mean grumpy alley cat, 3 palettes)

    tools/art/make_v3.sh

Owner feedback on v2 was "too girly, needs a mean grumpy attitude, better colours", so v3 redraws
the character as a scarred, notch-eared alley cat (angry V brows, half-lidded glare with one bigger
eye, snarl with one fang, ragged whiskers, wobbly hand-inked outlines, flat fills with one hard
shadow tone and a thin rim light). Same drawings in three palettes, rendered by `draw_v3.swift`
(built with `draw.swift` and `main_v3.swift`; v2 code is untouched) to
`tools/art/out/v3/<alley|toxic|bloodmoon>/`:

- `ui_img_mascot_png`, `ui_img_mascot_update_png`, `ui_img_message_box_bg_png`,
  `ui_img_background_png`, `ui_img_header_png`, and 70x70 `ui_img_dino_png`, `ui_img_badusb_png`,
  `ui_img_infrared_png`, `ui_img_voicerec_png` (exact LVGL sizes, transparent where the shape is not
  rectangular; icons sit on a dark disc so they read on any tile colour);
- `contact_sheet.png`, `expression_strip.png` (glare / smug / hiss / side-eye busts),
  `props_sheet.png` (DO NOT sign, cord from mouth, dead pixels, couch, trophy, dumpster);
- `tools/art/out/v3/overview.png`: the three palettes side by side.

`check_sizes_v3.py <dir>` compares a palette directory against `src/ui/images/<name>.c` (read-only);
`make_v3.sh` runs it for all three. Drawn text is limited to <= 5 chars (`MEH`, `NOPE`, `#1`).
Palettes: A alley (amber/orange on blue-grey), B toxic (acid green + hot magenta + cyan on black),
C bloodmoon (crimson + bone + gold eyes on burgundy-black). Nothing here writes to `src/`.
