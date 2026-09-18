"""KIỂM: tuỳ chọn "Thức dậy là vào sách" (wakeIntoBook).

Thức dậy từ ngủ sâu bằng nút nguồn: bật tuỳ chọn thì vào thẳng sách đang mở,
tắt thì về Trang chủ như cũ. Ngủ từ màn khác, hoặc sách đã bị xoá thì vẫn về
Trang chủ. Khung chạy theo test_wake_refresh.py: state.json showBootScreen
false + CROSSPOINT_SIM_WAKE_REASON=power.
"""

import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = REPO / ".pio/build/simulator_x3_uc8279/program"
FIXTURE = REPO / "test/epubs/test_dictionary_synonyms.epub"
SACH = "/books/sach.epub"

ENTERING = re.compile(r"Entering activity: (\S+)")
# `[GFX] Time = 2 ms from clearScreen to displayBuffer, mode=2`
REFRESH = re.compile(r"(?:displayBuffer|displayGrayscaleBase), mode=(\d)")


class WakeIntoBookTest(unittest.TestCase):
    maxDiff = None

    def chay(self, wake_into_book, last_sleep_from_reader=True, open_path=SACH, dat_sach=True, script="4000:QUIT",
             timeout=30):
        tmp = tempfile.TemporaryDirectory(prefix="cross-wake-book-")
        self.addCleanup(tmp.cleanup)
        sd = Path(tmp.name)
        store = sd / ".crosspoint"
        store.mkdir()
        if dat_sach:
            (sd / "books").mkdir()
            shutil.copy(FIXTURE, sd / open_path.lstrip("/"))
        (store / "settings.json").write_text(json.dumps({"language": "VI", "wakeIntoBook": wake_into_book}))
        (store / "state.json").write_text(json.dumps({
            "showBootScreen": False,
            "openEpubPath": open_path,
            "lastSleepFromReader": last_sleep_from_reader,
        }))
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(
            SDL_VIDEODRIVER="dummy",
            CROSSPOINT_SIM_SD=str(sd),
            CROSSPOINT_SIM_WAKE_REASON="power",
            CROSSPOINT_SIM_INPUT_SCRIPT=script,
        )
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, f"simulator exit {run.returncode}\n{log[-4000:]}")
        self.assertNotIn("Entering activity: Boot", log, f"thuc day ma con hien man khoi dong\n{log[-4000:]}")
        return log

    def da_vao(self, log):
        return ENTERING.findall(log)

    def truoc(self, vao, ten):
        return vao[:vao.index(ten)]

    # --- 1. bật tuỳ chọn: thức dậy là vào sách ------------------------------
    def test_1_bat_tuy_chon_thi_vao_thang_sach(self):
        log = self.chay(wake_into_book=1)
        vao = self.da_vao(log)

        self.assertIn("EpubReader", vao, f"khong vao trinh doc\n{log[-4000:]}")
        self.assertNotIn("Home", self.truoc(vao, "EpubReader"), "Trang chu khong duoc hien truoc trinh doc")

    # --- 2. trang sách phải được đẩy lên panel bằng nước sóng dọn bóng ------
    def test_2_lan_ve_trang_sau_thuc_day_khong_dung_FAST(self):
        log = self.chay(wake_into_book=1)
        self.assertIn("Entering activity: EpubReader", log, f"khong vao trinh doc\n{log[-4000:]}")

        # Đo trên giả lập: trình đọc xoá trắng panel một nhịp FAST (mode=2) rồi vẽ
        # trang bằng HALF (mode=1). Nhịp đưa TRANG lên panel mới là nhịp dọn bóng
        # màn ngủ; FAST ở đó là mất bóng đúng như allowFastInitialRefresh=true.
        sau_khi_vao = log[log.index("Entering activity: EpubReader"):]
        den_khi_ve_xong_trang = sau_khi_vao.split("Rendered page in", 1)[0]
        modes = REFRESH.findall(den_khi_ve_xong_trang)

        self.assertTrue(modes, f"trinh doc khong ve khung nao\n{sau_khi_vao[-2000:]}")
        self.assertNotEqual(modes[-1], "2",
                            f"trang sach len panel bang FAST, con bong anh man ngu\n{sau_khi_vao[-2000:]}")

    # --- 3. tắt tuỳ chọn: về Trang chủ như cũ -------------------------------
    def test_3_tat_tuy_chon_thi_ve_trang_chu(self):
        log = self.chay(wake_into_book=0)
        self.assertIn("Home", self.da_vao(log), f"khong ve Trang chu\n{log[-4000:]}")

    # --- 4. ngủ từ màn khác: về Trang chủ -----------------------------------
    def test_4_ngu_tu_man_khac_thi_ve_trang_chu(self):
        log = self.chay(wake_into_book=1, last_sleep_from_reader=False)
        self.assertIn("Home", self.da_vao(log), f"khong ve Trang chu\n{log[-4000:]}")

    # --- 5. sách đã bị xoá khỏi thẻ: về Trang chủ ---------------------------
    def test_5_sach_da_xoa_thi_ve_trang_chu(self):
        log = self.chay(wake_into_book=1, dat_sach=False)
        self.assertIn("Home", self.da_vao(log), f"khong ve Trang chu\n{log[-4000:]}")


if __name__ == "__main__":
    unittest.main()
