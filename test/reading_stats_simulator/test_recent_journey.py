"""GAN DAY: kiem danh sach sach gan day tren man Home bang simulator X3 that.

Bang chung, khong phai cam giac:
  - log firmware: `Frame row=<ringPos> top=<top> total=<ms> heap=..` (HomeActivity::render),
    `Recent card build=<ms> cache=<bytes>` (drawRecentCard), `Entering activity: ..`;
  - `.crosspoint/state.json` khoa `openEpubPath` (sach vua mo) va `.crosspoint/recent.json`;
  - anh BMP tu CROSSPOINT_SIM_SCREENSHOTS (Pillow) de so vung bia, hop chon va dai day.

Hop dong da do (dung dung, khong suy lai):
  - Home hien NAM sach gan nhat; tep thieu KHONG chiem mot hang;
  - ring: 1..N = hang, cap nut TRUOC (LEFT/RIGHT) di vong trong 1..N va khong
    ghe dai the (v1.0.3); hai nut CANH (UP/DOWN) doi the; onEnter dat con tro ve HANG 1;
  - the bia ve theo `recentBooks.front()`; vung bia duoc cache lai nen moi nhip
    dieu huong KHONG duoc dung lai (khong co them dong `Recent card build=`).

Chay: venv/bin/python -m unittest test_recent_journey
So do duoc in ra dong `DO_DUOC=...` va ghi vao CROSSPOINT_TEST_ARTIFACTS/recent_journey_measure.json.
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
PROGRAM = REPO / ".pio/build/simulator_x3_uc8279/program"

# `Frame row=1 top=0 total=250ms heap=123456`
FRAME = re.compile(r"Frame row=(\d+) top=(\d+) total=(\d+)ms")
# `Recent card build=42ms cache=12345`
BUILD = re.compile(r"Recent card build=(\d+)ms cache=(\d+)")

# TenorMenuChrome: TAB_TOP = 5 + 48, TAB_HEIGHT = 59, tile = TAB_TOP + TAB_HEIGHT + 16.
COVER_TILE_TOP = 128
TEN_DOC = ("Mot doan van ban du dai de trinh doc mo nhanh. " * 40 + "\n") * 4

# So do doc duoc, ghi ra artefact de doi chieu voi bao cao.
DO_DUOC = {}


def frames(log):
    """(ringPos, top, ms) theo thu tu ve khung hinh."""
    return [(int(a), int(b), int(c)) for a, b, c in FRAME.findall(log)]


def builds(log):
    return [(int(a), int(b)) for a, b in BUILD.findall(log)]


def bmp_gia(path: Path, mau):
    """BMP that de kiem nhanh 'co bia that' khac 'khong bia' (fallback)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGB", (120, 180), mau).save(path)


def hop_chon(image):
    """Hop cua hang dang chon (dao mau nen dam) trong vung danh sach.

    Tra ve (y_tren, y_duoi) cua dai dong dam lien tuc cao nhat; None neu khong co.
    Dung de biet hang cuoi co duoc ve TRON VEN (khong bi cat) hay khong.
    """
    px = image.load()
    le, phai = 24, image.width - 24
    buoc = 2
    dai, bat_dau = [], None
    for y in range(COVER_TILE_TOP, image.height):
        dam = sum(1 for x in range(le, phai, buoc) if sum(px[x, y]) < 300)
        day = dam > ((phai - le) // buoc) * 0.6
        if day and bat_dau is None:
            bat_dau = y
        if not day and bat_dau is not None:
            dai.append((bat_dau, y - 1))
            bat_dau = None
    if bat_dau is not None:
        dai.append((bat_dau, image.height - 1))
    return max(dai, key=lambda r: r[1] - r[0]) if dai else None


class RecentJourneyTest(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="cross-recent-journey-")
        self.addCleanup(self.tmp.cleanup)
        self.sd = Path(self.tmp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir()
        self._env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}

    # --- fixture ------------------------------------------------------------
    def dat_sach(self, so_sach, thieu=(), cover_o=None, tieu_de=None, tac_gia=None, excerpt=None):
        """Ghi recent.json nguoc thoi gian (book1 moi nhat) + tep sach.

        `thieu`: chi so 1-based cua sach CO trong recent.json nhung KHONG co tep.
        `cover_o`: chi so 1-based cua sach co bia that (BMP that, khong co [HEIGHT] nen
        getCoverThumbPath tra ve dung duong dan do).
        """
        books = []
        for i in range(1, so_sach + 1):
            path = f"/book{i}.txt"
            if i not in thieu:
                (self.sd / path[1:]).write_text(TEN_DOC)
            book = {"path": path, "title": f"Sach kiem {i}", "author": f"Tac gia {i}", "coverBmpPath": "",
                    "excerpt": "Trich doan kiem thu."}
            if tieu_de and i in tieu_de:
                book["title"] = tieu_de[i]
            if tac_gia and i in tac_gia:
                book["author"] = tac_gia[i]
            if excerpt and i in excerpt:
                book["excerpt"] = excerpt[i]
            if cover_o == i:
                bmp_gia(self.store / "covers" / f"book{i}.bmp", (10, 90, 200))
                book["coverBmpPath"] = f"/.crosspoint/covers/book{i}.bmp"
            books.append(book)
        (self.store / "recent.json").write_text(json.dumps({"books": books}))
        return books

    def dat_settings(self, global_status_bar=0, **them):
        settings = {"language": "VI", "uiTheme": 4, "sleepTimeout": 10, "globalStatusBarMode": global_status_bar}
        settings.update(them)
        (self.store / "settings.json").write_text(json.dumps(settings))

    def dat_state(self, **them):
        state = {"openEpubPath": "", "lastSleepFromReader": False, "showBootScreen": False,
                 "readerActivityLoadCount": 0}
        state.update(them)
        (self.store / "state.json").write_text(json.dumps(state))

    # --- chay ---------------------------------------------------------------
    def chay(self, script, shots=(), timeout=45):
        if not (self.store / "settings.json").exists():
            self.dat_settings()
        if not (self.store / "state.json").exists():
            self.dat_state()
        env = dict(self._env, SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=script)
        if shots:
            env["CROSSPOINT_SIM_SCREENSHOTS"] = ";".join(f"{ms}:{self.sd / (ten + '.bmp')}" for ms, ten in shots)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, f"simulator exit {run.returncode}\n{log[-4000:]}")
        self.assertIn("Entering activity: Home", log, log[-2000:])
        return log

    def anh(self, ten):
        path = self.sd / (ten + ".bmp")
        self.assertTrue(path.exists(), f"thieu anh {ten}")
        image = Image.open(path).convert("RGB")
        artifact = os.environ.get("CROSSPOINT_TEST_ARTIFACTS")
        if artifact:
            out = Path(artifact)
            out.mkdir(parents=True, exist_ok=True)
            image.save(out / (ten + ".png"))
        return image

    @staticmethod
    def vung_bia(image):
        """Vung anh bia trong the (Tenor chrome: coverX=24, coverY=tile+6+34)."""
        w, h = (176, 264) if image.height >= 700 else (88, 132)
        y = COVER_TILE_TOP + 6 + 34
        return (24, y, 24 + w, y + h)

    @staticmethod
    def so_hang_cuoi(log):
        """ringPos lon nhat da ve = so hang cua the dang mo."""
        return max((row for row, _, _ in frames(log)), default=0)

    def mo_hang_cuoi(self, shots=()):
        """Tu HANG 1 (onEnter): LEFT quay vong thang toi hang cuoi, CONFIRM -> mo sach.

        v1.0.3 (UiTabListActivity::navigateButtons): cap nut mat truoc di vong
        1..N va KHONG ghe vi tri 0 cua dai the; hai nut canh moi doi the. Kich ban
        cu (LEFT hai lan) la hop dong truoc do va da mo nham cuon ap chot.
        """
        return self.chay("2500:LEFT;4200:CONFIRM;6500:QUIT", shots)

    def sd_khac(self):
        """Doi sang mot the SD gia moi (cach ly ca fixture) va tra ve ham tra lai."""
        tmp = tempfile.TemporaryDirectory(prefix="cross-recent-case-")
        self.addCleanup(tmp.cleanup)
        goc = (self.sd, self.store)
        self.sd, self.store = Path(tmp.name), Path(tmp.name) / ".crosspoint"
        self.store.mkdir(parents=True)

        def tra_lai():
            self.sd, self.store = goc

        return tra_lai

    # --- 1. dung nam sach gan nhat, thu tu, khong lap ------------------------
    def test_1_so_hang_theo_so_sach_va_mo_dung_hang_cuoi(self):
        # (so sach trong recent.json, tep thieu, so hang mong doi, hang cuoi mong doi)
        ca = [(0, (), 0, None), (1, (), 1, "/book1.txt"), (3, (), 3, "/book3.txt"),
              (5, (), 5, "/book5.txt"), (8, (), 5, "/book5.txt"),
              (10, (1,), 5, "/book6.txt")]
        for so_sach, thieu, mong_doi, hang_cuoi in ca:
            with self.subTest(so_sach=so_sach, thieu=thieu):
                tra_lai = self.sd_khac()
                try:
                    self.dat_sach(so_sach, thieu=thieu)
                    self.dat_settings()
                    self.dat_state()
                    log = self.mo_hang_cuoi(shots=[(3600, f"hang-cuoi-{so_sach}-{len(thieu)}")])
                    self.assertEqual(self.so_hang_cuoi(log), mong_doi,
                                     f"so hang sai voi {so_sach} sach (thieu {thieu}): {frames(log)}")
                    if hang_cuoi is not None:
                        self.assertIn("Entering activity: TxtReader", log, "CONFIRM o hang cuoi phai mo sach")
                        self.assertEqual(json.loads((self.store / "state.json").read_text())["openEpubPath"],
                                         hang_cuoi,
                                         "hang cuoi phai la sach gan nhat thu nam (bo qua tep thieu)")
                    else:
                        self.assertNotIn("Entering activity: TxtReader", log, "0 sach: CONFIRM khong duoc mo gi")
                    DO_DUOC[f"rows_{so_sach}_thieu_{len(thieu)}"] = self.so_hang_cuoi(log)
                finally:
                    tra_lai()

    def test_1_khong_lap_cung_mot_cuon_va_thu_tu_moi_nhat_truoc(self):
        self.dat_sach(5)
        self.dat_settings()
        self.dat_state()
        # Mot nhip CONFIRM ngay khi vao Home = HANG 1 = the "Doc tiep" = sach moi nhat.
        log = self.chay("2500:CONFIRM;4500:BACK;5500:QUIT", shots=[(4000, "the-dau")])
        self.assertIn("Entering activity: TxtReader", log)
        self.assertEqual(json.loads((self.store / "state.json").read_text())["openEpubPath"], "/book1.txt")
        recent = json.loads((self.store / "recent.json").read_text())["books"]
        self.assertEqual([b["path"] for b in recent[:5]],
                         ["/book1.txt", "/book2.txt", "/book3.txt", "/book4.txt", "/book5.txt"],
                         "thu tu phai la moi nhat truoc, khong lap cuon nao")
        # Anh the dau: vung bia co muc (khong trong).
        anh = self.anh("the-dau")
        bia = anh.crop(self.vung_bia(anh))
        DO_DUOC["the_dau_bia_ink"] = sum(1 for px in bia.getdata() if sum(px) < 600)
        self.assertGreater(DO_DUOC["the_dau_bia_ink"], 200, "the dau (bia/fallback) phai co hinh")

    def test_1_metadata_dai_va_thieu_khong_lam_hong_the(self):
        # Sach moi nhat: ten rat dai, khong tac gia, khong excerpt.
        self.dat_sach(5, tieu_de={1: "Sach ten rat dai " * 12}, tac_gia={1: ""}, excerpt={1: ""})
        self.dat_settings()
        self.dat_state()
        log = self.chay("2500:CONFIRM;4500:BACK;5500:QUIT", shots=[(4000, "metadata-dai")])
        self.assertEqual(json.loads((self.store / "state.json").read_text())["openEpubPath"], "/book1.txt")
        self.assertGreater(len(builds(log)), 0, "the bia phai duoc dung lan dau")
        DO_DUOC["metadata_dai_builds"] = builds(log)

    # --- 2. the "Doc tiep" + khong dung lai bia moi nhip ---------------------
    def test_2_con_tro_di_khong_dung_lai_bia(self):
        self.dat_sach(5, cover_o=1)
        self.dat_settings()
        self.dat_state()
        # RIGHT = nut TRUOC, di vong qua tung hang (vao Home da o HANG 1).
        log = self.chay("2500:RIGHT;3200:RIGHT;3900:RIGHT;4600:RIGHT;5600:QUIT",
                        shots=[(3000, "hang-1"), (3700, "hang-2"), (5100, "hang-5")])
        nhip = frames(log)
        self.assertGreaterEqual(len(nhip), 5, f"thieu khung hinh: {nhip}")
        self.assertEqual([row for row, _, _ in nhip][:5], [1, 2, 3, 4, 5], f"ring sai: {nhip}")
        # Dung lai bia: chi MOT lan dung the bia cho ca phien.
        dung_bia = builds(log)
        self.assertEqual(len(dung_bia), 1, f"bia bi dung lai moi nhip: {dung_bia}")
        DO_DUOC["builds_moi_nhip_di_chuyen"] = dung_bia
        DO_DUOC["nhip_di_chuyen_frame_ms"] = [ms for _, _, ms in nhip]
        # Vung bia phai y nguyen giua cac nhip (cache tra lai, khong ve lai).
        anh1, anh2 = self.anh("hang-1"), self.anh("hang-2")
        self.assertEqual(anh1.size, anh2.size)
        self.assertIsNone(ImageChops.difference(anh1.crop(self.vung_bia(anh1)),
                                                anh2.crop(self.vung_bia(anh2))).getbbox(),
                          "vung bia doi giua hai nhip dieu huong")

    def test_2_bia_that_khac_bia_fallback(self):
        do_duoc = {}
        for nhan, cover_o in (("that", 1), ("khong", None)):
            tra_lai = self.sd_khac()
            try:
                self.dat_sach(3, cover_o=cover_o)
                self.dat_settings()
                self.dat_state()
                log = self.chay("2500:RIGHT;3500:QUIT", shots=[(3000, f"bia-{nhan}")])
                self.assertGreater(builds(log)[0][1], 0, "the bia phai co cache")
                anh = self.anh(f"bia-{nhan}")
                do_duoc[nhan] = anh.crop(self.vung_bia(anh))
            finally:
                tra_lai()
        self.assertIsNotNone(ImageChops.difference(do_duoc["that"], do_duoc["khong"]).getbbox(),
                             "bia that (BMP) phai khac bia fallback")

    # --- 3. hanh trinh mo roi quay lai --------------------------------------
    def test_3_mo_tu_hang_giua_roi_back_ve_dung_sach(self):
        self.dat_sach(5)
        self.dat_settings()
        self.dat_state()
        # HANG 1 -> RIGHT -> HANG 2 -> RIGHT -> HANG 3 = book3.
        log = self.chay("2500:RIGHT;3200:RIGHT;4200:CONFIRM;6500:BACK;8000:CONFIRM;9800:BACK;11000:QUIT",
                        shots=[(5800, "sau-back")])
        self.assertEqual(log.count("Entering activity: TxtReader"), 2, "phai mo trinh doc dung hai lan")
        self.assertEqual(json.loads((self.store / "state.json").read_text())["openEpubPath"], "/book3.txt",
                         "nhip CONFIRM thu hai phai mo lai dung cuon vua doc, khong mo cuon khac")
        # Sau BACK, Home vao lai o HANG 1 va sach do la sach moi nhat (the Doc tiep).
        after_back = log.split("Entering activity: TxtReader", 1)[1].split("Exiting activity: Home", 1)[-1]
        self.assertIn("Entering activity: Home", after_back)
        hang_sau_back = [row for row, _, _ in frames(after_back)]
        self.assertEqual(hang_sau_back[0], 1, f"con tro sau BACK phai o HANG 1: {hang_sau_back}")
        recent = json.loads((self.store / "recent.json").read_text())["books"]
        self.assertEqual(recent[0]["path"], "/book3.txt", "sach vua doc phai len dau danh sach")
        DO_DUOC["hang_sau_back"] = hang_sau_back[:3]
        self.anh("sau-back")

    # --- 4. tep mat sau khi da vao danh sach ---------------------------------
    def test_4_tep_mat_khong_chiem_hang_khong_mo_nham(self):
        self.dat_sach(5)
        self.dat_settings()
        self.dat_state()
        log = self.mo_hang_cuoi()
        self.assertEqual(self.so_hang_cuoi(log), 5)
        # Doc sach lam recent.json doi thu tu (sach vua doc len dau), nen dat lai
        # fixture truoc khi xoa tep de phep so sanh van dung goc.
        self.dat_sach(5)
        (self.sd / "book3.txt").unlink()
        log = self.mo_hang_cuoi()
        self.assertEqual(self.so_hang_cuoi(log), 4, f"tep thieu khong duoc chiem hang: {frames(log)}")
        # Bon sach con lai (book1,book2,book4,book5): hang cuoi la book5.
        self.assertEqual(json.loads((self.store / "state.json").read_text())["openEpubPath"], "/book5.txt")
        self.assertEqual(log.count("Entering activity: TxtReader"), 1, "khong duoc mo nham cuon khac")
        DO_DUOC["rows_sau_khi_mat_tep"] = self.so_hang_cuoi(log)

    # --- 5. thanh trang thai TAT --------------------------------------------
    def test_5_thanh_trang_thai_tat_khong_cat_hang_cuoi(self):
        ket_qua = {}
        for nhan, mode in (("mac-dinh", 0), ("tat", 1)):
            tra_lai = self.sd_khac()
            try:
                self.dat_sach(5)
                self.dat_settings(mode)
                self.dat_state()
                log = self.mo_hang_cuoi(shots=[(3600, f"hang-cuoi-{nhan}")])
                anh = self.anh(f"hang-cuoi-{nhan}")
                ket_qua[nhan] = {"hang": self.so_hang_cuoi(log), "anh": anh, "hop": hop_chon(anh)}
            finally:
                tra_lai()
        DO_DUOC["hang_thanh_mac_dinh"] = ket_qua["mac-dinh"]["hang"]
        DO_DUOC["hang_thanh_tat"] = ket_qua["tat"]["hang"]
        DO_DUOC["hop_chon_mac_dinh"] = ket_qua["mac-dinh"]["hop"]
        DO_DUOC["hop_chon_tat"] = ket_qua["tat"]["hop"]
        self.assertEqual(ket_qua["tat"]["hang"], 5, "thanh tat: hang cuoi phai toi duoc")
        self.assertGreaterEqual(ket_qua["tat"]["hang"], ket_qua["mac-dinh"]["hang"],
                                "thanh tat phai hien >= so hang cua mac dinh nho")
        anh_md, anh_tat = ket_qua["mac-dinh"]["anh"], ket_qua["tat"]["anh"]
        # Dai day that su khac nhau: thanh trang thai TAT thi khong con dai do.
        day = (0, anh_md.height - 32, anh_md.width, anh_md.height)
        self.assertIsNotNone(ImageChops.difference(anh_md.crop(day), anh_tat.crop(day)).getbbox(),
                             "globalStatusBarMode=1 phai bo dai trang thai day man")
        # Hang cuoi phai ve TRON VEN: hop chon du cao va khong cham mep duoi man hinh.
        for nhan in ("mac-dinh", "tat"):
            hop = ket_qua[nhan]["hop"]
            self.assertIsNotNone(hop, f"{nhan}: khong tim thay hop cua hang dang chon")
            cao = hop[1] - hop[0] + 1
            self.assertGreaterEqual(cao, 24, f"{nhan}: hop chon qua thap ({hop})")
            self.assertLessEqual(cao, 60, f"{nhan}: hop chon qua cao, khong phai mot hang ({hop})")
            self.assertLessEqual(hop[1], ket_qua[nhan]["anh"].height - 8,
                                 f"{nhan}: hang cuoi cham mep duoi man hinh ({hop})")
        self.assertEqual(ket_qua["mac-dinh"]["hop"][1] - ket_qua["mac-dinh"]["hop"][0],
                         ket_qua["tat"]["hop"][1] - ket_qua["tat"]["hop"][0],
                         "chieu cao hang cuoi phai nhu nhau giua hai muc thanh")

    # --- 6. do thoi gian moi nhip + log dung lai bia ------------------------
    def test_6_do_thoi_gian_moi_nhip_va_log_dung_bia(self):
        self.dat_sach(5, cover_o=1)
        self.dat_settings()
        self.dat_state()
        log = self.chay("2500:RIGHT;3200:RIGHT;3900:RIGHT;4600:RIGHT;5600:QUIT")
        nhip = frames(log)
        DO_DUOC["nhip_frame_ms_item6"] = [ms for _, _, ms in nhip]
        DO_DUOC["builds_trong_ca_phien"] = builds(log)
        self.assertEqual(len(builds(log)), 1, f"bia dung lai: {builds(log)}")
        self.assertEqual(len(nhip), 5, f"thieu khung hinh do: {nhip}")
        # Nhip dieu huong sau khi the bia da cache: khong dung lai bia, khong qua 50ms.
        self.assertLessEqual(max(ms for _, _, ms in nhip[1:]), 50,
                             f"nhip dieu huong qua cham tren simulator: {nhip}")
        DO_DUOC["chenh_ms_moi_nhip"] = [nhip[i + 1][2] - nhip[i][2] for i in range(len(nhip) - 1)]



def tearDownModule():
    out = os.environ.get("CROSSPOINT_TEST_ARTIFACTS")
    if out:
        Path(out).mkdir(parents=True, exist_ok=True)
        (Path(out) / "recent_journey_measure.json").write_text(json.dumps(DO_DUOC, indent=2, ensure_ascii=False))
    print("DO_DUOC=" + json.dumps(DO_DUOC, ensure_ascii=False))


if __name__ == "__main__":
    unittest.main()
