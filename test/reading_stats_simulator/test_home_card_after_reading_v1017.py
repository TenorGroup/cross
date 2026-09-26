"""v1.0.17 Recent card after reading: Home lands on the book just read.

The Recent tab shows one book at a time; its cursor is kept by Home's navigation memory while
another screen is up. Reading a book moves it to the front of the list, so the card Home comes
back to has to be the first one, whichever card the cursor was on when the book opened: Back on
Home opens the first book while another card is shown, and a long Back leaves the reader for
Home without the Continue landing. Before v1.0.17 that return put the old card up again, a book
other than the one just read. Stepping between cards with no book read in between keeps the card.
"""
from pathlib import Path
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
FRAME = re.compile(r'\[HOME\] Frame row=(-?\d+)')
TEXT = 'Một câu chuyện đang chờ bạn. Hôm nay chúng ta cùng nhau đọc một cuốn sách.\n' * 80


def run(name, script):
    root = Path(tempfile.mkdtemp(prefix='home-card-after-reading-'))
    try:
        sd = root / 'sd'
        store = sd / '.crosspoint'
        store.mkdir(parents=True)
        for book in ('dau', 'giua', 'cuoi'):
            (sd / f'{book}.txt').write_text(TEXT)
        books = [dict(path=f'/{book}.txt', title=title, author='A', coverBmpPath='')
                 for book, title in (('dau', 'Sách đầu'), ('giua', 'Sách giữa'), ('cuoi', 'Sách cuối'))]
        (store / 'recent.json').write_text(json.dumps(dict(books=books)))
        (store / 'settings.json').write_text(json.dumps(dict(language='VI', uiTheme=4, sleepScreen=8)))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script)
        run = subprocess.run([str(PROGRAM)], env=env, cwd=REPO, capture_output=True, text=True, timeout=60)
        log = run.stdout + run.stderr
        assert run.returncode == 0, log[-3000:]
        first = json.loads((store / 'recent.json').read_text())['books'][0]['path']
        return log, first
    finally:
        shutil.rmtree(root, ignore_errors=True)


def last_home_row(log):
    section = log.rsplit('Entering activity: Home', 1)[1]
    rows = FRAME.findall(section)
    assert rows, section[-3000:]
    return int(rows[-1])


class HomeCardAfterReading(unittest.TestCase):
    def test_back_opens_first_book(self):
        # Card two is shown; Back on Home opens the first book; a long Back comes home.
        log, first = run('back', '1000:RIGHT;2000:BACK;5000:BACK:1400;8500:QUIT')
        assert log.count('Entering activity: TxtReader') == 1, log[-3000:]
        assert first == '/dau.txt', first
        row = last_home_row(log)
        assert row == 1, f'Home came back on card {row}, the book just read is card 1'

    def test_confirm_opens_shown_book(self):
        # Card three is opened; it is now the first book, and the card Home comes back to.
        log, first = run('confirm', '1000:RIGHT;1600:RIGHT;2400:CONFIRM;5400:BACK:1400;8900:QUIT')
        assert first == '/cuoi.txt', first
        row = last_home_row(log)
        assert row == 1, f'Home came back on card {row}, the book just read is card 1'


if __name__ == '__main__':
    unittest.main()
