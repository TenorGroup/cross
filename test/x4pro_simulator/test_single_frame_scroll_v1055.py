"""Compare Text-sheet content and verify its external, idle-hidden scrollbar."""

import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    args = parser.parse_args()
    results = []
    for label in ("parent", "child", "back"):
        before = Image.open(args.before / f"{label}.png").convert("L")
        after = Image.open(args.after / f"{label}.png").convert("L")
        diff = ImageChops.difference(before.crop((0, 32, 480, 800)), after.crop((0, 32, 480, 800)))
        for left, right in ((452, 458), (469, 475)):
            diff.paste(0, (left, 0, right, diff.height))
        changed = sum(pixel != 0 for pixel in diff.getdata())
        assert changed == 0, f"{label}: {changed} sheet pixels changed"
        results.append({"frame": label, "changed_pixels": changed})
    parent = Image.open(args.after / "parent.png").convert("L")
    rows = [y for y in range(402, 712)
            if sum(parent.getpixel((x, y)) < 128 for x in range(469, 475)) >= 4]
    assert len(rows) >= 20, "Text bar is missing outside its frame"
    hidden = Image.open(args.after / "hidden.png").convert("L")
    assert not any(sum(hidden.getpixel((x, y)) < 128 for x in range(469, 475)) >= 4
                   for y in range(402, 712)), "Text bar remained after 2000 ms"
    diff = ImageChops.difference(parent, hidden)
    diff.paste(0, (469, 402, 475, 712))
    assert diff.getbbox() is None, "Text idle hide changed book or sheet content"
    (args.after / "pixel-comparison.json").write_text(json.dumps(results, indent=2) + "\n")
    print("GREEN", results, "external Text bar rows=", len(rows), "idle pixels confined to 6 columns")


if __name__ == "__main__":
    main()
