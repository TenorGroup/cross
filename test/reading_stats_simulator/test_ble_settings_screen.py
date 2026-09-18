"""BTH2: man Cai dat BLE (Page turner) tren simulator X3 that, khi co BLE dang TAT.

Bang chung, khong phai cam giac:
  - log firmware: `[ACT] Entering/Exiting activity: ...` (ngan xep hoat dong),
    `[BLE] BlePageTurner status=<chuoi> enabled=<0|1> bonds=<n> devices=<n>`
    (BlePageTurnerActivity::capNhatTrangThai, in ra moi lan trang thai THAT SU doi),
    va `[ERR] [BLE] BLE HID host begin() failed; preference kept, radio not running`;
  - anh BMP tu CROSSPOINT_SIM_SCREENSHOTS (Pillow) de nhin dung man dang mo;
  - `.crosspoint/settings.json` khoa `blePageTurnerEnabled` truoc/sau.

Ban do dieu huong do duoc (do lai bang anh, xem docstring cua class):
  Home (the dau) -> DOWN x4 = the Cai dat; RIGHT x5 = nhom `Thiet bi`; CONFIRM mo
  man Cai dat tai nhom do; trong nhom `Thiet bi` vong N dong bat dau o dong 1, va
  dong 4 la `Ble page turner`; LEFT tu dong 1 quay ve dong 4, CONFIRM mo man BLE.
  Trong man Cai dat: UP/DOWN doi nhom, RIGHT/LEFT di dong. Nhip phim >= 800 ms
  (nhip 900 ms tung bi hut mot nhip; bai nay dung 1200 ms).

Ky vong dung khi co BLE DANG TAT (FREEINK_CAP_BLE_HID_HOST=0, mac dinh):
  - man BLE VAN mo duoc (khong treo, khong chan);
  - trang thai hien `Bản dựng này không có Bluetooth` (STR_BLE_UNAVAILABLE) va
    KHONG BAO GIO thanh `BẬT` (STR_STATE_ON): radio khong chay duoc;
  - nhip Chon vao dong `BLE page turner` van ghi y dinh (enabled=1) nhung
    begin() that bai -> ghi log ERR; tat lai thi `settings.json` tro ve 0.
  - hai dong `Gán nút lật tới` / `Gán nút lật lui` nam sau dong quet, gia tri
    `Mặc định` khi chua hoc nut nao; vao mot dong roi cho het 15 giay thi dong
    trang thai doi sang `Không nhận được nút` va settings.json khong doi.

Chay: venv/bin/python -m pytest test/reading_stats_simulator/test_ble_settings_screen.py -q
Anh duoc luu vao CROSSPOINT_TEST_ARTIFACTS (mac dinh: <repo>/t3).
"""

import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[2]
# Cung quy uoc voi test_font_preference.py: TEST_PROGRAM tro toi ban mo phong da luu.
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))
ART = Path(os.environ.get("CROSSPOINT_TEST_ARTIFACTS", REPO / "t3"))

# Man BLE dang tat: chuoi trang thai cua STR_BLE_UNAVAILABLE trong lib/I18n/translations/vietnamese.yaml.
UNAVAILABLE = "Bản dựng này không có Bluetooth"
ON = "BẬT"

NHIP_MS = 1200  # >= 800 ms: nhip nhanh hon tung lam hut nhip phim
# `[BLE] BlePageTurner status=Bản dựng này không có Bluetooth enabled=0 bonds=0 devices=0`
TRANG_THAI = re.compile(r"BlePageTurner status=(?P<status>.*?) enabled=(?P<enabled>\d) bonds=\d+ devices=\d+")
BEGIN_LOI = "BLE HID host begin() failed; preference kept, radio not running"


def kich_ban(buoc):
    """[(moc_ms, phim)] -> chuoi CROSSPOINT_SIM_INPUT_SCRIPT, moi nhip cach nhau NHIP_MS."""
    return ";".join(f"{2000 + i * NHIP_MS}:{phim}" for i, phim in enumerate(buoc)) + ";"


class BleSettingsScreenTest(unittest.TestCase):
    """Man BLE khi co BLE tat: mo duoc, hien khong kha dung, khong bat duoc."""

    maxDiff = None

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="cross-ble-settings-")
        self.addCleanup(self.tmp.cleanup)
        self.sd = Path(self.tmp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir()
        (self.store / "settings.json").write_text(json.dumps({"language": "VI", "blePageTurnerEnabled": 0}))
        (self.store / "state.json").write_text(
            json.dumps({"openEpubPath": "", "lastSleepFromReader": False, "showBootScreen": False}))
        self.anh_dat = []

    def chay(self, buoc, shots=(), timeout=60, script=None):
        """Chay ban mo phong voi kich ban; `shots` = [(moc_ms, ten_anh)] -> tra ve log.

        `script` (chuoi tho) thay cho `buoc` khi mot bai can moc thoi gian dai hon
        nhip NHIP_MS, vi du cho het 15 giay cua luot gan nut.
        """
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=script if script is not None else kich_ban(buoc))
        if shots:
            ART.mkdir(parents=True, exist_ok=True)
            env["CROSSPOINT_SIM_SCREENSHOTS"] = ";".join(f"{ms}:{ART / (ten + '.bmp')}" for ms, ten in shots)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, f"simulator exit {run.returncode}\n{log[-4000:]}")
        (self.sd / "run.log").write_text(log)
        self.anh_dat = [ten for _, ten in shots]
        return log

    def anh(self, ten):
        path = ART / (ten + ".bmp")
        self.assertTrue(path.exists(), f"thieu anh {ten}")
        return Image.open(path).convert("RGB")

    def test_man_ble_mo_duoc_va_khong_bat_duoc_khi_co_ble_tat(self):
        # Home the dau -> the Cai dat (DOWN x4) -> nhom Thiet bi (RIGHT x5) ->
        # mo man Cai dat (CONFIRM) -> mo man BLE (CONFIRM) -> thu bat (CONFIRM)
        # -> tat lai (CONFIRM) -> ra khoi man BLE (BACK) -> ra khoi Cai dat (BACK).
        buoc = ["DOWN"] * 4 + ["RIGHT"] * 5 + ["CONFIRM", "LEFT", "CONFIRM", "CONFIRM", "CONFIRM"]
        # Moc chup: sau khi mo Cai dat, sau khi mo man BLE, sau moi nhip Chon.
        mo_cai_dat, mo_ble = 2000 + 9 * NHIP_MS, 2000 + 11 * NHIP_MS
        shots = [
            (mo_cai_dat + 900, "ble-cai-dat-nhom-thiet-bi"),
            (mo_ble + 900, "ble-man-hinh-1-khong-kha-dung"),
            (2000 + 12 * NHIP_MS + 900, "ble-man-hinh-2-thu-bat"),
            (2000 + 13 * NHIP_MS + 900, "ble-man-hinh-3-tat-lai"),
        ]
        buoc += ["BACK", "BACK", "QUIT"]
        log = self.chay(buoc, shots)

        # 1. Man BLE mo duoc tu man Cai dat, dung mot lan, khong di lac sang dong khac.
        self.assertEqual(log.count("Entering activity: Settings"), 1, log[-3000:])
        self.assertEqual(log.count("Entering activity: BlePageTurner"), 1, log[-3000:])
        self.assertEqual(log.count("Entering activity: KeyboardEntry"), 0, log[-3000:])

        # 2. Hai nhip BACK phai ra khoi BLE roi ra khoi Cai dat; ngan xep ve rong.
        self.assertIn("Exiting activity: BlePageTurner", log, log[-2000:])
        self.assertIn("Exiting activity: Settings", log, log[-2000:])
        self.assertIn("Popped from activity stack, new size = 0", log, log[-2000:])

        # 3. Trang thai do firmware bao: khong kha dung ngay tu lan ve dau, va y dinh
        #    bat/tat chi doi `enabled` chu KHONG BAO GIO lam radio chay.
        trang_thai = [(m.group("status"), int(m.group("enabled"))) for m in TRANG_THAI.finditer(log)]
        self.assertEqual([e for _, e in trang_thai], [0, 1, 0], f"y dinh bat/tat: {trang_thai}")
        self.assertEqual({s for s, _ in trang_thai}, {UNAVAILABLE}, f"trang thai: {trang_thai}")
        self.assertNotIn(f"status={ON} enabled=1", log, log[-2000:])
        self.assertIn(BEGIN_LOI, log, log[-2000:])

        # 4. Anh: nhom Thiet bi (dong 4 = BLE), man BLE luc chua bat, luc thu bat, luc tat lai.
        _nhom, ban_dau, thu_bat, tat_lai = (self.anh(ten) for ten in self.anh_dat)
        for anh in (ban_dau, thu_bat, tat_lai):
            self.assertEqual(anh.size, ban_dau.size)
        # Nhip Chon phai lam doi khung ve (dong `BLE page turner` doi gia tri): neu hut
        # nhip thi khung sau giong het khung truoc va phep so nay bat duoc.
        self.assertIsNotNone(ImageChops.difference(ban_dau, thu_bat).getbbox(),
                             "nhip Chon dau tien khong doi khung ve - co the bi hut phim")
        self.assertIsNotNone(ImageChops.difference(thu_bat, tat_lai).getbbox(),
                             "nhip Chon thu hai khong doi khung ve - co the bi hut phim")

        # 5. Thoat ra thi tuy chon van la TAT trong settings.json.
        luu = json.loads((self.store / "settings.json").read_text())
        self.assertEqual(luu["blePageTurnerEnabled"], 0, luu)

    def test_gan_nut_cho_het_muoi_lam_giay_roi_bao_khong_nhan_duoc(self):
        """Vao hang "Gan nut lat toi" roi cho: khong co radio nen khong co phim nao toi.

        Do duoc: dong trang thai doi tu cau cho sang cau khong nhan duoc, va
        settings.json KHONG doi (khong hoc duoc nut nao).
        """
        # ... mo man BLE (xem test tren), roi RIGHT x3 = hang "Gan nut lat toi", CONFIRM.
        buoc = ["DOWN"] * 4 + ["RIGHT"] * 5 + ["CONFIRM", "LEFT", "CONFIRM"] + ["RIGHT"] * 3 + ["CONFIRM"]
        bat_cho = 2000 + (len(buoc) - 1) * NHIP_MS
        script = kich_ban(buoc) + f"{bat_cho + 17000}:QUIT;"
        shots = [
            (bat_cho + 3000, "ble-gan-nut-dang-cho"),
            (bat_cho + 16000, "ble-gan-nut-khong-nhan-duoc"),
        ]
        log = self.chay(buoc, shots, timeout=70, script=script)

        self.assertEqual(log.count("Entering activity: BlePageTurner"), 1, log[-3000:])
        self.assertEqual(log.count("Entering activity: KeyboardEntry"), 0, log[-3000:])
        self.assertIn("Waiting for a button to bind to next", log, log[-3000:])
        self.assertIn("Bind wait ended with no key", log, log[-3000:])

        # Hai khung phai khac nhau: cau cho doi thanh cau khong nhan duoc.
        dang_cho, het_cho = (self.anh(ten) for ten in self.anh_dat)
        self.assertIsNotNone(ImageChops.difference(dang_cho, het_cho).getbbox(),
                             "dong trang thai khong doi sau khi het 15 giay cho")

        # Khong co phim nao toi thi khong hoc duoc gi.
        luu = json.loads((self.store / "settings.json").read_text())
        self.assertEqual(luu.get("bleNextKeyUsage", 0), 0, luu)
        self.assertEqual(luu.get("blePrevKeyUsage", 0), 0, luu)


if __name__ == "__main__":
    unittest.main()
