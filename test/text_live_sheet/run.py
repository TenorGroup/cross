"""Simulator journey: the toolbar's Text sheet changes the page behind it, with no Text settings screen.

X3 (toolbar menu, 5 tabs): menu, 3 steps right to the Text tab, row 2 (reader size), pick 12 pt.
X4 Pro: tap the foot bar, tap the Text tool, tap the size minus. Needs the simulator_x3_uc8279 and
simulator_x4pro programs (X3_PROGRAM, X4PRO_PROGRAM override the default build paths).
"""
import json, os, shutil, subprocess, tempfile, zipfile
from pathlib import Path
from PIL import Image, ImageChops

REPO = Path(__file__).resolve().parents[2]
CASES = {
    'x3': (os.environ.get('X3_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'),
           '2000:CONFIRM;3000:RIGHT;3400:RIGHT;3800:RIGHT;5000:CONFIRM;6000:RIGHT;7000:CONFIRM;8500:RIGHT;9500:CONFIRM;11000:QUIT',
           {5800: 'before', 10500: 'after'}),
    'x4pro': (os.environ.get('X4PRO_PROGRAM', REPO / '.pio/build/simulator_x4pro/program'),
              '1500:RIGHT;2500:TAP:240,775;3500:TAP:321,754;5000:TAP:290,495;6500:QUIT',
              {4200: 'before', 5800: 'after'}),
}


def fixture(sd):
    store = sd / '.crosspoint'
    store.mkdir(parents=True)
    with zipfile.ZipFile(sd / 'journey.epub', 'w') as z:
        z.writestr('mimetype', 'application/epub+zip')
        z.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Live text</dc:title><dc:identifier id="id">live-text</dc:identifier><dc:language>en</dc:language></metadata><manifest><item id="c" href="c.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="c"/></spine></package>')
        body = ''.join(f'<p>Paragraph {n:03d}. The page behind the sheet must follow every change to the size.</p>' for n in range(300))
        z.writestr('c.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>T</title></head><body><h1>Chapter</h1>' + body + '</body></html>')
    (store / 'state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': '/journey.epub', 'lastSleepFromReader': True}))
    (store / 'recent.json').write_text(json.dumps({'books': [{'path': '/journey.epub', 'title': 'Live text'}]}))
    (store / 'settings.json').write_text(json.dumps({
        'language': 'VI', 'uiShell': 0, 'uiTheme': 4, 'uiTextSize': 0, 'orientation': 0, 'readerMenuStyle': 1, 'wakeIntoBook': 1,
        'fontSize': 18, 'fontFamily': 0, 'textSpacingVersion': 3, 'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1,
        'tenorPresetVersion': 1, 'uiShellSleepMemo': 0, 'sleepTimeout': 10, 'showReaderMenu': 1}))


def run(name, program, script, shots):
    with tempfile.TemporaryDirectory(prefix=f'live-text-{name}-') as tmp:
        tmp = Path(tmp)
        fixture(tmp / 'sd')
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(tmp / 'sd'), CROSSPOINT_SIM_WAKE_REASON='power',
                   CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{tmp}/{n}.bmp' for ms, n in shots.items()))
        p = subprocess.run([str(program)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        log = p.stdout + p.stderr
        assert p.returncode == 0, log[-2000:]
        assert 'Entering activity: TextSettings' not in log, f'{name}: the Text tool opened the Text settings screen'
        before, after = (Image.open(tmp / f'{n}.bmp').convert('L') for n in ('before', 'after'))
        top = (0, 0, before.width, 200)  # page text above the sheet
        assert ImageChops.difference(before.crop(top), after.crop(top)).getbbox(), f'{name}: page behind the sheet did not change'
        print(f'GREEN {name}: Text sheet changed the page behind it, no Text settings screen')


if __name__ == '__main__':
    for case, (program, script, shots) in CASES.items():
        run(case, program, script, shots)
