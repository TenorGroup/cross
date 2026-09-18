"""Trang doc phai dien dong toi sat thanh trang thai, do bang muc that.

Truoc ban nay khung chu chua 28 px day va xet dong bang O dong: voi NotoSerif 16 gian dong
Sieu rong (o 54 px) trang chi duoc 13 dong vi dong 14 thieu dung 1 px o, du muc dong 14 con
cach thanh trang thai hon 10 px. Founder chup may that (Bookerly 16, Hep) cung thieu 1 px.
Luat moi: dong cuoi vua trang khi muc (ascender + descender) con cach muc thanh trang thai
it nhat READER_TEXT_TO_STATUS_GAP = 2 px.
"""
import json, os, shutil, subprocess, sys, tempfile, unittest, zipfile
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
STATUS_INK_TOP = 22          # tenorchrome::STATUS_INK_TOP
GAP = 2                      # tenorchrome::READER_TEXT_TO_STATUS_GAP

DOAN = ('Ông Gehry là kiến trúc sư đầu tiên thật sự đưa được đường cong tự do vào công trình lớn, '
        'nhưng chính công trình đầu tiên có đường cong của mình, Bảo tàng Thiết kế Vitra ở Weil am Rhein, '
        'Đức, hoàn thành vào năm 1989, lại cho thấy một chỗ gồ lên trông không giống chủ đích. ')


def write_epub(path: Path) -> None:
    container = ('<?xml version="1.0" encoding="utf-8"?><container version="1.0" '
                 'xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles>'
                 '<rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
    opf = ('<?xml version="1.0" encoding="utf-8"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
           'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
           '<dc:title>Ậ Ễ Ố đo mực thanh trạng thái</dc:title><dc:identifier id="id">do-trang</dc:identifier>'
           '<dc:language>vi</dc:language></metadata>'
           '<manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest>'
           '<spine><itemref idref="body"/></spine></package>')
    xhtml = ('<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head>'
             '<title>Do trang</title></head><body><p>' + DOAN * 40 + '</p></body></html>')
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml', container)
        z.writestr('book.opf', opf)
        z.writestr('body.xhtml', xhtml)


def ink_bands(bmp: Path):
    ink = np.array(Image.open(bmp).convert('L')) < 128
    rows = ink.sum(axis=1) > 0
    bands, start = [], None
    for y, on in enumerate(rows):
        if on and start is None:
            start = y
        if not on and start is not None:
            bands.append((start, y - 1))
            start = None
    if start is not None:
        bands.append((start, len(rows) - 1))
    # Dau gach, dau cham giua dong co the tach thanh dai rieng: gop dai cach nhau duoi 6 px.
    merged = []
    for b in bands:
        if merged and b[0] - merged[-1][1] < 6:
            merged[-1] = (merged[-1][0], b[1])
        else:
            merged.append(b)
    return merged, ink.shape[0]


class ReaderPageFillTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cross-page-fill-')
        self.sd = Path(self.tmp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'books').mkdir()
        write_epub(self.sd / 'books/do.epub')
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/books/do.epub', 'title': 'Do trang'}]}))
        self.shots = self.sd / 'shots'
        self.shots.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def chup(self, ten, **them):
        st = {'textSpacingVersion': 3, 'paragraphIndentVersion': 1, 'uiTheme': 4, 'language': 'VI',
              'fontFamily': 0, 'fontSize': 16, 'screenMargin': 5, 'readerStatusBarMode': 2,
              'lineSpacing': 0, 'extraParagraphSpacing': 1, 'wordSpacing': 3, 'letterSpacing': 0,
              'paragraphAlignment': 0, 'hyphenationEnabled': 0, 'textAntiAliasing': 1,
              'statusBarTitle': 1, 'statusBarBattery': 1, 'statusBarChapterPageCount': 1,
              'statusBarBookProgressPercentage': 1}
        st.update(them)
        (self.store / 'settings.json').write_text(json.dumps(st))
        state = self.store / 'state.json'
        if state.exists():
            state.unlink()
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        shot = self.shots / f'{ten}.bmp'
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT='1000:CONFIRM;4000:RIGHT;7000:QUIT',
                   CROSSPOINT_SIM_SCREENSHOTS=f'6000:{shot}')
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        bands, height = ink_bands(shot)
        bar_top = height - STATUS_INK_TOP
        text = [b for b in bands if b[1] < bar_top - GAP]
        bar = [b for b in bands if b[0] >= bar_top - GAP]
        return text, bar, height

    def test_sieu_rong_dien_du_14_dong(self):
        """NotoSerif 16, Sieu rong: 13 dong x 54 + muc 46 = 748 <= 792 - 9 - 24. Ban cu chi 13 dong."""
        text, bar, _ = self.chup('sieu-rong', lineSpacing=4)
        self.assertEqual(len(text), 14, text)

    def test_muc_dong_cuoi_cach_thanh_it_nhat_2px_moi_muc_gian_dong(self):
        for level in range(5):
            text, bar, height = self.chup(f'ls{level}', lineSpacing=level)
            self.assertTrue(bar, f'level {level}: khong thay muc thanh trang thai')
            self.assertGreaterEqual(bar[0][0], height - STATUS_INK_TOP,
                                    f'level {level}: muc thanh trang thai cao hon STATUS_INK_TOP: {bar}')
            self.assertLessEqual(text[-1][1], bar[0][0] - GAP, f'level {level}: dong cuoi cham thanh: {text[-1]} {bar}')
            # Dong ke tiep (cach dong cuoi mot o dong) phai KHONG con vua: khong bo phi ca mot dong.
            pitch = text[-1][0] - text[-2][0]
            self.assertGreater(text[-1][1] + pitch, bar[0][0] - GAP, f'level {level}: con cho cho mot dong nua: {text[-2:]} {bar}')

    def test_an_thanh_trang_thai_thi_chu_xuong_tan_day(self):
        text, bar, height = self.chup('an', lineSpacing=4, readerStatusBarMode=0)
        self.assertFalse(bar, bar)
        self.assertGreater(text[-1][1], height - STATUS_INK_TOP - 30, text[-1])


if __name__ == '__main__':
    unittest.main()
