#!/usr/bin/env python3
"""Grey (unselected) art of the 40 px tab icons, cut out of the selected art of the same icon.

    python3 scripts/icons/xam.py            # rewrite scripts/icons/icon57/png/*-xam.png
    python3 scripts/icons/xam.py --check    # exit 1 when a grey PNG differs from what this makes

A grey icon is the selected icon with 1 lane of every stroke taken away, so it is a solid 2 px line:
- it lies inside the selected ink, so it is never bigger (the icons drawn by tron.py may differ by 1 px);
- its ink box is the selected ink box, pixel for pixel (the lane left is always the outer one);
- it has no checkerboard: every edge is a clean run of ink, which is what a 1 bit panel draws sharply.
Which lane goes is decided per pixel from the unbroken ink run across the stroke: the lane that faces the
centre of the ink box. A run that is 4 wide and straddles that centre evenly loses both its outer lanes, so the
2 px that stay are centred and a mirrored icon stays mirrored. A stroke of 4 (the older shapes in hinh.py) goes
through this twice.
The clock, the sliders, the globe, the info ring, the dots of "more", the arcs of the gesture icon and the keyboard
are drawn from true circles and lines by tron.py instead (REDRAW below), so they are round and straight.
HAND holds the few pixels the rule cannot place (a dot, a stem end): ('del', x, y) or ('add', x, y), counted from
the top left of the 40 px image, checked against the selected ink.
"""
import os
import sys

from PIL import Image

import tron

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PNG = os.path.join(ROOT, 'scripts', 'icons', 'icon57', 'png')
N = 40
THIN = 5   # longest ink run across a stroke: stroke 4 on a diagonal is 6 px along a row

# Icons whose solid dots (the dot of the i) stay solid but 1 px smaller each side; other dots become 1 px rings.
SOLID_DOTS = {'thong_tin'}
# Pixels the lane rule cannot place, by icon name: end nubs it leaves (the corner of a rounded bar end) and the
# stem of the pilcrow, whose rounded foot sits on the left lane.
HAND = {
    'thiet_bi': [('del', 15, 28), ('del', 24, 28)],
    'thanh_trang_thai': [('del', 18, 27), ('del', 21, 27)],
    'bo_cuc': [('del', 24, 32)],
    'he_thong_b': [('del', 5, 25), ('del', 34, 25)],
    'font': [('del', 3, 31), ('del', 36, 31)],
    'kieu': [(kind, x, y) for y in range(7, 35) for kind, x in (('del', 29), ('add', 27))],
}


def _info(base):
    """The info icon: the lane rule keeps the dot and the stem, the ring is drawn."""
    ring = set().union(*[part for part in _parts(base) if _holes(part)])
    return (base - ring) | tron.ring(20, 20, 17, 2)


def _gesture(base):
    """The gesture icon: the pill stays as the lane rule leaves it, the two side arcs are drawn."""
    arcs = set().union(*[part for part in _parts(base) if box(part)[2] < 14 or box(part)[0] > 25])
    return (base - arcs) | tron.side_arcs()


# Icons drawn from true circles, lines and rounded rectangles (tron.py); each gets the lane-rule ink to start from.
REDRAW = {
    'gan_day': lambda base: tron.clock(),
    'cai_dat': lambda base: tron.sliders(),
    'ngon_ngu': lambda base: tron.globe(),
    'ban_phim': lambda base: tron.keyboard(),
    'them': lambda base: tron.dots(),
    'thong_tin': _info,
    'cu_chi': _gesture,
}


def ink_of(image):
    """Set of (x, y) of every ink (black) pixel of a 40 px image."""
    pixels = image.convert('L').load()
    return {(x, y) for y in range(N) for x in range(N) if pixels[x, y] == 0}


def image_of(ink):
    image = Image.new('1', (N, N), 1)
    pixels = image.load()
    for x, y in ink:
        pixels[x, y] = 0
    return image


def box(ink):
    xs = [x for x, _ in ink]
    ys = [y for _, y in ink]
    return min(xs), min(ys), max(xs), max(ys)


def mirrored(ink):
    return all((N - 1 - x, y) in ink for x, y in ink)


def _runs(ink, along_x):
    """(first, last) of the unbroken ink run through each ink pixel, along a row or along a column."""
    out = {}
    for line in range(N):
        start = None
        for k in range(N + 1):
            on = k < N and ((k, line) in ink if along_x else (line, k) in ink)
            if on and start is None:
                start = k
            if not on and start is not None:
                for j in range(start, k):
                    out[(j, line) if along_x else (line, j)] = (start, k - 1)
                start = None
    return out


def _inner(lo, hi, centre, at):
    """True when the pixel at `at` of the run lo..hi is a lane to take away."""
    length = hi - lo + 1
    if length < 3:
        return False
    if centre > hi:
        return at == hi
    if centre < lo:
        return at == lo
    if length == 4 and lo + hi == 2 * centre:
        return at in (lo, hi)
    return at == hi


def _parts(ink):
    """Connected pieces (8 neighbours) of the ink, each a set of (x, y)."""
    left, out = set(ink), []
    while left:
        stack, part = [left.pop()], set()
        while stack:
            x, y = stack.pop()
            part.add((x, y))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if (x + dx, y + dy) in left:
                        left.discard((x + dx, y + dy))
                        stack.append((x + dx, y + dy))
        out.append(part)
    return out


def _holes(part):
    """True when the piece closes off some white pixels (4 neighbours): a ring or a frame."""
    left, top, right, bottom = box(part)
    seen, stack = set(), [(left - 1, top - 1)]
    while stack:
        x, y = stack.pop()
        if (x, y) in seen or (x, y) in part or not (left - 1 <= x <= right + 1 and top - 1 <= y <= bottom + 1):
            continue
        seen.add((x, y))
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return (right - left + 3) * (bottom - top + 3) - len(seen) - len(part) > 0


def _blob(part):
    """A solid dot: 5 px or more each way, nothing closed off inside, nearly every pixel of its box inked."""
    left, top, right, bottom = box(part)
    wide, high = right - left + 1, bottom - top + 1
    return min(wide, high) >= 5 and max(wide, high) <= 8 and len(part) >= 0.7 * wide * high and not _holes(part)


def _pass(ink, centre):
    cx, cy = centre
    across_x, across_y = _runs(ink, True), _runs(ink, False)
    out = set(ink)
    for x, y in ink:
        (xlo, xhi), (ylo, yhi) = across_x[(x, y)], across_y[(x, y)]
        # Only a direction that crosses the stroke counts: along it the ink runs on, 6 px or more.
        votes = []
        if xhi - xlo + 1 <= THIN:
            votes.append(_inner(xlo, xhi, cx, x))
        if yhi - ylo + 1 <= THIN:
            votes.append(_inner(ylo, yhi, cy, y))
        if bool(votes) and all(votes):
            out.discard((x, y))
    return out


def _small(part):
    """A dot of 4 px or less each way (a key, the centre of the pad): too small to thin, kept as drawn."""
    left, top, right, bottom = box(part)
    return right - left < 4 and bottom - top < 4 and not _holes(part)


def _outline(part, inside=False):
    """The pixels of a piece that touch white (4 neighbours), or with inside=True the ones that do not."""
    return {(x, y) for x, y in part
            if any((x + dx, y + dy) not in part for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))) != inside}


def grey(name, selected, passes=1):
    """Grey ink of the icon `name` from its selected ink."""
    left, top, right, bottom = box(selected)
    whole = ((left + right) / 2, (top + bottom) / 2)
    ink = set()
    for part in _parts(selected):
        if _small(part):
            ink |= part
            continue
        if _blob(part):
            ink |= _outline(part, name in SOLID_DOTS)
            continue
        # A ring or a frame loses the lane that faces its own centre; a stroke that closes nothing,
        # the lane that faces the centre of the whole icon.
        left, top, right, bottom = box(part)
        centre = ((left + right) / 2, (top + bottom) / 2) if _holes(part) else whole
        piece = part
        for _ in range(passes):
            piece = _pass(piece, centre)
        ink |= piece
    for kind, x, y in HAND.get(name, ()):
        assert (x, y) in selected, (name, kind, x, y)
        (ink.add if kind == 'add' else ink.discard)((x, y))
    if name in REDRAW:
        ink = REDRAW[name](ink)
        assert box(ink) == box(selected), (name, box(ink), box(selected))
    return ink


def grey_image(name, selected_image, passes=1):
    return image_of(grey(name, ink_of(selected_image), passes))


def main():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import sinh
    check = '--check' in sys.argv[1:]
    bad = 0
    for name in sinh.ICON57_NAMES:
        with Image.open(os.path.join(PNG, f'{name}-chon.png')) as selected:
            made = grey_image(name, selected)
        path = os.path.join(PNG, f'{name}-xam.png')
        if check:
            with Image.open(path) as have:
                if have.convert('1').tobytes() != made.tobytes():
                    print('differs:', os.path.relpath(path, ROOT))
                    bad += 1
        else:
            made.save(path)
            print('wrote', os.path.relpath(path, ROOT))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
