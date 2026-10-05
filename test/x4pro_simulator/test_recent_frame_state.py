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


def prepare_recent_sd(folder):
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
    return sd


def check_recent_frame_state():
    artifacts = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
    with tempfile.TemporaryDirectory(prefix='x4-recent-frame-') as temporary:
        folder = Path(artifacts) if artifacts else Path(temporary)
        folder.mkdir(parents=True, exist_ok=True)
        sd = prepare_recent_sd(folder)
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


def check_rapid_recent_tabs():
    """24 real tab taps and 2 Stats swipes must settle on the Recent card."""
    artifacts = os.environ.get('CROSSPOINT_TEST_ARTIFACTS')
    with tempfile.TemporaryDirectory(prefix='x4-rapid-tabs-') as temporary:
        folder = Path(artifacts) / 'rapid-tabs' if artifacts else Path(temporary)
        folder.mkdir(parents=True, exist_ok=True)
        sd = prepare_recent_sd(folder)
        events, swipes, shots = [], [], [2200]
        at = 2500
        for cycle in range(8):
            events.extend([(at, 'TAP:423,754'), (at + 350, 'TAP:332,754')])
            if cycle in (2, 5):
                before, after = at + 450, at + 950
                shots.extend([before, after])
                swipes.append((before, after))
                events.append((at + 500, 'SWIPE:240,670,240,420,150'))
                at += 500
            events.append((at + 700, 'TAP:56,754'))
            at += 1050
        last_action = events[-1][0]
        quiet = [last_action + 900, last_action + 1900]
        shots.extend(quiet)
        script = ';'.join(f'{ms}:{action}' for ms, action in events)
        script += f';{quiet[-1] + 300}:QUIT'
        (folder / 'input-script.txt').write_text(script + '\n')
        env = {key: value for key, value in os.environ.items() if not key.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
                   CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{folder}/{ms}.bmp' for ms in shots))
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=30)
        log = run.stdout + run.stderr
        (folder / 'simulator.log').write_text(log)
        frames = [dict(ms=int(ms), row=int(row), top=int(top), render_ms=int(elapsed)) for ms, row, top, elapsed in
                  re.findall(r'\[(\d+)\].*\[HOME\].*Frame row=(\d+) top=(\d+) total=(\d+)ms', log)]
        images = {}
        for ms in shots:
            with Image.open(folder / f'{ms}.bmp') as im:
                images[ms] = im.convert('L').copy()
        # The body and selected-tab strip identify Book 0/Recent. Omit clock, radio and battery pixels.
        boxes = ((16, 100, 464, 715), (40, 724, 96, 784))
        def same_card(ms):
            return all(images[2200].crop(box).tobytes() == images[ms].crop(box).tobytes() for box in boxes)
        swipe_frames = [[frame for frame in frames if before < frame['ms'] < after] for before, after in swipes]
        errors = re.findall(r'Outside range \(', log)
        faults = re.findall(r'(?i)Guru Meditation|panic_abort|watchdog.*triggered|out of memory|bad_alloc|OOM', log)
        final = [frame for frame in frames if last_action <= frame['ms'] < quiet[0]]
        late = [frame for frame in frames if frame['ms'] >= quiet[0]]
        summary = dict(program=str(PROGRAM), program_sha256=hashlib.sha256(PROGRAM.read_bytes()).hexdigest(),
                       returncode=run.returncode, tab_taps=24, stats_swipes=2, frames=frames,
                       swipe_frames=swipe_frames, final_action_ms=last_action, final_frames=final, late_frames=late,
                       final_action_to_frame_ms=final[-1]['ms'] - last_action if final else None,
                       out_of_range=len(errors), faults=faults, final_card_matches=[same_card(ms) for ms in quiet],
                       pending_input_check='last Recent action produces row 1 frame; no late Home paints across 1 s queue window',
                       timing_scope='simulator diagnostic only')
        (folder / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
        assert run.returncode == 0, log[-1500:]
        assert len(errors) == 0 and not faults, json.dumps(summary, indent=2)
        assert final and final[-1]['row'] == 1, 'last Recent tap must be consumed and paint its card'
        assert not late, 'queued input must stop producing frames before the quiet window'
        assert all(summary['final_card_matches']), 'settled screen must be the original Book0 Recent card'
        assert all(any(frame['top'] > 0 for frame in group) for group in swipe_frames), 'Stats swipe must scroll to its lower Quotes rows'


if __name__ == '__main__':
    check_recent_frame_state()
    print('GREEN: X4 Pro Recent frame state stays local to each paint pass')
    check_rapid_recent_tabs()
    print('GREEN: X4 Pro 24 tab transitions and 2 Stats swipes settle on Recent')
