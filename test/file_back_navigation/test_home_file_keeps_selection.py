#!/usr/bin/env python3
"""Returning to Home File keeps the chosen root row and viewport.

The last-folder row is inserted on return. The selected root entry and the
viewport top must both move by one so the same rows stay in the same places.
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ui_paths_sim"))
import sim


FRAME = re.compile(r"\[HOME\] Frame row=(\d+) top=(\d+)")
SHOT = re.compile(r"Saved screenshot: .*/([^/]+)\.bmp")


def home_frames(log: str) -> dict[str, tuple[int, int]]:
    latest = None
    frames = {}
    for line in log.splitlines():
        match = FRAME.search(line)
        if match:
            latest = (int(match.group(1)), int(match.group(2)))
        shot = SHOT.search(line)
        if shot and latest is not None:
            frames[shot.group(1)] = latest
    return frames


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("program")
    parser.add_argument("output", type=Path)
    parser.add_argument("--port", type=int, default=31926)
    args = parser.parse_args()

    output = args.output.resolve()
    files = {f"/folder-{index:02d}/book.txt": b"Test.\n" for index in range(18)}
    sd = sim.card(output, files=files, extra={"statusBarClock": 0, "clockShowInHeader": 0})
    steps = [
        ("00-home", None),
        ("01-file", "DOWN"),
        ("02-last", "LEFT"),
        ("03-up-one", "LEFT"),
        ("04-up-two", "LEFT"),
        ("05-up-three", "LEFT"),
        ("06-up-four", "LEFT"),
        ("07-home-before", None),
        ("08-folder", "CONFIRM"),
        ("09-home-after", "BACK:1200"),
    ]
    log, _ = sim.run(args.program, output, sd, steps, args.port, gap=2800, settle=1800)
    frames = home_frames(log)
    before = frames.get("07-home-before")
    after = frames.get("09-home-after")
    print(f"before={before} after={after}")
    if before is None or after is None:
        print("RED: scripted journey did not capture both Home File frames")
        return 1
    expected = (before[0] + 1, before[1] + 1)
    if after != expected:
        print(f"RED: expected selected row and top {expected} after inserting Mo lai")
        return 1
    print("GREEN: Home File kept the selected root row and viewport")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
