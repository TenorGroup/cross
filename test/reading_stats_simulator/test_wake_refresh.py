"""Observe the refresh requested by the real Home screen on a persisted wake."""

import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]


class WakeRefreshTest(unittest.TestCase):
    def check_wake(self, quick_resume):
        with tempfile.TemporaryDirectory(prefix="cross-wake-") as directory:
            sd = Path(directory)
            store = sd / ".crosspoint"
            store.mkdir()
            (store / "state.json").write_text(json.dumps({"showBootScreen": False}))
            (store / "settings.json").write_text(json.dumps({"language": "EN"}))
            if quick_resume:
                (store / "sleep_frame.bin").write_bytes(b"\xff" * (528 * 792 // 8))
            env = os.environ.copy()
            for key in list(env):
                if key.startswith("CROSSPOINT_SIM_"):
                    del env[key]
            env.update(
                SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                CROSSPOINT_SIM_WAKE_REASON="power",
                CROSSPOINT_SIM_INPUT_SCRIPT="1200:DOWN;2200:DOWN;3500:QUIT",
            )
            run = subprocess.run(
                [str(REPO / ".pio/build/simulator_x3_uc8279/program")],
                cwd=REPO, env=env, capture_output=True, text=True, timeout=15,
            )
            log = run.stdout + run.stderr
            self.assertEqual(run.returncode, 0, log)
            self.assertIn("Entering activity: Home", log)
            self.assertNotIn("Entering activity: Boot", log)
            modes = re.findall(r"(?:displayBuffer|displayGrayscaleBase), mode=(\d)", log)
            self.assertGreaterEqual(len(modes), 3, log)
            self.assertEqual(modes[0], "0", log)
            self.assertTrue(all(mode == "2" for mode in modes[1:]), log)  # FAST_REFRESH
            self.assertNotIn("displayGrayscaleBase", log)

    def test_home_wake_cleans_once_then_uses_fast_refresh(self):
        self.check_wake(False)

    def test_quick_resume_cleans_saved_frame_once(self):
        self.check_wake(True)

    def test_tenor_sleep_and_wake_each_paint_once(self):
        with tempfile.TemporaryDirectory(prefix="cross-sleep-cycle-") as directory:
            sd = Path(directory)
            store = sd / ".crosspoint"
            store.mkdir()
            (store / "settings.json").write_text(json.dumps({"language": "EN", "sleepScreen": 8}))
            env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
            env.update(
                SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                CROSSPOINT_SIM_INPUT_SCRIPT="2500:SLEEP;4000:POWER;9000:QUIT",
                CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE="1500:DOWN;3000:QUIT",
            )
            run = subprocess.run(
                [str(REPO / ".pio/build/simulator_x3_uc8279/program")],
                cwd=REPO, env=env, capture_output=True, text=True, timeout=18,
            )
            log = run.stdout + run.stderr
            self.assertEqual(run.returncode, 0, log)
            self.assertIn("Entering activity: Sleep", log)
            sleep, wake = log.split("Entering activity: Sleep", 1)[1].split("Entering deep sleep", 1)
            self.assertEqual(re.findall(r"displayBuffer, mode=(\d)", sleep), ["0"], log)
            self.assertIn("Restored sleep frame baseline", wake)
            self.assertEqual(re.findall(r"displayBuffer, mode=(\d)", wake), ["0", "2"], log)
            self.assertNotIn("displayGrayscaleBase", wake)

    def test_epub_wake_has_no_intermediate_loading_frame(self):
        for sleep_screen in (8, 6):
            with self.subTest(sleep_screen=sleep_screen), tempfile.TemporaryDirectory(prefix="cross-reader-wake-") as directory:
                sd = Path(directory)
                store = sd / ".crosspoint"
                store.mkdir()
                shutil.copy(REPO / "test/epubs/test_dictionary_synonyms.epub", sd / "audit.epub")
                (store / "settings.json").write_text(json.dumps({
                    "language": "EN", "sleepScreen": sleep_screen, "textAntiAliasing": 0,
                }))
                env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
                env.update(
                    SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                    CROSSPOINT_SIM_INPUT_SCRIPT="1000:DOWN;1800:CONFIRM;2600:CONFIRM;5000:SLEEP;6500:POWER;12000:QUIT",
                    CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE="2500:QUIT",
                )
                run = subprocess.run(
                    [str(REPO / ".pio/build/simulator_x3_uc8279/program")],
                    cwd=REPO, env=env, capture_output=True, text=True, timeout=20,
                )
                log = run.stdout + run.stderr
                self.assertEqual(run.returncode, 0, log)
                self.assertIn("Entering deep sleep", log)
                wake = log.split("Entering deep sleep", 1)[1]
                self.assertIn("Restored sleep frame baseline", wake)
                self.assertIn("Entering activity: EpubReader", wake)
                self.assertNotIn("Entering activity: Home", wake)
                self.assertEqual(re.findall(r"displayBuffer, mode=(\d)", wake), ["1"], log)
                self.assertNotIn("displayGrayscaleBase", wake)

    def test_custom_gray_sleep_restores_baseline_on_wake(self):
        with tempfile.TemporaryDirectory(prefix="cross-gray-sleep-") as directory:
            sd = Path(directory)
            store = sd / ".crosspoint"
            store.mkdir()
            # A real 24-bit BMP with all four target levels, no image dependencies.
            pixels = b"".join(bytes([level, level, level]) * 8 for level in (0, 85, 170, 255)) * 32
            header = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
            header += struct.pack("<IiiHHIIiiII", 40, 32, 32, 1, 24, 0, len(pixels), 0, 0, 0, 0)
            (sd / "sleep.bmp").write_bytes(header + pixels)
            (store / "settings.json").write_text(json.dumps({"language": "EN", "sleepScreen": 2}))
            env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
            env.update(
                SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                CROSSPOINT_SIM_INPUT_SCRIPT="2500:SLEEP;5000:POWER;12000:QUIT",
                CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE="1500:DOWN;3000:QUIT",
            )
            run = subprocess.run(
                [str(REPO / ".pio/build/simulator_x3_uc8279/program")],
                cwd=REPO, env=env, capture_output=True, text=True, timeout=22,
            )
            log = run.stdout + run.stderr
            self.assertEqual(run.returncode, 0, log)
            self.assertIn("Entering deep sleep", log)
            wake = log.split("Entering deep sleep", 1)[1]
            self.assertIn("Restored sleep frame baseline", wake)
            self.assertIn("Sleep image 32x32, absolute=1", log)
            self.assertEqual(re.findall(r"displayBuffer, mode=(\d)", wake), ["0", "2"], log)


if __name__ == "__main__":
    unittest.main()
