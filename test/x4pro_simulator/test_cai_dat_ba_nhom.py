"""X4 Pro Settings (founder 06/10): 3 titled groups (Reading, System, Other), a frame a group, and an About &
updates row at the end that opens a screen of its own with About, the panel chip and both updates. A pinned
update from the card still opens: through that screen.

Runs the X4 Pro simulator (pio run -e simulator_x4pro). X4PRO_PROGRAM picks another build.
"""
import json
import re
import tempfile
from pathlib import Path

from test_thanh_day import run, TABS_X, H
from test_dong_deu import line_kind

OPEN = f'3000:TAP:{TABS_X[4]},754'


def rings(image):
    """The y of each round frame line (top or bottom): 2 px grey lines, the rules between rows are 1 px."""
    lines, y = [], 40
    while y < H - 80:
        if line_kind(image, y) and line_kind(image, y + 1):
            lines.append(y)
            y += 2
        else:
            y += 1
    return lines


def ink(image, box):
    return sum(1 for p in image.crop(box).getdata() if p < 128)


def pin_card_update(sach):
    (sach.parent / '.crosspoint/menu-customization.json').write_text(
        json.dumps(dict(version=1, pins=['action/8'], tabs={})))


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-groups-') as tmp:
        root = Path(tmp)
        swipes = ';'.join(f'{4000 + 2000 * k}:SWIPE:240,600,240,150,200' for k in range(3))
        top, end = run(root / 'a', OPEN + ';' + swipes, [3800, 10500])
        lines = rings(top)
        assert len(lines) >= 4, f'one frame for every group: {lines}'
        assert ink(top, (32, 40, 300, lines[0])) > 0, 'no title over the first group'
        assert ink(top, (32, lines[1] + 3, 300, lines[2])) > 0, 'no title between 2 groups'
        run(root / 'b', OPEN + ';' + swipes + f';10500:TAP:240,{rings(end)[-1] - 30}', [12500])
        log = (root / 'b/simulator.log').read_text()
        assert re.search(r'enter InfoUpdate', log), 'the last row did not open About & updates'
        run(root / 'c', f'3000:TAP:{TABS_X[2]},754;5000:TAP:240,80', [7500], write_books=pin_card_update)
        log = (root / 'c/simulator.log').read_text()
        assert re.search(r'enter InfoUpdate[\s\S]*enter SdFirmwareUpdate', log), 'a pinned update from the card did not open'
    print('GREEN: X4 Pro Settings in 3 titled frames; About & updates opens, a pinned update from the card opens through it')


if __name__ == '__main__':
    main()
