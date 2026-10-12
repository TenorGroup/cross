#!/usr/bin/env python3
"""X4 Pro picture viewer: a pill in the bar sets the picture as the sleep screen.

x4_sleep_picture.py <simulator_x4pro program> <output dir>
Home File card -> folder /anh -> a.bmp; a tap on the pill at the bar's right end copies it to /sleep.bmp.
RED on v1.0.58: only Select set it, and the X4 Pro has no Select key, so nothing on the screen could.
"""
import argparse
import io
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).parent))
import sim

ap = argparse.ArgumentParser()
ap.add_argument('program')
ap.add_argument('output', type=Path)
ap.add_argument('--port', type=int, default=30841)
a = ap.parse_args()
out = a.output.resolve()
picture = Image.new('L', (480, 800), 255)
draw = ImageDraw.Draw(picture)
for i in range(0, 480, 40):
    draw.rectangle([i, 100, i + 19, 700], fill=40)
data = io.BytesIO()
picture.convert('RGB').save(data, 'BMP')
sd = sim.card(out, files={'/anh/a.bmp': data.getvalue()})
log, shots = sim.run(a.program, out, sd, [('00-home', None), ('01-tab-file', 'TAP:150,754'), ('02-folder', 'TAP:240,90'),
                                          ('03-picture', 'TAP:240,90'), ('04-pill', 'TAP:420,754')], a.port, gap=3500)
made = (sd / 'sleep.bmp').exists()
print(f'viewer={"enter BmpViewer" in log} sleep.bmp={made}')
assert 'enter BmpViewer' in log, 'the picture did not open'
assert made, 'RED: the bar offers no way to set the picture as the sleep screen'
print('GREEN')
