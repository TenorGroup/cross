"""tenor/ugly on the X3 button reader: the screens met while reading are written by hand.

Each screen has 2 sides. In the ugly shell it draws its own frame (a log line names it) and the screen is not the
one tenor/cross draws. In tenor/cross every pixel is the one drawn before the ugly branch existed (sha256 of the
1-bit screen, the clock box rubbed out).

TEST_PROGRAM picks the simulator (simulator_x3 is the UC8253 panel, simulator_x3_uc8279 the other one).
"""
import hashlib
import json
import re
import struct
import unittest
import zipfile

from PIL import ImageDraw

from ugly_common import Card, digest

BODY = 'Gió lên từ phía bãi, mang theo mùi rong và mùi khói bếp của mấy nhà ven đê. Cậu đứng lâu ở đầu cầu, ' \
       'đếm từng chiếc thuyền về muộn, rồi mới chịu quay vào. '
CLOCK = (400, 748, 528, 792)  # the clock in the key bar of tenor/cross
# Screens of tenor/cross before the ugly branches (f71cd39d, 3376fa36), the clock box rubbed out. The end menu
# opens on its first row since the turn into it stopped moving the selection.
CROSS = {
    'eob_plain': '490012e7fcd7e735',
    'eob_menu': 'a0c8328098129235',
    'percent': '6ab5c7354d1e3bd2',
    'chapter_entry': '6c974a15195b58d5',
    'definition': '5db94663acffe057',
    'page': '6c235d5d47876125',
    'marked': '9feb91ec066ca9c1',
    'notice': '49ecd4c4e7dad049',
    'xtc_toc': '26d9891198b82a1a',
    'saved_quote': '77b3fe0a4546e437',
}
# The hand-written notices, the same strokes on every draw.
UGLY = {
    'notice': '6507229e90714cd4',
    'saved_quote': '9a6b15eb9f9dfa11',
}
NOTICE = (0, 120, 528, 330)  # the band a notice of the reader is drawn in


def write_epub(path, title, paragraphs):
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>' + title + '</dc:title><dc:identifier id="id">' + title + '</dc:identifier><dc:language>vi</dc:language></metadata><manifest><item id="c0" href="c0.xhtml" media-type="application/xhtml+xml"/><item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx"><itemref idref="c0"/><itemref idref="c1"/></spine></package>')
        z.writestr('toc.ncx', '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head><meta name="dtb:uid" content="' + title + '"/></head><docTitle><text>' + title + '</text></docTitle><navMap><navPoint id="n0" playOrder="1"><navLabel><text>Chương 1</text></navLabel><content src="c0.xhtml"/></navPoint><navPoint id="n1" playOrder="2"><navLabel><text>Chương 2</text></navLabel><content src="c1.xhtml"/></navPoint></navMap></ncx>')
        for i in range(2):
            z.writestr('c%d.xhtml' % i, '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>x</title></head><body><p>' + BODY * paragraphs + '</p></body></html>')


def write_dictionary(sd):
    """A StarDict dictionary at /dictionaries/vd with every word of BODY, each a definition of a few pages."""
    folder = sd / 'dictionaries' / 'vd'
    folder.mkdir(parents=True)
    words = sorted({w.strip('.,').lower().encode() for w in BODY.split()})
    meaning = ('Luồng không khí chuyển động, thổi từ nơi áp cao sang nơi áp thấp. ' * 40).encode()
    idx = b''.join(w + b'\0' + (0).to_bytes(4, 'big') + len(meaning).to_bytes(4, 'big') for w in words)
    (folder / 'vd.dict').write_bytes(meaning)
    (folder / 'vd.idx').write_bytes(idx)
    (folder / 'vd.ifo').write_text("StarDict's dict ifo file\nversion=2.4.2\nwordcount=%d\nidxfilesize=%d\nbookname=vd\n"
                                   "sametypesequence=m\n" % (len(words), len(idx)))


def write_xtc(path):
    """A 3-page XTC whose table of contents has 2 chapters."""
    names = [('Chương 1', 1, 2), ('Chương 2', 3, 3)]
    chapters = b''.join(n.encode().ljust(80, b'\0') + struct.pack('<HH', a, b).ljust(16, b'\0') for n, a, b in names)
    pages = []
    for number in range(3):
        bitmap = bytearray(b'\xff' * (528 * 792 // 8))
        for y in range(80 + number * 90, 120 + number * 90):
            bitmap[y * 66 + 8:y * 66 + 40] = b'\x00' * 32
        pages.append(struct.pack('<IHHBBIQ', 0x00475458, 528, 792, 0, 0, len(bitmap), 0) + bitmap)
    table_at = 56 + len(chapters)
    start = table_at + 16 * len(pages)
    header = struct.pack('<IBBHBBBBIQQQQII', 0x00435458, 1, 0, len(pages), 0, 0, 0, 1, 1, 0, table_at, start, 0, 56, 0)
    table = b''.join(struct.pack('<QIHH', start + i * len(page), len(page), 528, 792) for i, page in enumerate(pages))
    path.write_bytes(header + chapters + table + b''.join(pages))


def screen_digest(image):
    image = image.copy()
    ImageDraw.Draw(image).rectangle(CLOCK, fill=255)
    return hashlib.sha256(image.tobytes()).hexdigest()[:16]


class ReadingScreensTest(unittest.TestCase):
    def card(self, shell, siblings=(), paragraphs=1, dictionary=False, xtc=False, **settings):
        if dictionary:
            settings['dictionaryName'] = 'vd'
        card = Card(shell=shell, books=[], stats=False, textAntiAliasing=0, **settings)
        self.addCleanup(card.close)
        if dictionary:
            write_dictionary(card.sd)
        if xtc:
            write_xtc(card.sd / 'k.xtc')
            (card.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/k.xtc', 'title': 'Kidnapped', 'author': 'RLS'}]}))
            return card
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
        frames = [(int(t), int(s)) for t, s in re.findall(r'\[(\d+)\] \[INF\] \[UGLY\] EndOfBook frame rows=3 sel=(\d+)', log)]
        self.assertTrue(frames, 'no ugly end menu')
        self.assertEqual([s for t, s in frames if t < 9000], [0], 'the turn into the end does not move the selection')
        self.assertEqual([s for t, s in frames if t >= 9000], [1], 'one press, one row')
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

    def test_definition(self):
        script = self.MENU + ';4600:DOWN;5300:DOWN;6000:RIGHT;6700:CONFIRM;8500:CONFIRM;11500:RIGHT;13600:QUIT'
        log, ugly, cross = self.both(script, [(11000, 'word'), (13000, 'page2')], paragraphs=8, dictionary=True)
        self.assertIn('DictionaryDefinition', log)
        self.assertTrue(re.search(r'Definition frame page=1/[2-9]', log), 'no ugly headword')
        self.assertTrue(re.search(r'Definition frame page=2/[2-9]', log), 'the count follows the page')
        self.check('definition', ugly['word'], cross['word'])

    # A page, a bookmark by holding Confirm, the notice, then the marked page.
    MARK = '1000:CONFIRM;3200:CONFIRM:1200;7500:QUIT'

    def test_status_strip_in_a_book(self):
        log, ugly, cross = self.both(self.MARK, [(3000, 'page'), (7000, 'marked')], paragraphs=8, longPressMenuFunction=2)
        self.assertTrue('part=status reader=1 pct=1 mark=0' in log, 'no number beside the battery')
        self.assertTrue('part=status reader=1 pct=1 mark=1' in log, 'no star on a marked page')
        bare_log, bare = self.card(1, paragraphs=8, hideBatteryPercentage=1).run('1000:CONFIRM;3500:QUIT', [(3000, 'page')], timeout=90)
        self.assertTrue('part=status reader=1 pct=0' in bare_log, 'the number follows the setting')
        band = (0, 740, 400, 792)  # the strip, the clock left out
        self.assertNotEqual(digest(ugly['page'].crop(band)), digest(bare['page'].crop(band)), 'the number is written')
        self.assertNotEqual(digest(ugly['page'].crop(band)), digest(ugly['marked'].crop(band)), 'the star is drawn')
        self.check('page', ugly['page'], cross['page'])
        self.check('marked', ugly['marked'], cross['marked'])

    def test_bookmark_notice(self):
        log, ugly, cross = self.both(self.MARK, [(5000, 'notice')], paragraphs=8, longPressMenuFunction=2)
        self.assertTrue('part=notice' in log.split('[IN] press')[-1], 'no notice after the hold')
        self.assertEqual(digest(ugly['notice'].crop(NOTICE))[:16], UGLY['notice'], 'the notice says it in the voice of the shell')
        self.check('notice', ugly['notice'], cross['notice'])

    def test_xtc_contents(self):
        log, ugly, cross = self.both('1000:CONFIRM;3500:CONFIRM;6000:QUIT', [(5500, 'toc')], xtc=True)
        self.assertIn('XtcReaderChapterSelection', log)
        self.assertTrue('part=header' in log.split('Entering activity: XtcReaderChapterSelection')[1], 'no hand header')
        self.check('xtc_toc', ugly['toc'], cross['toc'])

    def test_saved_quote_notice(self):
        script = self.MENU + ';4600:DOWN;5300:DOWN;6000:RIGHT;6700:RIGHT;7400:CONFIRM;9000:CONFIRM;9700:RIGHT;10400:CONFIRM;12500:QUIT'
        log, ugly, cross = self.both(script, [(11500, 'saved')], paragraphs=8)
        self.assertIn('QuoteSelect', log)
        self.assertEqual(digest(ugly['saved'].crop(NOTICE))[:16], UGLY['saved_quote'], 'the jab and where the quote went')
        self.check('saved_quote', ugly['saved'], cross['saved'])


if __name__ == '__main__':
    unittest.main()
