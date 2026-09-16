"""Exercise the real reader and SD persistence in separate simulator processes."""

import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
PROGRAM = REPO / ".pio/build/simulator_x3_uc8279/program"


class ReadingStatsSimulatorTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cross-reading-stats-")
        self.addCleanup(self.temp.cleanup)
        self.sd = Path(self.temp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir()
        (self.sd / "audit.txt").write_text(
            "Reading statistics fixture with enough text for several pages.\n" * 200,
            encoding="utf-8",
        )
        (self.store / "settings.json").write_text(
            json.dumps({"language": "EN", "sleepTimeout": 10}), encoding="utf-8"
        )

    def run_reader(self, buttons):
        # Open the first book through Home > Folder, turn pages, then exit to Home.
        events = ["1000:DOWN", "1800:CONFIRM", "2600:CONFIRM"]
        events.extend(f"{4200 + i * 1000}:{button}" for i, button in enumerate(buttons))
        end = 4200 + len(buttons) * 1000
        events.extend([f"{end}:BACK", f"{end + 1500}:QUIT"])
        env = os.environ.copy()
        for key in list(env):
            if key.startswith("CROSSPOINT_SIM_"):
                del env[key]
        env.update(
            SDL_VIDEODRIVER="dummy",
            CROSSPOINT_SIM_SD=str(self.sd),
            CROSSPOINT_SIM_INPUT_SCRIPT=";".join(events),
        )
        run = subprocess.run(
            [str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=25
        )
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log)
        self.assertIn("Entering activity: TxtReader", log)
        self.assertIn("Exiting activity: TxtReader", log)
        path = self.store / "reading-stats.json"
        return json.loads(path.read_text()) if path.exists() else {}

    def test_history_survives_two_boots(self):
        history = {"ngay": [[20260913, 12, 30]], "lacPhut": 5, "lacTrang": 7}
        (self.store / "reading-stats.json").write_text(json.dumps(history), encoding="utf-8")
        for expected_turns in (9, 11):
            actual = self.run_reader(["RIGHT", "RIGHT"])
            with self.subTest(expected_turns=expected_turns):
                self.assertEqual(actual.get("ngay"), history["ngay"])
                self.assertEqual(actual.get("lacPhut"), 5)
                self.assertEqual(actual.get("lacTrang"), expected_turns)

    def test_back_at_first_page_does_not_count(self):
        actual = self.run_reader(["LEFT", "LEFT", "LEFT"])
        self.assertEqual(actual.get("ngay", []), [])
        self.assertEqual(actual.get("lacTrang", 0), 0)
        self.assertEqual(actual.get("lacPhut", 0), 0)

    def test_load_keeps_newest_days_and_rejects_invalid_counters(self):
        dates = [int((datetime.date(2026, 8, 1) + datetime.timedelta(days=i)).strftime("%Y%m%d"))
                 for i in range(35)]
        rows = [[day, 1, 2] for day in dates]
        rows += [[20260815, 3, 4], [20260914, -1, 2], [20260915, "7", 0],
                 [0, 10, 20], [20260916, 70000, 0], [20260917, True, 1]]
        (self.store / "reading-stats.json").write_text(json.dumps({
            "ngay": rows, "lacPhut": -1, "lacTrang": 7,
        }))
        actual = self.run_reader(["RIGHT", "RIGHT"])
        expected = [[day, 4, 6] if day == 20260815 else [day, 1, 2] for day in dates[-30:]]
        self.assertEqual(actual["ngay"], expected)
        self.assertEqual(actual.get("lacPhut", 0), 0)
        self.assertEqual(actual.get("lacTrang", 0), 9)

    def test_interrupted_checkpoint_recovers_previous_snapshot(self):
        (self.store / "reading-stats.json").write_text('{"ngay":[')
        (self.store / "reading-stats.json.bak").write_text(json.dumps({
            "ngay": [[20260913, 12, 30]], "lacMs": 59000, "lacTrang": 7}))
        actual = self.run_reader(["RIGHT", "RIGHT"])
        self.assertEqual(actual["ngay"], [[20260913, 12, 30]])
        self.assertGreaterEqual(actual.get("lacPhut", 0), 1)
        self.assertEqual(actual["lacTrang"], 9)

    def test_epub_menu_time_is_paused_and_active_book_matches_daily_checkpoint(self):
        import shutil
        shutil.copy(REPO / "test/epubs/test_dictionary_synonyms.epub", self.sd / "audit.epub")
        (self.sd / "audit.txt").unlink()
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(self.sd),
            CROSSPOINT_SIM_INPUT_SCRIPT="1000:DOWN;1800:CONFIRM;5000:CONFIRM;14000:BACK;16000:BACK;17000:QUIT")
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=25)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log)
        self.assertIn("Entering activity: EpubReaderMenu", log)
        data = json.loads((self.store / "reading-stats.json").read_text())
        measured = data.get("lacPhut", 0) * 60000 + data.get("lacMs", 0)
        self.assertGreater(measured, 2500, log)
        self.assertLess(measured, 6500, log)
        book = data["activeBook"]
        self.assertEqual(book["minutes"] * 60000 + book["ms"], measured)
        self.assertEqual(book["path"], "/audit.epub")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", type=Path, default=PROGRAM)
    args, remaining = parser.parse_known_args()
    PROGRAM = args.program.resolve()
    if not PROGRAM.is_file():
        parser.error(f"Build the simulator first: {PROGRAM}")
    unittest.main(argv=[__file__, *remaining])
