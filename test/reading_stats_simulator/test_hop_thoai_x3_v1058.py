"""X3 dialogs: every box has its radius and every tick stands where the row puts it (founder 11/10/2026).

Each dialog the X3 can open from Settings, the Home hold menu and the reader's text settings is run in the production
simulator at the 3 text sizes; the last frame is measured against the one before it (hop_thoai_do.py):
  - the 4 corners of the box fit one circle of the radius the theme gives that box (pop-up, hold menu and slider 28, confirmation 28 as the pop-up, a notice tenorradius::leaf of its height),
    each row of the arc has its pixel, and the 4 corners are the same;
  - a pop-up that marks its value has exactly 1 tick; the tick's ink box is centred on its row to 0.5 px and on the label's ink
    to 0.5 px, stands 16 px in from the row's ring (17 px of air to the ring's inner edge) and 40 px or more from the box's border.
The static half checks that the radius is set in one place for both boards and that the width of a marked row is the one
drawChoice lays out.

TEST_PROGRAM names the simulator_x3_uc8279 binary. POP_KEEP names a folder to keep every shot in, POP_FROM reads them back
instead of running the simulator, POP_QUICK runs text size 0 only.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import hop_thoai_do as measure  # noqa: E402

REPO = HERE.parents[1]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
TIERS = [0] if os.environ.get('POP_QUICK') else [0, 1, 2]

# name, kind, starts on Home, reader menu style (1 tool bar, 0 list), keys
POPUP, HOLD, CONFIRM, SLIDER, TOAST = 'popup', 'hold', 'confirm', 'slider', 'toast'
RADIUS = {POPUP: 28, HOLD: 28, CONFIRM: 28, SLIDER: 28}


def want_radius(kind, box):
    if kind == TOAST:  # a one-line notice is a leaf of its own height: tenorradius::leaf, 3 h / 20 to the nearest px, 3 to 14
        return min(14, max(3, (3 * (box[3] - box[1] + 1) + 10) // 20))
    return RADIUS[kind]
PAGE = lambda number: ['DOWN'] * 4 + ['RIGHT'] * number + ['CONFIRM']
ROWS = [
    ('hienthi', 0, [0, 1, 4, 5, 6, 7, 9]), ('ngu', 1, [1, 2]), ('trinhdoc', 2, [2, 3, 5, 6]), ('dieukhien', 3, [2]),
    ('cuchi', 4, list(range(10))), ('banphim', 8, [0]),
]
# a settings page, then the row (the first row is the one selected), then the press that opens its pop-up
SCREENS = [(f'set-{page}-r{row}', POPUP, True, 1, PAGE(number) + ['RIGHT'] * row + ['CONFIRM'])
           for page, number, rows in ROWS for row in rows]
SCREENS += [(f'text-r{row}', POPUP, False, 0, ['CONFIRM'] + ['DOWN'] * 4 + ['CONFIRM'] + ['RIGHT'] * row + ['CONFIRM'])
            for row in (0, 1, 2, 3, 4, 6)]
SCREENS += [('home-hold', HOLD, True, 1, ['CONFIRM:1100']),
            ('home-confirm', CONFIRM, True, 1, ['CONFIRM:1100', 'DOWN', 'CONFIRM']),
            ('file-confirm', CONFIRM, True, 1, ['DOWN', 'CONFIRM', 'UP:900']),
            ('percent-slider', SLIDER, False, 1, ['CONFIRM', 'CONFIRM', 'RIGHT', 'CONFIRM']),
            ('bookmark-toast', TOAST, False, 1, ['CONFIRM', 'CONFIRM', 'RIGHT', 'RIGHT', 'CONFIRM'])]
GAP, FIRST = 1700, 2000


def book(path, chapters=12):
    head = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body>'
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        for i in range(1, chapters + 1):
            body = ''.join(f'<p>Đoạn {n:02d} của chương {i}. Người đọc giữ nguyên vị trí khi menu mở ra và đóng lại. Chữ chạy hết dòng để trang có đủ nội dung. </p>' for n in range(30))
            z.writestr(f'c{i}.xhtml', f'{head}<h1 id="c{i}">Chương {i}</h1>{body}</body></html>')
        nav = ''.join(f'<navPoint id="n{i}" playOrder="{i}"><navLabel><text>Chương {i}</text></navLabel><content src="c{i}.xhtml#c{i}"/></navPoint>' for i in range(1, chapters + 1))
        z.writestr('toc.ncx', '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/><docTitle><text>Sách thử</text></docTitle><navMap>' + nav + '</navMap></ncx>')
        items = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(1, chapters + 1))
        spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(1, chapters + 1))
        z.writestr('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Sách thử</dc:title><dc:identifier id="id">hop-thoai</dc:identifier><dc:language>vi</dc:language></metadata>'
                   f'<manifest>{items}<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx">{spine}</spine></package>')


def shoot(name, home, style, keys, tier, port):
    """(the frame before the last key, the frame after it, the simulator's log) of one run."""
    tag = f'{name}-co{tier}'
    cached = os.environ.get('POP_FROM')
    if cached:
        return (*(Image.open(Path(cached) / f'{tag}-{part}.png').convert('L') for part in ('prev', 'last')),
                (Path(cached) / f'{tag}.log').read_text())
    folder = Path(tempfile.mkdtemp(prefix='x3-hop-thoai-'))
    try:
        sd = folder / 'sd'
        store = sd / '.crosspoint'
        store.mkdir(parents=True)
        book(sd / 'sach.epub')
        (sd / 'Truyện').mkdir()
        book(sd / 'Truyện' / 'trong.epub', 3)
        font = REPO / 'test/reading_stats_simulator/fixtures/BeVietnamPro_18.cpfont'
        for i in range(3):
            d = sd / '.fonts' / f'ThuFont{i:02d}'
            d.mkdir(parents=True)
            shutil.copyfile(font, d / f'ThuFont{i:02d}_18.cpfont')
        settings = {'language': 'VI', 'uiShell': 0, 'uiTheme': 4, 'uiTextSize': tier, 'readerMenuStyle': style,
                    'wakeIntoBook': 0 if home else 1, 'fontSize': 18, 'fontFamily': 0, 'textSpacingVersion': 3,
                    'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1, 'tenorPresetVersion': 1,
                    'uiShellSleepMemo': 0, 'sleepTimeout': 10, 'readerTapTip': 0, 'tipsEnabled': 1,
                    'tenorButtonSymbols': 1, 'tenorSideArrows': 1, 'readerFavoritesDaDat': 1,
                    'readerFavoriteCount': 0, 'sdFontFamilyName': ''}
        (store / 'settings.json').write_text(json.dumps(settings))
        (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': '' if home else '/sach.epub',
                                                     'lastSleepFromReader': not home}))
        (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/sach.epub', 'title': 'Sách thử'}]}))
        actions = [f'{FIRST + i * GAP}:{key}' for i, key in enumerate(keys)]
        last = FIRST + (len(keys) - 1) * GAP + 1300
        before = FIRST + (len(keys) - 2) * GAP + 1300 if len(keys) > 1 else 1000
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
                   CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_MARK_SEED='12345',
                   CROSSPOINT_SIM_HTTP_PORT=str(port),
                   CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions + [f'{last + 500}:QUIT']),
                   CROSSPOINT_SIM_SCREENSHOTS=f'{before}:{folder}/prev.bmp;{last}:{folder}/last.bmp')
        result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=150)
        assert result.returncode == 0, (name, (result.stdout + result.stderr)[-1500:])
        images = []
        for part in ('prev', 'last'):
            image = Image.open(folder / f'{part}.bmp').convert('L')
            image.load()
            images.append(image)
            keep = os.environ.get('POP_KEEP')
            if keep:
                Path(keep).mkdir(parents=True, exist_ok=True)
                image.save(Path(keep) / f'{tag}-{part}.png')
        if os.environ.get('POP_KEEP'):
            (Path(os.environ['POP_KEEP']) / f'{tag}.log').write_text(result.stdout + result.stderr)
        return (*images, result.stdout + result.stderr)
    finally:
        shutil.rmtree(folder, ignore_errors=True)


class HopThoaiX3(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        jobs = [(s, t) for t in TIERS for s in SCREENS]
        with ThreadPoolExecutor(max_workers=4) as pool:
            frames = list(pool.map(lambda j: shoot(j[0][0], j[0][2], j[0][3], j[0][4], j[1], 29800 + jobs.index(j)), jobs))
        cls.shots = {(s[0], t): (s, f) for (s, t), f in zip(jobs, frames)}
        cls.boxes = {key: measure.logged_box(f[2]) for key, (s, f) in cls.shots.items()}

    def test_every_dialog_is_found(self):
        for key, box in self.boxes.items():
            with self.subTest(key=key):
                self.assertIsNotNone(box, f'{key}: the painter logged no dialog')

    def test_corner_radius(self):
        # Ink of the screen under a border hides some of its pixels and pushes the fit of that corner outward (a toggle under a
        # corner: 33 for 28), never inward: the smallest of the 4 radii is the corner best read. It must be the box's radius,
        # and a second corner must agree.
        for key, (screen, frames) in self.shots.items():
            box = self.boxes[key]
            if not box:
                continue
            want = want_radius(screen[1], box)
            fits = measure.corners(frames[1], frames[0], box, halo=screen[1] in (HOLD, CONFIRM, TOAST))
            radii = sorted(fit[0] for fit in fits.values())
            with self.subTest(key=key, what='radius of the box'):
                self.assertLessEqual(abs(radii[0] - want), 1, f'{key}: radii {({c: f[0] for c, f in fits.items()})}, want {want}')
                near = [corner for corner, fit in fits.items() if abs(fit[0] - want) <= 1]
                self.assertGreaterEqual(len(near), 2, f'{key}: radii {({c: f[0] for c, f in fits.items()})}, want {want}')
            with self.subTest(key=key, what='arc without a gap'):
                gaps = sorted(sum(1 for row in fit[2][:want] if row == 60) for fit in fits.values())
                self.assertLessEqual(gaps[1], 2, f'{key}: rows of the arc with no pixel, per corner {gaps}')

    def test_confirmation_ring_keeps_6_px_from_the_edge(self):
        # the ring round Cancel stood 6 px in from the edge of the box at 19; the larger corner must not bring it nearer
        for key, (screen, frames) in self.shots.items():
            if screen[1] != CONFIRM:
                continue
            with self.subTest(key=key):
                near = measure.ring_clearance(frames[1], self.boxes[key], want_radius(CONFIRM, self.boxes[key]))
                self.assertGreaterEqual(near, 6, f'{key}: the ring comes {near} px from the edge')

    def test_tick(self):
        for key, (screen, frames) in self.shots.items():
            box = self.boxes[key]
            if not box or screen[1] != POPUP:
                continue
            found = measure.tick_metrics(frames[1], REPO, box)
            with self.subTest(key=key):
                self.assertEqual(found['ticks'], 1, f'{key}: {found}')
                self.assertLessEqual(abs(found['off_row']), 0.5, f'{key}: tick {found["off_row"]} px off its row, {found}')
                self.assertLessEqual(abs(found.get('off_text', 0)), 0.5, f'{key}: tick {found.get("off_text")} px off its label, {found}')
                self.assertTrue(16 <= found['air_ring'] <= 18, f'{key}: {found["air_ring"]} px of air to the ring, {found}')
                self.assertGreaterEqual(found['air_border'], 40, f'{key}: tick {found["air_border"]} px from the border, {found}')


class Static(unittest.TestCase):
    def test_radius_is_one_setting_for_both_boards(self):
        popup = (REPO / 'src/components/OptionPopup.h').read_text()
        self.assertNotIn('hasTouch() ? static_cast<uint8_t>(metrics.popupCornerRadius)', popup)
        self.assertEqual(len(re.findall(r'props\.styles\.normal\.radius = static_cast<uint8_t>\(metrics\.popupCornerRadius\);', popup)), 1)

    def test_marked_row_width_is_the_one_draw_choice_lays_out(self):
        popup = (REPO / 'src/components/OptionPopup.h').read_text()
        margin, mark, gap = (int(v) for v in re.search(r'constexpr int TEXT_X = (\d+), MARK = (\d+), GAP = (\d+);', popup).groups())
        laid = tuple(int(v) for v in re.search(r': textWidth \+ 2 \* innerPadding \+ (\d+) \+ (\d+) \+ (\d+) \+ (\d+);', popup).groups())
        self.assertEqual(laid, (margin, mark, gap, margin))

    def test_confirmation_takes_the_popup_radius_from_the_theme(self):
        layout = (REPO / 'src/components/ConfirmationLayout.h').read_text()
        self.assertNotRegex(layout, r', 19, headerHeight')
        activity = (REPO / 'src/activities/util/ConfirmationActivity.cpp').read_text()
        self.assertIn('UITheme::getInstance().getMetrics().popupCornerRadius);', activity)

    def test_tick_is_set_on_the_row_it_stands_in(self):
        popup = (REPO / 'src/components/OptionPopup.h').read_text()
        self.assertIn('const int markY = ry + (ROW - (MARK - 1)) / 2;', popup)


if __name__ == '__main__':
    unittest.main()
