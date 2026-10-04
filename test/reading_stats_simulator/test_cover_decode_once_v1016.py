"""v1.0.16 cover thumbnails of a book with a large cover, and Home's card without the book's index.

1. A book whose first page is text gets its two cover thumbnails (the card's 450 px and the theme's
   226 px) as the reader closes. Each used to decode the cover on its own: 3,4 s and 0,9 s for a
   900 x 1350 cover on the X3, on the Home key. One decode now makes both.
2. A book read to the power key each time never wrote them; Home writes them once the card sits
   idle. It loaded the book's whole index to find the cover (1,3 s for 5.000 chapters on the X3,
   more heap than the card has to spare). The reader now leaves the cover's path beside the book's
   cache after its first frame (cover.ref), and Home reads that instead.
3. A cache from an earlier release has no cover.ref, and a damaged one or one of another version
   must read as missing: Home loads the book as before and still writes the thumbnails.
4. Home wrote a missing thumbnail only with 96 KB free, sized for loading the book; after some
   reading it sat at 86 to 91 KB, so the cover came one visit and not the next. With cover.ref the
   copy out of the book is the peak (57.516 B on the X3): 72 KB will do. The old route keeps 96 KB.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from test_home_card_v1011 import CARD_BUILD, PROGRAM, THUMB, epub_with_cover

PATH = '/sach/cuon-moi.epub'
TITLE = 'Cuốn sách mới mở'
REPO = Path(__file__).resolve().parents[2]
# Every decode of the cover file: the per-height one logs its grid, the shared one its scale.
DECODES = re.compile(r'Scaling source \d+x\d+ \(decode grid|Cover thumbnail decode: \d+ ms, scale=1/\d, ok=1')
REF_READ = re.compile(r'Card cover ref ok=(\d) ms=\d+')
INDEX_LOAD = re.compile(r'Loading ePub: ')


class CoverDecodeOnceTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-cover-once-v1016-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.store / 'settings.json').write_text(json.dumps(
            {'language': 'VI', 'uiTheme': 4, 'sleepTimeout': 120, 'wakeIntoBook': 0}))
        (self.store / 'recent.json').write_text(json.dumps(
            {'books': [{'path': PATH, 'title': TITLE, 'author': 'Tenor', 'coverBmpPath': ''}]},
            ensure_ascii=False), encoding='utf-8')
        # A cover in the manifest, no cover page: the book opens on text.
        epub_with_cover(self.sd / PATH.lstrip('/'))
        artifacts = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
        self.artifacts = Path(artifacts) if artifacts else None

    def launch(self, before, after=None, heap=None):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=before)
        if heap:
            env['CROSSPOINT_SIM_FREE_HEAP'] = str(heap)
        if after:
            env.update(CROSSPOINT_SIM_WAKE_REASON='power', CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE=after)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=180)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        return log

    def thumbs(self, height):
        return sorted(self.store.glob(f'epub_*/thumb3_{height}.bmp'))

    def refs(self):
        return sorted(self.store.glob('epub_*/cover.ref'))

    def keep(self, name):
        if not self.artifacts:
            return
        self.artifacts.mkdir(parents=True, exist_ok=True)
        for height in (450, 226):
            for path in self.thumbs(height):
                shutil.copy(path, self.artifacts / f'{name}-thumb3_{height}.bmp')

    def test_both_thumbnails_come_from_one_decode(self):
        # Home, open the book, turn a page, Back to Home: the reader writes the thumbnails as it closes.
        log = self.launch('1500:CONFIRM;5000:RIGHT;7000:BACK;11000:QUIT')
        self.keep('back')
        written = [int(h) for h, _, ok in THUMB.findall(log) if ok == '1']
        self.assertEqual(written, [450, 226], log[-6000:])
        self.assertTrue(self.thumbs(450) and self.thumbs(226))
        self.assertEqual(len(DECODES.findall(log)), 1, 'the cover was decoded once per thumbnail\n' + log[-6000:])
        builds = CARD_BUILD.findall(log)
        self.assertEqual(int(builds[-1][2]), 450, 'the card was not drawn with its cover')

    def power_key_then_home(self):
        # Open the book (text first page), read, sleep with the power key; wake to Home and leave the
        # card alone past its idle pass.
        return self.launch('1500:CONFIRM;5000:RIGHT;7000:SLEEP;9000:POWER', '9000:QUIT')

    def test_home_writes_the_thumbnails_without_the_books_index(self):
        log = self.power_key_then_home()
        self.keep('power')
        woke = log.rindex('Entering activity: Home')
        reading = log[:woke]
        self.assertIsNone(THUMB.search(reading), 'precondition: the reader wrote a thumbnail')
        self.assertTrue(self.refs(), 'the reader left no cover.ref\n' + reading[-4000:])
        # Written after the first page, not before it.
        saved = reading.find('Cover ref saved ok=1')
        self.assertGreater(saved, reading.find('Rendered page in'), reading[-4000:])
        after = log[woke:]
        self.assertEqual(REF_READ.findall(after), ['1'], after[-4000:])
        self.assertIsNone(INDEX_LOAD.search(after), 'Home loaded the book to write its thumbnails\n' + after[-4000:])
        written = [int(h) for h, _, ok in THUMB.findall(after) if ok == '1']
        self.assertEqual(written, [450, 226], after[-4000:])
        builds = CARD_BUILD.findall(after)
        self.assertEqual(int(builds[-1][2]), 450, 'the card was not redrawn with its new cover')

    def test_the_thumbnails_come_with_home_heap_after_reading(self):
        # Home's heap after some reading on the X3 (r33: 86 to 91 KB).
        self.launch('1500:CONFIRM;5000:RIGHT;7000:SLEEP;9000:POWER', '1000:QUIT')
        self.assertTrue(self.refs(), 'precondition: the reader left cover.ref')
        log = self.launch('9000:QUIT', None, heap=86 * 1024)
        written = [int(h) for h, _, ok in THUMB.findall(log) if ok == '1']
        self.assertEqual(written, [450, 226], 'no thumbnail with 86 KB free\n' + log[-4000:])

    def test_the_old_route_still_waits_for_its_heap(self):
        self.launch('1500:CONFIRM;5000:RIGHT;7000:SLEEP;9000:POWER', '1000:QUIT')
        for ref in self.refs():
            ref.unlink()
        log = self.launch('9000:QUIT', None, heap=86 * 1024)
        self.assertEqual(REF_READ.findall(log), ['0'], log[-4000:])
        self.assertIsNone(THUMB.search(log), 'the book was loaded short of heap\n' + log[-4000:])

    def old_cache_then_home(self, damage):
        # The book read to the power key on one boot; its cover.ref then damaged, replaced by one of
        # another version or removed (a cache of an earlier release); Home on the next boot.
        self.launch('1500:CONFIRM;5000:RIGHT;7000:SLEEP;9000:POWER', '1000:QUIT')
        refs = self.refs()
        self.assertEqual(len(refs), 1, refs)
        self.assertFalse(self.thumbs(450), 'precondition: no thumbnail yet')
        damage(refs[0])
        log = self.launch('9000:QUIT', None)
        written = [int(h) for h, _, ok in THUMB.findall(log) if ok == '1']
        self.assertEqual(written, [450, 226], 'Home wrote no thumbnail from an old cache\n' + log[-4000:])
        self.assertIsNotNone(INDEX_LOAD.search(log), 'the old route did not load the book')
        return log

    def test_an_earlier_cache_takes_the_old_route(self):
        self.old_cache_then_home(lambda path: path.unlink())

    def test_another_version_takes_the_old_route(self):
        def bump(path):
            data = bytearray(path.read_bytes())
            data[4] += 1
            path.write_bytes(bytes(data))
        log = self.old_cache_then_home(bump)
        self.assertEqual(REF_READ.findall(log), ['0'], log[-4000:])

    def test_a_cut_file_takes_the_old_route(self):
        log = self.old_cache_then_home(lambda path: path.write_bytes(path.read_bytes()[:-3]))
        self.assertEqual(REF_READ.findall(log), ['0'], log[-4000:])


if __name__ == '__main__':
    unittest.main()
