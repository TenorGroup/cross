"""v1.0.16 cover thumbnails of a book with a large cover.

1. A book whose first page is text gets its two cover thumbnails (the card's 356 px and the theme's
   226 px) as the reader closes. Each used to decode the cover on its own: 3,4 s and 0,9 s for a
   900 x 1350 cover on the X3, on the Home key. One decode now makes both.
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

    def launch(self, before):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=before)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=180)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        return log

    def thumbs(self, height):
        return sorted(self.store.glob(f'epub_*/thumb2_{height}.bmp'))

    def keep(self, name):
        if not self.artifacts:
            return
        self.artifacts.mkdir(parents=True, exist_ok=True)
        for height in (356, 226):
            for path in self.thumbs(height):
                shutil.copy(path, self.artifacts / f'{name}-thumb2_{height}.bmp')

    def test_both_thumbnails_come_from_one_decode(self):
        # Home, open the book, turn a page, Back to Home: the reader writes the thumbnails as it closes.
        log = self.launch('1500:CONFIRM;5000:RIGHT;7000:BACK;11000:QUIT')
        self.keep('back')
        written = [int(h) for h, _, ok in THUMB.findall(log) if ok == '1']
        self.assertEqual(written, [356, 226], log[-6000:])
        self.assertTrue(self.thumbs(356) and self.thumbs(226))
        self.assertEqual(len(DECODES.findall(log)), 1, 'the cover was decoded once per thumbnail\n' + log[-6000:])
        builds = CARD_BUILD.findall(log)
        self.assertEqual(int(builds[-1][2]), 356, 'the card was not drawn with its cover')


if __name__ == '__main__':
    unittest.main()
