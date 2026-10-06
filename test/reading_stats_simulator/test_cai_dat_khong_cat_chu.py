"""Settings rows show their words whole: no label, value or note of a settings row ends in an ellipsis.

Founder 06/10/2026: a long label beside a long value was squeezed to 2 lines and cut ("Nhan nhanh nu..."), the value
kept 1 long line. The row now gives the label the width first and lets the value take 2 lines. The simulator logs
every line it cuts ([GFX] Ellipsis, simulator builds only); this walks every Settings group, every row, at the 3 text
sizes in Vietnamese and English on the button board, and asks for none while Settings is on screen.
"""
import json
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROGRAM = Path(os.environ.get('TEST_PROGRAM', REPO / '.pio/build/simulator_x3_uc8279/program'))
CUT = re.compile(r'\[GFX\] Ellipsis width=\d+ max=\d+: (.*)$', re.M)
GROUPS = 9  # Display, Sleep, Reader, Controls, Motion, System, Device, Keyboard, Other


def walk(language, size):
    with tempfile.TemporaryDirectory(prefix='cai-dat-cat-chu-') as tmp:
        sd = Path(tmp)
        (sd / '.crosspoint').mkdir()
        (sd / '.crosspoint/settings.json').write_text(json.dumps(dict(language=language, uiTextSize=size, uiShell=0)))
        (sd / '.crosspoint/state.json').write_text(json.dumps(dict(openEpubPath='', showBootScreen=False)))
        # Home's Settings, open the first group, walk its rows (front Right), then the edge Down to the next group.
        keys = ['UP', 'CONFIRM']
        for _ in range(GROUPS):
            keys += ['RIGHT'] * 22 + ['DOWN']
        script = ';'.join(f'{1500 + 260 * i}:{k}' for i, k in enumerate(keys)) + f';{1500 + 260 * len(keys) + 900}:QUIT'
        env = {k: v for k, v in os.environ.items() if not k.startswith('CROSSPOINT_SIM_')}
        env.update(SDL_VIDEODRIVER='dummy', CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script)
        run = subprocess.run([str(PROGRAM)], cwd=REPO, env=env, capture_output=True, text=True, timeout=180)
        log = run.stdout + run.stderr
        assert run.returncode == 0, log[-2000:]
        opened = log.find('Entering activity: Settings')
        assert opened >= 0, log[-2000:]
        return sorted(set(CUT.findall(log[opened:]))), log


class CaiDatKhongCatChuTest(unittest.TestCase):
    def test_no_settings_row_is_cut(self):
        cuts = {}
        for language in ('VI', 'EN'):
            for size in (0, 1, 2):
                found, _ = walk(language, size)
                if found:
                    cuts[f'{language} size {size}'] = found
        self.assertEqual(cuts, {}, 'settings rows cut with an ellipsis')


if __name__ == '__main__':
    unittest.main()
