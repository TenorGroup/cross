"""tenor/ink four-level Atkinson quantizer used for the X3 lockup and mark.

Recipe of lib/JpegToBmpConverter/TenorInk.cpp and AtkinsonDitherer (BitmapHelpers.h): luma
already box-filtered to panel size, then Atkinson error diffusion on the sRGB values with
thresholds 43/128/213, levels 0/85/170/255, error >> 3 sent to 6 neighbours, left to right
on every row.

Parity with the C++ kernel (tenorink::writeCover, test/tenor_ink/Kernel.cpp) was measured on the
scale 4 sleep shot: BOX to 528x792 in Python then tenor_ink4 equals the kernel output on the
full 2112x3168 gray, 0 of 418176 pixels differ (11/10/2026). The firmware test
(test/brand_boot/x3_art.py) only checks that X3BrandAssets.h equals the BMPs of this folder.
"""
import numpy as np


def tenor_ink4(g):
    """g: uint8 [h][w] luma at panel size. Returns uint8 with levels 0/85/170/255."""
    h, w = g.shape
    er = [[0] * (w + 4) for _ in range(3)]
    out = np.empty((h, w), np.uint8)
    for yy in range(h):
        e0, e1, e2 = er
        row = g[yy].tolist()
        o = [0] * w
        for x in range(w):
            v = row[x] + e0[x + 2]
            v = 0 if v < 0 else 255 if v > 255 else v
            q = 0 if v < 43 else 85 if v < 128 else 170 if v < 213 else 255
            o[x] = q
            e = (v - q) >> 3
            e0[x + 3] += e; e0[x + 4] += e
            e1[x + 1] += e; e1[x + 2] += e; e1[x + 3] += e
            e2[x + 2] += e
        out[yy] = o
        er = [e1, e2, [0] * (w + 4)]
    return out
