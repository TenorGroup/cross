"""v1.0.52: the back-stack of followed links outlives the reader (links.bin).

Following a footnote link and leaving the book (Home, sleep) used to bring the book back on the
page the link led to with no way back: the reader wrote the page the link was tapped on over the
saved progress, and the stack died with the activity. Now the progress keeps the page on screen
and the stack goes to <cache>/links.bin, read again on open. The card is touched only after the
first frame: the file is read on open and removed once the page is up (so an unclean shutdown
cannot bring a stale stack back); the exit writes it after the next screen's first frame, or at
once when the device is going to sleep. Preview (Hold Select on Home) neither reads nor removes it.

Page identity comes from the firmware's own `Progress saved: spine=` line, one per painted page.
Card order comes from the simulator's card trace (CROSSPOINT_SIM_SD_TRACE, `[SDW]` lines on the
same stream as the firmware log), not from anything the firmware says about itself.
"""
import json
import os
import re
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

from PIL import Image

from cai_dat_truoc_tenor import truoc_tenor

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
BOOK = '/sach/lien-ket.epub'
FOOTNOTES = 4  # shortPwrBtn: a short power press lists the footnotes of the page
SAVED = re.compile(r'Progress saved: spine=(\d+) offset=\d+ page=(\d+)')
ENTER = re.compile(r'Entering activity: EpubReader\s*$', re.M)
EXIT = re.compile(r'Exiting activity: EpubReader\s*$', re.M)
PAINTED = re.compile(r'Rendered page in')
HOME_FRAME = re.compile(r'\[HOME\] Frame row=')

FILL = ('Day la mot doan van du dai de sach sinh ra nhieu trang khi lat. ' * 6)


def write_epub(path):
    """Two chapters; the first page of chapter 1 holds a link to a note in chapter 2."""
    c1 = ('<p>Mo dau <a href="c2.xhtml#n1">1</a> roi doan van tiep theo.</p>' +
          ''.join(f'<p>{FILL}</p>' for _ in range(24)))
    c2 = '<p id="n1">Chu thich so mot.</p>' + ''.join(f'<p>{FILL}</p>' for _ in range(6))
    with zipfile.ZipFile(path, 'w') as epub:
        epub.writestr('mimetype', 'application/epub+zip')
        epub.writestr('META-INF/container.xml',
                      '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" '
                      'version="1.0"><rootfiles><rootfile full-path="book.opf" '
                      'media-type="application/oebps-package+xml"/></rootfiles></container>')
        epub.writestr('book.opf',
                      '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="2.0" '
                      'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                      '<dc:title>Link fixture</dc:title><dc:identifier id="id">links-v1052</dc:identifier>'
                      '<dc:language>vi</dc:language></metadata><manifest>'
                      '<item id="c1" href="c1.xhtml" media-type="application/xhtml+xml"/>'
                      '<item id="c2" href="c2.xhtml" media-type="application/xhtml+xml"/>'
                      '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest>'
                      '<spine toc="ncx"><itemref idref="c1"/><itemref idref="c2"/></spine></package>')
        epub.writestr('toc.ncx',
                      '<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
                      '<head/><docTitle><text>Link fixture</text></docTitle><navMap>'
                      '<navPoint id="n1" playOrder="1"><navLabel><text>Mot</text></navLabel><content src="c1.xhtml"/></navPoint>'
                      '<navPoint id="n2" playOrder="2"><navLabel><text>Hai</text></navLabel><content src="c2.xhtml"/></navPoint>'
                      '</navMap></ncx>')
        for name, body in (('c1', c1), ('c2', c2)):
            epub.writestr(f'{name}.xhtml',
                          '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head>'
                          f'<title>{name}</title></head><body>{body}</body></html>')


def spines(text):
    return [int(m.group(1)) for m in SAVED.finditer(text)]


class NganXepLinkTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-ngan-xep-link-v1052-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'sach').mkdir()
        write_epub(self.sd / BOOK.lstrip('/'))
        self.settings = truoc_tenor({'language': 'EN', 'fontSize': 14, 'shortPwrBtn': FOOTNOTES,
                                     'sleepTimeout': 10, 'clockHasBeenSynced': 1, 'clockUtcOffsetQ': 48})
        (self.store / 'settings.json').write_text(json.dumps(self.settings))
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': BOOK, 'title': 'Link fixture'}]}))

    def run_sim(self, before, after=None, wake=False, shots=()):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_SD_TRACE='1',
                   CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(before))
        if shots:
            env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join(f'{ms}:{self.sd / (name + ".bmp")}' for ms, name in shots)
        if wake:
            env.update(CROSSPOINT_SIM_WAKE_REASON='power', CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE=';'.join(after))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=240)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-4000:])
        return log

    def links_files(self):
        return sorted(self.store.glob('epub_*/links.bin'))

    # From Home: open the book, follow the footnote, leave with a long Back (a short one would
    # return to the page the link was on), open it again. 'reopen' lists what happens next.
    FOLLOW_AND_HOME = ['1000:CONFIRM', '5000:POWER', '9000:BACK:1300']

    def test_reopen_after_a_link_shows_the_note_and_back_returns(self):
        shots = [(3500, 'goc'), (7500, 'chu-thich'), (20000, 'mo-lai'), (25000, 'back')]
        log = self.run_sim([*self.FOLLOW_AND_HOME, '17000:CONFIRM', '22000:BACK', '27000:QUIT'], shots=shots)
        opens = [m.start() for m in ENTER.finditer(log)]
        self.assertEqual(len(opens), 2, log[-4000:])
        first = spines(log[opens[0]:opens[1]])
        self.assertEqual(first[:2], [0, 1], f'the link was not followed: {first}')
        second = spines(log[opens[1]:])
        # The reopened book shows the note's chapter; Back goes to the page the link was tapped on.
        self.assertEqual(second[:1], [1], f'reopened on the wrong page: {second}')
        self.assertEqual(second[1:2], [0], f'Back did not return to the link: {second}')
        # The same from the screen itself: the pixels of the reopened page are the note page's, and
        # Back brings back the first page's.
        # The strip under the text (clock, battery, percentage) is left out.
        image = {}
        for _, name in shots:
            with Image.open(self.sd / f'{name}.bmp') as shot:
                image[name] = shot.convert('L').crop((0, 0, shot.width, shot.height - 40)).tobytes()
        self.assertNotEqual(image['goc'], image['chu-thich'])
        self.assertEqual(image['mo-lai'], image['chu-thich'], 'the reopened page is not the note')
        self.assertEqual(image['back'], image['goc'], 'Back did not return to the page of the link')

    def test_sleep_and_wake_keep_the_way_back(self):
        # The wake opens the book that was open; the first run starts in it too.
        (self.store / 'state.json').write_text(json.dumps(
            {'showBootScreen': False, 'openEpubPath': BOOK, 'lastSleepFromReader': True}))
        self.settings['wakeIntoBook'] = 1
        (self.store / 'settings.json').write_text(json.dumps(self.settings))
        log = self.run_sim(['4000:POWER', '8000:SLEEP', '12000:POWER'],
                           ['5000:BACK', '10000:QUIT'], wake=True)
        parts = [m.start() for m in ENTER.finditer(log)]
        self.assertGreaterEqual(len(parts), 2, log[-4000:])
        after = spines(log[parts[-1]:])
        self.assertEqual(after[:2], [1, 0], f'wake / Back: {after}')

    def test_reopen_reads_the_card_only_after_the_first_frame(self):
        log = self.run_sim([*self.FOLLOW_AND_HOME, '17000:CONFIRM', '22000:BACK', '27000:QUIT'])
        opens = [m.start() for m in ENTER.finditer(log)]
        reopened = log[opens[1]:]
        painted = PAINTED.search(reopened)
        self.assertIsNotNone(painted, reopened[-3000:])
        before = reopened[:painted.start()]
        self.assertNotIn('links.bin', before, 'the card was written before the first frame:\n' + before[-2000:])
        # The stack was read, so the file goes once the page is up.
        self.assertRegex(reopened[painted.start():], r'\[SDW\] remove \S*links\.bin')

    def test_leaving_with_a_stack_writes_after_the_next_frame(self):
        log = self.run_sim([*self.FOLLOW_AND_HOME, '17000:QUIT'])
        left = [m.start() for m in EXIT.finditer(log)][0]
        tail = log[left:]
        frame = HOME_FRAME.search(tail)
        self.assertIsNotNone(frame, tail[-3000:])
        wrote = re.search(r'\[SDW\] \S+ \S*links\.bin', tail)
        self.assertIsNotNone(wrote, 'the stack was not written\n' + tail[-3000:])
        self.assertLess(frame.start(), wrote.start(), 'the stack went to the card before Home was drawn')
        # Written whole under a temporary name, then renamed into place.
        self.assertRegex(tail, r'\[SDW\] write \S*links\.bin\.tmp')
        self.assertRegex(tail, r'\[SDW\] rename \S*links\.bin\.tmp \S*links\.bin')
        self.assertEqual(len(self.links_files()), 1)

    def test_preview_does_not_touch_the_stack(self):
        self.run_sim([*self.FOLLOW_AND_HOME, '17000:QUIT'])
        saved = self.links_files()
        self.assertEqual(len(saved), 1)
        before = saved[0].read_bytes()
        # The File card opens the browser; holding Down on the book previews it (as test_quotes_preview
        # drives it); Select in the preview opens it for real; Back follows the link back.
        log = self.run_sim(['1600:DOWN', '2400:CONFIRM', '4000:DOWN:900', '8000:CONFIRM', '13000:BACK', '18000:QUIT'])
        opens = [m.start() for m in ENTER.finditer(log)]
        self.assertEqual(len(opens), 2, 'one preview, then the real reader\n' + log[-3000:])
        self.assertIn('Preview: ' + BOOK, log[opens[0]:opens[1]])
        preview = log[opens[0]:opens[1]]
        self.assertNotIn('links.bin', preview, 'the preview touched the stack:\n' + preview[-2000:])
        self.assertEqual(spines(log[opens[1]:])[:2], [1, 0], 'Back after the preview did not follow the link back')
        self.assertEqual(before[0], 1)

    def test_reading_without_a_link_leaves_no_file(self):
        log = self.run_sim(['1000:CONFIRM', '5000:RIGHT', '8000:BACK', '12000:QUIT'])
        self.assertNotIn('links.bin', log)
        self.assertEqual(self.links_files(), [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
