#!/usr/bin/env python3
"""tenor/ugly on the X3: Back from a folder right under "/" keeps the v1.0.61 way.

test_ugly_file_back_x3.py <simulator_x3_uc8279 program> <output dir> [--baseline <v1.0.61 program>]
A pinned folder opened from the notebook's Favorites page goes up to "/" in the file browser. A folder opened from
the notebook's File page goes back to the notebook. With --baseline both end screens must match the old build pixel
for pixel; without it the activity log decides.
"""
import argparse
import sys
from pathlib import Path

from PIL import ImageChops

sys.path.insert(0, str(Path(__file__).absolute().parents[1] / 'ui_paths_sim'))
import sim
import journeys

CASES = {
    'favorite': ([('fav', 'DOWN'), ('fav2', 'DOWN'), ('open', 'CONFIRM'), ('result', 'BACK')], 1),
    'folder': ([('file', 'DOWN'), ('open', 'CONFIRM'), ('result', 'BACK')], 2),
}


def journey(program, out, case, port):
    steps, notebooks = CASES[case]
    sd = sim.card(out, files={'/books/child/x.txt': b'x\n'},
                  extra={'uiShell': 1, 'uglyStartScreen': 2, 'statusBarClock': 0, 'clockShowInHeader': 0})
    journeys.pin(sd, '/books')
    log, shots = sim.run(program, out, sd, [('start', None)] + steps, port, gap=2800, settle=1800)
    return log, shots['result'], notebooks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('program')
    parser.add_argument('output', type=Path)
    parser.add_argument('--baseline')
    parser.add_argument('--port', type=int, default=31780)
    args = parser.parse_args()
    out = args.output.resolve()
    for index, case in enumerate(CASES):
        log, image, notebooks = journey(args.program, out / case, case, args.port + index * 2)
        seen = log.count('Entering activity: UglyNotebook')
        print(f'{case} notebook_entries={seen} expected={notebooks}', flush=True)
        assert seen == notebooks, f'RED: {case}: Back from /books led to the wrong screen'
        if args.baseline:
            _, base, _ = journey(args.baseline, out / (case + '-baseline'), case, args.port + index * 2 + 1)
            changed = sum(1 for v in ImageChops.difference(base, image).getdata() if v)
            print(f'{case} baseline_changed_pixels={changed}', flush=True)
            assert changed == 0, f'RED: {case}: differs from the v1.0.61 screen'
    print('GREEN')


if __name__ == '__main__':
    main()
