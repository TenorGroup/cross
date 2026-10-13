"""tenor/ugly on the X4 Pro: the reader bar writes the 4 tool names whole.

Founder, v1.0.60: the bar at the foot of the reader menu wrote "Yeu tN", "Muc lN" (the names cut where the 92 px
cell ended, a scrawl in place of the rest). The 4 names now stand each at its own width, the gaps between them equal,
inside the bar's tool room (x 90 to 458); names that do not fit shrink together.
One run: the book opened from the diary, the text menu opened by a tap on the foot, then a tap on each tool.
 1. Every name is written whole (the simulator log says what was written: the name and what was drawn).
 2. The names stand left to right, 6 px apart at least, inside the bar.
 3. The ink of the bar (the names) lies in the boxes the log names, and fills them.
 4. The circle of the chosen tool stays by its name, centred on it: it does not reach a neighbour's name.
 5. A tap on the last pixels of a name chooses that tool (the tap cells hold the names).
X4PRO_PROGRAM picks the simulator (pio run -e simulator_x4pro).
"""
import json
import os
import re
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(os.path.abspath(__file__)).parents[2]
PROGRAM = Path(os.environ.get('X4PRO_PROGRAM', REPO / '.pio/build/simulator_x4pro/program'))
BAR = (730, 778)   # rows of the names in the bar (the bar stands 724 to 784)
ACTIVE = [2, 1, 0, 3]  # the tool chosen in each shot: the foot tap opens Text, then Contents, Favorites, More
SHOTS = [5800, 7800, 9800, 11800]
SCRIPT = '2500:TAP:200,495;4500:TAP:240,775;6500:TAP:228,754;8500:TAP:136,754;10500:TAP:412,754'
ROOM = (84, 464)


def book(path):
    body = ''.join('<p>Paragraph %04d. ' % n + 'The reader keeps this place while the text menu changes. ' * 5 + '</p>' for n in range(60))
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Reader bar</dc:title><dc:identifier id="id">r</dc:identifier><dc:language>en</dc:language></metadata><manifest><item id="c" href="c.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="c"/></spine></package>')
        z.writestr('c.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>R</title></head><body><h1>Chapter</h1>' + body + '</body></html>')


def run(script=SCRIPT, shots=SHOTS):
    """(log, [ink of the bar's rows in each shot])."""
    with tempfile.TemporaryDirectory(prefix='x4pro-ugly-bar-') as tmp:
        sd = Path(tmp)
        store = sd / '.crosspoint'
        store.mkdir()
        (sd / 'books').mkdir()
        book(sd / 'books/journey.epub')
        (store / 'settings.json').write_text(json.dumps({'textSpacingVersion': 3, 'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1,
            'tenorPresetVersion': 1, 'uiShellSleepMemo': 0, 'language': 'VI', 'uiTextSize': 0, 'wakeIntoBook': 1, 'fontSize': 18,
            'fontFamily': 0, 'readerMenuStyle': 1, 'uiShell': 1, 'sleepTimeout': 10, 'readerTapTip': 0}))
        (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': '/books/journey.epub', 'lastSleepFromReader': True}))
        (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/books/journey.epub', 'title': 'Reader bar'}]}))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
                   CROSSPOINT_SIM_INPUT_SCRIPT=script + ';%d:QUIT' % (max(shots) + 700), CROSSPOINT_SIM_NOW='1790000000',
                   CROSSPOINT_SIM_MARK_SEED='12345', CROSSPOINT_SIM_UGLY_SCRAP='0',
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join('%d:%s/%d.bmp' % (ms, sd, ms) for ms in shots))
        done = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=150)
        pages = [np.asarray(Image.open(sd / ('%d.bmp' % ms)).convert('L'))[BAR[0]:BAR[1]] < 128 for ms in shots]
        return done.stdout + done.stderr, pages


def tools(log):
    """The tool lines of the log, in groups of 4 (one per frame): [(name, shown, x, width)]."""
    rows = re.findall(r'part=readertool tool=(\d) name=(.*?) shown=(.*?) x=(-?\d+) w=(\d+)', log)
    return [[(n, s, int(x), int(w)) for _, n, s, x, w in rows[i:i + 4]] for i in range(0, len(rows) - 3, 4)]


class UglyReaderBarTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        log, cls.pages = run()
        cls.frames = tools(log)

    def test_1_every_name_is_written_whole(self):
        self.assertGreater(len(self.frames), 0, 'no reader bar frame in the log')
        for frame in self.frames:
            self.assertEqual(len(frame), 4)
            for name, shown, _, _ in frame:
                self.assertEqual(shown, name, f'the name {name!r} was written as {shown!r}')

    def test_2_the_names_stand_apart_inside_the_bar(self):
        for frame in self.frames:
            boxes = [(x, x + w) for _, _, x, w in frame]
            self.assertGreaterEqual(boxes[0][0], ROOM[0])
            self.assertLessEqual(boxes[-1][1], ROOM[1])
            for (_, end), (start, _) in zip(boxes, boxes[1:]):
                self.assertGreaterEqual(start - end, 6, f'names too close: {boxes}')

    def test_3_the_ink_of_the_names_lies_in_their_boxes(self):
        boxes = [(x, x + w) for _, _, x, w in self.frames[-1]]
        common = np.sum(self.pages, axis=0) >= len(SHOTS) - 1
        columns = common[:, ROOM[0]:ROOM[1]].any(axis=0)
        inside = np.zeros_like(columns)
        for a, b in boxes:
            inside[max(0, a - 4 - ROOM[0]):b + 4 - ROOM[0]] = True
            self.assertGreaterEqual(int(columns[a - ROOM[0]:b - ROOM[0]].sum()), (b - a) // 2, f'the name at {a} to {b} is a blank')
        self.assertEqual(int((columns & ~inside).sum()), 0, 'ink in the bar outside the names')

    def test_4_the_circle_stays_by_its_name(self):
        boxes = [(x, x + w) for _, _, x, w in self.frames[-1]]
        common = np.sum(self.pages, axis=0) >= len(SHOTS) - 1
        for shot, tool in enumerate(ACTIVE):
            with self.subTest(tool=tool):
                ring = (self.pages[shot] & ~common)[:, ROOM[0]:ROOM[1]]
                ys, xs = np.nonzero(ring)
                self.assertGreater(len(xs), 80, 'the circle')
                a, b = boxes[tool]
                self.assertLessEqual(abs((xs.min() + xs.max()) / 2 + ROOM[0] - (a + b) / 2), 2, f'circle x {xs.min() + ROOM[0]} to {xs.max() + ROOM[0]}, name {a} to {b}')
                for other, (c, d) in enumerate(boxes):
                    if other != tool:
                        self.assertEqual(int(ring[:, c - ROOM[0]:d - ROOM[0]].sum()), 0, f'the circle of tool {tool} over name {other}')

    def test_5_a_tap_on_the_end_of_a_name_chooses_its_tool(self):
        boxes = [(x, x + w) for _, _, x, w in self.frames[-1]]
        common = np.sum(self.pages, axis=0) >= len(SHOTS) - 1
        taps = [(0, boxes[0][1] - 3), (1, boxes[1][1] - 3), (2, boxes[2][1] - 3)]  # Favorites, Contents, Text, at their names' ends
        _, pages = run('2500:TAP:200,495;4500:TAP:240,775;' + ';'.join('%d:TAP:%d,754' % (6500 + 2000 * k, x) for k, (_, x) in enumerate(taps)),
                       [7800, 9800, 11800])
        for (tool, _), page in zip(taps, pages):
            ys, xs = np.nonzero((page & ~common)[:, ROOM[0]:ROOM[1]])
            centre = (xs.min() + xs.max()) / 2 + ROOM[0]
            chosen = min(range(4), key=lambda t: abs(centre - (boxes[t][0] + boxes[t][1]) / 2))
            self.assertEqual(chosen, tool, f'a tap on the end of name {tool} chose tool {chosen}')


if __name__ == '__main__':
    unittest.main()
