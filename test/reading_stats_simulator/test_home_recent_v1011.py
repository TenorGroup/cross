"""v1.0.11 Recent card: one book at a time, saved quotes on the card.

The Recent tab shows one book: a large cover, the title, the author, then a line from the book.
That line is a saved quote of the book when it has one (the newest when it was kept since Home last
showed the book, otherwise a random one), else the page excerpt the reader left on. A rule and an
"another book" row name the next recent book; the front buttons step between books and Select opens
the one shown. Screenshots land in CROSSPOINT_TEST_ARTIFACTS when it is set.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from PIL import Image, ImageChops, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
FONTS = REPO / 'lib/EpdFont/builtinFonts/source'

TITLE_A = ('Những việc lớn được hoàn thành như thế nào: Những yếu tố bất ngờ quyết định số phận của mọi dự '
           'án, từ sửa nhà đến thám hiểm không gian và mọi thứ ở giữa (tenor version)')
PATH_A = ('/books/tenor/translated-by-tenor/Những việc lớn được hoàn thành như thế nào (tenor version) '
          '[How Big Things Get Done] - Bent Flyvbjerg & Dan Gardner.epub')
EXCERPT_A = ('Trong quyển sách này, chúng tôi đi tìm lời giải cho câu hỏi vì sao phần lớn dự án lớn vượt '
             'ngân sách, trễ hạn, và vì sao một số ít lại về đích.')
# The four quotes on the founder's X3, 23/09/2026: one book, 22/09/2026, no minute, by page.
QUOTES = [
    (16, 'lại trong một cuộc họp hội đồng quản trị. Đã có ai khác làm việc này chưa? Câu trả lời đầy '
         'phấn khích là: “Chưa!”', 7617, 7730),
    (23, 'Tham vọng không chỉ thôi thúc chúng ta trở thành người đầu tiên, mà còn có thể đẩy chúng ta tới '
         'chỗ hoàn thành thứ lớn nhất. Cao nhất. Dài nhất. Nhanh nhất.', 10790, 10946),
    (30, 'Nếu nhìn công nghệ theo cách này, ta sẽ thấy rõ rằng, khi mọi yếu tố khác như nhau, người lập kế '
         'hoạch dự án nên ưu tiên công nghệ dày dạn kinh nghiệm, cũng vì lý do người xây nhà nên ưu tiên '
         'thợ mộc giàu kinh nghiệm.', 14078, 14295),
    (40, 'Các bản vẽ của Taillibert hầu như không đếm xỉa đến những vấn đề thực tế thông thường. “Thiết kế '
         'sân vận động không tính đến khả năng thi công và không chừa chỗ cho giàn giáo bên trong,” các kỹ '
         'sư thẩm định viết, khiến công nhân không còn lựa chọn nào khác ngoài việc tập trung hàng chục '
         'cần cẩu', 19003, 19298),
]
CARD_QUOTE = re.compile(r'Card quote ([0-9a-f]{16}\.json) of (\d+)')
CARD_BUILD = re.compile(r'Recent card build=(\d+)ms')


def book_key(path):
    """quotes::bookKey: FNV-1a 32 over the path's bytes."""
    value = 2166136261
    for byte in path.encode('utf-8'):
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def day_code(day):
    year, month, date = day // 10000, day // 100 % 100, day % 100
    return min((year - 2020) * 372 + (month - 1) * 31 + (date - 1), 0xFFFF)


def quote_name(path, day, minute, slot):
    """quotes::nameOf(baseId + slot), as QuoteStore.cpp names a record."""
    return '%016x.json' % (book_key(path) << 32 | day_code(day) << 16 | minute << 4 | slot)


def cover(path, title, height):
    """A 1-bit thumbnail like Epub::generateThumbBmp writes, at 0.6 x height."""
    width = int(height * 0.6)
    image = Image.new('L', (width, height), 90)
    draw = ImageDraw.Draw(image)
    for y in range(height):
        draw.line((0, y, width, y), fill=60 + y * 150 // height)
    font = ImageFont.truetype(str(FONTS / 'Geist/Geist-Bold.ttf'), max(12, height // 9))
    y = height // 6
    for word in title.split():
        draw.text((width // 2, y), word, font=font, fill=255, anchor='ma')
        y += height // 8
    path.parent.mkdir(parents=True, exist_ok=True)
    image.convert('1').save(path)


class HomeRecentCardTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-home-v1011-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.store / 'settings.json').write_text(json.dumps(
            {'language': 'VI', 'uiTheme': 4, 'sleepTimeout': 120}))
        artifacts = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
        self.artifacts = Path(artifacts) if artifacts else None

    def add_book(self, path, title, author, excerpt='', epub='test_dictionary_synonyms.epub'):
        target = self.sd / path.lstrip('/')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(REPO / 'test/epubs' / epub, target)
        covers = f'/.crosspoint/home-covers/{book_key(path):08x}_[HEIGHT].bmp'
        cover(self.sd / covers.lstrip('/').replace('[HEIGHT]', '356'), title.split(':')[0], 356)
        return {'path': path, 'title': title, 'author': author, 'coverBmpPath': covers, 'excerpt': excerpt}

    def write_recent(self, books):
        (self.store / 'recent.json').write_text(json.dumps({'books': books}, ensure_ascii=False), encoding='utf-8')

    def seed_quotes(self):
        folder = self.store / 'quotes'
        folder.mkdir()
        names = []
        for slot, (page, text, start, end) in enumerate(QUOTES):
            name = quote_name(PATH_A, 20260922, 0, slot)
            (folder / name).write_text(json.dumps(
                {'schema': 1, 'path': PATH_A, 'title': TITLE_A, 'text': text, 'spine': 10, 'page': page,
                 'day': 20260922, 'vo': start, 've': end}, ensure_ascii=False, separators=(',', ':')),
                encoding='utf-8')
            names.append(name)
        (folder / '.ten-v2').write_text('2')
        return names

    def launch(self, events, shots):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=events,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        images = {}
        for _, name in shots:
            with Image.open(self.sd / (name + '.bmp')) as shot:
                images[name] = shot.convert('L')
            if self.artifacts:
                self.artifacts.mkdir(parents=True, exist_ok=True)
                images[name].save(self.artifacts / f'{self._testMethodName}-{name}.png')
        return log, images

    @staticmethod
    def ink(image, box):
        return ImageChops.invert(image.crop(box)).getbbox() is not None

    @staticmethod
    def same(a, b, box):
        return ImageChops.difference(a.crop(box), b.crop(box)).getbbox() is None

    # Regions of the default X3 card (528 x 792): cover, text block, other-book row, left arrow,
    # the footer tip lane the pin hint used, and the four footer hint cells.
    COVER = (146, 124, 382, 480)
    TEXT = (40, 498, 488, 712)
    ROW = (40, 712, 488, 756)
    LEFT_ARROW = (10, 722, 30, 756)
    FOOTER = (0, 756, 528, 792)
    HINTS = [(65, 770, 145, 792), (157, 770, 237, 792), (291, 770, 371, 792), (383, 770, 463, 792)]

    def test_one_book_without_quotes_shows_the_page_excerpt(self):
        self.write_recent([self.add_book(PATH_A, TITLE_A, 'Bent Flyvbjerg, Dan Gardner', EXCERPT_A)])
        log, shots = self.launch('3000:QUIT', [(1800, 'card')])
        card = shots['card']
        self.assertNotRegex(log, CARD_QUOTE)
        self.assertEqual(len(CARD_BUILD.findall(log)), 1, log[-4000:])
        self.assertTrue(self.ink(card, self.COVER), 'no cover drawn')
        self.assertTrue(self.ink(card, self.TEXT), 'no title, author or excerpt drawn')
        # One book: no rule and no other-book row, and no pin hint in the tip lane above the footer.
        self.assertFalse(self.ink(card, (0, 712, 528, 756)), 'row or tip drawn for a single book')
        # Footer: Back reads as text (it opens the book), Select stays, both arrows go (nothing to
        # step to with one book).
        back, select, previous, following = (self.ink(card, cell) for cell in self.HINTS)
        self.assertTrue(back and select, 'Back or Select hint missing')
        self.assertFalse(previous or following, 'direction hints drawn with one book')

    def test_book_with_quotes_shows_one_of_its_quotes(self):
        self.write_recent([self.add_book(PATH_A, TITLE_A, 'Bent Flyvbjerg, Dan Gardner', EXCERPT_A)])
        names = self.seed_quotes()
        log, shots = self.launch('3000:QUIT', [(1800, 'card')])
        found = CARD_QUOTE.findall(log)
        self.assertEqual(len(found), 1, log[-4000:])
        self.assertIn(found[0][0], names)
        self.assertEqual(found[0][1], '4')
        # Only the record shown was opened: no other quote file is read on the way to the card.
        self.assertTrue(self.ink(shots['card'], self.TEXT))

    def test_three_books_step_with_the_front_buttons_and_select_opens_the_shown_one(self):
        books = [self.add_book(PATH_A, TITLE_A, 'Bent Flyvbjerg, Dan Gardner', EXCERPT_A),
                 self.add_book('/ego.epub', 'Ego is the Enemy', 'Ryan Holiday',
                               'The ego is the enemy of what you want and of what you have.',
                               'test_kerning_ligature.epub'),
                 self.add_book('/score.epub', 'The Score Takes Care of Itself', 'Bill Walsh',
                               'The score takes care of itself when you take care of the effort.')]
        self.write_recent(books)
        events = '2000:RIGHT;3600:RIGHT;5200:RIGHT;6800:LEFT;8400:CONFIRM;12000:QUIT'
        shots = [(1800, 'first'), (3400, 'second'), (5000, 'third'), (6600, 'wrapped'), (8200, 'back-left'),
                 (11500, 'opened')]
        log, images = self.launch(events, shots)
        card = (0, 124, 528, 712)
        self.assertFalse(self.same(images['first'], images['second'], card), 'Right did not change the book')
        self.assertFalse(self.same(images['second'], images['third'], card))
        # Right from the oldest wraps to the most recent book, Left from there to the oldest.
        self.assertTrue(self.same(images['first'], images['wrapped'], (0, 124, 528, 756)))
        self.assertTrue(self.same(images['third'], images['back-left'], (0, 124, 528, 756)))
        # The row names the next book; the left arrow shows only when a newer book is to the left.
        for name in ('first', 'second', 'third'):
            self.assertTrue(self.ink(images[name], self.ROW), f'{name}: no other-book row')
        self.assertFalse(self.ink(images['first'], self.LEFT_ARROW), 'left arrow on the most recent book')
        self.assertTrue(self.ink(images['second'], self.LEFT_ARROW), 'no left arrow on an older book')
        # Four hints: Back as text, Select, and left and right arrows.
        self.assertTrue(all(self.ink(images['first'], cell) for cell in self.HINTS))
        self.assertGreaterEqual(len(CARD_BUILD.findall(log)), 5)
        # Select opened the book on the card (the third, oldest one) in the reader.
        self.assertIn('Entering activity: EpubReader', log)
        state = json.loads((self.store / 'state.json').read_text())
        self.assertEqual(state['openEpubPath'], '/score.epub')

    def test_quote_saved_in_the_reader_is_on_the_card_back_home(self):
        self.write_recent([self.add_book(PATH_A, TITLE_A, 'Bent Flyvbjerg, Dan Gardner', EXCERPT_A)])
        names = self.seed_quotes()
        # Home, Select opens the book; reader menu, Tools, Save quotation, pick two words, save,
        # dismiss; Back from the page to Home.
        events = ('1000:CONFIRM;3200:CONFIRM;4400:DOWN;5000:DOWN;5600:DOWN;'
                  '6600:RIGHT;7200:RIGHT;8200:CONFIRM;9200:CONFIRM;'
                  '9900:RIGHT;11000:CONFIRM;12500:CONFIRM;14500:BACK;19000:QUIT')
        log, images = self.launch(events, [(800, 'before'), (18000, 'after-save')])
        saved = sorted(set(p.name for p in (self.store / 'quotes').glob('*.json')) - set(names))
        self.assertEqual(len(saved), 1, log[-6000:])
        found = CARD_QUOTE.findall(log)
        self.assertEqual(len(found), 2, log[-6000:])
        self.assertIn(found[0][0], names)
        self.assertEqual(found[1], (saved[0], '5'))
        self.assertIn('Entering activity: Home', log.split('Exiting activity: QuoteSelect', 1)[1])
        self.assertFalse(self.same(images['before'], images['after-save'], self.TEXT))


if __name__ == '__main__':
    unittest.main()
