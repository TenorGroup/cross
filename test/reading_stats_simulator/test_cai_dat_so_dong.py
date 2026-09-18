"""KIEM: chi bao "1-4 / 4" chi hien khi danh sach THAT SU dai hon mot man.

Man Cai dat cua tenor/cross hien ten hai the ben canh o goc tren, kieu
`< He thong` va `Ban phim >`, de nguoi dung biet hai nut hai ben di dau. Chi bao
so dong duoc ve ngay BEN DUOI ten the ben phai (UiListActivity::renderUi, moc
goc phai o TAB_TOP + 18), nen no chen vao dung cho do.

Khi ca danh sach da hien het tren man thi cau "1-4 / 4" khong noi them gi, chi
lay mat cho cua thu co ich hon. Bai nay do bang PIXEL, khong can doc chu: cat
dung o chu nhat noi chi bao duoc ve, roi xem o do co muc den nao khong.

Chay: /usr/bin/python3 -m unittest test.reading_stats_simulator.test_cai_dat_so_dong
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
ART = Path(os.environ.get("CROSSPOINT_TEST_ARTIFACTS", REPO / "t3"))

NHIP_MS = 1200

# O chi bao so dong: neo goc phai (rong man 528, le phai 18) o TAB_TOP + 18 = 71.
# Lay rong rai quanh do, va CHI o nay, de khong cham vao ten the ben canh o tren.
O_CHI_BAO = (360, 70, 515, 100)


class CaiDatSoDongTest(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="cross-cai-dat-")
        self.addCleanup(self.tmp.cleanup)
        self.sd = Path(self.tmp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir()
        (self.store / "settings.json").write_text(json.dumps({"language": "VI"}))
        (self.store / "state.json").write_text(
            json.dumps({"openEpubPath": "", "lastSleepFromReader": False, "showBootScreen": False}))
        ART.mkdir(parents=True, exist_ok=True)

    def chay(self, buoc, shots, timeout=90):
        script = ";".join(f"{2000 + i * NHIP_MS}:{phim}" for i, phim in enumerate(buoc)) + ";"
        script += f"{2000 + (len(buoc) + 4) * NHIP_MS}:QUIT;"
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=";".join(f"{ms}:{ART / (ten + '.bmp')}" for ms, ten in shots))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, f"simulator exit {run.returncode}\n{log[-3000:]}")
        return log

    def muc_den_trong_o(self, ten):
        """So diem anh khong phai nen trang trong o chi bao."""
        path = ART / (ten + ".bmp")
        self.assertTrue(path.exists(), f"thieu anh {ten}")
        o = Image.open(path).convert("L").crop(O_CHI_BAO)
        return sum(1 for p in o.getdata() if p < 128)

    def test_the_cai_dat_ngan_thi_khong_ve_chi_bao_so_dong(self):
        """The `Thiet bi` co 4 dong, hien het tren mot man, nen o chi bao phai SACH."""
        buoc = ["DOWN"] * 4 + ["RIGHT"] * 5 + ["CONFIRM"]
        mo = 2000 + (len(buoc) - 1) * NHIP_MS
        self.chay(buoc, [(mo + 1500, "so-dong-the-ngan")])

        den = self.muc_den_trong_o("so-dong-the-ngan")
        self.assertEqual(den, 0,
                         f"con {den} diem muc trong o chi bao: cau '1-4 / 4' van dang chen duoi ten the ben canh")

    def test_the_cai_dat_dai_thi_van_ve_chi_bao_so_dong(self):
        """The `Hien thi` co 13 dong ma chi hien 10, o do chi bao noi that mot dieu.

        Ca nay giu cho ca tren khoi bi vá qua tay: xoa han chi bao la bai nay do.
        """
        # Mo Cai dat roi bam DOWN ba nhip de sang the `Hien thi` (13 dong, hien 10).
        buoc = ["DOWN"] * 4 + ["RIGHT"] * 5 + ["CONFIRM"] + ["DOWN"] * 3
        mo = 2000 + (len(buoc) - 1) * NHIP_MS
        self.chay(buoc, [(mo + 1500, "so-dong-the-dai")])

        den = self.muc_den_trong_o("so-dong-the-dai")
        self.assertGreater(den, 0,
                           "the dai ma khong ve chi bao: nguoi dung khong biet con dong o duoi")


if __name__ == "__main__":
    unittest.main()
