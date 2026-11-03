// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// GrumpyKitty v3.1 app icons, 70x70. Full-bleed: the head fills the canvas, no ring, no disc.
// A dark rounded-square backing (radius 14, ~88% opaque) keeps every icon readable on any
// tile colour. One chunky prop per icon, a different pose/silhouette per app.
// Origin top-left, y down. Compiled with draw.swift + draw_v3.swift by make_v3.sh.

import CoreGraphics
import Foundation

// MARK: shared bits
let icGreen = col(0x38f07a)
let icGreenD = col(0x0d5a2c)

func icBack(_ c: CGContext, _ p: Pal, tint: CGColor? = nil) {
    var b = mixc(p.bg0, p.line, 0.35)
    if let t = tint { b = mixc(b, t, 0.16) }
    fill(c, rrect(0, 0, 70, 70, 14), alpha(b, 0.9))
}
func icRim(_ c: CGContext, _ p: Pal) {
    c.saveGState()
    c.addPath(rrect(0.6, 0.6, 69.4, 69.4, 13.6)); c.setStrokeColor(alpha(p.bone, 0.16)); c.setLineWidth(1.2); c.strokePath()
    c.restoreGState()
}
func icClip(_ c: CGContext) { c.saveGState(); c.addPath(rrect(0, 0, 70, 70, 14)); c.clip() }

extension Mood {
    /// oversized glaring eyes for 70px icons
    static let icon = Mood(lidL: 0.3, lidR: 0.36, tilt: 9, browL: 0, browR: 0, kL: 1.4, kR: 1.28, mouth: 0)
    static let iconHiss = Mood(lidL: 0.2, lidR: 0.2, tilt: 12, browL: 1, browR: 1, kL: 1.35, kR: 1.25, slit: true, mouth: 2)
    static func iconLook(_ dx: CGFloat) -> Mood {
        Mood(lidL: 0.3, lidR: 0.36, tilt: 9, browL: 0, browR: 0, kL: 1.4, kR: 1.28, pupil: pt(dx, 0), mouth: 0)
    }
}
/// head for icons: effective ink ~2.5px
func icHead(_ c: CGContext, _ p: Pal, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, _ m: Mood = .icon, seed: Int = 4) {
    kHead(c, p, x, y, s, m, lw: 2.5 / s, full: false, seed: seed)
}
/// run drawing in head-local units
func inHead(_ c: CGContext, _ x: CGFloat, _ y: CGFloat, _ s: CGFloat, _ f: () -> Void) {
    c.saveGState(); c.translateBy(x: x, y: y); c.scaleBy(x: s, y: s); f(); c.restoreGState()
}
func amberSolid(_ c: CGContext, _ p: Pal, _ path: CGPath, lw: CGFloat = 2.2, seed: Int, d: CGFloat = 3) {
    solid(c, p, path, p.accent, shade: mixc(p.accent, p.line, 0.42), rim: mixc(p.accent, p.bone, 0.3), lw: lw, seed: seed, amp: 0.15, d: d)
}
func boneSolid(_ c: CGContext, _ p: Pal, _ path: CGPath, lw: CGFloat = 2.0, seed: Int, d: CGFloat = 3) {
    solid(c, p, path, p.bone, shade: p.boneShade, lw: lw, seed: seed, amp: 0.12, d: d)
}
func tinted(_ p: Pal, _ t: CGColor, _ k: CGFloat) -> Pal {
    Pal(name: p.name, bg0: p.bg0, bg1: p.bg1, fur: mixc(p.fur, t, k), belly: mixc(p.belly, t, k * 1.4), line: p.line, eye: p.eye,
        accent: p.accent, danger: p.danger, info: p.info, bone: p.bone, panel: p.panel, table: p.table)
}
func thickLine(_ c: CGContext, _ pth: CGPath, _ outer: CGFloat, _ inner: CGFloat, _ oc: CGColor, _ ic: CGColor) {
    c.saveGState(); c.setLineCap(.round); c.setLineJoin(.round)
    c.addPath(pth); c.setStrokeColor(oc); c.setLineWidth(outer); c.strokePath()
    c.addPath(pth); c.setStrokeColor(ic); c.setLineWidth(inner); c.strokePath()
    c.restoreGState()
}

// MARK: 1 dino: cat stomping a flattened dino, side-on
func v3IconDino(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); icBack(c, p); icClip(c)
    let d = p.accent
    let flat = mixc(d, p.line, 0.1)
    // flat dino pancake along the floor: tail, body with spikes, head with X eye
    solid(c, p, poly([pt(2, 64), pt(14, 57), pt(20, 64)]), flat, lw: 2.0, seed: 81, amp: 0.1)
    solid(c, p, rrect(12, 56, 52, 67, 5), flat, shade: mixc(d, p.line, 0.45), lw: 2.2, seed: 82, amp: 0.15, d: 3)
    for x in [20.0, 29.0, 38.0] as [CGFloat] {
        solid(c, p, poly([pt(x - 3, 56), pt(x, 51), pt(x + 3, 56)]), p.bone, lw: 1.6, seed: 90 + Int(x), amp: 0.05)
    }
    solid(c, p, rrect(46, 51, 68, 63, 5), flat, shade: mixc(d, p.line, 0.45), lw: 2.2, seed: 83, amp: 0.15, d: 3)
    inkLine(c, [pt(55, 54), pt(60, 59)], 2.0, p.bone, 84, amp: 0); inkLine(c, [pt(60, 54), pt(55, 59)], 2.0, p.bone, 85, amp: 0)
    solid(c, p, rectP(15, 66, 5, 3), flat, lw: 1.4, seed: 86, amp: 0.1); solid(c, p, rectP(44, 66, 5, 3), flat, lw: 1.4, seed: 86, amp: 0.1)
    // impact stars
    for (x0, y0, x1, y1) in [(5.0, 45.0, 11.0, 50.0), (62.0, 44.0, 67.0, 40.0), (3.0, 53.0, 9.0, 54.0), (56.0, 47.0, 62.0, 48.0)] as [(CGFloat, CGFloat, CGFloat, CGFloat)] {
        inkLine(c, [pt(x0, y0), pt(x1, y1)], 2.2, p.bone, 87, amp: 0)
    }
    // big stomping leg coming down from the head onto the dino, claws out over its back
    kArm(c, p, mk { $0.move(to: pt(50, 20)); $0.addQuadCurve(to: pt(38, 44), control: pt(54, 38)) }, w: 15, lw: 2.2, seed: 88)
    kPaw(c, p, 38, 46, 0.66, .pi / 2, lw: 2.8, seed: 89)
    icHead(c, p, 23, 24, 0.56)
    c.restoreGState(); icRim(c, p)
    return c
}

// MARK: 2 matrix rain: green-lit face, sunglasses, falling code
func v3IconMatrix(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); icBack(c, p, tint: icGreen); icClip(c)
    for i in 0..<9 {   // code columns
        let x = 4 + CGFloat(i) * 7.6
        let top = CGFloat((i * 37) % 23) - 6
        var y = top
        var k = 0
        while y < 70 {
            let b = max(0, 1 - CGFloat(k) * 0.16)
            fill(c, rectP(x, y, 3.2, 4.4), alpha(k == 0 ? p.bone : icGreen, 0.35 + 0.55 * b))
            y += 7; k += 1
            if k > 6 { break }
        }
    }
    let gp = tinted(p, icGreen, 0.42)
    icHead(c, gp, 35, 40, 0.66, .iconLook(0), seed: 6)
    // sunglasses (head-local)
    inHead(c, 35, 40, 0.66) {
        let lensL = poly([pt(-46, -17), pt(-3, -14), pt(-8, 11), pt(-40, 12)])
        let lensR = poly([pt(3, -14), pt(46, -17), pt(40, 12), pt(8, 11)])
        for (i, l) in [lensL, lensR].enumerated() {
            let w = wob(l, 0.4, 120 + i)
            fill(c, w, p.line)
            c.saveGState(); c.addPath(w); c.clip()
            inkLine(c, [pt(i == 0 ? -34 : 12, -14), pt(i == 0 ? -22 : 24, 12)], 3.2, alpha(icGreen, 0.85), 121 + i, amp: 0)
            inkLine(c, [pt(i == 0 ? -42 : 30, -12), pt(i == 0 ? -37 : 35, -1)], 2.4, alpha(icGreen, 0.6), 123 + i, amp: 0)
            c.restoreGState()
            outline(c, w, 3.6, p.line, 125 + i)
        }
        inkLine(c, [pt(-4, -10), pt(4, -10)], 4.2, p.line, 127, amp: 0)
        inkLine(c, [pt(-46, -15), pt(-56, -22)], 3.6, p.line, 128, amp: 0.1)
        inkLine(c, [pt(46, -15), pt(56, -22)], 3.6, p.line, 129, amp: 0.1)
    }
    c.restoreGState(); icRim(c, p)
    return c
}

// MARK: 3 vu meter: headphones, yelling
func v3IconVU(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); icBack(c, p); icClip(c)
    icHead(c, p, 35, 43, 0.58, .iconHiss, seed: 7)
    inHead(c, 35, 43, 0.58) {
        let band = mk { $0.addArc(center: pt(0, -4), radius: 55, startAngle: .pi * 1.03, endAngle: .pi * 1.97, clockwise: false) }
        thickLine(c, band, 17, 10, p.line, p.accent)
        thickLine(c, mk { $0.addArc(center: pt(0, -4), radius: 55, startAngle: .pi * 1.1, endAngle: .pi * 1.6, clockwise: false) }, 3, 3, alpha(p.bone, 0.0), alpha(mixc(p.accent, p.bone, 0.5), 0.8))
        for sx in [-1.0, 1.0] as [CGFloat] {
            let cup = rrect(sx > 0 ? 49 : -69, -26, sx > 0 ? 69 : -49, 24, 10)
            amberSolid(c, p, cup, lw: 4.0, seed: 130, d: 6)
            solid(c, p, rrect(sx > 0 ? 45 : -53, -17, sx > 0 ? 53 : -45, 15, 4), p.bone, shade: p.boneShade, lw: 3.4, seed: 131, amp: 0.1, d: 3)
        }
    }
    c.restoreGState(); icRim(c, p)
    return c
}

// MARK: 4 retro tv: cat head poking out of a tube tv
func v3IconTV(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); icBack(c, p); icClip(c)
    // antenna
    inkLine(c, [pt(35, 18), pt(15, 3)], 2.4, p.bone, 140, amp: 0); inkLine(c, [pt(35, 18), pt(56, 5)], 2.4, p.bone, 141, amp: 0)
    fill(c, ell(15, 3, 2.2, 2.2), p.accent); fill(c, ell(56, 5, 2.2, 2.2), p.accent)
    // tv body
    let body = rrect(2, 14, 68, 68, 9)
    solid(c, p, body, mixc(p.lid, p.bone, 0.22), shade: mixc(p.lid, p.line, 0.5), rim: mixc(p.lid, p.bone, 0.45), lw: 2.6, seed: 142, amp: 0.2, d: 4)
    // screen
    let scr = wob(rrect(6, 18, 49, 61, 12), 0.3, 143)
    fill(c, scr, mixc(p.line, p.accent, 0.42))
    c.saveGState(); c.addPath(scr); c.clip()
    for i in 0..<9 { fill(c, rectP(6, 19 + CGFloat(i) * 5, 44, 1.2), alpha(p.line, 0.35)) }   // scanlines
    icHead(c, p, 28, 44, 0.36, .icon, seed: 8)
    c.restoreGState()
    outline(c, scr, 3.2, p.line, 144)
    // paws over the bottom edge of the screen
    for x in [12.0, 44.0] as [CGFloat] {
        solid(c, p, rrect(x - 5, 54, x + 5, 63, 4), p.fur, shade: p.furShade, lw: 2.0, seed: 145 + Int(x), amp: 0.1, d: 2)
    }
    // knobs + vents
    for (i, y) in [24.0, 36.0].enumerated() {
        let kp = ell(59, CGFloat(y), 5, 5)
        solid(c, p, kp, i == 0 ? p.accent : p.bone, lw: 1.9, seed: 150 + i, amp: 0.05)
        inkLine(c, [pt(59, CGFloat(y)), pt(61.5, CGFloat(y) - 2.5)], 1.4, p.line, 152, amp: 0)
    }
    for i in 0..<3 { inkLine(c, [pt(55, 47 + CGFloat(i) * 4.5), pt(64, 47 + CGFloat(i) * 4.5)], 1.8, p.line, 153 + i, amp: 0) }
    c.restoreGState(); icRim(c, p)
    return c
}

// MARK: 5 pc monitor: cat sitting on top of the monitor, tail over the screen
func v3IconPC(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); icBack(c, p); icClip(c)
    // monitor
    let bez = rrect(3, 32, 67, 62, 4)
    solid(c, p, bez, mixc(p.lid, p.bone, 0.28), shade: mixc(p.lid, p.line, 0.5), rim: mixc(p.lid, p.bone, 0.5), lw: 2.4, seed: 160, amp: 0.15, d: 3)
    let scr = rectP(7, 36, 56, 18)
    fill(c, scr, mixc(p.line, p.accent, 0.14))
    for (i, w) in [26.0, 38.0, 18.0, 30.0].enumerated() {   // code lines on screen
        fill(c, rectP(10, 39 + CGFloat(i) * 4, CGFloat(w), 2), i == 1 ? p.accent : alpha(p.bone, 0.7))
    }
    outline(c, wob(scr, 0.1, 161), 2.0, p.line, 161)
    fill(c, rectP(57, 57, 4, 2.5), p.accent)
    solid(c, p, rectP(29, 62, 12, 4), mixc(p.lid, p.bone, 0.2), lw: 2.0, seed: 162, amp: 0.1)
    solid(c, p, rrect(19, 66, 51, 70, 2), mixc(p.lid, p.bone, 0.2), lw: 2.0, seed: 163, amp: 0.1)
    // cat sitting on top: body mass, head, hanging tail
    var body: [CGPoint] = []
    for i in 0..<18 { let a = CGFloat(i) / 18 * 2 * .pi; body.append(pt(28 + cos(a) * 18, 30 + sin(a) * 14 * (sin(a) > 0.4 ? 0.7 : 1))) }
    solid(c, p, poly(chaikin(body)), p.fur, shade: p.furShade, rim: p.furRim, lw: 2.4, seed: 164, amp: 0.4, d: 4)
    let tail = mk { $0.move(to: pt(44, 30)); $0.addCurve(to: pt(58, 56), control1: pt(70, 26), control2: pt(52, 40)) }
    kArm(c, p, tail, w: 6.5, lw: 2.2, seed: 165)
    let tip = mk { $0.move(to: pt(57.5, 52)); $0.addLine(to: pt(58, 56)) }
    thickLine(c, tip, 9, 5, p.line, p.accent)
    icHead(c, p, 26, 15, 0.44, .icon, seed: 9)
    solid(c, p, rrect(14, 34, 30, 40, 3), p.fur, shade: p.furShade, lw: 2.0, seed: 166, amp: 0.1, d: 2)   // front paw on bezel
    c.restoreGState(); icRim(c, p)
    return c
}

// MARK: 6 air mouse: cat dangling a mouse by its cable
func v3IconAirMouse(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); icBack(c, p); icClip(c)
    // swing arcs
    for (i, r) in [10.0, 15.0].enumerated() {
        let a = mk { $0.addArc(center: pt(34, 41), radius: CGFloat(r) + 24, startAngle: 0.25 + CGFloat(i) * 0.1, endAngle: 0.75, clockwise: false) }
        outline(c, a, 1.6, alpha(p.bone, 0.4), 170 + i)
    }
    icHead(c, p, 30, 24, 0.56, .icon, seed: 10)
    // cable from the mouth to the mouse
    let cable = mk { $0.move(to: pt(33, 32)); $0.addCurve(to: pt(50, 47), control1: pt(46, 30), control2: pt(52, 36)) }
    thickLine(c, cable, 6.0, 3.0, p.line, p.bone)
    // mouse hanging, tilted
    c.saveGState(); c.translateBy(x: 51, y: 56); c.rotate(by: -0.28)
    let body = poly(chaikin([pt(-10, -5), pt(-6, -13), pt(6, -13), pt(10, -5), pt(11, 8), pt(6, 15), pt(-6, 15), pt(-11, 8)]))
    solid(c, p, body, p.bone, shade: p.boneShade, rim: mixc(p.bone, p.bone, 0), lw: 2.4, seed: 171, amp: 0.15, d: 3)
    inkLine(c, [pt(0, -13), pt(0, -2)], 2.0, p.line, 172, amp: 0)
    inkLine(c, [pt(-10, -3), pt(10, -3)], 2.0, p.line, 173, amp: 0)
    solid(c, p, rrect(-2.2, -11, 2.2, -5, 2), p.accent, lw: 1.4, seed: 174, amp: 0.05, d: 1)
    c.restoreGState()
    c.restoreGState(); icRim(c, p)
    return c
}

// MARK: 7 ble spam: megaphone
func v3IconBLE(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); icBack(c, p); icClip(c)
    icHead(c, p, 25, 41, 0.62, .iconHiss, seed: 11)
    c.saveGState(); c.translateBy(x: 33, y: 41); c.rotate(by: -0.5)
    solid(c, p, rrect(-4, 3, 8, 16, 3), mixc(p.lid, p.bone, 0.2), lw: 2.0, seed: 180, amp: 0.1, d: 2)   // handle
    amberSolid(c, p, poly([pt(0, -6), pt(30, -17), pt(30, 17), pt(0, 6)]), lw: 2.6, seed: 181, d: 5)
    solid(c, p, rrect(26, -20, 34, 20, 4), p.bone, shade: p.boneShade, lw: 2.4, seed: 182, amp: 0.1, d: 3)
    c.restoreGState()
    // sound arcs
    for (i, r) in [9.0, 15.0, 21.0].enumerated() {
        let a = mk { $0.addArc(center: pt(52, 18), radius: CGFloat(r), startAngle: -1.2, endAngle: 0.55, clockwise: false) }
        outline(c, a, 2.6, i == 2 ? alpha(p.accent, 0.8) : p.accent, 183 + i)
    }
    c.restoreGState(); icRim(c, p)
    return c
}

// MARK: 8 badusb: chewing a usb stick
func v3IconBadUSB(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); icBack(c, p); icClip(c)
    icHead(c, p, 35, 31, 0.64, .icon, seed: 12)
    // fangs clamp the stick
    let stick = 1
    _ = stick
    // plug (metal) in the mouth + body + frayed cord
    solid(c, p, rrect(8, 43, 36, 55, 2), mixc(p.belly, p.bone, 0.5), shade: p.bellyShade, lw: 2.4, seed: 190, amp: 0.1, d: 3)
    fill(c, rectP(13, 47, 4.5, 4.5), p.line); fill(c, rectP(22, 47, 4.5, 4.5), p.line)
    amberSolid(c, p, rrect(34, 41, 62, 57, 6), lw: 2.6, seed: 191, d: 4)
    fill(c, ell(55, 49, 2.4, 2.4), p.line)
    let cord = mk { $0.move(to: pt(60, 50)); $0.addQuadCurve(to: pt(63, 66), control: pt(70, 55)) }
    thickLine(c, cord, 5.6, 2.8, p.line, p.bone)
    for (i, col) in [p.danger, p.info, p.bone].enumerated() {
        inkLine(c, [pt(63, 66), pt(59 + CGFloat(i) * 4, 70)], 1.8, col, 192 + i, amp: 0.1)
    }
    // upper fangs over the plug
    for x in [14.0, 30.0] as [CGFloat] {
        solid(c, p, poly([pt(x - 3.5, 40), pt(x + 3.5, 40), pt(x, 47)]), p.bone, lw: 1.6, seed: 195 + Int(x), amp: 0.05)
    }
    c.restoreGState(); icRim(c, p)
    return c
}

// MARK: 9 infrared: big bone remote, red signal waves
func v3IconIR(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); icBack(c, p); icClip(c)
    icHead(c, p, 25, 38, 0.58, .icon, seed: 13)
    // chunky remote, held upright at the right; red IR waves leave its top
    c.saveGState(); c.translateBy(x: 54, y: 46); c.rotate(by: 0.12)
    solid(c, p, rrect(-11, -17, 11, 27, 5), p.bone, shade: p.boneShade, lw: 2.6, seed: 205, amp: 0.1, d: 3)
    solid(c, p, rrect(-3.4, -22, 3.4, -15, 2), p.danger, lw: 1.8, seed: 204, amp: 0.05, d: 1)
    fill(c, ell(0, -9, 3.6, 3.6), p.danger)
    for (i, bx) in [-5.0, 5.0].enumerated() { for j in 0..<3 { fill(c, ell(CGFloat(bx), -1 + CGFloat(j) * 7.5, 2.9, 2.9), (i == 0 && j == 0) ? p.accent : p.line) } }
    c.restoreGState()
    let tip = pt(56, 20)
    for (i, r) in [6.0, 11.0, 16.0].enumerated() {
        let a = mk { $0.addArc(center: tip, radius: CGFloat(r), startAngle: -2.1, endAngle: -0.4, clockwise: false) }
        outline(c, a, 3.0, i == 2 ? alpha(p.danger, 0.8) : p.danger, 200 + i)
    }
    kPaw(c, p, 46, 63, 0.42, -0.9, lw: 2.8, seed: 206)
    c.restoreGState(); icRim(c, p)
    return c
}

// MARK: 10 mediaremote: paw slamming a giant pause button
func v3IconMedia(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); icBack(c, p); icClip(c)
    // impact lines
    for (x0, y0, x1, y1) in [(5.0, 24.0, 12.0, 30.0), (3.0, 40.0, 10.0, 42.0), (65.0, 24.0, 58.0, 30.0), (67.0, 40.0, 60.0, 42.0)] as [(CGFloat, CGFloat, CGFloat, CGFloat)] {
        inkLine(c, [pt(x0, y0), pt(x1, y1)], 2.6, p.bone, 210, amp: 0)
    }
    // giant button
    solid(c, p, ell(35, 45, 27, 22), mixc(p.accent, p.line, 0.5), lw: 2.6, seed: 211, amp: 0.2)   // button base
    solid(c, p, ell(35, 41, 25, 20), p.accent, shade: mixc(p.accent, p.line, 0.38), rim: mixc(p.accent, p.bone, 0.4), lw: 2.8, seed: 212, amp: 0.2, d: 4)
    fill(c, rrect(22, 31, 31, 52, 2), p.line); fill(c, rrect(39, 31, 48, 52, 2), p.line)
    // head peeking over the top-left
    icHead(c, p, 17, 13, 0.4, .icon, seed: 14)
    // slamming paw from above
    kArm(c, p, mk { $0.move(to: pt(60, -6)); $0.addQuadCurve(to: pt(50, 26), control: pt(62, 16)) }, w: 17, lw: 2.4, seed: 213)
    c.saveGState(); c.translateBy(x: 49, y: 31); c.rotate(by: .pi / 2 + 0.1); c.scaleBy(x: 1.0, y: 1.0)
    for dy in [-8.0, 0.0, 8.0] as [CGFloat] {
        solid(c, p, poly([pt(15, dy - 3), pt(31, dy + 0.5), pt(15, dy + 3.4)]), p.bone, lw: 1.5, seed: 214 + Int(dy), amp: 0.05)
    }
    solid(c, p, rrect(-14, -14, 14, 14, 10), p.fur, shade: p.furShade, rim: p.furRim, lw: 2.6, seed: 215, amp: 0.2, d: 4)
    for dy in [-8.0, 0.0, 8.0] as [CGFloat] { solid(c, p, ell(14, dy, 5.6, 5.2), p.fur, shade: p.furShade, lw: 2.0, seed: 216 + Int(dy), amp: 0.1, d: 2) }
    c.restoreGState()
    c.restoreGState(); icRim(c, p)
    return c
}

// MARK: 11 slide clicker: orange laser pen, one big red dot
func v3IconSlide(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); icBack(c, p); icClip(c)
    icHead(c, p, 27, 42, 0.64, .iconLook(8), seed: 15)
    let dot = pt(57, 15)
    fill(c, ell(dot.x, dot.y, 15, 15), alpha(p.danger, 0.28))
    fill(c, ell(dot.x, dot.y, 10, 10), alpha(p.danger, 0.6))
    fill(c, ell(dot.x, dot.y, 6, 6), p.danger)
    fill(c, ell(dot.x, dot.y, 2.4, 2.4), p.bone)
    for t in [0.3, 0.5, 0.7] as [CGFloat] { let q = lerp(pt(54, 40), dot, t); fill(c, ell(q.x, q.y, 1.8, 1.8), p.danger) }
    c.saveGState(); c.translateBy(x: 54, y: 56); c.rotate(by: 0.12)
    solid(c, p, rrect(-5.5, -14, 5.5, 16, 3.5), p.accent, shade: mixc(p.accent, p.line, 0.4), rim: mixc(p.accent, p.bone, 0.3), lw: 2.4, seed: 220, amp: 0.1, d: 2)
    solid(c, p, rrect(-3.6, -19, 3.6, -13, 2), p.bone, lw: 1.8, seed: 221, amp: 0.05, d: 1)
    fill(c, rrect(-2.6, -6, 2.6, 0, 2), p.bone)
    c.restoreGState()
    kPaw(c, p, 52, 64, 0.42, -1.45, lw: 2.8, seed: 222)
    c.restoreGState(); icRim(c, p)
    return c
}

// MARK: 12 radar scan: periscope + sweep
func v3IconRadar(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); icBack(c, p); icClip(c)
    // radar rings + sweep wedge, top-right
    let o = pt(66, 66)
    for r in [22.0, 36.0, 50.0] as [CGFloat] { outline(c, mk { $0.addArc(center: o, radius: r, startAngle: .pi, endAngle: .pi * 1.5, clockwise: false) }, 1.6, alpha(p.accent, 0.62), 230 + Int(r)) }
    fill(c, mk { $0.move(to: o); $0.addArc(center: o, radius: 56, startAngle: -2.55, endAngle: -2.1, clockwise: false); $0.closeSubpath() }, alpha(p.accent, 0.5))
    inkLine(c, [o, pt(66 - 56 * cos(2.1), 66 - 56 * sin(2.1))], 2.2, p.accent, 234, amp: 0)
    // cat low in the corner, one big eye at the periscope eyepiece
    icHead(c, p, 25, 50, 0.64, .icon, seed: 16)
    // periscope: tall tube with elbow
    let pipe = mk { $0.move(to: pt(60, 70)); $0.addLine(to: pt(60, 17)); $0.addLine(to: pt(42, 17)) }
    thickLine(c, pipe, 12.6, 8.6, p.line, mixc(p.lid, p.bone, 0.32))
    thickLine(c, mk { $0.move(to: pt(62, 66)); $0.addLine(to: pt(62, 19)) }, 2.2, 2.2, alpha(p.bone, 0), alpha(p.bone, 0.5))
    solid(c, p, rrect(36, 10, 46, 24, 3), p.accent, shade: mixc(p.accent, p.line, 0.4), lw: 2.4, seed: 235, amp: 0.05, d: 2)
    fill(c, ell(41, 17, 2.4, 3.8), alpha(p.bone, 0.9))
    solid(c, p, rrect(56, 36, 64, 41, 1.5), p.accent, lw: 1.8, seed: 236, amp: 0.05, d: 1)
    fill(c, ell(63, 58, 2.4, 2.4), p.bone)   // blip
    c.restoreGState(); icRim(c, p)
    return c
}

// MARK: 13 voice recorder: mic, hissing
func v3IconVoice(_ p: Pal) -> CGContext {
    let c = makeCtx(70, 70); icBack(c, p); icClip(c)
    icHead(c, p, 28, 32, 0.64, .iconHiss, seed: 17)
    // red rec dot
    fill(c, ell(60, 9, 6, 6), alpha(p.danger, 0.35)); solid(c, p, ell(60, 9, 4.4, 4.4), p.danger, lw: 1.5, seed: 240, amp: 0.05)
    // mic on a handle
    thickLine(c, mk { $0.move(to: pt(66, 71)); $0.addLine(to: pt(54, 56)) }, 11, 7, p.line, mixc(p.lid, p.bone, 0.2))
    let ball = ell(48, 50, 13, 13)
    solid(c, p, ball, mixc(p.belly, p.bone, 0.45), shade: p.bellyShade, lw: 2.6, seed: 241, amp: 0.1, d: 4)
    c.saveGState(); c.addPath(ball); c.clip()
    for i in -2...2 { inkLine(c, [pt(48 + CGFloat(i) * 5, 36), pt(48 + CGFloat(i) * 5, 64)], 1.3, alpha(p.line, 0.55), 242, amp: 0) }
    for i in -1...2 { inkLine(c, [pt(34, 48 + CGFloat(i) * 5), pt(62, 48 + CGFloat(i) * 5)], 1.3, alpha(p.line, 0.55), 243, amp: 0) }
    c.restoreGState()
    outline(c, ell(48, 50, 13, 13), 2.6, p.line, 244)
    solid(c, p, rrect(37, 58, 59, 63, 2.5), p.accent, lw: 2.0, seed: 245, amp: 0.05, d: 1)
    c.restoreGState(); icRim(c, p)
    return c
}
