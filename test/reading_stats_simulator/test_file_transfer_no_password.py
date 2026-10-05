"""Hành trình Gửi file qua chooser ba lựa chọn trên simulator X3."""

import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("CROSSPOINT_SIM_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))

# Home: UP mở Cài đặt tại Hiển thị; RIGHT x5 tới Gửi file, đầu nhóm máy.
DI_DEN_CHOOSER = "1500:UP;1800:RIGHT;2100:RIGHT;2400:RIGHT;2700:RIGHT;2900:RIGHT;3100:CONFIRM"
ENTERING = re.compile(r"Entering activity: (\S+)")


class FileTransferChooserTest(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="cross-file-transfer-")
        self.addCleanup(self.tmp.cleanup)
        self.sd = Path(self.tmp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir()
        self._env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}

    def dat_settings(self, **them):
        settings = {"language": "VI", "uiTheme": 4, "sleepTimeout": 10, "globalStatusBarMode": 0}
        settings.update(them)
        (self.store / "settings.json").write_text(json.dumps(settings))

    def dat_state(self, **them):
        state = {"openEpubPath": "", "lastSleepFromReader": False, "showBootScreen": False,
                 "readerActivityLoadCount": 0}
        state.update(them)
        (self.store / "state.json").write_text(json.dumps(state))

    def dat_mang_luu(self, ssid):
        (self.store / "wifi.json").write_text(
            json.dumps({"lastConnectedSsid": ssid, "credentials": [{"ssid": ssid}]}))

    def chay(self, tail="9000:QUIT", env_them=None, timeout=45):
        if not (self.store / "settings.json").exists():
            self.dat_settings()
        if not (self.store / "state.json").exists():
            self.dat_state()
        script = f"{DI_DEN_CHOOSER};{tail}"
        env = dict(self._env, SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=script)
        env.update(env_them or {})
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, f"simulator exit {run.returncode}\n{log[-4000:]}")
        self.assertIn("Entering activity: CrossPointWebServer", log,
                      f"khong toi duoc man Gui file\n{log[-4000:]}")
        return log

    @staticmethod
    def da_vao(log):
        return ENTERING.findall(log)

    def assert_chooser_dau_tien(self, log):
        vao = self.da_vao(log)
        transfer = vao.index("CrossPointWebServer")
        self.assertEqual(vao[transfer + 1], "NetworkModeSelection", log[-4000:])

    def test_1_khong_co_mang_luu_van_hien_chooser(self):
        log = self.chay()
        self.assert_chooser_dau_tien(log)
        self.assertNotIn("Network mode: AP", log)
        self.assertNotIn("Network mode: STA", log)

    def test_2_co_mang_luu_van_hien_chooser(self):
        self.dat_mang_luu("Nha Cua Toi")
        log = self.chay()
        self.assert_chooser_dau_tien(log)
        self.assertNotIn("Attempting saved network: Nha Cua Toi", log)

    def test_3_ket_noi_mang_mo_picker_va_back_ve_chooser(self):
        log = self.chay("4400:CONFIRM;6500:BACK:80;9000:QUIT")
        vao = self.da_vao(log)
        self.assert_chooser_dau_tien(log)
        self.assertIn("WifiSelection", vao)
        self.assertEqual(vao.count("NetworkModeSelection"), 2, log[-4000:])

    def test_4_calibre_mo_picker_va_back_ve_chooser(self):
        log = self.chay("4200:RIGHT;5000:CONFIRM;7000:BACK:80;10000:QUIT")
        vao = self.da_vao(log)
        self.assert_chooser_dau_tien(log)
        self.assertIn("CalibreConnect", vao)
        self.assertIn("WifiSelection", vao)
        self.assertEqual(vao.count("NetworkModeSelection"), 2, log[-4000:])

    def test_5_tao_diem_phat_chay_ap(self):
        log = self.chay("4200:RIGHT;4800:RIGHT;5600:CONFIRM;9500:QUIT")
        self.assert_chooser_dau_tien(log)
        self.assertIn("Network mode: AP", log)
        self.assertNotIn("WifiSelection", self.da_vao(log))
        self.assertNotIn("KeyboardEntry", self.da_vao(log))

    def test_6_mot_back_tu_chooser_thoat_gui_file(self):
        log = self.chay("4400:BACK:80;7500:QUIT")
        self.assert_chooser_dau_tien(log)
        self.assertEqual(log.count("Exiting activity: CrossPointWebServer"), 1, log[-4000:])
        self.assertEqual(self.da_vao(log).count("Home"), 2, log[-4000:])

    def test_7_mot_back_thoat_server_diem_phat(self):
        log = self.chay("4200:RIGHT;4800:RIGHT;5600:CONFIRM;8500:BACK:80;11000:QUIT")
        self.assertIn("Network mode: AP", log)
        self.assertEqual(log.count("Exiting activity: CrossPointWebServer"), 1, log[-4000:])

    def test_8_mot_back_thoat_server_mang_da_luu(self):
        self.dat_mang_luu("Nha Cua Toi")
        log = self.chay("4400:CONFIRM;9000:BACK:80;12000:QUIT")
        self.assertIn("Attempting saved network: Nha Cua Toi", log)
        self.assertIn("Network mode: STA", log)
        self.assertEqual(log.count("Exiting activity: CrossPointWebServer"), 1, log[-4000:])


if __name__ == "__main__":
    unittest.main()
