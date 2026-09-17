"""I1: giu nut canh doi chuong, bam ngan lat trang ngay, nha nut khong lat them.

Sach mau co muc luc that (toc.ncx) gom bon chuong, moi chuong mot tep XHTML nhieu trang.
Bang chung lay tu dong `[ERS] Progress saved: spine=.. offset=.. page=..` - spine doi la
doi chuong, page doi la lat trang.
"""

import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = REPO / '.pio/build/simulator_x3_uc8279/program'

DOAN = ('Chuong nay du dai de sinh nhieu trang khi lat. ' * 8 + '\n') * 12


def write_epub(path: Path, chuong: int = 4) -> None:
    """EPUB2 co toc.ncx: moi chuong mot tep, du de sinh vai trang."""
    muc = ''.join(
        f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>Chuong {i}</text></navLabel>'
        f'<content src="c{i}.xhtml"/></navPoint>'
        for i in range(1, chuong + 1))
    spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(1, chuong + 1))
    manifest = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>'
                       for i in range(1, chuong + 1))
    with zipfile.ZipFile(path, 'w') as epub:
        epub.writestr('mimetype', 'application/epub+zip')
        epub.writestr('META-INF/container.xml',
                      '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
                      'version="1.0"><rootfiles><rootfile full-path="book.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        epub.writestr('book.opf',
                      '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                      'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:title>Chapter fixture</dc:title><dc:identifier id="id">chapter-hold</dc:identifier>'
                      '<dc:language>vi</dc:language></metadata>'
                      f'<manifest>{manifest}<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
                      f'</manifest><spine toc="ncx">{spine}</spine></package>')
        epub.writestr('toc.ncx',
                      '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
                      '<head/><docTitle><text>Chapter fixture</text></docTitle>'
                      f'<navMap>{muc}</navMap></ncx>')
        for i in range(1, chuong + 1):
            epub.writestr(f'c{i}.xhtml',
                          '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head>'
                          f'<title>Chuong {i}</title></head><body>' +
                          ''.join(f'<p>{DOAN}</p>' for _ in range(6)) + '</body></html>')


class ChapterHoldTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cross-chapter-hold-')
        self.sd = Path(self.tmp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'books').mkdir()
        write_epub(self.sd / 'books/sach.epub')
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/books/sach.epub', 'title': 'Sach'}]}))
        self.settings = {'language': 'VI', 'fontSize': 14}

    def tearDown(self):
        self.tmp.cleanup()

    def chay(self, script, **cai_dat):
        st = dict(self.settings, **cai_dat)
        (self.store / 'settings.json').write_text(json.dumps(st))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=40)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log)
        self.assertIn('Entering activity: EpubReader', log)
        return log

    @staticmethod
    def moc(log):
        """Cac vi tri da luu, theo thu tu: (spine, page)."""
        return [(int(s), int(p)) for s, p in re.findall(r'Progress saved: spine=(\d+) offset=\d+ page=(\d+)', log)]

    def test_bam_ngan_lat_dung_mot_trang_ngay(self):
        # Mac dinh (OFF): nhip bam ngan phai lat NGAY, va chi mot trang.
        log = self.chay('1000:CONFIRM;3000:RIGHT;5000:RIGHT;7000:QUIT')
        viTri = self.moc(log)
        self.assertEqual([p for _, p in viTri], [0, 1, 2], f'nhip bam ngan phai lat dung mot trang: {viTri}')
        self.assertEqual({s for s, _ in viTri}, {0}, f'bam ngan khong duoc doi chuong: {viTri}')

    def test_bam_ngan_khi_bat_giu_van_lat_ngay_truoc_moc_giu(self):
        """Bam ngan trong che do CHAPTER_SKIP phai lat truoc khi cham nguong giu."""
        log = self.chay('1000:CONFIRM;3000:RIGHT:1000;3500:QUIT', longPressButtonBehavior=1)
        viTri = self.moc(log)
        self.assertTrue(viTri, 'phai co vi tri duoc luu')
        self.assertEqual(viTri[-1], (0, 1),
                         f'giu nut chua du 700ms van phai lat mot trang ngay: {viTri}')

    def test_giu_nut_doi_dung_mot_chuong_roi_khong_lat_them(self):
        # Giu nut (CHAPTER_SKIP): doi dung mot chuong, nha nut khong sinh luot lat them.
        log = self.chay('1000:CONFIRM;3000:RIGHT:1000;6000:QUIT', longPressButtonBehavior=1)
        viTri = self.moc(log)
        self.assertTrue(viTri, 'phai co vi tri duoc luu')
        self.assertEqual(viTri[-1][0], 1, f'giu nut phai sang chuong ke tiep: {viTri}')
        self.assertEqual(viTri[-1][1], 0, f'va dung o trang dau chuong moi: {viTri}')
        # Sau khi nha nut, khong duoc co them mot luot lat trang nao (page 1 o cung spine).
        self.assertNotIn((1, 1), viTri, f'nha nut khong duoc lat them: {viTri}')

    def test_giu_o_chuong_cuoi_khong_doi_gi(self):
        # Ba nhip giu di het bon chuong; nhip thu tu o chuong cuoi phai khong doi gi.
        log = self.chay('1000:CONFIRM;2500:RIGHT:1000;4500:RIGHT:1000;6500:RIGHT:1000;8500:RIGHT:1000;10500:QUIT',
                        longPressButtonBehavior=1)
        viTri = self.moc(log)
        self.assertEqual(viTri[-1][0], 3, f'chi co bon chuong: {viTri}')
        # O chuong cuoi, giu khong co muc TOC moi. Nhip bam dau van da lat mot trang ngay.
        self.assertEqual(viTri[-1], (3, 1), f'giu o chuong cuoi chi duoc them mot nhip lat trang: {viTri}')
        self.assertNotIn((3, 2), viTri, f'release khong duoc lat them sau nhip bam: {viTri}')

    def test_sach_khong_muc_luc_van_nhay_duoc(self):
        # Sach mot tep, khong co toc.ncx: giu nut khong duoc roi vao im lang.
        write_epub(self.sd / 'books/mot.epub', chuong=1)
        with zipfile.ZipFile(self.sd / 'books/mot.epub', 'a') as z:
            z.writestr('toc.ncx', '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" '
                                   'version="2005-1"><head/><docTitle><text>Mot</text></docTitle><navMap/>'
                                   '</ncx>')
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/books/mot.epub', 'title': 'Mot'}]}))
        log = self.chay('1000:CONFIRM;3000:RIGHT:1000;6000:QUIT', longPressButtonBehavior=1)
        viTri = self.moc(log)
        self.assertTrue(viTri, 'phai co vi tri duoc luu')
        self.assertEqual(viTri[-1][0], 0, f'sach mot chuong thi spine khong doi: {viTri}')

    def test_giu_trong_luc_dang_ve_khong_nhay_hai_lan(self):
        """Mot nhip lat trang roi giu ngay sau do (dang ban ve): chi duoc doi mot chuong."""
        log = self.chay('1000:CONFIRM;3000:RIGHT;3150:RIGHT:1000;6500:QUIT', longPressButtonBehavior=1)
        viTri = self.moc(log)
        self.assertTrue(viTri, 'phai co vi tri duoc luu')
        self.assertLessEqual(viTri[-1][0], 1, f'khong duoc nhay qua hai chuong: {viTri}')
        self.assertEqual(viTri[-1][0], 1, f'van phai doi dung mot chuong: {viTri}')

    def test_giu_khi_menu_dang_mo_khong_dung_toi_sach(self):
        """Menu doc dang mo thi nut phai thuoc menu, sach khong duoc nhay chuong."""
        log = self.chay('1000:CONFIRM;3000:CONFIRM;5000:RIGHT:1000;8000:BACK;10000:QUIT', longPressButtonBehavior=1)
        self.assertIn('Entering activity: EpubReaderMenu', log)
        viTri = self.moc(log)
        self.assertTrue(viTri, 'phai co vi tri duoc luu')
        self.assertEqual(viTri[-1][0], 0, f'sach khong duoc doi chuong khi menu mo: {viTri}')


    def test_giu_nut_sang_dung_muc_ke_tiep_khi_nhieu_anchor_mot_spine(self):
        """Mot tep XHTML chua ba muc TOC: giu nut phai sang muc KE TIEP (#a2), khong dung yen."""
        duong = self.sd / 'books/anchor.epub'
        than = ('<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>Anchor</title>'
                '</head><body>' + ''.join(f'<h2 id="a{i}">Chuong {i}</h2>' + ''.join(f'<p>{DOAN}</p>' for _ in range(4))
                                          for i in (1, 2, 3)) + '</body></html>')
        nav = ''.join(f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>Chuong {i}</text></navLabel>'
                      f'<content src="body.xhtml#a{i}"/></navPoint>' for i in (1, 2, 3))
        with zipfile.ZipFile(duong, 'w') as epub:
            epub.writestr('mimetype', 'application/epub+zip')
            epub.writestr('META-INF/container.xml',
                          '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
                          'version="1.0"><rootfiles><rootfile full-path="book.opf" '
                          'media-type="application/oebps-package+xml"/></rootfiles></container>')
            epub.writestr('book.opf',
                          '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                          'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                          '<dc:title>Anchor fixture</dc:title><dc:identifier id="id">anchor</dc:identifier>'
                          '<dc:language>vi</dc:language></metadata>'
                          '<manifest><item id="c" href="body.xhtml" media-type="application/xhtml+xml"/>'
                          '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>'
                          '<spine toc="ncx"><itemref idref="c"/></spine></package>')
            epub.writestr('body.xhtml', than)
            epub.writestr('toc.ncx',
                          '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
                          '<head/><docTitle><text>Anchor fixture</text></docTitle>'
                          f'<navMap>{nav}</navMap></ncx>')
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/books/anchor.epub', 'title': 'Anchor'}]}))
        log = self.chay('1000:CONFIRM;3000:RIGHT:1000;7000:QUIT', longPressButtonBehavior=1)
        viTri = self.moc(log)
        self.assertTrue(viTri, 'phai co vi tri duoc luu')
        self.assertIn("Resolved anchor 'a2'", log, f'phai nhay toi neo ke tiep: {log[-400:]}')
        self.assertEqual(viTri[-1][0], 0, f'van trong cung tep XHTML: {viTri}')
        self.assertGreater(viTri[-1][1], 0, f'phai roi khoi trang dau cua tep (truoc day dung yen): {viTri}')



    def test_giu_lau_naynhieu_chuong_lien_tuc_toi_khi_tha(self):
        """Turbo giữ nút: giữ nút bên cạnh 3 giây phải nắc NHIỀU chuong (spine tăng >= 3),
        và giữ đúng hướng; thả trước đó thì chỉ nắc một lần như cũ."""
        write_epub(self.sd / 'books/sach.epub', chuong=6)
        log = self.chay('1000:CONFIRM;3000:RIGHT:3000;8000:QUIT', longPressButtonBehavior=1)
        viTri = self.moc(log)
        self.assertTrue(viTri, 'phai co vi tri duoc luu')
        spineCaoNhat = max(s for s, _ in viTri)
        self.assertGreaterEqual(spineCaoNhat, 3,
                                f'giu 3 giay phai nac nhieu chuong (spine >= 3), thay vi {spineCaoNhat}: {viTri}')
        # Hướng phải giữ nguyên: không lùi chuong giữa các nắc.
        spines = [s for s, _ in viTri]
        self.assertEqual(spines, sorted(spines), f'cac nac phai di toi thuan huong: {spines}')

    def test_turbo_tha_som_dung_khong_nac_them(self):
        """Giữ ngắn hơn một nhịp turbo sau nắc đầu (thả ~1 giây): chỉ nắc một chuong."""
        log = self.chay('1000:CONFIRM;3000:RIGHT:1300;6000:QUIT', longPressButtonBehavior=1)
        viTri = self.moc(log)
        self.assertTrue(viTri, 'phai co vi tri duoc luu')
        self.assertEqual({s for s, _ in viTri}, {0, 1},
                         f'thua 1.3 giay chi duoc nac mot chuong, khong nac tiep: {viTri}')


def write_epub_neo(path: Path, so_neo: int = 3, so_spine: int = 2, anchor_spine: int = 0) -> None:
    """Tao nhieu muc TOC cung mot spine de kiem tra moc logic khi lat trang."""
    thanNeo = ('<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>Neo</title>'
               '</head><body>' + ''.join(f'<h2 id="a{i}">Chuong neo {i}</h2>' + ''.join(f'<p>{DOAN}</p>' for _ in range(3))
                                          for i in range(1, so_neo + 1)) + '</body></html>')
    thanThuong = ('<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>Tep</title>'
                  '</head><body>' + ''.join(f'<p>{DOAN}</p>' for _ in range(6)) + '</body></html>')
    navItems = []
    playOrder = 1
    for k in range(so_spine):
        if k == anchor_spine:
            navItems.extend(
                f'<navPoint id="n{k}_{i}" playOrder="{playOrder + i - 1}"><navLabel><text>{k + 1}.{i}</text></navLabel>'
                f'<content src="c{k}.xhtml#a{i}"/></navPoint>' for i in range(1, so_neo + 1))
            playOrder += so_neo
        else:
            navItems.append(f'<navPoint id="n{k}" playOrder="{playOrder}"><navLabel><text>Tep {k}</text></navLabel>'
                            f'<content src="c{k}.xhtml"/></navPoint>')
            playOrder += 1
    nav = ''.join(navItems)
    spine = ''.join(f'<itemref idref="c{k}"/>' for k in range(so_spine))
    manifest = ''.join(f'<item id="c{k}" href="c{k}.xhtml" media-type="application/xhtml+xml"/>'
                       for k in range(so_spine))
    with zipfile.ZipFile(path, 'w') as epub:
        epub.writestr('mimetype', 'application/epub+zip')
        epub.writestr('META-INF/container.xml',
                      '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
                      'version="1.0"><rootfiles><rootfile full-path="book.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        epub.writestr('book.opf',
                      '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                      'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:title>Neo fixture</dc:title><dc:identifier id="id">neo</dc:identifier>'
                      '<dc:language>vi</dc:language></metadata>'
                      f'<manifest>{manifest}<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
                      f'</manifest><spine toc="ncx">{spine}</spine></package>')
        epub.writestr('toc.ncx',
                      '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
                      '<head/><docTitle><text>Neo fixture</text></docTitle>'
                      f'<navMap>{nav}</navMap></ncx>')
        for k in range(so_spine):
            epub.writestr(f'c{k}.xhtml', thanNeo if k == anchor_spine else thanThuong)


class ChapterHoldMultiAnchorTest(unittest.TestCase):
    """Nhiều mục TOC trong cùng một tệp XHTML: phải đi đúng TỪNG mục, cả tiến lẫn lùi."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cross-chapter-neo-')
        self.sd = Path(self.tmp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'books').mkdir()
        write_epub_neo(self.sd / 'books/neo.epub')
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/books/neo.epub', 'title': 'Neo'}]}))
        self.settings = {'language': 'VI', 'fontSize': 14}

    def tearDown(self):
        self.tmp.cleanup()

    def chay(self, script, **cai_dat):
        st = dict(self.settings, **cai_dat)
        (self.store / 'settings.json').write_text(json.dumps(st))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log)
        self.assertIn('Entering activity: EpubReader', log)
        return log

    @staticmethod
    def neo(log):
        return re.findall(r"Resolved anchor '(\w+)' to page (\d+)", log)

    @staticmethod
    def moc(log):
        return [(int(s), int(p)) for s, p in re.findall(r'Progress saved: spine=(\d+) offset=\d+ page=(\d+)', log)]

    def test_giu_tiep_di_qua_tung_neo_roi_sang_spine_ke_tiep(self):
        """Giữ tới ba lần: a2, a3, rồi sang tệp kế tiếp — không lặp lại a2."""
        log = self.chay('1000:CONFIRM;2500:RIGHT:1000;4000:RIGHT:1000;5500:RIGHT:1000;7500:QUIT',
                        longPressButtonBehavior=1)
        neo = self.neo(log)
        self.assertEqual([t for t, _ in neo][:2], ['a2', 'a3'], f'phai di a1->a2->a3: {neo}')
        self.assertIn('Progress saved: spine=1', log, f'lan thu ba phai sang tep ke tiep: {log[-300:]}')

    def test_giu_lui_di_nguoc_tung_neo(self):
        """Giữ lùi: từ a3 về a2 rồi a1 — trước đây có thể lùi cả spine."""
        log = self.chay('1000:CONFIRM;2500:RIGHT:1000;4000:RIGHT:1000;5500:LEFT:1000;7000:LEFT:1000;9000:QUIT',
                        longPressButtonBehavior=1)
        neo = [t for t, _ in self.neo(log)]
        self.assertIn('a3', neo, f'phai tới được a3 trước: {neo}')
        self.assertEqual(neo[-2:], ['a2', 'a1'], f'lui phai di a3->a2->a1: {neo}')
        self.assertIn('Progress saved: spine=0 offset=0 page=0', log, 'a1 nam o dau tep, trang 0')

    def test_mo_lai_tu_giua_roi_giu_tiep_khong_lap_neo(self):
        """Đóng giữa a2 rồi mở lại: giữ tới phải ra a3, không lặp a2."""
        self.chay('1000:CONFIRM;2500:RIGHT:1000;4500:QUIT', longPressButtonBehavior=1)
        log = self.chay('1000:CONFIRM;3000:RIGHT:1000;5000:QUIT', longPressButtonBehavior=1)
        neo = [t for t, _ in self.neo(log)]
        self.assertEqual(neo, ['a3'], f'mo lai giua a2 thi giu tiep phai ra a3: {neo}')

    def test_toc_1300_neo_van_di_dung_muc_ke(self):
        """TOC 1.300 mục trong một tệp: giữ tới vẫn phải ra a2 và không treo."""
        write_epub_neo(self.sd / 'books/neo.epub', so_neo=1300, so_spine=1)
        log = self.chay('1500:CONFIRM;6000:RIGHT:1000;9000:QUIT', longPressButtonBehavior=1)
        neo = [t for t, _ in self.neo(log)]
        self.assertEqual(neo, ['a2'], f'TOC lon: giu tiep phai ra a2: {neo}')

    def test_bam_ngan_vuot_neo_roi_giu_van_tinh_tu_moc_truoc_nhip(self):
        """Bam ngan sang trang chua a2 roi giu van phai xem diem goc la a1."""
        warmup = self.chay('1000:CONFIRM;2500:RIGHT:1000;5000:QUIT', longPressButtonBehavior=1)
        match = re.search(r"Resolved anchor 'a2' to page (\d+)", warmup)
        self.assertIsNotNone(match, f'fixture phai giai duoc a2: {warmup[-500:]}')
        anchorPage = int(match.group(1))
        self.assertGreater(anchorPage, 0, f'a2 phai nam sau trang dau: {anchorPage}')

        progressFiles = list(self.store.glob('epub_*/progress.bin'))
        self.assertEqual(len(progressFiles), 1, f'phai co mot progress cache cua EPUB: {progressFiles}')
        # loadBook accepts the legacy 4-byte form: spine (u16), page (u16). Clearing
        # the optional visible offset keeps the setup at the exact page before a2.
        previousPage = anchorPage - 1
        progressFiles[0].write_bytes(bytes((0, 0, previousPage & 0xFF, (previousPage >> 8) & 0xFF)))

        log = self.chay('1000:CONFIRM;3000:RIGHT:1000;5000:QUIT', longPressButtonBehavior=1)
        neo = [t for t, _ in self.neo(log)]
        self.assertEqual(neo, ['a2'], f'giu sau nhip bam vuot a1 phai toi a2, khong nhay qua a3: {neo}')
        savedPages = [p for _, p in self.moc(log)]
        self.assertIn(anchorPage - 1, savedPages, f'phai mo lai o trang ngay truoc a2: {savedPages}')
        self.assertIn(anchorPage, savedPages, f'nhip bam ngan phai lat sang trang a2 truoc khi giu: {savedPages}')

    def test_giu_lui_tu_trang_dau_neo_quay_ve_spine_truoc(self):
        """Giu lui tu trang dau cua spine co nhieu neo phai ve muc luc truoc."""
        write_epub_neo(self.sd / 'books/neo.epub', so_neo=2, so_spine=3, anchor_spine=1)
        log = self.chay('1000:CONFIRM;2500:RIGHT:1000;5000:LEFT:1000;7500:QUIT', longPressButtonBehavior=1)
        neo = [t for t, _ in self.neo(log)]
        viTri = self.moc(log)
        self.assertEqual(neo, ['a1'], f'giu lui tu a1 khong duoc lap lai a1: {neo}')
        self.assertTrue(viTri, 'phai co vi tri duoc luu')
        self.assertEqual(viTri[-1], (0, 0), f'giu lui tu dau spine phai ve spine truoc: {viTri}')


def write_epub_neo_lo(path: Path, thu_tu: list, neo_thieu: bool = False) -> None:
    """Mot tep XHTML co cac neo a1..aN, nhung muc TOC di theo thu tu `thu_tu` (co the khong sap).

    `thu_tu` la danh sach chi so neo, vi du [3, 1, 2] = muc luc khong theo thu tu trang.
    `neo_thieu=True` thi chen mot muc tro toi neo KHONG ton tai o giua danh sach.
    """
    so = 4  # luon co a1..a4 trong tep de moi muc co the tro toi
    than = ('<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>Neo lo</title>'
            '</head><body>' + ''.join(f'<h2 id="a{i}">Chuong {i}</h2>' + ''.join(f'<p>{DOAN}</p>' for _ in range(3))
                                      for i in range(1, so + 1)) + '</body></html>')
    nav = []
    for k, i in enumerate(thu_tu, start=1):
        nav.append(f'<navPoint id="n{k}" playOrder="{k}"><navLabel><text>{i}</text></navLabel>'
                   f'<content src="c0.xhtml#a{i}"/></navPoint>')
    if neo_thieu:
        nav.insert(1, '<navPoint id="nx" playOrder="99"><navLabel><text>Thieu</text></navLabel>'
                      '<content src="c0.xhtml#khong_ton_tai"/></navPoint>')
    with zipfile.ZipFile(path, 'w') as epub:
        epub.writestr('mimetype', 'application/epub+zip')
        epub.writestr('META-INF/container.xml',
                      '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
                      'version="1.0"><rootfiles><rootfile full-path="book.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        epub.writestr('book.opf',
                      '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                      'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:title>Neo lo</dc:title><dc:identifier id="id">neo-lo</dc:identifier>'
                      '<dc:language>vi</dc:language></metadata>'
                      '<manifest><item id="c0" href="c0.xhtml" media-type="application/xhtml+xml"/>'
                      '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>'
                      '<spine toc="ncx"><itemref idref="c0"/></spine></package>')
        epub.writestr('toc.ncx',
                      '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
                      '<head/><docTitle><text>Neo lo</text></docTitle>'
                      f'<navMap>{"".join(nav)}</navMap></ncx>')
        epub.writestr('c0.xhtml', than)


class ChapterHoldMessyTocTest(unittest.TestCase):
    """Muc luc khong sap thu tu / co neo thieu: phai xu ly xac dinh va khong ket o trang dau."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cross-messy-toc-')
        self.sd = Path(self.tmp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'books').mkdir()
        self.settings = {'language': 'VI', 'fontSize': 14}

    def tearDown(self):
        self.tmp.cleanup()

    def chay(self, script, **cai_dat):
        st = dict(self.settings, **cai_dat)
        (self.store / 'settings.json').write_text(json.dumps(st))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log)
        self.assertIn('Entering activity: EpubReader', log)
        return log

    def dat_sach(self, thu_tu, neo_thieu=False):
        write_epub_neo_lo(self.sd / 'books/lo.epub', thu_tu=thu_tu, neo_thieu=neo_thieu)
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/books/lo.epub', 'title': 'Lo'}]}))

    def test_toc_khong_sap_thu_tu_van_di_theo_dung_muc_luc(self):
        """Muc luc [3,1,2] tren cung mot tep: tu dau tep (dang o muc a1), giu TOWI phai sang muc
        KE TIEP TRONG MUC LUC, tuc a2 — khong phai a3 nhu the di theo thu tu trong tep."""
        self.dat_sach([3, 1, 2])
        log = self.chay('1200:CONFIRM;3000:RIGHT:1200;5000:QUIT', longPressButtonBehavior=1)
        neo = re.findall(r"Resolved anchor '(\w+)' to page (\d+)", log)
        self.assertEqual([t for t, _ in neo], ['a2'],
                         f'theo thu tu muc luc thi muc ke tiep la a2, khong phai a3: {neo}')

    def test_neo_thieu_o_giua_khong_lam_ket_nut(self):
        """Mot muc tro toi neo khong ton tai o GIUA: giu nhieu lan van phai co buoc di that."""
        self.dat_sach([3, 1, 2], neo_thieu=True)
        log = self.chay('1200:CONFIRM;3000:RIGHT:1200;5000:RIGHT:1200;7000:RIGHT:1200;9000:QUIT',
                        longPressButtonBehavior=1)
        prog = re.findall(r"Progress saved: spine=(\d+) offset=(\d+) page=(\d+)", log)
        self.assertTrue(prog, 'phai co vi tri duoc luu')
        self.assertGreater(len({p for _, _, p in prog}), 1, f'khong duoc ket o mot trang: {prog[-4:]}')


if __name__ == '__main__':
    unittest.main()
