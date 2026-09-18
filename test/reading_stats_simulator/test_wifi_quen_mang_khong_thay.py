"""KIEM: mang da luu nhung KHONG quet thay van hien ra de xoa duoc.

Ca that cua nguoi dung: luu nham mot mang (go sai ten, hoac sai mat khau roi
luu). Mang do khong bao gio phat song dung cai ten da luu, nen no khong bao gio
nam trong ket qua quet, nen danh sach mang khong co dong nao cho no, nen tren
may KHONG co duong nao xoa no. No ket lai trong wifi.json.

Bai nay dung dung canh do: danh sach quet gia (CROSSPOINT_SIM_WIFI_NETWORKS)
khong chua ten da luu. Do duoc:
  - log `[WIFI] Saved network not in range: <ten>` khi dung dong cho no;
  - sau khi giu nut Trai roi chon Quen, wifi.json khong con ten do nua.

Ban do dieu huong: giong test_ble_settings_screen.py. Home -> DOWN x4 = the Cai
dat -> RIGHT x5 = nhom `Thiet bi` -> CONFIRM mo man Cai dat tai nhom do. Trong
nhom do vong bon dong: 1 Ngon ngu, 2 Ten may, 3 Mang Wi-Fi, 4 Ble page turner
(thu tu do DONG_HANH_DONG trong SettingsActivity.cpp quyet dinh). Tu dong 1 bam
RIGHT hai nhip la toi dong 3.
"""

import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))

# Ten da luu, co dau cach va khong nam trong danh sach quet gia duoi day.
TEN_DA_LUU = "Mang Go Nham"
QUET_GIA = "Mang Hang Xom:-55:3;Quan Ca Phe:-70:3"

NHIP_MS = 1200
KHONG_THAY = re.compile(r"Saved network not in range: (.+)")


class WifiQuenMangKhongThayTest(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="cross-wifi-quen-")
        self.addCleanup(self.tmp.cleanup)
        self.sd = Path(self.tmp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir()
        (self.store / "settings.json").write_text(json.dumps({"language": "VI"}))
        (self.store / "state.json").write_text(
            json.dumps({"openEpubPath": "", "lastSleepFromReader": False, "showBootScreen": False}))
        (self.store / "wifi.json").write_text(json.dumps({
            "lastConnectedSsid": TEN_DA_LUU,
            "credentials": [{"ssid": TEN_DA_LUU, "password": "go-nham"}],
        }))

    def chay(self, buoc, timeout=90):
        script = ";".join(f"{2000 + i * NHIP_MS}:{phim}" for i, phim in enumerate(buoc)) + ";"
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_WIFI_NETWORKS=QUET_GIA, CROSSPOINT_SIM_INPUT_SCRIPT=script)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, f"simulator exit {run.returncode}\n{log[-4000:]}")
        (self.sd / "run.log").write_text(log)
        return log

    def da_luu(self):
        return [c["ssid"] for c in json.loads((self.store / "wifi.json").read_text()).get("credentials", [])]

    # Toi man chon mang: mo Cai dat o nhom Thiet bi roi vao dong 3.
    TOI_MAN_MANG = ["DOWN"] * 4 + ["RIGHT"] * 5 + ["CONFIRM"] + ["RIGHT"] * 2 + ["CONFIRM"]

    def test_1_mang_da_luu_khong_quet_thay_van_co_dong_rieng(self):
        log = self.chay(self.TOI_MAN_MANG + ["QUIT"])

        self.assertIn("Entering activity: WifiSelection", log, f"khong mo duoc man chon mang\n{log[-4000:]}")
        self.assertEqual(KHONG_THAY.findall(log)[:1], [TEN_DA_LUU],
                         f"khong dung dong nao cho mang da luu ma khong quet thay\n{log[-4000:]}")

    def test_2_giu_nut_trai_roi_chon_quen_thi_xoa_duoc(self):
        # Mang da luu khong nam trong ket qua quet nen moi dong quet deu chua luu:
        # sap xep dua dong da luu len dau, con tro mo man o dong 0 la dung no.
        # LEFT = mo hoi "Quen mang?", RIGHT = chuyen tu Huy sang Quen, CONFIRM = chot.
        self.assertIn(TEN_DA_LUU, self.da_luu(), "fixture sai: chua luu gi")

        log = self.chay(self.TOI_MAN_MANG + ["LEFT", "RIGHT", "CONFIRM", "QUIT"])

        self.assertIn("Entering activity: WifiSelection", log, f"khong mo duoc man chon mang\n{log[-4000:]}")
        self.assertNotIn(TEN_DA_LUU, self.da_luu(),
                         f"van con trong wifi.json, tuc la khong co duong nao xoa no\n{log[-4000:]}")


if __name__ == "__main__":
    unittest.main()
