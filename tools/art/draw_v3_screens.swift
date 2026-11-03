// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// GrumpyKitty v3.1 screen mockups: full 320x240 screens composed with the REAL layout read from
// src/ui/screens/{ui_home,ui_apps_menu,ui_message_box,ui_update,ui_settings}.c (positions, sizes,
// 3-column grid, row pitch 105). Text is approximated with a system font (the device uses
// ui_font_name_14/24). Each screen is written at 1x and 2x (nearest neighbour) plus an RGB565
// version (every pixel truncated to 5/6/5 bits like the converter does, then bit-replicated).
// Compiled with draw.swift + draw_v3.swift by make_v3.sh.

import CoreGraphics
import CoreText
import Foundation

// MARK: text + pixel helpers
func textWidth(_ s: String, _ size: CGFloat, _ font: String) -> CGFloat {
    let f = CTFontCreateWithName(font as CFString, size, nil)
    let attrs: [NSAttributedString.Key: Any] = [NSAttributedString.Key(kCTFontAttributeName as String): f]
    return CTLineGetTypographicBounds(CTLineCreateWithAttributedString(NSAttributedString(string: s, attributes: attrs)), nil, nil, nil)
}
/// LVGL-style label: (x, y) is the top-left of the label box, font height `size`.
func lvLabel(_ c: CGContext, _ s: String, _ x: CGFloat, _ y: CGFloat, _ size: CGFloat, _ color: CGColor = col(0xffffff), font: String = "HelveticaNeue-Medium") {
    label(c, s, x, y + size * 0.92, size, color, font: font)
}
func lvLabelC(_ c: CGContext, _ s: String, cx: CGFloat, _ y: CGFloat, _ size: CGFloat, _ color: CGColor = col(0xffffff), font: String = "HelveticaNeue-Medium", maxW: CGFloat = 86) {
    var t = s
    while textWidth(t, size, font) > maxW && t.count > 2 { t.removeLast() }   // LV_LABEL_LONG_DOT-ish
    if t != s { t.removeLast(); t += "..." }
    lvLabel(c, t, cx - textWidth(t, size, font) / 2, y, size, color, font: font)
}
func screenBase() -> CGContext {
    let c = makeCtx(320, 240)
    fill(c, rectP(0, 0, 320, 240), col(0x000000))
    return c
}
func rotated(_ c: CGContext, _ src: CGContext, _ x: CGFloat, _ y: CGFloat, _ angle: CGFloat) {
    c.saveGState(); c.translateBy(x: x + CGFloat(src.width) / 2, y: y + CGFloat(src.height) / 2); c.rotate(by: angle)
    put(c, src, -CGFloat(src.width) / 2, -CGFloat(src.height) / 2); c.restoreGState()
}
/// Truncate each channel to 5/6/5 bits and expand by bit replication (what the panel shows).
func quant565(_ src: CGContext) -> CGContext {
    let c = makeCtx(src.width, src.height)
    let w = src.width, h = src.height
    let a = src.data!.assumingMemoryBound(to: UInt8.self), b = c.data!.assumingMemoryBound(to: UInt8.self)
    for y in 0..<h { for x in 0..<w {
        let o = y * src.bytesPerRow + x * 4, q = y * c.bytesPerRow + x * 4
        let r = Int(a[o]) >> 3, g = Int(a[o + 1]) >> 2, bl = Int(a[o + 2]) >> 3
        b[q] = UInt8((r << 3) | (r >> 2)); b[q + 1] = UInt8((g << 2) | (g >> 4)); b[q + 2] = UInt8((bl << 3) | (bl >> 2)); b[q + 3] = 255
    } }
    return c
}
func scaled(_ src: CGContext, _ k: Int) -> CGContext {
    let c = makeCtx(src.width * k, src.height * k)
    c.interpolationQuality = .none
    c.saveGState(); c.scaleBy(x: CGFloat(k), y: CGFloat(k))
    c.interpolationQuality = .none
    put(c, src, 0, 0)
    c.restoreGState()
    return c
}

// MARK: screens
let appNames = ["Dino", "Matrix Rain", "VU Meter", "Retro TV", "PC Monitor", "Air Mouse", "BLE Spam", "Bad USB",
                "Infrared", "Media Remote", "Slide Clicker", "Radio Scanner", "Voice Recorder"]

func screenHome(_ p: Pal, _ r: [String: CGContext]) -> CGContext {
    let c = screenBase()
    put(c, r["ui_img_background_h_png"]!, 0, 0)
    put(c, r["ui_img_navigation_center_png"]!, 10, 204)
    put(c, r["ui_img_storage_status_on_png"]!, 10, 10)
    put(c, r["ui_img_power_gauge_png"]!, 267, 10)
    for i in 0..<5 {   // ui_unit images: 5x12 battery segments
        let x = 276 + CGFloat(i) * 6
        fill(c, rectP(x, 15, 5, 12), p.eye); fill(c, rectP(x, 24, 5, 3), mixc(p.eye, p.line, 0.3))
    }
    put(c, r["ui_img_mascot_png"]!, 26, 17)
    lvLabel(c, "100/100", 210, 14, 14, col(0xffffff), font: "Menlo-Bold")
    return c
}

func screenApps(_ p: Pal, _ r: [String: CGContext], scroll: CGFloat, selected: Int) -> CGContext {
    let c = screenBase()
    put(c, r["ui_img_background_v_png"]!, 0, -1)
    rotated(c, r["ui_img_navigation_edge_png"]!, 10, 204, -.pi / 2)
    let names = ["ui_img_dino_png", "ui_img_matrix_rain_png", "ui_img_vu_meter_png", "ui_img_retro_tv_png", "ui_img_pc_montior_png",
                 "ui_img_air_mouse_png", "ui_img_ble_spam_png", "ui_img_badusb_png", "ui_img_infrared_png", "ui_img_mediaremote_png",
                 "ui_img_slideclicker_png", "ui_img_radarscan_png", "ui_img_voicerec_png"]
    let colX: [CGFloat] = [35, 125, 215]
    c.saveGState(); c.addPath(rectP(0, 0, 320, 240)); c.clip()
    for (i, n) in names.enumerated() {
        let bx = colX[i % 3], by = CGFloat(30 + (i / 3) * 105) - scroll
        if by > 240 || by + 100 < 0 { continue }
        // device: radius-35 button (here dark charcoal instead of lavender) with the icon as bg image
        fill(c, ell(bx + 35, by + 35, 35, 35), mixc(p.table, p.bg1, 0.35))
        if i == selected { outline(c, ell(bx + 35, by + 35, 37.5, 37.5), 5, p.accent, 900) }
        put(c, r[n]!, bx, by)
        lvLabelC(c, appNames[i], cx: bx + 35, by + 75, 14)
    }
    c.restoreGState()
    // scrollbar
    let barH: CGFloat = 240 * 240 / 560
    fill(c, rrect(315, 4 + scroll * (230 - barH) / 420, 318, 4 + scroll * (230 - barH) / 420 + barH, 1.5), alpha(p.accent, 0.9))
    return c
}

func screenMessage(_ p: Pal, _ r: [String: CGContext]) -> CGContext {
    let c = screenHome(p, r)
    fill(c, rectP(0, 0, 320, 240), alpha(col(0x000000), 0.6))
    put(c, r["ui_img_message_box_bg_png"]!, 30, 44)
    put(c, r["ui_img_message_box_mascot_png"]!, 166, 88)
    lvLabel(c, "Flash firmware?", 56, 62, 18, col(0xffffff), font: "HelveticaNeue-Bold")
    lvLabel(c, "Nobody asked.", 56, 88, 14)
    for (i, t) in ["YES", "NO."].enumerated() {
        let x = 56 + CGFloat(i) * 52
        let b = rrect(x, 150, x + 46, 174, 12)
        fill(c, b, i == 1 ? p.accent : mixc(p.line, p.bg1, 0.4))
        outline(c, b, 2, i == 1 ? p.line : p.accent, 910 + i)
        lvLabelC(c, t, cx: x + 23, 155, 14, col(0xffffff), font: "HelveticaNeue-Bold")
    }
    return c
}

func screenUpdate(_ p: Pal, _ r: [String: CGContext]) -> CGContext {
    let c = screenBase()
    put(c, r["ui_img_background_png"]!, 0, -1)
    put(c, r["ui_img_mascot_update_png"]!, 14, 22)
    lvLabel(c, "Updateing", 20, 18, 24, col(0xffffff), font: "HelveticaNeue-Bold")
    lvLabel(c, "100%", 175, 173, 24, col(0xffffff), font: "HelveticaNeue-Bold")
    put(c, r["ui_img_progress_bg_png"]!, -18, 168)       // not in ui_update.c: placed under the mascot as a proposal
    put(c, r["ui_img_progress_bar_png"]!, 24, 210)
    return c
}

func screenSettings(_ p: Pal, _ r: [String: CGContext]) -> CGContext {
    let c = screenBase()
    put(c, r["ui_img_background_png"]!, 0, -1)
    put(c, r["ui_img_bar_bg_png"]!, 23, 18)
    put(c, r["ui_img_bar_bg_png"]!, 81, 18)
    for (x, f) in [(42, 0.5), (100, 0.7)] as [(CGFloat, CGFloat)] {   // sliders: white indicator filled from the bottom
        c.saveGState(); c.addPath(rectP(x, 36 + 163 * (1 - f), 39, 163 * f)); c.clip()
        put(c, r["ui_img_slider_png"]!, x, 36); c.restoreGState()
    }
    put(c, r["ui_img_wifi_status_png"]!, 142, 19)
    // ui_settings.c: label pos (42,23) size 106x40 inside the button, ui_font_name_14, scroll-circular
    c.saveGState(); c.addPath(rectP(142 + 42, 19 + 23, 106, 40)); c.clip()
    lvLabel(c, "GrumpyNet  -52 dBm", 142 + 42, 19 + 23, 14)
    c.restoreGState()
    put(c, r["ui_img_button_bg_png"]!, 141, 52)
    put(c, r["ui_img_detailed_settings_png"]!, 172, 100)
    put(c, r["ui_img_bluetooth_png"]!, 172, 149)
    put(c, r["ui_img_usb_msc_png"]!, 224, 100)
    put(c, r["ui_img_info_png"]!, 224, 149)
    put(c, r["ui_img_volume_icon_png"]!, 48, 164)
    put(c, r["ui_img_brightness_icon_png"]!, 107, 164)
    put(c, r["ui_img_navigation_edge_png"]!, 10, 204)
    return c
}

/// ui_wifi.c: background_v, header (10,3) with BLACK 24px label at (80,14), scan switch, list border (10,47).
func screenWifi(_ p: Pal, _ r: [String: CGContext]) -> CGContext {
    let c = screenBase()
    put(c, r["ui_img_background_v_png"]!, 0, -1)
    put(c, r["ui_img_header_png"]!, 10, 3)
    lvLabel(c, "WLAN Setup", 80, 13, 24, col(0x000000), font: "HelveticaNeue-Bold")
    let sw = rrect(10 + 252, 3 + 12, 10 + 252 + 42, 3 + 12 + 22, 11)   // lv_switch, checked
    fill(c, sw, p.line); outline(c, sw, 1.6, p.line, 930)
    fill(c, ell(10 + 252 + 31, 3 + 23, 8, 8), p.bone)
    put(c, r["ui_img_border_png"]!, 10, 47)
    let rows = [("AlleyCat-5G", "-41"), ("FBI Surveillance Van", "-55"), ("dumpster_guest", "-63"), ("NOPE_2.4", "-78")]
    for (i, row) in rows.enumerated() {
        let y = CGFloat(58 + i * 34)
        if i == 1 { fill(c, rrect(22, y - 3, 298, y + 27, 8), alpha(p.accent, 0.9)) }
        lvLabel(c, row.0, 32, y + 3, 18, i == 1 ? col(0x000000) : col(0xffffff))
        lvLabel(c, row.1, 262, y + 5, 14, i == 1 ? col(0x000000) : col(0xffffff))
    }
    lvLabel(c, "A Select    B Back", 171, 210, 14)
    put(c, r["ui_img_navigation_edge_png"]!, 10, 204)
    return c
}

// MARK: output
func renderScreens(_ p: Pal, _ r: [String: CGContext], _ dir: String) {
    let sdir = "\(dir)/screens"
    try? FileManager.default.createDirectory(atPath: sdir, withIntermediateDirectories: true)
    let screens: [(String, CGContext)] = [
        ("home", screenHome(p, r)),
        ("apps_menu", screenApps(p, r, scroll: 0, selected: 1)),
        ("apps_menu_scrolled", screenApps(p, r, scroll: 105, selected: 4)),
        ("apps_menu_peek", screenApps(p, r, scroll: 25, selected: 4)),
        ("message_box", screenMessage(p, r)),
        ("update", screenUpdate(p, r)),
        ("settings", screenSettings(p, r)),
        ("wifi_list", screenWifi(p, r)),
    ]
    var q: [String: CGContext] = [:]
    for (n, s) in screens {
        let s565 = quant565(s)
        save(s, "\(sdir)/\(n)_1x.png"); save(scaled(s, 2), "\(sdir)/\(n)_2x.png")
        save(s565, "\(sdir)/\(n)_rgb565_1x.png"); save(scaled(s565, 2), "\(sdir)/\(n)_rgb565_2x.png")
        q[n] = s565
    }
    // review sheet: the six core screens at 2x RGB565
    let order: [(String, String)] = [("home", "home"), ("apps_menu_peek", "apps menu, dragged 25px (rows 1-2 + row 3 peeking)"), ("message_box", "message box over home"),
                                     ("update", "update (progress bar is a proposal)"), ("settings", "settings"), ("wifi_list", "WLAN list (header + border)")]
    let W = 1320, H = 1560
    let sheet = makeCtx(W, H)
    fill(sheet, rectP(0, 0, CGFloat(W), CGFloat(H)), col(0x0b0b0e))
    label(sheet, "GrumpyKitty v3.1 / \(p.name): real firmware layouts, 2x nearest-neighbour, RGB565 (5/6/5 truncated)", 20, 26, 15, col(0xdddddd), bold: true)
    for (i, o) in order.enumerated() {
        let x = CGFloat(20 + (i % 2) * 650), y = CGFloat(48 + (i / 2) * 500)
        put(sheet, scaled(q[o.0]!, 2), x, y)
        label(sheet, o.1, x, y + 500 - 14, 13, col(0x9aa0ab))
    }
    save(sheet, "\(dir)/review_sheet.png")
}

func mascotsSheet(_ p: Pal, _ r: [String: CGContext]) -> CGContext {
    let W = 1000, H = 730
    let c = makeCtx(W, H)
    fill(c, rectP(0, 0, CGFloat(W), CGFloat(H)), col(0x0b0b0e))
    label(c, "GrumpyKitty v3.1 / \(p.name) mascots (1x, on the wall background)", 14, 20, 13, col(0xdddddd), bold: true)
    let bg = r["ui_img_background_png"]!
    let items: [(String, CGFloat, CGFloat)] = [
        ("ui_img_mascot_png", 14, 34), ("ui_img_mascot_update_png", 300, 34), ("ui_img_mascot_02_png", 604, 34),
        ("ui_img_mascot_recovery_png", 14, 250), ("ui_img_mascot_usb_msc_png", 280, 250), ("ui_img_message_box_mascot_png", 600, 250)]
    for (n, x, y) in items {
        let a = r[n]!
        let tile = makeCtx(a.width, a.height)
        for ty in stride(from: 0, to: a.height, by: 241) { for tx in stride(from: 0, to: a.width, by: 320) { put(tile, bg, CGFloat(tx), CGFloat(ty)) } }
        put(tile, a, 0, 0)
        put(c, tile, x, y)
        label(c, "\(n.dropFirst(7).dropLast(4)) \(a.width)x\(a.height)", x, y + CGFloat(a.height) + 14, 10, col(0x888888))
    }
    // expression strip underneath
    put(c, expressionStrip(p), 14, 490, scale: 0.95)
    return c
}
