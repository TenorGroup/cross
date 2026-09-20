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
from concurrent.futures import ThreadPoolExecutor
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

    def chay(self, buoc, shots, timeout=90, sd=None):
        sd = self.sd if sd is None else sd
        script = ";".join(f"{2000 + i * NHIP_MS}:{phim}" for i, phim in enumerate(buoc)) + ";"
        script += f"{2000 + (len(buoc) + 4) * NHIP_MS}:QUIT;"
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=";".join(f"{ms}:{ART / (ten + '.bmp')}" for ms, ten in shots))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, f"simulator exit {run.returncode}\n{log[-3000:]}")
        return log

    def tao_sd(self, name, settings):
        sd = self.sd / name
        store = sd / ".crosspoint"
        store.mkdir(parents=True)
        (store / "settings.json").write_text(json.dumps(settings))
        (store / "state.json").write_text(
            json.dumps({"openEpubPath": "", "lastSleepFromReader": False, "showBootScreen": False}))
        return sd

    def muc_den_trong_o(self, ten, o):
        """So diem anh khong phai nen trang trong mot o chu nhat."""
        path = ART / (ten + ".bmp")
        self.assertTrue(path.exists(), f"thieu anh {ten}")
        return sum(1 for p in Image.open(path).convert("L").crop(o).getdata() if p < 128)

    @staticmethod
    def diem_duong(x1, y1, x2, y2):
        pixels = []
        dx, dy = x2 - x1, y2 - y1
        sx, sy = (1 if dx > 0 else -1), (1 if dy > 0 else -1)
        dx, dy = sx * dx, sy * dy
        err = dx - dy
        while True:
            pixels.append((x1, y1))
            if x1 == x2 and y1 == y2:
                return pixels
            twice = 2 * err
            if twice > -dy:
                err -= dy
                x1 += sx
            if twice < dx:
                err += dx
                y1 += sy

    @classmethod
    def mask_v(cls, cx, top):
        pixels = set()
        for d in range(2):
            pixels.update(cls.diem_duong(cx - 11, top + d, cx, top + 7 + d))
            pixels.update(cls.diem_duong(cx, top + 7 + d, cx + 11, top + d))
        return pixels

    def v_candidates(self, ten):
        with Image.open(ART / (ten + ".bmp")) as source:
            image = source.convert("L")
        cx = image.width // 2
        candidates = []
        # O giua man co the la dong dang chon to den, nen no tinh co phu het
        # mask V. Mui ten chi nam trong dai affordance gan chan man.
        for top in range(image.height - 180, image.height - 24):
            expected = self.mask_v(cx, top)
            if all(image.getpixel((x, y)) < 128 for x, y in expected):
                candidates.append((top, expected))
        return candidates

    def v_sach(self, ten):
        candidates = self.v_candidates(ten)
        self.assertEqual(len(candidates), 1, f"{ten}: khong tim thay dung mot mask V: {[top for top, _ in candidates]}")
        top, expected = candidates[0]
        with Image.open(ART / (ten + ".bmp")) as source:
            image = source.convert("L")
        cx = image.width // 2
        extras = {(x, y) for x in range(cx - 12, cx + 13) for y in range(top - 2, top + 11)
                  if image.getpixel((x, y)) < 128 and (x, y) not in expected}
        return top, extras

    @staticmethod
    def buoc_mo_bo_cuc():
        # Home Settings mo the He thong. Len hai nhip sang Doc, Chon dong dau
        # mo Cai dat van ban o the Ho font, roi xuong hai nhip sang Bo cuc.
        return ["DOWN"] * 4 + ["RIGHT"] * 5 + ["CONFIRM"] + ["UP"] * 2 + ["CONFIRM"] + ["DOWN"] * 2

    def mep_status(self, ten):
        with Image.open(ART / (ten + ".bmp")) as source:
            image = source.convert("L")
        pixels = [(x, y) for y in range(image.height - 70, image.height - 4) for x in range(image.width)
                  if image.getpixel((x, y)) < 128]
        self.assertTrue(pixels, f"{ten}: khong co pixel status")
        return min(x for x, _ in pixels), max(x for x, _ in pixels), image.width

    def test_the_ngan_thi_khong_ve_mui_ten(self):
        """The `He thong` co bon dong, hien het tren mot man."""
        buoc = ["DOWN"] * 4 + ["RIGHT"] * 5 + ["CONFIRM"]
        mo = 2000 + (len(buoc) - 1) * NHIP_MS
        self.chay(buoc, [(mo + 1500, "the-ngan")])

        den = self.muc_den_trong_o("the-ngan", O_MUI_TEN)
        self.assertEqual(den, 0, f"con {den} diem muc: ve mui ten trong khi khong con dong nao ben duoi")

    def test_the_dai_thi_ve_mui_ten_o_chan_man(self):
        """The Bo cuc co bay dong, o co chu Lon van con dong duoi."""
        buoc = self.buoc_mo_bo_cuc()
        mo = 2000 + (len(buoc) - 1) * NHIP_MS
        sd = self.tao_sd("the-dai", {"language": "VI", "uiTheme": 4, "uiTextSize": 2,
                                      "globalStatusBarMode": 2, "tenorButtonSymbols": 1, "statusBarClock": 1})
        log = self.chay(buoc, [(mo + 1500, "the-dai")], sd=sd)
        self.assertIn("Entering activity: TextSettings", log)

        top, extras = self.v_sach("the-dai")
        self.assertFalse(extras, f"the dai: co {len(extras)} pixel hang danh sach trong mask V o y={top}")

    def test_bo_cuc_thanh_lon_giu_mask_v_sach_o_ba_co_chu(self):
        def capture(tier):
            ten = f"large-layout-{tier}"
            sd = self.tao_sd(ten, {"language": "VI", "uiTheme": 4, "uiTextSize": tier,
                                  "globalStatusBarMode": 2, "tenorButtonSymbols": 1, "statusBarClock": 1})
            buoc = self.buoc_mo_bo_cuc()
            mo = 2000 + (len(buoc) - 1) * NHIP_MS
            log = self.chay(buoc, [(mo + 1500, ten)], sd=sd)
            self.assertIn("Entering activity: TextSettings", log)
            return ten

        with ThreadPoolExecutor(max_workers=3) as pool:
            captures = list(pool.map(capture, range(3)))
        for tier, ten in enumerate(captures):
            top, extras = self.v_sach(ten)
            self.assertFalse(extras, f"tier {tier}: co {len(extras)} pixel hang danh sach trong mask V o y={top}")

    def test_status_lon_bam_hai_goc_o_ba_co_chu_va_hai_chieu_dong_ho(self):
        cases = [(tier, clock) for tier in range(3) for clock in (1, 2)]

        def capture(case):
            tier, clock = case
            ten = f"large-status-{tier}-{clock}"
            sd = self.tao_sd(ten, {"language": "VI", "uiTheme": 4, "uiTextSize": tier,
                                  "globalStatusBarMode": 2, "tenorButtonSymbols": 1, "statusBarClock": clock})
            self.chay([], [(1800, ten)], sd=sd)
            return tier, clock, ten

        with ThreadPoolExecutor(max_workers=3) as pool:
            captures = list(pool.map(capture, cases))
        for tier, clock, ten in captures:
            left, right, width = self.mep_status(ten)
            right_margin = width - right - 1
            self.assertGreater(left, 0, f"tier {tier}, clock {clock}: status bi cat o mep trai")
            self.assertLess(right, width - 1, f"tier {tier}, clock {clock}: status bi cat o mep phai")
            self.assertGreaterEqual(left, 2, f"tier {tier}, clock {clock}: goc trai vuot vien an toan")
            self.assertLessEqual(left, 4, f"tier {tier}, clock {clock}: goc trai cach vien qua xa")
            self.assertGreaterEqual(right_margin, 2, f"tier {tier}, clock {clock}: goc phai vuot vien an toan")
            self.assertLessEqual(right_margin, 4, f"tier {tier}, clock {clock}: goc phai cach vien qua xa")


if __name__ == "__main__":
    unittest.main()
