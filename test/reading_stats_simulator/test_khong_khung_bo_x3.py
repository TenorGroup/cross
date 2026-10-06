"""Button readers: Home's Settings and Stats stand without round frames (founder 06/10/2026, on an X3 running
v1.0.52: the round frame belongs to the touch reader; the button readers keep their lists unframed).

v1.0.52 framed the 2 Settings groups with a 2 px black ring and the 2 Stats panels with a 1 px one. Measured on the
pixels: no straight ink edge of a frame (a vertical run of 60 px or more) near either side of the screen, between
the tab bar and the key hints. The selected row's pill has round ends, so it leaves no such edge.
"""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
EDGE_RUN = 60
SCREENS = [('Settings', '3000:UP', 4300, 0), ('Settings', '3000:UP', 4300, 1), ('Settings', '3000:UP', 4300, 2),
           ('Settings, a row picked', '3000:UP;4000:RIGHT;4800:RIGHT', 6000, 0),
           ('Stats', '3000:UP;4000:UP', 6000, 0)]


def shoot(script, at, size):
    with tempfile.TemporaryDirectory(prefix='x3-khung-') as tmp:
        sd = Path(tmp)
        (sd / '.crosspoint').mkdir()
        (sd / '.crosspoint/settings.json').write_text(json.dumps(dict(language='VI', uiTextSize=size, uiShell=0)))
        (sd / '.crosspoint/state.json').write_text(json.dumps(dict(openEpubPath='', showBootScreen=False)))
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=f'{script};{at + 600}:QUIT',
                   CROSSPOINT_SIM_SCREENSHOTS=f'{at}:{sd}/shot.bmp')
        result = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, result.stderr[-1500:]
        return Image.open(sd / 'shot.bmp').convert('L')


def frame_edges(image):
    width = image.width
    edges = []
    for x in list(range(8, 33)) + list(range(width - 32, width - 13)):
        run = best = 0
        for y in range(90, image.height - 50):
            run = run + 1 if image.getpixel((x, y)) < 128 else 0
            best = max(best, run)
        if best >= EDGE_RUN:
            edges.append((x, best))
    return edges


class ButtonReadersHaveNoRoundFrames(unittest.TestCase):
    def test_settings_and_stats(self):
        failures = []
        for name, script, at, size in SCREENS:
            edges = frame_edges(shoot(script, at, size))
            if edges:
                failures.append(f'{name}, text size {size}: frame edges at {edges[:4]}')
        self.assertFalse(failures, '\n'.join(failures))


if __name__ == '__main__':
    unittest.main()
