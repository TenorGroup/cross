#!/usr/bin/env python3
"""X4 Pro returns to the same child row after a folder round trip."""

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
    parser.add_argument("--port", type=int, default=31925)
    args = parser.parse_args()

    output = args.output.resolve()
    files = {f"/books/folder-{index:02d}/book.txt": b"Test.\n" for index in range(18)}
    files["/books/folder-00/child-dir/book.txt"] = b"Test.\n"
    sd = sim.card(output, files=files, extra={"statusBarClock": 0, "clockShowInHeader": 0})
    steps = [
        ("00-home", None),
        ("01-file", "TAP:150,754"),
        ("02-books", "TAP:130,65"),
        ("03-folder", "TAP:130,65"),
        ("04-child-before", None),
        ("05-enter-child", "TAP:130,65"),
        ("06-parent-after", "TAP:46,754"),
    ]
    log, shots = sim.run(args.program, output, sd, steps, args.port, gap=2800, settle=1800)
    changed = sum(1 for value in ImageChops.difference(shots["04-child-before"], shots["06-parent-after"]).getdata() if value)
    print(f"changed_pixels={changed}")
    if changed:
        print("RED: X4 Pro did not restore the selected child row")
        return 1
    if "Entering activity: FileBrowser" not in log:
        print("RED: X4 Pro did not complete the child round trip")
        return 1
    print("GREEN: X4 Pro restored the selected child row")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
