#!/usr/bin/env python3
"""Bake the scrawls of the quotation page of tenor/ugly: python3 scripts/ugly/gen_doodles.py [out-dir] [--preview dir]

Writes src/shells/ugly/UglyDoodleData.h: the stars, spirals, arrows, faces and the rest that fill the empty part of a
quotation page, as vector strokes in the stream of the sleep doodles (scripts/ugly/gen_sleep_set.py: the firmware draws both
with the same function, UglySleepSet.h drawPicture), one zlib stream, plus the ink box of each of these pictures and of the 8 sleep
doodles (those are drawn small on a page), so the firmware can place a drawing without drawing it first.
Everything is fixed numbers, so the output is the same byte for byte on every run.
"""
import math
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_sleep_set as gs  # noqa: E402  (its pictures are defined on import; its output is written only by its main)

ROOT = Path(__file__).resolve().parents[2]
PAD = 3  # the pen's own width, added to every box

DOODLES = []
NAMES = []


def doodle(name, *strokes):
    out = []
    for s in strokes:
        pts, w = (s, 3) if isinstance(s, list) else s
        out.append((pts, w, None))
    DOODLES.append(out)
    NAMES.append(name)


def line(a, b, w=3):
    return ([a, b], w)


def spiral(cx, cy, r0, r1, turns, step=28):
    n = int(turns * 360 / step)
    return [(cx + (r0 + (r1 - r0) * i / n) * math.cos(math.radians(i * step)),
             cy + (r0 + (r1 - r0) * i / n) * math.sin(math.radians(i * step))) for i in range(n + 1)]


def five_star(cx, cy, r, tilt=-90):
    p = [(cx + r * math.cos(math.radians(tilt + 72 * k)), cy + r * math.sin(math.radians(tilt + 72 * k))) for k in range(5)]
    return gs.sharp([p[0], p[2], p[4], p[1], p[3], p[0]])


def build():
    # 0 star
    doodle('star', (five_star(54, 56, 46), 3))
    # 1 sparkle: three crossing strokes
    doodle('sparkle', line((10, 40), (70, 42)), line((40, 8), (38, 72)), line((16, 14), (64, 66), 2), line((64, 12), (16, 68), 2))
    # 2 spiral
    doodle('spiral', (spiral(66, 66, 3, 58, 3), 2))
    # 3 arrow, straight
    doodle('arrow', ([(8, 44), (50, 40), (96, 42), (140, 38)], 3), ([(116, 18), (142, 38), (112, 60)], 3))
    # 4 arrow, curly (up and over)
    doodle('arrow-curl', ([(8, 86), (22, 40), (60, 12), (112, 20), (136, 50)], 3), ([(120, 36), (138, 54), (152, 32)], 3))
    # 5 face, lopsided
    doodle('face',
           ([(52, 8), (90, 14), (108, 46), (96, 86), (58, 104), (22, 90), (8, 52), (20, 20), (52, 8)], 3),
           (gs.ring(38, 44, 6, 8), 2), (gs.ring(76, 40, 11, 13), 2), ([(72, 40), (80, 42)], 3),
           ([(28, 76), (48, 86), (72, 84), (92, 70)], 3), ([(62, 84), (66, 100), (78, 98), (76, 82)], 2),
           ([(34, 22), (44, 14)], 2))
    # 6 cloud with rain
    doodle('cloud',
           ([(30, 66), (14, 54), (16, 38), (34, 32), (40, 14), (66, 8), (86, 20), (108, 14), (132, 28), (134, 48),
             (150, 58), (142, 68), (112, 72), (30, 66)], 3),
           ([(44, 88), (38, 106)], 2), ([(78, 90), (72, 108)], 2), ([(112, 88), (106, 106)], 2))
    # 7 graffiti, the way it goes on a toilet wall: two balls, a shaft, a smirk
    doodle('wall',
           (gs.ring(26, 26, 17, 17), 3), (gs.ring(26, 62, 17, 17), 3),
           ([(40, 30), (84, 28), (126, 30)], 3), ([(40, 56), (84, 58), (126, 56)], 3),
           ([(126, 30), (146, 33), (154, 44), (146, 54), (126, 56)], 3),
           ([(136, 38), (138, 40)], 3), ([(146, 38), (148, 40)], 3), ([(134, 50), (142, 54), (150, 49)], 2),
           ([(170, 28), (186, 20)], 2), ([(172, 44), (192, 44)], 2), ([(170, 60), (186, 68)], 2),
           ([(8, 14), (14, 8)], 2), ([(8, 72), (14, 80)], 2))
    # 8 bolt
    doodle('bolt', ([(48, 6), (16, 56), (38, 54), (22, 106), (64, 44), (42, 46), (48, 6)], 3))
    # 9 coil: loops walking right, a pen that will not let go
    pts = []
    for i in range(0, 61):
        t = i * 18
        pts.append((10 + i * 1.7 + 15 * math.cos(math.radians(t)), 26 + 13 * math.sin(math.radians(t))))
    doodle('coil', (pts, 2))
    # 10 zigzag fill, a word rubbed out
    z = []
    for i in range(0, 15):
        z.append((8 if i % 2 == 0 else 108, 6 + i * 2.4 + (3 if i % 2 else -2)))
    doodle('rub', (z, 3))
    # 11 big cross
    doodle('cross', line((8, 10), (70, 66), 3), line((70, 8), (10, 68), 3))
    # 12 heart, one stroke
    doodle('heart', ([(40, 22), (28, 8), (8, 14), (6, 34), (40, 70), (74, 34), (72, 14), (52, 8), (40, 22)], 3))
    # 13 three lines under a word: what a bored hand does
    doodle('lines', ([(8, 12), (120, 8)], 2), ([(8, 28), (96, 26)], 2), ([(8, 44), (128, 40)], 2))


build()
SLEEP_PICS = gs.PICS


def cdiv(a, b):
    """C division: toward zero (the firmware's; Python's // floors)."""
    return int(a / b)


def smooth_c(E):
    """gs.smooth with the firmware's integer division (they differ for a negative numerator)."""
    m, out = len(E), []
    for i in range(m - 1):
        p0, p1, p2, p3 = E[max(i - 1, 0)], E[i], E[i + 1], E[min(i + 2, m - 1)]
        for s in range(3):
            pt = []
            for a in (0, 1):
                q = 54 * p1[a] + (p2[a] - p0[a]) * s * 9 + (2 * p0[a] - 5 * p1[a] + 4 * p2[a] - p3[a]) * s * s * 3 + \
                    (-p0[a] + 3 * p1[a] - 3 * p2[a] + p3[a]) * s * s * s
                pt.append(cdiv(q + 27, 54))
            out.append(tuple(pt))
    out.append(E[-1])
    return out


def stroke_points(p, k, pts):
    seed = (p * 7 + k * 3 + 1) % 64
    return smooth_c(gs.expand(gs.split_steps(gs.clamp_pts(pts)), seed))


def box_of(strokes, index):
    xs, ys = [], []
    for k, (pts, w, _) in enumerate(strokes):
        for x, y in stroke_points(index, k, pts):
            xs.append(x)
            ys.append(y)
    return min(xs) - PAD, min(ys) - PAD, max(xs) + PAD + 1, max(ys) + PAD + 1


def blob():
    raw = bytearray([len(DOODLES)])
    for p, strokes in enumerate(DOODLES):
        raw.append(len(strokes))
        for k, (pts, w, seed) in enumerate(strokes):
            pts = gs.split_steps(gs.clamp_pts(pts))
            assert 2 <= len(pts) <= 255, (NAMES[p], k, len(pts))
            assert len(gs.expand(pts, (p * 7 + k * 3 + 1) % 64)) <= 256, (NAMES[p], k, 'too many points once wobbled')
            raw += gs.struct.pack('<BBBB', len(pts), w, pts[0][0] // 2, pts[0][1] // 2)
            for a, b in zip(pts, pts[1:]):
                raw += gs.struct.pack('<bb', (b[0] - a[0]) // 2, (b[1] - a[1]) // 2)
    return bytes(raw)


def emit(name, raw):
    z = zlib.compress(raw, 9)
    assert zlib.decompress(z) == raw
    out = ['inline constexpr uint8_t %s[] = {' % name]
    for i in range(0, len(z), 16):
        out.append('    ' + ', '.join('0x%02x' % b for b in z[i:i + 16]) + ',')
    out += ['};', 'inline constexpr unsigned %s_RAW = %d;' % (name, len(raw)), '']
    print(name, len(raw), '->', len(z), 'B')
    return out, len(z)


def preview(outdir):
    from PIL import Image, ImageDraw
    cols = 5
    rows = -(-len(DOODLES) // cols)
    cell = 240
    sheet = Image.new('L', (cols * cell, rows * cell), 255)
    for p, strokes in enumerate(DOODLES):
        im = Image.new('L', (cell, cell), 255)
        d = ImageDraw.Draw(im)
        for k, (pts, w, _) in enumerate(strokes):
            d.line(stroke_points(p, k, pts), fill=0, width=w, joint='curve')
        b = box_of(strokes, p)
        d.rectangle([b[0], b[1], b[2] - 1, b[3] - 1], outline=128)
        d.text((4, cell - 14), '%d %s' % (p, NAMES[p]), fill=0)
        sheet.paste(im, ((p % cols) * cell, (p // cols) * cell))
    sheet.save(Path(outdir) / 'doodles.png')


def main():
    args = sys.argv[1:]
    preview_dir = None
    if '--preview' in args:
        i = args.index('--preview')
        preview_dir = args[i + 1]
        del args[i:i + 2]
    out_dir = Path(args[0]) if args else ROOT / 'src/shells/ugly'
    o, n = emit('PICTURES', blob())
    out = ['// Generated by scripts/ugly/gen_doodles.py. Do not edit by hand.', '//',
           '// The scrawls of the quotation page as strokes (the stream of UglySleepData.h), and the ink boxes of these and of',
           '// the 8 sleep doodles, which the page draws small.',
           '#pragma once', '', '#include <cstdint>', '', 'namespace ugly::doodledata {', '',
           'struct Box {', '  int16_t x0, y0, x1, y1;  // the ink of the picture on its own canvas, PAD on each side, x1 and y1 exclusive', '};',
           'inline constexpr int PAD = %d;' % PAD, '']
    out += o
    out += ['inline constexpr int COUNT = %d;' % len(DOODLES)]
    out += ['// 0 star, 1 sparkle, 2 spiral, 3 arrow, 4 curly arrow, 5 face, 6 cloud, 7 the wall drawing, 8 bolt, 9 coil, 10 rubbed out,',
            '// 11 cross, 12 heart, 13 lines.']
    out += ['inline constexpr int WALL = %d;' % NAMES.index('wall')]
    out += ['inline constexpr Box BOX[COUNT] = {']
    for p, strokes in enumerate(DOODLES):
        out.append('    {%d, %d, %d, %d},  // %s' % (*box_of(strokes, p), NAMES[p]))
    out += ['};']
    out += ['inline constexpr Box SLEEP_BOX[%d] = {' % len(SLEEP_PICS)]
    for p, strokes in enumerate(SLEEP_PICS):
        out.append('    {%d, %d, %d, %d},' % box_of(strokes, p))
    out += ['};', '', '}  // namespace ugly::doodledata', '']
    (out_dir / 'UglyDoodleData.h').write_text('\n'.join(out))
    print('total packed', n, 'B,', len(DOODLES), 'pictures')
    if preview_dir:
        Path(preview_dir).mkdir(parents=True, exist_ok=True)
        preview(preview_dir)


if __name__ == '__main__':
    main()
