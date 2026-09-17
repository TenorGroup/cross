"""HOP DONG THONG KE: so lieu doc va danh hieu, do bang simulator X3 that.

Moi ca chay trinh mo phong that voi mot the SD tam (tempfile). KHONG dung du lieu
that cua founder: moi thu duoc ghi trong thu muc tam va xoa khi bai ket thuc.

Bang chung, khong phai cam giac:
  - log firmware: `Entering activity: ..` (man nao duoc mo), `Frame row=.. top=..`
    (Home), `Recent card build=..`;
  - `.crosspoint/reading-stats.json` (khoa schema/bookEpoch/habits/activeBook/
    ngay/lacMs/lacPhut/lacTrang) va tung file sach `.crosspoint/reading-stats/
    tenor_<hash>.json`;
  - `.crosspoint/state.json` (vi tri/doc dang mo) doi chieu truoc/sau khi reset;
  - anh BMP tu CROSSPOINT_SIM_SCREENSHOTS (Pillow) de do vung chu/hop chon.

Hop dong do duoc tu firmware trong cay nay (khong suy lai):
  - Home co NAM the; nut CANH (UP/DOWN) doi the: ba nhip DOWN tu the dau roi
    CONFIRM mo man Danh hieu (`Entering activity: ReadingHabits`);
  - the THONG KE co sau hang: 1 Danh hieu, 2 Theo sach, 3 30 ngay qua,
    4 Trich dan, 5 Xoa het thong ke, 6 Xoa danh hieu. Nut TRUOC (LEFT/RIGHT) di
    vong: vong 0 = dai the, 1..N = hang, nen tu hang 1 them mot nhip RIGHT la hang 2;
  - `ngay` trong file thong ke sap xep theo ngay TANG DAN (kho.cacNgay());
  - moi nhip lat trang (ke ca lat LUI giua sach) tinh mot luot: do 16/09/2026
    voi RIGHT,RIGHT,LEFT,RIGHT thu duoc `turns = 4` va dong `ngay` cua ngay do
    duoc cong 4 luot; lat lui o trang dau khong tinh (bai cu da khoa);
  - phien duoi 30 giay khong thanh mot "phien doc" cua thoi quen (firmware
    `habits::Ledger::finish`: `visibleMs >= 30000`);
  - danh hieu chi duoc cap khi co can cu: `summarize()` doi activeMs >= 600000
    (10 phut), coverage >= 1, sessions >= 1, va phan tram phien nhieu nhieu <= 1/5;
  - hop xac nhan reset (OptionPopup) hien [Huy, Xac nhan] va o san sang Huy:
    mot nhip RIGHT doi sang Xac nhan roi CONFIRM moi xoa.

Cach tinh tay cho fixture nho (muc 2): ba sach, hai ngay da ghi trong the:
  ngay D-2: 5 phut / 7 trang, ngay D-1: 12 phut / 30 trang → kho 17 phut / 37 trang.
  Doc sach 1 bon nhip lat (RIGHT,RIGHT,LEFT,RIGHT) → dong cua HOM NAY phai la
  0 phut + phan le ms, 4 luot; hai dong cu giu nguyen tung byte gia tri.
  Doc tiep sach 2 den het sach: dong HOM NAY = turns(sach1) + turns(sach2),
  vi mot ngay chi co mot dong va hai cuon deu doc trong ngay do.

Chay: venv/bin/python -m unittest test_stats_contract
Do duoc in ra `DO_DUOC=...` va ghi vao CROSSPOINT_TEST_ARTIFACTS/stats_contract.json.
"""

import datetime
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get("TEST_PROGRAM", REPO / ".pio/build/simulator_x3_uc8279/program"))

# Home: ba nhip DOWN (doi the) + CONFIRM o hang 1 = man Danh hieu.
MO_DANH_HIEU = "1000:DOWN;1500:DOWN;2000:DOWN;2600:CONFIRM"
# Ba nhip DOWN = the Thong ke; them RIGHT = cac hang 2..6 cua the do.
TOP_THONG_KE = "1000:DOWN;1500:DOWN;2000:DOWN"
SHOT = 2400  # moc chup anh cho man da mo xong

DO_DUOC = {}


# --- phep tinh firmware, chep dung cong thuc trong ReadingStatsStore.cpp ---------
def ma_ngay_dia_phuong(offset_q=48, luc=None):
    """Ngay YYYYMMDD theo gio dia phuong: gio UTC + (Q - 48) * 15 phut."""
    t = luc or datetime.datetime.now(datetime.timezone.utc)
    return int((t + datetime.timedelta(minutes=(offset_q - 48) * 15)).strftime("%Y%m%d"))


def thu_tu_ngay(ma):
    """habits::ordinal cua mot ngay YYYYMMDD: 2000-01-01 = 1."""
    ngay = datetime.date(ma // 10000, ma // 100 % 100, ma % 100)
    return (ngay - datetime.date(2000, 1, 1)).days + 1


def ngay_truoc(ma, so_ngay):
    ngay = datetime.date(ma // 10000, ma // 100 % 100, ma % 100) - datetime.timedelta(days=so_ngay)
    return int(ngay.strftime("%Y%m%d"))


def la_cuoi_tuan(ma):
    return datetime.date(ma // 10000, ma // 100 % 100, ma % 100).weekday() >= 5


def _dem_dai_ink(image, y0, y1):
    """So dai dong chu (nhom hang co muc) trong vung [y0, y1)."""
    px = image.load()
    x0, x1 = 24, image.width - 24
    dai = 0
    trong = True
    for y in range(max(0, y0), min(image.height, y1)):
        muc = any(sum(px[x, y]) < 300 for x in range(x0, x1, 2))
        if muc and trong:
            dai += 1
        trong = not muc
    return dai


def _hop_chon(image, y0):
    """Dai dong dam lien tuc (hop cua hang dang chon); None neu khong thay."""
    px = image.load()
    x0, x1 = 24, image.width - 24
    can = ((x1 - x0) // 2) * 0.7
    dai, bat_dau = [], None
    for y in range(y0, image.height):
        toi = sum(1 for x in range(x0, x1, 2) if sum(px[x, y]) < 300) > can
        if toi and bat_dau is None:
            bat_dau = y
        if not toi and bat_dau is not None:
            dai.append((bat_dau, y - 1))
            bat_dau = None
    if bat_dau is not None:
        dai.append((bat_dau, image.height - 1))
    return max(dai, key=lambda r: r[1] - r[0]) if dai else None


def _ty_le(image):
    """He so phong to cua anh BMP so voi pixel logic (Retina = 2)."""
    return max(1.0, round(image.width / 528.0))


class KichBan:
    """Mot the SD tam + ham chay trinh mo phong, dung chung cho moi ca."""

    def __init__(self, test):
        self.tmp = tempfile.TemporaryDirectory(prefix="cross-stats-")
        test.addCleanup(self.tmp.cleanup)
        self.sd = Path(self.tmp.name)
        self.store = self.sd / ".crosspoint"
        self.store.mkdir(parents=True)
        self.test = test
        self.bien_co = 48

    # --- fixture ----------------------------------------------------------
    def dat_settings(self, **them):
        cai_dat = {"language": "VI", "clockHasBeenSynced": 1, "clockUtcOffsetQ": self.bien_co,
                   "sleepTimeout": 120, "globalStatusBarMode": 0}
        cai_dat.update(them)
        self.bien_co = cai_dat["clockUtcOffsetQ"]
        (self.store / "settings.json").write_text(json.dumps(cai_dat))
        return cai_dat

    def dat_state(self, **them):
        state = {"openEpubPath": "/sach1.txt", "readerActivityLoadCount": 0,
                 "lastSleepFromReader": False, "showBootScreen": False}
        state.update(them)
        (self.store / "state.json").write_text(json.dumps(state))
        return state

    def dat_sach(self, ten="sach1.txt", so_dong=6):
        (self.sd / ten).write_text(("Dong van ban kiem thu so lieu doc. " * 30 + "\n") * so_dong)
        return "/" + ten

    def dat_recent(self, *duong_dan):
        sach = [{"path": p, "title": Path(p).stem} for p in duong_dan]
        (self.store / "recent.json").write_text(json.dumps({"books": sach}))

    def dat_thong_ke(self, **khoa):
        (self.store / "reading-stats.json").write_text(json.dumps(khoa))

    # --- chay -------------------------------------------------------------
    def chay(self, script, shots=(), timeout=90):
        if not (self.store / "settings.json").exists():
            self.dat_settings()
        if not (self.store / "state.json").exists():
            self.dat_state()
        env = {k: v for k, v in os.environ.items() if not k.startswith("CROSSPOINT_SIM_")}
        env.update(SDL_VIDEODRIVER="dummy", CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=script)
        if shots:
            env["CROSSPOINT_SIM_SCREENSHOTS"] = ";".join(f"{ms}:{self.sd / (ten + '.bmp')}" for ms, ten in shots)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        self.test.assertEqual(run.returncode, 0, f"simulator exit {run.returncode}\n{log[-4000:]}")
        self.test.assertIn("Entering activity: Home", log, log[-2000:])
        return log

    def doc(self):
        path = self.store / "reading-stats.json"
        return json.loads(path.read_text()) if path.exists() else {}

    def doc_sach(self):
        """Tung file sach rieng: {path: ban ghi}."""
        ket_qua = {}
        thu_muc = self.store / "reading-stats"
        if not thu_muc.is_dir():
            return ket_qua
        for tep in sorted(thu_muc.glob("tenor_*.json")):
            ban_ghi = json.loads(tep.read_text())
            ket_qua[ban_ghi.get("path", tep.name)] = ban_ghi
        return ket_qua

    def ngay(self, ma=None):
        """Dong `ngay` cua mot ma ngay (mac dinh HOM NAY theo gio dia phuong)."""
        ma = ma if ma is not None else ma_ngay_dia_phuong(self.bien_co)
        for dong in self.doc().get("ngay", []):
            if dong[0] == ma:
                return dong
        return None

    def anh(self, ten):
        path = self.sd / (ten + ".bmp")
        self.test.assertTrue(path.exists(), f"thieu anh {ten}")
        image = Image.open(path).convert("RGB")
        out = os.environ.get("CROSSPOINT_TEST_ARTIFACTS")
        if out:
            Path(out).mkdir(parents=True, exist_ok=True)
            image.save(Path(out) / (ten + ".png"))
        return image


def _sach_gia(kich_ban, so_sach=3):
    """Ba cuon txt sau sau dong (moi dong ~ mot trang) + recent.json tro cuon dau."""
    duong_dan = [kich_ban.dat_sach(f"sach{i}.txt", so_dong=6) for i in range(1, so_sach + 1)]
    kich_ban.dat_recent(*duong_dan)
    return duong_dan


class StatsContractTest(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.assertTrue(PROGRAM.exists(), f"thieu trinh mo phong: {PROGRAM}")

    # =====================================================================
    # MUC 1 — fixture: mot ngay, hai ngay, nhieu sach, khong du lieu,
    #          phien ngan, dong ho chua sync, mui gio / cuoi tuan.
    # =====================================================================
    def test_1a_khong_co_du_lieu_thi_khong_bia_so(self):
        kb = KichBan(self)
        _sach_gia(kb, 2)
        log = kb.chay(f"{MO_DANH_HIEU};3600:QUIT", shots=[(SHOT, "khong-du-lieu")])
        self.assertIn("Entering activity: ReadingHabits", log)
        self.assertFalse((kb.store / "reading-stats.json").exists(),
                         "chua doc gi thi firmware khong duoc tu tao file thong ke")
        anh = kb.anh("khong-du-lieu")
        # Sau khi doc mot cuon: file moi sinh ra va chi co dung ngay hom nay.
        log = kb.chay("1000:CONFIRM;2500:RIGHT;3200:RIGHT;4400:BACK;6200:QUIT")
        self.assertIn("Entering activity: TxtReader", log)
        du_lieu = kb.doc()
        self.assertEqual([dong[0] for dong in du_lieu.get("ngay", [])], [ma_ngay_dia_phuong(kb.bien_co)])
        DO_DUOC["khong_du_lieu"] = {"file_ton_tai": False, "man_cao": anh.height,
                                    "anh_rong": anh.width, "ngay_sau_lan_doc_dau": du_lieu.get("ngay")}

    def test_1b_mot_ngay_va_hai_ngay_giu_nguyen_so_cu(self):
        kb = KichBan(self)
        _sach_gia(kb, 2)
        hom_nay = ma_ngay_dia_phuong(48)
        hai_dong = [[ngay_truoc(hom_nay, 1), 12, 30], [ngay_truoc(hom_nay, 2), 5, 7]]
        kb.dat_settings()
        kb.dat_thong_ke(schema=3, bookEpoch=0, ngay=hai_dong, lacPhut=3, lacTrang=4)
        kb.chay("1000:CONFIRM;2500:RIGHT;2600:RIGHT;4000:BACK;6000:QUIT")
        du_lieu = kb.doc()
        self.assertEqual(sorted(dong[:3] for dong in du_lieu["ngay"] if dong[0] != hom_nay), sorted(hai_dong),
                         "hai ngay da ghi phai giu nguyen (kho sap xep ngay TANG DAN)")
        self.assertEqual(du_lieu["lacPhut"], 3)
        self.assertEqual(du_lieu["lacTrang"], 4)
        self.assertEqual(len(du_lieu["ngay"]), 3, "hai ngay cu + mot ngay hom nay")
        DO_DUOC["hai_ngay"] = du_lieu["ngay"]

    def test_1c_nhieu_sach_tach_so_tung_cuon(self):
        kb = KichBan(self)
        duong_dan = _sach_gia(kb, 3)
        kb.dat_settings()
        kb.chay("1000:CONFIRM;2500:RIGHT;2600:RIGHT;3800:BACK;5500:QUIT")
        # Mo cuon thu hai: recent.json tro thang toi no (hop dong cua founder).
        kb.dat_recent(duong_dan[1], duong_dan[0], duong_dan[2])
        kb.chay("1000:CONFIRM;2500:RIGHT;2600:RIGHT;3800:BACK;5500:QUIT")
        du_lieu = kb.doc()
        luu = kb.doc_sach()
        self.assertEqual(du_lieu["activeBook"]["path"], duong_dan[1], "dang doc cuon 2")
        self.assertIn(duong_dan[0], luu, "so cuon 1 phai duoc cat vao file rieng khi doi sach")
        self.assertEqual(luu[duong_dan[0]]["turns"], 2, "cuon 1: hai nhip lat")
        self.assertNotIn(duong_dan[1], luu, "cuon dang doc khong nam trong file rieng")
        self.assertEqual(du_lieu["activeBook"]["turns"], 2, "cuon 2: hai nhip lat")
        DO_DUOC["nhieu_sach"] = {"activeBook": du_lieu["activeBook"]["path"],
                                 "luu": {k: v["turns"] for k, v in luu.items()}}

    def test_1d_phien_ngan_khong_thanh_phien_doc(self):
        kb = KichBan(self)
        _sach_gia(kb, 1)
        kb.dat_settings()
        # Nhip 17/09/2026: QUIT ngay trong trinh doc chi giet tien trinh mo phong truoc khi
        # onExit kip chot so lieu, nen tep thong ke khong he duoc tao (do lai: khong co
        # reading-stats.json). Them mot nhip BACK de thoat han roi moi QUIT - phep kiem van la
        # "phien ngan", chi khac la no ket thuc dung cach nguoi dung ket thuc.
        kb.chay("1000:CONFIRM;2400:RIGHT;3200:BACK;3800:QUIT")
        du_lieu = kb.doc()
        phien = du_lieu.get("habits", {}).get("days", [])
        self.assertEqual(du_lieu.get("habits", {}).get("awarded", 0), 0)
        self.assertTrue(phien == [] or phien[0][4] == 0,
                        f"duoi 30 giay khong duoc tinh la mot phien doc: {phien}")
        self.assertEqual(du_lieu.get("ngay", [[0, 0, 0]])[0][2], 1, "van tinh dung mot nhip lat")
        DO_DUOC["phien_ngan"] = {"ngay": du_lieu.get("ngay"), "habits_days": phien,
                                 "activeBook": du_lieu.get("activeBook")}

    def test_1e_dong_ho_chua_sync_thi_khong_gan_bua_ngay(self):
        kb = KichBan(self)
        _sach_gia(kb, 1)
        kb.dat_settings(clockHasBeenSynced=0)
        kb.chay("1000:CONFIRM;2500:RIGHT;2600:RIGHT;3800:BACK;5500:QUIT")
        du_lieu = kb.doc()
        self.assertEqual(du_lieu.get("ngay", []), [], "chua co ngay thi khong duoc ghi vao mot ngay nao")
        self.assertGreaterEqual(du_lieu.get("lacTrang", 0), 2, "luot lat vao thung chua biet ngay")
        self.assertGreater(du_lieu.get("lacMs", 0), 0, "thoi gian vao thung chua biet ngay")
        self.assertEqual(du_lieu["activeBook"]["first"], 0, "chua biet ngay thi khong dan ngay bua")
        lac_truoc = (du_lieu["lacPhut"], du_lieu["lacTrang"])
        DO_DUOC["chua_sync"] = {"ngay": du_lieu.get("ngay"), "lacPhut": du_lieu["lacPhut"],
                                "lacTrang": du_lieu["lacTrang"], "lacMs": du_lieu["lacMs"]}
        # Dong bo dong ho roi doc tiep: ngay moi duoc ghi rieng, phan cu KHONG bi gan bua.
        kb.dat_settings(clockHasBeenSynced=1)
        kb.chay("1000:CONFIRM;2500:RIGHT;2600:RIGHT;3800:BACK;5500:QUIT")
        du_lieu = kb.doc()
        self.assertEqual([dong[0] for dong in du_lieu["ngay"]], [ma_ngay_dia_phuong(kb.bien_co)])
        self.assertGreaterEqual(du_lieu["lacTrang"], lac_truoc[1], "phan chua biet ngay phai con nguyen")
        DO_DUOC["sau_sync"] = {"ngay": du_lieu["ngay"], "lacTrang": du_lieu["lacTrang"],
                               "lacPhut": du_lieu["lacPhut"]}

    def test_1f_mui_gio_va_cuoi_tuan_khong_suy_dien(self):
        do_duoc = {}
        for q, nhan in ((48, "utc"), (104, "utc+14")):
            kb = KichBan(self)
            _sach_gia(kb, 1)
            kb.dat_settings(clockUtcOffsetQ=q)
            kb.chay("1000:CONFIRM;2500:RIGHT;3000:RIGHT;4200:BACK;6000:QUIT")
            ma = ma_ngay_dia_phuong(q)
            self.assertIsNotNone(kb.ngay(ma),
                                 f"{nhan}: ngay doc phai la ngay dia phuong {ma}: {kb.doc().get('ngay')}")
            do_duoc[nhan] = ma
        DO_DUOC["mui_gio"] = do_duoc
        # Cuoi tuan: mot ngay Chu nhat lon cung KHONG duoc cap danh hieu cuoi tuan,
        # vi luat doi coverage == 28 ngay (summarize: WEEKEND).
        kb = KichBan(self)
        _sach_gia(kb, 1)
        hom_nay = ma_ngay_dia_phuong(48)
        chu_nhat = max(ngay_truoc(hom_nay, i) for i in range(7) if la_cuoi_tuan(ngay_truoc(hom_nay, i)))
        kb.dat_settings()
        kb.dat_thong_ke(schema=3, bookEpoch=0, ngay=[[chu_nhat, 20, 40]],
                        habits={"first": thu_tu_ngay(chu_nhat), "evaluated": 0, "awarded": 0,
                                "days": [[thu_tu_ngay(chu_nhat), 7200000, 0, 0, 3, 0, 0, 0, False]]})
        kb.chay("1000:CONFIRM;2500:RIGHT;2600:RIGHT;3800:BACK;5500:QUIT")
        bit = kb.doc()["habits"]["awarded"]
        self.assertEqual(bit & 16, 0, f"hai ngay du lieu thi khong duoc cap danh hieu cuoi tuan (awarded={bit})")
        DO_DUOC["cuoi_tuan"] = {"ngay_chu_nhat": chu_nhat, "awarded": bit}

    # =====================================================================
    # MUC 2 — tong va theo tung sach, luot lat hai chieu, tien do khi het sach
    # =====================================================================
    def test_2_tong_ngay_va_tung_cuon_khop_phep_tinh_tay(self):
        kb = KichBan(self)
        duong_dan = _sach_gia(kb, 3)
        hom_nay = ma_ngay_dia_phuong(48)
        hai_dong = [[ngay_truoc(hom_nay, 1), 12, 30], [ngay_truoc(hom_nay, 2), 5, 7]]
        kb.dat_settings()
        kb.dat_thong_ke(schema=3, bookEpoch=0, ngay=hai_dong)
        # Cuon 1: bon nhip lat, trong do mot nhip lat LUI giua sach.
        kb.chay("1000:CONFIRM;2500:RIGHT;3200:RIGHT;3900:LEFT;4700:RIGHT;6000:BACK;8000:QUIT",
                shots=[(2000, "dang-doc")])
        du_lieu = kb.doc()
        dong = kb.ngay(hom_nay)
        self.assertIsNotNone(dong)
        self.assertEqual(dong[2], 4, "tat ca bon nhip lat (ke ca lat lui giua sach) deu tinh")
        self.assertEqual(dong[1], 0, "phien ngan chua du mot phut")
        self.assertGreater(dong[3], 200, "phai co phan le ms")
        cuon = du_lieu["activeBook"]
        self.assertEqual(cuon["path"], duong_dan[0])
        self.assertEqual(cuon["turns"], dong[2], "so lat cua cuon trung voi dong cua ngay")
        self.assertEqual(cuon["minutes"] * 60000 + cuon["ms"], dong[1] * 60000 + dong[3],
                         "thoi gian cuon trung voi dong cua ngay khi chi doc mot cuon")
        self.assertEqual([cuon["first"], cuon["last"]], [hom_nay, hom_nay])
        self.assertEqual(cuon["days"], 1)
        self.assertGreater(cuon["progress"], 0)
        self.assertEqual(sorted(d[:3] for d in du_lieu["ngay"] if d[0] != hom_nay), sorted(hai_dong),
                         "ngay cu giu nguyen")
        DO_DUOC["tay_cuon1"] = {"ngay": dong, "activeBook": cuon}
        # Cuon 2 doc den het sach: tien do phai cham 100%.
        kb.dat_recent(duong_dan[1], duong_dan[0], duong_dan[2])
        # Nhip 17/09/2026: fixture la 14 trang voi cai dat cua bai nay, nen 12 nhip chi toi
        # 13/14 = 93% (do lai), phai 13 nhip moi cham trang cuoi; 16 nhip thi vuot qua cuon
        # (activeBook doi sang sach khac). Giu nguyen moi ky vong, chi sua so nhip cho khop
        # do dai that cua fixture.
        script = "1000:CONFIRM;" + ";".join(f"{2400 + i * 260}:RIGHT" for i in range(13))
        kb.chay(script + ";7000:BACK;9000:QUIT")
        du_lieu = kb.doc()
        luu = kb.doc_sach()
        cuon2 = du_lieu["activeBook"]
        self.assertEqual(cuon2["path"], duong_dan[1])
        self.assertEqual(cuon2["progress"], 100, "doc het sach thi tien do vi tri la 100%")
        self.assertEqual(luu[duong_dan[0]]["turns"], 4, "cuon 1 duoc cat nguyen so cu")
        dong = kb.ngay(hom_nay)
        self.assertEqual(dong[2], 4 + cuon2["turns"], "tong luot lat trong ngay = cuon 1 + cuon 2")
        DO_DUOC["tay_cuon2"] = {"turns": cuon2["turns"], "progress": cuon2["progress"], "ngay": dong,
                                "luu_turns": {k: v["turns"] for k, v in luu.items()}}

    def test_2_ban_ghi_tung_sach_khop_voi_dong_ngay(self):
        # Ban ghi theo sach (man "Theo sach" doc dung file nay) phai khop voi
        # dong cua ngay khi ca ngay chi doc mot cuon.
        kb = KichBan(self)
        duong_dan = _sach_gia(kb, 2)
        kb.dat_settings()
        kb.chay("1000:CONFIRM;2500:RIGHT;3200:RIGHT;4400:BACK;6200:QUIT")
        cuon = kb.doc()["activeBook"]
        dong = kb.ngay()
        self.assertEqual(cuon["path"], duong_dan[0])
        self.assertEqual(cuon["turns"], 2)
        self.assertEqual(dong[2], cuon["turns"])
        self.assertEqual(dong[1] * 60000 + dong[3], cuon["minutes"] * 60000 + cuon["ms"])
        DO_DUOC["theo_sach"] = {"turns": cuon["turns"], "minutes": cuon["minutes"], "ms": cuon["ms"],
                                "progress": cuon["progress"], "ngay": dong}

    # =====================================================================
    # MUC 3 — danh hieu: co can cu that thi hien, it du lieu thi noi ro
    # =====================================================================
    def _fixture_danh_hieu(self, kb, co_can_cu):
        """Mot ngay hom qua: 26 phut 40 giay doc dem, mot phien dai (>=25 phut)."""
        hom_qua = ngay_truoc(ma_ngay_dia_phuong(48), 1)
        kb.dat_settings()
        kb.dat_thong_ke(
            schema=3, bookEpoch=0, ngay=[[hom_qua, 26, 40]],
            habits={"first": thu_tu_ngay(hom_qua), "evaluated": 0, "awarded": 0,
                    "days": [[thu_tu_ngay(hom_qua), 1600000, 1600000, 0, 1, 0, 1, 0, True]] if co_can_cu else []})
        return hom_qua

    def test_3_co_can_cu_thi_cap_danh_hieu_va_hien_ra(self):
        do_duoc = {}
        for co_can_cu, nhan in ((True, "du"), (False, "it")):
            # The Thong ke (dai the): dong ten danh hieu nam tren bang so.
            kb_the = KichBan(self)
            _sach_gia(kb_the, 2)
            self._fixture_danh_hieu(kb_the, co_can_cu)
            log = kb_the.chay(f"{TOP_THONG_KE};3400:QUIT", shots=[(SHOT, f"the-thong-ke-{nhan}")])
            self.assertNotIn("Entering activity: ReadingHabits", log, "ba nhip DOWN chi doi the, khong mo man")
            do_duoc[nhan + "_anh_the"] = kb_the.anh(f"the-thong-ke-{nhan}")
            # Man Danh hieu: tung dong co o "dat" hay khong.
            kb = KichBan(self)
            _sach_gia(kb, 2)
            self._fixture_danh_hieu(kb, co_can_cu)
            log = kb.chay(f"{MO_DANH_HIEU};3200:RIGHT;4200:QUIT", shots=[(SHOT, f"danh-hieu-{nhan}")])
            self.assertIn("Entering activity: ReadingHabits", log)
            do_duoc[nhan + "_anh"] = kb.anh(f"danh-hieu-{nhan}")
            # Doc tiep mot lan that: firmware phai ghi dung bit danh hieu.
            kb.chay("1000:CONFIRM;2500:RIGHT;3200:RIGHT;4400:BACK;6200:QUIT")
            do_duoc[nhan + "_awarded"] = kb.doc()["habits"]["awarded"]
        self.assertEqual(do_duoc["du_awarded"], 33,
                         "du can cu: NIGHT (1) + LONG (32); it du lieu: 0, khong suy dien")
        self.assertEqual(do_duoc["it_awarded"], 0)
        anh_du, anh_it = do_duoc["du_anh"], do_duoc["it_anh"]
        tl = _ty_le(anh_du)
        vung = (int(anh_du.width * 0.55), int(60 * tl), anh_du.width - 24, int(620 * tl))
        self.assertIsNotNone(ImageChops.difference(anh_du.crop(vung), anh_it.crop(vung)).getbbox(),
                             "man Danh hieu phai khac nhau giua 'dat' va 'chua du can cu'")
        # The Thong ke: co danh hieu thi bang so bi day xuong mot dong danh hieu
        # (ReadingStatsView::BADGE_HEIGHT = 40) — khac biet phai bat dau tu dai do.
        anh_the_du, anh_the_it = do_duoc["du_anh_the"], do_duoc["it_anh_the"]
        tl = _ty_le(anh_the_du)
        hieu = ImageChops.difference(anh_the_du, anh_the_it).getbbox()
        do_duoc["the_hieu_bbox"] = hieu
        do_duoc["dai_ink_the"] = {
            "du": _dem_dai_ink(anh_the_du, int(125 * tl), int(320 * tl)),
            "it": _dem_dai_ink(anh_the_it, int(125 * tl), int(320 * tl)),
        }
        self.assertIsNotNone(hieu, "the Thong ke phai ve khac nhau khi co danh hieu")
        self.assertLess(hieu[1], 200 * tl,
                        f"khac biet phai bat dau tu dai danh hieu (duoi dai the): {hieu}")
        DO_DUOC["danh_hieu"] = {k: v for k, v in do_duoc.items() if not isinstance(v, Image.Image)}

    def test_3_it_du_lieu_thi_noi_ro_tinh_trang(self):
        kb = KichBan(self)
        _sach_gia(kb, 1)
        kb.dat_settings()
        log = kb.chay(f"{MO_DANH_HIEU};3600:QUIT", shots=[(SHOT, "danh-hieu-trong")])
        self.assertIn("Entering activity: ReadingHabits", log)
        du_lieu = kb.doc() if (kb.store / "reading-stats.json").exists() else {}
        self.assertEqual(du_lieu.get("habits", {}).get("awarded", 0), 0)
        DO_DUOC["it_du_lieu"] = {"habits": du_lieu.get("habits"), "ngay": du_lieu.get("ngay")}

    # =====================================================================
    # MUC 4 — reset theo pham vi, co xac nhan, khong mat sach/vi tri
    # =====================================================================
    def _fixture_reset(self, kb):
        hom_nay = ma_ngay_dia_phuong(48)
        kb.dat_settings()
        kb.dat_state(openEpubPath="/sach1.txt", readerActivityLoadCount=3)
        kb.dat_thong_ke(schema=3, bookEpoch=0, ngay=[[ngay_truoc(hom_nay, 1), 12, 30]],
                        lacPhut=3, lacTrang=4,
                        habits={"first": thu_tu_ngay(hom_nay), "evaluated": 0, "awarded": 33, "hidden": 0},
                        activeBook={"bookEpoch": 0, "path": "/sach1.txt", "title": "sach1", "minutes": 12,
                                    "ms": 0, "turns": 30, "first": ngay_truoc(hom_nay, 1),
                                    "last": ngay_truoc(hom_nay, 1), "days": 1, "progress": 42,
                                    "startProgress": 5})
        (kb.store / "reading-stats").mkdir()
        (kb.store / "reading-stats" / "tenor_0000000000000001.json").write_text(
            json.dumps({"bookEpoch": 0, "path": "/sach2.txt", "title": "sach2", "minutes": 5, "ms": 0,
                        "turns": 11, "first": ngay_truoc(hom_nay, 2), "last": ngay_truoc(hom_nay, 2),
                        "days": 1, "progress": 10, "startProgress": 0}))
        return hom_nay

    def test_4a_bo_qua_xac_nhan_thi_khong_xoa(self):
        kb = KichBan(self)
        _sach_gia(kb, 2)
        self._fixture_reset(kb)
        truoc = (kb.store / "reading-stats.json").read_text()
        # Hang 5 = Xoa het thong ke; hop xac nhan mo o san sang Huy, BACK = bo.
        log = kb.chay(f"{TOP_THONG_KE};2600:RIGHT;2900:RIGHT;3200:RIGHT;3500:RIGHT;3900:CONFIRM;"
                      "4700:BACK;5800:QUIT")
        self.assertEqual(log.count("Entering activity: Confirmation"), 1, "phai hoi xac nhan truoc khi xoa")
        self.assertEqual((kb.store / "reading-stats.json").read_text(), truoc,
                         "bo qua xac nhan thi file thong ke khong duoc doi")

    def test_4b_xoa_het_tinh_lai_tu_dau_giu_sach_va_vi_tri(self):
        kb = KichBan(self)
        _sach_gia(kb, 2)
        hom_nay = self._fixture_reset(kb)
        state_truoc = (kb.store / "state.json").read_text()
        log = kb.chay(f"{TOP_THONG_KE};2600:RIGHT;2900:RIGHT;3200:RIGHT;3500:RIGHT;3900:CONFIRM;"
                      "4700:RIGHT;5200:CONFIRM;6200:QUIT")
        self.assertEqual(log.count("Entering activity: Confirmation"), 1)
        du_lieu = kb.doc()
        self.assertEqual(du_lieu["schema"], 4)
        self.assertEqual(du_lieu["bookEpoch"], 1, "xoa het thi vong doi so sach tang len")
        self.assertEqual(du_lieu.get("ngay", []), [], "so ngay da bi xoa")
        self.assertIsNone(du_lieu.get("activeBook"))
        self.assertEqual(du_lieu.get("habits", {}).get("awarded", 0), 0)
        self.assertEqual(du_lieu.get("lacPhut", 0), 0)
        self.assertEqual(du_lieu.get("lacTrang", 0), 0)
        self.assertEqual(kb.doc_sach(), {}, "xoa het thi bo file sach cu")
        self.assertEqual((kb.store / "state.json").read_text(), state_truoc,
                         "vi tri/doc dang mo phai con nguyen")
        self.assertTrue((kb.sd / "sach1.txt").exists() and (kb.sd / "sach2.txt").exists(),
                        "tep sach khong duoc dung toi")
        DO_DUOC["reset_het"] = {k: du_lieu.get(k) for k in ("schema", "bookEpoch", "ngay", "habits", "activeBook")}
        DO_DUOC["reset_het"]["state_nguyen"] = True
        # Sau khi xoa, doc lai thi so moi bat dau tu dau, khong cong don so cu.
        kb.chay("1000:CONFIRM;2500:RIGHT;3200:RIGHT;4400:BACK;6200:QUIT")
        du_lieu = kb.doc()
        self.assertEqual(du_lieu["bookEpoch"], 1)
        dong = kb.ngay(hom_nay)
        self.assertIsNotNone(dong, "sau reset van tinh ngay moi")
        self.assertEqual(dong[1], 0, "phut bat dau lai tu 0")
        self.assertEqual(dong[2], 2, "chi tinh hai nhip lat moi")
        self.assertNotEqual([dong[1], dong[2]], [12, 30], "khong duoc cong lai so cu")
        DO_DUOC["reset_het_tinh_lai"] = du_lieu["ngay"]

    def test_4c_xoa_danh_hieu_giu_so_ngay(self):
        kb = KichBan(self)
        _sach_gia(kb, 2)
        self._fixture_reset(kb)
        # Hang 6 = Xoa danh hieu.
        log = kb.chay(f"{TOP_THONG_KE};2600:RIGHT;2900:RIGHT;3200:RIGHT;3500:RIGHT;3800:RIGHT;"
                      "4200:CONFIRM;5000:RIGHT;5500:CONFIRM;6500:QUIT")
        self.assertEqual(log.count("Entering activity: Confirmation"), 1)
        du_lieu = kb.doc()
        self.assertEqual(du_lieu.get("habits", {}).get("awarded", 0), 0, "danh hieu da bi xoa")
        self.assertEqual(du_lieu.get("habits", {}).get("days", []), [], "so ngay cua thoi quen da bi xoa")
        self.assertIn("ngay", du_lieu, "so ngay doc phai con")
        self.assertEqual(len(du_lieu["ngay"]), 1)
        self.assertEqual(du_lieu["schema"], 3, "xoa danh hieu khong tang vong doi sach")
        self.assertEqual(du_lieu["bookEpoch"], 0)
        self.assertEqual(du_lieu["activeBook"]["turns"], 30, "ban ghi cuon dang doc phai con")
        self.assertEqual(du_lieu["lacPhut"], 3)
        DO_DUOC["reset_danh_hieu"] = {"habits": du_lieu.get("habits"), "ngay": du_lieu.get("ngay"),
                                      "bookEpoch": du_lieu["bookEpoch"]}

    # =====================================================================
    # MUC 5 — thanh trang thai tat: man Thong ke dung them duoc dien tich
    # =====================================================================
    def test_5_thanh_trang_thai_tat_cho_them_dien_tich(self):
        do_duoc = {}
        for nhan, mode in (("thanh-bat", 0), ("thanh-tat", 1)):
            kb = KichBan(self)
            _sach_gia(kb, 1)
            hom_nay = ma_ngay_dia_phuong(48)
            kb.dat_settings(globalStatusBarMode=mode)
            kb.dat_thong_ke(schema=3, bookEpoch=0,
                            ngay=[[ngay_truoc(hom_nay, i), 5 + i, 10 + i] for i in range(20)])
            # Hang 3 cua the Thong ke = "30 ngay qua"; roi RIGHT 29 nhip toi hang cuoi.
            script = f"{TOP_THONG_KE};2600:RIGHT;2900:RIGHT;3300:CONFIRM;"
            script += ";".join(f"{4200 + i * 250}:RIGHT" for i in range(29))
            script += ";12400:BACK;13400:QUIT"
            log = kb.chay(script, shots=[(11500, f"cuoi-30-ngay-{nhan}")], timeout=60)
            self.assertIn("Entering activity: ReadingHistory", log)
            image = kb.anh(f"cuoi-30-ngay-{nhan}")
            tl = _ty_le(image)
            do_duoc[nhan] = {
                "so_dai_ink": _dem_dai_ink(image, int(140 * tl), int(image.height - 40 * tl)),
                "hop_chon": _hop_chon(image, int(140 * tl)),
                "cao": image.height, "rong": image.width, "anh": image,
            }
        bat, tat = do_duoc["thanh-bat"], do_duoc["thanh-tat"]
        DO_DUOC["thanh_trang_thai"] = {k: {kk: vv for kk, vv in v.items() if kk != "anh"}
                                       for k, v in do_duoc.items()}
        # 1. Hang cuoi phai toi duoc va duoc ve tron ven khi thanh tat.
        self.assertIsNotNone(tat["hop_chon"], "thanh tat: khong thay hop cua hang dang chon")
        cao_hop = tat["hop_chon"][1] - tat["hop_chon"][0] + 1
        self.assertGreaterEqual(cao_hop, 20, f"hang cuoi qua thap: {tat['hop_chon']}")
        self.assertLessEqual(tat["hop_chon"][1], tat["cao"] - 6,
                             f"thanh tat: hang cuoi cham mep duoi man hinh: {tat['hop_chon']}")
        # 2. Thanh tat dung them dien tich: khong it dong hon.
        self.assertGreaterEqual(tat["so_dai_ink"], bat["so_dai_ink"],
                                f"thanh tat phai hien >= so dong: {tat['so_dai_ink']} vs {bat['so_dai_ink']}")
        # 3. Hang cuoi nam trong nua duoi man hinh (da vao vung nhin, khong bi cat).
        self.assertGreater(tat["hop_chon"][0], tat["cao"] * 0.55,
                           f"con tro chua toi hang cuoi: {tat['hop_chon']}")
        # 4. Dai thanh trang thai that su bien mat.
        day = (0, bat["cao"] - 32, bat["rong"], bat["cao"])
        self.assertIsNotNone(ImageChops.difference(bat["anh"].crop(day), tat["anh"].crop(day)).getbbox(),
                             "globalStatusBarMode=1 phai bo dai trang thai day man")


def tearDownModule():
    out = os.environ.get("CROSSPOINT_TEST_ARTIFACTS")
    if out:
        Path(out).mkdir(parents=True, exist_ok=True)
        (Path(out) / "stats_contract.json").write_text(json.dumps(DO_DUOC, indent=2, ensure_ascii=False))
    print("DO_DUOC=" + json.dumps(DO_DUOC, ensure_ascii=False))


if __name__ == "__main__":
    unittest.main()
