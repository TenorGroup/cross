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
  - v1.0.11: the Gan day hien MOT cuon, cuon thu (ring - 1); moi nhip nut truoc doi
    sang cuon khac nen dung the dung MOT lan cho cuon do (mot dong `Recent card build=`),
    con ve lai ma khong doi cuon (doi the roi quay ve) thi lay lai tu cache, khong dung lai.

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


def dong_cuon_khac(image):
    """Dong "Cuon khac" duoi the: (y cua vach ke, (y_tren, y_duoi) cua muc chu ngay duoi vach).

    v1.0.11 bo danh sach sach cu; dong nay la thu duy nhat o day the. Vach ke chay tu x=40
    toi x=487; chu nam ngay duoi. None neu khong co vach.
    """
    px = image.convert("L").load()
    for vach in range(COVER_TILE_TOP + 360, image.height):
        if all(px[x, vach] < 128 for x in range(40, image.width - 40, 4)):
            break
    else:
        return None
    # Mot dong chu cao chung 26 diem anh; dau thanh co the tach khoi than chu nen lay tu hang
    # co muc dau tien toi hang co muc cuoi cung, khong doi hai hang lien nhau.
    co_muc = [y for y in range(vach + 1, min(image.height, vach + 36))
              if any(px[x, y] < 128 for x in range(40, image.width - 40))]
    return (vach, (co_muc[0], co_muc[-1])) if co_muc else (vach, (vach + 1, vach + 1))


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
        """Vung anh bia trong the v1.0.11: 236 x 356, giua man, ngay duoi dai the (X3)."""
        x = (image.width - 236) // 2
        return (x, COVER_TILE_TOP - 4, x + 236, COVER_TILE_TOP - 4 + 356)

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
        # Mot nhip CONFIRM ngay khi vao Home = HANG 1 = the dang hien sach moi nhat.
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

    # --- 2. moi nhip mot cuon, moi cuon dung the mot lan --------------------
    def test_2_moi_nhip_doi_cuon_va_chi_dung_the_mot_lan(self):
        self.dat_sach(5, cover_o=1)
        self.dat_settings()
        self.dat_state()
        # RIGHT = nut TRUOC, di vong qua tung cuon (vao Home da o HANG 1). Sau do DOWN roi UP:
        # sang the ben canh roi ve, van cuon thu nam, the lay lai tu cache.
        log = self.chay("2500:RIGHT;3200:RIGHT;3900:RIGHT;4600:RIGHT;5300:DOWN;6000:UP;7000:QUIT",
                        shots=[(2200, "hang-1"), (3000, "hang-2"), (5100, "hang-5"), (6600, "hang-5-ve")])
        nhip = frames(log)
        self.assertGreaterEqual(len(nhip), 5, f"thieu khung hinh: {nhip}")
        self.assertEqual([row for row, _, _ in nhip][:5], [1, 2, 3, 4, 5], f"ring sai: {nhip}")
        # Nam cuon, nam lan dung the; doi the roi ve khong dung them lan nao.
        dung_bia = builds(log)
        self.assertEqual(len(dung_bia), 5, f"so lan dung the sai: {dung_bia}")
        DO_DUOC["builds_moi_nhip_di_chuyen"] = dung_bia
        DO_DUOC["nhip_di_chuyen_frame_ms"] = [ms for _, _, ms in nhip]
        # Cuon 1 co bia that, cuon 2 thi khong: vung bia phai doi theo cuon dang hien.
        anh1, anh2 = self.anh("hang-1"), self.anh("hang-2")
        self.assertEqual(anh1.size, anh2.size)
        self.assertIsNotNone(ImageChops.difference(anh1.crop(self.vung_bia(anh1)),
                                                   anh2.crop(self.vung_bia(anh2))).getbbox(),
                             "vung bia khong doi khi sang cuon khac")
        anh5, anh5_ve = self.anh("hang-5"), self.anh("hang-5-ve")
        self.assertIsNone(ImageChops.difference(anh5.crop((0, COVER_TILE_TOP - 4, anh5.width, 756)),
                                                anh5_ve.crop((0, COVER_TILE_TOP - 4, anh5.width, 756))).getbbox(),
                          "the doi sau khi sang the ben canh roi ve")

    def test_2_bia_that_khac_bia_fallback(self):
        do_duoc = {}
        for nhan, cover_o in (("that", 1), ("khong", None)):
            tra_lai = self.sd_khac()
            try:
                self.dat_sach(3, cover_o=cover_o)
                self.dat_settings()
                self.dat_state()
                log = self.chay("3500:QUIT", shots=[(3000, f"bia-{nhan}")])
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
                ket_qua[nhan] = {"hang": self.so_hang_cuoi(log), "anh": anh, "hop": dong_cuon_khac(anh)}
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
        # Cuon cuoi dang hien, dong "Cuon khac" o day the phai ve TRON VEN: co vach, co chu
        # du cao ngay duoi vach, khong cham mep duoi man hinh.
        for nhan in ("mac-dinh", "tat"):
            hop = ket_qua[nhan]["hop"]
            self.assertIsNotNone(hop, f"{nhan}: khong tim thay dong Cuon khac")
            vach, (dau, cuoi) = hop
            cao = cuoi - dau + 1
            self.assertGreaterEqual(cao, 12, f"{nhan}: chu dong Cuon khac qua thap ({hop})")
            self.assertLessEqual(cao, 30, f"{nhan}: chu dong Cuon khac qua cao, khong phai mot dong ({hop})")
            self.assertLessEqual(cuoi, ket_qua[nhan]["anh"].height - 8,
                                 f"{nhan}: dong Cuon khac cham mep duoi man hinh ({hop})")
        self.assertEqual(ket_qua["mac-dinh"]["hop"][1][1] - ket_qua["mac-dinh"]["hop"][1][0],
                         ket_qua["tat"]["hop"][1][1] - ket_qua["tat"]["hop"][1][0],
                         "chieu cao dong Cuon khac phai nhu nhau giua hai muc thanh")

    # --- 6. do thoi gian moi nhip + log dung lai bia ------------------------
    def test_6_do_thoi_gian_moi_nhip_va_log_dung_bia(self):
        self.dat_sach(5, cover_o=1)
        self.dat_settings()
        self.dat_state()
        log = self.chay("2500:RIGHT;3200:RIGHT;3900:RIGHT;4600:RIGHT;5600:QUIT")
        nhip = frames(log)
        DO_DUOC["nhip_frame_ms_item6"] = [ms for _, _, ms in nhip]
        DO_DUOC["builds_trong_ca_phien"] = builds(log)
        # v1.0.11: moi nhip hien mot cuon khac, nen moi cuon dung the dung mot lan.
        self.assertEqual(len(builds(log)), 5, f"so lan dung the sai: {builds(log)}")
        self.assertEqual(len(nhip), 5, f"thieu khung hinh do: {nhip}")
        # Nhip dieu huong, ke ca dung the cho cuon moi: khong qua 50ms tren simulator.
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
