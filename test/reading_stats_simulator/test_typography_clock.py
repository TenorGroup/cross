"""Check paragraph layout and clock refresh using the actual X3 simulator."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
import zipfile

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[2]
PROGRAM = REPO / '.pio/build/simulator_x3_uc8279/program'


def write_epub(path):
    # Cac doan KHONG duoc tu ep text-indent:0 trong CSS noi tuyen: lam vay thi muc "Thut dau
    # dong" trong Cai dat van ban khong the doi bo cuc, va bai kiem tuong nhu san pham hong.
    # De the <p> tran cho cai dat quyet dinh thut dau dong.
    #
    # Doan phai NGAN (2 dong) va nhieu doan: do trang dau chi chua ~19 dong, neu mot doan dai
    # ~20 dong thi trang 1 chi toan dong noi cua doan 1 (va bi chu lon dau chuong che mat dong
    # dau), nen doi muc Thut dau dong khong the doi anh trang 1 du tinh nang chay dung. Do la
    # ly do bai kiem do oan: assertion dung, fixture qua tho.
    paragraph = 'MMMM Reading keeps the first line distinct. '
    with zipfile.ZipFile(path, 'w') as epub:
        epub.writestr('mimetype', 'application/epub+zip')
        epub.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        epub.writestr('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Paragraph fixture</dc:title><dc:identifier id="id">indent-test</dc:identifier><dc:language>en</dc:language></metadata><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
        epub.writestr('body.xhtml', '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>Paragraph fixture</title></head><body>' + ''.join('<p>' + paragraph + '</p>' for _ in range(24)) + '</body></html>')


class TypographyClockTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='cross-type-clock-')
        self.addCleanup(self.temp.cleanup)
        self.sd = Path(self.temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        # paragraphIndentVersion=1 la bat buoc: file khong co khoa nay se bi duong di cu
        # (fromJson dong 281) quy doi paragraphIndent theo kieu cu, lam muc Thut dau dong
        # khong bao gio nhan gia tri nguoi dung dat.
        #
        # Nhip 17/09/2026: extraParagraphSpacing ghi muc 0 (Mac dinh) chu KHONG ghi bool true.
        # Day la tep kieu v3 (textSpacingVersion: 3) nen moi khoa gian phai la mot muc 0..4; bool
        # cu de lan vao day khong phai hinh dang tep that. Duong di cu (v1.0.2: bool true ->
        # paragraphGap = lineHeight/2, false -> clamp((lineHeight+4)/5, 2, 10)) da co bai rieng
        # o test_reader_defaults.py (true -> RONG, false -> MAC_DINH) va o do moi la cho kiem no.
        self.settings = {'language': 'VI', 'sleepTimeout': 10, 'textAntiAliasing': 0,
                         'paragraphAlignment': 1, 'extraParagraphSpacing': 0, 'statusBarClock': 0,
                         'paragraphIndentVersion': 1, 'textSpacingVersion': 3}
        write_epub(self.sd / 'audit.epub')
        # Nhip 17/09/2026: mot muc recent.json de the GAN DAY co dung mot hang, nho do mot nhip
        # CONFIRM mo duoc cuon fixture (khong co recent.json thi the rong va CONFIRM khong lam gi).
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/audit.epub', 'title': 'Paragraph fixture'}]}))

    def run_sim(self, events, captures, timeout=25):
        (self.store / 'settings.json').write_text(json.dumps(self.settings))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=events,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in captures))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log)
        self.assertIn('Entering activity: EpubReader', log)
        images = [Image.open(self.sd / (name + '.bmp')).convert('RGB') for _, name in captures]
        artifact_dir = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
        if artifact_dir:
            output = Path(artifact_dir)
            output.mkdir(parents=True, exist_ok=True)
            for (_, name), image in zip(captures, images):
                image.save(output / (name + '.png'))
        return images, log

    def test_indent_changes_layout_with_spacing_and_invalidates_warm_cache(self):
        # Nhip 17/09/2026: thang thut dau dong gio BA muc OFF/DEFAULT/WIDE voi mac dinh 1
        # (CrossPointSettings.h: 'uint8_t paragraphIndent = 1;'), khong con hai muc Tat/Bat, nen
        # che do 2 khong con nghia la "tat thut dau dong". Vong lap lay (0, 1, 0): lan thu ba
        # quay lai DUNG muc Tat va phai dung lai bo cuc khong thut, tuc cache am da bi dung lai.
        # Y dinh bai giu nguyen: bat thut dau dong phai doi bo cuc that, va tat di phai tra ve
        # dung anh cu. setUp da them mot muc recent.json nen the GAN DAY co dung mot hang va MOT
        # nhip CONFIRM mo duoc cuon fixture.
        images = []
        for index, mode in enumerate((0, 1, 0)):
            self.settings['paragraphIndent'] = mode
            captured, log = self.run_sim(
                '1000:CONFIRM;5800:BACK;6800:QUIT', [(5000, f'indent-{index}')])
            images.append(captured[0])
        # Nhip 17/09/2026: so sanh PHAN CHU cua trang, bo dai duoi trang, dung nhu bai dong ho cung
        # file da lam (crop (0,0,528,710)). Do tren ban that: hieu anh giua lan 0 va lan 2 chi con
        # mot vung 10x12 diem anh o goc duoi-phai (y 773-785 tren 792) tuc vung dem trang/duoi
        # trang, toan bo phan chu giong het nhau, nen phep so sanh toan anh khong con do dung y
        # dinh bai (bo cuc chu).
        portrait = [im.rotate(90, expand=True) if im.width > im.height else im for im in images]
        body = [im.crop((0, 0, 528, 710)) for im in portrait]
        # NGHI LOI SAN PHAM (giu do, khong noi ky vong): do tren ban that 17/09/2026, anh indent-0 va
        # indent-1 GIONG HET NHAU o phan chu — khac nhau chi o dai duoi trang (y 773-785 tren 792,
        # tuc bo dem trang). Bai duoi cung file cung thay ban xem truoc KHONG doi bo cuc khi doi muc
        # Thut dau dong. Nghia la muc Thut dau dong (Off/Mac dinh/Rong) khong lam doi bo cuc van ban
        # that lan ban xem truoc. Bang chung: /tmp/typo9.log dong 8 va 16.
        self.assertIsNotNone(ImageChops.difference(body[0], body[1]).getbbox(),
                             'Enabling indentation must change the actual EPUB layout with spacing enabled')
        self.assertIsNone(ImageChops.difference(body[0], body[2]).getbbox(),
                         'Disabling indentation must rebuild the warm cache and restore zero-indent layout')

    def test_preview_shows_first_line_indent_with_and_without_spaces(self):
        # Muc Thut dau dong phai doi duoc PAN XEM TRUOC, khong chi doi trang sach khi mo lai sach.
        # Pan phai ve bang chinh renderer cua ban doc, va doan thu hai - doan khong mang chu lon dau
        # chuong, tuc doan duy nhat lo ra duoc net thut - phai that su nam trong pan.
        #
        # Chay hai kieu mau: tieng Viet (co dau cach de cat tu) va tieng Trung (STR_FONT_PREVIEW_TEXT
        # 18 chu, KHONG co dau cach nao). Neu phan doan chi cat theo dau cach thi mau Trung chi con
        # mot "tu", doan hai rong, va pan khong bao gio doi - dung loi da bi bo sot.
        for language in ('VI', 'ZH_HANS'):
            with self.subTest(language=language):
                self.settings['readerFavorites'] = [2]  # Ghim san Cai dat van ban (hang 1) de vao thang.
                frames = []
                for index, mode in enumerate((0, 2)):
                    self.settings['language'] = language
                    self.settings['paragraphIndent'] = mode
                    captured, log = self.run_sim('1000:CONFIRM;3200:CONFIRM;4400:CONFIRM;6000:QUIT',
                                                 [(5000, f'indent-preview-{language}-{index}')])
                    self.assertIn('Entering activity: TextSettings', log)
                    frames.append(captured[0])
                portrait = [im.rotate(90, expand=True) if im.width > im.height else im for im in frames]
                pane_top = 128
                pane = [im.crop((0, pane_top, 528, 310)) for im in portrait]
                diff = ImageChops.difference(pane[0], pane[1]).getbbox()
                self.assertIsNotNone(diff, f'Đổi mức Thụt đầu dòng phải đổi pan xem trước ({language})')
                # Vung doi phai cham toi dong cua doan thu hai (dong dau doan 1 mang chu lon dau
                # chuong nen khong the hien net thut): neu chi dong dau doi thi pan chua chung minh
                # duoc muc Thut dau dong. Dong dau cao ~45 diem anh, nen moc la pane_top + 50.
                self.assertGreater(diff[3] + pane_top, pane_top + 50,
                                   f'Vùng đổi phải với tới dòng đầu đoạn hai trong pan ({language})')

    def test_indent_menu_updates_preview_and_survives_restart(self):
        self.settings['readerFavorites'] = [2]  # Pin the existing Text Settings action.
        captured, log = self.run_sim(
            # Nhip 17/09/2026: setUp them recent.json nen the GAN DAY co dung mot hang; MOT nhip CONFIRM mo
            # sach, nhip ke tiep mo menu doc (the Yeu thich co ghim [2] = Cai dat van ban, hang 1),
            # nhip thu hai mo MENU DOC o the Yeu thich (ghim [2] = Cai dat van ban, hang 1), nen
            # CONFIRM ke tiep mo THANG the Bo cuc cua Cua Cai dat van ban, con tro o hang 1.
            # Bay dong cua the Bo cuc: gian dong, gian chu, gian tu, gian doan, can le, le man
            # hinh, thut dau dong — nen hai nhip LEFT di vong qua dai the (1 -> 0 -> hang 7) toi
            # THUT DAU DONG. Mac dinh cua no la 1 (Muc dinh) nen mot nhip CONFIRM day sang 2 (Rong).
            '1000:CONFIRM;3200:CONFIRM;4400:CONFIRM;5600:LEFT;6400:LEFT;7200:CONFIRM;'
            '9500:BACK;10500:BACK;11500:QUIT',
            [(6800, 'menu-auto'), (8000, 'menu-on')])
        self.assertIn('Entering activity: TextSettings', log)
        self.assertIn('Exiting activity: TextSettings', log)
        self.settings = json.loads((self.store / 'settings.json').read_text())
        # Tien de cu (1 = da bat thut) bi thay boi thang ba muc: mac dinh 1 la Muc dinh, mot
        # nhip CONFIRM di tiep toi 2 la Rong.
        self.assertEqual(self.settings['paragraphIndent'], 2, log)
        # Nhip 17/09/2026: gian doan la mot MUC trong thang nam muc (0 Mac dinh .. 4 Sieu rong,
        # CrossPointSettings.h:273 'readerSpacing::LEVEL_DEFAULT'). Fixture ghi san muc 0 va bai
        # nay khong cham vao hang do, nen gia tri phai duoc giu nguyen 0 sau khi luu lai.
        self.assertEqual(self.settings['extraParagraphSpacing'], 0, log)
        portrait = [im.rotate(90, expand=True) if im.width > im.height else im for im in captured]
        self.assertIsNotNone(ImageChops.difference(portrait[0].crop((0, 70, 528, 300)),
                                                  portrait[1].crop((0, 70, 528, 300))).getbbox(),
                             'The text preview must immediately relayout after the choice changes')
        self.run_sim('1000:CONFIRM;3600:BACK;4600:QUIT', [(3100, 'menu-restart')])
        self.assertEqual(json.loads((self.store / 'settings.json').read_text())['paragraphIndent'], 2)

    def test_clock_waits_for_page_refresh_across_a_minute(self):
        self.settings.update(statusBarClock=1, clockFormat=0, clockUtcOffsetQ=76)
        captured, log = self.run_sim(
            '1000:DOWN;1800:CONFIRM;2600:CONFIRM;71500:RIGHT;73500:LEFT;77500:QUIT',
            [(5500, 'before'), (70500, 'idle'), (76500, 'after')], timeout=90)
        self.assertIsNone(ImageChops.difference(captured[0], captured[1]).getbbox(),
                         'A minute change while idle must leave the displayed frame unchanged')
        # Inspect the actual display calls, not only identical pixels.
        refresh_times = [int(ms) for ms in re.findall(r'\[(\d+)\].*display(?:Buffer|GrayscaleBase), mode=', log)]
        self.assertTrue(refresh_times, log)
        self.assertFalse([ms for ms in refresh_times if 5500 < ms < 70500], log)
        self.assertIsNotNone(ImageChops.difference(captured[1], captured[2]).getbbox(),
                             'Returning to the same page must display the new clock minute')
        # The panel buffer is landscape; portrait content is stored rotated.
        portrait = [im.rotate(90, expand=True) if im.width > im.height else im for im in captured]
        self.assertIsNone(ImageChops.difference(portrait[0].crop((0, 0, 528, 710)),
                                               portrait[2].crop((0, 0, 528, 710))).getbbox(),
                         'The page text must remain the same when only the clock changes')


if __name__ == '__main__':
    unittest.main()
