"""Home Settings' scrollbar stays inside a rounded group frame."""

import os
import argparse
from pathlib import Path
import tempfile

from test_thanh_day import run


ARTIFACTS = Path(os.environ.get("HOME_ARTIFACTS", tempfile.gettempdir()))


def check_frame_scrollbar_contract(repo, mutate):
    source = (repo / "src/activities/UiListActivity.cpp").read_text()
    if mutate:
        source = source.replace("tenorchrome::frameScrollBar", "settingsScrollBar")
    assert "const auto bar = tenorchrome::frameScrollBar(" in source, "Settings must use frameScrollBar"


def dark(image, x, y):
    return image.getpixel((x, y)) < 128


def main():
    with tempfile.TemporaryDirectory(prefix="x4pro-settings-scroll-") as tmp:
        (settings,) = run(Path(tmp), "3000:TAP:455,754", [4800], settings={"uiTextSize": 2})
        ARTIFACTS.mkdir(parents=True, exist_ok=True)
        settings.save(ARTIFACTS / "home-settings-scroll.png")
        side = [any(dark(settings, x, y) for x in (16, 17, 462, 463)) for y in range(40, 700)]
        gaps = []
        start = None
        for y, has_side in enumerate([*side, True], 40):
            if not has_side and start is None:
                start = y
            if has_side and start is not None:
                if y - start >= 16:
                    # Side ink starts after the rounded corner, 20 px below its top.
                    gaps.append((start + 20, y - 20))
                start = None
        gaps = gaps[1:-1]  # Content above the first frame and below the last is not a group gap.
        assert gaps, "could not find gaps between Settings group frames"
        hits = [(x, y) for top, bottom in gaps for y in range(top, bottom)
                for x in range(450, 461) if dark(settings, x, y)]
        print(f"frame gaps={gaps}, scrollbar ink outside frame={len(hits)}")
        assert not hits, f"scrollbar crosses a group gap at {hits[0]}"


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--mutate-frame-scrollbar", action="store_true")
    args = parser.parse_args()
    check_frame_scrollbar_contract(args.repo, args.mutate_frame_scrollbar)
    main()
