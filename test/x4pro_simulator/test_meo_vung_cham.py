"""X4 Pro reader tap-zone tip (founder 05/10): shown over the first page until "Don't show this tip
again"; a tap on the page closes it without turning and it comes back on the next open; the button
hides it for good."""
from pathlib import Path
import json
import tempfile

from test_thanh_day import run

OPEN = '3000:TAP:240,300'
HOME = 'SWIPE:.5,.99,.5,.75,250'
# The button between the centre cell and the foot band, in the forward zone (readertap::tipButton).
BUTTON = (300, 635)
PAGE = (0, 110, 480, 730)


def same(a, b, box=PAGE):
    return list(a.crop(box).getdata()) == list(b.crop(box).getdata())


def hidden(folder):
    return json.loads((folder / 'sd/.crosspoint/settings.json').read_text()).get('readerTapTipHidden')


def main():
    with tempfile.TemporaryDirectory(prefix='x4pro-tip-') as tmp:
        root = Path(tmp)
        # Tip, tap the page, back Home, open again: tip again, page never turned.
        f = root / 'close'
        tip, closed, again = run(f, OPEN + ';7000:TAP:300,200;9500:' + HOME + ';12000:TAP:240,300',
                                 [6600, 9000, 15500], settings=dict(readerTapTipHidden=0))
        assert 'enter EpubReader ' in (f / 'simulator.log').read_text(), 'fixture never entered Reader'
        assert not same(tip, closed), 'no tip over the first page'
        assert same(tip, again), 'the tip did not come back on the next open'
        assert hidden(f) == 0, 'closing the tip hid it'
        # The closing tap turned no page: the same page comes back without the tip under a turn's tap.
        g = root / 'reference'
        (plain,) = run(g, OPEN, [6600], settings=dict(readerTapTipHidden=1))
        assert same(plain, closed), 'the tap that closed the tip turned the page'
        # The button: the page without the tip, and none on the next open.
        h = root / 'button'
        _, after, reopened = run(h, OPEN + f';7000:TAP:{BUTTON[0]},{BUTTON[1]};9500:' + HOME + ';12000:TAP:240,300',
                                 [6600, 9000, 15500], settings=dict(readerTapTipHidden=0))
        assert same(plain, after), 'the button left the tip or turned the page'
        assert same(plain, reopened), 'the tip came back after the button'
        assert hidden(h) == 1, 'the button did not keep the tip hidden'
    print('GREEN: X4 Pro tap tip: shown, a page tap closes it without a turn, back next open, the button hides it')


if __name__ == '__main__':
    main()
