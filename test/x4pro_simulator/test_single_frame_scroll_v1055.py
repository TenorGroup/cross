"""Compare Text-sheet pixels and verify its persistent in-frame bar."""

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
        changed = sum(pixel != 0 for pixel in diff.getdata())
        assert changed == 0, f"{label}: {changed} sheet pixels changed"
        results.append({"frame": label, "changed_pixels": changed})
    parent = Image.open(args.after / "parent.png").convert("L")
    rows = [y for y in range(400, 693)
            if sum(parent.getpixel((x, y)) < 128 for x in range(452, 458)) >= 4]
    assert len(rows) >= 20, "Text bar disappeared after 2000 ms"
    (args.after / "pixel-comparison.json").write_text(json.dumps(results, indent=2) + "\n")
    print("GREEN", results, "persistent Text bar rows=", len(rows))


if __name__ == "__main__":
    main()
