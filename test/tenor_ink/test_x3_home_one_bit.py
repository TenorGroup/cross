"""X3 Home draws its tenor/ink covers 1 bit: the 4-level pass decoded the cover once per strip and plane (14 decodes,
5.7 s on an X3), so it is off until one decode feeds every strip. On the X3 simulator, cold then warm:
- the Home card is a tenor/ink frame and its cache is ink2_1_ only (no ink2_4_);
- 0 gray passes and 0 gray strip taken on Home;
- warm Home decodes no thumbnail again.
The reader keeps its tiled gray pass, its strip 80 rows of 99 B (heapfloor::kGrayStripRows, 7,920 B, checked in
HeapFloorTest). X3_PROGRAM picks the simulator_x3_uc8279 build.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'test/reading_stats_simulator'))
from test_home_card_v1011 import cover_jpeg  # noqa: E402

PROGRAM = Path(os.environ.get('X3_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
BOOKS = ['/books/a.epub', '/books/b.epub']


def cache_hash(tmp, path):
    exe = Path(tmp) / 'hash'
    if not exe.exists():
        src = Path(tmp) / 'hash.cpp'
        src.write_text('#include <functional>\n#include <iostream>\n#include <string>\n'
                       'int main(int c,char**v){std::cout<<std::hash<std::string>{}(v[1]);}\n')
        subprocess.run(['c++', '-std=c++17', str(src), '-o', str(exe)], check=True)
    return subprocess.check_output([str(exe), path], text=True).strip()


def run(sd, stamp, extra=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_NOW='1790000000',
               CROSSPOINT_SIM_INPUT_SCRIPT=f'{stamp}:QUIT', **(extra or {}))
    p = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
    log = p.stdout + p.stderr
    assert p.returncode == 0, log[-1500:]
    return log


def home(tmp, tier):
    sd = Path(tmp) / f'home-{tier}'
    store = sd / '.crosspoint'
    books = []
    for path in BOOKS:
        cache = store / ('epub_' + cache_hash(tmp, path))
        cache.mkdir(parents=True)
        (cache / 'cover.ref').write_bytes(b'CREF\x01\x09cover.jpg')
        book = sd / path.lstrip('/')
        book.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(book, 'w') as z:
            z.writestr('cover.jpg', cover_jpeg())
        books.append(dict(path=path, title=Path(path).stem, author='Tenor',
                          coverBmpPath='/' + str(cache.relative_to(sd)) + '/thumb2_[HEIGHT].bmp'))
    (store / 'recent.json').write_text(json.dumps(dict(books=books)))
    (store / 'state.json').write_text('{"showBootScreen":false,"openEpubPath":""}')
    (store / 'settings.json').write_text(json.dumps(dict(language='VI', homeStyle=0, uiTextSize=tier, uiShell=0, uiTheme=4,
                                                         tenorPresetVersion=1, sleepTimeout=120, uiShellClockOnce=1)))
    cold, warm = run(sd, 7300), run(sd, 1800)
    caches = sorted(p.name for p in sd.rglob('ink2_*.bmp'))
    fails = []
    if 'tenor/ink frame cover=' not in cold:
        fails.append('no tenor/ink frame')
    if not caches or any(not c.startswith('ink2_1_') for c in caches):
        fails.append(f'caches {caches}: want ink2_1_ only')
    for name, log in (('cold', cold), ('warm', warm)):
        passes = re.findall(r'tenor/ink gray covers=\d+ ready=\d', log)
        strips = re.findall(r'internal_scratch=(\d+)', log)
        if passes or strips:
            fails.append(f'{name}: {len(passes)} gray passes, strips {strips}')
    if 'Cover thumbnail ' in warm:
        fails.append('warm Home decoded a thumbnail again')
    return fails, caches


def reader(tmp):
    """The X3 reader page still takes its gray pass in strips (Page render (tiled)), and both strips are the heap floor's
    kGrayStripRows (80 rows of the X3's 99 B, 7,920 B: HeapFloorTest), not a height of their own."""
    sd = Path(tmp) / 'reader'
    (sd / '.crosspoint').mkdir(parents=True)
    (sd / 'sach').mkdir()
    shutil.copyfile(REPO / 'test/epubs/test_kerning_ligature.epub', sd / 'sach/k.epub')
    (sd / '.crosspoint/recent.json').write_text(json.dumps({'books': [{'path': '/sach/k.epub', 'title': 'k'}]}))
    (sd / '.crosspoint/state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': '/sach/k.epub',
                                                           'lastSleepFromReader': True}))
    (sd / '.crosspoint/settings.json').write_text(json.dumps(dict(language='VI', uiShell=0, uiTheme=4, tenorPresetVersion=1,
                                                                  wakeIntoBook=1, sleepTimeout=120, textAntiAliasing=1)))
    log = run(sd, 6000, dict(CROSSPOINT_SIM_WAKE_REASON='power'))
    fails = []
    if 'Page render (tiled)' not in log:
        fails.append('the reader page took no tiled gray pass')
    for name, pattern in (('src/activities/reader/EpubReaderActivity.cpp', r'STRIP_ROWS = heapfloor::kGrayStripRows;'),
                          ('src/activities/home/HomeActivity.cpp', r'stripRows = heapfloor::kGrayStripRows;')):
        if not re.search(pattern, (REPO / name).read_text()):
            fails.append(f'{name}: the strip height is not heapfloor::kGrayStripRows')
    return fails


def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix='x3-home-1bit-', dir=os.environ.get('TMPDIR')) as tmp:
        for tier in (0, 1, 2):
            fails, caches = home(tmp, tier)
            print(('RED' if fails else 'GREEN') + f' X3 Home size {tier}: caches {caches} ' + '; '.join(fails), flush=True)
            failures += [f'size {tier}: {f}' for f in fails]
        fails = reader(tmp)
        print(('RED ' + '; '.join(fails)) if fails else 'GREEN X3 reader: tiled gray pass, strip height heapfloor::kGrayStripRows',
              flush=True)
        failures += fails
    assert not failures, '\n'.join(failures)
    print('GREEN: X3 Home draws tenor/ink 1 bit with no gray pass or strip; the reader keeps its 7,920 B strip')


if __name__ == '__main__':
    main()
