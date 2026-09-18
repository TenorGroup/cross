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

REPO = Path(__file__).resolve().parents[2]
PROGRAM = REPO / '.pio/build/simulator_x3_uc8279/program'

class QuotesPreviewTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-quotes-preview-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        shutil.copy(REPO / 'test/epubs/test_dictionary_synonyms.epub', self.sd / 'audit.epub')
        (self.store / 'settings.json').write_text(json.dumps({'language':'VI', 'sleepTimeout':10, 'readerFavorites':[18]}))
        # Nhip 17/09/2026: thieu recent.json thi the GAN DAY rong, con tro kep ve dai the va mot nhip
        # CONFIRM khong mo duoc sach. Them dung mot muc de mot nhip CONFIRM mo cuon fixture.
        (self.store / 'recent.json').write_text(json.dumps({'books':[{'path':'/audit.epub','title':'Synonym Lookup Test'}]}))

    def launch(self, events):
        env = {k:v for k,v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=events)
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
