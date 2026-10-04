"""Finds and measures the selected row on a button device (X3, X4): a white pill ringed in black.

The pill is the shape of the selected tab on the tab bar: ends of radius half the row, a 3 px black
ring, white inside. Tests that used to look for a black block use `pill_band` instead.
"""

RING = 3


def dark(px, x, y):
    return px[x, y] < 128


def pill_band(image, top=120, bottom=730, x0=150, x1=380):
    """(y0, y1) of the selected pill, half-open: from the top ring line to the end of the bottom one.

    The ring's straight top and bottom are full-width dark runs exactly RING rows deep. A hairline
    (rule, header underline) is shallower and the tab bar's grey ring lies above `top`.
    """
    px = image.convert("L").load()
    xs = range(x0, x1, 2)
    runs, start = [], None
    for y in range(top, min(bottom, image.height)):
        full = all(dark(px, x, y) for x in xs)
        if full and start is None:
            start = y
        elif not full and start is not None:
            runs.append((start, y))
            start = None
    rings = [r for r in runs if r[1] - r[0] == RING]
    # Two ring lines close enough to be one row: a popup's own frame lines lie further apart.
    for top_ring, bottom_ring in zip(rings, rings[1:]):
        if bottom_ring[1] - top_ring[0] <= 100:
            return top_ring[0], bottom_ring[1]
    return None


def pill_extent(image, band, mirrored=False, bounds=(14, None)):
    """(left, right) outermost dark columns of the pill, rows of the band only, margins skipped.

    `mirrored`: the list band is centred, so the left end is the right end mirrored. A pin mark in
    the left margin is dark in the same rows and would be read as the ring's end.
    `bounds`: columns searched, to leave out a popup's own frame.
    """
    px = image.convert("L").load()
    cols = [x for x in range(bounds[0], (bounds[1] or image.width - 14))
            if any(dark(px, x, y) for y in range(band[0], band[1]))]
    if mirrored:
        return image.width - 1 - cols[-1], cols[-1]
    return cols[0], cols[-1]


def ink_inside(image, band, left, right):
    """Dark pixels inside the ring's straight part, rows between the two ring lines: the label."""
    px = image.convert("L").load()
    r = (band[1] - band[0]) // 2
    return [(x, y) for y in range(band[0] + RING, band[1] - RING)
            for x in range(left + r, right - r) if dark(px, x, y)]
