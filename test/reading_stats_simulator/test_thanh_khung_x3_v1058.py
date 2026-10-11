"""X3 dynamic bar: a ring round the bar of cards only, as on the X4 Pro (founder 11/10/2026).

Every X3 screen with a bar is run in the production simulator and its last frame measured.
Bars of cards (Home, the reader menu's tool bar with its Text sheet over the page, and its list of cards):
  - the ring (grey dots, 2 px, round-ended) stands 16 px in from both sides, found from the pixels alone;
  - it is the same rect on every screen of one text size (1 px apart is a failure) and the one the painter logged;
  - nothing but white touches it: the 3 rows over and under it are clear (tips above, the button strip below);
  - the black card inside keeps 6 px from its top and bottom, 8 px from its end, its icon on the card's centre.
Bars of a screen's path (the zone icon and the name pill: settings, files, the reader's text settings):
  - no ring round the bar, the 6 rows at its top and bottom hold no ink, and the zone icon pill stands 16 px in;
  - every pill is a grey dotted ring of 2 px (the name pill too: only a chosen card has a black ring), its icon on its centre.
The static half checks that the rings are drawn through the one function and only by the bars of cards.

TEST_PROGRAM names the simulator_x3_uc8279 binary. BAR_KEEP names a folder to keep every shot and log in, BAR_FROM reads
them back instead of running the simulator, BAR_QUICK runs the 13 screens at text size 0 only.
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
WIDTH, HEIGHT = 528, 792
MARGIN, END_INSET, TOP_INSET = 16, 8, 6
BAR_HEIGHT = {0: 60, 1: 67, 2: 72}
# the screens whose bar has a chosen card (black ring 3 px) and which of the 5 slots it is
CHOSEN = {'home-recent': 0, 'home-stats': 3, 'home-settings': 4, 'reader-toolbar': 1, 'reader-text-toolbar': 4,
          'reader-list': 0}
CARD_WIDTH = 84

# name, starts on Home, reader menu style (1 tool bar, 0 list), keys
SCREENS = [
    ('home-recent', True, 1, []),
    ('home-stats', True, 1, ['DOWN'] * 3),
    ('home-settings', True, 1, ['DOWN'] * 4),
    ('settings-display', True, 1, ['DOWN'] * 4 + ['CONFIRM']),
    ('settings-clock', True, 1, ['DOWN'] * 4 + ['RIGHT'] * 6 + ['CONFIRM', 'CONFIRM']),
    ('settings-send', True, 1, ['DOWN'] * 4 + ['RIGHT'] * 5 + ['CONFIRM']),
    ('file-folder', True, 1, ['DOWN', 'CONFIRM']),
    ('reader-toolbar', False, 1, ['CONFIRM']),
    ('reader-text-toolbar', False, 1, ['CONFIRM', 'RIGHT', 'RIGHT', 'RIGHT', 'CONFIRM']),
    ('reader-list', False, 0, ['CONFIRM']),
    ('reader-text-list', False, 0, ['CONFIRM'] + ['DOWN'] * 4 + ['CONFIRM']),
    ('settings-popup', True, 1, ['DOWN'] * 4 + ['CONFIRM', 'RIGHT', 'CONFIRM']),
    ('reader-page', False, 1, []),
]
NO_BAR = {'reader-page'}
FRAMED = set(CHOSEN)
NETWORK_ICON = 19  # tenorchrome::X3PathIcon::Network, the icon of 4 squares
# extra settings that move the bar: the key strip off (bar 16 px over the edge), button names as words, no tips
VARIANTS = {'': {}, 'bar-cuoi': {'globalStatusBarMode': 1}, 'nut-chu': {'tenorButtonSymbols': 0}, 'meo-tat': {'tipsEnabled': 0}}


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
        z.writestr('book.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Sách thử</dc:title><dc:identifier id="id">thanh-khung</dc:identifier><dc:language>vi</dc:language></metadata>'
                   f'<manifest>{items}<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx">{spine}</spine></package>')


def shoot(name, home, style, keys, tier, extra, port, tag=''):
    keep = os.environ.get('BAR_KEEP')
    cached = os.environ.get('BAR_FROM')
    if cached:
        image = Image.open(Path(cached) / f'{tag}.png').convert('L')
        image.load()
        text = (Path(cached) / f'{tag}.log').read_text()
        return image, text
    folder = Path(tempfile.mkdtemp(prefix='x3-thanh-'))
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
        settings.update(extra)
        (store / 'settings.json').write_text(json.dumps(settings))
        (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': '' if home else '/sach.epub',
                                                     'lastSleepFromReader': not home}))
        (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/sach.epub', 'title': 'Sách thử'}]}))
        gap, first = 2000, 2000
        actions = [f'{first + i * gap}:{key}' for i, key in enumerate(keys)]
        shot_at = (first + (len(keys) - 1) * gap if keys else 1000) + 1300
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
                   CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_MARK_SEED='12345',
                   CROSSPOINT_SIM_HTTP_PORT=str(port),
                   CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions + [f'{shot_at + 500}:QUIT']),
                   CROSSPOINT_SIM_SCREENSHOTS=f'{shot_at}:{folder}/shot.bmp')
        result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, (name, (result.stdout + result.stderr)[-1500:])
        image = Image.open(folder / 'shot.bmp').convert('L')
        image.load()
        if keep:
            Path(keep).mkdir(parents=True, exist_ok=True)
            image.save(Path(keep) / f'{tag}.png')
            (Path(keep) / f'{tag}.log').write_text(result.stdout + result.stderr)
        return image, result.stdout + result.stderr
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def logged_frames(text):
    """The rects the painter logged for the ring, in order: (x, y, w, h)."""
    return [tuple(int(v) for v in f[2:]) for f in re.findall(
        r'\[BARFRAME\] screen=(\S+) tier=(\d+) x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+)', text)]


def last_path(text):
    """The path bar of the last frame: ((x, y, w, h) of the bar, [(x, y, w, h, current, icon) of each pill]), None if the last bar drawn
    was not a path bar (a ring logged after it means a bar of cards was drawn over it)."""
    marks = [m for m in re.finditer(r'\[(BARFRAME|X3PATH)\]', text)]
    if not marks or marks[-1].group(1) != 'X3PATH':
        return None
    tail = text[marks[-1].start():]
    bar = tuple(int(v) for v in re.search(r'\[X3PATH\].*? bar=(\d+),(\d+),(\d+),(\d+)', tail).groups())
    pills = [tuple(int(v) for v in m) for m in re.findall(r'\[X3PILL\] x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+) current=(\d) icon=(\d+)', tail)]
    return bar, pills


def inside_pill(px, py, x, y, w, h):
    """Same pixel-centre test as tenorchrome::inPill (doubled to stay integer)."""
    if px < x or py < y or px >= x + w or py >= y + h:
        return False
    r = h // 2
    if x + r <= px < x + w - r:
        return True
    dx = 2 * (x + r) - (2 * px + 1) if px < x + r else (2 * px + 1) - 2 * (x + w - r)
    dy = 2 * py + 1 - (2 * y + h)
    return dx * dx + dy * dy < 4 * r * r


def ring_pixels(x, y, w, h, thick=2):
    cells = []
    for py in range(y, y + h):
        for px in range(x, x + w):
            if inside_pill(px, py, x, y, w, h) and not inside_pill(px, py, x + thick, y + thick, w - 2 * thick, h - 2 * thick):
                cells.append((px, py))
    return cells


def mismatch(image, x, y, w, h, half):
    """Ring pixels in one half of the frame that are not dots on (x + y) even, 0 for a ring that is exactly there."""
    pixels = image.load()
    bad = 0
    for px, py in ring_pixels(x, y, w, h):
        if (px < WIDTH // 2) != half:
            continue
        if (pixels[px, py] < 128) != ((px + py) % 2 == 0):
            bad += 1
    return bad


def find_frame(image):
    """The ring from the pixels alone: its top and bottom rows are dots on (x + y) even across the middle of the screen;
    its two ends are the ones whose dots fit best."""
    pixels = image.load()
    lo, hi = 150, WIDTH - 150
    rows = []
    for y in range(HEIGHT - 150, HEIGHT):
        match = sum(1 for x in range(lo, hi) if (pixels[x, y] < 128) == ((x + y) % 2 == 0))
        if match >= 0.985 * (hi - lo) and sum(1 for x in range(lo, hi) if pixels[x, y] < 128) >= 0.4 * (hi - lo):
            rows.append(y)
    # A frame standing over the bar (the reader's progress line) has dotted rows too: the ring is the last 2 runs.
    runs = [[rows[0]]] if rows else []
    for y in rows[1:]:
        if y == runs[-1][-1] + 1:
            runs[-1].append(y)
        else:
            runs.append([y])
    if len(runs) < 2 or len(runs[-2]) + len(runs[-1]) < 4:
        return None
    top, height = runs[-2][0], runs[-1][-1] - runs[-2][0] + 1
    left = min(range(0, 41), key=lambda x: mismatch(image, x, top, 2 * (WIDTH // 2 - x), height, True))
    right = min(range(WIDTH - 41, WIDTH), key=lambda e: mismatch(image, left, top, e - left + 1, height, False))
    return left, top, right - left + 1, height


def ink_bbox_inside(image, frame, skip_ring=True):
    x, y, w, h = frame
    ring = set(ring_pixels(x, y, w, h)) if skip_ring else set()
    pixels = image.load()
    xs, ys = [], []
    for py in range(y, y + h):
        for px in range(x, x + w):
            if (px, py) in ring or not inside_pill(px, py, x, y, w, h):
                continue
            if pixels[px, py] < 128:
                xs.append(px)
                ys.append(py)
    return (min(xs), min(ys), max(xs), max(ys)) if xs else None


def clear_rows(image, frame, direction, limit=12):
    """How many rows over (direction -1) or under (+1) the frame hold no ink in its x range."""
    x, y, w, h = frame
    pixels = image.load()
    count = 0
    py = y - 1 if direction < 0 else y + h
    while 0 <= py < HEIGHT and count < limit:
        if any(pixels[px, py] < 128 for px in range(x, x + w)):
            break
        count += 1
        py += direction
    return count


def card_centre(index):
    """Where slot `index` of 5 stands: BAR_END_INSET in from the ring's ends, evenly spread (the X4 Pro's formula)."""
    first = MARGIN + END_INSET + CARD_WIDTH // 2
    last = MARGIN + (WIDTH - 2 * MARGIN) - 1 - END_INSET - CARD_WIDTH // 2
    return first + (2 * (last - first) * index + 4) // 8


def find_card(image, frame):
    """The chosen card's black ring from the pixels: the first row inside the frame with a long black run is its top;
    on its middle row the first solid 3 px is the left side of the ring. Returns (left, top, height)."""
    x, y, w, h = frame
    pixels = image.load()
    card_h = h - 2 * TOP_INSET
    for py in range(y + 2, y + h - 2):
        if sum(1 for px in range(x + 4, x + w - 4) if pixels[px, py] < 128) >= 20 and any(
                all(pixels[px + k, py] < 128 for k in range(12)) for px in range(x + 4, x + w - 16)):
            mid = py + card_h // 2
            for px in range(x + 4, x + w - 4):
                if all(pixels[px + k, mid] < 128 for k in range(3)):
                    return px, py, card_h
            return None
    return None


def icon_offset(image, card):
    left, top, card_h = card
    pixels = image.load()
    ring = set(ring_pixels(left, top, CARD_WIDTH, card_h, 3))
    xs, ys = [], []
    for py in range(top, top + card_h):
        for px in range(left, left + CARD_WIDTH):
            if (px, py) not in ring and inside_pill(px, py, left, top, CARD_WIDTH, card_h) and pixels[px, py] < 128:
                xs.append(px)
                ys.append(py)
    return ((min(xs) + max(xs)) / 2 - (left + (CARD_WIDTH - 1) / 2), (min(ys) + max(ys)) / 2 - (top + (card_h - 1) / 2))


def measure_all():
    jobs = [(name, home, style, keys, tier, variant)
            for tier in ((0,) if os.environ.get('BAR_QUICK') else (0, 1, 2))
            for variant in VARIANTS if (variant == '' or tier == 0) and not (os.environ.get('BAR_QUICK') and variant)
            for name, home, style, keys in SCREENS]
    def one(job, port):
        name, home, style, keys, tier, variant = job
        image, logged = shoot(name, home, style, keys, tier, VARIANTS[variant], port, f'{name}-co{tier}-{variant or "mac-dinh"}')
        return job, image, logged
    with ThreadPoolExecutor(max_workers=3) as pool:
        futures = [pool.submit(one, job, 29500 + 2 * i) for i, job in enumerate(jobs)]
        return [f.result() for f in futures]


class BarFrameStatic(unittest.TestCase):
    def test_one_function_draws_the_ring_and_only_cards_call_it(self):
        chrome = (REPO / 'src/components/TenorMenuChrome.cpp').read_text()
        tabs = (REPO / 'src/activities/UiTabListActivity.cpp').read_text()
        tool = (REPO / 'src/activities/reader/ReaderToolbarUi.cpp').read_text()
        self.assertEqual(len(re.findall(r'void tenorchrome::drawBarFrame\(', chrome)), 1)
        path = chrome[chrome.index('void tenorchrome::drawX3NavigationPath('):chrome.index('void tenorchrome::drawX3ActivityPath(')]
        self.assertNotIn('drawBarFrame(', path, 'RED: the path bar draws a ring')
        self.assertIn('drawPillRing(renderer, leftEdge, top, pill.width, height, 2, true)', path,
                      'RED: a pill of the path bar is not a grey 2 px ring')
        for where, text in (('home and tab bars', tabs[tabs.index('::veThanhTheTenor('):tabs.index('UiTabListActivity::UiTabListActivity(')]),
                            ('reader tool bar', tool[tool.index('void ReaderToolbarUi::buildX3Bar('):tool.index('void ReaderToolbarUi::begin(')])):
            self.assertIn('drawBarFrame(', text, f'RED: {where} has no ring')
            self.assertNotIn('drawPillRing(renderer, x0, y0, w, h', text, f'RED: {where} draws its own ring')
        callers = sorted(str(f.relative_to(REPO)) for f in (REPO / 'src').rglob('*.cpp')
                         if f.name != 'TenorMenuChrome.cpp' and 'drawBarFrame(' in f.read_text())
        self.assertEqual(callers, ['src/activities/UiTabListActivity.cpp', 'src/activities/reader/ReaderToolbarUi.cpp'])


@unittest.skipUnless(PROGRAM.exists(), f'no simulator at {PROGRAM}')
class BarFrameOnScreen(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.results = measure_all()

    def test_ring_on_every_bar_of_cards(self):
        failures, report = [], []
        rects = {}
        for (name, home, style, keys, tier, variant), image, text in self.results:
            if name not in FRAMED:
                continue
            logged = logged_frames(text)
            label = f'{name} co{tier} {variant or "mac-dinh"}'
            frame = find_frame(image)
            if frame is None:
                failures.append(f'{label}: no ring at the foot')
                continue
            rects.setdefault((tier, variant), {})[name] = frame
            x, y, w, h = frame
            if h != BAR_HEIGHT[tier]:
                failures.append(f'{label}: ring {h} px tall, bar is {BAR_HEIGHT[tier]}')
            if (x, w) != (MARGIN, WIDTH - 2 * MARGIN):
                failures.append(f'{label}: ring at x {x} width {w}, wants x {MARGIN} width {WIDTH - 2 * MARGIN}')
            if WIDTH - (x + w) != x:
                failures.append(f'{label}: ring {x} px from the left edge and {WIDTH - (x + w)} from the right')
            if not logged or logged[-1] != frame:
                failures.append(f'{label}: painter logged {logged[-1] if logged else None}, pixels show {frame}')
            pixels = image.load()
            dots = [(px, py) for px, py in ring_pixels(*frame) if (px + py) % 2 == 0]
            missing = [c for c in dots if pixels[c] >= 128]
            if missing:
                failures.append(f'{label}: {len(missing)} of {len(dots)} ring dots missing, first {missing[0]}')
            above, below = clear_rows(image, frame, -1), clear_rows(image, frame, +1)
            if above < 3 or below < 3:
                failures.append(f'{label}: clear rows over {above} under {below}, wants 3')
            box = ink_bbox_inside(image, frame)
            if box is None:
                failures.append(f'{label}: nothing inside the ring')
                continue
            if box[1] - y != TOP_INSET or (y + h - 1) - box[3] != TOP_INSET:
                failures.append(f'{label}: card {box[1] - y} px from the top and {(y + h - 1) - box[3]} from the bottom, wants {TOP_INSET}')
            card = find_card(image, frame)
            if card is None:
                failures.append(f'{label}: no chosen card')
                continue
            if card[1] - y != TOP_INSET or card[0] + CARD_WIDTH // 2 != card_centre(CHOSEN[name]):
                failures.append(f'{label}: chosen card at x {card[0] + CARD_WIDTH // 2} top {card[1] - y}, wants slot {CHOSEN[name]} at {card_centre(CHOSEN[name])} top {TOP_INSET}')
            dx, dy = icon_offset(image, card)
            report.append(f'{label}: ring {frame} over {above} under {below}, card centre x {card[0] + CARD_WIDTH // 2}, '
                          f'icon off its card centre {dx:+.1f} {dy:+.1f}, card ink {box}')
            if abs(dx) > 0.5 or abs(dy) > 0.5:
                failures.append(f'{label}: icon {dx:+.1f} px across, {dy:+.1f} down from its card centre, wants 0.5')
        for key, group in rects.items():
            if len(set(group.values())) != 1:
                failures.append(f'text size/variant {key}: rects differ: {group}')
        print('\n'.join(report))
        self.assertFalse(failures, f'{len(failures)} failures, first 12:\n' + '\n'.join(failures[:12]))

    def test_no_ring_on_a_path_bar_and_grey_pills(self):
        failures, report = [], []
        for (name, home, style, keys, tier, variant), image, text in self.results:
            if name in FRAMED or name in NO_BAR:
                continue
            label = f'{name} co{tier} {variant or "mac-dinh"}'
            path = last_path(text)
            if path is None:
                failures.append(f'{label}: the last bar drawn is not a path bar, or a ring is drawn round it')
                continue
            (bx, by, bw, bh), pills = path
            pixels = image.load()
            for rows, where in ((range(by, by + TOP_INSET), 'top'), (range(by + bh - TOP_INSET, by + bh), 'bottom')):
                ink = sum(1 for py in rows for px in range(bx, bx + bw) if pixels[px, py] < 128)
                if ink:
                    failures.append(f'{label}: {ink} ink pixels in the {TOP_INSET} rows at the {where} of the bar, no ring stands there')
            if not pills or pills[0][0] != MARGIN or pills[0][1] != by + TOP_INSET:
                failures.append(f'{label}: first pill at {pills[0][:2] if pills else None}, wants x {MARGIN} y {by + TOP_INSET}')
            for px_, py_, pw, ph, current, icon in pills:
                ring = ring_pixels(px_, py_, pw, ph)
                dots = [c for c in ring if (c[0] + c[1]) % 2 == 0]
                gaps = [c for c in ring if (c[0] + c[1]) % 2 == 1]
                kind = 'name' if current else 'icon'
                if any(pixels[c] >= 128 for c in dots) or any(pixels[c] < 128 for c in gaps):
                    failures.append(f'{label}: {kind} pill at x {px_} is not a grey dotted ring of 2 px')
                # A name has nothing inside the ring but its text; an icon may reach the third pixel (the 4 squares do).
                third = [c for c in ring_pixels(px_, py_, pw, ph, 3) if c not in set(ring)]
                if current and any(pixels[c] < 128 for c in third):
                    failures.append(f'{label}: name pill at x {px_} has a third ring pixel inked (a black 3 px ring)')
                box = ink_bbox_inside(image, (px_, py_, pw, ph))
                if box is None:
                    continue
                # Only the pill with an icon is held to the half pixel on both axes; a name's accents move its box down.
                dx = (box[0] + box[2]) / 2 - (px_ + (pw - 1) / 2)
                dy = (box[1] + box[3]) / 2 - (py_ + (ph - 1) / 2)
                report.append(f'{label}: {kind} pill x {px_} w {pw}: ink off its centre {dx:+.1f} {dy:+.1f}')
                # The icon of 4 squares (Network) stands 1 px off its pill at text size 1 on the base too: reported, not held.
                if not current and icon != NETWORK_ICON and (abs(dx) > 0.5 or abs(dy) > 0.5):
                    failures.append(f'{label}: icon {dx:+.1f} px across, {dy:+.1f} down from its pill centre, wants 0.5')
                if current and abs(dx) > 0.5:
                    failures.append(f'{label}: name {dx:+.1f} px across from its pill centre, wants 0.5')
        print('\n'.join(report))
        self.assertFalse(failures, f'{len(failures)} failures, first 12:\n' + '\n'.join(failures[:12]))

    def test_no_ring_where_there_is_no_bar(self):
        for (name, home, style, keys, tier, variant), image, text in self.results:
            if name in NO_BAR:
                self.assertIsNone(find_frame(image), f'{name} co{tier}: a ring on a screen with no bar')


if __name__ == '__main__':
    unittest.main()
