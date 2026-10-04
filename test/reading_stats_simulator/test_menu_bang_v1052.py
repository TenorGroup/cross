"""v1.0.52: the reader toolbar's Text panel changes the font in place, saves once, and repaints once.

Reader Menu Style = Toolbar over an EPUB page, step to the Text tool, open its panel.
1. The Font row turns the Text sheet into the list of families (same frame); the page above stays the
   preview. It used to leave the book for the full Settings font screen.
2. Settings reach the card once, after the page that follows the panel's close is painted; they used to
   be written at every step.
3. With the page under an open sheet the panel is told once per change: one fast refresh for the page
   and the sheet together. The page's own push and its gray pass ran first and the sheet came after.
4. The Text rows change in place; the chapter is laid out once the presses stop.

Panel pushes are read from the simulator's panel trace (CROSSPOINT_SIM_PANEL_TRACE) and the card
writes from CROSSPOINT_SIM_SD_TRACE.
"""
import json
import hashlib
import os
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

from PIL import Image

from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
FONT = Path(__file__).resolve().parent / 'fixtures/BeVietnamPro_18.cpfont'
ARTIFACTS = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')

# Op codes of scripts/patch_simulator_panel_trace.py
OP_DISPLAY, OP_GRAY_BASE, OP_GRAY_BASE_MODE, OP_GRAY_DISPLAY = 2, 3, 4, 8
OP_NAMES = {1: 'BEGIN', 2: 'DISPLAY', 3: 'GRAY_BASE', 4: 'GRAY_BASE_MODE', 5: 'COPY_LSB', 6: 'COPY_MSB', 7: 'STRIP',
            8: 'GRAY_DISPLAY', 9: 'CLEANUP', 10: 'PRECONDITION', 11: 'DEEP_SLEEP', 12: 'SET_INVERTED'}
PUSHES = (OP_DISPLAY, OP_GRAY_BASE, OP_GRAY_BASE_MODE, OP_GRAY_DISPLAY)

CONTAINER = """<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>"""
OPF = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:title>Toolbar table fixture</dc:title><dc:identifier id="id">toolbar-table</dc:identifier>
<dc:language>en</dc:language></metadata>
<manifest><item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/>
<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>
<spine toc="ncx"><itemref idref="c1"/></spine></package>"""
NCX = """<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
<head><meta name="dtb:uid" content="toolbar-table"/></head>
<docTitle><text>Toolbar table fixture</text></docTitle>
<navMap><navPoint id="n1" playOrder="1"><navLabel><text>Chapter One</text></navLabel>
<content src="c1.xhtml"/></navPoint></navMap></ncx>"""
CHAPTER = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">
<head><title>Chapter One</title></head><body>{body}</body></html>"""

# Home opens the recent book (1500), Confirm opens the toolbar (3500), Right steps to Text (5000),
# Confirm opens the Text panel (6500).
OPEN_TEXT = [(1500, 'CONFIRM'), (3500, 'CONFIRM'), (5000, 'RIGHT'), (6500, 'CONFIRM')]
STEP = 1500  # ms between scripted presses after the panel is open


def read_trace(path):
    """[(op, a, b)] in the order the panel driver saw them."""
    data = Path(path).read_bytes() if Path(path).exists() else b''
    out, i = [], 0
    while i + 12 <= len(data):
        assert data[i:i + 1] == b'T', 'bad trace record'
        op, a, b, c, d, n = struct.unpack_from('<BBBHHI', data, i + 1)
        out.append((op, a, b))
        i += 12 + n
    return out


def run_sim(presses, shots=(), antialias=1, quit_after=2500, refresh_ms=0, extra_families=()):
    """presses: [(ms, KEY)]. shots: [(ms, label)]. Returns dict(log, trace, settings, writes, shots, sd)."""
    tmp = Path(tempfile.mkdtemp(prefix='menu-bang-'))
    store = tmp / '.crosspoint'
    store.mkdir()
    target = tmp / '.fonts/BeVietnamPro/BeVietnamPro_18.cpfont'
    target.parent.mkdir(parents=True)
    shutil.copy2(FONT, target)
    for name in extra_families:  # more families on the card, so the font list spans pages
        extra = tmp / f'.fonts/{name}/{name}_18.cpfont'
        extra.parent.mkdir(parents=True)
        shutil.copy2(FONT, extra)
    body = ''.join(f'<p>Body paragraph {i:02d} with plain words for a full page.</p>' for i in range(60))
    with zipfile.ZipFile(tmp / 'book.epub', 'w') as z:
        z.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml', CONTAINER)
        z.writestr('OEBPS/content.opf', OPF)
        z.writestr('OEBPS/toc.ncx', NCX)
        z.writestr('OEBPS/c1.xhtml', CHAPTER.format(body=body))
    settings = truoc_tenor({'language': 'EN', 'readerMenuStyle': 1, 'sdFontFamilyName': 'BeVietnamPro',
                            'fontSize': 18, 'textAntiAliasing': antialias, 'sleepTimeout': 120})
    (store / 'settings.json').write_text(json.dumps(settings) + '\n')
    (store / 'state.json').write_text(json.dumps({'openEpubPath': '/book.epub', 'lastSleepFromReader': False,
                                                  'showBootScreen': False, 'readerActivityLoadCount': 0}) + '\n')
    (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/book.epub', 'title': 'Toolbar'}]}) + '\n')
    script = ';'.join(f'{ms}:{key}' for ms, key in presses) + f';{presses[-1][0] + quit_after}:QUIT'
    trace = tmp / 'panel.trace'
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(tmp), CROSSPOINT_SIM_INPUT_SCRIPT=script,
               CROSSPOINT_SIM_PANEL_TRACE=str(trace), CROSSPOINT_SIM_SD_TRACE='1')
    if refresh_ms:
        env['CROSSPOINT_SIM_REFRESH_MS'] = str(refresh_ms)
    if shots:
        env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join(f'{ms}:{tmp}/{label}.bmp' for ms, label in shots)
    run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=90)
    log = run.stdout
    if ARTIFACTS:
        target = Path(ARTIFACTS)
        target.mkdir(parents=True, exist_ok=True)
        key = hashlib.sha256((script + str(antialias) + str(extra_families)).encode()).hexdigest()[:12]
        (target / f"menu-{key}.log").write_text(log)
        if trace.exists(): shutil.copy2(trace, target / f"menu-{key}.trace")
    assert run.returncode == 0, log[-3000:]
    images = {label: Image.open(tmp / f'{label}.bmp').convert('L') for _, label in shots}
    result = dict(log=log, trace=read_trace(trace), settings=json.loads((store / 'settings.json').read_text()),
                  writes=writes_after(log, len(OPEN_TEXT)), shots=images, tmp=tmp)
    return result


PRESS_AT = re.compile(r'^\[(\d+)\] \[INF\] \[IN\] press t=(\d+)', re.M)
PANEL_LINE = re.compile(r'^\[(\d+)\] \[DBG\] \[GFX\] (displayGrayscaleBase|displayGrayBuffer|Time = \d+ ms from clearScreen)', re.M)


def writes_after(log, press_index):
    """Card writes of settings.json after press number `press_index` (0 based), in log order."""
    marks = [m.start() for m in PRESS_AT.finditer(log)]
    tail = log[marks[press_index]:] if press_index < len(marks) else ''
    return len(re.findall(r'\[SDW\] write \S*settings\.json', tail))


def step_ms(log, index):
    """Press number `index` (0 based) to the last panel call before the next press, in simulator ms."""
    presses = [int(t) for _, t in PRESS_AT.findall(log)]
    start = presses[index]
    end = presses[index + 1] if index + 1 < len(presses) else start + 10**6
    calls = [int(t) for t, _ in PANEL_LINE.findall(log) if start <= int(t) < end]
    return (max(calls) - start) if calls else 0


def pushes(trace):
    return [OP_NAMES[op] for op, _, _ in trace if op in PUSHES]


def keep(label, img):
    if ARTIFACTS:
        Path(ARTIFACTS).mkdir(parents=True, exist_ok=True)
        img.save(Path(ARTIFACTS) / f'{label}.png')


def sheet_top(img):
    """y of the sheet's top edge: the first row from 150 down that is a full-width rule."""
    px = img.load()
    for y in range(150, img.height - 100):
        if sum(1 for x in range(img.width) if px[x, y] == 0) >= 0.9 * img.width:
            return y
    raise AssertionError('no sheet edge on screen')


EXTRA = ('AlphaSans', 'BetaSans', 'GammaSans', 'DeltaSans', 'EchoSans', 'FoxSans', 'GolfSans')  # 10 families with the 2 built in


class FontLevelTest(unittest.TestCase):
    """The Font row opens the family list inside the Text panel's own sheet; the page above is the preview."""

    @classmethod
    def setUpClass(cls):
        t = OPEN_TEXT[-1][0]
        n = iter(range(1, 100))
        at = lambda: t + next(n) * STEP
        # Run A: the rows, the list, the list one screen further (4 steps down).
        a = [(at(), 'CONFIRM')] + [(at(), 'DOWN') for _ in range(4)]
        cls.pages = run_sim(OPEN_TEXT + a, [(t + STEP - 300, 'rows'), (t + 2 * STEP - 300, 'list'),
                                             (t + 6 * STEP - 300, 'list2')], quit_after=2 * STEP, extra_families=EXTRA)
        # Run B: into the list, up to Noto Sans (the family is chosen with Confirm), back out, the panel closed.
        n = iter(range(1, 100))
        b = [(at(), 'CONFIRM'), (at(), 'UP'), (at(), 'UP'), (at(), 'CONFIRM'), (at(), 'BACK'), (at(), 'BACK'),
             (at(), 'BACK')]
        cls.chosen = run_sim(OPEN_TEXT + b, [(t + STEP - 300, 'rows'), (t + 2 * STEP - 300, 'list'),
                                              (t + 5 * STEP - 300, 'chosen'), (t + 6 * STEP - 300, 'back'),
                                              (t + 9 * STEP, 'closed')], quit_after=2 * STEP, extra_families=EXTRA)
        for label, res in (('a', cls.pages), ('b', cls.chosen)):
            for name, img in res['shots'].items():
                keep(f'font-{label}-{name}', img)
        # Panel pushes of each step: prefixes of run B, the later steps adding to the earlier.
        cls.steps = []
        prefixes = [b[:k] for k in range(0, len(b) + 1)]
        previous = None
        for prefix in prefixes[:6]:
            res = run_sim(OPEN_TEXT + prefix, quit_after=STEP, extra_families=EXTRA)
            pushed = pushes(res['trace'])
            cls.steps.append(pushed[len(previous):] if previous is not None else pushed)
            previous = pushed
            shutil.rmtree(res['tmp'], True)
        for res in (cls.pages, cls.chosen):
            shutil.rmtree(res['tmp'], True)

    def test_page_stays_and_the_sheet_keeps_its_frame(self):
        shots = self.pages['shots']
        top = (0, 0, shots['rows'].width, 120)
        for label in ('list', 'list2'):
            self.assertEqual(shots['rows'].crop(top).tobytes(), shots[label].crop(top).tobytes(), label)
            self.assertEqual(sheet_top(shots['rows']), sheet_top(shots[label]), f'{label}: same frame as the rows')
        sheet = lambda img: img.crop((0, sheet_top(img) + 20, img.width, img.height - 100)).tobytes()
        self.assertNotEqual(sheet(shots['rows']), sheet(shots['list']), 'the sheet now lists the families')
        self.assertNotEqual(sheet(shots['list']), sheet(shots['list2']), 'the list pages inside the sheet')

    def test_choice_lays_the_page_out_again_and_the_sheet_stays_on_the_list(self):
        shots = self.chosen['shots']
        top = (0, 0, shots['list'].width, 120)
        self.assertNotEqual(shots['list'].crop(top).tobytes(), shots['chosen'].crop(top).tobytes(),
                            'the page above the sheet is the preview in the new font')
        self.assertEqual(sheet_top(shots['chosen']), sheet_top(shots['list']))
        self.assertEqual(sheet_top(shots['back']), sheet_top(shots['rows']), 'Back from the list is the Text rows')
        self.assertEqual(self.chosen['settings'].get('fontFamily'), 1, 'Noto Sans chosen')
        self.assertEqual(self.chosen['settings'].get('sdFontFamilyName', ''), '')
        self.assertEqual(self.chosen['writes'], 1, 'saved once, when the panel closed')

    def test_every_step_is_one_fast_refresh(self):
        print('font level, panel pushes per step (into the list, up, up, choose, back):', self.steps[1:])
        for step in self.steps[1:]:
            self.assertEqual(step, ['DISPLAY'], self.steps)


class SingleSaveTest(unittest.TestCase):
    """Three changes in the panel, one write of settings.json, made when the panel closes."""

    @classmethod
    def setUpClass(cls):
        t = OPEN_TEXT[-1][0]
        # Spacing row: down twice, Confirm moves it to its next value. Alignment row the same. Then the
        # Font row: Confirm opens the list, one step up to Noto Sans, Confirm. Back up the levels, out.
        n = iter(range(1, 100))
        at = lambda: t + next(n) * STEP
        presses = OPEN_TEXT + [
            (at(), 'DOWN'), (at(), 'DOWN'), (at(), 'CONFIRM'),
            (at(), 'DOWN'), (at(), 'CONFIRM'),
            (at(), 'UP'), (at(), 'UP'), (at(), 'UP'), (at(), 'CONFIRM'), (at(), 'UP'), (at(), 'CONFIRM'),
            (at(), 'BACK'), (at(), 'BACK'), (at(), 'BACK')]
        before = run_sim(presses[:-3], quit_after=STEP)
        cls.before_writes = before["writes"]
        shutil.rmtree(before["tmp"], True)
        cls.res = run_sim(presses, quit_after=3 * STEP)
        shutil.rmtree(cls.res['tmp'], True)

    def test_all_changes_reached_the_card(self):
        s = self.res['settings']
        self.assertEqual((s.get('lineSpacing'), s.get('paragraphAlignment'), s.get('fontFamily')), (1, 1, 1), s)

    def test_one_write_for_the_panel(self):
        self.assertEqual(self.before_writes, 0, "open Font level has no settings write")
        self.assertEqual(self.res['writes'], 1, f"settings.json written {self.res['writes']} times")

    def test_write_comes_after_the_page(self):
        # The write after the last Back: the page that follows the close (gray pass included) goes first.
        log = self.res['log']
        marks = [m.start() for m in PRESS_AT.finditer(log)]
        tail = log[marks[-1]:]
        gray = tail.find('[GFX] displayGrayBuffer')
        write = re.search(r'\[SDW\] write \S*settings\.json', tail)
        self.assertGreaterEqual(gray, 0, 'the page was painted with its gray pass after the close')
        self.assertIsNotNone(write)
        self.assertLess(gray, write.start(), 'settings.json is written after the page is painted')


class NoGrayCloseSaveTest(unittest.TestCase):
    def test_bw_frame_closes_the_save_gate(self):
        t = OPEN_TEXT[-1][0]
        presses = OPEN_TEXT + [(t + STEP, 'DOWN'), (t + 2 * STEP, 'DOWN'), (t + 3 * STEP, 'CONFIRM'),
                               (t + 5 * STEP, 'BACK'), (t + 6 * STEP, 'BACK')]
        res = run_sim(presses, antialias=0, quit_after=3 * STEP)
        shutil.rmtree(res['tmp'], True)
        self.assertEqual(res['settings'].get('lineSpacing'), 1)
        self.assertEqual(res['writes'], 1, 'BW close frame releases the save exactly once')
        tail = res['log'][list(PRESS_AT.finditer(res['log']))[-1].start():]
        display = tail.find('ms from clearScreen to displayBuffer')
        write = re.search(r'\[SDW\] write \S*settings\.json', tail)
        self.assertGreaterEqual(display, 0)
        self.assertIsNotNone(write)
        self.assertLess(display, write.start())


class SleepWithPanelOpenTest(unittest.TestCase):
    """Sleep while the panel is open: the change waiting for the panel to close is written on the way out."""

    def test_change_survives_sleep(self):
        t = OPEN_TEXT[-1][0]
        presses = OPEN_TEXT + [(t + STEP, 'DOWN'), (t + 2 * STEP, 'DOWN'), (t + 3 * STEP, 'CONFIRM'),
                               (t + 5 * STEP, 'SLEEP')]
        res = run_sim(presses, quit_after=4 * STEP)
        shutil.rmtree(res['tmp'], True)
        self.assertEqual(res['settings'].get('lineSpacing'), 1, res['settings'])


LAYOUT = re.compile(r'Cache not found, building')


class QuietRelayoutTest(unittest.TestCase):
    """A Text row changes in place; the chapter is laid out once the presses stop, the page pushed once."""

    @classmethod
    def setUpClass(cls):
        t = OPEN_TEXT[-1][0]
        rows = OPEN_TEXT + [(t + STEP, 'DOWN'), (t + 2 * STEP, 'DOWN')]  # the cursor on Line Spacing
        first = len(rows)  # index of the first Confirm
        cls.runs = {}
        for label, antialias in (('aa', 1), ('bw', 0)):
            base = run_sim(rows, antialias=antialias, quit_after=STEP, refresh_ms=390)
            fast = [(t + 3 * STEP + 450 * k, 'CONFIRM') for k in range(4)]  # closer than the 600 ms quiet time
            quick = run_sim(rows + fast, antialias=antialias, quit_after=4 * STEP, refresh_ms=390)
            closed = run_sim(rows + fast + [(t + 8 * STEP, 'BACK'), (t + 9 * STEP, 'BACK')], antialias=antialias,
                             quit_after=3 * STEP, refresh_ms=390)
            one = run_sim(rows + [(t + 3 * STEP, 'CONFIRM')], antialias=antialias, quit_after=4 * STEP, refresh_ms=390)
            marks = [m.start() for m in PRESS_AT.finditer(quick['log'])]
            layouts = quick['log'][marks[first]:].count("CATCH_UP landed")
            previews = len(re.findall(r"PREVIEW_PAGE ok=1", quick['log'][marks[first]:]))
            base_pushes = len(pushes(base['trace']))
            quick_pushes = pushes(quick['trace'])[base_pushes:]
            one_pushes = pushes(one['trace'])[base_pushes:]
            print(f'{label}: 1 press -> panel pushes {one_pushes}; 4 quick presses -> {quick_pushes}, '
                  f'{layouts} layout(s), line spacing {closed["settings"].get("lineSpacing")}')
            cls.runs[label] = (one_pushes, quick_pushes, layouts, closed['settings'].get('lineSpacing'), previews)
            for r in (base, quick, one, closed):
                shutil.rmtree(r['tmp'], True)

    def test_four_quick_presses_are_one_layout(self):
        for label in ('aa', 'bw'):
            _, _, layouts, spacing, previews = self.runs[label]
            self.assertEqual(previews, 4, f"{label}: immediate preview for each choice")
            self.assertEqual(layouts, 1, label)
            self.assertEqual(spacing, 4, f'{label}: the value moved 4 times')

    def test_each_press_draws_the_page_and_sheet(self):
        for label in ('aa', 'bw'):
            one, quick, _, _, _ = self.runs[label]
            self.assertEqual(one, ['DISPLAY'], f'{label}: immediate page and sheet')
            self.assertEqual(quick, ['DISPLAY'] * 4, f'{label}: page and sheet for each choice')

    def test_drop_cap_applies_at_once(self):
        # 3 values and seldom pressed in a run: no sheet-only redraw first, the page and sheet in one push.
        t = OPEN_TEXT[-1][0]
        rows = OPEN_TEXT + [(t + (k + 1) * STEP, 'DOWN') for k in range(4)]
        base = run_sim(rows, quit_after=STEP)
        one = run_sim(rows + [(t + 5 * STEP, 'CONFIRM')], quit_after=3 * STEP)
        step = pushes(one['trace'])[len(pushes(base['trace'])):]
        for r in (base, one):
            shutil.rmtree(r['tmp'], True)
        self.assertEqual(step, ['DISPLAY'], step)

    def test_gray_pass_waits_for_the_sheet_to_close(self):
        t = OPEN_TEXT[-1][0]
        rows = OPEN_TEXT + [(t + STEP, 'DOWN'), (t + 2 * STEP, 'DOWN'), (t + 3 * STEP, 'CONFIRM')]
        base = run_sim(rows, quit_after=3 * STEP)
        closed = run_sim(rows + [(t + 6 * STEP, 'BACK'), (t + 7 * STEP, 'BACK')], quit_after=3 * STEP)
        closing = pushes(closed['trace'])[len(pushes(base['trace'])):]
        for r in (base, closed):
            shutil.rmtree(r['tmp'], True)
        self.assertEqual(closing.count('GRAY_DISPLAY'), 1, f'the gray pass runs once on close: {closing}')
        self.assertNotIn('GRAY_DISPLAY', pushes(base['trace'])[-3:], 'none while the sheet is open')


if __name__ == '__main__':
    unittest.main()
