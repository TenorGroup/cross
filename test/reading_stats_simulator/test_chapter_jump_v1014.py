"""v1.0.14: a held page button steps through chapters without reading the TOC from its first entry.

Each step of a turbo hold asks which TOC entry the reader sits on. Every new spine read the TOC
from entry 0 up to that spine, one SD read per entry: a book with thousands of chapters paid
that on every step. A later spine now picks up after the range already found.
"""

import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

import test_chapter_hold as hold

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
SCAN = re.compile(r"TOC_SCAN spine=(\d+) from=(-?\d+) items=(\d+)")
SAVED = re.compile(r"Progress saved: spine=(\d+) offset=\d+ page=(\d+)")
CHAPTERS = 8


class ChapterJumpTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="cross-chapter-jump-")
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        store = self.sd / ".crosspoint"
        store.mkdir()
        (self.sd / "books").mkdir()
        hold.write_epub(self.sd / "books/sach.epub", chuong=CHAPTERS)
        (store / "recent.json").write_text(json.dumps({"books": [{"path": "/books/sach.epub", "title": "Sach"}]}))
        (store / "settings.json").write_text(json.dumps({"language": "VI", "fontSize": 14, "longPressButtonBehavior": 1}))

    def test_turbo_hold_reads_each_toc_entry_once(self):
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT="1000:CONFIRM;3000:RIGHT:3600;9000:QUIT")
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-3000:])
        scans = [(int(s), int(f), int(n)) for s, f, n in SCAN.findall(log)]
        spines = sorted({int(s) for s, _ in SAVED.findall(log)})
        print(f"scans={scans} spines={spines}")
        self.assertGreaterEqual(len(spines), 4, "the hold did not step through chapters")
        self.assertGreaterEqual(len(scans), 3, log[-3000:])
        for spine, start, items in scans[1:]:
            self.assertGreater(start, 0, f"spine {spine} read the TOC from its first entry again")
        # Each entry is read about once over the whole hold, plus the one that ends each scan.
        self.assertLessEqual(sum(items for _, _, items in scans), CHAPTERS + len(scans))


if __name__ == "__main__":
    unittest.main()
