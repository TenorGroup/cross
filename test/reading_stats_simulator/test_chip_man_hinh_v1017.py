"""v1.0.17: the last row of the Device tab in Settings names the panel chip, read only.

An owner of a domestic X3 reporting ink trouble sends a photo of this row: the controller the
boot chose, the three VER bytes of the boot probe (a wake keeps them in RTC memory) and the OEM
record hw_calib/screenType. The simulator has neither probe nor NVS, so its row reads UC8279
alone. Evidence: the firmware logs the row value when the Device tab draws it, a Select on the
row opens nothing and writes no setting, and the tab screenshot lands in CROSSPOINT_TEST_ARTIFACTS.

Route (as test_ble_settings_screen.py): Home, DOWN x4 = Settings card, RIGHT x7 = Device group (after
the Motion sensor and System groups),
CONFIRM opens Settings on that tab at row 1, LEFT wraps to the last row.
"""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
BEAT_MS = 1200


class DisplayChipRowTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="cross-chip-row-")
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir()
        (self.store / "settings.json").write_text(json.dumps({"language": "VI"}))
        (self.store / "state.json").write_text(
            json.dumps({"openEpubPath": "", "lastSleepFromReader": False, "showBootScreen": False}))
        artifacts = os.environ.get("CROSSPOINT_TEST_ARTIFACTS")
        self.artifacts = Path(artifacts) if artifacts else None

    def run_keys(self, keys, shots):
        script = ";".join(f"{2000 + i * BEAT_MS}:{key}" for i, key in enumerate(keys)) + ";"
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script)
        if self.artifacts:
            self.artifacts.mkdir(parents=True, exist_ok=True)
            env["CROSSPOINT_SIM_SCREENSHOTS"] = ";".join(
                f"{ms}:{self.artifacts / (name + '.bmp')}" for ms, name in shots)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-4000:])
        return log

    def test_device_tab_ends_with_the_panel_chip_read_only(self):
        keys = ["DOWN"] * 4 + ["RIGHT"] * 7 + ["CONFIRM", "LEFT", "CONFIRM", "BACK", "QUIT"]
        opened = 2000 + 11 * BEAT_MS
        before = (self.store / "settings.json").read_text()
        log = self.run_keys(keys, [(opened + 900, "v1017-chip-thiet-bi"),
                                   (opened + 2 * BEAT_MS + 900, "v1017-chip-dong-cuoi")])
        self.assertEqual(log.count("Entering activity: Settings"), 1, log[-3000:])
        # The value is read when the Device tab draws the row, once per boot.
        self.assertEqual(log.count("Display chip: UC8279"), 1, log[-3000:])
        self.assertNotIn("Display chip: UC8279, VER", log)
        # Select on the last row opens nothing: Settings is the only activity entered after Home.
        entered = [line.split("Entering activity: ", 1)[1].strip() for line in log.splitlines()
                   if "Entering activity: " in line]
        self.assertEqual(entered[entered.index("Settings") + 1:], [], entered)
        after = json.loads((self.store / "settings.json").read_text())
        self.assertEqual({k: v for k, v in after.items() if k in json.loads(before)}, json.loads(before))


if __name__ == "__main__":
    unittest.main()
