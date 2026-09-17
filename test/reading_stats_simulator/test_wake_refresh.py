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
            # Nhip 17/09/2026: man ngu Tenor (sleepScreen=8) ve anh XAM bang duong grayscale cua
            # X3BrandScreen (displayGrayscaleBase -> copyGrayscaleLsb/Msb -> displayGrayBuffer,
            # src/components/X3BrandScreen.cpp:37-43), khong con di qua displayBuffer(mode=...)
            # nhu tien de cu. Y dinh bai giu nguyen: moi buoc ve chi chay MOT lan.
            # Trong ban simulator duong xam khong tu log tung buoc, dau vet duy nhat cua no la
            # dong "[BRAND] sleep ready=" o cuoi X3BrandScreen, nen dem dong do de bat ca ve hai lan.
            self.assertEqual(re.findall(r"displayBuffer, mode=(\d)", sleep), [], log)
            self.assertEqual(log.count("[BRAND] sleep ready=1"), 1, log)
            self.assertIn("Restored sleep frame baseline", wake)
            self.assertEqual(re.findall(r"displayBuffer, mode=(\d)", wake), ["0", "2"], log)
            # Thuc day khong duoc ve lai anh ngu lan nua.
            self.assertNotIn("[BRAND] sleep ready=", wake)

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
                # Nhip 17/09/2026: mot muc recent.json de the GAN DAY co dung mot hang; nho do MOT
                # nhip CONFIRM mo duoc sach truoc khi ngu (truoc day the rong nen khong mo duoc).
                (store / "recent.json").write_text(json.dumps({"books": [{"path": "/audit.epub", "title": "Audit"}]}))
                env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
                env.update(
                    SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                    # Nhip 17/09/2026: the GAN DAY co dung mot hang (recent.json o tren) nen MOT nhip
                    # CONFIRM mo cuon fixture; nhip cu '1000:DOWN;1800:CONFIRM' doi the Home thay vi
                    # buoc con tro, nen sach khong bao gio mo duoc.
                    CROSSPOINT_SIM_INPUT_SCRIPT="1000:CONFIRM;4200:SLEEP;5700:POWER;12000:QUIT",
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
                # HANH VI DA CHOT TU v1.0.2 (commit 9b40981 "Prepare v1.0.2 source candidate"):
                # thuc day bang nut nguon di thang ve MAN HINH CHINH voi dong "Doc tiep"
                # (`activityManager.goHome(HomeMenuItem::RECENT_CONTINUE, needsWakeRefresh)`),
                # khong tu mo lai trinh doc. Ky vong cu cua bai nay (thuc day phai vao lai
                # trinh doc) la tien de da cu, da doi lai theo ma nguon da phat hanh.
                self.assertIn("Entering activity: Home", wake)
                self.assertNotIn("Entering activity: EpubReader", wake)
                # Khong co khung trung gian: vi thuc day ve MAN HINH CHINH (khong co trang sach
                # de lam moi cuc bo) nen chi dung MOT lan ve, che do 0; neu sau nay doi ve lai
                # trinh doc thi bai nay phai doi thanh hai lan nhu bai Tenor o tren.
                self.assertEqual(re.findall(r"displayBuffer, mode=(\d)", wake), ["0"], log)
                self.assertNotIn("[BRAND] sleep ready=", wake)

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
