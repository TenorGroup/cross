"""Sach 5.000 chuong: mo lan dau (lap chi muc), lat trang, giu nut sang chuong ke.

Hai kieu sach: moi chuong mot tep XHTML, va ca 5.000 chuong trong mot tep voi muc
luc tro toi `mot.xhtml#cNNNNN`. Tren X3 ca hai tung lam may khoi dong lai khi mo.
Bang chung lay tu dong `[ERS] Progress saved: spine=.. offset=.. page=..`.
"""

import json
import os
import re
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
SO_CHUONG = 5000
DOAN = 'Mua nang gio chieu sang toi mat tay long viec chu sach trang pho cho. ' * 30


def write_epub(path: Path, mot_tep: bool, so_chuong: int = SO_CHUONG, chuong_dau: int = 1,
               neo_giua: bool = False) -> None:
    """EPUB2 co toc.ncx; chu tu bia, du dai de moi chuong sinh vai trang.

    neo_giua: tep chuong dau chua them mot chuong bat dau o <span id="giua">, co muc luc rieng."""
    ten = [f'c{i:05d}' for i in range(1, so_chuong + 1)]
    dich = [f'mot.xhtml#{t}' for t in ten] if mot_tep else [f'{t}.xhtml' for t in ten]
    if neo_giua:
        dich.insert(1, 'c00001.xhtml#giua')
    muc = ''.join(f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>Chuong {i}</text></navLabel>'
                  f'<content src="{d}"/></navPoint>' for i, d in enumerate(dich, 1))
    if mot_tep:
        manifest = '<item id="mot" href="mot.xhtml" media-type="application/xhtml+xml"/>'
        spine = '<itemref idref="mot"/>'
    else:
        manifest = ''.join(f'<item id="{t}" href="{t}.xhtml" media-type="application/xhtml+xml"/>' for t in ten)
        spine = ''.join(f'<itemref idref="{t}"/>' for t in ten)
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as epub:
        epub.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        epub.writestr('META-INF/container.xml',
                      '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
                      'version="1.0"><rootfiles><rootfile full-path="book.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        mo = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>'
        if mot_tep:
            epub.writestr('mot.xhtml', mo + ''.join(f'<h2 id="{t}">Chuong {i}</h2><p>{DOAN}</p>'
                                                    for i, t in enumerate(ten, 1)) + '</body></html>')
        else:
            for i, t in enumerate(ten, 1):
                doan = DOAN * (chuong_dau if i == 1 else 1)
                if neo_giua and i == 1:
                    doan += f'</p><p><span id="giua">Chuong giua</span> {DOAN * 2}'
                epub.writestr(f'{t}.xhtml', f'{mo}<h2>Chuong {i}</h2><p>{doan}</p></body></html>')
        epub.writestr('toc.ncx',
                      '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
                      f'<head/><docTitle><text>Sach thu</text></docTitle><navMap>{muc}</navMap></ncx>')
        epub.writestr('book.opf',
                      '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                      'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:title>Sach thu</dc:title><dc:identifier id="id">huge</dc:identifier>'
                      '<dc:language>vi</dc:language></metadata>'
                      f'<manifest>{manifest}<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
                      f'</manifest><spine toc="ncx">{spine}</spine></package>')


class HugeBookTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cross-huge-book-')
        self.sd = Path(self.tmp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'books').mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def chay(self, ten: str, mot_tep: bool, script: str, so_chuong: int = SO_CHUONG, chuong_dau: int = 1,
             neo_giua: bool = False, **them) -> str:
        write_epub(self.sd / f'books/{ten}', mot_tep, so_chuong, chuong_dau, neo_giua)
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': f'/books/{ten}', 'title': 'Sach'}]}))
        (self.store / 'settings.json').write_text(json.dumps({'language': 'VI', 'fontSize': 14,
                                                               'longPressButtonBehavior': 1}))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script, **them)
        anh = os.environ.get('HUGE_BOOK_ART')
        if anh:
            goc = Path(anh) / Path(ten).stem
            env['CROSSPOINT_SIM_SCREENSHOTS'] = f'7500:{goc}-trang-dau.bmp;13500:{goc}-chuong-hai.bmp'
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-2000:])
        self.assertIn('Entering activity: EpubReader', log)
        self.assertNotIn('Failed to load EPUB', log)
        return log

    @staticmethod
    def moc(log):
        return [(int(s), int(p)) for s, p in re.findall(r'Progress saved: spine=(\d+) offset=\d+ page=(\d+)', log)]

    def test_nam_nghin_tep_mo_lat_va_sang_chuong(self):
        # Heap nhu luc mo sach tren X3: book.bin phai dung qua hai khoi.
        log = self.chay('tach.epub', False, '1000:CONFIRM;8000:RIGHT;10000:RIGHT:1000;14000:QUIT',
                        CROSSPOINT_SIM_FREE_HEAP='100000', CROSSPOINT_SIM_MAX_ALLOC_HEAP='90000')
        viTri = self.moc(log)
        self.assertIn((0, 1), viTri, f'lat trang trong chuong dau: {viTri}')
        self.assertEqual(viTri[-1], (1, 0), f'giu nut sang dau chuong hai: {viTri}')

    def test_nam_nghin_neo_mot_tep_mo_lat_va_sang_chuong(self):
        log = self.chay('mot.epub', True, '1000:CONFIRM;8000:RIGHT;10000:RIGHT:1000;14000:QUIT')
        viTri = self.moc(log)
        self.assertTrue(viTri, 'phai co vi tri duoc luu')
        self.assertEqual({s for s, _ in viTri}, {0}, f'mot tep XHTML: {viTri}')
        self.assertIn("Resolved anchor 'c00002'", log, f'giu nut phai toi neo chuong hai: {log[-600:]}')
        self.assertGreater(viTri[-1][1], 0, f'phai roi trang dau: {viTri}')

    # Chi muc nen (book.part): trang dau truoc, muc luc va co chuong sau, tung buoc khi may ranh.
    @staticmethod
    def luc(log, mau):
        """Moc [ms] cua moi dong khop `mau`."""
        return [int(m) for m in re.findall(r'^\[(\d+)\][^\n]*' + mau, log, re.M)]

    def test_nam_nghin_tep_doc_duoc_truoc_khi_chi_muc_xong(self):
        log = self.chay('tach.epub', False, '1000:CONFIRM;8000:RIGHT;11000:QUIT',
                        CROSSPOINT_SIM_FREE_HEAP='100000', CROSSPOINT_SIM_MAX_ALLOC_HEAP='90000')
        trangDau = self.luc(log, r'Progress saved: spine=0 offset=\d+ page=0')
        mucLuc = self.luc(log, r'INDEX_BG step=toc ms=\d+ ok=1')
        xong = self.luc(log, r'Book index complete')
        self.assertTrue(trangDau and mucLuc and xong, log[-1500:])
        self.assertLess(trangDau[0], mucLuc[0], 'trang dau phai len truoc khi dung muc luc')
        self.assertLess(xong[0], 8000)
        self.assertIn((0, 1), self.moc(log))

    def test_chuong_dau_dai_van_lap_chi_muc(self):
        # X3 r19: chuong dau dai hon cac trang dan truoc, bo dan giu parser ca chuong, khong buoc
        # nao chay. Heap nhu X3 luc ranh trong sach khi radio tat.
        log = self.chay('tach.epub', False, '1000:CONFIRM;7000:RIGHT;7800:RIGHT;10000:QUIT', chuong_dau=60,
                        CROSSPOINT_SIM_FREE_HEAP='70772', CROSSPOINT_SIM_MAX_ALLOC_HEAP='59380')
        self.assertTrue(self.luc(log, r'Book index complete'), log[-1500:])
        # Bo dan da cat lai van dan tiep khi lat.
        self.assertEqual(self.moc(log), [(0, 0), (0, 1), (0, 2)])

    def test_neo_muc_luc_trong_chuong_dan_luc_chi_muc_do(self):
        # Soat v1016 VANG-2: chuong dan luc chi muc con do khong co neo muc luc; chi muc xong ma
        # bo dem chuong van nam lai thi nhay muc luc toi neo giua tep khong toi dau ca.
        log = self.chay('tach.epub', False, '1000:CONFIRM;7000:RIGHT:1000;10000:QUIT', neo_giua=True,
                        CROSSPOINT_SIM_FREE_HEAP='100000', CROSSPOINT_SIM_MAX_ALLOC_HEAP='90000')
        xong = self.luc(log, r'Book index complete')
        self.assertTrue(xong and xong[0] < 7000, log[-1500:])
        self.assertIn("Resolved anchor 'giua'", log, log[-1500:])
        self.assertNotIn("Anchor 'giua' not in the chapter's map", log)

    def test_lat_trang_trong_luc_chi_muc_nen_cho(self):
        # Moi cu bam doi lai buoc nen: buoc chi chay khi trang da len va may yen INDEX_QUIET_MS.
        bam = ';'.join(f'{t}:RIGHT' for t in range(2200, 6200, 800))
        log = self.chay('tach.epub', False, f'1000:CONFIRM;{bam};12000:QUIT',
                        CROSSPOINT_SIM_FREE_HEAP='100000', CROSSPOINT_SIM_MAX_ALLOC_HEAP='90000')
        buoc = self.luc(log, r'INDEX_BG step=')
        self.assertTrue(buoc, log[-1500:])
        self.assertGreater(buoc[0], 5400 + 1500, f'buoc nen chen giua cac cu bam: {buoc}')
        # Chuong dau co bon trang: nam cu bam toi trang hai cua chuong hai.
        self.assertEqual(self.moc(log)[-1], (1, 1), f'nam cu bam, nam trang: {self.moc(log)}')
        self.assertTrue(self.luc(log, r'Book index complete'))

    def test_back_giua_chung_roi_mo_lai_lam_tiep(self):
        log = self.chay('tach.epub', False, '1000:CONFIRM;2000:BACK;4000:CONFIRM;9000:QUIT',
                        CROSSPOINT_SIM_FREE_HEAP='100000', CROSSPOINT_SIM_MAX_ALLOC_HEAP='90000')
        self.assertEqual(len(self.luc(log, r'Chapter list ready')), 1, 'mo lai dung book.part, khong doc lai OPF')
        buoc = self.luc(log, r'INDEX_BG step=')
        self.assertTrue(buoc and buoc[0] > 4000, f'chua buoc nao truoc Back: {buoc}')
        self.assertTrue(self.luc(log, r'Book index complete'), log[-1500:])
        boDem = list(self.store.glob('epub_*'))
        self.assertEqual(len(boDem), 1)
        self.assertTrue((boDem[0] / 'book.bin').exists())
        self.assertFalse((boDem[0] / 'book.part').exists())

    def test_ghi_the_hong_khi_dung_muc_luc_van_doc_duoc(self):
        # X3 r08: ghi toc tmp hong lam may tu choi ca cuon sach 5.000 chuong.
        log = self.chay('tach.epub', False, '1000:CONFIRM;16000:RIGHT;18000:QUIT',
                        CROSSPOINT_SIM_FREE_HEAP='100000', CROSSPOINT_SIM_MAX_ALLOC_HEAP='90000',
                        CROSSPOINT_SIM_SHORT_WRITE_FILE='toc.bin.tmp')
        self.assertIn((0, 1), self.moc(log), 'van lat trang duoc')
        self.assertTrue(self.luc(log, r'INDEX_BG step=toc ms=\d+ ok=0'), log[-1500:])
        self.assertFalse(self.luc(log, r'Book index complete'))

    def test_sach_thuong_giu_duong_cu(self):
        log = self.chay('nho.epub', False, '1000:CONFIRM;4000:RIGHT;6000:QUIT', so_chuong=30)
        self.assertFalse(self.luc(log, r'Chapter list ready'))
        self.assertFalse(self.luc(log, r'INDEX_BG'))
        self.assertIn((0, 1), self.moc(log))


if __name__ == '__main__':
    unittest.main()
