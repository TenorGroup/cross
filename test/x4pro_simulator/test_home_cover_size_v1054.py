"""The X4 Pro Recent cover keeps its size when UI text moves to Medium."""

import json
import os
from pathlib import Path
import subprocess
import tempfile

from PIL import Image


REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("X4PRO_PROGRAM", REPO / ".pio/build/simulator_x4pro/program"))
ARTIFACTS = Path(os.environ.get("HOME_ARTIFACTS", tempfile.gettempdir()))


def longest_run(values):
    best = start = end = 0
    for index, dark in enumerate([*values, False]):
        if dark and start == end:
            start = index
        if dark:
            end = index + 1
        else:
            if end - start > best:
                best = end - start
                result = (start, end)
            start = end = index + 1
    return result if best else (0, 0)


def cover_rect(image):
    x = 40
    top, bottom = longest_run(image.getpixel((x, y)) < 128 for y in range(32, 715))
    top += 32
    bottom += 32
    assert bottom - top > 100, f"cover missing at x={x}: {top}..{bottom}"
    middle = (top + bottom) // 2
    left, right = longest_run(image.getpixel((xx, middle)) < 128 for xx in range(16, 340))
    return (left + 16, top, right + 16, bottom)


def capture(tier, recorded=False):
    with tempfile.TemporaryDirectory(prefix="x4pro-cover-v1054-") as tmp:
        folder = Path(tmp)
        sd = folder / "sd"
        store = sd / ".crosspoint"
        store.mkdir(parents=True)
        (sd / "book.txt").write_text("Cover geometry fixture.\n" * 10)
        Image.new("1", (298, 450), 0).save(sd / "cover.bmp")
        (store / "recent.json").write_text(json.dumps({"books": [
            {"path": "/book.txt", "title": "A quiet book", "author": "Tenor", "coverBmpPath": "/cover.bmp"}
        ]}))
        (store / "state.json").write_text(json.dumps({"openEpubPath": "", "showBootScreen": False}))
        (store / "settings.json").write_text(json.dumps({"language": "VI", "uiTextSize": tier}))
        if recorded:
            key = 14695981039346656037
            for byte in b"/book.txt":
                key = ((key ^ byte) * 1099511628211) & ((1 << 64) - 1)
            records = store / "reading-stats"
            records.mkdir()
            (records / f"tenor_{key:016x}.json").write_text(json.dumps({
                "bookEpoch": 0, "path": "/book.txt", "title": "A quiet book", "minutes": 999 * 60 + 59,
                "ms": 0, "turns": 412, "first": 20260805, "last": 20260817,
                "days": 6, "progress": 34, "startProgress": 0
            }))
        shot = folder / "home.bmp"
        env = {key: value for key, value in os.environ.items() if not key.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT="3000:QUIT", CROSSPOINT_SIM_SCREENSHOTS=f"2500:{shot}")
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
        assert run.returncode == 0, (run.stdout + run.stderr)[-1500:]
        with Image.open(shot) as raw:
            image = raw.convert("L")
        ARTIFACTS.mkdir(parents=True, exist_ok=True)
        suffix = "-recorded" if recorded else ""
        image.save(ARTIFACTS / f"home-cover-tier-{tier}{suffix}.png")
        (ARTIFACTS / f"home-cover-tier-{tier}{suffix}.log").write_text(run.stdout + run.stderr)
        if recorded:
            assert "Card stats rows=3f" in run.stdout + run.stderr, "six record-backed rows missing"
        return cover_rect(image)


def main():
    small, medium, large = [capture(tier) for tier in range(3)]
    print(f"cover rectangles: small={small}, medium={medium}, large={large}")
    small_width = small[2] - small[0]
    medium_width = medium[2] - medium[0]
    assert medium_width >= small_width - 2, f"Medium shrank the cover by {small_width - medium_width} px"
    recorded = capture(1, True)
    assert recorded == medium, "record-backed rows changed the cover geometry"


if __name__ == "__main__":
    main()
