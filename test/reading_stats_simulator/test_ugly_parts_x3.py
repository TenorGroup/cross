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

from ugly_common import Card, entered

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


if __name__ == '__main__':
    unittest.main()
