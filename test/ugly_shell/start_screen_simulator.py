import argparse
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "reading_stats_simulator"))
import ugly_common

parser = argparse.ArgumentParser()
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
os.environ["UGLY_SHOTS"] = str(args.output)
ugly_common.ARTIFACTS = str(args.output)

cases = [
    ("book", 1, 0, "/b0.txt", True, None, "TxtReader", None),
    ("diary", 1, 1, "/b0.txt", True, None, "UglyDiary", None),
    ("recent", 1, 2, "/b0.txt", True, None, "UglyNotebook", 0),
    ("desk", 1, 3, "/b0.txt", True, None, "UglyDesk", None),
    ("book-empty", 1, 0, "", True, None, "UglyDiary", None),
    ("book-gone", 1, 0, "/b0.txt", False, None, "UglyDiary", None),
    ("book-off-recent", 1, 0, "/other.txt", True, None, "UglyDiary", None),
    ("legacy-default", 1, None, "/b0.txt", True, None, "UglyDiary", None),
    ("cross-cold", 0, 0, "/b0.txt", True, None, "Home", None),
    ("book-panic", 1, 0, "/b0.txt", True, "start-screen panic fixture", "Crash", None),
    ("invalid-default", 1, 255, "/b0.txt", True, None, "UglyDiary", None),
]
cases = [(*case, False) for case in cases] + [
    ("book-wake", 1, 0, "/b0.txt", True, None, "TxtReader", None, True),
    ("diary-wake", 1, 1, "/b0.txt", True, None, "UglyDiary", None, True),
    ("recent-wake", 1, 2, "/b0.txt", True, None, "UglyNotebook", 0, True),
    ("desk-wake", 1, 3, "/b0.txt", True, None, "UglyDesk", None, True),
    ("cross-wake", 0, 0, "/b0.txt", True, None, "TxtReader", None, True),
]
results = []
for name, shell, choice, book, exists, panic, expected, page, wake in cases:
    settings = {"wakeIntoBook": 1}
    if choice is not None:
        settings["uglyStartScreen"] = choice
    card = ugly_common.Card(shell=shell, **settings)
    try:
        (card.store / "state.json").write_text(json.dumps({"openEpubPath": book, "showBootScreen": not wake}))
        if not exists:
            (card.sd / "b0.txt").unlink()
        if book == "/other.txt":
            (card.sd / "other.txt").write_text("A book outside Recent.\n" * 100)
        extra = {"CROSSPOINT_SIM_PANIC": panic} if panic else {}
        if wake:
            extra["CROSSPOINT_SIM_WAKE_REASON"] = "power"
        log, images = card.run("6500:QUIT", shots=[(5500, name)], timeout=40, **extra)
        (args.output / (name + ".log")).write_text(log)
        entered = ugly_common.entered(log)
        actual = entered[-1] if entered else "no activity"
        pages = ugly_common.notebook_pages(log)
        ok = actual == expected and (page is None or pages and pages[-1] == page) and name in images
        results.append({"case": name, "expected": expected, "actual": actual, "pages": pages, "ok": bool(ok)})
        print(name, "GREEN" if ok else "RED", actual, flush=True)
    finally:
        card.close()
(args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
raise SystemExit(any(not result["ok"] for result in results))
