"""The reader strip names the way back after a jump: "< tr. N" in the title's cell (X3 simulator; the X4 Pro
cases are test/x4pro_simulator/test_vien_quay_lai_x4pro_v1061.py, the same checks).

Read only. Contents jump leaves one entry on the way-back stack, the strip shows its page, Back
returns and the strip is the title again. An empty stack leaves the strip as it was. The ring and
what stands in it are measured from the real frame: the chevron and the text are centred in it.
The simulator prints the pill's place (JUMPBACK, SIMULATOR builds only); the pixels are read from
the screenshot, so a pill the log claims but the frame lacks fails.
"""
import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from cai_dat_truoc_tenor import truoc_tenor
from test_ngan_xep_link_v1052 import write_epub

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
BOOK = '/sach/lien-ket.epub'
SAVED = re.compile(r'Progress saved: spine=(\d+) offset=\d+ page=(\d+)')
PILL = re.compile(r'JUMPBACK\S* page=(\d+) label=(.*) x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+) rough=(\d)')


def black(image, box):
    """Rows and columns that hold a black pixel inside box, as (x0, y0, x1, y1) of the ink, or None."""
    left, top, right, bottom = box
    xs, ys = [], []
    pixels = image.load()
    for y in range(top, bottom):
        for x in range(left, right):
            if pixels[x, y] < 128:
                xs.append(x)
                ys.append(y)
    return (min(xs), min(ys), max(xs), max(ys)) if xs else None


class WayBackPill:
    """The checks, for a machine: its program, the script that reads to page 3, jumps by Contents and goes back."""
    PROGRAM = PROGRAM
    ORIGIN = '1000:CONFIRM;5000:RIGHT;7000:RIGHT;'
    ORIGIN_UGLY = None  # where the ugly shell reaches the book another way
    JUMP = '9000:CONFIRM;11000:CONFIRM;13000:RIGHT;15000:CONFIRM;'
    BACK = '20000:BACK;'
    WHEN = {'origin': 8500, 'jumped': 17500, 'back': 22500, 'quit': 25000}

    def run_card(self, parts, *names, ugly=0, **settings):
        """A fresh card in the shell, run ORIGIN + the parts named, shoot at WHEN[name] for each name, quit."""
        temp = tempfile.TemporaryDirectory(prefix='cross-way-back-pill-')
        self.addCleanup(temp.cleanup)
        sd = Path(temp.name)
        (sd / '.crosspoint').mkdir()
        (sd / 'sach').mkdir()
        write_epub(sd / BOOK.lstrip('/'))
        base = {'language': 'VI', 'fontSize': 14, 'readerMenuStyle': 0, 'readerFavorites': [0], 'readerFavoriteCount': 1,
                'readerFavoritesDaDat': 1, 'sleepTimeout': 10, 'statusBarClock': 0, 'uiShell': ugly, 'readerTapTip': 0}
        (sd / '.crosspoint/settings.json').write_text(json.dumps(truoc_tenor({**base, **settings})))
        (sd / '.crosspoint/recent.json').write_text(json.dumps({'books': [{'path': BOOK, 'title': 'Link fixture'}]}))
        (sd / '.crosspoint/state.json').write_text(json.dumps({'openEpubPath': '', 'showBootScreen': False}))
        origin = self.ORIGIN_UGLY if ugly and self.ORIGIN_UGLY else self.ORIGIN
        script = origin + ''.join(getattr(self, part) for part in parts) + f"{self.WHEN['quit']}:QUIT"
        shots = [(self.WHEN[name], name) for name in names]
        env = {key: value for key, value in os.environ.items() if not key.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_NOW='1791700000',
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{sd}/{name}.bmp' for ms, name in shots))
        env.update(getattr(self, 'SIM_ENV', {}))
        result = subprocess.run([str(self.PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=240)
        log = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, log[-5000:])
        directory = os.environ.get('T4V_LOG_DIR')
        if directory:
            Path(directory).mkdir(parents=True, exist_ok=True)
            (Path(directory) / f'{type(self).__name__}.{self._testMethodName}.ugly{ugly}.txt').write_text(log)
        return log, {name: Image.open(sd / f'{name}.bmp').convert('L') for _, name in shots}

    @staticmethod
    def events(log):
        """Painted pages and pills in the order the firmware printed them."""
        found = []
        for line in log.splitlines():
            saved, pill = SAVED.search(line), PILL.search(line)
            if saved:
                found.append(('page', int(saved.group(1)), int(saved.group(2))))
            elif pill:
                found.append(('pill', int(pill.group(1)), pill.group(2), *map(int, pill.group(3, 4, 5, 6, 7))))
        return found

    def test_jump_shows_the_page_and_back_removes_it(self):
        for ugly in (0, 1):
            with self.subTest(ugly=ugly):
                self.jump_and_back(ugly)

    def jump_and_back(self, ugly):
        log, image = self.run_card(('JUMP', 'BACK'), 'origin', 'jumped', 'back', ugly=ugly)
        found = self.events(log)
        pages = [event for event in found if event[0] == 'page']
        self.assertGreaterEqual(len(pages), 5, log[-5000:])
        origin_page = pages[2][2]
        # A page's pill is printed while it is drawn, just before the line that says it was saved.
        origin = found.index(pages[2])
        self.assertEqual([event for event in found[:origin + 1] if event[0] == 'pill'], [], 'a pill before any jump')
        pills = [event for event in found[origin + 1:] if event[0] == 'pill']
        self.assertTrue(pills, 'the jump drew no pill\n' + log[-3000:])
        self.assertEqual({event[1] for event in pills}, {origin_page + 1}, f'the pill names the wrong page: {pills}')
        last_pill = max(i for i, event in enumerate(found) if event[0] == 'pill')
        last_page = max(i for i, event in enumerate(found) if event[0] == 'page')
        self.assertLess(last_pill, last_page, 'a pill was drawn after Back')
        _, page, label, x, y, width, height, rough = pills[-1]
        self.assertTrue(label.endswith(str(origin_page + 1)), label)
        self.check_frame(image['jumped'], x, y, width, height, rough)
        # The origin page returns by Back and the strip is the title again: the page and the strip left of the pill's
        # right edge are the origin's pixel for pixel (the counts further right are an estimate that moves on its own).
        for name, box in (('page', (0, 0, image['back'].width, y - 12)), ('title cell', (0, y - 4, x + width + 24, image['back'].height))):
            left = sum(a != b for a, b in zip(image['back'].crop(box).getdata(), image['origin'].crop(box).getdata()))
            self.assertEqual(left, 0, f'Back did not bring back the origin {name}')
        box = (x, y, x + width, y + height)
        changed = sum(a != b for a, b in zip(image['jumped'].crop(box).getdata(), image['origin'].crop(box).getdata()))
        self.assertGreater(changed, 40, 'the strip under the pill is the title')

    def check_frame(self, image, x, y, width, height, rough):
        # The ring is on the frame, in the strip, and nothing of it is outside the screen.
        self.assertGreaterEqual(x, 0)
        self.assertLessEqual(x + width, image.width)
        if rough:
            return
        # The ring's top band is the dotted grey of the dynamic bar: half its pixels are black.
        band = [image.getpixel((px, py)) < 128 for py in (y, y + 1) for px in range(x + 11, x + width - 11)]
        self.assertGreater(sum(band) / len(band), 0.4, 'no ring on the frame')
        # Content: chevron and text, between the ring's round ends and clear of its top and bottom bands.
        content = black(image, (x + 8, y + 3, x + width - 8, y + height - 3))
        self.assertIsNotNone(content)
        centre_y = (2 * y + height - 1) / 2
        self.assertLessEqual(abs((content[1] + content[3]) / 2 - centre_y), 0.5, f'the content is off the ring: {content}')
        left_gap = content[0] - x
        right_gap = (x + width - 1) - content[2]
        self.assertLessEqual(abs(left_gap - right_gap) / 2, 0.5, f'gaps {left_gap} {right_gap}')
        # The chevron (first 7 columns of the content) and the text are each centred on the ring too.
        chevron = black(image, (content[0], y + 3, content[0] + 7, y + height - 3))
        self.assertLessEqual(abs((chevron[1] + chevron[3]) / 2 - centre_y), 0.5, f'chevron {chevron}')
        text = black(image, (content[0] + 13, y + 3, x + width - 8, y + height - 3))
        self.assertLessEqual(abs((text[1] + text[3]) / 2 - centre_y), 0.5, f'text {text}')

    def test_empty_stack_draws_no_pill(self):
        for ugly in (0, 1):
            with self.subTest(ugly=ugly):
                log, _ = self.run_card((), 'origin', ugly=ugly)
                self.assertGreaterEqual(len([e for e in self.events(log) if e[0] == 'page']), 3, log[-3000:])
                self.assertEqual([event for event in self.events(log) if event[0] == 'pill'], [])

    def test_hidden_title_draws_no_pill(self):
        # Clock and battery alone (the default bar without a title), and the slot bar with no title cell.
        for ugly in (0, 1):
            for mode in (1, 6):
                with self.subTest(ugly=ugly, readerStatusBarMode=mode):
                    log, _ = self.run_card(('JUMP',), 'jumped', ugly=ugly, readerStatusBarMode=mode)
                    self.assertGreaterEqual(len([e for e in self.events(log) if e[0] == 'page']), 4, log[-3000:])
                    self.assertEqual([event for event in self.events(log) if event[0] == 'pill'], [], log[-3000:])


class WayBackPillTest(WayBackPill, unittest.TestCase):
    pass


if __name__ == '__main__':
    unittest.main(verbosity=2)
