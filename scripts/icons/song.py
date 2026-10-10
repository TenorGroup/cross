"""Wi-Fi signal marks for the network list (10/10/2026): 4 levels and the padlock, 1 bit, drawn by net.py.

The wave is the 'wifi' icon of hinh.py (dot + 3 arcs) at stroke 2; a level draws its parts in ink and the rest
as the grey checkerboard (x + y even, the rule of the unselected icons) with the short-run compensation:
a straight run of 3 or more ink pixels that the checkerboard leaves with 1 or none gets 1 neighbour back.
Both marks are cropped to their ink box: 22 x 18 and 12 x 16.
"""
from PIL import Image, ImageChops

from net import G

N, W = 24, 2
CY = 19                    # centre of the arcs on the 24 grid: the wave's ink box is 18 high, 3 clear above and below
RADII = (5, 10, 15)        # one arc per level above the dot, 5 apart so the 2 px strokes keep a 3 px gap


def _ink(im):
    px = im.load()
    return {(x, y) for y in range(im.height) for x in range(im.width) if px[x, y] == 0}


def _grey(ink):
    keep = {p for p in ink if (p[0] + p[1]) % 2 == 0}
    for across in (True, False):
        lines = {}
        for x, y in ink:
            lines.setdefault(y if across else x, []).append(x if across else y)
        for k, vals in lines.items():
            vals.sort()
            runs, run = [], [vals[0]]
            for v in vals[1:]:
                if v == run[-1] + 1:
                    run.append(v)
                else:
                    runs.append(run)
                    run = [v]
            runs.append(run)
            for r in runs:
                if len(r) < 3:
                    continue
                kept = [v for v in r if ((v, k) if across else (k, v)) in keep]
                if len(kept) <= 1:
                    back = r[1] if not kept else (kept[0] + 1 if kept[0] + 1 in r else kept[0] - 1)
                    keep.add((back, k) if across else (k, back))
    return keep


def _crop(im):
    return im.crop(ImageChops.invert(im.convert('L')).getbbox())


def wave(level):
    """level 0..4: the dot is level 1, the arcs from the inside are levels 2 to 4. Parts above the level are grey."""
    out = Image.new('1', (N, N), 1)
    px = out.load()
    for part in range(4):
        g = G(N, W)
        if part == 0:
            g.dot(12, CY, 2.0)
        else:
            g.arc(12, CY, RADII[part - 1], 225, 315)
        ink = _ink(g.img())
        for p in (ink if part < level else _grey(ink)):
            px[p] = 0
    return _crop(out)


def lock():
    g = G(N, W)
    g.rrect(7, 11, 17, 19, r=2)
    g.rpoly((9, 11), (9, 5), (15, 5), (15, 11), r=3)
    g.dot(12, 15, 1.0)
    return _crop(g.img())
