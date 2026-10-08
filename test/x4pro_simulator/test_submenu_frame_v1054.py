"""Reader U11: actual BMP pixels, parent frame, nested choice, scroll, Back and selected value."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'test/reading_stats_simulator'))
from cai_dat_truoc_tenor import truoc_tenor

OPEN = '1500:CONFIRM;3500:TAP:420,754;5000:TAP:420,754'
CASES = {
    'status': (OPEN + ';7000:TAP:240,569;9500:SWIPE:240,660,240,440,600;11500:TAP:46,754;14000:QUIT',
               {6200: 'parent', 8500: 'child', 10800: 'scroll', 13000: 'back'}, {}, 2),
    'rotate': (OPEN + ';6500:SWIPE:240,680,240,432,600;8500:TAP:200,493;11000:TAP:46,754;13500:QUIT',
               {7800: 'parent', 10000: 'child', 12500: 'back'}, {}, 1),
    'auto': (OPEN + ';6500:SWIPE:240,680,240,432,600;8500:TAP:200,555;11000:TAP:46,754;13500:QUIT',
             {7800: 'parent', 10000: 'child', 12500: 'back'}, {}, 2),
    'letter': ('1500:CONFIRM;3500:TAP:240,775;7000:TAP:240,617;9500:TAP:46,754;12500:QUIT',
               {6200: 'parent', 8500: 'child', 11300: 'back'}, {}, 3),
    'favorite-letter': ('1500:CONFIRM;3500:TAP:240,775;5000:TAP:134,754;7000:TAP:240,433;'
                        '9500:TAP:240,620;12000:TAP:46,754;14500:QUIT',
                        {6200: 'parent', 8500: 'child', 10800: 'chosen', 13300: 'closed'},
                        {'readerFavorites': ['text/letterSpacing'], 'readerFavoriteCount': 1,
                         'readerFavoritesDaDat': 1}, 0),
}
for key in ('wordSpacing', 'extraParagraphSpacing', 'paragraphAlignment', 'screenMargin',
            'paragraphIndent', 'dropCapMode', 'readerInkWeight'):
    CASES['favorite-' + key] = (
        '1500:CONFIRM;3500:TAP:240,775;5000:TAP:134,754;7000:TAP:240,433;'
        '9500:TAP:46,754;12500:QUIT',
        {6200: 'parent', 8500: 'child', 11300: 'back'},
        {'readerFavorites': ['text/' + key], 'readerFavoriteCount': 1, 'readerFavoritesDaDat': 1}, 0)

def equal(a, b, box):
    return ImageChops.difference(a.crop(box), b.crop(box)).getbbox() is None

def run(name, spec, output, program):
    script, shot_names, extra, row = spec
    folder = output / name
    store = folder / 'sd/.crosspoint'
    store.mkdir(parents=True, exist_ok=True)
    shutil.copy2(REPO / 'test/epubs/test_kerning_ligature.epub', folder / 'sd/book.epub')
    initial = truoc_tenor({'language': 'VI', 'readerMenuStyle': 1, 'uiShell': 0,
                          'readerStatusBarMode': 2, 'fontSize': 18, 'textSpacingVersion': 3,
                          'sleepTimeoutMinutes': 120, 'readerTapTip': 0, **extra})
    (store / 'settings.json').write_text(json.dumps(initial))
    (store / 'state.json').write_text(json.dumps({'openEpubPath': '/book.epub', 'lastSleepFromReader': False,
                                                 'showBootScreen': False, 'readerActivityLoadCount': 0}))
    (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/book.epub', 'title': 'Menu U11'}]}))
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(folder / 'sd'),
               CROSSPOINT_SIM_INPUT_SCRIPT=script,
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{folder}/{label}.bmp' for ms, label in shot_names.items()))
    p = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=40)
    (folder / 'run.log').write_text(p.stdout + p.stderr)
    assert p.returncode == 0, p.stderr[-2000:]
    images = {}
    for label in shot_names.values():
        images[label] = Image.open(folder / f'{label}.bmp').convert('L')
        images[label].save(folder / f'{label}.png')
    parent, child = images['parent'], images['child']
    cy = 402 + row * 62 + 31
    chevron = sum(px < 128 for px in parent.crop((425, cy - 12, 439, cy + 12)).getdata())
    failures = []
    if chevron < 12: failures.append(f'entry chevron has {chevron} ink pixels')
    for box in ((16, 360, 464, 366), (16, 704, 464, 712), (16, 380, 20, 695), (460, 380, 464, 695)):
        if not equal(parent, child, box): failures.append(f'parent/child frame changed at {box}')
    if not equal(parent, child, (0, 35, 480, 358)): failures.append('page above frame changed')
    if not equal(parent, child, (0, 720, 480, 800)): failures.append('foot bar changed')
    if equal(parent, child, (30, 375, 445, 700)): failures.append('child list did not open')
    if 'scroll' in images and equal(child, images['scroll'], (30, 430, 445, 695)):
        failures.append('child swipe did not scroll')
    if 'back' in images and not equal(parent, images['back'], (0, 35, 480, 800)):
        failures.append('Back did not restore parent')
    saved = json.loads((store / 'settings.json').read_text())
    if name == 'favorite-letter' and saved.get('letterSpacing') != 3:
        failures.append(f'chosen wide spacing saved {saved.get("letterSpacing")}')
    result = {'case': name, 'program_sha256': hashlib.sha256(program.read_bytes()).hexdigest(),
              'chevron_ink_pixels': chevron, 'script': script, 'failures': failures,
              'source_rows': {'text': 14, 'more': 17, 'status_values': 8},
              'shots': {label: hashlib.sha256((folder / f'{label}.bmp').read_bytes()).hexdigest()
                        for label in shot_names.values()}}
    (folder / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(('RED' if failures else 'GREEN'), name, result['chevron_ink_pixels'], failures, flush=True)
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', type=Path, default=REPO / '.pio/build/simulator_x4pro/program')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--case', choices=CASES)
    args = parser.parse_args()
    results = [run(name, spec, args.output, args.program) for name, spec in CASES.items()
               if args.case is None or name == args.case]
    (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    assert not any(r['failures'] for r in results), results

if __name__ == '__main__':
    main()
