"""Reader ink acceptance against an explicitly frozen simulator and released packs.

Required: TEST_PROGRAM, TEST_PROGRAM_SHA256 and READER_INK_PACK (guarded-pack).
CROSSPOINT_TEST_ARTIFACTS retains logs, settings, image bounds and screenshots.
No simulator starts until its hash and the copied cpfont hashes are verified.
"""
import hashlib
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

from PIL import Image, ImageChops
from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PHYSICAL = (0, 2, 3, 4)
TEXT = ('Ậ Ễ Ố Đọc sách giữ dấu chữ rõ ràng. Người đọc gặp những câu chuyện gần gũi, '
        'nghe tiếng gió, nhìn ánh sáng và nhớ những ngày bình yên. q g p ộ ệ ữ. ')


def write_epub(path):
    with zipfile.ZipFile(path, 'w') as book:
        book.writestr('mimetype', 'application/epub+zip')
        book.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        book.writestr('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Ink fixture</dc:title><dc:identifier id="id">ink108</dc:identifier><dc:language>vi</dc:language></metadata><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx"><itemref idref="body"/></spine></package>')
        book.writestr('toc.ncx', '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head><meta name="dtb:uid" content="ink108"/><meta name="dtb:depth" content="1"/><meta name="dtb:totalPageCount" content="0"/><meta name="dtb:maxPageNumber" content="0"/></head><docTitle><text>Ink fixture</text></docTitle><navMap><navPoint id="chapter" playOrder="1"><navLabel><text>Ink</text></navLabel><content src="body.xhtml"/></navPoint></navMap></ncx>')
        book.writestr('body.xhtml', '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>Ink fixture</title></head><body><p>' + TEXT * 160 + '</p></body></html>')


def glyph_bounds(data):
    """Read actual regular glyph metrics for the fixture's accented alphabet."""
    result = {}
    for index in range(data[12]):
        toc = struct.unpack_from('<B3xIIBhhHHBBBIh2x', data, 32 + index * 32)
        if toc[0] != 0:
            continue
        intervals, count, start = toc[1], toc[2], toc[11]
        glyph_start = start + intervals * 12
        for i in range(intervals):
            low, high, first = struct.unpack_from('<III', data, start + i * 12)
            for cp in set(map(ord, TEXT)):
                if low <= cp <= high:
                    at = first + cp - low
                    if at >= count:
                        raise ValueError('Invalid glyph index')
                    w, h, advance, left, top, size, _ = struct.unpack_from('<BBHhhH2xI', data, glyph_start + at * 16)
                    result[str(cp)] = dict(width=w, height=h, advance=advance, left=left, top=top,
                                           bottom=h-top, bitmap_bytes=size)
    return result


def bounds(image):
    """Separate the reader body from the 22-pixel status ink region."""
    gray = image.convert('L')
    body = gray.crop((0, 0, gray.width, gray.height - 24))
    mask = body.point(lambda value: 255 if value < 250 else 0)
    box = mask.getbbox()
    bands = []
    start = None
    for y in range(body.height + 1):
        ink = y < body.height and mask.crop((0, y, body.width, y+1)).getbbox() is not None
        if ink and start is None:
            start = y
        elif not ink and start is not None:
            if bands and start - bands[-1][1] < 6:
                bands[-1][1] = y - 1
            else:
                bands.append([start, y-1])
            start = None
    histogram = body.histogram()
    return dict(body_bbox=box, line_bands=bands, bw_pixels=sum(histogram[:128]),
                coverage=sum((255-value)*n for value, n in enumerate(histogram)),
                body_sha256=hashlib.sha256(body.tobytes()).hexdigest())


class ReaderInkV108Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        required = ('TEST_PROGRAM', 'TEST_PROGRAM_SHA256', 'READER_INK_PACK')
        if any(not os.environ.get(key) for key in required):
            raise RuntimeError('Set ' + ', '.join(required) + ' before simulator acceptance')
        cls.program = Path(os.environ['TEST_PROGRAM']).resolve()
        cls.program_sha = os.environ['TEST_PROGRAM_SHA256']
        if hashlib.sha256(cls.program.read_bytes()).hexdigest() != cls.program_sha:
            raise RuntimeError('Frozen simulator hash mismatch')
        cls.pack = Path(os.environ['READER_INK_PACK']).resolve()
        cls.manifest = json.loads((cls.pack / 'font-manifest.json').read_text())
        cls.art = Path(os.environ.get('CROSSPOINT_TEST_ARTIFACTS',
                                     REPO.parent / 'research/reader-raster/simulator'))
        cls.art.mkdir(parents=True, exist_ok=True)
        (cls.art / 'binary.json').write_text(json.dumps(dict(path=str(cls.program), sha256=cls.program_sha), indent=2))

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='reader-ink108-')
        self.addCleanup(temporary.cleanup)
        self.sd = Path(temporary.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        self.output = self.art / self._testMethodName
        self.output.mkdir(parents=True, exist_ok=True)
        write_epub(self.sd / 'ink.epub')
        (self.sd / 'ink.txt').write_text((TEXT * 8 + '\n') * 40)
        self.recent('epub')
        self.settings = dict(language='VI', uiTheme=4, fontSize=16, sdFontFamilyName='Literata',
                             readerInkWeightVersion=1, readerInkWeight=0, textSpacingVersion=3,
                             paragraphIndentVersion=1, paragraphIndent=0, lineSpacing=0,
                             extraParagraphSpacing=0, dropCapMode=0, textAntiAliasing=1,
                             readerStatusBarMode=2, screenMargin=5, paragraphAlignment=0,
                             wordSpacing=0, letterSpacing=0, hyphenationEnabled=0,
                             statusBarClock=0, statusBarTitle=0, statusBarBattery=0,
                             statusBarChapterPageCount=1, sleepTimeout=120)
        metrics = {}
        for entry in self.manifest:
            if entry['family'] != 'Literata' or entry['size'] not in (16, 26):
                continue
            data = (self.pack / entry['path']).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), entry['sha256'])
            target = self.sd / entry['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            metrics[entry['path']] = glyph_bounds(data)
        self.assertEqual(len(metrics), 8, 'Fixture needs Literata 16/26 at physical 0/2/3/4')
        (self.output / 'glyph-bounds.json').write_text(json.dumps(metrics, indent=2))

    def recent(self, extension):
        (self.store / 'recent.json').write_text(json.dumps({'books': [dict(path=f'/ink.{extension}', title='Ink fixture')]}))

    def run_sim(self, name, script='1000:CONFIRM;5200:BACK;6100:QUIT', captures=((4300, 'page'),)):
        (self.store / 'settings.json').write_text(json.dumps(truoc_tenor(self.settings)))
        # Each process starts at Home; book progress/cache are retained separately.
        (self.store / 'state.json').write_text(json.dumps(dict(openEpubPath='', lastSleepFromReader=False,
                                                            showBootScreen=False, readerActivityLoadCount=0)))
        env = {key: value for key, value in os.environ.items() if not key.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{self.output / (name + "-" + label + ".bmp")}'
                                                       for ms, label in captures))
        run = subprocess.run([str(self.program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=50)
        log = run.stdout + run.stderr
        (self.output / (name + '.log')).write_text(log)
        (self.output / (name + '-input.txt')).write_text(script)
        self.assertEqual(hashlib.sha256(self.program.read_bytes()).hexdigest(), self.program_sha)
        self.assertEqual(run.returncode, 0, log[-8000:])
        self.assertNotIn('Warning: Could not parse any TOC format', log, 'Generated EPUB must include a readable NCX')
        self.settings = json.loads((self.store / 'settings.json').read_text())
        (self.output / (name + '-settings.json')).write_text(json.dumps(self.settings, indent=2))
        images = {}
        for _, label in captures:
            with Image.open(self.output / (name + '-' + label + '.bmp')) as raw:
                image = raw.convert('RGB')
            if image.width > image.height:
                image = image.rotate(90, expand=True)
            self.assertEqual(image.size, (528, 792))
            image.save(self.output / (name + '-' + label + '.png'))
            images[label] = image
        return images, log

    def assert_loaded(self, log, level, size=16):
        weight = PHYSICAL[level]
        path = f'/.fonts/Literata/' + (f'weight-{weight}/' if weight else '') + f'Literata_{size}.cpfont'
        match = re.search(r'Loaded ' + re.escape(path) + r' size=\d+ id=(-?\d+)', log)
        self.assertIsNotNone(match, path + '\n' + log[-7000:])
        self.assertEqual(self.settings['readerInkWeight'], level)
        self.assertEqual(self.settings['readerInkWeightVersion'], 1)
        return match.group(1)

    def assert_page(self, image):
        measured = bounds(image)
        self.assertTrue(measured['body_bbox'], 'Reader body is empty')
        left, top, right, bottom = measured['body_bbox']
        self.assertGreater(top, 0, 'First line touches top edge')
        self.assertGreater(left, 0, 'Ink touches left edge')
        self.assertLess(right, image.width, 'Ink touches right edge')
        self.assertLess(bottom, image.height - 24, 'Last line touches status boundary')
        self.assertGreaterEqual(len(measured['line_bands']), 5)
        return measured

    def test_public_levels_warm_cache_and_restart(self):
        evidence = []
        images = {}
        ids = {}
        for index, level in enumerate((0, 0, 1, 2, 3, 0)):
            self.settings['readerInkWeight'] = level
            captured, log = self.run_sim(f'aa-{index}-level{level}')
            self.assertIn('Entering activity: EpubReader', log)
            font_id = self.assert_loaded(log, level)
            if level in ids:
                self.assertEqual(font_id, ids[level])
            ids[level] = font_id
            measured = self.assert_page(captured['page'])
            evidence.append(dict(run=index, level=level, font_id=font_id, **measured))
            if level in images:
                self.assertEqual(measured['body_sha256'], images[level], 'Restart/cache changed body pixels')
            images[level] = measured['body_sha256']
            if index == 1:
                self.assertIn('Section cache HIT:', log)
                self.assertIn('Deserialization succeeded:', log)
        self.assertEqual(len(set(ids.values())), 4, 'Public levels collided in section-cache font ID')
        self.assertEqual(len(set(images.values())), 4, 'Four levels must produce distinct actual reader pixels')
        (self.output / 'measurements.json').write_text(json.dumps(evidence, indent=2))

    def test_bw_largest_size_first_and_last_line(self):
        self.settings.update(fontSize=26, textAntiAliasing=0)
        evidence = []
        for level in range(4):
            self.settings['readerInkWeight'] = level
            captured, log = self.run_sim(f'bw26-level{level}')
            self.assert_loaded(log, level, 26)
            evidence.append(dict(level=level, **self.assert_page(captured['page'])))
        self.assertEqual(len({entry['body_sha256'] for entry in evidence}), 4)
        # Advances are preserved; visible line ink may expand by one pixel.
        for entry in evidence[1:]:
            self.assertEqual(len(entry['line_bands']), len(evidence[0]['line_bands']))
        (self.output / 'measurements.json').write_text(json.dumps(evidence, indent=2))

    def test_ui_cycle_preview_return_and_restart(self):
        events = '1000:CONFIRM;2200:CONFIRM;2900:DOWN;3600:DOWN;4300:CONFIRM;5400:DOWN;'
        events += '6100:RIGHT;6800:RIGHT;7500:RIGHT;8200:RIGHT;'
        events += '9400:CONFIRM;10800:CONFIRM;12200:CONFIRM;13600:CONFIRM;15000:BACK;16600:BACK;17600:QUIT'
        captures = ((8900, 'preview0'), (10100, 'preview1'), (11500, 'preview2'),
                    (12900, 'preview3'), (14300, 'preview-return0'), (15900, 'reader-return0'))
        images, log = self.run_sim('cycle', events, captures)
        self.assertIn('Entering activity: TextSettings', log)
        self.assertIn('Exiting activity: TextSettings', log)
        self.assertEqual(self.settings['readerInkWeight'], 0)
        # Preview text lives above the settings tabs/list; omit captions and controls.
        panes = [images[f'preview{level}'].crop((16, 128, 512, 305)) for level in range(4)]
        self.assertEqual(len({hashlib.sha256(pane.tobytes()).hexdigest() for pane in panes}), 4,
                         'Changing ink must update the actual preview text')
        self.assertIsNone(ImageChops.difference(panes[0], images['preview-return0'].crop((16, 128, 512, 305))).getbbox())
        self.assert_page(images['reader-return0'])
        restarted, restart_log = self.run_sim('restart')
        self.assert_loaded(restart_log, 0)
        self.assertEqual(bounds(restarted['page'])['body_sha256'], bounds(images['reader-return0'])['body_sha256'])

    def test_ui_cycle_keeps_requested_missing_level(self):
        missing = self.sd / '.fonts/Literata/weight-3/Literata_16.cpfont'
        missing.unlink()
        self.settings['readerInkWeight'] = 1
        events = '1000:CONFIRM;2200:CONFIRM;2900:DOWN;3600:DOWN;4300:CONFIRM;5400:DOWN;'
        events += '6100:RIGHT;6800:RIGHT;7500:RIGHT;8200:RIGHT;9400:CONFIRM;10800:BACK;12200:BACK;13600:QUIT'
        images, log = self.run_sim('missing-cycle', events, ((8900, 'before'), (10100, 'requested')))
        self.assertIn('Entering activity: TextSettings', log)
        self.assertEqual(self.settings['readerInkWeight'], 2, log[-5000:])
        before = images['before'].crop((16, 128, 512, 305))
        requested = images['requested'].crop((16, 128, 512, 305))
        self.assertIsNotNone(ImageChops.difference(before, requested).getbbox(),
                             'Missing requested level must repaint the inline status')

    def test_missing_and_corrupt_new_variants_preserve_requested_level(self):
        for level in (2, 3):
            path = self.sd / '.fonts/Literata' / f'weight-{PHYSICAL[level]}' / 'Literata_16.cpfont'
            original = path.read_bytes()
            for condition in ('missing', 'corrupt'):
                if condition == 'missing':
                    path.unlink()
                else:
                    path.write_bytes(b'broken')
                self.settings['readerInkWeight'] = level
                _, log = self.run_sim(f'{condition}-{level}', '1800:QUIT', ())
                self.assertEqual(self.settings['readerInkWeight'], level)
                self.assertEqual(self.settings['sdFontFamilyName'], 'Literata')
                self.assertEqual(log.count('Loaded /.fonts/Literata/Literata_16.cpfont'), 1)
                path.write_bytes(original)
            _, log = self.run_sim(f'restored-{level}', '1800:QUIT', ())
            self.assert_loaded(log, level)

    def test_legacy_light_and_strong_migrate_once(self):
        for old in (1, 2):
            self.settings.pop('readerInkWeightVersion', None)
            self.settings['readerInkWeight'] = old
            _, log = self.run_sim(f'legacy-{old}', '1800:QUIT', ())
            self.assert_loaded(log, 1)
            _, log = self.run_sim(f'legacy-{old}-restart', '1800:QUIT', ())
            self.assert_loaded(log, 1)

    def test_txt_uses_each_public_level(self):
        self.recent('txt')
        measured = []
        for level in range(4):
            self.settings['readerInkWeight'] = level
            images, log = self.run_sim(f'txt-level{level}')
            self.assertIn('Entering activity: TxtReader', log)
            self.assert_loaded(log, level)
            measured.append(dict(level=level, **self.assert_page(images['page'])))
        self.assertEqual(len({entry['body_sha256'] for entry in measured}), 4)
        (self.output / 'measurements.json').write_text(json.dumps(measured, indent=2))

    def test_epub_dropcap_and_preview_use_existing_pack(self):
        self.settings['dropCapMode'] = 2
        script = ('1000:CONFIRM;5200:CONFIRM;5900:DOWN;6600:DOWN;7300:CONFIRM;'
                  '9300:BACK;10300:BACK;11500:QUIT')
        for level in (1, 3):
            self.settings['readerInkWeight'] = level
            images, log = self.run_sim(f'epub-dropcap-{level}', script, ((4300, 'page'), (8500, 'preview')))
            self.assert_loaded(log, level)
            self.assertEqual(self.settings['dropCapMode'], 2)
            self.assertIn('Entering activity: TextSettings', log)
            measured = self.assert_page(images['page'])
            (self.output / f'dropcap-{level}-bounds.json').write_text(json.dumps(measured, indent=2))


if __name__ == '__main__':
    unittest.main()
