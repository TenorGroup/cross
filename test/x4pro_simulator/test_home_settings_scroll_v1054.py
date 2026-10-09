"""Home Settings uses 1 shared scrollbar in the right margin."""

import argparse
import os
from pathlib import Path
import tempfile

from test_shared_scroll_v1055 import home_case


ARTIFACTS = Path(os.environ.get("HOME_ARTIFACTS", tempfile.gettempdir()))


def check_frame_scrollbar_contract(repo, mutate):
    source = (repo / "src/activities/UiListActivity.cpp").read_text()
    if mutate:
        source = source.replace("drawPageScrollbar();", "oldFrameScrollbar();")
    assert "drawPageScrollbar();" in source, "Settings must use the shared page scrollbar"


def main():
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    results = [home_case(ARTIFACTS, tier) for tier in range(3)]
    for result in results:
        print("RED" if result["failures"] else "GREEN", result)
    assert not any(result["failures"] for result in results), results


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--mutate-frame-scrollbar", action="store_true")
    args = parser.parse_args()
    check_frame_scrollbar_contract(args.repo, args.mutate_frame_scrollbar)
    main()
