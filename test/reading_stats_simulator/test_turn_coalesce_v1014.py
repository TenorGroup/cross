"""v1.0.14: a press waiting behind a paint skips that paint's gray pass.

On the X3 the gray pass runs after the page is readable (strips, a 156 ms waveform, cleanup), and
the progress write follows it, all under the render lock. A press that arrived meanwhile waited
about 500 ms for work nobody would see: the page it leaves is replaced by the next one. Back in
the middle of a paint waited the same way before Home could start.

Now, once the page is readable, a queued turn or a pending exit skips the gray pass and leaves the
progress write for the next paint or an idle pass. A single press keeps the full paint, and the
page the presses end on is painted exactly as a page reached one press at a time.
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
FULL_GRAY = "Page render (tiled):"
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

    def test_queued_presses_skip_the_gray_pass_and_end_on_the_same_page(self):
        end = START + 6000
        log, fast = self.run_sim("fast", base.presses("DOWN", 60, 3, 150), end, end - 500)
        skipped = SKIPPED.findall(log)
        print(f"fast: skipped={skipped} full_gray={log.count(FULL_GRAY)} pages={self.pages(log)}")
        self.assertIn("queued", skipped, "a paint with a press waiting still ran its gray pass")
        self.assertEqual(self.pages(log)[-1], 3)
        # The same page reached one press at a time, every paint complete.
        slow_end = START + 3 * 1600 + 4000
        slow_log, slow = self.run_sim("slow", base.presses("DOWN", 60, 3, 1540), slow_end, slow_end - 500)
        self.assertEqual(SKIPPED.findall(slow_log), [])
        self.assertEqual(self.pages(slow_log)[-1], 3)
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
