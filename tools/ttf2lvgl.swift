// ttf2lvgl.swift - converte un font TTF in un font LVGL 9 (lv_font_fmt_txt, 4 bpp)
// usando solo CoreText/CoreGraphics di macOS (nessuno strumento esterno).
//
// uso: swift ttf2lvgl.swift <font.ttf> <px> <nome_c> <caratteri> <out.c> [tabular] [fallback]
//   tabular  = 1 per cifre a larghezza fissa (valori che non "ballano")
//   fallback = nome di un lv_font_t da usare per i caratteri mancanti (es. lv_font_montserrat_24)
import Foundation
import CoreText
import CoreGraphics

let a = CommandLine.arguments
guard a.count >= 6 else {
    print("uso: ttf2lvgl <font.ttf> <px> <nome> <caratteri> <out.c> [tabular] [fallback]"); exit(1)
}
let path = a[1], px = CGFloat(Double(a[2])!), name = a[3], chars = a[4], out = a[5]
let tabular = a.count > 6 && a[6] == "1"
let fallback = a.count > 7 ? a[7] : ""
// tight = 1: altezza riga = solo l'ingombro dei glifi (font di sole cifre),
// cosi' i numeri grandi si posizionano senza enormi spazi sopra e sotto
let tight = a.count > 8 && a[8] == "1"

guard let prov = CGDataProvider(filename: path), let cg = CGFont(prov) else { print("font non leggibile"); exit(1) }
let font = CTFontCreateWithGraphicsFont(cg, px, nil, nil)
let ascent = CTFontGetAscent(font), descent = CTFontGetDescent(font)

struct G { var cp: UInt32; var w: Int; var h: Int; var ox: Int; var oy: Int; var adv: Int; var px: [UInt8] }
var glyphs: [G] = []
var seen = Set<UInt32>()
for s in chars.unicodeScalars where !seen.contains(s.value) {
    seen.insert(s.value)
    var u = Array(String(s).utf16)
    var gl = [CGGlyph](repeating: 0, count: u.count)
    if !CTFontGetGlyphsForCharacters(font, &u, &gl, u.count) || gl[0] == 0 {
        FileHandle.standardError.write("manca U+\(String(s.value, radix: 16))\n".data(using: .utf8)!); continue
    }
    var g1 = gl[0]
    var adv = CGSize.zero
    CTFontGetAdvancesForGlyphs(font, .horizontal, &g1, &adv, 1)
    var r = CTFontGetBoundingRectsForGlyphs(font, .horizontal, &g1, nil, 1)
    if r.isNull || r.width < 0.5 { // spazio o glifo vuoto
        glyphs.append(G(cp: s.value, w: 0, h: 0, ox: 0, oy: 0, adv: Int((adv.width * 16).rounded()), px: [])); continue
    }
    r = r.insetBy(dx: -2, dy: -2)
    let W = Int(ceil(r.width)) + 2, H = Int(ceil(r.height)) + 2
    let ox0 = Int(floor(r.origin.x)), oy0 = Int(floor(r.origin.y))
    var buf = [UInt8](repeating: 0, count: W * H)
    let cs = CGColorSpaceCreateDeviceGray()
    buf.withUnsafeMutableBytes { p in
        let ctx = CGContext(data: p.baseAddress, width: W, height: H, bitsPerComponent: 8, bytesPerRow: W,
                            space: cs, bitmapInfo: CGImageAlphaInfo.none.rawValue)!
        ctx.setAllowsAntialiasing(true); ctx.setShouldAntialias(true)
        ctx.setAllowsFontSmoothing(false); ctx.setShouldSmoothFonts(false)
        ctx.setFillColor(gray: 1, alpha: 1)
        var pos = CGPoint(x: -CGFloat(ox0), y: -CGFloat(oy0))
        CTFontDrawGlyphs(font, &g1, &pos, 1, ctx)
    }
    // ritaglio al riquadro non vuoto (riga 0 del buffer = in alto)
    var minx = W, maxx = -1, miny = H, maxy = -1
    for y in 0..<H { for x in 0..<W where buf[y * W + x] > 8 {
        minx = min(minx, x); maxx = max(maxx, x); miny = min(miny, y); maxy = max(maxy, y) } }
    if maxx < 0 { glyphs.append(G(cp: s.value, w: 0, h: 0, ox: 0, oy: 0, adv: Int((adv.width * 16).rounded()), px: [])); continue }
    let bw = maxx - minx + 1, bh = maxy - miny + 1
    var pxs = [UInt8](); pxs.reserveCapacity(bw * bh)
    for y in miny...maxy { for x in minx...maxx { pxs.append(UInt8((Int(buf[y * W + x]) * 15 + 127) / 255)) } }
    // ofs_y: dal baseline al bordo inferiore del riquadro (positivo verso l'alto)
    let bottom_from_img_bottom = H - 1 - maxy
    let oy = oy0 + bottom_from_img_bottom
    glyphs.append(G(cp: s.value, w: bw, h: bh, ox: ox0 + minx, oy: oy, adv: Int((adv.width * 16).rounded()), px: pxs))
}
glyphs.sort { $0.cp < $1.cp }

// Cifre a larghezza fissa: stesso avanzamento per 0-9, glifo centrato
if tabular {
    let digits = glyphs.indices.filter { glyphs[$0].cp >= 0x30 && glyphs[$0].cp <= 0x39 }
    let maxAdv = digits.map { glyphs[$0].adv }.max() ?? 0
    for i in digits {
        let extra = (maxAdv - glyphs[i].adv) / 16 / 2
        glyphs[i].ox += extra; glyphs[i].adv = maxAdv
    }
}

var c = "/* Generato da tools/ttf2lvgl.swift da \((path as NSString).lastPathComponent) a \(Int(px)) px.\n * Font Manrope: SIL Open Font License 1.1 (fonts/OFL.txt). */\n"
c += "#include \"lvgl.h\"\n\n"
if !fallback.isEmpty { c += "LV_FONT_DECLARE(\(fallback));\n\n" }
c += "static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {\n"
var idx = [Int](); var off = 0; var bytes = [UInt8]()
for g in glyphs {
    idx.append(off)
    var i = 0
    while i < g.px.count {
        let hi = g.px[i], lo = i + 1 < g.px.count ? g.px[i + 1] : 0
        bytes.append(hi << 4 | lo); i += 2
    }
    off = bytes.count
}
for (i, b) in bytes.enumerated() { c += String(format: "0x%02x,", b); c += (i % 16 == 15) ? "\n" : " " }
if bytes.isEmpty { c += "0x00" }
c += "\n};\n\nstatic const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {\n    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},\n"
for (i, g) in glyphs.enumerated() {
    c += "    {.bitmap_index = \(idx[i]), .adv_w = \(g.adv), .box_w = \(g.w), .box_h = \(g.h), .ofs_x = \(g.ox), .ofs_y = \(g.oy)}, /* U+\(String(format: "%04X", g.cp)) */\n"
}
c += "};\n\n"
let first = glyphs.first!.cp
c += "static const uint16_t unicode_list[] = {\n    " + glyphs.map { String(format: "0x%x", $0.cp - first) }.joined(separator: ", ") + "\n};\n\n"
c += "static const lv_font_fmt_txt_cmap_t cmaps[] = {\n    {.range_start = \(first), .range_length = \(glyphs.last!.cp - first + 1), .glyph_id_start = 1,\n     .unicode_list = unicode_list, .glyph_id_ofs_list = NULL, .list_length = \(glyphs.count), .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}\n};\n\n"
c += "static const lv_font_fmt_txt_dsc_t font_dsc = {\n    .glyph_bitmap = glyph_bitmap,\n    .glyph_dsc = glyph_dsc,\n    .cmaps = cmaps,\n    .kern_dsc = NULL,\n    .kern_scale = 0,\n    .cmap_num = 1,\n    .bpp = 4,\n    .kern_classes = 0,\n    .bitmap_format = 0,\n    .stride = 0,\n};\n\n"
let capH = Int(CTFontGetCapHeight(font).rounded()), xH = Int(CTFontGetXHeight(font).rounded())
c += "const lv_font_t \(name) = {\n    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n"
var lineH = Int(ceil(ascent + descent)), baseL = Int(ceil(descent))
if tight {
    let top = glyphs.map { $0.oy + $0.h }.max() ?? lineH
    let bot = glyphs.map { $0.oy }.min() ?? 0
    lineH = top - min(bot, 0); baseL = -min(bot, 0)
}
c += "    .line_height = \(lineH),\n    .base_line = \(baseL),\n    .cap_height = \(capH),\n    .x_height = \(xH),\n"
c += "    .subpx = LV_FONT_SUBPX_NONE,\n    .underline_position = -2,\n    .underline_thickness = 1,\n    .dsc = &font_dsc,\n"
c += "    .fallback = \(fallback.isEmpty ? "NULL" : "&" + fallback),\n    .user_data = NULL,\n};\n"
try! c.write(toFile: out, atomically: true, encoding: .utf8)
print("\(name): \(glyphs.count) glifi, \(bytes.count) byte bitmap")
