"""v1.0.13 every new book gets its cover on the Recent card, however its reading ends.

The reader writes a new book's thumbnails from its cover page when the book opens on it, and
otherwise as it closes towards another screen. A book whose first page is text, read in sessions
that each end with the power key, took neither route and kept the placeholder on the card for
good. Home now writes a missing thumbnail itself once the card has sat idle for a few seconds,
under a short notice, and redraws the card with the cover. A step away before that cancels it.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from PIL import Image, ImageChops

from test_home_card_v1011 import CARD_BUILD, PROGRAM, THUMB, epub_with_cover

PATH = '/sach/cuon-moi.epub'
TITLE = 'Cuốn sách mới mở'
COVER = (24, 124, 260, 480)
REPO = Path(__file__).resolve().parents[2]


class HomeThumbBackstopTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-thumb-backstop-')
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

    def launch(self, before, after, shots):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_WAKE_REASON='power',
                   CROSSPOINT_SIM_INPUT_SCRIPT=before, CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE=after,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=180)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        images = {}
        for _, name in shots:
            path = self.sd / (name + '.bmp')
            if not path.exists():
                continue
            with Image.open(path) as shot:
                images[name] = shot.convert('L')
            if self.artifacts:
                self.artifacts.mkdir(parents=True, exist_ok=True)
                images[name].save(self.artifacts / f'{self._testMethodName}-{name}.png')
        return log, images

    def thumbs(self, height):
        return sorted(self.store.glob(f'epub_*/thumb_{height}.bmp'))

    def test_book_left_with_the_power_key_gets_its_cover_on_home(self):
        # Home, open the book (text first page), read, sleep with the power key; wake to Home and
        # leave the card alone.
        log, shots = self.launch('1500:CONFIRM;5000:RIGHT;7000:SLEEP;9000:POWER', '9000:QUIT',
                                 [(2500, 'placeholder')])
        woke = log.rindex('Entering activity: Home')
        self.assertIsNone(THUMB.search(log[:woke]), 'precondition: the reader wrote a thumbnail')
        after = log[woke:]
        written = [int(h) for h, _, ok in THUMB.findall(after) if ok == '1']
        self.assertEqual(written, [356, 226], 'Home did not write the missing thumbnails\n' + after[-4000:])
        self.assertTrue(self.thumbs(356) and self.thumbs(226))
        builds = CARD_BUILD.findall(after)
        self.assertEqual(int(builds[0][2]), 0, 'the card had a cover before Home wrote one')
        self.assertEqual(int(builds[-1][2]), 356, 'the card was not redrawn with its new cover')
        # The notice goes up before the decode.
        self.assertLess(after.index('Card thumbnail write'), THUMB.search(after).start())

    def test_a_step_before_the_idle_pass_leaves_the_card_alone(self):
        log, _ = self.launch('1500:CONFIRM;5000:RIGHT;7000:SLEEP;9000:POWER', '1000:BACK;2500:QUIT', [])
        self.assertIsNone(THUMB.search(log[log.rindex('Entering activity: Home'):]), 'decoded during a short visit')


if __name__ == '__main__':
    unittest.main()
