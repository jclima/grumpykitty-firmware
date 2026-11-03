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

## Art v3.1 (mean grumpy alley cat, full 45-asset set, palette A "alley")

    tools/art/make_v3.sh

Redraws the character as a scarred, notch-eared alley cat (angry V brows, half-lidded glare, snarl with
one fang, ragged whiskers, wobbly hand-inked outlines, flat fills with one hard shadow tone). Built from
`draw.swift` + `draw_v3.swift` (cat kit, hero/update mascots, registry) + `draw_v3_icons.swift`
(13 app icons) + `draw_v3_ui.swift` (wall backgrounds, small UI, panels, other mascots) +
`draw_v3_sheets.swift` + `draw_v3_screens.swift` + `main_v3.swift`. Only palette A (`alley`) is rendered by
default; the `toxic` / `bloodmoon` `Pal`s still exist in `draw_v3.swift` (edit `runV3` to render them).
Output: `tools/art/out/v3/alley/<ui_img_variable_name>.png` for all 45 assets at the exact LVGL size, plus:

- `icons_rgb565_3x.png` (icons on the lavender tile, RGB565, 3x), `icons_sheet.png` (13 icons at 1x on lavender / lime / charcoal tiles, then 2x), `mascots_sheet.png`,
  `expression_strip.png` (glare / smug / hiss / side-eye), `props_sheet.png`;
- `screens/` : home, apps menu (unscrolled, one step scrolled, 25px peek), WLAN list, message box, update, settings,
  composed with the real layout read from `src/ui/screens/*.c`, each as `_1x`, `_2x` and `_rgb565_1x/_2x`
  (RGB565 = each channel truncated to 5/6/5 bits, as `png_to_lvgl_c.py` does); `review_sheet.png` shows the
  six core screens (home, apps menu, message box, update, settings, WLAN list) at 2x RGB565.

App icons are full-bleed (no ring or disc): a head crop or a big prop on a dark rounded-square backing
(radius 14, 90% opaque) so they read on any tile colour. Backgrounds are a low-contrast brick wall with a
vignette and an amber rim light, ordered-dithered so RGB565 does not band. Drawn text is limited to
<= 5 chars (`MEH`, `NOPE`, `NO`, `MSC`). `check_sizes_v3.py <dir>` compares a palette directory against
`src/ui/images/<name>.c` (read-only; `make_v3.sh` runs it). Nothing here writes to `src/`.

## Boot animation (GrumpyKitty alley cat)

    tools/art/make_boot_gif.sh

Builds `draw.swift` + `draw_v3*.swift` + `boot_gif.swift`, renders 23 opaque 320x240 frames and writes
`tools/art/out/v3/alley/boot_animation.gif` (ImageIO; 3160 ms in total, delays 7-95 cs, so 12-15 fps while it moves)
plus `boot_contact_sheet.png` (12 evenly spaced frames read back from the GIF). Choreography: black alley, amber eyes
open in the dark, light comes up on the half-asleep cat, slow blink, yawn, hiss, `MEH` bubble pops, `GrumpyKitty`
wordmark (Impact, bone + amber) slams in, 0.95 s hold on a frame that matches the home screen (same wall, cat and bubble).
The wall is flat/posterised (no grit or dither) so the 256-colour palette does not band and LZW stays tiny.
ImageIO writes a global palette and frame-delta images (transparent index 255, disposal 1, no loop extension, like the
original); `src/splash/splash_screen.cpp` draws each line straight to the LCD and skips transparent pixels, so this is
correct there. Beats and timings are the `beats` table in `boot_gif.swift`. The boot sound is trimmed to the GIF duration
by `tools/embed_gif.py`, so keep the total near 3 s. Deploying is a manual copy:

    cp tools/art/out/v3/alley/boot_animation.gif src/system/assets/boot_animation.gif

PlatformIO does not track the `.incbin` GIF as a dependency of `splash_gif_data.S`: after replacing the GIF delete
`.pio/build/<env>/src/splash/splash_gif_data.S.o` (or `make clean`) before `make build`, otherwise the firmware
carries the old bytes with the new length.
