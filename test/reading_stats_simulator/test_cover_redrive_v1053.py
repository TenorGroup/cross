"""v1.0.53 Back from a book drives the Recent card's cover again, and only the cover.

On the X3 the last page of a book is shown with gray levels. Home's first frame is a fast refresh,
and the UC8279 drives only the pixels whose black and white changes, so where the page's levels
and the cover agree the old page stays on the glass, inside the cover's halftone. Home's first
frame after a book now gives the cover rectangle its inverse as the controller's previous frame
and refreshes once: a full waveform on the UC8279 (it still drives only the cover), a fast one on
the UC8253 and the X4 Pro, where a full refresh flashes the whole glass.

Each journey reads the book, leaves it, then makes Home draw again with the same cover. The panel
trace (scripts/patch_simulator_panel_trace.py) must show one redrive: a cleanup whose frame is the
Home frame with exactly the cover's bytes inverted, then one refresh of the unchanged Home frame.
On the UC8279 the trace is replayed over the glass model (glass_model.py): before the redrive the
cover holds pixels of the page, after it none.
"""

import json
import os
from pathlib import Path
import re
import tempfile
import unittest
import zipfile

import glass_model

PROGRAM = glass_model.PROGRAM


def sibling(name, env):
    """A build named in `env`, else the one beside the UC8279 program (as the gates copy them)."""
    if os.environ.get(env):
        return Path(os.environ[env])
    near = PROGRAM.parent / name
    return near if near.is_file() else glass_model.REPO / f'.pio/build/{name}/program'


UC8253 = sibling('simulator_x3', 'SLEEP_UC8253_PROGRAM')
X4PRO = sibling('simulator_x4pro', 'X4PRO_PROGRAM')
BOOK = '/books/doc.epub'
OP_DISPLAY, OP_CLEANUP = 2, 9
FULL, FAST = 0, 2
REDRIVE = re.compile(r'Redrive region x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+)')
# Panel height and row bytes by frame size: X3 792 x 528, X4 Pro 800 x 480.
PANELS = {99 * 528: (99, 528), 100 * 480: (100, 480)}


def write_book(path):
    body = ''.join(f'<p>Paragraph {n:03d}. ' + 'The page keeps its words while the reader turns. ' * 6 + '</p>'
                   for n in range(60))
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr('mimetype', 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
                   'version="1.0"><rootfiles><rootfile full-path="book.opf" '
                   'media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">'
                   '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Cover redrive</dc:title>'
                   '<dc:identifier id="id">cover-redrive</dc:identifier><dc:language>en</dc:language></metadata>'
                   '<manifest><item id="c" href="c.xhtml" media-type="application/xhtml+xml"/></manifest>'
                   '<spine><itemref idref="c"/></spine></package>')
        z.writestr('c.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>c</title></head><body>'
                   '<h1>Chapter</h1>' + body + '</body></html>')


def read_trace(path):
    data = Path(path).read_bytes()
    calls, at = [], 0
    while at < len(data):
        length = int.from_bytes(data[at + 8:at + 12], 'little')
        calls.append((data[at + 1], data[at + 2], data[at + 12:at + 12 + length]))
        at += 12 + length
    return calls


def changed_box(one, other, row_bytes):
    """(x0, x1, y0, y1) in panel pixels of where two frames differ; None when alike."""
    rows = [i // row_bytes for i, (p, q) in enumerate(zip(one, other)) if p != q]
    if not rows:
        return None
    cols = [i % row_bytes for i, (p, q) in enumerate(zip(one, other)) if p != q]
    return min(cols) * 8, max(cols) * 8 + 7, min(rows), max(rows)


def cover_box(x, y, w, h, height):
    """The logical cover rectangle on the panel (portrait: panel x = y, panel y = height - 1 - x),
    widened to whole bytes as the redrive inverts it."""
    return y // 8 * 8, (y + h - 1) // 8 * 8 + 7, height - x - w, height - 1 - x


def redrives(calls):
    """Indices of cleanups that hand the controller the frame just shown with some bytes inverted."""
    return [i for i in range(1, len(calls) - 1)
            if calls[i][0] == OP_CLEANUP and calls[i - 1][0] == OP_DISPLAY and calls[i + 1][0] == OP_DISPLAY
            and calls[i][2] != calls[i - 1][2] and len(calls[i][2]) == len(calls[i - 1][2])]


class CoverRedriveTest(unittest.TestCase):
    def journey(self, program, script, keep):
        if not Path(program).is_file():
            self.skipTest(f'no simulator build at {program}')
        sd = Path(keep) / 'sd'
        store = sd / '.crosspoint'
        store.mkdir(parents=True)
        (sd / 'books').mkdir()
        write_book(sd / BOOK.lstrip('/'))
        (store / 'settings.json').write_text(json.dumps({'language': 'EN', 'wakeIntoBook': 1}))
        (store / 'state.json').write_text(
            json.dumps({'showBootScreen': False, 'openEpubPath': BOOK, 'lastSleepFromReader': True}))
        (store / 'recent.json').write_text(json.dumps({'books': [{'path': BOOK, 'title': 'Cover redrive'}]}))
        (store / 'sleep_frame.bin').write_bytes(b'\xff' * glass_model.PANEL_BYTES)
        trace = Path(keep) / 'panel.trace'
        saved = glass_model.PROGRAM
        glass_model.PROGRAM = Path(program)
        try:
            code, log = glass_model.run_simulator(sd, trace, script, wake='power', timeout=120)
        finally:
            glass_model.PROGRAM = saved
        self.assertEqual(code, 0, log[-3000:])
        self.assertIn('Exiting activity: EpubReader', log)
        return read_trace(trace), log, trace

    def check_one_cover_redrive(self, calls, log, mode):
        after_book = log[log.index('Exiting activity: EpubReader'):]
        rects = REDRIVE.findall(after_book)
        self.assertEqual(len(rects), 1, 'one cover redrive after the book\n' + after_book[-3000:])
        self.assertEqual(len(REDRIVE.findall(log)), 1, 'no redrive anywhere else')
        found = redrives(calls)
        self.assertEqual(len(found), 1, 'one redrive in the panel trace')
        at = found[0]
        home, handed, again = calls[at - 1][2], calls[at][2], calls[at + 1]
        row_bytes, height = PANELS[len(home)]
        x, y, w, h = map(int, rects[0])
        self.assertEqual(changed_box(home, handed, row_bytes), cover_box(x, y, w, h, height),
                         'the inverted bytes are the cover rectangle, nothing else')
        self.assertTrue(all(p ^ q in (0, 0xFF) for p, q in zip(home, handed)), 'whole bytes inverted')
        self.assertEqual(again[1], mode, 'refresh mode of the redrive')
        self.assertEqual(again[2], home, 'the redrive shows the Home frame unchanged')
        # Home went to another tab and came back to the same card, and drove nothing again.
        later = [c for c in calls[at + 2:] if c[0] == OP_DISPLAY]
        self.assertTrue(later and later[-1][2] == home, 'Home drew the same card again after the redrive')
        self.assertFalse([c for c in calls[at + 2:] if c[0] == OP_CLEANUP], 'a later Home frame redrove')
        return at, (x, y, w, h), height

    def test_uc8279_glass_keeps_no_page_in_the_cover(self):
        # The suite also runs with the UC8253 build as its program: that chip takes the fast redrive
        # and has no glass model.
        uc8279 = b'XTEINK X3 (UC8279d)' in Path(PROGRAM).read_bytes()
        with tempfile.TemporaryDirectory(prefix='cross-cover-8279-') as keep:
            calls, log, trace = self.journey(PROGRAM, '4000:RIGHT;6000:RIGHT;8000:BACK;11000:DOWN;12500:UP;14500:QUIT', keep)
            at, rect, height = self.check_one_cover_redrive(calls, log, FULL if uc8279 else FAST)
            if not uc8279:
                return
            tool = glass_model.build(Path(keep) / 'tool')
            before, after = Path(keep) / 'before.pgm', Path(keep) / 'after.pgm'
            records = glass_model.replay(tool, trace, [(at - 1, before), (at + 1, after)])
            self.assertEqual([r for r in records if 'anomaly' in r], [])
            self.assertEqual(records[at + 1]['bank'], 'gc')
            x0, x1, y0, y1 = cover_box(*rect, height)
            frame = calls[at - 1][2]

            def left_in_cover(pgm):
                pixels = pgm.read_bytes()[-792 * 528:]
                return sum(pixels[y * 792 + x] != (255 if frame[y * 99 + x // 8] & (0x80 >> (x & 7)) else 0)
                           for y in range(y0, y1 + 1) for x in range(x0, x1 + 1))

            ghost = left_in_cover(before)
            print(f'cover {rect}: page pixels left before the redrive {ghost}, after {left_in_cover(after)}; '
                  f'outside the cover after it {records[at + 1]["glass_vs_frame"]}')
            self.assertGreater(ghost, 0, 'the journey leaves no gray in the cover: it tests nothing')
            self.assertEqual(left_in_cover(after), 0)

    def test_uc8253_redrives_the_cover_with_a_fast_refresh(self):
        with tempfile.TemporaryDirectory(prefix='cross-cover-8253-') as keep:
            calls, log, _ = self.journey(UC8253, '4000:RIGHT;6000:RIGHT;8000:BACK;11000:DOWN;12500:UP;14500:QUIT', keep)
            self.check_one_cover_redrive(calls, log, FAST)

    def test_x4pro_redrives_the_cover_with_a_fast_refresh(self):
        # Home from the book, then the Settings tab and back to Recent: the same cover drawn again.
        with tempfile.TemporaryDirectory(prefix='cross-cover-x4p-') as keep:
            calls, log, _ = self.journey(
                X4PRO, '3000:RIGHT;5000:RIGHT;7000:HOME;10000:TAP:423,754;12000:TAP:56,754;14000:QUIT', keep)
            self.check_one_cover_redrive(calls, log, FAST)


if __name__ == '__main__':
    unittest.main()
