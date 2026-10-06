"""X4 Pro framed lists (founder 06/10): a row that runs past the page stays inside the frame, the frame going
on under it and fading with it; the faded rows at either end take no tap of their own, a tap there scrolls to
them. Settings > Reader > Text settings > Layout with a reading font whose preview is tall (an SD family at
18 pt and the large interface text: 6 of the 7 rows fit)."""
from pathlib import Path
import shutil
import tempfile

from test_thanh_day import run, REPO

FONT = REPO / 'test/reading_stats_simulator/fixtures/BeVietnamPro_18.cpfont'
SETTINGS = dict(sdFontFamilyName='BeVietnamPro', fontSize=18, fontFamily=1, letterSpacing=4, wordSpacing=0,
                extraParagraphSpacing=1, paragraphAlignment=0, screenMargin=5, paragraphIndent=2, uiTextSize=2,
                readerTapTip=0)
OPEN = '2500:TAP:423,754;4500:TAP:240,241;6500:TAP:240,80;8500:TAP:326,754'


def font(sach):
    target = sach.parent / '.fonts/BeVietnamPro'
    target.mkdir(parents=True)
    shutil.copyfile(FONT, target / FONT.name)


def ink(image, x, y0, y1):
    return sum(1 for y in range(y0, y1) if image.getpixel((x, y)) < 128)


def same(a, b, box=(0, 250, 480, 716)):
    return list(a.crop(box).getdata()) == list(b.crop(box).getdata())


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-fade-') as tmp:
        root = Path(tmp)
        # The page, then a tap on the faded row at the foot, then a tap on the faded row at the top.
        page, below, above = run(root / 'a', OPEN + ';10500:TAP:240,692;13000:TAP:240,305', [10300, 12800, 15300],
                                 settings=SETTINGS, write_books=font)
        # The last row on the page runs past the frame's full rows: the frame's sides go on beside it.
        assert ink(page, 16, 600, 650) > 10, 'no frame side beside the full rows'
        assert ink(page, 16, 668, 704) > 0, 'the row past the page sits outside the frame'
        (swiped,) = run(root / 'b', OPEN + ';10500:SWIPE:240,650,240,420,200', [12800], settings=SETTINGS,
                        write_books=font)
        assert same(below, swiped), 'a tap on the faded row at the foot did not scroll to it'
        assert same(above, page), 'a tap on the faded row at the top did not scroll back'
    print('GREEN: X4 Pro framed list: the row past the page stays in the frame, faded rows scroll, they do not open')


if __name__ == '__main__':
    main()
