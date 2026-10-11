"""Writes the cover fixtures (run once; the files are committed): a progressive 4:2:0 cover with
successive approximation and its libjpeg luma, a baseline one and a CMYK one. Generated pictures."""
import io
from pathlib import Path
import numpy as np
from PIL import Image

here = Path(__file__).resolve().parent
rng = np.random.default_rng(58)
h, w = 181, 120
y, x = np.mgrid[0:h, 0:w]
img = np.stack([x * 2, y, (x * y) % 256], -1).astype(np.int32) + rng.integers(-20, 21, (h, w, 3))
for _ in range(14):
    x0, y0 = rng.integers(0, w - 10), rng.integers(0, h - 5)
    img[y0:y0 + 3, x0:x0 + 9] = rng.integers(0, 256, 3)
rgb = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
rgb.save(here / 'cover.jpg', 'JPEG', progressive=True, quality=85, subsampling=2)
ref = Image.open(here / 'cover.jpg')
ref.draft('YCbCr', ref.size)
ref.convert('YCbCr').getchannel(0).save(here / 'cover.pgm')
rgb.save(here / 'baseline.jpg', 'JPEG', quality=85)
rgb.convert('CMYK').save(here / 'cmyk.jpg', 'JPEG', progressive=True, quality=85)
# DC of each component in a scan of its own: the X3's JPEGDEC fails on it (err=2, the first book of
# the X3 probe of 11/10/2026), so Home has no temporary cover for it at all.
import shutil, subprocess, tempfile
script = '0: 0 0 0 1;\n1: 0 0 0 0;\n2: 0 0 0 0;\n0: 1 9 0 0;\n0: 10 63 0 0;\n1: 1 63 0 0;\n2: 1 63 0 0;\n0: 0 0 1 0;\n'
with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
    f.write(script)
jpegtran = shutil.which('jpegtran') or '/opt/homebrew/bin/jpegtran'
(here / 'dcsplit.jpg').write_bytes(subprocess.run([jpegtran, '-copy', 'none', '-scans', f.name, str(here / 'cover.jpg')],
                                                  capture_output=True, check=True).stdout)
ref = Image.open(here / 'dcsplit.jpg')
ref.draft('YCbCr', ref.size)
ref.convert('YCbCr').getchannel(0).save(here / 'dcsplit.pgm')
