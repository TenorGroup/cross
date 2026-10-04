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

TITLE_A = 'Chuyện dài về một làng ven biển: những mùa gió, những con thuyền, những người đi xa và những người ở lại, kể theo lời bà ngoại vào các buổi tối mất điện (bản thử)'
PATH_A = '/sach/thu-vien-mau/ban-thu/Chuyện dài về một làng ven biển (bản thử) [Một tựa sách rất dài] - Người Kể & Người Chép.epub'
EXCERPT_A = ('Cuốn sách kể về một làng ven biển qua nhiều mùa gió, từ chiếc thuyền đầu tiên tới buổi '
             'tối cả làng ngồi nghe bà ngoại kể chuyện.')
# The long-title fixture: four quotes, one book, one day, no minute, by page.
QUOTES = [
    (12, 'Chiều nào bà cũng ra đầu ngõ ngồi đợi thuyền về. Gió mặn, trời thấp, nước lên chậm. Con chó nằm dưới chân bà, mắt nhìn ra biển.', 3120, 3247),
    (19, 'Mẹ bảo vá lưới thì phải ngồi yên một chỗ, mỗi mắt lưới buộc hai lần. Hỏi vì sao, mẹ chỉ đáp: “Cho chắc!”', 5044, 5148),
    (27, 'Mùa gió năm ấy về sớm hơn mọi năm. Cả làng kéo thuyền lên bãi cát từ lúc trời còn tối, người lớn hò nhau từng nhịp, trẻ con xách đèn chạy theo, còn các bà nấu nồi cháo lớn đặt giữa sân đình.', 8210, 8400),
    (35, 'Chú Tư đi biển ba mươi năm, chưa lần nào kể về những đêm bão. Chú chỉ kể chuyện con cá chuồn bay lên đậu trên mạn thuyền, chuyện trăng rằm soi xuống mặt nước phẳng như tấm kính, và chuyện một buổi sáng cả đàn cá heo bơi theo thuyền từ cửa lạch ra tới hòn đảo nhỏ phía xa.', 11375, 11646),
]
CARD_QUOTE = re.compile(r'Card quote ([0-9a-f]{16}\.json) of (\d+)')
CARD_BUILD = re.compile(r'Recent card build=(\d+)ms')
# From v1.0.13 a card shown before is read back from its file instead of built again.
CARD_FILE = re.compile(r'Recent card file=(\d+)ms')


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
        cover(self.sd / covers.lstrip('/').replace('[HEIGHT]', '450'), title.split(':')[0], 450)
        return {'path': path, 'title': title, 'author': author, 'coverBmpPath': covers, 'excerpt': excerpt}

    def write_recent(self, books):
        (self.store / 'recent.json').write_text(json.dumps({'books': books}, ensure_ascii=False), encoding='utf-8')

    def seed_quotes(self):
        folder = self.store / 'quotes'
        folder.mkdir()
        names = []
        for slot, (page, text, start, end) in enumerate(QUOTES):
            name = quote_name(PATH_A, 20260815, 0, slot)
            (folder / name).write_text(json.dumps(
                {'schema': 1, 'path': PATH_A, 'title': TITLE_A, 'text': text, 'spine': 6, 'page': page,
                 'day': 20260815, 'vo': start, 've': end}, ensure_ascii=False, separators=(',', ':')),
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
    COVER = (24, 124, 322, 574)
    TEXT = (24, 590, 504, 712)
    ROW = (40, 712, 488, 756)
    LEFT_ARROW = (10, 722, 30, 756)
    FOOTER = (0, 756, 528, 792)
    HINTS = [(65, 770, 145, 792), (157, 770, 237, 792), (291, 770, 371, 792), (383, 770, 463, 792)]

    def test_one_book_without_quotes_shows_the_page_excerpt(self):
        self.write_recent([self.add_book(PATH_A, TITLE_A, 'Tác giả Mẫu, Người Viết', EXCERPT_A)])
        log, shots = self.launch('3000:QUIT', [(1800, 'card')])
        card = shots['card']
        self.assertNotRegex(log, CARD_QUOTE)
        self.assertEqual(len(CARD_BUILD.findall(log)), 1, log[-4000:])
        self.assertTrue(self.ink(card, self.COVER), 'no cover drawn')
        self.assertTrue(self.ink(card, self.TEXT), 'no title, author or excerpt drawn')
        # One book: no rule and no other-book row, and no pin hint in the tip lane above the footer.
        self.assertFalse(self.ink(card, (0, 712, 528, 756)), 'row or tip drawn for a single book')
        # Footer: Back (it opens the book) and Select stay, both arrows go (nothing to
        # step to with one book).
        back, select, previous, following = (self.ink(card, cell) for cell in self.HINTS)
        self.assertTrue(back and select, 'Back or Select hint missing')
        self.assertFalse(previous or following, 'direction hints drawn with one book')

    def test_book_with_quotes_shows_one_of_its_quotes(self):
        self.write_recent([self.add_book(PATH_A, TITLE_A, 'Tác giả Mẫu, Người Viết', EXCERPT_A)])
        names = self.seed_quotes()
        log, shots = self.launch('3000:QUIT', [(1800, 'card')])
        found = CARD_QUOTE.findall(log)
        self.assertEqual(len(found), 1, log[-4000:])
        self.assertIn(found[0][0], names)
        self.assertEqual(found[0][1], '4')
        # Only the record shown was opened: no other quote file is read on the way to the card.
        self.assertTrue(self.ink(shots['card'], self.TEXT))

    def test_three_books_step_with_the_front_buttons_and_select_opens_the_shown_one(self):
        books = [self.add_book(PATH_A, TITLE_A, 'Tác giả Mẫu, Người Viết', EXCERPT_A),
                 self.add_book('/ben-song.epub', 'Bến sông ngày gió', 'Người Viết Thử',
                               'Nước lên từ sáng, bến vắng người, chỉ còn tiếng gió qua mấy mái chèo.',
                               'test_kerning_ligature.epub'),
                 self.add_book('/doi-che.epub', 'Mùa hái chè trên đồi', 'Tác Giả Thử',
                               'Sương còn đọng trên lá khi người hái chè lên tới đỉnh đồi.')]
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
        # Four hints: Back, Select, and left and right arrows.
        self.assertTrue(all(self.ink(images['first'], cell) for cell in self.HINTS))
        self.assertGreaterEqual(len(CARD_BUILD.findall(log)) + len(CARD_FILE.findall(log)), 5)
        # Select opened the book on the card (the third, oldest one) in the reader.
        self.assertIn('Entering activity: EpubReader', log)
        state = json.loads((self.store / 'state.json').read_text())
        self.assertEqual(state['openEpubPath'], '/doi-che.epub')

    @staticmethod
    def edge_runs(image, top, bottom):
        """Dark column runs nearest each screen edge inside rows top..bottom.

        Returns ((first, last) of the left run, (first, last) of the right run, the column where ink
        resumes after the left run, the column where ink ends before the right run).
        """
        width = image.width
        dark = [any(image.getpixel((x, y)) < 128 for y in range(top, bottom)) for x in range(width)]
        left0 = dark.index(True)
        left1 = left0
        while dark[left1 + 1]:
            left1 += 1
        right1 = width - 1 - dark[::-1].index(True)
        right0 = right1
        while dark[right0 - 1]:
            right0 -= 1
        after = next(x for x in range(left1 + 1, width) if dark[x])
        before = next(x for x in range(right0 - 1, -1, -1) if dark[x])
        return (left0, left1), (right0, right1), after, before

    def test_arrows_on_both_sides_mirror_each_other(self):
        # Two books and the second shown: the other-book row has an arrow on each side, and the side
        # buttons' arrows sit at mid height on both edges.
        self.write_recent([self.add_book(PATH_A, TITLE_A, 'Tác giả Mẫu, Người Viết', EXCERPT_A),
                           self.add_book('/ben-song.epub', 'Bến sông ngày gió', 'Người Viết Thử',
                                         'Nước lên từ sáng, bến vắng người, chỉ còn tiếng gió qua mấy mái chèo.',
                                         'test_kerning_ligature.epub')])
        _, images = self.launch('2000:RIGHT;5000:QUIT', [(3800, 'second')])
        card = images['second']
        last = card.width - 1
        # The rule is the full-width line above the row; the row's arrows are measured under it.
        rule = next(y for y in range(700, 760)
                    if sum(card.getpixel((x, y)) < 128 for x in range(card.width)) > 400)
        (l0, l1), (r0, r1), label, title = self.edge_runs(card, rule + 2, 756)
        self.assertEqual((l0, last - r1), (l0, l0), f'row arrows: left x {l0}..{l1}, right x {r0}..{r1}')
        self.assertEqual(l1 - l0, r1 - r0, 'row arrows differ in width')
        # The title keeps the label's distance from its arrow, within a glyph's side bearing.
        self.assertLessEqual(abs((label - l1) - (r0 - title)), 2,
                             f'row gaps: label {label - l1 - 1} px, title {r0 - title - 1} px')
        (l0, l1), (r0, r1), _, _ = self.edge_runs(card, 185, 206)
        self.assertEqual((l0, last - r1), (l0, l0), f'side arrows: left x {l0}..{l1}, right x {r0}..{r1}')
        self.assertEqual(l1 - l0, r1 - r0, 'side arrows differ in width')

    @staticmethod
    def bands(image, top, bottom):
        """Runs of rows with ink across the text width, as (first, last) pairs."""
        rows = [any(image.getpixel((x, y)) < 128 for x in range(24, image.width - 24)) for y in range(top, bottom)]
        runs = []
        for offset, dark in enumerate(rows):
            if dark and (not runs or runs[-1][1] != top + offset - 1):
                runs.append([top + offset, top + offset])
            elif dark:
                runs[-1][1] = top + offset
        return [tuple(run) for run in runs]

    def test_card_title_is_larger_and_clear_of_the_lines_around_it(self):
        # Before the title went one size up (Geist bold 12/14/16 at text sizes 0/1/2) the short
        # title's ink measured 25/28/31 rows. Size 2 has no larger Geist face in flash and keeps 31.
        base = {0: 25, 1: 28, 2: 31}
        books = [self.add_book(PATH_A, TITLE_A, 'Tác giả Mẫu, Người Viết', EXCERPT_A),
                 self.add_book('/ben-song.epub', 'Bến sông ngày gió', 'Người Viết Thử',
                               'Nước lên từ sáng, bến vắng người, chỉ còn tiếng gió qua mấy mái chèo. '
                               'Chiều xuống, thuyền về muộn, đèn trên bến bật lên từng ngọn một.',
                               'test_kerning_ligature.epub')]
        self.write_recent(books)
        for size in (0, 1, 2):
            with self.subTest(size=size):
                (self.store / 'settings.json').write_text(json.dumps(
                    {'language': 'VI', 'uiTheme': 4, 'sleepTimeout': 120, 'uiTextSize': size}))
                _, images = self.launch('2000:RIGHT;5000:QUIT', [(1800, f'long-{size}'), (4600, f'short-{size}')])
                for name, title_lines in ((f'long-{size}', 2), (f'short-{size}', 1)):
                    card = images[name]
                    rule = next(y for y in range(600, 780)
                                if sum(card.getpixel((x, y)) < 128 for x in range(card.width)) > 400)
                    runs = self.bands(card, 124, rule)
                    # The cover (with the stats beside it) is one tall run; then title, author, excerpt.
                    cover_run = next(i for i, (first, last) in enumerate(runs) if last - first > 100)
                    lines = runs[cover_run + 1:]
                    # Title lines, the author, and one line of excerpt; a two-line title leaves no room for it.
                    self.assertEqual(len(lines), title_lines + (1 if title_lines == 2 else 2), f'{name}: {runs}')
                    heights = [last - first + 1 for first, last in lines]
                    if name.startswith('short') and size < 2:
                        self.assertGreater(heights[0], base[size], f'{name}: title {heights[0]} rows, {runs}')
                    # Each run is one line of text: a title line running into the author or the
                    # excerpt would join two lines into one run taller than any single line.
                    self.assertLessEqual(max(heights), 38 if size == 0 else 43, f'{name}: {runs}')
                    self.assertLess(lines[-1][1], rule - 1, f'{name}: text reaches the rule')

    def test_keyboard_side_arrows_mirror_each_other(self):
        # The keyboard draws its side button arrows through the theme, apart from the list screens.
        (self.store / 'menu-customization.json').write_text(json.dumps(
            {'version': 1, 'tabs': {'home': [0, 1, 4, 2, 3], 'settings': [0, 7, 1, 2, 3, 4, 5, 6],
                                    'reader': [0, 1, 2, 3], 'text': [0, 1, 2, 3]},
             'pins': ['kosync/koServerUrl']}))
        log, images = self.launch('1000:DOWN;1600:DOWN;2300:CONFIRM;4500:QUIT', [(4000, 'keyboard')])
        self.assertIn('Entering activity: KeyboardEntry', log)
        board = images['keyboard']
        (l0, l1), (r0, r1), _, _ = self.edge_runs(board, 185, 206)
        self.assertEqual((l0, board.width - 1 - r1), (l0, l0),
                         f'keyboard arrows: left x {l0}..{l1}, right x {r0}..{r1}')
        self.assertEqual(l1 - l0, r1 - r0, 'keyboard arrows differ in width')

    def test_quote_saved_in_the_reader_is_on_the_card_back_home(self):
        self.write_recent([self.add_book(PATH_A, TITLE_A, 'Tác giả Mẫu, Người Viết', EXCERPT_A)])
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
