#!/usr/bin/env python3
"""Writes samples.txt: hand-like strokes on the 480x800 portrait screen, seeded, so the same file
comes out every run. One gesture a line:

    <name> <expected> <gap_ms> <stroke> [<stroke>]

expected is what the recognizer must return, results joined by '+' when a gesture gives more than
one. A stroke is x,y,dt;x,y,dt;... with dt the ms since the previous sample (0 for the first).
gap_ms is the time from lifting the first stroke to touching down the second.
"""
import math
import random
import sys
from pathlib import Path

STEP_MS = 10  # one sample a main-loop pass
KIN_SEEDS = (1, 6, 13, 19)  # draws that wound like a ring before rings had to be hollow


def sample(path, speed, rng, noise=0.0, bow=0.0):
    """Points along the polyline `path` at `speed` px/ms, one every STEP_MS, with jitter."""
    seg = [(path[i], path[i + 1]) for i in range(len(path) - 1)]
    total = sum(math.dist(a, b) for a, b in seg)
    n = max(2, int(total / (speed * STEP_MS)) + 1)
    pts = []
    for k in range(n):
        d = total * k / (n - 1)
        for a, b in seg:
            L = math.dist(a, b)
            if d <= L or (a, b) == seg[-1]:
                f = 0 if L == 0 else min(1.0, d / L)
                x = a[0] + (b[0] - a[0]) * f
                y = a[1] + (b[1] - a[1]) * f
                if bow and L:
                    off = bow * math.sin(math.pi * f)  # a hand line bows a little off its chord
                    x += -(b[1] - a[1]) / L * off
                    y += (b[0] - a[0]) / L * off
                break
            d -= L
        x += rng.uniform(-noise, noise)
        y += rng.uniform(-noise, noise)
        pts.append((round(x), round(y), 0 if k == 0 else STEP_MS))
    return pts


def ellipse(cx, cy, rx, ry, start_deg, sweep_deg, rot_deg=0.0, wobble=0.0, steps=90):
    out = []
    r0 = math.radians(rot_deg)
    for i in range(steps + 1):
        a = math.radians(start_deg + sweep_deg * i / steps)
        w = 1 + wobble * math.sin(3 * a + 1.0)
        x, y = rx * w * math.cos(a), ry * w * math.sin(a)
        out.append((cx + x * math.cos(r0) - y * math.sin(r0), cy + x * math.sin(r0) + y * math.cos(r0)))
    return out


def rot(pts, cx, cy, deg):
    r = math.radians(deg)
    return [(cx + (x - cx) * math.cos(r) - (y - cy) * math.sin(r), cy + (x - cx) * math.sin(r) + (y - cy) * math.cos(r))
            for x, y in pts]


def fmt(stroke):
    return ";".join(f"{x},{y},{dt}" for x, y, dt in stroke)


def main(out):
    rng = random.Random(20261004)
    rows = []

    def add(name, expected, *strokes, gap=250):
        rows.append(f"{name} {expected} {gap} " + " ".join(fmt(s) for s in strokes))

    def x2(name, cx, cy, w, h, tilt=0.0, noise=0.0, bow=0.0, gap=250, flip=False, speed=0.6):
        a = rot([(cx - w / 2, cy - h / 2), (cx + w / 2, cy + h / 2)], cx, cy, tilt)
        b = rot([(cx + w / 2, cy - h / 2), (cx - w / 2, cy + h / 2)], cx, cy, tilt)
        if flip:
            a, b = b[::-1], a[::-1]
        add(name, "cross", sample(a, speed, rng, noise, bow), sample(b, speed, rng, noise, -bow), gap=gap)

    # X in two strokes
    x2("x-chuan", 240, 300, 120, 120)
    x2("x-chuan-nho", 240, 500, 60, 60)
    x2("x-dong-det", 240, 420, 110, 56)
    x2("x-dong-rong", 240, 420, 200, 60, gap=400)
    x2("x-nghieng-15", 240, 300, 120, 120, tilt=15)
    x2("x-nghieng-tru20", 200, 600, 130, 100, tilt=-20)
    x2("x-run-tay", 240, 300, 120, 120, noise=3)
    x2("x-run-tay-cong", 260, 350, 140, 110, noise=3, bow=10)
    x2("x-nguoc-chieu", 240, 300, 120, 120, flip=True)
    x2("x-cham-tay", 240, 300, 120, 120, gap=600, speed=0.3)
    x2("x-nhanh", 240, 300, 120, 120, gap=120, speed=1.5)

    # X in one stroke: down one arm, up the right edge, down the other arm
    def x1(name, cx, cy, w, h, noise=0.0, tilt=0.0, bow=0.0, r=None):
        p = [(cx - w / 2, cy - h / 2), (cx + w / 2, cy + h / 2), (cx + w / 2, cy - h / 2), (cx - w / 2, cy + h / 2)]
        add(name, "cross", sample(rot(p, cx, cy, tilt), 0.6, r or rng, noise, bow))

    x1("x-mot-net", 240, 300, 120, 120)
    x1("x-mot-net-run", 240, 300, 130, 100, noise=3)
    x1("x-mot-net-det", 240, 420, 160, 60, noise=2, tilt=5)
    x1("x-mot-net-cong", 240, 300, 140, 140, noise=2, bow=22)
    # Flat enough that its ends come within a ring's gap: seen from the middle it can wind like one.
    for k in KIN_SEEDS:
        x1(f"x-mot-net-det-kin{k}", 240, 420, 160, 60, noise=2, tilt=5, r=random.Random(k))

    # Circles
    def circ(name, expected, cx, cy, rx, ry, start, sweep, rot_deg=0.0, wobble=0.0, noise=0.0, speed=0.5):
        add(name, expected, sample(ellipse(cx, cy, rx, ry, start, sweep, rot_deg, wobble), speed, rng, noise))

    circ("khoanh-tron", "circle", 240, 300, 60, 60, -90, 360)
    circ("khoanh-nguoc", "circle", 240, 300, 60, 60, -90, -360)
    circ("khoanh-nho", "circle", 240, 300, 28, 28, 0, 360)
    circ("khoanh-dong", "circle", 240, 420, 210, 40, 180, 360)
    circ("khoanh-meo", "circle", 240, 300, 70, 50, 30, 350, rot_deg=20, wobble=0.18)
    circ("khoanh-ho-15", "circle", 240, 300, 60, 60, -90, 306)
    circ("khoanh-dong-ho-15", "circle", 240, 420, 200, 38, 200, -306)
    circ("khoanh-qua-dau", "circle", 240, 300, 60, 55, -90, 400)
    circ("khoanh-run", "circle", 240, 300, 60, 60, -90, 350, noise=3)
    circ("khoanh-dong-run", "circle", 240, 420, 190, 45, 170, 345, wobble=0.1, noise=3)

    # Taps
    def tap(name, x, y, ms, jitter):
        n = ms // STEP_MS + 1
        add(name, "tap", [(x + rng.randint(-jitter, jitter), y + rng.randint(-jitter, jitter), 0 if i == 0 else STEP_MS)
                          for i in range(n)])

    tap("cham", 240, 300, 80, 2)
    tap("cham-lau", 100, 600, 300, 3)
    tap("cham-mot-diem", 400, 120, 0, 0)
    tap("cham-lan-ngon", 240, 300, 120, 6)

    # Swipes and lines that are not an X
    def line(name, expected, a, b, speed=1.0, noise=0.0, bow=0.0):
        add(name, expected, sample([a, b], speed, rng, noise, bow))

    line("vuot-trai", "swipe", (400, 400), (100, 410), speed=1.5)
    line("vuot-phai", "swipe", (80, 500), (380, 490), speed=1.5)
    line("vuot-len", "swipe", (240, 600), (245, 250), speed=1.5)
    line("vuot-xuong", "swipe", (240, 200), (235, 550), speed=1.5)
    line("gach-ngang", "swipe", (60, 300), (420, 306), speed=0.5, noise=2, bow=4)
    line("gach-ngang-ngan", "swipe", (200, 300), (290, 304), speed=0.4, noise=2)
    line("duong-cheo-don", "swipe", (150, 250), (330, 400), speed=0.8, noise=2)

    # Shapes that must not be an X or a circle
    add("chu-v", "unknown", sample([(180, 250), (240, 380), (300, 250)], 0.6, rng, 2))
    add("dau-tick", "unknown", sample([(180, 320), (220, 370), (320, 230)], 0.7, rng, 2))
    add("chu-c", "unknown", sample(ellipse(240, 300, 60, 60, 60, 240), 0.5, rng, 2))
    add("so-8", "unknown", sample(ellipse(240, 260, 40, 40, 90, 360) + ellipse(240, 340, 40, 40, -90, -360), 0.6, rng, 2))
    add("rang-cua", "unknown", sample([(120, 300), (180, 340), (240, 300), (300, 340), (360, 300)], 0.7, rng, 2))
    add("gach-di-gach-lai", "unknown", sample([(100, 300), (380, 305), (110, 312), (370, 318)], 1.2, rng, 2))
    add("v-hai-net", "unknown", sample([(180, 250), (240, 380)], 0.6, rng, 2), sample([(300, 250), (242, 378)], 0.6, rng, 2))
    add("hai-gach-song-song", "unknown", sample([(150, 250), (250, 350)], 0.6, rng, 2),
        sample([(220, 250), (320, 350)], 0.6, rng, 2))
    add("cheo-roi-cham", "unknown", sample([(150, 250), (300, 400)], 0.6, rng, 2),
        [(240, 600, 0), (241, 600, STEP_MS), (241, 601, STEP_MS)], gap=200)
    # Out along a row and back over it: closed, but a line, never a ring.
    add("di-roi-ve", "unknown", sample([(100, 300), (380, 300), (100, 300)], 1.0, rng))
    add("di-roi-ve-run", "unknown", sample([(100, 300), (380, 304), (104, 310)], 1.0, rng, 2))
    # A bow tie closed back to its start: crossing arms, but closed, so no X.
    add("no-buom", "unknown", sample([(180, 240), (300, 360), (300, 240), (180, 360), (180, 242)], 0.6, rng, 2))
    # A short tick across a long slash: they cross mid-way, but one arm is far too short.
    add("gach-cheo-ngan", "unknown", sample([(140, 200), (340, 400)], 0.6, rng, 2),
        sample([(258, 282), (222, 318)], 0.4, rng, 1))
    # Two strokes crossing at 16 degrees: a slash through a line, not an X.
    add("hai-gach-cat-hep", "unknown", sample([(100, 250), (380, 330)], 0.8, rng, 2),
        sample([(100, 300), (380, 300)], 0.8, rng, 2))
    # Scribbled out in rows and brought back near the start: closed, but winding back and forth.
    add("xoa-nguech-kin", "unknown", sample([(100, 280), (380, 288), (110, 296), (370, 304), (120, 312),
                                             (360, 320), (90, 300), (102, 284)], 1.2, rng, 2))
    # Too small to mean anything, yet more than a tap.
    add("vuot-ti-hon", "unknown", sample([(200, 300), (230, 318)], 0.3, rng))
    # A long tail ending in a small x: the crossing arms are a fifth of the whole, not an X.
    add("duoi-dai-x-nho", "unknown", sample([(100, 300), (300, 300), (340, 340), (340, 300), (300, 340)], 0.6, rng, 1))
    # Two diagonal strokes too far apart in time: two swipes, never an X.
    add("hai-cheo-cach-xa", "swipe+swipe", sample([(180, 240), (300, 360)], 0.6, rng, 2),
        sample([(300, 240), (180, 360)], 0.6, rng, 2), gap=1200)
    # Arms that cross 6% from one end: an L, never an X.
    add("chu-l-cat-dau", "unknown", sample([(150, 250), (300, 400)], 0.6, rng, 2),
        sample([(110, 320), (200, 210)], 0.6, rng, 2))

    Path(out).write_text("# Written by gen_samples.py; do not edit by hand.\n" + "\n".join(rows) + "\n")
    print(f"{len(rows)} gestures -> {out}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).with_name("samples.txt"))
