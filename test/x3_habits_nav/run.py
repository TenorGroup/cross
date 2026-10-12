#!/usr/bin/env python3
"""X3 Reading habits: Left/Right must still move between habits when a habit's explanation runs to 2 pages.

run.py <simulator_x3_uc8279 program> <output dir> [--size N] [--shots]
The card carries 16 days of reading, so each explanation has its evidence line and some run to 2 pages.
Home -> Stats -> Reading habits, then Right 12 times: each Right turns an explanation page or moves the cursor, so
the cursor must reach all 5 habits after the first (the habit rows change 5 times).
RED on v1.0.58: a 2-page explanation turned round and round under Right and the cursor stayed on that habit.
Not part of ctest: it needs a simulator binary (pio run -e simulator_x3_uc8279).
"""
import argparse
import json
import os
import subprocess
from pathlib import Path

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser()
ap.add_argument('program', type=Path)
ap.add_argument('output', type=Path)
ap.add_argument('--size', type=int, default=2)
ap.add_argument('--port', type=int, default=30790)
a = ap.parse_args()
out = a.output.resolve()
sd = out / 'sd'
(sd / '.crosspoint').mkdir(parents=True, exist_ok=True)
(sd / '.crosspoint/settings.json').write_text(json.dumps({
    'language': 'VI', 'uiShell': 0, 'uiTheme': 4, 'uiTextSize': a.size, 'tipsEnabled': 1, 'sleepTimeout': 10,
    'wakeIntoBook': 0, 'textSpacingVersion': 3, 'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1,
    'tenorPresetVersion': 1, 'uiShellSleepMemo': 0, 'sdFontFamilyName': ''}))
(sd / '.crosspoint/state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': ''}))
TODAY = 9761  # CROSSPOINT_SIM_NOW below, as a day ordinal (2000-01-01 is day 1)
days = [[d, 1800000, 600000, 300000, 3, 1, 1, 0, True] for d in range(TODAY - 16, TODAY)]
(sd / '.crosspoint/reading-stats.json').write_text(json.dumps({'schema': 3, 'bookEpoch': 0, 'habits': {
    'clockLost': False, 'qualityGap': 0, 'first': TODAY - 20, 'evaluated': TODAY - 1, 'awarded': 0, 'hidden': 0,
    'gates': [0] * 12, 'days': days}, 'ngay': []}))

# Home starts on Recent; the edge buttons step the tabs to Stats, Confirm opens its first row (Reading habits).
RIGHTS = 12
keys = ['DOWN', 'DOWN', 'DOWN', 'CONFIRM'] + ['RIGHT'] * RIGHTS
t, acts, shots = 3000, [], {}
for i, key in enumerate(keys):
    acts.append(f'{t}:{key}')
    shots[t + 1500] = f'{i:02d}-{key.lower()}'
    t += 2500
env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
           CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_HTTP_PORT=str(a.port), CROSSPOINT_SIM_USB='1',
           CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(acts + [f'{t + 1000}:QUIT']),
           CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{out}/{name}.bmp' for ms, name in shots.items()))
p = subprocess.run([str(a.program.resolve())], cwd=REPO, env=env, capture_output=True, text=True, timeout=180)
(out / 'run.log').write_text(p.stdout + p.stderr)
assert p.returncode == 0, (p.stdout + p.stderr)[-1500:]
frames = []
for name in shots.values():
    im = Image.open(out / f'{name}.bmp').convert('L')
    im.save(out / f'{name}.png')
    (out / f'{name}.bmp').unlink()
    frames.append(im)
habits = frames[len(keys) - RIGHTS - 1:]  # Reading habits as it opens, then after each Right
rows_box = (0, 0, habits[0].width, habits[0].height // 2)  # the habit rows, over the explanation
moves = [i + 1 for i in range(RIGHTS)
         if ImageChops.difference(habits[i].crop(rows_box), habits[i + 1].crop(rows_box)).getbbox()]
print(f'size={a.size} Rights that moved the cursor: {moves}')
assert len(moves) >= 5, f'RED: the cursor reached {len(moves)} of the 5 habits after the first'
print('GREEN')
