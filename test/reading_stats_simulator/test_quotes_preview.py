"""Exercise quote selection and preview through the real simulator UI."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import time
import unittest
from PIL import Image, ImageChops

from test_quotes_v1011 import write_quote
from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('CROSSPOINT_SIM_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))

class QuotesPreviewTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-quotes-preview-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        shutil.copy(REPO / 'test/epubs/test_dictionary_synonyms.epub', self.sd / 'audit.epub')
        language = os.environ.get('CROSSPOINT_TEST_LANGUAGE', 'VI')
        (self.store / 'settings.json').write_text(json.dumps(truoc_tenor({'language':language, 'sleepTimeout':10, 'readerFavorites':[18]})))
        # Nhip 17/09/2026: thieu recent.json thi the GAN DAY rong, con tro kep ve dai the va mot nhip
        # CONFIRM khong mo duoc sach. Them dung mot muc de mot nhip CONFIRM mo cuon fixture.
        (self.store / 'recent.json').write_text(json.dumps({'books':[{'path':'/audit.epub','title':'Synonym Lookup Test'}]}))

    def launch(self, events, shots=()):
        env = {k:v for k,v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=events)
        if shots:
            env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots)
        log = tempfile.TemporaryFile(mode='w+')
        self.addCleanup(log.close)
        process = subprocess.Popen([str(PROGRAM)], cwd=REPO, env=env, stdout=log, stderr=log)
        self.addCleanup(lambda: process.kill() if process.poll() is None else None)
        return process, log

    def finish(self, process, log):
        process.wait(timeout=25)
        log.seek(0)
        text = log.read()
        self.assertEqual(process.returncode, 0, text)
        return text

    def test_saved_feedback_dismisses_once_to_same_reader_page(self):
        events = ('1000:CONFIRM;3200:CONFIRM;4400:DOWN;5000:DOWN;5600:DOWN;'
                  '6600:RIGHT;7200:RIGHT;8200:CONFIRM;9200:CONFIRM;'
                  '9900:RIGHT;11000:CONFIRM;12500:CONFIRM;14000:QUIT')
        log = self.finish(*self.launch(events, ((2500, 'before'), (10500, 'selection'),
                                                (11700, 'saved'), (13200, 'dismissed'))))
        self.assertIn('Entering activity: QuoteSelect', log)
        exits = [line for line in log.splitlines() if 'Exiting activity: QuoteSelect' in line]
        self.assertEqual(len(exits), 1, log)
        self.assertGreaterEqual(int(exits[0].split(']')[0][1:]), 12500, log)
        files = list((self.store / 'quotes').glob('*.json'))
        self.assertEqual(len(files), 1)
        saved = json.loads(files[0].read_text())
        self.assertEqual(saved['text'], 'position. Clear')
        self.assertEqual((saved['path'], saved['spine'], saved['page']), ('/audit.epub', 0, 0))
        # Sau khi luu, trang quay ve dung cho cu nhung doan vua luu phai con to dam: phan khac
        # biet so voi truoc khi chon nam gon trong vung doan chon, ngoai vung do khong doi.
        with Image.open(self.sd / 'before.bmp') as before, Image.open(self.sd / 'dismissed.bmp') as dismissed, \
                Image.open(self.sd / 'selection.bmp') as selection:
            content = (0, 0, before.width, before.height - 45)
            picked = ImageChops.difference(before.crop(content).convert('RGB'),
                                           selection.crop(content).convert('RGB')).getbbox()
            kept = ImageChops.difference(before.crop(content).convert('RGB'),
                                         dismissed.crop(content).convert('RGB')).getbbox()
            self.assertIsNotNone(picked, log)
            self.assertIsNotNone(kept, 'saved quote is not highlighted after the feedback closes')
            self.assertTrue(kept[0] >= picked[0] - 2 and kept[1] >= picked[1] - 2 and
                            kept[2] <= picked[2] + 2 and kept[3] <= picked[3] + 2,
                            f'highlight {kept} spills outside the selected text {picked}')
        with Image.open(self.sd / 'selection.bmp') as selection, Image.open(self.sd / 'saved.bmp') as feedback:
            self.assertIsNotNone(ImageChops.difference(selection.convert('RGB'), feedback.convert('RGB')).getbbox())
        if evidence := os.environ.get('CROSSPOINT_QUOTE_EVIDENCE_DIR'):
            target = Path(evidence)
            target.mkdir(parents=True, exist_ok=True)
            language = os.environ.get('CROSSPOINT_TEST_LANGUAGE', 'VI')
            for name in ('before', 'selection', 'saved', 'dismissed'):
                shutil.copy2(self.sd / f'{name}.bmp', target / f'{language.lower()}-{name}.bmp')

    def test_failed_save_stays_in_selection_without_quote(self):
        (self.store / 'quotes').write_text('blocks quote directory creation')
        events = ('1000:CONFIRM;3200:CONFIRM;4400:DOWN;5000:DOWN;5600:DOWN;'
                  '6600:RIGHT;7200:RIGHT;8200:CONFIRM;9200:CONFIRM;'
                  '9900:RIGHT;11000:CONFIRM;13000:QUIT')
        log = self.finish(*self.launch(events, ((10500, 'selection'), (12100, 'failed'))))
        self.assertIn('Entering activity: QuoteSelect', log)
        self.assertNotIn('Exiting activity: QuoteSelect', log)
        self.assertEqual((self.store / 'quotes').read_text(), 'blocks quote directory creation')
        with Image.open(self.sd / 'selection.bmp') as selection, Image.open(self.sd / 'failed.bmp') as failed:
            self.assertIsNotNone(ImageChops.difference(selection.convert('RGB'), failed.convert('RGB')).getbbox(), log)

    def test_selected_text_survives_restart_and_duplicate_save(self):
        # Nhip 17/09/2026: setUp da them mot muc recent.json nen the GAN DAY co dung mot hang va
        # MOT nhip CONFIRM mo cuon fixture; nhip ke tiep mo menu doc (the Yeu thich).
        # The CONG CU co 1 Dong bo, 2 Tra tu, 3 Luu trich dan: ba nhip DOWN doi the Yeu thich ->
        # Vi tri -> Doc -> Cong cu, roi RIGHT hai nhip tu hang 1 xuong hang 3 va CONFIRM mo man
        # chon chu (QuoteSelect); hai nhip RIGHT + CONFIRM sau do chon tu va luu nhu cu.
        events = '1000:CONFIRM;3200:CONFIRM;4400:DOWN;5000:DOWN;5600:DOWN;6600:RIGHT;7200:RIGHT;8200:CONFIRM;9200:CONFIRM;11000:CONFIRM;12500:BACK;13500:QUIT'
        for _ in range(2):
            log = self.finish(*self.launch(events))
            self.assertIn('Entering activity: QuoteSelect', log)
            files = list((self.store / 'quotes').glob('*.json'))
            self.assertEqual(len(files), 1)
            saved = json.loads(files[0].read_text())
            # Nhip 17/09/2026: man chon chu nay luu DUNG TU dang duoc tro toi (mot tu), va con tro vao o tu
            # 'position.' cua doan dau; tien de cu 'Up and confirm' la cua luong chon mot DOAN tu truoc
            # dot rework, khong con dung nua. Y dinh bai giu nguyen: luu mot tu that trong EPUB roi kiem
            # vi tri nguon va viec luu trung chi tao mot ban ghi.
            self.assertEqual(saved['text'], 'position.')
            self.assertEqual(saved['title'], 'Synonym Lookup Test')
            self.assertEqual((saved['path'],saved['spine'],saved['page']), ('/audit.epub',0,0))

    # The number column of the Quotes list (src/components/QuoteListLayout.h): the selected
    # quote's number box is filled from SIDE_INSET, and nothing is drawn between the box and
    # the opening quote that hangs left of TEXT_X.
    SIDE_INSET = 20
    TEXT_X = 80

    def seed_quotes(self):
        quotes = self.store / 'quotes'
        quotes.mkdir()
        (quotes / '.ten-v2').write_text('2')
        rows = [
            ('Sương còn đọng trên lá khi người hái chè lên tới đỉnh đồi, còn mặt trời '
             'thì vừa ló.', 'Mùa hái chè trên đồi', 3, 12, 20260919),
            ('A short one.', 'Bến sông ngày gió', 1, 4, 20260920),
            ('The reader turns one page and keeps its position. Clear lines fit the page. '
             'Office affine affinity. The reader turns one page and keeps its position again, '
             'and the paragraph runs on well past the fourth line.', 'Synonym Lookup Test', 0, 2, 20260921),
        ]
        for text, title, spine, page, day in rows:
            write_quote(quotes, '/audit.epub', title, text, spine, page, day)

    @staticmethod
    def dark_runs(image, x0, x1, y0, y1):
        """Vertical runs of ink inside the column strip [x0, x1)."""
        pixels = image.load()
        runs, start = [], None
        for y in range(y0, y1):
            dark = any(pixels[x, y] < 128 for x in range(x0, x1))
            if dark and start is None:
                start = y
            if not dark and start is not None:
                runs.append((start, y - 1))
                start = None
        if start is not None:
            runs.append((start, y1 - 1))
        return runs

    @staticmethod
    def full_width_bands(image, footer=45):
        """Rows that are ink almost edge to edge, grouped: one group per highlighted line."""
        pixels = image.load()
        width, height = image.size
        rows = [y for y in range(height - footer)
                if sum(1 for x in range(width) if pixels[x, y] < 128) > width * 0.9]
        bands, start, previous = [], rows[0], rows[0]
        for y in rows[1:]:
            if y != previous + 1:
                bands.append((start, previous))
                start = y
            previous = y
        bands.append((start, previous))
        return bands

    def continuous_bands(self, image):
        """Bands with no unpainted column inside them: one mark per line, spaces included."""
        pixels = image.load()
        width = image.size[0]
        found = []
        for top, bottom in self.full_width_bands(image):
            columns = [x for x in range(width) if any(pixels[x, y] < 128 for y in range(top, bottom + 1))]
            left, right = min(columns), max(columns)
            gaps = sum(1 for x in range(left, right + 1)
                       if all(pixels[x, y] >= 128 for y in range(top, bottom + 1)))
            if gaps == 0 and right - left >= 400:
                found.append((top, bottom, right - left + 1))
        return found

    def test_holding_right_extends_the_quote_and_marks_whole_lines(self):
        # One hold instead of one press per word: 2.2 seconds of RIGHT has to cross tens of
        # words, which the old fixed 500 ms repeat could never do.
        events = ('1000:CONFIRM;3200:CONFIRM;4400:DOWN;5000:DOWN;5600:DOWN;'
                  '6600:RIGHT;7200:RIGHT;8200:CONFIRM;9200:CONFIRM;9900:RIGHT:2200;'
                  '13000:CONFIRM;14200:CONFIRM;16000:QUIT')
        log = self.finish(*self.launch(events, ((12300, 'selection'), (15200, 'reader'))))
        self.assertIn('Entering activity: QuoteSelect', log)
        files = list((self.store / 'quotes').glob('*.json'))
        self.assertEqual(len(files), 1, log)
        saved = json.loads(files[0].read_text())
        self.assertGreater(len(saved['text'].split()), 20, saved['text'])
        self.assertIn('vo', saved)
        # Every whole line of the selection is one unbroken mark, the spaces between the
        # words included, and the saved quote is drawn the same way back in the reader.
        with Image.open(self.sd / 'selection.bmp') as selection, Image.open(self.sd / 'reader.bmp') as reader:
            picked = self.continuous_bands(selection.convert('L'))
            kept = self.continuous_bands(reader.convert('L'))
            self.assertGreaterEqual(len(picked), 3, picked)
            self.assertEqual([band[:2] for band in picked], [band[:2] for band in kept])

    def test_quotes_list_marks_the_selected_number_and_opens_the_detail(self):
        self.seed_quotes()
        # Home: three DOWN to the Statistics tab, three RIGHT to its fourth row (Quotes). The
        # list of books opens on its one book; Select opens that book's three quotes, and
        # Right twice walks to the third before Select opens it.
        events = ('1000:DOWN;1800:DOWN;2600:DOWN;3400:RIGHT;4200:RIGHT;5000:RIGHT;6000:CONFIRM;'
                  '7500:CONFIRM;9000:RIGHT;10000:RIGHT;11500:CONFIRM;14000:QUIT')
        log = self.finish(*self.launch(events, ((8700, 'quotes-list'), (11200, 'quotes-third'),
                                                (13500, 'quotes-detail'))))
        self.assertIn('Entering activity: Quotes', log)
        self.assertIn('Entering activity: QuoteDetail', log)

        box = (self.SIDE_INSET + 1, self.SIDE_INSET + 5)
        gutter = (self.TEXT_X - 28, self.TEXT_X - 18)
        marked = []
        for name in ('quotes-list', 'quotes-third'):
            with Image.open(self.sd / f'{name}.bmp') as shot:
                image = shot.convert('L')
                height = image.size[1]
                boxes = self.dark_runs(image, box[0], box[1], 100, height - 60)
                self.assertEqual(len(boxes), 1, f'{name}: exactly one number box is filled, got {boxes}')
                self.assertGreaterEqual(boxes[0][1] - boxes[0][0], 20, f'{name}: {boxes}')
                # Nothing but the box and the hanging quote: the air between them stays empty.
                self.assertEqual(self.dark_runs(image, gutter[0], gutter[1], 100, height - 60), [],
                                 f'{name}: something was drawn in the number gutter')
                marked.append(boxes[0])
        self.assertLess(marked[0][0], marked[1][0], 'Right did not move the selection down')
        if evidence := os.environ.get('CROSSPOINT_QUOTE_EVIDENCE_DIR'):
            target = Path(evidence)
            target.mkdir(parents=True, exist_ok=True)
            for name in ('quotes-list', 'quotes-third', 'quotes-detail'):
                shutil.copy2(self.sd / f'{name}.bmp', target / f'{name}.bmp')

    def protected(self):
        return {str(p.relative_to(self.store)):hashlib.sha256(p.read_bytes()).hexdigest()
                for p in self.store.rglob('*') if p.is_file() and
                (p.suffix=='.json' or 'progress' in p.name or 'bookmark' in p.name)}

    def dua_vao_thu_muc(self, ten):
        # Nhip 18/09/2026 (v1.0.3): the File cua Home liet ke goc the nho ngay tai cho, GIU Chon tren
        # mot hang tep la GHIM (UiListActivity xu ly truoc HomeActivity::handleButtons), nen ban xem
        # truoc chi con mo duoc trong trinh duyet tep: dat sach vao thu muc /sach, Chon hang "sach/"
        # o the File mo trinh duyet, roi GIU DOWN (PageForward = nut canh tren X3, 400 ms).
        (self.sd / 'sach').mkdir(exist_ok=True)
        (self.sd / ten).rename(self.sd / 'sach' / ten)
        (self.store / 'recent.json').write_text(json.dumps({'books':[{'path':'/sach/' + ten,'title':'Synonym Lookup Test'}]}))
        return '1600:DOWN;2400:CONFIRM;4000:DOWN:900;6000:BACK;7500:BACK;9000:QUIT'

    def test_preview_preserves_stores_and_returns_to_browser(self):
        (self.store / 'reading-stats.json').write_text('{"ngay":[[20260914,12,33]]}')
        duong = self.dua_vao_thu_muc('audit.epub')
        # A warm read creates real progress and recent-book records to protect.
        self.finish(*self.launch('1000:CONFIRM;3800:RIGHT;5600:BACK;6600:QUIT'))
        process,log = self.launch(duong)
        time.sleep(1.3)
        before = self.protected()
        text = self.finish(process,log)
        self.assertIn('Preview: /sach/audit.epub',text)
        self.assertIn('Popped from activity stack, new size = 0',text)
        self.assertEqual(before, self.protected())
        self.finish(*self.launch('1800:QUIT'))
        self.assertEqual(before, self.protected())

    def test_stats_and_quotes_open_and_return_to_same_home(self):
        self.finish(*self.launch('1000:CONFIRM;3800:BACK;4800:QUIT'))
        # Nhip 17/09/2026: thu tu the Home mac dinh la {0,1,4,2,3} (MenuCustomization.h:16) nen ba nhip DOWN moi tu
        # GAN DAY qua THU MUC, YEU THICH toi THONG KE (con tro o
        # hang 1 = Thoi quen doc); RIGHT mot nhip xuong hang 2 = Thong ke theo sach (BookStats), lui
        # lai, roi RIGHT hai nhip nua xuong hang 4 = Trich dan (Quotes).
        log = self.finish(*self.launch('1000:DOWN;1800:DOWN;2600:DOWN;3400:RIGHT;4600:CONFIRM;5800:BACK;6800:RIGHT;7600:RIGHT;8600:CONFIRM;10200:BACK;11200:QUIT'))
        self.assertIn('Entering activity: Quotes', log)
        self.assertIn('Entering activity: BookStats', log)
        self.assertEqual(log.count('Popped from activity stack, new size = 0'), 2, log)

    def test_txt_and_xtc_preview_preserve_existing_progress(self):
        (self.sd / 'audit.epub').unlink()
        for extension in ('txt', 'xtc'):
            with self.subTest(extension=extension):
                source = self.sd / ('audit.' + extension)
                if extension == 'txt':
                    source.write_text(('A paragraph for reading preview. ' * 15 + '\n') * 60)
                else:
                    bitmap = b'\xff' * (528 * 792 // 8)
                    page = struct.pack('<IHHBBIQ', 0x00475458, 528, 792, 0, 0, len(bitmap), 0) + bitmap
                    header = struct.pack('<IBBHBBBBIQQQQII', 0x00435458, 1, 0, 3, 0, 0, 0, 0, 1,
                                         0, 56, 104, 0, 0, 0)
                    table = b''.join(struct.pack('<QIHH', 104 + i * len(page), len(page), 528, 792) for i in range(3))
                    source.write_bytes(header + table + page * 3)
                source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
                duong = self.dua_vao_thu_muc('audit.' + extension)
                source = self.sd / 'sach' / ('audit.' + extension)
                self.finish(*self.launch('1000:CONFIRM;3800:RIGHT;5600:BACK;6600:QUIT'))
                before = self.protected()
                # Cung duong nhip 18/09/2026 nhu bai xem truoc EPUB (xem dua_vao_thu_muc).
                process, log = self.launch(duong)
                text = self.finish(process, log)
                self.assertIn('Preview: /sach/audit.' + extension, text)
                self.assertEqual(before, self.protected())
                self.assertEqual(source_hash, hashlib.sha256(source.read_bytes()).hexdigest())
                source.unlink()
        # A further boot must still see the same stored progress/history.
        self.finish(*self.launch('1800:QUIT'))
        self.assertEqual(before, self.protected())

if __name__ == '__main__':
    unittest.main()
