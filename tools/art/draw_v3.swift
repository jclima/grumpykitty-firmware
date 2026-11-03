// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// FancyKitty art v3: a MEAN, GRUMPY alley cat. Three palettes, same drawings.
// Compiled with draw.swift (shared helpers) + main_v3.swift by make_v3.sh.
// Origin is top-left, y grows down. Hand-inked look: wobbled paths, outlines drawn as
// short segments with varying width, flat fills with ONE hard shadow tone and a rim band.

import CoreGraphics
import CoreText
import Foundation
import ImageIO

// MARK: palette
func mixc(_ a: CGColor, _ b: CGColor, _ t: CGFloat) -> CGColor {
    let x = a.components!, y = b.components!
    return CGColor(srgbRed: x[0] + (y[0] - x[0]) * t, green: x[1] + (y[1] - x[1]) * t,
                   blue: x[2] + (y[2] - x[2]) * t, alpha: 1)
}
func alpha(_ c: CGColor, _ a: CGFloat) -> CGColor { c.copy(alpha: a)! }

struct Pal {
    let name: String
    let bg0, bg1, fur, belly, line, eye, accent, danger, info, bone, panel, table: CGColor
    var furShade: CGColor { mixc(fur, line, 0.5) }
    var furRim: CGColor { mixc(fur, bone, 0.26) }
    var bellyShade: CGColor { mixc(belly, line, 0.38) }
    var eyeShade: CGColor { mixc(eye, line, 0.38) }
    var boneShade: CGColor { mixc(bone, line, 0.32) }
    var scar: CGColor { mixc(belly, bone, 0.6) }
    var disc: CGColor { mixc(line, bg0, 0.35) }
    var lid: CGColor { mixc(bg1, fur, 0.5) }
    var nose: CGColor { mixc(belly, line, 0.82) }
    var coffee: CGColor { col(0x2a1a12) }

    static let alley = Pal(name: "alley", bg0: col(0x14161c), bg1: col(0x1f232b), fur: col(0x3e434e),
        belly: col(0x9aa0ab), line: col(0x07080b), eye: col(0xffb000), accent: col(0xff6a00),
        danger: col(0xc8451c), info: col(0xffb000), bone: col(0xece4d0), panel: col(0x1a1d24), table: col(0x2c313c))
    static let toxic = Pal(name: "toxic", bg0: col(0x050608), bg1: col(0x0d1014), fur: col(0x3b3e49),
        belly: col(0x8f95a3), line: col(0x040405), eye: col(0xc6ff00), accent: col(0xc6ff00),
        danger: col(0xff2e88), info: col(0x00e5ff), bone: col(0xe6f0dc), panel: col(0x0b0d11), table: col(0x1c2029))
    static let blood = Pal(name: "bloodmoon", bg0: col(0x1a0b10), bg1: col(0x2a1118), fur: col(0x3e3b41),
        belly: col(0xa69c94), line: col(0x0a0507), eye: col(0xffc83d), accent: col(0xe0283c),
        danger: col(0xe0283c), info: col(0xf2e6d0), bone: col(0xf2e6d0), panel: col(0x22101a), table: col(0x3d1b24))
    static let all = [alley, toxic, blood]
}

// MARK: hand-inked geometry
func hn(_ a: Int, _ b: Int) -> CGFloat {
    var x = UInt32(truncatingIfNeeded: a &* 374761393 &+ b &* 668265263)
    x = (x ^ (x >> 13)) &* 1274126177
    x ^= x >> 16
    return CGFloat(x % 2001) / 1000 - 1
}
func dist(_ a: CGPoint, _ b: CGPoint) -> CGFloat { hypot(a.x - b.x, a.y - b.y) }
func lerp(_ a: CGPoint, _ b: CGPoint, _ t: CGFloat) -> CGPoint { pt(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t) }

func flatSub(_ p: CGPath, _ step: CGFloat) -> [([CGPoint], Bool)] {
    var out: [([CGPoint], Bool)] = []
    var cur: [CGPoint] = []
    var last = CGPoint.zero
    var start = CGPoint.zero
    p.applyWithBlock { ep in
        let e = ep.pointee
        switch e.type {
        case .moveToPoint:
            if cur.count > 1 { out.append((cur, false)) }
            cur = [e.points[0]]; last = e.points[0]; start = last
        case .addLineToPoint:
            let b = e.points[0]
            let n = max(1, Int(dist(last, b) / step))
            for i in 1...n { cur.append(lerp(last, b, CGFloat(i) / CGFloat(n))) }
            last = b
        case .addQuadCurveToPoint:
            let c1 = e.points[0], b = e.points[1]
            let n = max(2, Int((dist(last, c1) + dist(c1, b)) / step))
            let a = last
            for i in 1...n {
                let t = CGFloat(i) / CGFloat(n), u = 1 - t
                cur.append(pt(u * u * a.x + 2 * u * t * c1.x + t * t * b.x, u * u * a.y + 2 * u * t * c1.y + t * t * b.y))
            }
            last = b
        case .addCurveToPoint:
            let c1 = e.points[0], c2 = e.points[1], b = e.points[2]
            let n = max(3, Int((dist(last, c1) + dist(c1, c2) + dist(c2, b)) / step))
            let a = last
            for i in 1...n {
                let t = CGFloat(i) / CGFloat(n), u = 1 - t
                let x = u * u * u * a.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * b.x
                let y = u * u * u * a.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * b.y
                cur.append(pt(x, y))
            }
            last = b
        case .closeSubpath:
            if cur.count > 1 { out.append((cur, true)) }
            cur = []; last = start
        @unknown default: break
        }
    }
    if cur.count > 1 { out.append((cur, false)) }
    return out
}

/// Jitters a path a little so it looks drawn by hand (deterministic per seed).
func wob(_ p: CGPath, _ amp: CGFloat, _ seed: Int, step: CGFloat = 3) -> CGPath {
    let m = CGMutablePath()
    for (pts, closed) in flatSub(p, step) {
        var q = pts
        if closed, q.count > 2, dist(q[0], q[q.count - 1]) < 0.05 { q.removeLast() }
        for i in 0..<q.count {
            let fi = Double(i), fs = Double(seed)
            let nx = CGFloat(sin(fi * 0.9 + fs * 1.3) * 0.6 + sin(fi * 0.37 + fs) * 0.4)
            let ny = CGFloat(sin(fi * 0.77 + fs * 2.1) * 0.6 + sin(fi * 0.29 + fs * 0.5) * 0.4)
            q[i].x += amp * (nx * 0.7 + hn(i, seed) * 0.3)
            q[i].y += amp * (ny * 0.7 + hn(i, seed + 7) * 0.3)
        }
        m.addLines(between: q)
        if closed { m.closeSubpath() }
    }
    return m
}

/// Ink outline: short segments with uneven weight.
func outline(_ c: CGContext, _ p: CGPath, _ lw: CGFloat, _ color: CGColor, _ seed: Int = 0) {
    c.saveGState()
    c.setStrokeColor(color); c.setLineCap(.round); c.setLineJoin(.round)
    for (pts, closed) in flatSub(p, 2.2) {
        let n = pts.count
        let cnt = closed ? n : n - 1
        if cnt < 1 { continue }
        for i in 0..<cnt {
            let a = pts[i], b = pts[(i + 1) % n]
            let fi = Double(i), fs = Double(seed)
            let w = lw * (0.95 + 0.32 * CGFloat(sin(fi * 0.19 + fs) * sin(fi * 0.053 + fs * 1.7)))
            c.setLineWidth(w); c.move(to: a); c.addLine(to: b); c.strokePath()
        }
    }
    c.restoreGState()
}
func inkLine(_ c: CGContext, _ pts: [CGPoint], _ lw: CGFloat, _ color: CGColor, _ seed: Int = 0, amp: CGFloat = 0.3) {
    let p = wob(mk { $0.addLines(between: pts) }, amp, seed)
    outline(c, p, lw, color, seed)
}
func inkCurve(_ c: CGContext, _ p: CGPath, _ lw: CGFloat, _ color: CGColor, _ seed: Int = 0, amp: CGFloat = 0.3) {
    outline(c, wob(p, amp, seed), lw, color, seed)
}

func chaikin(_ p: [CGPoint], _ it: Int = 1) -> [CGPoint] {
    var q = p
    for _ in 0..<it {
        var o: [CGPoint] = []
        for i in 0..<q.count {
            let a = q[i], b = q[(i + 1) % q.count]
            o.append(lerp(a, b, 0.25)); o.append(lerp(a, b, 0.75))
        }
        q = o
    }
    return q
}

/// Hard shadow band: part of `p` NOT covered by `p` shifted by (-dx,-dy).
func band(_ c: CGContext, _ p: CGPath, _ color: CGColor, _ dx: CGFloat, _ dy: CGFloat) {
    c.saveGState()
    c.addPath(p); c.clip()
    var t = CGAffineTransform(translationX: -dx, y: -dy)
    let sh = p.copy(using: &t)!
    c.addPath(CGPath(rect: p.boundingBoxOfPath.insetBy(dx: -40, dy: -40), transform: nil))
    c.addPath(sh)
    c.setFillColor(color)
    c.fillPath(using: .evenOdd)
    c.restoreGState()
}

/// Flat fill + one hard shadow + thin rim + thick wobbly black outline. Returns the wobbled path.
@discardableResult
func solid(_ c: CGContext, _ p: Pal, _ path: CGPath, _ f: CGColor, shade: CGColor? = nil, rim: CGColor? = nil,
           lw: CGFloat, seed: Int, amp: CGFloat = 0.5, d: CGFloat = 5) -> CGPath {
    let w = wob(path, amp, seed)
    fill(c, w, f)
    if let s = shade { band(c, w, s, d * 0.8, d) }
    if let r = rim { band(c, w, r, -d * 0.3, -d * 0.3) }
    outline(c, w, lw, p.line, seed)
    return w
}

func rectP(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) -> CGPath { CGPath(rect: CGRect(x: x, y: y, width: w, height: h), transform: nil) }
func lineP(_ pts: [CGPoint]) -> CGPath { mk { $0.addLines(between: pts) } }

// MARK: text
func glyphs(_ s: String, size: CGFloat) -> CGPath {
    let font = CTFontCreateWithName("Impact" as CFString, size, nil)
    let m = CGMutablePath()
    var x: CGFloat = 0
    for ch in s.utf16 {
        var u = ch
        var g: CGGlyph = 0
        if !CTFontGetGlyphsForCharacters(font, &u, &g, 1) { continue }
        var adv = CGSize.zero
        CTFontGetAdvancesForGlyphs(font, .horizontal, &g, &adv, 1)
        if let gp = CTFontCreatePathForGlyph(font, g, nil) {
            m.addPath(gp, transform: CGAffineTransform(a: 1, b: 0, c: 0, d: -1, tx: x, ty: 0))
        }
        x += adv.width + size * 0.03
    }
    let b = m.boundingBox
    var t = CGAffineTransform(translationX: -b.midX, y: -b.midY)
    return m.copy(using: &t)!
}
/// Angry comic shout, <= 5 chars: fat outline, slight skew and tilt.
func shout(_ c: CGContext, _ s: String, _ cx: CGFloat, _ cy: CGFloat, _ size: CGFloat, _ rot: CGFloat,
           _ f: CGColor, _ line: CGColor, _ lw: CGFloat, skew: CGFloat = 0.2) {
    precondition(s.count <= 5, "shout text must be <= 5 chars")
    let g = glyphs(s, size: size)
    var t = CGAffineTransform(a: 1, b: 0, c: -skew, d: 1, tx: 0, ty: 0)
        .concatenating(CGAffineTransform(translationX: cx, y: cy).rotated(by: rot))
    let gp = g.copy(using: &t)!
    c.saveGState()
    c.addPath(gp); c.setStrokeColor(line); c.setLineWidth(lw * 2); c.setLineJoin(.round); c.strokePath()
    c.restoreGState()
    fill(c, gp, f)
}
func label(_ c: CGContext, _ s: String, _ x: CGFloat, _ y: CGFloat, _ size: CGFloat, _ color: CGColor, bold: Bool = false, font: String? = nil) {
    let f = CTFontCreateWithName((font ?? (bold ? "Menlo-Bold" : "Menlo")) as CFString, size, nil)
    let attrs: [NSAttributedString.Key: Any] = [
        NSAttributedString.Key(kCTFontAttributeName as String): f,
        NSAttributedString.Key(kCTForegroundColorAttributeName as String): color]
    let line = CTLineCreateWithAttributedString(NSAttributedString(string: s, attributes: attrs))
    c.saveGState()
    c.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
    c.textPosition = CGPoint(x: x, y: y)
    CTLineDraw(line, c)
    c.restoreGState()
}

// MARK: the cat
struct Mood {
    var lidL: CGFloat = 0.3, lidR: CGFloat = 0.38
    var tilt: CGFloat = 8
    var browL: CGFloat = 0, browR: CGFloat = 0
    var kL: CGFloat = 1.15, kR: CGFloat = 0.9
    var pupil = CGPoint.zero
    var slit = false
    var mouth = 0     // 0 snarl+fang, 1 smirk+fang, 2 hiss, 3 flat
    static let glare = Mood()
    static let smug = Mood(lidL: 0.55, lidR: 0.58, tilt: 1, browL: -4, browR: 1, kL: 1.1, kR: 1.0, pupil: pt(0, 1), mouth: 1)
    static let hiss = Mood(lidL: 0.18, lidR: 0.18, tilt: 12, browL: 1, browR: 1, kL: 1.12, kR: 1.0, slit: true, mouth: 2)
    static let side = Mood(lidL: 0.42, lidR: 0.42, tilt: 3, kL: 1.1, kR: 1.0, pupil: pt(8, 0), mouth: 3)
}

@discardableResult
func kEye(_ c: CGContext, _ p: Pal, ex: CGFloat, ey: CGFloat, k: CGFloat, lid: CGFloat, tilt: CGFloat,
          pup: CGPoint, slit: Bool, lw: CGFloat, seed: Int) -> (CGPoint, CGPoint) {
    let rx = 14 * k, ry = 10.5 * k
    let dir: CGFloat = ex < 0 ? 1 : -1
    let yc = ey - ry + 2 * ry * lid
    let o = pt(ex - dir * (rx + 5), yc - tilt / 2), i = pt(ex + dir * (rx + 5), yc + tilt / 2)
    let eyeP = wob(ell(ex, ey, rx, ry), 0.3, seed)
    c.saveGState()
    c.addPath(eyeP); c.clip()
    fill(c, eyeP, p.eye)
    fill(c, rectP(ex - rx - 2, ey + ry * 0.3, rx * 2 + 4, ry), p.eyeShade)
    let py = min(ey + ry * 0.55, (yc + ey + ry) / 2 + 0.5)
    let pp = slit ? ell(ex + pup.x * k, py, 1.7 * k, 5.8 * k) : ell(ex + pup.x * k, py, 3.1 * k, 4.4 * k)
    fill(c, pp, p.line)
    fill(c, poly([o, i, pt(i.x, ey - ry - 20), pt(o.x, ey - ry - 20)]), p.furShade)
    c.restoreGState()
    c.saveGState()
    c.addPath(poly([o, i, pt(i.x, ey + ry + 20), pt(o.x, ey + ry + 20)])); c.clip()
    outline(c, eyeP, lw * 0.95, p.line, seed)
    c.restoreGState()
    inkLine(c, [o, i], lw * 1.2, p.line, seed, amp: 0.2)
    return (o, i)
}

func kBrow(_ c: CGContext, _ p: Pal, _ o: CGPoint, _ i: CGPoint, ex: CGFloat, off: CGFloat, lw: CGFloat, seed: Int) {
    let dir: CGFloat = ex < 0 ? 1 : -1
    let ob = pt(o.x - dir * 4, o.y - 2 + off), ib = pt(i.x - dir * 4, i.y - 1.5 + off)
    let ot = pt(ob.x, ob.y - 5), it = pt(ib.x, ib.y - 8)
    let topA = lerp(ot, it, 0.3), topB = lerp(ot, it, 0.62)
    let pts: [CGPoint] = [ot, pt(topA.x - dir * 1.5, topA.y - 3), pt(topA.x + dir * 1, topA.y + 0.5),
                          pt(topB.x - dir * 1.5, topB.y - 2.5), pt(topB.x + dir * 1, topB.y + 0.5), it, ib, ob]
    let bp = wob(poly(pts), 0.35, seed)
    fill(c, bp, p.line)
    outline(c, bp, lw * 0.6, p.line, seed)
}

func kShoulders(_ c: CGContext, _ p: Pal, _ cx: CGFloat, _ cy: CGFloat, _ rx: CGFloat, _ ry: CGFloat, lw: CGFloat, seed: Int) {
    let pts: [CGPoint] = (0..<22).map { i in
        let a = CGFloat(i) / 22 * 2 * .pi
        let r: CGFloat = (i % 3 == 0) ? 1.04 : 0.97
        return pt(cx + cos(a) * rx * r, cy + sin(a) * ry * r)
    }
    solid(c, p, poly(chaikin(pts)), p.fur, shade: p.furShade, rim: p.furRim, lw: lw, seed: seed, amp: 0.7, d: 7)
}

/// Full head. Local units: head ~ 112 wide. `full` adds forehead marks and whiskers.
func kHead(_ c: CGContext, _ p: Pal, _ cx: CGFloat, _ cy: CGFloat, _ s: CGFloat, _ m: Mood,
           lw: CGFloat = 3.2, full: Bool = true, seed: Int = 1) {
    c.saveGState()
    c.translateBy(x: cx, y: cy); c.scaleBy(x: s, y: s)
    let earIn = mixc(p.fur, p.line, 0.72)
    // ears (behind head)
    let eL = poly([pt(-50, -18), pt(-47, -68), pt(-16, -34)])
    solid(c, p, eL, p.fur, shade: p.furShade, rim: p.furRim, lw: lw, seed: seed + 1, d: 4)
    fill(c, wob(poly([pt(-44, -26), pt(-43, -56), pt(-26, -35)]), 0.3, seed), earIn)
    let eR = poly([pt(18, -35), pt(29, -68), pt(35, -47), pt(44, -64), pt(50, -22)])
    solid(c, p, eR, p.fur, shade: p.furShade, rim: p.furRim, lw: lw, seed: seed + 2, d: 4)
    fill(c, wob(poly([pt(25, -36), pt(31, -56), pt(35, -40), pt(42, -52), pt(44, -32)]), 0.3, seed), earIn)
    // head outline with cheek tufts
    let rightHalf: [CGPoint] = [pt(0, -37), pt(16, -39), pt(32, -35), pt(44, -26), pt(52, -14), pt(62, -8), pt(51, -2),
                                pt(60, 7), pt(49, 10), pt(56, 20), pt(42, 24), pt(30, 33), pt(14, 39), pt(0, 40)]
    var pts = rightHalf
    for q in rightHalf.dropFirst().dropLast().reversed() { pts.append(pt(-q.x + hn(Int(q.y), seed) * 1.5, q.y + 1.5)) }
    let head = solid(c, p, poly(chaikin(pts)), p.fur, shade: p.furShade, rim: p.furRim, lw: lw, seed: seed + 3, amp: 0.45, d: 6)
    // tabby marks + muzzle (clipped to head)
    c.saveGState(); c.addPath(head); c.clip()
    if full {
        for (j, x) in [-9, 0, 9].enumerated() {
            let l: CGFloat = j == 1 ? 12 : 8
            inkLine(c, [pt(CGFloat(x), -40), pt(CGFloat(x) + CGFloat(j - 1) * 1.5, -40 + l)], lw * 0.55, alpha(p.line, 0.7), seed + j, amp: 0.2)
        }
        for j in 0..<2 {   // cheek stripes
            let sx: CGFloat = j == 0 ? 1 : -1
            inkLine(c, [pt(sx * 54, 0), pt(sx * 40, 4)], lw * 0.5, alpha(p.line, 0.65), seed + 9 + j, amp: 0.2)
            inkLine(c, [pt(sx * 54, 8), pt(sx * 41, 11)], lw * 0.5, alpha(p.line, 0.65), seed + 11 + j, amp: 0.2)
        }
    }
    for (x, y, rx, ry) in [(-13.0, 16.0, 19.0, 13.0), (13.0, 16.0, 19.0, 13.0), (0.0, 30.0, 16.0, 9.0)] as [(CGFloat, CGFloat, CGFloat, CGFloat)] {
        let e = wob(ell(x, y, rx, ry), 0.5, seed + Int(x))
        fill(c, e, p.belly)
        band(c, e, p.bellyShade, 2.5, 3.5)
    }
    c.restoreGState()
    // eyes + brows
    let ey: CGFloat = -3
    let (oL, iL) = kEye(c, p, ex: -21, ey: ey, k: m.kL, lid: m.lidL, tilt: m.tilt, pup: m.pupil, slit: m.slit, lw: lw, seed: seed + 20)
    let (oR, iR) = kEye(c, p, ex: 21, ey: ey, k: m.kR, lid: m.lidR, tilt: m.tilt, pup: m.pupil, slit: m.slit, lw: lw, seed: seed + 30)
    kBrow(c, p, oL, iL, ex: -21, off: m.browL, lw: lw, seed: seed + 40)
    kBrow(c, p, oR, iR, ex: 21, off: m.browR, lw: lw, seed: seed + 50)
    // scar across the left eye
    let a = pt(-37, -29), b = pt(-9, 14)
    let sk: CGFloat = full ? 1 : 0.55
    inkLine(c, [a, b], lw * 0.8 * sk, alpha(p.line, 0.9), seed + 60, amp: 0.4)
    inkLine(c, [a, b], lw * 0.5 * sk, p.scar, seed + 61, amp: 0.4)
    for t in (full ? [0.28, 0.5, 0.72] : [0.5]) as [CGFloat] {
        let q = lerp(a, b, t)
        inkLine(c, [pt(q.x - 3.2, q.y - 2.3), pt(q.x + 3.2, q.y + 2.3)], lw * 0.45, p.scar, seed + 62, amp: 0.1)
    }
    // nose
    let nose = poly([pt(-6.5, 0.5), pt(6.5, 0.5), pt(0, 8.5)])
    solid(c, p, nose, p.nose, lw: lw * 0.6, seed: seed + 70, amp: 0.2)
    // mouth
    let ml = lw * 0.95
    switch m.mouth {
    case 2:
        let mouth = poly(chaikin([pt(-19, 13), pt(-8, 10), pt(0, 12), pt(8, 10), pt(19, 13), pt(15, 28), pt(0, 34), pt(-15, 28)]))
        let mp = wob(mouth, 0.3, seed + 80)
        fill(c, mp, mixc(p.danger, p.line, 0.72))
        c.saveGState(); c.addPath(mp); c.clip(); fill(c, ell(0, 31, 11, 6), p.danger); c.restoreGState()
        outline(c, mp, ml, p.line, seed + 80)
        for sx in [-1.0, 1.0] as [CGFloat] {
            solid(c, p, poly([pt(sx * 12 - 3, 11), pt(sx * 12 + 3.5, 11.5), pt(sx * 12 + 0.5, 23)]), p.bone, lw: lw * 0.45, seed: seed + 81, amp: 0.1)
            solid(c, p, poly([pt(sx * 5 - 2, 29), pt(sx * 5 + 2, 29), pt(sx * 5, 24)]), p.bone, lw: lw * 0.35, seed: seed + 83, amp: 0.1)
        }
    case 3:
        inkCurve(c, mk { $0.move(to: pt(0, 9)); $0.addLine(to: pt(0, 13)) }, ml, p.line, seed + 80)
        inkCurve(c, mk { $0.move(to: pt(-13, 20)); $0.addQuadCurve(to: pt(13, 19), control: pt(0, 12)) }, ml, p.line, seed + 81)
    default:
        inkCurve(c, mk { $0.move(to: pt(0, 8)); $0.addLine(to: pt(0, 12)) }, ml, p.line, seed + 80)
        let rEnd = m.mouth == 1 ? pt(21, 11) : pt(18, 23)
        let rCtl = m.mouth == 1 ? pt(12, 21) : pt(13, 10)
        inkCurve(c, mk { $0.move(to: pt(0, 12)); $0.addQuadCurve(to: rEnd, control: rCtl) }, ml, p.line, seed + 81)
        inkCurve(c, mk { $0.move(to: pt(0, 12)); $0.addQuadCurve(to: pt(-18, 23), control: pt(-13, 10)) }, ml, p.line, seed + 82)
        solid(c, p, poly([pt(6.5, 12), pt(12.5, 14.5), pt(10, 22)]), p.bone, lw: lw * 0.5, seed: seed + 84, amp: 0.1)
    }
    // ragged whiskers
    if full {
        let wl = lw * 0.42
        let wc = alpha(p.bone, 0.92)
        let R: [[CGPoint]] = [[pt(22, 17), pt(42, 13), pt(60, 18)], [pt(21, 22), pt(40, 27), pt(50, 36)]]
        let Lh: [[CGPoint]] = [[pt(-22, 17), pt(-36, 14), pt(-44, 19)], [pt(-21, 22), pt(-44, 25), pt(-66, 33)]]
        for (j, w) in (R + Lh).enumerated() {
            let pth = mk { $0.move(to: w[0]); for q in w.dropFirst() { $0.addLine(to: q) } }
            outline(c, wob(pth, 0.25, seed + 90 + j), wl, wc, seed + j)
        }
    }
    c.restoreGState()
}

func kPaw(_ c: CGContext, _ p: Pal, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, _ rot: CGFloat, lw: CGFloat = 2.8, seed: Int = 5) {
    c.saveGState()
    c.translateBy(x: x, y: y); c.rotate(by: rot); c.scaleBy(x: s, y: s)
    for dy in [-6.5, 0.0, 6.5] as [CGFloat] {   // claws out
        solid(c, p, poly([pt(14, dy - 2.6), pt(30, dy + 0.6), pt(14, dy + 2.8)]), p.bone, lw: lw * 0.5, seed: seed + Int(dy), amp: 0.1)
    }
    solid(c, p, rrect(-17, -12, 12, 12, 9), p.fur, shade: p.furShade, rim: p.furRim, lw: lw, seed: seed, d: 4)
    for dy in [-6.5, 0.0, 6.5] as [CGFloat] {
        solid(c, p, ell(12, dy, 5.2, 4.8), p.fur, shade: p.furShade, lw: lw * 0.8, seed: seed + Int(dy) + 3, amp: 0.2, d: 2)
    }
    c.restoreGState()
}

func kArm(_ c: CGContext, _ p: Pal, _ path: CGPath, w: CGFloat, lw: CGFloat, seed: Int = 6) {
    let wp = wob(path, 0.5, seed)
    c.saveGState(); c.setLineCap(.round); c.setLineJoin(.round)
    c.addPath(wp); c.setStrokeColor(p.line); c.setLineWidth(w + lw * 2); c.strokePath()
    c.addPath(wp); c.setStrokeColor(p.fur); c.setLineWidth(w); c.strokePath()
    c.translateBy(x: 2.2, y: 2.8)
    c.addPath(wp); c.setStrokeColor(alpha(p.furShade, 0.9)); c.setLineWidth(w * 0.42); c.strokePath()
    c.restoreGState()
}

func kMug(_ c: CGContext, _ p: Pal, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, _ rot: CGFloat, lw: CGFloat = 2.6, seed: Int = 8) {
    c.saveGState()
    c.translateBy(x: x, y: y); c.rotate(by: rot); c.scaleBy(x: s, y: s)
    let hp = wob(ell(13, 0, 7.5, 8.5), 0.3, seed)
    c.addPath(hp); c.setStrokeColor(p.line); c.setLineWidth(6 + lw * 2); c.strokePath()
    c.addPath(hp); c.setStrokeColor(p.bone); c.setLineWidth(6); c.strokePath()
    let body = wob(poly([pt(-13, -15), pt(13, -15), pt(11, 15), pt(-11, 15)]), 0.3, seed + 1)
    fill(c, body, p.bone)
    c.saveGState(); c.addPath(body); c.clip()
    fill(c, poly([pt(-14, -3), pt(14, -3), pt(14, 5), pt(-14, 5)]), p.accent)
    band(c, body, p.boneShade, 4, 0)
    c.restoreGState()
    outline(c, body, lw, p.line, seed + 1)
    let cof = ell(0, -15, 13, 3.6)
    fill(c, cof, p.coffee); outline(c, cof, lw * 0.6, p.line, seed + 2)
    c.restoreGState()
}

func kDrops(_ c: CGContext, _ p: Pal, _ pts: [(CGFloat, CGFloat, CGFloat)]) {
    for (x, y, r) in pts { fill(c, ell(x, y, r, r * 1.25), p.coffee); stroke(c, ell(x, y, r, r * 1.25), p.line, 1) }
}

func kBurst(_ c: CGContext, _ p: Pal, _ r: CGRect, tail: CGPoint, baseX: CGFloat, f: CGColor, lw: CGFloat, seed: Int) {
    var pts: [CGPoint] = []
    let st: CGFloat = 9, jag: CGFloat = 3.5
    var j = 0
    var x = r.minX
    while x < r.maxX { pts.append(pt(x, r.minY - (j % 2 == 0 ? 0 : jag))); x += st; j += 1 }
    var y = r.minY
    while y < r.maxY { pts.append(pt(r.maxX + (j % 2 == 0 ? 0 : jag), y)); y += st; j += 1 }
    x = r.maxX
    var inserted = false
    while x > r.minX {
        if !inserted && x <= baseX + 10 {
            pts.append(pt(baseX + 10, r.maxY)); pts.append(tail); pts.append(pt(baseX - 10, r.maxY))
            inserted = true
            x = baseX - 10
        }
        pts.append(pt(x, r.maxY + (j % 2 == 0 ? 0 : jag))); x -= st; j += 1
    }
    y = r.maxY
    while y > r.minY { pts.append(pt(r.minX - (j % 2 == 0 ? 0 : jag), y)); y -= st; j += 1 }
    solid(c, p, poly(pts), f, shade: mixc(f, p.line, 0.28), lw: lw, seed: seed, amp: 0.35, d: 4)
}

func kCrackSticker(_ c: CGContext, _ p: Pal, _ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat, lw: CGFloat, seed: Int) {
    let r = rrect(x, y, x + w, y + h, 4)
    fill(c, r, p.line)
    outline(c, wob(rrect(x + 1.5, y + 1.5, x + w - 1.5, y + h - 1.5, 3), 0.2, seed), lw * 0.7, p.accent, seed)
    // crack burst
    let o = pt(x + w * 0.58, y + h * 0.42)
    let ends: [CGPoint] = [pt(x + 3, y + 2), pt(x + w * 0.3, y + h - 3), pt(x + w - 3, y + h - 4), pt(x + w - 4, y + 3), pt(x + w * 0.7, y + 3), pt(x + 3, y + h * 0.6)]
    for (i, e) in ends.enumerated() {
        let mid = lerp(o, e, 0.5)
        inkLine(c, [o, pt(mid.x + hn(i, seed) * 3, mid.y + hn(i, seed + 1) * 3), e], lw * 0.4, p.bone, seed + i, amp: 0.1)
    }
    for (i, d) in [pt(x + 6, y + h - 7), pt(x + w - 9, y + 7), pt(x + w * 0.32, y + 6)].enumerated() {
        fill(c, rectP(d.x, d.y, 2.6, 2.6), i == 1 ? p.danger : (i == 0 ? p.info : p.accent))
    }
}

func kLaptopBack(_ c: CGContext, _ p: Pal, lw: CGFloat) {
    // base slab then lid (we see the back of the lid)
    solid(c, p, rrect(20, 181, 188, 192, 4), mixc(p.lid, p.bone, 0.1), shade: mixc(p.lid, p.line, 0.5), lw: lw, seed: 31, amp: 0.3, d: 3)
    solid(c, p, rrect(32, 119, 176, 184, 7), p.lid, shade: mixc(p.lid, p.line, 0.45), rim: mixc(p.lid, p.bone, 0.18), lw: lw, seed: 32, amp: 0.4, d: 6)
    kCrackSticker(c, p, 84, 138, 54, 34, lw: lw, seed: 3)
    // no-symbol sticker
    let nc = pt(55, 160)
    fill(c, ell(nc.x, nc.y, 10, 10), p.bone)
    outline(c, wob(ell(nc.x, nc.y, 9, 9), 0.2, 4), lw * 0.9, p.danger, 4)
    inkLine(c, [pt(nc.x - 6.4, nc.y - 6.4), pt(nc.x + 6.4, nc.y + 6.4)], lw * 0.9, p.danger, 5)
    // scratches
    for i in 0..<3 {
        let dx = CGFloat(i) * 6
        inkLine(c, [pt(152 + dx, 132), pt(146 + dx, 160)], 1.3, alpha(p.bone, 0.28), 70 + i, amp: 0.3)
    }
}

// MARK: assets
func hardGradient(_ c: CGContext, _ a: CGColor, _ b: CGColor, _ w: CGFloat, _ h: CGFloat) {
    let g = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB)!, colors: [a, b] as CFArray, locations: [0, 1])!
    c.drawLinearGradient(g, start: pt(0, 0), end: pt(0, h), options: [])
}

func v3Hero(_ p: Pal) -> CGContext {
    let c = makeCtx(269, 203)
    let lw: CGFloat = 3.0
    // table
    solid(c, p, rrect(-8, 190, 240, 206, 5), p.table, shade: mixc(p.table, p.line, 0.45), rim: mixc(p.table, p.bone, 0.12), lw: lw, seed: 11, amp: 0.3, d: 4)
    // arm reaching around the laptop (behind lid)
    kArm(c, p, mk { $0.move(to: pt(140, 118)); $0.addQuadCurve(to: pt(196, 158), control: pt(190, 108)) }, w: 17, lw: lw)
    kShoulders(c, p, 104, 124, 64, 34, lw: lw, seed: 12)
    kHead(c, p, 104, 82, 0.96, .glare, lw: 3.2, seed: 2)
    kLaptopBack(c, p, lw: lw)
    kPaw(c, p, 62, 118, 0.62, .pi / 2 - 0.15, lw: 3.0, seed: 14)
    // knocked mug
    kPaw(c, p, 205, 160, 0.9, 0.12, lw: 2.8, seed: 15)
    kMug(c, p, 248, 168, 0.9, 0.62, seed: 16)
    kDrops(c, p, [(236, 144, 2.6), (258, 150, 2), (262, 182, 2.4), (243, 190, 1.8)])
    for (x0, y0, x1, y1) in [(232.0, 130.0, 242.0, 142.0), (222.0, 136.0, 230.0, 146.0), (252.0, 128.0, 256.0, 140.0)] as [(CGFloat, CGFloat, CGFloat, CGFloat)] {
        inkLine(c, [pt(x0, y0), pt(x1, y1)], 2, alpha(p.bone, 0.8), 77, amp: 0.2)
    }
    // MEH bubble
    kBurst(c, p, CGRect(x: 168, y: 10, width: 86, height: 40), tail: pt(150, 66), baseX: 176, f: p.bone, lw: 2.8, seed: 18)
    shout(c, "MEH", 211, 30, 30, -0.06, p.line, p.bone, 0.3, skew: 0.12)
    return c
}

func v3Update(_ p: Pal) -> CGContext {
    let c = makeCtx(292, 196)
    let lw: CGFloat = 3.0
    // floor + table
    solid(c, p, rectP(-6, 184, 304, 16), mixc(p.table, p.bg0, 0.35), lw: lw, seed: 21, amp: 0.25)
    solid(c, p, rectP(180, 160, 14, 26), p.table, shade: mixc(p.table, p.line, 0.45), lw: lw, seed: 22, amp: 0.3, d: 3)
    solid(c, p, rrect(-8, 146, 214, 162, 5), p.table, shade: mixc(p.table, p.line, 0.45), rim: mixc(p.table, p.bone, 0.12), lw: lw, seed: 23, amp: 0.3, d: 4)
    // tail
    kArm(c, p, mk { $0.move(to: pt(30, 130)); $0.addCurve(to: pt(34, 70), control1: pt(2, 128), control2: pt(10, 90)) }, w: 11, lw: lw, seed: 24)
    // body + haunch
    var body: [CGPoint] = []
    for i in 0..<22 {
        let a = CGFloat(i) / 22 * 2 * .pi
        body.append(pt(72 + cos(a) * 58 * ((i % 3 == 0) ? 1.04 : 0.97), 118 + sin(a) * 36 * ((i % 3 == 0) ? 1.04 : 0.97)))
    }
    solid(c, p, poly(chaikin(body)), p.fur, shade: p.furShade, rim: p.furRim, lw: lw, seed: 25, amp: 0.7, d: 7)
    inkCurve(c, mk { $0.addArc(center: pt(48, 128), radius: 22, startAngle: -1.2, endAngle: 1.6, clockwise: false) }, 2.2, alpha(p.line, 0.8), 26)
    solid(c, p, rrect(24, 140, 62, 152, 5), p.fur, shade: p.furShade, lw: lw, seed: 27, amp: 0.3, d: 3)   // hind foot
    // shoving arm
    kArm(c, p, mk { $0.move(to: pt(108, 112)); $0.addQuadCurve(to: pt(162, 124), control: pt(138, 104)) }, w: 17, lw: lw, seed: 28)
    kHead(c, p, 100, 70, 0.9, .glare, lw: 3.2, seed: 3)
    kPaw(c, p, 168, 126, 0.95, 0.04, lw: 2.8, seed: 29)
    // mug tipping over the edge
    kMug(c, p, 214, 124, 1.0, 0.5, seed: 30)
    kDrops(c, p, [(232, 108, 2.6), (240, 124, 2), (236, 146, 2.4)])
    // dashed fall + shards
    c.saveGState()
    c.setLineDash(phase: 0, lengths: [4, 5])
    inkCurve(c, mk { $0.move(to: pt(236, 150)); $0.addQuadCurve(to: pt(258, 178), control: pt(256, 156)) }, 2, alpha(p.bone, 0.55), 31)
    c.restoreGState()
    for (i, s) in [(232.0, 187.0, 13.0, 9.0), (247.0, 188.0, 10.0, 12.0), (260.0, 187.0, 15.0, 7.0), (276.0, 188.0, 9.0, 10.0)].enumerated() {
        let sh = poly([pt(s.0, s.1), pt(s.0 + s.2 * 0.45, s.1 - s.3), pt(s.0 + s.2, s.1 - 1)])
        solid(c, p, sh, i % 2 == 0 ? p.bone : p.boneShade, lw: 1.8, seed: 40 + i, amp: 0.1)
    }
    solid(c, p, ell(268, 187.5, 3.4, 2), p.coffee, lw: 1.2, seed: 49, amp: 0.1)
    shout(c, "NOPE", 244, 38, 30, -0.1, p.danger, p.line, 2.6)
    return c
}

func v3MessageBox(_ p: Pal) -> CGContext {
    let c = makeCtx(260, 152)
    let pts: [CGPoint] = [pt(4, 0), pt(38, 13), pt(222, 13), pt(236, 3), pt(240, 11), pt(247, 5), pt(258, 0), pt(259, 126), pt(252, 136),
                          pt(58, 137), pt(34, 151), pt(38, 137), pt(10, 137), pt(2, 128), pt(3, 40)]
    let path = poly(pts)
    let wp = wob(path, 0.5, 51)
    fill(c, wp, p.panel)
    band(c, wp, mixc(p.panel, p.line, 0.45), 0, 7)
    c.saveGState(); c.addPath(wp); c.clip()
    for i in 0..<3 {   // claw scratches in the bottom-right corner, kept low-contrast
        let o = CGFloat(i) * 7
        inkCurve(c, mk { $0.move(to: pt(206 + o, 100)); $0.addQuadCurve(to: pt(236 + o, 140), control: pt(222 + o, 116)) }, 2.2, alpha(p.accent, 0.3), 52 + i)
    }
    c.restoreGState()
    outline(c, wp, 4.2, p.line, 53)
    // accent border, inset
    var t = CGAffineTransform(translationX: 130, y: 76).scaledBy(x: 0.955, y: 0.9).translatedBy(x: -130, y: -76)
    let inner = wob(path.copy(using: &t)!, 0.5, 54)
    outline(c, inner, 3.0, p.accent, 55)
    return c
}

func v3Background(_ p: Pal) -> CGContext {
    let c = makeCtx(320, 241)
    hardGradient(c, p.bg0, p.bg1, 320, 241)
    c.setStrokeColor(alpha(p.bone, 0.035)); c.setLineWidth(1)
    var x: CGFloat = 0
    while x <= 320 { c.move(to: pt(x + 0.5, 0)); c.addLine(to: pt(x + 0.5, 241)); x += 20 }
    var y: CGFloat = 0
    while y <= 241 { c.move(to: pt(0, y + 0.5)); c.addLine(to: pt(320, y + 0.5)); y += 20 }
    c.strokePath()
    // concrete speckle
    for i in 0..<420 {
        let sx = (hn(i, 3) * 0.5 + 0.5) * 320, sy = (hn(i, 4) * 0.5 + 0.5) * 241
        let v = hn(i, 5) > 0 ? alpha(p.bone, 0.05) : alpha(p.line, 0.25)
        fill(c, rectP(sx, sy, 1.5, 1.5), v)
    }
    // claw scratch groups
    let groups: [(CGFloat, CGFloat, CGFloat)] = [(34, 40, 0.5), (236, 120, -0.2), (118, 196, 0.2)]
    for (gi, g) in groups.enumerated() {
        for k in 0..<4 {
            let o = CGFloat(k) * 6
            let a = pt(g.0 + o, g.1 + o * g.2), b = pt(g.0 + o + 26 + hn(k, gi) * 6, g.1 + 52 + hn(k, gi + 9) * 8)
            inkCurve(c, mk { $0.move(to: a); $0.addQuadCurve(to: b, control: pt((a.x + b.x) / 2 + 4, (a.y + b.y) / 2)) }, 1.9 - CGFloat(k) * 0.2, alpha(p.bone, 0.075), 60 + gi * 5 + k, amp: 0.4)
        }
    }
    return c
}

func v3Header(_ p: Pal) -> CGContext {
    let c = makeCtx(300, 39)
    var pts: [CGPoint] = [pt(0, 0), pt(300, 0), pt(300, 33)]
    var x: CGFloat = 300
    var j = 0
    while x > 0 { pts.append(pt(x, 33 + (j % 2 == 0 ? 0 : 3.5) + hn(j, 2) * 0.8)); x -= 12; j += 1 }
    pts.append(pt(0, 33))
    let hp = wob(poly(pts), 0.3, 71)
    c.saveGState(); c.addPath(hp); c.clip()
    hardGradient(c, mixc(p.panel, p.bg1, 0.4), mixc(p.panel, p.line, 0.5), 300, 39)
    for i in 0..<3 {
        let o = CGFloat(i) * 6
        inkLine(c, [pt(262 + o, -2), pt(250 + o, 30)], 1.8, alpha(p.bone, 0.08), 72 + i, amp: 0.3)
    }
    c.restoreGState()
    // jagged accent edge only along the bottom
    let edge = mk { e in e.addLines(between: Array(pts.dropFirst(2))) }
    outline(c, wob(edge, 0.3, 71), 2.6, p.accent, 73)
    return c
}

// MARK: icons (70x70, dark disc)
func discBase(_ c: CGContext, _ p: Pal) {
    fill(c, ell(35, 35, 34, 34), p.disc)
    outline(c, wob(ell(35, 35, 32.8, 32.8), 0.3, 80), 2.4, p.accent, 80)
}
func clipDisc(_ c: CGContext) { c.saveGState(); c.addPath(ell(35, 35, 31.4, 31.4)); c.clip() }
func iconHead(_ c: CGContext, _ p: Pal, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, _ m: Mood = .glare) {
    kHead(c, p, x, y, s, m, lw: 6.0, full: false, seed: 4)
}

func v3IconDino(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); discBase(c, p)
    let d = p.accent, ds = mixc(p.accent, p.line, 0.35)
    clipDisc(c)
    // dino being stomped flat: tail wedge, flat body, big head with X eye, stubby legs
    solid(c, p, poly([pt(10, 60), pt(24, 53), pt(26, 62)]), d, lw: 2.2, seed: 81, amp: 0.1)
    solid(c, p, rrect(20, 52, 46, 63, 5), d, shade: ds, lw: 2.2, seed: 82, amp: 0.15, d: 3)
    solid(c, p, rrect(40, 46, 60, 59, 4), d, shade: ds, lw: 2.2, seed: 83, amp: 0.15, d: 3)
    inkLine(c, [pt(48, 49), pt(52, 53)], 1.7, p.line, 84, amp: 0); inkLine(c, [pt(52, 49), pt(48, 53)], 1.7, p.line, 85, amp: 0)
    solid(c, p, rectP(26, 63, 5, 3), d, lw: 1.4, seed: 86, amp: 0.1); solid(c, p, rectP(38, 63, 5, 3), d, lw: 1.4, seed: 86, amp: 0.1)
    // impact lines
    for (x0, y0, x1, y1) in [(12.0, 46.0, 17.0, 49.0), (60.0, 63.0, 65.0, 66.0), (9.0, 54.0, 14.0, 54.0)] as [(CGFloat, CGFloat, CGFloat, CGFloat)] {
        inkLine(c, [pt(x0, y0), pt(x1, y1)], 2, p.bone, 87, amp: 0)
    }
    c.restoreGState()
    iconHead(c, p, 35, 28, 0.4)
    clipDisc(c)
    kPaw(c, p, 35, 45, 0.52, .pi / 2, lw: 2.6, seed: 87)
    c.restoreGState()
    return c
}
func v3IconBadUSB(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); discBase(c, p)
    iconHead(c, p, 31, 30, 0.44)
    // USB-A plug in the mouth, cable running off, frayed
    clipDisc(c)
    let cable = mk { $0.move(to: pt(48, 39)); $0.addQuadCurve(to: pt(53, 58), control: pt(63, 46)) }
    c.saveGState(); c.setLineCap(.round)
    c.addPath(cable); c.setStrokeColor(p.line); c.setLineWidth(6.4); c.strokePath()
    c.addPath(cable); c.setStrokeColor(p.bone); c.setLineWidth(3.2); c.strokePath()
    c.restoreGState()
    solid(c, p, rectP(24, 35, 24, 9), p.accent, shade: mixc(p.accent, p.line, 0.4), lw: 1.9, seed: 91, amp: 0.1, d: 2)
    fill(c, rectP(29, 37.5, 4, 4), p.line); fill(c, rectP(36, 37.5, 4, 4), p.line)
    for (i, col) in [p.danger, p.info, p.bone].enumerated() {
        inkLine(c, [pt(53, 58), pt(46 + CGFloat(i) * 7, 63 + CGFloat(i % 2))], 1.8, col, 92 + i, amp: 0.1)
    }
    c.restoreGState()
    return c
}
func v3IconIR(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); discBase(c, p)
    iconHead(c, p, 28, 37, 0.42)
    // remote + paw + IR rays
    clipDisc(c)
    c.saveGState(); c.translateBy(x: 51, y: 42); c.rotate(by: -0.5)
    solid(c, p, rrect(-6, -17, 6, 17, 3), p.accent, shade: mixc(p.accent, p.line, 0.4), lw: 2.0, seed: 95, amp: 0.1, d: 2)
    fill(c, ell(0, -9, 2.2, 2.2), p.line); fill(c, rectP(-2.4, -1, 4.8, 3), p.line); fill(c, rectP(-2.4, 5, 4.8, 3), p.line)
    c.restoreGState()
    for (i, r) in [8.0, 13.0, 18.0].enumerated() {
        let a = mk { $0.addArc(center: pt(54, 24), radius: CGFloat(r), startAngle: -1.9, endAngle: -0.1, clockwise: false) }
        outline(c, a, 2.2, i == 2 ? alpha(p.danger, 0.9) : p.danger, 96 + i)
    }
    kPaw(c, p, 46, 54, 0.46, -0.9, lw: 2.4, seed: 98)
    c.restoreGState()
    return c
}
func v3IconVoice(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); discBase(c, p)
    clipDisc(c)
    let band: CGPath = mk { $0.addArc(center: pt(35, 38), radius: 23, startAngle: .pi * 1.02, endAngle: .pi * 1.98, clockwise: false) }
    c.saveGState(); c.setLineCap(.round)
    c.addPath(band); c.setStrokeColor(p.line); c.setLineWidth(8.6); c.strokePath()
    c.addPath(band); c.setStrokeColor(p.accent); c.setLineWidth(4.2); c.strokePath()
    c.restoreGState()
    c.restoreGState()
    iconHead(c, p, 35, 40, 0.4)
    clipDisc(c)
    solid(c, p, rrect(7, 31, 18, 52, 4), p.accent, shade: mixc(p.accent, p.line, 0.4), lw: 2.1, seed: 99, amp: 0.1, d: 2)
    solid(c, p, rrect(52, 31, 63, 52, 4), p.accent, shade: mixc(p.accent, p.line, 0.4), lw: 2.1, seed: 100, amp: 0.1, d: 2)
    inkCurve(c, mk { $0.move(to: pt(12, 52)); $0.addQuadCurve(to: pt(27, 61), control: pt(12, 63)) }, 2.2, p.bone, 101)
    solid(c, p, ell(29, 61, 3.4, 3.4), p.bone, lw: 1.6, seed: 102, amp: 0.1)
    solid(c, p, ell(55, 15, 4.4, 4.4), p.danger, lw: 1.8, seed: 103, amp: 0.1)
    c.restoreGState()
    return c
}


// MARK: props / gag sheet (not firmware assets)
func propsSheet(_ p: Pal) -> CGContext {
    let c = makeCtx(960, 190)
    hardGradient(c, p.bg0, p.bg1, 960, 190)
    let lw: CGFloat = 3.0
    func cell(_ i: Int, _ name: String, _ f: () -> Void) {
        c.saveGState(); c.translateBy(x: CGFloat(i) * 160 + 10, y: 8); f(); c.restoreGState()
        label(c, name, CGFloat(i) * 160 + 14, 182, 10, alpha(p.bone, 0.7))
    }
    cell(0, "DO NOT + claw") {
        fill(c, ell(70, 80, 46, 46), p.bone)
        outline(c, wob(ell(70, 80, 40, 40), 0.4, 1), 9, p.danger, 1)
        inkLine(c, [pt(41, 51), pt(99, 109)], 9, p.danger, 2)
        outline(c, wob(ell(70, 80, 46, 46), 0.4, 3), lw, p.line, 3)
        for i in 0..<3 { let o = CGFloat(i) * 9; inkLine(c, [pt(88 + o, 30), pt(70 + o, 128)], 2.4, p.line, 4 + i, amp: 0.4); inkLine(c, [pt(90 + o, 30), pt(72 + o, 128)], 1.2, p.bone, 7 + i, amp: 0.4) }
    }
    cell(1, "cord from mouth") {
        kShoulders(c, p, 70, 140, 50, 28, lw: lw, seed: 5)
        kHead(c, p, 70, 80, 0.7, .glare, lw: 3.6, full: false, seed: 5)
        let cord = mk { $0.move(to: pt(76, 98)); $0.addCurve(to: pt(128, 150), control1: pt(120, 88), control2: pt(88, 130)) }
        c.saveGState(); c.setLineCap(.round)
        c.addPath(cord); c.setStrokeColor(p.line); c.setLineWidth(8); c.strokePath()
        c.addPath(cord); c.setStrokeColor(p.bone); c.setLineWidth(4); c.strokePath(); c.restoreGState()
        solid(c, p, rectP(58, 92, 22, 12), p.bone, shade: p.boneShade, lw: 2.2, seed: 6, amp: 0.1, d: 3)
        fill(c, rectP(44, 94, 14, 3), p.line); fill(c, rectP(44, 99, 14, 3), p.line)
    }
    cell(2, "dead pixels") {
        solid(c, p, rrect(14, 24, 126, 112, 6), p.lid, shade: mixc(p.lid, p.line, 0.5), lw: lw, seed: 7, d: 5)
        solid(c, p, rectP(24, 33, 92, 62), p.line, lw: 2, seed: 8, amp: 0.2)
        let o = pt(86, 58)
        for (i, e) in [pt(26, 36), pt(60, 93), pt(112, 90), pt(114, 38), pt(70, 35)].enumerated() { inkLine(c, [o, lerp(o, e, 0.5), e], 1.4, p.bone, 9 + i, amp: 1.2) }
        for (i, (x, y)) in [(34.0, 70.0), (100.0, 80.0), (50.0, 44.0), (94.0, 44.0)].enumerated() { fill(c, rectP(CGFloat(x), CGFloat(y), 3, 3), [p.danger, p.info, p.accent, p.bone][i]) }
        solid(c, p, rectP(60, 112, 20, 14), p.lid, lw: 2.4, seed: 10, amp: 0.1)
        solid(c, p, rrect(40, 126, 100, 134, 3), p.lid, lw: 2.4, seed: 11, amp: 0.1)
    }
    cell(3, "couch shredding") {
        solid(c, p, rrect(6, 50, 134, 120, 12), p.table, shade: mixc(p.table, p.line, 0.5), rim: mixc(p.table, p.bone, 0.12), lw: lw, seed: 12, d: 6)
        solid(c, p, rrect(18, 38, 122, 68, 10), mixc(p.table, p.bone, 0.06), lw: lw, seed: 13, d: 4)
        for i in 0..<4 { let o = CGFloat(i) * 9; inkLine(c, [pt(44 + o, 60), pt(38 + o, 112)], 3, p.line, 14 + i, amp: 0.5); inkLine(c, [pt(45 + o, 60), pt(39 + o, 112)], 1.2, alpha(p.bone, 0.6), 18 + i, amp: 0.5) }
        for (x, y) in [(30.0, 120.0), (60.0, 124.0), (96.0, 122.0)] { solid(c, p, poly([pt(CGFloat(x), CGFloat(y)), pt(CGFloat(x) + 10, CGFloat(y) + 3), pt(CGFloat(x) + 3, CGFloat(y) + 9)]), p.bone, lw: 1.6, seed: 22, amp: 0.1) }
        kPaw(c, p, 104, 52, 0.7, .pi / 2 + 0.3, lw: 2.8, seed: 23)
    }
    cell(4, "worst roommate") {
        solid(c, p, rrect(44, 126, 96, 142, 3), p.table, lw: lw, seed: 24, amp: 0.2)
        solid(c, p, rectP(62, 100, 16, 28), p.accent, shade: mixc(p.accent, p.line, 0.4), lw: lw, seed: 25, amp: 0.2, d: 3)
        for sx in [-1.0, 1.0] as [CGFloat] { outline(c, wob(ell(70 + sx * 30, 62, 10, 12), 0.2, 26), 4, p.accent, 26) }
        solid(c, p, poly([pt(36, 30), pt(104, 30), pt(94, 80), pt(76, 100), pt(64, 100), pt(46, 80)]), p.accent, shade: mixc(p.accent, p.line, 0.4), rim: mixc(p.accent, p.bone, 0.3), lw: lw, seed: 27, d: 6)
        shout(c, "#1", 70, 58, 24, 0, p.line, mixc(p.accent, p.bone, 0.3), 0.5)
        fill(c, rectP(52, 131, 36, 6), p.bone)
    }
    cell(5, "dumpster lid") {
        solid(c, p, poly([pt(10, 70), pt(130, 70), pt(120, 142), pt(20, 142)]), mixc(p.lid, p.fur, 0.3), shade: mixc(p.lid, p.line, 0.55), lw: lw, seed: 28, d: 7)
        fill(c, poly([pt(14, 58), pt(126, 58), pt(130, 70), pt(10, 70)]), p.line)
        for (x, ey) in [(48.0, 64.0), (88.0, 64.0)] { solid(c, p, ell(CGFloat(x), CGFloat(ey), 8, 3.4), p.eye, lw: 1.6, seed: 29, amp: 0.1) ; fill(c, ell(CGFloat(x), CGFloat(ey), 1.4, 3), p.line) }
        solid(c, p, poly([pt(8, 58), pt(132, 44), pt(136, 56), pt(14, 66)]), mixc(p.lid, p.fur, 0.5), shade: mixc(p.lid, p.line, 0.55), lw: lw, seed: 30, d: 4)
        for i in 0..<3 { inkLine(c, [pt(30 + CGFloat(i) * 30, 80), pt(34 + CGFloat(i) * 30, 130)], 2, alpha(p.line, 0.5), 31 + i, amp: 0.2) }
    }
    return c
}

// MARK: assets registry + sheets
struct Asset3 { let name: String; let w: Int; let h: Int; let fn: (Pal) -> CGContext }
let assets3: [Asset3] = [
    Asset3(name: "ui_img_mascot_png", w: 269, h: 203, fn: v3Hero),
    Asset3(name: "ui_img_mascot_update_png", w: 292, h: 196, fn: v3Update),
    Asset3(name: "ui_img_message_box_bg_png", w: 260, h: 152, fn: v3MessageBox),
    Asset3(name: "ui_img_background_png", w: 320, h: 241, fn: v3Background),
    Asset3(name: "ui_img_header_png", w: 300, h: 39, fn: v3Header),
    Asset3(name: "ui_img_dino_png", w: 70, h: 70, fn: v3IconDino),
    Asset3(name: "ui_img_badusb_png", w: 70, h: 70, fn: v3IconBadUSB),
    Asset3(name: "ui_img_infrared_png", w: 70, h: 70, fn: v3IconIR),
    Asset3(name: "ui_img_voicerec_png", w: 70, h: 70, fn: v3IconVoice),
]

func put(_ c: CGContext, _ src: CGContext, _ x: CGFloat, _ y: CGFloat, scale: CGFloat = 1) {
    drawImage(c, src.makeImage()!, CGRect(x: x, y: y, width: CGFloat(src.width) * scale, height: CGFloat(src.height) * scale))
}

func contactSheet(_ p: Pal, _ r: [String: CGContext]) -> CGContext {
    let W = 960, H = 760
    let c = makeCtx(W, H)
    fill(c, rectP(0, 0, CGFloat(W), CGFloat(H)), col(0x0a0a0c))
    label(c, "FancyKitty v3 / \(p.name)", 16, 22, 14, col(0xdddddd), bold: true)
    // hero + update on the palette background
    let bg = r["ui_img_background_png"]!
    for (x, nm) in [(16, "ui_img_mascot_png"), (300, "ui_img_mascot_update_png")] {
        let a = r[nm]!
        let tile = makeCtx(a.width, a.height)
        for ty in stride(from: 0, to: a.height, by: 241) { for tx in stride(from: 0, to: a.width, by: 320) { put(tile, bg, CGFloat(tx), CGFloat(ty)) } }
        put(tile, a, 0, 0)
        put(c, tile, CGFloat(x), 40)
    }
    label(c, "mascot 269x203", 16, 258, 10, col(0x888888)); label(c, "mascot_update 292x196", 300, 258, 10, col(0x888888))
    put(c, r["ui_img_message_box_bg_png"]!, 610, 40)
    label(c, "message_box_bg 260x152 (white text mock)", 610, 208, 10, col(0x888888))
    let mb = makeCtx(1, 1); _ = mb
    label(c, "Flash firmware?", 650, 100, 15, col(0xffffff), font: "Helvetica-Bold")
    label(c, "Nobody asked, but fine.", 650, 124, 12, col(0xffffff), font: "Helvetica")
    // background + header
    put(c, bg, 16, 290)
    let hd = r["ui_img_header_png"]!
    put(c, hd, 16, 290); label(c, "SETTINGS", 30, 315, 13, col(0xffffff), font: "Helvetica-Bold")
    label(c, "background 320x241 + header 300x39", 16, 540, 10, col(0x888888))
    // icons on varied tile colours, 1x and 2x
    let tiles: [UInt32] = [0xd9d9d9, 0x3a8dde, 0x2e2e2e, p.accent.components![0] > 0.5 ? 0xe8c070 : 0x80d0a0]
    let names = ["ui_img_dino_png", "ui_img_badusb_png", "ui_img_infrared_png", "ui_img_voicerec_png"]
    for (i, nm) in names.enumerated() {
        let x = CGFloat(360 + i * 140)
        fill(c, rrect(x, 290, x + 130, 420, 14), col(tiles[i]))
        put(c, r[nm]!, x + 30, 320, scale: 1.0)
        fill(c, rrect(x, 440, x + 130, 570, 14), col(tiles[(i + 1) % 4]))
        put(c, r[nm]!, x + 5, 445, scale: 1.8)
    }
    label(c, "icons 70x70 on four tile colours (1x) / 1.8x below", 360, 590, 10, col(0x888888))
    return c
}

func expressionStrip(_ p: Pal) -> CGContext {
    let c = makeCtx(760, 230)
    fill(c, rectP(0, 0, 760, 230), p.bg0)
    hardGradient(c, p.bg0, p.bg1, 760, 230)
    let moods: [(String, Mood)] = [("GLARE", .glare), ("SMUG", .smug), ("HISS", .hiss), ("SIDE-EYE", .side)]
    for (i, mm) in moods.enumerated() {
        let cx = CGFloat(95 + i * 190)
        kShoulders(c, p, cx, 178, 74, 38, lw: 3.4, seed: 100 + i)
        kHead(c, p, cx, 110, 1.18, mm.1, lw: 3.4, seed: 7 + i)
        label(c, mm.0, cx - 24, 218, 12, p.bone, bold: true)
    }
    return c
}

func overview(_ rs: [[String: CGContext]]) -> CGContext {
    let colW = 340, W = colW * 3 + 20, H = 700
    let c = makeCtx(W, H)
    fill(c, rectP(0, 0, CGFloat(W), CGFloat(H)), col(0x050506))
    for (i, p) in Pal.all.enumerated() {
        let r = rs[i]
        let x0 = CGFloat(10 + i * colW)
        let letter = ["A", "B", "C"][i]
        label(c, "\(letter)  \(p.name)", x0 + 6, 28, 18, col(0xffffff), bold: true)
        // hero on bg
        let bg = r["ui_img_background_png"]!
        let heroTile = makeCtx(320, 241); put(heroTile, bg, 0, 0)
        put(heroTile, r["ui_img_mascot_png"]!, 25, 18)
        put(c, heroTile, x0, 40)
        // icon row
        fill(c, rrect(x0, 292, x0 + 320, 372, 8), mixc(p.bg1, col(0x808080), 0.35))
        for (k, nm) in ["ui_img_dino_png", "ui_img_badusb_png", "ui_img_infrared_png", "ui_img_voicerec_png"].enumerated() {
            put(c, r[nm]!, x0 + 6 + CGFloat(k) * 78, 297)
        }
        // background + message box mock
        let mock = makeCtx(320, 241); put(mock, bg, 0, 0)
        put(mock, r["ui_img_header_png"]!, 10, 0)
        label(mock, "SETTINGS", 24, 25, 13, col(0xffffff), font: "Helvetica-Bold")
        put(mock, r["ui_img_message_box_bg_png"]!, 30, 60)
        label(mock, "Flash firmware?", 62, 120, 16, col(0xffffff), font: "Helvetica-Bold")
        label(mock, "Nobody asked, but fine.", 62, 146, 12, col(0xffffff), font: "Helvetica")
        label(mock, "[ YES ]    [ NO. ]", 62, 176, 12, col(0xffffff), font: "Helvetica")
        put(c, mock, x0, 390)
        label(c, "same drawing, different colours", x0 + 6, 650, 10, col(0x777777))
    }
    return c
}

func runV3(_ out: String) {
    var results: [[String: CGContext]] = []
    for p in Pal.all {
        let dir = "\(out)/\(p.name)"
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        var r: [String: CGContext] = [:]
        for a in assets3 {
            let ctx = a.fn(p)
            precondition(ctx.width == a.w && ctx.height == a.h, "\(a.name): drew \(ctx.width)x\(ctx.height), expected \(a.w)x\(a.h)")
            save(ctx, "\(dir)/\(a.name).png"); r[a.name] = ctx
        }
        save(contactSheet(p, r), "\(dir)/contact_sheet.png")
        save(expressionStrip(p), "\(dir)/expression_strip.png")
        save(propsSheet(p), "\(dir)/props_sheet.png")
        results.append(r)
    }
    save(overview(results), "\(out)/overview.png")
}
