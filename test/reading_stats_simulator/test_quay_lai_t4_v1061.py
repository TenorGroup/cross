"""Return after a chapter, percent or quote jump, through the simulator's reader UI."""
import json
import os
import re
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from cai_dat_truoc_tenor import truoc_tenor
from test_ngan_xep_link_v1052 import write_epub
from test_quotes_v1011 import write_quote

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
BOOK = '/sach/lien-ket.epub'
SAVED = re.compile(r'Progress saved: spine=(\d+) offset=(\d+) page=(\d+)')


class ReturnPositionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='cross-return-t4-')
        self.addCleanup(self.temp.cleanup)
        self.sd = Path(self.temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.sd / 'sach').mkdir()
        write_epub(self.sd / BOOK.lstrip('/'))
        self.settings = truoc_tenor({'language': 'EN', 'fontSize': 14, 'readerMenuStyle': 0,
                                    'readerFavorites': [0], 'readerFavoriteCount': 1,
                                    'readerFavoritesDaDat': 1, 'sleepTimeout': 10, 'statusBarClock': 0})
        self.save_settings()
        (self.store / 'recent.json').write_text(json.dumps({'books': [{'path': BOOK, 'title': 'Link fixture'}]}))

    def save_settings(self):
        (self.store / 'settings.json').write_text(json.dumps(self.settings))

    def run_card(self, script, after=None, shots=()):
        env = {key: value for key, value in os.environ.items() if not key.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=script, CROSSPOINT_SIM_NOW='1791700000')
        if after:
            env.update(CROSSPOINT_SIM_WAKE_REASON='power', CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE=after)
        if shots:
            env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join(f'{ms}:{self.sd}/{name}.bmp' for ms, name in shots)
        result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=240)
        log = result.stdout + result.stderr
        output = os.environ.get('T4_LOG_DIR')
        if output:
            Path(output).mkdir(parents=True, exist_ok=True)
            (Path(output) / (self._testMethodName + '.txt')).write_text(log)
        self.assertEqual(result.returncode, 0, log[-5000:])
        return log

    ORIGIN = '1000:CONFIRM;5000:RIGHT;7000:RIGHT;'
    TOC = ORIGIN + '9000:CONFIRM;11000:CONFIRM;13000:RIGHT;15000:CONFIRM;'

    def positions(self, log):
        return [tuple(map(int, match.groups())) for match in SAVED.finditer(log)]

    def assert_return(self, log):
        positions = self.positions(log)
        self.assertGreaterEqual(len(positions), 5, log[-6000:])
        self.assertEqual(positions[2][0], 0, positions)
        self.assertGreater(positions[2][2], 0, positions)
        self.assertEqual(positions[3][0], 1, positions)
        self.assertEqual(positions[-1], positions[2], f'Back did not return: {positions}')

    def test_toc_returns_to_the_page_before_the_jump(self):
        self.assert_return(self.run_card(self.TOC + '20000:BACK;25000:QUIT'))

    def test_toc_return_after_font_change_contains_the_old_character(self):
        first = self.run_card(self.TOC + '20000:BACK:1300;25000:QUIT')
        origin = self.positions(first)[2]
        links = list(self.store.glob('epub_*/links.bin'))
        self.assertEqual(len(links), 1, first[-4000:])
        self.settings.update(fontSize=20, fontPointSize=20)
        self.save_settings()
        second = self.run_card('1000:CONFIRM;5000:BACK;10000:RIGHT;14000:QUIT')
        positions = self.positions(second)
        self.assertGreaterEqual(len(positions), 3, second[-5000:])
        self.assertEqual(positions[1][0], origin[0], positions)
        self.assertLessEqual(positions[1][1], origin[1], positions)
        self.assertGreater(positions[2][1], origin[1], positions)

    def test_empty_stack_back_leaves_the_book(self):
        log = self.run_card(self.ORIGIN + '9000:BACK;14000:QUIT')
        self.assertIn('Exiting activity: EpubReader', log)
        self.assertEqual(len(self.positions(log)), 3, log[-5000:])

    def test_percent_jump_then_back_returns(self):
        self.settings.update(readerFavorites=[6], readerFavoriteCount=1)
        self.save_settings()
        steps = ';'.join(f'{13000 + i * 1000}:DOWN' for i in range(6))
        log = self.run_card(self.ORIGIN + '9000:CONFIRM;11000:CONFIRM;' + steps +
                            ';21000:CONFIRM;26000:BACK;31000:QUIT')
        positions = self.positions(log)
        self.assertGreaterEqual(len(positions), 5, log[-5000:])
        self.assertNotEqual(positions[3], positions[2], positions)
        self.assertEqual(positions[-1], positions[2], positions)

    def test_sleep_keeps_the_return_position(self):
        self.settings['wakeIntoBook'] = 1
        self.save_settings()
        first = self.TOC + '20000:SLEEP;26000:POWER'
        log = self.run_card(first, '5000:BACK;10000:QUIT')
        self.assert_return(log)

    def test_old_links_file_still_returns_by_page(self):
        log = self.run_card(self.ORIGIN + '9000:BACK;14000:QUIT')
        origin = self.positions(log)[2]
        cache = list(self.store.glob('epub_*'))
        self.assertEqual(len(cache), 1)
        (cache[0] / 'links.bin').write_bytes(struct.pack('<BHH', 1, origin[0], 0))
        log = self.run_card('1000:CONFIRM;5000:BACK;10000:QUIT')
        positions = self.positions(log)
        self.assertEqual(positions[-1][::2], (0, 0), positions)

    def test_quote_jump_then_back_returns(self):
        self.settings.update(readerFavorites=[21], readerFavoriteCount=1)
        self.save_settings()
        write_quote(self.store / 'quotes', BOOK, 'Link fixture', 'Chu thich so mot.', 1, 0, 20261014, 600)
        script = self.ORIGIN + ('9000:CONFIRM;11000:CONFIRM;13000:CONFIRM;15000:CONFIRM;'
                                '17000:CONFIRM;19000:RIGHT;21000:CONFIRM;'
                                '26000:BACK;30000:BACK;35000:QUIT')
        log = self.run_card(script)
        self.assertIn('Entering activity: QuoteSelect', log, log[-6000:])
        self.assert_return(log)


if __name__ == '__main__':
    unittest.main(verbosity=2)
