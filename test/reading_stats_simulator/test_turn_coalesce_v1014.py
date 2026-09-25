"""v1.0.14: presses that land while a page is still being built skip that page.

Round one skipped the gray pass of any paint with a press waiting. On the X3 (r43) that ended
paints earlier, so presses that used to merge into one repaint each got their own, and the last
press did not reach its page sooner (27 refreshes where 19 were). The gray pass is kept again for
queued turns; it is still dropped for Back and for a held chapter jump, which leave the page.

What remains for queued turns happens before anything reaches the panel: a press that arrives
while the page is laid out and drawn drops that paint, and the next paint goes straight to the
page the presses add up to. A single press keeps the full paint, and the page is painted exactly
as the same page reached one press at a time.
"""

import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

import test_page_turn_presses as base

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
SKIPPED = re.compile(r"Gray pass skipped: (\w+)")
DROPPED = re.compile(r"Paint dropped before display: (\w+)")
FULL_GRAY = "Page render (tiled):"
# One line per refresh the reader sends to the panel (plain or gray base).
REFRESH = re.compile(r"to displayBuffer, mode=|displayGrayscaleBase, mode=")
PRESS = re.compile(r"\[IN\] press t=(\d+)")
SAVED = re.compile(r"Progress saved: spine=(\d+) offset=\d+ page=(\d+)")
START = base.FIRST_PRESS_MS


def page_body(bmp):
    """Pixels above the status bar. The bar's page total is an estimate that follows how far the
    background layout has got, which depends on timing, and its clock follows the wall clock."""
    from PIL import Image
    image = Image.open(bmp).convert("RGB")
    return image.crop((0, 0, image.width, image.height - STATUS_BAR_ROWS)).tobytes()


STATUS_BAR_ROWS = 32


class TurnCoalesceTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="cross-coalesce-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def run_sim(self, name, script, end_ms, shot_ms=None):
        sd = self.root / name
        store = sd / ".crosspoint"
        store.mkdir(parents=True)
        (sd / "books").mkdir()
        base.write_epub(sd / "books/lat.epub")
        (store / "recent.json").write_text(json.dumps({"books": [{"path": "/books/lat.epub", "title": "Lat"}]}))
        (store / "settings.json").write_text(json.dumps({"language": "VI", "fontSize": 14}))
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_REFRESH_MS=str(base.REFRESH_MS), CROSSPOINT_SIM_STRICT_SAMPLING="1",
                   CROSSPOINT_SIM_INPUT_SCRIPT=f"{base.OPEN_AT_MS}:CONFIRM;{script}{end_ms}:QUIT")
        shot = self.root / f"{name}.bmp"
        if shot_ms is not None:
            env["CROSSPOINT_SIM_SCREENSHOTS"] = f"{shot_ms}:{shot}"
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-4000:])
        self.assertEqual(log.count("[SIM] lost press"), 0, log[-3000:])
        return log[log.index("Entering activity: EpubReader"):], shot

    @staticmethod
    def pages(log):
        return [int(p) for s, p in SAVED.findall(log) if s == "0"]

    def test_queued_presses_keep_the_gray_pass(self):
        log, _ = self.run_sim("rhythm", base.presses("DOWN", 60, 3, 150), START + 6000)
        print(f"rhythm: skipped={SKIPPED.findall(log)} pages={self.pages(log)}")
        self.assertNotIn("queued", SKIPPED.findall(log), "a queued turn still cut the gray pass short")
        self.assertEqual(self.pages(log)[-1], 3)

    def test_presses_during_the_build_go_straight_to_their_page(self):
        # The book's first page takes about a second to lay out on the simulator (the open paint);
        # two presses land inside it, before anything reaches the panel.
        end = 7000
        script = f"{base.OPEN_AT_MS + 500}:DOWN:60;{base.OPEN_AT_MS + 700}:DOWN:60;"
        log, fast = self.run_sim("during", script, end, end - 500)
        dropped = DROPPED.findall(log)
        # Refreshes from the first page press on (the log starts at the reader's entry).
        first = next(m for m in PRESS.finditer(log) if int(m.group(1)) >= base.OPEN_AT_MS + 400)
        refreshes = len(REFRESH.findall(log[first.start():]))
        print(f"during: dropped={dropped} refreshes={refreshes} pages={self.pages(log)}")
        self.assertIn("queued", dropped, "a page the presses had already left was still put on the panel")
        self.assertEqual(refreshes, 1, "more than one refresh for presses made during one paint")
        self.assertEqual(self.pages(log)[-1], 2)
        slow_log, slow = self.run_sim("steps", base.presses("DOWN", 60, 2, 1540), START + 5000, START + 4500)
        self.assertEqual(DROPPED.findall(slow_log), [])
        self.assertEqual(self.pages(slow_log)[-1], 2)
        self.assertEqual(page_body(fast), page_body(slow), "the page the presses end on is painted differently")

    def test_single_press_keeps_the_full_paint(self):
        log, _ = self.run_sim("single", base.presses("DOWN", 60, 1, 150), START + 4000)
        self.assertEqual(SKIPPED.findall(log), [])
        self.assertEqual(self.pages(log)[-1], 1)
        # Opening paint plus the turn, both with their gray pass.
        self.assertEqual(log.count(FULL_GRAY), 2)

    def test_back_during_a_paint_skips_its_gray_pass_and_keeps_the_page(self):
        log, _ = self.run_sim("back", f"{START}:DOWN:60;{START + 150}:BACK:60;", START + 5000)
        self.assertIn("leaving", SKIPPED.findall(log), "Back waited for the gray pass of a page it leaves")
        self.assertIn("Entering activity: Home", log)
        self.assertEqual(self.pages(log)[-1], 1, "the page shown before Back was not saved")


if __name__ == "__main__":
    unittest.main()
