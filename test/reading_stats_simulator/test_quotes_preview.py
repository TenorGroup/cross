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
        events = '1000:DOWN;1800:CONFIRM;2600:CONFIRM;5000:CONFIRM;6000:CONFIRM;7000:CONFIRM;8500:CONFIRM;9300:RIGHT;10000:RIGHT;11000:CONFIRM;13000:BACK;14000:QUIT'
        for _ in range(2):
            log = self.finish(*self.launch(events))
            self.assertIn('Entering activity: QuoteSelect', log)
            files = list((self.store / 'quotes').glob('*.json'))
            self.assertEqual(len(files), 1)
            saved = json.loads(files[0].read_text())
            self.assertEqual(saved['text'], 'Up and confirm')
            self.assertEqual(saved['title'], 'Synonym Lookup Test')
            self.assertEqual((saved['path'],saved['spine'],saved['page']), ('/audit.epub',0,0))

    def protected(self):
        return {str(p.relative_to(self.store)):hashlib.sha256(p.read_bytes()).hexdigest()
                for p in self.store.rglob('*') if p.is_file() and
                (p.suffix=='.json' or 'progress' in p.name or 'bookmark' in p.name)}

    def test_preview_preserves_stores_and_returns_to_browser(self):
        (self.store / 'reading-stats.json').write_text('{"ngay":[[20260914,12,33]]}')
        # A warm read creates real progress and recent-book records to protect.
        self.finish(*self.launch('1000:DOWN;1800:CONFIRM;2600:CONFIRM;4200:RIGHT;5500:BACK;6500:QUIT'))
        process,log = self.launch('2000:DOWN;3000:CONFIRM;4000:CONFIRM:1000;6500:RIGHT;8000:LEFT;9500:BACK;11000:QUIT')
        time.sleep(1.3)
        before = self.protected()
        text = self.finish(process,log)
        self.assertIn('Preview: /audit.epub',text)
        self.assertIn('Popped from activity stack, new size = 0',text)
        self.assertEqual(before, self.protected())
        self.finish(*self.launch('1800:QUIT'))
        self.assertEqual(before, self.protected())

    def test_stats_and_quotes_open_and_return_to_same_home(self):
        self.finish(*self.launch('1000:DOWN;1800:CONFIRM;2600:CONFIRM;4500:BACK;5500:QUIT'))
        log = self.finish(*self.launch('1000:DOWN;1800:DOWN;2600:CONFIRM;3400:CONFIRM;4500:BACK;5300:RIGHT;6200:CONFIRM;8000:BACK;9000:QUIT'))
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
                self.finish(*self.launch('1000:DOWN;1800:CONFIRM;2600:CONFIRM;4800:RIGHT;6500:BACK;7500:QUIT'))
                before = self.protected()
                process, log = self.launch('1800:DOWN;2800:CONFIRM;3800:CONFIRM:1000;5800:RIGHT;7000:BACK;8200:QUIT')
                text = self.finish(process, log)
                self.assertIn('Preview: /audit.' + extension, text)
                self.assertEqual(before, self.protected())
                self.assertEqual(source_hash, hashlib.sha256(source.read_bytes()).hexdigest())
                source.unlink()
        # A further boot must still see the same stored progress/history.
        self.finish(*self.launch('1800:QUIT'))
        self.assertEqual(before, self.protected())

if __name__ == '__main__':
    unittest.main()
