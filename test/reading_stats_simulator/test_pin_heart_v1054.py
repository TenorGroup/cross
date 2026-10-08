"""Pinned ugly rows draw E10A with the shared pen, without moving their words."""
import json
import os
from pathlib import Path
from PIL import ImageChops

from test_ugly_va_v1053 import frames
from test_menu_customization import state

OUT = Path(os.environ.get("HOME_ARTIFACTS", "/tmp/home-pin-heart"))


def prepare(card, pin=True):
    folder = card.sd / "Folder"
    folder.mkdir()
    (folder / "Book.txt").write_text("Fixture text. " * 60)
    h = 14695981039346656037
    for byte in b"/Folder/Book.txt":
        h = ((h ^ byte) * 1099511628211) & ((1 << 64) - 1)
    key = f"bookid/{h:016x}"
    records = card.store / "favorite-files"
    records.mkdir()
    (records / f"{h:016x}.txt").write_text("/Folder/Book.txt")
    (card.store / "menu-customization.json").write_text(json.dumps(state([key] if pin else [])))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    failures = []
    for level in (1, 2):
        keys = ["DOWN", "DOWN", "CONFIRM"]
        _, plain, _ = frames(keys, lambda card: prepare(card, False), uiUglyLevel=level, tenorSideArrows=1)
        plain.save(OUT / f"plain-{level}.png")
        for arrows in (1, 0):
            name = f"ugly-{level}-arrows-{arrows}"
            log, image, _ = frames(keys, prepare,
                                   uiUglyLevel=level, tenorSideArrows=arrows)
            assert "part=word text=\ue10aBook" in log, "fixture did not reach its pinned row"
            image.save(OUT / (name + ".png"))
            (OUT / (name + ".log")).write_text(log)
            drawn = "part=penmark cp=E10A" in log
            print(name, "PASS" if drawn else "UI-FALLBACK")
            if not drawn:
                failures.append(name)
            margin = [(x, y) for y in range(135, 185) for x in range(23)
                      if image.getpixel((x, y)) == 0]
            if len(margin) < 20:
                failures.append(f"{name}: heart has no margin ink")
            else:
                assert max(x for x, _ in margin) <= 21, f"{name}: heart leaves its margin"
            word_box = (78, 146, 118, 168)
            shifted = ImageChops.difference(plain.crop(word_box).convert("RGB"),
                                           image.crop(word_box).convert("RGB")).getbbox()
            if shifted is not None:
                failures.append(f"{name}: row words moved {shifted}")
    assert not failures, "pin heart still uses UI fallback: " + ", ".join(failures)


if __name__ == "__main__":
    main()
