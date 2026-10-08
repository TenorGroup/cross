"""The 2 end pills of the X4 Pro Home bar clear its rounded ends."""

import os
from pathlib import Path
import tempfile

from test_thanh_day import BAR_TOP, run


ARTIFACTS = Path(os.environ.get("HOME_ARTIFACTS", tempfile.gettempdir()))


def ring_edge(image, left, right, first):
    y = BAR_TOP + 30
    ink = [x for x in range(left, right) if image.getpixel((x, y)) < 128]
    assert ink, f"selected ring missing in {left}..{right}"
    return min(ink) if first else max(ink)


def main():
    with tempfile.TemporaryDirectory(prefix="x4pro-home-tabs-") as tmp:
        first, last = run(Path(tmp), "3000:TAP:455,754", [2800, 4800])
        ARTIFACTS.mkdir(parents=True, exist_ok=True)
        first.save(ARTIFACTS / "home-tab-first.png")
        last.save(ARTIFACTS / "home-tab-last.png")
        left = ring_edge(first, 20, 115, True)
        right = ring_edge(last, 365, 460, False)
        print(f"end pill ink edges: left={left}, right={right}")
        assert left >= 24 and right <= 454, "end pills need 8 px inside the bar edges at x=16 and x=463"


if __name__ == "__main__":
    main()
