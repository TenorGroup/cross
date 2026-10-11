"""Pixel measurements of the X3 dialogs, shared by test_hop_thoai_x3_v1058.py and the shoot tools.

The box of a dialog is the one the painter logged ([DIALOG] or [MENU] context in the simulator's output); what is measured is
the pixels inside it. The radius of a corner is the one whose circle best fits the first dark pixel of every row of that corner
(a corner of the real renderer, not the constant in the source). The tick is found by its exact pixels, the row it stands
in by the ring round the row.
"""
import math
import re
from pathlib import Path



def dark(p):
    return p < 128


def longest_run(pixels, y, lo=0, hi=528):
    best, start, run, at = 0, 0, 0, 0
    for x in range(lo, hi):
        if dark(pixels[x, y]):
            if run == 0:
                at = x
            run += 1
            if run > best:
                best, start = run, at
        else:
            run = 0
    return best, start


def logged_box(text):
    """(x0, y0, x1, y1) inclusive of the last dialog the painter logged, None when it logged none."""
    found = re.findall(r'\[(?:DIALOG\] \w+|MENU\] context) box=(-?\d+),(-?\d+),(\d+),(\d+)', text)
    if not found:
        return None
    x, y, w, h = (int(v) for v in found[-1])
    return x, y, x + w - 1, y + h - 1


def corner_profile(image, before, box, corner, halo=False, rows=48):
    """For each row of a corner, the offset from the outer edge of the first pixel the dialog changed there; 60 when the row has
    none. A dialog with a white halo round it (hold menu, confirmation) also changes white pixels outside its border, so
    only pixels that turned black count; ink of the screen under a border hides that pixel and pushes the row a pixel or 2
    inward, which the fit forgives."""
    px, old = image.load(), before.load()
    x0, y0, x1, y1 = box
    out = []
    for r in range(min(rows, (y1 - y0 + 1) // 2)):
        y = y0 + r if corner[0] == 't' else y1 - r
        found = 60
        for c in range(0, 60):
            x = x0 + c if corner[1] == 'l' else x1 - c
            if 0 <= x < image.size[0] and (dark(px[x, y]) and not dark(old[x, y]) if halo else px[x, y] != old[x, y]):
                found = c
                break
        out.append(found)
    return out


def predicted(radius, r):
    if radius <= 0 or r >= radius:
        return 0
    dy = radius - (r + 0.5)
    dx = math.sqrt(radius * radius - dy * dy)
    return max(0, math.ceil(radius - dx - 0.5))


def fit_radius(profile):
    """(radius, rows more than 2 px off its circle) of the circle that best fits the profile of a corner; a row's miss counts
    for 3 px at most, so a few rows hidden by ink under the border do not move the fit."""
    best = None
    for radius in range(0, min(44, len(profile))):
        errors = [abs(profile[r] - predicted(radius, r)) for r in range(len(profile))]
        score = (sum(min(e, 3) for e in errors), radius)
        if best is None or score < best[0]:
            best = (score, radius, sum(1 for e in errors if e > 2))
    return best[1], best[2]


def corners(image, before, box, halo=False):
    """{corner: (radius, rows off the circle, profile)} for the 4 corners of a box."""
    result = {}
    for corner in ('tl', 'tr', 'bl', 'br'):
        profile = corner_profile(image, before, box, corner, halo)
        radius, bad = fit_radius(profile)
        result[corner] = (radius, bad, profile)
    return result


def tick_pixels(root):
    text = (Path(root) / 'src/components/icons/tenorRowMarks.h').read_text()
    block = re.search(r'icon_row_chosen_24_bits\[\] = \{(.*?)\};', text, re.S).group(1)
    data = [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]{2}', block)]
    return [(c, r) for r in range(24) for c in range(24) if not (data[r * 3 + c // 8] >> (7 - c % 8)) & 1]


def find_ticks(image, ink, region):
    """The tick inside region (x0, y0, x1, y1) as a list of its top-left corner: where the 24 px tick is drawn exactly (its ink all
    dark) and the rest of its cell is the whitest. A tick stands alone in its cell, but when it is set against the ring
    of its row the ring's end is in the cell too; a stroke of a bold letter has far more ink in the cell than that."""
    px = image.load()
    x0, y0, x1, y1 = region
    ink = set(ink)
    rest = [(c, r) for r in range(24) for c in range(24) if (c, r) not in ink]
    best = None
    for y in range(max(0, y0), min(image.size[1] - 24, y1 - 23)):
        for x in range(max(0, x0), min(image.size[0] - 24, x1 - 23)):
            if all(dark(px[x + c, y + r]) for c, r in ink):
                extra = sum(1 for c, r in rest if dark(px[x + c, y + r]))
                if best is None or extra < best[0]:
                    best = (extra, x, y)
    return [(best[1], best[2])] if best and best[0] <= 60 else []


def row_ring(image, y, x0, x1, min_run=100):
    """Rows (top, bottom) of the ring round the row that holds y: the nearest solid long runs above and below it."""
    px = image.load()

    def solid(row):
        return longest_run(px, row, x0, x1)[0] >= min_run

    top = y
    while top > y - 60 and not solid(top):
        top -= 1
    if not solid(top):
        return None
    while solid(top - 1):
        top -= 1
    bottom = y
    while bottom < y + 60 and not solid(bottom):
        bottom += 1
    if not solid(bottom):
        return None
    while solid(bottom + 1):
        bottom += 1
    return top, bottom


def tick_metrics(image, root, box):
    """Where the tick of a chosen row stands: its ink box, the row's ring, and the gaps, or None when there is no tick."""
    ink = tick_pixels(root)
    x0, y0, x1, y1 = box
    hits = find_ticks(image, ink, (x0, y0, min(x1, x0 + 90), y1))
    if len(hits) != 1:
        return {'ticks': len(hits)}
    tx, ty = hits[0]
    xs = [tx + c for c, r in ink]
    ys = [ty + r for c, r in ink]
    ink_left, ink_right, ink_top, ink_bottom = min(xs), max(xs), min(ys), max(ys)
    centre = (ink_top + ink_bottom + 1) / 2
    out = {'ticks': 1, 'ink': (ink_left, ink_top, ink_right, ink_bottom), 'centre': centre}
    ring = row_ring(image, (ink_top + ink_bottom) // 2, x0 + 8, x1 - 8)
    px = image.load()
    mid = (ink_top + ink_bottom) // 2
    # first dark pixel left of the tick on its own centre line: the inner edge of the ring's left end
    x = ink_left - 1
    while x > x0 and not dark(px[x, mid]):
        x -= 1
    out['air_ring'] = ink_left - x - 1
    out['air_border'] = ink_left - x0
    if ring:
        out['ring'] = ring
        out['ring_centre'] = (ring[0] + ring[1] + 1) / 2
        out['off_row'] = centre - out['ring_centre']
        text_top, text_bottom = None, None
        for y in range(ring[0] + 4, ring[1] - 3):
            if any(dark(px[xx, y]) for xx in range(ink_right + 10, x1 - 12)):
                text_top = y if text_top is None else text_top
                text_bottom = y
        if text_top is not None:
            out['off_text'] = centre - (text_top + text_bottom + 1) / 2
    return out


def edge_distance(x, y, box, r):
    """How far inside the rounded box (corner radius r) the pixel (x, y) lies, counted from its outer edge."""
    x0, y0, x1, y1 = box
    cx = x0 + r if x < x0 + r else x1 + 1 - r if x > x1 - r else None
    cy = y0 + r if y < y0 + r else y1 + 1 - r if y > y1 - r else None
    if cx is not None and cy is not None:
        return r - math.hypot(x + 0.5 - cx, y + 0.5 - cy)
    return min(x - x0, x1 - x, y - y0, y1 - y) + 0.5


def ring_clearance(image, box, r):
    """How near the ring round Cancel (ink in the last 57 rows of a confirmation) comes to the box's edge, in px from the
    outer edge; the 2 px border and its blur (under 4 px) are not the ring."""
    px = image.load()
    x0, y0, x1, y1 = box
    near = 99.0
    for y in range(y1 - 56, y1 + 1):
        for x in range(x0, x1 + 1):
            if dark(px[x, y]):
                d = edge_distance(x, y, box, r)
                if d >= 4.0:
                    near = min(near, d)
    return near

