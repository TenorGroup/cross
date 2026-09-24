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


def write_epub(path: Path, mot_tep: bool) -> None:
    """EPUB2 co toc.ncx; chu tu bia, du dai de moi chuong sinh vai trang."""
    ten = [f'c{i:05d}' for i in range(1, SO_CHUONG + 1)]
    dich = [f'mot.xhtml#{t}' for t in ten] if mot_tep else [f'{t}.xhtml' for t in ten]
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
                epub.writestr(f'{t}.xhtml', f'{mo}<h2>Chuong {i}</h2><p>{DOAN}</p></body></html>')
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

    def chay(self, ten: str, mot_tep: bool, script: str, **them) -> str:
        write_epub(self.sd / f'books/{ten}', mot_tep)
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


if __name__ == '__main__':
    unittest.main()
