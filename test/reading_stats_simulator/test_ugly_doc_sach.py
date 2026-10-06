"""tenor/ugly on the X3 button reader: the screens met while reading are written by hand.

Each screen has 2 sides. In the ugly shell it draws its own frame (a log line names it) and the screen is not the
one tenor/cross draws. In tenor/cross every pixel is the one drawn before the ugly branch existed (sha256 of the
1-bit screen, the clock box rubbed out).

TEST_PROGRAM picks the simulator (simulator_x3 is the UC8253 panel, simulator_x3_uc8279 the other one).
"""
import hashlib
import json
import unittest
import zipfile

from PIL import ImageDraw

from ugly_common import Card

BODY = 'Gió lên từ phía bãi, mang theo mùi rong và mùi khói bếp của mấy nhà ven đê. Cậu đứng lâu ở đầu cầu, ' \
       'đếm từng chiếc thuyền về muộn, rồi mới chịu quay vào. '
CLOCK = (400, 748, 528, 792)  # the clock in the key bar of tenor/cross
# Screens of tenor/cross before the ugly branches (f71cd39d), the clock box rubbed out.
CROSS = {
    'eob_plain': '490012e7fcd7e735',
    'eob_menu': 'cd447ae2b5b6c6ed',
    'percent': '6ab5c7354d1e3bd2',
    'chapter_entry': '6c974a15195b58d5',
}


def write_epub(path, title, paragraphs):
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>' + title + '</dc:title><dc:identifier id="id">' + title + '</dc:identifier><dc:language>vi</dc:language></metadata><manifest><item id="c0" href="c0.xhtml" media-type="application/xhtml+xml"/><item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx"><itemref idref="c0"/><itemref idref="c1"/></spine></package>')
        z.writestr('toc.ncx', '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head><meta name="dtb:uid" content="' + title + '"/></head><docTitle><text>' + title + '</text></docTitle><navMap><navPoint id="n0" playOrder="1"><navLabel><text>Chương 1</text></navLabel><content src="c0.xhtml"/></navPoint><navPoint id="n1" playOrder="2"><navLabel><text>Chương 2</text></navLabel><content src="c1.xhtml"/></navPoint></navMap></ncx>')
        for i in range(2):
            z.writestr('c%d.xhtml' % i, '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>x</title></head><body><p>' + BODY * paragraphs + '</p></body></html>')


def screen_digest(image):
    image = image.copy()
    ImageDraw.Draw(image).rectangle(CLOCK, fill=255)
    return hashlib.sha256(image.tobytes()).hexdigest()[:16]


class ReadingScreensTest(unittest.TestCase):
    def card(self, shell, siblings=(), paragraphs=1):
        card = Card(shell=shell, books=[], stats=False, textAntiAliasing=0)
        self.addCleanup(card.close)
        write_epub(card.sd / 'k.epub', 'Kidnapped', paragraphs)
        for name in siblings:
            write_epub(card.sd / name, name, 1)
        (card.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/k.epub', 'title': 'Kidnapped', 'author': 'RLS'}]}))
        return card

    def both(self, script, shot, **kw):
        """The same keys in tenor/ugly and in tenor/cross: (ugly log, ugly shots, cross shots)."""
        ugly = self.card(1, **kw).run(script, shot, timeout=90)
        cross = self.card(0, **kw).run(script, shot, timeout=90)
        return ugly[0], ugly[1], cross[1]

    def check(self, name, ugly_shot, cross_shot):
        self.assertNotEqual(screen_digest(ugly_shot), screen_digest(cross_shot), 'ugly draws its own %s' % name)
        self.assertEqual(screen_digest(cross_shot), CROSS[name], 'tenor/cross draws %s as before' % name)

    # 1 page a chapter, 2 chapters: 2 turns reach the end.
    END = '1000:CONFIRM;5000:RIGHT;6500:RIGHT'

    def test_end_of_book_plain(self):
        log, ugly, cross = self.both(self.END + ';10000:QUIT', [(9000, 'eob')])
        self.assertTrue('EndOfBook frame rows=0' in log, 'no ugly plain end')
        self.check('eob_plain', ugly['eob'], cross['eob'])

    def test_end_of_book_with_the_next_books(self):
        script = self.END + ';9000:DOWN;11500:QUIT'
        log, ugly, cross = self.both(script, [(8500, 'eob'), (11000, 'down')], siblings=('l.epub', 'm.epub'))
        self.assertTrue('EndOfBook frame rows=3 sel=0' in log, 'no ugly end menu')
        self.assertNotEqual(screen_digest(ugly['eob']), screen_digest(ugly['down']), 'the circle moves')
        self.check('eob_menu', ugly['eob'], cross['eob'])

    # The reader menu, its Place tab, then a row of it.
    MENU = '1000:CONFIRM;3200:CONFIRM;3900:DOWN'

    def test_go_to_percent(self):
        script = self.MENU + ';4600:RIGHT;5300:CONFIRM;7500:RIGHT;9600:QUIT'
        log, ugly, cross = self.both(script, [(7000, 'pct'), (9000, 'step')], paragraphs=8)
        self.assertIn('EpubReaderPercentSelection', log)
        self.assertTrue('Percent frame value=0' in log, 'no ugly percent')
        self.assertTrue('Percent frame value=1' in log, 'a step is drawn')
        self.assertNotEqual(screen_digest(ugly['pct']), screen_digest(ugly['step']), 'the number and the circle move')
        self.check('percent', ugly['pct'], cross['pct'])

    def test_chapter_number_entry(self):
        script = self.MENU + ';4600:CONFIRM;6000:CONFIRM:1000;9500:DOWN;11600:QUIT'
        log, ugly, cross = self.both(script, [(9000, 'entry'), (11000, 'next')], paragraphs=8)
        self.assertIn('ChapterNumberEntry', log)
        self.assertTrue('ChapterNumber frame cursor=5' in log, 'no ugly chapter number')
        self.assertTrue('ChapterNumber frame cursor=0' in log, 'the underline moves')
        self.check('chapter_entry', ugly['entry'], cross['entry'])


if __name__ == '__main__':
    unittest.main()
