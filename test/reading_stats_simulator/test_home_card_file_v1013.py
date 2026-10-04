"""v1.0.13 Recent card read back from a file instead of drawn again.

Stepping between books on the Recent card rebuilt the whole card every time: the cover read from
its thumbnail and scaled, the title, author and a quote laid out in a compressed serif. Home now
keeps the finished card next to the book's thumbnail after the frame that built it, and a later
step to that book, in this visit or a later one, reads the file back. The card must look the same
pixel for pixel, and a card whose text or cover has changed since must be drawn again.
Screenshots land in CROSSPOINT_TEST_ARTIFACTS when it is set.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from PIL import Image, ImageChops

from test_home_recent_v1011 import book_key, cover, quote_name

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))

CARD_BUILD = re.compile(r'Recent card build=\d+ms')
CARD_FILE = re.compile(r'Recent card file=\d+ms')
# Cover, text block and the other-book row of the default X3 card (528 x 792).
CARD = (0, 124, 528, 756)
COVER = (24, 124, 322, 574)
TEXT = (40, 498, 488, 712)

BOOKS = [
    ('/ven-bien.epub', 'Làng ven biển mùa gió', 'Người Kể Thử',
     'Cuốn sách kể về một làng ven biển qua nhiều mùa gió, từ chiếc thuyền đầu tiên tới buổi tối cả làng ngồi nghe kể chuyện.'),
    ('/ben-song.epub', 'Bến sông ngày gió', 'Người Viết Thử',
     'Nước lên từ sáng, bến vắng người, chỉ còn tiếng gió qua mấy mái chèo.'),
    ('/doi-che.epub', 'Mùa hái chè trên đồi', 'Tác Giả Thử',
     'Sương còn đọng trên lá khi người hái chè lên tới đỉnh đồi.'),
]
QUOTE = 'Chiều nào bà cũng ra đầu ngõ ngồi đợi thuyền về. Gió mặn, trời thấp, nước lên chậm.'


class HomeCardFileTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-card-file-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.store / 'settings.json').write_text(json.dumps(
            {'language': 'VI', 'uiTheme': 4, 'sleepTimeout': 120}))
        artifacts = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
        self.artifacts = Path(artifacts) if artifacts else None
        self.books = []
        # The first book has a cover and its page excerpt, the second a cover and one saved quote,
        # the third no thumbnail yet (a book the reader has not written one for).
        for index, (path, title, author, excerpt) in enumerate(BOOKS):
            target = self.sd / path.lstrip('/')
            shutil.copy(REPO / 'test/epubs/test_dictionary_synonyms.epub', target)
            covers = f'/.crosspoint/home-covers/{book_key(path):08x}_[HEIGHT].bmp'
            if index < 2:
                cover(self.thumb(covers), title, 450)
            self.books.append({'path': path, 'title': title, 'author': author, 'coverBmpPath': covers,
                               'excerpt': excerpt})
        self.write_recent()
        folder = self.store / 'quotes'
        folder.mkdir()
        path = BOOKS[1][0]
        (folder / quote_name(path, 20260815, 0, 0)).write_text(json.dumps(
            {'schema': 1, 'path': path, 'title': BOOKS[1][1], 'text': QUOTE, 'spine': 1, 'page': 3,
             'day': 20260815, 'vo': 10, 've': 90}, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
        (folder / '.ten-v2').write_text('2')

    def thumb(self, covers):
        return self.sd / covers.lstrip('/').replace('[HEIGHT]', '450')

    def write_recent(self):
        (self.store / 'recent.json').write_text(json.dumps({'books': self.books}, ensure_ascii=False),
                                                encoding='utf-8')

    def launch(self, name, events, shots):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=events,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{self.sd / (name + shot + ".bmp")}' for ms, shot in shots))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        images = {}
        for _, shot in shots:
            with Image.open(self.sd / (name + shot + '.bmp')) as image:
                images[shot] = image.convert('L')
            if self.artifacts:
                self.artifacts.mkdir(parents=True, exist_ok=True)
                images[shot].save(self.artifacts / f'{self._testMethodName}-{name}-{shot}.png')
        return log, images

    @staticmethod
    def same(a, b, box):
        return ImageChops.difference(a.crop(box), b.crop(box)).getbbox() is None

    WALK = '2000:RIGHT;3600:RIGHT;5600:QUIT'
    SHOTS = [(1800, 'a'), (3400, 'b'), (5200, 'c')]

    def test_later_visit_reads_every_card_from_its_file_and_shows_the_same_pixels(self):
        log, first = self.launch('first-', self.WALK, self.SHOTS)
        self.assertEqual(len(CARD_BUILD.findall(log)), 3, log[-4000:])
        self.assertEqual(len(CARD_FILE.findall(log)), 0, log[-4000:])
        log, again = self.launch('again-', self.WALK, self.SHOTS)
        self.assertEqual(len(CARD_BUILD.findall(log)), 0, log[-4000:])
        self.assertEqual(len(CARD_FILE.findall(log)), 3, log[-4000:])
        for shot in ('a', 'b', 'c'):
            self.assertTrue(self.same(first[shot], again[shot], CARD), f'card {shot} differs when read back')

    def test_step_back_in_the_same_visit_reads_the_file(self):
        log, images = self.launch('back-', '2000:RIGHT;3600:LEFT;5600:QUIT', [(1800, 'a'), (5200, 'a2')])
        self.assertEqual(len(CARD_BUILD.findall(log)), 2, log[-4000:])
        self.assertEqual(len(CARD_FILE.findall(log)), 1, log[-4000:])
        self.assertTrue(self.same(images['a'], images['a2'], CARD))

    def test_a_card_whose_text_or_cover_changed_is_drawn_again(self):
        _, first = self.launch('first-', self.WALK, self.SHOTS)
        # The first book was read further (new page excerpt); the third got its thumbnail.
        self.books[0]['excerpt'] = 'Buổi sáng hôm ấy cả làng ra bãi, thuyền lớn thuyền nhỏ nằm san sát.'
        self.write_recent()
        cover(self.thumb(self.books[2]['coverBmpPath']), BOOKS[2][1], 450)
        log, after = self.launch('after-', self.WALK, self.SHOTS)
        self.assertEqual(len(CARD_BUILD.findall(log)), 2, log[-4000:])
        self.assertEqual(len(CARD_FILE.findall(log)), 1, log[-4000:])
        self.assertFalse(self.same(first['a'], after['a'], TEXT), 'new excerpt not drawn')
        self.assertTrue(self.same(first['a'], after['a'], COVER))
        self.assertTrue(self.same(first['b'], after['b'], CARD))
        self.assertFalse(self.same(first['c'], after['c'], COVER), 'new thumbnail not drawn')


if __name__ == '__main__':
    unittest.main()
