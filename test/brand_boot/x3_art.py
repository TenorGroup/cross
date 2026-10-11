"""X3 boot and sleep art in X3BrandAssets.h must equal the BMPs in scripts/brand-art/x3/bmp,
the boot lockup must carry the 100/50/30 tones (cross / tenor / slash), and the one bit sleep
frame in ManNguTenor.h must equal the sleep BMP folded the way SleepGrayPlanes::show folds it."""
import re
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / 'scripts/brand-art/x3/bmp'
HEADER = (ROOT / 'src/components/X3BrandAssets.h').read_text()
W, H = 528, 792
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def bmp_gray(path):
    """Rows (top first) of an uncompressed 8-bit palette BMP, as gray values."""
    b = path.read_bytes()
    off, w, h, bpp = (int.from_bytes(b[o:o + n], 'little') for o, n in ((10, 4), (18, 4), (22, 4), (28, 2)))
    assert (w, h, bpp) == (W, H, 8), (path, w, h, bpp)
    pal = [b[54 + 4 * i] for i in range(256)]
    stride = (w + 3) // 4 * 4
    return [[pal[v] for v in b[off + (h - 1 - y) * stride:off + (h - 1 - y) * stride + w]] for y in range(h)]


def plane(name):
    data = re.search(r'\b' + name + r'\[\] = \{(.*?)\};', HEADER, re.S).group(1)
    raw = zlib.decompress(bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', data)))
    crc = int(re.search(name + r'_CRC = 0x([0-9a-f]{8})', HEADER).group(1), 16)
    assert zlib.crc32(raw) == crc, name + ' CRC'
    return [(raw[i // 8] >> (7 - i % 8)) & 1 for i in range(len(raw) * 8)]


def decode(prefix):
    """Native planes back to the portrait image (inverse of scripts/pack_x3_brand.py)."""
    lsb, msb = plane(prefix + '_LSB'), plane(prefix + '_MSB')
    # Native pixel (row r, column c) holds portrait pixel (x = W-1-r, y = c).
    return [[(lsb[(W - 1 - x) * H + y] | msb[(W - 1 - x) * H + y] << 1) * 85 for x in range(W)] for y in range(H)]


def folded_frame():
    """mannogu::KHUNG inflated: native 1 bit framebuffer, 528 rows of 792 pixels, bit 7 first."""
    data = re.search(r'KHUNG\[\] = \{(.*?)\};', (ROOT / 'src/components/ManNguTenor.h').read_text(), re.S).group(1)
    raw = zlib.decompress(bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', data)))
    assert len(raw) == W * H // 8, len(raw)
    return [(raw[i // 8] >> (7 - i % 8)) & 1 for i in range(len(raw) * 8)]


def fold_diff(frame, bmp):
    """Pixels where the frame differs from the sleep BMP folded to black and white: gray level 1
    is white where the Bayer cell holds under 4, level 2 where it holds under 12 (the cuts of
    SleepGrayPlanes.cpp), level 0 black and level 3 white."""
    diff = 0
    for y in range(H):
        for x in range(W):
            r, c = W - 1 - x, y  # native row and column of portrait pixel (x, y)
            cut = (0, 4, 12, 16)[bmp[y][x] // 85]
            diff += frame[r * H + c] != (BAYER[r & 3][c & 3] < cut)
    return diff


def bayer(tone):
    """Firmware four-level Bayer response of a flat tone, averaged over one 4x4 cell."""
    out = []
    for row in BAYER:
        for h in row:
            adj = min(255, max(0, tone + (h - 8) * 5))
            out.append(0 if adj < 64 else 85 if adj < 128 else 170 if adj < 192 else 255)
    return sum(out) / 16


def main():
    bad = 0
    for prefix, name in (('BOOT', 'khoi-dong'), ('SLEEP', 'muc-dang')):
        bmp = bmp_gray(ART / f'261011_tenor_branding_xteink-x3-cross-{name}.bmp')
        sleep = bmp if prefix == 'SLEEP' else None
        diff = sum(a != b for ra, rb in zip(decode(prefix), bmp) for a, b in zip(ra, rb))
        print(f'{prefix}: {diff} pixels differ from the BMP')
        bad += diff != 0
    fold = fold_diff(folded_frame(), sleep)
    print(f'KHUNG: {fold} pixels differ from the sleep BMP folded to black and white')
    bad += fold != 0
    boot = decode('BOOT')
    # 4x4 windows fully inside one glyph stroke (any 4x4 window holds each Bayer threshold once).
    mean = {k: sum(sum(r[x:x + 4]) for r in boot[y:y + 4]) / 16 for k, (y, x) in
            {'tenor': (160, 166), 'slash': (156, 259), 'cross': (161, 323)}.items()}
    want = {'tenor': bayer(0x80), 'slash': bayer(0x4D), 'cross': bayer(0xFF)}
    print('lockup mean gray', mean, 'expected', want)
    print('ratio to cross: tenor %.3f slash %.3f' % (mean['tenor'] / mean['cross'], mean['slash'] / mean['cross']))
    for k in mean:
        bad += abs(mean[k] - want[k]) > 4
    bad += not (0.45 <= mean['tenor'] / mean['cross'] <= 0.55 and mean['slash'] < mean['tenor'] * 0.5)
    print('RED' if bad else 'GREEN')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
