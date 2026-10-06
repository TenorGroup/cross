"""X4 Pro reader tap-zone tip (founder 05/10): shown over the first page until "Don't show this tip
again"; a tap on the page closes it without turning and it comes back on the next open; the button
hides it for good."""
from pathlib import Path
import json
import struct
import tempfile

from test_thanh_day import run

OPEN = '3000:TAP:240,300'
HOME = 'SWIPE:.5,.99,.5,.75,250'
# The button between the centre cell and the foot band, in the forward zone (readertap::tipButton).
BUTTON = (300, 635)
PAGE = (0, 110, 480, 730)


def same(a, b, box=PAGE):
    return list(a.crop(box).getdata()) == list(b.crop(box).getdata())


def full_refreshes(trace):
    """Panel pushes that run a full waveform (DISPLAY with mode FULL 0 or HALF 1, a gray base with a
    FULL or HALF fallback): one 1,3 s refresh each on the X4 Pro."""
    data = Path(trace).read_bytes() if Path(trace).exists() else b''
    count, i = 0, 0
    while i + 12 <= len(data):
        op, a, b, c, d, n = struct.unpack_from('<BBBHHI', data, i + 1)
        if (op == 2 and a in (0, 1)) or (op == 3 and a in (0, 1)) or (op == 4 and b in (0, 1)):
            count += 1
        i += 12 + n
    return count


def tip_on(folder):
    return json.loads((folder / 'sd/.crosspoint/settings.json').read_text()).get('readerTapTip')


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-tip-') as tmp:
        root = Path(tmp)
        # Tip, tap the page, back Home, open again: tip again, page never turned.
        f = root / 'close'
        tip, closed, again = run(f, OPEN + ';7000:TAP:300,200;9500:' + HOME + ';12000:TAP:240,300',
                                 [6600, 9000, 15500], settings=dict(readerTapTip=1))
        assert 'enter EpubReader ' in (f / 'simulator.log').read_text(), 'fixture never entered Reader'
        assert not same(tip, closed), 'no tip over the first page'
        assert same(tip, again), 'the tip did not come back on the next open'
        assert tip_on(f) == 1, 'closing the tip hid it'
        # The closing tap turned no page: the same page comes back without the tip under a turn's tap.
        g = root / 'reference'
        (plain,) = run(g, OPEN, [6600], settings=dict(readerTapTip=0))
        assert same(plain, closed), 'the tap that closed the tip turned the page'
        # The button: the page without the tip, and none on the next open.
        h = root / 'button'
        _, after, reopened = run(h, OPEN + f';7000:TAP:{BUTTON[0]},{BUTTON[1]};9500:' + HOME + ';12000:TAP:240,300',
                                 [6600, 9000, 15500], settings=dict(readerTapTip=1))
        assert same(plain, after), 'the button left the tip or turned the page'
        assert same(plain, reopened), 'the tip came back after the button'
        assert tip_on(h) == 0, 'the button did not keep the tip hidden'
        # Closing the tip costs exactly 1 full refresh more than turning the page without it: the founder
        # saw the map's lines after a page turn's refresh (06/10), and a second one would be waste.
        counts = []
        for name, tip in (('trace-tip', 1), ('trace-plain', 0)):
            folder = root / name
            trace = root / (name + '.bin')
            run(folder, OPEN + ';7000:TAP:300,200', [9000], settings=dict(readerTapTip=tip),
                sim_env=dict(CROSSPOINT_SIM_PANEL_TRACE=str(trace)))
            counts.append(full_refreshes(trace))
        assert counts[0] == counts[1] + 1, f'closing the tip ran {counts[0] - counts[1]} extra full refreshes, not 1'
    print('GREEN: X4 Pro tap tip: shown, a page tap closes it without a turn, back next open, the button hides it')


if __name__ == '__main__':
    main()
