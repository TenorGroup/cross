"""The reference tenor/ink of the cover placeholder, written from the definition and not from the firmware code.

reduce_box(gray, w, h): the source is cut to the frame's shape (centred), each frame pixel is the
mean of the source pixel centres inside its cell, in the fixed point the cover pipeline uses.
tenor_ink4 / tenor_ink1: Atkinson on the sRGB code of the gray, thresholds 43/128/213, levels
0/85/170/255, error >> 3 to 6 neighbours, rows left to right.
"""
import struct
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
MASTER_PNG = HERE.parents[1] / 'scripts/brand-art/cover-holder/cover-holder-gray.png'
# Every frame size Home draws the placeholder at (anh58 measurement, simulator, 3 styles x 3 text sizes).
X4_SIZES = [(254, 383), (182, 274), (270, 407), (262, 397), (252, 382), (180, 271), (170, 257),
            (127, 191), (125, 188)]
X3_SIZES = [(298, 450), (230, 347)]


def master():
    return np.asarray(Image.open(MASTER_PNG).convert('L'), dtype=np.uint8)


def _f32(x):
    return float(np.float32(x))


def _span(center, step, size):
    radius = max(1.0, step) / 2
    lo = min(max(int(center - radius + 0.5), 0), size - 1)
    hi = min(max(int(center + radius + 0.5), lo + 1), size)
    reciprocal = 1.0 / max(1.0, step)
    if hi - lo > 1 and (lo - center + 0.5) * reciprocal <= -0.5:
        lo += 1
    if hi - lo > 1 and (hi - 1 - center + 0.5) * reciprocal > 0.5:
        hi -= 1
    return lo, hi


def reduce_box(gray, w, h):
    sh, sw = gray.shape
    scale = max(w / sw, h / sh)
    crop_w, crop_h = w / scale, h / scale
    left = _f32(max(0.0, (sw - crop_w) / 2))
    top = _f32(max(0.0, (sh - crop_h) / 2))
    right = _f32(min(float(sw), (sw + crop_w) / 2))
    bottom = _f32(min(float(sh), (sh + crop_h) / 2))
    step_x, step_y = (right - left) / w, (bottom - top) / h
    xb = np.array([_span(left + (x + 0.5) * step_x, step_x, sw) for x in range(w)], dtype=np.int64)
    x0, x1 = xb[:, 0], xb[:, 1]
    nx = x1 - x0
    cx = ((1 << 22) + nx // 2) // nx
    csum = np.concatenate([np.zeros((sh, 1), np.int64), np.cumsum(gray.astype(np.int64), axis=1)], axis=1)
    out = np.empty((h, w), np.int64)
    for y in range(h):
        y0, y1 = _span(top + (y + 0.5) * step_y, step_y, sh)
        ny = y1 - y0
        cy = ((1 << 22) + ny // 2) // ny
        total = np.zeros(w, np.int64)
        for sy in range(y0, y1):
            horizontal = csum[sy, x1] - csum[sy, x0]
            total += (horizontal * cx + (1 << 21)) >> 22
        out[y] = (total * cy + (1 << 21)) >> 22
    return out


def _atkinson(g, four):
    h, w = g.shape
    err = [[0] * (w + 4) for _ in range(3)]
    out = np.empty((h, w), np.uint8)
    for yy in range(h):
        e0, e1, e2 = err
        row = g[yy].tolist()
        o = [0] * w
        for x in range(w):
            v = row[x] + e0[x + 2]
            v = 0 if v < 0 else 255 if v > 255 else v
            if four:
                q = 0 if v < 43 else 85 if v < 128 else 170 if v < 213 else 255
            else:
                q = 255 if v >= 128 else 0
            o[x] = q
            e = (v - q) >> 3
            e0[x + 3] += e
            e0[x + 4] += e
            e1[x + 1] += e
            e1[x + 2] += e
            e1[x + 3] += e
            e2[x + 2] += e
        out[yy] = o
        err = [e1, e2, [0] * (w + 4)]
    return out


def tenor_ink4(g):
    return _atkinson(g, True)


def tenor_ink1(g):
    return _atkinson(g, False)


def expected(w, h, four):
    """The placeholder at w x h as 0/85/170/255 (or 0/255 for 1 bit)."""
    g = reduce_box(master(), w, h)
    return tenor_ink4(g) if four else tenor_ink1(g)


def read_bmp(path):
    """Pixels of a top-down 1 or 2 bit BMP as 0/85/170/255 (index * 85, or 0/255 for 1 bit)."""
    data = Path(path).read_bytes()
    offset = struct.unpack_from('<I', data, 10)[0]
    w, h = struct.unpack_from('<ii', data, 18)
    bpp = struct.unpack_from('<H', data, 28)[0]
    assert h < 0, 'top-down expected'
    h = -h
    stride = (w * bpp + 31) // 32 * 4
    out = np.empty((h, w), np.uint8)
    for y in range(h):
        row = np.frombuffer(data, np.uint8, stride, offset + y * stride)
        if bpp == 2:
            vals = np.stack([(row >> 6) & 3, (row >> 4) & 3, (row >> 2) & 3, row & 3], axis=1).reshape(-1)[:w]
            out[y] = vals * 85
        else:
            out[y] = np.unpackbits(row)[:w] * 255
    return out
