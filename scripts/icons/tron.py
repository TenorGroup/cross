"""Grey art that is drawn from true circles, straight lines and rounded rectangles instead of cut out of the selected art.

A circle cut out of a raster that is itself a little lumpy stays lumpy, so the grey icons that carry circles
(the clock, the two knobs of the sliders, the globe, the info ring, the dots of "more", the arcs of the gesture icon) and the keyboard are built
here. Pixel (x, y) is ink when its centre (x + 0,5, y + 0,5) passes the shape test, so a circle centred on a pixel edge is
mirrored in x and in y and equal under the transpose: 8 equal arcs. Every shape keeps the ink box of the selected icon.
"""
import math

N = 40


def _where(test):
    return {(x, y) for y in range(N) for x in range(N) if test(x + 0.5, y + 0.5)}


def disc(cx, cy, r):
    return _where(lambda px, py: (px - cx) ** 2 + (py - cy) ** 2 <= r * r)


def ring(cx, cy, r, width):
    """Annulus of outer radius r and radial thickness `width`."""
    return disc(cx, cy, r) - disc(cx, cy, r - width)


def rect(x0, y0, x1, y1):
    """Whole pixels x0..x1, y0..y1 (both ends included)."""
    return {(x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)}


def _segment_distance(px, py, a, b):
    (ax, ay), (bx, by) = a, b
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def stroke(points, width):
    """Pen of the given width along a polyline, round at the ends and at the joints."""
    return _where(lambda px, py: min(_segment_distance(px, py, a, b)
                                     for a, b in zip(points, points[1:])) <= width / 2)


def _inside_rrect(px, py, x0, y0, x1, y1, r):
    hx, hy = (x1 - x0) / 2, (y1 - y0) / 2
    qx, qy = abs(px - (x0 + x1) / 2) - (hx - r), abs(py - (y0 + y1) / 2) - (hy - r)
    return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) <= r


def rrect_ring(x0, y0, x1, y1, r, width):
    """Outline of a rounded rectangle whose outer edge runs along pixel edges x0..x1 + 1, y0..y1 + 1."""
    x1, y1 = x1 + 1, y1 + 1
    return (_where(lambda px, py: _inside_rrect(px, py, x0, y0, x1, y1, r))
            - _where(lambda px, py: _inside_rrect(px, py, x0 + width, y0 + width, x1 - width, y1 - width, r - width)))


def ellipse_band(cx, cy, a, b, width):
    """Band of `width` along the ellipse of semi-axes a (x) and b (y), by the first order distance to it."""
    def test(px, py):
        dx, dy = px - cx, py - cy
        f = (dx / a) ** 2 + (dy / b) ** 2 - 1
        grad = math.hypot(2 * dx / (a * a), 2 * dy / (b * b))
        return grad > 0 and abs(f) / grad <= width / 2
    return _where(test)


def clock():
    return ring(20, 20, 17, 2) | stroke([(20, 13), (20, 20), (24, 24)], 2)


def sliders():
    return (ring(30, 11, 6, 2) | ring(10, 29, 6, 2) | rect(4, 10, 20, 11) | rect(19, 28, 35, 29))


def globe():
    return ring(20, 20, 17, 2) | (rect(3, 19, 36, 20) & disc(20, 20, 17)) | (ellipse_band(20, 20, 5, 15, 2) & disc(20, 20, 17))


def keyboard():
    keys = set()
    for left in (11, 16, 21, 26):
        for top in (14, 19):
            keys |= rect(left, top, left + 2, top + 2)
    return rrect_ring(3, 6, 36, 33, 6, 2) | keys | rect(12, 24, 27, 25)


def dots():
    return ring(8, 20, 3, 1.0) | ring(20, 20, 3, 1.0) | ring(32, 20, 3, 1.0)


def side_arcs():
    """The two arcs beside the pill of the gesture icon: circles of radius 20, rows 8..29."""
    right = ring(18, 19, 20, 2) & rect(26, 8, 39, 29)
    return right | {(N - 1 - x, y) for x, y in right}
