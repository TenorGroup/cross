#!/usr/bin/env python3
"""X4 Pro restores a child's saved middle-list position when re-entered."""

import argparse
import sys
from pathlib import Path

from PIL import ImageChops

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ui_paths_sim"))
import sim


def journey(program, output, port):
    files = {f"/books/child/row-{index:02d}/book.txt": b"Test.\n" for index in range(18)}
    sd = sim.card(output, files=files, extra={"statusBarClock": 0, "clockShowInHeader": 0})
    steps = [
        ("00-home", None),
        ("01-file", "TAP:150,754"),
        ("02-books", "TAP:130,65"),
        ("03-child", "TAP:130,65"),
        ("04-scroll", "SWIPE:240,650,240,350,500"),
        ("05-child-before", None),
        ("06-enter-middle", "TAP:130,390"),
        ("07-child-again", "TAP:46,754"),
        ("08-parent", "TAP:46,754"),
        ("09-reenter", "TAP:130,65"),
        ("10-restored", None),
    ]
    return sim.run(program, output, sd, steps, port, gap=2800, settle=1800)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("program")
    parser.add_argument("output", type=Path)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--port", type=int, default=31925)
    args = parser.parse_args()

    output = args.output.resolve()
    log, shots = journey(args.program, output / "candidate", args.port)
    _, base_shots = journey(args.baseline.resolve(), output / "baseline", args.port + 1)
    changed = sum(
        sum(1 for value in ImageChops.difference(base_shots[name], shots[name]).getdata() if value)
        for name in ("05-child-before", "07-child-again", "10-restored")
    )
    print(f"baseline_changed_pixels={changed}")
    if changed:
        print("RED: X4 Pro file back differs from the v1.0.61 baseline")
        return 1
    if "Entering activity: FileBrowser" not in log:
        print("RED: X4 Pro did not complete the child round trip")
        return 1
    print("GREEN: X4 Pro file back matches the v1.0.61 baseline")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
