"""Home tab wrap keeps the Recent card warm on the real simulator path."""

import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageChops


REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
BUILD = re.compile(r"Recent card build=(\d+)ms cache=(\d+)")


class HomeTabWrapLatencyTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="cross-home-tab-wrap-")
        self.addCleanup(self.tmp.cleanup)
        self.sd = Path(self.tmp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir()
        (self.sd / "book1.txt").write_text(("Mot doan van ban de test. " * 80 + "\n") * 5)
        (self.store / "recent.json").write_text(json.dumps({
            "books": [{
                "path": "/book1.txt",
                "title": "Sach kiem",
                "author": "Tac gia",
                "coverBmpPath": "",
                "excerpt": "Trich doan.",
            }]
        }))
        (self.store / "settings.json").write_text(json.dumps({
            "language": "VI", "uiTheme": 4, "sleepTimeout": 10, "globalStatusBarMode": 0,
        }))
        (self.store / "state.json").write_text(json.dumps({
            "openEpubPath": "", "lastSleepFromReader": False,
            "showBootScreen": False, "readerActivityLoadCount": 0,
        }))
        (self.store / "menu-customization.json").write_text(json.dumps({
            "version": 1,
            "tabs": {
                "home": [0, 2, 1, 3, 4],
                "settings": list(range(7)),
                "reader": list(range(4)),
                "text": list(range(4)),
            },
            "pins": [],
        }))

    def run_sim(self):
        captures = [(900, "recent-before"), (1300, "stats"), (2200, "recent-after")]
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(
            SDL_VIDEODRIVER="dummy",
            CROSSPOINT_SIM_SD=str(self.sd),
            # Custom Home order puts Stats beside Recent: Recent -> Stats -> Recent.
            CROSSPOINT_SIM_INPUT_SCRIPT="1000:DOWN;1600:UP;3000:QUIT",
            CROSSPOINT_SIM_SCREENSHOTS=";".join(
                f"{ms}:{self.sd / (name + '.bmp')}" for ms, name in captures
            ),
        )
        result = subprocess.run(
            [str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=30
        )
        log = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, log[-4000:])
        return log

    def test_wrap_reuses_card_without_a_second_build(self):
        log = self.run_sim()
        self.assertEqual(len(BUILD.findall(log)), 1, log)

        before = Image.open(self.sd / "recent-before.bmp").convert("L")
        after = Image.open(self.sd / "recent-after.bmp").convert("L")
        stats = Image.open(self.sd / "stats.bmp").convert("L")
        self.assertIsNone(
            ImageChops.difference(before.crop((0, 128, before.width, 520)),
                                  after.crop((0, 128, after.width, 520))).getbbox()
        )
        self.assertIsNotNone(ImageChops.difference(before, stats).getbbox())


if __name__ == "__main__":
    unittest.main()
