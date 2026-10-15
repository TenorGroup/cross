#!/usr/bin/env python3
"""File Back restores the selected folder and its exact viewport.

Run with a simulator_x3_uc8279 program. The test moves to the last child,
steps back within the visible window, enters that child, then presses Back.
The parent screen before and after the round trip must be pixel-identical.
"""

import argparse
import sys
from pathlib import Path

from PIL import ImageChops

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ui_paths_sim"))
import sim


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("program")
    parser.add_argument("output", type=Path)
    parser.add_argument("--port", type=int, default=31924)
    args = parser.parse_args()

    output = args.output.resolve()
    files = {}
    for index in range(18):
        files[f"/books/folder-{index:02d}/book.txt"] = b"Test.\n"
    sd = sim.card(output, files=files, extra={"statusBarClock": 0, "clockShowInHeader": 0})

    steps = [
        ("00-home", None),
        ("01-file", "DOWN"),
        ("02-books", "CONFIRM"),
        ("03-last", "LEFT"),
        ("04-up-one", "LEFT"),
        ("05-up-two", "LEFT"),
        ("06-up-three", "LEFT"),
        ("07-up-four", "LEFT"),
        ("08-parent-before", None),
        ("09-child", "CONFIRM"),
        ("10-parent-after", "BACK"),
    ]
    log, shots = sim.run(args.program, output, sd, steps, args.port, gap=2800, settle=1800)
    before = shots["08-parent-before"]
    after = shots["10-parent-after"]
    difference = ImageChops.difference(before, after)
    changed = sum(1 for value in difference.getdata() if value)
    print(f"changed_pixels={changed}")
    if changed:
        print("RED: Back changed the selected row or viewport")
        return 1
    if log.count("Entering activity: FileBrowser") != 1:
        print("RED: scripted journey left the File browser")
        return 1
    print("GREEN: Back restored the selected row and viewport")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
