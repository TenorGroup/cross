"""KIEM: con dong ben duoi thi bao bang dai mo (v1.0.53), truoc day la mui ten chu V o chan man.

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
from cai_dat_truoc_tenor import truoc_tenor

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
        (self.store / "settings.json").write_text(json.dumps(truoc_tenor({"language": "VI"})))
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
        (store / "settings.json").write_text(json.dumps(truoc_tenor(settings)))
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
        # v1.0.14: chu V "con nua" chung (tenorchrome::drawMoreChevron), mo 11 moi ben, sau 8,
        # net ba diem theo chieu doc (khoang 2,4 diem vuong goc canh). Ban hai diem cu nhat qua.
        pixels = set()
        for t in range(-11, 12):
            doc = 8 - (abs(t) * 16 + 11) // 22
            for d in range(3):
                pixels.add((cx + t, top + doc + d))
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

    @staticmethod
    def buoc_mo_bo_cuc():
        # Home Settings: Hien thi, Ngu, Trinh doc. Mo thang nhom Trinh doc,
        # Chon dong dau mo Cai dat van ban, roi xuong hai nhip sang Bo cuc.
        return ["DOWN"] * 4 + ["RIGHT"] * 2 + ["CONFIRM", "CONFIRM"] + ["DOWN"] * 2

    def anh_status(self, ten):
        with Image.open(ART / (ten + ".bmp")) as source:
            return source.convert("L")

    def kiem_status(self, ten, lon, clock):
        image = self.anh_status(ten)
        pixels = [(x, y) for y in range(image.height - 70, image.height - 4) for x in range(image.width)
                  if image.getpixel((x, y)) < 128]
        self.assertTrue(pixels, f"{ten}: khong co pixel status")
        left = min(x for x, _ in pixels)
        right = max(x for x, _ in pixels)
        right_margin = image.width - right - 1
        if clock == 1:
            self.assertEqual(left, 8, f"{ten}: icon pin trai phai bat dau tai inset 8 px")
            self.assertIn(right_margin, range(8, 11), f"{ten}: dong ho phai sai inset")
        else:
            self.assertIn(left, range(8, 11), f"{ten}: dong ho trai sai inset hoac bearing")
            self.assertEqual(right_margin, 8, f"{ten}: dau pin phai phai ket thuc tai inset 8 px")

        battery_width = 32 if lon else 26
        bx = 8 if clock == 1 else image.width - 8 - battery_width
        icon_right = bx + battery_width - 1
        icon_pixels = [(x, y) for x, y in pixels if bx <= x <= icon_right]
        self.assertTrue(icon_pixels, f"{ten}: khong thay icon pin tai goc quy dinh")
        top = min(y for _, y in icon_pixels)
        bottom = max(y for _, y in icon_pixels)
        body_right = icon_right - 2

        # Bon goc than pin phai co khoang trang nhin thay, canh tren va duoi van
        # lien mach o giua. Day la hinh hoc anh, doc lap voi cach renderer ve cung.
        for dx in range(3):
            for x, y in ((bx + dx, top), (body_right - dx, top),
                         (bx + dx, bottom), (body_right - dx, bottom)):
                self.assertGreaterEqual(image.getpixel((x, y)), 128,
                                        f"{ten}: than pin con goc vuong tai {(x, y)}")
        for y in (top, bottom):
            self.assertTrue(any(image.getpixel((x, y)) < 128 for x in range(bx + 3, body_right - 2)),
                            f"{ten}: canh than pin bi dut o y={y}")

        # Hai cot cuoi la dau pin. Khi pin o ben phai, day cung la net muc ngoai
        # cung cua thanh; phan tram nam tron ven ben trai va cach icon mot khe.
        for x in (icon_right - 1, icon_right):
            self.assertTrue(any(image.getpixel((x, y)) < 128 for y in range(top, bottom + 1)),
                            f"{ten}: dau pin thieu cot x={x}")
        if clock == 2:
            percent = [(x, y) for x, y in pixels if bx - 60 <= x <= bx - 5]
            self.assertTrue(percent, f"{ten}: thieu phan tram ben trai icon pin phai")
            visible_gap = bx - max(x for x, _ in percent) - 1
            self.assertIn(visible_gap, range(4, 9), f"{ten}: khe phan tram-pin sai: {visible_gap}")

    def test_the_ngan_thi_khong_ve_mui_ten(self):
        """The `He thong` co bon dong, hien het tren mot man."""
        buoc = ["DOWN"] * 4 + ["RIGHT"] * 6 + ["CONFIRM"]
        mo = 2000 + (len(buoc) - 1) * NHIP_MS
        self.chay(buoc, [(mo + 1500, "the-ngan")])

        den = self.muc_den_trong_o("the-ngan", O_MUI_TEN)
        self.assertEqual(den, 0, f"con {den} diem muc: ve mui ten trong khi khong con dong nao ben duoi")

    # Bayer 8x8 cua tenorchrome::fadeBand: dai mo chi giu diem co nguong thap, cang xuong cang it.
    BAYER8 = [[0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26], [12, 44, 4, 36, 14, 46, 6, 38],
              [60, 28, 52, 20, 62, 30, 54, 22], [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25],
              [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21]]

    @staticmethod
    def cot_thanh_cuon(image):
        """Cac dong co muc cua thanh cuon 6 px o mep phai (x 519..524); cot 521..524 tranh mui ten canh."""
        ys = [y for y in range(120, image.height) if any(image.getpixel((x, y)) < 128 for x in range(521, 525))]
        # Doan lien dai nhat (ranh cham cach 2 dong): bo mui ten canh, chu xem truoc va dong ho o chan man.
        runs = [[ys[0]]] if ys else []
        for y in ys[1:]:
            if y - runs[-1][-1] > 3:
                runs.append([])
            runs[-1].append(y)
        return max(runs, key=len) if runs else []

    def kiem_dai_mo(self, ten):
        """v1.0.53: con dong ben duoi thi hang ke tiep hien mo dan duoi hang du cuoi, khong con chu V,
        va thanh cuon la vien thuoc (dau bo)."""
        self.assertEqual([top for top, _ in self.v_candidates(ten)], [], f"{ten}: con mui ten chu V")
        image = self.anh_status(ten)
        # Cum dong muc thap nhat o nua trai danh sach (chu V cu nam giua man nen khong lot vao), tren dai meo.
        rows = [y for y in range(330, O_MUI_TEN[3] - 52) if any(image.getpixel((x, y)) < 128 for x in range(20, 230))]
        self.assertTrue(rows, f"{ten}: danh sach rong")
        group = [rows[-1]]
        for y in reversed(rows[:-1]):
            if group[-1] - y > 4:
                break
            group.append(y)
        ink = [(x, y) for y in group for x in range(20, 230) if image.getpixel((x, y)) < 128]
        cao = sum(1 for x, y in ink if self.BAYER8[x & 7][y & 7] >= 48)
        # Chu thuong: ~1/4 so diem muc co nguong >= 48. Hang mo: gan nhu khong con.
        self.assertLess(cao / len(ink), 0.12, f"{ten}: hang cuoi y {min(group)}..{max(group)} khong mo ({cao}/{len(ink)})")
        # Thanh cuon: dong dau cua con truot hep hon than no (dau bo), khong vuong.
        bar = self.cot_thanh_cuon(image)
        self.assertTrue(bar, f"{ten}: khong co thanh cuon")
        width = lambda y: sum(1 for x in range(521, 525) if image.getpixel((x, y)) < 128)
        self.assertLess(width(bar[0]), width(bar[0] + 2), f"{ten}: dau thanh cuon vuong")

    def test_the_dai_thi_hang_ke_tiep_mo_dan(self):
        """The Bo cuc co bay dong, o co chu Lon van con dong duoi."""
        buoc = self.buoc_mo_bo_cuc()
        mo = 2000 + (len(buoc) - 1) * NHIP_MS
        sd = self.tao_sd("the-dai", {"language": "VI", "uiTheme": 4, "uiTextSize": 2,
                                      "globalStatusBarMode": 2, "tenorButtonSymbols": 1, "statusBarClock": 1})
        log = self.chay(buoc, [(mo + 1500, "the-dai")], sd=sd)
        self.assertIn("Entering activity: TextSettings", log)
        self.kiem_dai_mo("the-dai")

    def test_hang_hai_dong_van_de_lo_dau_hang_mo(self):
        """Cai dat/Hien thi co Lon: mot hang xuong hai dong lam trang chua it hang hon uoc. Hang mo van phai
        lo du phan dau de doc (do theo hang that), khong chi la mot vet cham."""
        ten = "hien-thi-lon"
        sd = self.tao_sd(ten, {"language": "VI", "uiTheme": 4, "uiTextSize": 2,
                               "globalStatusBarMode": 2, "tenorButtonSymbols": 1, "statusBarClock": 1})
        buoc = ["DOWN"] * 4 + ["CONFIRM"]
        mo = 2000 + (len(buoc) - 1) * NHIP_MS
        self.chay(buoc, [(mo + 1500, ten)], sd=sd)
        self.kiem_dai_mo(ten)
        image = self.anh_status(ten)
        bar = self.cot_thanh_cuon(image)
        rows = [y for y in range(150, bar[-1]) if any(image.getpixel((x, y)) < 128 for x in range(20, 230))]
        group = [rows[-1]]
        for y in reversed(rows[:-1]):
            if group[-1] - y > 4:
                break
            group.append(y)
        self.assertGreaterEqual(max(group) - min(group), 14, f"{ten}: hang mo chi cao {max(group) - min(group)} px")

    def test_bo_cuc_thanh_lon_mo_dan_o_ba_co_chu(self):
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
            with self.subTest(tier=tier):
                self.kiem_dai_mo(ten)

    def test_status_thuong_va_lon_bam_hai_goc_o_ba_co_chu_va_hai_chieu_dong_ho(self):
        cases = [(mode, tier, clock) for mode in (0, 2) for tier in range(3) for clock in (1, 2)]

        def capture(case):
            mode, tier, clock = case
            ten = f"status-{mode}-{tier}-{clock}"
            sd = self.tao_sd(ten, {"language": "VI", "uiTheme": 4, "uiTextSize": tier,
                                  "globalStatusBarMode": mode, "tenorButtonSymbols": 1,
                                  "statusBarClock": clock})
            self.chay([], [(1800, ten)], sd=sd)
            return mode, tier, clock, ten

        with ThreadPoolExecutor(max_workers=3) as pool:
            captures = list(pool.map(capture, cases))
        for mode, tier, clock, ten in captures:
            with self.subTest(mode=mode, tier=tier, clock=clock):
                self.kiem_status(ten, lon=(mode == 2), clock=clock)


if __name__ == "__main__":
    unittest.main()
