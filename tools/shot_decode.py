#!/usr/bin/env python3
"""Converte le schermate stampate dalla UI v2 (CONFIG_ABARTH_SCREENSHOT) in PNG.
uso: shot_decode.py <log_seriale> <cartella_out> [rotazione: 90|270|0]
"""
import base64, os, struct, sys, zlib

def png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def main():
    log, outdir = sys.argv[1], sys.argv[2]
    rot = int(sys.argv[3]) if len(sys.argv) > 3 else 90
    os.makedirs(outdir, exist_ok=True)
    data = open(log, 'rb').read().decode('latin1').splitlines()
    shots, cur, lines = {}, None, {}
    for l in data:
        l = l.strip()
        if l.startswith('@SHOT_BEGIN'):
            _, idx, total = l.split(); cur = (int(idx), int(total)); lines = {}
        elif l.startswith('@S ') and cur:
            p = l.split(' ', 2)
            if len(p) == 3:
                try: lines[int(p[1])] = base64.b64decode(p[2])
                except Exception: pass
        elif l.startswith('@SHOT_END') and cur:
            shots[cur[0]] = (cur[1], lines); cur = None
    W, H = 1024, 600
    for idx, (total, lines) in sorted(shots.items()):
        blob = b''.join(lines[k] for k in sorted(lines))
        px = []
        for o in range(0, len(blob) - 3, 4):
            cnt, v = struct.unpack_from('<HH', blob, o)
            px.extend([v] * cnt)
        px = (px + [0] * (W * H))[:W * H]
        if rot in (90, 270):
            ow, oh = H, W
            out = bytearray(ow * oh * 3)
            for y in range(oh):
                for x in range(ow):
                    # 90: logico(x,y) = fisico(riga=x, col=W-1-y)
                    v = px[x * W + (W - 1 - y)] if rot == 90 else px[(H - 1 - x) * W + y]
                    r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
                    i = (y * ow + x) * 3
                    out[i] = r << 3 | r >> 2; out[i + 1] = g << 2 | g >> 4; out[i + 2] = b << 3 | b >> 2
        else:
            ow, oh = W, H
            out = bytearray(W * H * 3)
            for i, v in enumerate(px):
                r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
                out[i * 3:i * 3 + 3] = bytes((r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2))
        ok = 'completa' if len(blob) == total else f'PARZIALE {len(blob)}/{total}'
        path = os.path.join(outdir, f'pagina_{idx}.png')
        png(path, ow, oh, bytes(out))
        print(path, ok)

if __name__ == '__main__':
    main()
