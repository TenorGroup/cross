"""Ba muc thanh trang thai ngoai trinh doc va sau muc thanh trang thai trong trinh doc.

U1: ngoai trinh doc co dung ba muc (0 Mac dinh nho, 1 Tat, 2 Lon). Khi Tat, vung noi
dung phai THAT SU rong ra (khong chi mat hinh ve pin/giờ/nhan nut).
U2: trong trinh doc co sau muc, doc lap voi lua chon ngoai trinh doc.

Bang chung la anh khung hinh cua may mo phong: do day cua dong chu cuoi cung phia
tren dai day, va luong muc o dai day.
"""

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = REPO / '.pio/build/simulator_x3_uc8279/program'
EPUB = REPO / 'test/epubs/test_kerning_ligature.epub'

# Man Cai dat > nhom Hien thi: bon nhip DOWN sang the Cai dat, hai nhip RIGHT sang nhom.
MO_HIEN_THI = '1000:DOWN;1500:DOWN;2000:DOWN;2500:DOWN;3000:RIGHT;3400:CONFIRM;'
DAY_DAY = 745  # tu day tro xuong la dai trang thai + nhan nut


class StatusBarModesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cross-status-modes-')
        self.sd = Path(self.tmp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'books').mkdir()
        shutil.copy(EPUB, self.sd / 'books/sach.epub')
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/books/sach.epub', 'title': 'Sach'}]}))
        self.shots = Path(self.tmp.name) / 'shots'
        self.shots.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def viet_cai_dat(self, **kwargs):
        st = {'language': 'VI', 'fontSize': 14, 'screenInverted': 0}
        st.update(kwargs)
        (self.store / 'settings.json').write_text(json.dumps(st))

    def chay(self, script, ten_anh, shot_ms):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=f'{shot_ms}:{self.shots}/{ten_anh}.bmp')
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=40)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log)
        return log

    def do(self, ten_anh):
        img = np.array(Image.open(self.shots / f'{ten_anh}.bmp').convert('L'))
        muc = img > 128 if (img > 128).sum() < img.size * 0.5 else img < 128
        rows = muc.sum(axis=1)
        noi_dung = [y for y in range(DAY_DAY) if rows[y] > 0]
        # So HANG chu that trong vung danh sach: mot hang la mot doan muc cao >= 8 px,
        # bo qua cac vet mong (vien, duong ke) de khong dem nham.
        hang = 0
        trong = True
        bat_dau = 0
        for y in range(200, DAY_DAY):
            if rows[y] > 0 and trong:
                bat_dau = y
                trong = False
            elif rows[y] == 0 and not trong:
                if y - bat_dau >= 8:
                    hang += 1
                trong = True
        if not trong and DAY_DAY - bat_dau >= 8:
            hang += 1
        return {
            'day_noi_dung': max(noi_dung) if noi_dung else 0,
            'muc_dai_day': int(muc[DAY_DAY:].sum()),
            'so_hang': hang,
            # Vung cua hang thu 11 trong danh sach Hien thi. Muc Mac dinh nho khong
            # voi toi day nen gan nhu trong; muc Tat phai ve duoc nhan cua hang do.
            'muc_hang_11': int(muc[700:721].sum()),
        }

    def test_tat_thanh_lay_lai_dung_mot_hang(self):
        """Tat: hang cuoi cua danh sach tut xuong thap hon, va dai day mat pin/giờ/nhan nut."""
        self.viet_cai_dat(globalStatusBarMode=0)
        self.chay(MO_HIEN_THI + '5600:QUIT', 'small', 5000)
        nho = self.do('small')
        self.viet_cai_dat(globalStatusBarMode=1)
        self.chay(MO_HIEN_THI + '5600:QUIT', 'off', 5000)
        tat = self.do('off')
        # Danh sach Hien thi co 11 hang: muc Mac dinh nho chi thay 10, muc Tat thay du 11.
        self.assertGreater(tat['muc_hang_11'], 400,
                           f"Tat phai ve duoc hang thu 11: {nho} vs {tat}")
        self.assertLess(nho['muc_hang_11'], 120,
                        f"Mac dinh nho khong duoc voi toi hang thu 11: {nho} vs {tat}")
        self.assertLess(tat['muc_dai_day'], nho['muc_dai_day'] / 2,
                        f"Tat phai bo pin/gio/nhan nut: {nho} vs {tat}")

    def test_muc_lon_giu_nguyen_bo_cuc(self):
        """Lon: van 10 hang nhu Mac dinh nho, chi khac net ve o dai day."""
        self.viet_cai_dat(globalStatusBarMode=0)
        self.chay(MO_HIEN_THI + '5600:QUIT', 'small2', 5000)
        nho = self.do('small2')
        self.viet_cai_dat(globalStatusBarMode=2)
        self.chay(MO_HIEN_THI + '5600:QUIT', 'large2', 5000)
        lon = self.do('large2')
        self.assertEqual(lon['day_noi_dung'], nho['day_noi_dung'],
                         f"Lon khong duoc dan lai bo cuc: {nho} vs {lon}")
        self.assertGreater(lon['muc_dai_day'], nho['muc_dai_day'],
                           f"Lon phai ve to hon trong cung dai: {nho} vs {lon}")

    def test_hai_pham_vi_doc_lap(self):
        """Chung Tat khong keo theo thanh cua trinh doc, va reader Tat cho them mot dong."""
        self.viet_cai_dat(globalStatusBarMode=0, readerStatusBarMode=2)
        self.chay('1000:CONFIRM;4000:QUIT', 'r-nho', 3400)
        chuan = self.do('r-nho')
        self.viet_cai_dat(globalStatusBarMode=1, readerStatusBarMode=2)
        self.chay('1000:CONFIRM;4000:QUIT', 'r-chung-tat', 3400)
        chung_tat = self.do('r-chung-tat')
        # Anh khac nhau o vai net cua dong ho trong thanh reader (gio doi theo phien),
        # nen so sanh phan noi dung va doi chieu rang thanh reader VAN duoc ve.
        self.assertEqual(chung_tat['day_noi_dung'], chuan['day_noi_dung'],
                         f"chung Tat khong duoc keo theo thanh reader: {chuan} vs {chung_tat}")
        self.assertGreater(chung_tat['muc_dai_day'], 200,
                           f"thanh reader phai con duoc ve khi chi chung Tat: {chung_tat}")
        self.viet_cai_dat(globalStatusBarMode=0, readerStatusBarMode=0)
        self.chay('1000:CONFIRM;4000:QUIT', 'r-tat', 3400)
        reader_tat = self.do('r-tat')
        self.assertGreater(reader_tat['day_noi_dung'], chuan['day_noi_dung'] + 10,
                           f"reader Tat phai cho them dong: {chuan} vs {reader_tat}")

    def test_co_cu_di_cu_thanh_muc_tat(self):
        """File v1.0.2 co hideGlobalStatusBar = 1 phai thanh muc Tat va bo khoa cu."""
        (self.store / 'settings.json').write_text(json.dumps({'language': 'VI', 'hideGlobalStatusBar': 1}))
        self.chay('1000:QUIT', 'mig', 700)
        luu = json.loads((self.store / 'settings.json').read_text())
        self.assertEqual(luu.get('globalStatusBarMode'), 1, luu)
        self.assertNotIn('hideGlobalStatusBar', luu, luu)


if __name__ == '__main__':
    unittest.main()
