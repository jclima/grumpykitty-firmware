// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
//
// GrumpyKitty boot animation (art v3.1, palette A "alley"). Renders ~23 full 320x240 opaque
// frames with CoreGraphics and writes an animated GIF with ImageIO. Choreography:
//   dark alley -> amber eyes open in the dark -> light comes up on the half-asleep cat ->
//   slow blink -> yawn -> hiss -> "MEH" bubble pops -> "GrumpyKitty" wordmark slams in -> hold.
// Usage: boot_gif <out.gif> <contact_sheet.png>   (tools/art/make_boot_gif.sh)
// Compiled with draw.swift + draw_v3*.swift (cat kit); see make_boot_gif.sh.

import CoreGraphics
import CoreText
import Foundation
import ImageIO

let P3 = Pal.alley

// MARK: flat comic wall (no grit, no dither: tiny LZW payload, no banding after palette quantisation)
func flatWall(_ p: Pal, w: Int, h: Int, light: CGPoint, seed: Int) -> CGContext {
    let c = makeCtx(w, h)
    let base = mixc(p.bg0, p.bg1, 0.55).components!
    let rust = p.danger.components!
    let amber = p.accent.components!
    let buf = c.data!.assumingMemoryBound(to: UInt8.self), bpr = c.bytesPerRow
    let bw = 36, bh = 16
    for y in 0..<h { for x in 0..<w {
        let row = y / bh
        let xx = x + ((row % 2 == 0) ? 0 : bw / 2) + (row * 5) % 7
        let lx = xx % bw, ly = y % bh
        let id = (xx / bw) * 131 + row * 17 + seed
        var v: CGFloat = CGFloat(Int(hn(id, 1) * 3)) * 0.009          // three flat brick tones
        let rustMix: CGFloat = hn(id, 2) > 0.6 ? 0.07 : 0
        if lx < 2 || ly < 2 { v -= 0.045 }                            // mortar
        else if ly == 2 { v += 0.03 } else if lx == 2 { v += 0.015 }  // lit top / left edge
        else if ly == bh - 1 { v -= 0.016 }
        let d = hypot(CGFloat(x) - light.x, CGFloat(y) - light.y)
        let l = floor(pow(max(0, 1 - d / 250), 2.0) * 4) / 4          // posterised rim light
        let nx = (CGFloat(x) / CGFloat(w) - 0.5) * 2, ny = (CGFloat(y) / CGFloat(h) - 0.5) * 2
        let vg = 1 - floor(0.34 * (nx * nx + ny * ny) / 2 * 12) / 12  // posterised vignette
        for ch in 0..<3 {
            var s = base[ch] + v * (1 + 2.2 * l) + rust[ch] * rustMix * 0.25
            s *= vg
            s += amber[ch] * l * 0.17
            let o = y * bpr + x * 4 + ch
            buf[o] = UInt8(max(0, min(1, s)) * 255 + 0.5)
        }
        buf[y * bpr + x * 4 + 3] = 255
    } }
    for i in 0..<6 {   // a few grime drips
        let sx = CGFloat((i * 53 + seed * 7) % w), sy = CGFloat((i * 31) % 60)
        inkLine(c, [pt(sx, sy), pt(sx + hn(i, 4) * 3, sy + 40 + CGFloat((i * 17) % 70))], 3 + hn(i, 5), alpha(p.line, 0.13), 300 + i, amp: 1.0)
    }
    return c
}

// MARK: frame spec
struct Beat {
    var cs: Int                       // GIF delay in 1/100 s
    var dark: CGFloat = 0             // black overlay alpha (eyes punch through)
    var mood: Mood = .glare
    var dy: CGFloat = 0, dx: CGFloat = 0, sy: CGFloat = 1   // head offset / vertical stretch (yawn)
    var cat = true                    // draw the cat at all
    var bubble: CGFloat = 0           // 0 = none, else pop scale
    var word: CGFloat = 0             // 0 = none, else slam scale
}

func lidMood(_ l: CGFloat, mouth: Int = 3) -> Mood {
    Mood(lidL: l, lidR: min(0.98, l + 0.06), tilt: 8, browL: 0, browR: 0, kL: 1.15, kR: 0.9, mouth: mouth)
}
let asleep = lidMood(0.55)
let annoyed = Mood(lidL: 0.4, lidR: 0.45, tilt: 9, browL: 1, browR: 1, kL: 1.15, kR: 0.9, mouth: 0)
let yawn = Mood(lidL: 0.84, lidR: 0.86, tilt: 10, browL: 2, browR: 2, kL: 1.15, kR: 0.9, mouth: 2)

let beats: [Beat] = [
    Beat(cs: 18, dark: 0.97, mood: lidMood(0.98), cat: false),                // 0 black alley, nothing yet
    Beat(cs: 7, dark: 0.975, mood: lidMood(0.93)),                   // 1 thin amber slits
    Beat(cs: 7, dark: 0.975, mood: lidMood(0.75)),                   // 2
    Beat(cs: 7, dark: 0.975, mood: lidMood(0.52)),                   // 3
    Beat(cs: 14, dark: 0.97, mood: annoyed),                        // 4 eyes fully open, glaring in the dark
    Beat(cs: 7, dark: 0.78, mood: annoyed),                         // 5 light comes up
    Beat(cs: 7, dark: 0.55, mood: asleep),                          // 6
    Beat(cs: 7, dark: 0.30, mood: asleep),                          // 7
    Beat(cs: 7, dark: 0.12, mood: asleep),                          // 8
    Beat(cs: 30, mood: asleep),                                     // 9 half-asleep hold
    Beat(cs: 7, mood: lidMood(0.97)),                               // 10 slow blink
    Beat(cs: 24, mood: annoyed),                                    // 11 annoyed at being woken
    Beat(cs: 7, mood: yawn, dy: -2, sy: 1.02),                      // 12 yawn starts
    Beat(cs: 10, mood: yawn, dy: -5, sy: 1.06),                     // 13 yawn peak
    Beat(cs: 10, mood: yawn, dy: -6, sy: 1.07),                     // 14
    Beat(cs: 7, mood: .hiss, dx: -3),                               // 15 hiss, shaking
    Beat(cs: 7, mood: .hiss, dx: 3),                                // 16
    Beat(cs: 7, mood: .glare, bubble: 0.5),                         // 17 bubble pops
    Beat(cs: 7, mood: .glare, bubble: 1.15),                        // 18
    Beat(cs: 10, mood: .glare, bubble: 1.0),                        // 19
    Beat(cs: 7, mood: .glare, bubble: 1.0, word: 1.16),              // 20 wordmark slams in
    Beat(cs: 7, mood: .glare, bubble: 1.0, word: 1.06),             // 21
    Beat(cs: 95, mood: .glare, bubble: 1.0, word: 1.0),             // 22 final hold (home-screen look)
]

// MARK: scene
let CX: CGFloat = 124, CY: CGFloat = 112, HS: CGFloat = 1.2
let wall = flatWall(P3, w: 320, h: 240, light: pt(322, -4), seed: 1)

func drawWordmark(_ c: CGContext, _ k: CGFloat) {
    let a = glyphs("Grumpy", size: 50), b = glyphs("Kitty", size: 50)
    let wa = a.boundingBox.width, wb = b.boundingBox.width, gap: CGFloat = 3
    let total = wa + gap + wb
    let fit = min(1, 292 / total)
    let cx: CGFloat = 160, cy: CGFloat = 216
    c.saveGState()
    c.translateBy(x: cx, y: cy); c.rotate(by: -0.025); c.scaleBy(x: k * fit, y: k * fit)
    let skew = CGAffineTransform(a: 1, b: 0, c: -0.1, d: 1, tx: 0, ty: 0)
    let ox = -total / 2
    for (path, x0, f) in [(a, ox + wa / 2, P3.bone), (b, ox + wa + gap + wb / 2, P3.eye)] as [(CGPath, CGFloat, CGColor)] {
        var t = skew.concatenating(CGAffineTransform(translationX: x0, y: 0))
        let g = path.copy(using: &t)!
        var ts = CGAffineTransform(translationX: 2.5, y: 3)
        let sh = g.copy(using: &ts)!
        c.addPath(sh); c.setStrokeColor(P3.line); c.setLineWidth(6); c.setLineJoin(.round); c.strokePath()
        fill(c, sh, P3.line)
        c.addPath(g); c.setStrokeColor(P3.line); c.setLineWidth(6); c.setLineJoin(.round); c.strokePath()
        fill(c, g, f)
    }
    c.restoreGState()
}

func renderBeat(_ b: Beat) -> CGContext {
    let p = P3
    let c = makeCtx(320, 240)
    put(c, wall, 0, 0)
    // body behind the ledge, then ledge, then head
    if b.cat { kShoulders(c, p, CX, CY + 80, 92, 40, lw: 3.4, seed: 100) }
    solid(c, p, rrect(-8, 192, 330, 250, 4), p.table, shade: mixc(p.table, p.line, 0.45), rim: mixc(p.table, p.bone, 0.12), lw: 3.0, seed: 11, amp: 0.3, d: 4)
    c.saveGState()
    c.translateBy(x: b.dx, y: b.dy)
    c.translateBy(x: CX, y: CY); c.scaleBy(x: 1, y: b.sy); c.translateBy(x: -CX, y: -CY)
    if b.cat { kHead(c, p, CX, CY, HS, b.mood, lw: 3.2, seed: 2) }
    c.restoreGState()
    if b.bubble > 0 {
        c.saveGState()
        let o = pt(246, 37)
        c.translateBy(x: o.x, y: o.y); c.scaleBy(x: b.bubble, y: b.bubble); c.translateBy(x: -o.x, y: -o.y)
        kBurst(c, p, CGRect(x: 200, y: 16, width: 92, height: 42), tail: pt(186, 76), baseX: 208, f: p.bone, lw: 2.8, seed: 22)
        shout(c, "MEH", o.x, o.y, 32, -0.06, p.line, p.bone, 0.3, skew: 0.12)
        c.restoreGState()
    }
    if b.word > 0 { drawWordmark(c, b.word) }
    guard b.dark > 0 else { return c }
    // darkness overlay; the eyes keep glowing through it
    let lit = c.makeImage()!
    let out = makeCtx(320, 240)
    drawImage(out, lit, CGRect(x: 0, y: 0, width: 320, height: 240))
    let eyes: [CGPoint] = [pt(CX + b.dx - 21 * HS, CY + b.dy - 3 * HS), pt(CX + b.dx + 21 * HS, CY + b.dy - 3 * HS)]
    out.setFillColor(col(0x040508, b.dark)); out.fill(CGRect(x: 0, y: 0, width: 320, height: 240))
    if b.cat && b.mood.lidL < 0.97 {
        for (i, e) in eyes.enumerated() {
            out.saveGState()
            let k = i == 0 ? b.mood.kL : b.mood.kR
            out.addPath(ell(e.x, e.y, (14 * k + 0.5) * HS, (10.5 * k + 0.5) * HS)); out.clip()
            drawImage(out, lit, CGRect(x: 0, y: 0, width: 320, height: 240))
            out.restoreGState()
        }
    }
    return out
}

// MARK: GIF out
func writeGIF(_ path: String, _ frames: [CGContext]) {
    let url = URL(fileURLWithPath: path) as CFURL
    let d = CGImageDestinationCreateWithURL(url, "com.compuserve.gif" as CFString, frames.count, nil)!
    // no file-level loop extension: the original plays once and the player ignores it anyway
    for (i, f) in frames.enumerated() {
        let props: [String: Any] = [kCGImagePropertyGIFDictionary as String: [
            kCGImagePropertyGIFDelayTime as String: Double(beats[i].cs) / 100.0,
            kCGImagePropertyGIFUnclampedDelayTime as String: Double(beats[i].cs) / 100.0]]
        CGImageDestinationAddImage(d, f.makeImage()!, props as CFDictionary)
    }
    precondition(CGImageDestinationFinalize(d), "GIF finalize failed")
}

/// Contact sheet read back from the written GIF (so it shows what the decoder sees).
func contactSheet(gif: String, out: String, count: Int = 12) {
    let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: gif) as CFURL, nil)!
    let n = CGImageSourceGetCount(src)
    let cols = 4, rows = (count + cols - 1) / cols, tw = 320, th = 240, pad = 8, lab = 16
    let W = pad + cols * (tw + pad), H = pad + rows * (th + lab + pad)
    let c = makeCtx(W, H)
    fill(c, rectP(0, 0, CGFloat(W), CGFloat(H)), col(0x0b0b0e))
    for k in 0..<count {
        let i = count == 1 ? 0 : Int((Double(k) * Double(n - 1) / Double(count - 1)).rounded())
        let im = CGImageSourceCreateImageAtIndex(src, i, nil)!
        let x = CGFloat(pad + (k % cols) * (tw + pad)), y = CGFloat(pad + (k / cols) * (th + lab + pad))
        drawImage(c, im, CGRect(x: x, y: y, width: CGFloat(tw), height: CGFloat(th)))
        label(c, "frame \(i)  \(beats[min(i, beats.count - 1)].cs * 10) ms", x, y + CGFloat(th) + 12, 11, col(0xdddddd))
    }
    save(c, out)
}

@main
struct BootGifMain {
    static func main() {
        let args = CommandLine.arguments
        guard args.count >= 3 else { print("usage: boot_gif <out.gif> <contact_sheet.png>"); exit(1) }
        let frames = beats.map { renderBeat($0) }
        writeGIF(args[1], frames)
        let total = beats.reduce(0) { $0 + $1.cs * 10 }
        print("wrote \(args[1]): \(frames.count) frames, \(total) ms")
        contactSheet(gif: args[1], out: args[2])
    }
}
