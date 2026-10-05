"""Reader status bar: chapter name, chapter page count and book percentage switch on and off one by one.

Battery and clock keep following the reader status bar mode. The evidence is the
simulator frame: ink columns of the bottom band, split into clusters wherever at
least GAP blank columns separate them. The battery or the clock is the cluster that
touches a screen edge, the chapter name starts right of the left corner block, and
the counts end left of the right corner block.
"""

import json
import os
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image
from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
ARTIFACTS = os.environ.get('STATUS_ITEMS_ARTIFACTS') or os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
ARTIFACTS = Path(ARTIFACTS) if ARTIFACTS else None

# Corner blocks sit 8 px in from the edge; content keeps 14 px from them. A gap of
# 10 blank columns is wider than any space inside a word group or the battery.
GAP = 10
BAND = 80  # bottom rows: the fixture's single paragraph ends far above
SHORT_TITLE = 'Mở đầu'
LONG_TITLE = ('Chương mười hai: người giữ đèn ở cuối con đường mưa phùn '
              'kéo dài qua ba mùa gió chướng và một mùa nước nổi')
DEFAULT = 2
TITLE, PAGES, PERCENT = 1, 2, 4

CONTAINER = '''<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>'''
OPF = '''<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Status items fixture</dc:title>
<dc:identifier id="id">status-items-{key}</dc:identifier><dc:language>vi</dc:language></metadata>
<manifest><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
<item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest>
<spine toc="ncx"><itemref idref="body"/></spine></package>'''
NCX = '''<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
<head><meta name="dtb:uid" content="status-items-{key}"/></head><docTitle><text>Status items fixture</text></docTitle>
<navMap><navPoint id="p1" playOrder="1"><navLabel><text>{title}</text></navLabel>
<content src="body.xhtml"/></navPoint></navMap></ncx>'''
BODY = '''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Status items fixture</title></head>
<body><p>Sáng nay trời trong. Anchor line.</p></body></html>'''


def write_epub(path: Path, key: str, title: str) -> None:
    with zipfile.ZipFile(path, 'w') as archive:
        archive.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        archive.writestr('META-INF/container.xml', CONTAINER)
        archive.writestr('book.opf', OPF.format(key=key))
        archive.writestr('toc.ncx', NCX.format(key=key, title=title))
        archive.writestr('body.xhtml', BODY)


def items(mask):
    return {'statusBarTitle': 1 if mask & TITLE else 2,
            'statusBarChapterPageCount': 1 if mask & PAGES else 0,
            'statusBarBookProgressPercentage': 1 if mask & PERCENT else 0}


class ReaderStatusItemsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cross-status-items-')
        self.sd = Path(self.tmp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'books').mkdir()
        write_epub(self.sd / 'books/short.epub', 'short', SHORT_TITLE)
        write_epub(self.sd / 'books/long.epub', 'long', LONG_TITLE)
        self.shots = self.sd / 'shots'
        self.shots.mkdir()
        if ARTIFACTS:
            ARTIFACTS.mkdir(parents=True, exist_ok=True)

    def tearDown(self):
        self.tmp.cleanup()

    def run_sim(self, name, settings, script=None, shot=3400, book='short'):
        st = {'language': 'VI', 'fontSize': 14, 'statusBarClock': 1}
        st.update(settings)
        (self.store / 'settings.json').write_text(json.dumps(truoc_tenor(st)))
        (self.store / 'recent.json').write_text(
            json.dumps({'books': [{'path': f'/books/{book}.epub', 'title': 'Status items fixture'}]}))
        state = self.store / 'state.json'
        if state.exists():
            state.unlink()
        script = script or '1000:CONFIRM;4000:QUIT'
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=f'{shot}:{self.shots}/{name}.bmp')
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
        log = run.stdout + run.stderr
        if ARTIFACTS:
            if (self.shots / f'{name}.bmp').exists():
                Image.open(self.shots / f'{name}.bmp').save(ARTIFACTS / f'{name}.png')
            (ARTIFACTS / f'{name}.log').write_text(log)
        self.assertEqual(run.returncode, 0, log)
        return log

    def clusters(self, name):
        img = np.array(Image.open(self.shots / f'{name}.bmp').convert('L'))
        ink = img < 128 if (img < 128).sum() < img.size * 0.5 else img > 128
        band = ink[-BAND:]
        width = img.shape[1]
        cols = [x for x in range(width) if band[:, x].any()]
        runs = []
        for x in cols:
            if runs and x - runs[-1][1] <= GAP:
                runs[-1][1] = x
            else:
                runs.append([x, x])
        left = runs.pop(0) if runs and runs[0][0] <= 12 else None
        right = runs.pop() if runs and runs[-1][1] >= width - 13 else None
        return {'left': left, 'right': right, 'middle': runs, 'width': width, 'cols': cols}

    def bar(self, name, settings, **kw):
        self.run_sim(name, settings, **kw)
        return self.clusters(name)

    def test_each_switch_changes_only_its_value(self):
        """Every combination of the three switches, battery and clock always kept."""
        seen = {}
        for mask in range(8):
            c = self.bar(f'items-{mask}', {'readerStatusBarMode': DEFAULT, 'statusBarItemsMode': DEFAULT,
                                            **items(mask)})
            seen[mask] = c
            self.assertIsNotNone(c['left'], (mask, c))
            self.assertIsNotNone(c['right'], (mask, c))
            limit_left = c['left'][1] + 14
            limit_right = c['right'][0] - 14
            title = [r for r in c['middle'] if abs(r[0] - limit_left) <= 3]
            counts = [r for r in c['middle'] if abs(r[1] - limit_right) <= 3]
            self.assertEqual(bool(title), bool(mask & TITLE), (mask, c))
            self.assertEqual(bool(counts), bool(mask & (PAGES | PERCENT)), (mask, c))
            if mask == 0:
                self.assertEqual(c['middle'], [], f'all three off leaves battery and clock only: {c}')
        if ARTIFACTS:
            (ARTIFACTS / 'items-clusters.json').write_text(json.dumps(seen, indent=1) + '\n')

        def span(mask, which):
            c = seen[mask]
            if which == 'counts':
                r = [r for r in c['middle'] if abs(r[1] - (c['right'][0] - 14)) <= 3][0]
            else:
                r = [r for r in c['middle'] if abs(r[0] - (c['left'][1] + 14)) <= 3][0]
            return r[1] - r[0]
        self.assertGreater(span(PAGES | PERCENT, 'counts'), span(PAGES, 'counts'))
        self.assertGreater(span(PAGES | PERCENT, 'counts'), span(PERCENT, 'counts'))
        self.assertNotEqual(span(PAGES, 'counts'), span(PERCENT, 'counts'))
        # With no number to its right the chapter name drops its trailing colon.
        self.assertLess(span(TITLE, 'title'), span(TITLE | PAGES, 'title'))

    def test_long_chapter_name_never_reaches_battery_or_clock(self):
        """Every UI text size, clock on either side, with and without counts."""
        for size in (0, 1, 2):
            for clock in (1, 2):
                base = {'readerStatusBarMode': DEFAULT, 'statusBarItemsMode': DEFAULT, 'uiTextSize': size,
                        'statusBarClock': clock}
                ref = self.bar(f'long-ref-{size}-{clock}', {**base, **items(0)}, book='long')
                # The counts alone, to know how wide they are at this size.
                only = self.bar(f'long-counts-{size}-{clock}', {**base, **items(PAGES | PERCENT)}, book='long')
                counts_width = only['middle'][-1][1] - only['middle'][0][0]
                for mask in (TITLE, TITLE | PAGES | PERCENT):
                    name = f'long-{size}-{clock}-{mask}'
                    c = self.bar(name, {**base, **items(mask)}, book='long')
                    with self.subTest(name=name):
                        # The battery block is exactly as wide as without the name: nothing merged
                        # into it. The clock block changes with the minute between runs, so its side
                        # is held by the GAP checks below instead.
                        if clock == 1:
                            self.assertLessEqual(abs(c['left'][1] - ref['left'][1]), 3, (ref, c))
                        else:
                            self.assertLessEqual(abs(c['right'][0] - ref['right'][0]), 3, (ref, c))
                        self.assertTrue(c['middle'], c)
                        first = c['middle'][0][0]
                        last = c['middle'][-1][1]
                        self.assertGreaterEqual(first - c['left'][1], GAP, c)
                        self.assertGreaterEqual(c['right'][0] - last, GAP, c)
                        # The name is cut to the room it has, so it runs right up to the limit:
                        # the counts when they show, the right corner block otherwise. Word
                        # spaces can be wider than GAP, so this reads columns.
                        right_limit = c['right'][0] - 14
                        if mask == TITLE:
                            self.assertGreaterEqual(last, right_limit - 40, c)
                        else:
                            counts_start = min(x for x in c['cols'] if x >= right_limit - counts_width - 3)
                            name_end = max(x for x in c['cols'] if c['left'][1] < x < counts_start)
                            self.assertGreaterEqual(counts_start - name_end - 1, 4, (counts_start, name_end, c))
                            self.assertGreaterEqual(name_end, counts_start - 45, (counts_start, name_end, c))

    def test_legacy_modes_keep_what_they_showed(self):
        """A file from before the switches, carrying switch values that disagree with its mode."""
        old = {1: (True, True, False, False), 2: (True, True, True, True), 3: (False, False, True, True),
               4: (False, True, True, False), 5: (True, False, True, False)}
        for mode, (battery, clock, title, counts) in old.items():
            stale = items(0 if title else TITLE | PAGES | PERCENT)
            name = f'legacy-{mode}'
            self.run_sim(name, {'readerStatusBarMode': mode, **stale})
            c = self.clusters(name)
            with self.subTest(mode=mode):
                self.assertEqual(c['left'] is not None, battery, c)
                self.assertEqual(c['right'] is not None, clock, c)
                width = c['width']
                self.assertEqual(any(r[0] < width // 2 for r in c['middle']), title, c)
                self.assertEqual(any(r[1] > width // 2 for r in c['middle']), counts, c)
                saved = json.loads((self.store / 'settings.json').read_text())
                self.assertEqual(saved.get('statusBarItemsMode'), mode, saved)
                self.assertEqual(saved['statusBarTitle'] != 2, title, saved)

    def test_switch_screen_from_reader_settings(self):
        """Home's Reader group, last row (one step back from the first) opens the switches; the chapter name switch hides the name."""
        keys = ['UP', 'RIGHT', 'RIGHT', 'CONFIRM', 'LEFT', 'CONFIRM']
        events = [f'{1000 + 600 * i}:{k}' for i, k in enumerate(keys)]
        t = 1000 + 600 * len(keys)
        script = ';'.join(events + [f'{t + 800}:CONFIRM', f'{t + 2400}:BACK', f'{t + 3200}:BACK',
                                    f'{t + 4000}:QUIT'])
        st = {'language': 'VI', 'statusBarClock': 1, 'readerStatusBarMode': DEFAULT}
        (self.store / 'settings.json').write_text(json.dumps(truoc_tenor(st)))
        (self.store / 'state.json').write_text(json.dumps({'openEpubPath': '', 'lastSleepFromReader': False,
                                                           'showBootScreen': False}))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=f'{t + 400}:{self.shots}/screen-before.bmp;'
                                              f'{t + 2000}:{self.shots}/screen-after.bmp')
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
        log = run.stdout + run.stderr
        if ARTIFACTS:
            for shot in ('screen-before', 'screen-after'):
                if (self.shots / f'{shot}.bmp').exists():
                    Image.open(self.shots / f'{shot}.bmp').save(ARTIFACTS / f'{shot}.png')
            (ARTIFACTS / 'screen.log').write_text(log)
        self.assertEqual(run.returncode, 0, log)
        self.assertIn('Entering activity: StatusBarSettings', log)
        saved = json.loads((self.store / 'settings.json').read_text())
        self.assertEqual(saved['statusBarTitle'], 2, saved)
        self.assertEqual(saved['statusBarItemsMode'], DEFAULT, saved)
        self.assertEqual(saved['statusBarChapterPageCount'], 1, saved)
        self.assertEqual(saved['statusBarBookProgressPercentage'], 1, saved)
        self.assertEqual(saved['readerStatusBarMode'], DEFAULT, saved)


if __name__ == '__main__':
    unittest.main()
