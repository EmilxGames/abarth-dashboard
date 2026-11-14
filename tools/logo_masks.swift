// logo_masks.swift - converte le immagini del logo (PNG/JPG/WEBP) in maschere
// A8 ritagliate e ridimensionate, per gli sfondi "logo in rilievo" della UI
// Moderno. Solo CoreGraphics/ImageIO di macOS, nessuno strumento esterno.
// uso: swift tools/logo_masks.swift <out.c> <nome> <file> <max_w> <max_h> [<nome> <file> <max_w> <max_h> ...]
import Foundation
import CoreGraphics
import ImageIO

func loadMask(_ path: String) -> (w: Int, h: Int, m: [UInt8])? {
    guard let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil),
          let img = CGImageSourceCreateImageAtIndex(src, 0, nil) else { return nil }
    let w = img.width, h = img.height
    var rgba = [UInt8](repeating: 0, count: w * h * 4)
    let ctx = CGContext(data: &rgba, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                        space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.draw(img, in: CGRect(x: 0, y: 0, width: w, height: h))
    let hasAlpha = img.alphaInfo != .none && img.alphaInfo != .noneSkipLast && img.alphaInfo != .noneSkipFirst
    var m = [UInt8](repeating: 0, count: w * h)
    for i in 0..<(w * h) {
        let a = Double(rgba[i * 4 + 3]) / 255.0
        if hasAlpha {
            m[i] = UInt8(a * 255)                       // logo = parte non trasparente
        } else {                                         // logo scuro su fondo chiaro
            let l = (0.299 * Double(rgba[i * 4]) + 0.587 * Double(rgba[i * 4 + 1]) + 0.114 * Double(rgba[i * 4 + 2])) / 255.0
            m[i] = UInt8(max(0, min(255, (1.0 - l) * 255 * 1.1)))
        }
    }
    return (w, h, m)
}

func cropScale(_ src: (w: Int, h: Int, m: [UInt8]), maxW: Int, maxH: Int) -> (w: Int, h: Int, m: [UInt8]) {
    var x0 = src.w, y0 = src.h, x1 = 0, y1 = 0
    for y in 0..<src.h { for x in 0..<src.w where src.m[y * src.w + x] > 24 {
        x0 = min(x0, x); y0 = min(y0, y); x1 = max(x1, x); y1 = max(y1, y)
    } }
    let cw = x1 - x0 + 1, ch = y1 - y0 + 1
    let s = min(Double(maxW) / Double(cw), Double(maxH) / Double(ch))
    let ow = Int(Double(cw) * s), oh = Int(Double(ch) * s)
    // ritaglio -> immagine grigia -> ridimensionata con interpolazione di qualita'
    var crop = [UInt8](repeating: 0, count: cw * ch)
    for y in 0..<ch { for x in 0..<cw { crop[y * cw + x] = src.m[(y + y0) * src.w + x + x0] } }
    let gray = CGColorSpaceCreateDeviceGray()
    let prov = CGDataProvider(data: Data(crop) as CFData)!
    let cimg = CGImage(width: cw, height: ch, bitsPerComponent: 8, bitsPerPixel: 8, bytesPerRow: cw, space: gray,
                       bitmapInfo: CGBitmapInfo(rawValue: 0), provider: prov, decode: nil, shouldInterpolate: true, intent: .defaultIntent)!
    var out = [UInt8](repeating: 0, count: ow * oh)
    let octx = CGContext(data: &out, width: ow, height: oh, bitsPerComponent: 8, bytesPerRow: ow, space: gray, bitmapInfo: 0)!
    octx.interpolationQuality = .high
    octx.draw(cimg, in: CGRect(x: 0, y: 0, width: ow, height: oh))
    return (ow, oh, out)
}

let args = Array(CommandLine.arguments.dropFirst())
var c = "/* Generato da tools/logo_masks.swift: maschere A8 dei loghi per gli sfondi della UI Moderno. */\n#include <stdint.h>\n\n"
var i = 1
while i + 3 < args.count {
    let name = args[i], file = args[i + 1], mw = Int(args[i + 2])!, mh = Int(args[i + 3])!
    guard let m = loadMask(file) else { print("impossibile leggere \(file)"); exit(1) }
    let r = cropScale(m, maxW: mw, maxH: mh)
    c += "const uint16_t \(name)_w = \(r.w), \(name)_h = \(r.h);\nconst uint8_t \(name)_map[] = {\n"
    for (k, b) in r.m.enumerated() { c += String(format: "%d,", b); if k % 40 == 39 { c += "\n" } }
    c += "};\n\n"
    print("\(name): \(r.w)x\(r.h) da \(file)")
    i += 4
}
try! c.write(toFile: args[0], atomically: true, encoding: .utf8)
