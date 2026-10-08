"""The first Home frame after a gray page includes cleanup in one fast refresh."""

import json
import os
from pathlib import Path
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
FAST = 2


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

    def check_first_frame(self, program, script, diff_only):
        with tempfile.TemporaryDirectory(prefix='cover-first-frame-') as keep:
            calls, log, trace = self.journey(program, script, keep)
            self.assertNotIn('Redrive region', log, 'Home adds a second refresh')
            last_gray = max(i for i, call in enumerate(calls) if call[0] == 8)
            home_and_tabs = [c for c in calls[last_gray + 1:] if c[0] == OP_DISPLAY]
            self.assertGreaterEqual(len(home_and_tabs), 3, 'first Home, another tab, and the Recent card again')
            fused = [i for i in range(len(calls) - 1)
                     if calls[i][0] == OP_CLEANUP and calls[i + 1][0] == OP_DISPLAY
                     and len(calls[i][2]) == len(calls[i + 1][2])
                     and all(p ^ q == 255 for p, q in zip(calls[i][2], calls[i + 1][2]))]
            self.assertEqual(len(fused), int(diff_only), 'exactly one fused Home cleanup on the diff-only panel')
            if not diff_only:
                return
            at = fused[0] + 1
            self.assertEqual(calls[at][1], FAST, 'the first Home frame uses the fast waveform')
            self.assertNotEqual(calls[at - 2][2], calls[at][2], 'no Home frame before its fused refresh')
            self.assertFalse([c for c in calls[at + 1:] if c[0] == OP_CLEANUP], 'one-shot flag was consumed')
            records = glass_model.replay(glass_model.build(Path(keep) / 'tool'), trace)
            self.assertFalse([r for r in records if 'anomaly' in r])
            self.assertEqual(records[at]['refreshes'], 1)
            self.assertEqual(records[at]['bank'], 'du')
            self.assertEqual(records[at]['glass_vs_frame'], 0, 'all of Home matches the glass after its first frame')
            self.assertGreater(records[at - 2]['gray'], 0, 'the reader left real gray pixels on the glass')

    def test_uc8279_first_frame_is_clean(self):
        if not PROGRAM.is_file():
            self.skipTest(f'no simulator build at {PROGRAM}')
        diff_only = b'XTEINK X3 (UC8279d)' in PROGRAM.read_bytes()
        self.check_first_frame(PROGRAM, '4000:RIGHT;6000:RIGHT;8000:BACK;11000:DOWN;12500:UP;14500:QUIT', diff_only)

    def test_uc8253_has_no_second_refresh(self):
        self.check_first_frame(UC8253, '4000:RIGHT;6000:RIGHT;8000:BACK;11000:DOWN;12500:UP;14500:QUIT', False)

    def test_x4pro_has_no_second_refresh(self):
        self.check_first_frame(X4PRO,
                               '3000:RIGHT;5000:RIGHT;7000:HOME;10000:TAP:423,754;12000:TAP:56,754;14000:QUIT',
                               False)


if __name__ == '__main__':
    unittest.main()
