"""X4 Pro: the tab bar sits at the foot of the screen and a tap there changes the tab.

Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('X4PRO_PROGRAM', REPO / '.pio/build/simulator_x4pro/program'))
W, H = 480, 800
BAR_H, GAP = 60, 16          # tenorchrome::TAB_HEIGHT at the default text size, TOUCH_BAR_BOTTOM_GAP
BAR_TOP = H - GAP - BAR_H
TABS_X = [56, 148, 240, 331, 423]


def run(folder, script, shots, settings=None, extra_books=0, sim_env=None, write_books=None):
    sd = folder / 'sd'
    (sd / '.crosspoint').mkdir(parents=True)
    (sd / 'sach').mkdir()
    books = []
    for name in ('test_kerning_ligature.epub', 'test_dictionary_synonyms.epub'):
        shutil.copyfile(REPO / 'test/epubs' / name, sd / 'sach' / name)
        books.append(dict(path='/sach/' + name, title=name[:-5]))
    # extra_books: a count, or the list of names (without .epub) of the books to add to sach/.
    extra = [f'extra_{i:02d}' for i in range(extra_books)] if isinstance(extra_books, int) else extra_books
    for name in extra:
        shutil.copyfile(REPO / 'test/epubs/test_kerning_ligature.epub', sd / 'sach' / f'{name}.epub')
    # write_books(sach): makes books of its own in sach/ before the run.
    if write_books:
        write_books(sd / 'sach')
    (sd / '.crosspoint/state.json').write_text(json.dumps(dict(openEpubPath='', showBootScreen=False)))
    (sd / '.crosspoint/settings.json').write_text(json.dumps(dict(language='VI', sdFontFamilyName='', **(settings or {}))))
    (sd / '.crosspoint/recent.json').write_text(json.dumps({'books': books}))
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(sim_env or {})
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=(script + ';' if script else '') + f'{max(shots) + 600}:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{folder}/{ms}.bmp' for ms in shots))
    result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=60)
    (folder / "simulator.log").write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result.stderr[-1500:]
    return [Image.open(folder / f'{ms}.bmp').convert('L') for ms in shots]


def ink(image, box):
    pixels = image.crop(box).getdata()
    return sum(1 for p in pixels if p < 128) / max(1, len(pixels))


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-bar-') as tmp:
        folder = Path(tmp)
        home, after_foot = run(folder, f'3000:TAP:{TABS_X[4]},{BAR_TOP + BAR_H // 2}', [2800, 4800])
        # The bar's grey ring: its top edge is a 50% dotted row across the bar.
        edge = ink(home, (60, BAR_TOP, 420, BAR_TOP + 2))
        assert 0.35 < edge < 0.65, f'no dotted bar edge at y={BAR_TOP}: ink {edge:.2f}'
        # Nothing of the bar is left at the top, where the button readers keep it.
        assert ink(home, (60, 60, 280, 62)) < 0.05, 'a bar is still drawn at the top'
        # A tap on the bar's last tab opens it: the title changes and the selected pill moves there.
        assert list(home.crop((0, 0, 300, 56)).getdata()) != list(after_foot.crop((0, 0, 300, 56)).getdata()), \
            'a tap on the foot bar did not change the tab'
        box = (TABS_X[4] - 44, BAR_TOP, TABS_X[4] + 44, BAR_TOP + BAR_H)
        assert ink(after_foot, box) > ink(home, box), 'the selected tab did not move to the tap'
    print('GREEN: X4 Pro tab bar at the foot, tapped there')


if __name__ == '__main__':
    main()
