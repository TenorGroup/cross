"""tenor/ugly in the reader: the chapter's heading is written by hand, and only there.

Founder rule (04/10/2026): while the ugly shell is on, the name of a chapter on the first page of the
chapter is written in the handwriting of the shell; the body keeps the reading font; tenor/cross draws
every pixel as before. The layout is not touched, so the pages, the saved position and the section cache
are the same in both shells.

The page is a one-chapter EPUB whose heading is the title in its table of contents. A heading that is
not that title, or whose letters the baked font lacks, stays as laid out.
"""
import hashlib
import json
import re
import unittest
import zipfile

from PIL import Image

from ugly_common import Card, digest

BODY = 'Gió lên từ phía bãi, mang theo mùi rong và mùi khói bếp của mấy nhà ven đê. Cậu đứng lâu ở đầu cầu, ' \
       'đếm từng chiếc thuyền về muộn, rồi mới chịu quay vào. ' * 12
PAGE = (0, 0, 528, 740)  # the page without the footer (the clock lives there)
# Pages of the fixture drawn by tenor/cross before the hand heading existed (sha256 of the 1-bit page).
CROSS_PAGE_1 = '179b0f49cab19056'
CROSS_PAGE_2 = '40e89bcf13f3cccb'
UGLY_PAGE_1 = '9f0f9f9e5ac12752'


def write_epub(path, toc_title, heading):
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Kidnapped</dc:title><dc:identifier id="id">chuong1</dc:identifier><dc:language>vi</dc:language></metadata><manifest><item id="c0" href="c0.xhtml" media-type="application/xhtml+xml"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx"><itemref idref="c0"/></spine></package>')
        z.writestr('toc.ncx', '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head><meta name="dtb:uid" content="chuong1"/></head><docTitle><text>Kidnapped</text></docTitle><navMap><navPoint id="n0" playOrder="1"><navLabel><text>' + toc_title + '</text></navLabel><content src="c0.xhtml"/></navPoint></navMap></ncx>')
        z.writestr('c0.xhtml', '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>x</title></head><body>' + (heading if heading.startswith('<') else '<h1>' + heading + '</h1>') + '<p>' + BODY + '</p></body></html>')


def page_digest(image, box=PAGE):
    return hashlib.sha256(image.crop(box).tobytes()).hexdigest()[:16]


class ChapterTitleTest(unittest.TestCase):
    TITLE = 'Chương 3. Gió ngoài bãi'

    def card(self, shell, title=TITLE, heading=TITLE, **settings):
        card = Card(shell=shell, books=[], stats=False, **dict({'textAntiAliasing': 0}, **settings))
        self.addCleanup(card.close)
        write_epub(card.sd / 'k.epub', title, heading)
        (card.store / 'recent.json').write_text(json.dumps({'books': [{'path': '/k.epub', 'title': 'Kidnapped', 'author': 'RLS'}]}))
        return card

    def open_book(self, card, turn=False, **kw):
        """Opens the book from the first screen of either shell; `turn` goes one page on before the second shot."""
        script = '1000:CONFIRM;9000:RIGHT;11000:QUIT' if turn else '1000:CONFIRM;9000:QUIT'
        return card.run(script, [(7000, 'p1')] + ([(10500, 'p2')] if turn else []), timeout=90, **kw)

    def band(self, log):
        top, bottom = re.search(r'Hand heading lines=[1-3] band=(\d+)-(\d+)', log).groups()
        return int(top), int(bottom)

    def test_cross_draws_the_pages_as_before(self):
        log, shots = self.open_book(self.card(0), turn=True)
        self.assertNotIn('Hand heading', log)
        self.assertEqual(page_digest(shots['p1']), CROSS_PAGE_1)
        self.assertEqual(page_digest(shots['p2']), CROSS_PAGE_2)

    def test_ugly_writes_the_heading_by_hand_and_leaves_the_body_alone(self):
        log, shots = self.open_book(self.card(1), turn=True)
        cross = self.open_book(self.card(0))[1]
        top, bottom = self.band(log)
        self.assertEqual(log.count('Hand heading lines=1'), 1, 'only the first page of the chapter')
        # The band holds other pixels than the heading it replaces (whose descenders reach a few rows past
        # it), and from there down to the footer the page is the one tenor/cross draws.
        foot = bottom + 8
        self.assertNotEqual(page_digest(shots['p1'], (0, 0, 528, foot)), page_digest(cross['p1'], (0, 0, 528, foot)))
        self.assertEqual(page_digest(shots['p1'], (0, foot, 528, 740)), page_digest(cross['p1'], (0, foot, 528, 740)))
        self.assertEqual(page_digest(shots['p2']), CROSS_PAGE_2, 'the second page is a page of the reading font')
        self.assertEqual(page_digest(shots['p1']), UGLY_PAGE_1, 'the strokes are the same on every draw')

    def test_the_heading_is_gone_from_the_gray_passes(self):
        # With anti-aliased text the page has gray pixels; the hand heading is pure black, so nothing of
        # the heading it replaced may be left in the gray planes.
        def band_values(shell):
            card = self.card(shell, textAntiAliasing=1)
            log, _ = self.open_book(card)
            raw = Image.open(card.sd / 'p1.bmp').convert('L')
            top, bottom = self.band(log) if shell else (9, 52)
            return {v for v in raw.crop((0, top, 528, bottom - 8)).getdata()}
        self.assertEqual(band_values(1), {0, 255})
        self.assertTrue(band_values(0) - {0, 255}, 'the heading of tenor/cross is anti-aliased')

    def test_a_heading_in_two_blocks_is_one_title_in_the_band_of_both(self):
        heading = '<h1>Chương 3.</h1><h2>Gió ngoài bãi</h2>'
        ugly, cross = self.open_book(self.card(1, heading=heading)), self.open_book(self.card(0, heading=heading))
        top, bottom = self.band(ugly[0])
        self.assertIn('Hand heading lines=2', ugly[0])
        self.assertGreater(bottom - top, 80, 'the room of two headings holds the larger pen')
        foot = bottom + 8
        self.assertNotEqual(page_digest(ugly[1]['p1'], (0, 0, 528, foot)), page_digest(cross[1]['p1'], (0, 0, 528, foot)))
        self.assertEqual(page_digest(ugly[1]['p1'], (0, foot, 528, 740)), page_digest(cross[1]['p1'], (0, foot, 528, 740)))

    def test_a_heading_that_is_not_the_title_stays_as_laid_out(self):
        ugly = self.open_book(self.card(1, heading='Mở đầu'))
        cross = self.open_book(self.card(0, heading='Mở đầu'))
        self.assertIn('Hand heading lines=0', ugly[0])
        self.assertEqual(digest(ugly[1]['p1'].crop(PAGE)), digest(cross[1]['p1'].crop(PAGE)))

    def test_a_title_the_baked_font_lacks_stays_as_laid_out(self):
        title = '三体 Gió'
        ugly = self.open_book(self.card(1, title=title, heading=title))
        cross = self.open_book(self.card(0, title=title, heading=title))
        self.assertIn('Hand heading lines=0', ugly[0])
        self.assertEqual(digest(ugly[1]['p1'].crop(PAGE)), digest(cross[1]['p1'].crop(PAGE)))

    def test_the_place_in_the_book_survives_a_change_of_shell_both_ways(self):
        card = self.card(1)
        self.open_book(card, turn=True)  # read to page 2 in the ugly shell
        for shell in (0, 1):  # out to tenor/cross and back to ugly, each time reopening on page 2
            settings = card.settings()
            settings['uiShell'] = shell
            (card.store / 'settings.json').write_text(json.dumps(settings))
            log, shots = card.run('1000:CONFIRM;9000:QUIT', [(7000, 'again')], timeout=90)
            self.assertEqual(page_digest(shots['again']), CROSS_PAGE_2, 'shell %d reopens on page 2' % shell)
            self.assertNotIn('Hand heading', log)
        # and back on page 1 the hand writes the heading again
        log, shots = card.run('1000:CONFIRM;9000:LEFT;11000:QUIT', [(10500, 'first')], timeout=90)
        self.assertEqual(page_digest(shots['first']), UGLY_PAGE_1)


if __name__ == '__main__':
    unittest.main()
