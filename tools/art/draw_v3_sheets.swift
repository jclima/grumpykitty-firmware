// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// GrumpyKitty v3.1 review sheets: icons sheet (1x on several tile colours + 3x), mascots sheet.
// Compiled with draw.swift + draw_v3.swift by make_v3.sh.

import CoreGraphics
import Foundation

let iconOrder: [(String, String)] = [
    ("ui_img_dino_png", "dino"), ("ui_img_matrix_rain_png", "matrix"), ("ui_img_vu_meter_png", "vu"),
    ("ui_img_retro_tv_png", "tv"), ("ui_img_pc_montior_png", "pc"), ("ui_img_air_mouse_png", "mouse"),
    ("ui_img_ble_spam_png", "ble"), ("ui_img_badusb_png", "badusb"), ("ui_img_infrared_png", "ir"),
    ("ui_img_mediaremote_png", "media"), ("ui_img_slideclicker_png", "slide"), ("ui_img_radarscan_png", "radar"),
    ("ui_img_voicerec_png", "voice"),
]

func iconsSheet(_ p: Pal, _ r: [String: CGContext]) -> CGContext {
    let W = 1200, H = 760
    let c = makeCtx(W, H)
    fill(c, rectP(0, 0, CGFloat(W), CGFloat(H)), col(0x0b0b0e))
    label(c, "GrumpyKitty v3.1 / \(p.name) app icons: 1x on lavender / lime / charcoal tiles (as the device draws them), then 2x", 14, 20, 13, col(0xdddddd), bold: true)
    let tiles: [CGColor] = [col(0xB9A6FF), col(0xBCE400), p.table]
    for (row, t) in tiles.enumerated() {
        for (i, io) in iconOrder.enumerated() {
            let x = CGFloat(14 + i * 90), y = CGFloat(34 + row * 84)
            fill(c, ell(x + 35, y + 35, 35, 35), t)    // device: radius-35 button, icon as bg image
            put(c, r[io.0]!, x, y)
        }
    }
    for (i, io) in iconOrder.enumerated() {
        let col3 = i % 6, row3 = i / 6
        let x = CGFloat(14 + col3 * 196), y = CGFloat(300 + row3 * 150)
        fill(c, rrect(x - 4, y - 4, x + 190, y + 144, 10), col(0x24262e))
        put(c, r[io.0]!, x, y, scale: 2.0)
        label(c, io.1, x + 146, y + 14, 12, col(0xcccccc))
    }
    return c
}

/// What the device really shows: icons on the lavender radius-35 tile, RGB565-truncated, nearest-neighbour 3x.
func iconsRGB565Sheet(_ p: Pal, _ r: [String: CGContext]) -> CGContext {
    let k = 3, cell = 80 * k
    let W = 7 * cell + 20, H = 2 * (cell + 26) + 30
    let c = makeCtx(W, H)
    fill(c, rectP(0, 0, CGFloat(W), CGFloat(H)), col(0x1c1c20))
    label(c, "app icons as drawn on the lavender tile, RGB565, nearest-neighbour 3x", 10, 18, 12, col(0xdddddd), bold: true)
    for (i, io) in iconOrder.enumerated() {
        let t = makeCtx(80, 80)
        fill(t, rectP(0, 0, 80, 80), col(0xB9A6FF))   // flat lavender behind the tile (dark corners avoided: tile is a circle, bg is lavender here)
        put(t, r[io.0]!, 5, 5)
        let q = scaled(quant565(t), k)
        put(c, q, CGFloat(10 + (i % 7) * cell), CGFloat(28 + (i / 7) * (cell + 26)))
        label(c, io.1, CGFloat(14 + (i % 7) * cell), CGFloat(28 + (i / 7) * (cell + 26) + cell + 14), 11, col(0xcccccc))
    }
    return c
}
