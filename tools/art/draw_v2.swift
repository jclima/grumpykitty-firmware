// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// GrumpyKitty art v2: a grumpy, witty cream calico kitty with props, on a
// "midnight plum" theme. One function per asset, driven by the registry at the
// bottom (see assetRegistry). Reuses the kit from draw.swift.

import CoreGraphics
import CoreText
import Foundation

// MARK: v2 palette
enum V {
    static let plumDeep = col(0x2B2040)
    static let plumLite = col(0x3A2A58)
    static let plumMid = col(0x33264E)
    static let tan = col(0xE9C9A0)
    static let tanDark = col(0xC59D6B)
    static let mauve = col(0x6B4A73)
    static let mauveLite = col(0x8A6A94)
    static let darkInk = col(0x3A2E48)
    static let discTop = col(0xFFFBFF)
    static let discBot = col(0xE4D6FA)
    static let slate = col(0x5E5873)
    static let screen = col(0x2E2A44)
    static let teal = col(0x7DD8C6)
}

// MARK: text (CoreText in the flipped, top-left-origin context)
func drawText(_ c: CGContext, _ s: String, _ x: CGFloat, _ y: CGFloat, size: CGFloat, color: CGColor,
              font: String = "Menlo-Bold", center: Bool = false) {
    let f = CTFontCreateWithName(font as CFString, size, nil)
    let attrs: [CFString: Any] = [kCTFontAttributeName: f, kCTForegroundColorAttributeName: color]
    let line = CTLineCreateWithAttributedString(NSAttributedString(string: s, attributes: attrs as [NSAttributedString.Key: Any]))
    let wd = CTLineGetTypographicBounds(line, nil, nil, nil)
    c.saveGState()
    c.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
    c.textPosition = pt(center ? x - CGFloat(wd) / 2 : x, y)
    CTLineDraw(line, c)
    c.restoreGState()
}

// MARK: mood
enum BrowK { case none, flat, angry, up, sad }
enum MouthK { case frown, flat, smirk, grin, yawn, blep, wavy }
struct Mood {
    var lidL: CGFloat = 0.5, lidR: CGFloat = 0.5      // fraction of the eye covered from the top
    var tiltL: CGFloat = 0, tiltR: CGFloat = 0        // + = angry slant (inner end lower)
    var look = pt(0, 3)
    var browL: BrowK = .none, browR: BrowK = .none
    var mouth: MouthK = .frown
    var closed = false
    var blush: CGFloat = 0.22
    var flatEars = true
    var sweat = false
    var bow = true
    var whiskers = true
    var shades = false
}
extension Mood {
    static var grumpy: Mood { var m = Mood(); m.lidL = 0.52; m.lidR = 0.52; m.tiltL = 3; m.tiltR = 3
        m.browL = .flat; m.browR = .up; m.look = pt(0, 3); return m }
    static var smug: Mood { var m = Mood(); m.lidL = 0.45; m.lidR = 0.45; m.look = pt(7, 2); m.browL = .up
        m.mouth = .smirk; m.flatEars = false; return m }
    static var unimpressed: Mood { var m = Mood(); m.lidL = 0.6; m.lidR = 0.6; m.browL = .flat; m.browR = .flat
        m.mouth = .flat; m.look = pt(0, 4); return m }
    static var annoyed: Mood { var m = Mood(); m.lidL = 0.42; m.lidR = 0.42; m.tiltL = 8; m.tiltR = 8
        m.browL = .angry; m.browR = .angry; m.mouth = .wavy; m.look = pt(-3, 2); m.blush = 0.4; return m }
    static var triumphant: Mood { var m = Mood(); m.lidL = 0.35; m.lidR = 0.35; m.browL = .up; m.browR = .up
        m.mouth = .grin; m.look = pt(0, -2); m.flatEars = false; m.blush = 0.35; return m }
    static var sleepy: Mood { var m = Mood(); m.closed = true; m.browL = .angry; m.browR = .angry
        m.mouth = .frown; m.blush = 0.15; return m }
}

func brow(_ c: CGContext, _ ex: CGFloat, _ sx: CGFloat, _ k: BrowK, _ l: CGFloat) {
    let p: CGPath
    switch k {
    case .none: return
    case .flat: p = mk { $0.move(to: pt(ex - 15, -27)); $0.addLine(to: pt(ex + 15, -27)) }
    case .angry: p = mk { $0.move(to: pt(ex + sx * 16, -22)); $0.addLine(to: pt(ex - sx * 16, -33)) }  // low at the nose side
    case .up: p = mk { $0.move(to: pt(ex - 14, -33)); $0.addQuadCurve(to: pt(ex + 15, -39), control: pt(ex, -50)) }
    case .sad: p = mk { $0.move(to: pt(ex - sx * 16, -24)); $0.addLine(to: pt(ex + sx * 16, -33)) }
    }
    stroke(c, p, P.ink, l * 1.35)
}

func shadesProp(_ c: CGContext, _ l: CGFloat) {
    let lenses = [rrect(-56, -16, -6, 16, 11), rrect(6, -16, 56, 16, 11)]
    for r in lenses {
        shape(c, r, V.darkInk, l * 1.1)
    }
    stroke(c, mk { $0.move(to: pt(-6, -6)); $0.addQuadCurve(to: pt(6, -6), control: pt(0, -11)) }, P.ink, l * 1.4)
    for sx: CGFloat in [-1, 1] {
        stroke(c, mk { $0.move(to: pt(sx * 50, -9)); $0.addLine(to: pt(sx * 40, -3)) }, P.lavender, l * 0.9)  // glint
        stroke(c, mk { $0.move(to: pt(sx * 56, -7)); $0.addLine(to: pt(sx * 84, -10)) }, P.ink, l * 1.1)    // arms
    }
}

/// Grumpy cat head centred on (cx, cy); head ellipse is 86x64 at scale 1.
func drawHead(_ c: CGContext, _ cx: CGFloat, _ cy: CGFloat, _ s: CGFloat, lw: CGFloat, mood m: Mood,
              fur: CGColor = P.cream, patch: CGColor? = P.peach) {
    c.saveGState(); c.translateBy(x: cx, y: cy); c.scaleBy(x: s, y: s)
    let l = lw / s
    // ears: flat "airplane" ears when grumpy, perked otherwise
    for sx: CGFloat in [-1, 1] {
        if m.flatEars {
            shape(c, poly([pt(sx * 30, -58), pt(sx * 104, -46), pt(sx * 80, -12)]), fur, l)
            fill(c, poly([pt(sx * 46, -50), pt(sx * 90, -44), pt(sx * 76, -26)]), P.pink)
        } else {
            shape(c, poly([pt(sx * 80, -8), pt(sx * 76, -84), pt(sx * 22, -54)]), fur, l)
            fill(c, poly([pt(sx * 68, -22), pt(sx * 66, -66), pt(sx * 38, -49)]), P.pink)
        }
    }
    let head = ell(0, 0, 86, 64)
    fill(c, head, fur)
    if let pc = patch {
        c.saveGState(); c.addPath(head); c.clip()
        fill(c, ell(-52, -52, 56, 34), pc); fill(c, ell(66, -48, 30, 22), pc)
        c.restoreGState()
    }
    stroke(c, head, P.ink, l)
    for dx: CGFloat in [-11, 0, 11] {
        stroke(c, mk { $0.move(to: pt(dx, -60)); $0.addLine(to: pt(dx * 0.8, dx == 0 ? -42 : -47)) }, P.lavender, l * 1.1)
    }
    // eyes
    if !m.shades || true {
        for sx: CGFloat in [-1, 1] {
            let ex = sx * 34
            let lid = sx < 0 ? m.lidL : m.lidR, tilt = sx < 0 ? m.tiltL : m.tiltR
            if m.closed {
                stroke(c, mk { $0.move(to: pt(ex - 15, -2)); $0.addQuadCurve(to: pt(ex + 15, -2), control: pt(ex, 13)) }, P.ink, l * 1.4)
                for k: CGFloat in [0, 1] { stroke(c, mk { $0.move(to: pt(ex + sx * (10 + k * 3), 4 + k * 3)); $0.addLine(to: pt(ex + sx * (14 + k * 4), 7 + k * 5)) }, P.ink, l * 0.8) }
            } else {
                let eye = ell(ex, 0, 15, 15)
                fill(c, eye, P.white)
                c.saveGState(); c.addPath(eye); c.clip()
                fill(c, ell(ex + m.look.x, m.look.y, 7.5, 9.5), P.ink)
                fill(c, ell(ex + m.look.x - 2.5, m.look.y - 3, 2.6, 2.6), P.white)
                // lid with slanted edge: inner end (toward nose) lower when tilt > 0
                let base = -16 + 31 * lid
                let yin = base + tilt, yout = base - tilt
                let xin = ex - sx * 20, xout = ex + sx * 20
                fill(c, poly([pt(xout - sx * 0, -20), pt(xin, -20), pt(xin, yin), pt(xout, yout)]), fur)
                c.restoreGState()
                stroke(c, mk { $0.move(to: pt(ex - sx * 15, yin)); $0.addLine(to: pt(ex + sx * 15, yout)) }, P.ink, l * 1.3)
                stroke(c, eye, P.ink, l * 0.7)
            }
            brow(c, ex, sx, sx < 0 ? m.browL : m.browR, l)
        }
    }
    if m.shades { shadesProp(c, l) }
    // blush
    for sx: CGFloat in [-1, 1] { fill(c, ell(sx * 60, 24, 14, 8), col(0xFF7F7F, m.blush)) }
    // nose + mouth
    shape(c, poly([pt(-6, 9), pt(6, 9), pt(0, 17)]), P.coral, l * 0.7)
    let ms = l * 1.0
    func line(_ f: (CGMutablePath) -> Void) { stroke(c, mk(f), P.ink, ms) }
    switch m.mouth {
    case .frown:
        line { $0.move(to: pt(0, 17)); $0.addLine(to: pt(0, 21))
            $0.move(to: pt(0, 21)); $0.addQuadCurve(to: pt(-13, 29), control: pt(-9, 20))
            $0.move(to: pt(0, 21)); $0.addQuadCurve(to: pt(13, 29), control: pt(9, 20)) }
    case .flat:
        line { $0.move(to: pt(0, 17)); $0.addLine(to: pt(0, 22)); $0.move(to: pt(-12, 24)); $0.addLine(to: pt(12, 24)) }
    case .wavy:
        line { $0.move(to: pt(0, 17)); $0.addLine(to: pt(0, 21)); $0.move(to: pt(-15, 27))
            $0.addQuadCurve(to: pt(-5, 25), control: pt(-10, 20)); $0.addQuadCurve(to: pt(5, 27), control: pt(0, 32))
            $0.addQuadCurve(to: pt(15, 25), control: pt(10, 20)) }
    case .smirk:
        line { $0.move(to: pt(0, 17)); $0.addLine(to: pt(0, 21)); $0.move(to: pt(0, 21)); $0.addQuadCurve(to: pt(-11, 24), control: pt(-6, 25))
            $0.move(to: pt(0, 21)); $0.addQuadCurve(to: pt(19, 12), control: pt(10, 27)) }
        fill(c, poly([pt(8, 22), pt(13, 19), pt(12, 26)]), P.white)
    case .grin:
        let mo = mk { $0.move(to: pt(-17, 20)); $0.addQuadCurve(to: pt(17, 20), control: pt(0, 24))
            $0.addQuadCurve(to: pt(-17, 20), control: pt(0, 50)); $0.closeSubpath() }
        fill(c, mo, col(0xE0556B))
        c.saveGState(); c.addPath(mo); c.clip(); fill(c, ell(0, 42, 10, 8), P.pink); c.restoreGState()
        fill(c, poly([pt(-11, 21.5), pt(-5, 22.5), pt(-8, 29)]), P.white); fill(c, poly([pt(11, 21.5), pt(5, 22.5), pt(8, 29)]), P.white)
        stroke(c, mo, P.ink, ms)
    case .yawn:
        let mo = ell(0, 31, 11, 12)
        fill(c, mo, col(0xE0556B)); c.saveGState(); c.addPath(mo); c.clip(); fill(c, ell(0, 40, 8, 6), P.pink); c.restoreGState()
        stroke(c, mo, P.ink, ms)
    case .blep:
        line { $0.move(to: pt(0, 17)); $0.addLine(to: pt(0, 21)); $0.move(to: pt(-12, 24)); $0.addLine(to: pt(12, 24)) }
        shape(c, mk { $0.move(to: pt(-1, 24)); $0.addLine(to: pt(-1, 31)); $0.addArc(center: pt(3, 31), radius: 4.5, startAngle: .pi, endAngle: 0, clockwise: true); $0.addLine(to: pt(7, 24)) }, P.pink, ms * 0.8)
    }
    if m.sweat {
        let d = mk { $0.move(to: pt(78, -34)); $0.addQuadCurve(to: pt(86, -20), control: pt(84, -27))
            $0.addArc(center: pt(78, -18), radius: 8, startAngle: 0.3, endAngle: .pi - 0.3, clockwise: false)
            $0.addQuadCurve(to: pt(78, -34), control: pt(72, -27)); $0.closeSubpath() }
        shape(c, d, P.blue, l * 0.8)
    }
    if m.whiskers {
        for sx: CGFloat in [-1, 1] {
            for (dy, ang) in [(-2.0, -0.18), (6.0, 0.0), (14.0, 0.18)] as [(CGFloat, CGFloat)] {
                let x0 = sx * 80, y0 = 18 + dy
                stroke(c, mk { $0.move(to: pt(x0, y0)); $0.addLine(to: pt(x0 + sx * 28 * cos(ang), y0 + 28 * sin(ang) + dy * 0.1)) }, P.ink, l * 0.6)
            }
        }
    }
    if m.bow {
        let bx: CGFloat = m.flatEars ? -64 : -62, by: CGFloat = m.flatEars ? -50 : -58
        shape(c, poly([pt(bx, by), pt(bx - 22, by - 12), pt(bx - 22, by + 10)]), P.coral, l * 0.8)
        shape(c, poly([pt(bx, by), pt(bx + 22, by - 12), pt(bx + 22, by + 10)]), P.coral, l * 0.8)
        shape(c, ell(bx, by, 6, 6), col(0xFF9A9A), l * 0.8)
    }
    c.restoreGState()
}

// MARK: body / limbs / props
/// Paw ellipse with toe dots at (x, y), scale s.
func paw(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, lw: CGFloat, rot: CGFloat = 0) {
    c.saveGState(); c.translateBy(x: x, y: y); c.rotate(by: rot); c.scaleBy(x: s, y: s)
    let l = lw / s
    shape(c, ell(0, 0, 17, 12), P.cream, l)
    fill(c, ell(0, 3, 5.5, 4), P.pink)
    for dx: CGFloat in [-8, 0, 8] { fill(c, ell(dx, -4, 2.4, 2.9), P.pink) }
    c.restoreGState()
}
/// Tube arm from a to b with a paw at b.
func arm(_ c: CGContext, from a: CGPoint, to b: CGPoint, bend: CGFloat, w: CGFloat, lw: CGFloat, pawScale: CGFloat) {
    let mid = pt((a.x + b.x) / 2, (a.y + b.y) / 2 + bend)
    tube(c, mk { $0.move(to: a); $0.addQuadCurve(to: b, control: mid) }, P.cream, w, lw)
    paw(c, b.x, b.y, pawScale, lw: lw, rot: atan2(b.y - a.y, b.x - a.x) * 0.5)
}
/// Chubby body. Centre (cx, cy); rx ~ 66, ry ~ 44 at scale 1. Collar + bell on top.
func catBody(_ c: CGContext, _ cx: CGFloat, _ cy: CGFloat, _ s: CGFloat, lw: CGFloat, collar: Bool = true) {
    c.saveGState(); c.translateBy(x: cx, y: cy); c.scaleBy(x: s, y: s)
    let l = lw / s
    let b = ell(0, 0, 68, 46)
    fill(c, b, P.cream)
    c.saveGState(); c.addPath(b); c.clip(); fill(c, ell(50, 10, 34, 44), P.peach); fill(c, ell(-48, 30, 24, 20), P.peach); c.restoreGState()
    stroke(c, b, P.ink, l)
    if collar {
        let cl = mk { $0.move(to: pt(-32, -36)); $0.addQuadCurve(to: pt(32, -36), control: pt(0, -16)) }
        tube(c, cl, P.coral, 7, l * 0.75)
        shape(c, ell(0, -24, 8, 8), P.gold, l * 0.8)
        stroke(c, mk { $0.move(to: pt(-6, -23)); $0.addLine(to: pt(6, -23)) }, P.ink, l * 0.55)
        fill(c, ell(0, -20, 1.8, 1.8), P.ink)
    }
    c.restoreGState()
}
func tailCurve(_ c: CGContext, _ a: CGPoint, _ b: CGPoint, ctrl1: CGPoint, ctrl2: CGPoint, w: CGFloat, lw: CGFloat) {
    let p = mk { $0.move(to: a); $0.addCurve(to: b, control1: ctrl1, control2: ctrl2) }
    tube(c, p, P.cream, w, lw)
    // peach tip
    let tip = mk { $0.move(to: pt(b.x + (ctrl2.x - b.x) * 0.0, b.y)); $0.addLine(to: pt(b.x + (ctrl2.x - b.x) * 0.18, b.y + (ctrl2.y - b.y) * 0.18)) }
    stroke(c, tip, P.peach, w)
}

func mug(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, rot: CGFloat, lw: CGFloat, body: CGColor = P.mint) {
    c.saveGState(); c.translateBy(x: x, y: y); c.rotate(by: rot); c.scaleBy(x: s, y: s)
    let l = lw / s
    stroke(c, mk { $0.move(to: pt(13, -6)); $0.addCurve(to: pt(13, 10), control1: pt(28, -8), control2: pt(28, 12)) }, P.ink, 6 + l * 2)
    stroke(c, mk { $0.move(to: pt(13, -6)); $0.addCurve(to: pt(13, 10), control1: pt(28, -8), control2: pt(28, 12)) }, body, 6)
    shape(c, mk { $0.move(to: pt(-15, -16)); $0.addLine(to: pt(15, -16)); $0.addLine(to: pt(12, 14)); $0.addQuadCurve(to: pt(-12, 14), control: pt(0, 22)); $0.closeSubpath() }, body, l)
    fill(c, ell(0, -16, 15, 4.5), col(0xF6F0FF)); stroke(c, ell(0, -16, 15, 4.5), P.ink, l * 0.8)
    // little paw-print on the mug
    pawPrint(c, 0, 1, 0.5, P.white.copy(alpha: 0.85)!)
    c.restoreGState()
}
func yarnBall(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ r: CGFloat, _ f: CGColor, lw: CGFloat) {
    shape(c, ell(x, y, r, r), f, lw)
    c.saveGState(); c.addPath(ell(x, y, r, r)); c.clip()
    for k in -2...2 {
        let o = CGFloat(k) * r * 0.42
        stroke(c, mk { $0.move(to: pt(x - r, y + o + r * 0.5)); $0.addQuadCurve(to: pt(x + r, y + o - r * 0.5), control: pt(x, y + o)) }, P.ink, lw * 0.55)
    }
    c.restoreGState()
}
func usbStick(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, rot: CGFloat, lw: CGFloat, body: CGColor = P.blue) {
    c.saveGState(); c.translateBy(x: x, y: y); c.rotate(by: rot); c.scaleBy(x: s, y: s)
    let l = lw / s
    shape(c, rrect(-9, -4, 9, 22, 4), body, l)
    shape(c, rrect(-6, -17, 6, -3, 1.5), P.white, l)
    fill(c, rrect(-3.5, -14, -1.5, -9, 0.5), P.ink); fill(c, rrect(1.5, -14, 3.5, -9, 0.5), P.ink)
    fill(c, ell(0, 14, 2.4, 2.4), P.coral)
    c.restoreGState()
}
func wifiArcs(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, _ f: CGColor, lw: CGFloat) {
    for r in [8.0, 15.0, 22.0] as [CGFloat] {
        let a = mk { $0.addArc(center: pt(x, y), radius: r * s, startAngle: -.pi * 0.82, endAngle: -.pi * 0.18, clockwise: false) }
        stroke(c, a, P.ink, lw + 2.4); stroke(c, a, f, lw)
    }
    shape(c, ell(x, y, 3 * s, 3 * s), f, 1.8)
}
func fishProp(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, rot: CGFloat, lw: CGFloat) {
    c.saveGState(); c.translateBy(x: x, y: y); c.rotate(by: rot)
    fish(c, 0, 0, s, col(0x9FD7F0), lw)
    c.restoreGState()
}
func speechBubble(_ c: CGContext, _ r: CGRect, tail: CGPoint, lw: CGFloat) {
    shape(c, poly([pt(r.minX + r.width * 0.55, r.maxY - 2), pt(r.minX + r.width * 0.75, r.maxY - 2), tail]), P.cream, lw)
    shape(c, rrect(r.minX, r.minY, r.maxX, r.maxY, r.height / 2.2), P.cream, lw)
    fill(c, CGRect(x: r.minX + r.width * 0.55 + lw * 0.8, y: r.maxY - lw - 1.2, width: r.width * 0.2 - lw * 1.6, height: lw + 2.4).path(), P.cream)
}
func trophy(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, lw: CGFloat) {
    c.saveGState(); c.translateBy(x: x, y: y); c.scaleBy(x: s, y: s)
    let l = lw / s
    for sx: CGFloat in [-1, 1] {
        stroke(c, mk { $0.move(to: pt(sx * 11, -14)); $0.addQuadCurve(to: pt(sx * 12, -2), control: pt(sx * 21, -10)) }, P.ink, l * 2.4)
        stroke(c, mk { $0.move(to: pt(sx * 11, -14)); $0.addQuadCurve(to: pt(sx * 12, -2), control: pt(sx * 21, -10)) }, P.gold, l * 0.9)
    }
    shape(c, mk { $0.move(to: pt(-12, -20)); $0.addLine(to: pt(12, -20)); $0.addQuadCurve(to: pt(0, 0), control: pt(12, -2))
        $0.addQuadCurve(to: pt(-12, -20), control: pt(-12, -2)); $0.closeSubpath() }, P.gold, l)
    shape(c, rrect(-3, -1, 3, 8, 1), P.gold, l)
    shape(c, rrect(-10, 8, 10, 15, 2), col(0xE8A8C8), l)
    fill(c, CGRect(x: -6, y: -17, width: 3, height: 9).path(), P.white.copy(alpha: 0.7)!)
    c.restoreGState()
}
func zzz(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, size: CGFloat) {
    drawText(c, "Z", x, y, size: size, color: P.lavender, font: "Arial-BoldMT")
    drawText(c, "z", x + size * 0.8, y - size * 0.8, size: size * 0.75, color: P.blue, font: "Arial-BoldMT")
    drawText(c, "z", x + size * 1.45, y - size * 1.45, size: size * 0.55, color: P.pink, font: "Arial-BoldMT")
}
func annoyMark(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat) {
    // vein-pop: four curved ticks in coral
    for (a, b) in [(-1.0, -1.0), (1.0, -1.0), (-1.0, 1.0), (1.0, 1.0)] as [(CGFloat, CGFloat)] {
        let p = mk { $0.move(to: pt(x + a * 2 * s, y + b * 2 * s)); $0.addQuadCurve(to: pt(x + a * 8 * s, y + b * 8 * s), control: pt(x + a * 8 * s, y + b * 2 * s)) }
        stroke(c, p, P.ink, 3.6 * s); stroke(c, p, P.coral, 2 * s)
    }
}
func motionLines(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ dx: CGFloat, _ dy: CGFloat, _ n: Int, col: CGColor, w: CGFloat) {
    for i in 0..<n {
        let o = CGFloat(i) * 7
        stroke(c, mk { $0.move(to: pt(x + o * 0.3, y + o)); $0.addLine(to: pt(x + dx + o * 0.3, y + dy + o)) }, col, w)
    }
}
func glow(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ r: CGFloat, _ color: UInt32, _ a: CGFloat) {
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    let g = CGGradient(colorsSpace: cs, colors: [col(color, a), col(color, 0)] as CFArray, locations: [0, 1])!
    c.drawRadialGradient(g, startCenter: pt(x, y), startRadius: 0, endCenter: pt(x, y), endRadius: r, options: [])
}

// MARK: mascots
func shadow(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ rx: CGFloat, _ ry: CGFloat, _ a: CGFloat = 0.35) {
    fill(c, ell(x, y, rx, ry), col(0x120A22, a))
}

/// ui_img_mascot_png 269x203: grumpy kitty in shades at a laptop; USB stick, wifi, "meh" bubble, trophy.
func drawMascotHome() -> CGContext {
    let c = makeCtx(269, 203); let lw: CGFloat = 3.6
    glow(c, 134, 110, 120, 0xCDB8F0, 0.18)
    shadow(c, 134, 197, 112, 8)
    tailCurve(c, pt(190, 168), pt(250, 126), ctrl1: pt(238, 188), ctrl2: pt(264, 150), w: 13, lw: 3)
    catBody(c, 134, 152, 1.0, lw: lw)
    var m = Mood.grumpy; m.shades = true; m.flatEars = true
    drawHead(c, 134, 86, 0.82, lw: lw, mood: m)
    // laptop (lid seen from the back) + base
    shape(c, rrect(70, 124, 198, 182, 8), col(0x504A70), lw)
    fill(c, rrect(76, 130, 192, 176, 5), col(0x5C5680))
    fish(c, 134, 153, 0.9, P.cream, 2.4)
    shape(c, rrect(58, 180, 210, 192, 6), P.lavender, lw)
    fill(c, ell(134, 186, 9, 2), col(0x8E7BC0))
    // paws gripping the lid
    paw(c, 104, 124, 0.95, lw: lw); paw(c, 164, 124, 0.95, lw: lw)
    // floating props
    glow(c, 40, 88, 30, 0xB5DCF5, 0.25)
    usbStick(c, 40, 88, 1.15, rot: -0.45, lw: 2.6)
    wifiArcs(c, 40, 54, 1.0, P.mint, lw: 3.2)
    sparkle(c, 70, 112, 5, P.gold, 1.8); sparkle(c, 238, 80, 6, P.pink, 1.8)
    // "meh" bubble
    speechBubble(c, CGRect(x: 186, y: 14, width: 72, height: 36), tail: pt(200, 66), lw: 3)
    drawText(c, "meh", 222, 40, size: 22, color: V.darkInk, font: "ArialRoundedMTBold", center: true)
    // tiny trophy and mug
    trophy(c, 24, 176, 1.0, lw: 2.4)
    mug(c, 244, 172, 0.8, rot: 0, lw: 2.6, body: P.coral)
    return c
}

/// ui_img_mascot_02_png 147x103: loaf pose with a tiny door-hanger sign (shapes only).
func drawMascotLoaf() -> CGContext {
    let c = makeCtx(147, 103); let lw: CGFloat = 3
    shadow(c, 72, 98, 62, 5)
    // loaf body
    let body = rrect(14, 44, 132, 98, 30)
    shape(c, body, P.cream, lw)
    c.saveGState(); c.addPath(body); c.clip(); fill(c, ell(112, 80, 30, 34), P.peach); fill(c, ell(34, 100, 24, 14), P.peach); c.restoreGState()
    stroke(c, body, P.ink, lw)
    tube(c, mk { $0.move(to: pt(128, 90)); $0.addQuadCurve(to: pt(142, 70), control: pt(144, 88)) }, P.cream, 9, 2.4)
    // collar + bell peeking under head
    tube(c, mk { $0.move(to: pt(40, 62)); $0.addQuadCurve(to: pt(88, 62), control: pt(64, 74)) }, P.coral, 6, 2.2)
    shape(c, ell(64, 70, 5.5, 5.5), P.gold, 2)
    var m = Mood.unimpressed; m.lidL = 0.55; m.lidR = 0.55
    drawHead(c, 62, 46, 0.55, lw: 2.6, mood: m)
    // tucked paws
    paw(c, 38, 90, 0.7, lw: 2.4); paw(c, 88, 90, 0.7, lw: 2.4)
    // door-hanger sign on a little knob (shapes only: no-entry symbol)
    stroke(c, mk { $0.move(to: pt(118, 6)); $0.addLine(to: pt(118, 16)) }, P.ink, 3)
    c.saveGState(); c.translateBy(x: 120, y: 12); c.rotate(by: 0.12)
    let sign = mk { p in
        p.addPath(rrect(-17, 0, 17, 52, 7))
    }
    shape(c, sign, P.pink, 2.6)
    shape(c, ell(0, 10, 6, 6), col(0x2B2040), 2)       // knob hole
    fill(c, ell(0, 10, 3.5, 3.5), col(0x3A2A58))
    shape(c, ell(0, 34, 11, 11), P.cream, 2.2)
    stroke(c, ell(0, 34, 7, 7), P.coral, 2.6)
    stroke(c, mk { $0.move(to: pt(-5, 29)); $0.addLine(to: pt(5, 39)) }, P.coral, 2.6)
    c.restoreGState()
    return c
}

/// ui_img_mascot_recovery_png 254x129: annoyed kitty tangled in yarn.
func drawMascotYarn() -> CGContext {
    let c = makeCtx(254, 129); let lw: CGFloat = 3.2
    shadow(c, 120, 124, 100, 6)
    // loose strand to the ball (behind)
    let strand = mk { $0.move(to: pt(150, 98)); $0.addCurve(to: pt(214, 112), control1: pt(172, 130), control2: pt(196, 98)) }
    stroke(c, strand, P.ink, 7); stroke(c, strand, P.coral, 3.6)
    tailCurve(c, pt(158, 98), pt(232, 40), ctrl1: pt(214, 112), ctrl2: pt(242, 74), w: 11, lw: 2.6)
    catBody(c, 112, 92, 0.78, lw: lw, collar: true)
    var m = Mood.annoyed; m.sweat = true
    drawHead(c, 100, 46, 0.62, lw: lw, mood: m)
    // paws up, one stuck in yarn
    paw(c, 62, 76, 0.7, lw: 2.6, rot: -0.3); paw(c, 150, 76, 0.7, lw: 2.6, rot: 0.3)
    // yarn wraps around body
    let wraps: [(CGColor, [CGPoint])] = [
        (P.coral, [pt(60, 100), pt(112, 130), pt(166, 100)]),
        (P.lavender, [pt(62, 90), pt(112, 118), pt(164, 88)]),
        (P.mint, [pt(66, 80), pt(112, 106), pt(160, 78)]),
    ]
    for (colr, ps) in wraps {
        let p = mk { $0.move(to: ps[0]); $0.addQuadCurve(to: ps[2], control: ps[1]) }
        stroke(c, p, P.ink, 6.4); stroke(c, p, colr, 3.4)
    }
    // diagonal criss-cross
    let x1 = mk { $0.move(to: pt(64, 84)); $0.addQuadCurve(to: pt(160, 112), control: pt(112, 96)) }
    stroke(c, x1, P.ink, 6.4); stroke(c, x1, P.pink, 3.4)
    // knot + loose ends
    shape(c, ell(152, 96, 7, 6), P.pink, 2.2)
    yarnBall(c, 214, 98, 20, P.coral, lw: 2.8)
    annoyMark(c, 148, 22, 1.2)
    sparkle(c, 30, 24, 5, P.gold, 1.6)
    return c
}

/// ui_img_mascot_update_png 292x196: kitty pushes a mug off the table while staring at the viewer.
func drawMascotMug() -> CGContext {
    let c = makeCtx(292, 196); let lw: CGFloat = 3.6
    // table (deep mauve so white text stays readable on it)
    let top = poly([pt(8, 140), pt(228, 140), pt(240, 154), pt(0, 154)])
    shape(c, rrect(0, 140, 240, 196, 6), V.mauve, lw)
    fill(c, rrect(4, 144, 236, 160, 3), V.mauveLite)
    _ = top
    stroke(c, mk { $0.move(to: pt(6, 160)); $0.addLine(to: pt(234, 160)) }, V.darkInk, 2.4)
    for x: CGFloat in [40, 110, 180] { stroke(c, mk { $0.move(to: pt(x, 168)); $0.addQuadCurve(to: pt(x + 36, 168), control: pt(x + 18, 174)) }, col(0x4A3556), 2) }
    shadow(c, 112, 141, 100, 4, 0.3)
    // cat behind table
    tailCurve(c, pt(52, 136), pt(14, 70), ctrl1: pt(10, 130), ctrl2: pt(-2, 96), w: 12, lw: 3)
    catBody(c, 112, 112, 1.0, lw: lw)
    var m = Mood.unimpressed; m.lidL = 0.5; m.lidR = 0.5; m.look = pt(0, 4); m.browR = .up; m.browL = .flat
    drawHead(c, 112, 62, 0.88, lw: lw, mood: m)
    // pushing arm reaching to the mug
    arm(c, from: pt(150, 124), to: pt(218, 134), bend: -14, w: 17, lw: lw, pawScale: 0.9)
    paw(c, 80, 132, 0.95, lw: lw)
    // mug tipping over the edge, falling droplets + motion
    mug(c, 244, 148, 1.15, rot: 0.75, lw: 3, body: P.mint)
    for (x, y, r) in [(264.0, 168.0, 4.0), (272.0, 182.0, 3.0), (252.0, 184.0, 3.4)] as [(CGFloat, CGFloat, CGFloat)] {
        fill(c, ell(x, y, r, r * 1.3), col(0xF6F0FF))
    }
    motionLines(c, 232, 168, 0, 12, 3, col: P.lavender, w: 2.2)
    stroke(c, mk { $0.move(to: pt(226, 118)); $0.addLine(to: pt(234, 110)); $0.move(to: pt(232, 124)); $0.addLine(to: pt(244, 118)) }, P.lavender, 2.4)
    sparkle(c, 266, 36, 6, P.gold, 1.8); sparkle(c, 34, 34, 5, P.pink, 1.8)
    return c
}

/// ui_img_mascot_usb_msc_png 299x206: kitty in a too-small cardboard box with a USB drive.
func drawMascotBox() -> CGContext {
    let c = makeCtx(299, 206); let lw: CGFloat = 3.6
    shadow(c, 150, 200, 120, 7)
    // back flaps
    shape(c, poly([pt(66, 118), pt(30, 84), pt(84, 82), pt(98, 112)]), V.tanDark, lw)
    shape(c, poly([pt(232, 118), pt(268, 84), pt(214, 82), pt(200, 112)]), V.tanDark, lw)
    tailCurve(c, pt(232, 140), pt(276, 86), ctrl1: pt(268, 150), ctrl2: pt(290, 120), w: 12, lw: 3)
    // cat squished in the box: body + head above rim
    catBody(c, 150, 128, 1.05, lw: lw)
    var m = Mood.grumpy; m.lidL = 0.55; m.lidR = 0.55; m.blush = 0.35
    drawHead(c, 150, 70, 0.9, lw: lw, mood: m)
    // paws gripping the USB drive, held up
    usbStick(c, 150, 118, 1.5, rot: 0.0, lw: 3, body: P.mint)
    paw(c, 128, 130, 0.9, lw: lw); paw(c, 172, 130, 0.9, lw: lw)
    // box front
    let front = rrect(52, 130, 248, 198, 6)
    shape(c, front, V.tan, lw)
    stroke(c, mk { $0.move(to: pt(142, 130)); $0.addLine(to: pt(142, 198)); $0.move(to: pt(158, 130)); $0.addLine(to: pt(158, 198)) }, V.tanDark, 1.5)
    fill(c, CGRect(x: 144, y: 131.5, width: 12, height: 66).path(), col(0xF2DDBC))   // tape
    // front flaps folded out
    shape(c, poly([pt(52, 130), pt(40, 150), pt(60, 158), pt(70, 130)]), V.tanDark, lw * 0.8)
    shape(c, poly([pt(248, 130), pt(260, 150), pt(240, 158), pt(230, 130)]), V.tanDark, lw * 0.8)
    // label: fragile wine glass (shape-only) and arrows
    shape(c, rrect(172, 150, 232, 188, 5), P.cream, 2.4)
    stroke(c, mk { $0.move(to: pt(192, 158)); $0.addLine(to: pt(212, 158)); $0.addQuadCurve(to: pt(202, 172), control: pt(212, 170))
        $0.addQuadCurve(to: pt(192, 158), control: pt(192, 170)); $0.move(to: pt(202, 172)); $0.addLine(to: pt(202, 180)); $0.move(to: pt(195, 180)); $0.addLine(to: pt(209, 180)) }, P.coral, 2.4)
    // up arrows
    for x: CGFloat in [74, 106] {
        stroke(c, mk { $0.move(to: pt(x, 182)); $0.addLine(to: pt(x, 152)); $0.move(to: pt(x - 8, 162)); $0.addLine(to: pt(x, 152)); $0.addLine(to: pt(x + 8, 162)) }, V.mauve, 4)
    }
    // "it fits" squeeze marks
    for (a, b) in [(-1.0, 1.0), (1.0, 1.0)] as [(CGFloat, CGFloat)] {
        stroke(c, mk { $0.move(to: pt(150 + a * 96, 100)); $0.addLine(to: pt(150 + a * 104, 94)) ; $0.move(to: pt(150 + a * 96, 110)); $0.addLine(to: pt(150 + a * 108, 110)) }, P.lavender, 2.4)
        _ = b
    }
    sparkle(c, 40, 46, 6, P.gold, 1.8); sparkle(c, 262, 40, 6, P.pink, 1.8)
    return c
}

/// ui_img_message_box_mascot_png 106x93: small grumpy kitty peeking up from the bottom edge.
func drawMascotPeek() -> CGContext {
    let c = makeCtx(106, 93); let lw: CGFloat = 3
    shadow(c, 53, 90, 40, 3, 0.3)
    var m = Mood.grumpy; m.lidL = 0.58; m.lidR = 0.58; m.browL = .up; m.browR = .flat
    drawHead(c, 53, 58, 0.5, lw: lw, mood: m)
    // paws gripping the bottom edge
    paw(c, 22, 86, 0.62, lw: 2.4); paw(c, 84, 86, 0.62, lw: 2.4)
    sparkle(c, 94, 20, 4, P.gold, 1.4)
    return c
}

/// Expression sampler (not an asset): the six moods as busts.
func drawExpressionBust(_ k: Int) -> CGContext {
    let c = makeCtx(150, 130); let lw: CGFloat = 3
    var m: Mood
    switch k {
    case 0: m = .grumpy
    case 1: m = .smug
    case 2: m = .unimpressed
    case 3: m = .annoyed
    case 4: m = .triumphant
    default: m = .sleepy
    }
    drawHead(c, 75, 74, 0.62, lw: lw, mood: m)
    catBody(c, 75, 128, 0.7, lw: lw)
    switch k {
    case 1: sparkle(c, 128, 28, 6, P.gold, 1.6)
    case 3:
        annoyMark(c, 124, 24, 1.0)
        let ps = [pt(52, 104), pt(76, 112), pt(100, 104)]
        for (i, colr) in [P.coral, P.lavender, P.mint].enumerated() {
            let p = mk { $0.move(to: pt(34 + CGFloat(i) * 4, 108 + CGFloat(i) * 6)); $0.addQuadCurve(to: pt(116 - CGFloat(i) * 4, 108 + CGFloat(i) * 6), control: pt(75, 120 + CGFloat(i) * 6)) }
            stroke(c, p, P.ink, 5.6); stroke(c, p, colr, 3)
            _ = ps
        }
    case 4:
        fishProp(c, 118, 100, 0.8, rot: -0.5, lw: 2.2); sparkle(c, 24, 28, 6, P.gold, 1.6); sparkle(c, 128, 30, 5, P.pink, 1.6)
    case 5: zzz(c, 106, 40, size: 15)
    default: break
    }
    return c
}

// MARK: app icons 70x70 (light inner disc so they work on lime, plum or light tiles)
func iconBase() -> CGContext {
    let c = makeCtx(70, 70)
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    c.saveGState(); c.addPath(ell(35, 35, 33.5, 33.5)); c.clip()
    let g = CGGradient(colorsSpace: cs, colors: [V.discTop, V.discBot] as CFArray, locations: [0, 1])!
    c.drawLinearGradient(g, start: pt(20, 0), end: pt(50, 70), options: [])
    c.restoreGState()
    stroke(c, ell(35, 35, 33.2, 33.2), P.ink, 2.2)
    return c
}
func miniCat(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, _ m: Mood) {
    var mm = m; mm.whiskers = false; mm.bow = true; mm.flatEars = false
    drawHead(c, x, y, s, lw: 2.1, mood: mm)
}
func bluetoothRune(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, _ color: CGColor, _ w: CGFloat) {
    let p = mk { $0.move(to: pt(x - 5 * s, y - 6 * s)); $0.addLine(to: pt(x + 5 * s, y + 6 * s)); $0.addLine(to: pt(x, y + 11 * s))
        $0.addLine(to: pt(x, y - 11 * s)); $0.addLine(to: pt(x + 5 * s, y - 6 * s)); $0.addLine(to: pt(x - 5 * s, y + 6 * s)) }
    stroke(c, p, color, w)
}

func drawIconDino() -> CGContext {
    let c = iconBase()
    // tiny dinosaur on the right
    let ink = P.ink
    c.saveGState(); c.translateBy(x: -4, y: 0)
    shape(c, mk { $0.move(to: pt(40, 56)); $0.addQuadCurve(to: pt(46, 42), control: pt(40, 46)); $0.addLine(to: pt(58, 40)); $0.addLine(to: pt(58, 50))
        $0.addLine(to: pt(54, 50)); $0.addQuadCurve(to: pt(52, 56), control: pt(54, 56)); $0.closeSubpath() }, P.mint, 2)
    shape(c, mk { $0.move(to: pt(40, 56)); $0.addLine(to: pt(30, 62)); $0.addLine(to: pt(44, 60)); $0.closeSubpath() }, P.mint, 1.8)  // tail
    shape(c, rrect(50, 28, 64, 41, 4), P.mint, 2)
    fill(c, ell(58, 33, 1.6, 1.8), ink)
    for x: CGFloat in [44, 49] { stroke(c, mk { $0.move(to: pt(x, 42)); $0.addLine(to: pt(x + 1, 38)) }, P.coral, 2) }
    stroke(c, mk { $0.move(to: pt(54, 44)); $0.addLine(to: pt(58, 45)) }, ink, 1.8)
    stroke(c, mk { $0.move(to: pt(48, 56)); $0.addLine(to: pt(48, 62)); $0.move(to: pt(54, 54)); $0.addLine(to: pt(54, 62)) }, ink, 2)
    c.restoreGState()
    // cat, unimpressed, looking at it
    var m = Mood.unimpressed; m.look = pt(8, 3)
    miniCat(c, 26, 42, 0.25, m)
    paw(c, 26, 60, 0.4, lw: 1.8)
    sparkle(c, 12, 14, 3.5, P.gold, 1.3)
    return c
}
func drawIconMatrix() -> CGContext {
    let c = iconBase()
    let g1 = col(0x2FBF6A), g2 = col(0x8BE8A8)
    for (x, y, n) in [(14.0, 8.0, 5), (24.0, 4.0, 3), (46.0, 5.0, 4), (56.0, 10.0, 5), (14.0, 40.0, 3), (58.0, 42.0, 3), (50.0, 50.0, 3)] as [(CGFloat, CGFloat, Int)] {
        for i in 0..<n {
            let yy = y + CGFloat(i) * 6.2
            if yy > 62 { continue }
            let a = CGFloat(i + 1) / CGFloat(n)
            stroke(c, mk { $0.move(to: pt(x, yy)); $0.addLine(to: pt(x, yy + 3.2)) }, i == n - 1 ? g2 : g1.copy(alpha: 0.35 + 0.65 * a)!, 2.8)
        }
    }
    var m = Mood.unimpressed; m.shades = true; m.mouth = .smirk; m.lidL = 0.5
    miniCat(c, 35, 40, 0.34, m)
    paw(c, 22, 62, 0.4, lw: 1.8); paw(c, 48, 62, 0.4, lw: 1.8)
    return c
}
func drawIconVU() -> CGContext {
    let c = iconBase()
    // mixing deck with meter bars
    shape(c, rrect(10, 50, 60, 64, 4), P.lavender, 2)
    for (i, (h, colr)) in [(8.0, P.mint), (5.0, P.pink), (9.0, P.gold), (6.0, P.coral)].enumerated() as EnumeratedSequence<[(CGFloat, CGColor)]> {
        fill(c, rrect(15 + CGFloat(i) * 11, 62 - h, 21 + CGFloat(i) * 11, 62, 1.5), colr)
    }
    var m = Mood.grumpy; m.mouth = .flat; m.lidL = 0.55; m.lidR = 0.55
    miniCat(c, 35, 33, 0.27, m)
    // headphones
    tube(c, mk { $0.move(to: pt(10, 34)); $0.addCurve(to: pt(60, 34), control1: pt(6, 4), control2: pt(64, 4)) }, P.coral, 2.2, 1.4)
    for x: CGFloat in [10, 60] { shape(c, rrect(x - 4.5, 28, x + 4.5, 44, 4), P.coral, 1.8); fill(c, rrect(x - 1.5, 31, x + 1.5, 41, 1.5), P.pink) }
    paw(c, 22, 50, 0.4, lw: 1.8); paw(c, 48, 50, 0.4, lw: 1.8)
    return c
}
func drawIconTV() -> CGContext {
    let c = iconBase()
    // antenna + tube TV
    stroke(c, mk { $0.move(to: pt(44, 18)); $0.addLine(to: pt(37, 6)); $0.move(to: pt(44, 18)); $0.addLine(to: pt(54, 7)) }, P.ink, 2)
    shape(c, rrect(24, 17, 63, 52, 8), P.lavender, 2.2)
    shape(c, rrect(28, 21, 52, 48, 6), V.screen, 1.8)
    // static / colour bars on screen
    c.saveGState(); c.addPath(rrect(28, 21, 52, 48, 6)); c.clip()
    for (i, colr) in [P.pink, P.peach, P.mint, P.blue, P.lavender].enumerated() { fill(c, CGRect(x: 28 + CGFloat(i) * 5, y: 21, width: 5, height: 27).path(), colr.copy(alpha: 0.9)!) }
    fill(c, CGRect(x: 28, y: 36, width: 24, height: 12).path(), col(0x2E2A44, 0.55))
    c.restoreGState()
    fill(c, ell(58, 26, 2, 2), P.coral); fill(c, ell(58, 33, 2, 2), P.mint)
    for x: CGFloat in [30, 56] { stroke(c, mk { $0.move(to: pt(x, 52)); $0.addLine(to: pt(x, 57)) }, P.ink, 2.4) }
    // cat in front, bottom-left, staring at the TV, holding a snack (fish)
    var m = Mood.unimpressed; m.look = pt(8, -4); m.lidL = 0.55; m.lidR = 0.55
    miniCat(c, 26, 50, 0.27, m)
    paw(c, 17, 64, 0.38, lw: 1.8)
    fishProp(c, 46, 62, 0.34, rot: -0.2, lw: 1.6)
    return c
}
func drawIconPC() -> CGContext {
    let c = iconBase()
    // monitor
    shape(c, rrect(11, 30, 59, 54, 4), V.slate, 2.2)
    shape(c, rrect(14, 33, 56, 51, 2), V.screen, 1.4)
    for (i, w) in [20.0, 28.0, 14.0].enumerated() as EnumeratedSequence<[CGFloat]> {
        stroke(c, mk { $0.move(to: pt(18, 38 + CGFloat(i) * 4.5)); $0.addLine(to: pt(18 + w, 38 + CGFloat(i) * 4.5)) }, [P.mint, P.pink, P.blue][i], 2)
    }
    stroke(c, mk { $0.move(to: pt(35, 54)); $0.addLine(to: pt(35, 60)) }, P.ink, 3); shape(c, rrect(25, 59, 45, 63, 2), V.slate, 1.8)
    // cat loafing on top
    shape(c, rrect(17, 22, 53, 32, 6), P.cream, 2.2)
    var m = Mood.unimpressed; m.lidL = 0.55; m.lidR = 0.55; m.look = pt(0, 4)
    miniCat(c, 31, 19, 0.21, m)
    tailCurve(c, pt(52, 28), pt(58, 44), ctrl1: pt(64, 28), ctrl2: pt(64, 38), w: 4, lw: 1.5)
    return c
}
func drawIconAir() -> CGContext {
    let c = iconBase()
    // swoosh trail
    let trail = mk { $0.move(to: pt(10, 56)); $0.addQuadCurve(to: pt(40, 22), control: pt(10, 30)) }
    c.saveGState(); c.setLineDash(phase: 0, lengths: [3, 4]); stroke(c, trail, P.lavender, 2.4); c.restoreGState()
    var m = Mood.unimpressed; m.look = pt(6, -4); m.lidL = 0.55; m.lidR = 0.55
    miniCat(c, 28, 46, 0.25, m)
    // mouse flown up by the paw
    c.saveGState(); c.translateBy(x: 49, y: 26); c.rotate(by: 0.5)
    shape(c, rrect(-9, -13, 9, 13, 8), P.lavender, 2)
    stroke(c, mk { $0.move(to: pt(-9, -2)); $0.addLine(to: pt(9, -2)); $0.move(to: pt(0, -13)); $0.addLine(to: pt(0, -2)) }, P.ink, 1.5)
    c.restoreGState()
    tube(c, mk { $0.move(to: pt(40, 32)); $0.addQuadCurve(to: pt(32, 56), control: pt(56, 50)) }, P.lavender, 1.2, 1.2)
    paw(c, 40, 56, 0.42, lw: 1.8)
    sparkle(c, 58, 52, 4, P.gold, 1.3); sparkle(c, 12, 14, 3.5, P.pink, 1.3)
    return c
}
func drawIconBLE() -> CGContext {
    let c = iconBase()
    var m = Mood.smug; m.look = pt(8, -1)
    miniCat(c, 28, 44, 0.27, m)
    paw(c, 26, 62, 0.4, lw: 1.8)
    // spamming bluetooth: big rune + echoes + waves
    bluetoothRune(c, 50, 30, 1.2, P.ink, 5.2); bluetoothRune(c, 50, 30, 1.2, col(0x6FA8FF), 3)
    bluetoothRune(c, 38, 14, 0.5, col(0x6FA8FF), 2); bluetoothRune(c, 62, 54, 0.5, col(0x6FA8FF), 2)
    for (r, colr) in [(8.0, P.coral), (14.0, P.pink)] as [(CGFloat, CGColor)] {
        let a = mk { $0.addArc(center: pt(46, 30), radius: r + 8, startAngle: -0.7, endAngle: 0.7, clockwise: false) }
        _ = a
        let b = mk { $0.addArc(center: pt(46, 30), radius: r + 10, startAngle: -0.8, endAngle: 0.8, clockwise: false) }
        stroke(c, b, P.ink, 3.6); stroke(c, b, colr, 1.8)
    }
    return c
}
func drawIconBadUSB2() -> CGContext {
    let c = iconBase()
    var m = Mood.smug; m.look = pt(5, 3); m.lidL = 0.5; m.lidR = 0.5; m.flatEars = false; m.browL = .angry; m.browR = .angry; m.mouth = .flat
    miniCat(c, 30, 40, 0.27, m)
    // devil horns
    for sx: CGFloat in [-1, 1] { shape(c, poly([pt(30 + sx * 9, 21), pt(30 + sx * 12, 10), pt(30 + sx * 3, 19)]), P.coral, 1.6) }
    // USB stick carried in the mouth like a fish
    c.saveGState(); c.translateBy(x: 48, y: 47); c.rotate(by: -1.45)
    shape(c, rrect(-8, -4, 8, 16, 3), P.blue, 1.8); shape(c, rrect(-5, -15, 5, -3, 1.5), P.white, 1.8)
    fill(c, rrect(-3, -12, -1.2, -7, 0.4), P.ink); fill(c, rrect(1.2, -12, 3, -7, 0.4), P.ink); fill(c, ell(0, 10, 2, 2), P.coral)
    c.restoreGState()
    paw(c, 24, 61, 0.42, lw: 1.8); paw(c, 38, 61, 0.42, lw: 1.8)
    return c
}
func drawIconIR() -> CGContext {
    let c = iconBase()
    var m = Mood.grumpy; m.look = pt(7, -2)
    miniCat(c, 28, 44, 0.27, m)
    // remote held out
    c.saveGState(); c.translateBy(x: 48, y: 36); c.rotate(by: 0.7)
    shape(c, rrect(-7, -16, 7, 17, 4), P.lavender, 2)
    fill(c, ell(0, -9, 2.8, 2.8), P.coral)
    for (dx, dy) in [(-2.6, 0.0), (2.6, 0.0), (-2.6, 6.0), (2.6, 6.0)] as [(CGFloat, CGFloat)] { fill(c, ell(dx, dy, 1.8, 1.8), P.cream); stroke(c, ell(dx, dy, 1.8, 1.8), P.ink, 0.8) }
    c.restoreGState()
    paw(c, 38, 56, 0.42, lw: 1.8, rot: -0.4)
    for (r, colr) in [(6.0, P.coral), (11.0, P.pink), (16.0, P.peach)] as [(CGFloat, CGColor)] {
        let a = mk { $0.addArc(center: pt(56, 12), radius: r, startAngle: 0.2, endAngle: 1.5, clockwise: false) }
        _ = a
        let b = mk { $0.addArc(center: pt(44, 28), radius: r + 6, startAngle: -1.2, endAngle: -0.3, clockwise: false) }
        stroke(c, b, P.ink, 3.4); stroke(c, b, colr, 1.8)
    }
    return c
}
func drawIconMedia() -> CGContext {
    let c = iconBase()
    // giant play button, paw pressing it
    shape(c, ell(43, 44, 19, 19), P.coral, 2.4)
    shape(c, poly([pt(37, 35), pt(37, 53), pt(53, 44)]), P.cream, 1.8)
    var m = Mood.grumpy; m.look = pt(5, 4)
    miniCat(c, 27, 26, 0.25, m)
    arm(c, from: pt(24, 40), to: pt(33, 54), bend: -4, w: 6, lw: 1.8, pawScale: 0.45)
    sparkle(c, 58, 14, 4, P.gold, 1.3)
    // volume ticks
    for (i, h) in [4.0, 6.0, 8.0].enumerated() as EnumeratedSequence<[CGFloat]> { stroke(c, mk { $0.move(to: pt(50 + CGFloat(i) * 5, 18)); $0.addLine(to: pt(50 + CGFloat(i) * 5, 18 - h)) }, P.lavender, 2) }
    return c
}
func drawIconSlide() -> CGContext {
    let c = iconBase()
    // slide board
    shape(c, rrect(24, 8, 62, 34, 3), P.white, 2)
    for (i, h) in [8.0, 14.0, 10.0].enumerated() as EnumeratedSequence<[CGFloat]> { fill(c, rrect(30 + CGFloat(i) * 9, 30 - h, 36 + CGFloat(i) * 9, 30, 1), [P.pink, P.mint, P.lavender][i]) }
    stroke(c, mk { $0.move(to: pt(43, 34)); $0.addLine(to: pt(43, 41)) }, P.ink, 2)
    // laser pointer beam + dot
    var m = Mood.smug; m.look = pt(6, -4); m.flatEars = true
    miniCat(c, 27, 50, 0.26, m)
    let beam = mk { $0.move(to: pt(36, 52)); $0.addLine(to: pt(54, 17)) }
    stroke(c, beam, col(0xFF5E7A, 0.75), 1.6)
    fill(c, ell(54, 17, 3.2, 3.2), col(0xFF2E58)); fill(c, ell(54, 17, 6, 6), col(0xFF2E58, 0.3))
    shape(c, rrect(34, 51, 44, 55, 1.5), P.coral, 1.4)
    paw(c, 34, 60, 0.4, lw: 1.8)
    return c
}
func drawIconRadar() -> CGContext {
    let c = iconBase()
    let g = col(0x3DBE76)
    // radar sweep, upper right
    for r in [10.0, 18.0, 26.0] as [CGFloat] { stroke(c, ell(40, 26, r, r), g.copy(alpha: 0.7)!, 1.4) }
    let sweep = mk { $0.move(to: pt(40, 26)); $0.addArc(center: pt(40, 26), radius: 26, startAngle: -1.2, endAngle: -0.3, clockwise: false); $0.closeSubpath() }
    fill(c, sweep, g.copy(alpha: 0.35)!)
    stroke(c, mk { $0.move(to: pt(40, 26)); $0.addLine(to: pt(40 + 26 * cos(-0.75), 26 + 26 * sin(-0.75))) }, g, 2)
    fill(c, ell(54, 18, 2.2, 2.2), P.coral); fill(c, ell(30, 14, 1.8, 1.8), P.coral)
    // periscope
    tube(c, mk { $0.move(to: pt(53, 62)); $0.addLine(to: pt(53, 40)); $0.addLine(to: pt(46, 40)) }, V.slate, 4, 1.5)
    shape(c, rrect(41, 37, 47, 43, 1.5), P.blue, 1.4)
    var m = Mood.unimpressed; m.look = pt(8, -3); m.lidL = 0.55; m.lidR = 0.55
    miniCat(c, 28, 52, 0.25, m)
    return c
}
func drawIconVoice2() -> CGContext {
    let c = iconBase()
    var m = Mood.grumpy; m.mouth = .flat; m.look = pt(0, 4)
    miniCat(c, 35, 31, 0.27, m)
    tube(c, mk { $0.move(to: pt(10, 36)); $0.addCurve(to: pt(60, 36), control1: pt(6, 4), control2: pt(64, 4)) }, P.coral, 2.2, 1.4)
    for x: CGFloat in [10, 60] { shape(c, rrect(x - 4.5, 30, x + 4.5, 46, 4), P.coral, 1.8); fill(c, rrect(x - 1.5, 33, x + 1.5, 43, 1.5), P.pink) }
    // mic in paws
    shape(c, rrect(29, 48, 41, 63, 6), P.lavender, 2)
    for yy: CGFloat in [52, 55.5, 59] { stroke(c, mk { $0.move(to: pt(31, yy)); $0.addLine(to: pt(39, yy)) }, P.ink, 1) }
    paw(c, 24, 60, 0.4, lw: 1.8); paw(c, 46, 60, 0.4, lw: 1.8)
    fill(c, ell(58, 56, 3, 3), col(0xFF2E58)); stroke(c, ell(58, 56, 3, 3), P.ink, 1)
    return c
}

// MARK: backgrounds / panels
/// Ordered (Bayer 4x4) dither on an opaque context: the plum gradient spans only a
/// couple of RGB565 steps, so without this it bands on the 16-bit panel.
func ditherOpaque(_ c: CGContext) {
    guard let d = c.data else { return }
    let w = c.width, h = c.height, bpr = c.bytesPerRow
    let bayer: [Int] = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]
    let p = d.bindMemory(to: UInt8.self, capacity: bpr * h)
    for y in 0..<h {
        for x in 0..<w {
            let t = (CGFloat(bayer[(y & 3) * 4 + (x & 3)]) + 0.5) / 16 - 0.5
            let o = y * bpr + x * 4
            for (ch, step) in [(0, CGFloat(8)), (1, CGFloat(4)), (2, CGFloat(8))] {
                p[o + ch] = UInt8(max(0, min(255, (CGFloat(p[o + ch]) + t * step).rounded())))
            }
        }
    }
}
func plumBase(_ w: Int, _ h: Int, pattern: CGFloat = 0.07, vignette: CGFloat = 0.5) -> CGContext {
    let c = makeCtx(w, h)
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    fill(c, CGRect(x: 0, y: 0, width: w, height: h).path(), V.plumDeep)
    let g = CGGradient(colorsSpace: cs, colors: [V.plumDeep, V.plumLite] as CFArray, locations: [0, 1])!
    c.drawLinearGradient(g, start: pt(0, 0), end: pt(CGFloat(w) * 0.45, CGFloat(h)), options: [.drawsBeforeStartLocation, .drawsAfterEndLocation])
    if pattern > 0 {
        var i = 0
        for row in 0..<7 {
            for ci in 0..<8 {
                let x = CGFloat(ci) * 46 + (row % 2 == 0 ? 12 : 35), y = CGFloat(row) * 40 + 16
                let rot = CGFloat((i * 37) % 60 - 30) * .pi / 180
                if i % 2 == 0 { pawPrint(c, x, y, 0.8, col(0xD9C8FF, pattern), rot: rot) }
                else { fishBone(c, x, y, 0.8, col(0xD9C8FF, pattern), rot: rot) }
                i += 1
            }
        }
    }
    if vignette > 0 {
        let g2 = CGGradient(colorsSpace: cs, colors: [col(0x120A22, 0), col(0x120A22, vignette)] as CFArray, locations: [0, 1])!
        c.saveGState(); c.translateBy(x: CGFloat(w) / 2, y: CGFloat(h) / 2); c.scaleBy(x: CGFloat(w) / CGFloat(h) * 0.78, y: 1)
        c.drawRadialGradient(g2, startCenter: .zero, startRadius: CGFloat(h) * 0.38, endCenter: .zero, endRadius: CGFloat(h) * 0.9, options: [.drawsAfterEndLocation])
        c.restoreGState()
    }
    return c
}
func drawBackgroundPlum() -> CGContext { let c = plumBase(320, 241); ditherOpaque(c); return c }
func drawBackgroundH() -> CGContext {
    let c = plumBase(320, 241)
    // floor: perspective paw-tile grid fading in below the horizon
    let hz: CGFloat = 176
    fill(c, CGRect(x: 0, y: hz, width: 320, height: 65).path(), col(0x1A1230, 0.35))
    for k in 0...12 {
        let x = CGFloat(k) * 26.7 - 10
        stroke(c, mk { $0.move(to: pt(160 + (x - 160) * 0.35, hz)); $0.addLine(to: pt(160 + (x - 160) * 1.5, 241)) }, col(0xD9C8FF, 0.08), 1.2)
    }
    for (i, y) in [hz + 8, hz + 20, hz + 36, hz + 58].enumerated() { _ = i
        stroke(c, mk { $0.move(to: pt(0, y)); $0.addLine(to: pt(320, y)) }, col(0xD9C8FF, 0.08), 1.2) }
    glow(c, 160, hz, 150, 0xCDB8F0, 0.08)
    ditherOpaque(c); return c
}
func drawBackgroundV() -> CGContext {
    let c = plumBase(320, 241)
    // side shelves with cat-toy silhouettes, keeping the centre clean for lists
    for sx in [0, 1] {
        let x0: CGFloat = sx == 0 ? 0 : 292, f: CGFloat = sx == 0 ? 1 : -1
        fill(c, CGRect(x: x0, y: 0, width: 28, height: 241).path(), col(0x1A1230, 0.30))
        stroke(c, mk { $0.move(to: pt(sx == 0 ? 28 : 292, 0)); $0.addLine(to: pt(sx == 0 ? 28 : 292, 241)) }, col(0xD9C8FF, 0.10), 1.5)
        for y in stride(from: CGFloat(24), to: 241, by: 52) {
            stroke(c, mk { $0.move(to: pt(x0, y + 20)); $0.addLine(to: pt(x0 + 28, y + 20)) }, col(0xD9C8FF, 0.10), 1.5)
            pawPrint(c, x0 + 14, y + 6, 0.55, col(0xD9C8FF, 0.09), rot: f * 0.3)
        }
    }
    ditherOpaque(c); return c
}
func drawClockBG() -> CGContext {
    let c = plumBase(321, 241, pattern: 0.05)
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    // rounded "cat TV" screen with a pink-lavender glow rim; centre kept clear for the digits
    glow(c, 160, 120, 190, 0xCDB8F0, 0.10)
    let panel = rrect(10, 8, 311, 222, 34)
    fill(c, panel, col(0x1E1532, 0.55))
    c.saveGState(); c.addPath(panel); c.clip()
    for y in stride(from: CGFloat(10), to: 222, by: 4) { stroke(c, mk { $0.move(to: pt(10, y)); $0.addLine(to: pt(311, y)) }, col(0xD9C8FF, 0.035), 1) }
    c.restoreGState()
    let g = CGGradient(colorsSpace: cs, colors: [P.pink, P.lavender, P.blue] as CFArray, locations: [0, 0.5, 1])!
    c.saveGState(); c.addPath(panel); c.setLineWidth(3); c.replacePathWithStrokedPath(); c.clip()
    c.drawLinearGradient(g, start: pt(10, 8), end: pt(311, 222), options: []); c.restoreGState()
    // paws gripping the bottom edge + tiny ear tips on top right
    paw(c, 128, 226, 1.0, lw: 3); paw(c, 194, 226, 1.0, lw: 3)
    for ox: CGFloat in [250, 284] { shape(c, poly([pt(ox - 13, 9), pt(ox, -4), pt(ox + 13, 9)]), P.cream, 2.4); fill(c, poly([pt(ox - 6, 8), pt(ox, 1), pt(ox + 6, 8)]), P.pink) }
    sparkle(c, 30, 205, 6, P.gold, 1.8); sparkle(c, 294, 190, 5, P.pink, 1.8)
    ditherOpaque(c); return c
}
func softShadow(_ c: CGContext, _ p: CGPath, blur: CGFloat, dy: CGFloat, _ a: CGFloat) {
    c.saveGState(); c.setShadow(offset: CGSize(width: 0, height: -dy), blur: blur, color: col(0x0C0618, a))
    fill(c, p, col(0x2B2040)); c.restoreGState()
}
func drawHeader() -> CGContext {
    let c = makeCtx(300, 39)
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    let bar = rrect(1, 7, 299, 37, 15)
    c.saveGState(); c.addPath(bar); c.clip()
    c.drawLinearGradient(CGGradient(colorsSpace: cs, colors: [col(0x7A58B8), col(0x5B3F8C)] as CFArray, locations: [0, 1])!, start: pt(0, 7), end: pt(0, 37), options: [])
    c.restoreGState()
    stroke(c, bar, P.lavender, 2.4)
    // cat ears bumps
    for ox: CGFloat in [26, 274] {
        shape(c, poly([pt(ox - 14, 9), pt(ox - 6, -1), pt(ox + 6, 9)]), col(0x6A4AA3), 2.2)
        fill(c, poly([pt(ox - 9, 8), pt(ox - 6, 3), pt(ox + 1, 8)]), P.pink)
    }
    fill(c, CGRect(x: 14, y: 8.2, width: 24, height: 4).path(), col(0x6A4AA3))
    fill(c, CGRect(x: 262, y: 8.2, width: 24, height: 4).path(), col(0x6A4AA3))
    for x: CGFloat in [150, 160, 170] { pawPrint(c, x, 22, 0.3, col(0xFFFFFF, 0.14)) }
    return c
}
func drawMessageBoxBG() -> CGContext {
    let c = makeCtx(260, 152); let lw: CGFloat = 4
    shape(c, poly([pt(36, 128), pt(84, 128), pt(22, 150)]), P.cream, lw)
    for sx in [0, 1] {
        let f: CGFloat = sx == 0 ? 1 : -1, ox: CGFloat = sx == 0 ? 0 : 260
        shape(c, poly([pt(ox + f * 8, 44), pt(ox + f * 14, -2), pt(ox + f * 66, 22)]), P.cream, lw)
        fill(c, poly([pt(ox + f * 19, 33), pt(ox + f * 21, 9), pt(ox + f * 48, 21)]), P.pink)
    }
    shape(c, rrect(6, 18, 254, 138, 24), P.cream, lw)
    fill(c, CGRect(x: 40, y: 135.8, width: 42, height: 4.4).path(), P.cream)
    c.saveGState(); c.setLineDash(phase: 0, lengths: [6, 6]); stroke(c, rrect(15, 27, 245, 129, 17), P.pink, 2); c.restoreGState()
    pawPrint(c, 224, 112, 0.8, P.pink, rot: 0.3)
    heart(c, 30, 46, 6, P.pink, 1.8); sparkle(c, 226, 46, 6, P.gold, 2)
    return c
}
func drawButtonBG() -> CGContext {
    let c = makeCtx(154, 193)
    let card = rrect(17, 41, 136, 147, 18)
    softShadow(c, card, blur: 14, dy: 5, 0.6)
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    c.saveGState(); c.addPath(card); c.clip()
    c.drawLinearGradient(CGGradient(colorsSpace: cs, colors: [col(0x5A4488), col(0x42306A)] as CFArray, locations: [0, 1])!, start: pt(0, 41), end: pt(0, 147), options: [])
    pawPrint(c, 112, 126, 1.1, col(0xFFFFFF, 0.07), rot: 0.4)
    c.restoreGState()
    stroke(c, card, col(0xCDB8F0, 0.9), 2.2)
    fill(c, rrect(24, 46, 128, 50, 2), col(0xFFFFFF, 0.14))
    return c
}
func drawBorder() -> CGContext {
    let c = makeCtx(300, 152)
    let r = rrect(2, 2, 298, 150, 18)
    stroke(c, r, P.lavender, 3.2)
    c.saveGState(); c.setLineDash(phase: 0, lengths: [5, 6]); stroke(c, rrect(7, 7, 293, 145, 14), col(0xF8B4C8, 0.7), 1.6); c.restoreGState()
    for (x, y) in [(18.0, 2.0), (282.0, 2.0)] as [(CGFloat, CGFloat)] { fill(c, ell(x, y, 8, 5), V.plumDeep); stroke(c, ell(x, y, 8, 5), P.lavender, 0); pawPrint(c, x, y + 1, 0.42, P.pink) }
    return c
}
func drawProgressBG() -> CGContext {
    let c = makeCtx(280, 106)
    let pill = rrect(37, 36, 243, 70, 17)
    softShadow(c, pill, blur: 12, dy: 3, 0.6)
    fill(c, pill, col(0x1F1633))
    stroke(c, pill, P.lavender, 3)
    pawPrint(c, 52, 53, 0.42, col(0xFFFFFF, 0.14), rot: -0.3); pawPrint(c, 228, 53, 0.42, col(0xFFFFFF, 0.14), rot: 0.3)
    return c
}
func drawProgressBar() -> CGContext {
    let c = makeCtx(196, 22)
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    let bar = rrect(0, 1, 196, 21, 10)
    c.saveGState(); c.addPath(bar); c.clip()
    c.drawLinearGradient(CGGradient(colorsSpace: cs, colors: [P.pink, P.lavender, P.blue] as CFArray, locations: [0, 0.55, 1])!, start: pt(0, 0), end: pt(196, 0), options: [])
    fill(c, CGRect(x: 0, y: 2, width: 196, height: 6).path(), col(0xFFFFFF, 0.3))
    for x in stride(from: CGFloat(14), to: 196, by: 26) { pawPrint(c, x, 12, 0.36, col(0xFFFFFF, 0.5), rot: 0.2) }
    c.restoreGState()
    return c
}
func drawBarBG() -> CGContext {
    let c = makeCtx(77, 202)
    let bar = rrect(19, 19, 58, 181, 19)
    softShadow(c, bar, blur: 12, dy: 4, 0.6)
    fill(c, bar, col(0x1F1633)); stroke(c, bar, P.lavender, 2.6)
    for y in stride(from: CGFloat(34), to: 175, by: 18) { stroke(c, mk { $0.move(to: pt(26, y)); $0.addLine(to: pt(32, y)) }, col(0xFFFFFF, 0.18), 1.6) }
    return c
}
func drawSlider() -> CGContext {
    let c = makeCtx(39, 163)
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    let bar = rrect(0, 1, 38, 162, 19)
    c.saveGState(); c.addPath(bar); c.clip()
    c.drawLinearGradient(CGGradient(colorsSpace: cs, colors: [col(0xFFF8FB), col(0xF8B4C8)] as CFArray, locations: [0, 1])!, start: pt(0, 0), end: pt(0, 163), options: [])
    for y in stride(from: CGFloat(18), to: 150, by: 30) { pawPrint(c, 19.5, y, 0.42, col(0xFFFFFF, 0.7)) }
    c.restoreGState()
    return c
}
func drawWifiStatus() -> CGContext {
    let c = makeCtx(155, 80)
    let card = rrect(17, 18, 136, 61, 18)
    softShadow(c, card, blur: 10, dy: 3, 0.55)
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    c.saveGState(); c.addPath(card); c.clip()
    c.drawLinearGradient(CGGradient(colorsSpace: cs, colors: [P.pink, P.lavender] as CFArray, locations: [0, 1])!, start: pt(17, 18), end: pt(136, 61), options: [])
    c.restoreGState()
    stroke(c, card, P.ink, 2.4)
    // ears
    for ox: CGFloat in [40, 116] {
        shape(c, poly([pt(ox - 10, 19), pt(ox - 3, 6), pt(ox + 8, 19)]), P.pink, 2.2)
    }
    // wifi disc + slot for the SSID text
    shape(c, ell(38, 39.5, 15, 15), V.plumDeep, 2)
    wifiArcs(c, 38, 45, 0.55, P.cream, lw: 2)
    shape(c, rrect(58, 26, 128, 54, 12), V.plumDeep, 2)
    return c
}

// MARK: small UI icons (cat/paw themed, same function and size as the originals)
func smallDisc(_ c: CGContext, _ w: CGFloat, _ h: CGFloat, _ top: CGColor, _ bot: CGColor) {
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    c.saveGState(); c.addPath(ell(w / 2, h / 2 + 0.5, w / 2 - 1.2, h / 2 - 1.7)); c.clip()
    c.drawLinearGradient(CGGradient(colorsSpace: cs, colors: [top, bot] as CFArray, locations: [0, 1])!, start: pt(0, 0), end: pt(w, h), options: [])
    c.restoreGState()
    stroke(c, ell(w / 2, h / 2 + 0.5, w / 2 - 1.2, h / 2 - 1.7), P.ink, 1.8)
}
func earsOn(_ c: CGContext, _ w: CGFloat, _ yTop: CGFloat, _ f: CGColor) {
    for sx: CGFloat in [-1, 1] {
        shape(c, poly([pt(w / 2 + sx * 17, yTop + 7), pt(w / 2 + sx * 16, yTop - 5), pt(w / 2 + sx * 6, yTop + 1)]), f, 1.8)
    }
}
func drawInfo() -> CGContext {
    let c = makeCtx(42, 43)
    earsOn(c, 42, 5, P.lavender); smallDisc(c, 42, 43, V.discTop, P.lavender)
    // "i": dot is a tiny cat head
    shape(c, ell(21, 13.5, 4.2, 3.8), P.peach, 1.4)
    for sx: CGFloat in [-1, 1] { shape(c, poly([pt(21 + sx * 4.2, 12.5), pt(21 + sx * 3.8, 8.8), pt(21 + sx * 1.2, 10.4)]), P.peach, 1.1) }
    stroke(c, mk { $0.move(to: pt(21, 20)); $0.addLine(to: pt(21, 33)) }, P.ink, 4.2)
    stroke(c, mk { $0.move(to: pt(17.5, 33)); $0.addLine(to: pt(24.5, 33)) }, P.ink, 2.6)
    return c
}
func drawBluetooth(on: Bool) -> CGContext {
    let c = makeCtx(42, 43)
    earsOn(c, 42, 5, on ? P.mint : col(0x9A90B5))
    smallDisc(c, 42, 43, on ? col(0xE8FFF3) : col(0xD6D0E6), on ? P.mint : col(0x9A90B5))
    bluetoothRune(c, 21, 22, 1.35, P.ink, on ? 3.2 : 2.6)
    if on { for (r, a) in [(11.0, 0.9), (15.0, 0.5)] as [(CGFloat, CGFloat)] { stroke(c, mk { $0.addArc(center: pt(21, 22), radius: r, startAngle: -0.8, endAngle: 0.8, clockwise: false) }, col(0xFF7F7F, a), 1.8) } }
    return c
}
func drawDetailedSettings() -> CGContext {
    let c = makeCtx(42, 43)
    smallDisc(c, 42, 43, V.discTop, V.discBot)
    var p = CGMutablePath()
    for i in 0..<8 {
        let a = CGFloat(i) * .pi / 4
        p.addPath(rrect(-3.2, -16.5, 3.2, -10, 1.5), transform: CGAffineTransform(translationX: 21, y: 22).rotated(by: a))
    }
    fill(c, p, V.slate); stroke(c, p, P.ink, 1.2)
    shape(c, ell(21, 22, 12, 12), P.lavender, 1.8)
    pawPrint(c, 21, 22.5, 0.5, P.ink)
    return c
}
func drawUsbMsc() -> CGContext {
    let c = makeCtx(42, 43)
    earsOn(c, 42, 5, P.lavender); smallDisc(c, 42, 43, V.discTop, P.lavender)
    shape(c, rrect(13, 21, 29, 36, 4), P.blue, 1.8)
    shape(c, rrect(15, 8, 27, 23, 2), P.white, 1.8)
    fill(c, rrect(17.5, 12, 20, 17, 0.6), P.ink); fill(c, rrect(22, 12, 24.5, 17, 0.6), P.ink)
    fill(c, ell(21, 30, 2.2, 2.2), P.coral)
    return c
}
func drawStorage(on: Bool) -> CGContext {
    let c = makeCtx(20, 21)
    pawPrint(c, 8.5, 9.5, 0.62, on ? P.mint : P.cream)
    let b = ell(14.5, 15, 5.8, 5.8)
    fill(c, b, on ? col(0x2F9E6A) : P.coral); stroke(c, b, P.ink, 1.2)
    let m = on ? mk { $0.move(to: pt(11.8, 15.2)); $0.addLine(to: pt(13.7, 17.2)); $0.addLine(to: pt(17.4, 12.8)) }
               : mk { $0.move(to: pt(12, 12.6)); $0.addLine(to: pt(17, 17.4)); $0.move(to: pt(17, 12.6)); $0.addLine(to: pt(12, 17.4)) }
    stroke(c, m, P.white, 1.7)
    return c
}
func drawPowerGauge() -> CGContext {
    let c = makeCtx(43, 21)
    let body = rrect(2, 2, 37, 19, 6)
    stroke(c, body, P.lavender, 2.4)
    // terminal = tiny cat ear nub; interior stays empty for the code-drawn fill
    shape(c, mk { $0.move(to: pt(38.5, 7)); $0.addLine(to: pt(42, 5)); $0.addLine(to: pt(42, 17)); $0.addLine(to: pt(38.5, 15)); $0.closeSubpath() }, P.pink, 1.4)
    return c
}
func drawVolume() -> CGContext {
    let c = makeCtx(28, 28)
    smallDisc(c, 28, 28, V.discTop, P.lavender)
    let sp = mk { $0.move(to: pt(6.5, 11)); $0.addLine(to: pt(10.5, 11)); $0.addLine(to: pt(15, 6.5)); $0.addLine(to: pt(15, 21.5)); $0.addLine(to: pt(10.5, 17)); $0.addLine(to: pt(6.5, 17)); $0.closeSubpath() }
    shape(c, sp, P.pink, 1.4)
    for r in [5.0, 8.0] as [CGFloat] { stroke(c, mk { $0.addArc(center: pt(15, 14), radius: r, startAngle: -0.8, endAngle: 0.8, clockwise: false) }, P.ink, 1.5) }
    return c
}
func drawBrightness() -> CGContext {
    let c = makeCtx(27, 28)
    smallDisc(c, 27, 28, col(0xFFF8E0), P.gold)
    for i in 0..<8 {
        let a = CGFloat(i) * .pi / 4
        stroke(c, mk { $0.move(to: pt(13.5 + 8.2 * cos(a), 14 + 8.2 * sin(a))); $0.addLine(to: pt(13.5 + 11 * cos(a), 14 + 11 * sin(a))) }, P.ink, 1.5)
    }
    shape(c, ell(13.5, 14, 6, 6), P.peach, 1.4)
    for sx: CGFloat in [-1, 1] { shape(c, poly([pt(13.5 + sx * 5.6, 11.5), pt(13.5 + sx * 4.8, 7.2), pt(13.5 + sx * 1.8, 9.4)]), P.peach, 1.0) }
    fill(c, ell(11.6, 14, 0.9, 1), P.ink); fill(c, ell(15.4, 14, 0.9, 1), P.ink); fill(c, ell(13.5, 16.2, 0.9, 0.7), P.coral)
    return c
}
func drawNav(center: Bool) -> CGContext {
    let c = makeCtx(26, 27)
    let dots: [(CGFloat, CGFloat)] = center ? [(13, 4.5), (4.5, 13.5), (13, 13.5), (21.5, 13.5), (13, 22.5)] : [(13, 4.5), (4.5, 13.5), (13, 13.5), (21.5, 13.5), (13, 22.5)]
    for (i, d) in dots.enumerated() {
        let hot = center ? i == 2 : i == 0
        pawPrint(c, d.0, d.1 + 0.5, 0.4, hot ? P.coral : col(0xD9D1EE, 0.9))
        if hot { fill(c, ell(d.0, d.1, 5.5, 5.5), col(0xFF7F7F, 0.25)) }
    }
    return c
}
func drawSwitch() -> CGContext {
    let c = makeCtx(38, 29)
    // 3x3 grid of toe beans, bottom-right highlighted (same layout/meaning as the original grid)
    for r in 0..<3 {
        for k in 0..<3 {
            let hot = r == 2 && k == 2
            let x = 2 + CGFloat(k) * 12.4, y = 1.5 + CGFloat(r) * 9.2
            let bean = rrect(x, y, x + 11, y + 8, 3.6)
            fill(c, bean, hot ? P.white : P.pink)
            if !hot { fill(c, rrect(x + 2, y + 1.2, x + 6, y + 3, 1), col(0xFFFFFF, 0.35)) }
            stroke(c, bean, hot ? P.coral : col(0xE58CAB), 1.2)
        }
    }
    return c
}

// MARK: asset registry (one function per asset; names are the LVGL variable names)
struct Asset {
    let name: String, w: Int, h: Int
    let fn: () -> CGContext
}
let mascotAssets: [Asset] = [
    Asset(name: "ui_img_mascot_png", w: 269, h: 203, fn: drawMascotHome),
    Asset(name: "ui_img_mascot_02_png", w: 147, h: 103, fn: drawMascotLoaf),
    Asset(name: "ui_img_mascot_recovery_png", w: 254, h: 129, fn: drawMascotYarn),
    Asset(name: "ui_img_mascot_update_png", w: 292, h: 196, fn: drawMascotMug),
    Asset(name: "ui_img_mascot_usb_msc_png", w: 299, h: 206, fn: drawMascotBox),
    Asset(name: "ui_img_message_box_mascot_png", w: 106, h: 93, fn: drawMascotPeek),
]
/// Order = APP_REGISTRY order in src/app/app.h (note the firmware's own "montior" spelling).
let iconAssets: [Asset] = [
    Asset(name: "ui_img_dino_png", w: 70, h: 70, fn: drawIconDino),
    Asset(name: "ui_img_matrix_rain_png", w: 70, h: 70, fn: drawIconMatrix),
    Asset(name: "ui_img_vu_meter_png", w: 70, h: 70, fn: drawIconVU),
    Asset(name: "ui_img_retro_tv_png", w: 70, h: 70, fn: drawIconTV),
    Asset(name: "ui_img_pc_montior_png", w: 70, h: 70, fn: drawIconPC),
    Asset(name: "ui_img_air_mouse_png", w: 70, h: 70, fn: drawIconAir),
    Asset(name: "ui_img_ble_spam_png", w: 70, h: 70, fn: drawIconBLE),
    Asset(name: "ui_img_badusb_png", w: 70, h: 70, fn: drawIconBadUSB2),
    Asset(name: "ui_img_infrared_png", w: 70, h: 70, fn: drawIconIR),
    Asset(name: "ui_img_mediaremote_png", w: 70, h: 70, fn: drawIconMedia),
    Asset(name: "ui_img_slideclicker_png", w: 70, h: 70, fn: drawIconSlide),
    Asset(name: "ui_img_radarscan_png", w: 70, h: 70, fn: drawIconRadar),
    Asset(name: "ui_img_voicerec_png", w: 70, h: 70, fn: drawIconVoice2),
]
let smallAssets: [Asset] = [
    Asset(name: "ui_img_info_png", w: 42, h: 43, fn: drawInfo),
    Asset(name: "ui_img_bluetooth_png", w: 42, h: 43, fn: { drawBluetooth(on: false) }),
    Asset(name: "ui_img_bluetooth_on_png", w: 42, h: 43, fn: { drawBluetooth(on: true) }),
    Asset(name: "ui_img_detailed_settings_png", w: 42, h: 43, fn: drawDetailedSettings),
    Asset(name: "ui_img_usb_msc_png", w: 42, h: 43, fn: drawUsbMsc),
    Asset(name: "ui_img_storage_status_null_png", w: 20, h: 21, fn: { drawStorage(on: false) }),
    Asset(name: "ui_img_storage_status_on_png", w: 20, h: 21, fn: { drawStorage(on: true) }),
    Asset(name: "ui_img_wifi_status_png", w: 155, h: 80, fn: drawWifiStatus),
    Asset(name: "ui_img_power_gauge_png", w: 43, h: 21, fn: drawPowerGauge),
    Asset(name: "ui_img_volume_icon_png", w: 28, h: 28, fn: drawVolume),
    Asset(name: "ui_img_brightness_icon_png", w: 27, h: 28, fn: drawBrightness),
    Asset(name: "ui_img_navigation_center_png", w: 26, h: 27, fn: { drawNav(center: true) }),
    Asset(name: "ui_img_navigation_edge_png", w: 26, h: 27, fn: { drawNav(center: false) }),
    Asset(name: "ui_img_switch_png", w: 38, h: 29, fn: drawSwitch),
]
let panelAssets: [Asset] = [
    Asset(name: "ui_img_background_png", w: 320, h: 241, fn: drawBackgroundPlum),
    Asset(name: "ui_img_background_h_png", w: 320, h: 241, fn: drawBackgroundH),
    Asset(name: "ui_img_background_v_png", w: 320, h: 241, fn: drawBackgroundV),
    Asset(name: "ui_img_clock_bg_png", w: 321, h: 241, fn: drawClockBG),
    Asset(name: "ui_img_header_png", w: 300, h: 39, fn: drawHeader),
    Asset(name: "ui_img_message_box_bg_png", w: 260, h: 152, fn: drawMessageBoxBG),
    Asset(name: "ui_img_button_bg_png", w: 154, h: 193, fn: drawButtonBG),
    Asset(name: "ui_img_border_png", w: 300, h: 152, fn: drawBorder),
    Asset(name: "ui_img_progress_bg_png", w: 280, h: 106, fn: drawProgressBG),
    Asset(name: "ui_img_progress_bar_png", w: 196, h: 22, fn: drawProgressBar),
    Asset(name: "ui_img_bar_bg_png", w: 77, h: 202, fn: drawBarBG),
    Asset(name: "ui_img_slider_png", w: 39, h: 163, fn: drawSlider),
]

// MARK: contact sheets (flow layout; labels in a small system font)
struct Cell { let w: CGFloat, h: CGFloat; let label: String; let draw: (CGContext, CGFloat, CGFloat) -> Void }
let sheetBG = col(0x120C1E)
func flowSheet(width: Int, title: String, _ cells: [Cell]) -> CGContext {
    let pad: CGFloat = 12, gap: CGFloat = 10, labelH: CGFloat = 14
    var pos: [(CGFloat, CGFloat)] = []
    var x = pad, y = pad + 22, rowH: CGFloat = 0
    for cl in cells {
        let cw = max(cl.w, 74)
        if x + cw > CGFloat(width) - pad && x > pad { x = pad; y += rowH + gap; rowH = 0 }
        pos.append((x, y)); x += cw + gap; rowH = max(rowH, cl.h + labelH)
    }
    let total = Int(y + rowH + pad)
    let c = makeCtx(width, total)
    fill(c, CGRect(x: 0, y: 0, width: width, height: total).path(), sheetBG)
    drawText(c, title, pad, 18, size: 12, color: P.lavender)
    for (cl, p) in zip(cells, pos) {
        cl.draw(c, p.0, p.1)
        drawText(c, cl.label, p.0, p.1 + cl.h + 10, size: 8.5, color: col(0xCFC6E6))
    }
    return c
}
func img(_ ctx: CGContext, _ scale: CGFloat = 1) -> (CGContext, CGFloat, CGFloat) -> Void {
    return { c, x, y in
        c.saveGState(); c.interpolationQuality = scale == floor(scale) ? .none : .high
        drawImage(c, ctx.makeImage()!, CGRect(x: x, y: y, width: CGFloat(ctx.width) * scale, height: CGFloat(ctx.height) * scale)); c.restoreGState()
    }
}
func tileFill(_ c: CGContext, _ r: CGRect, _ f: CGColor, _ rad: CGFloat = 12) { fill(c, rrect(r.minX, r.minY, r.maxX, r.maxY, rad), f) }

func mascotSheet(_ bgPlum: CGContext, _ mascots: [(Asset, CGContext)]) -> CGContext {
    var cells: [Cell] = []
    for (a, ctx) in mascots {
        cells.append(Cell(w: CGFloat(a.w), h: CGFloat(a.h), label: "\(a.name) \(a.w)x\(a.h)") { c, x, y in
            c.saveGState(); c.clip(to: CGRect(x: x, y: y, width: CGFloat(a.w), height: CGFloat(a.h)))
            drawImage(c, bgPlum.makeImage()!, CGRect(x: x, y: y, width: 320, height: 241)); c.restoreGState()
            img(ctx)(c, x, y)
        })
    }
    // expression set (reference busts, not firmware assets)
    let names = ["grumpy-idle", "smug", "unimpressed", "annoyed (yarn)", "triumphant (fish)", "sleepy-grumpy"]
    for k in 0..<6 {
        let ctx = drawExpressionBust(k)
        cells.append(Cell(w: 150, h: 130, label: "expr: \(names[k])") { c, x, y in
            c.saveGState(); c.clip(to: CGRect(x: x, y: y, width: 150, height: 130))
            drawImage(c, bgPlum.makeImage()!, CGRect(x: x - 60, y: y - 40, width: 320, height: 241)); c.restoreGState()
            img(ctx)(c, x, y)
        })
    }
    return flowSheet(width: 1000, title: "GrumpyKitty v2 - mascots (1x on plum) + expression set", cells)
}

func iconSheet(_ icons: [(Asset, CGContext)], _ small: [(Asset, CGContext)], _ bgPlum: CGContext) -> CGContext {
    var cells: [Cell] = []
    let tiles: [(CGColor, String)] = [(col(0xBEE700), "lime"), (col(0x4A3670), "plum"), (col(0xF3EAF8), "light")]
    for (a, ctx) in icons {
        cells.append(Cell(w: 78, h: 3 * 76, label: a.name.replacingOccurrences(of: "ui_img_", with: "").replacingOccurrences(of: "_png", with: "")) { c, x, y in
            for (i, t) in tiles.enumerated() {
                let r = CGRect(x: x + 4, y: y + CGFloat(i) * 76, width: 70, height: 70)
                tileFill(c, r, t.0); img(ctx)(c, r.minX, r.minY)
            }
        })
    }
    // icons at 2x on plum for detail
    for (a, ctx) in icons {
        cells.append(Cell(w: 140, h: 140, label: "2x " + a.name.replacingOccurrences(of: "ui_img_", with: "")) { c, x, y in
            tileFill(c, CGRect(x: x, y: y, width: 140, height: 140), col(0x33264E), 10); img(ctx, 2)(c, x, y)
        })
    }
    for (a, ctx) in small {
        let sc: CGFloat = a.w > 100 ? 1.5 : (a.w > 40 ? 2.5 : 3)
        let w = CGFloat(a.w) * sc, h = CGFloat(a.h) * sc
        cells.append(Cell(w: max(w + 8, 100), h: h + 8, label: "\(a.name.replacingOccurrences(of: "ui_img_", with: "").replacingOccurrences(of: "_png", with: "").replacingOccurrences(of: "storage_status_", with: "sd_")) @\(sc)x") { c, x, y in
            tileFill(c, CGRect(x: x, y: y, width: w + 8, height: h + 8), col(0x33264E), 8); img(ctx, sc)(c, x + 4, y + 4)
        })
    }
    // 1x strip on plum + lime to judge real size
    cells.append(Cell(w: 960, h: 100, label: "small icons at 1x on plum (top) and lime (bottom)") { c, x, y in
        tileFill(c, CGRect(x: x, y: y, width: 960, height: 48), col(0x33264E), 8)
        tileFill(c, CGRect(x: x, y: y + 52, width: 960, height: 48), col(0x6A8F00), 8)
        var px = x + 6
        for (_, ctx) in small where ctx.height < 50 { img(ctx)(c, px, y + 2); img(ctx)(c, px, y + 54); px += CGFloat(ctx.width) + 8 }
    })
    return flowSheet(width: 1180, title: "GrumpyKitty v2 - app icons (lime / plum / light tile) and small UI icons", cells)
}

func panelSheet(_ bgPlum: CGContext, _ panels: [(Asset, CGContext)]) -> CGContext {
    var cells: [Cell] = []
    let byName = Dictionary(uniqueKeysWithValues: panels.map { ($0.0.name, $0.1) })
    for (a, ctx) in panels {
        let opaque = a.name.contains("background") || a.name.contains("clock")
        cells.append(Cell(w: CGFloat(a.w), h: CGFloat(a.h), label: "\(a.name) \(a.w)x\(a.h)") { c, x, y in
            if !opaque {
                c.saveGState(); c.clip(to: CGRect(x: x, y: y, width: CGFloat(a.w), height: CGFloat(a.h)))
                drawImage(c, bgPlum.makeImage()!, CGRect(x: x, y: y, width: 320, height: 241)); c.restoreGState()
            }
            img(ctx)(c, x, y)
        })
    }
    // composite mock: settings-like screen (bg_v + header + border + progress + white text)
    let mock = Cell(w: 320, h: 241, label: "mock: bg_v + header + border + progress + white text") { c, x, y in
        c.saveGState(); c.clip(to: CGRect(x: x, y: y, width: 320, height: 241))
        img(byName["ui_img_background_v_png"]!)(c, x, y)
        img(byName["ui_img_header_png"]!)(c, x + 10, y + 3)
        drawText(c, "Wi-Fi", x + 26, y + 26, size: 13, color: P.white, font: "Arial-BoldMT")
        img(byName["ui_img_border_png"]!)(c, x + 10, y + 47)
        for (i, s) in ["HomeNet_5G", "CatCafe", "Guest"].enumerated() { drawText(c, s, x + 28, y + 76 + CGFloat(i) * 26, size: 13, color: P.white, font: "Arial-BoldMT") }
        c.restoreGState()
    }
    cells.append(mock)
    let mock2 = Cell(w: 320, h: 241, label: "mock: bg + message_box_bg (dark text) + peek cat") { c, x, y in
        c.saveGState(); c.clip(to: CGRect(x: x, y: y, width: 320, height: 241))
        img(byName["ui_img_background_png"]!)(c, x, y)
        img(byName["ui_img_message_box_bg_png"]!)(c, x + 30, y + 40)
        drawText(c, "Hello human.", x + 160, y + 112, size: 13, color: V.darkInk, font: "Arial-BoldMT", center: true)
        c.restoreGState()
    }
    cells.append(mock2)
    return flowSheet(width: 1100, title: "GrumpyKitty v2 - backgrounds and panels (opaque ones raw, panels over plum)", cells)
}

// MARK: v2 main
func runV2(_ out: String, _ ref: String) {
    try? FileManager.default.createDirectory(atPath: out, withIntermediateDirectories: true)
    func render(_ list: [Asset]) -> [(Asset, CGContext)] {
        list.map { a in
            let ctx = a.fn()
            precondition(ctx.width == a.w && ctx.height == a.h, "\(a.name): drew \(ctx.width)x\(ctx.height), expected \(a.w)x\(a.h)")
            save(ctx, "\(out)/\(a.name).png"); return (a, ctx)
        }
    }
    let m = render(mascotAssets), i = render(iconAssets), s = render(smallAssets), p = render(panelAssets)
    let bg = p[0].1
    save(mascotSheet(bg, m), "\(out)/contact_sheet_v2_1_mascots.png")
    save(iconSheet(i, s, bg), "\(out)/contact_sheet_v2_2_icons.png")
    save(panelSheet(bg, p), "\(out)/contact_sheet_v2_3_backgrounds_panels.png")
}
