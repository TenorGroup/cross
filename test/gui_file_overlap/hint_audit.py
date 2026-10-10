"""Nhan dai nut X3 tren anh 1 bit (muc that): nhan khong cach pin hoac dong ho duoi 6 px, khong dinh thanh path.
Dilation 7x7 gop muc cach <= 5 px thanh mot cum; cum chua pin, cum chua dong ho, cum chu qua day thanh la loi."""
import re

import numpy as np
from scipy import ndimage as ndi

BAT_END = {0: 65, 1: 73, 2: 79}   # x cuoi cua so pin '100', do tren man khong co nhan nut
CLOCK_W = {0: 48, 1: 58, 2: 69}   # be rong lon nhat cua dong ho 'HH:MM'
S7 = np.ones((7, 7), bool)
S8 = np.ones((3, 3), bool)


def bar_of(log):
    i = log.rfind('[X3PATH]')
    if i < 0:
        return None
    return tuple(map(int, re.search(r'bar=(\d+),(\d+),(\d+),(\d+)', log[i:]).groups()))


def audit(img, tier, bar):
    """Tra ve list (loai, mo ta, hop). bar = (x, y, w, h) cua thanh path, chi de dinh vi; None (man Home, thanh 5 the)
    thi chi do pin va dong ho."""
    M = np.array(img.convert('L')) < 128
    H, W = M.shape
    by, bh = (H - 100, 0) if bar is None else (bar[1], bar[3])
    foot = by + bh
    R = M.copy()
    R[:by - 4] = False
    R[H - 3:] = False  # gach duoi nhan
    cc, n = ndi.label(ndi.binary_dilation(R, structure=S7), structure=S8)
    ink = np.where(R, cc, 0)
    objs = ndi.find_objects(cc)

    def box(i):
        sl = objs[i - 1]
        ys, xs = np.where(ink[sl] == i)
        return [int(sl[1].start + xs.min()), int(sl[0].start + ys.min()), int(sl[1].start + xs.max() + 1),
                int(sl[0].start + ys.max() + 1)]
    bat = set(np.unique(ink[H - 24:H - 4, 8:BAT_END[tier] + 1])) - {0}
    clk = set(np.unique(ink[H - 24:H - 4, W - CLOCK_W[tier] - 8:W - 8])) - {0}
    out = []
    for i in bat:
        b = box(i)
        if b[2] > BAT_END[tier] + 1 or b[1] < H - 30:
            out.append(('pin', 'nhan cach so pin duoi 6 px', b))
    for i in clk - bat:
        b = box(i)
        if b[0] < W - 8 - CLOCK_W[tier] - 2 or b[1] < H - 30:
            out.append(('dong-ho', 'nhan cach dong ho duoi 6 px', b))
    for i in set(range(1, n + 1)) - bat - clk:
        if not (ink == i).any():
            continue
        b = box(i)
        if bar is not None and b[1] < foot - 3 and b[3] > foot + 1:
            out.append(('thanh', f'chu dinh day thanh y {foot}', b))
    return out
