// icons.swift - disegna le icone della barra di stato (A8, 20x20) con CoreGraphics
// e le scrive come immagini LVGL 9 (disegno su griglia 26, reso a 20 px). Nessuno strumento esterno.
// uso: swift tools/icons.swift main/ui_v2/icons.c
import Foundation
import CoreGraphics

let S = 20, SS = 4            // lato in pixel, supercampionamento
let W = S * SS
func render(_ draw: (CGContext) -> Void) -> [UInt8] {
    var buf = [UInt8](repeating: 0, count: W * W)
    buf.withUnsafeMutableBytes { p in
        let ctx = CGContext(data: p.baseAddress, width: W, height: W, bitsPerComponent: 8, bytesPerRow: W,
                            space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.none.rawValue)!
        ctx.translateBy(x: 0, y: CGFloat(W)); ctx.scaleBy(x: CGFloat(SS), y: -CGFloat(SS))  // coordinate in px, y verso il basso
        ctx.scaleBy(x: CGFloat(S) / 26, y: CGFloat(S) / 26)   // disegni pensati su 26 px
        ctx.setFillColor(gray: 1, alpha: 1); ctx.setStrokeColor(gray: 1, alpha: 1)
        ctx.setLineCap(.round); ctx.setLineJoin(.round)
        draw(ctx)
    }
    var out = [UInt8](repeating: 0, count: S * S)
    for y in 0..<S { for x in 0..<S {
        var sum = 0
        for yy in 0..<SS { for xx in 0..<SS { sum += Int(buf[(y * SS + yy) * W + x * SS + xx]) } }
        out[y * S + x] = UInt8(sum / (SS * SS))
    } }
    return out
}

// Termometro: gambo con contorno, bulbo pieno, liquido, tre tacche
let thermo = render { c in
    c.setLineWidth(2)
    let stem = CGPath(roundedRect: CGRect(x: 8, y: 2.5, width: 7, height: 15), cornerWidth: 3.5, cornerHeight: 3.5, transform: nil)
    c.addPath(stem); c.strokePath()
    c.fillEllipse(in: CGRect(x: 5.5, y: 14, width: 12, height: 10))
    c.fill(CGRect(x: 10.5, y: 8, width: 2, height: 9))
    c.setLineWidth(1.6)
    for y in [5.5, 9.0, 12.5] { c.move(to: CGPoint(x: 18, y: y)); c.addLine(to: CGPoint(x: 21.5, y: y)) }
    c.strokePath()
}

// Oliatore: corpo, tappo, beccuccio inclinato e goccia
let oil = render { c in
    let body = CGMutablePath()
    body.move(to: CGPoint(x: 3, y: 12)); body.addLine(to: CGPoint(x: 15, y: 12))
    body.addLine(to: CGPoint(x: 22, y: 8.5)); body.addLine(to: CGPoint(x: 23.5, y: 10))
    body.addLine(to: CGPoint(x: 16.5, y: 19)); body.addLine(to: CGPoint(x: 3, y: 19)); body.closeSubpath()
    c.addPath(body); c.fillPath()
    c.fill(CGRect(x: 6.5, y: 9, width: 5, height: 3))          // collo
    c.fill(CGRect(x: 5, y: 7.5, width: 8, height: 2))          // tappo
    c.setLineWidth(1.8); c.move(to: CGPoint(x: 3, y: 13.5)); c.addLine(to: CGPoint(x: 1, y: 12)); c.strokePath()  // manico
    let drop = CGMutablePath()                                  // goccia sotto il beccuccio
    drop.move(to: CGPoint(x: 23, y: 13.5))
    drop.addCurve(to: CGPoint(x: 23, y: 19.5), control1: CGPoint(x: 21, y: 16.5), control2: CGPoint(x: 21, y: 19.5))
    drop.addCurve(to: CGPoint(x: 23, y: 13.5), control1: CGPoint(x: 25, y: 19.5), control2: CGPoint(x: 25, y: 16.5))
    c.addPath(drop); c.fillPath()
}

func emit(_ name: String, _ px: [UInt8]) -> String {
    var s = "static const uint8_t \(name)_map[] = {\n"
    for (i, b) in px.enumerated() { s += String(format: "0x%02x,", b); s += (i % S == S - 1) ? "\n" : "" }
    s += "};\nconst lv_image_dsc_t \(name) = {\n    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .flags = 0, .w = \(S), .h = \(S), .stride = \(S), .reserved_2 = 0},\n"
    s += "    .data_size = sizeof(\(name)_map),\n    .data = \(name)_map,\n    .reserved = NULL,\n};\n\n"
    return s
}
var c = "/* Generato da tools/icons.swift: icone A8 20x20 della barra di stato (colore dal recolor). */\n#include \"lvgl.h\"\n\n"
c += emit("icon_thermo", thermo) + emit("icon_oil", oil)
try! c.write(toFile: CommandLine.arguments[1], atomically: true, encoding: .utf8)
print("icone scritte")
