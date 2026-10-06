"""tenor/ugly on the X3: the parts every screen shares draw by hand.

One representative screen per part, reached from the diary by keys: the header, the list rows, the key bar and
the status strip on a list (Language); a tip under a list (the book's contents); a notice (looking up a word
without a dictionary); the question box (resetting the statistics). Each part logs `part=<name>` in the
simulator when it draws by hand; the screens drawn the tenor/cross way log nothing. UGLY_SHOTS keeps the frames.
"""
import html
import re
import unittest
import zipfile

from ugly_common import Card, digest, entered, ink

GAP, START, SETTLE = 700, 1500, 1800


def settings_question(group, question):
    """Keys from the diary to question `question` of Settings group `group` (both counted from 1, groups from 0)."""
    return ['UP'] + ['RIGHT'] * group + ['CONFIRM'] + ['RIGHT'] * (question - 1) + ['CONFIRM']


def book(path):
    """A 3-chapter EPUB with a table of contents."""
    chapters = ['Chương 1. Mở đầu', 'Chương 2. Đường xa', 'Chương 3. Trở về']
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="b.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        items = ''.join('<item id="c%d" href="c%d.xhtml" media-type="application/xhtml+xml"/>' % (i, i) for i in range(3))
        spine = ''.join('<itemref idref="c%d"/>' % i for i in range(3))
        z.writestr('b.opf', '<?xml version="1.0" encoding="utf-8"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Sách thử</dc:title><dc:identifier id="id">parts</dc:identifier><dc:language>vi</dc:language></metadata><manifest><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>' + items + '</manifest><spine toc="ncx">' + spine + '</spine></package>')
        nav = ''.join('<navPoint id="n%d" playOrder="%d"><navLabel><text>%s</text></navLabel><content src="c%d.xhtml"/></navPoint>' % (i, i + 1, html.escape(t), i) for i, t in enumerate(chapters))
        z.writestr('toc.ncx', '<?xml version="1.0" encoding="utf-8"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head><meta name="dtb:uid" content="parts"/></head><docTitle><text>Sách thử</text></docTitle><navMap>' + nav + '</navMap></ncx>')
        for i, t in enumerate(chapters):
            z.writestr('c%d.xhtml' % i, '<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>%s</title></head><body><h1>%s</h1>%s</body></html>' % (t, t, '<p>Dế Mèn đi qua cánh đồng.</p>' * 40))


READER = [('Sách thử', 'sach.epub'), ('Kidnapped', 'b0.txt')]
# The representative screens: (keys from the diary, recent books, the activity it must reach).
SCREENS = {
    'language': (settings_question(7, 1), None, 'LanguageSelect'),
    'contents': (['CONFIRM', 'WAIT:2200', 'CONFIRM', 'DOWN', 'CONFIRM'], READER, 'EpubReaderChapterSelection'),
    'notice': (['CONFIRM', 'WAIT:2200', 'CONFIRM'] + ['DOWN'] * 3 + ['RIGHT', 'CONFIRM'], READER, 'EpubReader'),
    'ask': (['DOWN'] * 4 + ['RIGHT'] * 4 + ['CONFIRM'], None, 'Confirmation'),
}


class UglyPartsX3(unittest.TestCase):
    logs = {}

    def drawn(self, screen):
        """The parts drawn by hand on a screen, each screen driven once."""
        if screen not in self.logs:
            keys, books, activity = SCREENS[screen]
            card = Card(books=books) if books else Card()
            try:
                if books:
                    book(card.sd / 'sach.epub')
                t, parts = START, []
                for k in keys:
                    if k.startswith('WAIT:'):
                        t += int(k[5:])
                        continue
                    parts.append('%d:%s' % (t, k))
                    t += GAP
                shot = t - GAP + SETTLE
                parts.append('%d:QUIT' % (shot + 600))
                log, shots = card.run(';'.join(parts), [(shot, 'parts_' + screen)], timeout=120)
            finally:
                card.close()
            self.assertIn('parts_' + screen, shots, log[-1500:])
            self.assertIn(activity, ' '.join(entered(log)), log[-1500:])
            self.logs[screen] = (set(re.findall(r'\bpart=(\w+)', log)), log)
        return self.logs[screen]

    def assertHand(self, screen, part):
        parts, log = self.drawn(screen)
        self.assertIn(part, parts, '%s on %s drawn the tenor/cross way; by hand: %s\n%s' % (part, screen, sorted(parts), log[-1200:]))

    def test_header(self):
        self.assertHand('language', 'header')

    def test_key_bar(self):
        self.assertHand('language', 'keys')

    def test_status_strip(self):
        self.assertHand('language', 'status')

    def test_list_rows(self):
        self.assertHand('language', 'rows')

    def test_tip_under_a_list(self):
        self.assertHand('contents', 'tip')

    def test_notice(self):
        self.assertHand('notice', 'notice')

    def test_question_box(self):
        self.assertHand('ask', 'ask')

    def test_the_cursor_circle_keeps_off_the_value(self):
        # The current chapter is written "Đang đọc" at the row's right end; the circle on the cursor row goes round the
        # label, so the value has the same pixels with the cursor on its row and on the next one.
        keys, books, _ = SCREENS['contents']
        card = Card(books=books)
        try:
            book(card.sd / 'sach.epub')
            t, parts = START, []
            for k in keys:
                if k.startswith('WAIT:'):
                    t += int(k[5:])
                    continue
                parts.append('%d:%s' % (t, k))
                t += GAP
            on = t - GAP + SETTLE
            parts += ['%d:RIGHT' % (on + 300), '%d:QUIT' % (on + 2600)]
            log, shots = card.run(';'.join(parts), [(on, 'on'), (on + 2000, 'below')], timeout=120)
        finally:
            card.close()
        value, label = (330, 128, 528, 188), (0, 128, 330, 188)
        self.assertNotEqual(digest(shots['on'].crop(label)), digest(shots['below'].crop(label)), 'the cursor left the first row')
        self.assertEqual(digest(shots['on'].crop(value)), digest(shots['below'].crop(value)),
                         'the circle of the cursor row crosses the value at its right end')
    def shot(self, keys, books=READER):
        """One frame SETTLE ms after the last key."""
        card = Card(books=books)
        try:
            book(card.sd / 'sach.epub')
            t, parts = START, []
            for k in keys:
                if k.startswith('WAIT:'):
                    t += int(k[5:])
                    continue
                parts.append('%d:%s' % (t, k))
                t += GAP
            at = t - GAP + SETTLE
            parts.append('%d:QUIT' % (at + 600))
            log, shots = card.run(';'.join(parts), [(at, 'frame')], timeout=120)
        finally:
            card.close()
        self.assertIn('frame', shots, log[-1500:])
        return shots['frame']

    def assertBlank(self, image, box, what):
        self.assertEqual(ink(image, box), 0, what)

    def test_the_reader_menu_circles_the_label_of_its_cursor_row(self):
        # Reading tab, the cursor on "Cài đặt văn bản": the right end of its row, where a value would stand, stays clear.
        menu = self.shot(['CONFIRM', 'WAIT:2200', 'CONFIRM', 'DOWN', 'DOWN'])
        self.assertBlank(menu, (330, 172, 476, 228), 'the circle of the reader menu runs to the row end')

    def test_a_popup_circles_the_label_of_its_focused_option(self):
        # The status bar popup of the reader menu, focus on its current choice: the option's ends stay clear.
        popup = self.shot(['CONFIRM', 'WAIT:2200', 'CONFIRM', 'DOWN', 'DOWN', 'RIGHT', 'RIGHT', 'CONFIRM'])
        self.assertBlank(popup, (80, 358, 160, 412), 'the circle of the popup runs to the option start')
        self.assertBlank(popup, (370, 358, 450, 412), 'the circle of the popup runs to the option end')

    def test_a_chinese_letter_leaves_the_rest_of_the_name_in_hand(self):
        # The notebook's File page: "Tam thể tập hai" and the same name with 体 in it. The pen lacks 体 alone, so the
        # 2 names start with the same handwritten "Tam thể"; the long one trails off in the scrawl, no dots.
        card = Card(books=[], files=['A.txt', 'Tam thể tập hai.txt', 'Tam thể 体 tập hai.txt'])
        try:
            log, shots = card.run('1000:DOWN;1800:DOWN;4000:QUIT', [(3200, 'files')])
        finally:
            card.close()
        page = shots['files']
        bands, inside = [], False
        for y in range(175, 520):
            inked = ink(page, (48, y, 132, y + 1)) > 0
            if inked and not inside:
                bands.append(y)
            inside = inked
        self.assertGreaterEqual(len(bands), 3, bands)
        hand, mixed = bands[1], bands[2]
        self.assertEqual(digest(page.crop((48, hand, 132, hand + 30))), digest(page.crop((48, mixed, 132, mixed + 30))),
                         'a name with a Chinese letter is written whole in the UI font')

    def wifi(self):
        if 'wifi' not in self.logs:
            card = Card()
            try:
                t, parts = START, []
                for k in settings_question(7, 3):
                    parts.append('%d:%s' % (t, k))
                    t += GAP
                at = t + 3000
                parts.append('%d:QUIT' % (at + 600))
                log, shots = card.run(';'.join(parts), [(at, 'wifi')], timeout=120)
            finally:
                card.close()
            self.assertIn('WifiSelection', entered(log), log[-1500:])
            self.logs['wifi'] = (shots['wifi'], log)
        return self.logs['wifi']

    def test_the_header_writes_its_note(self):
        # Wi-Fi names the networks found at the right end of its header, above the underline.
        self.assertGreater(ink(self.wifi()[0], (300, 10, 504, 44)), 0, 'the header drops the count of networks')



if __name__ == '__main__':
    unittest.main()
