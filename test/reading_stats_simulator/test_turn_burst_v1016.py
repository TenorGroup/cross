"""v1.0.16: a burst of page turn presses reaches the panel as one refresh, on the page it ends on.

v1.0.14 dropped a page that a press had already left before it reached the panel. On the X3 a page
reaches the panel 207-250 ms after its press (r03), and a quick second press came 230-240 ms after
the first: 3 ms too late, so the burst took two refreshes. A text page now waits for the panel until
250 ms after the last press. A press inside that window drops the page, and the next paint goes to
where the presses add up. A lone press pays what is left of the window after its page is drawn: on
the simulator that is nearly all of it, on the X3 0-43 ms.
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
HOLD_MS = 250
# One line per refresh the reader sends to the panel (plain or gray base), with its stamp.
REFRESH = re.compile(r"^\[(\d+)\] .*(?:to displayBuffer, mode=|displayGrayscaleBase, mode=)", re.M)
PRESS = re.compile(r"\[IN\] press t=(\d+)")
SAVED = re.compile(r"Progress saved: spine=(\d+) offset=\d+ page=(\d+)")
START = base.FIRST_PRESS_MS


class TurnBurstTest(unittest.TestCase):
    def run_presses(self, script, end_ms):
        tmp = tempfile.TemporaryDirectory(prefix="cross-turn-burst-")
        self.addCleanup(tmp.cleanup)
        sd = Path(tmp.name)
        store = sd / ".crosspoint"
        store.mkdir()
        (sd / "books").mkdir()
        base.write_epub(sd / "books/lat.epub")
        (store / "recent.json").write_text(json.dumps({"books": [{"path": "/books/lat.epub", "title": "Lat"}]}))
        (store / "settings.json").write_text(json.dumps({"language": "VI", "fontSize": 14}))
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_REFRESH_MS=str(base.REFRESH_MS), CROSSPOINT_SIM_STRICT_SAMPLING="1",
                   CROSSPOINT_SIM_INPUT_SCRIPT=f"{base.OPEN_AT_MS}:CONFIRM;{script}{end_ms}:QUIT")
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-4000:])
        self.assertEqual(log.count(base.LOST), 0, log[-3000:])
        log = log[log.index("Entering activity: EpubReader"):]
        presses = [int(t) for t in PRESS.findall(log) if int(t) >= START - 50]
        refreshes = [int(t) for t in REFRESH.findall(log) if int(t) >= START - 50]
        pages = [int(p) for s, p in SAVED.findall(log) if s == "0"]
        return presses, refreshes, pages

    def test_quick_bursts_take_one_refresh_to_their_page(self):
        for count in (2, 3, 5):
            with self.subTest(presses=count):
                presses, refreshes, pages = self.run_presses(base.presses("DOWN", 80, count, 150), START + 5000)
                gaps = [b - a for a, b in zip(presses, presses[1:])]
                print(f"burst x{count}: press gaps={gaps} refreshes after first press={[t - presses[0] for t in refreshes]} "
                      f"pages={pages}")
                self.assertEqual(len(presses), count)
                self.assertTrue(all(gap < HOLD_MS for gap in gaps), f"presses are not a quick burst: {gaps}")
                self.assertEqual(len(refreshes), 1, "a quick burst of presses took more than one refresh")
                self.assertGreater(refreshes[0], presses[-1], "the refresh went out before the burst ended")
                self.assertEqual(pages[-1], count, "the burst did not end on the page its presses add up to")

    def test_single_press_waits_no_longer_than_the_window(self):
        presses, refreshes, pages = self.run_presses(base.presses("DOWN", 80, 1, 150), START + 3000)
        print(f"single: refresh {refreshes[0] - presses[0]} ms after the press, pages={pages}")
        self.assertEqual(len(refreshes), 1)
        self.assertLessEqual(refreshes[0] - presses[0], HOLD_MS + 40, "a lone press waited past the burst window")
        self.assertEqual(pages[-1], 1)


if __name__ == "__main__":
    unittest.main()
