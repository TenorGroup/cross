"""Run a simulator binary on a scripted card and keep its screenshots and log (the UI path checks of this folder).

Not part of ctest: every check needs a simulator binary (pio run -e simulator_x3_uc8279 / simulator_x4pro).
"""
import json
import os
import subprocess
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
TODAY_SECONDS = 1790000000


def card(out, size=0, files=(), extra=None):
    """A card with Vietnamese settings, tips on, and `files` ({path: bytes}) at the root."""
    sd = out / 'sd'
    (sd / '.crosspoint').mkdir(parents=True, exist_ok=True)
    settings = {'language': 'VI', 'uiShell': 0, 'uiTheme': 4, 'uiTextSize': size, 'tipsEnabled': 1, 'sleepTimeout': 10,
                'wakeIntoBook': 0, 'textSpacingVersion': 3, 'paragraphIndentVersion': 1, 'readerInkWeightVersion': 1,
                'tenorPresetVersion': 1, 'uiShellSleepMemo': 0, 'sdFontFamilyName': ''}
    settings.update(extra or {})
    (sd / '.crosspoint/settings.json').write_text(json.dumps(settings))
    (sd / '.crosspoint/state.json').write_text(json.dumps({'showBootScreen': False, 'openEpubPath': ''}))
    for path, data in dict(files).items():
        target = sd / path.lstrip('/')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return sd


def run(program, out, sd, steps, port, gap=2500, settle=1500):
    """steps: [(name, key)] one every `gap` ms from 3 s; a screenshot `settle` ms after each. Returns (log, shots)."""
    out.mkdir(parents=True, exist_ok=True)
    t, acts, shots = 3000, [], {}
    for name, key in steps:
        if key:
            acts.append(f'{t}:{key}')
        shots[t + settle] = name
        t += gap
    env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
    env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_WAKE_REASON='power',
               CROSSPOINT_SIM_NOW=str(TODAY_SECONDS), CROSSPOINT_SIM_HTTP_PORT=str(port), CROSSPOINT_SIM_USB='1',
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(acts + [f'{t + 500}:QUIT']),
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{out}/{name}.bmp' for ms, name in shots.items()))
    p = subprocess.run([str(Path(program).resolve())], cwd=REPO, env=env, capture_output=True, text=True, timeout=240)
    log = p.stdout + p.stderr
    (out / 'run.log').write_text(log)
    assert p.returncode == 0, log[-1500:]
    images = {}
    for name in shots.values():
        bmp = out / f'{name}.bmp'
        image = Image.open(bmp).convert('L')
        image.save(out / f'{name}.png')
        bmp.unlink()
        images[name] = image
    return log, images
