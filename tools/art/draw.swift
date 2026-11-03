// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// FancyKitty procedural art (kawaii pastel kitty). CoreGraphics only, no
// external art. Usage: draw <outdir> [<reference_dir>]
// Build/run via tools/art/make_preview.sh. Origin is top-left, y grows down.

import CoreGraphics
import Foundation
import ImageIO

// MARK: palette (8 colours + 1 small gold accent for bells)
func col(_ hex: UInt32, _ a: CGFloat = 1) -> CGColor {
    CGColor(srgbRed: CGFloat((hex >> 16) & 255) / 255, green: CGFloat((hex >> 8) & 255) / 255,
            blue: CGFloat(hex & 255) / 255, alpha: a)
}
enum P {
    static let pink = col(0xF8B4C8)
    static let peach = col(0xFFD3B0)
    static let cream = col(0xFFF3DC)
    static let lavender = col(0xCDB8F0)
    static let mint = col(0xB8EBD0)
    static let blue = col(0xB5DCF5)
    static let ink = col(0x4A3F55)       // charcoal outline
    static let coral = col(0xFF7F7F)     // accent
    static let gold = col(0xF9D56E)
    static let white = col(0xFFFFFF)
}

// MARK: context helpers
func makeCtx(_ w: Int, _ h: Int) -> CGContext {
    let c = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
                      space: CGColorSpace(name: CGColorSpace.sRGB)!,
                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    c.translateBy(x: 0, y: CGFloat(h)); c.scaleBy(x: 1, y: -1)
    c.setLineCap(.round); c.setLineJoin(.round)
    c.setAllowsAntialiasing(true); c.setShouldAntialias(true)
    c.interpolationQuality = .high
    return c
}
func save(_ c: CGContext, _ path: String) {
    let img = c.makeImage()!
    let d = CGImageDestinationCreateWithURL(URL(fileURLWithPath: path) as CFURL, "public.png" as CFString, 1, nil)!
    CGImageDestinationAddImage(d, img, nil)
    if !CGImageDestinationFinalize(d) { fatalError("write failed \(path)") }
    print("wrote \(path) (\(c.width)x\(c.height))")
}
func loadImage(_ path: String) -> CGImage? {
    guard let s = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil) else { return nil }
    return CGImageSourceCreateImageAtIndex(s, 0, nil)
}
func drawImage(_ c: CGContext, _ img: CGImage, _ r: CGRect) {
    c.saveGState(); c.translateBy(x: r.minX, y: r.maxY); c.scaleBy(x: 1, y: -1)
    c.draw(img, in: CGRect(x: 0, y: 0, width: r.width, height: r.height)); c.restoreGState()
}

// MARK: path helpers
func mk(_ f: (CGMutablePath) -> Void) -> CGPath { let p = CGMutablePath(); f(p); return p }
func pt(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: x, y: y) }
func ell(_ cx: CGFloat, _ cy: CGFloat, _ rx: CGFloat, _ ry: CGFloat) -> CGPath {
    CGPath(ellipseIn: CGRect(x: cx - rx, y: cy - ry, width: rx * 2, height: ry * 2), transform: nil)
}
func rrect(_ x0: CGFloat, _ y0: CGFloat, _ x1: CGFloat, _ y1: CGFloat, _ r: CGFloat) -> CGPath {
    CGPath(roundedRect: CGRect(x: x0, y: y0, width: x1 - x0, height: y1 - y0), cornerWidth: r, cornerHeight: r, transform: nil)
}
func poly(_ p: [CGPoint]) -> CGPath { mk { $0.addLines(between: p); $0.closeSubpath() } }
func fill(_ c: CGContext, _ p: CGPath, _ col: CGColor) { c.addPath(p); c.setFillColor(col); c.fillPath() }
func stroke(_ c: CGContext, _ p: CGPath, _ col: CGColor, _ w: CGFloat) {
    c.addPath(p); c.setStrokeColor(col); c.setLineWidth(w); c.strokePath()
}
/// Filled shape with thick rounded charcoal outline.
func shape(_ c: CGContext, _ p: CGPath, _ f: CGColor?, _ lw: CGFloat, _ line: CGColor = P.ink) {
    if let f = f { fill(c, p, f) }
    stroke(c, p, line, lw)
}
/// Thick line drawn as outline + fill (a "tube").
func tube(_ c: CGContext, _ p: CGPath, _ f: CGColor, _ w: CGFloat, _ lw: CGFloat) {
    stroke(c, p, P.ink, w + lw * 2); stroke(c, p, f, w)
}
func heartPath(_ x: CGFloat, _ y: CGFloat, _ s: CGFloat) -> CGPath {
    func q(_ a: CGFloat, _ b: CGFloat) -> CGPoint { pt(x + a * s, y + b * s) }
    return mk { p in
        p.move(to: q(0, 1))
        p.addCurve(to: q(-1, -0.2), control1: q(-0.4, 0.7), control2: q(-1, 0.3))
        p.addCurve(to: q(0, -0.35), control1: q(-1, -0.85), control2: q(-0.2, -0.95))
        p.addCurve(to: q(1, -0.2), control1: q(0.2, -0.95), control2: q(1, -0.85))
        p.addCurve(to: q(0, 1), control1: q(1, 0.3), control2: q(0.4, 0.7))
        p.closeSubpath()
    }
}
func sparklePath(_ x: CGFloat, _ y: CGFloat, _ r: CGFloat) -> CGPath {
    mk { p in
        let k = r * 0.18
        p.move(to: pt(x, y - r))
        p.addQuadCurve(to: pt(x + r, y), control: pt(x + k, y - k))
        p.addQuadCurve(to: pt(x, y + r), control: pt(x + k, y + k))
        p.addQuadCurve(to: pt(x - r, y), control: pt(x - k, y + k))
        p.addQuadCurve(to: pt(x, y - r), control: pt(x - k, y - k))
        p.closeSubpath()
    }
}
func sparkle(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ r: CGFloat, _ f: CGColor, _ lw: CGFloat) {
    shape(c, sparklePath(x, y, r), f, lw)
}
func heart(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, _ f: CGColor, _ lw: CGFloat) {
    shape(c, heartPath(x, y, s), f, lw)
}
func fish(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, _ f: CGColor, _ lw: CGFloat) {
    c.saveGState(); c.translateBy(x: x, y: y); c.scaleBy(x: s, y: s)
    let l = lw / s
    shape(c, poly([pt(-10, 0), pt(-24, -9), pt(-24, 9)]), P.pink, l)
    shape(c, ell(2, 0, 16, 10), f, l)
    fill(c, ell(9, -2, 2.4, 2.4), P.ink)
    stroke(c, mk { $0.move(to: pt(-6, -6)); $0.addQuadCurve(to: pt(-6, 6), control: pt(-1, 0)) }, P.ink, l * 0.6)
    c.restoreGState()
}
func pawPrint(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, _ f: CGColor, rot: CGFloat = 0) {
    c.saveGState(); c.translateBy(x: x, y: y); c.rotate(by: rot); c.scaleBy(x: s, y: s)
    fill(c, ell(0, 5, 9, 7.5), f)
    fill(c, ell(-10, -5, 3.6, 4.8), f); fill(c, ell(-3.6, -11, 3.6, 4.8), f)
    fill(c, ell(3.6, -11, 3.6, 4.8), f); fill(c, ell(10, -5, 3.6, 4.8), f)
    c.restoreGState()
}
func fishBone(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, _ col: CGColor, rot: CGFloat = 0) {
    c.saveGState(); c.translateBy(x: x, y: y); c.rotate(by: rot); c.scaleBy(x: s, y: s)
    let p = mk { p in
        p.move(to: pt(-12, 0)); p.addLine(to: pt(12, 0))
        for i in 0..<3 {
            let xx = CGFloat(i) * 6 - 6
            p.move(to: pt(xx, 0)); p.addLine(to: pt(xx + 3, -6))
            p.move(to: pt(xx, 0)); p.addLine(to: pt(xx + 3, 6))
        }
        p.move(to: pt(-12, 0)); p.addLine(to: pt(-18, -6)); p.move(to: pt(-12, 0)); p.addLine(to: pt(-18, 6))
    }
    stroke(c, p, col, 2 / max(s, 0.1) * s * 1.2)
    stroke(c, ell(17, 0, 5, 4), col, 2 * 1.2)
    c.restoreGState()
}

// MARK: cat head
enum Expr { case idle, happy, sleepy, surprised, smug }

/// Cat head centred on (cx, cy); head ellipse is 86x64 at scale 1.
func drawHead(_ c: CGContext, _ cx: CGFloat, _ cy: CGFloat, _ s: CGFloat, lw: CGFloat, _ e: Expr,
              fur: CGColor = P.cream, patch: CGColor? = P.peach, bow: Bool = true, whiskers: Bool = true) {
    c.saveGState(); c.translateBy(x: cx, y: cy); c.scaleBy(x: s, y: s)
    let l = lw / s
    // ears
    for sx: CGFloat in [-1, 1] {
        shape(c, poly([pt(sx * 80, -8), pt(sx * 76, -84), pt(sx * 22, -54)]), fur, l)
        fill(c, poly([pt(sx * 68, -22), pt(sx * 66, -66), pt(sx * 38, -49)]), P.pink)
    }
    // head
    let head = ell(0, 0, 86, 64)
    fill(c, head, fur)
    if let pc = patch {
        c.saveGState(); c.addPath(head); c.clip()
        fill(c, ell(-52, -52, 56, 34), pc)
        fill(c, ell(66, -48, 30, 22), pc)
        c.restoreGState()
    }
    stroke(c, head, P.ink, l)
    // forehead tabby marks
    for dx: CGFloat in [-11, 0, 11] {
        stroke(c, mk { $0.move(to: pt(dx, -60)); $0.addLine(to: pt(dx * 0.8, dx == 0 ? -42 : -47)) }, P.lavender, l * 1.1)
    }
    // eyes
    for sx: CGFloat in [-1, 1] {
        let ex = sx * 36
        switch e {
        case .idle:
            fill(c, ell(ex, 0, 14, 18), P.ink)
            fill(c, ell(ex, 8, 9, 8), col(0x7B6BA8))
            fill(c, ell(ex - 4, -7, 6, 6.5), P.white); fill(c, ell(ex + 5, 7, 3, 3), P.white)
        case .happy:
            stroke(c, mk { $0.move(to: pt(ex - 14, 6)); $0.addQuadCurve(to: pt(ex + 14, 6), control: pt(ex, -22)) }, P.ink, l * 1.35)
        case .sleepy:
            stroke(c, mk { $0.move(to: pt(ex - 14, -3)); $0.addQuadCurve(to: pt(ex + 14, -3), control: pt(ex, 14)) }, P.ink, l * 1.35)
            stroke(c, mk { $0.move(to: pt(ex + sx * 13, -2)); $0.addLine(to: pt(ex + sx * 19, -6)) }, P.ink, l * 0.8)
        case .surprised:
            shape(c, ell(ex, 0, 18, 21), P.white, l * 0.8)
            fill(c, ell(ex, 1, 8, 10), P.ink); fill(c, ell(ex - 3, -3, 3.4, 3.4), P.white)
            stroke(c, mk { $0.move(to: pt(ex - 14, -29)); $0.addQuadCurve(to: pt(ex + 14, -29), control: pt(ex, -38)) }, P.ink, l * 0.9)
        case .smug:
            let lid = mk { p in
                p.addArc(center: pt(ex, 0), radius: 14, startAngle: 0, endAngle: .pi, clockwise: false)
                p.closeSubpath()
            }
            fill(c, ell(ex, 2, 14, 15), P.ink)
            c.saveGState(); c.addPath(ell(ex, 2, 14, 15)); c.clip()
            fill(c, CGPath(rect: CGRect(x: ex - 20, y: -20, width: 40, height: 18), transform: nil), fur)
            c.restoreGState()
            _ = lid
            fill(c, ell(ex - 3, 5, 4, 4), P.white)
            stroke(c, mk { $0.move(to: pt(ex - 16, -2)); $0.addLine(to: pt(ex + 16, -2)) }, P.ink, l * 1.3)
            stroke(c, mk { $0.move(to: pt(ex - sx * 14, -17)); $0.addLine(to: pt(ex + sx * 14, -9)) }, P.ink, l * 1.1)
        }
    }
    // blush
    for sx: CGFloat in [-1, 1] {
        fill(c, ell(sx * 60, 24, 15, 9), col(0xFF7F7F, 0.5))
    }
    // nose + mouth
    shape(c, poly([pt(-6, 9), pt(6, 9), pt(0, 17)]), P.coral, l * 0.7)
    let mouthStroke = l * 0.9
    switch e {
    case .idle:
        stroke(c, mk { $0.move(to: pt(0, 17)); $0.addLine(to: pt(0, 21)); $0.addQuadCurve(to: pt(-13, 24), control: pt(-6, 30))
            $0.move(to: pt(0, 21)); $0.addQuadCurve(to: pt(13, 24), control: pt(6, 30)) }, P.ink, mouthStroke)
    case .happy:
        let m = mk { $0.move(to: pt(-15, 21)); $0.addQuadCurve(to: pt(15, 21), control: pt(0, 24))
            $0.addQuadCurve(to: pt(-15, 21), control: pt(0, 52)); $0.closeSubpath() }
        shape(c, m, col(0xE0556B), mouthStroke)
        c.saveGState(); c.addPath(m); c.clip(); fill(c, ell(0, 40, 9, 7), P.pink); c.restoreGState()
        stroke(c, m, P.ink, mouthStroke)
    case .sleepy:
        stroke(c, mk { $0.move(to: pt(0, 17)); $0.addLine(to: pt(0, 21)); $0.addQuadCurve(to: pt(-9, 24), control: pt(-4, 27))
            $0.move(to: pt(0, 21)); $0.addQuadCurve(to: pt(9, 24), control: pt(4, 27)) }, P.ink, mouthStroke)
        // snot bubble
        c.saveGState()
        fill(c, ell(14, 12, 10, 10), col(0xB5DCF5, 0.75)); stroke(c, ell(14, 12, 10, 10), P.ink, l * 0.6)
        fill(c, ell(11, 8, 3, 3), P.white); c.restoreGState()
    case .surprised:
        shape(c, ell(0, 31, 7, 9), col(0xE0556B), mouthStroke)
        stroke(c, mk { $0.move(to: pt(0, 17)); $0.addLine(to: pt(0, 21)) }, P.ink, mouthStroke)
        // sweat drop
        let d = mk { $0.move(to: pt(70, -36)); $0.addQuadCurve(to: pt(78, -22), control: pt(76, -29))
            $0.addArc(center: pt(70, -20), radius: 8, startAngle: 0.3, endAngle: .pi - 0.3, clockwise: false)
            $0.addQuadCurve(to: pt(70, -36), control: pt(64, -29)); $0.closeSubpath() }
        shape(c, d, P.blue, l * 0.8)
    case .smug:
        stroke(c, mk { $0.move(to: pt(0, 17)); $0.addLine(to: pt(0, 21)); $0.addQuadCurve(to: pt(-12, 22), control: pt(-6, 26))
            $0.move(to: pt(0, 21)); $0.addQuadCurve(to: pt(18, 14), control: pt(8, 28)) }, P.ink, mouthStroke)
        fill(c, poly([pt(9, 22), pt(14, 20), pt(12, 27)]), P.white)
    }
    // whiskers
    if whiskers {
        for sx: CGFloat in [-1, 1] {
            for (dy, ang) in [(-2.0, -0.18), (6.0, 0.0), (14.0, 0.18)] as [(CGFloat, CGFloat)] {
                let x0 = sx * 80, y0 = 18 + dy
                stroke(c, mk { $0.move(to: pt(x0, y0)); $0.addLine(to: pt(x0 + sx * 28 * cos(ang), y0 + 28 * sin(ang) + dy * 0.1)) },
                       P.ink, l * 0.6)
            }
        }
    }
    // bow on left ear
    if bow {
        let bx: CGFloat = -62, by: CGFloat = -58
        shape(c, poly([pt(bx, by), pt(bx - 22, by - 12), pt(bx - 22, by + 10)]), P.coral, l * 0.8)
        shape(c, poly([pt(bx, by), pt(bx + 22, by - 12), pt(bx + 22, by + 10)]), P.coral, l * 0.8)
        shape(c, ell(bx, by, 6, 6), col(0xFF9A9A), l * 0.8)
    }
    c.restoreGState()
}

// MARK: (a) mascot 269x203
func drawMascot(_ e: Expr) -> CGContext {
    let c = makeCtx(269, 203)
    let lw: CGFloat = 4
    // tail
    let tail = mk { $0.move(to: pt(180, 178)); $0.addCurve(to: pt(238, 104), control1: pt(240, 190), control2: pt(262, 140)) }
    tube(c, tail, P.cream, 14, 3.5)
    stroke(c, mk { $0.move(to: pt(236, 112)); $0.addLine(to: pt(238, 104)) }, P.lavender, 14)
    c.addPath(tail); c.setLineWidth(1); // keep path warm
    // body
    shape(c, ell(134, 160, 66, 40), P.cream, lw)
    c.saveGState(); c.addPath(ell(134, 160, 66, 40)); c.clip(); fill(c, ell(190, 150, 30, 40), P.peach); c.restoreGState()
    stroke(c, ell(134, 160, 66, 40), P.ink, lw)
    // collar + bell
    let collar = mk { $0.move(to: pt(96, 143)); $0.addQuadCurve(to: pt(172, 143), control: pt(134, 176)) }
    tube(c, collar, P.coral, 7, 3)
    shape(c, ell(134, 165, 9, 9), P.gold, 3); stroke(c, mk { $0.move(to: pt(126, 166)); $0.addLine(to: pt(142, 166)) }, P.ink, 2)
    // paws
    for x: CGFloat in [102, 166] {
        shape(c, ell(x, 188, 21, 13), P.cream, lw)
        fill(c, ell(x, 190, 6, 4.5), P.pink)
        for dx: CGFloat in [-9, 0, 9] { fill(c, ell(x + dx, 184, 2.6, 3), P.pink) }
    }
    drawHead(c, 134, 92, 1.0, lw: lw, e)
    // motifs
    fish(c, 36, 172, 0.95, P.mint, 3)
    switch e {
    case .idle:
        sparkle(c, 232, 40, 11, P.gold, 2.5); sparkle(c, 30, 62, 8, P.lavender, 2.5); sparkle(c, 245, 70, 6, P.pink, 2)
    case .happy:
        heart(c, 232, 38, 11, P.coral, 2.5); heart(c, 28, 60, 8, P.pink, 2.5); sparkle(c, 248, 66, 7, P.gold, 2)
        sparkle(c, 50, 30, 6, P.blue, 2)
    case .sleepy:
        sparkle(c, 236, 40, 8, P.lavender, 2.5); sparkle(c, 30, 60, 6, P.blue, 2.5)
        // crescent moon
        let moon = mk { p in p.addArc(center: pt(228, 62), radius: 12, startAngle: 0.7, endAngle: 5.6, clockwise: false)
            p.addArc(center: pt(234, 58), radius: 9.5, startAngle: 5.3, endAngle: 1.0, clockwise: true); p.closeSubpath() }
        shape(c, moon, P.gold, 2.5)
    case .surprised:
        sparkle(c, 236, 36, 9, P.gold, 2.5); sparkle(c, 30, 58, 9, P.pink, 2.5)
        for (a, b) in [(222.0, 20.0), (240.0, 30.0)] as [(CGFloat, CGFloat)] {
            stroke(c, mk { $0.move(to: pt(a, b)); $0.addLine(to: pt(a + 6, b - 6)) }, P.ink, 2.5)
        }
    case .smug: break
    }
    return c
}

// MARK: (b) icons 70x70
func drawIconAirMouse() -> CGContext {
    let c = makeCtx(70, 70); let lw: CGFloat = 2.6
    // mouse cable (tail)
    tube(c, mk { $0.move(to: pt(46, 33)); $0.addCurve(to: pt(60, 14), control1: pt(46, 20), control2: pt(66, 24)) }, P.lavender, 1.6, 1.4)
    // mouse body
    let body = rrect(26, 32, 62, 66, 15)
    shape(c, body, P.lavender, lw)
    stroke(c, mk { $0.move(to: pt(26, 46)); $0.addLine(to: pt(62, 46)); $0.move(to: pt(44, 33)); $0.addLine(to: pt(44, 46)) }, P.ink, 1.8)
    // mouse ears + eyes
    shape(c, ell(33, 31, 5, 6), P.pink, 2); shape(c, ell(55, 31, 5, 6), P.pink, 2)
    fill(c, ell(38, 55, 2.2, 2.6), P.ink); fill(c, ell(50, 55, 2.2, 2.6), P.ink)
    fill(c, ell(34, 57, 3, 2), col(0xFF7F7F, 0.55)); fill(c, ell(54, 57, 3, 2), col(0xFF7F7F, 0.55))
    shape(c, ell(44, 58, 1.6, 1.3), P.coral, 1.2)
    // paw on top
    c.saveGState(); c.translateBy(x: 22, y: 26); c.rotate(by: -0.35)
    shape(c, ell(0, 4, 13, 11), P.cream, lw)
    fill(c, ell(0, 7, 6, 4.6), P.pink)
    for (dx, dy) in [(-8.5, -3.0), (-3, -6.5), (3, -6.5), (8.5, -3.0)] as [(CGFloat, CGFloat)] {
        fill(c, ell(dx, dy + 1.5, 2.4, 2.8), P.pink)
    }
    c.restoreGState()
    sparkle(c, 12, 55, 5, P.gold, 1.6); heart(c, 60, 62, 4, P.coral, 1.4)
    return c
}
func drawIconBadUSB() -> CGContext {
    let c = makeCtx(70, 70); let lw: CGFloat = 2.6
    // tail with USB plug
    let tail = mk { $0.move(to: pt(40, 58)); $0.addCurve(to: pt(60, 38), control1: pt(60, 66), control2: pt(66, 52)) }
    tube(c, tail, P.peach, 5, 2)
    // plug: housing + metal shell with two contact holes
    shape(c, rrect(52, 25, 68, 40, 3.5), P.blue, 2.2)
    shape(c, rrect(54, 11, 66, 27, 2), P.white, 2.2)
    fill(c, rrect(56.2, 15, 58.8, 19.5, 0.6), P.ink); fill(c, rrect(61.2, 15, 63.8, 19.5, 0.6), P.ink)
    fill(c, ell(60, 33, 2.2, 2.2), P.coral)
    // body
    shape(c, ell(27, 56, 17, 10), P.cream, lw)
    shape(c, ell(17, 62, 6, 4), P.cream, 2); shape(c, ell(37, 62, 6, 4), P.cream, 2)
    drawHead(c, 28, 30, 0.27, lw: 2.4, .smug, bow: false, whiskers: false)
    sparkle(c, 8, 10, 4, P.gold, 1.4)
    return c
}
func drawIconInfrared() -> CGContext {
    let c = makeCtx(70, 70); let lw: CGFloat = 2.6
    drawHead(c, 27, 40, 0.27, lw: 2.4, .idle, bow: false, whiskers: false)
    // remote (tilted) held in front
    c.saveGState(); c.translateBy(x: 53, y: 44); c.rotate(by: 0.22)
    shape(c, rrect(-9, -20, 9, 22, 5), P.lavender, lw)
    fill(c, ell(0, -12, 3.6, 3.6), P.coral); stroke(c, ell(0, -12, 3.6, 3.6), P.ink, 1.3)
    for (dx, dy) in [(-3.8, -2.0), (3.8, -2.0), (-3.8, 5.0), (3.8, 5.0), (0, 12.0)] as [(CGFloat, CGFloat)] {
        fill(c, ell(dx, dy, 2.2, 2.2), dy == 12 ? P.mint : P.cream)
    }
    c.restoreGState()
    // IR waves above remote
    for (r, f) in [(5.0, P.coral), (10.0, P.pink)] as [(CGFloat, CGColor)] {
        let a = mk { $0.addArc(center: pt(57, 21), radius: r, startAngle: -2.45, endAngle: -0.7, clockwise: false) }
        stroke(c, a, P.ink, 3.4); stroke(c, a, f, 1.8)
    }
    sparkle(c, 10, 12, 4, P.gold, 1.4)
    return c
}
func drawIconVoice() -> CGContext {
    let c = makeCtx(70, 70); let lw: CGFloat = 2.6
    drawHead(c, 35, 33, 0.26, lw: 2.4, .happy, bow: false, whiskers: false)
    // headphones: band + cups
    tube(c, mk { $0.move(to: pt(10, 38)); $0.addCurve(to: pt(60, 38), control1: pt(6, 2), control2: pt(64, 2)) }, P.coral, 2.2, 1.5)
    for x: CGFloat in [9, 61] { shape(c, rrect(x - 4.5, 31, x + 4.5, 47, 4), P.coral, 2.2); fill(c, rrect(x - 1.5, 34, x + 1.5, 44, 1.5), P.pink) }
    // microphone below chin
    shape(c, rrect(29.5, 51, 40.5, 65, 5.5), P.lavender, lw)
    for yy: CGFloat in [55, 58.5, 62] { stroke(c, mk { $0.move(to: pt(31.5, yy)); $0.addLine(to: pt(38.5, yy)) }, P.ink, 1.1) }
    heart(c, 56, 60, 4, P.coral, 1.4); sparkle(c, 12, 62, 4, P.gold, 1.4)
    return c
}

// MARK: (c) background 320x241
func drawBackground() -> CGContext {
    let w = 320, h = 241
    let c = makeCtx(w, h)
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    let g = CGGradient(colorsSpace: cs, colors: [col(0xFFF1F5), col(0xFBD3E4), col(0xDCCFF6)] as CFArray, locations: [0, 0.55, 1])!
    c.drawLinearGradient(g, start: pt(0, 0), end: pt(CGFloat(w) * 0.35, CGFloat(h)), options: [.drawsBeforeStartLocation, .drawsAfterEndLocation])
    // faint pattern, avoids the central text area
    var i = 0
    for row in 0..<7 {
        for colI in 0..<8 {
            let x = CGFloat(colI) * 46 + (row % 2 == 0 ? 12 : 35)
            let y = CGFloat(row) * 40 + 16
            let dx = (x - 160) / 118, dy = (y - 120) / 78
            if dx * dx + dy * dy < 1 { continue }
            let rot = CGFloat((i * 37) % 60 - 30) * .pi / 180
            if i % 2 == 0 { pawPrint(c, x, y, 0.8, col(0xFFFFFF, 0.38), rot: rot) }
            else { fishBone(c, x, y, 0.8, col(0xFFFFFF, 0.38), rot: rot) }
            i += 1
        }
    }
    return c
}

// MARK: (d) message box 260x152
func drawMessageBox() -> CGContext {
    let c = makeCtx(260, 152); let lw: CGFloat = 4
    // speech tail (bottom-left) drawn first
    shape(c, poly([pt(36, 130), pt(74, 130), pt(32, 149)]), P.cream, lw)
    // ears
    for sx in [0, 1] {
        let f: CGFloat = sx == 0 ? 1 : -1, ox: CGFloat = sx == 0 ? 0 : 260
        shape(c, poly([pt(ox + f * 12, 34), pt(ox + f * 18, 1), pt(ox + f * 58, 20)]), P.cream, lw)
        fill(c, poly([pt(ox + f * 22, 26), pt(ox + f * 24, 10), pt(ox + f * 42, 19)]), P.pink)
    }
    let panel = rrect(6, 16, 254, 138, 24)
    shape(c, panel, P.cream, lw)
    fill(c, CGRect(x: 41, y: 135.8, width: 29, height: 4.4).path(), P.cream)  // hide seam under tail
    // stitched inner border
    c.saveGState(); c.setLineDash(phase: 0, lengths: [6, 6])
    stroke(c, rrect(15, 25, 245, 129, 17), col(0xF8B4C8), 2); c.restoreGState()
    // corner decorations (outside the central text area)
    pawPrint(c, 222, 112, 0.9, P.pink, rot: 0.3)
    heart(c, 30, 42, 6, P.pink, 1.8)
    sparkle(c, 226, 42, 6, P.gold, 2)
    return c
}
extension CGRect { func path() -> CGPath { CGPath(rect: self, transform: nil) } }

// MARK: (e) contact sheet 640x480
func tile(_ c: CGContext, _ r: CGRect, _ f: CGColor) { shape(c, rrect(r.minX, r.minY, r.maxX, r.maxY, 14), f, 0.001); fill(c, rrect(r.minX, r.minY, r.maxX, r.maxY, 14), f) }

func drawSheet(_ out: String, _ ref: String, _ mascots: [CGContext], _ icons: [CGContext], _ bg: CGContext, _ msg: CGContext) -> CGContext {
    let c = makeCtx(640, 480)
    fill(c, CGRect(x: 0, y: 0, width: 640, height: 480).path(), col(0x3A3347))
    let gray = col(0x2C2636)
    // row 1: reference mascot + 4 new mascots (each 122x92)
    let mw: CGFloat = 122, mh: CGFloat = 92, gx: CGFloat = 8
    let x0: CGFloat = (640 - (5 * mw + 4 * gx)) / 2
    let row1: CGFloat = 12
    for i in 0..<5 {
        let r = CGRect(x: x0 + CGFloat(i) * (mw + gx), y: row1, width: mw, height: mh)
        fill(c, rrect(r.minX, r.minY, r.maxX, r.maxY, 10), i == 0 ? gray : col(0xFFF6EE))
        if i == 0 { if let im = loadImage(ref + "/ui_img_mascot_png.png") { drawImage(c, im, r) } }
        else { drawImage(c, mascots[i - 1].makeImage()!, r) }
    }
    // row 2: 4 reference icons on lime + 4 new on pastel tile (70x70, tile 78)
    let row2: CGFloat = 120
    let names = ["air_mouse", "badusb", "infrared", "dino"]
    let tx0: CGFloat = (640 - (8 * 70 + 7 * 10 + 20)) / 2
    for i in 0..<8 {
        let x = tx0 + CGFloat(i) * 80 + (i >= 4 ? 20 : 0)
        let r = CGRect(x: x, y: row2, width: 70, height: 70)
        if i < 4 {
            fill(c, rrect(x, row2, x + 70, row2 + 70, 12), col(0xBEE700))
            if let im = loadImage(ref + "/ui_img_\(names[i])_png.png") { drawImage(c, im, r) }
        } else {
            fill(c, rrect(x, row2, x + 70, row2 + 70, 12), i % 2 == 0 ? col(0xFBD3E4) : col(0xDCCFF6))
            drawImage(c, icons[i - 4].makeImage()!, r)
        }
    }
    // row 3: backgrounds (ref | new) 160x120 and message boxes 130x76
    let row3: CGFloat = 206
    if let im = loadImage(ref + "/ui_img_background_png.png") { drawImage(c, im, CGRect(x: 14, y: row3, width: 150, height: 113)) }
    drawImage(c, bg.makeImage()!, CGRect(x: 172, y: row3, width: 150, height: 113))
    if let im = loadImage(ref + "/ui_img_message_box_bg_png.png") {
        fill(c, rrect(336, row3, 486, row3 + 113, 8), col(0xE7E1EE)); drawImage(c, im, CGRect(x: 346, y: row3 + 20, width: 130, height: 76))
    }
    fill(c, rrect(494, row3, 640 - 14, row3 + 113, 8), col(0xE7E1EE))
    drawImage(c, msg.makeImage()!, CGRect(x: 504, y: row3 + 20, width: 112, height: 66))
    // row 4: mock home screen (new bg + mascot + icons at 1x) and mock message box over the new bg
    let row4: CGFloat = 332
    c.saveGState(); c.clip(to: CGRect(x: 14, y: row4, width: 300, height: 135))
    drawImage(c, bg.makeImage()!, CGRect(x: 14, y: row4 - 53, width: 320, height: 241))
    drawImage(c, mascots[0].makeImage()!, CGRect(x: 18, y: row4 + 24, width: 130, height: 98))
    for i in 0..<2 {
        let x = 162 + CGFloat(i) * 74
        fill(c, rrect(x, row4 + 28, x + 70, row4 + 98, 14), col(0xFFF3DC, 0.9))
        drawImage(c, icons[i].makeImage()!, CGRect(x: x, y: row4 + 28, width: 70, height: 70))
    }
    c.restoreGState()
    c.saveGState(); c.clip(to: CGRect(x: 326, y: row4, width: 300, height: 135))
    drawImage(c, bg.makeImage()!, CGRect(x: 326, y: row4 - 53, width: 320, height: 241))
    drawImage(c, msg.makeImage()!, CGRect(x: 372, y: row4 + 6, width: 208, height: 122))
    c.restoreGState()
    return c
}

// MARK: main
let args = CommandLine.arguments
guard args.count >= 2 else { print("usage: draw <outdir> [<reference_dir>]"); exit(1) }
let out = args[1]
let ref = args.count > 2 ? args[2] : out + "/reference"
try? FileManager.default.createDirectory(atPath: out, withIntermediateDirectories: true)
let exprs: [(String, Expr)] = [("idle", .idle), ("happy", .happy), ("sleepy", .sleepy), ("surprised", .surprised)]
var mascots: [CGContext] = []
for (n, e) in exprs { let m = drawMascot(e); save(m, "\(out)/mascot_\(n).png"); mascots.append(m) }
let icons = [drawIconAirMouse(), drawIconBadUSB(), drawIconInfrared(), drawIconVoice()]
for (n, ic) in zip(["air_mouse", "badusb", "infrared", "voicerec"], icons) { save(ic, "\(out)/icon_\(n).png") }
let bg = drawBackground(); save(bg, "\(out)/background.png")
let msg = drawMessageBox(); save(msg, "\(out)/message_box_bg.png")
save(drawSheet(out, ref, mascots, icons, bg, msg), "\(out)/contact_sheet.png")
