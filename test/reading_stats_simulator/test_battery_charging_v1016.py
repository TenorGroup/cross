"""v1.0.16: the Tenor status battery shows a charging bolt inside its body while USB power is in.

The footer battery on Home (and the reader status bar, same function) read only the percentage, so
a reader on the cable looked the same as one on battery. Charging now draws the bolt alone in the
body; on battery the body holds the level as before. CROSSPOINT_SIM_USB=1 plugs the simulator in.
"""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
BOLT = ["....###.", "...###..", "..###...", ".###....", "########", "########",
        "...###..", "..###...", ".###....", ".##....."]


class BatteryChargingTest(unittest.TestCase):
    def home(self, usb):
        tmp = tempfile.TemporaryDirectory(prefix="cross-charging-")
        self.addCleanup(tmp.cleanup)
        sd = Path(tmp.name)
        store = sd / ".crosspoint"
        store.mkdir()
        (store / "settings.json").write_text(json.dumps({"language": "VI"}))
        (store / "state.json").write_text(json.dumps({"showBootScreen": False, "openEpubPath": ""}))
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT="3000:QUIT",
                   CROSSPOINT_SIM_SCREENSHOTS=f"2500:{sd}/home.bmp")
        if usb:
            env["CROSSPOINT_SIM_USB"] = "1"
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-3000:])
        return Image.open(sd / "home.bmp").convert("1")

    def test_bolt_inside_the_body_only_while_charging(self):
        battery, cable = self.home(False), self.home(True)
        self.assertEqual(battery.size, cable.size)
        w, h = battery.size
        diff = [(x, y) for y in range(h) for x in range(w) if battery.getpixel((x, y)) != cable.getpixel((x, y))]
        self.assertTrue(diff, "plugging in changed nothing on Home: no charging mark")
        xs = [p[0] for p in diff]
        ys = [p[1] for p in diff]
        # Every changed pixel sits inside the 24 x 14 battery body of the footer.
        self.assertLessEqual(max(xs) - min(xs), 21, f"change spreads past the battery body: {min(xs)}..{max(xs)}")
        self.assertLessEqual(max(ys) - min(ys), 11, f"change spreads past the battery body: {min(ys)}..{max(ys)}")
        self.assertGreater(min(ys), h * 3 // 4, "change is not in the footer")
        # On the cable the black pixels in that box are exactly the bolt.
        x0, y0 = min(xs), min(ys)
        black = {(x, y) for y in range(y0, max(ys) + 1) for x in range(x0, max(xs) + 1) if cable.getpixel((x, y)) == 0}
        bx, by = min(p[0] for p in black), min(p[1] for p in black)
        drawn = ["".join("#" if (bx + i, by + j) in black else "." for i in range(len(BOLT[0])))
                 for j in range(len(BOLT))]
        self.assertEqual(drawn, BOLT, "the charging mark is not the bolt")


if __name__ == "__main__":
    unittest.main()
