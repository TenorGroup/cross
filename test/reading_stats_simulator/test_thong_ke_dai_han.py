"""MAN THONG KE DAI HAN tren trinh mo phong X3 (UC8279): 4 trang, doc diem anh tren anh chup.

Hom nay co dinh 21/09/2026 (thu Hai, CROSSPOINT_SIM_NOW). The co 90 ngay toi hom nay va 24 bo dem gio.
  - Trang 1: 4 tuan tu thu Hai 31/08, 7 cot; hom nay o hang 4 cot 1, nen co 22 o (6 ngay con lai cua tuan chua co o).
  - Trang 2, 3: 28 o moi trang.
  - Trang 4 (Theo gio): khong co o.
Phim canh DOWN sang trang sau, UP ve trang truoc.

BIEU DO (cot tuan o trang 1-3, 24 cot gio o trang 4): truc y trai (thoi gian) va truc y phai (luot lat trang) la 2 vach
doc den dai bang ca vung ve. Duong thu 2 co 1 cham den vien trang o moi diem: tuan = phut/lan doc, chi tuan co lan doc
moi co diem (trang 1: tuan 07/09 chua dem lan doc, tuan 21/09 chua co ngay doc, nen 2 diem); gio = so lan doc, 24 diem.
Cham do bang phep mo 5 x 5: net 2 px va chu bien mat, cot co vien trang tach khoi cham.
"""

import datetime
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))

# Home: ba nhip DOWN sang the Thong ke, hai nhip RIGHT toi hang 3, CONFIRM mo man; roi DOWN lat trang.
MO = ["2000:DOWN", "3700:DOWN", "5400:DOWN", "7100:RIGHT", "8800:RIGHT", "10500:CONFIRM"]
SHOTS = {12000: "trang1", 14000: "trang2", 16000: "trang3", 18000: "trang4", 20000: "lui3"}
TURNS = ["12600:DOWN", "14600:DOWN", "16600:DOWN", "18600:UP"]
SIDE = (528 - 48) // 7 - 8  # canh o: buoc 68, o 60


def fixture(sd):
    store = sd / ".crosspoint"
    store.mkdir()
    settings = {"language": "VI", "uiShell": 0, "uiTheme": 4, "uiTextSize": 0, "tenorPresetVersion": 1,
                "tipsEnabled": 1, "tenorButtonSymbols": 1, "tenorSideArrows": 1, "wakeIntoBook": 0,
                "sleepTimeout": 10}
    (store / "settings.json").write_text(json.dumps(settings))
    (store / "state.json").write_text(json.dumps({"showBootScreen": False, "openEpubPath": "",
                                                  "lastSleepFromReader": False}))
    end = datetime.date(2026, 9, 21)
    minutes = [0, 25, 70, 0, 140, 45, 210, 15, 0, 95, 60, 0, 185, 30]
    rows = []
    for back in range(89, -1, -1):
        m = minutes[back % len(minutes)]
        day = end - datetime.timedelta(days=back)
        # Tuan 07/09 - 13/09 doc ma chua dem lan doc (nhu truoc ban nang cap): tuan do khong co diem.
        sessions = 0 if datetime.date(2026, 9, 7) <= day <= datetime.date(2026, 9, 13) else 1 + m // 60
        if m:
            rows.append([int(day.strftime("%Y%m%d")), m, m // 2 + 3, 0, sessions])
    hours = [0, 0, 0, 0, 0, 0, 95, 140, 30, 0, 0, 20, 45, 30, 0, 0, 10, 25, 60, 150, 260, 410, 330, 120]
    sessions = [(h + 29) // 30 for h in hours]
    (store / "reading-stats.json").write_text(json.dumps(
        {"schema": 3, "ngay": rows, "hourMinutes": hours, "hourRemainderMs": [0] * 24, "hourSessions": sessions}))


def run():
    tmp = tempfile.TemporaryDirectory(prefix="cross-dai-han-")
    sd = Path(tmp.name)
    fixture(sd)
    env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
    last = max(SHOTS)
    env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_NOW="1790000000",
               CROSSPOINT_SIM_WAKE_REASON="power", CROSSPOINT_SIM_HTTP_PORT="29391",
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(MO + TURNS) + f";{last + 700}:QUIT",
               CROSSPOINT_SIM_SCREENSHOTS=";".join(f"{ms}:{sd}/{name}.bmp" for ms, name in SHOTS.items()))
    p = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
    log = p.stdout + p.stderr
    images = {name: np.asarray(Image.open(sd / f"{name}.bmp").convert("L")) for name in SHOTS.values()}
    out = os.environ.get("CROSSPOINT_TEST_ARTIFACTS")
    if out:
        Path(out).mkdir(parents=True, exist_ok=True)
        for name, image in images.items():
            Image.fromarray(image).save(Path(out) / f"{name}.png")
    tmp.cleanup()
    return p.returncode, log, images


def cells(image):
    """Cac o vuong canh SIDE trong vung than (dong 1 px xam truoc khi dem)."""
    body = ndimage.binary_closing(image[:690] < 128, structure=np.ones((3, 3)))
    lab, _ = ndimage.label(body, structure=np.ones((3, 3)))
    found = []
    for sl in ndimage.find_objects(lab):
        w, h = sl[1].stop - sl[1].start, sl[0].stop - sl[0].start
        # A white day's grey rim closes 1 px short; today's cell merges with its frame (3 px out).
        if w == h and w in (SIDE - 1, SIDE, SIDE + 6):
            found.append((sl[1].start, sl[0].start))
    return sorted(found, key=lambda c: (c[1], c[0]))


def runs(image, plot):
    """Cac doan den doc lien tuc it nhat `plot` diem trong vung than: (x, y dau, y cuoi)."""
    ink = image[:690] < 128
    found = []
    for x in range(image.shape[1]):
        run = 0
        for y, v in enumerate(ink[:, x]):
            run = run + 1 if v else 0
            if run == plot:
                end = y
                while end + 1 < ink.shape[0] and ink[end + 1, x]:
                    end += 1
                found.append((x, y - plot + 1, end))
                break
    return found


def dots(image, plot):
    """Cham tron 7 px trong vung ve (giua 2 truc, tu dinh truc toi day): khoi nho con lai sau phep mo 5 x 5."""
    found = runs(image, plot)
    top, bottom = min(r[1] for r in found) - 6, max(r[2] for r in found) + 6
    band = ndimage.binary_opening(image[top:bottom] < 128, structure=np.ones((5, 5)))
    lab, _ = ndimage.label(band)
    count = 0
    for sl in ndimage.find_objects(lab):
        w, h = sl[1].stop - sl[1].start, sl[0].stop - sl[0].start
        if 4 <= w <= 7 and 4 <= h <= 7:  # a bar is at least 8 px wide
            count += 1
    return count


class ThongKeDaiHan(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.code, cls.log, cls.images = run()

    def test_chay_va_vao_man(self):
        self.assertEqual(self.code, 0, self.log[-3000:])
        self.assertIn("Entering activity: ReadingHistory", self.log, self.log[-2000:])

    def test_ba_trang_tuan_bay_cot(self):
        self.assertEqual(len(cells(self.images["trang1"])), 22)
        for name in ("trang2", "trang3"):
            found = cells(self.images[name])
            self.assertEqual(len(found), 28, name)
            self.assertEqual(len({x for x, _ in found}), 7, name + ": 7 cot")
            xs = sorted({x for x, _ in found})
            left, right = xs[0], 528 - (xs[-1] + SIDE)
            self.assertLessEqual(abs(left - right), 1, name + ": luoi can giua")

    def test_trang_theo_gio_khong_co_o(self):
        self.assertEqual(len(cells(self.images["trang4"])), 0)
        self.assertFalse(np.array_equal(self.images["trang4"], self.images["trang3"]))

    def test_bieu_do_co_hai_truc_y(self):
        for name, plot in (("trang1", 72), ("trang4", 132)):
            xs = [x for x, _, _ in runs(self.images[name], plot)]
            self.assertTrue(xs and xs[0] < 120 and xs[-1] > 400, f"{name}: truc trai va phai {xs}")

    def test_duong_thu_hai_co_cham_o_moi_diem_co_so(self):
        self.assertEqual(dots(self.images["trang1"], 72), 2)
        self.assertEqual(dots(self.images["trang4"], 132), 24)

    def test_phim_len_ve_trang_truoc(self):
        self.assertTrue(np.array_equal(self.images["lui3"], self.images["trang3"]))


if __name__ == "__main__":
    unittest.main()
