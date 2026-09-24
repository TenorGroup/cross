"""v1.0.14 Back from a book: the recent card's excerpt reaches recent.json after Home is drawn.

Leaving a book on a page with new text rewrote recent.json in front of Home's first frame. On the
X3 that write took 130 to 175 ms, and 2.5 s once while the card was slow. The excerpt is only read
by Home, which reads it from RAM, so the reader now updates RAM and leaves the file to the activity
manager's closing writes. The entry itself was written when the book opened, so a power cut before
those writes loses the excerpt and nothing else.
"""

import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
FIXTURE = REPO / "test/epubs/test_dictionary_synonyms.epub"
BOOK = "/books/doc.epub"
DEFERRED = re.compile(r"\[ACT\] Deferred writes n=(\d+)")
HOME_FRAME = re.compile(r"\[HOME\] Frame row=")


class ReaderExitRecentTest(unittest.TestCase):
    def fresh_card(self):
        temp = tempfile.TemporaryDirectory(prefix="cross-exit-recent-")
        self.addCleanup(temp.cleanup)
        sd = Path(temp.name)
        store = sd / ".crosspoint"
        store.mkdir()
        (sd / "books").mkdir()
        shutil.copy(FIXTURE, sd / BOOK.lstrip("/"))
        (store / "settings.json").write_text(json.dumps(
            {"language": "EN", "clockHasBeenSynced": 1, "clockUtcOffsetQ": 48, "wakeIntoBook": 1}))
        (store / "state.json").write_text(
            json.dumps({"showBootScreen": False, "openEpubPath": BOOK, "lastSleepFromReader": True}))
        return sd

    def run_card(self, sd, script):
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON="power",
                   CROSSPOINT_SIM_INPUT_SCRIPT=";".join(script), CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE="3000:QUIT")
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=240)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-3000:])
        return log

    @staticmethod
    def recent(sd):
        return json.loads((sd / ".crosspoint/recent.json").read_text())

    def test_excerpt_is_written_after_the_home_frame(self):
        sd = self.fresh_card()
        log = self.run_card(sd, ["4000:RIGHT", "6000:RIGHT", "8000:BACK", "12000:QUIT"])
        tail = log[log.index("Exiting activity: EpubReader"):]
        frame = HOME_FRAME.search(tail)
        written = DEFERRED.search(tail)
        self.assertIsNotNone(frame, tail[-3000:])
        self.assertIsNotNone(written, "nothing was left for after the frame\n" + tail[-3000:])
        # Reading stats, state.json and now recent.json.
        self.assertEqual(written.group(1), "3", "recent.json was written in front of the Home frame")
        self.assertLess(frame.start(), written.start())
        book = self.recent(sd)["books"][0]
        self.assertEqual(book["path"], BOOK)
        self.assertTrue(book.get("excerpt"), "the excerpt never reached recent.json")

    def test_a_cut_before_the_closing_writes_loses_only_the_excerpt(self):
        # The page is read, then the power goes before the closing writes (QUIT runs no exit).
        cut = self.fresh_card()
        self.run_card(cut, ["4000:RIGHT", "6000:RIGHT", "9000:QUIT"])
        # The same reading, left through Back, with the closing writes done.
        kept = self.fresh_card()
        self.run_card(kept, ["4000:RIGHT", "6000:RIGHT", "8000:BACK", "12000:QUIT"])
        before, after = self.recent(cut), self.recent(kept)
        excerpt = after["books"][0].pop("excerpt", "")
        lost = before["books"][0].pop("excerpt", "")
        self.assertTrue(excerpt)
        self.assertNotEqual(excerpt, lost)
        self.assertEqual(before, after, "a cut before the closing writes lost more than the excerpt")


if __name__ == "__main__":
    unittest.main()
