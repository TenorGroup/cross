"""LUOI 30 NGAY tren man "30 ngay qua": doc diem anh tung o tren anh chup cua trinh mo phong X3.

Moi ca chay trinh mo phong that voi mot the SD tam. Hai chu ky ve do duoc:
  - o trong: khong muc; o dam: ti le diem den trong ruot o (0, ~1/4, ~1/2, ~1), moi o bo tron goc;
  - hom nay (o cuoi) co khung den bo tron nam ngoai o, cach 2 px trang.
Hang dau cua danh sach phai nam duoi luoi, khong de len o nao.

Cach tinh tay (hom nay = D, mui gio UTC), moc co dinh: duoi 60 phut bac 1, 60 toi duoi 180 bac 2, tu 180 bac 3.
  D-0 200 phut -> bac 3; D-1 60 -> 2; D-2 30 -> 1; D-5 10 -> 1; D-12 90 -> 2;
  D-29 180 -> 3 (o dau); D-30 600 phut nam NGOAI cua so nen khong co o.

BIEU DO GIO DOC THEO TUAN (duoi luoi): 4 cot, moi cot 7 ngay, cot cuoi ket thuc hom nay. Do chieu cao tung cot
tren anh chup, so voi phut tinh tay: cot cao 36 px ung voi phut lon nhat cua 4 khung. Ca bieu do cao 68 px (dong chu 22 + 36 + 10).
"""

import datetime
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image
from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))

# Home: ba nhip DOWN sang the Thong ke, hai nhip RIGHT toi hang 3 ("30 ngay qua"), CONFIRM mo no.
MO_30_NGAY = "1000:DOWN;1500:DOWN;2000:DOWN;3300:RIGHT;3800:RIGHT;4300:CONFIRM"
SHOT = 5600

# Hinh hoc cua man 528 rong: tenorchrome::tabTop() = 53, luoi cach 12 px; pitch = (528 - 48) / 10.
PITCH = 48
SIDE = PITCH - 8
GRID_TOP = 53 + 12
LABEL_H = 20  # dong ngay dau/cuoi nam duoi hang thu ba


def ma(d):
    return int(d.strftime("%Y%m%d"))


class Lich30Test(unittest.TestCase):
    def chay(self, ngay_phut, shot_ms=SHOT):
        tmp = tempfile.TemporaryDirectory(prefix="cross-lich30-")
        self.addCleanup(tmp.cleanup)
        sd = Path(tmp.name)
        store = sd / ".crosspoint"
        store.mkdir()
        (store / "settings.json").write_text(json.dumps(truoc_tenor(
            {"language": "VI", "clockHasBeenSynced": 1, "clockUtcOffsetQ": 48, "sleepTimeout": 120,
             "globalStatusBarMode": 0})))
        (store / "state.json").write_text(json.dumps(
            {"openEpubPath": "/sach1.txt", "readerActivityLoadCount": 0, "lastSleepFromReader": False,
             "showBootScreen": False}))
        (sd / "sach1.txt").write_text(("Dong van ban kiem thu. " * 30 + "\n") * 6)
        (store / "recent.json").write_text(json.dumps({"books": [{"path": "/sach1.txt", "title": "sach1"}]}))
        hom_nay = datetime.datetime.now(datetime.timezone.utc).date()
        rows = sorted([ma(hom_nay - datetime.timedelta(days=truoc)), phut, 10] for truoc, phut in ngay_phut.items())
        (store / "reading-stats.json").write_text(json.dumps({"schema": 3, "bookEpoch": 0, "ngay": rows}))
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        shot = sd / "ba-muoi-ngay.bmp"
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=MO_30_NGAY + ";%d:QUIT" % (shot_ms + 1500),
                   CROSSPOINT_SIM_SCREENSHOTS=f"{shot_ms}:{shot}")
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=90)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-3000:])
        self.assertIn("Entering activity: ReadingHistory", log, log[-2000:])
        image = Image.open(shot).convert("L")
        out = os.environ.get("CROSSPOINT_TEST_ARTIFACTS")
        if out:
            Path(out).mkdir(parents=True, exist_ok=True)
            image.save(Path(out) / "ba-muoi-ngay.png")
        self.assertEqual(image.size, (528, 792))
        return image

    @staticmethod
    def o(i):
        return 24 + i % 10 * PITCH + 4, GRID_TOP + i // 10 * PITCH

    def ink(self, image, i):
        """Ti le diem den trong ruot o i, bo 5 px vien (goc bo tron an vao ~3 px)."""
        x, y = self.o(i)
        px = image.load()
        den = tong = 0
        for yy in range(y + 5, y + SIDE - 5):
            for xx in range(x + 5, x + SIDE - 5):
                tong += 1
                den += px[xx, yy] < 128
        return den / tong

    def vien(self, image, i):
        """Ti le diem den trong dai 2 px tren cung cua o i, o doan thang giua canh."""
        x, y = self.o(i)
        px = image.load()
        diem = [px[xx, yy] < 128 for yy in (y, y + 1) for xx in range(x + 14, x + SIDE - 14)]
        return sum(diem) / len(diem)

    def bac(self, image, i):
        t = self.ink(image, i)
        return 0 if t < 0.08 else 1 if t < 0.38 else 2 if t < 0.75 else 3

    def test_muc_dam_tung_o_theo_phut_doc(self):
        image = self.chay({0: 200, 1: 60, 2: 30, 5: 10, 12: 90, 29: 180, 30: 600})
        muc = [self.bac(image, i) for i in range(30)]
        mong = [0] * 30
        for truoc, bac in {0: 3, 1: 2, 2: 1, 5: 1, 12: 2, 29: 3}.items():
            mong[29 - truoc] = bac
        self.assertEqual(muc, mong)

    def test_chi_o_trong_co_vien_xam_o_dam_to_lien_mot_mang(self):
        image = self.chay({0: 120, 1: 60, 2: 30, 12: 200})
        # o trong (D-3 la o 26): vien xam 2 px = nua so diem den. O dam: vien trung fill.
        self.assertGreater(self.vien(image, 26), 0.4)
        self.assertLess(self.vien(image, 26), 0.6)
        self.assertGreater(self.vien(image, 17), 0.98, "o den co vien khac fill")
        self.assertGreater(self.vien(image, 28), 0.4, "o xam dam (D-1) lech fill")
        self.assertLess(self.vien(image, 28), 0.6)
        self.assertLess(self.vien(image, 27), 0.35, "o xam nhat (D-2) co vien dam hon fill")

    def test_nguoi_doc_it_khong_bi_day_len_den(self):
        # Nguoi doc it: 20 va 10 phut deu duoi 1 gio nen la bac 1.
        image = self.chay({0: 20, 1: 10})
        self.assertEqual([self.bac(image, 28), self.bac(image, 29)], [1, 1])

    def test_hom_nay_co_khung_ngoai_va_hang_dau_danh_sach_nam_duoi_luoi(self):
        image = self.chay({0: 120})
        px = image.load()
        x, y = self.o(29)
        # Khung ngoai bo tron: cot x-3 den o doan thang, hai cot x-2 va x-1 la khoang trang 2 px.
        doan = range(y + 10, y + SIDE - 10)
        self.assertTrue(all(px[x - 3, yy] < 128 for yy in doan), "thieu khung ngoai cua hom nay")
        self.assertTrue(all(px[x - 2, yy] > 128 and px[x - 1, yy] > 128 for yy in doan), "khung ngoai dinh vao o")
        # O ngay hom qua (cot ben trai) khong co khung ngoai.
        x2, y2 = self.o(28)
        self.assertTrue(all(px[x2 + SIDE + 1, yy] > 128 for yy in range(y2 + 10, y2 + SIDE - 10)))
        # Hang chon dau tien cua danh sach (nen den, rong) bat dau duoi dong ngay cua bieu do. Do tu duoi vach day
        # bieu do: vach day cung dam nhu hang chon, do tu tren no thi bat nham vach day.
        base = self.vach_day(image)
        hang = next((yy for yy in range(base + 2, 792) if sum(px[xx, yy] < 128 for xx in range(40, 488, 4)) > 90), None)
        self.assertIsNotNone(hang, "khong thay hang chon cua danh sach")
        self.assertGreater(hang, base + LABEL_H, "danh sach de len dong ngay cua bieu do")

    def test_khong_ve_mui_ten_canh_ngang_luoi(self):
        # Mui ten phim canh (x 4..10 va doi xung ben phai, y 195 +- 4) nam ngang hang 3 cua luoi, doc nhu nut
        # sang thang. Man nay bo chung khi co luoi.
        px = self.chay({0: 120}).load()
        den = sum(px[xx, yy] < 128 for yy in range(191, 200) for xx in list(range(4, 11)) + list(range(517, 524)))
        self.assertEqual(den, 0, "con mui ten canh ngang luoi")

    def vach_day(self, image):
        """Hang diem anh cua vach day bieu do: hang dau tien duoi luoi co hon 400 diem den trong 24..504, day 1 px
        (hang chon cua danh sach cung dam nhu vay nhung nam sau vach, va cao nhieu px)."""
        px = image.load()
        tu = GRID_TOP + 3 * PITCH + LABEL_H
        den = lambda yy: sum(px[xx, yy] < 128 for xx in range(24, 504)) >= 400
        hang = next((yy for yy in range(tu, tu + 160) if den(yy)), None)
        self.assertIsNotNone(hang, "khong thay vach day bieu do")
        self.assertFalse(den(hang + 1), "vach day phai mong 1 px")
        return hang

    def cot(self, image):
        """Chieu cao 4 cot, tu cot cu nhat. Cot rong 15 px (1/8 cua 120) bat dau 12 px sau mep cot, do o giua cot; chu gio nam ben phai cot."""
        px = image.load()
        base = self.vach_day(image)
        cao = []
        for k in range(4):
            x = 24 + k * 120 + 12 + 7
            n = 0
            while px[x, base - 1 - n] < 128:
                n += 1
            cao.append(n)
        return cao

    def test_bieu_do_tuan_cot_ty_le_voi_phut_trong_bon_khung_bay_ngay(self):
        # Khung 3 (D-0 toi D-6): 120 + 60 = 180 phut. Khung 2 (D-7 toi D-13): 600. Khung 1 (D-14 toi D-20): 30.
        # Khung 0 (D-21 toi D-27): 60 phut o ngay cu nhat D-27. D-28 doc 900 phut nam ngoai bieu do: neu lot vao
        # thi cot lon nhat doi va moi cot thap di.
        image = self.chay({0: 120, 1: 60, 8: 600, 20: 30, 27: 60, 28: 900})
        mong = [60 * 36 // 600, 2, 36, 180 * 36 // 600]
        for ra, can in zip(self.cot(image), mong):
            self.assertAlmostEqual(ra, can, delta=1)

    def test_bieu_do_nam_duoi_luoi_va_danh_sach_nam_duoi_bieu_do(self):
        image = self.chay({0: 120, 8: 600})
        px = image.load()
        base = self.vach_day(image)
        # Dinh cot cao nhat, khong phai vach day, phai nam duoi dong ngay cua luoi.
        self.assertGreater(base - max(self.cot(image)), GRID_TOP + 3 * PITCH + LABEL_H, "bieu do de len luoi")
        hang = next((yy for yy in range(base + 2, 792) if sum(px[xx, yy] < 128 for xx in range(40, 488, 4)) > 90), None)
        self.assertIsNotNone(hang, "khong thay hang chon cua danh sach")
        self.assertGreater(hang, base + LABEL_H, "danh sach de len dong ngay cua bieu do")

    def test_khong_doc_gi_thi_bieu_do_chi_co_vach_day(self):
        self.assertEqual(self.cot(self.chay({})), [0, 0, 0, 0])

    def test_khong_doc_gi_thi_luoi_chi_co_khung(self):
        image = self.chay({})
        self.assertEqual([self.bac(image, i) for i in range(30)], [0] * 30)


if __name__ == "__main__":
    unittest.main()
