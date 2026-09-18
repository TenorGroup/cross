"""KIEM: con dong ben duoi thi bao bang mui ten chu V o chan man.

Truoc day cho nay ve mot con so kieu "1-10 / 13" o goc tren phai, ngay ben
duoi ten the ben canh. Con so do gan nhu vo dung khi ca danh sach da hien het,
va no lay mat cho cua ten the, thu duy nhat o goc do dang doc. Nay bo han con
so; con dong ben duoi thi ve mot mui ten chu V o giua chan man, ngay tren dong
mach nuoc, vi it nguoi nhin thanh cuon.

Bai nay do bang PIXEL, khong can doc chu: cat dung dai ngang noi mui ten duoc
ve roi dem diem muc. Dai do nam duoi dong cuoi cua danh sach va tren dong mach
nuoc, nen khong dinh chu nao khac.

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

# O mui ten: giua man (rong 528) va o tipY - 30. Do duoc tren anh that: net mui
# ten nam o y 696..704, dong cuoi danh sach het o y 670, dong mach nuoc o y 737.
O_MUI_TEN = (240, 690, 290, 712)


class CaiDatConDongBenDuoiTest(unittest.TestCase):
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

    def muc_den_trong_o(self, ten, o):
        """So diem anh khong phai nen trang trong mot o chu nhat."""
        path = ART / (ten + ".bmp")
        self.assertTrue(path.exists(), f"thieu anh {ten}")
        return sum(1 for p in Image.open(path).convert("L").crop(o).getdata() if p < 128)

    def test_the_ngan_thi_khong_ve_mui_ten(self):
        """The `Thiet bi` co 4 dong, hien het tren mot man: khong con gi o duoi de bao."""
        buoc = ["DOWN"] * 4 + ["RIGHT"] * 5 + ["CONFIRM"]
        mo = 2000 + (len(buoc) - 1) * NHIP_MS
        self.chay(buoc, [(mo + 1500, "the-ngan")])

        den = self.muc_den_trong_o("the-ngan", O_MUI_TEN)
        self.assertEqual(den, 0, f"con {den} diem muc: ve mui ten trong khi khong con dong nao ben duoi")

    def test_the_dai_thi_ve_mui_ten_o_chan_man(self):
        """The `Hien thi` co 13 dong ma chi hien 10, nen phai bao la con dong ben duoi.

        Ca nay giu cho ca tren khoi bi va qua tay: bo han mui ten la bai nay do.
        """
        # Mo Cai dat roi bam DOWN ba nhip de sang the `Hien thi` (13 dong, hien 10).
        buoc = ["DOWN"] * 4 + ["RIGHT"] * 5 + ["CONFIRM"] + ["DOWN"] * 3
        mo = 2000 + (len(buoc) - 1) * NHIP_MS
        self.chay(buoc, [(mo + 1500, "the-dai")])

        den = self.muc_den_trong_o("the-dai", O_MUI_TEN)
        self.assertGreater(den, 0, "the dai ma khong ve mui ten: nguoi dung khong biet con dong o duoi")


if __name__ == "__main__":
    unittest.main()
