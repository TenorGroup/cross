"""Measure Home changes and keep every screenshot and panel trace for bus replay."""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from test_home_refresh import BOOKS, cover, make_book, run

REPO = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser()
ap.add_argument('device', choices=['x3', 'x4'])
ap.add_argument('program', type=Path)
ap.add_argument('output', type=Path)
ap.add_argument('--style', type=int, default=0)
ap.add_argument('--seed', action='store_true')
ap.add_argument('--check', action='store_true')
ap.add_argument('--analyze-only', action='store_true')
ap.add_argument('--count', type=int, choices=[10, 20], default=10)
a = ap.parse_args()
root = a.output.resolve()
root.mkdir(parents=True, exist_ok=True)
sd = root / 'sd'
program = a.program.resolve()
port = 30700 + a.style + (10 if a.device == 'x4' else 0)
key = 'SWIPE:420,400,60,400,150' if a.device == 'x4' else 'RIGHT'
if a.seed:
    (sd / '.crosspoint').mkdir(parents=True, exist_ok=True)
    for name, title, seed, progressive in BOOKS:
        make_book(sd / name, title, None if seed is None else cover(abs(seed)), progressive)
    (sd / '.crosspoint/recent.json').write_text(json.dumps({'books': [
        {'path': '/' + name, 'title': title} for name, title, *_ in BOOKS]}))
    for i, (name, *_rest) in enumerate(reversed(BOOKS)):
        run(program, sd, root / f'read{i}', name, a.style, ['2500:BACK'], 5000, port)
    run(program, sd, root / 'warm', None, a.style,
        [f'{5000 + i * 5000}:{key}' for i in range(10)], 57000, port)
else:
    assert sd.exists(), 'seed the card or copy the baseline card first'

# Cover caches are now warm; the action interval is shorter than the idle restoration delay.
edges = [3000 + i * 1400 for i in range(a.count)]
folder = root / 'measure'
folder.mkdir(exist_ok=True)
trace = folder / 'trace.bin'
if not a.analyze_only:
    trace.unlink(missing_ok=True)
shots = {2400: 'before', edges[0] + 1000: 'after-1', edges[4] + 1000: 'after-5',
         edges[9] + 1000: 'after-10', edges[-1] + 4500: 'idle'}
if a.count == 20:
    shots[edges[-1] + 1000] = 'after-20'
quit_ms = edges[-1] + 5000
env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
           CROSSPOINT_SIM_NOW='1790000000', CROSSPOINT_SIM_MARK_SEED='12345', CROSSPOINT_SIM_HTTP_PORT=str(port),
           CROSSPOINT_SIM_USB='1', CROSSPOINT_SIM_PANEL_TRACE=str(trace),
           CROSSPOINT_SIM_INPUT_SCRIPT=';'.join([f'{ms}:{key}' for ms in edges] + [f'{quit_ms}:QUIT']),
           CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{folder}/{name}.bmp' for ms, name in shots.items()))
if a.analyze_only:
    log = (folder / 'run.log').read_text()
else:
    p = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=180)
    log = p.stdout + p.stderr
    (folder / 'run.log').write_text(log)
    assert p.returncode == 0, log[-2000:]
for name in shots.values():
    if not (folder / f'{name}.bmp').exists():
        continue
    image = Image.open(folder / f'{name}.bmp').convert('L')
    image.save(folder / f'{name}.png')
    (folder / f'{name}.bmp').unlink()

records, at = [], 0
data = trace.read_bytes()
while at < len(data):
    magic, op, a0, b0, c, d, length = struct.unpack_from('<4B2HI', data, at)
    assert magic == ord('T') and at + 12 + length <= len(data)
    records.append(dict(index=len(records), op=op, a=a0, b=b0, c=c, d=d))
    at += 12 + length
frame_ends = [r['index'] for r in records if r['op'] in (2, 8)]
frames, updates = [], []
lo, event = 0, 0
for line in log.splitlines():
    if '[GFX] Time =' in line and 'displayBuffer' in line:
        updates.append(None)
        event += 1
    elif '[GFX] Window logical=' in line:
        m = re.search(r'panel=(\d+),(\d+) (\d+)x(\d+)', line)
        updates.append(tuple(map(int, m.groups())))
        event += 1
    elif '[GFX] displayGrayBuffer' in line:
        event += 1
    m = re.match(r'^\[(\d+)\] \[\w+\] \[HOME\] Frame row=(\d+).*?total=(\d+)ms', line)
    if m:
        end = frame_ends[event - 1]
        frames.append(dict(ms=int(m[1]), row=int(m[2]), duration=int(m[3]),
                           records=[r['index'] for r in records[lo:end + 1]], gray=records[end]['op'] == 8))
        lo = end + 1
assert event == len(frame_ends), (event, len(frame_ends))
displays = [r['index'] for r in records if r['op'] == 2]
assert len(displays) == len(updates), (len(displays), len(updates))
mapping = [f'{i} ' + ' '.join(map(str, box)) for i, box in zip(displays, updates) if box]
(folder / 'windows.txt').write_text('\n'.join(mapping) + '\n')
summary = dict(device=a.device, style=a.style, frames=frames, edges=edges,
               samples={str(n): [f for f in frames if edges[0] <= f['ms'] < edges[n - 1] + 1200]
                        for n in ((1, 5, 10, 20) if a.count == 20 else (1, 5, 10))})
(folder / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
bad = []
for n, start in enumerate(edges):
    selected = [f for f in frames if start <= f['ms'] < start + 1200]
    print(f'{a.device} style={a.style} swap={n + 1} frames={selected}', flush=True)
    if len(selected) != 1 or selected[0]['gray']:
        bad.append(n + 1)
if a.check:
    assert not bad, f'RED: swaps {bad} need exactly 1 B/W frame'
