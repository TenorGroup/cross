"""KIỂM: "Gửi file" trên máy nút bấm không bao giờ bắt gõ mật khẩu.

Máy không cảm ứng (X3): bấm "Gửi file" phải đi thẳng vào điểm phát mở khi chưa
có mạng lưu, hoặc tự nối mạng đã lưu khi có. Không hiện màn chọn chế độ, không
mở màn chọn mạng, không mở bàn phím.

Máy cảm ứng giữ nguyên hành vi cũ (màn chọn chế độ) — không kiểm ở đây vì giả
lập X3 không có cảm ứng.
"""

import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = REPO / ".pio/build/simulator_x3_uc8279/program"

# Home: thẻ Cài đặt (CAI_DAT) cách thẻ Gần đây ba nhịp RIGHT, dòng đầu của nó là
# "Gửi file"; con trỏ mở màn ở dòng 1 nên cần một nhịp UP.
DI_DEN_GUI_FILE = "1500:RIGHT;1900:RIGHT;2300:RIGHT;2700:UP;3100:CONFIRM;9000:QUIT"

# `[3180] [DBG] [ACT] Entering activity: CrossPointWebServer`
ENTERING = re.compile(r"Entering activity: (\S+)")


class FileTransferNoPasswordTest(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="cross-file-transfer-")
        self.addCleanup(self.tmp.cleanup)
        self.sd = Path(self.tmp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir()
        self._env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}

    # --- fixture ------------------------------------------------------------
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
        """wifi.json với một mạng đã lưu, không mật khẩu (mạng mở)."""
        (self.store / "wifi.json").write_text(
            json.dumps({"lastConnectedSsid": ssid, "credentials": [{"ssid": ssid}]}))

    # --- chạy ---------------------------------------------------------------
    def chay(self, script=DI_DEN_GUI_FILE, env_them=None, timeout=45):
        if not (self.store / "settings.json").exists():
            self.dat_settings()
        if not (self.store / "state.json").exists():
            self.dat_state()
        env = dict(self._env, SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=script)
        env.update(env_them or {})
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, f"simulator exit {run.returncode}\n{log[-4000:]}")
        self.assertIn("Entering activity: CrossPointWebServer", log,
                      f"khong toi duoc man Gui file\n{log[-4000:]}")
        return log

    def da_vao(self, log):
        """Danh sach activity da vao, theo thu tu."""
        return ENTERING.findall(log)

    def khong_hoi_gi(self, log):
        """Khong bao gio hien man chon che do hay ban phim."""
        vao = self.da_vao(log)
        for ten in ("NetworkModeSelection", "KeyboardEntry"):
            self.assertNotIn(ten, vao, f"man {ten} khong duoc hien tren may nut bam\n{log[-4000:]}")

    # --- 1. chua co mang luu: vao thang diem phat mo -------------------------
    def test_1_khong_co_mang_luu_thi_vao_thang_diem_phat(self):
        self.dat_settings()
        self.dat_state()
        log = self.chay()

        self.assertIn("Network mode: AP", log, f"khong chay diem phat\n{log[-4000:]}")
        self.khong_hoi_gi(log)
        self.assertNotIn("WifiSelection", self.da_vao(log), "khong co mang luu thi khong mo man chon mang")
        self.assertNotIn("WIFI:T:WPA", log, "ma QR Wi-Fi con khoa WPA")

    # --- 2. co mang luu: tu noi, khong hoi ----------------------------------
    def test_2_co_mang_luu_thi_tu_noi_khong_hoi(self):
        self.dat_settings()
        self.dat_state()
        self.dat_mang_luu("Nha Cua Toi")
        log = self.chay()

        self.assertIn("Attempting saved network: Nha Cua Toi", log,
                      f"khong thu noi mang da luu\n{log[-4000:]}")
        self.assertIn("Network mode: STA", log, f"khong chay che do noi mang\n{log[-4000:]}")
        self.khong_hoi_gi(log)

    # --- 3. mang luu noi that bai: van khong hoi ----------------------------
    def test_3_mang_luu_that_bai_thi_van_vao_diem_phat(self):
        self.dat_settings()
        self.dat_state()
        self.dat_mang_luu("Nha Cua Toi")
        log = self.chay(env_them={"CROSSPOINT_SIM_WIFI_CONNECT": "fail"})

        self.assertIn("Attempting saved network: Nha Cua Toi", log, log[-4000:])
        self.assertIn("Network mode: AP", log, f"khong lui ve diem phat\n{log[-4000:]}")
        self.khong_hoi_gi(log)


if __name__ == "__main__":
    unittest.main()
