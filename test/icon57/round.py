"""Tab icons (grey and selected): circles round, lines straight, keys in a grid.

Run `python3 test/icon57/round.py`. Reads the grey bitmap the way scripts/icons/sinh.py hands it to the headers and fails when
- a ring (the clock, the info ring, the globe, the two knobs of the sliders) is not square, has an edge midpoint more than
  0,5 px from its circle, is thinner than 1,8 px or thicker than 2,4 px on average (3 px strokes: 2,8 to 3,4), or is not the same in all 8 mirrorings;
- the globe is not mirrored in x and in y;
- an arc beside the gesture pill has an edge midpoint more than 0,5 px from its circle (radius 20 outside, 18 inside);
- the clock hand has a vertical piece that is not 1 unbroken pair of columns, or a diagonal piece that is not 45 degrees;
- the lines of the sliders are not 2 rows of equal length, or the lines are not centred on their knobs;
- the keys of the keyboard are not 8 equal squares in 2 rows and 4 columns with equal gaps, or the space bar is not 2 rows centred.
"""
import math
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'scripts/icons'))
import sinh
import xam

failures = []
checks = 0


def grey(name):
    return xam.ink_of(sinh.draw(name, 40, 3, False))


def check(ok, text):
    global checks
    checks += 1
    if not ok:
        failures.append(text)


def edges(part):
    out = []
    for x, y in part:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if (x + dx, y + dy) not in part:
                out.append((x + 0.5 + dx / 2, y + 0.5 + dy / 2, (x + dx, y + dy)))
    return out


def width_height(part):
    left, top, right, bottom = xam.box(part)
    return right - left + 1, bottom - top + 1


def ring_report(name, part, with_inner=True, full=True, thick=(1.8, 2.4)):
    """Outer and inner edge deviation from the circle, thickness, and 8-fold mismatch of one closed ring."""
    left, top, right, bottom = xam.box(part)
    wide, high = width_height(part)
    check(wide == high, f'{name}: ring {wide}x{high} is not square')
    cx, cy, radius = (left + right + 1) / 2, (top + bottom + 1) / 2, wide / 2
    outside, stack = set(), [(left - 1, top - 1)]
    while stack:
        p = stack.pop()
        if p in outside or p in part or not (left - 1 <= p[0] <= right + 1 and top - 1 <= p[1] <= bottom + 1):
            continue
        outside.add(p)
        stack += [(p[0] + 1, p[1]), (p[0] - 1, p[1]), (p[0], p[1] + 1), (p[0], p[1] - 1)]
    outer = [math.hypot(x - cx, y - cy) for x, y, q in edges(part) if q in outside]
    inner = [math.hypot(x - cx, y - cy) for x, y, q in edges(part) if q not in outside]
    check(max(abs(v - radius) for v in outer) <= 0.5,
          f'{name}: outer edge is up to {max(abs(v - radius) for v in outer):.2f} px from its circle')
    if with_inner:
        mean = sum(inner) / len(inner)
        check(max(abs(v - mean) for v in inner) <= 0.5,
              f'{name}: inner edge is up to {max(abs(v - mean) for v in inner):.2f} px from its circle')
        check(thick[0] <= radius - mean <= thick[1], f'{name}: ring is {radius - mean:.2f} px thick on average')
    if full:
        bad = sum(1 for x, y in part
                  if any((math.floor(qx + cx), math.floor(qy + cy)) not in part
                         for qx, qy in ((-(x + 0.5 - cx), y + 0.5 - cy), (x + 0.5 - cx, -(y + 0.5 - cy)),
                                        (y + 0.5 - cy, x + 0.5 - cx))))
        check(bad == 0, f'{name}: {bad} pixels of the ring differ from their mirror image')


def closed(ink, size):
    return [part for part in xam._parts(ink) if xam._holes(part) and width_height(part) == size]


def arc_report(name, part, centre, outer_radius, inner_radius):
    """Edge midpoints of an arc against its 2 circles (the flat cuts at both ends of the arc are not part of them)."""
    left, top, right, bottom = xam.box(part)
    for side, radius in (('convex', outer_radius), ('concave', inner_radius)):
        own = [(x, y) for x, y, q in edges(part) if y not in (top, bottom + 1)
               and (math.hypot(x - centre[0], y - centre[1]) >= (outer_radius + inner_radius) / 2) == (side == 'convex')]
        worst = max(abs(math.hypot(x - centre[0], y - centre[1]) - radius) for x, y in own)
        check(worst <= 0.5, f'{name}: {side} edge is up to {worst:.2f} px from its circle of radius {radius}')


for name in ('gan_day', 'thong_tin'):
    ink = grey(name)
    found = closed(ink, (34, 34))
    check(len(found) == 1, f'{name}: expected 1 closed ring of 34x34, found {len(found)}')
    for part in found:
        ring_report(name, part)

ink = grey('ngon_ngu')
found = closed(ink, (34, 34))
check(len(found) == 1, f'ngon_ngu: expected 1 closed ring of 34x34, found {len(found)}')
for part in found:
    ring_report('ngon_ngu', part, with_inner=False, full=False)
check(all((39 - x, y) in ink and (x, 39 - y) in ink for x, y in ink), 'ngon_ngu: globe is not mirrored in x and in y')

ink = grey('cai_dat')
knobs = closed(ink, (12, 12))
check(len(knobs) == 2, f'cai_dat: expected 2 round knobs of 12x12, found {len(knobs)}')
for part in knobs:
    ring_report('cai_dat knob', part)
lines = [part for part in xam._parts(ink) if not xam._holes(part) and width_height(part)[0] >= 15]
check(len(lines) == 2 and all(width_height(p)[1] == 2 and len(p) == 2 * width_height(p)[0] for p in lines),
      'cai_dat: the 2 lines are not 2 rows of equal length')
for part in lines:
    for knob in knobs:
        if abs((xam.box(part)[1] + xam.box(part)[3]) - (xam.box(knob)[1] + xam.box(knob)[3])) < 3:
            check(xam.box(part)[1] + xam.box(part)[3] == xam.box(knob)[1] + xam.box(knob)[3],
                  'cai_dat: a line is not centred on its knob')

ink = grey('cu_chi')
arcs = [part for part in xam._parts(ink) if xam.box(part)[2] < 14 or xam.box(part)[0] > 25]
check(len(arcs) == 2, f'cu_chi: expected 2 arcs, found {len(arcs)}')
for part in arcs:
    arc_report('cu_chi arc', part, (18, 19) if xam.box(part)[0] > 25 else (22, 19), 20, 18)

ink = grey('them')
dots = xam._parts(ink)
check(len(dots) == 3 and all(width_height(p) == (6, 6) for p in dots), 'them: expected 3 rings of 6x6')
for part in dots:
    ring_report('them dot', part, with_inner=False)

ink = grey('gan_day')
hands = [part for part in xam._parts(ink) if not xam._holes(part)]
check(len(hands) == 1, f'gan_day: expected 1 hand, found {len(hands)}')
if hands:
    rows = {}
    for x, y in hands[0]:
        rows.setdefault(y, []).append(x)
    runs = [(y, min(v), max(v)) for y, v in sorted(rows.items())]
    check(all(max(v) - min(v) + 1 == len(v) for v in rows.values()), 'gan_day: a row of the hand has a gap')
    vertical = [r for r in runs if r[1:] == runs[0][1:]]
    check(len(vertical) >= 6 and vertical == runs[:len(vertical)] and vertical[0][2] - vertical[0][1] == 1,
          'gan_day: the upright piece of the hand is not 1 unbroken pair of columns')
    slant = runs[len(vertical):]
    check(len(slant) >= 4, 'gan_day: no diagonal piece')
    check(slant[0][1] == vertical[-1][1] and all(b[1] - a[1] == 1 for a, b in zip(slant, slant[1:])),
          'gan_day: the diagonal piece of the hand is not 45 degrees')

ink = grey('ban_phim')
parts = xam._parts(ink)
keys = [p for p in parts if not xam._holes(p) and width_height(p)[0] <= 4]
bars = [p for p in parts if not xam._holes(p) and width_height(p)[0] > 8]
check(len(keys) == 8 and {width_height(p) for p in keys} == {(3, 3)} and all(len(p) == 9 for p in keys),
      f'ban_phim: expected 8 keys of 3x3, found {len(keys)} ({sorted({width_height(p) for p in keys})})')
if len(keys) == 8:
    lefts = sorted({xam.box(p)[0] for p in keys})
    tops = sorted({xam.box(p)[1] for p in keys})
    check(len(lefts) == 4 and len(tops) == 2, f'ban_phim: keys are not in 4 columns and 2 rows ({lefts}, {tops})')
    check(len({b - a for a, b in zip(lefts, lefts[1:])}) == 1 and len({b - a for a, b in zip(tops, tops[1:])}) <= 1,
          f'ban_phim: key pitch is not even ({lefts}, {tops})')
    check(all(len([p for p in keys if xam.box(p)[0] == left]) == 2 and len([p for p in keys if xam.box(p)[1] == top]) == 4
              for left in lefts for top in tops), 'ban_phim: keys are not on a grid')
check(len(bars) == 1 and width_height(bars[0])[1] == 2 and len(bars[0]) == 2 * width_height(bars[0])[0],
      'ban_phim: space bar is not 2 rows of equal length')
check(all((39 - x, y) in ink for x, y in ink), 'ban_phim: not mirrored in x')

# The selected (black) art of the same icons: stroke 3, same circles and lines.
def selected(name):
    return xam.ink_of(sinh.draw(name, 40, 4, True))


THICK3 = (2.8, 3.4)
for name in ('gan_day', 'thong_tin'):
    found = closed(selected(name), (34, 34))
    check(len(found) == 1, f'{name} selected: expected 1 closed ring of 34x34, found {len(found)}')
    for part in found:
        ring_report(f'{name} selected', part, thick=THICK3)
found = closed(selected('ngon_ngu'), (34, 34))
check(len(found) == 1, 'ngon_ngu selected: expected 1 closed ring of 34x34')
for part in found:
    ring_report('ngon_ngu selected', part, with_inner=False, full=False)
knobs = closed(selected('cai_dat'), (13, 13))
check(len(knobs) == 2, f'cai_dat selected: expected 2 round knobs of 13x13, found {len(knobs)}')
for part in knobs:
    ring_report('cai_dat selected knob', part, thick=THICK3)
ink = selected('gan_day')
hands = [part for part in xam._parts(ink) if not xam._holes(part)]
rows = {}
for x, y in hands[0]:
    rows.setdefault(y, []).append(x)
starts = [min(v) for _, v in sorted(rows.items())]
vertical = [s for s in starts if s == starts[len(starts) // 3]]
slant = starts[starts.index(vertical[0]) + len(vertical) - 1:]
check(len(hands) == 1 and len(vertical) >= 6 and all(b - a == 1 for a, b in zip(slant, slant[1:])),
      'gan_day selected: the hand is not an upright piece and a 45 degree slant')
ink = selected('ban_phim')
keys = [p for p in xam._parts(ink) if not xam._holes(p) and width_height(p)[0] <= 5]
check(len(keys) == 8 and {width_height(p) for p in keys} == {(4, 4)} and all(len(p) == 16 for p in keys),
      'ban_phim selected: expected 8 square keys of 4x4')
if len(keys) == 8:
    lefts, tops = sorted({xam.box(p)[0] for p in keys}), sorted({xam.box(p)[1] for p in keys})
    check(len(lefts) == 4 and len(tops) == 2 and len({b - a for a, b in zip(lefts, lefts[1:])}) == 1,
          f'ban_phim selected: keys are not on an even grid ({lefts}, {tops})')
check(all((39 - x, y) in ink for x, y in ink), 'ban_phim selected: not mirrored in x')
ink = selected('cu_chi')
arcs = [part for part in xam._parts(ink) if xam.box(part)[2] < 14 or xam.box(part)[0] > 25]
check(len(arcs) == 2, 'cu_chi selected: expected 2 arcs')
for part in arcs:
    arc_report('cu_chi selected arc', part, (18, 19) if xam.box(part)[0] > 25 else (22, 19), 20, 17)
dots = xam._parts(selected('them'))
check(len(dots) == 3 and all(width_height(p) == (6, 6) for p in dots), 'them selected: expected 3 round dots of 6x6')

print(f'{"RED" if failures else "GREEN"}: {checks} round and straight checks, {len(failures)} failures')
print('\n'.join(failures))
raise SystemExit(bool(failures))
