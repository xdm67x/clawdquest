// Renders the Icons8 Clawd mood icons into src/sprites.h.
//
//   make sprites   # or: swift tools/mksprites.swift ~/Downloads/Icons8 src/sprites.h
//
// The icons are flat rect-only SVGs (48px grid, with rotate transforms), so
// this tool rasterizes them itself — no qlmanage or other renderer needed.
// Each icon is drawn at 144px into one RGBA atlas (3x3 cells, straight
// alpha) and emitted as a base64 C header the pet loads at startup (the same
// pattern as the font atlas). Icons by Icons8.
import Foundation
import CoreGraphics
import AppKit

let CELL = 144
let COLS = 3
let ROWS = 3

// Atlas slot order (row-major) and the icon file each one comes from.
let icons: [(file: String, key: String)] = [
    ("icons8-clawd-48", "SPR_IDLE"),              // 0 idle
    ("icons8-clawd-thinking-48", "SPR_WORK"),     // 1 chronometer running
    ("icons8-clawd-sleeping-48", "SPR_DOZE"),     // 2 dozing
    ("icons8-clawd-exclamation-mark-48", "SPR_WAKE"), // 3 woke up
    ("icons8-clawd-hands-up-48", "SPR_HAPPY"),    // 4 patted
    ("icons8-clawd-happy-48", "SPR_FED"),         // 5 all quests done
    ("icons8-clawd-in-love-48", "SPR_EAT"),       // 6 eating his apple
    ("icons8-clawd-dizzy-48", "SPR_DIZZY"),       // 7 being dragged
    ("icons8-clawd-sparkles-48", "SPR_PARTY"),    // 8 board cleared
]

let args = CommandLine.arguments
guard args.count == 3 else {
    print("usage: swift tools/mksprites.swift <icons-dir> <out-header>")
    exit(1)
}
let dir = URL(fileURLWithPath: args[1], isDirectory: true)
let out = URL(fileURLWithPath: args[2])

struct SvgRect {
    var x: Double = 0, y: Double = 0, w: Double = 0, h: Double = 0
    var fill: CGColor?
    var rot: (a: Double, cx: Double, cy: Double)?
}

final class RectCollector: NSObject, XMLParserDelegate {
    var rects: [SvgRect] = []

    private func color(_ s: String?) -> CGColor? {
        guard var hex = s, hex.hasPrefix("#") else { return s == nil ? CGColor.black : nil }
        hex.removeFirst()
        if hex.count == 3 { hex = hex.map { "\($0)\($0)" }.joined() }
        guard hex.count == 6, let v = UInt64(hex, radix: 16) else { return nil }
        let r = CGFloat((v >> 16) & 0xFF) / 255
        let g = CGFloat((v >> 8) & 0xFF) / 255
        let b = CGFloat(v & 0xFF) / 255
        return CGColor(srgbRed: r, green: g, blue: b, alpha: 1)
    }

    func parser(_ p: XMLParser, didStartElement name: String, namespaceURI: String?,
                qualifiedName qName: String?, attributes: [String: String]) {
        guard name == "rect" else { return }
        var r = SvgRect()
        r.x = Double(attributes["x"] ?? "0") ?? 0
        r.y = Double(attributes["y"] ?? "0") ?? 0
        r.w = Double(attributes["width"] ?? "0") ?? 0
        r.h = Double(attributes["height"] ?? "0") ?? 0
        r.fill = color(attributes["fill"])
        if let tr = attributes["transform"], tr.hasPrefix("rotate") {
            let nums = tr.split(whereSeparator: { !"0123456789.-".contains($0) })
                .compactMap { Double($0) }
            if nums.count >= 3 { r.rot = (nums[0], nums[1], nums[2]) }
            else if nums.count == 1 { r.rot = (nums[0], r.x + r.w / 2, r.y + r.h / 2) }
        }
        rects.append(r)
    }
}

func render(_ svg: URL) -> [UInt8] {
    guard let parser = XMLParser(contentsOf: svg) else {
        print("cannot read \(svg.path)"); exit(1)
    }
    let collector = RectCollector()
    parser.delegate = collector
    guard parser.parse() else {
        print("parse error in \(svg.path): \(parser.parserError?.localizedDescription ?? "?")")
        exit(1)
    }

    // CGContext only supports premultiplied alpha for RGBA8; that is what we
    // want: the pet filters the texture premultiplied (no edge fringes) and
    // composites it with premultiplied source-over blending.
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    var buf = [UInt8](repeating: 0, count: CELL * CELL * 4)
    let ctx = CGContext(data: &buf, width: CELL, height: CELL, bitsPerComponent: 8,
                        bytesPerRow: CELL * 4, space: cs,
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    // Work in the icon's own coordinates: y down, 48-unit grid.
    ctx.translateBy(x: 0, y: CGFloat(CELL))
    ctx.scaleBy(x: CGFloat(CELL) / 48, y: -CGFloat(CELL) / 48)
    for r in collector.rects {
        ctx.saveGState()
        if let rot = r.rot {
            ctx.translateBy(x: rot.cx, y: rot.cy)
            ctx.rotate(by: rot.a * .pi / 180)
            ctx.translateBy(x: -rot.cx, y: -rot.cy)
        }
        ctx.setFillColor(r.fill ?? CGColor.black)
        ctx.fill(CGRect(x: r.x, y: r.y, width: r.w, height: r.h))
        ctx.restoreGState()
    }
    return buf
}

let W = CELL * COLS
let H = CELL * ROWS
var atlas = [UInt8](repeating: 0, count: W * H * 4)

for (i, icon) in icons.enumerated() {
    // The context's RGBA8 output is premultiplied — keep it that way: the
    // pet samples with GL_LINEAR, and filtering premultiplied colors avoids
    // dark fringes on the sprite's edges (it blends premultiplied).
    let buf = render(dir.appendingPathComponent(icon.file + ".svg"))

    // Copy into the atlas, top-down row order (pet.c maps uv to match).
    let col = i % COLS
    let row = i / COLS
    for y in 0..<CELL {
        let srcRow = y * CELL * 4
        let dstRow = (row * CELL + y) * W * 4
        for x in 0..<CELL * 4 {
            atlas[dstRow + col * CELL * 4 + x] = buf[srcRow + x]
        }
    }
}

let b64 = Data(atlas).base64EncodedString()
var header = """
// Generated by tools/mksprites.swift from the Icons8 Clawd icons — do not
// edit by hand. One premultiplied RGBA atlas, base64-encoded; row-major cell
// order.
#pragma once

#define SPR_COLS \(COLS)
#define SPR_ROWS \(ROWS)
#define SPR_W \(W)
#define SPR_H \(H)
#define SPR_B64_LEN \(b64.count)

enum { \(icons.map { $0.key }.joined(separator: ", ")) };

static const char * const SPR_B64 =
"""
for chunk in stride(from: 0, to: b64.count, by: 120) {
    let end = min(chunk + 120, b64.count)
    header += "\"" + String(b64[b64.index(b64.startIndex, offsetBy: chunk)..<b64.index(b64.startIndex, offsetBy: end)]) + "\"\n"
}
header += ";\n"
try header.write(to: out, atomically: true, encoding: .utf8)
print("wrote \(out.path) (\(b64.count) base64 chars)")
