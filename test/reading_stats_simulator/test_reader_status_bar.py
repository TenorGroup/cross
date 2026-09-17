"""Sau muc thanh trang thai trong trinh doc: thanh phan, chieu cao, anchor.

Muc 0 Tat; 1 Dong ho & pin; 2 Mac dinh du; 3 Ten chuong & tien trinh chuong;
4 Ten chuong & dong ho; 5 Ten chuong & pin.

Bang chung lay tu anh khung hinh cua may mo phong: muc trong dai day chia lam ba
vung - goc trai (pin), giua (ten chuong va so trang), goc phai (dong ho) - va day
cua dong chu cuoi cung tren trang.
"""

import json
import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('STATUSBAR_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
EPUB = REPO / 'test/epubs/test_kerning_ligature.epub'
DAI_DAY = 765  # vung do dai day: duoi day chu (day chu cao nhat khi Tat la 764)

SPARSE_CONTAINER = '''<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>'''
SPARSE_OPF = '''<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Sparse status fixture</dc:title>
<dc:identifier id="id">status-bar-sparse</dc:identifier><dc:language>en</dc:language></metadata>
<manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest>
<spine><itemref idref="body"/></spine></package>'''
SPARSE_BODY = '''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Sparse status fixture</title></head>
<body><p>Office affine affinity. Anchor line.</p></body></html>'''


def write_sparse_epub(path: Path) -> None:
    """Create a short body so mode Off's bottom band contains no body descenders."""
    with zipfile.ZipFile(path, 'w') as archive:
        archive.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        archive.writestr('META-INF/container.xml', SPARSE_CONTAINER)
        archive.writestr('book.opf', SPARSE_OPF)
        archive.writestr('body.xhtml', SPARSE_BODY)

TAT, GIO_PIN, MAC_DINH, CHUONG_TIEN_TRINH, CHUONG_GIO, CHUONG_PIN = range(6)

# Doi muc ngay trong menu doc: mo sach, mo menu, sang the Doc hai nhip, xuong hang 3
# ("Thanh trang thai"), Chon mo popup, di toi muc can chon, Chon ap dung, Quay lai.
def doi_muc_qua_menu(buoc_popup: int) -> str:
    di = ''.join(f'{7600 + i * 500}:LEFT;' for i in range(buoc_popup))
    return ('1000:CONFIRM;3000:CONFIRM;3600:DOWN;4200:DOWN;5000:RIGHT;5400:RIGHT;6000:CONFIRM;'
            + di + '9800:CONFIRM;11200:BACK;12800:QUIT')


class ReaderStatusBarTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cross-reader-bar-')
        self.sd = Path(self.tmp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'books').mkdir()
        shutil.copy(EPUB, self.sd / 'books/sach.epub')
        write_sparse_epub(self.sd / 'books/sparse.epub')
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/books/sach.epub', 'title': 'Sach'}]}))
        self.shots = Path(self.tmp.name) / 'shots'
        self.shots.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def chay(self, mode, ten_anh, script=None, them=None, fixture='sach.epub', shot_time=3400):
        st = {'language': 'VI', 'fontSize': 14, 'readerStatusBarMode': mode}
        if them:
            st.update(them)
        (self.store / 'settings.json').write_text(json.dumps(st))
        title = 'Sparse status fixture' if fixture == 'sparse.epub' else 'Sach'
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': f'/books/{fixture}', 'title': title}]}))
        # Each process must open the selected recent fixture. The simulator writes
        # state.json on exit and otherwise can reopen the previous book instead.
        state = self.store / 'state.json'
        if state.exists():
            state.unlink()
        script = script or '1000:CONFIRM;4000:QUIT'
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=f'{shot_time}:{self.shots}/{ten_anh}.bmp')
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=40)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log)
        return log

    def do(self, ten_anh):
        img = np.array(Image.open(self.shots / f'{ten_anh}.bmp').convert('L'))
        muc = img > 128 if (img > 128).sum() < img.size * 0.5 else img < 128
        dai = muc[DAI_DAY:]
        rong = img.shape[1]
        noi_dung = [y for y in range(DAI_DAY) if muc[y].sum() > 0]
        cot = [x for x in range(rong) if dai[:, x].sum() > 0]
        return {
            'trai': int(dai[:, :80].sum()),
            'giua': int(dai[:, 80:rong - 80].sum()),
            'phai': int(dai[:, rong - 80:].sum()),
            'tong': int(dai.sum()),
            # Vi tri cua lan muc ngoai cung la dau vet theo THIET KE cua tung thanh phan:
            # pin bat dau o le 12, dong ho ket thuc cach mep 12, ten chuong bat dau o 26,
            # so trang ket thuc cach mep 26. Nho vay phan biet duoc muc nao co dong ho,
            # muc nao co pin ma khong phu thuoc vao so phut tren dong ho.
            'xa_trai': min(cot) if cot else -1,
            'xa_phai': max(cot) if cot else -1,
            'day_chu': max(noi_dung) if noi_dung else 0,
        }

    def test_sau_muc_thanh_phan_dung_nhu_thiet_ke(self):
        """Moi muc chi hien dung thanh phan co ten trong muc, do tren ba vung cua dai day.

        Vung trai = pin (x < 80); giua = ten chuong va so trang; phai = dong ho.
        So do duoi day do tren chinh fixture nay (sach mot chuong ten rong, 14 pt) nen
        la vet nhan on dinh cua tung muc, dung lam bang chung cho thiet ke:
          0 Tat: khong ve gi.
          1 Dong ho & pin: pin trai, dong ho phai, KHONG co ten chuong/so trang (giua = 0).
          2 Mac dinh du: du pin, so trang, ten chuong, dong ho (giua lon nhat).
          3 Ten chuong & tien trinh: ten chuong + so trang, khong pin, khong dong ho.
          4 Ten chuong & dong ho: ten chuong + dong ho, khong pin, khong so trang.
          5 Ten chuong & pin: ten chuong + pin, khong dong ho, khong so trang.
        """
        do_duoc = {}
        for mode in range(6):
            self.chay(mode, f'thanh-{mode}', fixture='sparse.epub')
            do_duoc[mode] = self.do(f'thanh-{mode}')
        d = do_duoc
        # 0 Tat: khong mot net nao o dai day.
        self.assertEqual(d[0]['tong'], 0, d[0])
        # 1 Dong ho & pin: pin sat le trai (12), dong ho sat le phai (515), khong co ten chuong.
        self.assertLessEqual(d[1]['xa_trai'], 14, d[1])
        self.assertGreaterEqual(d[1]['xa_phai'], 510, d[1])
        self.assertEqual(d[1]['giua'], 0, d[1])
        # 2 Mac dinh du: pin, dong ho, so trang va ten chuong - nhieu muc nhat.
        self.assertLessEqual(d[2]['xa_trai'], 14, d[2])
        self.assertGreaterEqual(d[2]['xa_phai'], 510, d[2])
        self.assertGreater(d[2]['tong'], 1200, d[2])
        # 3 Ten chuong & tien trinh chuong: KHONG pin (bat dau o 26), KHONG dong ho (ket thuc
        # o 501), nhung co so trang nen van nhieu muc hon muc 1.
        self.assertGreaterEqual(d[3]['xa_trai'], 20, d[3])
        self.assertLessEqual(d[3]['xa_phai'], 505, d[3])
        self.assertGreater(d[3]['tong'], 400, d[3])
        # 4 Ten chuong & dong ho: KHONG pin nhung CO dong ho.
        self.assertGreaterEqual(d[4]['xa_trai'], 20, d[4])
        self.assertGreaterEqual(d[4]['xa_phai'], 510, d[4])
        # 5 Ten chuong & pin: CO pin nhung KHONG dong ho, va khong co so trang.
        self.assertLessEqual(d[5]['xa_trai'], 14, d[5])
        self.assertLessEqual(d[5]['xa_phai'], 505, d[5])
        self.assertGreater(d[5]['tong'], 400, d[5])

    def test_tat_cho_them_dong_va_giu_dung_doan_dang_doc(self):
        """Muc Tat phai keo day chu xuong, va van o dung muc dau chuong (anchor)."""
        self.chay(MAC_DINH, 'anchor-truoc')
        truoc = self.do('anchor-truoc')
        # Chon muc Tat ngay trong menu doc (popup dang o muc 2, LEFT hai nhip toi muc 0).
        # The menu remains open at the old 3400 ms screenshot point. Capture after
        # the explicit 11200 ms BACK closes it and the reader reflows.
        log = self.chay(MAC_DINH, 'anchor-sau', script=doi_muc_qua_menu(2), shot_time=11800)
        sau = self.do('anchor-sau')
        luu = json.loads((self.store / 'settings.json').read_text())
        self.assertEqual(luu['readerStatusBarMode'], TAT, log)
        # Khi Tat, khong con dai day danh rieng: dong chu cuoi cung tut xuong sat mep
        # duoi panel (muc Mac dinh du dung o ~724 vi con 32 px cho thanh).
        self.assertGreater(sau['day_chu'], truoc['day_chu'] + 20,
                           f'muc Tat phai dung them dien tich: {truoc} vs {sau}')
        self.assertGreaterEqual(sau['day_chu'], 755,
                                f'muc Tat: dong cuoi phai xuong sat mep duoi: {sau}')
        # Cung mot doan dau chuong: doan van mo dau bang "Office affine affinity".
        # Anh sau khi doi muc phai con dong chu dau tien o cung vi tri y.
        for ten_anh, ky_vong in (('anchor-truoc', None), ('anchor-sau', None)):
            img = np.array(Image.open(self.shots / f'{ten_anh}.bmp').convert('L'))
            muc = img > 128 if (img > 128).sum() < img.size * 0.5 else img < 128
            hang = [y for y in range(200, 320) if muc[y].sum() > 0]
            self.assertTrue(hang, f'{ten_anh}: phai co dong chu dau tien trong khoang 200-320')
            if ten_anh == 'anchor-truoc':
                dau = hang[0]
            else:
                self.assertLessEqual(abs(hang[0] - dau), 2,
                                     f'doi muc khong duoc day doan dang doc: {dau} vs {hang[0]}')

    def test_doi_giua_hai_muc_cung_chieu_cao_khong_dan_lai_trang(self):
        """Muc 2 sang muc 3: thanh doi thanh phan, nhung trang sach phai y nguyen."""
        self.chay(MAC_DINH, 'cung-2')
        a = self.do('cung-2')
        self.chay(CHUONG_TIEN_TRINH, 'cung-3')
        b = self.do('cung-3')
        self.assertEqual(b['day_chu'], a['day_chu'], f'cung chieu cao thi khong dan lai: {a} vs {b}')

    def test_muc_reader_doc_lap_voi_thanh_chung(self):
        self.chay(MAC_DINH, 'doc-lap-a', them={'globalStatusBarMode': 0})
        a = self.do('doc-lap-a')
        self.chay(MAC_DINH, 'doc-lap-b', them={'globalStatusBarMode': 1})
        b = self.do('doc-lap-b')
        self.assertEqual(b['day_chu'], a['day_chu'], f'thanh chung khong duoc keo theo reader: {a} vs {b}')
        self.assertGreater(b['trai'] + b['giua'] + b['phai'], 300,
                           f'thanh reader phai con duoc ve: {b}')


if __name__ == '__main__':
    unittest.main()
