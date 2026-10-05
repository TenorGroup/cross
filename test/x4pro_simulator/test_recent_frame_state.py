"""X4 Pro: Recent's card keeps frame state local to each paint pass."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('X4PRO_PROGRAM', REPO / '.pio/build/simulator_x4pro/program'))


def check_recent_frame_state():
    artifacts = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
    with tempfile.TemporaryDirectory(prefix='x4-recent-frame-') as temporary:
        folder = Path(artifacts) if artifacts else Path(temporary)
        folder.mkdir(parents=True, exist_ok=True)
        sd = folder / 'sd'
        store = sd / '.crosspoint'
        store.mkdir(parents=True)
        books = []
        for index in range(5):
            name = f'book{index}.txt'
            (sd / name).write_text('Recent frame fixture.\n' * 100)
            books.append(dict(path='/' + name, title=f'Book {index}'))
        (store / 'recent.json').write_text(json.dumps(dict(books=books)))
        (store / 'settings.json').write_text(json.dumps(dict(language='VI')))
        (store / 'state.json').write_text(json.dumps(dict(openEpubPath='', showBootScreen=False)))
        env = {key: value for key, value in os.environ.items() if not key.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT='2500:TAP:423,754;4500:TAP:56,754;6500:TAP:148,754;8500:TAP:56,754;10500:QUIT',
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{folder}/{ms}.bmp' for ms in (2200, 4200, 6200, 8200, 10200)))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=30)
        log = run.stdout + run.stderr
        (folder / 'simulator.log').write_text(log)
        errors = re.findall(r'\[(\d+)\].*Outside range \((\d+),\s*(\d+)\)', log)
        edge_ink = {}
        for ms in (2200, 4200, 6200, 8200, 10200):
            if (folder / f'{ms}.bmp').exists():
                with Image.open(folder / f'{ms}.bmp') as image:
                    edge_ink[str(ms)] = sum(pixel < 128 for pixel in image.convert('L').crop((16, 100, 18, 650)).getdata())
        summary = dict(program=str(PROGRAM), program_sha256=hashlib.sha256(PROGRAM.read_bytes()).hexdigest(),
                       returncode=run.returncode, recent_entries=5, errors=len(errors),
                       first_error=errors[0] if errors else None, last_error=errors[-1] if errors else None,
                       home_frames=re.findall(r'\[\d+\].*\[HOME\].*Frame.*', log), edge_ink=edge_ink,
                       shots=[ms for ms in (2200, 4200, 6200, 8200, 10200) if (folder / f'{ms}.bmp').exists()])
        (folder / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
        assert run.returncode == 0, log[-1500:]
        assert 'Recent books loaded from file (5 entries)' in log
        assert len(summary['shots']) == 5, 'all transition screenshots must be captured'
        assert len(summary['home_frames']) == 5, 'boot, Settings, Recent, Folder, Recent must all render'
        assert len(errors) == 0, json.dumps(summary, indent=2)
        assert edge_ink['4200'] > 100, 'Settings keeps its visible list frame'
        assert edge_ink['8200'] > 100, 'Folder keeps its visible list frame'
        assert edge_ink['6200'] == edge_ink['2200'], 'Recent after Settings matches the clean card edge'
        assert edge_ink['10200'] == edge_ink['2200'], 'Recent after Folder matches the clean card edge'


if __name__ == '__main__':
    check_recent_frame_state()
    print('GREEN: X4 Pro Recent frame state stays local to each paint pass')
