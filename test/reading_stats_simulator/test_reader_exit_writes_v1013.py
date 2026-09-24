"""v1.0.13 Back from a book: Home first, then the reader's closing writes.

Leaving a book wrote the reading-stats checkpoint and state.json before Home could draw, and the
X3 showed the book for another 250 to 450 ms after the press. Both files copy what RAM holds, so
the reader now leaves them to the activity manager, which writes them once the next screen's first
frame is up. Sleep still writes them before the device powers down. Either way the files must end
up holding what was read.
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


class ReaderExitWritesTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="cross-exit-writes-")
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir()
        (self.sd / "books").mkdir()
        shutil.copy(FIXTURE, self.sd / BOOK.lstrip("/"))
        (self.store / "settings.json").write_text(json.dumps(
            {"language": "EN", "clockHasBeenSynced": 1, "clockUtcOffsetQ": 48, "wakeIntoBook": 1}))
        (self.store / "state.json").write_text(
            json.dumps({"showBootScreen": False, "openEpubPath": BOOK, "lastSleepFromReader": True}))

    def run_card(self, before, after=("3000:QUIT",)):
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_WAKE_REASON="power",
                   CROSSPOINT_SIM_INPUT_SCRIPT=";".join(before), CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE=";".join(after))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=240)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-3000:])
        return log

    def book(self):
        return json.loads((self.store / "reading-stats.json").read_text())["activeBook"]

    def test_back_to_home_writes_after_the_home_frame(self):
        log = self.run_card(["4000:RIGHT", "6000:RIGHT", "8000:BACK", "12000:QUIT"])
        closed = log.index("Exiting activity: EpubReader")
        tail = log[closed:]
        written = DEFERRED.search(tail)
        self.assertIsNotNone(written, "the reader's closing writes were not left for after the frame\n" + tail[-3000:])
        # Stats and state.json; since v1.0.14 the new excerpt's recent.json joins them.
        self.assertEqual(written.group(1), "3")
        frame = HOME_FRAME.search(tail)
        self.assertIsNotNone(frame, tail[-3000:])
        self.assertLess(frame.start(), written.start(), "the writes ran before Home was drawn")
        # What was read reached the card all the same.
        book = self.book()
        self.assertEqual(book["path"], BOOK)
        self.assertEqual(book["turns"], 2)
        self.assertGreater(book["minutes"] * 60000 + book["ms"], 0)
        self.assertEqual(json.loads((self.store / "state.json").read_text())["openEpubPath"], BOOK)

    def test_sleep_from_the_book_writes_before_the_device_sleeps(self):
        log = self.run_card(["4000:RIGHT", "6000:RIGHT", "8000:SLEEP", "11000:POWER"])
        slept = log.index("Exiting activity: EpubReader")
        self.assertIsNone(DEFERRED.search(log[slept:log.find("Entering activity", slept)]),
                          "sleep left the stats for a frame that never comes")
        self.assertEqual(self.book()["turns"], 2)


if __name__ == "__main__":
    unittest.main()
