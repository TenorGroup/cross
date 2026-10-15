#!/usr/bin/env python3
"""X4 Pro restores a child's saved middle-list position when re-entered."""

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
    log, shots = sim.run(args.program, output, sd, steps, args.port, gap=2800, settle=1800)
    round_trip = ImageChops.difference(shots["05-child-before"], shots["07-child-again"]).crop((0, 0, 465, 800))
    reentry = ImageChops.difference(shots["05-child-before"], shots["10-restored"]).crop((0, 0, 465, 800))
    round_trip_changed = sum(1 for value in round_trip.getdata() if value)
    reentry_changed = sum(1 for value in reentry.getdata() if value)
    print(f"round_trip_changed_pixels={round_trip_changed} reentry_changed_pixels={reentry_changed}")
    if round_trip_changed or reentry_changed:
        print("RED: X4 Pro did not restore the selected child row")
        return 1
    if "Entering activity: FileBrowser" not in log:
        print("RED: X4 Pro did not complete the child round trip")
        return 1
    print("GREEN: X4 Pro restored the selected child row")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
