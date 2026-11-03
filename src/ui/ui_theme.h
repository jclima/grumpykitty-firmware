// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// GrumpyKitty "alley" theme: the single place to change the UI palette.
// C and C++ compatible (plain #defines only); include it from LVGL screens,
// from LovyanGFX app code and from the TUI helpers. Art colours live in
// tools/art (draw_v3*.swift); keep the two in sync when changing the palette.
#pragma once

// ── 24-bit RGB (0xRRGGBB), for lv_color_hex() and constexpr uint32_t ─────
#define THEME_BG              0x14161C  // screen background (charcoal)
#define THEME_BG_ALT          0x1F232B  // alternate rows / inset panels
#define THEME_SURFACE         0x262A33  // tiles, buttons, cards
#define THEME_SURFACE_ALT     0x30353F  // pressed / raised surface, tracks
#define THEME_BORDER          0x3D4350  // outlines and dividers
#define THEME_ACCENT          0xFFB000  // amber: selection, highlights
#define THEME_ACCENT_HOT      0xFF6A00  // orange: hot / emphasised accent
#define THEME_TEXT            0xF2EDE4  // primary text (warm white)
#define THEME_TEXT_DIM        0x9AA0AB  // secondary text, hints
#define THEME_TEXT_ON_ACCENT  0x14161C  // dark text on amber fills/bitmaps
#define THEME_DANGER          0xD9482B  // errors, destructive actions
#define THEME_OK              0x00CC00  // OK / charging green (unchanged)

// ── RGB565 for LovyanGFX (uint16_t) ──────────────────────────────────────
#define THEME_BG_565              0x10A3  // #14161C
#define THEME_BG_ALT_565          0x1905  // #1F232B
#define THEME_SURFACE_565         0x2146  // #262A33
#define THEME_SURFACE_ALT_565     0x31A7  // #30353F
#define THEME_BORDER_565          0x3A0A  // #3D4350
#define THEME_ACCENT_565          0xFD80  // #FFB000
#define THEME_ACCENT_HOT_565      0xFB40  // #FF6A00
#define THEME_TEXT_565            0xF77C  // #F2EDE4
#define THEME_TEXT_DIM_565        0x9D15  // #9AA0AB
#define THEME_TEXT_ON_ACCENT_565  0x10A3  // #14161C
#define THEME_DANGER_565          0xDA45  // #D9482B
#define THEME_OK_565              0x0660  // #00CC00

// ── LVGL helper (only where lvgl.h is included) ──────────────────────────
// Usage: lv_obj_set_style_bg_color(obj, THEME_LV(THEME_SURFACE), 0);
#define THEME_LV(c) lv_color_hex(c)
