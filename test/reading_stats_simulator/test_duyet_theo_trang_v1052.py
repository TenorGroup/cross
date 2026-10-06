"""v1.0.52: a front-button step past the page edge flips the whole page, one repaint per press.

Folder of 30 files, real X3 simulator input. Two screens carry the file list:
the File tab on Home (a tab list, no page buttons) and the folder screen (a flat list).
Before: the viewport crawled one row per press (page 2 opened on rows 1..10 with the
selection on the bottom row). Now: step past the last row opens the next page with the
selection on its first row, step before the first row opens the previous page with the
selection on its last row, and the wrap at either end lands on a full page.
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from pill_row import pill_band

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
PRESS = re.compile(r'\[IN\] press t=(\d+)')
REFRESH = re.compile(r'^\[(\d+)\] .*from clearScreen to displayBuffer', re.M)
FILES = 30
PITCH = 56  # row pitch of the X3 list in px
LIST_TOP, LIST_BOTTOM = 120, 760  # the tab bar ends at row 112 and its grey ring would pass the ring probe


def highlight_top(img):
    """Top y of the selected row: the top line of its black ring (the row is a white pill since 1.0.52)."""
    band = pill_band(img, LIST_TOP, LIST_BOTTOM)
    assert band, 'no selected row on screen'
    return band[0]


class DuyetTheoTrangTest(unittest.TestCase):
    def run_sim(self, in_folder, presses, shots):
        """presses: list of (ms, KEY). shots: list of (ms, label). Returns (log, {label: Image})."""
        tmp = tempfile.mkdtemp(prefix='cross-duyet-trang-')
        self.addCleanup(shutil.rmtree, tmp, True)
        sd = Path(tmp)
        store = sd / '.crosspoint'
        store.mkdir()
        files = sd / 'books' if in_folder else sd
        files.mkdir(exist_ok=True)
        for i in range(FILES):
            (files / f'tep{i:02d}.txt').write_text('Original test text.\n' * 15)
        (store / 'settings.json').write_text(json.dumps({
            'language': 'VI', 'uiTheme': 4, 'sleepTimeoutMinutes': 31, 'globalStatusBarMode': 1}))
        (store / 'state.json').write_text(json.dumps({
            'openEpubPath': '', 'lastSleepFromReader': False, 'showBootScreen': False}))
        (store / 'menu-customization.json').write_text(json.dumps({
            'version': 1, 'tabs': {'home': [0, 1, 4, 2, 3], 'settings': list(range(7)),
                                 'reader': list(range(4)), 'text': list(range(4))}, 'pins': []}))
        head = '1200:DOWN;' + ('1800:CONFIRM;' if in_folder else '')
        script = head + ';'.join(f'{ms}:{key}' for ms, key in presses) + f';{presses[-1][0] + 1500}:QUIT'
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{sd}/{label}.bmp' for ms, label in shots))
        result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=90)
        log = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, log[-4000:])
        return log, {label: Image.open(sd / f'{label}.bmp').convert('L') for _, label in shots}

    def check_one_repaint_per_press(self, log):
        presses = [int(t) for t in PRESS.findall(log)]
        refreshes = [int(t) for t in REFRESH.findall(log)]
        counted = 0
        for i, start in enumerate(presses):
            end = presses[i + 1] if i + 1 < len(presses) else start + 400
            if i == 0 and start < 1500:
                continue  # the DOWN that opens the File tab
            n = sum(1 for t in refreshes if start <= t < end)
            counted += 1
            self.assertEqual(n, 1, f'press at {start} ms made {n} repaints')
        self.assertGreater(counted, 5)

    def flow(self, in_folder, rows, last_rows):
        t0, step = (2600 if in_folder else 2000), 350
        keys = (['RIGHT'] * rows + ['LEFT'] + ['RIGHT'] * 2 + ['LEFT'] * 3 +  # page 1 -> 2 -> 1 -> 2 -> 1
                ['LEFT'] * (rows - 2) + ['LEFT'] + ['RIGHT'])                # row rows-2 up to row 0, wrap back, wrap on
        presses = [(t0 + step * i, key) for i, key in enumerate(keys)]
        after = {'p1end': rows - 2, 'p2start': rows - 1, 'p1back': rows, 'p2again': rows + 2, 'row0': 2 * rows + 3,
                 'lastpage': 2 * rows + 4, 'firstpage': 2 * rows + 5}
        shots = [(t0 - 200, 'start')] + [(t0 + step * i + 220, label) for label, i in after.items()]
        log, im = self.run_sim(in_folder, presses, shots)
        first = highlight_top(im['start'])

        def sel(label):
            return round((highlight_top(im[label]) - first) / PITCH)

        self.assertEqual(sel('start'), 0)
        rows_per_page = sel('p1end') + 1
        self.assertEqual(rows_per_page, rows)
        self.assertEqual(sel('p2start'), 0, 'past the last row the next page opens on its first row')
        self.assertEqual(sel('p1back'), rows_per_page - 1, 'before the first row the page before ends on it')
        self.assertEqual(im['p1back'].tobytes(), im['p1end'].tobytes(), 'the page before is the page left')
        self.assertEqual(sel('p2again'), 1)
        self.assertEqual(sel('row0'), 0)
        self.assertEqual(sel('lastpage'), last_rows - 1, 'the wrap from the first row lands on a full last page')
        self.assertEqual(sel('firstpage'), 0)
        self.assertEqual(im['firstpage'].tobytes(), im['start'].tobytes(), 'the wrap forward is the first page again')
        # No row of the old page stays on the new one: the unselected rows all differ.
        bands = lambda img: [img.crop((20, first + PITCH * i, 300, first + PITCH * i + PITCH - 4)).tobytes()
                             for i in range(rows_per_page)]
        old, new = bands(im['p1end']), bands(im['p2start'])
        for i, band in enumerate(new[1:], 1):
            self.assertNotIn(band, old[:-1], f'row {i} of page 2 repeats a row of page 1')
        self.check_one_repaint_per_press(log)

    def test_last_page_fills_to_the_foot(self):
        """v1.0.53: a page whose next row would show too little gives its last row to the faded glimpse, so it
        holds a row less than fits. Stepping onto the last page must still land it full: its last row ends
        less than a row above the list's foot, the selection on screen. Folder /sach of 24 books (icons)."""
        tmp = Path(tempfile.mkdtemp(prefix='cross-trang-cuoi-'))
        self.addCleanup(shutil.rmtree, tmp, True)
        store, books = tmp / '.crosspoint', tmp / 'sach'
        store.mkdir()
        books.mkdir()
        epub = REPO / 'test/epubs/test_kerning_ligature.epub'
        for i in range(24):
            shutil.copyfile(epub, books / f'{"n" * (i + 1)}.epub')
        (store / 'settings.json').write_text(json.dumps({'language': 'VI', 'sdFontFamilyName': ''}))
        (store / 'state.json').write_text(json.dumps({'openEpubPath': '', 'showBootScreen': False}))
        shot = 6000 + 350 * 16 + 600
        script = '3000:DOWN;4500:CONFIRM;' + ';'.join(f'{6000 + 350 * i}:RIGHT' for i in range(16))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(tmp),
                   CROSSPOINT_SIM_INPUT_SCRIPT=f'{script};{shot + 600}:QUIT',
                   CROSSPOINT_SIM_SCREENSHOTS=f'{shot}:{tmp}/last.bmp')
        result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr)[-3000:])
        img = Image.open(tmp / 'last.bmp').convert('L')
        self.assertTrue(pill_band(img, LIST_TOP, LIST_BOTTOM), 'the selected row left the screen')
        # The path line under the list runs the screen's width (the selected ring stops 23 px short of each side).
        line = next(y for y in range(LIST_TOP, img.height)
                    if sum(1 for x in range(img.width) if img.getpixel((x, y)) < 128) >= 500)
        text = [y for y in range(LIST_TOP, line) if any(img.getpixel((x, y)) < 128 for x in range(80, 300))]
        # Less than a row of room under the last row's box: its words sit mid-row, about 20 px above its foot.
        self.assertLess(line - text[-1], PITCH + 20, f'{line - text[-1]} px under the last row')

    def test_home_file_tab(self):
        # v1.0.53: the down chevron's room went back to the list: 11 full rows, the 12th faded under them.
        self.flow(False, 11, 11)

    def test_folder_screen(self):
        # The folder keeps its path line under the list. Its pages give their 11th row to the faded glimpse;
        # the last page has no row after it and fills to the foot with 11.
        self.flow(True, 10, 11)


if __name__ == '__main__':
    unittest.main()
