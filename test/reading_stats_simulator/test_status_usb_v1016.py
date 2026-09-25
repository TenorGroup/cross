"""v1.0.16: plugging the cable in or pulling it while a page is up redraws the status bar alone.

The reader left the charging bolt to the next page turn: a repaint was a whole page render, a flash
and the gray pass again. Now the status bar is drawn again over the page frame still in the
framebuffer and sent with one fast refresh. No page render, no gray base, no gray pass; the only
pixels that change are the battery's.

A fast refresh drives only pixels whose black or white value changes. Every gray pixel of a text
page is black in that frame, and the UC8279's fast bank leaves unchanged black pixels undriven, so
there the page keeps its grays. The UC8253's fast bank drives them black for two frames, so there a
page with grays keeps its old battery until the next turn, and a page without grays is redrawn.

The simulator shows the frame of the last refresh, so a gray pixel of the page shows black once the
fast refresh has run: the page is compared as black and white.
"""

import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image

import test_page_turn_presses as base

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
# A simulator_x3 build (UC8253), as test_sleep_ends_bw takes it; run_all passes the UC8279 one.
UC8253_PROGRAM = os.environ.get("SLEEP_UC8253_PROGRAM")
PLUG_MS = 5000
PULL_MS = 7500
REFRESH = re.compile(r"^\[(\d+)\] .*from clearScreen to displayBuffer, mode=(\d+)", re.M)
PAGE_WORK = re.compile(r"^\[(\d+)\] .*(Page render|displayGrayscaleBase|displayGrayBuffer|Loading file)", re.M)
FAST = "2"
# Rows the reader's status bar owns at the default text size (tenorchrome::readerStatusTop).
STATUS_ROWS = 22
BOLT = ["....###.", "...###..", "..###...", ".###....", "########", "########",
        "...###..", "..###...", ".###....", ".##....."]


def ink(path):
    """Black and white view of a screenshot: any gray counts as ink."""
    image = Image.open(path).convert("L")
    return image.size, [value < 255 for value in image.getdata()]


class StatusBarUsbTest(unittest.TestCase):
    def run_reader(self, antialiasing, program=PROGRAM):
        tmp = tempfile.TemporaryDirectory(prefix="cross-status-usb-")
        self.addCleanup(tmp.cleanup)
        sd = Path(tmp.name)
        store = sd / ".crosspoint"
        store.mkdir()
        (sd / "books").mkdir()
        base.write_epub(sd / "books/lat.epub")
        (store / "recent.json").write_text(json.dumps({"books": [{"path": "/books/lat.epub", "title": "Lat"}]}))
        (store / "settings.json").write_text(
            json.dumps({"language": "VI", "fontSize": 14, "textAntiAliasing": 1 if antialiasing else 0}))
        shots = {"battery": PLUG_MS - 200, "cable": PLUG_MS + 1000, "unplugged": PULL_MS + 1000}
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_REFRESH_MS=str(base.REFRESH_MS),
                   CROSSPOINT_SIM_USB_AT=f"{PLUG_MS}:1;{PULL_MS}:0",
                   CROSSPOINT_SIM_INPUT_SCRIPT=f"{base.OPEN_AT_MS}:CONFIRM;{PULL_MS + 1500}:QUIT",
                   CROSSPOINT_SIM_SCREENSHOTS=";".join(f"{ms}:{sd}/{shot}.bmp" for shot, ms in shots.items()))
        run = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-3000:])
        self.assertIn("Entering activity: EpubReader", log)
        return log, {shot: ink(sd / f"{shot}.bmp") for shot in shots}

    def assert_status_alone(self, log, images, name):
        refreshes = [(int(t), mode) for t, mode in REFRESH.findall(log) if int(t) >= PLUG_MS]
        page_work = [(int(t), what) for t, what in PAGE_WORK.findall(log) if int(t) >= PLUG_MS]
        print(f"{name}: refreshes={refreshes} page_work={page_work}")
        self.assertEqual(page_work, [], "the page was laid out, drawn or given a gray pass again")
        plugged = [r for r in refreshes if r[0] < PULL_MS]
        pulled = [r for r in refreshes if r[0] >= PULL_MS]
        self.assertEqual(len(plugged), 1, "plugging in did not redraw the status bar with one refresh")
        self.assertEqual(len(pulled), 1, "pulling the cable did not redraw the status bar with one refresh")
        for t, mode in plugged + pulled:
            self.assertEqual(mode, FAST, "the status bar went out with a slower refresh than a fast one")
        self.assertLess(plugged[0][0] - PLUG_MS, 1000)
        self.assertLess(pulled[0][0] - PULL_MS, 1000)

        (w, h), battery = images["battery"]
        cable = images["cable"][1]
        # The clock sits at the bar's right end and follows the wall clock; the battery is at its left.
        clock = {y * w + x for y in range(h - STATUS_ROWS, h) for x in range(w // 2, w)}
        changed = [i for i in range(w * h) if battery[i] != cable[i] and i not in clock]
        self.assertTrue(changed, "plugging in changed nothing on the page")
        rows = {i // w for i in changed}
        self.assertGreaterEqual(min(rows), h - STATUS_ROWS, "a pixel above the status bar changed")
        cols = [i % w for i in changed]
        x0, y0 = min(cols), min(rows)
        black = {(i % w, i // w) for i in range(w * h) if cable[i] and x0 <= i % w <= max(cols) and y0 <= i // w <= max(rows)}
        bx, by = min(p[0] for p in black), min(p[1] for p in black)
        drawn = ["".join("#" if (bx + i, by + j) in black else "." for i in range(len(BOLT[0])))
                 for j in range(len(BOLT))]
        self.assertEqual(drawn, BOLT, "the battery does not show the charging bolt")
        unplugged = images["unplugged"][1]
        self.assertEqual([i for i in range(w * h) if unplugged[i] != battery[i] and i not in clock], [],
                         "pulling the cable did not bring the battery back")

    def test_plug_and_pull_redraw_the_status_bar_alone(self):
        log, images = self.run_reader(antialiasing=False)
        self.assert_status_alone(log, images, "plain")

    def test_page_with_grays(self):
        log, images = self.run_reader(antialiasing=True)
        self.assert_status_alone(log, images, "gray")

    @unittest.skipUnless(UC8253_PROGRAM, "set SLEEP_UC8253_PROGRAM to a simulator_x3 build")
    def test_uc8253_redraws_only_a_page_without_grays(self):
        log, images = self.run_reader(antialiasing=False, program=UC8253_PROGRAM)
        self.assert_status_alone(log, images, "plain uc8253")
        # The page's grays would darken under the fast bank, so the page stays as it is.
        log, _ = self.run_reader(antialiasing=True, program=UC8253_PROGRAM)
        refreshes = [t for t, _ in REFRESH.findall(log) if int(t) >= PLUG_MS]
        print(f"gray uc8253: refreshes={refreshes}")
        self.assertEqual(refreshes, [], "a UC8253 page with grays was refreshed for its status bar")


if __name__ == "__main__":
    unittest.main()
