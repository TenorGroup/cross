"""Fixtures for the tenor/ugly simulator tests: a card with books, a statistics file, and a run helper."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
ARTIFACTS = os.environ.get('UGLY_SHOTS')  # a directory: keep the screenshots as PNG
BOOKS = [('Kidnapped', 'b0.txt'), ('Sách thử 5000 chương có bìa', 'b1.txt'), ('Truyện Kiều', 'b2.txt'),
         ('Dế Mèn phiêu lưu ký', 'b3.txt'), ('Số đỏ', 'b4.txt')]
# The sleep sentence depends on the day, and the clock must not draw in the corner (uiShellClockOnce says the
# one-time turn on of a legacy file is done, so the hidden clock stays hidden).
SETTINGS = {'tenorPresetVersion': 1, 'language': 'VI', 'sleepTimeout': 10, 'clockShowInHeader': 0, 'uiShellClockOnce': 1}


def yesterday():
    return int((datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(days=1)).strftime('%Y%m%d'))


class Card:
    """A temporary SD card. `shell` is the uiShell value, `books` the recent list (title, file), `files` more names in the root."""

    def __init__(self, shell=1, books=BOOKS, stats=True, files=(), **settings):
        self.temp = tempfile.TemporaryDirectory(prefix='cross-ugly-')
        self.sd = Path(self.temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        recent = []
        for i, (title, name) in enumerate(books):
            (self.sd / name).write_text(('Fixture text %d. ' % i) * 400)
            recent.append({'path': '/' + name, 'title': title, 'author': 'R. L. Stevenson'})
        for name in files:
            (self.sd / name).write_text('x')
        (self.store / 'recent.json').write_text(json.dumps({'books': recent}))
        merged = dict(SETTINGS, uiShell=shell, **settings)
        (self.store / 'settings.json').write_text(json.dumps(merged))
        if stats:
            (self.store / 'reading-stats.json').write_text(json.dumps({'ngay': [[yesterday(), 12, 34]]}))

    def close(self):
        self.temp.cleanup()

    def settings(self):
        return json.loads((self.store / 'settings.json').read_text())

    def run(self, script, shots=(), timeout=60, **env_extra):
        """Runs the simulator on the card. `shots` is [(ms, name)]. Returns (log, {name: PIL image})."""
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=script, **env_extra)
        if shots:
            env['CROSSPOINT_SIM_SCREENSHOTS'] = ';'.join('%d:%s' % (ms, self.sd / (name + '.bmp')) for ms, name in shots)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=timeout)
        log = run.stdout + run.stderr
        assert run.returncode == 0, log[-2000:]
        images = {}
        for _, name in shots:
            path = self.sd / (name + '.bmp')
            if path.exists():
                images[name] = Image.open(path).convert('1')
                if ARTIFACTS:
                    Path(ARTIFACTS).mkdir(parents=True, exist_ok=True)
                    images[name].save(Path(ARTIFACTS) / (name + '.png'))
        return log, images


def digest(image):
    return hashlib.sha256(image.tobytes()).hexdigest()


def ink(image, box=None):
    """Black pixels of an image (or a box of it)."""
    part = image.crop(box) if box else image
    return sum(1 for p in part.getdata() if p == 0)


def entered(log):
    """The activities entered, in order."""
    return re.findall(r'Entering activity: (\w+)', log)


def notebook_pages(log):
    """The page id of every notebook frame drawn, in order."""
    return [int(n) for n in re.findall(r'Notebook frame page=(\d+)', log)]
