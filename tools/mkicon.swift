// Quest Log — app icon renderer (macOS).
//
// Renders the 1024pt master art and emits a full .iconset, downsampled from
// the master. The composition mirrors the in-game hero panel: violet glass
// background, gradient level badge with a sword, a quest row, and the gold
// XP bar. Palette is the one baked into src/ui.c.
//
//   swift tools/mkicon.swift build/AppIcon.iconset

import AppKit
import CoreGraphics
import Foundation

// Palette (from src/ui.c).
let accPurple = (0.545, 0.361, 0.965) // C_ACC  #8B5CF6
let accPink = (0.925, 0.282, 0.600) // C_PINK #EC489A
let xpGold = (1.0, 0.824, 0.247) // C_XP   #FFD23F
let xpAmber = (1.0, 0.624, 0.110) // C_XP2  #FF9F1C
let okGreen = (0.239, 0.863, 0.592) // C_OK   #3DDC97

let space = CGColorSpace(name: CGColorSpace.sRGB)!

func color(_ c: (Double, Double, Double), _ a: Double = 1.0) -> CGColor {
    CGColor(colorSpace: space, components: [c.0, c.1, c.2, a])!
}

func gradient(_ stops: [CGColor]) -> CGGradient {
    let comps: [CGFloat] = stops.flatMap { $0.components ?? [0, 0, 0, 1] }
    let locs: [CGFloat] = stops.indices.map { CGFloat($0) / CGFloat(stops.count - 1) }
    return CGGradient(colorSpace: space, colorComponents: comps, locations: locs, count: stops.count)!
}

func roundedRect(_ x: Double, _ y: Double, _ w: Double, _ h: Double, _ r: Double) -> CGPath {
    CGPath(roundedRect: CGRect(x: x, y: y, width: w, height: h), cornerWidth: r, cornerHeight: r, transform: nil)
}

// All drawing happens in 1024-space, y-up.
func drawIcon(_ ctx: CGContext) {
    // --- Squircle base ----------------------------------------------------
    let squircle = roundedRect(0, 0, 1024, 1024, 232)
    ctx.saveGState()
    ctx.addPath(squircle)
    ctx.clip()

    ctx.drawLinearGradient(
        gradient([color((0.141, 0.094, 0.337)), color((0.055, 0.039, 0.125))]),
        start: CGPoint(x: 512, y: 1024), end: CGPoint(x: 512, y: 0), options: [])

    // Ambient purple glow behind the card.
    ctx.drawRadialGradient(
        gradient([color(accPurple, 0.30), color(accPurple, 0)]),
        startCenter: CGPoint(x: 512, y: 560), startRadius: 0,
        endCenter: CGPoint(x: 512, y: 560), endRadius: 640, options: [])

    // Glass edge highlight.
    ctx.addPath(roundedRect(3, 3, 1018, 1018, 229))
    ctx.setStrokeColor(color((1, 1, 1), 0.06))
    ctx.setLineWidth(3)
    ctx.strokePath()
    ctx.restoreGState()

    // --- Quest card (glass panel) ----------------------------------------
    let card = roundedRect(232, 244, 560, 500, 44)
    ctx.saveGState()
    ctx.setShadow(offset: CGSize(width: 0, height: -20), blur: 36,
                  color: CGColor(colorSpace: space, components: [0, 0, 0, 0.40])!)
    ctx.addPath(card)
    ctx.setFillColor(color((0.10, 0.067, 0.21), 0.8))
    ctx.fillPath()
    ctx.restoreGState()

    ctx.saveGState()
    ctx.addPath(card)
    ctx.clip()
    ctx.drawLinearGradient(
        gradient([color((0.133, 0.082, 0.275), 0.5), color((0.082, 0.055, 0.173), 0.5)]),
        start: CGPoint(x: 512, y: 744), end: CGPoint(x: 512, y: 244), options: [])
    ctx.restoreGState()

    ctx.addPath(card)
    ctx.setStrokeColor(color((0.627, 0.510, 1.0), 0.35)) // C_LINE
    ctx.setLineWidth(3)
    ctx.strokePath()

    // --- Level badge (purple -> pink tile) --------------------------------
    let badge = roundedRect(428, 500, 168, 168, 40)
    ctx.saveGState()
    ctx.setShadow(offset: CGSize(width: 0, height: -18), blur: 36, color: color(accPurple, 0.55))
    ctx.addPath(badge)
    ctx.setFillColor(color(accPurple))
    ctx.fillPath()
    ctx.restoreGState()

    ctx.saveGState()
    ctx.addPath(badge)
    ctx.clip()
    ctx.drawLinearGradient(
        gradient([color(accPurple), color(accPink)]),
        start: CGPoint(x: 428, y: 668), end: CGPoint(x: 596, y: 500), options: [])
    ctx.restoreGState()

    // Sword glyph, centered on the badge, pointing up.
    ctx.saveGState()
    ctx.translateBy(x: 512, y: 584)
    ctx.setFillColor(color((0.988, 0.976, 1.0), 0.97)) // C_TXT

    let blade = CGMutablePath()
    blade.move(to: CGPoint(x: 0, y: 62))
    blade.addLine(to: CGPoint(x: 11, y: 44))
    blade.addLine(to: CGPoint(x: 11, y: -22))
    blade.addLine(to: CGPoint(x: -11, y: -22))
    blade.addLine(to: CGPoint(x: -11, y: 44))
    blade.closeSubpath()
    ctx.addPath(blade)
    ctx.fillPath()

    ctx.addPath(roundedRect(-30, -22, 60, 10, 5))
    ctx.fillPath()
    ctx.addPath(roundedRect(-6, -46, 12, 24, 4))
    ctx.fillPath()

    ctx.addEllipse(in: CGRect(x: -9, y: -63, width: 18, height: 18))
    ctx.fillPath()
    ctx.restoreGState()

    // --- Quest row: checkbox + text bar ----------------------------------
    ctx.addEllipse(in: CGRect(x: 284, y: 424, width: 32, height: 32))
    ctx.setFillColor(color(okGreen))
    ctx.fillPath()
    ctx.setLineCap(.round)
    ctx.setLineJoin(.round)
    ctx.setStrokeColor(color((1, 1, 1), 0.95))
    ctx.setLineWidth(4.5)
    ctx.beginPath()
    ctx.move(to: CGPoint(x: 293, y: 440))
    ctx.addLine(to: CGPoint(x: 298, y: 445))
    ctx.addLine(to: CGPoint(x: 308, y: 431))
    ctx.strokePath()

    ctx.addPath(roundedRect(332, 428, 300, 24, 12))
    ctx.setFillColor(color((1, 1, 1), 0.10))
    ctx.fillPath()

    // --- XP bar ------------------------------------------------------------
    ctx.addPath(roundedRect(280, 296, 464, 56, 28))
    ctx.setFillColor(CGColor(colorSpace: space, components: [0, 0, 0, 0.45])!)
    ctx.fillPath()
    ctx.addPath(roundedRect(280, 296, 464, 56, 28))
    ctx.setStrokeColor(color((1, 1, 1), 0.08))
    ctx.setLineWidth(2)
    ctx.strokePath()

    ctx.saveGState()
    ctx.addPath(roundedRect(280, 296, 292, 56, 28))
    ctx.clip()
    ctx.drawLinearGradient(
        gradient([color(xpGold), color(xpAmber)]),
        start: CGPoint(x: 280, y: 324), end: CGPoint(x: 572, y: 324), options: [])
    ctx.restoreGState()
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
