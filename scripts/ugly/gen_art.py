#!/usr/bin/env python3
"""Bake the pictures of the tenor/ugly shell: python3 scripts/ugly/gen_art.py [out-dir]

Writes src/shells/ugly/UglyArt.h: the desk of tier 2 (six objects, no labels) and the sleep doodle,
both as 528x792 1-bit pictures laid out as the X3 portrait framebuffer and zlib packed, so the
firmware inflates them straight into the framebuffer. Strokes are one wobbly line from a seed, the
same way for every run, so the output is byte for byte the same each time.
"""
import math
import random
import sys
import zlib
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_tables import cr  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
W, H = 528, 792


class Man:
    def __init__(s):
        s.a = np.zeros((H, W), bool)  # True = ink

    def net(s, pts, w=2):
        h = w / 2 - 1e-6
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            x0, x1 = max(int(math.floor(min(ax, bx) - h)) - 1, 0), min(int(math.ceil(max(ax, bx) + h)) + 1, W)
            y0, y1 = max(int(math.floor(min(ay, by) - h)) - 1, 0), min(int(math.ceil(max(ay, by) + h)) + 1, H)
            if x0 >= x1 or y0 >= y1:
                continue
            X, Y = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
            dx, dy = bx - ax, by - ay
            L = dx * dx + dy * dy
            t = np.zeros_like(X) if L == 0 else np.clip(((X - ax) * dx + (Y - ay) * dy) / L, 0, 1)
            s.a[y0:y1, x0:x1] |= np.hypot(X - ax - t * dx, Y - ay - t * dy) < h

    def run(s, anchors, seed, amp=1.6, step=34, w=2, n=10):
        r = random.Random(seed)
        pts = []
        for (ax, ay), (bx, by) in zip(anchors, anchors[1:]):
            L = math.hypot(bx - ax, by - ay)
            k = max(1, int(L // step))
            nx, ny = (-(by - ay) / L, (bx - ax) / L) if L else (0, 0)
            for i in range(k):
                t = i / k
                o = r.gauss(0, amp) if pts else 0
                pts.append((ax + (bx - ax) * t + nx * o, ay + (by - ay) * t + ny * o))
        pts.append(anchors[-1])
        s.net(cr(pts, n), w)

    def packed(s):
        """The picture as the X3 portrait framebuffer: row = 527 - x, column = y, a set bit is white."""
        raw = s.a.T[::-1, :]
        return np.packbits(~raw).tobytes()


def circ(cx, cy, r, a0, a1, step=20):
    n = max(2, int(abs(a1 - a0) / step))
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def lamp(m):
    p = [(130, 236), (88, 242), (44, 236), (46, 226), (88, 222), (120, 228), (90, 226), (66, 168)] + \
        circ(62, 158, 9, 60, 420, 40) + [(72, 148), (112, 96)]
    p += [(104, 86), (126, 70), (176, 108), (166, 128), (140, 136), (112, 96)]
    m.run(p, 401, amp=1.2, step=30)
    m.run([(146, 146), (150, 156)], 409, amp=0.3, step=20)
    m.run([(168, 140), (176, 148)], 410, amp=0.3, step=20)


def clock(m):
    C, R = (262, 160), 46
    p = [(282, 172), C, (264, 127)] + circ(*C, R, -80, 50, 22) + [(300, 230), (290, 200)] + circ(*C, R, 64, 116, 20)
    p += [(224, 230), (234, 200)] + circ(*C, R, 130, 265, 22) + [(266, 104), (276, 96), (262, 88), (252, 98), (258, 108)]
    m.run(p, 402, amp=1.2, step=26)


def calendar(m):
    p = [(392, 92)] + circ(392, 80, 9, 90, 430, 40) + [(392, 92), (362, 92), (360, 232), (490, 230), (488, 92), (456, 92)]
    p += circ(456, 80, 9, 90, 430, 40) + [(456, 94), (456, 130), (362, 130)]
    m.run(p, 403, amp=1.3, step=30)


def book(m, x, y, k=1.0):
    def P(px, py):
        return (x + (px - 265) * k, y + (py - 300) * k)
    p = [(265, 478), (215, 458), (110, 466), (80, 452), (82, 320), (100, 304), (190, 302), (265, 328), (265, 478),
         (265, 328), (340, 302), (430, 304), (448, 320), (450, 452), (420, 466), (330, 458), (310, 466), (312, 520),
         (322, 506), (332, 522), (330, 461), (265, 478)]
    m.run([P(*q) for q in p], 404, amp=1.3 * k, step=30 * k, w=2)


def stack(m):
    p = [(46, 668), (44, 700), (230, 700), (230, 668), (62, 668), (60, 638), (216, 636), (216, 668), (216, 638),
         (206, 638), (206, 606), (50, 610), (52, 638), (62, 638)]
    m.run([(x, y - 40) for x, y in p], 405, amp=1.2, step=34)
    for yy in (636, 644, 652):
        m.run([(196, yy), (224, yy)], 406 + yy, amp=0.4, step=40, w=1)


def note(m):
    p = [(398, 676), (370, 650), (360, 628), (366, 612), (382, 608), (398, 624), (412, 606), (430, 610), (436, 628),
         (424, 652), (398, 676), (338, 702), (332, 566), (464, 560), (470, 668), (446, 702), (446, 676), (470, 668)]
    m.run([(x, y - 40) for x, y in p], 407, amp=1.2, step=30)


def desk():
    m = Man()
    lamp(m), clock(m), calendar(m), book(m, 265, 290), stack(m), note(m)
    return m


def sleeper():
    """A book under a moon, three z's climbing away from it."""
    m = Man()
    book(m, 264, 420, 0.82)
    mx, my, r = 448, 352, 38  # a crescent: the outer rim, then the rim of the bite taken out of it
    m.run(circ(mx, my, r, 60, 300, 20) + circ(mx + 0.5 * r, my, 0.87 * r, 270, 90, 20), 801, amp=0.8, step=18)
    for i, (zx, zy, s) in enumerate(((214, 372, 16), (262, 322, 24), (330, 262, 34))):  # z z Z
        m.run([(zx, zy), (zx + s, zy - 2), (zx, zy + s), (zx + s + 2, zy + s)], 810 + i, amp=0.9, step=14, w=3)
    return m


def emit(name, man, out):
    raw = man.packed()
    packed = zlib.compress(raw, 9)
    assert zlib.decompress(packed) == raw
    out.append('inline constexpr uint8_t %s[] = {' % name)
    for i in range(0, len(packed), 16):
        out.append('    ' + ', '.join('0x%02x' % b for b in packed[i:i + 16]) + ',')
    out.append('};')
    print(name, len(raw), '->', len(packed), 'B')


def main(out_dir=None):
    out = ['// Generated by scripts/ugly/gen_art.py. Do not edit by hand.',
           '//',
           '// 528x792 1-bit pictures as the X3 portrait framebuffer, zlib packed (52272 bytes raw each).',
           '#pragma once', '', '#include <cstdint>', '', 'namespace ugly::art {', '']
    emit('DESK', desk(), out)
    out.append('')
    emit('SLEEP', sleeper(), out)
    out += ['', '}  // namespace ugly::art', '']
    (Path(out_dir) if out_dir else ROOT / 'src/shells/ugly').joinpath('UglyArt.h').write_text('\n'.join(out))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else None)
