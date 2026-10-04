#!/usr/bin/env python3
"""Bake the sleep set of the tenor/ugly shell: python3 scripts/ugly/gen_sleep_set.py [out-dir] [--md file] [--preview dir]

Writes src/shells/ugly/UglySleepData.h: 8 doodles as vector strokes (anchor points as int8 steps plus
a wobble seed, drawn by the firmware with the ugly pen) and the sleep sentences in Vietnamese and
English, one zlib stream per block so the firmware inflates only what it shows. Everything is fixed
text and fixed numbers, so the output is the same byte for byte on every run.

Stream of the pictures (the wobble seed of stroke k of picture p is (p * 7 + k * 3 + 1) % 64): u8 count, then per picture u8 strokes, then per stroke
  u8 points, u8 width, u8 x0 / 2, u8 y0 / 2, then (points - 1) pairs of int8 (dx / 2, dy / 2).
Every point sits on a grid of 2 px, which halves what a step costs.
Canvas of a picture: 480 x 440. Text stream: one record per line, a code byte then the words, ended by \\n.
"""
import math
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CW, CH = 480, 440
STEP, AMP = 40, 2  # the firmware adds a wobbling point every STEP px of a long segment
SENTENCE_CAP = 128

# ---------------------------------------------------------------- sentences
# code: a generic sleep line, b nothing read today, d night reader, f..l by the hour of going to sleep:
# 0-5, 5-9, 9-12, 12-14, 14-18, 18-22, 22-24. Wake lines live in the i18n YAML files.
LINES = [
    ('a', 'Tao ngủ đây, mai nhớ thức tao dậy. Còn giờ thì kệ cmm, zz Z Z', "I'm out. Wake me up tomorrow. Until then, screw everything, zz Z Z"),
    ('a', 'Tắt máy rồi. Mày cũng tắt cái mồm đi mà ngủ.', 'Powered off. Now zip it and go to sleep too.'),
    ('a', 'Ngủ đi cha nội, đọc có 3 trang cũng bày đặt thức khuya.', 'Go to bed, pal. 3 pages and you call that staying up late?'),
    ('a', 'Tao tắt màn hình, mày tắt não. Hòa.', 'I turn off my screen, you turn off your brain. Fair.'),
    ('a', 'Tao đi ngủ trước, mày ở lại với cái trần nhà.', "I'm off. You stay with the ceiling."),
    ('a', 'Mai dậy sớm đọc đi. Nói vậy chứ tao biết thừa rồi.', 'Wake up early and read. Yeah right, I know you.'),
    ('b', 'Hôm nay đọc đúng 0 phút. Ngủ cho khỏe để mai lười tiếp.', '0 minutes read today. Rest up, so you can be lazy again tomorrow.'),
    ('d', 'Toàn đọc lúc nửa đêm, vừa đi ăn trộm vừa đọc à?', 'Always reading at midnight. Burgling and reading at once?'),
    ('d', 'Đọc đêm nhiều vậy, mày là dơi hả? Ngủ đi.', 'So much night reading. Are you a bat? Go to sleep.'),
    ('f', 'Gần sáng rồi. Ngủ giờ này cho có lệ thôi.', 'Almost sunrise. Sleeping now is a formality.'),
    ('f', 'Giờ này còn đọc? Mày trực hộ mặt trời à?', "Still reading? Covering the sun's shift?"),
    ('g', 'Sáng bảnh mắt mới ngủ. Cú đêm chính hiệu.', "Going to bed when it's already bright out. A true night owl."),
    ('h', 'Ngủ sáng hả? Sếp nào cho nghỉ mà sướng thế.', 'Morning nap? Which boss lets you get away with that?'),
    ('h', 'Chưa trưa đã ngủ, đời nó nhàn thật.', 'Napping before noon. Must be nice.'),
    ('i', 'Ngủ trưa hả, sướng ghê. Tao nghỉ cùng cho đỡ tủi.', "Nap time, huh? Must be nice. I'll nap with you so I don't feel left out."),
    ('i', 'Cơm xong ngủ liền. Chuẩn bài dân nhàn.', 'Eat, then sleep. Textbook loafer.'),
    ('j', 'Chiều chiều ngủ, tối nay thao thức cho coi.', "A late-afternoon nap, tonight you'll toss and turn. Watch."),
    ('l', 'Đi ngủ đúng giờ. Hiếm lắm, ghi nhận.', 'In bed on schedule. Rare. Noted.'),
]

# ---------------------------------------------------------------- strokes
PICS = []


def arc(cx, cy, rx, ry, a0, a1, gap=46, least=2):
    per = math.pi * (rx + ry) * abs(a1 - a0) / 180
    n = max(least, int(per / gap))
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def ring(cx, cy, rx, ry, a0=-100):
    return arc(cx, cy, rx, ry, a0, a0 + 372, least=7)


def sharp(pts):
    """Every inner corner twice, so the smoothing keeps it a corner."""
    out = [pts[0]]
    for p in pts[1:-1]:
        out += [p, p]
    return out + [pts[-1]]


def zed(x, y, s):
    return sharp([(x, y), (x + s, y - 2), (x, y + s), (x + s + 2, y + s)])


def star(x, y, r=10):
    return [[(x - r, y), (x + r, y)], [(x, y - r), (x, y + r)]]


def pic(*strokes):
    """strokes: (points, width, seed). A bare list of points draws at width 3."""
    out = []
    for s in strokes:
        pts, w, seed = (s, 3, 0) if isinstance(s, list) else s
        out.append((pts, w, seed))
    PICS.append(out)


def S(pts, w=3, seed=None):
    return (pts, w, seed)


def bed_sleeper():
    s = [S([(20, 345), (460, 345)], 3), S([(34, 345), (34, 405)]), S([(446, 345), (446, 405)]),
         S([(20, 230), (20, 405)]),
         S([(48, 322), (46, 292), (62, 262), (110, 252), (176, 262), (182, 296), (160, 320), (100, 330), (48, 322)]),
         S(ring(112, 232, 40, 38)),
         S(arc(97, 226, 10, 6, 15, 165, 8), 2), S(arc(131, 226, 10, 6, 15, 165, 8), 2),
         S(ring(116, 256, 9, 12), 2),
         S([(156, 290), (200, 262), (290, 252), (380, 268), (448, 300), (450, 345)]),
         S([(150, 345), (150, 290)]),
         S([(326, 258), (338, 330), (332, 344)], 2)]
    s += [S(zed(210, 170, 22), 3), S(zed(268, 118, 32), 3), S(zed(346, 50, 44), 3)]
    pic(*s)


def reader_in_pillow():
    pillow = [(222, 262), (160, 228), (100, 214), (56, 220), (30, 262), (38, 330), (90, 392), (240, 404), (390, 392),
              (440, 340), (452, 262), (422, 214), (380, 220), (340, 250)]
    s = [S(pillow, 3),
         S([(203, 70), (214, 168), (222, 262)], 3), S([(318, 52), (330, 150), (340, 250)], 3),
         S(sharp([(203, 70), (318, 52)]), 3),
         S([(204, 118), (160, 170), (150, 214)], 2), S([(326, 100), (372, 150), (384, 200)], 2),
         S(ring(130, 316, 26, 9), 2)]
    s += [S(zed(380, 120, 18), 3), S(zed(414, 76, 26), 3), S(zed(450, 20, 24), 3)]
    pic(*s)


def dog_on_books():
    s = [S(sharp([(60, 392), (60, 432), (420, 432), (420, 392), (60, 392)])),
         S(sharp([(80, 352), (80, 392), (400, 392), (400, 352), (80, 352)])),
         S(sharp([(100, 316), (100, 352), (380, 352), (380, 316), (100, 316)])),
         S([(120, 316), (104, 276), (140, 236), (220, 222), (300, 228), (352, 252), (376, 290), (372, 316)]),
         S(ring(142, 280, 36, 34)),
         S([(120, 252), (92, 262), (84, 296), (104, 306)], 3),
         S(arc(146, 276, 9, 6, 15, 165, 8), 2),
         S([(372, 306), (408, 290), (430, 258), (418, 228)]),
         ]
    s += [S(zed(200, 150, 22)), S(zed(252, 100, 32)), S(zed(320, 40, 42), 3)]
    pic(*s)


def moon_yawn():
    cx, cy = 230, 255
    s = [S(ring(cx, cy, 136, 136)),
         S([(165, 196), (205, 214), (166, 232)], 3), S([(300, 196), (262, 214), (300, 232)], 3),
         S(ring(232, 288, 34, 44), 3), S(arc(232, 306, 22, 14, 10, 170, 8), 2),
         S([(166, 150), (230, 76), (350, 36), (348, 104), (318, 132)], 3), S(ring(360, 48, 16, 14), 2),
         ]
    pic(*s)


def torn_calendar():
    s = [S(sharp([(70, 84), (70, 392), (300, 392), (300, 84), (70, 84)])),
         S([(70, 150), (300, 150)], 2),
         S(ring(120, 76, 8, 14), 2), S(ring(250, 76, 8, 14), 2),
         S([(70, 220), (300, 220)], 1), S([(70, 300), (300, 300)], 1),
         S([(184, 150), (184, 392)], 1),
         S([(84, 164), (112, 204)], 2), S([(112, 164), (84, 204)], 2),
         S([(140, 164), (170, 204)], 2), S([(170, 164), (140, 204)], 2),
         S([(196, 164), (226, 204)], 2), S([(226, 164), (196, 204)], 2),
         S(sharp([(332, 150), (350, 134), (366, 156), (384, 130), (402, 154), (420, 128), (438, 152), (452, 140),
                  (440, 250), (430, 340), (338, 346), (332, 150)])),
         S([(350, 190), (420, 186)], 2), S([(350, 230), (410, 226)], 2),
         ]
    s += [S(zed(360, 380, 14), 3), S(zed(392, 360, 20), 3), S(zed(430, 330, 26), 3)]
    pic(*s)


def book_in_blanket():
    s = [S(sharp([(150, 70), (150, 340), (330, 340), (330, 70), (150, 70)])),
         S([(150, 70), (150, 56), (330, 56), (330, 70)], 2),
         S(arc(200, 160, 14, 8, 15, 165, 8), 2), S(arc(280, 160, 14, 8, 15, 165, 8), 2),
         S(ring(240, 200, 8, 10), 2),
         S([(190, 250), (290, 248)], 2),
         S([(120, 232), (150, 218), (190, 236), (230, 216), (270, 236), (310, 216), (350, 232), (372, 244),
            (372, 412), (120, 412), (120, 232)]),
         S([(220, 262), (226, 408)], 2),
         S([(150, 56), (205, 22), (270, 18), (330, 56)], 2), S(ring(282, 14, 10, 8), 2)]
    s += [S(zed(380, 150, 18)), S(zed(414, 100, 26)), S(zed(446, 40, 32), 3)]
    pic(*s)


def messy_bed():
    zig = [(120, 246)]
    for i in range(1, 11):
        zig.append((120 + i * 28, 290 if i % 2 else 244))
    s = [S(sharp([(40, 300), (440, 300), (440, 340), (40, 340), (40, 300)])),
         S([(60, 340), (60, 400)]), S([(420, 340), (420, 400)]),
         S(sharp([(28, 170), (28, 400), (70, 400), (70, 170), (28, 170)]), 3),
         S(zig, 3),
         S([(340, 300), (352, 350), (338, 378), (362, 398)], 3),
         S([(310, 404), (350, 372), (440, 360), (452, 400), (420, 422), (330, 424), (310, 404)], 3),
         S([(130, 392), (130, 430), (190, 430), (200, 416)], 3),
         S(sharp([(210, 392), (210, 430), (280, 430), (280, 392), (210, 392)]), 2)]
    pic(*s)


def smashed_alarm():
    cx, cy = 190, 250
    s = [S(ring(cx, cy, 112, 112)),
         S(ring(112, 112, 36, 30), 3), S(ring(268, 112, 36, 30), 3),
         S([(112, 80), (190, 60), (268, 80)], 3),
         S([(120, 350), (96, 400)]), S([(260, 350), (284, 400)]),
         S([(150, 215), (186, 251)], 3), S([(186, 215), (150, 251)], 3),
         S([(210, 215), (246, 251)], 3), S([(246, 215), (210, 251)], 3),
         S(arc(198, 285, 28, 16, 10, 170, 8), 2), S([(206, 296), (206, 318), (220, 316), (222, 296)], 2),
         S([(196, 140), (180, 180), (200, 200), (178, 236)], 2),
         S(sharp([(330, 20), (420, 4), (440, 70), (350, 88), (330, 20)]), 3),
         S([(390, 80), (300, 200), (292, 214)], 3),
         ]
    pic(*s)


for f in (bed_sleeper, reader_in_pillow, dog_on_books, moon_yawn, torn_calendar, book_in_blanket, messy_bed,
          smashed_alarm):
    f()

# ---------------------------------------------------------------- stream
def clamp_pts(pts):
    out = []
    for x, y in pts:
        out.append((2 * max(2, min(CW // 2 - 2, int(round(x / 2)))), 2 * max(2, min(CH // 2 - 2, int(round(y / 2))))))
    return out


def split_steps(pts):
    """Insert midpoints (on the 2 px grid) so no step is longer than int8 can hold."""
    out = [pts[0]]
    for p in pts[1:]:
        q = out[-1]
        k = max(1, -(-max(abs(p[0] - q[0]), abs(p[1] - q[1])) // 250))
        for j in range(1, k + 1):
            out.append((2 * ((q[0] + (p[0] - q[0]) * j // k) // 2), 2 * ((q[1] + (p[1] - q[1]) * j // k) // 2)))
    return out


def strokes_blob():
    raw = bytearray([len(PICS)])
    for p, strokes in enumerate(PICS):
        raw.append(len(strokes))
        for k, (pts, w, seed) in enumerate(strokes):
            pts = split_steps(clamp_pts(pts))
            assert 2 <= len(pts) <= 255, (p, k, len(pts))
            assert seed is None, 'the firmware derives the seed'
            seed = (p * 7 + k * 3 + 1) % 64
            assert len(expand(pts, seed)) <= 256, (p, k, 'too many points once wobbled')
            raw += struct.pack('<BBBB', len(pts), w, pts[0][0] // 2, pts[0][1] // 2)
            for a, b in zip(pts, pts[1:]):
                raw += struct.pack('<bb', (b[0] - a[0]) // 2, (b[1] - a[1]) // 2)
    return bytes(raw)


def text_blob(col):
    records = [(code, rest[col]) for code, *rest in LINES]
    for code, text in records:
        size = len(text.encode('utf-8'))
        assert size < SENTENCE_CAP, '%s line is %d UTF-8 bytes, exceeds the firmware sentence buffer' % (code, size)
    return ''.join(code + text + '\n' for code, text in records).encode('utf-8')


# ---------------------------------------------------------------- reference of the firmware's pen (preview only)
def wobble(seed, i, r):
    h = (seed * 2654435761 + i * 40503 + 12345) & 0xFFFFFFFF
    h ^= h >> 15
    h = (h * 2246822519) & 0xFFFFFFFF
    h ^= h >> 13
    return h % (2 * r + 1) - r


def tdiv(a, b):
    return int(a / b)


def expand(P, seed):
    out, idx = [P[0]], 0
    for s in range(len(P) - 1):
        dx, dy = P[s + 1][0] - P[s][0], P[s + 1][1] - P[s][1]
        k = max(1, max(abs(dx), abs(dy)) // STEP)
        for j in range(1, k):
            out.append((P[s][0] + tdiv(dx * j, k) + wobble(seed, 2 * idx, AMP), P[s][1] + tdiv(dy * j, k) + wobble(seed, 2 * idx + 1, AMP)))
            idx += 1
        out.append(P[s + 1])
    return out


def smooth(E):
    m, out = len(E), []
    for i in range(m - 1):
        p0, p1, p2, p3 = E[max(i - 1, 0)], E[i], E[i + 1], E[min(i + 2, m - 1)]
        for s in range(3):
            pt = []
            for a in (0, 1):
                q = 54 * p1[a] + (p2[a] - p0[a]) * s * 9 + (2 * p0[a] - 5 * p1[a] + 4 * p2[a] - p3[a]) * s * s * 3 + \
                    (-p0[a] + 3 * p1[a] - 3 * p2[a] + p3[a]) * s * s * s
                pt.append((q + 27) // 54)
            out.append(tuple(pt))
    out.append(E[-1])
    return out


def preview(outdir):
    from PIL import Image, ImageDraw
    sheet = Image.new('L', (CW * 4, CH * 2), 255)
    for p, strokes in enumerate(PICS):
        im = Image.new('L', (CW, CH), 255)
        d = ImageDraw.Draw(im)
        for k, (pts, w, seed) in enumerate(strokes):
            seed = (p * 7 + k * 3 + 1) % 64
            P = clamp_pts(pts)
            Q = smooth(expand(P, seed))
            d.line(Q, fill=0, width=w, joint='curve')
        sheet.paste(im, ((p % 4) * CW, (p // 4) * CH))
        im.save(Path(outdir) / ('pic%d.png' % p))
    sheet.save(Path(outdir) / 'sheet.png')


def emit(name, raw):
    z = zlib.compress(raw, 9)
    assert zlib.decompress(z) == raw
    out = ['inline constexpr uint8_t %s[] = {' % name]
    for i in range(0, len(z), 16):
        out.append('    ' + ', '.join('0x%02x' % b for b in z[i:i + 16]) + ',')
    out += ['};', 'inline constexpr unsigned %s_RAW = %d;' % (name, len(raw)), '']
    print(name, len(raw), '->', len(z), 'B')
    return out, len(z)


def main():
    args = sys.argv[1:]
    md = preview_dir = None
    if '--md' in args:
        i = args.index('--md'); md = args[i + 1]; del args[i:i + 2]
    if '--preview' in args:
        i = args.index('--preview'); preview_dir = args[i + 1]; del args[i:i + 2]
    out_dir = Path(args[0]) if args else ROOT / 'src/shells/ugly'
    out = ['// Generated by scripts/ugly/gen_sleep_set.py. Do not edit by hand.', '//',
           '// The sleep doodles as strokes and the sleep sentences, one zlib stream each.',
           '#pragma once', '', '#include <cstdint>', '', 'namespace ugly::sleepdata {', '']
    total = 0
    for name, raw in (('PICTURES', strokes_blob()), ('TEXT_VI', text_blob(0)), ('TEXT_EN', text_blob(1))):
        o, n = emit(name, raw)
        out += o
        total += n
    out += ['}  // namespace ugly::sleepdata', '']
    (out_dir / 'UglySleepData.h').write_text('\n'.join(out))
    print('total packed', total, 'B,', len(LINES), 'lines,', len(PICS), 'pictures')
    if preview_dir:
        Path(preview_dir).mkdir(parents=True, exist_ok=True)
        preview(preview_dir)
    if md:
        write_md(md)


NAMES = {'a': 'ngủ, chung', 'b': 'ngủ, hôm nay đọc 0 phút', 'd': 'ngủ, thói quen đọc đêm (23-5 giờ chiếm từ 40% thời gian đọc)',
         'f': 'ngủ lúc 0-5 giờ', 'g': 'ngủ lúc 5-9 giờ',
         'h': 'ngủ lúc 9-12 giờ', 'i': 'ngủ lúc 12-14 giờ', 'j': 'ngủ lúc 14-18 giờ', 'k': 'ngủ lúc 18-22 giờ', 'l': 'ngủ lúc 22-24 giờ'}


def write_md(path):
    rows = ['| Khoá | Điều kiện | Tiếng Việt | English |', '|---|---|---|---|']
    for i, (code, vi, en) in enumerate(LINES):
        rows.append('| %s%d | %s | %s | %s |' % (code.upper(), i + 1, NAMES[code], vi.replace('|', '/'), en.replace('|', '/')))
    Path(path).write_text('\n'.join(rows) + '\n')


if __name__ == '__main__':
    main()
