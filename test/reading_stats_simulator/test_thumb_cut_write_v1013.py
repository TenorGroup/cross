"""v1.0.13 a cover thumbnail cut off mid-write is written again.

The reader wrote `thumb_<height>.bmp` straight under its final name, and decides a thumbnail is
missing by whether that name exists. Power lost (or the X3 put to sleep) during the decode left a
short file under the real name, and the book kept a broken cover for good. The thumbnail now goes
to `<name>.tmp` and takes its real name only once complete. The case kills the simulator while the
first thumbnail is being written, then opens and closes the book again.
"""
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest
import zipfile

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
FIXTURE = REPO / 'test/epubs/test_dictionary_synonyms.epub'
PATH = '/sach/bia-lon.epub'


def big_cover(width=2000, height=3000):
    """A large cover within the decoder's 2048 x 3072 limit: its decode keeps the file open long enough to cut."""
    image = Image.new('L', (width, height))
    draw = ImageDraw.Draw(image)
    for y in range(0, height, 3):
        draw.line((0, y, width, y), fill=(y * 7919) % 256)
    out = io.BytesIO()
    image.convert('RGB').save(out, 'JPEG', quality=95)
    return out.getvalue()


def book_with_big_cover(target):
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(FIXTURE) as source, zipfile.ZipFile(target, 'w') as book:
        for item in source.infolist():
            data = source.read(item.filename)
            if item.filename == 'book.opf':
                text = data.decode('utf-8')
                text = text.replace('</metadata>', '<meta name="cover" content="cover"/></metadata>')
                text = text.replace('</manifest>', '<item id="cover" href="cover.jpg" media-type="image/jpeg"/>'
                                    '</manifest>')
                data = text.encode('utf-8')
            compress = zipfile.ZIP_STORED if item.filename == 'mimetype' else zipfile.ZIP_DEFLATED
            book.writestr(item, data, compress_type=compress)
        book.writestr('cover.jpg', big_cover(), compress_type=zipfile.ZIP_STORED)


class ThumbCutWriteTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='cross-thumb-cut-')
        self.addCleanup(temp.cleanup)
        self.sd = Path(temp.name)
        self.store = self.sd / '.crosspoint'
        self.store.mkdir()
        (self.store / 'settings.json').write_text(json.dumps({'language': 'VI', 'uiTheme': 4, 'sleepTimeout': 120}))
        (self.store / 'recent.json').write_text(json.dumps(
            {'books': [{'path': PATH, 'title': 'Bìa lớn', 'author': 'Tenor', 'coverBmpPath': ''}]},
            ensure_ascii=False), encoding='utf-8')
        book_with_big_cover(self.sd / PATH.lstrip('/'))

    def env(self, events):
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_INPUT_SCRIPT=events)
        return env

    def cut_during_first_thumbnail(self):
        """Open the book, go back to Home, and kill the program once a 356 px thumbnail file appears."""
        process = subprocess.Popen([str(PROGRAM)], cwd=REPO, env=self.env('1500:CONFIRM;5000:BACK;60000:QUIT'),
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + 60
        try:
            while time.monotonic() < deadline and process.poll() is None:
                if list(self.store.glob('epub_*/thumb2_356.bmp*')):
                    process.send_signal(signal.SIGKILL)
                    break
                time.sleep(0.0005)
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
        self.assertTrue(list(self.store.glob('epub_*/thumb2_356.bmp*')), 'no thumbnail write began')

    def test_cut_thumbnail_is_written_again_whole(self):
        self.cut_during_first_thumbnail()
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=self.env('1500:CONFIRM;5000:BACK;30000:QUIT'),
                             capture_output=True, text=True, timeout=180)
        log = run.stdout + run.stderr
        self.assertEqual(run.returncode, 0, log[-4000:])
        thumbs = sorted(self.store.glob('epub_*/thumb2_356.bmp'))
        self.assertEqual(len(thumbs), 1, log[-4000:])
        with Image.open(thumbs[0]) as image:
            image.load()
            self.assertEqual(image.height, 356, 'the thumbnail is not a whole image')
        self.assertEqual(list(self.store.glob('epub_*/*.tmp')), [], 'a cut write was left behind')


if __name__ == '__main__':
    unittest.main()
