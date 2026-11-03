// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// GrumpyKitty v3.1: textured alley-wall backgrounds (ordered-dithered for RGB565), small UI
// icons and panels (paw / claw / cat-eye motifs, same geometry as the reference firmware art),
// and the secondary mascots. Compiled with draw.swift + draw_v3.swift by make_v3.sh.

import CoreGraphics
import Foundation

// MARK: textured background
func bayerAt(_ x: Int, _ y: Int) -> CGFloat {
    let m: [[Int]] = [
        [0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26], [12, 44, 4, 36, 14, 46, 6, 38], [60, 28, 52, 20, 62, 30, 54, 22],
        [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25], [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21]]
    return (CGFloat(m[y & 7][x & 7]) + 0.5) / 64
}

/// Ordered dither over an opaque context so the device's RGB565 truncation shows texture, not bands.
func ditherTrunc(_ c: CGContext) {
    let w = c.width, h = c.height, bpr = c.bytesPerRow
    let buf = c.data!.assumingMemoryBound(to: UInt8.self)
    for y in 0..<h { for x in 0..<w {
        let o = y * bpr + x * 4
        let u = bayerAt(x, y)
        for (ch, step) in [(0, 8.0), (1, 4.0), (2, 8.0)] as [(Int, CGFloat)] {
            buf[o + ch] = UInt8(max(0, min(255, (CGFloat(buf[o + ch]) + u * step).rounded(.down))))
        }
    } }
}

/// Brick alley wall: low-contrast bricks and mortar, grime, warm amber rim light from `light`, vignette.
func wallBackground(_ p: Pal, w: Int, h: Int, light: CGPoint, seed: Int, ring: Bool = false) -> CGContext {
    let c = makeCtx(w, h)
    let base = mixc(p.bg0, p.bg1, 0.55).components!
    let rust = p.danger.components!
    let amber = p.accent.components!
    let buf = c.data!.assumingMemoryBound(to: UInt8.self), bpr = c.bytesPerRow
    let bw = 36, bh = 16
    let reach: CGFloat = 250
    for y in 0..<h { for x in 0..<w {
        let row = y / bh
        let xx = x + ((row % 2 == 0) ? 0 : bw / 2) + (row * 5) % 7
        let lx = xx % bw, ly = y % bh
        let id = (xx / bw) * 131 + row * 17 + seed
        var v: CGFloat = hn(id, 1) * 0.016                       // per-brick lightness
        let rustMix: CGFloat = hn(id, 2) > 0.55 ? 0.05 : 0
        if lx < 2 || ly < 2 { v -= 0.04 }                       // mortar
        else if ly == 2 { v += 0.028 } else if lx == 2 { v += 0.014 }   // lit top/left edge
        else if ly == bh - 1 { v -= 0.014 }
        v += hn(x * 7 + seed, y * 13) * 0.011                   // grit
        // chipped bricks
        if lx > 4 && ly > 4 && hn(id, 3) > 0.9 && (lx + ly) % 11 < 3 { v -= 0.03 }
        let d = hypot(CGFloat(x) - light.x, CGFloat(y) - light.y)
        let l = pow(max(0, 1 - d / reach), 2.0)
        let nx = (CGFloat(x) / CGFloat(w) - 0.5) * 2, ny = (CGFloat(y) / CGFloat(h) - 0.5) * 2
        let vg = 1 - 0.34 * (nx * nx + ny * ny) / 2
        var rgb: [CGFloat] = [0, 0, 0]
        for ch in 0..<3 {
            var s = base[ch] + v * (1 + 2.2 * l) + rust[ch] * rustMix * 0.25
            s *= vg
            s += amber[ch] * l * 0.17
            rgb[ch] = max(0, min(1, s))
        }
        let o = y * bpr + x * 4
        buf[o] = UInt8(rgb[0] * 255 + 0.5); buf[o + 1] = UInt8(rgb[1] * 255 + 0.5); buf[o + 2] = UInt8(rgb[2] * 255 + 0.5); buf[o + 3] = 255
    } }
    // drips / grime streaks
    for i in 0..<9 {
        let sx = CGFloat((i * 53 + seed * 7) % w), sy = CGFloat((i * 31) % 60)
        inkLine(c, [pt(sx, sy), pt(sx + hn(i, 4) * 3, sy + 40 + CGFloat((i * 17) % 70))], 3 + hn(i, 5), alpha(p.line, 0.13), 300 + i, amp: 1.0)
    }
    // faint claw-scratch groups
    let groups: [(CGFloat, CGFloat, CGFloat)] = [(34, 40, 0.5), (236, 120, -0.2), (118, 176, 0.2)]
    for (gi, g) in groups.enumerated() {
        for k in 0..<4 {
            let o = CGFloat(k) * 6
            let a = pt(g.0 + o, g.1 + o * g.2), b = pt(g.0 + o + 24 + hn(k, gi) * 6, g.1 + 48 + hn(k, gi + 9) * 8)
            inkCurve(c, mk { $0.move(to: a); $0.addQuadCurve(to: b, control: pt((a.x + b.x) / 2 + 4, (a.y + b.y) / 2)) }, 1.7 - CGFloat(k) * 0.2, alpha(p.bone, 0.055), 360 + gi * 5 + k, amp: 0.4)
        }
    }
    if ring {   // clock: a faint dial ring
        outline(c, ell(CGFloat(w) / 2, CGFloat(h) / 2, 96, 96), 3, alpha(p.accent, 0.12), 380)
        outline(c, ell(CGFloat(w) / 2, CGFloat(h) / 2, 88, 88), 1.4, alpha(p.bone, 0.06), 381)
    }
    ditherTrunc(c)
    return c
}
func v3Background(_ p: Pal) -> CGContext { wallBackground(p, w: 320, h: 241, light: pt(322, -4), seed: 1) }
func v3BackgroundH(_ p: Pal) -> CGContext { wallBackground(p, w: 320, h: 241, light: pt(-2, 244), seed: 2) }
func v3BackgroundV(_ p: Pal) -> CGContext { wallBackground(p, w: 320, h: 241, light: pt(-2, -2), seed: 3) }
func v3ClockBg(_ p: Pal) -> CGContext { wallBackground(p, w: 321, h: 241, light: pt(323, 244), seed: 4, ring: true) }

// MARK: small UI helpers
func uiDisc(_ p: Pal) -> CGColor { mixc(p.line, p.bg1, 0.45) }
func softShadow(_ c: CGContext, _ r: CGRect, _ rad: CGFloat, _ spread: CGFloat, _ a: CGFloat, dy: CGFloat = 3) {
    for i in 0..<8 {
        let g = spread * CGFloat(8 - i) / 8
        fill(c, rrect(r.minX - g, r.minY - g + dy, r.maxX + g, r.maxY + g + dy, rad + g), alpha(col(0x000000), a * 0.16))
    }
}
func catEars(_ c: CGContext, _ p: Pal, cx: CGFloat, top: CGFloat, spread: CGFloat, fillc: CGColor, seed: Int) {
    for sx in [-1.0, 1.0] as [CGFloat] {
        let pts = [pt(cx + sx * (spread + 1), top + 12), pt(cx + sx * (spread - 1), top), pt(cx + sx * (spread - 12), top + 6)]
        solid(c, p, poly(pts), fillc, lw: 1.8, seed: seed + Int(sx), amp: 0.05)
    }
}
func roundBtn(_ p: Pal, on: Bool, seed: Int, _ glyph: (CGContext, CGColor) -> Void) -> CGContext {
    let c = makeCtx(42, 43)
    let fillc = on ? p.eye : uiDisc(p)
    let ink = on ? p.line : p.eye
    catEars(c, p, cx: 21, top: 1.5, spread: 15, fillc: fillc, seed: seed)
    let d = ell(21, 24.5, 17.6, 17.6)
    solid(c, p, d, fillc, shade: on ? mixc(p.eye, p.line, 0.3) : nil, lw: 2.0, seed: seed + 3, amp: 0.1, d: 3)
    if !on { outline(c, wob(ell(21, 24.5, 15.2, 15.2), 0.1, seed + 4), 1.0, alpha(p.eye, 0.35), seed + 4) }
    glyph(c, ink)
    return c
}
func btRune(_ c: CGContext, _ ink: CGColor, _ seed: Int) {
    let pts = [pt(14.5, 18.5), pt(27.5, 30.5), pt(21, 37), pt(21, 12), pt(27.5, 18.5), pt(14.5, 30.5)]
    c.saveGState(); c.setLineCap(.round); c.setLineJoin(.round); c.setStrokeColor(ink); c.setLineWidth(2.8)
    c.addLines(between: pts); c.strokePath(); c.restoreGState()
}
func catEye(_ c: CGContext, _ p: Pal, _ x: CGFloat, _ y: CGFloat, _ rx: CGFloat, _ ry: CGFloat, _ eyec: CGColor, _ slit: CGColor) {
    fill(c, ell(x, y, rx, ry), eyec)
    fill(c, ell(x, y, rx * 0.28, ry * 0.92), slit)
}

func v3Bluetooth(_ p: Pal) -> CGContext { roundBtn(p, on: false, seed: 400) { c, ink in btRune(c, ink, 1) } }
func v3BluetoothOn(_ p: Pal) -> CGContext { roundBtn(p, on: true, seed: 410) { c, ink in btRune(c, ink, 2) } }
func v3Info(_ p: Pal) -> CGContext {
    roundBtn(p, on: false, seed: 420) { c, ink in
        outline(c, ell(21, 24.5, 11.5, 11.5), 2.2, p.eye, 421)
        catEye(c, p, 21, 17.6, 3.6, 3.0, p.eye, uiDisc(p))
        fill(c, rrect(19.1, 22.5, 22.9, 31.5, 1.6), p.bone)
    }
}
func v3Detailed(_ p: Pal) -> CGContext {
    roundBtn(p, on: false, seed: 430) { c, ink in
        let hex = poly((0..<6).map { i in let a = CGFloat(i) * .pi / 3 + .pi / 6; return pt(21 + cos(a) * 13.4, 24.5 + sin(a) * 13.4) })
        outline(c, hex, 2.8, p.eye, 431)
        catEye(c, p, 21, 24.5, 6.6, 4.8, p.eye, uiDisc(p))
    }
}
func v3UsbMsc(_ p: Pal) -> CGContext {
    roundBtn(p, on: false, seed: 440) { c, ink in
        outline(c, rrect(14, 20, 28, 36, 4), 2.6, p.eye, 441)
        outline(c, rectP(17, 12, 8, 8.5), 2.2, p.bone, 442)
        fill(c, rectP(18.8, 14, 1.8, 3), p.bone); fill(c, rectP(21.8, 14, 1.8, 3), p.bone)
        inkLine(c, [pt(17, 27), pt(25, 33)], 1.6, p.eye, 443, amp: 0)
    }
}
func v3SdNull(_ p: Pal) -> CGContext { sdIcon(p, on: false) }
func v3SdOn(_ p: Pal) -> CGContext { sdIcon(p, on: true) }
func sdIcon(_ p: Pal, on: Bool) -> CGContext {
    let c = makeCtx(20, 21)
    let card = poly([pt(1.5, 1.5), pt(13.5, 1.5), pt(18.5, 6.5), pt(18.5, 19.5), pt(1.5, 19.5)])
    solid(c, p, card, on ? p.eye : p.belly, lw: 1.5, seed: 450, amp: 0.02)
    for x in [4.5, 7.5, 10.5] as [CGFloat] { fill(c, rectP(x, 3.5, 1.7, 4.4), p.line) }
    fill(c, rectP(9.5, 11, 10.5, 10), p.line)
    c.saveGState(); c.setStrokeColor(on ? p.eye : p.danger); c.setLineWidth(1.8); c.setLineCap(.round)
    if on { c.move(to: pt(11.6, 16.2)); c.addLine(to: pt(13.6, 18.2)); c.addLine(to: pt(17.6, 13.4)) }
    else { c.move(to: pt(11.8, 13.4)); c.addLine(to: pt(17.4, 18.8)); c.move(to: pt(17.4, 13.4)); c.addLine(to: pt(11.8, 18.8)) }
    c.strokePath(); c.restoreGState()
    return c
}
func v3PowerGauge(_ p: Pal) -> CGContext {
    let c = makeCtx(43, 21)
    fill(c, rrect(0, 6.5, 3.6, 14.5, 1.4), p.eye)
    let body = rrect(2.2, 1.1, 41.9, 19.9, 4.5)
    c.saveGState(); c.addPath(body); c.setStrokeColor(p.eye); c.setLineWidth(2.2); c.strokePath(); c.restoreGState()
    return c
}
func v3Volume(_ p: Pal) -> CGContext {
    let c = makeCtx(28, 28)
    solid(c, p, ell(14, 14, 13, 13), uiDisc(p), lw: 1.4, seed: 460, amp: 0.05)
    fill(c, poly([pt(6, 11.5), pt(10.5, 11.5), pt(15.5, 7), pt(15.5, 21), pt(10.5, 16.5), pt(6, 16.5)]), p.eye)
    for (i, r) in [4.0, 7.2].enumerated() { outline(c, mk { $0.addArc(center: pt(16, 14), radius: CGFloat(r), startAngle: -0.8, endAngle: 0.8, clockwise: false) }, 1.7, p.eye, 461 + i) }
    return c
}
func v3Brightness(_ p: Pal) -> CGContext {
    let c = makeCtx(27, 28)
    solid(c, p, ell(13.5, 14, 13, 13), uiDisc(p), lw: 1.4, seed: 470, amp: 0.05)
    for i in 0..<8 {
        let a = CGFloat(i) * .pi / 4
        inkLine(c, [pt(13.5 + cos(a) * 7.6, 14 + sin(a) * 7.6), pt(13.5 + cos(a) * 10.4, 14 + sin(a) * 10.4)], 1.7, p.eye, 471 + i, amp: 0)
    }
    catEye(c, p, 13.5, 14, 5.4, 5.4, p.eye, uiDisc(p))
    return c
}
func navDots(_ p: Pal, active: Int, seed: Int) -> CGContext {
    let c = makeCtx(26, 27)
    let pos = [pt(13, 13.5), pt(13, 3.6), pt(3.6, 13.5), pt(22.4, 13.5), pt(13, 23.4)]
    for (i, q) in pos.enumerated() {
        if i == active {
            solid(c, p, ell(q.x, q.y, 3.4, 3.4), p.accent, lw: 1.1, seed: seed + i, amp: 0.02)
        } else {
            solid(c, p, ell(q.x, q.y, 3.1, 3.1), mixc(p.belly, p.line, 0.15), lw: 1.0, seed: seed + i, amp: 0.02)
        }
    }
    return c
}
func v3NavCenter(_ p: Pal) -> CGContext { navDots(p, active: 0, seed: 480) }
func v3NavEdge(_ p: Pal) -> CGContext { navDots(p, active: 1, seed: 490) }
func v3Switch(_ p: Pal) -> CGContext {
    let c = makeCtx(38, 29)
    let xs: [(CGFloat, CGFloat)] = [(0, 12), (13, 24), (26, 37)]
    let ys: [(CGFloat, CGFloat)] = [(0, 8), (10, 18), (20, 28)]
    for (r, y) in ys.enumerated() { for (cl, x) in xs.enumerated() {
        let last = r == 2 && cl == 2
        let f = last ? p.bone : p.eye
        fill(c, rrect(x.0 + 0.5, y.0 + 0.5, x.1 + 0.5, y.1 + 0.5, 2.2), f)
        fill(c, rectP(x.0 + 0.5, y.1 - 1.8, x.1 - x.0, 2.3), mixc(f, p.line, 0.28))
    } }
    inkLine(c, [pt(27.5, 21.5), pt(32.5, 25.5)], 1.2, p.line, 500, amp: 0)   // claw nick on one tile
    return c
}
func v3Slider(_ p: Pal) -> CGContext {
    let c = makeCtx(39, 163)
    let b = rrect(0, 0, 39, 163, 12)
    fill(c, b, p.bone)
    c.saveGState(); c.addPath(b); c.clip()
    fill(c, rectP(28, 0, 11, 163), p.boneShade)
    for i in 0..<3 { inkLine(c, [pt(9 + CGFloat(i) * 6, 14), pt(5 + CGFloat(i) * 6, 52)], 1.4, alpha(p.line, 0.28), 510 + i, amp: 0.4) }
    for i in 0..<5 { fill(c, rectP(8, 20 + CGFloat(i) * 5, 0, 0), p.line) }
    c.restoreGState()
    return c
}
func v3BarBg(_ p: Pal) -> CGContext {
    let c = makeCtx(77, 202)
    let r = CGRect(x: 19, y: 18, width: 39, height: 163)
    softShadow(c, r, 12, 11, 0.9, dy: 2)
    let b = rrect(19, 18, 58, 181, 12)
    fill(c, b, mixc(p.line, p.table, 0.62))
    c.saveGState(); c.addPath(b); c.clip()
    fill(c, rectP(19, 18, 39, 6), alpha(p.bone, 0.06))
    for i in 0..<3 { inkLine(c, [pt(44 + CGFloat(i) * 5, 120), pt(36 + CGFloat(i) * 5, 170)], 1.3, alpha(p.accent, 0.22), 520 + i, amp: 0.4) }
    c.restoreGState()
    outline(c, wob(rrect(19.8, 18.8, 57.2, 180.2, 11.5), 0.15, 523), 1.6, alpha(p.accent, 0.5), 523)
    return c
}
func v3ButtonBg(_ p: Pal) -> CGContext {
    let c = makeCtx(154, 193)
    let r = CGRect(x: 17, y: 29, width: 120, height: 135)
    softShadow(c, r, 16, 11, 0.9, dy: 2)
    let b = rrect(17, 29, 137, 164, 16)
    fill(c, b, mixc(p.line, p.table, 0.7))
    c.saveGState(); c.addPath(b); c.clip()
    fill(c, rectP(17, 29, 120, 5), alpha(p.bone, 0.06))
    for i in 0..<3 { inkCurve(c, mk { $0.move(to: pt(100 + CGFloat(i) * 8, 126)); $0.addQuadCurve(to: pt(128 + CGFloat(i) * 8, 160), control: pt(114 + CGFloat(i) * 8, 140)) }, 1.8, alpha(p.accent, 0.22), 530 + i) }
    c.restoreGState()
    outline(c, wob(rrect(18.2, 30.2, 135.8, 162.8, 15), 0.2, 533), 2.2, alpha(p.accent, 0.55), 533)
    return c
}
func v3WifiStatus(_ p: Pal) -> CGContext {
    let c = makeCtx(155, 80)
    softShadow(c, CGRect(x: 17, y: 20, width: 120, height: 38), 10, 9, 0.8, dy: 2)
    let card = wob(rrect(17, 20, 137, 58, 10), 0.2, 540)
    fill(c, card, p.eye)
    band(c, card, mixc(p.eye, p.line, 0.35), 0, 4)
    outline(c, card, 2.0, p.line, 540)
    // wifi glyph disc (left)
    let disc = ell(30, 39, 11.5, 11.5)
    solid(c, p, disc, uiDisc(p), lw: 1.8, seed: 541, amp: 0.05)
    for (i, r) in [4.0, 7.0, 10.0].enumerated() {
        outline(c, mk { $0.addArc(center: pt(30, 44.5), radius: CGFloat(r), startAngle: -2.35, endAngle: -0.8, clockwise: false) }, 1.8, p.eye, 542 + i)
    }
    fill(c, ell(30, 44.5, 1.7, 1.7), p.eye)
    // text slot (right)
    let slot = rrect(43, 22.5, 133, 54.5, 14)
    fill(c, slot, mixc(p.line, p.bg1, 0.4))
    band(c, slot, alpha(col(0x000000), 0.6), 0, 4)
    outline(c, wob(slot, 0.1, 545), 1.6, p.line, 545)
    return c
}
func v3Border(_ p: Pal) -> CGContext {
    let c = makeCtx(300, 152)
    let r = wob(rrect(1.5, 1.5, 298.5, 150.5, 22), 0.3, 550)
    outline(c, r, 3.0, p.accent, 550)
    // claw rake across the top-right corner, tucked inside the frame
    for i in 0..<3 { let o = CGFloat(i) * 5; inkCurve(c, mk { $0.move(to: pt(262 + o, 6)); $0.addQuadCurve(to: pt(292 + o * 0.5, 36), control: pt(280 + o, 14)) }, 1.6, alpha(p.accent, 0.8), 551 + i, amp: 0.2) }
    return c
}
func v3ProgressBg(_ p: Pal) -> CGContext {
    let c = makeCtx(280, 106)
    softShadow(c, CGRect(x: 37, y: 37, width: 206, height: 32), 16, 16, 0.85, dy: 3)
    let pill = rrect(37, 37, 243, 69, 16)
    fill(c, pill, mixc(p.line, p.table, 0.65))
    outline(c, wob(rrect(38.6, 38.6, 241.4, 67.4, 14.4), 0.15, 560), 3.0, p.bone, 560)
    return c
}
func v3ProgressBar(_ p: Pal) -> CGContext {
    let c = makeCtx(196, 22)
    let b = rrect(0, 0, 196, 22, 11)
    fill(c, b, p.eye)
    c.saveGState(); c.addPath(b); c.clip()
    fill(c, rectP(0, 14, 196, 8), mixc(p.eye, p.accent, 0.8))
    fill(c, rectP(0, 2, 196, 3), alpha(p.bone, 0.45))
    var x: CGFloat = -10
    while x < 200 { inkLine(c, [pt(x, 22), pt(x + 14, 0)], 3.0, alpha(p.line, 0.16), 570, amp: 0); x += 22 }   // claw slashes
    c.restoreGState()
    return c
}

// MARK: secondary mascots
func v3Mascot02(_ p: Pal) -> CGContext {   // 147x103: bread loaf with a door-hanger
    let c = makeCtx(147, 103)
    let lw: CGFloat = 2.6
    solid(c, p, rrect(-6, 98, 160, 106, 3), p.table, lw: lw, seed: 600, amp: 0.2)
    // tail peeking out the right side
    kArm(c, p, mk { $0.move(to: pt(100, 92)); $0.addCurve(to: pt(128, 74), control1: pt(120, 98), control2: pt(124, 90)) }, w: 8, lw: lw, seed: 601)
    // loaf body
    var body: [CGPoint] = []
    for i in 0..<20 { let a = CGFloat(i) / 20 * 2 * .pi; body.append(pt(58 + cos(a) * 54 * ((i % 3 == 0) ? 1.04 : 0.97), 80 + sin(a) * 22 * (sin(a) > 0 ? 0.85 : 1))) }
    solid(c, p, poly(chaikin(body)), p.fur, shade: p.furShade, rim: p.furRim, lw: lw, seed: 602, amp: 0.6, d: 6)
    inkCurve(c, mk { $0.move(to: pt(86, 70)); $0.addQuadCurve(to: pt(96, 92), control: pt(100, 78)) }, 2.0, alpha(p.line, 0.7), 603)
    kHead(c, p, 50, 54, 0.72, Mood(lidL: 0.6, lidR: 0.62, tilt: 5, browL: 0, browR: 0, kL: 1.1, kR: 1.0, mouth: 3), lw: 3.4, seed: 5)
    // tucked paws
    for x in [28.0, 50.0] as [CGFloat] { solid(c, p, rrect(x, 88, x + 17, 99, 5), p.fur, shade: p.furShade, lw: 2.2, seed: 604 + Int(x), amp: 0.1, d: 2) }
    // door hanger hooked on the ear
    c.saveGState(); c.translateBy(x: 108, y: 26); c.rotate(by: 0.12)
    inkLine(c, [pt(-4, -20), pt(-30, 6)], 2.0, p.bone, 606, amp: 0.1)
    solid(c, p, rrect(-8, -5, 36, 58, 7), p.bone, shade: p.boneShade, lw: 2.6, seed: 607, amp: 0.15, d: 4)
    fill(c, ell(14, 5, 5, 5), p.line)
    shout(c, "NO", 14, 30, 22, 0, p.danger, p.line, 0.6, skew: 0.08)
    inkLine(c, [pt(-2, 42), pt(30, 42)], 2.0, p.danger, 608, amp: 0.1)
    c.restoreGState()
    return c
}

func v3MessageBoxMascot(_ p: Pal) -> CGContext {   // 106x93: peeking from a dumpster
    let c = makeCtx(106, 93)
    let lw: CGFloat = 2.6
    // lid propped open behind
    solid(c, p, poly([pt(6, 42), pt(20, 16), pt(88, 14), pt(98, 40)]), mixc(p.lid, p.fur, 0.45), shade: mixc(p.lid, p.line, 0.55), lw: lw, seed: 620, amp: 0.2, d: 5)
    kHead(c, p, 53, 36, 0.62, .glare, lw: 3.9, full: false, seed: 6)
    // dumpster front
    let front = poly([pt(2, 50), pt(104, 50), pt(98, 92), pt(8, 92)])
    solid(c, p, front, mixc(p.lid, p.fur, 0.2), shade: mixc(p.lid, p.line, 0.55), rim: mixc(p.lid, p.bone, 0.2), lw: lw, seed: 621, amp: 0.2, d: 6)
    solid(c, p, rrect(0, 45, 106, 55, 3), mixc(p.lid, p.bone, 0.1), shade: mixc(p.lid, p.line, 0.5), lw: lw, seed: 622, amp: 0.15, d: 3)
    for x in [28.0, 53.0, 78.0] as [CGFloat] { inkLine(c, [pt(x, 60), pt(x + (x - 53) * 0.12, 88)], 2.0, alpha(p.line, 0.55), 623 + Int(x), amp: 0.2) }
    fill(c, rectP(10, 78, 16, 4), p.accent)
    // paws on the rim + fish bone
    for x in [30.0, 76.0] as [CGFloat] {
        solid(c, p, rrect(x - 8, 41, x + 8, 51, 5), p.fur, shade: p.furShade, lw: 2.2, seed: 624 + Int(x), amp: 0.1, d: 2)
        for k in [-4.0, 0.0, 4.0] as [CGFloat] { inkLine(c, [pt(x + k, 41), pt(x + k, 38)], 1.8, p.bone, 625, amp: 0) }
    }
    fishBone(c, 94, 40, 0.5, p.bone, rot: 0.3)
    return c
}

func v3MascotRecovery(_ p: Pal) -> CGContext {   // 254x129: tangled in cut cables
    let c = makeCtx(254, 129)
    let lw: CGFloat = 2.8
    solid(c, p, rrect(-6, 118, 260, 134, 3), p.table, lw: lw, seed: 640, amp: 0.2)
    // cut cables trailing on the floor to the right, with sparks and a plug
    let cab: [(CGColor, [CGPoint])] = [
        (p.danger, [pt(150, 112), pt(190, 118), pt(222, 108), pt(240, 112)]),
        (p.info, [pt(146, 100), pt(186, 106), pt(214, 98), pt(236, 94)]),
        (p.bone, [pt(150, 90), pt(184, 96), pt(206, 84), pt(226, 78)])]
    for (i, cc) in cab.enumerated() {
        let pth = mk { $0.move(to: cc.1[0]); $0.addCurve(to: cc.1[3], control1: cc.1[1], control2: cc.1[2]) }
        thickLine(c, wob(pth, 0.5, 641 + i), 9, 5.2, p.line, cc.0)
        let e = cc.1[3]
        for k in 0..<3 { inkLine(c, [e, pt(e.x + 8 + CGFloat(k) * 2, e.y - 5 + CGFloat(k) * 5)], 1.8, k == 1 ? p.eye : p.bone, 650 + i * 3 + k, amp: 0.1) }
    }
    sparkle(c, 236, 70, 7, p.eye, 1.4); sparkle(c, 246, 100, 5, p.bone, 1.2)
    // body + tangled cables over it
    var body: [CGPoint] = []
    for i in 0..<22 { let a = CGFloat(i) / 22 * 2 * .pi; body.append(pt(86 + cos(a) * 66 * ((i % 3 == 0) ? 1.04 : 0.97), 98 + sin(a) * 24 * ((i % 3 == 0) ? 1.04 : 0.97))) }
    solid(c, p, poly(chaikin(body)), p.fur, shade: p.furShade, rim: p.furRim, lw: lw, seed: 660, amp: 0.6, d: 6)
    kHead(c, p, 84, 52, 0.72, Mood(lidL: 0.4, lidR: 0.46, tilt: 5, browL: 0, browR: 0, kL: 1.1, kR: 0.95, pupil: pt(-3, -1), mouth: 3), lw: 3.8, seed: 7)
    let wraps: [(CGColor, CGPath)] = [
        (p.danger, mk { $0.move(to: pt(30, 84)); $0.addCurve(to: pt(150, 112), control1: pt(70, 112), control2: pt(110, 80)) }),
        (p.info, mk { $0.move(to: pt(26, 104)); $0.addCurve(to: pt(146, 100), control1: pt(60, 72), control2: pt(110, 126)) }),
        (p.bone, mk { $0.move(to: pt(42, 92)); $0.addCurve(to: pt(150, 90), control1: pt(70, 124), control2: pt(112, 96)) })]
    for (i, w) in wraps.enumerated() { thickLine(c, wob(w.1, 0.6, 670 + i), 8.5, 4.6, p.line, w.0) }
    // loop round the neck
    thickLine(c, wob(mk { $0.addArc(center: pt(84, 82), radius: 26, startAngle: 0.35, endAngle: 2.8, clockwise: false) }, 0.5, 675), 8.5, 4.6, p.line, p.eye)
    // scissors, open, on the floor
    c.saveGState(); c.translateBy(x: 28, y: 114); c.rotate(by: -0.12)
    for sgn in [-1.0, 1.0] as [CGFloat] {
        solid(c, p, poly([pt(0, 0), pt(30, sgn * 7), pt(30, sgn * 3.5)]), mixc(p.belly, p.bone, 0.5), lw: 1.8, seed: 680 + Int(sgn), amp: 0.05)
        outline(c, ell(-8, sgn * 6, 5.5, 5.5), 2.6, p.accent, 682 + Int(sgn))
    }
    c.restoreGState()
    return c
}

func v3MascotUsbMsc(_ p: Pal) -> CGContext {   // 299x206: cat sitting in a box, glaring, with a USB drive
    let c = makeCtx(299, 206)
    let lw: CGFloat = 3.0
    let card = mixc(mixc(p.accent, p.line, 0.62), p.bone, 0.08)
    let cardShade = mixc(card, p.line, 0.5)
    solid(c, p, rrect(-6, 198, 306, 214, 4), p.table, lw: lw, seed: 700, amp: 0.2)
    // back flaps
    solid(c, p, poly([pt(60, 118), pt(46, 74), pt(120, 86), pt(130, 120)]), mixc(card, p.line, 0.25), lw: lw, seed: 701, amp: 0.3, d: 5)
    solid(c, p, poly([pt(232, 118), pt(252, 70), pt(176, 86), pt(168, 120)]), mixc(card, p.line, 0.25), lw: lw, seed: 702, amp: 0.3, d: 5)
    // cat, seen from the chest up
    kShoulders(c, p, 148, 122, 66, 34, lw: lw, seed: 703)
    kHead(c, p, 148, 72, 1.0, .glare, lw: 3.2, seed: 8)
    // raised paw holding a USB drive
    kArm(c, p, mk { $0.move(to: pt(196, 140)); $0.addQuadCurve(to: pt(238, 108), control: pt(226, 142)) }, w: 17, lw: lw, seed: 704)
    c.saveGState(); c.translateBy(x: 244, y: 82); c.rotate(by: -0.2)
    boneSolid(c, p, rrect(-12, -34, 12, -12, 3), lw: 2.6, seed: 705)
    fill(c, rectP(-7, -29, 5, 7), p.line); fill(c, rectP(2, -29, 5, 7), p.line)
    solid(c, p, rrect(-14, -12, 14, 26, 6), p.accent, shade: mixc(p.accent, p.line, 0.4), rim: mixc(p.accent, p.bone, 0.3), lw: 3.0, seed: 706, amp: 0.15, d: 4)
    fill(c, ell(0, 14, 3.4, 3.4), p.line)
    c.restoreGState()
    kPaw(c, p, 236, 112, 0.55, -1.2, lw: 2.8, seed: 707)
    // box front over the cat's lower body
    let front = rrect(48, 122, 250, 204, 6)
    solid(c, p, front, card, shade: cardShade, rim: mixc(card, p.bone, 0.2), lw: lw, seed: 708, amp: 0.3, d: 8)
    solid(c, p, rectP(116, 122, 66, 18), mixc(card, p.bone, 0.22), lw: lw, seed: 709, amp: 0.2, d: 2)   // tape
    solid(c, p, poly([pt(48, 122), pt(250, 122), pt(250, 130), pt(48, 130)]), mixc(card, p.line, 0.2), lw: 2.2, seed: 710, amp: 0.2, d: 2)
    shout(c, "MSC", 149, 172, 34, -0.04, p.eye, p.line, 2.4)
    inkLine(c, [pt(62, 186), pt(100, 190)], 2.4, alpha(p.line, 0.45), 711, amp: 0.5)
    // claws hooked on the near rim
    for x in [96.0, 116.0, 136.0] as [CGFloat] { solid(c, p, poly([pt(x - 3, 124), pt(x + 3, 124), pt(x, 112)]), p.bone, lw: 1.8, seed: 712 + Int(x), amp: 0.05) }
    return c
}
