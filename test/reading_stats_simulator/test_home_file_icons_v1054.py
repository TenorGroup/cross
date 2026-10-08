"""File pages in the handwritten shell retain the existing file-kind icons."""
import os
from pathlib import Path
import re
import unittest

from test_ugly_va_v1053 import frames

REPO = Path(__file__).resolve().parents[2]
OUT = Path(os.environ.get("HOME_ARTIFACTS", os.environ.get("CROSSPOINT_TEST_ARTIFACTS", "/tmp/home-file-icons")))


def files(card):
    for path in card.sd.glob("b*.txt"):
        path.unlink()
    folder = card.sd / "Folder"
    folder.mkdir()
    for base in (card.sd, folder):
        (base / "Nested").mkdir()
        for name in ("book.epub", "image.png", "text.md"):
            (base / name).write_text("fixture")


def icon_ink(kind):
    source = (REPO / "src/components/icons/tenorRowIcons.h").read_text()
    bits = re.search(r"icon_" + kind + r"_32_bits\[\]\s*=\s*\{([^}]+)", source).group(1)
    values = [int(value, 16) for value in re.findall(r"0x[0-9A-Fa-f]+", bits)]
    return [(x, y) for y in range(32) for x in range(32)
            if not values[y * 4 + x // 8] & (0x80 >> (x % 8))]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    failures = []
    for name, keys, x in (("root", ["DOWN", "DOWN"], 48),
                          ("browser", ["DOWN", "DOWN", "CONFIRM"], 36)):
        log, image, _ = frames(keys, files)
        image.save(OUT / (name + ".png"))
        (OUT / (name + ".log")).write_text(log)
        for kind in ("folder", "book", "image", "file_text"):
            mask = icon_ink(kind)
            matched = any(all(image.getpixel((icon_x + dx, y + dy)) == 0 for dx, dy in mask)
                          for icon_x in range(x - 4, x + 5) for y in range(100, 540))
            print(name, kind, "PASS" if matched else "MISSING")
            if not matched:
                failures.append(name + ":" + kind)
    assert not failures, "missing file-kind icons: " + ", ".join(failures)


class HomeFileIconsV1054Test(unittest.TestCase):
    def test_file_kind_icons(self):
        main()


if __name__ == "__main__":
    unittest.main()
