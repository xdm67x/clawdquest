// Clawd Quest — app icon renderer (macOS).
//
// Renders the 1024pt master art and emits a full .iconset, downsampled from
// the master. The mark is Clawd himself (the Icons8 blocky mascot, drawn from
// his 48-grid) with the quest-complete effect: the apple he eats and a burst of
// sparks. No plate: the icon is transparent so Clawd sits on any background.
// Palette is the one baked into src/ui.c and src/pet.c.
//
//   swift tools/mkicon.swift build/AppIcon.iconset

import AppKit
import CoreGraphics
import Foundation

// Palette (from src/ui.c and src/pet.c).
let clawdOrange = (0.843, 0.467, 0.341) // Clawd   #D77757
let clawdEye = (0.155, 0.070, 0.027) // Clawd's eye holes
let appleRed = (1.0, 0.302, 0.427) // apple body   #ff4d6d
let appleHi = (1.0, 0.502, 0.596) // apple shine  #ff8098
let appleStem = (0.42, 0.247, 0.114) // stem         #6b3f1d
let leafGreen = (0.239, 0.863, 0.592) // leaf + spark #3ddc97
let sparkRed = (1.0, 0.302, 0.427) // quest burst  #ff4d6d
let sparkAmber = (1.0, 0.741, 0.180) // quest burst  #ffbd2e

let space = CGColorSpace(name: CGColorSpace.sRGB)!

func color(_ c: (Double, Double, Double), _ a: Double = 1.0) -> CGColor {
    CGColor(colorSpace: space, components: [c.0, c.1, c.2, a])!
}

// A four-point "spark" star, centered at (cx, cy).
func sparkle(_ cx: Double, _ cy: Double, _ r: Double) -> CGPath {
    let p = CGMutablePath()
    let inR = r * 0.20
    p.move(to: CGPoint(x: cx, y: cy + r))
    p.addLine(to: CGPoint(x: cx + inR, y: cy + inR))
    p.addLine(to: CGPoint(x: cx + r, y: cy))
    p.addLine(to: CGPoint(x: cx + inR, y: cy - inR))
    p.addLine(to: CGPoint(x: cx, y: cy - r))
    p.addLine(to: CGPoint(x: cx - inR, y: cy - inR))
    p.addLine(to: CGPoint(x: cx - r, y: cy))
    p.addLine(to: CGPoint(x: cx - inR, y: cy + inR))
    p.closeSubpath()
    return p
}

// Clawd, drawn straight from his 48-grid (grid units are y-down with the origin
// at the top-left of the 48x48 box; `tx`/`ty` place grid (0,0) in the y-up host
// space and `s` is the units-per-pixel scale).
func drawClawd(_ ctx: CGContext, tx: Double, ty: Double, s: Double) {
    func r(_ gx: Double, _ gy: Double, _ gw: Double, _ gh: Double) -> CGRect {
        CGRect(x: tx + gx * s, y: ty - (gy + gh) * s, width: gw * s, height: gh * s)
    }

    ctx.setFillColor(color(clawdOrange))
    ctx.fill(r(6, 18, 36, 24)) // body
    ctx.fill(r(9, 39, 3, 9)) // legs
    ctx.fill(r(15, 39, 3, 9))
    ctx.fill(r(30, 39, 3, 9))
    ctx.fill(r(36, 39, 3, 9))
    ctx.fill(r(0, 33, 7.5, 6)) // side claws
    ctx.fill(r(40.5, 33, 7.5, 6))

    ctx.setFillColor(color(clawdEye))
    ctx.fill(r(12, 24, 3, 6)) // eyes
    ctx.fill(r(33, 24, 3, 6))
}

// The apple, in the app's own apple-local units (body radius 1, shifted down
// 0.15). `cx`/`cy` is the apple origin and `R` the body radius in host pixels.
func drawApple(_ ctx: CGContext, cx: Double, cy: Double, R: Double) {
    func p(_ x: Double, _ y: Double) -> CGPoint { CGPoint(x: cx + x * R, y: cy + y * R) }

    // Body: egg fan, slightly wider at the bottom.
    let body = CGMutablePath()
    let n = 48
    for i in 0...n {
        let a = Double(i) / Double(n) * 2 * Double.pi
        let rr = 1.0 - 0.12 * sin(a)
        let pt = p(cos(a) * rr, -0.15 + sin(a) * rr)
        if i == 0 { body.move(to: pt) } else { body.addLine(to: pt) }
    }
    body.closeSubpath()
    ctx.addPath(body)
    ctx.setFillColor(color(appleRed))
    ctx.fillPath()

    // Shine, upper left.
    ctx.addEllipse(in: CGRect(x: cx - 0.66 * R, y: cy + 0.11 * R, width: 0.48 * R, height: 0.48 * R))
    ctx.setFillColor(color(appleHi))
    ctx.fillPath()

    // Stem.
    let stem = CGMutablePath()
    stem.move(to: p(0, 0.78))
    stem.addLine(to: p(-0.09, 1.05))
    stem.addLine(to: p(0.09, 1.05))
    stem.addLine(to: p(0.14, 0.82))
    stem.closeSubpath()
    ctx.addPath(stem)
    ctx.setFillColor(color(appleStem))
    ctx.fillPath()

    // Leaf.
    let leaf = CGMutablePath()
    leaf.move(to: p(0.16, 1.02))
    leaf.addLine(to: p(0.30, 1.22))
    leaf.addLine(to: p(0.62, 1.06))
    leaf.addLine(to: p(0.44, 0.92))
    leaf.closeSubpath()
    ctx.addPath(leaf)
    ctx.setFillColor(color(leafGreen))
    ctx.fillPath()
}

// All drawing happens in 1024-space, y-up.
func drawIcon(_ ctx: CGContext) {
    // --- Clawd ------------------------------------------------------------
    // Soft shadow pooling under him (the only ground in a transparent icon).
    ctx.saveGState()
    ctx.setShadow(offset: CGSize(width: 0, height: -8), blur: 30,
                  color: CGColor(colorSpace: space, components: [0, 0, 0, 0.5])!)
    ctx.addEllipse(in: CGRect(x: 232, y: 300, width: 560, height: 44))
    ctx.setFillColor(CGColor(colorSpace: space, components: [0, 0, 0, 0.34])!)
    ctx.fillPath()
    ctx.restoreGState()

    drawClawd(ctx, tx: 219.2, ty: 905.6, s: 12.2)

    // --- The apple, floating beside him -----------------------------------
    drawApple(ctx, cx: 820, cy: 706, R: 58)

    // --- Sparks (the quest-complete burst) --------------------------------
    ctx.setFillColor(color(sparkAmber))
    ctx.addPath(sparkle(188, 806, 34))
    ctx.fillPath()

    ctx.setFillColor(color((1, 1, 1), 0.92))
    ctx.addPath(sparkle(150, 560, 14))
    ctx.fillPath()

    ctx.setFillColor(color(sparkRed))
    ctx.addPath(sparkle(250, 380, 22))
    ctx.fillPath()

    ctx.setFillColor(color(leafGreen))
    ctx.addPath(sparkle(920, 798, 26))
    ctx.fillPath()

    ctx.setFillColor(color(leafGreen))
    ctx.addPath(sparkle(604, 190, 16))
    ctx.fillPath()
}

func renderMaster() -> CGImage {
    let ctx = CGContext(data: nil, width: 1024, height: 1024, bitsPerComponent: 8, bytesPerRow: 0,
                        space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    drawIcon(ctx)
    return ctx.makeImage()!
}

func downscale(_ image: CGImage, _ size: Int) -> Data {
    let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0,
                        space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.interpolationQuality = .high
    ctx.draw(image, in: CGRect(x: 0, y: 0, width: size, height: size))
    let rep = NSBitmapImageRep(cgImage: ctx.makeImage()!)
    return rep.representation(using: .png, properties: [:])!
}

let outDir = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "build/AppIcon.iconset"
try! FileManager.default.createDirectory(atPath: outDir, withIntermediateDirectories: true)

let master = renderMaster()
let files: [(String, Int)] = [
    ("icon_16x16.png", 16), ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32), ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128), ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256), ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512), ("icon_512x512@2x.png", 1024),
]
for (name, size) in files {
    try! downscale(master, size).write(to: URL(fileURLWithPath: outDir).appendingPathComponent(name))
}
print("wrote \(files.count) pngs to \(outDir)")
