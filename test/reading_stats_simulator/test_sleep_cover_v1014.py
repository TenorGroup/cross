"""v1.0.14 sleep covers on the X3 with "Black and white refresh before sleep" on (the default).

1. The Cover sleep screen. The cover was cached as a 4-level picture dithered for gray, and the
   X3 then folded those levels into black and white patterns: the dither had spread its error by
   the levels' own values, while the glass shows the patterns' share of white. Tones came out far
   too light (64 as 130, 128 as 208, 192 and above white). The cover is now dithered straight to
   black and white once, and drawn 1:1.
2. The small cover of the "Book + quotation" screen (96 x 145) read the whole 4-level sleep cover
   (104 KB on the X3, made at sleep when missing: 2 s) and kept one pixel in 5.5 of its folded
   patterns. It is now shrunk from the book's card thumbnail (11 KB, written when the reader
   closed) by area and dithered once.
3. The sleep screen leaves the saving of the device state to the one save that sends the device
   to sleep (main.cpp), instead of a second write of the same file of its own (130 to 300 ms on
   the X3). What it chose (the quote shown) must still be on the card after the sleep.

Fixtures are drawn here: a cover of flat tone bands. Set SLEEP_COVER_SHOTS to keep screenshots.
"""
import io
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
import zipfile

from PIL import Image, ImageDraw

from test_sleep_quote_v1011 import quote_id
from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
COVER_BOX = (48, 596, 144, 741)  # SleepQuoteLayout.h, the quote screen's cover tile
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
SHOTS = os.environ.get('SLEEP_COVER_SHOTS')
SLEEP_AT = 2500
BOOK = '/books/dai-tong.epub'
TITLE = 'Dải tông xám'
TONES = (32, 64, 128, 192, 224)


def tone_cover(width=600, height=900):
    """Flat bands, darkest at the top; the cover has the screen's own shape, so nothing is cropped."""
    image = Image.new('L', (width, height))
    draw = ImageDraw.Draw(image)
    for i, tone in enumerate(TONES):
        draw.rectangle((0, height * i // len(TONES), width, height * (i + 1) // len(TONES)), fill=tone)
    out = io.BytesIO()
    image.convert('RGB').save(out, 'JPEG', quality=95)
    return out.getvalue()


def write_epub(path, cover):
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>' + TITLE + '</dc:title><dc:identifier id="id">sleep-cover-v1014</dc:identifier><dc:language>vi</dc:language><meta name="cover" content="cover"/></metadata><manifest><item id="cover" href="cover.jpg" media-type="image/jpeg"/><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
        z.writestr('cover.jpg', cover)
        z.writestr('body.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>t</title></head><body><p>' + 'Một đoạn văn thử. ' * 200 + '</p></body></html>')


def band_tones(image, box, count):
    """Mean of the middle of each of `count` horizontal bands of `box`: a dither seen from afar."""
    x0, y0, x1, y1 = box
    tones = []
    for i in range(count):
        top, bottom = y0 + (y1 - y0) * i // count, y0 + (y1 - y0) * (i + 1) // count
        band = image.crop((x0 + 8, top + 8, x1 - 8, bottom - 8))
        tones.append(round(sum(band.getdata()) / (band.width * band.height)))
    return tones


class SleepCoverTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-sleep-cover-v1014-')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)

    def make_sd(self, name, mode, state=None, settings=None):
        sd = self.root / name
        store = sd / '.crosspoint'
        store.mkdir(parents=True)
        (store / 'settings.json').write_text(json.dumps(truoc_tenor(dict({'language': 'VI', 'sleepScreen': mode}, **(settings or {})))))
        (store / 'state.json').write_text(json.dumps(dict({'showBootScreen': False}, **(state or {}))))
        return sd

    def sleep_once(self, sd, label, script=None, sleep_at=SLEEP_AT):
        times = [sleep_at + 50 * i for i in range(1, 90)]
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=script or f'{SLEEP_AT}:SLEEP;{SLEEP_AT + 5000}:QUIT',
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{t}:{sd / f"shot-{t}.bmp"}' for t in times))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-6000:])
        self.assertIn('Entering deep sleep', log)
        sleep = log.split('Entering activity: Sleep', 1)[1].split('Entering deep sleep', 1)[0]
        taken = [sd / f'shot-{t}.bmp' for t in times if (sd / f'shot-{t}.bmp').exists()]
        self.assertTrue(taken, 'no screenshot taken')
        image = Image.open(taken[-1]).convert('L')
        if SHOTS:
            Path(SHOTS).mkdir(parents=True, exist_ok=True)
            image.save(Path(SHOTS) / f'{label}.png')
        return log, sleep, image

    def test_cover_sleep_keeps_the_cover_tones(self):
        sd = self.make_sd('bia', 3, {'openEpubPath': BOOK})
        write_epub(sd / BOOK.lstrip('/'), tone_cover())
        log, sleep, image = self.sleep_once(sd, 'bia-dai-tong')
        self.assertEqual(sum(1 for p in image.getdata() if 0 < p < 255), 0, 'gray left on the glass')
        seen = band_tones(image, (0, 0, 528, 792), len(TONES))
        drawn = re.findall(r'Sleep image (\d+x\d+)', sleep)
        print('SLEEP_COVER_TONES', list(zip(TONES, seen)), 'image', drawn)
        for tone, value in zip(TONES, seen):
            if tone in (64, 128, 192):
                self.assertLessEqual(abs(value - tone), 16, list(zip(TONES, seen)))
        # The black and white cover is the screen's own size: drawn 1:1.
        self.assertEqual(drawn, ['528x792'], sleep)
        self.assertEqual(len(re.findall(r'\[BMP\] Timing bpp=1 ', sleep)), 1, sleep)

    def quote_after_reading(self, label):
        """The book is opened and closed once (its card thumbnail is written), then the device sleeps
        on the quote screen with one quote of it."""
        sd = self.make_sd('trich-dan', 10, settings={'uiTheme': 4})
        write_epub(sd / BOOK.lstrip('/'), tone_cover())
        (sd / '.crosspoint/recent.json').write_text(json.dumps(
            {'books': [{'path': BOOK, 'title': TITLE, 'author': 'Tenor', 'coverBmpPath': ''}]}, ensure_ascii=False))
        record = {'schema': 1, 'path': BOOK, 'title': TITLE, 'text': 'Một câu trích để thử.', 'spine': 0, 'page': 0,
                  'day': 20260925, 'gio': 480}
        name = f'{quote_id(record, 0):016x}'
        quotes = sd / '.crosspoint/quotes'
        quotes.mkdir()
        (quotes / '.ten-v2').write_text('2')
        (quotes / f'{name}.json').write_text(json.dumps(record, ensure_ascii=False))
        log, sleep, image = self.sleep_once(sd, label, '1500:CONFIRM;6500:BACK;9000:SLEEP;14000:QUIT', 9000)
        return sd, name, log, sleep, image

    def test_quote_tile_comes_from_the_card_thumbnail(self):
        sd, _, log, sleep, image = self.quote_after_reading('trich-dan-dai-tong')
        self.assertTrue(list((sd / '.crosspoint').glob('epub_*/thumb*_356.bmp')), 'no card thumbnail\n' + log[-4000:])
        seen = band_tones(image, (COVER_BOX[0], COVER_BOX[1], COVER_BOX[2], COVER_BOX[3]), len(TONES))
        tile = re.findall(r'Sleep quote tile .*', sleep)
        print('SLEEP_QUOTE_TILE', list(zip(TONES, seen)), tile, re.findall(r'\[BMP\] Timing.*', sleep))
        # No cover is decoded at sleep, and the 4-level sleep cover is not read.
        self.assertNotIn('Sleep quote cover made', sleep, 'a cover was decoded at sleep')
        self.assertNotIn('[BMP] Timing bpp=2', sleep, 'the 4-level sleep cover was read')
        self.assertEqual(len(tile), 1, 'no tile line')
        for tone, value in zip(TONES, seen):
            if tone in (64, 128, 192):
                self.assertLessEqual(abs(value - tone), 16, list(zip(TONES, seen)))

    def test_sleep_screen_writes_no_state_of_its_own(self):
        source = (REPO / 'src/activities/boot_sleep/SleepActivity.cpp').read_text()
        self.assertFalse('saveToFile' in source, 'the sleep screen saves the device state itself')
        sd, name, log, _, _ = self.quote_after_reading('trich-dan-luu-trang-thai')
        state = json.loads((sd / '.crosspoint/state.json').read_text())
        shown = int(name, 16)
        self.assertEqual((state.get('lastSleepQuoteHi'), state.get('lastSleepQuoteLo')),
                         (shown >> 32, shown & 0xFFFFFFFF), 'the quote shown is not on the card after the sleep')


if __name__ == '__main__':
    unittest.main()
