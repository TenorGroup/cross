"""Every sleep screen on the X3 simulator ends on one B/W full refresh (v1.0.12).

On X3 the panel keeps the sleep screen for hours with no power at all. Black and white
driven by a full (GC) refresh hold that long; gray levels set by a single gray waveform
pass drift, which reads as a dark smear. So on X3 the last panel operation before deep
sleep must be `displayBuffer, mode=0` (FULL_REFRESH), and gray art reaches the glass as
an ordered dither of black and white pixels.

Evidence read here, nothing assumed:
  - firmware log between `Entering activity: Sleep` and `Entering deep sleep`: every
    panel call the renderer makes (`displayBuffer, mode=<m>`,
    `displayGrayscaleBase, mode=<m>`, `displayGrayBuffer`);
  - the screenshot taken at the deep-sleep present, i.e. the frame left on the glass.

The switch "Black and white refresh before sleep" (settings key `sleepBwRefresh`, v1.0.12)
chooses this path. It is on when settings.json has no such key; off gives back the v1.0.11
panel sequence unchanged.

The black and white passes run once the sleep image is complete in RAM (v1.0.13): the reader
sees black, white and the image back to back instead of a white panel while a cover decodes.
A gray cover is decoded once and dithered as it is drawn, not once per gray plane.

Fixtures are made up here: a four-tone cover drawn with PIL, an invented title and quote.
Set SLEEP_BW_SHOTS to a directory to keep the screenshots as PNG. Set SLEEP_BASE_PROGRAM to an
earlier simulator_x3_uc8279 build to check that every sleep image keeps the same pixels.
"""

import io
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
SHOTS = os.environ.get('SLEEP_BW_SHOTS')
SLEEP_AT = 2500
BOOK = '/books/vuon-sau-nha.epub'
TITLE = 'Khu vườn sau nhà'
QUOTE = 'Buổi chiều, mấy chậu húng quế ngoài hiên lại thơm như chưa từng bị ai hái.'
PANEL_OP = re.compile(r'displayBuffer, mode=\d|displayGrayscaleBase, mode=\d|displayGrayBuffer')
CLEAR_STEP = re.compile(r'\[SLP\] clear (?:black|white)|displayBuffer, mode=\d|displayGrayscaleBase, mode=\d|displayGrayBuffer')
COVER_BOX = (48, 596, 144, 741)  # SleepQuoteLayout.h, the cover tile of the quote screen


def cover_jpeg():
    """Four flat tones, one per panel level, so the dither has every level to show. The cover
    converter's own curve puts 35 on level 1 and 90 on level 2 (measured on this simulator)."""
    im = Image.new('L', (600, 900), 255)
    d = ImageDraw.Draw(im)
    for i, tone in enumerate((0, 35, 90, 255)):
        d.rectangle((0, i * 225, 600, i * 225 + 224), fill=tone)
    out = io.BytesIO()
    im.convert('RGB').save(out, 'JPEG', quality=95)
    return out.getvalue()


def write_epub(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>' + TITLE + '</dc:title><dc:identifier id="id">sleep-bw</dc:identifier><dc:language>vi</dc:language><meta name="cover" content="cover"/></metadata><manifest><item id="cover" href="cover.jpg" media-type="image/jpeg"/><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
        z.writestr('cover.jpg', cover_jpeg())
        z.writestr('body.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>t</title></head><body><p>' + 'Một đoạn văn thử. ' * 200 + '</p></body></html>')


def gray_bmp(width, height):
    """An 8-bit BMP of four vertical bands; its palette is the panel's own four levels, so the
    reader maps each band straight to one level."""
    row = bytes(x * 4 // width for x in range(width))
    row += b'\0' * (-len(row) % 4)
    palette = b''.join(bytes([v, v, v, 0]) for v in (0, 85, 170, 255))
    offset = 54 + len(palette)
    header = struct.pack('<2sIHHI', b'BM', offset + len(row) * height, 0, 0, offset)
    header += struct.pack('<IiiHHIIiiII', 40, width, height, 1, 8, 0, len(row) * height, 0, 0, 4, 0)
    return header + palette + row * height


def book_key(path):
    h = 2166136261
    for c in path.encode('utf-8'):
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


def quote_name(record):
    year, month, date = record['day'] // 10000, record['day'] // 100 % 100, record['day'] % 100
    day = (year - 2020) * 372 + (month - 1) * 31 + (date - 1)
    return f"{book_key(record['path']) << 32 | day << 16 | record['gio'] << 4:016x}.json"


class SleepEndsBwTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='cross-sleep-bw-')
        cls.root = Path(cls.temp.name)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def make_sd(self, name, mode, state=None, settings=None):
        sd = self.root / name
        store = sd / '.crosspoint'
        store.mkdir(parents=True)
        (store / 'settings.json').write_text(json.dumps(dict({'language': 'VI', 'sleepScreen': mode}, **(settings or {}))))
        (store / 'state.json').write_text(json.dumps(dict({'showBootScreen': False}, **(state or {}))))
        return sd

    def run_sim(self, sd, script, shots, program=None):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=shots)
        run = subprocess.run([str(program or PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=40)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log)
        return log

    def sleep_once(self, sd, label, program=None):
        times = [SLEEP_AT + 50 * i for i in range(1, 90)]
        log = self.run_sim(sd, f'{SLEEP_AT}:SLEEP;{SLEEP_AT + 5000}:QUIT',
                           ';'.join(f'{t}:{sd / f"shot-{t}.bmp"}' for t in times), program)
        self.assertIn('Entering activity: Sleep', log)
        self.assertIn('Entering deep sleep', log)
        sleep = log.split('Entering activity: Sleep', 1)[1].split('Entering deep sleep', 1)[0]
        taken = [sd / f'shot-{t}.bmp' for t in times if (sd / f'shot-{t}.bmp').exists()]
        self.assertTrue(taken, 'no screenshot taken')
        image = Image.open(taken[-1]).convert('L')
        self.assertEqual(image.size, (528, 792))
        if SHOTS:
            Path(SHOTS).mkdir(parents=True, exist_ok=True)
            image.save(Path(SHOTS) / f'{label}.png')
        return log, sleep, image

    def assert_ends_on_bw_full(self, sleep, clears=True, blank=False):
        ops = PANEL_OP.findall(sleep)
        # The switch promises a refresh the reader can see. A GC pass on this panel only drives
        # the pixels that change, so the page underneath ghosts into a sleep image painted over it.
        # A screen that paints a new image therefore first drives every pixel black, then white.
        # Quick resume and transparent keep the page itself, so they paint straight away.
        steps = [m.group(0) for m in CLEAR_STEP.finditer(sleep)]
        expected = ['[SLP] clear black', 'displayBuffer, mode=0', '[SLP] clear white', 'displayBuffer, mode=0']
        if clears:
            self.assertEqual(steps[:4], expected, steps)
            # The image is complete before the first pass: black, white and the image follow each
            # other with no decoding in between, so the panel never sits white while a cover loads.
            before, after = sleep.split('[SLP] clear black', 1)
            self.assertNotIn('[BMP] Timing', after)
            # The blank screen is what the white pass leaves: a third pass would change nothing.
            self.assertEqual(steps, expected + ([] if blank else ['displayBuffer, mode=0']), steps)
            if not blank:
                self.assertIn('[SLP] Timing frame-ready', before)
        else:
            self.assertNotIn('[SLP] clear black', steps, steps)
        self.assertTrue(ops, sleep)
        self.assertEqual(ops[-1], 'displayBuffer, mode=0', ops)
        # No gray waveform anywhere in the sleep path.
        self.assertNotIn('displayGrayBuffer', ops)
        self.assertEqual([op for op in ops if op.startswith('displayGrayscaleBase')], [])

    @staticmethod
    def grays(image, box=None):
        return sum(1 for p in (image.crop(box) if box else image).getdata() if 0 < p < 255)

    @staticmethod
    def dither_blocks(image, box):
        """Aligned 4x4 cells whose white count is a Bayer level: 4 of 16 (dark), 12 of 16 (light)."""
        found = {4: 0, 12: 0}
        px = image.load()
        x0, y0 = box[0] // 4 * 4, box[1] // 4 * 4
        for y in range(y0, box[3] - 3, 4):
            for x in range(x0, box[2] - 3, 4):
                white = sum(px[x + i, y + j] == 255 for i in range(4) for j in range(4))
                if white in found:
                    found[white] += 1
        return found

    def cover_cache(self):
        """One real Cover sleep makes the cover cache the quote screen reads."""
        if getattr(type(self), 'cover_sd', None) is None:
            sd = self.make_sd('cover', 3, {'openEpubPath': BOOK})
            write_epub(sd / BOOK.lstrip('/'))
            type(self).cover_sd = sd
            type(self).cover_run = self.sleep_once(sd, 'bia-sach')
            self.assertTrue(list((sd / '.crosspoint').glob('epub_*/cover_*.bmp')))
        return type(self).cover_sd, type(self).cover_run

    def test_tenor_screen_is_a_dithered_bw_frame(self):
        log, sleep, image = self.sleep_once(self.make_sd('tenor', 8), 'man-ngu-tenor')
        self.assertIn('[BRAND] sleep ready=1', sleep)
        self.assert_ends_on_bw_full(sleep)
        self.assertEqual(self.grays(image), 0)
        found = self.dither_blocks(image, (0, 0, 528, 792))
        self.assertGreater(found[4] + found[12], 200, found)

    def quote_sd(self, name, settings=None):
        cover_sd, _ = self.cover_cache()
        record = {'schema': 1, 'path': BOOK, 'title': TITLE, 'text': QUOTE, 'spine': 1, 'page': 3,
                  'day': 20260923, 'gio': 700}
        sd = self.make_sd(name, 10, settings=settings)
        (sd / '.crosspoint/quotes').mkdir()
        (sd / '.crosspoint/quotes/.ten-v2').write_text('2')
        (sd / '.crosspoint/quotes' / quote_name(record)).write_text(json.dumps(record, ensure_ascii=False))
        for cache in (cover_sd / '.crosspoint').glob('epub_*'):
            shutil.copytree(cache, sd / '.crosspoint' / cache.name)
        return sd

    def test_quote_screen_dithers_its_cover(self):
        log, sleep, image = self.sleep_once(self.quote_sd('quote'), 'man-ngu-trich-dan')
        self.assertIn('Sleep quote gray ready=1', sleep)
        self.assert_ends_on_bw_full(sleep)
        self.assertEqual(self.grays(image), 0)
        found = self.dither_blocks(image, COVER_BOX)
        self.assertGreater(found[4], 10, found)
        self.assertGreater(found[12], 10, found)

    def test_cover_screen_dithers(self):
        _, (log, sleep, image) = self.cover_cache()
        self.assert_ends_on_bw_full(sleep)
        self.assertEqual(self.grays(image), 0)
        found = self.dither_blocks(image, (0, 0, 528, 792))
        self.assertGreater(found[4], 100, found)
        self.assertGreater(found[12], 100, found)

    def test_gray_cover_is_decoded_once(self):
        # Each decode of the cover reads the whole file from the card; on the X3 that is most of a
        # second per pass. The dithered frame needs the cover's levels only once.
        _, (_, cover_sleep, _) = self.cover_cache()
        _, quote_sleep, _ = self.sleep_once(self.quote_sd('quote-once'), 'trich-dan-mot-lan')
        sd = self.make_sd('custom-once', 2)
        (sd / 'sleep.bmp').write_bytes(gray_bmp(528, 792))
        _, custom_sleep, _ = self.sleep_once(sd, 'anh-rieng-mot-lan')
        for name, sleep in (('cover', cover_sleep), ('quote', quote_sleep), ('custom', custom_sleep)):
            with self.subTest(screen=name):
                self.assertEqual(len(re.findall(r'\[BMP\] Timing bpp=', sleep)), 1, sleep)

    @unittest.skipUnless(os.environ.get('SLEEP_BASE_PROGRAM'), 'set SLEEP_BASE_PROGRAM to an earlier build')
    def test_same_pixels_as_base(self):
        # The same sleep, on the same fixtures, from the earlier build and from this one: the image
        # left on the glass must match pixel for pixel, switch on and off.
        base = Path(os.environ['SLEEP_BASE_PROGRAM'])
        cases = [(mode, {}) for mode in (0, 1, 2, 3, 5, 8, 9, 10)]
        cases += [(mode, {'sleepBwRefresh': 0}) for mode in (2, 3, 8, 10)]
        for mode, settings in cases:
            with self.subTest(sleepScreen=mode, settings=settings):
                images = []
                for side, program in (('truoc', base), ('sau', None)):
                    name = f'giong-{mode}-{len(settings)}-{side}'
                    if mode == 10:
                        sd = self.quote_sd(name, settings)
                    else:
                        sd = self.make_sd(name, mode, {'openEpubPath': BOOK} if mode == 3 else None, settings)
                    if mode == 2:
                        (sd / 'sleep.bmp').write_bytes(gray_bmp(528, 792))
                    if mode == 3:
                        write_epub(sd / BOOK.lstrip('/'))
                    images.append(self.sleep_once(sd, name, program)[2])
                self.assertEqual(images[0].tobytes(), images[1].tobytes())

    def test_every_other_mode_ends_on_bw_full(self):
        for mode in (0, 1, 2, 5, 6, 7, 9):
            with self.subTest(sleepScreen=mode):
                sd = self.make_sd(f'mode-{mode}', mode)
                if mode == 2:
                    (sd / 'sleep.bmp').write_bytes(gray_bmp(528, 792))
                if mode == 7:
                    (sd / 'sleep-overlay.bmp').write_bytes(gray_bmp(200, 200))
                log, sleep, image = self.sleep_once(sd, f'che-do-{mode}')
                self.assertIn(f'Sleep screen mode={mode},', log)
                self.assert_ends_on_bw_full(sleep, clears=mode not in (6, 7), blank=mode == 5)
                self.assertEqual(self.grays(image), 0)
                if mode == 2:
                    found = self.dither_blocks(image, (0, 0, 528, 792))
                    self.assertGreater(found[4], 100, found)
                    self.assertGreater(found[12], 100, found)

    @unittest.skipUnless(os.environ.get('SLEEP_UC8253_PROGRAM'), 'set SLEEP_UC8253_PROGRAM to a simulator_x3 build')
    def test_uc8253_paints_without_clear(self):
        # The black and white passes exist for the UC8279, whose GC pass drives only the pixels that
        # change. The earlier UC8253 X3 rewrites its previous-frame plane white on every full refresh,
        # so each such refresh already flashes the whole panel: extra passes there only add flashes.
        global PROGRAM
        saved, PROGRAM = PROGRAM, Path(os.environ['SLEEP_UC8253_PROGRAM'])
        try:
            sd = self.make_sd('uc8253-tenor', 8)
            log, sleep, image = self.sleep_once(sd, 'uc8253-tenor')
        finally:
            PROGRAM = saved
        self.assertNotIn('[SLP] clear', sleep)

    def test_switch_on_by_default_and_when_set(self):
        # The fixtures above write no sleepBwRefresh key: those runs are the default.
        for settings in ({}, {'sleepBwRefresh': 1}):
            with self.subTest(settings=settings):
                sd = self.make_sd(f'bat-{len(settings)}', 8, settings=settings)
                if not settings:
                    self.assertNotIn('sleepBwRefresh', (sd / '.crosspoint/settings.json').read_text())
                _, sleep, image = self.sleep_once(sd, f'bat-{len(settings)}')
                self.assert_ends_on_bw_full(sleep)
                self.assertEqual(self.grays(image), 0)

    def test_switch_off_keeps_the_v1011_waveforms(self):
        off = {'sleepBwRefresh': 0}
        # The whole panel sequence of v1.0.11 on this simulator, per mode: the Tenor and quote
        # screens run one GC pass to the image's B/W threshold and then the gray pass (on the UC8279
        # that GC pass leaves unchanged pixels undriven, so the page can still ghost with the
        # switch off; the switch is what buys the extra passes); quick resume adds the moon with
        # the soft base waveform; the blank screen is one HALF refresh.
        whole = {8: ['displayBuffer, mode=0', 'displayGrayBuffer'],
                 10: ['displayBuffer, mode=0', 'displayGrayBuffer'],
                 6: ['displayGrayscaleBase, mode=2'],
                 5: ['displayBuffer, mode=1']}
        for mode, expected in whole.items():
            with self.subTest(sleepScreen=mode):
                sd = self.quote_sd('tat-10', off) if mode == 10 else self.make_sd(f'tat-{mode}', mode, settings=off)
                _, sleep, image = self.sleep_once(sd, f'tat-che-do-{mode}')
                self.assertEqual(PANEL_OP.findall(sleep), expected)
                if mode in (8, 10):
                    self.assertGreater(self.grays(image), 1000)
        for mode in (2, 3, 7):
            with self.subTest(sleepScreen=mode):
                sd = self.make_sd(f'tat-{mode}', mode, {'openEpubPath': BOOK} if mode == 3 else None, off)
                if mode == 2:
                    (sd / 'sleep.bmp').write_bytes(gray_bmp(528, 792))
                if mode == 3:
                    write_epub(sd / BOOK.lstrip('/'))
                if mode == 7:
                    (sd / 'sleep-overlay.bmp').write_bytes(gray_bmp(200, 200))
                _, sleep, _ = self.sleep_once(sd, f'tat-che-do-{mode}')
                self.assertEqual(PANEL_OP.findall(sleep)[-1], 'displayGrayBuffer')

    def test_settings_row_turns_the_switch_off(self):
        # Settings, Sleep: the switch is the sixth row, after "Wake into the book", and is on.
        sd = self.make_sd('cai-dat', 8, settings={'uiTheme': 4})
        shot = sd / 'hang.bmp'
        after = sd / 'sau-khi-bam.bmp'
        rows = ';'.join(f'{3200 + 400 * i}:RIGHT' for i in range(5))
        log = self.run_sim(sd, f'1000:UP;1500:RIGHT;2000:RIGHT;2500:CONFIRM;{rows};5600:CONFIRM;7600:QUIT',
                           f'5400:{shot};7000:{after}')
        for path, label in ((shot, 'cai-dat-hang-moi'), (after, 'cai-dat-hang-moi-da-tat')):
            image = Image.open(path).convert('L')
            if SHOTS:
                Path(SHOTS).mkdir(parents=True, exist_ok=True)
                image.save(Path(SHOTS) / f'{label}.png')
        saved = json.loads((sd / '.crosspoint/settings.json').read_text())
        self.assertEqual(saved.get('sleepBwRefresh'), 0, log)

if __name__ == '__main__':
    unittest.main()
