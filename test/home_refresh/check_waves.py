"""Check DRF commands from the real X4 Pro driver for each measured Home change."""
import argparse
import json
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('folder', type=Path)
a = ap.parse_args()
summary = json.loads((a.folder / 'summary.json').read_text())
waves = [json.loads(line) for line in (a.folder / 'waves.jsonl').read_text().splitlines()]
windows = {row[0]: row[1:] for line in (a.folder / 'windows.txt').read_text().splitlines()
           if (row := list(map(int, line.split())))}
bad = []
for n, edge in enumerate(summary['edges'], 1):
    frames = [f for f in summary['frames'] if edge <= f['ms'] < edge + 1200]
    selected = [w for f in frames for w in waves if w['record'] in f['records']]
    print(f'swap={n} frames={len(frames)} DRF={len(selected)} waves={selected}', flush=True)
    if (len(frames) != 1 or len(selected) != 1 or selected[0]['mode'] != 'fast' or
            selected[0]['rect'] == [0, 0, 800, 480] or
            selected[0]['rect'] != windows.get(selected[0]['record'])):
        bad.append(n)
assert not bad, f'RED: swaps {bad} need exactly 1 fast window DRF'
print('GREEN: every book change has 1 fast window DRF')
