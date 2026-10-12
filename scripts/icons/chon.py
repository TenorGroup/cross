#!/usr/bin/env python3
"""Selected (black, stroke 3) art of the icons whose circles and lines are drawn true: tron.py geometry at width 3.

    python3 scripts/icons/chon.py            # rewrite scripts/icons/icon57/png/<name>-chon.png of these 7 icons
    python3 scripts/icons/chon.py --check    # exit 1 when a PNG differs from what this makes

Every icon keeps the ink box of the approved art. The pieces that are not circles (the stem and dot of the i,
the pill of the gesture icon) stay as they are in the PNG; running this twice changes nothing.
"""
import os
import sys

from PIL import Image

import tron
import xam

PNG = xam.PNG


def _without(ink, drop):
    return ink - set().union(*[part for part in xam._parts(ink) if drop(part)])


def art(name, current):
    """Ink of the selected icon `name`, from its current ink (the parts that are kept)."""
    if name == 'gan_day':
        return tron.ring(20, 20, 17, 3) | tron.stroke([(20.5, 13), (20.5, 20.5), (24.5, 24.5)], 3)
    if name == 'cai_dat':
        # 13 px knobs: a 3 px line is centred on the middle row of a knob of an odd height
        return (tron.ring(29.5, 11.5, 6.5, 3) | tron.ring(10.5, 28.5, 6.5, 3)
                | tron.rect(4, 10, 20, 12) | tron.rect(19, 27, 35, 29))
    if name == 'ngon_ngu':
        return (tron.ring(20, 20, 17, 3) | (tron.rect(3, 19, 36, 21) & tron.disc(20, 20, 17))
                | (tron.ellipse_band(20, 20, 5, 15, 3) & tron.disc(20, 20, 17)))
    if name == 'thong_tin':
        return _without(current, xam._holes) | tron.ring(20, 20, 17, 3)
    if name == 'ban_phim':
        keys = set()
        for left in (9, 15, 21, 27):
            for top in (13, 19):
                keys |= tron.rect(left, top, left + 3, top + 3)
        return tron.rrect_ring(3, 6, 36, 33, 6, 3) | keys | tron.rect(12, 25, 27, 27)
    if name == 'them':
        return tron.disc(8, 20, 3) | tron.disc(20, 20, 3) | tron.disc(32, 20, 3)
    if name == 'cu_chi':
        arcs = set().union(*[p for p in xam._parts(current) if xam.box(p)[2] < 14 or xam.box(p)[0] > 25])
        right = tron.ring(18, 19, 20, 3) & tron.rect(26, 8, 39, 29)
        return (current - arcs) | right | {(tron.N - 1 - x, y) for x, y in right}
    raise KeyError(name)


NAMES = ('gan_day', 'cai_dat', 'ngon_ngu', 'thong_tin', 'ban_phim', 'them', 'cu_chi')


def main():
    check = '--check' in sys.argv[1:]
    bad = 0
    for name in NAMES:
        path = os.path.join(PNG, f'{name}-chon.png')
        with Image.open(path) as image:
            current = xam.ink_of(image)
            ink = art(name, current)
            assert xam.box(ink) == xam.box(current), (name, xam.box(ink), xam.box(current))
            made = xam.image_of(ink)
            if check:
                if image.convert('1').tobytes() != made.tobytes():
                    print('differs:', os.path.relpath(path, xam.ROOT))
                    bad += 1
                continue
        made.save(path)
        print('wrote', os.path.relpath(path, xam.ROOT))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
