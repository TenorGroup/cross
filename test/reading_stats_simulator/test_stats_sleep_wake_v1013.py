"""Reading time and page turns survive reading, sleeping and waking on the X3.

Each case runs the real X3 simulator on a temporary card, reads a book through the
normal reader, sleeps through the real power path and wakes as a fresh process
(the X3 on battery loses power in sleep, so a wake is a new boot). The recorded
statistics are then compared with the time the reader was actually on screen,
measured from the firmware's own `Entering/Exiting activity` lines.

Journeys:
  1. read, turn, sleep with the power button, wake into the book, read on, go Home;
  2. presses that land while a page is still painting (the page-turn queue);
  3. the reader left alone until the sleep timeout puts it to sleep;
  4. a card whose per-book folder exists but whose main statistics file is gone;
  5. a save cut between writing the new snapshot and renaming it into place;
  6. the same cut on a per-book snapshot.
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
OTHER = "/books/khac.epub"
LINE = re.compile(r"^\[(\d+)\] \[DBG\] \[ACT\] (Entering|Exiting) activity: EpubReader", re.M)


def book_file(path):
    """File name of a per-book snapshot: FNV-1a 64 of the path, as ReadingStatsStore does."""
    value = 14695981039346656037
    for byte in path.encode():
        value = ((value ^ byte) * 1099511628211) % (1 << 64)
    return f"tenor_{value:016x}.json"


def presses(start_ms, count, gap_ms, key="RIGHT"):
    return [f"{start_ms + i * gap_ms}:{key}" for i in range(count)]


def reader_windows(log):
    """Milliseconds the reader was the current screen, one entry per visit."""
    windows, opened = [], None
    for match in LINE.finditer(log):
        at, kind = int(match.group(1)), match.group(2)
        if kind == "Entering":
            opened = at
        elif opened is not None:
            windows.append(at - opened)
            opened = None
    return windows


class Card:
    def __init__(self, test, settings=None, open_path=BOOK):
        self.tmp = tempfile.TemporaryDirectory(prefix="cross-stats-v1013-")
        test.addCleanup(self.tmp.cleanup)
        self.test = test
        self.sd = Path(self.tmp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir()
        (self.sd / "books").mkdir()
        for path in (BOOK, OTHER):
            shutil.copy(FIXTURE, self.sd / path.lstrip("/"))
        values = {"language": "EN", "clockHasBeenSynced": 1, "clockUtcOffsetQ": 48, "wakeIntoBook": 1}
        values.update(settings or {})
        (self.store / "settings.json").write_text(json.dumps(values))
        (self.store / "state.json").write_text(
            json.dumps({"showBootScreen": False, "openEpubPath": open_path, "lastSleepFromReader": True}))
        # The wake opens the book only while it is in Recent, as it is once it has been opened.
        (self.store / "recent.json").write_text(json.dumps({"books": [{"path": open_path, "title": "Book"}]}))

    def run(self, before, after, extra_env=None, timeout=240):
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_WAKE_REASON="power",
                   CROSSPOINT_SIM_INPUT_SCRIPT=";".join(before),
                   CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE=";".join(after))
        env.update(extra_env or {})
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        self.test.assertEqual(run.returncode, 0, log[-3000:])
        return log

    def stats(self):
        path = self.store / "reading-stats.json"
        self.test.assertTrue(path.exists(), "no statistics were written")
        return json.loads(path.read_text())

    def today(self, data):
        rows = [row for row in data.get("ngay", []) if row[0] != 0]
        return rows[-1] if rows else None


def recorded_ms(row):
    return row[1] * 60000 + (row[3] if len(row) > 3 else 0)


class StatsSleepWakeTest(unittest.TestCase):
    maxDiff = None

    def assert_matches_reader(self, log, data, turns, slack_ms):
        windows = reader_windows(log)
        self.assertTrue(windows, log[-3000:])
        on_screen = sum(windows)
        book = data["activeBook"]
        book_ms = book["minutes"] * 60000 + book["ms"]
        # Time is counted from the first painted page to the moment the reader leaves.
        self.assertLessEqual(book_ms, on_screen, f"more time than the reader was open: {windows}")
        self.assertGreaterEqual(book_ms, on_screen - slack_ms, f"reading time lost: {book_ms} of {windows}")
        self.assertEqual(book["turns"], turns)
        row = next(r for r in data["ngay"] if r[0] == book["last"])
        self.assertEqual(recorded_ms(row), book_ms, "daily total and book total disagree")
        self.assertEqual(row[2], turns)
        return on_screen, book_ms

    def test_1_read_sleep_wake_read_home(self):
        card = Card(self)
        log = card.run(presses(4000, 6, 2000) + ["18000:SLEEP", "21000:POWER"],
                       presses(3000, 2, 2000) + ["8000:BACK", "10000:QUIT"])
        self.assertEqual(len(reader_windows(log)), 2, "the wake did not reopen the book")
        self.assert_matches_reader(log, card.stats(), 8, 2500)

    def test_2_presses_during_a_paint_are_all_counted(self):
        card = Card(self)
        log = card.run(presses(4000, 8, 150) + ["20000:SLEEP", "32000:POWER"], ["3000:QUIT"],
                       {"CROSSPOINT_SIM_REFRESH_MS": "1500"})
        # A paint holds the render lock for 1.5 s, so most presses wait in the queue.
        self.assert_matches_reader(log, card.stats(), 8, 6000)

    def test_3_sleep_timeout(self):
        card = Card(self, {"sleepTimeoutMinutes": 1})
        log = card.run(presses(4000, 4, 2000) + ["90000:POWER"], ["3000:QUIT"], timeout=300)
        self.assertIn("Auto-sleep triggered", log)
        self.assert_matches_reader(log, card.stats(), 4, 2500)

    def test_4_book_folder_without_main_file(self):
        card = Card(self)
        (card.store / "reading-stats").mkdir()
        log = card.run(presses(4000, 3, 2000) + ["12000:SLEEP", "15000:POWER"], ["3000:QUIT"])
        self.assert_matches_reader(log, card.stats(), 3, 2500)

    def test_5_save_cut_before_rename_keeps_the_new_snapshot(self):
        card = Card(self)
        (card.store / "reading-stats").mkdir()
        earlier = {"schema": 3, "bookEpoch": 0, "ngay": [[20260101, 20, 50]],
                   "activeBook": {"bookEpoch": 0, "path": BOOK, "title": "t", "minutes": 20, "ms": 0,
                                  "turns": 50, "first": 20260101, "last": 20260101, "days": 1,
                                  "progress": 10, "startProgress": 0}}
        (card.store / "reading-stats.json.tmp").write_text(json.dumps(earlier))
        log = card.run(presses(4000, 3, 2000) + ["12000:SLEEP", "15000:POWER"], ["3000:QUIT"])
        data = card.stats()
        self.assertEqual(data["ngay"][0], [20260101, 20, 50], "the interrupted snapshot was dropped")
        book = data["activeBook"]
        self.assertEqual(book["turns"], 53)
        self.assertEqual(book["minutes"], 20)
        self.assertGreater(book["ms"], 0)
        self.assertEqual(len(reader_windows(log)), 1)

    def test_6_book_snapshot_cut_before_rename(self):
        card = Card(self, open_path=OTHER)
        folder = card.store / "reading-stats"
        folder.mkdir()
        (card.store / "reading-stats.json").write_text(json.dumps({
            "schema": 3, "bookEpoch": 0, "ngay": [[20260101, 15, 40]],
            "activeBook": {"bookEpoch": 0, "path": BOOK, "title": "t", "minutes": 0, "ms": 0, "turns": 0,
                           "first": 0, "last": 0, "days": 0, "progress": 0, "startProgress": 0}}))
        other = {"bookEpoch": 0, "path": OTHER, "title": "k", "minutes": 15, "ms": 0, "turns": 40,
                 "first": 20260101, "last": 20260101, "days": 1, "progress": 30, "startProgress": 0}
        (folder / (book_file(OTHER) + ".tmp")).write_text(json.dumps(other))
        card.run(presses(4000, 2, 2000) + ["10000:SLEEP", "13000:POWER"], ["3000:QUIT"])
        book = card.stats()["activeBook"]
        self.assertEqual(book["path"], OTHER)
        self.assertEqual(book["minutes"], 15, "the book's earlier reading time was dropped")
        self.assertEqual(book["turns"], 42)


if __name__ == "__main__":
    unittest.main()
